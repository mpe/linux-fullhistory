/*
 *      Copyright (C) 1993-1995 Bas Laarhoven.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 *      This file contains the code that interfaces the kernel
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/ftape.h>
#include <asm/dma.h>

#include "tracing.h"
#include "kernel-interface.h"
#include "ftape-read.h"
#include "ftape-write.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "fdc-io.h"


/*      Global vars.
 */

/* Allocating a 96Kb DMAable buffer in one chunk won't work due to
 * memory fragmentation.  To avoid this, it is broken up into
 * NR_BUFFERS chunks of 32Kbyte. --khp
 */

byte *tape_buffer[NR_BUFFERS] = {NULL};

/*      Local vars.
 */
static int busy_flag = 0;
static int old_sigmask;

static int ftape_open(struct inode *ino, struct file *filep);
static void ftape_close(struct inode *ino, struct file *filep);
static int ftape_ioctl(struct inode *ino, struct file *filep,
		       unsigned int command, unsigned long arg);
static int ftape_read(struct inode *ino, struct file *fp, char *buff,
		      int req_len);
static int ftape_write(struct inode *ino, struct file *fp, const char *buff,
		       int req_len);

static struct file_operations ftape_cdev =
{
	NULL,			/* lseek */
	ftape_read,		/* read */
	ftape_write,		/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	ftape_ioctl,		/* ioctl */
	NULL,			/* mmap */
	ftape_open,		/* open */
	ftape_close,		/* release */
	NULL,			/* fsync */
};

/*
 * DMA'able memory allocation stuff.
 */

/* Pure 2^n version of get_order */
static inline int __get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

static inline
void *dmaalloc(int order)
{
	return (void *) __get_dma_pages(GFP_KERNEL, order);
}

static inline
void dmafree(void *addr, int order)
{
	free_pages((unsigned long) addr, order);
}

/*
 * Called by modules package when installing the driver
 * or by kernel during the initialization phase
 */

#ifdef MODULE
#define ftape_init init_module
#endif

int ftape_init(void)
{
	int n;
	int order;
	TRACE_FUN(5, "ftape_init");
#ifdef MODULE
	printk(KERN_INFO "ftape-2.08 960314\n"
	       KERN_INFO " (c) 1993-1995 Bas Laarhoven (bas@vimec.nl)\n"
	       KERN_INFO " (c) 1995-1996 Kai Harrekilde-Petersen (khp@dolphinics.no)\n"
	KERN_INFO " QIC-117 driver for QIC-40/80/3010/3020 tape drives\n"
	       KERN_INFO " Compiled for kernel version %s"
#ifdef MODVERSIONS
	       " with versioned symbols"
#endif
	       "\n", kernel_version);
#else /* !MODULE */
	/* print a short no-nonsense boot message */
	printk("ftape-2.08 960314 for Linux 1.3.70\n");
#endif				/* MODULE */
	TRACE(3, "installing QIC-117 ftape driver...");
	if (register_chrdev(QIC117_TAPE_MAJOR, "ft", &ftape_cdev)) {
		TRACE(1, "register_chrdev failed");
		TRACE_EXIT;
		return -EIO;
	}
	TRACEx1(3, "ftape_init @ 0x%p", ftape_init);
	/*
	 * Allocate the DMA buffers. They are deallocated at cleanup() time.
	 */
	order = __get_order(BUFF_SIZE);
	for (n = 0; n < NR_BUFFERS; n++) {
		tape_buffer[n] = (byte *) dmaalloc(order);
		if (!tape_buffer[n]) {
			TRACE(1, "dmaalloc() failed");
			for (n = 0; n < NR_BUFFERS; n++) {
				if (tape_buffer[n]) {
					dmafree(tape_buffer[n], order);
					tape_buffer[n] = NULL;
				}
			}
			current->blocked = old_sigmask;		/* restore mask */
			if (unregister_chrdev(QIC117_TAPE_MAJOR, "ft") != 0) {
				TRACE(3, "unregister_chrdev failed");
			}
			TRACE_EXIT;
			return -ENOMEM;
		} else {
			TRACEx2(3, "dma-buffer #%d @ %p", n, tape_buffer[n]);
		}
	}
	busy_flag = 0;
	ftape_unit = -1;
	ftape_failure = 1;	/* inhibit any operation but open */
	udelay_calibrate();	/* must be before fdc_wait_calibrate ! */
	fdc_wait_calibrate();
	TRACE_EXIT;
#ifdef MODULE
	register_symtab(0);	/* remove global ftape symbols */
#endif
	return 0;
}


#ifdef MODULE
/*      Called by modules package when removing the driver
 */
void cleanup_module(void)
{
	int n;
	int order;
	TRACE_FUN(5, "cleanup_module");

	if (unregister_chrdev(QIC117_TAPE_MAJOR, "ft") != 0) {
		TRACE(3, "failed");
	} else {
		TRACE(3, "successful");
	}
	order = __get_order(BUFF_SIZE);
	for (n = 0; n < NR_BUFFERS; n++) {
		if (tape_buffer[n]) {
			dmafree(tape_buffer[n], order);
			tape_buffer[n] = NULL;
			TRACEx1(3, "removed dma-buffer #%d", n);
		} else {
			TRACEx1(1, "dma-buffer #%d == NULL (bug?)", n);
		}
	}
	TRACE_EXIT;
}
#endif				/* MODULE */

/*      Open ftape device
 */
static int ftape_open(struct inode *ino, struct file *filep)
{
	TRACE_FUN(4, "ftape_open");
	int result;
	MOD_INC_USE_COUNT;	/* lock module in memory */

	TRACEi(5, "called for minor", MINOR(ino->i_rdev));
	if (busy_flag) {
		TRACE(1, "failed: already busy");
		MOD_DEC_USE_COUNT;	/* unlock module in memory */
		TRACE_EXIT;
		return -EBUSY;
	}
	if ((MINOR(ino->i_rdev) & ~FTAPE_NO_REWIND) > 3) {
		TRACE(1, "failed: illegal unit nr");
		MOD_DEC_USE_COUNT;	/* unlock module in memory */
		TRACE_EXIT;
		return -ENXIO;
	}
	if (ftape_unit == -1 || FTAPE_UNIT != (MINOR(ino->i_rdev) & 3)) {
		/*  Other selection than last time
		 */
		ftape_init_driver();
	}
	ftape_unit = MINOR(ino->i_rdev);
	ftape_failure = 0;	/* allow tape operations */
	old_sigmask = current->blocked;
	current->blocked = _BLOCK_ALL;
	fdc_save_drive_specs();	/* save Drive Specification regs on i82078-1's */
	result = _ftape_open();
	if (result < 0) {
		TRACE(1, "_ftape_open failed");
		current->blocked = old_sigmask;		/* restore mask */
		MOD_DEC_USE_COUNT;	/* unlock module in memory */
		TRACE_EXIT;
		return result;
	} else {
		busy_flag = 1;
		/*  Mask signals that will disturb proper operation of the
		 *  program that is calling.
		 */
		current->blocked = old_sigmask | _DO_BLOCK;
		TRACE_EXIT;
		return 0;
	}
}

/*      Close ftape device
 */
static void ftape_close(struct inode *ino, struct file *filep)
{
	TRACE_FUN(4, "ftape_close");
	int result;

	if (!busy_flag || MINOR(ino->i_rdev) != ftape_unit) {
		TRACE(1, "failed: not busy or wrong unit");
		TRACE_EXIT;
		return;		/* keep busy_flag !(?) */
	}
	current->blocked = _BLOCK_ALL;
	result = _ftape_close();
	if (result < 0) {
		TRACE(1, "_ftape_close failed");
	}
	fdc_restore_drive_specs();	/* restore original values */
	ftape_failure = 1;	/* inhibit any operation but open */
	busy_flag = 0;
	current->blocked = old_sigmask;		/* restore before open state */
	TRACE_EXIT;
	MOD_DEC_USE_COUNT;	/* unlock module in memory */
}

/*      Ioctl for ftape device
 */
static int ftape_ioctl(struct inode *ino, struct file *filep,
		       unsigned int command, unsigned long arg)
{
	TRACE_FUN(4, "ftape_ioctl");
	int result = -EIO;
	int old_sigmask;

	if (!busy_flag || MINOR(ino->i_rdev) != ftape_unit || ftape_failure) {
		TRACE(1, "failed: not busy, failure or wrong unit");
		TRACE_EXIT;
		return -EIO;
	}
	old_sigmask = current->blocked;		/* save mask */
	current->blocked = _BLOCK_ALL;
	/* This will work as long as sizeof( void*) == sizeof( long)
	 */
	result = _ftape_ioctl(command, (void *) arg);
	current->blocked = old_sigmask;		/* restore mask */
	TRACE_EXIT;
	return result;
}

/*      Read from tape device
 */
static int ftape_read(struct inode *ino, struct file *fp, char *buff, int req_len)
{
	TRACE_FUN(5, "ftape_read");
	int result = -EIO;
	int old_sigmask;

	TRACEi(5, "called with count:", req_len);
	if (!busy_flag || MINOR(ino->i_rdev) != ftape_unit || ftape_failure) {
		TRACE(1, "failed: not busy, failure or wrong unit");
		TRACE_EXIT;
		return -EIO;
	}
	old_sigmask = current->blocked;		/* save mask */
	current->blocked = _BLOCK_ALL;
	result = _ftape_read(buff, req_len);
	TRACEi(7, "return with count:", result);
	current->blocked = old_sigmask;		/* restore mask */
	TRACE_EXIT;
	return result;
}

/*      Write to tape device
 */
static int ftape_write(struct inode *ino, struct file *fp, const char *buff, int req_len)
{
	TRACE_FUN(8, "ftape_write");
	int result = -EIO;
	int old_sigmask;

	TRACEi(5, "called with count:", req_len);
	if (!busy_flag || MINOR(ino->i_rdev) != ftape_unit || ftape_failure) {
		TRACE(1, "failed: not busy, failure or wrong unit");
		TRACE_EXIT;
		return -EIO;
	}
	old_sigmask = current->blocked;		/* save mask */
	current->blocked = _BLOCK_ALL;
	result = _ftape_write(buff, req_len);
	TRACEi(7, "return with count:", result);
	current->blocked = old_sigmask;		/* restore mask */
	TRACE_EXIT;
	return result;
}
