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
 * Determine if a dentry tree is in use.  This is much the
 * same as the standard is_root_busy() function, except
 * that :-
 *  - the extra dentry reference in autofs dentries is not
 *    considered to be busy
 *  - mountpoints within the tree are not busy
 *  - it traverses across mountpoints
 * XXX doesn't consider children of covered dentries at mountpoints
 */
static int is_tree_busy(struct dentry *root)
{
	struct dentry *this_parent;
	struct list_head *next;
	int count;

	root = root->d_mounts;

	count = root->d_count;
	this_parent = root;

	DPRINTK(("is_tree_busy: starting at %.*s/%.*s, d_count=%d\n",
		 root->d_covers->d_parent->d_name.len,
		 root->d_covers->d_parent->d_name.name,
		 root->d_name.len, root->d_name.name,
		 root->d_count));

	/* Ignore autofs's extra reference */
	if (is_autofs4_dentry(root)) {
		DPRINTK(("is_tree_busy: autofs\n"));
		count--;
	}

	/* Mountpoints don't count (either mountee or mounter) */
	if (d_mountpoint(root) ||
	    root != root->d_covers) {
		DPRINTK(("is_tree_busy: mountpoint\n"));
		count--;
	}

repeat:
	next = this_parent->d_mounts->d_subdirs.next;
resume:
	while (next != &this_parent->d_mounts->d_subdirs) {
		int adj = 0;
		struct list_head *tmp = next;
		struct dentry *dentry = list_entry(tmp, struct dentry,
						   d_child);

		next = tmp->next;

		dentry = dentry->d_mounts;

		DPRINTK(("is_tree_busy: considering %.*s/%.*s, d_count=%d, count=%d\n",
			 this_parent->d_name.len,
			 this_parent->d_name.name,
			 dentry->d_covers->d_name.len, 
			 dentry->d_covers->d_name.name,
			 dentry->d_count, count));

		/* Decrement count for unused children */
		count += (dentry->d_count - 1);

		/* Mountpoints don't count */
		if (d_mountpoint(dentry)) {
			DPRINTK(("is_tree_busy: mountpoint dentry=%p covers=%p mounts=%p\n",
				 dentry, dentry->d_covers, dentry->d_mounts));
			adj++;
		}

		/* ... and roots - twice as much... */
		if (dentry != dentry->d_covers) {
			DPRINTK(("is_tree_busy: mountpoint dentry=%p covers=%p mounts=%p\n",
				 dentry, dentry->d_covers, dentry->d_mounts));
			adj+=2;
		}

		/* Ignore autofs's extra reference */
		if (is_autofs4_dentry(dentry)) {
			DPRINTK(("is_tree_busy: autofs\n"));
			adj++;
		}

		count -= adj;

		if (!list_empty(&dentry->d_mounts->d_subdirs)) {
			this_parent = dentry->d_mounts;
			goto repeat;
		}

		/* root is busy if any leaf is busy */
		if (dentry->d_count != adj) {
			DPRINTK(("is_tree_busy: busy leaf (d_count=%d adj=%d)\n",
				 dentry->d_count, adj));
			return 1;
		}
	}
	/*
	 * All done at this level ... ascend and resume the search.
	 */
	if (this_parent != root) {
		next = this_parent->d_covers->d_child.next; 
		this_parent = this_parent->d_covers->d_parent;
		goto resume;
	}

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
				    struct autofs_sb_info *sbi,
				    int do_now)
{
	unsigned long now = jiffies; /* snapshot of now */
	unsigned long timeout;
	struct dentry *root = sb->s_root;
	struct list_head *tmp;

	if (!sbi->exp_timeout || !root)
		return NULL;

	timeout = sbi->exp_timeout;

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

		if (!is_tree_busy(dentry)) {
			DPRINTK(("autofs_expire: returning %p %.*s\n",
				 dentry, dentry->d_name.len, dentry->d_name.name));
			/* Start from here next time */
			list_del(&root->d_subdirs);
			list_add(&root->d_subdirs, &dentry->d_child);
			return dentry;
		}
	}

	return NULL;
}

/* Perform an expiry operation */
int autofs4_expire_run(struct super_block *sb,
		      struct autofs_sb_info *sbi,
		      struct autofs_packet_expire *pkt_p)
{
	struct autofs_packet_expire pkt;
	struct dentry *dentry;

	memset(&pkt,0,sizeof pkt);

	pkt.hdr.proto_version = sbi->version;
	pkt.hdr.type = autofs_ptype_expire;

	if ((dentry = autofs4_expire(sb, sbi, 0)) == NULL)
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
int autofs4_expire_multi(struct super_block *sb,
			struct autofs_sb_info *sbi, int *arg)
{
	struct dentry *dentry;
	int ret = -EAGAIN;
	int do_now = 0;

	if (arg && get_user(do_now, arg))
		return -EFAULT;

	if ((dentry = autofs4_expire(sb, sbi, do_now)) != NULL) {
		struct autofs_info *de_info = autofs4_dentry_ino(dentry);

		/* This is synchronous because it makes the daemon a
                   little easier */
		de_info->flags |= AUTOFS_INF_EXPIRING;
		ret = autofs4_wait(sbi, &dentry->d_name, NFY_EXPIRE);
		de_info->flags &= ~AUTOFS_INF_EXPIRING;
	}
		
	return ret;
}

