/* $Id: namei.h,v 1.1 1996/12/13 14:48:21 jj Exp $
 * linux/include/asm-i386/namei.h
 *
 * Included from linux/fs/namei.c
 */

#ifndef __I386_NAMEI_H
#define __I386_NAMEI_H

/* This dummy routine maybe changed to something useful
 * for /usr/gnemul/ emulation stuff.
 * Look at asm-sparc/namei.h for details.
 */

#define __prefix_namei(retrieve_mode, name, base, buf, res_dir, res_inode, \
		       last_name, last_entry, last_error) 1

#endif /* __I386_NAMEI_H */
