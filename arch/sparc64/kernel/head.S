/* $Id: head.S,v 1.17 1997/03/18 17:59:37 jj Exp $
 * head.S: Initial boot code for the Sparc64 port of Linux.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
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
	
/* This section from from _start to sparc64_boot_end should fit into
 * 0xffff.f800.0000.4000 to 0xffff.f800.0000.8000 and will be sharing space
 * with bootup_user_stack, which is from 0xffff.f800.0000.4000 to
 * 0xffff.f800.0000.6000 and bootup_kernel_stack, which is from
 * 0xffff.f800.0000.6000 to 0xffff.f800.0000.8000. 
 */

	.text
	.globl	start, _start
_start:
start:
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
	mov	(LSU_CONTROL_IC|LSU_CONTROL_DC|LSU_CONTROL_IM|LSU_CONTROL_DM), %g1
	stxa	%g1, [%g0] ASI_LSU_CONTROL

	/*
	 * Make sure we are in privileged mode, have address masking,
         * using the ordinary globals and have enabled floating
         * point.
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
	be	%xcc,sun4u_init
	 nop

create_mappings:
	/* %g5 holds the tlb data */
        sethi   %uhi(_PAGE_VALID | _PAGE_SZ4MB), %g5
        sllx    %g5, 32, %g5
        or      %g5, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W | _PAGE_G), %g5

	/* We aren't mapped in at KERNBASE, so we need to create
	 * an I and D tlb entry to map KERNBASE to 0.  Both entries
	 * are 4 Megs mappings and are locked in.
	 */
	set	TLB_TAG_ACCESS, %g3           /* 0x30 */

	stxa	%g4, [%g3] ASI_IMMU           /* 0x50 */
	stxa	%g5, [%g0] ASI_ITLB_DATA_IN   /* 0x54 */
	membar  #Sync

	/* Put KERNBASE into the I/D Tag Access Register (TAR) */
	stxa	%g4, [%g3] ASI_DMMU           /* 0x58 */
	stxa    %g5, [%g0] ASI_DTLB_DATA_IN
	membar  #Sync

	nop
	nop	
	membar	#Sync
	
	ba,pt	%xcc, go_to_highmem
	 nop

/* Now do a non-relative jump so that PC is in high-memory */
go_to_highmem:
	set	sun4u_init, %g1
	jmpl     %g1 + %g4, %g0
	 nop

sun4u_init:
	/* Remap our prom interface code */
	sethi	%hi(__p1275_loc), %g7
	or	%g7, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W | _PAGE_G | _PAGE_L), %g7
	/* The lock bit has to be removed from this page later on, 
	   but before firing up init we will use PROM a lot, so we
	   lock it there now... */
	sethi	%uhi(_PAGE_VALID), %g5
	sethi	%hi(0x8000), %g3
	sllx	%g5, 32, %g5
	mov	TLB_TAG_ACCESS, %g6
	or	%g5, %g7, %g5
	stxa	%g3, [%g6] ASI_IMMU
	stxa	%g5, [%g0] ASI_ITLB_DATA_IN
	membar	#Sync

	stxa	%g3, [%g6] ASI_DMMU
	stxa	%g5, [%g0] ASI_DTLB_DATA_IN
	membar	#Sync
	flush	%g3

	nop
	nop
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

#if 0
/* This is to dangerous for now... To many traps unfilled yet... */
	sethi	%hi(sparc64_ttable_tl0), %g5
	add	%g5, %g4, %g5
	wrpr	%g5, %tba
#endif

	sethi	%hi(bootup_kernel_stack + 0x2000 - STACK_BIAS - REGWIN_SZ), %g5
	or	%g5, %lo(bootup_kernel_stack + 0x2000 - STACK_BIAS - REGWIN_SZ), %g5
	add	%g5, %g4, %sp
	mov	0, %fp
	wrpr	%g0, PSTATE_PEF | PSTATE_PRIV, %pstate
	wrpr	%g0, 0, %wstate
	wrpr	%g0, 0x0, %tl
	fzero	%f48
	fzero	%f50
	fzero	%f52
	fzero	%f54
	fzero	%f56
	fzero	%f58
	fzero	%f60
	fzero	%f62

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

	mov	%l6, %o1			! OpenPROM stack
	call	prom_init
	 mov	%l7, %o0			! OpenPROM cif handler

	/* Off we go.... */
	call	start_kernel
	 nop
	/* Not reached... */

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
