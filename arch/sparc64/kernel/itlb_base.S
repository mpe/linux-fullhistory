/* $Id: itlb_base.S,v 1.5 1998/06/15 16:59:32 jj Exp $
 * itlb_base.S:	Front end to ITLB miss replacement strategy.
 *              This is included directly into the trap table.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@dm.cobaltmicro.com)
 * Copyright (C) 1997,1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#define TAG_CONTEXT_BITS	0x3ff
#define VPTE_SHIFT		(PAGE_SHIFT - 3)

/* Ways we can get here:
 *
 * 1) Nucleus instruction misses from module code.
 * 2) All user instruction misses.
 *
 * All real page faults merge their code paths to the
 * sparc64_realfault_* labels below.
 */

	.globl		sparc64_vpte_patchme

/* ITLB ** ICACHE line 1: Quick user TLB misses		*/
	ldxa		[%g1 + %g1] ASI_IMMU, %g4	! Get TAG_ACCESS
	srax		%g4, VPTE_SHIFT, %g6		! Create VPTE offset
	ldxa		[%g3 + %g6] ASI_P, %g5		! Load VPTE
1:	brgez,pn	%g5, 3f				! Not valid, branch out
	 and		%g5, (_PAGE_PRESENT|_PAGE_READ), %g4	! Mask readable bits
2:	stxa		%g5, [%g0] ASI_ITLB_DATA_IN	! Load PTE into TLB
	retry						! Trap return
3:	cmp		%g4, (_PAGE_PRESENT|_PAGE_READ)	! Readable page?

/* ITLB ** ICACHE line 2: Quick user ref updates	*/
	bne,pn		%xcc, 4f			! Nope, real missing page
	 sllx		%g1, 60, %g4			! Sliiickkk...
	or		%g5, _PAGE_ACCESSED, %g5	! Mark as touched
	or		%g5, %g4, %g5			! Allow user to see it
	ba,pt		%xcc, 2b			! Branch to load TLB
	 stxa		%g5, [%g3 + %g6] ASI_S		! Update PTE table
4:	rdpr		%pstate, %g4			! Move into alternate globals
	wrpr		%g4, PSTATE_AG|PSTATE_MG, %pstate

/* ITLB ** ICACHE line 3: Real faults			*/
	rdpr		%tpc, %g5			! And load faulting VA
sparc64_realfault_common:				! Called by TL0 dtlb_miss too
	sethi		%hi(1f), %g7			! Save state
	ba,pt		%xcc, etrap			! ...
1:	 or		%g7, %lo(1b), %g7		! ...
	clr		%o2				! It was read
sparc64_realfault_continue:				! Called by dtlb_prot handler
	srlx		%l5, PAGE_SHIFT, %o1		! Page align faulting VA
	add		%sp, STACK_BIAS + REGWIN_SZ, %o0! Compute pt_regs arg
	call		do_sparc64_fault		! Call fault handler

/* ITLB ** ICACHE line 4: Call fault processing code	 */
	 sllx		%o1, PAGE_SHIFT, %o1		! Finish page alignment
	ba,a,pt		%xcc, rtrap_clr_l6		! Restore cpu state
winfix_trampoline:
	rdpr		%tpc, %g3			! Prepare winfixup TNPC
	or		%g3, 0x7c, %g3			! Compute offset to branch
	wrpr		%g3, %tnpc			! Write it into TNPC
	done						! Do it to it
sparc64_vpte_nucleus:
	ba,pt		%xcc, sparc64_vpte_continue	! Part of dtlb_backend
sparc64_vpte_patchme:
	 sethi		%hi(0), %g5			! This has to be patched

#undef TAG_CONTEXT_BITS
#undef VPTE_SHIFT
