/* $Id: namei.h,v 1.14 1999/06/10 05:23:12 davem Exp $
 * linux/include/asm-sparc/namei.h
 *
 * Routines to handle famous /usr/gnemul/s*.
 * Included from linux/fs/namei.c
 */

#ifndef __SPARC_NAMEI_H
#define __SPARC_NAMEI_H

#define SPARC_BSD_EMUL "usr/gnemul/sunos/"
#define SPARC_SOL_EMUL "usr/gnemul/solaris/"

static inline char * __emul_prefix(void)
{
	switch (current->personality) {
	case PER_BSD:
		return SPARC_BSD_EMUL;
	case PER_SVR4:
		return SPARC_SOL_EMUL;
	default:
		return NULL;
	}
}

#endif /* __SPARC_NAMEI_H */
