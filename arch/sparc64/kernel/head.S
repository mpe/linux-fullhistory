/* $Id: head.S,v 1.6 1997/01/06 20:32:44 jj Exp $
 * head.S: Initial boot code for the Sparc64 port of Linux.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/pstate.h>
#include <asm/spitfire.h>
#include <asm/pgtable.h>
#include <asm/errno.h>

	.text

#include "ttable.S"

sparc64_boot:
	rdpr	%ver, %g1			/* Get VERSION register.	*/

	/* We must be careful, 32-bit OpenBOOT will get confused if it
	 * tries to save away a register window to a 64-bit kernel
	 * stack address.  Flush all windows, disable interrupts,
	 * remap if necessary, jump onto kernel trap table, then kernel
	 * stack, or else we die.
	 */
	flushw					/* Flush register file.      */
	wrpr	%g0, 0xf, %pil			/* Interrupts off.           */

	/* Remap ourselves to upper 64-bit addresses if necessary.
	 * SILO64 will have loaded us to the right location already.
	 */
	mov	%o7, %g4
current_pc:
	call	1f
	 mov	%o7, %g3
1:
	mov	%g4, %o7

	set	current_pc, %g7
	cmp	%g3, %g7
	be	go_to_highmem
	 nop

	/* Remap ourselves into high addresses. */
	set	PAGE_OFFSET, %g4
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

go_to_highmem:
	set	execute_in_high_mem, %g7
	jmpl	%g7, %g0
	 nop

execute_in_high_mem:

	set	nwindows, %g7
	and	%g1, VERS_MAXWIN, %g5
	add	%g5, 1, %g4
	stx	%g4, [%g7]
	set	nwindowsm1, %g6
	stx	%g5, [%g6]
	mov	%sp, %o1			! second argument to prom_init
	set	swapper_pg_dir, %g6
	set	PAGE_OFFSET, %g4		! this stays here for a long time
	sub	%g6, %g4, %g5
	set	init_task, %g6			! g6 usage is fixed as well

	set	sparc64_ttable_tl0, %g5
	wrpr	%g5, %tba

	set	bootup_kernel_stack, %sp
	mov	0, %fp
	wrpr	%g0, PSTATE_KERNEL, %pstate
	wrpr	%g0, WSTATE_KERNEL, %wstate
	wrpr	%g0, 0x0, %tl

	/* XXX Map in PROM 32-bit trampoline code. */
	
	call	prom_init
	 mov	%o4, %o0			! OpenPROM cif handler

	/* Off we go.... */
	call	start_kernel
	 nop

	/* Not reached... */

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
