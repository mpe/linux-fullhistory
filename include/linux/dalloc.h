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
#define D_NORMAL     0
#define D_BASKET     1 /* put into basket (deleted/unref'd files) */
#define D_DUPLICATE  2 /* allow duplicate entries */
#define D_NOCHECKDUP 4 /* no not check for duplicates */

/* public flags for d_flag */
#define D_PRELOADED 8

/* public flags for d_del() */
#define D_REMOVE         0
#define D_NO_CLEAR_INODE 1

#define IS_ROOT(x) ((x) == (x)->d_parent)

/* "quick string" -- I introduced this to shorten the parameter list
 * of many routines. Think of it as a (str,stlen) pair.
 * Storing the len instead of doing strlen() very often is performance
 * critical.
 */
struct qstr {
	char * name;
	int len;
};

struct dentry {
	union {
		struct inode  * d_inode;  /* Where the name belongs to */
		unsigned long d_ino;      /* for preliminary entries */
	} u;
	struct dentry * d_parent; /* parent directory */
	struct dentry * d_next;   /* hardlink aliasname / empty list */
	struct dentry * d_prev;   /* hardlink aliasname */
	struct dentry * d_hash_next;
	struct dentry * d_hash_prev;
	struct dentry * d_basket_next;
	struct dentry * d_basket_prev;
	struct qstr d_name;
	unsigned int d_flag;
};

extern struct dentry * the_root;

/* Note that all these routines must be called with vfs_lock() held */

/* get inode, if necessary retrieve it with iget() */
extern blocking struct inode * d_inode(struct dentry ** changing_entry);

/* allocate proper space for the len */
extern struct dentry * d_alloc(struct dentry * parent, int len, int isdir);

/* only used once at mount_root() */
extern blocking
struct dentry * d_alloc_root(struct inode * root_inode);

/* d_inode is connected with inode, and d_name is copied from ininame.
 * either of them may be NULL, but when ininame is NULL, dname must be
 * set by the caller prior to calling this. */
extern blocking
void d_add(struct dentry * entry, struct inode * inode,
	   struct qstr * ininame, int flags);

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
void d_move(struct dentry * entry, struct inode * newdir,
	    struct qstr * newname, struct qstr * newapp);

/* appendix may either be NULL or be used for transname suffixes */
extern struct dentry * d_lookup(struct inode * dir, struct qstr * name,
				struct qstr * appendix);

/* write full pathname into buffer and return length */
extern int d_path(struct dentry * entry, struct inode * chroot, char * buf);

extern struct dentry * d_basket(struct dentry * dir_entry);

extern int d_isbasket(struct dentry * entry);
#endif
