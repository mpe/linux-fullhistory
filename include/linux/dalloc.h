#ifndef DALLOC_H
#define DALLOC_H
/*
 * $Id: dalloc.h,v 1.3 1997/06/13 04:39:34 davem Exp $
 *
 * include/linux/dalloc.h - alloc routines for dcache
 * alloc / free space for pathname strings
 * Copyright (C) 1997, Thomas Schoebel-Theuer,
 * <schoebel@informatik.uni-stuttgart.de>.
 */

#define D_MAXLEN 1024

/* public flags for d_add() */
#define D_NORMAL	  0
#define D_BASKET	  1 /* put into basket (deleted/unref'd files) */
#define D_DUPLICATE	  2 /* allow duplicate entries */
#define D_NOCHECKDUP	  4 /* no not check for duplicates */
#define D_NEGATIVE	  8 /* negative entry */
#define D_PRELOADED	 16
#define D_DIR		 32 /* directory entry - look out for allocation issues */
#define D_HASHED	 64
#define D_ZOMBIE	128
#define D_INC_DDIR	512

/* public flags for d_del() */
#define D_REMOVE         0
#define D_NO_CLEAR_INODE 1

#define IS_ROOT(x) ((x) == (x)->d_parent)

/* "quick string" -- I introduced this to shorten the parameter list
 * of many routines. Think of it as a (str,stlen,hash) pair.
 * Storing the len instead of doing strlen() very often is performance
 * critical.
 */
struct qstr {
	const unsigned char * name;
	int len, hash;
};

/* Name hashing routines. Initial hash value */
#define init_name_hash()		0

/* partial hash update function. Assume roughly 4 bits per character */
static inline unsigned long partial_name_hash(unsigned char c, unsigned long prevhash)
{
	prevhash = (prevhash << 4) | (prevhash >> (8*sizeof(unsigned long)-4));
	return prevhash ^ c;
}

/* Finally: cut down the number of bits to a int value (and try to avoid losing bits) */
static inline unsigned long end_name_hash(unsigned long hash)
{
	if (sizeof(hash) > sizeof(unsigned int))
		hash += hash >> 4*sizeof(hash);
	return (unsigned int) hash;
}

struct dentry {
	unsigned int d_flag;
	unsigned int d_count;
	struct inode  * d_inode;	/* Where the name belongs to */
	struct dentry * d_parent;	/* parent directory */
	struct dentry * d_mounts;	/* mount information */
	struct dentry * d_covers;
	struct dentry * d_next;		/* hardlink aliasname / empty list */
	struct dentry * d_prev;		/* hardlink aliasname */
	struct dentry * d_hash_next;
	struct dentry * d_hash_prev;
	struct dentry * d_basket_next;
	struct dentry * d_basket_prev;
	struct qstr d_name;
};

extern struct dentry * the_root;

/*
 * These are the low-level FS interfaces to the dcache..
 */
extern void d_instantiate(struct dentry *, struct inode *, int);
extern void d_delete(struct dentry *);


/* Note that all these routines must be called with vfs_lock() held */

/* get inode, if necessary retrieve it with iget() */
extern blocking struct inode * d_inode(struct dentry ** changing_entry);

/* allocate/de-allocate */
extern void d_free(struct dentry *);
extern struct dentry * d_alloc(struct dentry * parent, struct qstr *name, int isdir);

/* only used at mount-time */
extern blocking
struct dentry * d_alloc_root(struct inode * root_inode, struct dentry * old_root);

/*
 * This adds the entry to the hash queues and initializes "d_inode".
 * The entry was actually filled in earlier during "d_alloc()"
 */
extern blocking
void d_add(struct dentry * entry, struct inode * inode, int flags);

/* combination of d_alloc() and d_add(), less lookup overhead */
extern blocking 
struct dentry * d_entry(struct dentry * parent, struct qstr * name, struct inode * inode);
extern blocking
void d_entry_preliminary(struct dentry * parent, struct qstr * name, unsigned long ino);

/* recursive d_del() all successors */
extern blocking
void d_del(struct dentry * entry, int flags);

/* used for rename() and baskets */
extern blocking 
void d_move(struct dentry * entry, struct dentry * newparent, struct qstr * newname);

/* appendix may either be NULL or be used for transname suffixes */
extern struct dentry * d_lookup(struct dentry * dir, struct qstr * name);

/* write full pathname into buffer and return length */
extern int d_path(struct dentry * entry, struct dentry * chroot, char * buf);

extern struct dentry * d_basket(struct dentry * dir_entry);

extern int d_isbasket(struct dentry * entry);

/*
 * Whee..
 */
static inline void dput(struct dentry *dentry)
{
	if (dentry)
		dentry->d_count--;
}

static inline struct dentry * dget(struct dentry *dentry)
{
	if (dentry)
		dentry->d_count++;
	return dentry;
}

#endif
