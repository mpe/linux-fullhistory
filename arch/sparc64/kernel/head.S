/* $Id: head.S,v 1.9 1997/02/26 11:09:25 jj Exp $
 * head.S: Initial boot code for the Sparc64 port of Linux.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/version.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/errno.h>

/* This section from from _start to sparc64_boot_end should fit into
   0xfffff80000004000 to 0xfffff80000008000 and will be sharing space
   with bootup_user_stack, which is from 0xfffff80000004000 to
   0xfffff80000006000 and bootup_kernel_stack, which is from
   0xfffff80000006000 to 0xfffff80000008000. */

	.text
	.globl	start, _start
_start:
start:
bootup_user_stack:
! 0xfffff80000004000
	b	sparc64_boot
	 rdpr	%ver, %g1			/* Get VERSION register.	*/

/* This stuff has to be in sync with SILO and other potential boot loaders
 * Fields should be kept upward compatible and whenever any change is made,
 * HdrS version should be incremented.
 */
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

sparc64_boot:
	/* We must be careful, 32-bit OpenBOOT will get confused if it
	 * tries to save away a register window to a 64-bit kernel
	 * stack address.  Flush all windows, disable interrupts,
	 * remap if necessary, jump onto kernel trap table, then kernel
	 * stack, or else we die.
	 */
	flushw					/* Flush register file.      */
	wrpr	%g0, 0xf, %pil			/* Interrupts off.           */

	/* Remap ourselves to upper 64-bit addresses if necessary.
	 */
	sethi	%uhi(PAGE_OFFSET), %g4
current_pc:
	rd	%pc, %g2
	sllx	%g4, 32, %g4
	sethi	%hi(current_pc), %g3
	or	%g3, %lo(current_pc), %g3
	add	%g4, %g3, %g3
	cmp	%g3, %g2
	be	go_to_highmem
	 nop

	/* Remap ourselves into high addresses. */
	sethi	%uhi(_PAGE_VALID | _PAGE_SZ4MB), %g5
	sllx	%g5, 32, %g5
	or	%g5, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W | _PAGE_G | _PAGE_L), %g5

	/* Be real fucking anal... */
	stxa	%g0, [%g4] ASI_IMMU_DEMAP
	stxa	%g0, [%g4] ASI_DMMU_DEMAP
	membar	#Sync
	flush	%g4

	mov	TLB_TAG_ACCESS, %g6
	stxa	%g4, [%g6] ASI_IMMU
	stxa	%g5, [%g0] ASI_ITLB_DATA_IN
	membar	#Sync
	flush	%g4

	stxa	%g4, [%g6] ASI_DMMU
	stxa	%g5, [%g0] ASI_DTLB_DATA_IN
	membar	#Sync
	flush	%g4

/* FIXME: Should clean here the page @ phys. 0 and map one page @ */

go_to_highmem:
	jmpl	%g3 + (execute_in_high_mem - current_pc), %g0
	 nop

execute_in_high_mem:

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
	flush	%g3
	stxa	%g3, [%g6] ASI_DMMU
	stxa	%g5, [%g0] ASI_DTLB_DATA_IN
	membar	#Sync
	flush	%g3

	sethi	%hi(nwindows), %g7
	and	%g1, VERS_MAXWIN, %g5
	add	%g7, %lo(nwindows), %g7
	add	%g5, 1, %g6
	add	%g7, (nwindows - nwindowsm1), %g3
	stx	%g6, [%g7 + %g4]
	stx	%g5, [%g3 + %g4]
	mov	%sp, %o1			! second argument to prom_init
	sethi	%hi(init_task), %g6
	or	%g6, %lo(init_task), %g6
	add	%g6, %g4, %g6			! g6 usage is fixed as well

/* FIXME: Initialize MMU globals??? */

	sethi	%hi(sparc64_ttable_tl0), %g5
	add	%g5, %g4, %g5
	wrpr	%g5, %tba

	sethi	%hi(bootup_kernel_stack + 0x2000 - STACK_BIAS - REGWIN_SZ), %g5
	or	%g5, %lo(bootup_kernel_stack + 0x2000 - STACK_BIAS - REGWIN_SZ), %g5
	add	%g5, %g4, %sp
	mov	0, %fp
	wrpr	%g0, PSTATE_PEF | PSTATE_PRIV, %pstate
	wrpr	%g0, 0, %wstate
	wrpr	%g0, 0x0, %tl

	call	prom_init
	 mov	%o4, %o0			! OpenPROM cif handler

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

        .global root_flags
        .global ram_flags
        .global root_dev
        .global ramdisk_image
        .global ramdisk_size

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
