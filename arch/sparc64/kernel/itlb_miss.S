/* $Id: itlb_miss.S,v 1.5 1997/02/25 20:00:05 jj Exp $
 * itlb_miss.S:	Instruction TLB miss code, this is included directly
 *              into the trap table.
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
	 *
	 * It looks very hairy and slow.  But I take only 1 more
	 * overhead of loads from ram than the Solaris version, and
	 * my version is one instruction quicker for a true TLB miss.
	 * And more importantly, all true TLB misses under Linux will be
	 * serviced in _constant_ time.  When using the TSB in the
	 * manner it was intended to be used (like solaris does) the
	 * overhead for a TLB miss is _indeterminate_ especially during
	 * processes startup when the TSB is cold.
	 */

	/* I-cache line 0 */
	ldxa		[%g0] ASI_IMMU, %g1		! grab Tag Target
	srlx		%g1, 8, %g3			! put high vaddr bits in place
	and		%g3, %g2, %g3			! get offset
	ldxa		[%g7 + %g3] ASI_PHYS_USE_EC, %g5! load pgd
	sllx		%g1, 2, %g4			! begin pmd_offset formation
	and		%g4, %g2, %g3			! and now mask it
	ldxa		[%g5 + %g3] ASI_PHYS_USE_EC, %g4! load pmd
	ldxa		[%g0] ASI_IMMU_TSB_8KB_PTR, %g1	! get 8KB pointer bits
	/* I-cache line 1 */
	srlx		%g1, 1, %g1			! shift right to get pte_offset
	ldxa		[%g4 + %g1] ASI_PHYS_USE_EC, %g2! load pte
	brlz,a,pt	%g2, 1f				! is valid bit clear?
	 stxa		%g2, [%g0] ASI_ITLB_DATA_IN	! nope, load TTE into ITLB

	ba,a,pt		%xcc, sparc64_itlb_refbit_catch	! longer processing needed
1:
	retry						! return from trap

	nop; nop;
	/* I-cache line 2 */
	nop; nop; nop; nop; nop; nop; nop; nop;
	/* I-cache line 3 */
	nop; nop; nop; nop; nop; nop; nop; nop;
