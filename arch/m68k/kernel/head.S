/* -*- mode: asm -*-
**
** head.S -- This file contains the initial boot code for the the
**	     Linux/68k kernel.
**
** Copyright 1993 by Hamish Macdonald
**
** 68040 fixes by Michael Rausch
** 68060 fixes by Roman Hodek
**
** Atari support by Andreas Schwab, using ideas of Robert de Vries
** and Bjoern Brauel
**
** 94/11/14 Andreas Schwab: put kernel at PAGESIZE
** 94/11/18 Andreas Schwab: remove identity mapping of STRAM for Atari 
** ++ Bjoern & Roman: ATARI-68040 support for the Medusa
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file README.legal in the main directory of this archive
** for more details.
**
*/

/*
 * Linux startup code.
 *
 * At this point, the boot loader has:
 * Disabled interrupts
 * Disabled caches
 * Put us in supervisor state.
 *
 * The kernel setup code takes the following steps:
 *   Raise interrupt level
 *   Set up initial kernel memory mapping.
 *	This sets up a mapping of the 4M of memory the kernel
 *	is located in.	It also does a mapping of any initial
 *	machine specific areas.
 * Note that the kernel is located at virtual address 0x1000 == _start
 *   Enable cache memories
 *   Jump to kernel startup
 *
 * Register d6 contains the CPU flags and d4 the machine type
 * from the boot_info information for most of this file.
 * The upper word of d6 contains a bit for '040 or '060, since these two
 * are quite similar for initial mm setup. Another bit in d6 allows
 * distinction of the '060. The lower word of d6 contains the cache mode
 * that should be applied to pages containing descriptors. This mode is
 * non-cached/non-serialized for the '040 and cacheable/write-through for
 * the '060.
 */

#include <linux/linkage.h>
#include <asm/bootinfo.h>

.text
.globl SYMBOL_NAME(kernel_pg_dir), SYMBOL_NAME(kpt)
.globl SYMBOL_NAME(availmem), SYMBOL_NAME(is_medusa)
.globl SYMBOL_NAME(m68k_pgtable_cachemode)
.globl SYMBOL_NAME(kernel_pmd_table), SYMBOL_NAME(swapper_pg_dir)

PAGESIZE = 4096
BI_CPU = 4
BI_MACH = 0
BI_AMIGA_ECLK = 1234
LF   = 10
CR   = 13
BI_BIT040 = 2	/* CPU bits in bootinfo */
BI_BIT060 = 3
BIT0460   = 16	/* indicates '0[46]0 in d6 */
BIT060    = 17	/* indicates '060 in d6 */
D6VAL_040 = 0x00010000
D6VAL_060 = 0x00030000
/* BIT040 = 2 */

     /* Some definitions for the special registers of the 68040.
      * (MMU, caches)
      */

/* Translation control register */
TC_ENABLE = 0x8000
TC_PAGE8K = 0x4000
TC_PAGE4K = 0x0000

/* Transparent translation registers */
TTR_ENABLE = 0x8000

/* Some bits used in the page and table descriptors as well as in the 
 * special registers.
 */

CM_CACHE_WT     = 0x0000          /* cacheable, write-through */
CM_CACHE_CB     = 0x0020          /* cacheable, copyback */
CM_NONCACHE_SER = 0x0040          /* noncacheable, serialized */
CM_NONCACHE     = 0x0060          /* noncacheable */
CM_MASK         = 0xffffff9f      /* mask */

MODIFIED        = 0x0010
WRITE_PROT      = 0x0004
USED            = 0x0008
GLOBAL          = 0x0400
SV_ONLY         = 0x0080
PAGEDESC	= 0x0001
TABLEDESC	= 0x0002
INVALID         = 0x0000

/* Cache enabling */
I_HALF		= 0x00002000	/* half-cache mode for I-cache ('060) */
I_FREEZE	= 0x00004000	/* freeze I-cache ('060) */
I_ENABLE        = 0x00008000	/* enable I-cache */
BC_CLRU		= 0x00200000	/* clear user entries in branch cache ('060) */
BC_CLRA		= 0x00400000	/* clear all entries in branch cache ('060) */
BC_ENABLE	= 0x00800000	/* enable branch cache ('060) */
D_HALF		= 0x08000000	/* half-cache mode for D-cache ('060) */
PUSH_DPI	= 0x10000000	/* disable CPUSH invalidation ('060) */
SB_ENABLE	= 0x20000000	/* enable store buffer ('060) */
D_FREEZE	= 0x40000000	/* freeze D-cache ('060) */
D_ENABLE        = 0x80000000	/* enable D-cache */

/* Miscellaneous definitions */
PAGE_MASK       = (~(PAGESIZE-1))

ROOT_TABLE_SIZE = 128
PTR_TABLE_SIZE  = 128
PAGE_TABLE_SIZE = 64
ROOT_INDEX_SHIFT = 25
PAGE_INDEX_SHIFT = 12

ENTRY(_stext)
ENTRY(_start)
	bras	1f /* Jump over bootinfo version numbers */
/*
 * Version numbers of the bootinfo interface
 */

	.long	BOOTINFOV_MAGIC
	.long	MACH_AMIGA, AMIGA_BOOTI_VERSION
	.long	MACH_ATARI, ATARI_BOOTI_VERSION
	.long	0

1:

/*
 * raise interrupt level
 */

	movew	#0x2700,%sr

/*
 * Copy bootinfo from position after BSS to final resting place
 */
	lea	%pc@(SYMBOL_NAME(_end)),%a0
	lea	%pc@(SYMBOL_NAME(boot_info)),%a1
	movel	%pc@(SYMBOL_NAME(bisize)),%d0
	subql	#1,%d0
1:	moveb	%a0@+,%a1@+
	dbra	%d0,1b

/*
 * Record the CPU and machine type.
 */
	lea	%pc@(SYMBOL_NAME(boot_info)),%a0
	movel	%a0@(BI_MACH),%d4
	movel	%a0@(BI_CPU),%d0
	movel	%a0@(BI_CPU),%d6      /* !!!!!!!!!!!!!!!! */

	btst	#BI_BIT060,%d0
	beq	1f
	/* '060: d6 := BIT0460|BIT060, cache mode 0x60 (no-cache/non-ser) */
	movel	#(D6VAL_060+CM_NONCACHE),%d6
	bra	2f
1:	btst	#BI_BIT040,%d0
	beq	1f
	/* '040: d6 := BIT0460, cache mode 0x00 (write-through) */
	movel	#(D6VAL_040+CM_CACHE_WT),%d6
	bra	2f
1:	/* '020 or '030: d6 := no CPU bit, cache mode unused */
	moveq	#0,%d6

2:	lea	%pc@(SYMBOL_NAME(m68k_pgtable_cachemode)),%a0
	movel	%d6,%a0@		/* save cache mode for page tables */
	andl	#0x0000ffff,%a0@

/*
 * Initialize serial port
 */
	jbsr Lserial_init

	moveq	#CR,%d7
	jbsr	Lserial_putc
	moveq	#LF,%d7
	jbsr	Lserial_putc
	moveq	#'A',%d7
	jbsr	Lserial_putc

/*
 * Get address at end of kernel code/data/bss and
 * mask off at a page boundary.
 */
	lea	%pc@(SYMBOL_NAME(_end)),%a0
	movel	%a0,%d0
	addl	#(PAGESIZE-1),%d0
	andl	#PAGE_MASK,%d0
	movel	%d0,%a6

	moveq	#'B',%d7
	jbsr	Lserial_putc

/*
 * initialize the kernel root table.
 */
	lea	%pc@(SYMBOL_NAME(kernel_pg_dir)),%a4
	movel	%a4,%a0
	moveq	#0,%d0
	moveq	#(ROOT_TABLE_SIZE-1),%d1
1:	movel	%d0,%a0@+
	dbra	%d1,1b

	/*
	 * Initialize root table descriptor pointing to the kernel pointer
	 * table.
	 */
	movel	%a6,%a5
	addw	#PAGESIZE,%a6

	movel	%a5,%a0
	addql	#TABLEDESC,%a0
	movel	%a0,%a4@

	moveq	#'C',%d7
	jbsr	Lserial_putc

/*
 * Initialize the pointer tables referred to above.  They either point
 * to page tables in the case of the 680[46]0 or contain early
 * termination page descriptors in the case of the 68851 or 68030.
 *
 * Each pointer table entry points to a 64 entry page table.  16 of these
 * page tables are grouped to form a single 1024 entry page table which
 * fits in a single 4096 byte page.
 *
 * Some register usages:
 *    a0 -> pointer table descriptor address
 *    a1 -> pointer table descriptor
 *    d1 -> counter
 *    d2 -> pointer table descriptor increment (varies according to CPU)
 */

	/* clear the kernel pointer table */
	movel	%a5,%a0
	moveq	#0,%d0
	moveq	#(PTR_TABLE_SIZE-1),%d1
1:	movel	%d0,%a0@+
	dbra	%d1,1b

	movel	%a5,%a0
	moveq	#15,%d1

	/*
	 * base value of pointer table descriptor is either
	 * the address of the first page table (680[46]0)
	 * or the base address of physical memory (68030).
	 */
	btst	#BIT0460,%d6
	bne	1f

	/* 680[23]0 */
	lea	%pc@(SYMBOL_NAME(_stext)-PAGESIZE:w),%a1  /* base address */
	addql	#PAGEDESC,%a1	/* descriptor type */
	movel	#0x40000,%d2	/* increment */
	bra	2f

1:	/* 680[46]0 */
	movel	%a6,%a1		/* base address */
	addw	#PAGESIZE,%a6	/* allocate the page table */
	lea	%pc@(SYMBOL_NAME(kpt)),%a3
	movel	%a1,%a3@		/* save address of page table */
	addql	#TABLEDESC,%a1	/* descriptor type */
	movel	#256,%d2 	/* increment */

2:	movel	%a1,%a0@+
	addl	%d2,%a1
	dbra	%d1,2b

	moveq	#'D',%d7
	jbsr	Lserial_putc

/*
 * If we are running on a 680[46]0, we have a kernel page table and
 * must initialize it.	Make the entries point to the first
 * 4M of physical memory (the memory we are residing in).
 * Set the cache mode bits to Cacheable, Copyback.  Set the Global bits
 * in the descriptors also.
 */

	btst	#BIT0460,%d6
	jeq	Lnot040

	moveq	#'F',%d7
	jbsr	Lserial_putc

	movel	%pc@(SYMBOL_NAME(kpt)),%a0
	lea	%pc@(SYMBOL_NAME(_stext)-PAGESIZE:w),%a1

	addw	#(GLOBAL+CM_CACHE_CB+PAGEDESC),%a1
	movew	#((PAGESIZE/4)-1),%d1
	movel	#PAGESIZE,%d2

1:	movel	%a1,%a0@+
	addl	%d2,%a1
	dbra	%d1,1b

	/*
	 * on the 68040, pages used to hold mmu tables should
	 * be initialized as noncachable; the '060 allows write-through.
	 * Do this for the root table page (which also contains
	 * all pointer tables utilitized thus far) and the
	 * kernel page table.
	 */
	lea	%pc@(SYMBOL_NAME(_stext)-PAGESIZE:w),%a0
	movel	%a4,%d0		/* address of root table */
	subl	%a0,%d0		/* determine offset of root table page */
	moveq	#PAGE_INDEX_SHIFT,%d1	/* determine offset into kernel page table */
	lsrl	%d1,%d0		/* i.e. page number of the address offset */
	movel	%pc@(SYMBOL_NAME(kpt)),%a0
	lea	%a0@(%d0:l:4),%a0
	movel	%a0@,%d1
	andl	#CM_MASK,%d1
	orw	%d6,%d1
	movel	%d1,%a0@+

	movel	%a0@,%d1	/* do the same for the kernel page table */
	bclr	#5,%d1		/* the kernel page table resides in the  */
	bset	#6,%d1		/* page after the page containing the	 */
	movel	%d1,%a0@	/* root table				 */

Lnot040:
/*
 * Do any machine specific page table initializations.
 */
	moveq	#MACH_AMIGA,%d0
	cmpl	%d4,%d0
	bne	Lnotami

/*
 * On the Amiga:
 * Our current stack (in CHIP ram) may become invalid after the remapping
 * of the kernel virtual address space, so set it to point to PAGE_SIZE.
 * This will be in CHIP ram until after the remapping, and in the unused
 * first page (temporarily) after that.
 *
 * Setup a mapping of the first 16M of physical address space at virtual
 * address 0x80000000, using early termination page descriptors for the
 * 68030, and proper page tables for the 680[46]0.  Set this area as
 * non-cacheable.
 */

	moveq	#'H',%d7
	jbsr	Lserial_putc

	move.w	#PAGESIZE,%sp

	btst	#BIT0460,%d6
	bne	Lspami68040


	/*
	 * for the 68030, just setup a translation to map in the first
	 * 32M of physical address space at virtual address 0x80000000
         * using an early termination page descriptor.
	 */

	moveq	#'I',%d7
	jbsr	Lserial_putc

	moveq	#0x41,%d0
	movel	%d0,%a4@(64*4)

	bra	Lmapphys

Lspami68040:

	/*
	 * for the 680[46]0, use another pointer table, and allocate 4 more
	 * page tables.  Initialize the pointer table to point to the
	 * page tables.  Then initialize the page tables to point to
	 * the first 16M of memory, with no caching (noncachable/serialized).
	 */

	/* clear the amiga pointer table */
	lea	%a5@(512),%a0
	moveq	#0,%d0
	moveq	#(PTR_TABLE_SIZE-1),%d1
1:	movel	%d0,%a0@+
	dbra	%d1,1b

	/* allocate 4 page tables */
	movel	%a6,%a3
	addw	#(4*PAGESIZE),%a6

	/* initialize the pointer table */
	lea	%a5@(512),%a0
	movel	%a3,%a1
	addql	#TABLEDESC,%a1	/* base descriptor */
	movel	#256,%d2 	/* increment */
	moveq	#(PAGE_TABLE_SIZE-1),%d1

1:	movel	%a1,%a0@+
	addl	%d2,%a1
	dbra	%d1,1b

	/* ensure that the root table points to the pointer table */
	lea	%a5@(512),%a0
	addql	#TABLEDESC,%a0
	movel	%a0,%a4@(256)	/* 0x80000000>>(ROOT_INDEX_SHIFT-2) doesn't
				   work */

	/*
	 * initialize the page tables
	 * descriptor bits include noncachable/serialized and global bits.
	 */
	movel	%a3,%a0
	movew	#(GLOBAL+CM_NONCACHE_SER+PAGEDESC),%a1
	movel	#PAGESIZE,%d2
	movew	#PAGESIZE-1,%d1

1:	movel	%a1,%a0@+
	addl	%d2,%a1
	dbra	%d1,1b

	/*
	 * Finally, since we just allocated 4 page tables, make sure that
	 * the virtual mapping of the 4 page tables indicates
	 * noncachable/serialized.
	 */
	movel	%a3,%d0		/* ami page table start address */
	lea	%pc@(SYMBOL_NAME(_stext)-PAGESIZE:w),%a0
	subl	%a0,%d0		/* determine offset of root table page */
	moveq	#PAGE_INDEX_SHIFT,%d1	/* determine offset into kernel page table */
	lsrl	%d1,%d0
	movel	%pc@(SYMBOL_NAME(kpt)),%a0
	movel	#3,%d2
1:	lea	%a0@(%d0:l:4),%a1
	movel	%a1@,%d1
	bclr	#5,%d1
	bset	#6,%d1
	movel	%d1,%a1@
	addql	#1,%d0
	dbra	%d2,1b

	bra	Lmapphys


Lnotami:	/* other machines specific mappings go here! */

	moveq	#MACH_ATARI,%d0
	cmpl	%d4,%d0
	bne	Lnotatari

	move.w	#PAGESIZE,%sp

/* On the Atari, we map the I/O region (phys. 0x00ffxxxx) by mapping
   the last 16 MB of virtual address space to the first 16 MB (i.e.
   0xffxxxxxx -> 0x00xxxxxx). For this, an additional pointer table is
   needed. I/O ranges are marked non-cachable.

   For the Medusa it is better to map the I/O region transparently
   (i.e. 0xffxxxxxx -> 0xffxxxxxx), because some I/O registers are
   accessible only in the high area. The test whether it is a Medusa
   is done by writing to the byte at phys. 0x0. This should result
   in a bus error on all other machines.

   ...should, but doesn't. The Atferburner040 for the Falcon has the
   same behaviour (0x0..0x7 are no ROM shadow). So we have to do
   another test to distinuish Medusa and AB040. This is a
   read attempt for 0x00ff82fe phys. that should bus error on a Falcon
   (+AB040), but is in the range where the Medusa always asserts DTACK.
*/

	moveq	#0,%d3			/* base addr for others: 0x00000000 */
	movec	%d3,%vbr
	lea	    %pc@(Ltest_berr),%a0
	movel	%a0,0x8
	movel	%sp,%a0
	moveb	0x0,%d1
	clrb	0x0
	nop
	moveb	%d1,0x0
	nop
	tstb	0x00ff82fe
	nop
	movel	#0xff000000,%d3		/* Medusa base addr: 0xff000000 */
Ltest_berr:
	movel	%a0,%sp
	lea     %pc@(SYMBOL_NAME(is_medusa)),%a0
	movel	%d3,%a0@

	/* Let the root table point to the new pointer table */
	lea	%a5@(512),%a0
	addl	#TABLEDESC,%a0
	movel	%a0,%a4@(508)		/* 0xFE000000 - 0xFFFFFFFF */

	/* clear lower half of the pointer table (0xfexxxxxx) */
	lea 	%a5@(512),%a0
	movel	#0,%d0
	movel	#63,%d2
1:	movel	%d0,%a0@+
	dbra	%d2,1b

	btst	#BIT0460,%d6
	bne	Lspata68040

/* Mapping of the last 16M of virtual address space to the first 16M
   for efficient addressing of hardware registers */
	movel	#0x40000,%d1
	movel	#63,%d2
	movel	%d3,%d0
	addl	#PAGEDESC,%d0
1:	movel	%d0,%a0@+
	addl	%d1,%d0
	dbra	%d2,1b
	moveq	#0x40,%d0	/* make non-cachable */
	addl	%d0,%a5@(1020)	/* 0xFFFC0000-0xFFFFFFFF (I/O space) */
/* GK: 0xFFF00000-0xFFF3FFFF (IDE-bus) has to be non-cachable too */
	addl	%d0,%a5@(1008)

	bra	Lmapphys

Lspata68040:
	/* allocate 4 page tables */
	movel	%a6,%a3
	addw	#(4*PAGESIZE),%a6

/* Initialize the upper half of the pointer table (a0 is still valid) */
	movel	%a3,%a1
	addql	#TABLEDESC,%a1
	movel	#256,%d2
	moveq	#63,%d1
1:	movel	%a1,%a0@+
	addl	%d2,%a1
	dbra 	%d1,1b

	/* Initialize the page tables as noncacheable/serialized! */
	movel	%a3,%a0
	movel	%d3,%a1
	addw	#(GLOBAL+CM_NONCACHE_SER+PAGEDESC),%a1
	movel	#PAGESIZE,%d2
	movew	#(PAGESIZE-1),%d1
1:	movel	%a1,%a0@+
	addl	%d2,%a1
	dbra	%d1,1b

	/*
	 * Finally, since we just allocated 4 page tables, make sure that
	 * the virtual mapping of the 4 page tables indicates
	 * noncachable or write-through.
	 */
	movel	%a3,%d0		/* page table start address */
	lea	%pc@(SYMBOL_NAME(_stext)-PAGESIZE:w),%a0
	subl	%a0,%d0		/* determine offset of root table page */
	moveq	#PAGE_INDEX_SHIFT,%d1 /* determine offset into 
                                         kernel page table */
	lsrl	%d1,%d0
	movel	%pc@(SYMBOL_NAME(kpt)),%a0
	moveq	#3,%d2
1:	lea	%a0@(%d0:l:4),%a1
	movel	%a1@,%d1
	andl	#CM_MASK,%d1
	orw	%d6,%d1
	movel	%d1,%a1@
	addql	#1,%d0
	dbra	%d2,1b

Lnotatari:

/*
 * Setup a transparent mapping of the physical memory we are executing in.
 *
 * Only do this if the physical memory is not in the first 16M Meg, or not on
 * an Amiga since the first 16M is already identity mapped on the Amiga.
 */
Lmapphys:
	moveq	#'J',%d7
	jbsr	Lserial_putc

	clrl	%d5		/* indicate that no cleanup is required */

	cmpl	#MACH_AMIGA,%d4
	bne	Lmapphysnotamiga	/* other machines will probably have
					 * to put in code and jump to it here
					 */

/*
 * The virtual address of the start of the kernel is 0x1000.  On ALL
 * Amigas, there is CHIP RAM in this area.  Hence we will copy the MMU
 * enabling code to CHIP RAM (to the same physical address as the kernel
 * virtual address) and jump to it.  When the MMU is enabled, we will be
 * running from the code in the kernel virtual space, rather than the
 * physical space.
 */

/*
 * Setup Supervisor Root Pointer register to point to page directory,
 * setup translation register contents and enable translation.
 */
	btst	#BIT0460,%d6
	bne	Lamimmu68040

	moveq	#'K',%d7
	jbsr	Lserial_putc

	lea	%pc@(mmu),%a0
	movel	#0x80000002,%a0@   /* no limit, 4byte descriptors */
	movel	%a4,%a0@(4)
	pmove	%a0@,%srp
	pmove	%a0@,%crp
	/*
	 * enable,super root enable,4096 byte pages,7 bit root index,
	 * 7 bit pointer index, 6 bit page table index.
	 */
	movel	#0x82c07760,%a0@

	/* setup registers for jumping MMU enabling code */
	lea	%pc@(Ldoit030ami),%a2
	lea	Ldoit030ami,%a1

	moveq	#CR,%d7
	jbsr	Lserial_putc
	moveq	#LF,%d7
	jbsr	Lserial_putc
	movel	%a2,%d7
	jbsr	Lserial_putnum
	moveq	#' ',%d7
	jbsr	Lserial_putc
	movel	%a1,%d7
	jbsr	Lserial_putnum

	bra	LdoitAmiga

#ifdef __ELF__
	.align	16
#else
	.align	4
#endif
Ldoit030ami:
	pmove	%a0@,%tc	/* enable the MMU */
	jra	LdoneMMUenable	/* branch to continuation of startup */

Lamimmu68040:
	moveq	#'L',%d7
	jbsr	Lserial_putc
	
	.word	0xf4d8		/* CINVA I/D */
	.word	0xf518		/* pflusha	*/
	.long	0x4e7bc807	/* movec a5,srp */
	.long	0x4e7bc806	/* movec a5,urp */
	movel	#(TC_ENABLE+TC_PAGE4K),%d0

	/* setup registers for jumping MMU enabling code */
	lea	Ldoit040ami,%a1
	lea	%pc@(Ldoit040ami),%a2
	bra	LdoitAmiga

#ifdef __ELF__
	.align	16
#else
	.align	4
#endif
Ldoit040ami:
	.long	0x4e7b0003	/* movec d0,tc  (enable the MMU) */
	jra	LdoneMMUenable	/* branch to continuation of startup */

LdoitAmiga:
	/* copy the appropriate code (two longwords) to chipmem */
	movel	%a2@,%a1@
	movel	%a2@(4),%a1@(4)

	/*
 	 * save physaddr of phys mem in register a3
	 */
	lea	%pc@(SYMBOL_NAME(_stext)-PAGESIZE:w),%a3

	/* jump to the physical address in chipmem */
	jmp	%a1@

Lmapphysnotamiga:

	cmpl	#MACH_ATARI,%d4
	bne	Lmapphysnotatari

	lea	%pc@(SYMBOL_NAME(_stext)-PAGESIZE),%a0
	tstl	%a0
	jeq	Lnophys2

	/* The kernel physical address is different from its virtual
	   address.  Temporarily set up an identity mapping of the
	   16MB chunk where the kernel is executing. */
  
	/* 680[46]0? */
	btst	#BIT0460,%d6
	jeq	1f

	/*
	 * For the 68040, we will use the transparent translation
	 * registers to identity map the 16M chunk that contains
	 * the physical memory.
	 */
	movel	%a0,%d2
	andl	#0xff000000,%d2		/* logical address base */
	orw	#0xc040,%d2		/* add in magic bits */
	.long	0x4e7b2004		/* movec d2,ittr0 */
	.long	0x4e7b2006		/* movec d2,dttr0 */

	/* d5 is kept 0 to do no cleanup. This didn't work in head.S, I don't
	 * know why... The transparent translation is turned off in
	 * atari/config.c instead.
	 */
	jra	Lnophys2

	/* Transparent translation for the 68030 via transparent translation
	   register */

1:
	/* cleanup is needed; note it */
	moveq	#1,%d5

	movel	%a0,%d2
	andl	#0xff000000,%d2		/* logical address base */
	orw	#0x8143,%d2		/* add in magic bits */
	lea	%pc@(mmu),%a0
	movel	%d2,%a0@
	pmove	%a0@,%tt0

Lnophys2:
	/*
 	 * save physaddr of phys mem in register a3
	 */
	lea	%pc@(SYMBOL_NAME(_stext)-PAGESIZE:w),%a3

	btst	#BIT0460,%d6
	jne	Latarimmu68040

	moveq	#'K',%d7
	jbsr	Lserial_putc

	lea	%pc@(mmu),%a0
	movel	#0x80000002,%a0@   /* no limit, 4byte descriptors */
	movel	%a4,%a0@(4)
	pmove	%a0@,%srp
	pmove	%a0@,%crp
	/*
	 * enable,super root enable,4096 byte pages,7 bit root index,
	 * 7 bit pointer index, 6 bit page table index.
	 */
	movel	#0x82c07760,%a0@
	pmove	%a0@,%tc		/* enable the MMU */

	/* Jump to the virtual address of continuation of startup. */
	jmp	LdoneMMUenable

Latarimmu68040:
	moveq	#'L',%d7
	jbsr	Lserial_putc

	.word	0xf4d8		/* CINVA I/D */
	.word	0xf518		/* pflusha	*/
	.long	0x4e7bc807	/* movec a4,srp */
	.long	0x4e7bc806	/* movec a4,urp */
	movel	#(TC_ENABLE+TC_PAGE4K),%d0
	/* this value is also ok for the '060, we don't use the cache
	 * mode/protection defaults */
	.long	0x4e7b0003	/* movec d0,tc  (enable the MMU) */
	jmp	LdoneMMUenable	/* jump to virtual address of
				   continuation of startup */

Lmapphysnotatari:


LdoneMMUenable:

/*
 * Fixup the addresses for the kernel pointer table and availmem.
 * Convert them from physical addresses to virtual addresses.
 */

	/*
	 * fixup the Amiga custom register location before printing
	 */
	addl	#0x80000000,Lcustom

	/* The same for the Atari */
	movel	#0xff000000,Liobase

	moveq	#'M',%d7
	jbsr	Lserial_putc

	/*
	 * a3 contains physaddr of kernel start
	 */
	movel	SYMBOL_NAME(kpt),%d1
	subl	%a3,%d1
	movel	%d1,SYMBOL_NAME(kpt)

	/*
	 * do the same conversion on the first available memory
	 * address (in a6).
	 */
	subl	%a3,%a6

	/* Allocate a page to be used by get_kpointer_table.  */
	movel	%a6,SYMBOL_NAME(kernel_pmd_table)
	addl	#PAGESIZE,%a6
	movel	%a6,SYMBOL_NAME(availmem) /* first available memory address */

	moveq	#'N',%d7
	jbsr	Lserial_putc

	/*
	 * Clean up the temporary physical mapping (if necessary)
	 */

	tstl	%d5
	jeq	Lnoclean

	btst	#BIT0460,%d6
	jne	Loff040

	/* clean up physical identity mapping for 68020/68030 */
	/* Is this needed for the Amiga anymore? */
	/* it's not in 1.2.13pl6 - Jes */
	cmpl	#MACH_AMIGA,%d4
	jeq	Lclean030
	cmpl	#MACH_ATARI,%d4
	jne	Lnoclean

	/* clear transparent translation register */
	lea	%pc@(mmu),%a0
	clrl	%a0@
	pmove	%a0@,%tt0
	jra	Lnoclean

Lclean030:
	movel	%a0,%d2		/* a0 contains physical start address */
	moveq	#25,%d3		/* find appropriate index in root table */
	lsrl	%d3,%d2
	moveq	#0,%d0
	movel	%d0,%a4@(%d2:l:4)  /* clear descriptor */

	jra	Lnoclean

Loff040:
	/* turn off transparent translation registers for '0[46]0 */
	moveq	#0,%d0
	.long	0x4e7b0004	/* movec d0,ittr0 */
	.long	0x4e7b0006	/* movec d0,dttr0 */

Lnoclean:
	moveq	#'O',%d7
	jbsr	Lserial_putc


/*
 * Enable caches
 */
	btst	#BIT0460,%d6
	jne	Lcache680460

	movel	#0x00001919,%d0
	movec   %d0,%cacr
	jra	1f

Lcache680460:
	btst	#BIT060,%d6
	jne	Lcache68060

	.word	0xf4d8	     /* CINVA I/D */
	movel	#I_ENABLE+D_ENABLE,%d0
	/* MMU stuff works in copyback mode now, so enable the cache */
	movec	%d0,%cacr
	jra	1f

Lcache68060:
	.word	0xf4d8	     /* CINVA I/D */
	movel	#I_ENABLE+D_ENABLE+SB_ENABLE+PUSH_DPI+BC_ENABLE+BC_CLRA,%d0
	/* MMU stuff works in copyback mode now, so enable the cache */
	movec	%d0,%cacr
	/* enable superscalar dispatch in PCR */
	moveq	#1,%d0
	.long	0x4e7b0808	/* movec d0,pcr */

1:

/*
 * Setup initial stack pointer
 */
	lea	SYMBOL_NAME(init_user_stack)+PAGESIZE,%sp

/* jump to the kernel start */

	jbsr	SYMBOL_NAME(start_kernel)

#if 0
tmp_fault_handler:
     lea       %pc@(tfh_1st_str),%a0
     jbsr       Lserial_puts
     move.l    %sp@(2),%d7                     /* PC */
     jbsr       Lserial_putnum
     move.w    %sp@(0xa),%d7
     swap      %d7
     move.w    %sp@(0x6),%d7
     jbsr       Lserial_putnum
     lea       %pc@(tfh_2nd_str),%a0
     jbsr       Lserial_puts
     move.l    %sp@(0x10),%d7                  /* Fault address */
     jbsr       Lserial_putnum
     moveq     #CR,%d7
     jbsr       Lserial_putc
     moveq     #LF,%d7
     jbsr       Lserial_putc
1:   jra       1b

tfh_1st_str:
     .byte     CR
     .byte     LF
     .ascii    "Access fault occurred. PC = "
     .byte     0

tfh_2nd_str:
     .byte     CR
     .byte     LF
     .ascii    "FaultAddress = "
     .byte     0
#endif

/*
 * Serial port output support.
 */
LSERPER      = 0xdff032
LSERDAT      = 0xdff030
LSERDATR     = 0xdff018
LNTSC_PERIOD = 371
LPAL_PERIOD  = 368/2        /* /2 for 19200 baud */
LNTSC_ECLOCK = 7159090
LSERIAL_CNTRL = 0xbfd000
LSERIAL_DTR   = 7

/*
 * Debug output support
 * Atarians have a choice between the parallel port, the serial port
 * from the MFP or a serial port of the SCC
 */

/* #define USE_PRINTER */
/* #define USE_SCC */
#define USE_MFP

#ifdef USE_PRINTER

LPSG_SELECT	= 0xff8800
LPSG_READ	= 0xff8800
LPSG_WRITE	= 0xff8802
LPSG_IO_A	= 14
LPSG_IO_B	= 15
LPSG_CONTROL	= 7
LSTMFP_GPIP	= 0xfffa01
LSTMFP_DDR	= 0xfffa05
LSTMFP_IERB	= 0xfffa09

#elif defined(USE_SCC)
 
LSCC_CTRL_B	= 0xff8c85
LSCC_DATA_B	= 0xff8c87

/* Initialisation table for SCC */
scc_initable:
	.byte	9,12		/* Reset */
	.byte	4,0x44		/* x16, 1 stopbit, no parity */
	.byte	3,0xc0		/* receiver: 8 bpc */
	.byte	5,0xe2		/* transmitter: 8 bpc, assert dtr/rts */
	.byte	9,0		/* no interrupts */
	.byte	10,0		/* NRZ */
	.byte	11,0x50		/* use baud rate generator */
	.byte	12,24,13,0	/* 9600 baud */
	.byte	14,2,14,3	/* use master clock for BRG, enable */
	.byte	3,0xc1		/* enable receiver */
	.byte	5,0xea		/* enable transmitter */
	.byte	-1
	.even

#elif defined(USE_MFP)

LMFP_UCR     = 0xfffa29
LMFP_TDCDR   = 0xfffa1d
LMFP_TDDR    = 0xfffa25
LMFP_TSR     = 0xfffa2d
LMFP_UDR     = 0xfffa2f

#endif

/*
 * Initialize serial port hardware for 9600/8/1
 * a0 thrashed
 * Atari d0 trashed (a1 in case of SCC)
 */
	.even
Lserial_init:
	cmpil	#MACH_AMIGA,%d4
	jne	1f
	bclr	#LSERIAL_DTR,LSERIAL_CNTRL
	movew	#LNTSC_PERIOD,LSERPER
	cmpl	#LNTSC_ECLOCK,%a0@(BI_AMIGA_ECLK)
	jeq	9f
	movew	#LPAL_PERIOD,LSERPER
	jra	9f
1:	cmpil   #MACH_ATARI,%d4
	jne	9f
#ifdef USE_PRINTER
	bclr	#0,LSTMFP_IERB
	bclr	#0,LSTMFP_DDR
	moveb	#LPSG_CONTROL,LPSG_SELECT
	moveb	#0xff,LPSG_WRITE
	moveb	#LPSG_IO_B,LPSG_SELECT
	clrb	LPSG_WRITE
	moveb	#LPSG_IO_A,LPSG_SELECT
	moveb	LPSG_READ,%d0
	bset	#5,%d0
	moveb	%d0,LPSG_WRITE
#elif defined(USE_SCC)
	lea	LSCC_CTRL_B,%a0
	lea	%pc@(scc_initable:w),%a1
2:	moveb	%a1@+,%d0
	jmi	3f
	moveb	%d0,%a0@
	moveb	%a1@+,%a0@
	jra	2b
3:	clrb	%a0@
#elif defined(USE_MFP)
	bclr	#1,LMFP_TSR
	moveb   #0x88,LMFP_UCR
	andb	#0x70,LMFP_TDCDR
	moveb   #2,LMFP_TDDR
	orb	#1,LMFP_TDCDR
	bset	#1,LMFP_TSR
#endif
9:
	rts

/*
 * Output character in d7 on serial port.
 * d7 thrashed.
 */
Lserial_putc:
	moveml	%a0/%a1,%sp@-
	cmpil	#MACH_AMIGA,%d4
	jne	2f
	andw	#0x00ff,%d7
	oriw	#0x0100,%d7
	movel	%pc@(Lcustom),%a1
	movew	%d7,%a1@(LSERDAT)
1:	movew	%a1@(LSERDATR),%d7
	andw	#0x2000,%d7
	jeq	1b
	jra	9f
2:	cmpil   #MACH_ATARI,%d4
	jne	9f
	movel	%pc@(Liobase),%a1
#ifdef USE_PRINTER
3:	btst	#0,%a1@(LSTMFP_GPIP)
	jne	3b
	moveb	#LPSG_IO_B,%a1@(LPSG_SELECT)
	moveb	%d7,%a1@(LPSG_WRITE)
	moveb	#LPSG_IO_A,%a1@(LPSG_SELECT)
	moveb	%a1@(LPSG_READ),%d7
	bclr	#5,%d7
	moveb	%d7,%a1@(LPSG_WRITE)
	nop
	nop
	bset	#5,%d7
	moveb	%d7,%a1@(LPSG_WRITE)
#elif defined(USE_SCC)
3:	btst	#2,%a1@(LSCC_CTRL_B)
	jeq	3b
	moveb	%d7,%a1@(LSCC_DATA_B)
#elif defined(USE_MFP)
3:	btst	#7,%a1@(LMFP_TSR)
	jeq	3b
	moveb	%d7,%a1@(LMFP_UDR)
#endif
9:
	moveml	%sp@+,%a0/%a1
	rts

/*
 * Output string pointed to by a0 to serial port.
 * a0 trashed.
 */
Lserial_puts:
	movel	%d7,%sp@-
1:	moveb	%a0@+,%d7
	jeq	2f
	jbsr	Lserial_putc
	jra	1b
2:	movel	%sp@+,%d7
	rts

/*
 * Output number in d7 in hex notation on serial port.
 * d0-d2 trashed.
 * d7 trashed.
 */

Lserial_putnum:
	moveml	%d0-%d2/%d7,%sp@-
	movel	%d7,%d1
	moveq	#4,%d0
	moveq	#7,%d2
L1:	roll	%d0,%d1
	moveb	%d1,%d7
	andb	#0x0f,%d7
	cmpb	#0x0a,%d7
	bccs	1f
	addb	#'0',%d7
	jra	2f
1:	addb	#'A'-10,%d7
2:	jbsr	Lserial_putc
	dbra	%d2,L1
	moveq	#32,%d7
	jbsr	Lserial_putc
	moveml	%sp@+,%d0-%d2/%d7
	rts
#if 0
.globl showtest
showtest:
	moveml	%a1/%d7,%sp@-
	moveq	#'A',%d7
	jbsr	Lserial_putc
	moveq	#'=',%d7
	jbsr	Lserial_putc
	movel	%a0,%d7
	jbsr	Lserial_putnum

	ptestr	#5,%a0@,#7,%a1

	moveq	#'D',%d7
	jbsr	Lserial_putc
	moveq	#'A',%d7
	jbsr	Lserial_putc
	moveq	#'=',%d7
	jbsr	Lserial_putc

	movel	%a1,%d7
	jbsr	Lserial_putnum

	moveq	#'D',%d7
	jbsr	Lserial_putc
	moveq	#'=',%d7
	jbsr	Lserial_putc
	movel	%a1@,%d7
	jbsr	Lserial_putnum

	moveq	#'S',%d7
	jbsr	Lserial_putc
	moveq	#'=',%d7
	jbsr	Lserial_putc

	lea	%pc@(mmu),%a1
	pmove	%psr,%a1@
	clrl	%d7
	movew	%a1@,%d7
	jbsr	Lserial_putnum

	moveq	#CR,%d7
	jbsr	Lserial_putc
	moveq	#LF,%d7
	jbsr	Lserial_putc

	moveml	%sp@+,%a1/%d7
	rts
#endif

	.align 512	/*
			 * MMU root-pointers need to be 512-byte
			 * aligned on the 680[46]0 - Jes
			 */

SYMBOL_NAME_LABEL(swapper_pg_dir)
	.skip ROOT_TABLE_SIZE * 4
SYMBOL_NAME_LABEL(kernel_pg_dir)
	.skip ROOT_TABLE_SIZE * 4

	.data
	.even
Lcustom:
	.long 0
Liobase:
	.long 0
mmu:	.quad 0
SYMBOL_NAME_LABEL(kernel_pmd_table)
	.long 0
SYMBOL_NAME_LABEL(kpt)
	.long 0
SYMBOL_NAME_LABEL(availmem)
	.long 0
SYMBOL_NAME_LABEL(is_medusa)
	.long 0
SYMBOL_NAME_LABEL(m68k_pgtable_cachemode)
	.long 0
