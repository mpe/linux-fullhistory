/* $Id: dtlb_prot.S,v 1.17 1998/05/25 16:59:11 davem Exp $
 * dtlb_prot.S: DTLB protection trap strategy.
 *              This is included directly into the trap table.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@dm.cobaltmicro.com)
 * Copyright (C) 1997,1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#define TAG_CONTEXT_BITS	0x3ff
#define VPTE_SHIFT		(PAGE_SHIFT - 3)
#define MODIFIED_BITS		(_PAGE_WRITE | _PAGE_W | _PAGE_MODIFIED | _PAGE_ACCESSED)

/* Ways we can get here:
 *
 * [TL == 0] 1) User stores to readonly pages.
 * [TL == 0] 2) Nucleus stores to user readonly pages.
 * [TL >  0] 3) Nucleus stores to user readonly stack frame.
 */

/* PROT ** ICACHE line 1: User DTLB protection trap	*/
	ldxa		[%g1] ASI_DMMU, %g6		! Primary or Secondary ctx?
	and		%g6, 0x10, %g6			! Get pri/sec ctx bit
	stxa		%g0, [%g1] ASI_DMMU		! Clear SFSR FaultValid bit
	membar		#Sync				! Synchronize ASI stores
	ldxa		[%g1 + %g1] ASI_DMMU, %g4	! Load TAG_ACCESS
	andn		%g4, TAG_CONTEXT_BITS, %g4	! Clear CTX bits
	stxa		%g0, [%g4 + %g6] ASI_DMMU_DEMAP	! Perform TLB flush of page
	membar		#Sync				! Synchronize ASI stores

/* PROT ** ICACHE line 2: Further normal processing	*/
	srax		%g4, VPTE_SHIFT, %g6		! Compute VPTE offset
	ldxa		[%g3 + %g6] ASI_S, %g5		! Load PTE entry
	andcc		%g5, _PAGE_WRITE, %g0		! Writable page?
	be,pt		%xcc, 1f			! Nope, real fault
	 or		%g5, (MODIFIED_BITS), %g5	! Mark as writable/modified
	stxa		%g5, [%g3 + %g6] ASI_S		! Update PTE entry
	stxa		%g5, [%g0] ASI_DTLB_DATA_IN	! Load PTE into TLB
	retry						! Trap return

/* PROT ** ICACHE line 3: Real user faults		*/
1:	rdpr		%pstate, %g5			! Move into alternate globals
	wrpr		%g5, PSTATE_AG|PSTATE_MG, %pstate
	rdpr		%tl, %g4			! Need to do a winfixup?
	cmp		%g4, 1				! Trap level >1?
	mov		TLB_TAG_ACCESS, %g4		! Prepare reload of vaddr
	bgu,pn		%xcc, winfix_trampoline		! Yes, perform winfixup
	 ldxa		[%g4] ASI_DMMU, %g5		! Put tagaccess in %g5
	sethi		%hi(1f), %g7			! Nope, normal fault

/* PROT ** ICACHE line 4: More real fault processing */
	ba,pt		%xcc, etrap			! Save state
1:	 or		%g7, %lo(1b), %g7		! ...
	ba,pt		%xcc, sparc64_realfault_continue! Now call the fault handler
	 mov		1, %o2				! Indicate this was a write
	nop
	nop
	nop
	nop

#undef TAG_CONTEXT_BITS
#undef VPTE_SHIFT
#undef MODIFIED_BITS
