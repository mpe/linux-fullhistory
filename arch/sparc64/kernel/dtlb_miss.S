/* $Id: dtlb_miss.S,v 1.15 1998/01/14 17:14:44 jj Exp $
 * dtlb_miss.S:	Data TLB miss code, this is included directly
 *              into the trap table.
 *
 * Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

/* The basic algorithm is:
 *
 * if(faulting_context != 0) {
 *		pgd = pgd_offset(current->mm.pgd, fault_address);
 * page_table_walk_continue:
 *		pmd = pmd_offset(pgd, fault_address);
 *		pte = pte_offset(pmd, fault_address);
 *		if(pte & _PAGE_V) {
 *			tlb_load(pte, fault_address);
 *			return_from_trap();
 *		}
 *		goto longer_processing;
 * } else {
 *		if(fault_address >= PAGE_OFFSET) {
 *			pte_val = PAGE_KERNEL;
 *			if (fault_address & 0x10000000000)
 *				pte_val = PAGE_KERNEL_IO;
 *			tlb_load(__pa(fault_address) | pte_val);
 *			return_from_trap();
 *		} else {
 *			pgd = pgd_offset(swapper_pg_dir, fault_address);
 *			goto page_table_walk_continue;
 *		}
 * }
 *
 * This is optimized for user TLB misses on purpose.
 */

#define KERN_HIGHBITS	((_PAGE_VALID | _PAGE_SZ4MB) ^ 0xfffff80000000000)
#define KERN_LOWBITS	(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)
#define KERN_LOWBITS_IO	(_PAGE_E | _PAGE_P | _PAGE_W)

				/* ICACHE line 1 */
  /*0x00*/	ldxa		[%g0] ASI_DMMU, %g1		! Get TAG_TARGET
  /*0x04*/	srlx		%g1, 10, %g3			! Position PGD offset
  /*0x08*/	andcc		%g1, %g2, %g0			! Test CONTEXT bits
  /*0x0c*/	and		%g3, 0xffc, %g3			! Mask PGD offset
  /*0x18*/	and		%g1, 0xffe, %g4			! Mask PMD offset
  /*0x14*/	be,pn		%xcc, 3f			! Context 0 == kernel
  /*0x10*/	 add		%g4, %g4, %g4			! Position PMD offset
  /*0x1c*/	ldxa		[%g0] ASI_DMMU_TSB_8KB_PTR, %g1	! For PTE offset

				/* ICACHE line 2 */
  /*0x20*/	lduwa		[%g7 + %g3] ASI_PHYS_USE_EC, %g5	! Load PGD
  /*0x24*/	srlx		%g1, 1, %g1				! PTE offset
2:/*0x28*/	lduwa		[%g5 + %g4] ASI_PHYS_USE_EC, %g3	! Load PMD
  /*0x2c*/	ldxa		[%g3 + %g1] ASI_PHYS_USE_EC, %g5	! Load PTE
  /*0x30*/	brgez,pn	%g5, sparc64_dtlb_refbit_catch		! Valid set?
  /*0x34*/	 nop							! delay
  /*0x38*/	stxa		%g5, [%g0] ASI_DTLB_DATA_IN		! TLB load
  /*0x3c*/	retry							! Trap return

3:				/* ICACHE line 3 */
  /*0x40*/	sllx		%g1, 22, %g5			! This is now physical page + PAGE_OFFSET
  /*0x44*/	brgez,pn	%g5, 4f				! If >= 0, then walk down page tables
  /*0x48*/	 or		%g2, (KERN_LOWBITS), %g1	! Construct PTE ^ PAGE_OFFSET
  /*0x4c*/	andcc		%g3, 0x100, %g0			! Slick trick...
  /*0x50*/	bne,a,pn	%icc, 5f			! Is it an IO page?
  /*0x54*/	 or		%g2, (KERN_LOWBITS_IO), %g1	! Aha, it is IO...
5:/*0x58*/	xor		%g1, %g5, %g1			! Slick trick II...
  /*0x5c*/	stxa		%g1, [%g0] ASI_DTLB_DATA_IN	! TLB load

				/* ICACHE line 4 */
  /*0x60*/	retry						! Trap return
  /*0x64*/	nop
  /*0x68*/	nop
  /*0x6c*/	nop
4:/*0x70*/	ldxa		[%g0] ASI_DMMU_TSB_8KB_PTR, %g1		! For PTE offset
  /*0x74*/	lduwa		[%g6 + %g3] ASI_PHYS_USE_EC, %g5	! Load kern PGD
  /*0x78*/	ba,pt		%xcc, 2b				! Go back up top
  /*0x7c*/	 srlx		%g1, 1, %g1

#undef KERN_HIGHBITS
#undef KERN_LOWBITS
#undef KERN_LOWBITS_IO
