/* $Id: namei.h,v 1.5 1997/06/07 08:32:54 ecd Exp $
 * linux/include/asm-sparc/namei.h
 *
 * Routines to handle famous /usr/gnemul/s*.
 * Included from linux/fs/namei.c
 */

#ifndef __SPARC_NAMEI_H
#define __SPARC_NAMEI_H

#define SPARC_BSD_EMUL "usr/gnemul/sunos/"
#define SPARC_SOL_EMUL "usr/gnemul/solaris/"

#if 0 /* XXX FIXME */

extern int __namei(int, const char *, struct inode *, char *, struct inode **,
		   struct inode **, struct qstr *, struct dentry **, int *);

static __inline__ int
__prefix_namei(int retrieve_mode, const char * name, struct inode * base,
	       char * buf, struct inode ** res_dir, struct inode ** res_inode,
	       struct qstr * last_name, struct dentry ** last_entry,
	       int * last_error)
{
	int error;

	if (!(current->personality & (PER_BSD|PER_SVR4)))
		return -ENOENT;

	while (*name == '/')
		name++;

	atomic_inc(&current->fs->root->i_count);
	error = __namei(NAM_FOLLOW_LINK,
		        current->personality & PER_BSD ?
		        SPARC_BSD_EMUL : SPARC_SOL_EMUL, current->fs->root,
		        buf, NULL, &base, NULL, NULL, NULL);
	if (error)
		return error;

	error = __namei(retrieve_mode, name, base, buf, res_dir, res_inode,
			last_name, last_entry, last_error);
	if (error)
		return error;

	return 0;
}

#endif /* XXX FIXME */

#endif /* __SPARC_NAMEI_H */
