/*
 * Cache operations for Coda.
 * For Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/list.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_psdev.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_cache.h>

/* create a new acl cache entry and enlist it */
static struct coda_cache *coda_cache_create(struct inode *inode)
{
	struct coda_inode_info *cii = ITOC(inode);
	struct coda_sb_info *sbi = coda_sbp(inode->i_sb);
	struct coda_cache *cc = NULL;
	ENTRY;

	if ( !sbi || !cii ) {
		printk("coda_cache_create: NULL sbi or cii!\n");
		return NULL;
	}

	CODA_ALLOC(cc, struct coda_cache *, sizeof(*cc));

	if ( !cc ) {
		printk("Out of memory in coda_cache_create!\n");
		return NULL;
	}

	coda_load_creds(&cc->cc_cred);
	cc->cc_mask = 0;

	INIT_LIST_HEAD(&cc->cc_cclist);
	INIT_LIST_HEAD(&cc->cc_cnlist);
	list_add(&cc->cc_cclist, &sbi->sbi_cchead);
	list_add(&cc->cc_cnlist, &cii->c_cnhead);

	return cc;
}

/* destroy an acl cache entry */
static void coda_cache_destroy(struct coda_cache *el)
{
	ENTRY;
        if (list_empty(&el->cc_cclist) || list_empty(&el->cc_cnlist)) {
		printk("coda_cache_destroy: loose entry!");
		return;
	}
	list_del(&el->cc_cclist);
	list_del(&el->cc_cnlist);
	CODA_FREE(el, sizeof(struct coda_cache));
}

/* see if there is a match for the current 
   credentials already */
static struct coda_cache * coda_cache_find(struct inode *inode)
{
	struct coda_inode_info *cii = ITOC(inode);
	struct list_head *le;
	struct coda_cache *cc = NULL;
	
	list_for_each(le, &cii->c_cnhead)
	{
		/* compare name and creds */
		cc = list_entry(le, struct coda_cache, cc_cnlist);
		if ( !coda_cred_ok(&cc->cc_cred) )
			continue;
		CDEBUG(D_CACHE, "HIT for ino %ld\n", inode->i_ino );
		return cc; /* cache hit */
	}
	return NULL;
}

/* create or extend an acl cache hit */
void coda_cache_enter(struct inode *inode, int mask)
{
	struct coda_cache *cc;

	cc = coda_cache_find(inode);

	if (!cc)
		cc = coda_cache_create(inode);
	if (cc)
		cc->cc_mask |= mask;
}

/* remove all cached acl matches from an inode */
void coda_cache_clear_inode(struct inode *inode)
{
	struct list_head *le;
	struct coda_inode_info *cii;
	struct coda_cache *cc;
	ENTRY;

	if ( !inode ) {
		CDEBUG(D_CACHE, "coda_cache_clear_inode: NULL inode\n");
		return;
	}
	cii = ITOC(inode);
	
	le = cii->c_cnhead.next;
	while ( le != &cii->c_cnhead ) {
		cc = list_entry(le, struct coda_cache, cc_cnlist);
		le = le->next;
		coda_cache_destroy(cc);
	}
}

/* remove all acl caches */
void coda_cache_clear_all(struct super_block *sb)
{
	struct list_head *le;
	struct coda_cache *cc;
	struct coda_sb_info *sbi = coda_sbp(sb);

	if ( !sbi ) {
		printk("coda_cache_clear_all: NULL sbi\n");
		return;
	}
	
	le = sbi->sbi_cchead.next;
	while ( le != &sbi->sbi_cchead ) {
		cc = list_entry(le, struct coda_cache, cc_cclist);
		le = le->next;
		coda_cache_destroy(cc);
	}
}

/* remove all acl caches for a principal */
void coda_cache_clear_cred(struct super_block *sb, struct coda_cred *cred)
{
	struct list_head *le;
	struct coda_cache *cc;
	struct coda_sb_info *sbi = coda_sbp(sb);

	if ( !sbi ) {
		printk("coda_cache_clear_all: NULL sbi\n");
		return;
	}
	
	le = sbi->sbi_cchead.next;
	while ( le != &sbi->sbi_cchead ) {
		cc = list_entry(le, struct coda_cache, cc_cclist);
		le = le->next;
		if ( coda_cred_eq(&cc->cc_cred, cred))
			coda_cache_destroy(cc);
	}
}


/* check if the mask has been matched against the acl
   already */
int coda_cache_check(struct inode *inode, int mask)
{
	struct coda_inode_info *cii = ITOC(inode);
	struct list_head *le;
	struct coda_cache *cc = NULL;
	
	list_for_each(le, &cii->c_cnhead)
	{
		/* compare name and creds */
		cc = list_entry(le, struct coda_cache, cc_cnlist);
		if ( (cc->cc_mask & mask) != mask ) 
			continue; 
		if ( !coda_cred_ok(&cc->cc_cred) )
			continue;
		CDEBUG(D_CACHE, "HIT for ino %ld\n", inode->i_ino );
		return 1; /* cache hit */
	}
	CDEBUG(D_CACHE, "MISS for ino %ld\n", inode->i_ino );
	return 0;
}


/* Purging dentries and children */
/* The following routines drop dentries which are not
   in use and flag dentries which are in use to be 
   zapped later.

   The flags are detected by:
   - coda_dentry_revalidate (for lookups) if the flag is C_PURGE
   - coda_dentry_delete: to remove dentry from the cache when d_count
     falls to zero
   - an inode method coda_revalidate (for attributes) if the 
     flag is C_VATTR
*/

/* 
   Some of this is pretty scary: what can disappear underneath us?
   - shrink_dcache_parent calls on purge_one_dentry which is safe:
     it only purges children.
   - dput is evil since it  may recurse up the dentry tree
 */

void coda_purge_dentries(struct inode *inode)
{
	if (!inode)
		return ;

	/* better safe than sorry: dput could kill us */
	iget(inode->i_sb, inode->i_ino);
	/* catch the dentries later if some are still busy */
	coda_flag_inode(inode, C_PURGE);
	d_prune_aliases(inode);
	iput(inode);
}

/* this won't do any harm: just flag all children */
static void coda_flag_children(struct dentry *parent, int flag)
{
	struct list_head *child;
	struct dentry *de;

	spin_lock(&dcache_lock);
	list_for_each(child, &parent->d_subdirs)
	{
		de = list_entry(child, struct dentry, d_child);
		/* don't know what to do with negative dentries */
		if ( ! de->d_inode ) 
			continue;
		CDEBUG(D_DOWNCALL, "%d for %*s/%*s\n", flag, 
		       de->d_name.len, de->d_name.name, 
		       de->d_parent->d_name.len, de->d_parent->d_name.name);
		coda_flag_inode(de->d_inode, flag);
	}
	spin_unlock(&dcache_lock);
	return; 
}

void coda_flag_inode_children(struct inode *inode, int flag)
{
	struct dentry *alias_de;

	ENTRY;
	if ( !inode || !S_ISDIR(inode->i_mode)) 
		return; 

	alias_de = d_find_alias(inode);
	if (!alias_de)
		return;
	coda_flag_children(alias_de, flag);
	shrink_dcache_parent(alias_de);
	dput(alias_de);
}

