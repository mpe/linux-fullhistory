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

void dput(struct dentry *dentry)
{
	if (dentry) {
		dentry->d_count--;
		if (dentry->d_count < 0) {
			printk("dentry->count = %d for %s\n",
				dentry->d_count, dentry->d_name.name);
			return;
		}
		if (!dentry->d_count) {
			list_del(&dentry->d_lru);
			list_add(&dentry->d_lru, &dentry_unused);
		}
	}
}

void d_free(struct dentry *dentry)
{
	kfree(dentry->d_name.name);
	kfree(dentry);
}

/*
 * Note! This tries to free the last entry on the dentry
 * LRU list. The dentries are put on the LRU list when
 * they are free'd, but that doesn't actually mean that
 * all LRU entries have d_count == 0 - it might have been
 * re-allocated. If so we delete it from the LRU list
 * here.
 *
 * Rationale:
 * - keep "dget()" extremely simple
 * - if there have been a lot of lookups in the LRU list
 *   we want to make freeing more unlikely anyway, and
 *   keeping used dentries on the LRU list in that case
 *   will make the algorithm less likely to free an entry.
 */
static inline struct dentry * free_one_dentry(struct dentry * dentry)
{
	struct dentry * parent;

	list_del(&dentry->d_hash);
	parent = dentry->d_parent;
	if (parent != dentry)
		dput(parent);
	return dentry;
}

static inline struct dentry * try_free_one_dentry(struct dentry * dentry)
{
	struct inode * inode = dentry->d_inode;

	if (inode) {
		if (atomic_read(&inode->i_count) != 1) {
			list_add(&dentry->d_lru, &dentry_unused);
			return NULL;
		}
		list_del(&dentry->d_alias);
		iput(inode);
		dentry->d_inode = NULL;
	}
	return free_one_dentry(dentry);
}

static struct dentry * try_free_dentries(void)
{
	struct list_head * tmp = dentry_unused.next;

	if (tmp != &dentry_unused) {
		struct dentry * dentry;

		list_del(tmp);
		dentry = list_entry(tmp, struct dentry, d_lru);
		if (dentry->d_count == 0)
			return try_free_one_dentry(dentry);
	}
	return NULL;
}

#define NAME_ALLOC_LEN(len)	((len+16) & ~15)

struct dentry * d_alloc(struct dentry * parent, const struct qstr *name)
{
	int len;
	char * str;
	struct dentry *dentry;

	dentry = try_free_dentries();
	len = NAME_ALLOC_LEN(name->len);
	if (dentry) {
		str = (char *) dentry->d_name.name;
		if (len == NAME_ALLOC_LEN(dentry->d_name.len))
			goto right_size;
		kfree(dentry->d_name.name);
	} else {
		dentry = kmalloc(sizeof(struct dentry), GFP_KERNEL);
		if (!dentry)
			return NULL;
	}
	str = kmalloc(len, GFP_KERNEL);
	if (!str) {
		kfree(dentry);
		return NULL;
	}
right_size:
	len = name->len;
	memcpy(str, name->name, len);
	str[len] = 0;

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
 * Remove the inode from the dentry.. This removes
 * it from the parent hashes but otherwise leaves it
 * around - it may be a "zombie", part of a path
 * that is still in use...
 *
 * "The Night of the Living Dead IV - the Dentry"
 */
void d_delete(struct dentry * dentry)
{
	list_del(&dentry->d_hash);
	/*
	 * Make the hash lists point to itself.. When we
	 * later dput this, we want the list_del() there
	 * to not do anything strange..
	 */
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
