#ifndef __LINUX_DCACHE_H
#define __LINUX_DCACHE_H

/*
 * linux/include/linux/dcache.h
 *
 * Directory cache data structures
 */

#define D_MAXLEN 1024

#define IS_ROOT(x) ((x) == (x)->d_parent)

/*
 * "quick string" -- eases parameter passing, but more importantly
 * saves "metadata" about the string (ie length and the hash).
 */
struct qstr {
	const unsigned char * name;
	unsigned int len, hash;
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
	int d_count;
	unsigned int d_flags;
	struct inode  * d_inode;	/* Where the name belongs to - NULL is negative */
	struct dentry * d_parent;	/* parent directory */
	struct dentry * d_mounts;	/* mount information */
	struct dentry * d_covers;
	struct list_head d_hash;	/* lookup hash list */
	struct list_head d_alias;	/* inode alias list */
	struct list_head d_lru;		/* d_count = 0 LRU list */
	struct qstr d_name;
};

/*
 * These are the low-level FS interfaces to the dcache..
 */
extern void d_instantiate(struct dentry *, struct inode *);
extern void d_delete(struct dentry *);


/* allocate/de-allocate */
extern void d_free(struct dentry *);
extern struct dentry * d_alloc(struct dentry * parent, const struct qstr *name);
extern void shrink_dcache(void);

/* only used at mount-time */
extern struct dentry * d_alloc_root(struct inode * root_inode, struct dentry * old_root);

/*
 * This adds the entry to the hash queues and initializes "d_inode".
 * The entry was actually filled in earlier during "d_alloc()"
 */
extern void d_add(struct dentry * entry, struct inode * inode);

/* used for rename() and baskets */
extern void d_move(struct dentry * entry, struct dentry * newparent, struct qstr * newname);

/* appendix may either be NULL or be used for transname suffixes */
extern struct dentry * d_lookup(struct dentry * dir, struct qstr * name);

/* write full pathname into buffer and return length */
extern int d_path(struct dentry * entry, struct dentry * chroot, char * buf);

/* Allocation counts.. */
static inline struct dentry * dget(struct dentry *dentry)
{
	if (dentry)
		dentry->d_count++;
	return dentry;
}

extern void dput(struct dentry *);

/*
 * This is ugly. The inode:dentry relationship is a 1:n
 * relationship, so we have to return one (random) dentry
 * from the alias list. We select the first one..
 */
#define i_dentry(inode) \
	list_entry((inode)->i_dentry.next, struct dentry, d_alias)

#endif	/* __LINUX_DCACHE_H */
