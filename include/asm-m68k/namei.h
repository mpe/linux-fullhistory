/*
 * linux/include/asm-m68k/namei.h
 *
 * Included from linux/fs/namei.c
 */

#ifndef __M68K_NAMEI_H
#define __M68K_NAMEI_H

/* This dummy routine maybe changed to something useful
 * for /usr/gnemul/ emulation stuff.
 * Look at asm-sparc/namei.h for details.
 */

#define __prefix_lookup_dentry(name, lookup_flags) \
        do {} while (0)

#endif
