/*
 * fs/dcache.c
 *
 * Complete reimplementation
 * (C) 1997 Thomas Schoebel-Theuer
 */

/* The new dcache is exclusively called from the VFS, not from
 * the specific fs'es any more. Despite having the same name as in the
 * old code, it has less to do with it.
 *
 * It serves many purposes:
 *
 *  1) Any inode that has been retrieved with lookup() and is in use
 *     (i_count>0), has access to its full absolute path name, by going
 *     to inode->i_dentry and then recursively following the entry->d_parent
 *     chain. Use d_path() as predefined method for that.
 *     You may find out the corresponding inode belonging to
 *     a dentry by calling d_inode(). This can be used as an easy way for
 *     determining .. and its absolute pathname, an old UNIX problem that
 *     deserved a solution for a long time.
 *     Note that hardlinked inodes may have multiple dentries assigned to
 *     (via the d_next chain), reflecting multiple alias pathnames.
 *
 *  2) If not disabled by filesystem types specifying FS_NO_DCACHE,
 *     the dentries of unused (aged) inodes are retained for speeding up
 *     lookup()s, by allowing hashed inquiry starting from the dentry of
 *     the parent directory.
 *
 *  3) It can remeber so-called "negative entries", that is dentries for
 *     pathnames that are known to *not* exist, so unneccessary repeated
 *     lookup()s for non-existant names can be saved.
 *
 *  4) It provides a means for keeping deleted files (inode->i_nlink==0)
 *     accessible in the so-called *basket*. Inodes in the basket have been
 *     removed with unlink() while being in use (i_count>0), so they would
 *     normally use up space on the disk and be accessile through their
 *     filedescriptor, but would not be accessible for lookup() any more.
 *     The basket simply keeps such files in the dcache (for potential
 *     dcache lookup) until they are either eventually removed completely,
 *     or transferred to the second-level basket, the so-called *ibasket*.
 *     The ibasket is implemented in the new inode code, on request of
 *     filesystem types that have the flag FS_IBASKET set, and proliferates
 *     the unlinked files when i_count has gone to zero, at least as long
 *     as there is space on the disk and enough inodes remain available
 *     and no umount() has started.
 *
 *  5) Preliminary dentries can be added by readdir(). While normal dentries
 *     directly point to the inode via u.d_inode only the inode number is
 *     known from readdir(), but not more. They can be converted to
 *     normal dentries by using d_inode().
 */

/*
 * Notes on the allocation strategy:
 *
 * The dcache is a full slave cache of the inodes. Whenever an inode
 * is cleared, all the dentries associated with it will recursively
 * disappear. dentries have no own reference counting; this has to
 * be obeyed for SMP.
 * If directories could go out of inode cache while
 * successors are alive, this would interrupt the d_parent chain of
 * the live successors. To prevent this without using zombies, all
 * directories are thus prevented from __iput() as long as successors
 * are alive.
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/dalloc.h>
#include <linux/dlists.h>

/* this should be removed after the beta phase */
/* #define DEBUG */
/*#undef DEBUG*/
/* #define DEBUG_DDIR_COUNT */

#define D_HASHSIZE   64

/* local flags for d_flag */
#define D_DIR          32
#define D_HASHED       64
#define D_ZOMBIE      128
#define D_PRELIMINARY 256
#define D_INC_DDIR    512

/* local flags for d_del() */
#define D_RECURSIVE 4
#define D_NO_FREE   8

/* adjust these constants if you know a probability distribution ... */
#define D_SMALL 16
#define D_MEDIUM 64
#define D_LARGE 256
#define D_HUGE D_MAXLEN

#define BASE_DHEADER(x) (struct dheader*)((unsigned long)(x) & ~(PAGE_SIZE-1))
#define BYTE_ADD(x,n) (void*)((char*)(x) + (n))
#define BYTE_SUB(x,n) (void*)((char*)(x) - (n))

/* This is for global allocation of dentries.  Remove this when
 * converting to SLAB.
 */
struct dheader {
	struct dentry * emptylist;
	short free, maxfree;
	struct dheader * next;
	struct dheader * prev;
};

struct anchors {
	struct dheader * free; /* each contains at least 1 empty dentry */
	struct dheader * full; /* all the used up ones */
	struct dheader * dir_free; 
	struct dheader * dir_full; 
};

/* This is only used for directory dentries. Think of it as an extension
 * of the dentry.
 * It is defined as separate struct, so it uses up space only
 * where necessary.
 */
struct ddir {
	struct dentry * dd_hashtable[D_HASHSIZE];
	struct dentry * dd_neglist;
	struct dentry * dd_basketlist;
	struct dentry * dd_zombielist;
	unsigned short dd_alloced; /* # d_alloc()ed, but not yet d_add()ed */
	unsigned short dd_hashed;  /* # of entries in hashtable */
	unsigned short dd_true_hashed; /* # non-preliminaries in hashtable */
	unsigned short dd_negs; /* # of negative entries */
};

DEF_INSERT(header,struct dheader,next,prev)
DEF_REMOVE(header,struct dheader,next,prev)

DEF_INSERT(alias,struct dentry,d_next,d_prev)
DEF_REMOVE(alias,struct dentry,d_next,d_prev)

DEF_INSERT(hash,struct dentry,d_hash_next,d_hash_prev)
DEF_REMOVE(hash,struct dentry,d_hash_next,d_hash_prev)

DEF_INSERT(basket,struct dentry,d_basket_next,d_basket_prev)
DEF_REMOVE(basket,struct dentry,d_basket_next,d_basket_prev)

static struct anchors anchors[4];

struct dentry * the_root = NULL;

unsigned long name_cache_init(unsigned long mem_start, unsigned long mem_end)
{
	memset(anchors, 0, sizeof(anchors));
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
	if(cnt>=20000) panic("stop");
}

#if 0
static inline int search(void* ptr)
{
	int i;
	for(i = cnt-1; i>=0; i--)
		if(tst[i] == ptr)
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
	if(entry) {
		TST(txt,entry);
	}
	if(count) {
		count--;
		printk("%s: entry=%p\n", txt, entry);
	}
}

#ifdef DEBUG_DDIR_COUNT
static struct ddir * d_dir(struct dentry * entry);
void recursive_test(struct dentry * entry)
{
	int i;
	struct ddir * ddir = d_dir(entry);
	int sons = 0;

	if(ddir->dd_zombielist)
		sons++;
	for(i=0; i < D_HASHSIZE; i++) {
		struct dentry ** base = &ddir->dd_hashtable[i];
		struct dentry * tmp = *base;
		if(tmp) do {
			TST("__clear",tmp);
			if(!(tmp->d_flag & D_HASHED)) {
				printk("VFS: dcache entry not hashed!\n");
				printpath(*base); printk("\n");
				printpath(tmp);
			}
			if(!(tmp->d_flag & D_PRELIMINARY))
				sons++;
			if(tmp->d_flag & D_DIR)
				recursive_test(tmp);
			tmp = tmp->d_hash_next;
		} while(tmp && tmp != *base);
	}
	if(!sons && !(entry->d_flag & D_PRELIMINARY) && entry->u.d_inode) {
		struct inode * inode = entry->u.d_inode;
		if(!atomic_read(&inode->i_count)) {
			if(!(inode->i_status & 1/*ST_AGED*/)) {
				printpath(entry);
				printk(" is not aged!\n");
			}
			if(inode->i_ddir_count) {
				printpath(entry);
				printk(" has ddir_count blockage!\n");
			}
		}
	}
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
	if(!IS_ROOT(entry))
		printpath(entry->d_parent);
	printk("/%s", entry->d_name);
}

static inline long has_sons(struct ddir * ddir)
{
	return ((ddir->dd_alloced | ddir->dd_hashed)	||
		ddir->dd_neglist			||
		ddir->dd_basketlist			||
		ddir->dd_zombielist);
}

static inline int has_true_sons(struct ddir * ddir)
{
	return (ddir->dd_alloced | ddir->dd_true_hashed);
}

/* Only hold the i_ddir_count pseudo refcount when neccessary (i.e. when
 * they have true_sons), to prevent keeping too much dir inodes in use.
 */
static inline void inc_ddir(struct dentry * entry, struct inode * inode)
{
	if(!(entry->d_flag & D_INC_DDIR)) {
		entry->d_flag |= D_INC_DDIR;
#ifdef DEBUG
		if(inode->i_ddir_count) {
			printpath(entry);
			printk(" ddir_count=%d\n", inode->i_ddir_count);
		}
#endif
		inode->i_ddir_count++;
		_get_inode(inode);
	}
}

static inline blocking void dec_ddir(struct dentry * entry, struct inode * inode)
{
	if(entry->d_flag & D_INC_DDIR) {
		entry->d_flag &= ~D_INC_DDIR;
		inode->i_ddir_count--;
		if(!inode->i_ddir_count)
			__iput(inode);
	}
}

/* Do not inline this many times. */
static void d_panic(void)
{
	panic("VFS: dcache directory corruption");
}

static inline struct ddir * d_dir(struct dentry * entry)
{
	struct ddir * res = BYTE_SUB(entry, sizeof(struct ddir));

	if(!(entry->d_flag & D_DIR))
		d_panic();
#ifdef DEBUG
	if(!entry)
		panic("entry NULL!");
	if(BASE_DHEADER(res) != BASE_DHEADER(entry))
		printk("Scheisse!!!\n");
#endif
	return res;
}

static /*inline*/ struct dheader * dinit(int isdir, int size)
{
	struct dheader * res = (struct dheader*)__get_free_page(GFP_KERNEL);
	int restlen = PAGE_SIZE - sizeof(struct dheader);
	struct dentry * ptr = BYTE_ADD(res, sizeof(struct dheader));

	if(!res)
		return NULL;
        memset(res, 0, sizeof(struct dheader));
	if(isdir) {
		ptr = BYTE_ADD(ptr, sizeof(struct ddir));
		size += sizeof(struct ddir);
	}
	if(BASE_DHEADER(ptr) != res) 
		panic("Bad kernel page alignment");
	size += sizeof(struct dentry) - D_MAXLEN;
	res->emptylist = NULL;
	res->free = 0;
	while(restlen >= size) {
#ifdef DEBUG
		ins(ptr);
		if(BASE_DHEADER(ptr) != res)
			panic("Wrong dinit!");
#endif
		ptr->d_next = res->emptylist;
		res->emptylist = ptr;
		ptr = BYTE_ADD(ptr, size);
		res->free++;
		restlen -= size;
	}
	res->maxfree = res->free;
	return res;
}

static /*inline*/ struct dentry * __dalloc(struct anchors * anchor,
					   struct dentry * parent, int isdir,
					   int len, int size)
{
	struct dheader ** free = isdir ? &anchor->dir_free : &anchor->free;
	struct dheader ** full = isdir ? &anchor->dir_full : &anchor->full;
	struct dheader * base = *free;
	struct dentry * res;

	if(!base) {
		base = dinit(isdir, size);
		if(!base)
			return NULL;
		insert_header(free, base);
	}
	base->free--;
	res = base->emptylist;
	if(!(base->emptylist = res->d_next)) {
		remove_header(free, base);
		insert_header(full, base);
	}
	memset(res, 0, sizeof(struct dentry) - D_MAXLEN);
	if(isdir) {
		res->d_flag = D_DIR;
		memset(d_dir(res), 0, sizeof(struct ddir));
	}
	res->d_len = len;
	res->d_parent = parent;
	if(parent) {
		struct ddir * pdir = d_dir(parent);
#ifdef DEBUG
		if(pdir->dd_alloced > 1 && !IS_ROOT(parent)) {
			printpath(parent);
			printk(" dd_alloced=%d\n", pdir->dd_alloced);
		}
#endif
		pdir->dd_alloced++;
	}
#ifdef DEBUG
	x_alloc++;
#endif
	return res;
}

struct dentry * d_alloc(struct dentry * parent, int len, int isdir)
{
	int i, size;

#ifdef DEBUG
        if(the_root)
	        recursive_test(the_root);
	LOG("d_alloc", parent);
#endif
	if(len >= D_MEDIUM) {
		if(len >= D_LARGE) {
			i = 3;
			size = D_HUGE;
		} else {
			i = 2;
			size = D_LARGE;
		} 
	} else if(len >= D_SMALL) {
		i = 1;
		size = D_MEDIUM;
	} else {
		i = 0;
		size = D_SMALL;
	}
	return __dalloc(&anchors[i], parent, isdir, len, size);
}

extern blocking struct dentry * d_alloc_root(struct inode * root_inode)
{
	struct dentry * res = the_root;

	if(res) {
		d_del(res, D_NO_CLEAR_INODE); /* invalidate everything beyond */
	} else {
		struct ddir * ddir;

		the_root = res = d_alloc(NULL, 0, 1);
		LOG("d_alloc_root", res);
		res->d_parent = res;
		res->d_name[0]='\0';
		ddir = d_dir(res);
		ddir->dd_alloced = 999; /* protect from deletion */
	}
	insert_alias(&root_inode->i_dentry, res);
	root_inode->i_dent_count++;
	root_inode->i_ddir_count++;
	res->u.d_inode = root_inode;
	return res;
}

static inline unsigned long d_hash(char first, char last)
{
	return ((unsigned long)first ^ ((unsigned long)last << 4)) & (D_HASHSIZE-1);
}

static inline struct dentry ** d_base_entry(struct ddir * pdir, struct dentry * entry)
{
	return &pdir->dd_hashtable[d_hash(entry->d_name[0],
					  entry->d_name[entry->d_len-1])];
}

static inline struct dentry ** d_base_qstr(struct ddir * pdir,
					   struct qstr * s1,
					   struct qstr * s2)
{
	unsigned long hash;

	if(s2 && s2->len) {
		hash = d_hash(s1->name[0], s2->name[s2->len-1]);
	} else {
		hash = d_hash(s1->name[0], s1->name[s1->len-1]);
	}
	return &pdir->dd_hashtable[hash];
}


static /*inline*/ blocking void _d_remove_from_parent(struct dentry * entry,
						      struct ddir * pdir,
						      struct inode * inode,
						      int flags)
{
	if(entry->d_flag & D_HASHED) {
		struct dentry ** base = d_base_entry(pdir, entry);

		remove_hash(base, entry);
		entry->d_flag &= ~D_HASHED;
		pdir->dd_hashed--;
		if(!(entry->d_flag & D_PRELIMINARY)) {
			pdir->dd_true_hashed--;
			if(!inode) {
#ifdef DEBUG
				if(!entry->d_next || !entry->d_prev) {
					printpath(entry);
					printk(" flags=%x d_flag=%x negs=%d "
					       "hashed=%d\n", flags, entry->d_flag,
					       pdir->dd_negs, pdir->dd_hashed);
				}
#endif
				remove_alias(&pdir->dd_neglist, entry);
				pdir->dd_negs--;
			}
		}
	} else if(!(entry->d_flag & D_ZOMBIE)) {
#ifdef DEBUG
		if(!pdir->dd_alloced) printk("dd_alloced is 0!\n");
#endif
		pdir->dd_alloced--;
	}
	if(entry->d_flag & D_BASKET) {
		remove_basket(&pdir->dd_basketlist, entry);
		entry->d_flag &= ~D_BASKET;
	}
}

/* Theoretically, zombies should never or extremely seldom appear,
 * so this code is nearly superfluous.
 * A way to get zombies is while using inodes (i_count>0), unlink()
 * them as well as rmdir() the parent dir => the parent dir becomes a zombie.
 * Zombies are *not* in the hashtable, because somebody could re-creat()
 * that filename in it's parent dir again.
 * Besides coding errors during beta phase, when forcing an umount()
 * (e.g. at shutdown time), inodes could be in use such that the parent
 * dir is cleared, resulting also in zombies.
 */
static /*inline*/ void _d_handle_zombie(struct dentry * entry,
					struct ddir * ddir,
					struct ddir * pdir)
{
	if(entry->d_flag & D_DIR) {
		if(entry->d_flag & D_ZOMBIE) {
			if(!has_sons(ddir)) {
				entry->d_flag &= ~D_ZOMBIE;
				remove_hash(&pdir->dd_zombielist, entry);
				if(!pdir->dd_zombielist &&
				   (entry->d_parent->d_flag & D_ZOMBIE)) {
					d_del(entry->d_parent, D_NORMAL);
				}
			}
		} else if(has_sons(ddir)) {
			entry->d_flag |= D_ZOMBIE;
			insert_hash(&pdir->dd_zombielist, entry);

			/* This condition is no longer a bug, with the removal
			 * of recursive_clear() this happens naturally during
			 * an unmount attempt of a filesystem which is busy.
			 */
#if 0
			/* Not sure when this message should show up... */
			if(!IS_ROOT(entry)) {
				printk("VFS: clearing dcache directory "
				       "with successors\n");
#ifdef DEBUG
				printpath(entry);
				printk(" d_flag=%x alloced=%d negs=%d hashed=%d "
				       "basket=%p zombies=%p\n",
				       entry->d_flag, ddir->dd_alloced,
				       ddir->dd_negs, ddir->dd_hashed,
				       ddir->dd_basketlist, ddir->dd_zombielist);
#endif
			}
#endif
		}
	}
}

static /*inline*/ blocking void _d_del(struct dentry * entry,
				       struct anchors * anchor,
				       int flags)
{
	struct dheader ** free;
	struct dheader ** full;
	struct dheader * base = BASE_DHEADER(entry);
	struct ddir * ddir = NULL;
	struct ddir * pdir;
	struct inode * inode = entry->d_flag & D_PRELIMINARY ? NULL : entry->u.d_inode;

#ifdef DEBUG
	if(inode)
		xcheck("_d_del", inode);
#endif
	if(!entry->d_parent) {
		printk("VFS: dcache parent is NULL\n");
		return;
	}
	if(entry->d_flag & D_DIR) {
		free = &anchor->dir_free;
		full = &anchor->dir_full;
	} else {
		free = &anchor->free;
		full = &anchor->full;
	}
	pdir = d_dir(entry->d_parent);
	if(!IS_ROOT(entry))
		_d_remove_from_parent(entry, pdir, inode, flags);

	/* This may block, be careful! _d_remove_from_parent() is
	 * thus called before.
	 */
	if(entry->d_flag & D_DIR)
		ddir = d_dir(entry);
	if(IS_ROOT(entry))
		return;

	if(flags & D_NO_FREE) {
		/* Make it re-d_add()able */
		pdir->dd_alloced++;
		entry->d_flag &= D_DIR;
	} else
		_d_handle_zombie(entry, ddir, pdir);

	/* This dec_ddir() must occur after zombie handling. */
	if(!has_true_sons(pdir))
		dec_ddir(entry->d_parent, entry->d_parent->u.d_inode);

	entry->u.d_inode = NULL;
	if(inode) {
		remove_alias(&inode->i_dentry, entry);
		inode->i_dent_count--;
		if (entry->d_flag & D_DIR)
			dec_ddir(entry, inode);

		if(!(flags & D_NO_CLEAR_INODE) &&
		   !(atomic_read(&inode->i_count) +
		     inode->i_ddir_count +
		     inode->i_dent_count)) {
#ifdef DEBUG
			printk("#");
#endif
			/* This may block also. */
			_clear_inode(inode, 0, 0);
		}
	}
	if(!(flags & D_NO_FREE) && !(entry->d_flag & D_ZOMBIE)) {
		base->free++;
		if(base->free == base->maxfree) {
#ifndef DEBUG
			remove_header(free, base);
			free_page((unsigned long)base);
			goto done;
#endif
		}
		entry->d_next = base->emptylist;
		base->emptylist = entry;
		if(!entry->d_next) {
			remove_header(full, base);
			insert_header(free, base);
		}
#ifdef DEBUG
		x_freed++;
#endif
	}
#ifndef DEBUG
done:
#else
	x_free++;
#endif
}

blocking void d_del(struct dentry * entry, int flags)
{
	int i;

	if(!entry)
		return;
	LOG("d_clear", entry);
	if(entry->d_len >= D_MEDIUM) {
		if(entry->d_len >= D_LARGE) {
			i = 3;
		} else {
			i = 2;
		} 
	} else if(entry->d_len >= D_SMALL) {
		i = 1;
	} else {
		i = 0;
	}
	_d_del(entry, &anchors[i], flags);
}

static inline struct dentry * __dlookup(struct dentry ** base,
					struct qstr * name,
					struct qstr * appendix)
{
	struct dentry * tmp = *base;

	if(tmp && name->len) {
		int totallen = name->len;

		if(appendix)
			totallen += appendix->len;
		do {
			if(tmp->d_len == totallen			&&
			   !(tmp->d_flag & D_DUPLICATE)			&&
			   !strncmp(tmp->d_name, name->name, name->len)	&&
			   (!appendix || !strncmp(tmp->d_name+name->len,
						  appendix->name, appendix->len)))
				return tmp;
			tmp = tmp->d_hash_next;
		} while(tmp != *base);
	}
	return NULL;
}

struct dentry * d_lookup(struct inode * dir,
			 struct qstr * name,
			 struct qstr * appendix)
{
	if(dir->i_dentry) {
		struct ddir * ddir = d_dir(dir->i_dentry);
		struct dentry ** base = d_base_qstr(ddir, name, appendix);

		return __dlookup(base, name, appendix);
	}
	return NULL;
}

static /*inline*/ blocking void _d_insert_to_parent(struct dentry * entry,
						    struct ddir * pdir,
						    struct inode * inode,
						    struct qstr * ininame,
						    int flags)
{
	struct dentry ** base;
	struct dentry * parent = entry->d_parent;

#ifdef DEBUG
	if(!pdir->dd_alloced)
		printk("dd_alloced is 0!\n");
#endif
	base = d_base_qstr(pdir, ininame, NULL);
	if(!(flags & (D_NOCHECKDUP|D_DUPLICATE)) &&
	   __dlookup(base, ininame, NULL)) {
		d_del(entry, D_NO_CLEAR_INODE);
		return;
	}
	if(entry->d_flag & D_HASHED) {
		printk("VFS: dcache entry is already hashed\n");
		return;
	}
	if(!(flags & D_PRELIMINARY))
		pdir->dd_true_hashed++;
	pdir->dd_hashed++;
	insert_hash(base, entry);
	entry->d_flag |= D_HASHED;
	pdir->dd_alloced--;
	if(flags & D_BASKET)
		insert_basket(&pdir->dd_basketlist, entry);

#ifdef DEBUG
	if(inode && inode->i_dentry && (entry->d_flag & D_DIR)) {
		struct dentry * tmp = inode->i_dentry;
		printk("Auweia inode=%p entry=%p (%p %p %s)\n",
		       inode, entry, parent->u.d_inode, parent, parent->d_name);
		printk("entry path="); printpath(entry); printk("\n");
		do {
			TST("auweia",tmp);
			printk("alias path="); printpath(tmp); printk("\n");
			tmp = tmp->d_next;
		} while(tmp != inode->i_dentry);
		printk("\n");
	}
#endif
	if(has_true_sons(pdir))
		inc_ddir(parent, parent->u.d_inode);
	if(!inode && !(flags & D_PRELIMINARY)) {
		insert_alias(&pdir->dd_neglist, entry);
		pdir->dd_negs++;

		/* Don't allow the negative list to grow too much ... */
		while(pdir->dd_negs > (pdir->dd_true_hashed >> 1) + 5)
			d_del(pdir->dd_neglist->d_prev, D_REMOVE);
	}
}

blocking void d_add(struct dentry * entry, struct inode * inode,
		    struct qstr * ininame, int flags)
{
	struct dentry * parent = entry->d_parent;
	struct qstr dummy;
	struct ddir * pdir;

#ifdef DEBUG
	if(inode)
		xcheck("d_add", inode);
	if(IS_ROOT(entry)) {
		printk("VFS: d_add for root dentry ");
		printpath(entry);
		printk(" -> ");
		if(ininame)
			printk("%s", ininame->name);
		printk("\n");
		return;
	}
	if(!parent)
		panic("d_add with parent==NULL");
	LOG("d_add", entry);
#endif
	if(ininame) {
		if(ininame->len != entry->d_len) {
			printk("VFS: d_add with wrong string length");
			entry->d_len = ininame->len; /* kludge */
		}
		memcpy(entry->d_name, ininame->name, ininame->len);
		entry->d_name[ininame->len] = '\0';
	} else {
		dummy.name = entry->d_name;
		dummy.len = entry->d_len;
		ininame = &dummy;
	}
        if(entry->d_flag & D_HASHED)
		printk("VFS: d_add of already added dcache entry\n");

	pdir = d_dir(parent);
	_d_insert_to_parent(entry, pdir, inode, ininame, flags);
	entry->d_flag |= flags;
	if(inode && !(flags & D_PRELIMINARY)) {
		if(entry->d_flag & D_DIR) {
			if(inode->i_dentry) {
				printk("VFS: creating dcache directory alias\n");
				return;
			}
		}
		insert_alias(&inode->i_dentry, entry);
		inode->i_dent_count++;
	}
	entry->u.d_inode = inode;
}

blocking struct dentry * d_entry(struct dentry * parent,
				 struct qstr * name,
				 struct inode * inode)
{
	struct ddir * pdir = d_dir(parent);
	struct dentry ** base = d_base_qstr(pdir, name, NULL);
	struct dentry * found = __dlookup(base, name, NULL);

	if(!found) {
		int isdir = (inode && S_ISDIR(inode->i_mode));

		found = d_alloc(parent, name->len, isdir);
		if(found) {
			d_add(found, inode, name,
			      isdir ? (D_DIR|D_NOCHECKDUP) : D_NOCHECKDUP);
		} else
			printk("VFS: problem with d_alloc\n");
	}
	return found;
}

blocking void d_entry_preliminary(struct dentry * parent,
				  struct qstr * name,
				  unsigned long ino)
{
	struct ddir * pdir = d_dir(parent);
	struct dentry ** base = d_base_qstr(pdir, name, NULL);
	struct dentry * found = __dlookup(base, name, NULL);

	if(!found && ino) {
		struct dentry * new = d_alloc(parent, name->len, 0);

		if(new) {
			d_add(new, NULL, name, D_PRELIMINARY|D_NOCHECKDUP);
			new->u.d_ino = ino;
		} else
			printk("VFS: problem with d_alloc\n");
	}
}

blocking void d_move(struct dentry * entry, struct inode * newdir, 
		     struct qstr * newname, struct qstr * newapp)
{
	struct ddir tmp;
	struct dentry * new;
	struct inode * inode;
	int len;
	int flags;

	if(!entry)
		return;
	inode = entry->u.d_inode;
	flags = entry->d_flag;
	if((flags & D_PRELIMINARY) || !inode) {
		if(!(flags & D_PRELIMINARY))
			printk("VFS: trying to move negative dcache entry\n");
		d_del(entry, D_NO_CLEAR_INODE);
		return;
	}
#if 0
printk("d_move %p '%s' -> '%s%s' dent_count=%d\n", inode, entry->d_name,
       newname->name, newapp ? newapp->name : "", inode->i_dent_count);
#endif
	if(flags & D_ZOMBIE) {
		printk("VFS: moving zombie entry\n");
	}
	if(flags & D_DIR) {
		struct ddir * ddir = d_dir(entry);

		memcpy(&tmp, ddir, sizeof(struct ddir));

                /* Simulate empty dir for d_del(). */
		memset(ddir, 0, sizeof(struct ddir));
	}
	len = newname->len;
	if(newapp) {
		len += newapp->len;
		flags |= D_BASKET;
	} else
		flags &= ~D_BASKET;
	new = d_alloc(newdir->i_dentry, len, flags & D_DIR);
	memcpy(new->d_name, newname->name, newname->len);
	if(newapp)
		memcpy(new->d_name+newname->len, newapp->name, newapp->len);
	new->d_name[len] = '\0';
	d_del(entry, D_NO_CLEAR_INODE);
	d_add(new, inode, NULL, flags & (D_DIR|D_BASKET));
	if(flags & D_DIR) {
		struct ddir * ddir = d_dir(new);

		memcpy(ddir, &tmp, sizeof(struct ddir));
       	}
}

int d_path(struct dentry * entry, struct inode * chroot, char * buf)
{
	if(IS_ROOT(entry) || (chroot && entry->u.d_inode == chroot &&
			      !(entry->d_flag & D_PRELIMINARY))) {
		*buf = '/';
		return 1;
	} else {
		int len = d_path(entry->d_parent, chroot, buf);

		buf += len;
		if(len > 1) {
			*buf++ = '/';
			len++;
		}
		memcpy(buf, entry->d_name, entry->d_len);
		return len + entry->d_len;
	}
}

struct dentry * d_basket(struct dentry * dir_entry)
{
	if(dir_entry && (dir_entry->d_flag & D_DIR)) {
		struct ddir * ddir = d_dir(dir_entry);

		return ddir->dd_basketlist;
	} else
		return NULL;
}

int d_isbasket(struct dentry * entry)
{
	return entry->d_flag & D_BASKET;
}

blocking struct inode * d_inode(struct dentry ** changing_entry)
{
	struct dentry * entry = *changing_entry;
	struct inode * inode;

#ifdef CONFIG_DCACHE_PRELOAD
	if(entry->d_flag & D_PRELIMINARY) {
		struct qstr name = { entry->d_name, entry->d_len };
		struct ddir * pdir = d_dir(entry->d_parent);
		struct dentry ** base = d_base_qstr(pdir, &name, NULL);
		struct dentry * found;
		unsigned long ino;
		struct inode * dir = entry->d_parent->u.d_inode;
		TST("d_inode",entry);
		ino = entry->u.d_ino;
		if(!dir)
			d_panic();

		/* Prevent concurrent d_lookup()s or d_inode()s before
		 * giving up vfs_lock. This just removes from the parent,
		 * but does not deallocate it.
		 */

		/* !!!!!!! Aiee, here is an unresolved race if somebody
		 * unlink()s the inode during the iget(). The problem is
		 * that we need to synchronize externally. Proposed solution:
		 * put a rw_lock (read-mode) on the parent dir for each
		 * iget(), lookup() and so on, and a write-mode lock for
		 * everything that changes the dir (e.g. unlink()), and do
		 * this consistently everywhere in the generic VFS (not in
		 * the concrete filesystems). This should kill similar
		 * races everywhere, with a single clean concept.
		 * Later, the synchronization stuff can be cleaned out
		 * of the concrete fs'es.
		 */
		d_del(entry, D_NO_CLEAR_INODE|D_NO_FREE);
		vfs_unlock();

		/* This circumvents the normal lookup() of pathnames.
		 * Therefore,  preliminary entries must not be used
		 * (see FS_NO_DCACHE and FS_NO_PRELIM) if the fs does not
		 * permit fetching *valid* inodes with plain iget().
		 */
		inode = __iget(dir->i_sb, ino, 0);
		vfs_lock();
		if(!inode) {
			printk("VFS: preliminary dcache entry was invalid\n");
			*changing_entry = NULL;
			return NULL;
		}
		xcheck("d_inode iget()", inode);
		if((found = __dlookup(base, &name, NULL))) {
			d_del(entry, D_NO_CLEAR_INODE);
			*changing_entry = found;
		} else if(S_ISDIR(inode->i_mode)) {
			struct dentry * new = d_alloc(entry->d_parent, entry->d_len, 1);
			if(new)
				d_add(new, inode, &name, D_DIR);
			*changing_entry = new;

			/* Finally deallocate old entry. */
			d_del(entry, D_NO_CLEAR_INODE);
		} else {
			/* Re-insert to the parent, but now as normal dentry. */
			d_add(entry, inode, NULL, 0);
		}
		return inode;
	}
#endif
	inode = entry->u.d_inode;
	if(inode) {
#ifdef DEBUG
		xcheck("d_inode", inode);
#endif
		iinc_zero(inode);
	}
	return inode;
}
