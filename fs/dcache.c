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
#include <linux/dalloc.h>
#include <linux/dlists.h>
#include <linux/malloc.h>

/* this should be removed after the beta phase */
/*#define DEBUG*/
/*#undef DEBUG*/
/*#define DEBUG_DDIR_COUNT*/

void printpath(struct dentry * entry);

DEF_INSERT(alias,struct dentry,d_next,d_prev)
DEF_REMOVE(alias,struct dentry,d_next,d_prev)

DEF_INSERT(hash,struct dentry,d_hash_next,d_hash_prev)
DEF_REMOVE(hash,struct dentry,d_hash_next,d_hash_prev)

struct dentry * the_root = NULL;

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
struct dentry * dentry_hashtable[D_HASHSIZE];

static inline unsigned long dentry_hash(struct dentry *dir, int name_hash)
{
	unsigned long hash = name_hash + (unsigned long) dir;
	hash = hash ^ (hash >> D_HASHBITS) ^ (hash >> D_HASHBITS*2);
	return hash & D_HASHMASK;
}

unsigned long name_cache_init(unsigned long mem_start, unsigned long mem_end)
{
	return mem_start;
}

#ifdef DEBUG
/* throw this away after the beta phase */
/*************************************************************************/
extern void xcheck(char * txt, struct inode * p);

static int x_alloc = 0;
static int x_freed = 0;
static int x_free = 0;

static void * tst[20000];
static int cnt = 0;

static void ins(void* ptr)
{
	extern int inodes_stat;
	tst[cnt++] = ptr;
        if(cnt % 1000 == 0)
		printk("------%d allocated: %d: %d %d %d\n", inodes_stat, cnt,
		       x_alloc, x_freed, x_free);
	if (cnt>=20000) panic("stop");
}

#if 0
static inline int search(void* ptr)
{
	int i;
	for(i = cnt-1; i>=0; i--)
		if (tst[i] == ptr)
			return i;
	return -1;
}

#define TST(n,x) if(search(x)<0) printk("%s bad ptr %p line %d\n", n, x, __LINE__)
#else
#define TST(n,x) /*nothing*/
#endif

void LOG(char * txt, struct dentry * entry)
{
	static int count = 0;
	if (entry) {
		TST(txt,entry);
	}
	if (count) {
		count--;
		printk("%s: entry=%p\n", txt, entry);
	}
}

#ifdef DEBUG_DDIR_COUNT
void recursive_test(struct dentry * entry)
{
}
#else
#define recursive_test(e) /*nothing*/
#endif
#else
#define TST(n,x) /*nothing*/
#define LOG(n,x) /*nothing*/
#define xcheck(t,i) /*nothing*/
#define recursive_test(e) /*nothing*/
/*****************************************************************************/
#endif

void printpath(struct dentry * entry)
{
	if (!IS_ROOT(entry))
		printpath(entry->d_parent);
	printk("/%s", entry->d_name.name);
}

void d_free(struct dentry *dentry)
{
	if (dentry) {
		kfree(dentry->d_name.name);
		kfree(dentry);
	}
}

#define NAME_ALLOC_LEN(len)	((len+16) & ~15)

struct dentry * d_alloc(struct dentry * parent, struct qstr *name, int isdir)
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
	res->d_flag = isdir ? D_DIR : 0;

	res->d_name.name = str;
	res->d_name.len = name->len;
	res->d_name.hash = name->hash;

#ifdef DEBUG
	x_alloc++;
#endif
	return res;
}

extern blocking struct dentry * d_alloc_root(struct inode * root_inode, struct dentry *old_root)
{
	struct dentry *res;
	struct qstr name = { "/", 1, 0 };	/* dummy qstr */

	if (!root_inode)
		return NULL;
	res = d_alloc(NULL, &name, 1);
	LOG("d_alloc_root", res);
	res->d_parent = res;
	d_instantiate(res, root_inode, D_DIR);
	return res;
}

static inline struct dentry ** d_base_qstr(struct dentry * parent, struct qstr * s)
{
	return dentry_hashtable + dentry_hash(parent, s->hash);
}

static inline struct dentry ** d_base_entry(struct dentry * pdir, struct dentry * entry)
{
	return d_base_qstr(pdir, &entry->d_name);
}


static /*inline*/ blocking void _d_remove_from_parent(struct dentry * entry,
						      struct dentry * parent)
{
	if (entry->d_flag & D_HASHED) {
		struct dentry ** base = d_base_entry(parent, entry);

		remove_hash(base, entry);
		entry->d_flag &= ~D_HASHED;
	}
}

static inline struct dentry * __dlookup(struct dentry * base, struct dentry * parent, struct qstr * name)
{
	if (base) {
		struct dentry * tmp = base;
		int len = name->len;
		int hash = name->hash;
		const unsigned char *str = name->name;

		do {
			if (tmp->d_name.hash == hash		&&
			    tmp->d_name.len == len		&&
			    tmp->d_parent == parent		&&
			   !(tmp->d_flag & D_DUPLICATE)		&&
			   !memcmp(tmp->d_name.name, str, len))
				return tmp;
			tmp = tmp->d_hash_next;
		} while(tmp != base);
	}
	return NULL;
}

struct dentry * d_lookup(struct dentry * dir, struct qstr * name)
{
	struct dentry ** base = d_base_qstr(dir, name);

	return __dlookup(*base, dir, name);
}

static /*inline*/ blocking void _d_insert_to_parent(struct dentry * entry,
						    struct dentry * parent,
						    struct inode * inode,
						    int flags)
{
	struct dentry ** base;

	base = d_base_qstr(parent, &entry->d_name);
	if (entry->d_flag & D_HASHED) {
		printk("VFS: dcache entry is already hashed\n");
		return;
	}
	insert_hash(base, entry);
	entry->d_flag |= D_HASHED;
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
void d_instantiate(struct dentry *entry, struct inode * inode, int flags)
{
	entry->d_flag = (entry->d_flag & ~D_NEGATIVE) | flags;

	if (inode && !(flags & D_NEGATIVE)) {
		if (entry->d_flag & D_DIR) {
			if (inode->i_dentry) {
				printk("VFS: creating dcache directory alias\n");
				return;
			}
		}
		insert_alias(&inode->i_dentry, entry);
	}

	entry->d_inode = inode;
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
	struct inode * inode = dentry->d_inode;

	_d_remove_from_parent(dentry, dentry->d_parent);
	if (inode) {
		remove_alias(&inode->i_dentry, dentry);
		dentry->d_inode = NULL;
		iput(inode);
	}
}

blocking void d_add(struct dentry * entry, struct inode * inode, int flags)
{
	struct dentry * parent = entry->d_parent;

#ifdef DEBUG
	if (inode)
		xcheck("d_add", inode);
	if (IS_ROOT(entry)) {
		printk("VFS: d_add for root dentry ");
		printpath(entry);
		printk(" -> ");
		printk("\n");
		return;
	}
	if (!parent)
		panic("d_add with parent==NULL");
	LOG("d_add", entry);
#endif
        if(entry->d_flag & D_HASHED)
		printk("VFS: d_add of already added dcache entry\n");

	_d_insert_to_parent(entry, parent, inode, flags);
	d_instantiate(entry, inode, flags);
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

static inline void d_remove_old_parent(struct dentry * entry)
{
	struct dentry * parent;
	struct inode * inode;

	parent = entry->d_parent;
	inode = entry->d_inode;
	_d_remove_from_parent(entry, parent);
}

static inline void d_add_new_parent(struct dentry * entry, struct dentry * parent)
{
	struct inode * inode;

	entry->d_parent = parent;
	inode = entry->d_inode;

	_d_insert_to_parent(entry, parent, inode, entry->d_flag);
}


blocking void d_move(struct dentry * entry, struct dentry * newdir, struct qstr * newname)
{
	struct inode * inode;
	int flags;

	if (!entry)
		return;
	inode = entry->d_inode;
	flags = entry->d_flag;
	if (!inode) {
		printk("VFS: moving negative dcache entry\n");
	}

	if (flags & D_ZOMBIE) {
		printk("VFS: moving zombie entry\n");
	}

	d_remove_old_parent(entry);
	alloc_new_name(entry, newname);
	d_add_new_parent(entry, newdir);
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
