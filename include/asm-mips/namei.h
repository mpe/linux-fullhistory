/*
 * linux/include/asm-mips/namei.h
 *
 * Included from linux/fs/namei.c
 */
#ifndef __ASM_MIPS_NAMEI_H
#define __ASM_MIPS_NAMEI_H

#include <linux/config.h>

/* Only one at this time. */
#define IRIX32_EMUL "usr/gnemul/irix/"

static inline struct dentry *
__mips_lookup_dentry(const char *name, int follow_link)
{
	int error;
	struct dentry *base;

	if (current->personality != PER_IRIX32)
		return ERR_PTR(-ENOENT);

	base = lookup_dentry (IRIX32_EMUL,
			dget (current->fs->root), 1);
			
	if (IS_ERR (base)) return base;
	
	return lookup_dentry (name, base, follow_link);
}

#ifdef CONFIG_BINFMT_IRIX

#define __prefix_lookup_dentry(name, follow_link)				\
	dentry = __mips_lookup_dentry (name, follow_link);			\
	if (!IS_ERR (dentry)) return dentry;

#else /* !defined(CONFIG_BINFMT_IRIX) */

#define __prefix_lookup_dentry(name, follow_link) \
        do {} while (0)

#endif /* !defined(CONFIG_BINFMT_IRIX) */

#endif /* __ASM_MIPS_NAMEI_H */
