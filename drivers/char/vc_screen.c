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
 */

#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include "vt_kern.h"
#include "selection.h"

#define HEADER_SIZE	4

static inline int
vcs_size(struct inode *inode)
{
	int size = video_num_lines * video_num_columns;
	if (MINOR(inode->i_rdev) & 128)
		size = 2*size + HEADER_SIZE;
	return size;
}

static int
vcs_lseek(struct inode *inode, struct file *file, off_t offset, int orig)
{
	int size = vcs_size(inode);

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
		return -EINVAL;
	return file->f_pos;
}

static int
vcs_read(struct inode *inode, struct file *file, char *buf, int count)
{
	unsigned long p = file->f_pos;
	unsigned int cons = MINOR(inode->i_rdev);
	int viewed, attr, size, read;
	char *buf0;
	unsigned short *org;

	attr = (cons & 128);
	cons = (cons & 127);
	if (cons == 0) {
		cons = fg_console;
		viewed = 1;
	} else {
		cons--;
		viewed = 0;
	}
	if (!vc_cons_allocated(cons))
		return -ENXIO;

	size = vcs_size(inode);
	if (count < 0 || p > size)
		return -EINVAL;
	if (count > size - p)
		count = size - p;

	buf0 = buf;
	if (!attr) {
		org = screen_pos(cons, p, viewed);
		while (count-- > 0)
			put_fs_byte(scr_readw(org++) & 0xff, buf++);
	} else {
		if (p < HEADER_SIZE) {
			char header[HEADER_SIZE];
			header[0] = (char) video_num_lines;
			header[1] = (char) video_num_columns;
			getconsxy(cons, header+2);
			while (p < HEADER_SIZE && count-- > 0)
			    put_fs_byte(header[p++], buf++);
		}
		p -= HEADER_SIZE;
		org = screen_pos(cons, p/2, viewed);
		if ((p & 1) && count-- > 0)
			put_fs_byte(scr_readw(org++) >> 8, buf++);
		while (count > 1) {
			put_fs_word(scr_readw(org++), buf);
			buf += 2;
			count -= 2;
		}
		if (count > 0)
			put_fs_byte(scr_readw(org) & 0xff, buf++);
	}
	read = buf - buf0;
	file->f_pos += read;
	return read;
}

static int
vcs_write(struct inode *inode, struct file *file, char *buf, int count)
{
	unsigned long p = file->f_pos;
	unsigned int cons = MINOR(inode->i_rdev);
	int viewed, attr, size, written;
	char *buf0;
	unsigned short *org;

	attr = (cons & 128);
	cons = (cons & 127);
	if (cons == 0) {
		cons = fg_console;
		viewed = 1;
	} else {
		cons--;
		viewed = 0;
	}
	if (!vc_cons_allocated(cons))
		return -ENXIO;

	size = vcs_size(inode);
	if (count < 0 || p > size)
		return -EINVAL;
	if (count > size - p)
		count = size - p;

	buf0 = buf;
	if (!attr) {
		org = screen_pos(cons, p, viewed);
		while (count-- > 0) {
			scr_writew((scr_readw(org) & 0xff00) |
				get_fs_byte(buf++), org);
			org++;
		}
	} else {
		if (p < HEADER_SIZE) {
			char header[HEADER_SIZE];
			getconsxy(cons, header+2);
			while (p < HEADER_SIZE && count-- > 0)
				header[p++] = get_fs_byte(buf++);
			if (!viewed)
				putconsxy(cons, header+2);
		}
		p -= HEADER_SIZE;
		org = screen_pos(cons, p/2, viewed);
		if ((p & 1) && count-- > 0) {
			scr_writew((get_fs_byte(buf++) << 8) |
				   (scr_readw(org) & 0xff), org);
			org++;
		}
		while (count > 1) {
			scr_writew(get_fs_word(buf), org++);
			buf += 2;
			count -= 2;
		}
		if (count > 0)
			scr_writew((scr_readw(org) & 0xff00) |
				   get_fs_byte(buf++), org);
	}
	written = buf - buf0;
	file->f_pos += written;
	return written;
}

static int
vcs_open(struct inode *inode, struct file *filp)
{
	unsigned int cons = (MINOR(inode->i_rdev) & 127);
	if(cons && !vc_cons_allocated(cons-1))
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

long vcs_init(long kmem_start)
{
	if (register_chrdev(VCS_MAJOR, "vcs", &vcs_fops))
		printk("unable to get major %d for vcs device", VCS_MAJOR);
	return kmem_start;
}
