/* $Id: sunfb.c,v 1.18 1996/11/22 11:57:07 ecd Exp $
 * sunfb.c: Sun generic frame buffer support.
 *
 * Copyright (C) 1995, 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * 
 * Added getcmap ioctl, may, 96
 * Support for multiple fbs, sep, 96
 */

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/types.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/oplib.h>
#include <asm/sbus.h>
#include <asm/fbio.h>
#include <asm/io.h>
#include <asm/pgtsun4c.h>	/* for the sun4c_nocache */

#include "../../char/kbd_kern.h"
#include "../../char/vt_kern.h"
#include "../../char/consolemap.h"
#include "../../char/selection.h"
#include "../../char/console_struct.h"

#include "fb.h"

extern void set_other_palette (int);
extern void sun_clear_fb(int);
extern void set_cursor (int);

#define FB_SETUP(err) \
	int minor = FB_DEV (inode->i_rdev); \
\
	if (minor >= fbinfos || \
	    fbinfo [minor].type.fb_type == FBTYPE_NOTYPE) \
		return -(err);

static int
fb_open (struct inode * inode, struct file * file)
{
	FB_SETUP(EBADF)
	if (fbinfo [minor].open)
		return -EBUSY;
	fbinfo [minor].mmaped = 0;
	fbinfo [minor].open = 1;
	return 0;
}

static int
fb_ioctl (struct inode *inode, struct file *file, uint cmd, unsigned long arg)
{
	fbinfo_t *fb;
	struct fbcmap *cmap;
	int i;
	FB_SETUP(EBADF)
	
	fb = &fbinfo [minor];
	
	switch (cmd){
	case FBIOGTYPE:		/* return frame buffer type */
		copy_to_user_ret((struct fbtype *)arg, &fb->type, sizeof(struct fbtype), -EFAULT);
		break;
	case FBIOGATTR:{
		struct fbgattr *fba = (struct fbgattr *) arg;
		
		i = verify_area (VERIFY_WRITE, (void *) arg, sizeof (struct fbgattr));
		if (i) return i;
		__put_user_ret(fb->real_type, &fba->real_type, -EFAULT);
		__put_user_ret(0, &fba->owner, -EFAULT);
		__copy_to_user_ret(&fba->fbtype, &fb->type,
				   sizeof(struct fbtype), -EFAULT);
		__put_user_ret(0, &fba->sattr.flags, -EFAULT);
		__put_user_ret(fb->type.fb_type, &fba->sattr.emu_type, -EFAULT);
		__put_user_ret(-1, &fba->sattr.dev_specific[0], -EFAULT);
		__put_user_ret(fb->type.fb_type, &fba->emu_types[0], -EFAULT);
		for (i = 1; i < 4; i++)
			put_user_ret(fb->emulations[i], &fba->emu_types[i], -EFAULT);
		break;
	}
	case FBIOSATTR:
		i = verify_area (VERIFY_READ, (void *) arg, sizeof (struct fbsattr));
		if (i) return i;
		return -EINVAL;
	case FBIOSVIDEO:
 		if (fb == fbinfo && vt_cons[fg_console]->vc_mode == KD_TEXT)
 			break;
		get_user_ret(i, (int *)arg, -EFAULT);
		if (i){
			if (!fb->blanked || !fb->unblank)
				break;
			if (!minor || (fb->open && fb->mmaped))
				(*fb->unblank)(fb);
			fb->blanked = 0;
		} else {
			if (fb->blanked || !fb->blank)
				break;
			(*fb->blank)(fb);
			fb->blanked = 1;
		}
		break;
	case FBIOGVIDEO:
		put_user_ret(fb->blanked, (int *) arg, -EFAULT);
		break;
	case FBIOGETCMAP: {
		char *rp, *gp, *bp;
		int end, count, index;

		if (!fb->loadcmap)
			return -EINVAL;
		i = verify_area (VERIFY_READ, (void *) arg, sizeof (struct fbcmap));
		if (i) return i;
		cmap = (struct fbcmap *) arg;
		__get_user_ret(count, &cmap->count, -EFAULT);
		__get_user_ret(index, &cmap->index, -EFAULT);
		if ((index < 0) || (index > 255))
			return -EINVAL;
		if (index + count > 256)
			count = 256 - cmap->index;
		__get_user_ret(rp, &cmap->red, -EFAULT);
		__get_user_ret(gp, &cmap->green, -EFAULT);
		__get_user_ret(bp, &cmap->blue, -EFAULT);
		if(verify_area (VERIFY_WRITE, rp, count))  return -EFAULT;
		if(verify_area (VERIFY_WRITE, gp, count))  return -EFAULT;
		if(verify_area (VERIFY_WRITE, bp, count))  return -EFAULT;
		end = index + count;
		for (i = index; i < end; i++){
			__put_user_ret(fb->color_map CM(i,0), rp, -EFAULT);
			__put_user_ret(fb->color_map CM(i,1), gp, -EFAULT);
			__put_user_ret(fb->color_map CM(i,2), bp, -EFAULT);
			rp++; gp++; bp++;
		}
		(*fb->loadcmap)(fb, cmap->index, count);
                break;			

	}
	case FBIOPUTCMAP: {	/* load color map entries */
		char *rp, *gp, *bp;
		int end, count, index;
		
		if (!fb->loadcmap)
			return -EINVAL;
		i = verify_area (VERIFY_READ, (void *) arg, sizeof (struct fbcmap));
		if (i) return i;
		cmap = (struct fbcmap *) arg;
		__get_user_ret(count, &cmap->count, -EFAULT);
		__get_user_ret(index, &cmap->index, -EFAULT);
		if ((index < 0) || (index > 255))
			return -EINVAL;
		if (index + count > 256)
			count = 256 - cmap->index;
		__get_user_ret(rp, &cmap->red, -EFAULT);
		__get_user_ret(gp, &cmap->green, -EFAULT);
		__get_user_ret(bp, &cmap->blue, -EFAULT);
		if(verify_area (VERIFY_READ, rp, cmap->count)) return -EFAULT;
		if(verify_area (VERIFY_READ, gp, cmap->count)) return -EFAULT;
		if(verify_area (VERIFY_READ, bp, cmap->count)) return -EFAULT;

		end = index + count;
		for (i = index; i < end; i++){
			__get_user_ret(fb->color_map CM(i,0), rp, -EFAULT);
			__get_user_ret(fb->color_map CM(i,1), gp, -EFAULT);
			__get_user_ret(fb->color_map CM(i,2), bp, -EFAULT);
			rp++; gp++; bp++;
		}
		(*fb->loadcmap)(fb, cmap->index, count);
                break;			
	}

	case FBIOGCURMAX: {
		struct fbcurpos *p = (struct fbcurpos *) arg;
		if (!fb->setcursor) return -EINVAL;
		if(verify_area (VERIFY_WRITE, p, sizeof (struct fbcurpos)))
			return -EFAULT;
		__put_user_ret(fb->cursor.hwsize.fbx, &p->fbx, -EFAULT);
		__put_user_ret(fb->cursor.hwsize.fby, &p->fby, -EFAULT);
		break;
	}
	case FBIOSCURSOR:
		if (!fb->setcursor) return -EINVAL;
 		if (fb == fbinfo) {
 			if (vt_cons[fg_console]->vc_mode == KD_TEXT)
 				return -EINVAL; /* Don't let graphics programs hide our nice text cursor */
 			sun_hw_cursor_shown = 0; /* Forget state of our text cursor */
		}
		return sun_hw_scursor ((struct fbcursor *) arg, fb);

	case FBIOSCURPOS:
		if (!fb->setcursor) return -EINVAL;
                /* Don't let graphics programs move our nice text cursor */
 		if (fb == fbinfo) {
 			if (vt_cons[fg_console]->vc_mode == KD_TEXT)
 				return -EINVAL; /* Don't let graphics programs move our nice text cursor */
 		}
		i= verify_area (VERIFY_READ, (void *) arg, sizeof (struct fbcurpos));
		if (i) return i;
		fb->cursor.cpos = *(struct fbcurpos *)arg;
		(*fb->setcursor) (fb);
		break;
		
	default:
		if (fb->ioctl){
			i = fb->ioctl (inode, file, cmd, arg, fb);
			if (i == -ENOSYS) {
				printk ("[[FBIO: %8.8x]]\n", cmd);
				return -EINVAL;
			}
			return i;
		}
		printk ("[[FBIO: %8.8x]]\n", cmd);
		return -EINVAL;
	}
	return 0;
}

static int
fb_close (struct inode * inode, struct file *filp)
{
	fbinfo_t *fb;
	struct fbcursor cursor;
	FB_SETUP(EBADF)	

	fb = &fbinfo[minor];
	
	if (!minor)
		vt_cons [fb->vtconsole]->vc_mode = KD_TEXT;

	/* Leaving graphics mode, turn off the cursor */
	if (fb->mmaped)
		sun_clear_fb (minor);
	cursor.set    = FB_CUR_SETCUR;
	cursor.enable = 0;
	
	/* Reset the driver */
	if (fb->reset)
		fb->reset(fb);

	if (fb->open)
		fb->open = 0;
	fb_ioctl (inode, filp, FBIOSCURPOS, (unsigned long) &cursor);
	set_other_palette (minor);
	if (!minor) {
		render_screen ();
		set_cursor (fg_console);
	} else if (fb->blank)
		(*fb->blank)(fb);
	return 0;
}

static int
fb_mmap (struct inode *inode, struct file *file, struct vm_area_struct *vma)
{
	fbinfo_t *fb;
	FB_SETUP(ENXIO)

	fb = &fbinfo [minor];

	if (fb->mmap){
		int v;
		
		v = (*fb->mmap)(inode, file, vma, fb->base, fb);
		if (v) return v;
		if (!fb->mmaped) {
			fb->mmaped = 1;
			if (!minor) {
				fb->vtconsole = fg_console;
				vt_cons [fg_console]->vc_mode = KD_GRAPHICS;
			} else {
				if (fb->unblank && !fb->blanked)
					(*fb->unblank)(fb);
			}
		}
		return 0;
	} else
		return -ENXIO;
}

static struct file_operations graphdev_fops =
{
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	fb_ioctl,
	fb_mmap,
	fb_open,		/* open */
	(void(*)(struct inode *, struct file *))
	fb_close,		/* close */
};

__initfunc(int fb_init (void))
{
	/* Register the frame buffer device */
	if (register_chrdev (GRAPHDEV_MAJOR, "graphics", &graphdev_fops)){
		printk ("Could not register graphics device\n");
		return -EIO;
	}
	return 0; /* success */
}    
