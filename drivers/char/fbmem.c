/*
 *  linux/drivers/char/fbmem.c
 *
 *  Copyright (C) 1994 Martin Schaller
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/tty.h>

#include <asm/segment.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/fb.h>

#define FB_MAJOR	29

#define FB_MODES_SHIFT    5	/* 32 modes per framebuffer */
#define FB_NUM_MINORS 	256	/* 256 Minors		    */
#define FB_MAX		(FB_NUM_MINORS / (1 << FB_MODES_SHIFT))

#define GET_INODE(i) MKDEV(FB_MAJOR, (i) << FB_MODES_SHIFT)
#define GET_FB_IDX(node) (MINOR(node) >> FB_MODES_SHIFT)
#define GET_FB_VAR_IDX(node) (MINOR(node) & ((1 << FB_MODES_SHIFT)-1)) 

struct fb_ops *registered_fb[FB_MAX];
struct fb_var_screeninfo *registered_fb_var[FB_MAX];
int registered_fb_var_num[FB_MAX];
int fb_curr_open[FB_MAX];
int fb_open_count[FB_MAX];

static inline int PROC_CONSOLE(void)
{
	if (!current->tty)
		return fg_console;

	if (current->tty->driver.type != TTY_DRIVER_TYPE_CONSOLE)
		/* XXX Should report error here? */
		return fg_console;

	if (MINOR(current->tty->device) < 1)
		return fg_console;

	return MINOR(current->tty->device) - 1;
}

static int 
fb_read(struct inode *inode, struct file *file, char *buf, int count)
{
	unsigned long p = file->f_pos;
	struct fb_ops *fb = registered_fb[GET_FB_IDX(inode->i_rdev)];
	struct fb_fix_screeninfo fix;
	char *base_addr;
	int copy_size;

	if (! fb)
		return -ENODEV;
	if (count < 0)
		return -EINVAL;

	fb->fb_get_fix(&fix,PROC_CONSOLE());
	base_addr=(char *) fix.smem_start;
	copy_size=(count + p <= fix.smem_len ? count : fix.smem_len - p);
	memcpy_tofs(buf, base_addr+p, copy_size);
	file->f_pos += copy_size;
	return copy_size;
}

static int 
fb_write(struct inode *inode, struct file *file, const char *buf, int count)
{
	unsigned long p = file->f_pos;
	struct fb_ops *fb = registered_fb[GET_FB_IDX(inode->i_rdev)];
	struct fb_fix_screeninfo fix;
	char *base_addr;
	int copy_size;

	if (! fb)
		return -ENODEV;
	if (count < 0)
		return -EINVAL;
	fb->fb_get_fix(&fix, PROC_CONSOLE());
	base_addr=(char *) fix.smem_start;
	copy_size=(count + p <= fix.smem_len ? count : fix.smem_len - p);
	memcpy_fromfs(base_addr+p, buf, copy_size); 
	file->f_pos += copy_size;
	return copy_size;
}


static int 
fb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	 unsigned long arg)
{
	struct fb_ops *fb = registered_fb[GET_FB_IDX(inode->i_rdev)];
	struct fb_cmap cmap;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;

	int i,fbidx,vidx;
	
	if (! fb)
		return -ENODEV;
	switch (cmd) {
	case FBIOGET_VSCREENINFO:
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(struct fb_var_screeninfo));
		if (i) return i;
		fbidx=GET_FB_IDX(inode->i_rdev);
		vidx=GET_FB_VAR_IDX(inode->i_rdev);
		if (! vidx) /* ask device driver for current */
			i=fb->fb_get_var(&var, PROC_CONSOLE());
		else
			var=registered_fb_var[fbidx][vidx-1];
		memcpy_tofs((void *) arg, &var, sizeof(var));
		return i;
	case FBIOPUT_VSCREENINFO:
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(struct fb_var_screeninfo));
		if (i) return i;
		memcpy_fromfs(&var, (void *) arg, sizeof(var));
		i=fb->fb_set_var(&var, PROC_CONSOLE());
		memcpy_tofs((void *) arg, &var, sizeof(var));
		fbidx=GET_FB_IDX(inode->i_rdev);
		vidx=GET_FB_VAR_IDX(inode->i_rdev);
		if (! i && vidx)
			registered_fb_var[fbidx][vidx-1]=var;
		return i;
	case FBIOGET_FSCREENINFO:
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(struct fb_fix_screeninfo));
		if (i)	return i;
		i=fb->fb_get_fix(&fix, PROC_CONSOLE());
		memcpy_tofs((void *) arg, &fix, sizeof(fix));
		return i;
	case FBIOPUTCMAP:
		i = verify_area(VERIFY_READ, (void *) arg,
				sizeof(struct fb_cmap));
		if (i) return i;
		memcpy_fromfs(&cmap, (void *) arg, sizeof(cmap));
		i = verify_area(VERIFY_READ, (void *) cmap.red, 
				cmap.len * sizeof(unsigned short));
		if (i) return i;
		i = verify_area(VERIFY_READ, (void *) cmap.green, 
				cmap.len * sizeof(unsigned short));
		if (i) return i;
		i = verify_area(VERIFY_READ, (void *) cmap.blue, 
				cmap.len * sizeof(unsigned short));
		if (i) return i;
		if (cmap.transp) {
			i = verify_area(VERIFY_READ, (void *) cmap.transp, 
					cmap.len * sizeof(unsigned short));
			if (i) return i;
		}
		return (fb->fb_set_cmap(&cmap, 0, PROC_CONSOLE()));
	case FBIOGETCMAP:
		i = verify_area(VERIFY_READ, (void *) arg,
				sizeof(struct fb_cmap));
		if (i)	return i;
		memcpy_fromfs(&cmap, (void *) arg, sizeof(cmap));
		i = verify_area(VERIFY_WRITE, (void *) cmap.red, 
				cmap.len * sizeof(unsigned short));
		if (i) return i;
		i = verify_area(VERIFY_WRITE, (void *) cmap.green, 
				cmap.len * sizeof(unsigned short));
		if (i) return i;
		i = verify_area(VERIFY_WRITE, (void *) cmap.blue, 
				cmap.len * sizeof(unsigned short));
		if (i) return i;
		if (cmap.transp) {
			i = verify_area(VERIFY_WRITE, (void *) cmap.transp, 
					cmap.len * sizeof(unsigned short));
			if (i) return i;
		}
		return (fb->fb_get_cmap(&cmap, 0, PROC_CONSOLE()));
	case FBIOPAN_DISPLAY:
		i = verify_area(VERIFY_WRITE, (void *) arg, 
				sizeof(struct fb_var_screeninfo));
		if (i) return i;
		memcpy_fromfs(&var, (void *) arg, sizeof(var));
		i=fb->fb_pan_display(&var, PROC_CONSOLE());
		memcpy_tofs((void *) arg, &var, sizeof(var));
		fbidx=GET_FB_IDX(inode->i_rdev);
		vidx=GET_FB_VAR_IDX(inode->i_rdev);
		if (! i && vidx)
			registered_fb_var[fbidx][vidx-1]=var;
		return i;
	default:
		return (fb->fb_ioctl(inode, file, cmd, arg, PROC_CONSOLE()));
	}
}

static int 
fb_mmap(struct inode *inode, struct file *file, struct vm_area_struct * vma)
{
	struct fb_ops *fb = registered_fb[GET_FB_IDX(inode->i_rdev)];
	struct fb_fix_screeninfo fix;

	if (! fb)
		return -ENODEV;
	fb->fb_get_fix(&fix, PROC_CONSOLE());
	if ((vma->vm_end - vma->vm_start + vma->vm_offset) > fix.smem_len)
		return -EINVAL;
	vma->vm_offset += fix.smem_start;
	if (vma->vm_offset & ~PAGE_MASK)
		return -ENXIO;
	if (m68k_is040or060) {
		pgprot_val(vma->vm_page_prot) &= _CACHEMASK040;
		/* Use write-through cache mode */
		pgprot_val(vma->vm_page_prot) |= _PAGE_CACHE040W;
	}
	if (remap_page_range(vma->vm_start, vma->vm_offset,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	vma->vm_inode = inode;
	inode->i_count++;
	return 0;
}

static int
fb_open(struct inode *inode, struct file *file)
{
	int fbidx=GET_FB_IDX(inode->i_rdev);
	int vidx=GET_FB_VAR_IDX(inode->i_rdev);
	struct fb_ops *fb = registered_fb[fbidx];
	int err;
	
	if (! vidx)		/* fb?current always succeeds */ 
		return 0;
	if (vidx > registered_fb_var_num[fbidx])
		return -EINVAL;
	if (fb_curr_open[fbidx] && fb_curr_open[fbidx] != vidx)
		return -EBUSY;
 	if (file->f_mode & 2) /* only set parameters if opened writeable */
		if ((err=fb->fb_set_var(registered_fb_var[fbidx] + vidx-1, PROC_CONSOLE())))
			return err;
	fb_curr_open[fbidx] = vidx;
	fb_open_count[fbidx]++;
	return 0;
}

static void 
fb_release(struct inode *inode, struct file *file)
{
	int fbidx=GET_FB_IDX(inode->i_rdev);
	int vidx=GET_FB_VAR_IDX(inode->i_rdev);
	if (! vidx)
		return;
	if (! (--fb_open_count[fbidx]))
		fb_curr_open[fbidx]=0;
}

static struct file_operations fb_fops = {
	NULL,		/* lseek	*/
	fb_read,	/* read		*/
	fb_write,	/* write	*/
	NULL,		/* readdir 	*/
	NULL,		/* select 	*/
	fb_ioctl,	/* ioctl 	*/
	fb_mmap,	/* mmap		*/
	fb_open,	/* open 	*/
	fb_release,	/* release 	*/
	NULL		/* fsync 	*/
};

int
register_framebuffer(char *id, int *node, struct fb_ops *fbops, int fbvar_num, 
		     struct fb_var_screeninfo *fbvar)
{
	int i;
	for (i = 0 ; i < FB_MAX; i++)
		if (! registered_fb[i])
			break;
	if (i == FB_MAX)
		return -ENXIO;
	registered_fb[i]=fbops;
	registered_fb_var[i]=fbvar;
	registered_fb_var_num[i]=fbvar_num;
	*node=GET_INODE(i);
	return 0;
}

int
unregister_framebuffer(int node)
{
	int i=GET_FB_IDX(node);
	if (! registered_fb[i])
		return -EINVAL; 
	registered_fb[i]=NULL;
	registered_fb_var[i]=NULL;
	return 0;
}

void
fbmem_init(void)
{
	if (register_chrdev(FB_MAJOR,"fb",&fb_fops))
		printk("unable to get major %d for fb devs\n", FB_MAJOR);
}

