/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/include/linux/auto_fs.h
 *
 *  Copyright 1997 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */


#ifndef _LINUX_AUTO_FS_H
#define _LINUX_AUTO_FS_H

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/ioctl.h>
#include <asm/types.h>

#define AUTOFS_PROTO_VERSION 2

enum autofs_packet_type {
	autofs_ptype_missing,	/* Missing entry (create wait queue) */
	/* ...need more in the future... */
};

struct autofs_packet_hdr {
	int proto_version;	      /* Protocol version */
	enum autofs_packet_type type; /* Type of packet */
};

struct autofs_packet_missing {
	struct autofs_packet_hdr hdr;
        unsigned long wait_queue_token;
	int len;
	char name[NAME_MAX+1];
};	

#define AUTOFS_IOC_READY     _IO(0x93,0x60)
#define AUTOFS_IOC_FAIL      _IO(0x93,0x61)
#define AUTOFS_IOC_CATATONIC _IO(0x93,0x62)

#ifdef __KERNEL__

#include <linux/wait.h>
#include <linux/sched.h>

#if LINUX_VERSION_CODE < 0x20100

#include <asm/segment.h>
#define copy_to_user	memcpy_tofs
#define copy_from_user	memcpy_fromfs

#else

#include <asm/uaccess.h>
#define register_symtab(x)	do { } while (0)

#endif

#ifndef DPRINTK
#ifdef DEBUG
#define DPRINTK(D) printk D;
#else
#define DPRINTK(D)
#endif
#endif

#define AUTOFS_SUPER_MAGIC 0x0187

/* Structures associated with the root directory hash */

#define AUTOFS_HASH_SIZE 67

typedef u32 autofs_hash_t;	/* Type returned by autofs_hash() */

struct autofs_dir_ent {
	autofs_hash_t hash;
	struct autofs_dir_ent *next;
	struct autofs_dir_ent **back;
	char *name;
	int len;
	ino_t ino;
	time_t expiry;		/* Reserved for use in failed-lookup cache */
};

struct autofs_dirhash {
	struct autofs_dir_ent *h[AUTOFS_HASH_SIZE];
};

struct autofs_wait_queue {
	unsigned long wait_queue_token;
	struct wait_queue *queue;
	struct autofs_wait_queue *next;
	/* We use the following to see what we are waiting for */
	autofs_hash_t hash;
	int len;
	char *name;
	/* This is for status reporting upon return */
	int status;
	int wait_ctr;
};

struct autofs_symlink {
	int len;
	char *data;
	time_t mtime;
};

#define AUTOFS_MAX_SYMLINKS 256

#define AUTOFS_ROOT_INO      1
#define AUTOFS_FIRST_SYMLINK 2
#define AUTOFS_FIRST_DIR_INO (AUTOFS_FIRST_SYMLINK+AUTOFS_MAX_SYMLINKS)

#define AUTOFS_SYMLINK_BITMAP_LEN ((AUTOFS_MAX_SYMLINKS+31)/32)

#ifndef END_OF_TIME
#define END_OF_TIME ((time_t)((unsigned long)((time_t)(~0UL)) >> 1))
#endif

struct autofs_sb_info {
	struct file *pipe;
	pid_t oz_pgrp;
	int catatonic;
	ino_t next_dir_ino;
	struct autofs_wait_queue *queues; /* Wait queue pointer */
	struct autofs_dirhash dirhash; /* Root directory hash */
	struct autofs_symlink symlink[AUTOFS_MAX_SYMLINKS];
	u32 symlink_bitmap[AUTOFS_SYMLINK_BITMAP_LEN];
};

/* autofs_oz_mode(): do we see the man behind the curtain? */
static inline int autofs_oz_mode(struct autofs_sb_info *sbi) {
	return sbi->catatonic || current->pgrp == sbi->oz_pgrp;
}

/* Init function */
int init_autofs_fs(void);

/* Hash operations */

autofs_hash_t autofs_hash(const char *,int);
void autofs_initialize_hash(struct autofs_dirhash *);
struct autofs_dir_ent *autofs_hash_lookup(const struct autofs_dirhash *,autofs_hash_t,const char *,int);
void autofs_hash_insert(struct autofs_dirhash *,struct autofs_dir_ent *);
void autofs_hash_delete(struct autofs_dir_ent *);
struct autofs_dir_ent *autofs_hash_enum(const struct autofs_dirhash *,off_t *);
void autofs_hash_nuke(struct autofs_dirhash *);

/* Operations structures */

extern struct inode_operations autofs_root_inode_operations;
extern struct inode_operations autofs_symlink_inode_operations;
extern struct inode_operations autofs_dir_inode_operations;

/* Initializing function */

struct super_block *autofs_read_super(struct super_block *, void *,int);

/* Queue management functions */

int autofs_wait(struct autofs_sb_info *,autofs_hash_t,const char *,int);
int autofs_wait_release(struct autofs_sb_info *,unsigned long,int);
void autofs_catatonic_mode(struct autofs_sb_info *);

#ifdef DEBUG
void autofs_say(const char *name, int len);
#else
#define autofs_say(n,l)
#endif

#endif /* __KERNEL__ */
#endif /* _LINUX_AUTO_FS_H */
