/* $Id: head.S,v 1.27 1997/04/04 00:49:49 davem Exp $
 * head.S: Initial boot code for the Sparc64 port of Linux.
 *
 * Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 David Sitsky (David.Sitsky@anu.edu.au)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */

#include <linux/version.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/errno.h>
#include <asm/lsu.h>
#include <asm/head.h>
	
/* This section from from _start to sparc64_boot_end should fit into
 * 0xffff.f800.0000.4000 to 0xffff.f800.0000.8000 and will be sharing space
 * with bootup_user_stack, which is from 0xffff.f800.0000.4000 to
 * 0xffff.f800.0000.6000 and bootup_kernel_stack, which is from
 * 0xffff.f800.0000.6000 to 0xffff.f800.0000.8000. 
 */

	.text
	.globl	start, _start, stext, _stext
_start:
start:
_stext:
stext:
bootup_user_stack:
! 0xfffff80000004000
	b	sparc64_boot
	 flushw					/* Flush register file.      */

/* This stuff has to be in sync with SILO and other potential boot loaders
 * Fields should be kept upward compatible and whenever any change is made,
 * HdrS version should be incremented.
 */
        .global root_flags, ram_flags, root_dev
        .global ramdisk_image, ramdisk_size

        .ascii  "HdrS"
        .word   LINUX_VERSION_CODE
        .half   0x0201          /* HdrS version */
root_flags:
        .half   1
root_dev:
        .half   0
ram_flags:
        .half   0
ramdisk_image:
        .word   0
ramdisk_size:
        .word   0
        .word   reboot_command

	/* We must be careful, 32-bit OpenBOOT will get confused if it
	 * tries to save away a register window to a 64-bit kernel
	 * stack address.  Flush all windows, disable interrupts,
	 * remap if necessary, jump onto kernel trap table, then kernel
	 * stack, or else we die.
	 *
	 * PROM entry point is on %o4
	 */
sparc64_boot:
	/* Typically PROM has already enabled both MMU's and both on-chip
	 * caches, but we do it here anyway just to be paranoid.
	 */
	mov	(LSU_CONTROL_IC|LSU_CONTROL_DC|LSU_CONTROL_IM|LSU_CONTROL_DM), %g1
	stxa	%g1, [%g0] ASI_LSU_CONTROL

	/*
	 * Make sure we are in privileged mode, have address masking,
         * using the ordinary globals and have enabled floating
         * point.
	 *
	 * Again, typically PROM has left %pil at 13 or similar, and
	 * (PSTATE_PRIV | PSTATE_PEF | PSTATE_IE) in %pstate.
         */
	wrpr	%g0, 0xf, %pil			/* Interrupts off.           */
	wrpr    %g0, (PSTATE_PRIV|PSTATE_PEF), %pstate

	/* Check if we are mapped where we expect to be in virtual
	 * memory.  The Solaris /boot elf format bootloader
	 * will peek into our elf header and load us where
	 * we want to be, otherwise we have to re-map.
	 */
current_pc:
	rd	%pc, %g3
	sethi	%uhi(KERNBASE), %g4
	sllx	%g4, 32, %g4

	/* Check the run time program counter. */

	set	current_pc, %g5
	add	%g5, %g4, %g5
	cmp	%g3, %g5
	be	%xcc, sun4u_init
	 nop

create_mappings:
	/* %g5 holds the tlb data */
        sethi   %uhi(_PAGE_VALID | _PAGE_SZ4MB), %g5
        sllx    %g5, 32, %g5
        or      %g5, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W | _PAGE_G), %g5

	/* Base of physical memory cannot reliably be assumed to be
	 * at 0x0!  Figure out where it happens to be. -DaveM
	 */

	/* Put PADDR tlb data mask into %g3. */
	sethi	%uhi(_PAGE_PADDR), %g3
	or	%g3, %ulo(_PAGE_PADDR), %g3
	sllx	%g3, 32, %g3
	sethi	%hi(_PAGE_PADDR), %g7
	or	%g7, %lo(_PAGE_PADDR), %g7
	or	%g3, %g7, %g3

	/* Walk through entire ITLB, looking for entry which maps
	 * our %pc currently, stick PADDR from there into %g5 tlb data.
	 */
	clr	%l0			/* TLB entry walker. */
	set	0x1fff, %l2		/* Page mask. */
	rd	%pc, %l3
	andn	%l3, %l2, %g2		/* vaddr comparator */
1:
	/* Yes, the nops seem to be necessary for now, don't ask me why. -DaveM */
	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1		/* Get vaddr */
	cmp	%g1, %g2
	be,a,pn	%xcc, got_tlbentry
	 ldxa	[%l0] ASI_ITLB_DATA_ACCESS, %g1
	cmp	%l1, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

boot_failed:
	/* Debugging 8-) */
	set	0xdeadbeef, %g1
	t	0x11

got_tlbentry:
	/* Nops here again, perhaps Cheetah/Blackbird are better behaved... */
	nop
	nop
	nop
	and	%g1, %g3, %g1		/* Mask to just get paddr bits.       */
	sub	%g1, %g2, %g1		/* Get rid of %pc offset to get base. */

	/* NOTE: We hold on to %g1 paddr base as we need it below to lock
	 * NOTE: the PROM cif code into the TLB.
	 */

	or	%g5, %g1, %g5		/* Or it into TAG being built.        */

	/* PROM never puts any TLB entries into the MMU with the lock bit
	 * set.  So we gladly use tlb entry 63 for KERNBASE, 62 for
	 * boot time locked PROM CIF handler page, we remove the locked
	 * bit for the CIF page in paging_init().
	 */
	mov	TLB_TAG_ACCESS, %g3
	mov	(63 << 3), %g7
	stxa	%g4, [%g3] ASI_IMMU		/* KERNBASE into TLB TAG	*/
	stxa	%g5, [%g7] ASI_ITLB_DATA_ACCESS	/* TTE into TLB DATA		*/
	membar	#Sync

	/* Same for DTLB */
	stxa	%g4, [%g3] ASI_DMMU		/* KERNBASE into TLB TAG	*/
	stxa	%g5, [%g7] ASI_DTLB_DATA_ACCESS	/* TTE into TLB DATA		*/
	membar	#Sync

	/* Kill instruction prefetch queues. */
	flush	%g4
	membar	#Sync

	ba,pt	%xcc, go_to_highmem
	 nop

go_to_highmem:
	/* Now do a non-relative jump so that PC is in high-memory */
	set	sun4u_init, %g2
	jmpl     %g2 + %g4, %g0
	 nop

sun4u_init:
	/* Set ctx 0 */
	mov	PRIMARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync

	mov	SECONDARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync

	/* The lock bit has to be removed from this page later on, 
	 * but before firing up init we will use PROM a lot, so we
	 * lock it there now...
	 */

	/* Compute PROM CIF interface page TTE. */
	sethi	%hi(__p1275_loc), %g7
	or	%g7, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W | _PAGE_L), %g7
	sethi	%uhi(_PAGE_VALID), %g5
	sethi	%hi(0x8000), %g3
	sllx	%g5, 32, %g5
	mov	TLB_TAG_ACCESS, %g6
	or	%g5, %g7, %g5
	add	%g5, %g1, %g5				/* Add in physbase. */

	mov	(62 << 3), %g7				/* TLB entry 62		  */
	stxa	%g3, [%g6] ASI_IMMU			/* CIF page into TLB TAG  */
	stxa	%g5, [%g7] ASI_ITLB_DATA_ACCESS		/* TTE into TLB DATA	  */
	membar	#Sync

	/* Same for DTLB */
	stxa	%g3, [%g6] ASI_DMMU			/* CIF page into TLB TAG  */
	stxa	%g5, [%g7] ASI_DTLB_DATA_ACCESS		/* TTE into TLB DATA	  */
	membar	#Sync

	/* Kill instruction prefetch queues. */
	flush	%g3
	membar	#Sync

	/* We are now safely (we hope) in Nucleus context (0), rewrite
	 * the KERNBASE TTE's so they no longer have the global bit set.
	 * Don't forget to setup TAG_ACCESS first 8-)
	 */
	mov	TLB_TAG_ACCESS, %g2
	stxa	%g4, [%g2] ASI_IMMU
	stxa	%g4, [%g2] ASI_DMMU

	mov	(63 << 3), %g7
	ldxa	[%g7] ASI_ITLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_ITLB_DATA_ACCESS
	membar	#Sync

	ldxa	[%g7] ASI_DTLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_DTLB_DATA_ACCESS
	membar	#Sync

	/* Kill instruction prefetch queues. */
	flush	%g4
	membar	#Sync

	/* Compute the number of windows in this machine
	 * store this in nwindows and nwindowsm1
	 */
	rdpr	%ver, %g1			/* Get VERSION register.	*/
	sethi   %hi(nwindows), %g2
	and     %g1, VERS_MAXWIN, %g5
	or	%g2,%lo(nwindows),%g2
	add	%g5, 1, %g6
	add	%g2, (nwindows - nwindowsm1), %g3
	stx     %g6, [%g2 + %g4]
	stx     %g5, [%g3 + %g4]

	sethi	%hi(init_task), %g6
	or	%g6, %lo(init_task), %g6
	add	%g6, %g4, %g6			! g6 usage is fixed as well
	mov	%sp, %l6
	mov	%o4, %l7

	sethi	%hi(bootup_kernel_stack + 0x2000 - STACK_BIAS - REGWIN_SZ), %g5
	or	%g5, %lo(bootup_kernel_stack + 0x2000 - STACK_BIAS - REGWIN_SZ), %g5
	add	%g5, %g4, %sp
	mov	0, %fp
	wrpr	%g0, 0, %wstate
	wrpr	%g0, 0x0, %tl

	/* Clear the bss */
	sethi	%hi(8191), %l2
	or	%l2, %lo(8191), %l2
	sethi	%hi(__bss_start), %l0
	or	%l0, %lo(__bss_start), %l0
	sethi	%hi(_end), %l1
	or	%l1, %lo(_end), %l1
	add	%l1, %l2, %l1
	andn	%l1, %l2, %l1
	add	%l2, 1, %l2
	add	%l0, %g4, %o0
1:
	call	bzero_1page
	 add	%l0, %l2, %l0
	cmp	%l0, %l1
	blu,pt	%xcc, 1b
	 add	%l0, %g4, %o0

	/* Now clear empty_zero_page */
	call	bzero_1page
	 mov	%g4, %o0

	mov	%l6, %o1			! OpenPROM stack
	call	prom_init
	 mov	%l7, %o0			! OpenPROM cif handler

	/* Off we go.... */
	call	start_kernel
	 nop
	/* Not reached... */

	.globl	setup_tba
setup_tba:
	sethi	%hi(sparc64_ttable_tl0), %g5
	add	%g5, %g4, %g5
	wrpr	%g5, %tba

	/* Set up MMU globals */
	rdpr	%pstate, %o1
	wrpr	%o1, PSTATE_MG, %pstate

	/* PGD/PMD offset mask, used by TLB miss handlers. */
	sethi	%hi(0x1ff8), %g2
	or	%g2, %lo(0x1ff8), %g2

	/* Kernel PGDIR used by TLB miss handlers. */
	mov	%o0, %g6

	/* To catch bootup bugs, this is user PGDIR for TLB miss handlers. */
	clr	%g7

	/* Setup Interrupt globals */
	wrpr	%o1, PSTATE_IG, %pstate
	sethi	%uhi(ivector_to_mask), %g4
	or	%g4, %ulo(ivector_to_mask), %g4
	sethi	%hi(ivector_to_mask), %g5
	or	%g5, %lo(ivector_to_mask), %g5
	or	%g5, %g4, %g1			/* IVECTOR table */
	mov	0x40, %g2			/* INTR data 0 register */

	andn	%o1, PSTATE_IE, %o1
	wrpr	%g0, %g0, %wstate
	wrpr	%o1, %g0, %pstate

	/* Zap TSB BASE to zero with TSB_size==1. */
	mov	TSB_REG, %o4
	mov	1, %o5
	stxa	%o5, [%o4] ASI_DMMU
	stxa	%o5, [%o4] ASI_IMMU

	membar	#Sync

	retl
	 nop

sparc64_boot_end:
	.skip	0x2000 + _start - sparc64_boot_end
bootup_user_stack_end:

bootup_kernel_stack:
	.skip	0x2000

! 0xfffff80000008000

#include "ttable.S"

	.data
	.align	8
	.globl	nwindows, nwindowsm1 
nwindows:	.xword	0
nwindowsm1:	.xword	0
	.section	".fixup",#alloc,#execinstr
	.globl	__ret_efault
__ret_efault:
	ret
	 restore %g0, -EFAULT, %o0
