/*
 * fs/proc/omirr.c  -  online mirror support
 *
 * (C) 1997 Thomas Schoebel-Theuer
 */

#ifndef OMIRR_H
#define OMIRR_H
#include <linux/fs.h>
#include <linux/dalloc.h>

extern int omirr_print(struct dentry * ent1, struct dentry * ent2, 
		       struct qstr * suffix, const char * fmt, ...);

extern int omirr_printall(struct inode * inode, const char * fmt, ...);

#endif
