/*
 * linux/include/asm-mips/namei.h
 *
 * Included from linux/fs/namei.c
 *
 * $Id: namei.h,v 1.10 1998/10/28 08:13:24 jj Exp $
 */
#ifndef __ASM_MIPS_NAMEI_H
#define __ASM_MIPS_NAMEI_H

#include <linux/config.h>

/* Only one at this time. */
#define IRIX32_EMUL "usr/gnemul/irix/"

static inline struct dentry *
__mips_lookup_dentry(const char *name, int lookup_flags)
{
	struct dentry *base;

	if (current->personality != PER_IRIX32)
		return ERR_PTR(-ENOENT);

	base = lookup_dentry (IRIX32_EMUL,
			dget (current->fs->root), 
			(LOOKUP_FOLLOW | LOOKUP_DIRECTORY | LOOKUP_SLASHOK));
			
	if (IS_ERR (base)) return base;
	
	base = lookup_dentry (name, base, lookup_flags);

	if (IS_ERR (base)) return base;

	if (!base->d_inode) {
		dput(base);
		return ERR_PTR(-ENOENT);
	}
        
        return base;
}

#ifdef CONFIG_BINFMT_IRIX

#define __prefix_lookup_dentry(name, lookup_flags)				\
	dentry = __mips_lookup_dentry (name, lookup_flags);			\
	if (!IS_ERR (dentry)) return dentry;

#else /* !defined(CONFIG_BINFMT_IRIX) */

#define __prefix_lookup_dentry(name, lookup_flags) \
        do {} while (0)

#endif /* !defined(CONFIG_BINFMT_IRIX) */

#endif /* __ASM_MIPS_NAMEI_H */
