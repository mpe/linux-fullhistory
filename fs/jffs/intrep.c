/*
 * JFFS -- Journaling Flash File System, Linux implementation.
 *
 * Copyright (C) 1999, 2000  Axis Communications, Inc.
 *
 * Created by Finn Hakansson <finn@axis.com>.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * $Id: intrep.c,v 1.15 2000/06/27 15:33:43 dwmw2 Exp $
 *
 * Ported to Linux 2.3.x and MTD:
 * Copyright (C) 2000  Alexander Larsson (alex@cendio.se), Cendio Systems AB
 *
 */

/* This file contains the code for the internal structure of the
   Journaling Flash File System, JFFS.  */

/*
 * Todo list:
 *
 * memcpy_to_flash() and memcpy_from_flash()-functions.
 *
 * Implementation of hard links.
 *
 * Organize the source code in a better way. Against the VFS we could
 * have jffs_ext.c, and against the block device jffs_int.c.
 * A better file-internal organization too.
 *
 * A better checksum algorithm.
 *
 * Consider endianness stuff. ntohl() etc.
 *
 * Are we handling the atime, mtime, ctime members of the inode right?
 *
 * Remove some duplicated code. Take a look at jffs_write_node() and
 * jffs_rewrite_data() for instance.
 *
 * Implement more meaning of the nlink member in various data structures.
 * nlink could be used in conjunction with hard links for instance.
 *
 * Fix the rename stuff. (I.e. if we have two files `a' and `b' and we
 * do a `mv b a'.) Half of this is already implemented.
 *
 * Better memory management. Allocate data structures in larger chunks
 * if possible.
 *
 * If too much meta data is stored, a garbage collect should be issued.
 * We have experienced problems with too much meta data with for instance
 * log files.
 *
 * Improve the calls to jffs_ioctl(). We would like to retrieve more
 * information to be able to debug (or to supervise) JFFS during run-time.
 *
 */
#define __NO_VERSION__
#include <linux/config.h>
#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/jffs.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/pagemap.h>
#include <linux/locks.h>
#include <asm/semaphore.h>
#include <asm/byteorder.h>
#include <linux/version.h>

#include "intrep.h"
#include "jffs_fm.h"

#if LINUX_VERSION_CODE < 0x20300
#define set_current_state(x) do{current->state = x;} while (0)
#endif

#if defined(CONFIG_JFFS_FS_VERBOSE) && CONFIG_JFFS_FS_VERBOSE
#define D(x) x
#else
#define D(x)
#endif
#define D1(x) D(x)
#define D2(x) 
#define D3(x) 
#define ASSERT(x) x

#if defined(JFFS_MEMORY_DEBUG) && JFFS_MEMORY_DEBUG
long no_jffs_file = 0;
long no_jffs_node = 0;
long no_jffs_control = 0;
long no_jffs_raw_inode = 0;
long no_jffs_node_ref = 0;
long no_jffs_fm = 0;
long no_jffs_fmcontrol = 0;
long no_hash = 0;
long no_name = 0;
#endif

static int jffs_scan_flash(struct jffs_control *c);
static int jffs_update_file(struct jffs_file *f, struct jffs_node *node);
static __u8 flash_read_u8(struct mtd_info *mtd, loff_t from);

#if 1
#define _U      01
#define _L      02
#define _N      04
#define _S      010
#define _P      020
#define _C      040
#define _X      0100
#define _B      0200

const unsigned char jffs_ctype_[1 + 256] = {
	0,
	_C,     _C,     _C,     _C,     _C,     _C,     _C,     _C,
	_C,     _C|_S,  _C|_S,  _C|_S,  _C|_S,  _C|_S,  _C,     _C,
	_C,     _C,     _C,     _C,     _C,     _C,     _C,     _C,
	_C,     _C,     _C,     _C,     _C,     _C,     _C,     _C,
	_S|_B,  _P,     _P,     _P,     _P,     _P,     _P,     _P,
	_P,     _P,     _P,     _P,     _P,     _P,     _P,     _P,
	_N,     _N,     _N,     _N,     _N,     _N,     _N,     _N,
	_N,     _N,     _P,     _P,     _P,     _P,     _P,     _P,
	_P,     _U|_X,  _U|_X,  _U|_X,  _U|_X,  _U|_X,  _U|_X,  _U,
	_U,     _U,     _U,     _U,     _U,     _U,     _U,     _U,
	_U,     _U,     _U,     _U,     _U,     _U,     _U,     _U,
	_U,     _U,     _U,     _P,     _P,     _P,     _P,     _P,
	_P,     _L|_X,  _L|_X,  _L|_X,  _L|_X,  _L|_X,  _L|_X,  _L,
	_L,     _L,     _L,     _L,     _L,     _L,     _L,     _L,
	_L,     _L,     _L,     _L,     _L,     _L,     _L,     _L,
	_L,     _L,     _L,     _P,     _P,     _P,     _P,     _C
};

#define jffs_isalpha(c)      ((jffs_ctype_+1)[(int)c]&(_U|_L))
#define jffs_isupper(c)      ((jffs_ctype_+1)[(int)c]&_U)
#define jffs_islower(c)      ((jffs_ctype_+1)[(int)c]&_L)
#define jffs_isdigit(c)      ((jffs_ctype_+1)[(int)c]&_N)
#define jffs_isxdigit(c)     ((jffs_ctype_+1)[(int)c]&(_X|_N))
#define jffs_isspace(c)      ((jffs_ctype_+1)[(int)c]&_S)
#define jffs_ispunct(c)      ((jffs_ctype_+1)[(int)c]&_P)
#define jffs_isalnum(c)      ((jffs_ctype_+1)[(int)c]&(_U|_L|_N))
#define jffs_isprint(c)      ((jffs_ctype_+1)[(int)c]&(_P|_U|_L|_N|_B))
#define jffs_isgraph(c)      ((jffs_ctype_+1)[(int)c]&(_P|_U|_L|_N))
#define jffs_iscntrl(c)      ((jffs_ctype_+1)[(int)c]&_C)

void
jffs_hexdump(struct mtd_info *mtd, loff_t pos, int size)
{
	char line[16];
	int j = 0;

	while (size > 0) {
		int i;

		printk("%ld:", (long) pos);
		for (j = 0; j < 16; j++) {
			line[j] = flash_read_u8(mtd, pos++);
		}
		for (i = 0; i < j; i++) {
			if (!(i & 1)) {
				printk(" %.2x", line[i] & 0xff);
			}
			else {
				printk("%.2x", line[i] & 0xff);
			}
		}

		/* Print empty space */
		for (; i < 16; i++) {
			if (!(i & 1)) {
				printk("   ");
			}
			else {
				printk("  ");
			}
		}
		printk("  ");

		for (i = 0; i < j; i++) {
			if (jffs_isgraph(line[i])) {
				printk("%c", line[i]);
			}
			else {
				printk(".");
			}
		}
		printk("\n");
		size -= 16;
	}
}
#endif

#define flash_safe_acquire(arg) 
#define flash_safe_release(arg) 

static int
flash_safe_read(struct mtd_info *mtd, loff_t from,
		u_char *buf, size_t count)
{
  size_t retlen;
  
  MTD_READ(mtd, from, count, &retlen, buf);
  if (retlen != count) {
    printk("Didn't read all bytes in flash_safe_read()\n");
  }
  return retlen;
}

static __u32
flash_read_u32(struct mtd_info *mtd, loff_t from)
{
  size_t retlen;
  __u32 ret;
  
  MTD_READ(mtd, from, 4, &retlen, (unsigned char *)&ret);
  if (retlen != 4) {
    printk("Didn't read all bytes in flash_read_u32()\n");
    return 0;
  }

  return ret;
}

static __u8
flash_read_u8(struct mtd_info *mtd, loff_t from)
{
  size_t retlen;
  __u8 ret;
  
  MTD_READ(mtd, from, 1, &retlen, &ret);
  if (retlen != 1) {
    printk("Didn't read all bytes in flash_read_u32()\n");
    return 0;
  }

  return ret;
}


static int
flash_safe_write(struct mtd_info *mtd, loff_t to,
		 const u_char *buf, size_t count)
{
  size_t retlen;

  MTD_WRITE(mtd, to, count, &retlen, buf);
  if (retlen != count) {
    printk("Didn't write all bytes in flash_safe_write()\n");
  }
  return retlen;
}

static int
flash_memset(struct mtd_info *mtd, loff_t to,
	     const u_char c, size_t size)
{
  static unsigned char pattern[16];
  int i;

  /* fill up pattern */
  
  for(i = 0; i < 16; i++)
    pattern[i] = c;

  /* write as many 16-byte chunks as we can */
  
  while(size >= 16) {
    flash_safe_write(mtd, to, pattern, 16);
    size -= 16;
    to += 16;
  }

  /* and the rest */
  
  if(size)
    flash_safe_write(mtd, to, pattern, size);

  return size;
}

static void intrep_erase_callback(struct erase_info *done)
{
  wait_queue_head_t *wait_q;

  wait_q = (wait_queue_head_t *)done->priv;

  wake_up(wait_q);
}

static int
flash_erase_region(struct mtd_info *mtd, loff_t start,
		   size_t size)
{
  struct erase_info *erase;
  DECLARE_WAITQUEUE(wait, current);
  wait_queue_head_t wait_q;

  erase = kmalloc(sizeof(struct erase_info), GFP_KERNEL);
  if (!erase) 
    return -ENOMEM;

  init_waitqueue_head(&wait_q);

  erase->mtd = mtd;
  erase->callback = intrep_erase_callback;
  erase->addr = start;
  erase->len = size;
  erase->priv = (u_long)&wait_q;

  set_current_state(TASK_INTERRUPTIBLE);
  add_wait_queue(&wait_q, &wait);
	
  if (MTD_ERASE(mtd, erase) < 0) {
    set_current_state(TASK_RUNNING);
    remove_wait_queue(&wait_q, &wait);
    kfree(erase);
    
    printk(KERN_WARNING "flash: erase of region [0x%ld, 0x%ld] totally failed\n",
	   (long)start, (long)start + size);
    
    return -1;
  }

  schedule(); /* Wait for flash to finish. */
  /* FIXME: We could have been interrupted here. We don't deal with it */
  remove_wait_queue(&wait_q, &wait);

  kfree(erase);

  return 0;
}

inline int
jffs_min(int a, int b)
{
	return (a < b ? a : b);
}


inline int
jffs_max(int a, int b)
{
	return (a > b ? a : b);
}


/* This routine calculates checksums in JFFS.  */
__u32
jffs_checksum(const void *data, int size)
{
	__u32 sum = 0;
	__u8 *ptr = (__u8 *)data;
	while (size-- > 0) {
		sum += *ptr++;
	}
	D3(printk(", result: 0x%08x\n", sum));
	return sum;
}

__u32
jffs_checksum_flash(struct mtd_info *mtd, loff_t start, int size)
{
	__u32 sum = 0;
	loff_t ptr = start;
	while (size-- > 0) {
		sum += flash_read_u8(mtd, ptr++);
	}
	D3(printk("checksum result: 0x%08x\n", sum));
	return sum;
}

/* Create and initialize a new struct jffs_file.  */
static struct jffs_file *
jffs_create_file(struct jffs_control *c,
		 const struct jffs_raw_inode *raw_inode)
{
	struct jffs_file *f;

	if (!(f = (struct jffs_file *)kmalloc(sizeof(struct jffs_file),
					      GFP_KERNEL))) {
		D(printk("jffs_create_file(): Failed!\n"));
		return 0;
	}
	DJM(no_jffs_file++);
	memset(f, 0, sizeof(struct jffs_file));
	f->ino = raw_inode->ino;
	f->pino = raw_inode->pino;
	f->nlink = raw_inode->nlink;
	f->deleted = raw_inode->deleted;
	f->c = c;

	return f;
}


/* Build a control block for the file system.  */
static struct jffs_control *
jffs_create_control(kdev_t dev)
{
	struct jffs_control *c;
	register int s = sizeof(struct jffs_control);
	int i;
	D(char *t = 0);

	D2(printk("jffs_create_control()\n"));

	if (!(c = (struct jffs_control *)kmalloc(s, GFP_KERNEL))) {
		goto fail_control;
	}
	DJM(no_jffs_control++);
	c->root = 0;
	c->hash_len = JFFS_HASH_SIZE;
	s = sizeof(struct list_head) * c->hash_len;
	if (!(c->hash = (struct list_head *)kmalloc(s, GFP_KERNEL))) {
		goto fail_hash;
	}
	DJM(no_hash++);
	for (i=0;i<c->hash_len;i++) 
		INIT_LIST_HEAD(&c->hash[i]);
	if (!(c->fmc = jffs_build_begin(c, dev))) {
		goto fail_fminit;
	}
	c->next_ino = JFFS_MIN_INO + 1;
	return c;

fail_fminit:
	D(t = "c->fmc");
fail_hash:
	kfree(c);
	DJM(no_jffs_control--);
	D(t = t ? t : "c->hash");
fail_control:
	D(t = t ? t : "control");
	D(printk("jffs_create_control(): Allocation failed: (%s)\n", t));
	return (struct jffs_control *)0;
}


/* Clean up all data structures associated with the file system.  */
void
jffs_cleanup_control(struct jffs_control *c)
{
	D2(printk("jffs_cleanup_control()\n"));

	if (!c) {
		D(printk("jffs_cleanup_control(): c == NULL !!!\n"));
		return;
	}

	/* Free all files and nodes.  */
	if (c->hash) {
		jffs_foreach_file(c, jffs_free_node_list);
		kfree(c->hash);
		DJM(no_hash--);
	}
	jffs_cleanup_fmcontrol(c->fmc);
	kfree(c);
	DJM(no_jffs_control--);
	D3(printk("jffs_cleanup_control(): Leaving...\n"));
}


/* This function adds a virtual root node to the in-RAM representation.
   Called by jffs_build_fs().  */
static int
jffs_add_virtual_root(struct jffs_control *c)
{
	struct jffs_file *root;
	struct jffs_node *node;

	D2(printk("jffs_add_virtual_root(): "
		  "Creating a virtual root directory.\n"));

	if (!(root = (struct jffs_file *)kmalloc(sizeof(struct jffs_file),
						 GFP_KERNEL))) {
		return -ENOMEM;
	}
	DJM(no_jffs_file++);
	if (!(node = (struct jffs_node *)kmalloc(sizeof(struct jffs_node),
						 GFP_KERNEL))) {
		kfree(root);
		DJM(no_jffs_file--);
		return -ENOMEM;
	}
	DJM(no_jffs_node++);
	memset(node, 0, sizeof(struct jffs_node));
	node->ino = JFFS_MIN_INO;
	memset(root, 0, sizeof(struct jffs_file));
	root->ino = JFFS_MIN_INO;
	root->mode = S_IFDIR | S_IRWXU | S_IRGRP
		     | S_IXGRP | S_IROTH | S_IXOTH;
	root->atime = root->mtime = root->ctime = CURRENT_TIME;
	root->nlink = 1;
	root->c = c;
	root->version_head = root->version_tail = node;
	jffs_insert_file_into_hash(root);
	return 0;
}


/* This is where the file system is built and initialized.  */
int
jffs_build_fs(struct super_block *sb)
{
	struct jffs_control *c;
	int err = 0;

	D2(printk("jffs_build_fs()\n"));

	if (!(c = jffs_create_control(sb->s_dev))) {
		return -ENOMEM;
	}
	c->building_fs = 1;
	c->sb = sb;
	if ((err = jffs_scan_flash(c)) < 0) {
		goto jffs_build_fs_fail;
	}

	/* Add a virtual root node if no one exists.  */
	if (!jffs_find_file(c, JFFS_MIN_INO)) {
		if ((err = jffs_add_virtual_root(c)) < 0) {
			goto jffs_build_fs_fail;
		}
	}

	/* Remove deleted nodes.  */
	if ((err = jffs_foreach_file(c, jffs_possibly_delete_file)) < 0) {
		printk(KERN_ERR "JFFS: Failed to remove deleted nodes.\n");
		goto jffs_build_fs_fail;
	}
	/* Remove redundant nodes.  (We are not interested in the
	   return value in this case.)  */
	jffs_foreach_file(c, jffs_remove_redundant_nodes);
	/* Try to build a tree from all the nodes.  */
	if ((err = jffs_foreach_file(c, jffs_insert_file_into_tree)) < 0) {
		printk("JFFS: Failed to build tree.\n");
		goto jffs_build_fs_fail;
	}
	/* Compute the sizes of all files in the filesystem.  Adjust if
	   necessary.  */
	if ((err = jffs_foreach_file(c, jffs_build_file)) < 0) {
		printk("JFFS: Failed to build file system.\n");
		goto jffs_build_fs_fail;
	}
	sb->u.generic_sbp = (void *)c;
	c->building_fs = 0;

	D1(jffs_print_hash_table(c));
	D1(jffs_print_tree(c->root, 0));

	return 0;

jffs_build_fs_fail:
	jffs_cleanup_control(c);
	return err;
} /* jffs_build_fs()  */


/* Scan the whole flash memory in order to find all nodes in the
   file systems.  */
static int
jffs_scan_flash(struct jffs_control *c)
{
	char name[JFFS_MAX_NAME_LEN + 2];
	struct jffs_raw_inode raw_inode;
	struct jffs_node *node = 0;
	struct jffs_fmcontrol *fmc = c->fmc;
	__u32 checksum;
	__u8 tmp_accurate;
	__u16 tmp_chksum;
	loff_t pos = fmc->flash_start;
	loff_t start;
	loff_t end = fmc->flash_start + fmc->flash_size;

	D1(printk("jffs_scan_flash(): start pos = 0x%ld, end = 0x%ld\n",
		  (long)pos, (long)end));

	flash_safe_acquire(fmc->mtd);

	/* Start the scan.  */
	while (pos < end) {

		/* Remember the position from where we started this scan.  */
		start = pos;

		switch (flash_read_u32(fmc->mtd, pos)) {
		case JFFS_EMPTY_BITMASK:
			/* We have found 0xff on this block.  We have to
			   scan the rest of the block to be sure it is
			   filled with 0xff.  */
			D1(printk("jffs_scan_flash(): 0xff at pos 0x%ld.\n",
				  (long)pos));
			for (; pos < end
			       && JFFS_EMPTY_BITMASK == flash_read_u32(fmc->mtd, pos);
			     pos += 4);
			D1(printk("jffs_scan_flash(): 0xff ended at "
				  "pos 0x%ld.\n", (long)pos));
			continue;

		case JFFS_DIRTY_BITMASK:
			/* We have found 0x00 on this block.  We have to
			   scan as far as possible to find out how much
			   is dirty.  */
			D1(printk("jffs_scan_flash(): 0x00 at pos 0x%ld.\n",
				  (long)pos));
			for (; pos < end
			       && JFFS_DIRTY_BITMASK == flash_read_u32(fmc->mtd, pos);
			     pos += 4);
			D1(printk("jffs_scan_flash(): 0x00 ended at "
				  "pos 0x%ld.\n", (long)pos));
			jffs_fmalloced(fmc, (__u32) start,
				       (__u32) (pos - start), 0);
			continue;

		case JFFS_MAGIC_BITMASK:
			/* We have probably found a new raw inode.  */
			break;

		default:
		bad_inode:
			/* We're f*cked.  This is not solved yet.  We have
			   to scan for the magic pattern.  */
			D1(printk("*************** Dirty flash memory or bad inode: "
				  "hexdump(pos = 0x%ld, len = 128):\n",
				  (long)pos));
			D1(jffs_hexdump(fmc->mtd, pos, 128));
			for (pos += 4; pos < end; pos += 4) {
				switch (flash_read_u32(fmc->mtd, pos)) {
				case JFFS_MAGIC_BITMASK:
					jffs_fmalloced(fmc, (__u32) start,
						       (__u32) (pos - start),
						       0);
					goto cont_scan;
				default:
					break;
				}
			}
			cont_scan:
			continue;
		}

		/* We have found the beginning of an inode.  Create a
		   node for it.  */
		if (!node) {
			if (!(node = (struct jffs_node *)
				     kmalloc(sizeof(struct jffs_node),
					     GFP_KERNEL))) {
				flash_safe_release(fmc->mtd);
				return -ENOMEM;
			}
			DJM(no_jffs_node++);
		}

		/* Read the next raw inode.  */

		flash_safe_read(fmc->mtd, pos, (u_char *) &raw_inode, sizeof(struct jffs_raw_inode));

		/* When we compute the checksum for the inode, we never
		   count the 'accurate' or the 'checksum' fields.  */
		tmp_accurate = raw_inode.accurate;
		tmp_chksum = raw_inode.chksum;
		raw_inode.accurate = 0;
		raw_inode.chksum = 0;
		checksum = jffs_checksum(&raw_inode,
					 sizeof(struct jffs_raw_inode));
		raw_inode.accurate = tmp_accurate;
		raw_inode.chksum = tmp_chksum;

		D3(printk("*** We have found this raw inode at pos 0x%ld "
			  "on the flash:\n", (long)pos));
		D3(jffs_print_raw_inode(&raw_inode));

		if (checksum != raw_inode.chksum) {
			D1(printk("jffs_scan_flash(): Bad checksum: "
				  "checksum = %u, "
				  "raw_inode.chksum = %u\n",
				  checksum, raw_inode.chksum));
			pos += sizeof(struct jffs_raw_inode);
			jffs_fmalloced(fmc, (__u32) start,
				       (__u32) (pos - start), 0);
			/* Reuse this unused struct jffs_node.  */
			continue;
		}

		/* Check the raw inode read so far.  Start with the
		   maximum length of the filename.  */
		if (raw_inode.nsize > JFFS_MAX_NAME_LEN) {
			goto bad_inode;
		}
		/* The node's data segment should not exceed a
		   certain length.  */
		if (raw_inode.dsize > fmc->max_chunk_size) {
			goto bad_inode;
		}

		pos += sizeof(struct jffs_raw_inode);

		/* This shouldn't be necessary because a node that
		   violates the flash boundaries shouldn't be written
		   in the first place. */
		if (pos >= end) {
			goto check_node;
		}

		/* Read the name.  */
		*name = 0;
		if (raw_inode.nsize) {
		        flash_safe_read(fmc->mtd, pos, name, raw_inode.nsize);
			name[raw_inode.nsize] = '\0';
			pos += raw_inode.nsize
			       + JFFS_GET_PAD_BYTES(raw_inode.nsize);
			D3(printk("name == \"%s\"\n", name));
			checksum = jffs_checksum(name, raw_inode.nsize);
			if (checksum != raw_inode.nchksum) {
				D1(printk("jffs_scan_flash(): Bad checksum: "
					  "checksum = %u, "
					  "raw_inode.nchksum = %u\n",
					  checksum, raw_inode.nchksum));
				jffs_fmalloced(fmc, (__u32) start,
					       (__u32) (pos - start), 0);
				/* Reuse this unused struct jffs_node.  */
				continue;
			}
			if (pos >= end) {
				goto check_node;
			}
		}

		/* Read the data in order to be sure it matches the
		   checksum.  */
		checksum = jffs_checksum_flash(fmc->mtd, pos, raw_inode.dsize);
		pos += raw_inode.dsize + JFFS_GET_PAD_BYTES(raw_inode.dsize);

		if (checksum != raw_inode.dchksum) {
			D1(printk("jffs_scan_flash(): Bad checksum: "
				  "checksum = %u, "
				  "raw_inode.dchksum = %u\n",
				  checksum, raw_inode.dchksum));
			jffs_fmalloced(fmc, (__u32) start,
				       (__u32) (pos - start), 0);
			/* Reuse this unused struct jffs_node.  */
			continue;
		}

		check_node:

		/* Remember the highest inode number in the whole file
		   system.  This information will be used when assigning
		   new files new inode numbers.  */
		if (c->next_ino <= raw_inode.ino) {
			c->next_ino = raw_inode.ino + 1;
		}

		if (raw_inode.accurate) {
			int err;
			node->data_offset = raw_inode.offset;
			node->data_size = raw_inode.dsize;
			node->removed_size = raw_inode.rsize;
			/* Compute the offset to the actual data in the
			   on-flash node.  */
			node->fm_offset
			= sizeof(struct jffs_raw_inode)
			  + raw_inode.nsize
			  + JFFS_GET_PAD_BYTES(raw_inode.nsize);
			node->fm = jffs_fmalloced(fmc, (__u32) start,
						  (__u32) (pos - start),
						  node);
			if (!node->fm) {
				D(printk("jffs_scan_flash(): !node->fm\n"));
				kfree(node);
				DJM(no_jffs_node--);
				flash_safe_release(fmc->mtd);
				return -ENOMEM;
			}
			if ((err = jffs_insert_node(c, 0, &raw_inode,
						    name, node)) < 0) {
				printk("JFFS: Failed to handle raw inode. "
				       "(err = %d)\n", err);
				break;
			}
			D3(jffs_print_node(node));
			node = 0; /* Don't free the node!  */
		}
		else {
			jffs_fmalloced(fmc, (__u32) start,
				       (__u32) (pos - start), 0);
			D3(printk("jffs_scan_flash(): Just found an obsolete "
				  "raw_inode. Continuing the scan...\n"));
			/* Reuse this unused struct jffs_node.  */
		}
	}

	if (node) {
		kfree(node);
		DJM(no_jffs_node--);
	}
	jffs_build_end(fmc);
	D3(printk("jffs_scan_flash(): Leaving...\n"));
	flash_safe_release(fmc->mtd);
	return 0;
} /* jffs_scan_flash()  */


/* Insert any kind of node into the file system.  Take care of data
   insertions and deletions.  Also remove redundant information. The
   memory allocated for the `name' is regarded as "given away" in the
   caller's perspective.  */
int
jffs_insert_node(struct jffs_control *c, struct jffs_file *f,
		 const struct jffs_raw_inode *raw_inode,
		 const char *name, struct jffs_node *node)
{
	int update_name = 0;
	int insert_into_tree = 0;

	D2(printk("jffs_insert_node(): ino = %u, version = %u, name = \"%s\"\n",
		  raw_inode->ino, raw_inode->version,
		  ((name && *name) ? name : "")));

	/* If there doesn't exist an associated jffs_file, then
	   create, initialize and insert one into the file system.  */
	if (!f && !(f = jffs_find_file(c, raw_inode->ino))) {
		if (!(f = jffs_create_file(c, raw_inode))) {
			return -ENOMEM;
		}
		jffs_insert_file_into_hash(f);
		insert_into_tree = 1;
	}

	node->ino = raw_inode->ino;
	node->version = raw_inode->version;
	node->data_size = raw_inode->dsize;
	node->fm_offset = sizeof(struct jffs_raw_inode) + raw_inode->nsize
			  + JFFS_GET_PAD_BYTES(raw_inode->nsize);
	node->name_size = raw_inode->nsize;

	/* Now insert the node at the correct position into the file's
	   version list.  */
	if (!f->version_head) {
		/* This is the first node.  */
		f->version_head = node;
		f->version_tail = node;
		node->version_prev = 0;
		node->version_next = 0;
		f->highest_version = node->version;
		update_name = 1;
		f->mode = raw_inode->mode;
		f->uid = raw_inode->uid;
		f->gid = raw_inode->gid;
		f->atime = raw_inode->atime;
		f->mtime = raw_inode->mtime;
		f->ctime = raw_inode->ctime;
		f->deleted = raw_inode->deleted;
	}
	else if ((f->highest_version < node->version)
		 || (node->version == 0)) {
		/* Insert at the end of the list.  I.e. this node is the
		   oldest one so far.  */
		node->version_prev = f->version_tail;
		node->version_next = 0;
		f->version_tail->version_next = node;
		f->version_tail = node;
		f->highest_version = node->version;
		update_name = 1;
		f->pino = raw_inode->pino;
		f->mode = raw_inode->mode;
		f->uid = raw_inode->uid;
		f->gid = raw_inode->gid;
		f->atime = raw_inode->atime;
		f->mtime = raw_inode->mtime;
		f->ctime = raw_inode->ctime;
		f->deleted = raw_inode->deleted;
	}
	else if (f->version_head->version > node->version) {
		/* Insert at the bottom of the list.  */
		node->version_prev = 0;
		node->version_next = f->version_head;
		f->version_head->version_prev = node;
		f->version_head = node;
		if (!f->name) {
			update_name = 1;
		}
		if (raw_inode->deleted) {
			f->deleted = raw_inode->deleted;
		}
	}
	else {
		struct jffs_node *n;
		int newer_name = 0;
		/* Search for the insertion position starting from
		   the tail (newest node).  */
		for (n = f->version_tail; n; n = n->version_prev) {
			if (n->version < node->version) {
				node->version_prev = n;
				node->version_next = n->version_next;
				node->version_next->version_prev = node;
				n->version_next = node;
				if (!newer_name) {
					update_name = 1;
				}
				break;
			}
			if (n->name_size) {
				newer_name = 1;
			}
		}
	}

	/* Perhaps update the name.  */
	if (raw_inode->nsize && update_name && name && *name && (name != f->name)) {
		if (f->name) {
			kfree(f->name);
			DJM(no_name--);
		}
		if (!(f->name = (char *) kmalloc(raw_inode->nsize + 1,
						 GFP_KERNEL))) {
			return -ENOMEM;
		}
		DJM(no_name++);
		memcpy(f->name, name, raw_inode->nsize);
		f->name[raw_inode->nsize] = '\0';
		f->nsize = raw_inode->nsize;
		D3(printk("jffs_insert_node(): Updated the name of "
			  "the file to \"%s\".\n", name));
	}

	if (!c->building_fs) {
		D3(printk("jffs_insert_node(): ---------------------------"
			  "------------------------------------------- 1\n"));
		if (insert_into_tree) {
			jffs_insert_file_into_tree(f);
		}
		if (f->deleted) {
			/* Mark all versions of the node as obsolete.  */
			jffs_possibly_delete_file(f);
		}
		else {
			if (node->data_size || node->removed_size) {
				jffs_update_file(f, node);
			}
			jffs_remove_redundant_nodes(f);
		}
#ifdef USE_GC
		if (!c->fmc->no_call_gc) {
			jffs_garbage_collect(c);
		}
#endif
		D3(printk("jffs_insert_node(): ---------------------------"
			  "------------------------------------------- 2\n"));
	}

	return 0;
} /* jffs_insert_node()  */


/* Unlink a jffs_node from the version list it is in.  */
static inline void
jffs_unlink_node_from_version_list(struct jffs_file *f,
				   struct jffs_node *node)
{
	if (node->version_prev) {
		node->version_prev->version_next = node->version_next;
	} else {
		f->version_head = node->version_next;
	}
	if (node->version_next) {
		node->version_next->version_prev = node->version_prev;
	} else {
		f->version_tail = node->version_prev;
	}
}


/* Unlink a jffs_node from the range list it is in.  */
static inline void
jffs_unlink_node_from_range_list(struct jffs_file *f, struct jffs_node *node)
{
	if (node->range_prev) {
		node->range_prev->range_next = node->range_next;
	}
	else {
		f->range_head = node->range_next;
	}
	if (node->range_next) {
		node->range_next->range_prev = node->range_prev;
	}
	else {
		f->range_tail = node->range_prev;
	}
}


/* Function used by jffs_remove_redundant_nodes() below.  This function
   classifies what kind of information a node adds to a file.  */
static inline __u8
jffs_classify_node(struct jffs_node *node)
{
	__u8 mod_type = JFFS_MODIFY_INODE;

	if (node->name_size) {
		mod_type |= JFFS_MODIFY_NAME;
	}
	if (node->data_size || node->removed_size) {
		mod_type |= JFFS_MODIFY_DATA;
	}
	return mod_type;
}


/* Remove redundant nodes from a file.  Mark the on-flash memory
   as dirty.  */
int
jffs_remove_redundant_nodes(struct jffs_file *f)
{
	struct jffs_node *newest_node;
	struct jffs_node *cur;
	struct jffs_node *prev;
	__u8 newest_type;
	__u8 mod_type;
	__u8 node_with_name_later = 0;

	if (!(newest_node = f->version_tail)) {
		return 0;
	}

	/* What does the `newest_node' modify?  */
	newest_type = jffs_classify_node(newest_node);
	node_with_name_later = newest_type & JFFS_MODIFY_NAME;

	D3(printk("jffs_remove_redundant_nodes(): ino: %u, name: \"%s\", "
		  "newest_type: %u\n", f->ino, (f->name ? f->name : ""),
		  newest_type));

	/* Traverse the file's nodes and determine which of them that are
	   superfluous.  Yeah, this might look very complex at first
	   glance but it is actually very simple.  */
	for (cur = newest_node->version_prev; cur; cur = prev) {
		prev = cur->version_prev;
		mod_type = jffs_classify_node(cur);
		if ((mod_type <= JFFS_MODIFY_INODE)
		    || ((newest_type & JFFS_MODIFY_NAME)
			&& (mod_type
			    <= (JFFS_MODIFY_INODE + JFFS_MODIFY_NAME)))
		    || (cur->data_size == 0 && cur->removed_size
			&& !cur->version_prev && node_with_name_later)) {
			/* Yes, this node is redundant. Remove it.  */
			D2(printk("jffs_remove_redundant_nodes(): "
				  "Removing node: ino: %u, version: %u, "
				  "mod_type: %u\n", cur->ino, cur->version,
				  mod_type));
			jffs_unlink_node_from_version_list(f, cur);
			jffs_fmfree(f->c->fmc, cur->fm, cur);
			kfree(cur);
			DJM(no_jffs_node--);
		}
		else {
			node_with_name_later |= (mod_type & JFFS_MODIFY_NAME);
		}
	}

	return 0;
}


/* Insert a file into the hash table.  */
int
jffs_insert_file_into_hash(struct jffs_file *f)
{
	int i = f->ino % f->c->hash_len;

	D3(printk("jffs_insert_file_into_hash(): f->ino: %u\n", f->ino));

	list_add(&f->hash, &f->c->hash[i]);
	return 0;
}


/* Insert a file into the file system tree.  */
int
jffs_insert_file_into_tree(struct jffs_file *f)
{
	struct jffs_file *parent;

	D3(printk("jffs_insert_file_into_tree(): name: \"%s\"\n",
		  (f->name ? f->name : "")));

	if (!(parent = jffs_find_file(f->c, f->pino))) {
		if (f->pino == 0) {
			f->c->root = f;
			f->parent = 0;
			f->sibling_prev = 0;
			f->sibling_next = 0;
			return 0;
		}
		else {
			D1(printk("jffs_insert_file_into_tree(): Found "
				  "inode with no parent and pino == %u\n",
				  f->pino));
			return -1;
		}
	}
	f->parent = parent;
	f->sibling_next = parent->children;
	if (f->sibling_next) {
		f->sibling_next->sibling_prev = f;
	}
	f->sibling_prev = 0;
	parent->children = f;
	return 0;
}


/* Remove a file from the hash table.  */
int
jffs_unlink_file_from_hash(struct jffs_file *f)
{
	D3(printk("jffs_unlink_file_from_hash(): f: 0x%p, "
		  "ino %u\n", f, f->ino));

	list_del(&f->hash);
	return 0;
}


/* Just remove the file from the parent's children.  Don't free
   any memory.  */
int
jffs_unlink_file_from_tree(struct jffs_file *f)
{
	D3(printk("jffs_unlink_file_from_tree(): ino: %d, pino: %d, name: "
		  "\"%s\"\n", f->ino, f->pino, (f->name ? f->name : "")));

	if (f->sibling_prev) {
		f->sibling_prev->sibling_next = f->sibling_next;
	}
	else if (f->parent) {
	        D3(printk("f->parent=%p\n", f->parent));
		f->parent->children = f->sibling_next;
	}
	if (f->sibling_next) {
		f->sibling_next->sibling_prev = f->sibling_prev;
	}
	return 0;
}


/* Find a file with its inode number.  */
struct jffs_file *
jffs_find_file(struct jffs_control *c, __u32 ino)
{
	struct jffs_file *f;
	int i = ino % c->hash_len;
	struct list_head *tmp;

	D3(printk("jffs_find_file(): ino: %u\n", ino));

	for (tmp = c->hash[i].next; tmp != &c->hash[i]; tmp = tmp->next) {
		f = list_entry(tmp, struct jffs_file, hash);
		if (ino != f->ino)
			continue;
		D3(printk("jffs_find_file(): Found file with ino "
			       "%u. (name: \"%s\")\n",
			       ino, (f->name ? f->name : ""));
		);
		return f;
	}
	D3(printk("jffs_find_file(): Didn't find file "
			 "with ino %u.\n", ino);
	);
	return NULL;
}


/* Find a file in a directory.  We are comparing the names.  */
struct jffs_file *
jffs_find_child(struct jffs_file *dir, const char *name, int len)
{
	struct jffs_file *f;

	D3(printk("jffs_find_child()\n"));

	for (f = dir->children; f; f = f->sibling_next) {
		if (f->name
		    && !strncmp(f->name, name, len)
		    && f->name[len] == '\0') {
			break;
		}
	}

	D3(if (f) {
		printk("jffs_find_child(): Found \"%s\".\n", f->name);
	}
	else {
		char *copy = (char *) kmalloc(len + 1, GFP_KERNEL);
		if (copy) {
			memcpy(copy, name, len);
			copy[len] = '\0';
		}
		printk("jffs_find_child(): Didn't find the file \"%s\".\n",
		       (copy ? copy : ""));
		if (copy) {
			kfree(copy);
		}
	});

	return f;
}


/* Write a raw inode that takes up a certain amount of space in the flash
   memory.  At the end of the flash device, there is often space that is
   impossible to use.  At these times we want to mark this space as not
   used.  In the cases when the amount of space is greater or equal than
   a struct jffs_raw_inode, we write a "dummy node" that takes up this
   space.  The space after the raw inode, if it exists, is left as it is.
   Since this space after the raw inode contains JFFS_EMPTY_BITMASK bytes,
   we can compute the checksum of it; we don't have to manipulate it any
   further.

   If the space left on the device is less than the size of a struct
   jffs_raw_inode, this space is filled with JFFS_DIRTY_BITMASK bytes.
   No raw inode is written this time.  */
static int
jffs_write_dummy_node(struct jffs_control *c, struct jffs_fm *dirty_fm)
{
	struct jffs_fmcontrol *fmc = c->fmc;
	int err;

	D1(printk("jffs_write_dummy_node(): dirty_fm->offset = 0x%08x, "
		  "dirty_fm->size = %u\n",
		  dirty_fm->offset, dirty_fm->size));

	if (dirty_fm->size >= sizeof(struct jffs_raw_inode)) {
		struct jffs_raw_inode raw_inode;
		memset(&raw_inode, 0, sizeof(struct jffs_raw_inode));
		raw_inode.magic = JFFS_MAGIC_BITMASK;
		raw_inode.dsize = dirty_fm->size
				  - sizeof(struct jffs_raw_inode);
		raw_inode.dchksum = raw_inode.dsize * 0xff;
		raw_inode.chksum
		= jffs_checksum(&raw_inode, sizeof(struct jffs_raw_inode));

		if ((err = flash_safe_write(fmc->mtd,
					    dirty_fm->offset,
					    (u_char *)&raw_inode,
					    sizeof(struct jffs_raw_inode)))
		    < 0) {
                       printk(KERN_ERR "JFFS: jffs_write_dummy_node: "
                              "flash_safe_write failed!\n");
			return err;
		}
	}
	else {
		flash_safe_acquire(fmc->mtd);
		flash_memset(fmc->mtd, dirty_fm->offset, 0, dirty_fm->size);
		flash_safe_release(fmc->mtd);
	}

	D3(printk("jffs_write_dummy_node(): Leaving...\n"));
	return 0;
}

/* Write a raw inode, possibly its name and possibly some data.  */
int
jffs_write_node(struct jffs_control *c, struct jffs_node *node,
		struct jffs_raw_inode *raw_inode,
		const char *name, const unsigned char *data)
{
	struct jffs_fmcontrol *fmc = c->fmc;
	struct jffs_fm *fm;
	__u32 pos;
	int err;
	__u32 total_name_size = raw_inode->nsize
				+ JFFS_GET_PAD_BYTES(raw_inode->nsize);
	__u32 total_data_size = raw_inode->dsize
				+ JFFS_GET_PAD_BYTES(raw_inode->dsize);
	__u32 total_size = sizeof(struct jffs_raw_inode)
			   + total_name_size + total_data_size;

	/* Fire the retrorockets and shoot the fruiton torpedoes, sir!  */

	ASSERT(if (!node) {
		printk("jffs_write_node(): node == NULL\n");
		return -EINVAL;
	});
	ASSERT(if (raw_inode && raw_inode->nsize && !name) {
		printk("*** jffs_write_node(): nsize = %u but name == NULL\n",
		       raw_inode->nsize);
		return -EINVAL;
	});

	D1(printk("jffs_write_node(): filename = \"%s\", ino = %u, "
		  "version = %u, total_size = %u\n",
		  (name ? name : ""), raw_inode->ino,
		  raw_inode->version, total_size));

	/* First try to allocate some flash memory.  */
	if ((err = jffs_fmalloc(fmc, total_size, node, &fm)) < 0) {
		D(printk("jffs_write_node(): jffs_fmalloc(0x%p, %u) "
			 "failed!\n", fmc, total_size));
		return err;
	}
	else if (!fm->nodes) {
		/* The jffs_fm struct that we got is not good enough.
		   Make that space dirty.  */
		if ((err = jffs_write_dummy_node(c, fm)) < 0) {
			D(printk("jffs_write_node(): "
				 "jffs_write_dummy_node(): Failed!\n"));
			kfree(fm);
			DJM(no_jffs_fm--);
			return err;
		}
		/* Get a new one.  */
		if ((err = jffs_fmalloc(fmc, total_size, node, &fm)) < 0) {
			D(printk("jffs_write_node(): Second "
				 "jffs_fmalloc(0x%p, %u) failed!\n",
				 fmc, total_size));
			return err;
		}
	}
	node->fm = fm;

	ASSERT(if (fm->nodes == 0) {
		printk(KERN_ERR "jffs_write_node(): fm->nodes == 0\n");
	});

	pos = node->fm->offset;

	/* Compute the checksum for the data and name chunks.  */
	raw_inode->dchksum = jffs_checksum(data, raw_inode->dsize);
	raw_inode->nchksum = jffs_checksum(name, raw_inode->nsize);

	/* The checksum is calculated without the chksum and accurate
	   fields so set them to zero first.  */
	raw_inode->accurate = 0;
	raw_inode->chksum = 0;
	raw_inode->chksum = jffs_checksum(raw_inode,
					  sizeof(struct jffs_raw_inode));
	raw_inode->accurate = 0xff;

	D3(printk("jffs_write_node(): About to write this raw inode to the "
		  "flash at pos 0x%ld:\n", (long)pos));
	D3(jffs_print_raw_inode(raw_inode));

	/* Step 1: Write the raw jffs inode to the flash.  */
	if ((err = flash_safe_write(fmc->mtd, pos,
				    (u_char *)raw_inode,
				    sizeof(struct jffs_raw_inode))) < 0) {
		jffs_fmfree_partly(fmc, fm,
				   total_name_size + total_data_size);
               printk(KERN_ERR "JFFS: jffs_write_node: Failed to write "
                      "raw_inode.\n");
		return err;
	}
	pos += sizeof(struct jffs_raw_inode);

	/* Step 2: Write the name, if there is any.  */
	if (raw_inode->nsize) {
		if ((err = flash_safe_write(fmc->mtd, pos,
                                 	    (u_char *)name,
					    raw_inode->nsize)) < 0) {
			jffs_fmfree_partly(fmc, fm, total_data_size);
			printk(KERN_ERR "JFFS: jffs_write_node: Failed to "
                              "write the name.\n");
			return err;
		}
		pos += total_name_size;
	}

	/* Step 3: Append the actual data, if any.  */
	if (raw_inode->dsize) {
		if ((err = flash_safe_write(fmc->mtd, pos, data,
					    raw_inode->dsize)) < 0) {
			jffs_fmfree_partly(fmc, fm, 0);
                        printk(KERN_ERR "JFFS: jffs_write_node: Failed to "
			       "write the data.\n");
			return err;
		}
	}

	D3(printk("jffs_write_node(): Leaving...\n"));
	return raw_inode->dsize;
} /* jffs_write_node()  */


/* Read data from the node and write it to the buffer.  'node_offset'
   is how much we have read from this particular node before and which
   shouldn't be read again.  'max_size' is how much space there is in
   the buffer.  */
static int
jffs_get_node_data(struct jffs_file *f, struct jffs_node *node, char *buf,
		   __u32 node_offset, __u32 max_size, kdev_t dev)
{
	struct jffs_fmcontrol *fmc = f->c->fmc;
	__u32 pos = node->fm->offset + node->fm_offset + node_offset;
	__u32 avail = node->data_size - node_offset;
	__u32 r;

	D2(printk("  jffs_get_node_data(): file: \"%s\", ino: %u, "
		  "version: %u, node_offset: %u\n",
		  f->name, node->ino, node->version, node_offset));

	r = jffs_min(avail, max_size);
	flash_safe_read(fmc->mtd, pos, buf, r);

	D3(printk("  jffs_get_node_data(): Read %u byte%s.\n",
		  r, (r == 1 ? "" : "s")));

	return r;
}


/* Read data from the file's nodes.  Write the data to the buffer
   'buf'.  'read_offset' tells how much data we should skip.  */
int
jffs_read_data(struct jffs_file *f, char *buf, __u32 read_offset, __u32 size)
{
	struct jffs_node *node;
	__u32 read_data = 0; /* Total amount of read data.  */
	__u32 node_offset = 0;
	__u32 pos = 0; /* Number of bytes traversed.  */

	D1(printk("jffs_read_data(): file = \"%s\", read_offset = %d, "
		  "size = %u\n",
		  (f->name ? f->name : ""), read_offset, size));

	if (read_offset >= f->size) {
		D(printk("  f->size: %d\n", f->size));
		return 0;
	}

	/* First find the node to read data from.  */
	node = f->range_head;
	while (pos <= read_offset) {
		node_offset = read_offset - pos;
		if (node_offset >= node->data_size) {
			pos += node->data_size;
			node = node->range_next;
		}
		else {
			break;
		}
	}

	/* "Cats are living proof that not everything in nature
	   has to be useful."
	   - Garrison Keilor ('97)  */

	/* Fill the buffer.  */
	while (node && (read_data < size)) {
		int r;
		if (!node->fm) {
			/* This node does not refer to real data.  */
			r = jffs_min(size - read_data,
				     node->data_size - node_offset);
			memset(&buf[read_data], 0, r);
		}
		else if ((r = jffs_get_node_data(f, node, &buf[read_data],
						 node_offset,
						 size - read_data,
						 f->c->sb->s_dev)) < 0) {
			return r;
		}
		read_data += r;
		node_offset = 0;
		node = node->range_next;
	}
	D3(printk("  jffs_read_data(): Read %u bytes.\n", read_data));
	return read_data;
}


/* Used for traversing all nodes in the hash table.  */
int
jffs_foreach_file(struct jffs_control *c, int (*func)(struct jffs_file *))
{
	int pos;
	int r;
	int result = 0;

	for (pos = 0; pos < c->hash_len; pos++) {
		struct list_head *p, *next;
		for (p = c->hash[pos].next; p != &c->hash[pos]; p = next) {
			/* We need a reference to the next file in the
			   list because `func' might remove the current
			   file `f'.  */
			next = p->next;
			r = func(list_entry(p, struct jffs_file, hash));
			if (r < 0)
				return r;
			result += r;
		}
	}

	return result;
}


/* Free all memory associated with a file.  */
int
jffs_free_node_list(struct jffs_file *f)
{
	struct jffs_node *node;
	struct jffs_node *p;

	D3(printk("jffs_free_node_list(): f #%u, \"%s\"\n",
		  f->ino, (f->name ? f->name : "")));
	node = f->version_head;
	while (node) {
		p = node;
		node = node->version_next;
		kfree(p);
		DJM(no_jffs_node--);
	}
	return 0;
}


/* See if a file is deleted. If so, mark that file's nodes as obsolete.  */
int
jffs_possibly_delete_file(struct jffs_file *f)
{
	struct jffs_node *n;

	D3(printk("jffs_possibly_delete_file(): ino: %u\n",
		  f->ino));

	ASSERT(if (!f) {
		printk(KERN_ERR "jffs_possibly_delete_file(): f == NULL\n");
		return -1;
	});

	if (f->deleted) {
		/* First try to remove all older versions.  */
		for (n = f->version_head; n; n = n->version_next) {
			if (!n->fm) {
				continue;
			}
			if (jffs_fmfree(f->c->fmc, n->fm, n) < 0) {
				break;
			}
		}
		/* Unlink the file from the filesystem.  */
		jffs_unlink_file_from_tree(f);
		jffs_unlink_file_from_hash(f);
		jffs_free_node_list(f);
		if (f->name) {
			kfree(f->name);
			DJM(no_name--);
		}
		kfree(f);
		DJM(no_jffs_file--);
	}
	return 0;
}


/* Used in conjunction with jffs_foreach_file() to count the number
   of files in the file system.  */
int
jffs_file_count(struct jffs_file *f)
{
	return 1;
}


/* Build up a file's range list from scratch by going through the
   version list.  */
int
jffs_build_file(struct jffs_file *f)
{
	struct jffs_node *n;

	D3(printk("jffs_build_file(): ino: %u, name: \"%s\"\n",
		  f->ino, (f->name ? f->name : "")));

	for (n = f->version_head; n; n = n->version_next) {
		jffs_update_file(f, n);
	}
	return 0;
}


/* Remove an amount of data from a file. If this amount of data is
   zero, that could mean that a node should be split in two parts.
   We remove or change the appropriate nodes in the lists.

   Starting offset of area to be removed is node->data_offset,
   and the length of the area is in node->removed_size.   */
static void
jffs_delete_data(struct jffs_file *f, struct jffs_node *node)
{
	struct jffs_node *n;
	__u32 offset = node->data_offset;
	__u32 remove_size = node->removed_size;

	D3(printk("jffs_delete_data(): offset = %u, remove_size = %u\n",
		  offset, remove_size));

	if (remove_size == 0
	    && f->range_tail
	    && f->range_tail->data_offset + f->range_tail->data_size
	       == offset) {
		/* A simple append; nothing to remove or no node to split.  */
		return;
	}

	/* Find the node where we should begin the removal.  */
	for (n = f->range_head; n; n = n->range_next) {
		if (n->data_offset + n->data_size > offset) {
			break;
		}
	}
	if (!n) {
		/* If there's no data in the file there's no data to
		   remove either.  */
		return;
	}

	if (n->data_offset > offset) {
		/* XXX: Not implemented yet.  */
		printk(KERN_WARNING "JFFS: An unexpected situation "
		       "occurred in jffs_delete_data.\n");
	}
	else if (n->data_offset < offset) {
		/* See if the node has to be split into two parts.  */
		if (n->data_offset + n->data_size < offset + remove_size) {
			/* Do the split.  */
			struct jffs_node *new_node;
			D3(printk("jffs_delete_data(): Split node with "
				  "version number %u.\n", n->version));

			if (!(new_node = (struct jffs_node *)
					 kmalloc(sizeof(struct jffs_node),
						 GFP_KERNEL))) {
				D(printk("jffs_delete_data(): -ENOMEM\n"));
				return;
			}
			DJM(no_jffs_node++);

			new_node->ino = n->ino;
			new_node->version = n->version;
			new_node->data_offset = offset;
			new_node->data_size = n->data_size
					      - (remove_size
						 + (offset - n->data_offset));
			new_node->fm_offset = n->fm_offset + n->data_size
					      + remove_size;
			new_node->name_size = n->name_size;
			new_node->fm = n->fm;
			new_node->version_prev = n;
			new_node->version_next = n->version_next;
			if (new_node->version_next) {
				new_node->version_next->version_prev
				= new_node;
			}
			else {
				f->version_tail = new_node;
			}
			n->version_next = new_node;
			new_node->range_prev = n;
			new_node->range_next = n->range_next;
			if (new_node->range_next) {
				new_node->range_next->range_prev = new_node;
			}
			else {
				f->range_tail = new_node;
			}
			/* A very interesting can of worms.  */
			n->range_next = new_node;
			n->data_size = offset - n->data_offset;
			jffs_add_node(new_node);
			n = new_node->range_next;
			remove_size = 0;
		}
		else {
			/* No.  No need to split the node.  Just remove
			   the end of the node.  */
			int r = jffs_min(n->data_offset + n->data_size
					 - offset, remove_size);
			n->data_size -= r;
			remove_size -= r;
			n = n->range_next;
		}
	}

	/* Remove as many nodes as necessary.  */
	while (n && remove_size) {
		if (n->data_size <= remove_size) {
			struct jffs_node *p = n;
			remove_size -= n->data_size;
			n = n->range_next;
			D3(printk("jffs_delete_data(): Removing node: "
				  "ino: %u, version: %u\n",
				  p->ino, p->version));
			if (p->fm) {
				jffs_fmfree(f->c->fmc, p->fm, p);
			}
			jffs_unlink_node_from_range_list(f, p);
			jffs_unlink_node_from_version_list(f, p);
			kfree(p);
			DJM(no_jffs_node--);
		}
		else {
			n->data_size -= remove_size;
			n->fm_offset += remove_size;
			n->data_offset -= (node->removed_size - remove_size);
			n = n->range_next;
			break;
		}
	}

	/* Adjust the following nodes' information about offsets etc.  */
	while (n && node->removed_size) {
		n->data_offset -= node->removed_size;
		n = n->range_next;
	}

	f->size -= node->removed_size;
	D3(printk("jffs_delete_data(): f->size = %d\n", f->size));
} /* jffs_delete_data()  */


/* Insert some data into a file.  Prior to the call to this function,
   jffs_delete_data() should be called.  */
static void
jffs_insert_data(struct jffs_file *f, struct jffs_node *node)
{
	D3(printk("jffs_insert_data(): node->data_offset = %u, "
		  "node->data_size = %u, f->size = %u\n",
		  node->data_offset, node->data_size, f->size));

	/* Find the position where we should insert data.  */

	if (node->data_offset == f->size) {
		/* A simple append.  This is the most common operation.  */
		node->range_next = 0;
		node->range_prev = f->range_tail;
		if (node->range_prev) {
			node->range_prev->range_next = node;
		}
		f->range_tail = node;
		f->size += node->data_size;
		if (!f->range_head) {
			f->range_head = node;
		}
	}
	else if (node->data_offset < f->size) {
		/* Trying to insert data into the middle of the file.  This
		   means no problem because jffs_delete_data() has already
		   prepared the range list for us.  */
		struct jffs_node *n;

		/* Find the correct place for the insertion and then insert
		   the node.  */
		for (n = f->range_head; n; n = n->range_next) {
			D1(printk("Cool stuff's happening!\n"));

			if (n->data_offset == node->data_offset) {
				node->range_prev = n->range_prev;
				if (node->range_prev) {
					node->range_prev->range_next = node;
				}
				else {
					f->range_head = node;
				}
				node->range_next = n;
				n->range_prev = node;
				break;
			}
			ASSERT(else if (n->data_offset + n->data_size >
					node->data_offset) {
				printk(KERN_ERR "jffs_insert_data(): "
				       "Couldn't find a place to insert "
				       "the data!\n");
				return;
			});
		}

		/* Adjust later nodes' offsets etc.  */
		n = node->range_next;
		while (n) {
			n->data_offset += node->data_size;
			n = n->range_next;
		}
		f->size += node->data_size;
	}
	else if (node->data_offset > f->size) {
		/* Not implemented yet.  */
#if 0
		/* Below is some example code for future use if we decide
		   to implement it.  */
		/* This is code that isn't supported by VFS. So there aren't
		   really any reasons to implement it yet.  */
		if (!f->range_head) {
			if (node->data_offset > f->size) {
				if (!(nn = jffs_alloc_node())) {
					D(printk("jffs_insert_data(): "
						 "Allocation failed.\n"));
					return;
				}
				nn->version = JFFS_MAGIC_BITMASK;
				nn->data_offset = 0;
				nn->data_size = node->data_offset;
				nn->removed_size = 0;
				nn->fm_offset = 0;
				nn->name_size = 0;
				nn->fm = 0; /* This is a virtual data holder.  */
				nn->version_prev = 0;
				nn->version_next = 0;
				nn->range_prev = 0;
				nn->range_next = 0;
				nh->range_head = nn;
				nh->range_tail = nn;
			}
		}
#endif
	}

	D3(printk("jffs_insert_data(): f->size = %d\n", f->size));
}


/* A new node (with data) has been added to the file and now the range
   list has to be modified.  */
static int
jffs_update_file(struct jffs_file *f, struct jffs_node *node)
{
	D3(printk("jffs_update_file(): ino: %u, version: %u\n",
		  f->ino, node->version));

	if (node->data_size == 0) {
		if (node->removed_size == 0) {
			/* data_offset == X  */
			/* data_size == 0  */
			/* remove_size == 0  */
		}
		else {
			/* data_offset == X  */
			/* data_size == 0  */
			/* remove_size != 0  */
			jffs_delete_data(f, node);
		}
	}
	else {
		/* data_offset == X  */
		/* data_size != 0  */
		/* remove_size == Y  */
		jffs_delete_data(f, node);
		jffs_insert_data(f, node);
	}
	return 0;
}


/* Print the contents of a node.  */
void
jffs_print_node(struct jffs_node *n)
{
	D(printk("jffs_node: 0x%p\n", n));
	D(printk("{\n"));
	D(printk("        0x%08x, /* version  */\n", n->version));
	D(printk("        0x%08x, /* data_offset  */\n", n->data_offset));
	D(printk("        0x%08x, /* data_size  */\n", n->data_size));
	D(printk("        0x%08x, /* removed_size  */\n", n->removed_size));
	D(printk("        0x%08x, /* fm_offset  */\n", n->fm_offset));
	D(printk("        0x%02x,       /* name_size  */\n", n->name_size));
	D(printk("        0x%p, /* fm,  fm->offset: %u  */\n",
		 n->fm, n->fm->offset));
	D(printk("        0x%p, /* version_prev  */\n", n->version_prev));
	D(printk("        0x%p, /* version_next  */\n", n->version_next));
	D(printk("        0x%p, /* range_prev  */\n", n->range_prev));
	D(printk("        0x%p, /* range_next  */\n", n->range_next));
	D(printk("}\n"));
}


/* Print the contents of a raw inode.  */
void
jffs_print_raw_inode(struct jffs_raw_inode *raw_inode)
{
	D(printk("jffs_raw_inode: inode number: %u\n", raw_inode->ino));
	D(printk("{\n"));
	D(printk("        0x%08x, /* magic  */\n", raw_inode->magic));
	D(printk("        0x%08x, /* ino  */\n", raw_inode->ino));
	D(printk("        0x%08x, /* pino  */\n", raw_inode->pino));
	D(printk("        0x%08x, /* version  */\n", raw_inode->version));
	D(printk("        0x%08x, /* mode  */\n", raw_inode->mode));
	D(printk("        0x%04x,     /* uid  */\n", raw_inode->uid));
	D(printk("        0x%04x,     /* gid  */\n", raw_inode->gid));
	D(printk("        0x%08x, /* atime  */\n", raw_inode->atime));
	D(printk("        0x%08x, /* mtime  */\n", raw_inode->mtime));
	D(printk("        0x%08x, /* ctime  */\n", raw_inode->ctime));
	D(printk("        0x%08x, /* offset  */\n", raw_inode->offset));
	D(printk("        0x%08x, /* dsize  */\n", raw_inode->dsize));
	D(printk("        0x%08x, /* rsize  */\n", raw_inode->rsize));
	D(printk("        0x%02x,       /* nsize  */\n", raw_inode->nsize));
	D(printk("        0x%02x,       /* nlink  */\n", raw_inode->nlink));
	D(printk("        0x%02x,       /* spare  */\n",
		 raw_inode->spare));
	D(printk("        %u,          /* rename  */\n",
		 raw_inode->rename));
	D(printk("        %u,          /* deleted  */\n",
		 raw_inode->deleted));
	D(printk("        0x%02x,       /* accurate  */\n",
		 raw_inode->accurate));
	D(printk("        0x%08x, /* dchksum  */\n", raw_inode->dchksum));
	D(printk("        0x%04x,     /* nchksum  */\n", raw_inode->nchksum));
	D(printk("        0x%04x,     /* chksum  */\n", raw_inode->chksum));
	D(printk("}\n"));
}


/* Print the contents of a file.  */
int
jffs_print_file(struct jffs_file *f)
{
	D(int i);
	D(printk("jffs_file: 0x%p\n", f));
	D(printk("{\n"));
	D(printk("        0x%08x, /* ino  */\n", f->ino));
	D(printk("        0x%08x, /* pino  */\n", f->pino));
	D(printk("        0x%08x, /* mode  */\n", f->mode));
	D(printk("        0x%04x,     /* uid  */\n", f->uid));
	D(printk("        0x%04x,     /* gid  */\n", f->gid));
	D(printk("        0x%08x, /* atime  */\n", f->atime));
	D(printk("        0x%08x, /* mtime  */\n", f->mtime));
	D(printk("        0x%08x, /* ctime  */\n", f->ctime));
	D(printk("        0x%02x,       /* nsize  */\n", f->nsize));
	D(printk("        0x%02x,       /* nlink  */\n", f->nlink));
	D(printk("        0x%02x,       /* deleted  */\n", f->deleted));
	D(printk("        \"%s\", ", (f->name ? f->name : "")));
	D(for (i = strlen(f->name ? f->name : ""); i < 8; ++i) {
		printk(" ");
	});
	D(printk("/* name  */\n"));
	D(printk("        0x%08x, /* size  */\n", f->size));
	D(printk("        0x%08x, /* highest_version  */\n",
		 f->highest_version));
	D(printk("        0x%p, /* c  */\n", f->c));
	D(printk("        0x%p, /* parent  */\n", f->parent));
	D(printk("        0x%p, /* children  */\n", f->children));
	D(printk("        0x%p, /* sibling_prev  */\n", f->sibling_prev));
	D(printk("        0x%p, /* sibling_next  */\n", f->sibling_next));
	D(printk("        0x%p, /* hash_prev  */\n", f->hash.prev));
	D(printk("        0x%p, /* hash_next  */\n", f->hash.next));
	D(printk("        0x%p, /* range_head  */\n", f->range_head));
	D(printk("        0x%p, /* range_tail  */\n", f->range_tail));
	D(printk("        0x%p, /* version_head  */\n", f->version_head));
	D(printk("        0x%p, /* version_tail  */\n", f->version_tail));
	D(printk("}\n"));
	return 0;
}


void
jffs_print_hash_table(struct jffs_control *c)
{
	int i;

	printk("JFFS: Dumping the file system's hash table...\n");
	for (i = 0; i < c->hash_len; i++) {
		struct list_head *p;
		for (p = c->hash[i].next; p != &c->hash[i]; p = p->next) {
			struct jffs_file *f=list_entry(p,struct jffs_file,hash);
			printk("*** c->hash[%u]: \"%s\" "
			       "(ino: %u, pino: %u)\n",
			       i, (f->name ? f->name : ""),
			       f->ino, f->pino);
		}
	}
}


void
jffs_print_tree(struct jffs_file *first_file, int indent)
{
	struct jffs_file *f;
	char *space;

	if (!first_file) {
		return;
	}

	if (!(space = (char *) kmalloc(indent + 1, GFP_KERNEL))) {
		printk("jffs_print_tree(): Out of memory!\n");
		return;
	}

	memset(space, ' ', indent);
	space[indent] = '\0';

	for (f = first_file; f; f = f->sibling_next) {
		printk("%s%s (ino: %u, highest_version: %u, size: %u)\n",
		       space, (f->name ? f->name : "/"),
		       f->ino, f->highest_version, f->size);
		if (S_ISDIR(f->mode)) {
			jffs_print_tree(f->children, indent + 2);
		}
	}

	kfree(space);
}


#if defined(JFFS_MEMORY_DEBUG) && JFFS_MEMORY_DEBUG
void
jffs_print_memory_allocation_statistics(void)
{
	static long printout = 0;
	printk("________ Memory printout #%ld ________\n", ++printout);
	printk("no_jffs_file = %ld\n", no_jffs_file);
	printk("no_jffs_node = %ld\n", no_jffs_node);
	printk("no_jffs_control = %ld\n", no_jffs_control);
	printk("no_jffs_raw_inode = %ld\n", no_jffs_raw_inode);
	printk("no_jffs_node_ref = %ld\n", no_jffs_node_ref);
	printk("no_jffs_fm = %ld\n", no_jffs_fm);
	printk("no_jffs_fmcontrol = %ld\n", no_jffs_fmcontrol);
	printk("no_hash = %ld\n", no_hash);
	printk("no_name = %ld\n", no_name);
	printk("\n");
}
#endif


/* Rewrite `size' bytes, and begin at `node'.  */
int
jffs_rewrite_data(struct jffs_file *f, struct jffs_node *node, int size)
{
	struct jffs_control *c = f->c;
	struct jffs_fmcontrol *fmc = c->fmc;
	struct jffs_raw_inode raw_inode;
	struct jffs_node *new_node;
	struct jffs_fm *fm;
	__u32 pos;
	__u32 pos_dchksum;
	__u32 total_name_size;
	__u32 total_data_size;
	__u32 total_size;
	int err;

	D1(printk("***jffs_rewrite_data(): node: %u, name: \"%s\", size: %u\n",
		  f->ino, (f->name ? f->name : ""), size));

	/* Create and initialize the new node.  */
	if (!(new_node = (struct jffs_node *)
			 kmalloc(sizeof(struct jffs_node), GFP_KERNEL))) {
		D(printk("jffs_rewrite_data(): "
			 "Failed to allocate node.\n"));
		return -ENOMEM;
	}
	DJM(no_jffs_node++);
	new_node->data_offset = node->data_offset;
	new_node->data_size = size;
	new_node->removed_size = size;
	total_name_size = f->nsize + JFFS_GET_PAD_BYTES(f->nsize);
	total_data_size = size + JFFS_GET_PAD_BYTES(size);
	total_size = sizeof(struct jffs_raw_inode)
		     + total_name_size + total_data_size;
	new_node->fm_offset = sizeof(struct jffs_raw_inode)
			      + total_name_size;

	if ((err = jffs_fmalloc(fmc, total_size, new_node, &fm)) < 0) {
		D(printk("jffs_rewrite_data(): Failed to allocate fm.\n"));
		kfree(new_node);
		DJM(no_jffs_node--);
		return err;
	}
	else if (!fm->nodes) {
		/* The jffs_fm struct that we got is not good enough.  */
		if ((err = jffs_write_dummy_node(c, fm)) < 0) {
			D(printk("jffs_rewrite_data(): "
				 "jffs_write_dummy_node() Failed!\n"));
			kfree(fm);
			DJM(no_jffs_fm--);
			return err;
		}
		/* Get a new one.  */
		if ((err = jffs_fmalloc(fmc, total_size, node, &fm)) < 0) {
			D(printk("jffs_rewrite_data(): Second "
				 "jffs_fmalloc(0x%p, %u) failed!\n",
				 fmc, total_size));
			return err;
		}
	}
	new_node->fm = fm;

	ASSERT(if (new_node->fm->nodes == 0) {
		printk(KERN_ERR "jffs_rewrite_data(): "
		       "new_node->fm->nodes == 0\n");
	});

	/* Initialize the raw inode.  */
	raw_inode.magic = JFFS_MAGIC_BITMASK;
	raw_inode.ino = f->ino;
	raw_inode.pino = f->pino;
	raw_inode.version = f->highest_version + 1;
	raw_inode.mode = f->mode;
	raw_inode.uid = f->uid;
	raw_inode.gid = f->gid;
	raw_inode.atime = f->atime;
	raw_inode.mtime = f->mtime;
	raw_inode.ctime = f->ctime;
	raw_inode.offset = node->data_offset;
	raw_inode.dsize = size;
	raw_inode.rsize = size;
	raw_inode.nsize = f->nsize;
	raw_inode.nlink = f->nlink;
	raw_inode.spare = 0;
	raw_inode.rename = 0;
	raw_inode.deleted = 0;
	raw_inode.accurate = 0xff;
	raw_inode.dchksum = 0;
	raw_inode.nchksum = 0;

	pos = new_node->fm->offset;
	pos_dchksum = pos +JFFS_RAW_INODE_DCHKSUM_OFFSET;

	D3(printk("jffs_rewrite_data(): Writing this raw inode "
		  "to pos 0x%ul.\n", pos));
	D3(jffs_print_raw_inode(&raw_inode));

	if ((err = flash_safe_write(fmc->mtd, pos,
				    (u_char *) &raw_inode,
				    sizeof(struct jffs_raw_inode)
				    - sizeof(__u32)
				    - sizeof(__u16) - sizeof(__u16))) < 0) {
                printk(KERN_ERR "JFFS: jffs_rewrite_data: Write error during "
                         "rewrite. (raw inode)\n");
		jffs_fmfree_partly(fmc, fm,
				   total_name_size + total_data_size);
		return err;
	}
	pos += sizeof(struct jffs_raw_inode);

	/* Write the name to the flash memory.  */
	if (f->nsize) {
		D3(printk("jffs_rewrite_data(): Writing name \"%s\" to "
			  "pos 0x%ul.\n", f->name, (long)pos));
		if ((err = flash_safe_write(fmc->mtd, pos,
					    (u_char *)f->name,
					    f->nsize)) < 0) {
                        printk(KERN_ERR "JFFS: jffs_rewrite_data: Write "
                               "error during rewrite. (name)\n");
			jffs_fmfree_partly(fmc, fm, total_data_size);
			return err;
		}
		pos += total_name_size;
		raw_inode.nchksum = jffs_checksum(f->name, f->nsize);
	}

	/* Write the data.  */
	if (size) {
		int r;
		unsigned char *page;
		__u32 offset = node->data_offset;

		if (!(page = (unsigned char *)__get_free_page(GFP_KERNEL))) {
			jffs_fmfree_partly(fmc, fm, 0);
			return -1;
		}

		while (size) {
			__u32 s = jffs_min(size, PAGE_SIZE);
			if ((r = jffs_read_data(f, (char *)page,
						offset, s)) < s) {
                                printk(KERN_ERR "JFFS: jffs_rewrite_data: "
					 "jffs_read_data() "
					 "failed! (r = %d)\n", r);
				jffs_fmfree_partly(fmc, fm, 0);
				return -1;
			}
			if ((err = flash_safe_write(fmc->mtd,
						    pos, page, r)) < 0) {
                                printk(KERN_ERR "JFFS: jffs_rewrite_data: "
                                       "Write error during rewrite. "
                                       "(data)\n");
				free_page((unsigned long)page);
				jffs_fmfree_partly(fmc, fm, 0);
				return err;
			}
			pos += r;
			size -= r;
			offset += r;
			raw_inode.dchksum += jffs_checksum(page, r);
		}

	        free_page((unsigned long)page);
	}

	raw_inode.accurate = 0;
	raw_inode.chksum = jffs_checksum(&raw_inode,
					 sizeof(struct jffs_raw_inode)
					 - sizeof(__u16));

	/* Add the checksum.  */
	if ((err
	     = flash_safe_write(fmc->mtd, pos_dchksum,
				&((u_char *)
				&raw_inode)[JFFS_RAW_INODE_DCHKSUM_OFFSET],
				sizeof(__u32) + sizeof(__u16)
				+ sizeof(__u16))) < 0) {
                printk(KERN_ERR "JFFS: jffs_rewrite_data: Write error during "
                         "rewrite. (checksum)\n");
		jffs_fmfree_partly(fmc, fm, 0);
		return err;
	}

	/* Now make the file system aware of the newly written node.  */
	jffs_insert_node(c, f, &raw_inode, f->name, new_node);

	D3(printk("jffs_rewrite_data(): Leaving...\n"));
	return 0;
} /* jffs_rewrite_data()  */


/* jffs_garbage_collect_next implements one step in the garbage collect
   process and is often called multiple times at each occasion of a
   garbage collect.  */
int
jffs_garbage_collect_next(struct jffs_control *c)
{
	struct jffs_fmcontrol *fmc = c->fmc;
	struct jffs_node *node;
	struct jffs_file *f;
	int size;
        int data_size;
        int total_name_size;
	int free_size = fmc->flash_size	- (fmc->used_size + fmc->dirty_size);
	__u32 free_chunk_size1 = jffs_free_size1(fmc);
	D2(__u32 free_chunk_size2 = jffs_free_size2(fmc));

	/* Get the oldest node in the flash.  */
	node = jffs_get_oldest_node(fmc);
	ASSERT(if (!node) {
                printk(KERN_ERR "JFFS: jffs_garbage_collect_next: "
                       "No oldest node found!\n");
		return -1;
	});

	/* Find its corresponding file too.  */
	f = jffs_find_file(c, node->ino);
	ASSERT(if (!f) {
                printk(KERN_ERR "JFFS: jffs_garbage_collect_next: "
                       "No file to garbage collect! "
                       "(ino = 0x%08x)\n", node->ino);
		return -1;
	});

	D1(printk("jffs_garbage_collect_next(): \"%s\", "
		  "ino: %u, version: %u\n",
		  (f->name ? f->name : ""), node->ino, node->version));

	/* Compute how much we want to rewrite at the moment.  */
        data_size = f->size - node->data_offset;
        total_name_size = f->nsize + JFFS_GET_PAD_BYTES(f->nsize);
        size = sizeof(struct jffs_raw_inode) + total_name_size
               + data_size + JFFS_GET_PAD_BYTES(data_size);

        D2(printk("  total_name_size: %u\n", total_name_size));
        D2(printk("  data_size: %u\n", data_size));
	D2(printk("  size: %u\n", size));
	D2(printk("  f->nsize: %u\n", f->nsize));
	D2(printk("  f->size: %u\n", f->size));
	D2(printk("  free_chunk_size1: %u\n", free_chunk_size1));
	D2(printk("  free_chunk_size2: %u\n", free_chunk_size2));

	if (size > fmc->max_chunk_size) {
		size = fmc->max_chunk_size;
                data_size = size - sizeof(struct jffs_raw_inode)
                            - total_name_size;
	}
	if (size > free_chunk_size1) {

		if (free_chunk_size1 <
		    (sizeof(struct jffs_raw_inode) + f->nsize + BLOCK_SIZE)) {
			/* The space left is too small to be of any
			   use really.  */
			struct jffs_fm *dirty_fm
			= jffs_fmalloced(fmc,
					 fmc->tail->offset + fmc->tail->size,
					 free_chunk_size1, NULL);
                        if (!dirty_fm) {
                                printk(KERN_ERR "JFFS: "
                                       "jffs_garbage_collect_next: "
                                       "Failed to allocate `dirty' "
                                       "flash memory!\n");
				return -1;
			}
			jffs_write_dummy_node(c, dirty_fm);
			goto jffs_garbage_collect_next_end;
		}

		size = free_chunk_size1;
                data_size = size - sizeof(struct jffs_raw_inode)
                            - total_name_size;
	}

	D2(printk("  size: %u (again)\n", size));

	if (free_size - size < fmc->sector_size) {
		/* Just rewrite that node (or even less).  */
                jffs_rewrite_data(f, node,
                                  jffs_min(node->data_size, data_size));
	}
	else {
		size -= (sizeof(struct jffs_raw_inode) + f->nsize);
                jffs_rewrite_data(f, node, data_size);
	}

jffs_garbage_collect_next_end:
        D3(printk("jffs_garbage_collect_next: Leaving...\n"));
	return 0;
} /* jffs_garbage_collect_next */


/* If an obsolete node is partly going to be erased due to garbage
   collection, the part that isn't going to be erased must be filled
   with zeroes so that the scan of the flash will work smoothly next
   time.
     There are two phases in this procedure: First, the clearing of
   the name and data parts of the node. Second, possibly also clearing
   a part of the raw inode as well.  If the box is power cycled during
   the first phase, only the checksum of this node-to-be-cleared-at-
   the-end will be wrong.  If the box is power cycled during, or after,
   the clearing of the raw inode, the information like the length of
   the name and data parts are zeroed.  The next time the box is
   powered up, the scanning algorithm manages this faulty data too
   because:

   - The checksum is invalid and thus the raw inode must be discarded
     in any case.
   - If the lengths of the data part or the name part are zeroed, the
     scanning just continues after the raw inode.  But after the inode
     the scanning procedure just finds zeroes which is the same as
     dirt.

   So, in the end, this could never fail. :-)  Even if it does fail,
   the scanning algorithm should manage that too.  */

static int
jffs_clear_end_of_node(struct jffs_control *c, __u32 erase_size)
{
	struct jffs_fm *fm;
	struct jffs_fmcontrol *fmc = c->fmc;
	__u32 zero_offset;
	__u32 zero_size;
	__u32 zero_offset_data;
	__u32 zero_size_data;
	__u32 cutting_raw_inode = 0;

	if (!(fm = jffs_cut_node(fmc, erase_size))) {
		D3(printk("jffs_clear_end_of_node(): fm == NULL\n"));
		return 0;
	}

	/* Where and how much shall we clear?  */
	zero_offset = fmc->head->offset + erase_size;
	zero_size = fm->offset + fm->size - zero_offset;

	/* Do we have to clear the raw_inode explicitly?  */
	if (fm->size - zero_size < sizeof(struct jffs_raw_inode)) {
		cutting_raw_inode = sizeof(struct jffs_raw_inode)
				    - (fm->size - zero_size);
	}

	/* First, clear the name and data fields.  */
	zero_offset_data = zero_offset + cutting_raw_inode;
	zero_size_data = zero_size - cutting_raw_inode;
	flash_safe_acquire(fmc->mtd);
	flash_memset(fmc->mtd, zero_offset_data, 0, zero_size_data);
	flash_safe_release(fmc->mtd);

	/* Should we clear a part of the raw inode?  */
	if (cutting_raw_inode) {
		/* I guess it is ok to clear the raw inode in this order.  */
		flash_safe_acquire(fmc->mtd);
		flash_memset(fmc->mtd, zero_offset, 0,
			     cutting_raw_inode);
		flash_safe_release(fmc->mtd);
	}

	return 0;
} /* jffs_clear_end_of_node()  */

/* Try to erase as much as possible of the dirt in the flash memory.  */
long
jffs_try_to_erase(struct jffs_control *c)
{
	struct jffs_fmcontrol *fmc = c->fmc;
	long erase_size;
	int err;
	__u32 offset;

	D3(printk("jffs_try_to_erase()\n"));

	erase_size = jffs_erasable_size(fmc);

	D2(printk("jffs_try_to_erase(): erase_size = %ld\n", erase_size));

        if (erase_size == 0) {
                return 0;
        }
        else if (erase_size < 0) {
                printk(KERN_ERR "JFFS: jffs_try_to_erase: "
                       "jffs_erasable_size returned %ld.\n", erase_size);
		return erase_size;
	}

        if ((err = jffs_clear_end_of_node(c, erase_size)) < 0) {
                printk(KERN_ERR "JFFS: jffs_try_to_erase: "
                       "Clearing of node failed.\n");
                return err;
	}

	offset = fmc->head->offset - fmc->flash_start;

	/* Now, let's try to do the erase.  */
	if ((err = flash_erase_region(fmc->mtd,
				      offset, erase_size)) < 0) {
		printk(KERN_ERR "JFFS: Erase of flash failed. "
		       "offset = %u, erase_size = %ld\n",
		       offset, erase_size);
		/* XXX: Here we should allocate this area as dirty
		   with jffs_fmalloced or something similar.  Now
		   we just report the error.  */
		return err;
	}

#if 0
	/* Check if the erased sectors really got erased.  */
	{
		__u32 pos;
		__u32 end;

		pos = (__u32)flash_get_direct_pointer(c->sb->s_dev, offset);
		end = pos + erase_size;

		D2(printk("JFFS: Checking erased sector(s)...\n"));

		flash_safe_acquire(fmc->mtd);

		for (; pos < end; pos += 4) {
			if (*(__u32 *)pos != JFFS_EMPTY_BITMASK) {
				printk("JFFS: Erase failed! pos = 0x%ld\n",
				       (long)pos);
				jffs_hexdump(fmc->mtd, pos,
					     jffs_min(256, end - pos));
				err = -1;
				break;
			}
		}

		flash_safe_release(fmc->mtd);

		if (!err) {
			D2(printk("JFFS: Erase succeeded.\n"));
		}
		else {
			/* XXX: Here we should allocate the memory
			   with jffs_fmalloced() in order to prevent
			   JFFS from using this area accidentally.  */
			return err;
		}
	}
#endif

	/* Update the flash memory data structures.  */
	jffs_sync_erase(fmc, erase_size);

	return erase_size;
}


/* There are different criteria that should trigger a garbage collect:
   
   1. There is too much dirt in the memory.
   2. The free space is becoming small.
   3. There are many versions of a node.

   The garbage collect should always be done in a manner that guarantees
   that future garbage collects cannot be locked.  E.g. Rewritten chunks
   should not be too large (span more than one sector in the flash memory
   for exemple).  Of course there is a limit on how intelligent this garbage
   collection can be.  */
int
jffs_garbage_collect(struct jffs_control *c)
{
	struct jffs_fmcontrol *fmc = c->fmc;
	long erased_total = 0;
	long erased;
	int result = 0;
	D1(int i = 1);

	D2(printk("***jffs_garbage_collect(): fmc->dirty_size = %u\n",
		  fmc->dirty_size));
	D2(jffs_print_fmcontrol(fmc));

	c->fmc->no_call_gc = 1;

	/* While there is too much dirt left and it is possible
	   to garbage collect, do so.  */

	while (fmc->dirty_size >= fmc->sector_size) {

		D1(printk("***jffs_garbage_collect(): round #%u, "
			 "fmc->dirty_size = %u\n", i++, fmc->dirty_size));
		D2(jffs_print_fmcontrol(fmc));

		/* At least one sector should be able to free now.  */
		if ((erased = jffs_try_to_erase(c)) < 0) {
			printk(KERN_WARNING "JFFS: Error in "
			       "garbage collector.\n");
			result = erased;
			goto gc_end;
		}
		else if (erased == 0) {
			__u32 free_size = fmc->flash_size
					  - (fmc->used_size
					     + fmc->dirty_size);

			if (free_size > 0) {
				/* Let's dare to make a garbage collect.  */
				if ((result = jffs_garbage_collect_next(c))
				    < 0) {
					printk(KERN_ERR "JFFS: Something "
					       "has gone seriously wrong "
					       "with a garbage collect.\n");
					goto gc_end;
				}
			}
			else {
				/* What should we do here?  */
				D(printk("   jffs_garbage_collect(): "
					 "erased: %ld, free_size: %u\n",
					 erased, free_size));
				result = -1;
				goto gc_end;
			}
		}

		D1(printk("   jffs_garbage_collect(): erased: %ld\n", erased));
		erased_total += erased;
		DJM(jffs_print_memory_allocation_statistics());
	}


gc_end:
	c->fmc->no_call_gc = 0;

	D3(printk("   jffs_garbage_collect(): Leaving...\n"));
	D1(if (erased_total) {
		printk("erased_total = %ld\n", erased_total);
		jffs_print_fmcontrol(fmc);
	});
	return result;
}
