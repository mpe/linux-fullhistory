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
#include <linux/init.h>

/* For managing the dcache */
extern int nr_free_pages, free_pages_low;

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

static inline void d_free(struct dentry *dentry)
{
	kfree(dentry->d_name.name);
	kfree(dentry);
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
	if (dentry) {
		int count;
repeat:
		count = dentry->d_count-1;
		if (count < 0) {
			printk("Negative d_count (%d) for %s/%s\n",
				count,
				dentry->d_parent->d_name.name,
				dentry->d_name.name);
			*(int *)0 = 0;
		}
		dentry->d_count = count;
		if (!count) {
			list_del(&dentry->d_lru);
			if (dentry->d_op && dentry->d_op->d_delete)
				dentry->d_op->d_delete(dentry);
			if (list_empty(&dentry->d_hash)) {
				struct inode *inode = dentry->d_inode;
				struct dentry * parent;
				if (inode)
					iput(inode);
				parent = dentry->d_parent;
				d_free(dentry);
				if (dentry == parent)
					return;
				dentry = parent;
				goto repeat;
			}
			list_add(&dentry->d_lru, &dentry_unused);
		}
	}
}

/*
 * Try to invalidate the dentry if it turns out to be
 * possible. If there are other users of the dentry we
 * can't invalidate it.
 *
 * This is currently incorrect. We should try to see if
 * we can invalidate any unused children - right now we
 * refuse to invalidate way too much.
 */
int d_invalidate(struct dentry * dentry)
{
	/* We should do a partial shrink_dcache here */
	if (dentry->d_count != 1)
		return -EBUSY;

	d_drop(dentry);
	return 0;
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
		list_del(tmp);
		INIT_LIST_HEAD(tmp);
		dentry = list_entry(tmp, struct dentry, d_lru);
		if (!dentry->d_count) {
			struct dentry * parent;

			list_del(&dentry->d_hash);
			if (dentry->d_inode) {
				struct inode * inode = dentry->d_inode;

				dentry->d_inode = NULL;
				iput(inode);
			}
			parent = dentry->d_parent;
			d_free(dentry);
			dput(parent);
			if (!--count)
				break;
		}
	}
}

#define NAME_ALLOC_LEN(len)	((len+16) & ~15)

struct dentry * d_alloc(struct dentry * parent, const struct qstr *name)
{
	char * str;
	struct dentry *dentry;

	/*
	 * Check whether to shrink the dcache ... this greatly reduces
	 * the likelyhood that kmalloc() will need additional memory.
	 */
	if (nr_free_pages < free_pages_low)
		shrink_dcache();

	dentry = kmalloc(sizeof(struct dentry), GFP_KERNEL);
	if (!dentry)
		return NULL;

	str = kmalloc(NAME_ALLOC_LEN(name->len), GFP_KERNEL);
	if (!str) {
		kfree(dentry);
		return NULL;
	}

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
	}
	dentry->d_mounts = dentry;
	dentry->d_covers = dentry;
	INIT_LIST_HEAD(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_lru);

	dentry->d_name.name = str;
	dentry->d_name.len = name->len;
	dentry->d_name.hash = name->hash;
	dentry->d_op = NULL;
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

static inline struct dentry * __dlookup(struct list_head *head, struct dentry * parent, struct qstr * name)
{
	struct list_head *tmp = head->next;
	int len = name->len;
	int hash = name->hash;
	const unsigned char *str = name->name;

	while (tmp != head) {
		struct dentry * dentry = list_entry(tmp, struct dentry, d_hash);

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
		return dget(dentry->d_mounts);
	}
	return NULL;
}

struct dentry * d_lookup(struct dentry * dir, struct qstr * name)
{
	return __dlookup(d_hash(dir, name->hash), dir, name);
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
	struct list_head *base = d_hash(dparent, hash);
	struct list_head *lhp = base;

	while ((lhp = lhp->next) != base) {
		if (dentry == list_entry(lhp, struct dentry, d_hash))
			goto found_it;
	}

	/* Special case, local mount points don't live in the hashes.
	 * So if we exhausted the chain, search the super blocks.
	 */
	if (dentry && dentry == dparent) {
		struct super_block *sb;

		for (sb = super_blocks + 0; sb < super_blocks + NR_SUPER; sb++) {
			if (sb->s_root == dentry)
				goto found_it;
		}
	}
	return 0;
found_it:
	return	(dentry->d_parent == dparent) &&
		(dentry->d_name.hash == hash) &&
		(dentry->d_name.len == len);
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
		struct inode * inode = dentry->d_inode;
		if (inode) {
			dentry->d_inode = NULL;
			iput(inode);
		}
		return;
	}

	/*
	 * If not, just drop the dentry and let dput
	 * pick up the tab..
	 */
	d_drop(dentry);
}

void d_add(struct dentry * entry, struct inode * inode)
{
	struct dentry * parent = entry->d_parent;

	list_add(&entry->d_hash, d_hash(parent, entry->d_name.hash));
	d_instantiate(entry, inode);
}

#define switch(x,y) do { \
	__typeof__ (x) __tmp = x; \
	x = y; y = __tmp; } while (0)

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
		printk("VFS: moving negative dcache entry\n");

	/* Move the dentry to the target hash queue */
	list_del(&dentry->d_hash);
	list_add(&dentry->d_hash, &target->d_hash);

	/* Unhash the target: dput() will then get rid of it */
	list_del(&target->d_hash);
	INIT_LIST_HEAD(&target->d_hash);

	/* Switch the parents and the names.. */
	switch(dentry->d_parent, target->d_parent);
	switch(dentry->d_name.name, target->d_name.name);
	switch(dentry->d_name.len, target->d_name.len);
	switch(dentry->d_name.hash, target->d_name.hash);
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

__initfunc(void dcache_init(void))
{
	int i;
	struct list_head *d = dentry_hashtable;

	i = D_HASHSIZE;
	do {
		INIT_LIST_HEAD(d);
		d++;
		i--;
	} while (i);
}
