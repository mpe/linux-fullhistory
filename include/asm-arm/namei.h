/* 
 * linux/include/asm-arm/namei.h
 *
 * Routines to handle famous /usr/gnemul
 * Derived from the Sparc version of this file
 *
 * Included from linux/fs/namei.c
 */

#ifndef __ASMARM_NAMEI_H
#define __ASMARM_NAMEI_H

#define ARM_BSD_EMUL "usr/gnemul/bsd/"

static inline struct dentry *
__arm_lookup_dentry(const char *name, int lookup_flags)
{
	struct dentry *base;
	char *emul;

	switch (current->personality) {
	case PER_BSD:
		emul = ARM_BSD_EMUL; break;
	default:
		return NULL;
	}

	base = lookup_dentry (emul, dget (current->fs->root),
		      (LOOKUP_FOLLOW | LOOKUP_DIRECTORY | LOOKUP_SLASHOK));
			
	if (IS_ERR (base)) return NULL;
	
	base = lookup_dentry (name, base, lookup_flags);
	
	if (IS_ERR (base)) return NULL;
	
	if (!base->d_inode) {
		struct dentry *fromroot;
		
		fromroot = lookup_dentry (name, dget (current->fs->root), 
					  lookup_flags);
		
		if (IS_ERR (fromroot)) return base;
		
		if (fromroot->d_inode) {
			dput(base);
			return fromroot;
		}
		
		dput(fromroot);
	}
	
	return base;
}

#define __prefix_lookup_dentry(name, lookup_flags)			\
	if (current->personality) {					\
		dentry = __arm_lookup_dentry (name, lookup_flags);	\
		if (dentry) return dentry;				\
	}

#endif /* __ASMARM_NAMEI_H */
