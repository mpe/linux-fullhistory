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
#include <linux/smp_lock.h>

#include <linux/fs.h>

#if defined(CONFIG_QUOTA)

/*
 * declaration of quota_function calls in kernel.
 */
extern void sync_dquots(struct super_block *sb, int type);

extern void dquot_initialize(struct inode *inode, int type);
extern void dquot_drop(struct inode *inode);

extern int  dquot_alloc_space(struct inode *inode, qsize_t number, int prealloc);
extern int  dquot_alloc_inode(const struct inode *inode, unsigned long number);

extern void dquot_free_space(struct inode *inode, qsize_t number);
extern void dquot_free_inode(const struct inode *inode, unsigned long number);

extern int  dquot_transfer(struct inode *inode, struct iattr *iattr);

/*
 * Operations supported for diskquotas.
 */
extern struct dquot_operations dquot_operations;
extern struct quotactl_ops vfs_quotactl_ops;

#define sb_dquot_ops (&dquot_operations)
#define sb_quotactl_ops (&vfs_quotactl_ops)

static __inline__ void DQUOT_INIT(struct inode *inode)
{
	if (!inode->i_sb)
		BUG();
	if (sb_any_quota_enabled(inode->i_sb) && !IS_NOQUOTA(inode))
		inode->i_sb->dq_op->initialize(inode, -1);
}

static __inline__ void DQUOT_DROP(struct inode *inode)
{
	if (IS_QUOTAINIT(inode)) {
		if (!inode->i_sb)
			BUG();
		inode->i_sb->dq_op->drop(inode);	/* Ops must be set when there's any quota... */
	}
}

static __inline__ int DQUOT_PREALLOC_SPACE_NODIRTY(struct inode *inode, qsize_t nr)
{
	if (sb_any_quota_enabled(inode->i_sb)) {
		/* Used space is updated in alloc_space() */
		if (inode->i_sb->dq_op->alloc_space(inode, nr, 1) == NO_QUOTA)
			return 1;
	}
	else {
		spin_lock(&dq_data_lock);
		inode_add_bytes(inode, nr);
		spin_unlock(&dq_data_lock);
	}
	return 0;
}

static __inline__ int DQUOT_PREALLOC_SPACE(struct inode *inode, qsize_t nr)
{
	int ret;
        if (!(ret =  DQUOT_PREALLOC_SPACE_NODIRTY(inode, nr)))
		mark_inode_dirty(inode);
	return ret;
}

static __inline__ int DQUOT_ALLOC_SPACE_NODIRTY(struct inode *inode, qsize_t nr)
{
	if (sb_any_quota_enabled(inode->i_sb)) {
		/* Used space is updated in alloc_space() */
		if (inode->i_sb->dq_op->alloc_space(inode, nr, 0) == NO_QUOTA)
			return 1;
	}
	else {
		spin_lock(&dq_data_lock);
		inode_add_bytes(inode, nr);
		spin_unlock(&dq_data_lock);
	}
	return 0;
}

static __inline__ int DQUOT_ALLOC_SPACE(struct inode *inode, qsize_t nr)
{
	int ret;
	if (!(ret = DQUOT_ALLOC_SPACE_NODIRTY(inode, nr)))
		mark_inode_dirty(inode);
	return ret;
}

static __inline__ int DQUOT_ALLOC_INODE(struct inode *inode)
{
	if (sb_any_quota_enabled(inode->i_sb)) {
		DQUOT_INIT(inode);
		if (inode->i_sb->dq_op->alloc_inode(inode, 1) == NO_QUOTA)
			return 1;
	}
	return 0;
}

static __inline__ void DQUOT_FREE_SPACE_NODIRTY(struct inode *inode, qsize_t nr)
{
	if (sb_any_quota_enabled(inode->i_sb))
		inode->i_sb->dq_op->free_space(inode, nr);
	else {
		spin_lock(&dq_data_lock);
		inode_sub_bytes(inode, nr);
		spin_unlock(&dq_data_lock);
	}
}

static __inline__ void DQUOT_FREE_SPACE(struct inode *inode, qsize_t nr)
{
	DQUOT_FREE_SPACE_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
}

static __inline__ void DQUOT_FREE_INODE(struct inode *inode)
{
	if (sb_any_quota_enabled(inode->i_sb))
		inode->i_sb->dq_op->free_inode(inode, 1);
}

static __inline__ int DQUOT_TRANSFER(struct inode *inode, struct iattr *iattr)
{
	if (sb_any_quota_enabled(inode->i_sb) && !IS_NOQUOTA(inode)) {
		DQUOT_INIT(inode);
		if (inode->i_sb->dq_op->transfer(inode, iattr) == NO_QUOTA)
			return 1;
	}
	return 0;
}

#define DQUOT_SYNC(sb)	sync_dquots(sb, -1)

static __inline__ int DQUOT_OFF(struct super_block *sb)
{
	int ret = -ENOSYS;

	if (sb->s_qcop && sb->s_qcop->quota_off)
		ret = sb->s_qcop->quota_off(sb, -1);
	return ret;
}

#else

/*
 * NO-OP when quota not configured.
 */
#define sb_dquot_ops				(NULL)
#define sb_quotactl_ops				(NULL)
#define sync_dquots_dev(dev,type)		(NULL)
#define DQUOT_INIT(inode)			do { } while(0)
#define DQUOT_DROP(inode)			do { } while(0)
#define DQUOT_ALLOC_INODE(inode)		(0)
#define DQUOT_FREE_INODE(inode)			do { } while(0)
#define DQUOT_SYNC(sb)				do { } while(0)
#define DQUOT_OFF(sb)				do { } while(0)
#define DQUOT_TRANSFER(inode, iattr)		(0)
extern __inline__ int DQUOT_PREALLOC_SPACE_NODIRTY(struct inode *inode, qsize_t nr)
{
	inode_add_bytes(inode, nr);
	return 0;
}

extern __inline__ int DQUOT_PREALLOC_SPACE(struct inode *inode, qsize_t nr)
{
	DQUOT_PREALLOC_SPACE_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
	return 0;
}

extern __inline__ int DQUOT_ALLOC_SPACE_NODIRTY(struct inode *inode, qsize_t nr)
{
	inode_add_bytes(inode, nr);
	return 0;
}

extern __inline__ int DQUOT_ALLOC_SPACE(struct inode *inode, qsize_t nr)
{
	DQUOT_ALLOC_SPACE_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
	return 0;
}

extern __inline__ void DQUOT_FREE_SPACE_NODIRTY(struct inode *inode, qsize_t nr)
{
	inode_sub_bytes(inode, nr);
}

extern __inline__ void DQUOT_FREE_SPACE(struct inode *inode, qsize_t nr)
{
	DQUOT_FREE_SPACE_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
}	

#endif /* CONFIG_QUOTA */

#define DQUOT_PREALLOC_BLOCK_NODIRTY(inode, nr)	DQUOT_PREALLOC_SPACE_NODIRTY(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_PREALLOC_BLOCK(inode, nr)	DQUOT_PREALLOC_SPACE(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_ALLOC_BLOCK_NODIRTY(inode, nr) DQUOT_ALLOC_SPACE_NODIRTY(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_ALLOC_BLOCK(inode, nr) DQUOT_ALLOC_SPACE(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_FREE_BLOCK_NODIRTY(inode, nr) DQUOT_FREE_SPACE_NODIRTY(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_FREE_BLOCK(inode, nr) DQUOT_FREE_SPACE(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)

#endif /* _LINUX_QUOTAOPS_ */
