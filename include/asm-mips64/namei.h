/* $Id: namei.h,v 1.1 1999/08/20 21:59:08 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_NAMEI_H
#define _ASM_NAMEI_H

/*
 * This dummy routine maybe changed to something useful
 * for /usr/gnemul/ emulation stuff.
 * Look at asm-sparc/namei.h for details.
 */

#define __prefix_lookup_dentry(name, lookup_flags) \
	do {} while (0)

#endif /* _ASM_NAMEI_H */
