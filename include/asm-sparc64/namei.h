/* $Id: namei.h,v 1.13 1998/10/28 08:13:49 jj Exp $
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
__sparc64_lookup_dentry(const char *name, int lookup_flags)
{
	struct dentry *base;
	char *emul;

	switch (current->personality) {
#if 0
/* Until we solve, why SunOS apps sometime crash, disable gnemul support for SunOS */
	case PER_BSD:
		emul = SPARC_BSD_EMUL; break;
#endif		
	case PER_SVR4:
		emul = SPARC_SOL_EMUL; break;
	default:
		return NULL;
	}

	base = lookup_dentry (emul, 
			      dget (current->fs->root), 
			      (LOOKUP_FOLLOW | LOOKUP_DIRECTORY | LOOKUP_SLASHOK));
			
	if (IS_ERR (base)) return NULL;
	
	base = lookup_dentry (name, base, lookup_flags);
	
	if (IS_ERR (base)) return NULL;
	
	if (!base->d_inode) {
		struct dentry *fromroot;
		
		fromroot = lookup_dentry (name, dget (current->fs->root), lookup_flags);
		
		if (IS_ERR (fromroot)) return base;
		
		if (fromroot->d_inode) {
			dput(base);
			return fromroot;
		}
		
		dput(fromroot);
	}
	
	return base;
}

#define __prefix_lookup_dentry(name, lookup_flags)				\
	if (current->personality) {						\
		dentry = __sparc64_lookup_dentry (name, lookup_flags);		\
		if (dentry) return dentry;					\
	}

#endif /* __SPARC64_NAMEI_H */
