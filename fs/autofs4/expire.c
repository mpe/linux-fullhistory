/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/expire.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *  Copyright 1999 Jeremy Fitzhardinge <jeremy@goop.org>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include "autofs_i.h"

/*
 * Determine if a subtree of the namespace is busy.
 */
static int is_tree_busy(struct vfsmount *mnt)
{
	struct vfsmount *this_parent = mnt;
	struct list_head *next;
	int count;

	spin_lock(&dcache_lock);
	count = atomic_read(&mnt->mnt_count);
repeat:
	next = this_parent->mnt_mounts.next;
resume:
	while (next != &this_parent->mnt_mounts) {
		struct list_head *tmp = next;
		struct vfsmount *p = list_entry(tmp, struct vfsmount,
						mnt_child);
		next = tmp->next;
		/* Decrement count for unused children */
		count += atomic_read(&p->mnt_count) - 1;
		if (!list_empty(&p->mnt_mounts)) {
			this_parent = p;
			goto repeat;
		}
		/* root is busy if any leaf is busy */
		if (atomic_read(&p->mnt_count) > 1) {
			spin_unlock(&dcache_lock);
			return 1;
		}
	}
	/*
	 * All done at this level ... ascend and resume the search.
	 */
	if (this_parent != mnt) {
		next = this_parent->mnt_child.next; 
		this_parent = this_parent->mnt_parent;
		goto resume;
	}
	spin_unlock(&dcache_lock);

	DPRINTK(("is_tree_busy: count=%d\n", count));
	return count != 0; /* remaining users? */
}

/*
 * Find an eligible tree to time-out
 * A tree is eligible if :-
 *  - it is unused by any user process
 *  - it has been unused for exp_timeout time
 */
static struct dentry *autofs4_expire(struct super_block *sb,
				    struct vfsmount *mnt,
				    struct autofs_sb_info *sbi,
				    int do_now)
{
	unsigned long now = jiffies; /* snapshot of now */
	unsigned long timeout;
	struct dentry *root = sb->s_root;
	struct list_head *tmp;
	struct dentry *d;
	struct vfsmount *p;

	if (!sbi->exp_timeout || !root)
		return NULL;

	timeout = sbi->exp_timeout;

	spin_lock(&dcache_lock);
	for(tmp = root->d_subdirs.next;
	    tmp != &root->d_subdirs; 
	    tmp = tmp->next) {
		struct autofs_info *ino;
		struct dentry *dentry = list_entry(tmp, struct dentry, d_child);

		if (dentry->d_inode == NULL)
			continue;

		ino = autofs4_dentry_ino(dentry);

		if (ino == NULL) {
			/* dentry in the process of being deleted */
			continue;
		}

		/* No point expiring a pending mount */
		if (dentry->d_flags & DCACHE_AUTOFS_PENDING)
			continue;

		if (!do_now) {
			/* Too young to die */
			if (time_after(ino->last_used+timeout, now))
				continue;
		
			/* update last_used here :- 
			   - obviously makes sense if it is in use now
			   - less obviously, prevents rapid-fire expire
			   attempts if expire fails the first time */
			ino->last_used = now;
		}
		p = mntget(mnt);
		d = dget(dentry);
		spin_unlock(&dcache_lock);
		while(d_mountpoint(d) && follow_down(&p, &d))
			;

		if (!is_tree_busy(p)) {
			dput(d);
			mntput(p);
			DPRINTK(("autofs_expire: returning %p %.*s\n",
				 dentry, dentry->d_name.len, dentry->d_name.name));
			/* Start from here next time */
			spin_lock(&dcache_lock);
			list_del(&root->d_subdirs);
			list_add(&root->d_subdirs, &dentry->d_child);
			spin_unlock(&dcache_lock);
			return dentry;
		}
		dput(d);
		mntput(p);
		spin_lock(&dcache_lock);
	}
	spin_unlock(&dcache_lock);

	return NULL;
}

/* Perform an expiry operation */
int autofs4_expire_run(struct super_block *sb,
		      struct vfsmount *mnt,
		      struct autofs_sb_info *sbi,
		      struct autofs_packet_expire *pkt_p)
{
	struct autofs_packet_expire pkt;
	struct dentry *dentry;

	memset(&pkt,0,sizeof pkt);

	pkt.hdr.proto_version = sbi->version;
	pkt.hdr.type = autofs_ptype_expire;

	if ((dentry = autofs4_expire(sb, mnt, sbi, 0)) == NULL)
		return -EAGAIN;

	pkt.len = dentry->d_name.len;
	memcpy(pkt.name, dentry->d_name.name, pkt.len);
	pkt.name[pkt.len] = '\0';

	if ( copy_to_user(pkt_p, &pkt, sizeof(struct autofs_packet_expire)) )
		return -EFAULT;

	return 0;
}

/* Call repeatedly until it returns -EAGAIN, meaning there's nothing
   more to be done */
int autofs4_expire_multi(struct super_block *sb, struct vfsmount *mnt,
			struct autofs_sb_info *sbi, int *arg)
{
	struct dentry *dentry;
	int ret = -EAGAIN;
	int do_now = 0;

	if (arg && get_user(do_now, arg))
		return -EFAULT;

	if ((dentry = autofs4_expire(sb, mnt, sbi, do_now)) != NULL) {
		struct autofs_info *de_info = autofs4_dentry_ino(dentry);

		/* This is synchronous because it makes the daemon a
                   little easier */
		de_info->flags |= AUTOFS_INF_EXPIRING;
		ret = autofs4_wait(sbi, &dentry->d_name, NFY_EXPIRE);
		de_info->flags &= ~AUTOFS_INF_EXPIRING;
	}
		
	return ret;
}

