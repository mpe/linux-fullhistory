/* 
 * linux/include/asm-arm/namei.h
 *
 * Included from linux/fs/namei.c
 */

#ifndef __ASMARM_NAMEI_H
#define __ASMARM_NAMEI_H

/* This dummy routine maybe changed to something useful
 * for /usr/gnemul/ emulation stuff.
 * Look at asm-sparc/namei.h for details.
 */

#define __prefix_lookup_dentry(name, lookup_flags) \
        do {} while (0)

#endif /* __ASMARM_NAMEI_H */
