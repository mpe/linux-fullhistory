/*
 *  NSA Security-Enhanced Linux (SELinux) security module
 *
 *  This file contains the SELinux hook function implementations.
 *
 *  Authors:  Stephen Smalley, <sds@epoch.ncsc.mil>
 *            Chris Vance, <cvance@nai.com>
 *            Wayne Salamon, <wsalamon@nai.com>
 *            James Morris <jmorris@redhat.com>
 *
 *  Copyright (C) 2001,2002 Networks Associates Technology, Inc.
 *  Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *      as published by the Free Software Foundation.
 */

#define XATTR_SECURITY_PREFIX "security."
#define XATTR_SELINUX_SUFFIX "selinux"
#define XATTR_NAME_SELINUX XATTR_SECURITY_PREFIX XATTR_SELINUX_SUFFIX

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/xattr.h>
#include <linux/capability.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/ext2_fs.h>
#include <linux/proc_fs.h>
#include <linux/kd.h>
#include <net/icmp.h>
#include <net/ip.h>		/* for sysctl_local_port_range[] */
#include <net/tcp.h>		/* struct or_callable used in sock_rcv_skb */
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/ioctls.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>	/* for network interface checks */
#include <linux/netlink.h>
#include <linux/tcp.h>
#include <linux/quota.h>
#include <linux/un.h>		/* for Unix socket types */
#include <net/af_unix.h>	/* for Unix socket types */

#include "avc.h"
#include "objsec.h"

#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
int selinux_enforcing = 0;

static int __init enforcing_setup(char *str)
{
	selinux_enforcing = simple_strtol(str,NULL,0);
	return 1;
}
__setup("enforcing=", enforcing_setup);
#endif

#ifdef CONFIG_SECURITY_SELINUX_BOOTPARAM
int selinux_enabled = 1;

static int __init selinux_enabled_setup(char *str)
{
	selinux_enabled = simple_strtol(str, NULL, 0);
	return 1;
}
__setup("selinux=", selinux_enabled_setup);
#endif

/* Original (dummy) security module. */
static struct security_operations *original_ops = NULL;

/* Minimal support for a secondary security module,
   just to allow the use of the dummy or capability modules.
   The owlsm module can alternatively be used as a secondary
   module as long as CONFIG_OWLSM_FD is not enabled. */
static struct security_operations *secondary_ops = NULL;

/* Lists of inode and superblock security structures initialized
   before the policy was loaded. */
static LIST_HEAD(superblock_security_head);
static spinlock_t sb_security_lock = SPIN_LOCK_UNLOCKED;

/* Allocate and free functions for each kind of security blob. */

static int task_alloc_security(struct task_struct *task)
{
	struct task_security_struct *tsec;

	tsec = kmalloc(sizeof(struct task_security_struct), GFP_KERNEL);
	if (!tsec)
		return -ENOMEM;

	memset(tsec, 0, sizeof(struct task_security_struct));
	tsec->magic = SELINUX_MAGIC;
	tsec->task = task;
	tsec->osid = tsec->sid = SECINITSID_UNLABELED;
	task->security = tsec;

	return 0;
}

static void task_free_security(struct task_struct *task)
{
	struct task_security_struct *tsec = task->security;

	if (!tsec || tsec->magic != SELINUX_MAGIC)
		return;

	task->security = NULL;
	kfree(tsec);
}

static int inode_alloc_security(struct inode *inode)
{
	struct task_security_struct *tsec = current->security;
	struct inode_security_struct *isec;

	isec = kmalloc(sizeof(struct inode_security_struct), GFP_KERNEL);
	if (!isec)
		return -ENOMEM;

	memset(isec, 0, sizeof(struct inode_security_struct));
	init_MUTEX(&isec->sem);
	INIT_LIST_HEAD(&isec->list);
	isec->magic = SELINUX_MAGIC;
	isec->inode = inode;
	isec->sid = SECINITSID_UNLABELED;
	isec->sclass = SECCLASS_FILE;
	if (tsec && tsec->magic == SELINUX_MAGIC)
		isec->task_sid = tsec->sid;
	else
		isec->task_sid = SECINITSID_UNLABELED;
	inode->i_security = isec;

	return 0;
}

static void inode_free_security(struct inode *inode)
{
	struct inode_security_struct *isec = inode->i_security;
	struct superblock_security_struct *sbsec = inode->i_sb->s_security;

	if (!isec || isec->magic != SELINUX_MAGIC)
		return;

	spin_lock(&sbsec->isec_lock);
	if (!list_empty(&isec->list))
		list_del_init(&isec->list);
	spin_unlock(&sbsec->isec_lock);

	inode->i_security = NULL;
	kfree(isec);
}

static int file_alloc_security(struct file *file)
{
	struct task_security_struct *tsec = current->security;
	struct file_security_struct *fsec;

	fsec = kmalloc(sizeof(struct file_security_struct), GFP_ATOMIC);
	if (!fsec)
		return -ENOMEM;

	memset(fsec, 0, sizeof(struct file_security_struct));
	fsec->magic = SELINUX_MAGIC;
	fsec->file = file;
	if (tsec && tsec->magic == SELINUX_MAGIC) {
		fsec->sid = tsec->sid;
		fsec->fown_sid = tsec->sid;
	} else {
		fsec->sid = SECINITSID_UNLABELED;
		fsec->fown_sid = SECINITSID_UNLABELED;
	}
	file->f_security = fsec;

	return 0;
}

static void file_free_security(struct file *file)
{
	struct file_security_struct *fsec = file->f_security;

	if (!fsec || fsec->magic != SELINUX_MAGIC)
		return;

	file->f_security = NULL;
	kfree(fsec);
}

static int superblock_alloc_security(struct super_block *sb)
{
	struct superblock_security_struct *sbsec;

	sbsec = kmalloc(sizeof(struct superblock_security_struct), GFP_KERNEL);
	if (!sbsec)
		return -ENOMEM;

	memset(sbsec, 0, sizeof(struct superblock_security_struct));
	init_MUTEX(&sbsec->sem);
	INIT_LIST_HEAD(&sbsec->list);
	INIT_LIST_HEAD(&sbsec->isec_head);
	spin_lock_init(&sbsec->isec_lock);
	sbsec->magic = SELINUX_MAGIC;
	sbsec->sb = sb;
	sbsec->sid = SECINITSID_UNLABELED;
	sb->s_security = sbsec;

	return 0;
}

static void superblock_free_security(struct super_block *sb)
{
	struct superblock_security_struct *sbsec = sb->s_security;

	if (!sbsec || sbsec->magic != SELINUX_MAGIC)
		return;

	spin_lock(&sb_security_lock);
	if (!list_empty(&sbsec->list))
		list_del_init(&sbsec->list);
	spin_unlock(&sb_security_lock);

	sb->s_security = NULL;
	kfree(sbsec);
}

/* The security server must be initialized before
   any labeling or access decisions can be provided. */
extern int ss_initialized;

/* The file system's label must be initialized prior to use. */

static char *labeling_behaviors[5] = {
	"uses xattr",
	"uses transition SIDs",
	"uses task SIDs",
	"uses genfs_contexts",
	"not configured for labeling"
};

static int inode_doinit_with_dentry(struct inode *inode, struct dentry *opt_dentry);

static inline int inode_doinit(struct inode *inode)
{
	return inode_doinit_with_dentry(inode, NULL);
}

static int superblock_doinit(struct super_block *sb)
{
	struct superblock_security_struct *sbsec = sb->s_security;
	struct dentry *root = sb->s_root;
	struct inode *inode = root->d_inode;
	int rc = 0;

	down(&sbsec->sem);
	if (sbsec->initialized)
		goto out;

	if (!ss_initialized) {
		/* Defer initialization until selinux_complete_init,
		   after the initial policy is loaded and the security
		   server is ready to handle calls. */
		spin_lock(&sb_security_lock);
		if (list_empty(&sbsec->list))
			list_add(&sbsec->list, &superblock_security_head);
		spin_unlock(&sb_security_lock);
		goto out;
	}

	/* Determine the labeling behavior to use for this filesystem type. */
	rc = security_fs_use(sb->s_type->name, &sbsec->behavior, &sbsec->sid);
	if (rc) {
		printk(KERN_WARNING "%s:  security_fs_use(%s) returned %d\n",
		       __FUNCTION__, sb->s_type->name, rc);
		goto out;
	}

	if (sbsec->behavior == SECURITY_FS_USE_XATTR) {
		/* Make sure that the xattr handler exists and that no
		   error other than -ENODATA is returned by getxattr on
		   the root directory.  -ENODATA is ok, as this may be
		   the first boot of the SELinux kernel before we have
		   assigned xattr values to the filesystem. */
		if (!inode->i_op->getxattr) {
			printk(KERN_WARNING "SELinux: (dev %s, type %s) has no "
			       "xattr support\n", sb->s_id, sb->s_type->name);
			rc = -EOPNOTSUPP;
			goto out;
		}
		rc = inode->i_op->getxattr(root, XATTR_NAME_SELINUX, NULL, 0);
		if (rc < 0 && rc != -ENODATA) {
			if (rc == -EOPNOTSUPP)
				printk(KERN_WARNING "SELinux: (dev %s, type "
				       "%s) has no security xattr handler\n",
				       sb->s_id, sb->s_type->name);
			else
				printk(KERN_WARNING "SELinux: (dev %s, type "
				       "%s) getxattr errno %d\n", sb->s_id,
				       sb->s_type->name, -rc);
			goto out;
		}
	}

	if (strcmp(sb->s_type->name, "proc") == 0)
		sbsec->proc = 1;

	sbsec->initialized = 1;

	if (sbsec->behavior > ARRAY_SIZE(labeling_behaviors)) {
		printk(KERN_INFO "SELinux: initialized (dev %s, type %s), unknown behavior\n",
		       sb->s_id, sb->s_type->name);
	}
	else {
		printk(KERN_INFO "SELinux: initialized (dev %s, type %s), %s\n",
		       sb->s_id, sb->s_type->name,
		       labeling_behaviors[sbsec->behavior-1]);
	}

	/* Initialize the root inode. */
	rc = inode_doinit_with_dentry(sb->s_root->d_inode, sb->s_root);

	/* Initialize any other inodes associated with the superblock, e.g.
	   inodes created prior to initial policy load or inodes created
	   during get_sb by a pseudo filesystem that directly
	   populates itself. */
	spin_lock(&sbsec->isec_lock);
next_inode:
	if (!list_empty(&sbsec->isec_head)) {
		struct inode_security_struct *isec =
				list_entry(sbsec->isec_head.next,
				           struct inode_security_struct, list);
		struct inode *inode = isec->inode;
		spin_unlock(&sbsec->isec_lock);
		inode = igrab(inode);
		if (inode) {
			inode_doinit(inode);
			iput(inode);
		}
		spin_lock(&sbsec->isec_lock);
		list_del_init(&isec->list);
		goto next_inode;
	}
	spin_unlock(&sbsec->isec_lock);
out:
	up(&sbsec->sem);
	return rc;
}

static inline u16 inode_mode_to_security_class(umode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFSOCK:
		return SECCLASS_SOCK_FILE;
	case S_IFLNK:
		return SECCLASS_LNK_FILE;
	case S_IFREG:
		return SECCLASS_FILE;
	case S_IFBLK:
		return SECCLASS_BLK_FILE;
	case S_IFDIR:
		return SECCLASS_DIR;
	case S_IFCHR:
		return SECCLASS_CHR_FILE;
	case S_IFIFO:
		return SECCLASS_FIFO_FILE;

	}

	return SECCLASS_FILE;
}

static inline u16 socket_type_to_security_class(int family, int type)
{
	switch (family) {
	case PF_UNIX:
		switch (type) {
		case SOCK_STREAM:
			return SECCLASS_UNIX_STREAM_SOCKET;
		case SOCK_DGRAM:
			return SECCLASS_UNIX_DGRAM_SOCKET;
		}
	case PF_INET:
	case PF_INET6:
		switch (type) {
		case SOCK_STREAM:
			return SECCLASS_TCP_SOCKET;
		case SOCK_DGRAM:
			return SECCLASS_UDP_SOCKET;
		case SOCK_RAW:
			return SECCLASS_RAWIP_SOCKET;
		}
	case PF_NETLINK:
		return SECCLASS_NETLINK_SOCKET;
	case PF_PACKET:
		return SECCLASS_PACKET_SOCKET;
	case PF_KEY:
		return SECCLASS_KEY_SOCKET;
	}

	return SECCLASS_SOCKET;
}

#ifdef CONFIG_PROC_FS
static int selinux_proc_get_sid(struct proc_dir_entry *de,
				u16 tclass,
				u32 *sid)
{
	int buflen, rc;
	char *buffer, *path, *end;

	buffer = (char*)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buflen = PAGE_SIZE;
	end = buffer+buflen;
	*--end = '\0';
	buflen--;
	path = end-1;
	*path = '/';
	while (de && de != de->parent) {
		buflen -= de->namelen + 1;
		if (buflen < 0)
			break;
		end -= de->namelen;
		memcpy(end, de->name, de->namelen);
		*--end = '/';
		path = end;
		de = de->parent;
	}
	rc = security_genfs_sid("proc", path, tclass, sid);
	free_page((unsigned long)buffer);
	return rc;
}
#else
static int selinux_proc_get_sid(struct proc_dir_entry *de,
				u16 tclass,
				u32 *sid)
{
	return -EINVAL;
}
#endif

/* The inode's security attributes must be initialized before first use. */
static int inode_doinit_with_dentry(struct inode *inode, struct dentry *opt_dentry)
{
	struct superblock_security_struct *sbsec = NULL;
	struct inode_security_struct *isec = inode->i_security;
	u32 sid;
	struct dentry *dentry;
#define INITCONTEXTLEN 255
	char *context = NULL;
	unsigned len = 0;
	int rc = 0;
	int hold_sem = 0;

	if (isec->initialized)
		goto out;

	down(&isec->sem);
	hold_sem = 1;
	if (isec->initialized)
		goto out;

	sbsec = inode->i_sb->s_security;
	if (!sbsec->initialized) {
		/* Defer initialization until selinux_complete_init,
		   after the initial policy is loaded and the security
		   server is ready to handle calls. */
		spin_lock(&sbsec->isec_lock);
		if (list_empty(&isec->list))
			list_add(&isec->list, &sbsec->isec_head);
		spin_unlock(&sbsec->isec_lock);
		goto out;
	}

	switch (sbsec->behavior) {
	case SECURITY_FS_USE_XATTR:
		if (!inode->i_op->getxattr) {
			isec->sid = SECINITSID_FILE;
			break;
		}

		/* Need a dentry, since the xattr API requires one.
		   Life would be simpler if we could just pass the inode. */
		if (opt_dentry) {
			/* Called from d_instantiate or d_splice_alias. */
			dentry = dget(opt_dentry);
		} else {
			/* Called from selinux_complete_init, try to find a dentry. */
			dentry = d_find_alias(inode);
		}
		if (!dentry) {
			printk(KERN_WARNING "%s:  no dentry for dev=%s "
			       "ino=%ld\n", __FUNCTION__, inode->i_sb->s_id,
			       inode->i_ino);
			goto out;
		}

		len = INITCONTEXTLEN;
		context = kmalloc(len, GFP_KERNEL);
		if (!context) {
			rc = -ENOMEM;
			dput(dentry);
			goto out;
		}
		rc = inode->i_op->getxattr(dentry, XATTR_NAME_SELINUX,
					   context, len);
		if (rc == -ERANGE) {
			/* Need a larger buffer.  Query for the right size. */
			rc = inode->i_op->getxattr(dentry, XATTR_NAME_SELINUX,
						   NULL, 0);
			if (rc < 0) {
				dput(dentry);
				goto out;
			}
			kfree(context);
			len = rc;
			context = kmalloc(len, GFP_KERNEL);
			if (!context) {
				rc = -ENOMEM;
				dput(dentry);
				goto out;
			}
			rc = inode->i_op->getxattr(dentry,
						   XATTR_NAME_SELINUX,
						   context, len);
		}
		dput(dentry);
		if (rc < 0) {
			if (rc != -ENODATA) {
				printk(KERN_WARNING "%s:  getxattr returned "
				       "%d for dev=%s ino=%ld\n", __FUNCTION__,
				       -rc, inode->i_sb->s_id, inode->i_ino);
				kfree(context);
				goto out;
			}
			/* Map ENODATA to the default file SID */
			sid = SECINITSID_FILE;
			rc = 0;
		} else {
			rc = security_context_to_sid(context, rc, &sid);
			if (rc) {
				printk(KERN_WARNING "%s:  context_to_sid(%s) "
				       "returned %d for dev=%s ino=%ld\n",
				       __FUNCTION__, context, -rc,
				       inode->i_sb->s_id, inode->i_ino);
				kfree(context);
				goto out;
			}
		}
		kfree(context);
		isec->sid = sid;
		break;
	case SECURITY_FS_USE_TASK:
		isec->sid = isec->task_sid;
		break;
	case SECURITY_FS_USE_TRANS:
		/* Default to the fs SID. */
		isec->sid = sbsec->sid;

		/* Try to obtain a transition SID. */
		isec->sclass = inode_mode_to_security_class(inode->i_mode);
		rc = security_transition_sid(isec->task_sid,
					     sbsec->sid,
					     isec->sclass,
					     &sid);
		if (rc)
			goto out;
		isec->sid = sid;
		break;
	default:
		/* Default to the fs SID. */
		isec->sid = sbsec->sid;

		if (sbsec->proc) {
			struct proc_inode *proci = PROC_I(inode);
			if (proci->pde) {
				isec->sclass = inode_mode_to_security_class(inode->i_mode);
				rc = selinux_proc_get_sid(proci->pde,
							  isec->sclass,
							  &sid);
				if (rc)
					goto out;
				isec->sid = sid;
			}
		}
		break;
	}

	isec->initialized = 1;

out:
	if (inode->i_sock) {
		struct socket *sock = SOCKET_I(inode);
		if (sock->sk) {
			isec->sclass = socket_type_to_security_class(sock->sk->sk_family,
			                                             sock->sk->sk_type);
		} else {
			isec->sclass = SECCLASS_SOCKET;
		}
	} else {
		isec->sclass = inode_mode_to_security_class(inode->i_mode);
	}

	if (hold_sem)
		up(&isec->sem);
	return rc;
}

/* Convert a Linux signal to an access vector. */
static inline u32 signal_to_av(int sig)
{
	u32 perm = 0;

	switch (sig) {
	case SIGCHLD:
		/* Commonly granted from child to parent. */
		perm = PROCESS__SIGCHLD;
		break;
	case SIGKILL:
		/* Cannot be caught or ignored */
		perm = PROCESS__SIGKILL;
		break;
	case SIGSTOP:
		/* Cannot be caught or ignored */
		perm = PROCESS__SIGSTOP;
		break;
	default:
		/* All other signals. */
		perm = PROCESS__SIGNAL;
		break;
	}

	return perm;
}

/* Check permission betweeen a pair of tasks, e.g. signal checks,
   fork check, ptrace check, etc. */
int task_has_perm(struct task_struct *tsk1,
		  struct task_struct *tsk2,
		  u32 perms)
{
	struct task_security_struct *tsec1, *tsec2;

	tsec1 = tsk1->security;
	tsec2 = tsk2->security;
	return avc_has_perm(tsec1->sid, tsec2->sid,
			    SECCLASS_PROCESS, perms, &tsec2->avcr, NULL);
}

/* Check whether a task is allowed to use a capability. */
int task_has_capability(struct task_struct *tsk,
			int cap)
{
	struct task_security_struct *tsec;
	struct avc_audit_data ad;

	tsec = tsk->security;

	AVC_AUDIT_DATA_INIT(&ad,CAP);
	ad.tsk = tsk;
	ad.u.cap = cap;

	return avc_has_perm(tsec->sid, tsec->sid,
			    SECCLASS_CAPABILITY, CAP_TO_MASK(cap), NULL, &ad);
}

/* Check whether a task is allowed to use a system operation. */
int task_has_system(struct task_struct *tsk,
		    u32 perms)
{
	struct task_security_struct *tsec;

	tsec = tsk->security;

	return avc_has_perm(tsec->sid, SECINITSID_KERNEL,
			    SECCLASS_SYSTEM, perms, NULL, NULL);
}

/* Check whether a task has a particular permission to an inode.
   The 'aeref' parameter is optional and allows other AVC
   entry references to be passed (e.g. the one in the struct file).
   The 'adp' parameter is optional and allows other audit
   data to be passed (e.g. the dentry). */
int inode_has_perm(struct task_struct *tsk,
		   struct inode *inode,
		   u32 perms,
		   struct avc_entry_ref *aeref,
		   struct avc_audit_data *adp)
{
	struct task_security_struct *tsec;
	struct inode_security_struct *isec;
	struct avc_audit_data ad;

	tsec = tsk->security;
	isec = inode->i_security;

	if (!adp) {
		adp = &ad;
		AVC_AUDIT_DATA_INIT(&ad, FS);
		ad.u.fs.inode = inode;
	}

	return avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			    perms, aeref ? aeref : &isec->avcr, adp);
}

/* Same as inode_has_perm, but pass explicit audit data containing
   the dentry to help the auditing code to more easily generate the
   pathname if needed. */
static inline int dentry_has_perm(struct task_struct *tsk,
				  struct vfsmount *mnt,
				  struct dentry *dentry,
				  u32 av)
{
	struct inode *inode = dentry->d_inode;
	struct avc_audit_data ad;
	AVC_AUDIT_DATA_INIT(&ad,FS);
	ad.u.fs.mnt = mnt;
	ad.u.fs.dentry = dentry;
	return inode_has_perm(tsk, inode, av, NULL, &ad);
}

/* Check whether a task can use an open file descriptor to
   access an inode in a given way.  Check access to the
   descriptor itself, and then use dentry_has_perm to
   check a particular permission to the file.
   Access to the descriptor is implicitly granted if it
   has the same SID as the process.  If av is zero, then
   access to the file is not checked, e.g. for cases
   where only the descriptor is affected like seek. */
static inline int file_has_perm(struct task_struct *tsk,
				struct file *file,
				u32 av)
{
	struct task_security_struct *tsec = tsk->security;
	struct file_security_struct *fsec = file->f_security;
	struct vfsmount *mnt = file->f_vfsmnt;
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct avc_audit_data ad;
	int rc;

	AVC_AUDIT_DATA_INIT(&ad, FS);
	ad.u.fs.mnt = mnt;
	ad.u.fs.dentry = dentry;

	if (tsec->sid != fsec->sid) {
		rc = avc_has_perm(tsec->sid, fsec->sid,
				  SECCLASS_FD,
				  FD__USE,
				  &fsec->avcr, &ad);
		if (rc)
			return rc;
	}

	/* av is zero if only checking access to the descriptor. */
	if (av)
		return inode_has_perm(tsk, inode, av, &fsec->inode_avcr, &ad);

	return 0;
}

/* Check whether a task can create a file. */
static int may_create(struct inode *dir,
		      struct dentry *dentry,
		      u16 tclass)
{
	struct task_security_struct *tsec;
	struct inode_security_struct *dsec;
	struct superblock_security_struct *sbsec;
	u32 newsid;
	struct avc_audit_data ad;
	int rc;

	tsec = current->security;
	dsec = dir->i_security;

	AVC_AUDIT_DATA_INIT(&ad, FS);
	ad.u.fs.dentry = dentry;

	rc = avc_has_perm(tsec->sid, dsec->sid, SECCLASS_DIR,
			  DIR__ADD_NAME | DIR__SEARCH,
			  &dsec->avcr, &ad);
	if (rc)
		return rc;

	if (tsec->create_sid) {
		newsid = tsec->create_sid;
	} else {
		rc = security_transition_sid(tsec->sid, dsec->sid, tclass,
					     &newsid);
		if (rc)
			return rc;
	}

	rc = avc_has_perm(tsec->sid, newsid, tclass, FILE__CREATE, NULL, &ad);
	if (rc)
		return rc;

	sbsec = dir->i_sb->s_security;

	return avc_has_perm(newsid, sbsec->sid,
			    SECCLASS_FILESYSTEM,
			    FILESYSTEM__ASSOCIATE, NULL, &ad);
}

#define MAY_LINK   0
#define MAY_UNLINK 1
#define MAY_RMDIR  2

/* Check whether a task can link, unlink, or rmdir a file/directory. */
static int may_link(struct inode *dir,
		    struct dentry *dentry,
		    int kind)

{
	struct task_security_struct *tsec;
	struct inode_security_struct *dsec, *isec;
	struct avc_audit_data ad;
	u32 av;
	int rc;

	tsec = current->security;
	dsec = dir->i_security;
	isec = dentry->d_inode->i_security;

	AVC_AUDIT_DATA_INIT(&ad, FS);
	ad.u.fs.dentry = dentry;

	av = DIR__SEARCH;
	av |= (kind ? DIR__REMOVE_NAME : DIR__ADD_NAME);
	rc = avc_has_perm(tsec->sid, dsec->sid, SECCLASS_DIR,
			  av, &dsec->avcr, &ad);
	if (rc)
		return rc;

	switch (kind) {
	case MAY_LINK:
		av = FILE__LINK;
		break;
	case MAY_UNLINK:
		av = FILE__UNLINK;
		break;
	case MAY_RMDIR:
		av = DIR__RMDIR;
		break;
	default:
		printk(KERN_WARNING "may_link:  unrecognized kind %d\n", kind);
		return 0;
	}

	rc = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			  av, &isec->avcr, &ad);
	return rc;
}

static inline int may_rename(struct inode *old_dir,
			     struct dentry *old_dentry,
			     struct inode *new_dir,
			     struct dentry *new_dentry)
{
	struct task_security_struct *tsec;
	struct inode_security_struct *old_dsec, *new_dsec, *old_isec, *new_isec;
	struct avc_audit_data ad;
	u32 av;
	int old_is_dir, new_is_dir;
	int rc;

	tsec = current->security;
	old_dsec = old_dir->i_security;
	old_isec = old_dentry->d_inode->i_security;
	old_is_dir = S_ISDIR(old_dentry->d_inode->i_mode);
	new_dsec = new_dir->i_security;

	AVC_AUDIT_DATA_INIT(&ad, FS);

	ad.u.fs.dentry = old_dentry;
	rc = avc_has_perm(tsec->sid, old_dsec->sid, SECCLASS_DIR,
			  DIR__REMOVE_NAME | DIR__SEARCH,
			  &old_dsec->avcr, &ad);
	if (rc)
		return rc;
	rc = avc_has_perm(tsec->sid, old_isec->sid,
			  old_isec->sclass,
			  FILE__RENAME,
			  &old_isec->avcr, &ad);
	if (rc)
		return rc;
	if (old_is_dir && new_dir != old_dir) {
		rc = avc_has_perm(tsec->sid, old_isec->sid,
				  old_isec->sclass,
				  DIR__REPARENT,
				  &old_isec->avcr, &ad);
		if (rc)
			return rc;
	}

	ad.u.fs.dentry = new_dentry;
	av = DIR__ADD_NAME | DIR__SEARCH;
	if (new_dentry->d_inode)
		av |= DIR__REMOVE_NAME;
	rc = avc_has_perm(tsec->sid, new_dsec->sid, SECCLASS_DIR,
			  av,&new_dsec->avcr, &ad);
	if (rc)
		return rc;
	if (new_dentry->d_inode) {
		new_isec = new_dentry->d_inode->i_security;
		new_is_dir = S_ISDIR(new_dentry->d_inode->i_mode);
		rc = avc_has_perm(tsec->sid, new_isec->sid,
				  new_isec->sclass,
				  (new_is_dir ? DIR__RMDIR : FILE__UNLINK),
				  &new_isec->avcr, &ad);
		if (rc)
			return rc;
	}

	return 0;
}

/* Check whether a task can perform a filesystem operation. */
int superblock_has_perm(struct task_struct *tsk,
			struct super_block *sb,
			u32 perms,
			struct avc_audit_data *ad)
{
	struct task_security_struct *tsec;
	struct superblock_security_struct *sbsec;

	tsec = tsk->security;
	sbsec = sb->s_security;
	return avc_has_perm(tsec->sid, sbsec->sid, SECCLASS_FILESYSTEM,
			    perms, NULL, ad);
}

/* Convert a Linux mode and permission mask to an access vector. */
static inline u32 file_mask_to_av(int mode, int mask)
{
	u32 av = 0;

	if ((mode & S_IFMT) != S_IFDIR) {
		if (mask & MAY_EXEC)
			av |= FILE__EXECUTE;
		if (mask & MAY_READ)
			av |= FILE__READ;

		if (mask & MAY_APPEND)
			av |= FILE__APPEND;
		else if (mask & MAY_WRITE)
			av |= FILE__WRITE;

	} else {
		if (mask & MAY_EXEC)
			av |= DIR__SEARCH;
		if (mask & MAY_WRITE)
			av |= DIR__WRITE;
		if (mask & MAY_READ)
			av |= DIR__READ;
	}

	return av;
}

/* Convert a Linux file to an access vector. */
static inline u32 file_to_av(struct file *file)
{
	u32 av = 0;

	if (file->f_mode & FMODE_READ)
		av |= FILE__READ;
	if (file->f_mode & FMODE_WRITE) {
		if (file->f_flags & O_APPEND)
			av |= FILE__APPEND;
		else
			av |= FILE__WRITE;
	}

	return av;
}

/* Set an inode's SID to a specified value. */
int inode_security_set_sid(struct inode *inode, u32 sid)
{
	struct inode_security_struct *isec = inode->i_security;

	down(&isec->sem);
	isec->sclass = inode_mode_to_security_class(inode->i_mode);
	isec->sid = sid;
	isec->initialized = 1;
	up(&isec->sem);
	return 0;
}

/* Set the security attributes on a newly created file. */
static int post_create(struct inode *dir,
		       struct dentry *dentry)
{

	struct task_security_struct *tsec;
	struct inode *inode;
	struct inode_security_struct *dsec;
	struct superblock_security_struct *sbsec;
	u32 newsid;
	char *context;
	unsigned int len;
	int rc;

	tsec = current->security;
	dsec = dir->i_security;

	inode = dentry->d_inode;
	if (!inode) {
		/* Some file system types (e.g. NFS) may not instantiate
		   a dentry for all create operations (e.g. symlink),
		   so we have to check to see if the inode is non-NULL. */
		printk(KERN_WARNING "post_create:  no inode, dir (dev=%s, "
		       "ino=%ld)\n", dir->i_sb->s_id, dir->i_ino);
		return 0;
	}

	if (tsec->create_sid) {
		newsid = tsec->create_sid;
	} else {
		rc = security_transition_sid(tsec->sid, dsec->sid,
					     inode_mode_to_security_class(inode->i_mode),
					     &newsid);
		if (rc) {
			printk(KERN_WARNING "post_create:  "
			       "security_transition_sid failed, rc=%d (dev=%s "
			       "ino=%ld)\n",
			       -rc, inode->i_sb->s_id, inode->i_ino);
			return rc;
		}
	}

	rc = inode_security_set_sid(inode, newsid);
	if (rc) {
		printk(KERN_WARNING "post_create:  inode_security_set_sid "
		       "failed, rc=%d (dev=%s ino=%ld)\n",
		       -rc, inode->i_sb->s_id, inode->i_ino);
		return rc;
	}

	sbsec = dir->i_sb->s_security;
	if (!sbsec)
		return 0;

	if (sbsec->behavior == SECURITY_FS_USE_XATTR &&
	    inode->i_op->setxattr) {
		/* Use extended attributes. */
		rc = security_sid_to_context(newsid, &context, &len);
		if (rc) {
			printk(KERN_WARNING "post_create:  sid_to_context "
			       "failed, rc=%d (dev=%s ino=%ld)\n",
			       -rc, inode->i_sb->s_id, inode->i_ino);
			return rc;
		}
		down(&inode->i_sem);
		rc = inode->i_op->setxattr(dentry,
					   XATTR_NAME_SELINUX,
					   context, len, 0);
		up(&inode->i_sem);
		kfree(context);
		if (rc < 0) {
			printk(KERN_WARNING "post_create:  setxattr failed, "
			       "rc=%d (dev=%s ino=%ld)\n",
			       -rc, inode->i_sb->s_id, inode->i_ino);
			return rc;
		}
	}

	return 0;
}


/* Hook functions begin here. */

static int selinux_ptrace(struct task_struct *parent, struct task_struct *child)
{
	int rc;

	rc = secondary_ops->ptrace(parent,child);
	if (rc)
		return rc;

	return task_has_perm(parent, child, PROCESS__PTRACE);
}

static int selinux_capget(struct task_struct *target, kernel_cap_t *effective,
                          kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	int error;

	error = task_has_perm(current, target, PROCESS__GETCAP);
	if (error)
		return error;

	return secondary_ops->capget(target, effective, inheritable, permitted);
}

static int selinux_capset_check(struct task_struct *target, kernel_cap_t *effective,
                                kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	int error;

	error = task_has_perm(current, target, PROCESS__SETCAP);
	if (error)
		return error;

	return secondary_ops->capset_check(target, effective, inheritable, permitted);
}

static void selinux_capset_set(struct task_struct *target, kernel_cap_t *effective,
                               kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	int error;

	error = task_has_perm(current, target, PROCESS__SETCAP);
	if (error)
		return;

	return secondary_ops->capset_set(target, effective, inheritable, permitted);
}

static int selinux_capable(struct task_struct *tsk, int cap)
{
	int rc;

	rc = secondary_ops->capable(tsk, cap);
	if (rc)
		return rc;

	return task_has_capability(tsk,cap);
}

static int selinux_sysctl(ctl_table *table, int op)
{
	int error = 0;
	u32 av;
	struct task_security_struct *tsec;
	u32 tsid;
	int rc;

	tsec = current->security;

	rc = selinux_proc_get_sid(table->de, (op == 001) ?
	                          SECCLASS_DIR : SECCLASS_FILE, &tsid);
	if (rc) {
		/* Default to the well-defined sysctl SID. */
		tsid = SECINITSID_SYSCTL;
	}

	/* The op values are "defined" in sysctl.c, thereby creating
	 * a bad coupling between this module and sysctl.c */
	if(op == 001) {
		error = avc_has_perm(tsec->sid, tsid,
				     SECCLASS_DIR, DIR__SEARCH, NULL, NULL);
	} else {
		av = 0;
		if (op & 004)
			av |= FILE__READ;
		if (op & 002)
			av |= FILE__WRITE;
		if (av)
			error = avc_has_perm(tsec->sid, tsid,
					     SECCLASS_FILE, av, NULL, NULL);
        }

	return error;
}

static int selinux_quotactl(int cmds, int type, int id, struct super_block *sb)
{
	int rc = 0;

	if (!sb)
		return 0;

	switch (cmds) {
		case Q_SYNC:
		case Q_QUOTAON:
		case Q_QUOTAOFF:
	        case Q_SETINFO:
		case Q_SETQUOTA:
			rc = superblock_has_perm(current,
						 sb,
						 FILESYSTEM__QUOTAMOD, NULL);
			break;
	        case Q_GETFMT:
	        case Q_GETINFO:
		case Q_GETQUOTA:
			rc = superblock_has_perm(current,
						 sb,
						 FILESYSTEM__QUOTAGET, NULL);
			break;
		default:
			rc = 0;  /* let the kernel handle invalid cmds */
			break;
	}
	return rc;
}

static int selinux_quota_on(struct file *f)
{
	return file_has_perm(current, f, FILE__QUOTAON);;
}

static int selinux_syslog(int type)
{
	int rc;

	rc = secondary_ops->syslog(type);
	if (rc)
		return rc;

	switch (type) {
		case 3:         /* Read last kernel messages */
			rc = task_has_system(current, SYSTEM__SYSLOG_READ);
			break;
		case 6:         /* Disable logging to console */
		case 7:         /* Enable logging to console */
		case 8:		/* Set level of messages printed to console */
			rc = task_has_system(current, SYSTEM__SYSLOG_CONSOLE);
			break;
		case 0:         /* Close log */
		case 1:         /* Open log */
		case 2:         /* Read from log */
		case 4:         /* Read/clear last kernel messages */
		case 5:         /* Clear ring buffer */
		default:
			rc = task_has_system(current, SYSTEM__SYSLOG_MOD);
			break;
	}
	return rc;
}

/*
 * Check that a process has enough memory to allocate a new virtual
 * mapping. 0 means there is enough memory for the allocation to
 * succeed and -ENOMEM implies there is not.
 *
 * We currently support three overcommit policies, which are set via the
 * vm.overcommit_memory sysctl.  See Documentation/vm/overcommit-acounting
 *
 * Strict overcommit modes added 2002 Feb 26 by Alan Cox.
 * Additional code 2002 Jul 20 by Robert Love.
 */
static int selinux_vm_enough_memory(long pages)
{
	unsigned long free, allowed;
	int rc;
	struct task_security_struct *tsec = current->security;

	vm_acct_memory(pages);

        /*
	 * Sometimes we want to use more memory than we have
	 */
	if (sysctl_overcommit_memory == 1)
		return 0;

	if (sysctl_overcommit_memory == 0) {
		free = get_page_cache_size();
		free += nr_free_pages();
		free += nr_swap_pages;

		/*
		 * Any slabs which are created with the
		 * SLAB_RECLAIM_ACCOUNT flag claim to have contents
		 * which are reclaimable, under pressure.  The dentry
		 * cache and most inode caches should fall into this
		 */
		free += atomic_read(&slab_reclaim_pages);

		/*
		 * Leave the last 3% for privileged processes.
		 * Don't audit the check, as it is applied to all processes
		 * that allocate mappings.
		 */
		rc = secondary_ops->capable(current, CAP_SYS_ADMIN);
		if (!rc) {
			rc = avc_has_perm_noaudit(tsec->sid, tsec->sid,
						  SECCLASS_CAPABILITY,
						  CAP_TO_MASK(CAP_SYS_ADMIN),
						  NULL, NULL);
		}
		if (rc)
			free -= free / 32;

		if (free > pages)
			return 0;
		vm_unacct_memory(pages);
		return -ENOMEM;
	}

	allowed = totalram_pages * sysctl_overcommit_ratio / 100;
	allowed += total_swap_pages;

	if (atomic_read(&vm_committed_space) < allowed)
		return 0;

	vm_unacct_memory(pages);

	return -ENOMEM;
}

static int selinux_netlink_send(struct sk_buff *skb)
{
	if (capable(CAP_NET_ADMIN))
		cap_raise (NETLINK_CB (skb).eff_cap, CAP_NET_ADMIN);
	else
		NETLINK_CB(skb).eff_cap = 0;
	return 0;
}

static int selinux_netlink_recv(struct sk_buff *skb)
{
	if (!cap_raised(NETLINK_CB(skb).eff_cap, CAP_NET_ADMIN))
		return -EPERM;
	return 0;
}

/* binprm security operations */

static int selinux_bprm_alloc_security(struct linux_binprm *bprm)
{
	struct bprm_security_struct *bsec;

	bsec = kmalloc(sizeof(struct bprm_security_struct), GFP_KERNEL);
	if (!bsec)
		return -ENOMEM;

	memset(bsec, 0, sizeof *bsec);
	bsec->magic = SELINUX_MAGIC;
	bsec->bprm = bprm;
	bsec->sid = SECINITSID_UNLABELED;
	bsec->set = 0;

	bprm->security = bsec;
	return 0;
}

static int selinux_bprm_set_security(struct linux_binprm *bprm)
{
	struct task_security_struct *tsec;
	struct inode *inode = bprm->file->f_dentry->d_inode;
	struct inode_security_struct *isec;
	struct bprm_security_struct *bsec;
	u32 newsid;
	struct avc_audit_data ad;
	int rc;

	rc = secondary_ops->bprm_set_security(bprm);
	if (rc)
		return rc;

	bsec = bprm->security;

	if (bsec->set)
		return 0;

	tsec = current->security;
	isec = inode->i_security;

	/* Default to the current task SID. */
	bsec->sid = tsec->sid;

	/* Reset create SID on execve. */
	tsec->create_sid = 0;

	if (tsec->exec_sid) {
		newsid = tsec->exec_sid;
		/* Reset exec SID on execve. */
		tsec->exec_sid = 0;
	} else {
		/* Check for a default transition on this program. */
		rc = security_transition_sid(tsec->sid, isec->sid,
		                             SECCLASS_PROCESS, &newsid);
		if (rc)
			return rc;
	}

	AVC_AUDIT_DATA_INIT(&ad, FS);
	ad.u.fs.mnt = bprm->file->f_vfsmnt;
	ad.u.fs.dentry = bprm->file->f_dentry;

	if (bprm->file->f_vfsmnt->mnt_flags & MNT_NOSUID)
		newsid = tsec->sid;

        if (tsec->sid == newsid) {
		rc = avc_has_perm(tsec->sid, isec->sid,
				  SECCLASS_FILE, FILE__EXECUTE_NO_TRANS,
				  &isec->avcr, &ad);
		if (rc)
			return rc;
	} else {
		/* Check permissions for the transition. */
		rc = avc_has_perm(tsec->sid, newsid,
				  SECCLASS_PROCESS, PROCESS__TRANSITION,
				  NULL,
				  &ad);
		if (rc)
			return rc;

		rc = avc_has_perm(newsid, isec->sid,
				  SECCLASS_FILE, FILE__ENTRYPOINT,
				  &isec->avcr, &ad);
		if (rc)
			return rc;

		/* Set the security field to the new SID. */
		bsec->sid = newsid;
	}

	bsec->set = 1;
	return 0;
}

static int selinux_bprm_check_security (struct linux_binprm *bprm)
{
	return 0;
}


static int selinux_bprm_secureexec (struct linux_binprm *bprm)
{
	struct task_security_struct *tsec = current->security;
	int atsecure = 0;

	if (tsec->osid != tsec->sid) {
		/* Enable secure mode for SIDs transitions unless
		   the noatsecure permission is granted between
		   the two SIDs, i.e. ahp returns 0. */
		atsecure = avc_has_perm(tsec->osid, tsec->sid,
					 SECCLASS_PROCESS,
					 PROCESS__NOATSECURE, NULL, NULL);
	}

	/* Note that we must include the legacy uid/gid test below
	   to retain it, as the new userland will simply use the
	   value passed by AT_SECURE to decide whether to enable
	   secure mode. */
	return ( atsecure || current->euid != current->uid ||
		current->egid != current->gid);
}

static void selinux_bprm_free_security(struct linux_binprm *bprm)
{
	struct bprm_security_struct *bsec = bprm->security;
	bprm->security = NULL;
	kfree(bsec);
}

/* Derived from fs/exec.c:flush_old_files. */
static inline void flush_unauthorized_files(struct files_struct * files)
{
	struct avc_audit_data ad;
	struct file *file;
	long j = -1;

	AVC_AUDIT_DATA_INIT(&ad,FS);

	spin_lock(&files->file_lock);
	for (;;) {
		unsigned long set, i;

		j++;
		i = j * __NFDBITS;
		if (i >= files->max_fds || i >= files->max_fdset)
			break;
		set = files->open_fds->fds_bits[j];
		if (!set)
			continue;
		spin_unlock(&files->file_lock);
		for ( ; set ; i++,set >>= 1) {
			if (set & 1) {
				file = fget(i);
				if (!file)
					continue;
				if (file_has_perm(current,
						  file,
						  file_to_av(file)))
					sys_close(i);
				fput(file);
			}
		}
		spin_lock(&files->file_lock);

	}
	spin_unlock(&files->file_lock);
}

static void selinux_bprm_compute_creds(struct linux_binprm *bprm)
{
	struct task_security_struct *tsec, *psec;
	struct bprm_security_struct *bsec;
	u32 sid;
	struct av_decision avd;
	struct itimerval itimer;
	int rc, i;

	secondary_ops->bprm_compute_creds(bprm);

	tsec = current->security;

	bsec = bprm->security;
	sid = bsec->sid;

	tsec->osid = tsec->sid;
	if (tsec->sid != sid) {
		/* Check for shared state.  If not ok, leave SID
		   unchanged and kill. */
		if ((atomic_read(&current->fs->count) > 1 ||
		     atomic_read(&current->files->count) > 1 ||
		     atomic_read(&current->sighand->count) > 1)) {
			rc = avc_has_perm(tsec->sid, sid,
					  SECCLASS_PROCESS, PROCESS__SHARE,
					  NULL, NULL);
			if (rc) {
				force_sig_specific(SIGKILL, current);
				return;
			}
		}

		/* Check for ptracing, and update the task SID if ok.
		   Otherwise, leave SID unchanged and kill. */
		task_lock(current);
		if (current->ptrace & PT_PTRACED) {
			psec = current->parent->security;
			rc = avc_has_perm_noaudit(psec->sid, sid,
					  SECCLASS_PROCESS, PROCESS__PTRACE,
					  NULL, &avd);
			if (!rc)
				tsec->sid = sid;
			task_unlock(current);
			avc_audit(psec->sid, sid, SECCLASS_PROCESS,
				  PROCESS__PTRACE, &avd, rc, NULL);
			if (rc) {
				force_sig_specific(SIGKILL, current);
				return;
			}
		} else {
			tsec->sid = sid;
			task_unlock(current);
		}

		/* Close files for which the new task SID is not authorized. */
		flush_unauthorized_files(current->files);

		/* Check whether the new SID can inherit signal state
		   from the old SID.  If not, clear itimers to avoid
		   subsequent signal generation and flush and unblock
		   signals. This must occur _after_ the task SID has
                  been updated so that any kill done after the flush
                  will be checked against the new SID. */
		rc = avc_has_perm(tsec->osid, tsec->sid, SECCLASS_PROCESS,
				  PROCESS__SIGINH, NULL, NULL);
		if (rc) {
			memset(&itimer, 0, sizeof itimer);
			for (i = 0; i < 3; i++)
				do_setitimer(i, &itimer, NULL);
			flush_signals(current);
			spin_lock_irq(&current->sighand->siglock);
			flush_signal_handlers(current, 1);
			sigemptyset(&current->blocked);
			recalc_sigpending();
			spin_unlock_irq(&current->sighand->siglock);
		}

		/* Wake up the parent if it is waiting so that it can
		   recheck wait permission to the new task SID. */
		wake_up_interruptible(&current->parent->wait_chldexit);
	}
}

/* superblock security operations */

static int selinux_sb_alloc_security(struct super_block *sb)
{
	return superblock_alloc_security(sb);
}

static void selinux_sb_free_security(struct super_block *sb)
{
	superblock_free_security(sb);
}

static int selinux_sb_kern_mount(struct super_block *sb)
{
	struct avc_audit_data ad;
	int rc;

	rc = superblock_doinit(sb);
	if (rc)
		return rc;

	AVC_AUDIT_DATA_INIT(&ad,FS);
	ad.u.fs.dentry = sb->s_root;
	return superblock_has_perm(current, sb, FILESYSTEM__MOUNT, &ad);
}

static int selinux_sb_statfs(struct super_block *sb)
{
	struct avc_audit_data ad;

	AVC_AUDIT_DATA_INIT(&ad,FS);
	ad.u.fs.dentry = sb->s_root;
	return superblock_has_perm(current, sb, FILESYSTEM__GETATTR, &ad);
}

static int selinux_mount(char * dev_name,
                         struct nameidata *nd,
                         char * type,
                         unsigned long flags,
                         void * data)
{
	if (flags & MS_REMOUNT)
		return superblock_has_perm(current, nd->mnt->mnt_sb,
		                           FILESYSTEM__REMOUNT, NULL);
	else
		return dentry_has_perm(current, nd->mnt, nd->dentry,
		                       FILE__MOUNTON);
}

static int selinux_umount(struct vfsmount *mnt, int flags)
{
	return superblock_has_perm(current,mnt->mnt_sb,
	                           FILESYSTEM__UNMOUNT,NULL);
}

/* inode security operations */

static int selinux_inode_alloc_security(struct inode *inode)
{
	return inode_alloc_security(inode);
}

static void selinux_inode_free_security(struct inode *inode)
{
	inode_free_security(inode);
}

static int selinux_inode_create(struct inode *dir, struct dentry *dentry, int mask)
{
	return may_create(dir, dentry, SECCLASS_FILE);
}

static void selinux_inode_post_create(struct inode *dir, struct dentry *dentry, int mask)
{
	post_create(dir, dentry);
}

static int selinux_inode_link(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry)
{
	int rc;

	rc = secondary_ops->inode_link(old_dentry,dir,new_dentry);
	if (rc)
		return rc;
	return may_link(dir, old_dentry, MAY_LINK);
}

static void selinux_inode_post_link(struct dentry *old_dentry, struct inode *inode, struct dentry *new_dentry)
{
	return;
}

static int selinux_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	return may_link(dir, dentry, MAY_UNLINK);
}

static int selinux_inode_symlink(struct inode *dir, struct dentry *dentry, const char *name)
{
	return may_create(dir, dentry, SECCLASS_LNK_FILE);
}

static void selinux_inode_post_symlink(struct inode *dir, struct dentry *dentry, const char *name)
{
	post_create(dir, dentry);
}

static int selinux_inode_mkdir(struct inode *dir, struct dentry *dentry, int mask)
{
	return may_create(dir, dentry, SECCLASS_DIR);
}

static void selinux_inode_post_mkdir(struct inode *dir, struct dentry *dentry, int mask)
{
	post_create(dir, dentry);
}

static int selinux_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
	return may_link(dir, dentry, MAY_RMDIR);
}

static int selinux_inode_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	return may_create(dir, dentry, inode_mode_to_security_class(mode));
}

static void selinux_inode_post_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	post_create(dir, dentry);
}

static int selinux_inode_rename(struct inode *old_inode, struct dentry *old_dentry,
                                struct inode *new_inode, struct dentry *new_dentry)
{
	return may_rename(old_inode, old_dentry, new_inode, new_dentry);
}

static void selinux_inode_post_rename(struct inode *old_inode, struct dentry *old_dentry,
                                      struct inode *new_inode, struct dentry *new_dentry)
{
	return;
}

static int selinux_inode_readlink(struct dentry *dentry)
{
	return dentry_has_perm(current, NULL, dentry, FILE__READ);
}

static int selinux_inode_follow_link(struct dentry *dentry, struct nameidata *nameidata)
{
	int rc;

	rc = secondary_ops->inode_follow_link(dentry,nameidata);
	if (rc)
		return rc;
	return dentry_has_perm(current, NULL, dentry, FILE__READ);
}

static int selinux_inode_permission(struct inode *inode, int mask,
				    struct nameidata *nd)
{
	if (!mask) {
		/* No permission to check.  Existence test. */
		return 0;
	}

	return inode_has_perm(current, inode,
			       file_mask_to_av(inode->i_mode, mask), NULL, NULL);
}

static int selinux_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
	if (iattr->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID |
			       ATTR_ATIME_SET | ATTR_MTIME_SET))
		return dentry_has_perm(current, NULL, dentry, FILE__SETATTR);

	return dentry_has_perm(current, NULL, dentry, FILE__WRITE);
}

static int selinux_inode_getattr(struct vfsmount *mnt, struct dentry *dentry)
{
	return dentry_has_perm(current, mnt, dentry, FILE__GETATTR);
}

static int selinux_inode_setxattr(struct dentry *dentry, char *name, void *value, size_t size, int flags)
{
	struct task_security_struct *tsec = current->security;
	struct inode *inode = dentry->d_inode;
	struct inode_security_struct *isec = inode->i_security;
	struct superblock_security_struct *sbsec;
	struct avc_audit_data ad;
	u32 newsid;
	int rc = 0;

	if (strcmp(name, XATTR_NAME_SELINUX)) {
		if (!strncmp(name, XATTR_SECURITY_PREFIX,
			     sizeof XATTR_SECURITY_PREFIX - 1) &&
		    !capable(CAP_SYS_ADMIN)) {
			/* A different attribute in the security namespace.
			   Restrict to administrator. */
			return -EPERM;
		}

		/* Not an attribute we recognize, so just check the
		   ordinary setattr permission. */
		return dentry_has_perm(current, NULL, dentry, FILE__SETATTR);
	}

	AVC_AUDIT_DATA_INIT(&ad,FS);
	ad.u.fs.dentry = dentry;

	rc = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			  FILE__RELABELFROM,
			  &isec->avcr, &ad);
	if (rc)
		return rc;

	rc = security_context_to_sid(value, size, &newsid);
	if (rc)
		return rc;

	rc = avc_has_perm(tsec->sid, newsid, isec->sclass,
			  FILE__RELABELTO, NULL, &ad);
	if (rc)
		return rc;

	sbsec = inode->i_sb->s_security;
	if (!sbsec)
		return 0;

	return avc_has_perm(newsid,
			    sbsec->sid,
			    SECCLASS_FILESYSTEM,
			    FILESYSTEM__ASSOCIATE,
			    NULL,
			    &ad);
}

static void selinux_inode_post_setxattr(struct dentry *dentry, char *name,
                                        void *value, size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct inode_security_struct *isec = inode->i_security;
	u32 newsid;
	int rc;

	if (strcmp(name, XATTR_NAME_SELINUX)) {
		/* Not an attribute we recognize, so nothing to do. */
		return;
	}

	rc = security_context_to_sid(value, size, &newsid);
	if (rc) {
		printk(KERN_WARNING "%s:  unable to obtain SID for context "
		       "%s, rc=%d\n", __FUNCTION__, (char*)value, -rc);
		return;
	}

	isec->sid = newsid;
	return;
}

static int selinux_inode_getxattr (struct dentry *dentry, char *name)
{
	return dentry_has_perm(current, NULL, dentry, FILE__GETATTR);
}

static int selinux_inode_listxattr (struct dentry *dentry)
{
	return dentry_has_perm(current, NULL, dentry, FILE__GETATTR);
}

static int selinux_inode_removexattr (struct dentry *dentry, char *name)
{
	if (strcmp(name, XATTR_NAME_SELINUX)) {
		if (!strncmp(name, XATTR_SECURITY_PREFIX,
			     sizeof XATTR_SECURITY_PREFIX - 1) &&
		    !capable(CAP_SYS_ADMIN)) {
			/* A different attribute in the security namespace.
			   Restrict to administrator. */
			return -EPERM;
		}

		/* Not an attribute we recognize, so just check the
		   ordinary setattr permission. Might want a separate
		   permission for removexattr. */
		return dentry_has_perm(current, NULL, dentry, FILE__SETATTR);
	}

	/* No one is allowed to remove a SELinux security label.
	   You can change the label, but all data must be labeled. */
	return -EACCES;
}

static int selinux_inode_getsecurity(struct dentry *dentry, const char *name, void *buffer, size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct inode_security_struct *isec = inode->i_security;
	char *context;
	unsigned len;
	int rc;

	/* Permission check handled by selinux_inode_getxattr hook.*/

	if (strcmp(name, XATTR_SELINUX_SUFFIX))
		return -EOPNOTSUPP;

	rc = security_sid_to_context(isec->sid, &context, &len);
	if (rc)
		return rc;

	if (!buffer || !size) {
		kfree(context);
		return len;
	}
	if (size < len) {
		kfree(context);
		return -ERANGE;
	}
	memcpy(buffer, context, len);
	kfree(context);
	return len;
}

static int selinux_inode_setsecurity(struct dentry *dentry, const char *name,
                                     const void *value, size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct inode_security_struct *isec = inode->i_security;
	u32 newsid;
	int rc;

	if (strcmp(name, XATTR_SELINUX_SUFFIX))
		return -EOPNOTSUPP;

	if (!value || !size)
		return -EACCES;

	rc = security_context_to_sid((void*)value, size, &newsid);
	if (rc)
		return rc;

	isec->sid = newsid;
	return 0;
}

static int selinux_inode_listsecurity(struct dentry *dentry, char *buffer)
{
	const int len = sizeof(XATTR_NAME_SELINUX);
	if (buffer)
		memcpy(buffer, XATTR_NAME_SELINUX, len);
	return len;
}

/* file security operations */

static int selinux_file_permission(struct file *file, int mask)
{
	struct inode *inode = file->f_dentry->d_inode;

	if (!mask) {
		/* No permission to check.  Existence test. */
		return 0;
	}

	/* file_mask_to_av won't add FILE__WRITE if MAY_APPEND is set */
	if ((file->f_flags & O_APPEND) && (mask & MAY_WRITE))
		mask |= MAY_APPEND;

	return file_has_perm(current, file,
			     file_mask_to_av(inode->i_mode, mask));
}

static int selinux_file_alloc_security(struct file *file)
{
	return file_alloc_security(file);
}

static void selinux_file_free_security(struct file *file)
{
	file_free_security(file);
}

static int selinux_file_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	int error = 0;

	switch (cmd) {
		case FIONREAD:
		/* fall through */
		case FIBMAP:
		/* fall through */
		case FIGETBSZ:
		/* fall through */
		case EXT2_IOC_GETFLAGS:
		/* fall through */
		case EXT2_IOC_GETVERSION:
			error = file_has_perm(current, file, FILE__GETATTR);
			break;

		case EXT2_IOC_SETFLAGS:
		/* fall through */
		case EXT2_IOC_SETVERSION:
			error = file_has_perm(current, file, FILE__SETATTR);
			break;

		/* sys_ioctl() checks */
		case FIONBIO:
		/* fall through */
		case FIOASYNC:
			error = file_has_perm(current, file, 0);
			break;

	        case KDSKBENT:
	        case KDSKBSENT:
			error = task_has_capability(current,CAP_SYS_TTY_CONFIG);
			break;

		/* default case assumes that the command will go
		 * to the file's ioctl() function.
		 */
		default:
			error = file_has_perm(current, file, FILE__IOCTL);

	}
	return error;
}

static int selinux_file_mmap(struct file *file, unsigned long prot, unsigned long flags)
{
	u32 av;

	if (file) {
		/* read access is always possible with a mapping */
		av = FILE__READ;

		/* write access only matters if the mapping is shared */
		if ((flags & MAP_TYPE) == MAP_SHARED && (prot & PROT_WRITE))
			av |= FILE__WRITE;

		if (prot & PROT_EXEC)
			av |= FILE__EXECUTE;

		return file_has_perm(current, file, av);
	}
	return 0;
}

static int selinux_file_mprotect(struct vm_area_struct *vma,
				 unsigned long prot)
{
	return selinux_file_mmap(vma->vm_file, prot, vma->vm_flags);
}

static int selinux_file_lock(struct file *file, unsigned int cmd)
{
	return file_has_perm(current, file, FILE__LOCK);
}

static int selinux_file_fcntl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	int err = 0;

	switch (cmd) {
	        case F_SETFL:
			if (!file->f_dentry || !file->f_dentry->d_inode) {
				err = -EINVAL;
				break;
			}

			if ((file->f_flags & O_APPEND) && !(arg & O_APPEND)) {
				err = file_has_perm(current, file,FILE__WRITE);
				break;
			}
			/* fall through */
	        case F_SETOWN:
	        case F_SETSIG:
	        case F_GETFL:
	        case F_GETOWN:
	        case F_GETSIG:
			/* Just check FD__USE permission */
			err = file_has_perm(current, file, 0);
			break;
		case F_GETLK:
		case F_SETLK:
	        case F_SETLKW:
#if BITS_PER_LONG == 32
	        case F_GETLK64:
		case F_SETLK64:
	        case F_SETLKW64:
#endif
			if (!file->f_dentry || !file->f_dentry->d_inode) {
				err = -EINVAL;
				break;
			}
			err = file_has_perm(current, file, FILE__LOCK);
			break;
	}

	return err;
}

static int selinux_file_set_fowner(struct file *file)
{
	struct task_security_struct *tsec;
	struct file_security_struct *fsec;

	tsec = current->security;
	fsec = file->f_security;
	fsec->fown_sid = tsec->sid;

	return 0;
}

static int selinux_file_send_sigiotask(struct task_struct *tsk,
				       struct fown_struct *fown,
				       int fd, int reason)
{
        struct file *file;
	u32 perm;
	struct task_security_struct *tsec;
	struct file_security_struct *fsec;

	/* struct fown_struct is never outside the context of a struct file */
        file = (struct file *)((long)fown - offsetof(struct file,f_owner));

	tsec = tsk->security;
	fsec = file->f_security;

	if (!fown->signum)
		perm = signal_to_av(SIGIO); /* as per send_sigio_to_task */
	else
		perm = signal_to_av(fown->signum);

	return avc_has_perm(fsec->fown_sid, tsec->sid,
			    SECCLASS_PROCESS, perm, NULL, NULL);
}

static int selinux_file_receive(struct file *file)
{
	return file_has_perm(current, file, file_to_av(file));
}

/* task security operations */

static int selinux_task_create(unsigned long clone_flags)
{
	return task_has_perm(current, current, PROCESS__FORK);
}

static int selinux_task_alloc_security(struct task_struct *tsk)
{
	struct task_security_struct *tsec1, *tsec2;
	int rc;

	tsec1 = current->security;

	rc = task_alloc_security(tsk);
	if (rc)
		return rc;
	tsec2 = tsk->security;

	tsec2->osid = tsec1->osid;
	tsec2->sid = tsec1->sid;

	/* Retain the exec and create SIDs across fork */
	tsec2->exec_sid = tsec1->exec_sid;
	tsec2->create_sid = tsec1->create_sid;

	return 0;
}

static void selinux_task_free_security(struct task_struct *tsk)
{
	task_free_security(tsk);
}

static int selinux_task_setuid(uid_t id0, uid_t id1, uid_t id2, int flags)
{
	/* Since setuid only affects the current process, and
	   since the SELinux controls are not based on the Linux
	   identity attributes, SELinux does not need to control
	   this operation.  However, SELinux does control the use
	   of the CAP_SETUID and CAP_SETGID capabilities using the
	   capable hook. */
	return 0;
}

static int selinux_task_post_setuid(uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return secondary_ops->task_post_setuid(id0,id1,id2,flags);
}

static int selinux_task_setgid(gid_t id0, gid_t id1, gid_t id2, int flags)
{
	/* See the comment for setuid above. */
	return 0;
}

static int selinux_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return task_has_perm(current, p, PROCESS__SETPGID);
}

static int selinux_task_getpgid(struct task_struct *p)
{
	return task_has_perm(current, p, PROCESS__GETPGID);
}

static int selinux_task_getsid(struct task_struct *p)
{
	return task_has_perm(current, p, PROCESS__GETSESSION);
}

static int selinux_task_setgroups(int gidsetsize, gid_t *grouplist)
{
	/* See the comment for setuid above. */
	return 0;
}

static int selinux_task_setnice(struct task_struct *p, int nice)
{
	return task_has_perm(current,p, PROCESS__SETSCHED);
}

static int selinux_task_setrlimit(unsigned int resource, struct rlimit *new_rlim)
{
	/* SELinux does not currently provide a process
	   resource limit policy based on security contexts.
	   It does control the use of the CAP_SYS_RESOURCE capability
	   using the capable hook. */
	return 0;
}

static int selinux_task_setscheduler(struct task_struct *p, int policy, struct sched_param *lp)
{
	struct task_security_struct *tsec1, *tsec2;

	tsec1 = current->security;
	tsec2 = p->security;

	/* No auditing from the setscheduler hook, since the runqueue lock
	   is held and the system will deadlock if we try to log an audit
	   message. */
	return avc_has_perm_noaudit(tsec1->sid, tsec2->sid,
				    SECCLASS_PROCESS, PROCESS__SETSCHED,
				    &tsec2->avcr, NULL);
}

static int selinux_task_getscheduler(struct task_struct *p)
{
	return task_has_perm(current, p, PROCESS__GETSCHED);
}

static int selinux_task_kill(struct task_struct *p, struct siginfo *info, int sig)
{
	u32 perm;

	if (info && ((unsigned long)info == 1 ||
	             (unsigned long)info == 2 || SI_FROMKERNEL(info)))
		return 0;

	if (!sig)
		perm = PROCESS__SIGNULL; /* null signal; existence test */
	else
		perm = signal_to_av(sig);

	return task_has_perm(current, p, perm);
}

static int selinux_task_prctl(int option,
			      unsigned long arg2,
			      unsigned long arg3,
			      unsigned long arg4,
			      unsigned long arg5)
{
	/* The current prctl operations do not appear to require
	   any SELinux controls since they merely observe or modify
	   the state of the current process. */
	return 0;
}

static int selinux_task_wait(struct task_struct *p)
{
	u32 perm;

	perm = signal_to_av(p->exit_signal);

	return task_has_perm(p, current, perm);
}

static void selinux_task_reparent_to_init(struct task_struct *p)
{
  	struct task_security_struct *tsec;

	secondary_ops->task_reparent_to_init(p);

	tsec = p->security;
	tsec->osid = tsec->sid;
	tsec->sid = SECINITSID_KERNEL;
	return;
}

static void selinux_task_to_inode(struct task_struct *p,
				  struct inode *inode)
{
	struct task_security_struct *tsec = p->security;
	struct inode_security_struct *isec = inode->i_security;

	isec->sid = tsec->sid;
	isec->initialized = 1;
	return;
}

#ifdef CONFIG_SECURITY_NETWORK

/* socket security operations */
static int socket_has_perm(struct task_struct *task, struct socket *sock,
			   u32 perms)
{
	struct inode_security_struct *isec;
	struct task_security_struct *tsec;
	struct avc_audit_data ad;
	int err;

	tsec = task->security;
	isec = SOCK_INODE(sock)->i_security;

	AVC_AUDIT_DATA_INIT(&ad,NET);
	ad.u.net.sk = sock->sk;
	err = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			   perms, &isec->avcr, &ad);

	return err;
}

static int selinux_socket_create(int family, int type, int protocol)
{
	int err;
	struct task_security_struct *tsec;

	tsec = current->security;

	err = avc_has_perm(tsec->sid, tsec->sid,
			   socket_type_to_security_class(family, type),
			   SOCKET__CREATE, NULL, NULL);

	return err;
}

static void selinux_socket_post_create(struct socket *sock, int family, int type, int protocol)
{
	int err;
	struct inode_security_struct *isec;
	struct task_security_struct *tsec;

	err = inode_doinit(SOCK_INODE(sock));
	if (err < 0)
		return;
	isec = SOCK_INODE(sock)->i_security;

	tsec = current->security;
	isec->sclass = socket_type_to_security_class(family, type);
	isec->sid = tsec->sid;

	return;
}

/* Range of port numbers used to automatically bind.
   Need to determine whether we should perform a name_bind
   permission check between the socket and the port number. */
#define ip_local_port_range_0 sysctl_local_port_range[0]
#define ip_local_port_range_1 sysctl_local_port_range[1]

static int selinux_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
	int err;

	err = socket_has_perm(current, sock, SOCKET__BIND);
	if (err)
		return err;

	/*
	 * If PF_INET, check name_bind permission for the port.
	 */
	if (sock->sk->sk_family == PF_INET) {
		struct inode_security_struct *isec;
		struct task_security_struct *tsec;
		struct avc_audit_data ad;
		struct sockaddr_in *addr = (struct sockaddr_in *)address;
		unsigned short snum = ntohs(addr->sin_port);
		struct sock *sk = sock->sk;
		u32 sid;

		tsec = current->security;
		isec = SOCK_INODE(sock)->i_security;

		if (snum&&(snum < max(PROT_SOCK,ip_local_port_range_0) ||
			   snum > ip_local_port_range_1)) {
			err = security_port_sid(sk->sk_family, sk->sk_type,
						sk->sk_protocol, snum, &sid);
			if (err)
				return err;
			AVC_AUDIT_DATA_INIT(&ad,NET);
			ad.u.net.port = snum;
			err = avc_has_perm(isec->sid, sid,
					   isec->sclass,
					   SOCKET__NAME_BIND, NULL, &ad);
			if (err)
				return err;
		}
	}

	return 0;
}

static int selinux_socket_connect(struct socket *sock, struct sockaddr *address, int addrlen)
{
	int err;
	struct sock *sk = sock->sk;
	struct avc_audit_data ad;
	struct task_security_struct *tsec;
	struct inode_security_struct *isec;

	isec = SOCK_INODE(sock)->i_security;

	tsec = current->security;

	AVC_AUDIT_DATA_INIT(&ad, NET);
	ad.u.net.sk = sk;
	err = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			   SOCKET__CONNECT, &isec->avcr, &ad);
	if (err)
		return err;

	return 0;
}

static int selinux_socket_listen(struct socket *sock, int backlog)
{
	int err;
	struct task_security_struct *tsec;
	struct inode_security_struct *isec;
	struct avc_audit_data ad;

	tsec = current->security;

	isec = SOCK_INODE(sock)->i_security;

	AVC_AUDIT_DATA_INIT(&ad, NET);
	ad.u.net.sk = sock->sk;

	err = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			   SOCKET__LISTEN, &isec->avcr, &ad);
	if (err)
		return err;

	return 0;
}

static int selinux_socket_accept(struct socket *sock, struct socket *newsock)
{
	int err;
	struct task_security_struct *tsec;
	struct inode_security_struct *isec;
	struct inode_security_struct *newisec;
	struct avc_audit_data ad;

	tsec = current->security;

	isec = SOCK_INODE(sock)->i_security;

	AVC_AUDIT_DATA_INIT(&ad, NET);
	ad.u.net.sk = sock->sk;

	err = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			   SOCKET__ACCEPT, &isec->avcr, &ad);
	if (err)
		return err;

	err = inode_doinit(SOCK_INODE(newsock));
	if (err < 0)
		return err;
	newisec = SOCK_INODE(newsock)->i_security;

	newisec->sclass = isec->sclass;
	newisec->sid = isec->sid;

	return 0;
}

static int selinux_socket_sendmsg(struct socket *sock, struct msghdr *msg,
 				  int size)
{
	struct task_security_struct *tsec;
	struct inode_security_struct *isec;
	struct avc_audit_data ad;
	struct sock *sk;
	int err;

	isec = SOCK_INODE(sock)->i_security;

	tsec = current->security;

	sk = sock->sk;

	AVC_AUDIT_DATA_INIT(&ad, NET);
	ad.u.net.sk = sk;
	err = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			   SOCKET__WRITE, &isec->avcr, &ad);
	if (err)
		return err;

	return 0;
}

static int selinux_socket_recvmsg(struct socket *sock, struct msghdr *msg,
				  int size, int flags)
{
	struct inode_security_struct *isec;
	struct task_security_struct *tsec;
	struct avc_audit_data ad;
	int err;

	isec = SOCK_INODE(sock)->i_security;
	tsec = current->security;

	AVC_AUDIT_DATA_INIT(&ad,NET);
	ad.u.net.sk = sock->sk;
	err = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			   SOCKET__READ, &isec->avcr, &ad);
	if (err)
		return err;

	return 0;
}

static int selinux_socket_getsockname(struct socket *sock)
{
	struct inode_security_struct *isec;
	struct task_security_struct *tsec;
	struct avc_audit_data ad;
	int err;

	tsec = current->security;
	isec = SOCK_INODE(sock)->i_security;

	AVC_AUDIT_DATA_INIT(&ad,NET);
	ad.u.net.sk = sock->sk;
	err = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			   SOCKET__GETATTR, &isec->avcr, &ad);
	if (err)
		return err;

	return 0;
}

static int selinux_socket_getpeername(struct socket *sock)
{
	struct inode_security_struct *isec;
	struct task_security_struct *tsec;
	struct avc_audit_data ad;
	int err;

	tsec = current->security;
	isec = SOCK_INODE(sock)->i_security;

	AVC_AUDIT_DATA_INIT(&ad,NET);
	ad.u.net.sk = sock->sk;
	err = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			   SOCKET__GETATTR, &isec->avcr, &ad);
	if (err)
		return err;

	return 0;
}

static int selinux_socket_setsockopt(struct socket *sock,int level,int optname)
{
	return socket_has_perm(current, sock, SOCKET__SETOPT);
}

static int selinux_socket_getsockopt(struct socket *sock, int level,
				     int optname)
{
	return socket_has_perm(current, sock, SOCKET__GETOPT);
}

static int selinux_socket_shutdown(struct socket *sock, int how)
{
	return socket_has_perm(current, sock, SOCKET__SHUTDOWN);
}

static int selinux_socket_unix_stream_connect(struct socket *sock,
					      struct socket *other,
					      struct sock *newsk)
{
	struct inode_security_struct *isec;
	struct inode_security_struct *other_isec;
	struct avc_audit_data ad;
	int err;

	isec = SOCK_INODE(sock)->i_security;
	other_isec = SOCK_INODE(other)->i_security;

	AVC_AUDIT_DATA_INIT(&ad,NET);
	ad.u.net.sk = other->sk;

	err = avc_has_perm(isec->sid, other_isec->sid,
			   isec->sclass,
			   UNIX_STREAM_SOCKET__CONNECTTO,
			   &other_isec->avcr, &ad);
	if (err)
		return err;

	return 0;
}

static int selinux_socket_unix_may_send(struct socket *sock,
					struct socket *other)
{
	struct inode_security_struct *isec;
	struct inode_security_struct *other_isec;
	struct avc_audit_data ad;
	int err;

	isec = SOCK_INODE(sock)->i_security;
	other_isec = SOCK_INODE(other)->i_security;

	AVC_AUDIT_DATA_INIT(&ad,NET);
	ad.u.net.sk = other->sk;

	err = avc_has_perm(isec->sid, other_isec->sid,
			   isec->sclass,
			   SOCKET__SENDTO,
			   &other_isec->avcr, &ad);
	if (err)
		return err;

	return 0;
}

#endif

static int ipc_alloc_security(struct task_struct *task,
			      struct kern_ipc_perm *perm,
			      u16 sclass)
{
	struct task_security_struct *tsec = task->security;
	struct ipc_security_struct *isec;

	isec = kmalloc(sizeof(struct ipc_security_struct), GFP_KERNEL);
	if (!isec)
		return -ENOMEM;

	memset(isec, 0, sizeof(struct ipc_security_struct));
	isec->magic = SELINUX_MAGIC;
	isec->sclass = sclass;
	isec->ipc_perm = perm;
	if (tsec) {
		isec->sid = tsec->sid;
	} else {
		isec->sid = SECINITSID_UNLABELED;
	}
	perm->security = isec;

	return 0;
}

static void ipc_free_security(struct kern_ipc_perm *perm)
{
	struct ipc_security_struct *isec = perm->security;
	if (!isec || isec->magic != SELINUX_MAGIC)
		return;

	perm->security = NULL;
	kfree(isec);
}

static int msg_msg_alloc_security(struct msg_msg *msg)
{
	struct msg_security_struct *msec;

	msec = kmalloc(sizeof(struct msg_security_struct), GFP_KERNEL);
	if (!msec)
		return -ENOMEM;

	memset(msec, 0, sizeof(struct msg_security_struct));
	msec->magic = SELINUX_MAGIC;
	msec->msg = msg;
	msec->sid = SECINITSID_UNLABELED;
	msg->security = msec;

	return 0;
}

static void msg_msg_free_security(struct msg_msg *msg)
{
	struct msg_security_struct *msec = msg->security;
	if (!msec || msec->magic != SELINUX_MAGIC)
		return;

	msg->security = NULL;
	kfree(msec);
}

static int ipc_has_perm(struct kern_ipc_perm *ipc_perms,
			u16 sclass, u32 perms)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;

	tsec = current->security;
	isec = ipc_perms->security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = ipc_perms->key;

	return avc_has_perm(tsec->sid, isec->sid, sclass,
			    perms, &isec->avcr, &ad);
}

static int selinux_msg_msg_alloc_security(struct msg_msg *msg)
{
	return msg_msg_alloc_security(msg);
}

static void selinux_msg_msg_free_security(struct msg_msg *msg)
{
	return msg_msg_free_security(msg);
}

/* message queue security operations */
static int selinux_msg_queue_alloc_security(struct msg_queue *msq)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;
	int rc;

	rc = ipc_alloc_security(current, &msq->q_perm, SECCLASS_MSGQ);
	if (rc)
		return rc;

	tsec = current->security;
	isec = msq->q_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
 	ad.u.ipc_id = msq->q_perm.key;

	rc = avc_has_perm(tsec->sid, isec->sid, SECCLASS_MSGQ,
			  MSGQ__CREATE, &isec->avcr, &ad);
	if (rc) {
		ipc_free_security(&msq->q_perm);
		return rc;
	}
	return 0;
}

static void selinux_msg_queue_free_security(struct msg_queue *msq)
{
	ipc_free_security(&msq->q_perm);
}

static int selinux_msg_queue_associate(struct msg_queue *msq, int msqflg)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;

	tsec = current->security;
	isec = msq->q_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = msq->q_perm.key;

	return avc_has_perm(tsec->sid, isec->sid, SECCLASS_MSGQ,
			    MSGQ__ASSOCIATE, &isec->avcr, &ad);
}

static int selinux_msg_queue_msgctl(struct msg_queue *msq, int cmd)
{
	int err;
	int perms;

	switch(cmd) {
	case IPC_INFO:
	case MSG_INFO:
		/* No specific object, just general system-wide information. */
		return task_has_system(current, SYSTEM__IPC_INFO);
	case IPC_STAT:
	case MSG_STAT:
		perms = MSGQ__GETATTR | MSGQ__ASSOCIATE;
		break;
	case IPC_SET:
		perms = MSGQ__SETATTR;
		break;
	case IPC_RMID:
		perms = MSGQ__DESTROY;
		break;
	default:
		return 0;
	}

	err = ipc_has_perm(&msq->q_perm, SECCLASS_MSGQ, perms);
	return err;
}

static int selinux_msg_queue_msgsnd(struct msg_queue *msq, struct msg_msg *msg, int msqflg)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct msg_security_struct *msec;
	struct avc_audit_data ad;
	int rc;

	tsec = current->security;
	isec = msq->q_perm.security;
	msec = msg->security;

	/*
	 * First time through, need to assign label to the message
	 */
	if (msec->sid == SECINITSID_UNLABELED) {
		/*
		 * Compute new sid based on current process and
		 * message queue this message will be stored in
		 */
		rc = security_transition_sid(tsec->sid,
					     isec->sid,
					     SECCLASS_MSG,
					     &msec->sid);
		if (rc)
			return rc;
	}

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = msq->q_perm.key;

	/* Can this process write to the queue? */
	rc = avc_has_perm(tsec->sid, isec->sid, SECCLASS_MSGQ,
			  MSGQ__WRITE, &isec->avcr, &ad);
	if (!rc)
		/* Can this process send the message */
		rc = avc_has_perm(tsec->sid, msec->sid,
				  SECCLASS_MSG, MSG__SEND,
				  &msec->avcr, &ad);
	if (!rc)
		/* Can the message be put in the queue? */
		rc = avc_has_perm(msec->sid, isec->sid,
				  SECCLASS_MSGQ, MSGQ__ENQUEUE,
				  &isec->avcr, &ad);

	return rc;
}

static int selinux_msg_queue_msgrcv(struct msg_queue *msq, struct msg_msg *msg,
				    struct task_struct *target,
				    long type, int mode)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct msg_security_struct *msec;
	struct avc_audit_data ad;
	int rc;

	tsec = target->security;
	isec = msq->q_perm.security;
	msec = msg->security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
 	ad.u.ipc_id = msq->q_perm.key;

	rc = avc_has_perm(tsec->sid, isec->sid,
			  SECCLASS_MSGQ, MSGQ__READ,
			  &isec->avcr, &ad);
	if (!rc)
		rc = avc_has_perm(tsec->sid, msec->sid,
				  SECCLASS_MSG, MSG__RECEIVE,
				  &msec->avcr, &ad);
	return rc;
}

/* Shared Memory security operations */
static int selinux_shm_alloc_security(struct shmid_kernel *shp)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;
	int rc;

	rc = ipc_alloc_security(current, &shp->shm_perm, SECCLASS_SHM);
	if (rc)
		return rc;

	tsec = current->security;
	isec = shp->shm_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
 	ad.u.ipc_id = shp->shm_perm.key;

	rc = avc_has_perm(tsec->sid, isec->sid, SECCLASS_SHM,
			  SHM__CREATE, &isec->avcr, &ad);
	if (rc) {
		ipc_free_security(&shp->shm_perm);
		return rc;
	}
	return 0;
}

static void selinux_shm_free_security(struct shmid_kernel *shp)
{
	ipc_free_security(&shp->shm_perm);
}

static int selinux_shm_associate(struct shmid_kernel *shp, int shmflg)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;

	tsec = current->security;
	isec = shp->shm_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = shp->shm_perm.key;

	return avc_has_perm(tsec->sid, isec->sid, SECCLASS_SHM,
			    SHM__ASSOCIATE, &isec->avcr, &ad);
}

/* Note, at this point, shp is locked down */
static int selinux_shm_shmctl(struct shmid_kernel *shp, int cmd)
{
	int perms;
	int err;

	switch(cmd) {
	case IPC_INFO:
	case SHM_INFO:
		/* No specific object, just general system-wide information. */
		return task_has_system(current, SYSTEM__IPC_INFO);
	case IPC_STAT:
	case SHM_STAT:
		perms = SHM__GETATTR | SHM__ASSOCIATE;
		break;
	case IPC_SET:
		perms = SHM__SETATTR;
		break;
	case SHM_LOCK:
	case SHM_UNLOCK:
		perms = SHM__LOCK;
		break;
	case IPC_RMID:
		perms = SHM__DESTROY;
		break;
	default:
		return 0;
	}

	err = ipc_has_perm(&shp->shm_perm, SECCLASS_SHM, perms);
	return err;
}

static int selinux_shm_shmat(struct shmid_kernel *shp,
			     char *shmaddr, int shmflg)
{
	u32 perms;

	if (shmflg & SHM_RDONLY)
		perms = SHM__READ;
	else
		perms = SHM__READ | SHM__WRITE;

	return ipc_has_perm(&shp->shm_perm, SECCLASS_SHM, perms);
}

/* Semaphore security operations */
static int selinux_sem_alloc_security(struct sem_array *sma)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;
	int rc;

	rc = ipc_alloc_security(current, &sma->sem_perm, SECCLASS_SEM);
	if (rc)
		return rc;

	tsec = current->security;
	isec = sma->sem_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
 	ad.u.ipc_id = sma->sem_perm.key;

	rc = avc_has_perm(tsec->sid, isec->sid, SECCLASS_SEM,
			  SEM__CREATE, &isec->avcr, &ad);
	if (rc) {
		ipc_free_security(&sma->sem_perm);
		return rc;
	}
	return 0;
}

static void selinux_sem_free_security(struct sem_array *sma)
{
	ipc_free_security(&sma->sem_perm);
}

static int selinux_sem_associate(struct sem_array *sma, int semflg)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;

	tsec = current->security;
	isec = sma->sem_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = sma->sem_perm.key;

	return avc_has_perm(tsec->sid, isec->sid, SECCLASS_SEM,
			    SEM__ASSOCIATE, &isec->avcr, &ad);
}

/* Note, at this point, sma is locked down */
static int selinux_sem_semctl(struct sem_array *sma, int cmd)
{
	int err;
	u32 perms;

	switch(cmd) {
	case IPC_INFO:
	case SEM_INFO:
		/* No specific object, just general system-wide information. */
		return task_has_system(current, SYSTEM__IPC_INFO);
	case GETPID:
	case GETNCNT:
	case GETZCNT:
		perms = SEM__GETATTR;
		break;
	case GETVAL:
	case GETALL:
		perms = SEM__READ;
		break;
	case SETVAL:
	case SETALL:
		perms = SEM__WRITE;
		break;
	case IPC_RMID:
		perms = SEM__DESTROY;
		break;
	case IPC_SET:
		perms = SEM__SETATTR;
		break;
	case IPC_STAT:
	case SEM_STAT:
		perms = SEM__GETATTR | SEM__ASSOCIATE;
		break;
	default:
		return 0;
	}

	err = ipc_has_perm(&sma->sem_perm, SECCLASS_SEM, perms);
	return err;
}

static int selinux_sem_semop(struct sem_array *sma,
			     struct sembuf *sops, unsigned nsops, int alter)
{
	u32 perms;

	if (alter)
		perms = SEM__READ | SEM__WRITE;
	else
		perms = SEM__READ;

	return ipc_has_perm(&sma->sem_perm, SECCLASS_SEM, perms);
}

static int selinux_ipc_permission(struct kern_ipc_perm *ipcp, short flag)
{
	struct ipc_security_struct *isec = ipcp->security;
	u16 sclass = SECCLASS_IPC;
	u32 av = 0;

	if (isec && isec->magic == SELINUX_MAGIC)
		sclass = isec->sclass;

	av = 0;
	if (flag & S_IRUGO)
		av |= IPC__UNIX_READ;
	if (flag & S_IWUGO)
		av |= IPC__UNIX_WRITE;

	if (av == 0)
		return 0;

	return ipc_has_perm(ipcp, sclass, av);
}

/* module stacking operations */
int selinux_register_security (const char *name, struct security_operations *ops)
{
	if (secondary_ops != original_ops) {
		printk(KERN_INFO "%s:  There is already a secondary security "
		       "module registered.\n", __FUNCTION__);
		return -EINVAL;
 	}

	secondary_ops = ops;

	printk(KERN_INFO "%s:  Registering secondary module %s\n",
	       __FUNCTION__,
	       name);

	return 0;
}

int selinux_unregister_security (const char *name, struct security_operations *ops)
{
	if (ops != secondary_ops) {
		printk (KERN_INFO "%s:  trying to unregister a security module "
		        "that is not registered.\n", __FUNCTION__);
		return -EINVAL;
	}

	secondary_ops = original_ops;

	return 0;
}

static void selinux_d_instantiate (struct dentry *dentry, struct inode *inode)
{
	if (inode)
		inode_doinit_with_dentry(inode, dentry);
}

static int selinux_getprocattr(struct task_struct *p,
			       char *name, void *value, size_t size)
{
	struct task_security_struct *tsec;
	u32 sid, len;
	char *context;
	int error;

	if (current != p) {
		error = task_has_perm(current, p, PROCESS__GETATTR);
		if (error)
			return error;
	}

	if (!size)
		return -ERANGE;

	tsec = p->security;

	if (!strcmp(name, "current"))
		sid = tsec->sid;
	else if (!strcmp(name, "prev"))
		sid = tsec->osid;
	else if (!strcmp(name, "exec"))
		sid = tsec->exec_sid;
	else if (!strcmp(name, "fscreate"))
		sid = tsec->create_sid;
	else
		return -EINVAL;

	if (!sid)
		return 0;

	error = security_sid_to_context(sid, &context, &len);
	if (error)
		return error;
	if (len > size) {
		kfree(context);
		return -ERANGE;
	}
	memcpy(value, context, len);
	kfree(context);
	return len;
}

static int selinux_setprocattr(struct task_struct *p,
			       char *name, void *value, size_t size)
{
	struct task_security_struct *tsec;
	u32 sid = 0;
	int error;

	if (current != p || !strcmp(name, "current")) {
		/* SELinux only allows a process to change its own
		   security attributes, and it only allows the process
		   current SID to change via exec. */
		return -EACCES;
	}

	/*
	 * Basic control over ability to set these attributes at all.
	 * current == p, but we'll pass them separately in case the
	 * above restriction is ever removed.
	 */
	if (!strcmp(name, "exec"))
		error = task_has_perm(current, p, PROCESS__SETEXEC);
	else if (!strcmp(name, "fscreate"))
		error = task_has_perm(current, p, PROCESS__SETFSCREATE);
	else
		error = -EINVAL;
	if (error)
		return error;

	/* Obtain a SID for the context, if one was specified. */
	if (size) {
		int error;
		error = security_context_to_sid(value, size, &sid);
		if (error)
			return error;
	}

	/* Permission checking based on the specified context is
	   performed during the actual operation (execve,
	   open/mkdir/...), when we know the full context of the
	   operation.  See selinux_bprm_set_security for the execve
	   checks and may_create for the file creation checks. The
	   operation will then fail if the context is not permitted. */
	tsec = p->security;
	if (!strcmp(name, "exec"))
		tsec->exec_sid = sid;
	else if (!strcmp(name, "fscreate"))
		tsec->create_sid = sid;
	else
		return -EINVAL;

	return size;
}

struct security_operations selinux_ops = {
	.ptrace =			selinux_ptrace,
	.capget =			selinux_capget,
	.capset_check =			selinux_capset_check,
	.capset_set =			selinux_capset_set,
	.sysctl =			selinux_sysctl,
	.capable =			selinux_capable,
	.quotactl =			selinux_quotactl,
	.quota_on =			selinux_quota_on,
	.syslog =			selinux_syslog,
	.vm_enough_memory =		selinux_vm_enough_memory,

	.netlink_send =			selinux_netlink_send,
        .netlink_recv =			selinux_netlink_recv,

	.bprm_alloc_security =		selinux_bprm_alloc_security,
	.bprm_free_security =		selinux_bprm_free_security,
	.bprm_compute_creds =		selinux_bprm_compute_creds,
	.bprm_set_security =		selinux_bprm_set_security,
	.bprm_check_security =		selinux_bprm_check_security,
	.bprm_secureexec =		selinux_bprm_secureexec,

	.sb_alloc_security =		selinux_sb_alloc_security,
	.sb_free_security =		selinux_sb_free_security,
	.sb_kern_mount =	        selinux_sb_kern_mount,
	.sb_statfs =			selinux_sb_statfs,
	.sb_mount =			selinux_mount,
	.sb_umount =			selinux_umount,

	.inode_alloc_security =		selinux_inode_alloc_security,
	.inode_free_security =		selinux_inode_free_security,
	.inode_create =			selinux_inode_create,
	.inode_post_create =		selinux_inode_post_create,
	.inode_link =			selinux_inode_link,
	.inode_post_link =		selinux_inode_post_link,
	.inode_unlink =			selinux_inode_unlink,
	.inode_symlink =		selinux_inode_symlink,
	.inode_post_symlink =		selinux_inode_post_symlink,
	.inode_mkdir =			selinux_inode_mkdir,
	.inode_post_mkdir =		selinux_inode_post_mkdir,
	.inode_rmdir =			selinux_inode_rmdir,
	.inode_mknod =			selinux_inode_mknod,
	.inode_post_mknod =		selinux_inode_post_mknod,
	.inode_rename =			selinux_inode_rename,
	.inode_post_rename =		selinux_inode_post_rename,
	.inode_readlink =		selinux_inode_readlink,
	.inode_follow_link =		selinux_inode_follow_link,
	.inode_permission =		selinux_inode_permission,
	.inode_setattr =		selinux_inode_setattr,
	.inode_getattr =		selinux_inode_getattr,
	.inode_setxattr =		selinux_inode_setxattr,
	.inode_post_setxattr =		selinux_inode_post_setxattr,
	.inode_getxattr =		selinux_inode_getxattr,
	.inode_listxattr =		selinux_inode_listxattr,
	.inode_removexattr =		selinux_inode_removexattr,
	.inode_getsecurity =            selinux_inode_getsecurity,
	.inode_setsecurity =            selinux_inode_setsecurity,
	.inode_listsecurity =           selinux_inode_listsecurity,

	.file_permission =		selinux_file_permission,
	.file_alloc_security =		selinux_file_alloc_security,
	.file_free_security =		selinux_file_free_security,
	.file_ioctl =			selinux_file_ioctl,
	.file_mmap =			selinux_file_mmap,
	.file_mprotect =		selinux_file_mprotect,
	.file_lock =			selinux_file_lock,
	.file_fcntl =			selinux_file_fcntl,
	.file_set_fowner =		selinux_file_set_fowner,
	.file_send_sigiotask =		selinux_file_send_sigiotask,
	.file_receive =			selinux_file_receive,

	.task_create =			selinux_task_create,
	.task_alloc_security =		selinux_task_alloc_security,
	.task_free_security =		selinux_task_free_security,
	.task_setuid =			selinux_task_setuid,
	.task_post_setuid =		selinux_task_post_setuid,
	.task_setgid =			selinux_task_setgid,
	.task_setpgid =			selinux_task_setpgid,
	.task_getpgid =			selinux_task_getpgid,
	.task_getsid =		        selinux_task_getsid,
	.task_setgroups =		selinux_task_setgroups,
	.task_setnice =			selinux_task_setnice,
	.task_setrlimit =		selinux_task_setrlimit,
	.task_setscheduler =		selinux_task_setscheduler,
	.task_getscheduler =		selinux_task_getscheduler,
	.task_kill =			selinux_task_kill,
	.task_wait =			selinux_task_wait,
	.task_prctl =			selinux_task_prctl,
	.task_reparent_to_init =	selinux_task_reparent_to_init,
	.task_to_inode =                selinux_task_to_inode,

	.ipc_permission =		selinux_ipc_permission,

	.msg_msg_alloc_security =	selinux_msg_msg_alloc_security,
	.msg_msg_free_security =	selinux_msg_msg_free_security,

	.msg_queue_alloc_security =	selinux_msg_queue_alloc_security,
	.msg_queue_free_security =	selinux_msg_queue_free_security,
	.msg_queue_associate =		selinux_msg_queue_associate,
	.msg_queue_msgctl =		selinux_msg_queue_msgctl,
	.msg_queue_msgsnd =		selinux_msg_queue_msgsnd,
	.msg_queue_msgrcv =		selinux_msg_queue_msgrcv,

	.shm_alloc_security =		selinux_shm_alloc_security,
	.shm_free_security =		selinux_shm_free_security,
	.shm_associate =		selinux_shm_associate,
	.shm_shmctl =			selinux_shm_shmctl,
	.shm_shmat =			selinux_shm_shmat,

	.sem_alloc_security = 		selinux_sem_alloc_security,
	.sem_free_security =  		selinux_sem_free_security,
	.sem_associate =		selinux_sem_associate,
	.sem_semctl =			selinux_sem_semctl,
	.sem_semop =			selinux_sem_semop,

	.register_security =		selinux_register_security,
	.unregister_security =		selinux_unregister_security,

	.d_instantiate =                selinux_d_instantiate,

	.getprocattr =                  selinux_getprocattr,
	.setprocattr =                  selinux_setprocattr,

#ifdef CONFIG_SECURITY_NETWORK
        .unix_stream_connect =		selinux_socket_unix_stream_connect,
	.unix_may_send =		selinux_socket_unix_may_send,

	.socket_create =		selinux_socket_create,
	.socket_post_create =		selinux_socket_post_create,
	.socket_bind =			selinux_socket_bind,
	.socket_connect =		selinux_socket_connect,
	.socket_listen =		selinux_socket_listen,
	.socket_accept =		selinux_socket_accept,
	.socket_sendmsg =		selinux_socket_sendmsg,
	.socket_recvmsg =		selinux_socket_recvmsg,
	.socket_getsockname =		selinux_socket_getsockname,
	.socket_getpeername =		selinux_socket_getpeername,
	.socket_getsockopt =		selinux_socket_getsockopt,
	.socket_setsockopt =		selinux_socket_setsockopt,
	.socket_shutdown =		selinux_socket_shutdown,
#endif
};

__init int selinux_init(void)
{
	struct task_security_struct *tsec;

	if (!selinux_enabled) {
		printk(KERN_INFO "SELinux:  Disabled at boot.\n");
		return 0;
	}

	printk(KERN_INFO "SELinux:  Initializing.\n");

	/* Set the security state for the initial task. */
	if (task_alloc_security(current))
		panic("SELinux:  Failed to initialize initial task.\n");
	tsec = current->security;
	tsec->osid = tsec->sid = SECINITSID_KERNEL;

	avc_init();

	original_ops = secondary_ops = security_ops;
	if (!secondary_ops)
		panic ("SELinux: No initial security operations\n");
	if (register_security (&selinux_ops))
		panic("SELinux: Unable to register with kernel.\n");

	if (selinux_enforcing) {
		printk(KERN_INFO "SELinux:  Starting in enforcing mode\n");
	} else {
		printk(KERN_INFO "SELinux:  Starting in permissive mode\n");
	}
	return 0;
}

void selinux_complete_init(void)
{
	printk(KERN_INFO "SELinux:  Completing initialization.\n");

	/* Set up any superblocks initialized prior to the policy load. */
	printk(KERN_INFO "SELinux:  Setting up existing superblocks.\n");
	spin_lock(&sb_security_lock);
next_sb:
	if (!list_empty(&superblock_security_head)) {
		struct superblock_security_struct *sbsec =
				list_entry(superblock_security_head.next,
				           struct superblock_security_struct,
				           list);
		struct super_block *sb = sbsec->sb;
		spin_lock(&sb_lock);
		sb->s_count++;
		spin_unlock(&sb_lock);
		spin_unlock(&sb_security_lock);
		down_read(&sb->s_umount);
		if (sb->s_root)
			superblock_doinit(sb);
		drop_super(sb);
		spin_lock(&sb_security_lock);
		list_del_init(&sbsec->list);
		goto next_sb;
	}
	spin_unlock(&sb_security_lock);
}

/* SELinux requires early initialization in order to label
   all processes and objects when they are created. */
security_initcall(selinux_init);

