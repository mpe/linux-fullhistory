/*
 * gfx.c: support for SGI's /dev/graphics, /dev/opengl
 *
 * Author: Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * On IRIX, /dev/graphics is [10, 146]
 *          /dev/opengl   is [10, 147]
 *
 * From a mail with Mark J. Kilgard, /dev/opengl and /dev/graphics are
 * the same thing, the use of /dev/graphics seems deprecated though.
 *
 * The reason that the original SGI programmer had to use only one
 * device for all the graphic cards on the system will remain a
 * mistery for the rest of our lives.  Why some ioctls take a board
 * number and some others not?  Mistery.  Why do they map the hardware
 * registers into the user address space with an ioctl instead of
 * mmap?  Mistery too.  Why they did not use the standard way of
 * making ioctl constants and instead sticked a random constant?
 * Mistery too.
 *
 * We implement those misterious things, and tried not to think about
 * the reasons behind them.
 */
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <asm/uaccess.h>
#include "gconsole.h"
#include "graphics.h"
#include <asm/gfx.h>
#include <asm/rrm.h>
#include <asm/page.h>
#include <asm/pgtable.h>

/* The boards */
#include "newport.h"

#ifdef PRODUCTION_DRIVER
#define enable_gconsole()
#define disable_gconsole()
#endif

static struct graphics_ops cards [MAXCARDS];
static int boards;

#define GRAPHICS_CARD(inode) 0

int
sgi_graphics_open (struct inode *inode, struct file *file)
{
	return 0;
}

int
sgi_graphics_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int board;
	unsigned int devnum = GRAPHICS_CARD (inode->i_rdev);
	int i;
	
	if ((cmd >= RRM_BASE) && (cmd <= RRM_CMD_LIMIT))
		return rrm_command (cmd-RRM_BASE, (void *) arg);
	
	switch (cmd){
	case GFX_GETNUM_BOARDS:
		return boards;

	case GFX_GETBOARD_INFO: {
		struct gfx_getboardinfo_args *bia = (void *) arg;
		void   *dest_buf;
		int    max_len;
		
		i = verify_area (VERIFY_READ, (void *) arg, sizeof (struct gfx_getboardinfo_args));
		if (i) return i;
		
		__get_user_ret (board,    &bia->board, -EFAULT);
		__get_user_ret (dest_buf, &bia->buf,   -EFAULT);
		__get_user_ret (max_len,  &bia->len,   -EFAULT);

		if (board >= boards)
			return -EINVAL;
		if (max_len < sizeof (struct gfx_getboardinfo_args))
			return -EINVAL;
		if (max_len > cards [board].g_board_info_len)
			max_len = cards [boards].g_board_info_len;
		i = verify_area (VERIFY_WRITE, dest_buf, max_len);
		if (i) return i;
		if (copy_to_user (dest_buf, cards [board].g_board_info, max_len))
			return -EFAULT;
		return max_len;
	}

	case GFX_ATTACH_BOARD: {
		struct gfx_attach_board_args *att = (void *) arg;
		void *vaddr;
		int  r;

		i = verify_area (VERIFY_READ, (void *)arg, sizeof (struct gfx_attach_board_args));
		if (i) return i;

		__get_user_ret (board, &att->board, -EFAULT);
		__get_user_ret (vaddr, &att->vaddr, -EFAULT);

		/* Ok for now we are assuming /dev/graphicsN -> head N even
		 * if the ioctl api suggests that this is not quite the case.
		 *
		 * Otherwise we fail, we use this assumption in the mmap code
		 * below to find our board information.
		 */
		if (board != devnum){
			printk ("Parameter board does not match the current board\n");
			return -EINVAL;
		}
		
		if (board >= boards)
			return -EINVAL;

		/* If it is the first opening it, then make it the board owner */
		if (!cards [board].g_owner)
			cards [board].g_owner = current;

		/*
		 * Ok, we now call mmap on this file, which will end up calling
		 * sgi_graphics_mmap
		 */
		disable_gconsole ();
		r = do_mmap (file, (unsigned long)vaddr, cards [board].g_regs_size,
			 PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE, 0);
		if (r)
			return r;

	}

	/* Strange, the real mapping seems to be done at GFX_ATTACH_BOARD,
	 * GFX_MAPALL is not even used by IRIX X server
	 */
	case GFX_MAPALL:
		return 0;

	case GFX_LABEL:
		return 0;
			
		/* Version check
		 * for my IRIX 6.2 X server, this is what the kernel returns
		 */
	case 1:
		return 3;
		
	/* Xsgi does not use this one, I assume minor is the board being queried */
	case GFX_IS_MANAGED:
		if (devnum > boards)
			return -EINVAL;
		return (cards [devnum].g_owner != 0);

	default:
		if (cards [devnum].g_ioctl)
			return (*cards [devnum].g_ioctl)(devnum, cmd, arg);
		
	}
	return -EINVAL;
}

int
sgi_graphics_close (struct inode *inode, struct file *file)
{
	int board = GRAPHICS_CARD (inode->i_rdev);
	
	/* Tell the rendering manager that one client is going away */
	rrm_close (inode, file);

	/* Was this file handle from the board owner?, clear it */
	if (current == cards [board].g_owner){
		cards [board].g_owner = 0;
		(*cards [board].g_reset_console)();
		enable_gconsole ();
	}
	return 0;
}

/* 
 * This is the core of the direct rendering engine.
 */

unsigned long
sgi_graphics_nopage (struct vm_area_struct *vma, unsigned long address, int write_access)
{
	unsigned long page;
	int board = GRAPHICS_CARD (vma->vm_dentry->d_inode->i_rdev);

#ifdef DEBUG_GRAPHICS
	printk ("Got a page fault for board %d address=%lx guser=%lx\n", board, address,
		cards [board].g_user);
#endif
	
	/* 1. figure out if another process has this mapped,
	 * and revoke the mapping in that case.
	 */
	if (cards [board].g_user && cards [board].g_user != current){
		/* FIXME: save graphics context here, dump it to rendering node? */
		remove_mapping (cards [board].g_user, vma->vm_start, vma->vm_end);
	}
	cards [board].g_user = current;
#if DEBUG_GRAPHICS
	printk ("Registers: 0x%lx\n", cards [board].g_regs);
	printk ("vm_start:  0x%lx\n", vma->vm_start);
	printk ("address:   0x%lx\n", address);
	printk ("diff:      0x%lx\n", (address - vma->vm_start));

	printk ("page/pfn:  0x%lx\n", page);
	printk ("TLB entry: %lx\n", pte_val (mk_pte (page + PAGE_OFFSET, PAGE_USERIO)));
#endif
	
	/* 2. Map this into the current process address space */
	page = ((cards [board].g_regs) + (address - vma->vm_start));
	return page + PAGE_OFFSET;
}

/*
 * We convert a GFX ioctl for mapping hardware registers, in a nice sys_mmap
 * call, which takes care of everything that must be taken care of.
 *
 */

static struct vm_operations_struct graphics_mmap = {
	NULL,			/* no special mmap-open */
	NULL,			/* no special mmap-close */
	NULL,			/* no special mmap-unmap */
	NULL,			/* no special mmap-protect */
	NULL,			/* no special mmap-sync */
	NULL,			/* no special mmap-advise */
	sgi_graphics_nopage,	/* our magic no-page fault handler */
	NULL,			/* no special mmap-wppage */
	NULL,			/* no special mmap-swapout */
	NULL			/* no special mmap-swapin */
};
	
int
sgi_graphics_mmap (struct inode *inode, struct file *file, struct vm_area_struct *vma)
{
	uint size;

	size = vma->vm_end - vma->vm_start;
	if (vma->vm_offset & ~PAGE_MASK)
		return -ENXIO;

	/* 1. Set our special graphic virtualizer  */
	vma->vm_ops = &graphics_mmap;

	/* 2. Set the special tlb permission bits */
	vma->vm_page_prot = PAGE_USERIO;
		
	/* final setup */
	vma->vm_dentry = dget (file->f_dentry);
	return 0;
}
	
/* Do any post card-detection setup on graphics_ops */
static void
graphics_ops_post_init (int slot)
{
	/* There is no owner for the card initially */
	cards [slot].g_owner = (struct task_struct *) 0;
	cards [slot].g_user  = (struct task_struct *) 0;
}

struct file_operations sgi_graphics_fops = {
	NULL,			/* llseek */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* poll */
	sgi_graphics_ioctl,	/* ioctl */
	sgi_graphics_mmap,	/* mmap */
	sgi_graphics_open,	/* open */
	NULL,			/* flush */
	sgi_graphics_close,	/* release */
	NULL,			/* fsync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
	NULL			/* lock */
};

/* /dev/graphics */
static struct miscdevice dev_graphics = {
        SGI_GRAPHICS_MINOR, "sgi-graphics", &sgi_graphics_fops
};

/* /dev/opengl */
static struct miscdevice dev_opengl = {
        SGI_OPENGL_MINOR, "sgi-opengl", &sgi_graphics_fops
};

/* This is called later from the misc-init routine */
void
gfx_register (void)
{
	misc_register (&dev_graphics);
	misc_register (&dev_opengl);
}

void 
gfx_init (const char **name)
{
	struct console_ops *console;
	struct graphics_ops *g;

	printk ("GFX INIT: ");
	shmiq_init ();
	usema_init ();
	
	if ((g = newport_probe (boards, name)) != 0){
		cards [boards] = *g;
		graphics_ops_post_init (boards);
		boards++;
		console = 0;
	}
	/* Add more graphic drivers here */
	/* Keep passing console around */
	
	if (boards > MAXCARDS){
		printk ("Too many cards found on the system\n");
		prom_halt ();
	}
}


