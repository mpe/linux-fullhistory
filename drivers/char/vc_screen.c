/*
 * linux/drivers/char/vc_screen.c
 *
 * Provide access to virtual console memory.
 * /dev/vcs0: the screen as it is being viewed right now (possibly scrolled)
 * /dev/vcsN: the screen of /dev/ttyN (1 <= N <= 63)
 *            [minor: N]
 *
 * /dev/vcsaN: idem, but including attributes, and prefixed with
 *	the 4 bytes lines,columns,x,y (as screendump used to give)
 *            [minor: N+128]
 *
 * This replaces screendump and part of selection, so that the system
 * administrator can control access using file system permissions.
 *
 * aeb@cwi.nl - efter Friedas begravelse - 950211
 *
 * machek@k332.feld.cvut.cz - modified not to send characters to wrong console
 *	 - fixed some fatal of-by-one bugs (0-- no longer == -1 -> looping and looping and looping...)
 *	 - making it working with multiple monitor patches
 *	 - making it shorter - scr_readw are macros which expand in PRETTY long code
 */

#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include "vt_kern.h"
#include "selection.h"

#undef attr
#undef org
#undef addr
#define HEADER_SIZE	4

static unsigned short
func_scr_readw(unsigned short *org)
{
return scr_readw( org );
}

static void
func_scr_writew(unsigned short val, unsigned short *org)
{
scr_writew( val, org );
}

static int
vcs_size(struct inode *inode)
{
	int size;
#ifdef CONFIG_MULTIMON
   	int currcons = MINOR(inode->i_rdev) & 127;
	/* Multimon patch	*/
	if (!vc_cons[currcons].d) return 0;
#endif
   	size= video_num_lines * video_num_columns;
	if (MINOR(inode->i_rdev) & 128)
		size = 2*size + HEADER_SIZE;
	return size;
}

static long long
vcs_lseek(struct inode *inode, struct file *file, long long offset, int orig)
{
	int size;
	size = vcs_size(inode);

	switch (orig) {
		case 0:
			file->f_pos = offset;
			break;
		case 1:
			file->f_pos += offset;
			break;
		case 2:
			file->f_pos = size + offset;
			break;
		default:
			return -EINVAL;
	}
	if (file->f_pos < 0 || file->f_pos > size)
		{ file->f_pos = 0; return -EINVAL; }
	return file->f_pos;
}

#define RETURN( x ) { enable_bh( CONSOLE_BH ); return x; }
static long
vcs_read(struct inode *inode, struct file *file, char *buf, unsigned long count)
{
	int p = file->f_pos;
	unsigned int currcons = MINOR(inode->i_rdev);
	int viewed, attr, size, read;
	char *buf0;
	unsigned short *org;

	attr = (currcons & 128);
	currcons = (currcons & 127);
	disable_bh( CONSOLE_BH );
	if (currcons == 0) {
		currcons = fg_console;
		viewed = 1;
	} else {
		currcons--;
		viewed = 0;
	}
	if (!vc_cons_allocated(currcons))
		RETURN( -ENXIO );

	size = vcs_size(inode);
	if (p < 0 || p > size)
		RETURN( -EINVAL );
	if (count > size - p)
		count = size - p;

	buf0 = buf;
	if (!attr) {
		org = screen_pos(currcons, p, viewed);
		while (count-- > 0)
			put_user(func_scr_readw(org++) & 0xff, buf++);
	} else {
		if (p < HEADER_SIZE) {
			char header[HEADER_SIZE];
			header[0] = (char) video_num_lines;
			header[1] = (char) video_num_columns;
			getconsxy(currcons, header+2);
			while (p < HEADER_SIZE && count > 0)
			    { count--; put_user(header[p++], buf++); }
		}
		if (count > 0) {
		    p -= HEADER_SIZE;
		    org = screen_pos(currcons, p/2, viewed);
		    if ((p & 1) && count > 0)
			    { count--; put_user(func_scr_readw(org++) >> 8, buf++); }
		}
		while (count > 1) {
			put_user(func_scr_readw(org++), (unsigned short *) buf);
			buf += 2;
			count -= 2;
		}
		if (count > 0)
			put_user(func_scr_readw(org) & 0xff, buf++);
	}
	read = buf - buf0;
	file->f_pos += read;
	RETURN( read );
}

static long
vcs_write(struct inode *inode, struct file *file, const char *buf, unsigned long count)
{
	int p = file->f_pos;
	unsigned int currcons = MINOR(inode->i_rdev);
	int viewed, attr, size, written;
	const char *buf0;
	unsigned short *org;

	attr = (currcons & 128);
	currcons = (currcons & 127);
	disable_bh( CONSOLE_BH );
	if (currcons == 0) {
		currcons = fg_console;
		viewed = 1;
	} else {
		currcons--;
		viewed = 0;
	}
	if (!vc_cons_allocated(currcons))
		RETURN( -ENXIO );

	size = vcs_size(inode);
	if (p < 0 || p > size)
		RETURN( -EINVAL );
	if (count > size - p)
		count = size - p;

	buf0 = buf;
	if (!attr) {
		org = screen_pos(currcons, p, viewed);
		while (count > 0) {
			unsigned char c;
			count--;
			get_user(c, (const unsigned char*)buf++);
			func_scr_writew((func_scr_readw(org) & 0xff00) | c, org);
			org++;
		}
	} else {
		if (p < HEADER_SIZE) {
			char header[HEADER_SIZE];
			getconsxy(currcons, header+2);
			while (p < HEADER_SIZE && count > 0)
				{ count--; get_user(header[p++], buf++); }
			if (!viewed)
				putconsxy(currcons, header+2);
		}
		if (count > 0) {
		    p -= HEADER_SIZE;
			org = screen_pos(currcons, p/2, viewed);
			if ((p & 1) && count > 0) {
			    char c;
				count--;
				get_user(c,buf++);
				func_scr_writew((c << 8) |
				     (func_scr_readw(org) & 0xff), org);
				org++;
			}
		}
		while (count > 1) {
			unsigned short w;
			get_user(w, (const unsigned short *) buf);
			func_scr_writew(w, org++);
			buf += 2;
			count -= 2;
		}
		if (count > 0) {
			unsigned char c;
			get_user(c, (const unsigned char*)buf++);
			func_scr_writew((func_scr_readw(org) & 0xff00) | c, org);
		}
	}
	written = buf - buf0;
	file->f_pos += written;
	RETURN( written );
}

static int
vcs_open(struct inode *inode, struct file *filp)
{
	unsigned int currcons = (MINOR(inode->i_rdev) & 127);
	if(currcons && !vc_cons_allocated(currcons-1))
		return -ENXIO;
	return 0;
}

static struct file_operations vcs_fops = {
	vcs_lseek,	/* lseek */
	vcs_read,	/* read */
	vcs_write,	/* write */
	NULL,		/* readdir */
	NULL,		/* select */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	vcs_open,	/* open */
	NULL,		/* release */
	NULL		/* fsync */
};

int vcs_init(void)
{
	int error;

	error = register_chrdev(VCS_MAJOR, "vcs", &vcs_fops);
	if (error)
		printk("unable to get major %d for vcs device", VCS_MAJOR);
	return error;
}
