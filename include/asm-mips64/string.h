/* $Id: string.h,v 1.3 1999/12/04 03:59:12 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1994, 1995, 1996, 1997, 1998 by Ralf Baechle
 *
 * XXX For now I'm too lazy to fix the string functions, let's rely on the
 * generic stuff.
 */
#ifndef _ASM_STRING_H
#define _ASM_STRING_H

#define __HAVE_ARCH_MEMSET
extern void *memset(void *__s, int __c, size_t __count);

#undef __HAVE_ARCH_MEMCPY
//extern void *memcpy(void *__to, __const__ void *__from, size_t __n);

#undef __HAVE_ARCH_MEMMOVE
//extern void *memmove(void *__dest, __const__ void *__src, size_t __n);

/* Don't build bcopy at all ...  */
#define __HAVE_ARCH_BCOPY

#endif /* _ASM_STRING_H */
