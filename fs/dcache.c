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
struct list_head dentry_hashtable[D_HASHSIZE];

void d_free(struct dentry *dentry)
{
	if (dentry) {
		kfree(dentry->d_name.name);
		kfree(dentry);
	}
}

void dput(struct dentry *dentry)
{
repeat:
	if (dentry) {
		dentry->d_count--;
		if (dentry->d_count < 0) {
			printk("dentry->count = %d for %s\n",
				dentry->d_count, dentry->d_name.name);
			return;
		}
		/*
		 * This is broken right now: we should really put
		 * the dentry on a free list to be reclaimed later
		 * when we think we should throw it away.
		 *
		 * Instead we free it completely if the inode count
		 * indicates that we're the only ones holding onto
		 * the inode - if not we just fall back on the old
		 * (broken) behaviour of not reclaiming it at all.
		 */
		if (!dentry->d_count && (!dentry->d_inode || atomic_read(&dentry->d_inode->i_count) == 1)) {
			struct dentry *parent = dentry->d_parent;

			if (parent != dentry) {
				struct inode * inode = dentry->d_inode;

				if (inode) {
					list_del(&dentry->d_list);
					iput(inode);
					dentry->d_inode = NULL;
				}
				list_del(&dentry->d_hash);
				d_free(dentry);
				dentry = parent;
				goto repeat;
			}
		}
	}
}

#define NAME_ALLOC_LEN(len)	((len+16) & ~15)

struct dentry * d_alloc(struct dentry * parent, const struct qstr *name)
{
	char *str;
	struct dentry *res;

	res = kmalloc(sizeof(struct dentry), GFP_KERNEL);
	if (!res)
		return NULL;

	str = kmalloc(NAME_ALLOC_LEN(name->len), GFP_KERNEL);
	if (!str) {
		kfree(res);
		return NULL;
	}
	
	memcpy(str, name->name, name->len);
	str[name->len] = 0;

	memset(res, 0, sizeof(struct dentry));

	res->d_parent = parent;
	res->d_mounts = res;
	res->d_covers = res;
	res->d_flags = 0;

	res->d_name.name = str;
	res->d_name.len = name->len;
	res->d_name.hash = name->hash;
	return res;
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
		list_add(&entry->d_list, &inode->i_dentry);

	entry->d_inode = inode;
}

struct dentry * d_alloc_root(struct inode * root_inode, struct dentry *old_root)
{
	struct dentry *res = NULL;

	if (root_inode) {
		res = d_alloc(NULL, &(const struct qstr) { "/", 1, 0 });
		res->d_parent = res;
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
