/* $Id: ksyms.c,v 1.1 1996/02/25 06:30:18 davem Exp $
 * arch/sparc/kernel/ksyms.c: Sparc specific ksyms support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>

/* We really haven't played around with modules at all in our
 * port, but this is here as a starting point for when we do.
 * One thing to note is that the way the symbols of the mul/div
 * support routines are named is a mess, they all start with
 * a '.' which makes it a bitch to export, we'll see.
 */

extern void bcopy (const char *src, char *dst, int len);
extern void * memmove(void *,const void *,size_t);
extern void * memcpy(void *,const void *,size_t);

static struct symbol_table arch_symbol_table = {
#include <linux/symtab_begin.h>
	/* platform dependent support */
	X(bcopy),
	X(memmove),
	X(memcpy),
#include <linux/symtab_end.h>
};

void arch_syms_export(void)
{
	register_symtab(&arch_symbol_table);
}
