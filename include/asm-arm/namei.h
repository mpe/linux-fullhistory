/*
 * linux/include/asm-i386/namei.h
 *
 * Included from linux/fs/namei.c
 */

#ifndef __ASMARM_NAMEI_H
#define __ASMARM_NAMEI_H

/* This dummy routine maybe changed to something useful
 * for /usr/gnemul/ emulation stuff.
 * Look at asm-sparc/namei.h for details.
 */

#define __prefix_namei(retrieve_mode, name, base, buf, res_dir, res_inode, \
		       last_name, last_entry, last_error) 1

#endif /* __ASMARM_NAMEI_H */
