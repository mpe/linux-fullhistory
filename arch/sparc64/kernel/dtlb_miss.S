/* $Id: dtlb_miss.S,v 1.4 1996/12/28 18:39:40 davem Exp $
 * dtlb_miss.S:	Data TLB miss code, this is included directly
 *              into the trap table.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

	/* We are in the MMU globals, %g7 contains the physical
	 * address of current->mm->pgd at all times.
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
	 *
	 * XXX I think I can knock off two more instructions here...
	 */

dtlb_miss:
	/* I-cache line 0 */
	ldxa		[%g0] ASI_DMMU, %g1		! grab Tag Target either way
	brlz,pnt	%g1, 3f				! special kernel processing
	 srlx		%g1, 8, %g3			! put high vaddr bits in place

1:
	and		%g3, %g2, %g3			! get offset
	ldxa		[%g7 + %g3] ASI_PHYS_USE_EC, %g5! load pgd
	sllx		%g1, 2, %g4			! begin pmd_offset formation
	and		%g4, %g2, %g3			! and now mask it
	ldxa		[%g5 + %g3] ASI_PHYS_USE_EC, %g4! load pmd
	/* I-cache line 1 */
	ldxa		[%g0] ASI_DMMU_TSB_8KB_PTR, %g1	! get 8KB pointer bits
	srlx		%g1, 1, %g1			! shift right to get pte_offset
	ldxa		[%g4 + %g1] ASI_PHYS_USE_EC, %g3! load pte
	brlz,a,pt	%g3, 2f				! is valid bit clear?
	 stxa		%g3, [%g0] ASI_DTLB_DATA_IN	! nope, load TTE into DTLB

	ba,a,pt		%xcc, sparc64_dtlb_refbit_catch	! longer processing needed
2:
	retry						! return from trap

#define KTTE_HIGH_BITS	(_PAGE_VALID | _PAGE_SZ4MB)
#define KTTE_LOW_BITS	(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W | _PAGE_G)

	nop						! align next insn on cache line
3:
	/* I-cache line 2 */
	srax		%g1, 19, %g5			! mask down high bits
	cmp		%g5, -1				! if -1 this is VMALLOC area
	be,pnt		%xcc, 1b			! yep
	 sethi		%uhi(KTTE_HIGH_BITS), %g4	! begin pte formation

	sllx		%g1, 23, %g1			! begin masking for physpage
	sllx		%g4, 32, %g4			! high protection TTE bits
	or		%g4, (KTTE_LOW_BITS), %g4	! low protection TTE bits
	srlx		%g1, 41, %g1			! put physpage into place
	/* I-cache line 3 */
	or		%g4, %g1, %g1			! finish TTE computation
	stxa		%g1, [%g0] ASI_DTLB_DATA_IN	! load TTE into DTLB
	retry						! return from trap

	nop; nop; nop; nop; nop;

#undef KTTE_HIGH_BITS
#undef KTTE_LOW_BITS
