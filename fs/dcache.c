/*
 * fs/dcache.c
 *
 * Complete reimplementation
 * (C) 1997 Thomas Schoebel-Theuer
 */

/*
 * Notes on the allocation strategy:
 *
 * The dcache is a master of the icache - whenever a dcache entry
 * exists, the inode will always exist. "iput()" is done either when
 * the dcache entry is deleted or garbage collected.
 */

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <asm/uaccess.h>

#define DCACHE_PARANOIA 1
/* #define DCACHE_DEBUG 1 */

/* For managing the dcache */
extern unsigned long num_physpages, page_cache_size;
extern int inodes_stat[];
#define nr_inodes (inodes_stat[0])

kmem_cache_t *dentry_cache; 

/*
 * This is the single most critical data structure when it comes
 * to the dcache: the hashtable for lookups. Somebody should try
 * to make this good - I've just made it work.
 *
 * This hash-function tries to avoid losing too many bits of hash
 * information, yet avoid using a prime hash-size or similar.
 */
#define D_HASHBITS     10
#define D_HASHSIZE     (1UL << D_HASHBITS)
#define D_HASHMASK     (D_HASHSIZE-1)

static struct list_head dentry_hashtable[D_HASHSIZE];
static LIST_HEAD(dentry_unused);

struct {
	int nr_dentry;
	int nr_unused;
	int age_limit;		/* age in seconds */
	int want_pages;		/* pages requested by system */
	int dummy[2];
} dentry_stat = {0, 0, 45, 0,};

static inline void d_free(struct dentry *dentry)
{
	if (dentry->d_op && dentry->d_op->d_release)
		dentry->d_op->d_release(dentry);
	if (dname_external(dentry)) 
		kfree(dentry->d_name.name);
	kmem_cache_free(dentry_cache, dentry); 
}

/*
 * Release the dentry's inode, using the fileystem
 * d_iput() operation if defined.
 */
static inline void dentry_iput(struct dentry * dentry)
{
	struct inode *inode = dentry->d_inode;
	if (inode) {
		dentry->d_inode = NULL;
		list_del(&dentry->d_alias);
		INIT_LIST_HEAD(&dentry->d_alias);
		if (dentry->d_op && dentry->d_op->d_iput)
			dentry->d_op->d_iput(dentry, inode);
		else
			iput(inode);
	}
}

/*
 * dput()
 *
 * This is complicated by the fact that we do not want to put
 * dentries that are no longer on any hash chain on the unused
 * list: we'd much rather just get rid of them immediately.
 *
 * However, that implies that we have to traverse the dentry
 * tree upwards to the parents which might _also_ now be
 * scheduled for deletion (it may have been only waiting for
 * its last child to go away).
 *
 * This tail recursion is done by hand as we don't want to depend
 * on the compiler to always get this right (gcc generally doesn't).
 * Real recursion would eat up our stack space.
 */
void dput(struct dentry *dentry)
{
	int count;

	if (!dentry)
		return;

repeat:
	count = dentry->d_count - 1;
	if (count != 0)
		goto out;

	/*
	 * Note that if d_op->d_delete blocks,
	 * the dentry could go back in use.
	 * Each fs will have to watch for this.
	 */
	if (dentry->d_op && dentry->d_op->d_delete) {
		dentry->d_op->d_delete(dentry);

		count = dentry->d_count - 1;
		if (count != 0)
			goto out;
	}

	if (!list_empty(&dentry->d_lru)) {
		dentry_stat.nr_unused--;
		list_del(&dentry->d_lru);
	}
	if (list_empty(&dentry->d_hash)) {
		struct dentry * parent;

		list_del(&dentry->d_child);
		dentry_iput(dentry);
		parent = dentry->d_parent;
		d_free(dentry);
		if (dentry == parent)
			return;
		dentry = parent;
		goto repeat;
	}
	list_add(&dentry->d_lru, &dentry_unused);
	dentry_stat.nr_unused++;
	/*
	 * Update the timestamp
	 */
	dentry->d_reftime = jiffies;

out:
	if (count >= 0) {
		dentry->d_count = count;
		return;
	}

	printk(KERN_CRIT "Negative d_count (%d) for %s/%s\n",
		count,
		dentry->d_parent->d_name.name,
		dentry->d_name.name);
	*(int *)0 = 0;	
}

/*
 * Try to invalidate the dentry if it turns out to be
 * possible. If there are other dentries that can be
 * reached through this one we can't delete it.
 */
int d_invalidate(struct dentry * dentry)
{
	/*
	 * Check whether to do a partial shrink_dcache
	 * to get rid of unused child entries.
	 */
	if (!list_empty(&dentry->d_subdirs)) {
		shrink_dcache_parent(dentry);
	}

	/*
	 * Somebody else still using it?
	 *
	 * If it's a directory, we can't drop it
	 * for fear of somebody re-populating it
	 * with children (even though dropping it
	 * would make it unreachable from the root,
	 * we might still populate it if it was a
	 * working directory or similar).
	 */
	if (dentry->d_count > 1) {
		if (dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode))
			return -EBUSY;
	}

	d_drop(dentry);
	return 0;
}

/*
 * Select less valuable dentries to be pruned when we need
 * inodes or memory. The selected dentries are moved to the
 * old end of the list where prune_dcache() can find them.
 * 
 * Negative dentries are included in the selection so that
 * they don't accumulate at the end of the list. The count
 * returned is the total number of dentries selected, which
 * may be much larger than the requested number of inodes.
 */
int select_dcache(int inode_count, int page_count)
{
	struct list_head *next, *tail = &dentry_unused;
	int found = 0;
	int depth = dentry_stat.nr_unused >> 1;
	unsigned long max_value = 4;

	if (page_count)
		max_value = -1;

	next = tail->prev;
	while (next != &dentry_unused && depth--) {
		struct list_head *tmp = next;
		struct dentry *dentry = list_entry(tmp, struct dentry, d_lru);
		struct inode *inode = dentry->d_inode;
		unsigned long value = 0;	

		next = tmp->prev;
		if (dentry->d_count) {
			dentry_stat.nr_unused--;
			list_del(tmp);
			INIT_LIST_HEAD(tmp);
			continue;
		}

		/*
		 * Select dentries based on the page cache count ...
		 * should factor in number of uses as well. We take
		 * all negative dentries so that they don't accumulate.
		 * (We skip inodes that aren't immediately available.)
		 */
		if (inode) {
			value = inode->i_nrpages;	
			if (value >= max_value)
				continue;
			if (inode->i_state || inode->i_count > 1)
				continue;
		}

		/*
		 * Move the selected dentries behind the tail.
		 */
		if (tmp != tail->prev) {
			list_del(tmp);
			list_add(tmp, tail->prev);
		}
		tail = tmp;
		found++;
		if (inode && --inode_count <= 0)
			break;
		if (page_count && (page_count -= value) <= 0)
			break;
	}
	return found;
}

/*
 * Throw away a dentry - free the inode, dput the parent.
 * This requires that the LRU list has already been
 * removed.
 */
static inline void prune_one_dentry(struct dentry * dentry)
{
	struct dentry * parent;

	list_del(&dentry->d_hash);
	list_del(&dentry->d_child);
	dentry_iput(dentry);
	parent = dentry->d_parent;
	d_free(dentry);
	dput(parent);
}

/*
 * Shrink the dcache. This is done when we need
 * more memory, or simply when we need to unmount
 * something (at which point we need to unuse
 * all dentries).
 */
void prune_dcache(int count)
{
	for (;;) {
		struct dentry *dentry;
		struct list_head *tmp = dentry_unused.prev;

		if (tmp == &dentry_unused)
			break;
		dentry_stat.nr_unused--;
		list_del(tmp);
		INIT_LIST_HEAD(tmp);
		dentry = list_entry(tmp, struct dentry, d_lru);
		if (!dentry->d_count) {
			prune_one_dentry(dentry);
			if (!--count)
				break;
		}
	}
}

/*
 * Shrink the dcache for the specified super block.
 * This allows us to unmount a device without disturbing
 * the dcache for the other devices.
 *
 * This implementation makes just two traversals of the
 * unused list.  On the first pass we move the selected
 * dentries to the most recent end, and on the second
 * pass we free them.  The second pass must restart after
 * each dput(), but since the target dentries are all at
 * the end, it's really just a single traversal.
 */
void shrink_dcache_sb(struct super_block * sb)
{
	struct list_head *tmp, *next;
	struct dentry *dentry;

	/*
	 * Pass one ... move the dentries for the specified
	 * superblock to the most recent end of the unused list.
	 */
	next = dentry_unused.next;
	while (next != &dentry_unused) {
		tmp = next;
		next = tmp->next;
		dentry = list_entry(tmp, struct dentry, d_lru);
		if (dentry->d_sb != sb)
			continue;
		list_del(tmp);
		list_add(tmp, &dentry_unused);
	}

	/*
	 * Pass two ... free the dentries for this superblock.
	 */
repeat:
	next = dentry_unused.next;
	while (next != &dentry_unused) {
		tmp = next;
		next = tmp->next;
		dentry = list_entry(tmp, struct dentry, d_lru);
		if (dentry->d_sb != sb)
			continue;
		if (dentry->d_count)
			continue;
		dentry_stat.nr_unused--;
		list_del(tmp);
		INIT_LIST_HEAD(tmp);
		prune_one_dentry(dentry);
		goto repeat;
	}
}

/*
 * Check whether a root dentry would be in use if all of its
 * child dentries were freed. This allows a non-destructive
 * test for unmounting a device.
 */
int is_root_busy(struct dentry *root)
{
	struct dentry *this_parent = root;
	struct list_head *next;
	int count = root->d_count;

repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct list_head *tmp = next;
		struct dentry *dentry = list_entry(tmp, struct dentry, d_child);
		next = tmp->next;
		/* Decrement count for unused children */
		count += (dentry->d_count - 1);
		if (!list_empty(&dentry->d_subdirs)) {
			this_parent = dentry;
			goto repeat;
		}
		/* root is busy if any leaf is busy */
		if (dentry->d_count)
			return 1;
	}
	/*
	 * All done at this level ... ascend and resume the search.
	 */
	if (this_parent != root) {
		next = this_parent->d_child.next; 
		this_parent = this_parent->d_parent;
		goto resume;
	}
	return (count > 1); /* remaining users? */
}

/*
 * Search the dentry child list for the specified parent,
 * and move any unused dentries to the end of the unused
 * list for prune_dcache(). We descend to the next level
 * whenever the d_subdirs list is non-empty and continue
 * searching.
 */
static int select_parent(struct dentry * parent)
{
	struct dentry *this_parent = parent;
	struct list_head *next;
	int found = 0;

repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct list_head *tmp = next;
		struct dentry *dentry = list_entry(tmp, struct dentry, d_child);
		next = tmp->next;
		if (!dentry->d_count) {
			list_del(&dentry->d_lru);
			list_add(&dentry->d_lru, dentry_unused.prev);
			found++;
		}
		/*
		 * Descend a level if the d_subdirs list is non-empty.
		 */
		if (!list_empty(&dentry->d_subdirs)) {
			this_parent = dentry;
#ifdef DCACHE_DEBUG
printk(KERN_DEBUG "select_parent: descending to %s/%s, found=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, found);
#endif
			goto repeat;
		}
	}
	/*
	 * All done at this level ... ascend and resume the search.
	 */
	if (this_parent != parent) {
		next = this_parent->d_child.next; 
		this_parent = this_parent->d_parent;
#ifdef DCACHE_DEBUG
printk(KERN_DEBUG "select_parent: ascending to %s/%s, found=%d\n",
this_parent->d_parent->d_name.name, this_parent->d_name.name, found);
#endif
		goto resume;
	}
	return found;
}

/*
 * Prune the dcache to remove unused children of the parent dentry.
 */
void shrink_dcache_parent(struct dentry * parent)
{
	int found;

	while ((found = select_parent(parent)) != 0)
		prune_dcache(found);
}

/*
 * This is called from kswapd when we think we need some
 * more memory, but aren't really sure how much. So we
 * carefully try to free a _bit_ of our dcache, but not
 * too much.
 *
 * Priority:
 *   0 - very urgent: shrink everything
 *  ...
 *   6 - base-level: try to shrink a bit.
 */
void shrink_dcache_memory(int priority, unsigned int gfp_mask)
{
	prune_dcache(0);
}

#define NAME_ALLOC_LEN(len)	((len+16) & ~15)

struct dentry * d_alloc(struct dentry * parent, const struct qstr *name)
{
	char * str;
	struct dentry *dentry;

	dentry = kmem_cache_alloc(dentry_cache, GFP_KERNEL); 
	if (!dentry)
		return NULL;

	if (name->len > DNAME_INLINE_LEN-1) {
		str = kmalloc(NAME_ALLOC_LEN(name->len), GFP_KERNEL);
		if (!str) {
			kmem_cache_free(dentry_cache, dentry); 
			return NULL;
		}
	} else
		str = dentry->d_iname; 

	memcpy(str, name->name, name->len);
	str[name->len] = 0;

	dentry->d_count = 1;
	dentry->d_flags = 0;
	dentry->d_inode = NULL;
	dentry->d_parent = NULL;
	dentry->d_sb = NULL;
	if (parent) {
		dentry->d_parent = dget(parent);
		dentry->d_sb = parent->d_sb;
		list_add(&dentry->d_child, &parent->d_subdirs);
	} else
		INIT_LIST_HEAD(&dentry->d_child);
		
	dentry->d_mounts = dentry;
	dentry->d_covers = dentry;
	INIT_LIST_HEAD(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_lru);
	INIT_LIST_HEAD(&dentry->d_subdirs);
	INIT_LIST_HEAD(&dentry->d_alias);

	dentry->d_name.name = str;
	dentry->d_name.len = name->len;
	dentry->d_name.hash = name->hash;
	dentry->d_op = NULL;
	dentry->d_fsdata = NULL;
	return dentry;
}

/*
 * Fill in inode information in the entry.
 *
 * This turns negative dentries into productive full members
 * of society.
 *
 * NOTE! This assumes that the inode count has been incremented
 * (or otherwise set) by the caller to indicate that it is now
 * in use by the dcache..
 */
void d_instantiate(struct dentry *entry, struct inode * inode)
{
	if (inode)
		list_add(&entry->d_alias, &inode->i_dentry);
	entry->d_inode = inode;
}

struct dentry * d_alloc_root(struct inode * root_inode, struct dentry *old_root)
{
	struct dentry *res = NULL;

	if (root_inode) {
		res = d_alloc(NULL, &(const struct qstr) { "/", 1, 0 });
		if (res) {
			res->d_sb = root_inode->i_sb;
			res->d_parent = res;
			d_instantiate(res, root_inode);
		}
	}
	return res;
}

static inline struct list_head * d_hash(struct dentry * parent, unsigned long hash)
{
	hash += (unsigned long) parent;
	hash = hash ^ (hash >> D_HASHBITS) ^ (hash >> D_HASHBITS*2);
	return dentry_hashtable + (hash & D_HASHMASK);
}

struct dentry * d_lookup(struct dentry * parent, struct qstr * name)
{
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct list_head *head = d_hash(parent,hash);
	struct list_head *tmp = head->next;

	for (;;) {
		struct dentry * dentry = list_entry(tmp, struct dentry, d_hash);
		if (tmp == head)
			break;
		tmp = tmp->next;
		if (dentry->d_name.hash != hash)
			continue;
		if (dentry->d_parent != parent)
			continue;
		if (parent->d_op && parent->d_op->d_compare) {
			if (parent->d_op->d_compare(parent, &dentry->d_name, name))
				continue;
		} else {
			if (dentry->d_name.len != len)
				continue;
			if (memcmp(dentry->d_name.name, str, len))
				continue;
		}
		return dget(dentry);
	}
	return NULL;
}

/*
 * An insecure source has sent us a dentry, here we verify it.
 *
 * This is just to make knfsd able to have the dentry pointer
 * in the NFS file handle.
 *
 * NOTE! Do _not_ dereference the pointers before we have
 * validated them. We can test the pointer values, but we
 * must not actually use them until we have found a valid
 * copy of the pointer in kernel space..
 */
int d_validate(struct dentry *dentry, struct dentry *dparent,
	       unsigned int hash, unsigned int len)
{
	struct list_head *base, *lhp;
	int valid = 1;

	if (dentry != dparent) {
		base = d_hash(dparent, hash);
		lhp = base;
		while ((lhp = lhp->next) != base) {
			if (dentry == list_entry(lhp, struct dentry, d_hash))
				goto out;
		}
	} else {
		/*
		 * Special case: local mount points don't live in
		 * the hashes, so we search the super blocks.
		 */
		struct super_block *sb = sb_entry(super_blocks.next);

		for (; sb != sb_entry(&super_blocks); 
		     sb = sb_entry(sb->s_list.next)) {
			if (!sb->s_dev)
				continue;
			if (sb->s_root == dentry)
				goto out;
		}
	}
	valid = 0;
out:
	return valid;
}

/*
 * When a file is deleted, we have two options:
 * - turn this dentry into a negative dentry
 * - unhash this dentry and free it.
 *
 * Usually, we want to just turn this into
 * a negative dentry, but if anybody else is
 * currently using the dentry or the inode
 * we can't do that and we fall back on removing
 * it from the hash queues and waiting for
 * it to be deleted later when it has no users
 */
void d_delete(struct dentry * dentry)
{
	/*
	 * Are we the only user?
	 */
	if (dentry->d_count == 1) {
		dentry_iput(dentry);
		return;
	}

	/*
	 * If not, just drop the dentry and let dput
	 * pick up the tab..
	 */
	d_drop(dentry);
}

void d_rehash(struct dentry * entry)
{
	struct dentry * parent = entry->d_parent;

	list_add(&entry->d_hash, d_hash(parent, entry->d_name.hash));
}

#define do_switch(x,y) do { \
	__typeof__ (x) __tmp = x; \
	x = y; y = __tmp; } while (0)

/*
 * When switching names, the actual string doesn't strictly have to
 * be preserved in the target - because we're dropping the target
 * anyway. As such, we can just do a simple memcpy() to copy over
 * the new name before we switch.
 *
 * Note that we have to be a lot more careful about getting the hash
 * switched - we have to switch the hash value properly even if it
 * then no longer matches the actual (corrupted) string of the target.
 * The has value has to match the hash queue that the dentry is on..
 */
static inline void switch_names(struct dentry * dentry, struct dentry * target)
{
	const unsigned char *old_name, *new_name;

	memcpy(dentry->d_iname, target->d_iname, DNAME_INLINE_LEN); 
	old_name = target->d_name.name;
	new_name = dentry->d_name.name;
	if (old_name == target->d_iname)
		old_name = dentry->d_iname;
	if (new_name == dentry->d_iname)
		new_name = target->d_iname;
	target->d_name.name = new_name;
	dentry->d_name.name = old_name;
}

/*
 * We cannibalize "target" when moving dentry on top of it,
 * because it's going to be thrown away anyway. We could be more
 * polite about it, though.
 *
 * This forceful removal will result in ugly /proc output if
 * somebody holds a file open that got deleted due to a rename.
 * We could be nicer about the deleted file, and let it show
 * up under the name it got deleted rather than the name that
 * deleted it.
 *
 * Careful with the hash switch. The hash switch depends on
 * the fact that any list-entry can be a head of the list.
 * Think about it.
 */
void d_move(struct dentry * dentry, struct dentry * target)
{
	if (!dentry->d_inode)
		printk(KERN_WARNING "VFS: moving negative dcache entry\n");

	/* Move the dentry to the target hash queue */
	list_del(&dentry->d_hash);
	list_add(&dentry->d_hash, &target->d_hash);

	/* Unhash the target: dput() will then get rid of it */
	list_del(&target->d_hash);
	INIT_LIST_HEAD(&target->d_hash);

	list_del(&dentry->d_child);
	list_del(&target->d_child);

	/* Switch the parents and the names.. */
	switch_names(dentry, target);
	do_switch(dentry->d_parent, target->d_parent);
	do_switch(dentry->d_name.len, target->d_name.len);
	do_switch(dentry->d_name.hash, target->d_name.hash);

	/* And add them back to the (new) parent lists */
	list_add(&target->d_child, &target->d_parent->d_subdirs);
	list_add(&dentry->d_child, &dentry->d_parent->d_subdirs);
}

/*
 * "buflen" should be PAGE_SIZE or more.
 */
char * d_path(struct dentry *dentry, char *buffer, int buflen)
{
	char * end = buffer+buflen;
	char * retval;
	struct dentry * root = current->fs->root;

	*--end = '\0';
	buflen--;
	if (dentry->d_parent != dentry && list_empty(&dentry->d_hash)) {
		buflen -= 10;
		end -= 10;
		memcpy(end, " (deleted)", 10);
	}

	/* Get '/' right */
	retval = end-1;
	*retval = '/';

	for (;;) {
		struct dentry * parent;
		int namelen;

		if (dentry == root)
			break;
		dentry = dentry->d_covers;
		parent = dentry->d_parent;
		if (dentry == parent)
			break;
		namelen = dentry->d_name.len;
		buflen -= namelen + 1;
		if (buflen < 0)
			break;
		end -= namelen;
		memcpy(end, dentry->d_name.name, namelen);
		*--end = '/';
		retval = end;
		dentry = parent;
	}
	return retval;
}

/*
 * NOTE! The user-level library version returns a
 * character pointer. The kernel system call just
 * returns the length of the buffer filled (which
 * includes the ending '\0' character), or a negative
 * error value. So libc would do something like
 *
 *	char *getcwd(char * buf, size_t size)
 *	{
 *		int retval;
 *
 *		retval = sys_getcwd(buf, size);
 *		if (retval >= 0)
 *			return buf;
 *		errno = -retval;
 *		return NULL;
 *	}
 */
asmlinkage int sys_getcwd(char *buf, unsigned long size)
{
	int error;
	struct dentry *pwd = current->fs->pwd; 

	error = -ENOENT;
	/* Has the current directory has been unlinked? */
	if (pwd->d_parent == pwd || !list_empty(&pwd->d_hash)) {
		char *page = (char *) __get_free_page(GFP_USER);
		error = -ENOMEM;
		if (page) {
			unsigned long len;
			char * cwd = d_path(pwd, page, PAGE_SIZE);

			error = -ERANGE;
			len = PAGE_SIZE + page - cwd;
			if (len <= size) {
				error = len;
				if (copy_to_user(buf, cwd, len))
					error = -EFAULT;
			}
			free_page((unsigned long) page);
		}
	}
	return error;
}

/*
 * Test whether new_dentry is a subdirectory of old_dentry.
 *
 * Trivially implemented using the dcache structure
 */
int is_subdir(struct dentry * new_dentry, struct dentry * old_dentry)
{
	int result;

	result = 0;
	for (;;) {
		if (new_dentry != old_dentry) {
			struct dentry * parent = new_dentry->d_parent;
			if (parent == new_dentry)
				break;
			new_dentry = parent;
			continue;
		}
		result = 1;
		break;
	}
	return result;
}

/*
 * Check whether a dentry already exists for the given name,
 * and return the inode number if it has an inode.
 *
 * This routine is used to post-process directory listings for
 * filesystems using synthetic inode numbers, and is necessary
 * to keep getcwd() working.
 */
ino_t find_inode_number(struct dentry *dir, struct qstr *name)
{
	struct dentry * dentry;
	ino_t ino = 0;

	/*
	 * Check for a fs-specific hash function. Note that we must
	 * calculate the standard hash first, as the d_op->d_hash()
	 * routine may choose to leave the hash value unchanged.
	 */
	name->hash = full_name_hash(name->name, name->len);
	if (dir->d_op && dir->d_op->d_hash)
	{
		if (dir->d_op->d_hash(dir, name) != 0)
			goto out;
	}

	dentry = d_lookup(dir, name);
	if (dentry)
	{
		if (dentry->d_inode)
			ino = dentry->d_inode->i_ino;
		dput(dentry);
	}
out:
	return ino;
}

void __init dcache_init(void)
{
	int i;
	struct list_head *d = dentry_hashtable;

	/* 
	 * A constructor could be added for stable state like the lists,
	 * but it is probably not worth it because of the cache nature
	 * of the dcache. 
	 * If fragmentation is too bad then the SLAB_HWCACHE_ALIGN
	 * flag could be removed here, to hint to the allocator that
	 * it should not try to get multiple page regions.  
	 */
	dentry_cache = kmem_cache_create("dentry_cache",
					 sizeof(struct dentry),
					 0,
					 SLAB_HWCACHE_ALIGN,
					 NULL, NULL);
	if (!dentry_cache)
		panic("Cannot create dentry cache");

	i = D_HASHSIZE;
	do {
		INIT_LIST_HEAD(d);
		d++;
		i--;
	} while (i);
}
