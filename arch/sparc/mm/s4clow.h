/* s4clow.h: Defines for in-window low level tlb refill code.
 *
 * Copyright (C) 1995 David S. Miller (davem@caipfs.rutgers.edu)
 */
#ifndef _SPARC_S4CLOW_H
#define _SPARC_S4CLOW_H

#define         PAGE_SIZE       0x00001000
#define		REAL_PGDIR_MASK	0xfffc0000
#define		VMALLOC_START	0xfe100000

#define		RING_RINGHD	0x00
#define		RING_NENTRIES   0x10

#define		MMU_ENTRY_NEXT	0x00
#define		MMU_ENTRY_PREV	0x04
#define		MMU_ENTRY_VADDR	0x08
#define		MMU_ENTRY_PSEG	0x0c
#define		MMU_ENTRY_LCK	0x0d

#define		VACINFO_SIZE	0x00
#define		VACINFO_HWFLSH	0x08
#define		VACINFO_LSIZE	0x0c

/* Each of the routines could get called by any of the
 * other low level sun4c tlb routines.  Well... at least
 * we code it that way.  Because we are in window we need
 * a way to make a routine completely self contained and
 * only need to worry about saving it's own set of registers
 * which it in fact uses.  With traps off this is difficult
 * ... however...
 *
 * The Sparc can address anywhere in the two ranges
 * 0 --> PAGE_SIZE and -PAGE_SIZE --> -1 without any
 * address calculation registers.  So we pull a trick,
 * we map a special page for these low level tlb routines
 * since they must be as quick as possible.  Since the low
 * page is the NULL unmapped page and in user space we use
 * the high one for simplicity.  Kids, do not try this at
 * home.
 */
#define          REGSAVE_BASE   (-PAGE_SIZE)

#define          FLUSHREGS      0
#define          KFLTREGS       256
#define          UFLTREGS       512

#endif /* !(_SPARC_S4CLOW_H) */
