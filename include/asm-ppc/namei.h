/* $Id: namei.h,v 1.2 1997/07/31 07:10:55 paulus Exp $
 * linux/include/asm-ppc/namei.h
 * Adapted from linux/include/asm-alpha/namei.h
 *
 * Included from linux/fs/namei.c
 */

#ifndef __PPC_NAMEI_H
#define __PPC_NAMEI_H

/* These dummy routines maybe changed to something useful
 * for /usr/gnemul/ emulation stuff.
 * Look at asm-sparc/namei.h for details.
 */

#define translate_namei(pathname, base, follow_links, res_inode)	\
	do { } while (0)

#define translate_open_namei(pathname, flag, mode, res_inode, base) \
	do { } while (0)

#endif /* __PPC_NAMEI_H */
