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

#define __prefix_namei(retrieve_mode, name, base, buf, res_dir, res_inode, \
		       last_name, last_entry, last_error) 1

#endif
