/* $Id: namei.h,v 1.1 1996/12/13 14:48:21 jj Exp $
 * linux/include/asm-i386/namei.h
 *
 * Included from linux/fs/namei.c
 */

#ifndef __I386_NAMEI_H
#define __I386_NAMEI_H

/* These dummy routines maybe changed to something useful
 * for /usr/gnemul/ emulation stuff.
 * Look at asm-sparc/namei.h for details.
 */

#define translate_namei(pathname, base, follow_links, res_inode)	\
	do { } while (0)

#define translate_open_namei(pathname, flag, mode, res_inode, base) \
	do { } while (0)

#endif /* __I386_NAMEI_H */
