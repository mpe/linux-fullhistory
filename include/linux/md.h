
/*
   md.h : Multiple Devices driver for Linux
          Copyright (C) 1994-96 Marc ZYNGIER
	  <zyngier@ufr-info-p7.ibp.fr> or
	  <maz@gloups.fdn.fr>
	  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#ifndef _MD_H
#define _MD_H

#include <asm/segment.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/ioctl.h>

#define MD_VERSION "0.35"

/* ioctls */
#define REGISTER_DEV _IO (MD_MAJOR, 1)
#define START_MD     _IO (MD_MAJOR, 2)
#define STOP_MD      _IO (MD_MAJOR, 3)

/*
   personalities :
   Byte 0 : Chunk size factor
   Byte 1 : Fault tolerance count for each physical device
            (   0 means no fault tolerance,
             0xFF means always tolerate faults), not used by now.
   Byte 2 : Personality
   Byte 3 : Reserved.
 */

#define FAULT_SHIFT       8
#define PERSONALITY_SHIFT 16

#define FACTOR_MASK       0xFFUL
#define FAULT_MASK        0xFF00UL
#define PERSONALITY_MASK  0xFF0000UL

#define MD_RESERVED       0	/* Not used by now */
#define LINEAR            (1UL << PERSONALITY_SHIFT)
#define STRIPED           (2UL << PERSONALITY_SHIFT)
#define RAID0             STRIPED
#define RAID1             (3UL << PERSONALITY_SHIFT)
#define RAID5             (4UL << PERSONALITY_SHIFT)
#define MAX_PERSONALITY   5

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/blkdev.h>

#define MAX_REAL     8		/* Max number of physical dev per md dev */
#define MAX_MD_DEV   4		/* Max number of md dev */

#define FACTOR(a)         ((a)->repartition & FACTOR_MASK)
#define MAX_FAULT(a)      (((a)->repartition & FAULT_MASK)>>8)
#define PERSONALITY(a)    ((a)->repartition & PERSONALITY_MASK)

#define FACTOR_SHIFT(a) (PAGE_SHIFT + (a) - 10)

struct real_dev
{
  kdev_t dev;			/* Device number */
  int size;			/* Device size (in blocks) */
  int offset;			/* Real device offset (in blocks) in md dev
				   (only used in linear mode) */
  struct inode *inode;		/* Lock inode */
};

struct md_dev;

struct md_personality
{
  char *name;
  int (*map)(struct md_dev *md_dev, kdev_t *rdev,
	     unsigned long *rsector, unsigned long size);
  int (*run)(int minor, struct md_dev *md_dev);
  int (*stop)(int minor, struct md_dev *md_dev);
  int (*status)(char *page, int minor, struct md_dev *md_dev);
  int (*ioctl)(struct inode *inode, struct file *file,
	       unsigned int cmd, unsigned long arg);
  int max_invalid_dev;
};

struct md_dev
{
  struct real_dev devices[MAX_REAL];
  struct md_personality *pers;
  int repartition;
  int busy;
  int nb_dev;
  void *private;
};

extern struct md_dev md_dev[MAX_MD_DEV];
extern int md_size[MAX_MD_DEV];

extern char *partition_name (kdev_t dev);

extern int register_md_personality (int p_num, struct md_personality *p);
extern int unregister_md_personality (int p_num);

#endif __KERNEL__
#endif _MD_H
