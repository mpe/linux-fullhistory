#ifndef _ASM_IA64_STRING_H
#define _ASM_IA64_STRING_H

/*
 * Here is where we want to put optimized versions of the string
 * routines.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#define __HAVE_ARCH_STRLEN	1 /* see arch/ia64/lib/strlen.S */
#define __HAVE_ARCH_MEMSET	1 /* see arch/ia64/lib/memset.S */

#endif /* _ASM_IA64_STRING_H */
