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

void d_free(struct dentry *dentry)
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
			if (list_empty(&dentry->d_hash)) {
				struct inode *inode = dentry->d_inode;
				struct dentry * parent;
				if (inode) {
					list_del(&dentry->d_alias);
					iput(inode);
				}
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
 * Shrink the dcache. This is done when we need
 * more memory, or simply when we need to unmount
 * something (at which point we need to unuse
 * all dentries).
 */
void shrink_dcache(void)
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

				list_del(&dentry->d_alias);
				dentry->d_inode = NULL;
				iput(inode);
			}
			parent = dentry->d_parent;
			d_free(dentry);
			dput(parent);
		}
	}
}

#define NAME_ALLOC_LEN(len)	((len+16) & ~15)

struct dentry * d_alloc(struct dentry * parent, const struct qstr *name)
{
	char * str;
	struct dentry *dentry;

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

	dentry->d_count = 0;
	dentry->d_flags = 0;
	dentry->d_inode = NULL;
	dentry->d_parent = parent;
	dentry->d_mounts = dentry;
	dentry->d_covers = dentry;
	INIT_LIST_HEAD(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_alias);
	INIT_LIST_HEAD(&dentry->d_lru);

	dentry->d_name.name = str;
	dentry->d_name.len = name->len;
	dentry->d_name.hash = name->hash;
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
		res->d_parent = res;
		res->d_count = 2;
		d_instantiate(res, root_inode);
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
		if (dentry->d_name.len != len)
			continue;
		if (dentry->d_parent != parent)
			continue;
		if (memcmp(dentry->d_name.name, str, len))
			continue;
		return dentry;
	}
	return NULL;
}

struct dentry * d_lookup(struct dentry * dir, struct qstr * name)
{
	return __dlookup(d_hash(dir, name->hash), dir, name);
}

static inline void d_insert_to_parent(struct dentry * entry, struct dentry * parent)
{
	list_add(&entry->d_hash, d_hash(dget(parent), entry->d_name.hash));
}

static inline void d_remove_from_parent(struct dentry * dentry, struct dentry * parent)
{
	list_del(&dentry->d_hash);
	dput(parent);
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

		dentry->d_inode = NULL;
		list_del(&dentry->d_alias);
		iput(inode);
		return;
	}

	/*
	 * If not, just unhash us and wait for dput()
	 * to pick up the tab..
	 */
	list_del(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_hash);

}

void d_add(struct dentry * entry, struct inode * inode)
{
	d_insert_to_parent(entry, entry->d_parent);
	d_instantiate(entry, inode);
}

static inline void alloc_new_name(struct dentry * entry, struct qstr *newname)
{
	int len = newname->len;
	int hash = newname->hash;
	char *name = (char *) entry->d_name.name;

	if (NAME_ALLOC_LEN(len) != NAME_ALLOC_LEN(entry->d_name.len)) {
		name = kmalloc(NAME_ALLOC_LEN(len), GFP_KERNEL);
		if (!name)
			printk("out of memory for dcache\n");
		kfree(entry->d_name.name);
		entry->d_name.name = name;
	}
	memcpy(name, newname->name, len);
	name[len] = 0;
	entry->d_name.len = len;
	entry->d_name.hash = hash;
}

void d_move(struct dentry * dentry, struct dentry * newdir, struct qstr * newname)
{
	if (!dentry)
		return;

	if (!dentry->d_inode)
		printk("VFS: moving negative dcache entry\n");

	d_remove_from_parent(dentry, dentry->d_parent);
	alloc_new_name(dentry, newname);
	dentry->d_parent = newdir;
	d_insert_to_parent(dentry, newdir);
}

int d_path(struct dentry * entry, struct dentry * chroot, char * buf)
{
	if (IS_ROOT(entry) || (chroot && entry == chroot)) {
		*buf = '/';
		return 1;
	} else {
		int len = d_path(entry->d_parent, chroot, buf);

		buf += len;
		if (len > 1) {
			*buf++ = '/';
			len++;
		}
		memcpy(buf, entry->d_name.name, entry->d_name.len);
		return len + entry->d_name.len;
	}
}

void dcache_init(void)
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
