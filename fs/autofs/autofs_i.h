/* -*- linux-c -*- ------------------------------------------------------- *
 *   
 * linux/fs/autofs/autofs_i.h
 *
 *   Copyright 1997 Transmeta Corporation - All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/* Internal header file for autofs */

#include <linux/auto_fs.h>

/* This is the range of ioctl() numbers we claim as ours */
#define AUTOFS_IOC_FIRST     AUTOFS_IOC_READY
#define AUTOFS_IOC_COUNT     32

#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/wait.h>

#define kver(a,b,c) (((a) << 16) + ((b) << 8) + (c)) 

#if LINUX_VERSION_CODE < kver(2,1,0)

/* Segmentation stuff for pre-2.1 kernels */
#include <asm/segment.h>

static inline int copy_to_user(void *dst, void *src, unsigned long len)
{
	int rv = verify_area(VERIFY_WRITE, dst, len);
	if ( rv )
		return -1;
	memcpy_tofs(dst,src,len);
	return 0;
}

static inline int copy_from_user(void *dst, void *src, unsigned long len)
{
	int rv = verify_area(VERIFY_READ, src, len);
	if ( rv )
		return -1;
	memcpy_fromfs(dst,src,len);
	return 0;
}

#else

/* Segmentation stuff for post-2.1 kernels */
#include <asm/uaccess.h>
#define register_symtab(x)	((void)0)

#endif

#ifdef DEBUG
#define DPRINTK(D) printk D;
#else
#define DPRINTK(D)
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
	/* The following entries are for the expiry system */
	unsigned long last_usage;
	struct autofs_dir_ent *exp_next;
	struct autofs_dir_ent *exp_prev;
};

struct autofs_dirhash {
	struct autofs_dir_ent *h[AUTOFS_HASH_SIZE];
	struct autofs_dir_ent expiry_head;
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

#define AUTOFS_SBI_MAGIC 0x6d4a556d

struct autofs_sb_info {
	u32 magic;
	struct file *pipe;
	pid_t oz_pgrp;
	int catatonic;
	unsigned long exp_timeout;
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

/* Debug the mysteriously disappearing wait list */

#ifdef DEBUG_WAITLIST
#define CHECK_WAITLIST(S,O) autofs_check_waitlist_integrity(S,O)
void autofs_check_waitlist_integrity(struct autofs_sb_info *,char *);
#else
#define CHECK_WAITLIST(S,O)
#endif

/* Hash operations */

autofs_hash_t autofs_hash(const char *,int);
void autofs_initialize_hash(struct autofs_dirhash *);
struct autofs_dir_ent *autofs_hash_lookup(const struct autofs_dirhash *,autofs_hash_t,const char *,int);
void autofs_hash_insert(struct autofs_dirhash *,struct autofs_dir_ent *);
void autofs_hash_delete(struct autofs_dir_ent *);
struct autofs_dir_ent *autofs_hash_enum(const struct autofs_dirhash *,off_t *);
void autofs_hash_nuke(struct autofs_dirhash *);

/* Expiration-handling functions */

void autofs_update_usage(struct autofs_dirhash *,struct autofs_dir_ent *);
struct autofs_dir_ent *autofs_expire(struct autofs_dirhash *,unsigned long);

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
