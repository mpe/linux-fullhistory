/* $Id: namei.h,v 1.8 1997/09/06 02:25:27 davem Exp $
 * linux/include/asm-sparc64/namei.h
 *
 * Routines to handle famous /usr/gnemul/s*.
 * Included from linux/fs/namei.c
 */

#ifndef __SPARC64_NAMEI_H
#define __SPARC64_NAMEI_H

#define SPARC_BSD_EMUL "usr/gnemul/sunos/"
#define SPARC_SOL_EMUL "usr/gnemul/solaris/"

static inline struct dentry *
__sparc64_lookup_dentry(const char *name, int follow_link)
{
	struct dentry *base;

	switch (current->personality) {
	case PER_BSD:
	case PER_SVR4:
		break;
	default:
		return ERR_PTR(-ENOENT);
	}

	base = lookup_dentry ((current->personality == PER_BSD) ?
			SPARC_BSD_EMUL : SPARC_SOL_EMUL,
			dget (current->fs->root), 1);
			
	if (IS_ERR (base)) return base;
	
	base = lookup_dentry (name, base, follow_link);
	
	if (IS_ERR (base)) return base;
	
	if (!base->d_inode) {
		dput(base);
		return ERR_PTR(-ENOENT);
	}
	
	return base;
}

#define __prefix_lookup_dentry(name, follow_link)				\
	dentry = __sparc64_lookup_dentry (name, follow_link);			\
	if (!IS_ERR (dentry)) return dentry;

#endif /* __SPARC64_NAMEI_H */
