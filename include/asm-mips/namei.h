/*
 * linux/include/asm-mips/namei.h
 *
 * Included from linux/fs/namei.c
 */
#ifndef __ASM_MIPS_NAMEI_H
#define __ASM_MIPS_NAMEI_H

#include <linux/config.h>

#ifdef CONFIG_BINFMT_IRIX

/* Only one at this time. */
#define IRIX32_EMUL "usr/gnemul/irix/"

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

	if (current->personality != PER_IRIX32)
		return -EINVAL;

	while (*name == '/')
		name++;

	atomic_inc(&current->fs->root->i_count);
	error = __namei(NAM_FOLLOW_LINK, IRIX32_EMUL, current->fs->root,
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

#else /* !defined(CONFIG_BINFMT_IRIX) */

#define __prefix_namei(retrieve_mode, name, base, buf, res_dir, res_inode, \
		       last_name, last_entry, last_error) 1

#endif /* !defined(CONFIG_BINFMT_IRIX) */

#endif /* __ASM_MIPS_NAMEI_H */
