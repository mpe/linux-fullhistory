/*
 * Definitions for diskquota-operations. When diskquota is configured these
 * macros expand to the right source-code.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: quotaops.h,v 1.2 1998/01/15 16:22:26 ecd Exp $
 *
 */
#ifndef _LINUX_QUOTAOPS_
#define _LINUX_QUOTAOPS_

#include <linux/config.h>

#if defined(CONFIG_QUOTA)

/*
 * declaration of quota_function calls in kernel.
 */
extern void dquot_initialize(struct inode *inode, short type);
extern void dquot_drop(struct inode *inode);
extern void invalidate_dquots(kdev_t dev, short type);
extern int  quota_off(kdev_t dev, short type);
extern int  sync_dquots(kdev_t dev, short type);

extern int  dquot_alloc_block(const struct inode *inode, unsigned long number,
                              uid_t initiator, char warn);
extern int  dquot_alloc_inode(const struct inode *inode, unsigned long number,
                              uid_t initiator);

extern void dquot_free_block(const struct inode *inode, unsigned long number);
extern void dquot_free_inode(const struct inode *inode, unsigned long number);

extern int  dquot_transfer(struct inode *inode, struct iattr *iattr,
                           char direction, uid_t initiator);

/*
 * Operations supported for diskquotas.
 */
#define DQUOT_INIT(inode) \
if (inode->i_sb && inode->i_sb->dq_op) { \
	inode->i_sb->dq_op->initialize(inode, -1); \
}

#define DQUOT_DROP(inode) \
if (IS_QUOTAINIT(inode)) { \
	if (inode->i_sb && inode->i_sb->dq_op) \
		inode->i_sb->dq_op->drop(inode); \
}

#define DQUOT_SAVE_DROP(inode) \
if (IS_QUOTAINIT(inode)) { \
	inode->i_lock = 1; \
	if (inode->i_sb && inode->i_sb->dq_op) \
		inode->i_sb->dq_op->drop(inode); \
	unlock_inode(inode); \
	goto we_slept; \
}

#define DQUOT_PREALLOC_BLOCK(sb, inode, nr) \
if (sb->dq_op) { \
	if (sb->dq_op->alloc_block(inode, fs_to_dq_blocks(nr, sb->s_blocksize), \
	                           current->euid, 0) == NO_QUOTA) \
		break; \
}

#define DQUOT_ALLOC_BLOCK(sb, inode, nr) \
if (sb->dq_op) { \
	if (sb->dq_op->alloc_block(inode, fs_to_dq_blocks(nr, sb->s_blocksize), \
	                           current->euid, 1) == NO_QUOTA) { \
		unlock_super (sb); \
		*err = -EDQUOT; \
		return 0; \
	} \
}

#define DQUOT_ALLOC_INODE(sb, inode) \
if (sb->dq_op) { \
	sb->dq_op->initialize (inode, -1); \
	if (sb->dq_op->alloc_inode (inode, 1, current->euid)) { \
		sb->dq_op->drop (inode); \
		inode->i_nlink = 0; \
		iput (inode); \
		*err = -EDQUOT; \
		return NULL; \
	} \
	inode->i_flags |= S_QUOTA; \
}

#define DQUOT_FREE_BLOCK(sb, inode, nr) \
if (sb->dq_op) { \
	sb->dq_op->free_block(inode, fs_to_dq_blocks(nr, sb->s_blocksize)); \
}

#define DQUOT_FREE_INODE(sb, inode) \
if (sb->dq_op) { \
	sb->dq_op->free_inode(inode, 1); \
}

#define DQUOT_TRANSFER(dentry, iattr) \
if (dentry->d_inode->i_sb->dq_op) { \
	if (IS_QUOTAINIT(dentry->d_inode) == 0) \
		dentry->d_inode->i_sb->dq_op->initialize(dentry->d_inode, -1); \
	if (dentry->d_inode->i_sb->dq_op->transfer(dentry->d_inode, &iattr, 0, current->euid)) { \
		error = -EDQUOT; \
		goto out; \
	} \
	error = notify_change(dentry, &iattr); \
	if (error) \
		inode->i_sb->dq_op->transfer(dentry->d_inode, &iattr, 1, current->euid); \
} else { \
	error = notify_change(dentry, &iattr); \
}

#define DQUOT_SYNC(dev) \
sync_dquots(dev, -1)

#define DQUOT_OFF(dev) \
quota_off(dev, -1)

#else

/*
 * NO-OP when quota not configured.
 */
#define DQUOT_INIT(inode)
#define DQUOT_DROP(inode)
#define DQUOT_SAVE_DROP(inode)
#define DQUOT_PREALLOC_BLOCK(sb, inode, nr)
#define DQUOT_ALLOC_BLOCK(sb, inode, nr)
#define DQUOT_ALLOC_INODE(sb, inode)
#define DQUOT_FREE_BLOCK(sb, inode, nr)
#define DQUOT_FREE_INODE(sb, inode)
#define DQUOT_SYNC(dev)
#define DQUOT_OFF(dev)

/*
 * Special case expands to a simple notify_change.
 */
#define DQUOT_TRANSFER(dentry, iattr) \
error = notify_change(dentry, &iattr)

#endif /* CONFIG_QUOTA */
#endif /* _LINUX_QUOTAOPS_ */
