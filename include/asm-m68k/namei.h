/*
 * linux/include/asm-m68k/namei.h
 *
 * Included from linux/fs/namei.c
 */

#ifndef __M68K_NAMEI_H
#define __M68K_NAMEI_H

/* These dummy routines maybe changed to something useful
 * for /usr/gnemul/ emulation stuff.
 * Look at asm-sparc/namei.h for details.
 */

#define translate_namei(pathname, base, follow_links, res_inode)	\
	do { } while (0)

#define translate_open_namei(pathname, flag, mode, res_inode, base) \
	do { } while (0)

#endif
