/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/root.c
 *
 *  Copyright 1997 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/param.h>
#include "autofs_i.h"

static int autofs_root_readdir(struct inode *,struct file *,void *,filldir_t);
static int autofs_root_lookup(struct inode *,struct qstr *,struct inode **);
static int autofs_root_symlink(struct inode *,struct dentry *,const char *);
static int autofs_root_unlink(struct inode *,struct dentry *);
static int autofs_root_rmdir(struct inode *,struct dentry *);
static int autofs_root_mkdir(struct inode *,struct dentry *,int);
static int autofs_root_ioctl(struct inode *, struct file *,unsigned int,unsigned long);

static struct file_operations autofs_root_operations = {
        NULL,                   /* lseek */
        NULL,                   /* read */
        NULL,                   /* write */
        autofs_root_readdir,    /* readdir */
        NULL,                   /* select */
        autofs_root_ioctl,	/* ioctl */
        NULL,                   /* mmap */
        NULL,                   /* open */
        NULL,                   /* release */
        NULL                    /* fsync */
};

struct inode_operations autofs_root_inode_operations = {
        &autofs_root_operations, /* file operations */
        NULL,                   /* create */
        autofs_root_lookup,     /* lookup */
        NULL,                   /* link */
        autofs_root_unlink,     /* unlink */
        autofs_root_symlink,    /* symlink */
        autofs_root_mkdir,      /* mkdir */
        autofs_root_rmdir,      /* rmdir */
        NULL,                   /* mknod */
        NULL,                   /* rename */
        NULL,                   /* readlink */
        NULL,                   /* follow_link */
        NULL,                   /* readpage */
        NULL,                   /* writepage */
        NULL,                   /* bmap */
        NULL,                   /* truncate */
        NULL                    /* permission */
};

static int autofs_root_readdir(struct inode *inode, struct file *filp,
			       void *dirent, filldir_t filldir)
{
	struct autofs_dir_ent *ent;
	struct autofs_dirhash *dirhash;
	off_t onr, nr;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	dirhash = &((struct autofs_sb_info *)inode->i_sb->u.generic_sbp)->dirhash;
	nr = filp->f_pos;

	switch(nr)
	{
	case 0:
		if (filldir(dirent, ".", 1, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	default:
		while ( onr = nr, ent = autofs_hash_enum(dirhash,&nr) ) {
			if (filldir(dirent,ent->name,ent->len,onr,ent->ino) < 0)
				return 0;
			filp->f_pos = nr;
		}
		break;
	}

	return 0;
}

static int autofs_root_lookup(struct inode *dir, struct qstr *str, struct inode **result)
{
	struct autofs_sb_info *sbi;
	struct autofs_dir_ent *ent;
	struct inode *res;
	int status, oz_mode;

	DPRINTK(("autofs_root_lookup: name = "));
	autofs_say(str->name,str->len);

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	*result = res = NULL;
	sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;

	oz_mode = autofs_oz_mode(sbi);
	DPRINTK(("autofs_lookup: pid = %u, pgrp = %u, catatonic = %d, oz_mode = %d\n", current->pid, current->pgrp, sbi->catatonic, oz_mode));

	do {
		while ( !(ent = autofs_hash_lookup(&sbi->dirhash,str)) ) {
			DPRINTK(("lookup failed, pid = %u, pgrp = %u\n", current->pid, current->pgrp));
			
			if ( oz_mode )
				return -ENOENT;
			up(&dir->i_sem);
			status = autofs_wait(sbi,str);
			down(&dir->i_sem);
			DPRINTK(("autofs_wait returned %d\n", status));
			if ( status )
				return status;
		}

		DPRINTK(("lookup successful, inode = %08x\n", (unsigned int)ent->ino));

		if (!(res = iget(dir->i_sb,ent->ino))) {
			printk("autofs: iget returned null!\n");
			return -EACCES;
		}
		
		if ( !oz_mode && S_ISDIR(res->i_mode) && i_dentry(res)->d_covers == i_dentry(res)) {
			/* Not a mount point yet, call 1-800-DAEMON */
			DPRINTK(("autofs: waiting on non-mountpoint dir, inode = %lu, pid = %u, pgrp = %u\n", res->i_ino, current->pid, current->pgrp));
			iput(res);
			res = NULL;
			up(&dir->i_sem);
			status = autofs_wait(sbi,str);
			down(&dir->i_sem);
			if ( status )
				return status;
		}
	} while(!res);
	autofs_update_usage(&sbi->dirhash,ent);
	
	*result = res;
	return 0;
}

static int autofs_root_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct autofs_sb_info *sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;
	struct autofs_dirhash *dh = &sbi->dirhash;
	struct autofs_dir_ent *ent;
	unsigned int n;
	int slsize;
	struct autofs_symlink *sl;

	DPRINTK(("autofs_root_symlink: %s <- ", symname));
	autofs_say(name,len);

	if ( !autofs_oz_mode(sbi) )
		return -EPERM;

	if ( autofs_hash_lookup(dh, &dentry->d_name) )
		return -EEXIST;

	n = find_first_zero_bit(sbi->symlink_bitmap,AUTOFS_MAX_SYMLINKS);
	if ( n >= AUTOFS_MAX_SYMLINKS )
		return -ENOSPC;

	set_bit(n,sbi->symlink_bitmap);
	sl = &sbi->symlink[n];
	sl->len = strlen(symname);
	sl->data = kmalloc(slsize = sl->len+1, GFP_KERNEL);
	if ( !sl->data ) {
		clear_bit(n,sbi->symlink_bitmap);
		return -ENOSPC;
	}

	ent = kmalloc(sizeof(struct autofs_dir_ent), GFP_KERNEL);
	if ( !ent ) {
		kfree(sl->data);
		clear_bit(n,sbi->symlink_bitmap);
		return -ENOSPC;
	}

	ent->name = kmalloc(dentry->d_name.len, GFP_KERNEL);
	if ( !ent->name ) {
		kfree(sl->data);
		kfree(ent);
		clear_bit(n,sbi->symlink_bitmap);
		return -ENOSPC;
	}

	memcpy(sl->data,symname,slsize);
	sl->mtime = CURRENT_TIME;

	ent->ino = AUTOFS_FIRST_SYMLINK + n;
	ent->hash = dentry->d_name.hash;
	memcpy(ent->name, dentry->d_name.name,ent->len = dentry->d_name.len);

	autofs_hash_insert(dh,ent);
	d_instantiate(dentry, iget(dir->i_sb,ent->ino));

	return 0;
}

static int autofs_root_unlink(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;
	struct autofs_dirhash *dh = &sbi->dirhash;
	struct autofs_dir_ent *ent;
	unsigned int n;

	if ( !autofs_oz_mode(sbi) )
		return -EPERM;

	ent = autofs_hash_lookup(dh, &dentry->d_name);
	if ( !ent )
		return -ENOENT;

	n = ent->ino - AUTOFS_FIRST_SYMLINK;
	if ( n >= AUTOFS_MAX_SYMLINKS || !test_bit(n,sbi->symlink_bitmap) )
		return -EINVAL;	/* Not a symlink inode, can't unlink */

	autofs_hash_delete(ent);
	clear_bit(n,sbi->symlink_bitmap);
	kfree(sbi->symlink[n].data);
	d_delete(dentry);
	
	return 0;
}

static int autofs_root_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;
	struct autofs_dirhash *dh = &sbi->dirhash;
	struct autofs_dir_ent *ent;

	if ( !autofs_oz_mode(sbi) )
		return -EPERM;

	ent = autofs_hash_lookup(dh, &dentry->d_name);
	if ( !ent )
		return -ENOENT;

	if ( (unsigned int)ent->ino < AUTOFS_FIRST_DIR_INO )
		return -ENOTDIR; /* Not a directory */

	autofs_hash_delete(ent);
	dir->i_nlink--;
	d_delete(dentry);

	return 0;
}

static int autofs_root_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct autofs_sb_info *sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;
	struct autofs_dirhash *dh = &sbi->dirhash;
	struct autofs_dir_ent *ent;

	if ( !autofs_oz_mode(sbi) )
		return -EPERM;

	ent = autofs_hash_lookup(dh, &dentry->d_name);
	if ( ent )
		return -EEXIST;

	if ( sbi->next_dir_ino < AUTOFS_FIRST_DIR_INO ) {
		printk("autofs: Out of inode numbers -- what the heck did you do??\n");
		return -ENOSPC;
	}

	ent = kmalloc(sizeof(struct autofs_dir_ent), GFP_KERNEL);
	if ( !ent )
		return -ENOSPC;

	ent->name = kmalloc(dentry->d_name.len, GFP_KERNEL);
	if ( !ent->name ) {
		kfree(ent);
		return -ENOSPC;
	}

	ent->hash = dentry->d_name.hash;
	memcpy(ent->name, dentry->d_name.name, ent->len = dentry->d_name.len);
	ent->ino = sbi->next_dir_ino++;
	autofs_hash_insert(dh,ent);
	dir->i_nlink++;
	d_instantiate(dentry, iget(dir->i_sb,ent->ino));

	return 0;
}

/* Get/set timeout ioctl() operation */
static inline int autofs_get_set_timeout(struct autofs_sb_info *sbi,
					 unsigned long *p)
{
	int rv;
	unsigned long ntimeout;

#if LINUX_VERSION_CODE < kver(2,1,0)
	if ( (rv = verify_area(VERIFY_WRITE, p, sizeof(unsigned long))) )
		return rv;
	ntimeout = get_user(p);
	put_user(sbi->exp_timeout/HZ, p);
#else
	if ( (rv = get_user(ntimeout, p)) ||
	     (rv = put_user(sbi->exp_timeout/HZ, p)) )
		return rv;
#endif

	if ( ntimeout > ULONG_MAX/HZ )
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
}

/* Return protocol version */
static inline int autofs_get_protover(int *p)
{
#if LINUX_VERSION_CODE < kver(2,1,0)
	int rv;
	if ( (rv = verify_area(VERIFY_WRITE, p, sizeof(int))) )
		return rv;
	put_user(AUTOFS_PROTO_VERSION, p);
	return 0;
#else
	return put_user(AUTOFS_PROTO_VERSION, p);
#endif
}

/* Perform an expiry operation */
static inline int autofs_expire_run(struct autofs_sb_info *sbi,
				    struct autofs_packet_expire *pkt_p)
{
	struct autofs_dir_ent *ent;
	struct autofs_packet_expire pkt;
	struct autofs_dirhash *dh = &(sbi->dirhash);
	
	memset(&pkt,0,sizeof pkt);

	pkt.hdr.proto_version = AUTOFS_PROTO_VERSION;
	pkt.hdr.type = autofs_ptype_expire;

	if ( !sbi->exp_timeout ||
	     !(ent = autofs_expire(dh,sbi->exp_timeout)) )
		return -EAGAIN;

	pkt.len = ent->len;
	memcpy(pkt.name, ent->name, pkt.len);
	pkt.name[pkt.len] = '\0';

	if ( copy_to_user(pkt_p, &pkt, sizeof(struct autofs_packet_expire)) )
		return -EFAULT;
	
	autofs_update_usage(dh,ent);

	return 0;
}

/*
 * ioctl()'s on the root directory is the chief method for the daemon to
 * generate kernel reactions
 */
static int autofs_root_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	struct autofs_sb_info *sbi =
		(struct autofs_sb_info *)inode->i_sb->u.generic_sbp;

	DPRINTK(("autofs_ioctl: cmd = 0x%08x, arg = 0x%08lx, sbi = %p, pgrp = %u\n",cmd,arg,sbi,current->pgrp));

	if ( _IOC_TYPE(cmd) != _IOC_TYPE(AUTOFS_IOC_FIRST) ||
	     _IOC_NR(cmd) - _IOC_NR(AUTOFS_IOC_FIRST) >= AUTOFS_IOC_COUNT )
		return -ENOTTY;
	
	if ( !autofs_oz_mode(sbi) && !fsuser() )
		return -EPERM;
	
	switch(cmd) {
	case AUTOFS_IOC_READY:	/* Wait queue: go ahead and retry */
		return autofs_wait_release(sbi,arg,0);
	case AUTOFS_IOC_FAIL:	/* Wait queue: fail with ENOENT */
		return autofs_wait_release(sbi,arg,-ENOENT);
	case AUTOFS_IOC_CATATONIC: /* Enter catatonic mode (daemon shutdown) */
		autofs_catatonic_mode(sbi);
		return 0;
	case AUTOFS_IOC_PROTOVER: /* Get protocol version */
		return autofs_get_protover((int *)arg);
	case AUTOFS_IOC_SETTIMEOUT:
		return autofs_get_set_timeout(sbi,(unsigned long *)arg);
	case AUTOFS_IOC_EXPIRE:
		return autofs_expire_run(sbi,(struct autofs_packet_expire *)arg);
	default:
		return -ENOSYS;
	}
}
