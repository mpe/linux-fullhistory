/* $Id: trampoline.S,v 1.3 1998/02/22 21:06:11 jj Exp $
 * trampoline.S: Jump start slave processors on sparc64.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/lsu.h>
#include <asm/pstate.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/spitfire.h>
#include <asm/asm_offsets.h>

	.data
	.align		8
	.globl		smp_trampoline
smp_trampoline:		.skip	0x300

	.text
	.align		8
	.globl		sparc64_cpu_startup, sparc64_cpu_startup_end
sparc64_cpu_startup:
	flushw
	mov	(LSU_CONTROL_IC | LSU_CONTROL_DC | LSU_CONTROL_IM | LSU_CONTROL_DM), %g1
	stxa	%g1, [%g0] ASI_LSU_CONTROL
	membar	#Sync
	wrpr	%g0, (PSTATE_PRIV | PSTATE_PEF | PSTATE_IE), %pstate
	wrpr	%g0, 15, %pil

	sethi	%uhi(PAGE_OFFSET), %g4
	sllx	%g4, 32, %g4

	/* XXX Buggy PROM... */
	srl	%o0, 0, %g6
	add	%g6, %g4, %g6

	sethi	%uhi(_PAGE_VALID | _PAGE_SZ4MB), %g5
	sllx	%g5, 32, %g5
	or	%g5, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W | _PAGE_G), %g5

	sethi	%uhi(_PAGE_PADDR), %g3
	or	%g3, %ulo(_PAGE_PADDR), %g3
	sllx	%g3, 32, %g3
	sethi	%hi(_PAGE_PADDR), %g7
	or	%g7, %lo(_PAGE_PADDR), %g7
	or	%g3, %g7, %g3

	clr	%l0
	set	0x1fff, %l2
	rd	%pc, %l3
	andn	%l3, %l2, %g2
1:	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1
	cmp	%g1, %g2
	be,a,pn	%xcc, 2f
	 ldxa	[%l0] ASI_ITLB_DATA_ACCESS, %g1
	cmp	%l0, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

2:	nop
	nop
	nop
	and	%g1, %g3, %g1
	sub	%g1, %g2, %g1
	or	%g5, %g1, %g5
	clr	%l0
	sethi	%hi(KERNBASE), %g3
	sethi	%hi(KERNBASE<<1), %g7
	mov	TLB_TAG_ACCESS, %l7
1:	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1
	cmp	%g1, %g3
	blu,pn	%xcc, 2f
	 cmp	%g1, %g7
	bgeu,pn	%xcc, 2f
	 nop
	stxa	%g0, [%l7] ASI_IMMU
	stxa	%g0, [%l0] ASI_ITLB_DATA_ACCESS
2:	cmp	%l0, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	nop
	nop
	nop
	clr	%l0
1:	ldxa	[%l0] ASI_DTLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1
	cmp	%g1, %g3
	blu,pn	%xcc, 2f
	 cmp	%g1, %g7
	bgeu,pn	%xcc, 2f
	 nop
	stxa	%g0, [%l7] ASI_DMMU
	stxa	%g0, [%l0] ASI_DTLB_DATA_ACCESS
2:	cmp	%l0, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	nop
	nop
	nop
	sethi	%hi(KERNBASE), %g3
	mov	(63 << 3), %g7
	stxa	%g3, [%l7] ASI_DMMU
	stxa	%g5, [%g7] ASI_DTLB_DATA_ACCESS
	membar	#Sync
	stxa	%g3, [%l7] ASI_IMMU
	stxa	%g5, [%g7] ASI_ITLB_DATA_ACCESS
	membar	#Sync
	flush	%g3
	membar	#Sync
	b,pt	%xcc, 1f
	 nop
1:	set	bounce, %g2
	jmpl	%g2 + %g0, %g0
	 nop

bounce:
	mov	PRIMARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync
	mov	SECONDARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync

	mov	TLB_TAG_ACCESS, %g2
	stxa	%g3, [%g2] ASI_IMMU
	stxa	%g3, [%g2] ASI_DMMU

	mov	(63 << 3), %g7
	ldxa	[%g7] ASI_ITLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_ITLB_DATA_ACCESS
	membar	#Sync

	ldxa	[%g7] ASI_DTLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_DTLB_DATA_ACCESS
	membar	#Sync

	flush	%g3
	membar	#Sync

	mov	1, %g5
	sllx	%g5, (PAGE_SHIFT + 1), %g5
	sub	%g5, (REGWIN_SZ + STACK_BIAS), %g5
	add	%g6, %g5, %sp
	mov	0, %fp

	wrpr	%g0, 0, %wstate
	wrpr	%g0, 0, %tl

	/* Setup the trap globals, then we can resurface. */
	rdpr	%pstate, %o1
	mov	%g6, %o2
	wrpr	%o1, (PSTATE_AG | PSTATE_IE), %pstate
	sethi	%hi(sparc64_ttable_tl0), %g5
	wrpr	%g5, %tba
	mov	%o2, %g6

	wrpr	%o1, (PSTATE_MG | PSTATE_IE), %pstate
#define KERN_HIGHBITS ((_PAGE_VALID | _PAGE_SZ4MB) ^ 0xfffff80000000000)
	sethi   %uhi(KERN_HIGHBITS), %g2
	sllx    %g2, 32, %g2
#undef KERN_HIGHBITS
	ldx	[%o2 + AOFF_task_mm], %g6
	ldx	[%g6 + AOFF_mm_pgd], %g6
	clr	%g7

	wrpr	%o1, (PSTATE_IG | PSTATE_IE), %pstate
	sethi	%hi(ivector_to_mask), %g5
	or	%g5, %lo(ivector_to_mask), %g1
	mov	0x40, %g2

	wrpr	%g0, 0, %wstate
	wrpr	%o1, PSTATE_IE, %pstate

	mov	TSB_REG, %o4
	mov	1, %o5
	stxa	%o5, [%o4] ASI_DMMU
	stxa	%o5, [%o4] ASI_IMMU
	membar	#Sync

	or	%o1, PSTATE_IE, %o1
	wrpr	%o1, 0, %pstate

	call	smp_callin
	 nop
	call	cpu_idle
	 mov	0, %o0
	call	cpu_panic
	 nop
1:	b,a,pt	%xcc, 1b

	.align		8
sparc64_cpu_startup_end:
