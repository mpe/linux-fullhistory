/* $Id: dtlb_prot.S,v 1.5 1997/02/26 11:09:26 jj Exp $
 * dtlb_prot.S:	Fast TLB protection trap processing.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

	/* We are in the MMU globals, %g7 contains the physical
	 * address of current->mm->pgd at all times.  %g2 is
	 * also preloaded with the mask 0x1ff8 to make things
	 * even quicker.
	 *
	 * Many subtle things are done here.  The high bits of
	 * the virtual address missed are most easily obtained
	 * from the tag target (it is at address zero in ASI_IMMU
	 * so no address formation is necessary to get at this).
	 * This is used to compute the pgd and pmd table offsets.
	 *
	 * Even more clever is that physical page zero is always
	 * a page full of zeroes.  This means we can just follow
	 * through with all the page table traversals even if nothing
	 * is mapped because we'll just do loads from page zero
	 * and get yet another zero.  We only need to do the check
	 * for the valid bit being set in the final pte we obtain.
	 *
	 * Furthermore, we set the TSB base register to the address
	 * zero, and we use the 8KB tsb ptr to calculate the pte
	 * offset.  Again it is at address zero in ASI_IMMU_TSB_8KB_PTR
	 * so no address formation is necessary, saves more instructions.
	 *
	 * We use physical address accesses to get at the page
	 * tables, and this is for two reasons.  This makes it
	 * impossible to take a fault while we are servicing the
	 * miss.  Also this physical bypass access only allocates
	 * in the E-cache, and thus we prevent D-cache pollution
	 * from the miss handlers probing the page tables.
	 */

	/* I-cache line 0 */
	ldxa		[%g0] ASI_DMMU, %g1
	srlx		%g1, 8, %g3
	and		%g3, %g2, %g3
	ldxa		[%g7 + %g3] ASI_PHYS_USE_EC, %g5
	sllx		%g1, 2, %g4
	and		%g4, %g2, %g3
	ldxa		[%g5 + %g3] ASI_PHYS_USE_EC, %g4
	ldxa		[%g0] ASI_DMMU_TSB_8KB_PTR, %g1
	/* I-cache line 1 */
	srlx		%g1, 1, %g1
	ldxa		[%g4 + %g1] ASI_PHYS_USE_EC, %g3
	andcc		%g3, _PAGE_WRITE, %g0
	be,pn		%xcc, sparc64_dtlb_fault
	 or		%g3, (_PAGE_WRITE|_PAGE_W|_PAGE_MODIFIED|_PAGE_ACCESSED), %g3

	/* Blamo... */
	stxa		%g3, [%g4 + %g1] ASI_DMMU
	stxa		%g3, [%g0] ASI_DTLB_DATA_IN
	retry

	/* I-cache line 2 */
	nop; nop; nop; nop; nop; nop; nop; nop;
	/* I-cache line 3 */
	nop; nop; nop; nop; nop; nop; nop; nop;
