/* $Id: dtlb_miss.S,v 1.11 1997/04/10 01:59:35 davem Exp $
 * dtlb_miss.S:	Data TLB miss code, this is included directly
 *              into the trap table.
 *
 * Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
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
 *		if(fault_address >= KERNBASE &&
 *		   fault_address < VMALLOC_START) {
 *			tlb_load(__pa(fault_address) | PAGE_KERNEL);
 *			return_from_trap();
 *		} else {
 *			pgd = pgd_offset(swapper_pg_dir, fault_address);
 *			goto page_table_walk_continue;
 *		}
 * }
 *
 * This is optimized for user TLB misses on purpose.
 */

#define KERN_HIGHBITS	(_PAGE_VALID | _PAGE_SZ4MB)
#define KERN_LOWBITS	(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)
#define KERN_LOWBITS_IO	(_PAGE_E | _PAGE_P | _PAGE_W)

				/* ICACHE line 1 */
  /*0x00*/	ldxa		[%g0] ASI_DMMU, %g1		! Get TAG_TARGET
  /*0x04*/	srlx		%g1, 8, %g3			! Position PGD offset
  /*0x08*/	srlx		%g1, 48, %g5			! Shift down CONTEXT bits
  /*0x0c*/	and		%g3, %g2, %g3			! Mask PGD offset
  /*0x10*/	sllx		%g1, 2, %g4			! Position PMD offset
  /*0x14*/	brz,pn		%g5, 3f				! Context 0 == kernel
  /*0x18*/	 and		%g4, %g2, %g4			! Mask PMD offset
  /*0x1c*/	ldxa		[%g0] ASI_DMMU_TSB_8KB_PTR, %g1	! For PTE offset

				/* ICACHE line 2 */
  /*0x20*/	ldxa		[%g7 + %g3] ASI_PHYS_USE_EC, %g5	! Load PGD
  /*0x24*/	srlx		%g1, 1, %g1				! PTE offset
2:/*0x28*/	ldxa		[%g5 + %g4] ASI_PHYS_USE_EC, %g3	! Load PMD
  /*0x2c*/	ldxa		[%g3 + %g1] ASI_PHYS_USE_EC, %g5	! Load PTE
  /*0x30*/	brlz,a,pt	%g5, 1f					! Valid set?
  /*0x34*/	 stxa		%g5, [%g0] ASI_DTLB_DATA_IN		! TLB load
  /*0x38*/	ba,a,pt		%xcc, sparc64_dtlb_refbit_catch		! Nope...
1:/*0x3c*/	retry							! Trap return

3:				/* ICACHE line 3 */
  /*0x40*/	sllx		%g1, 43, %g5		! This gets >= VMALLOC_START...
  /*0x44*/	brlz,pn		%g5, 4f			! ...if now less than zero.
  /*0x48*/	 andncc		%g1, 0x3ff, %g0		! Slick trick...
  /*0x4c*/	be,pn		%xcc, 4f		! Yes, it is some PROM mapping
  /*0x50*/	 srlx		%g5, 21, %g5		! This is now physical page
  /*0x54*/	sethi		%uhi(KERN_HIGHBITS), %g1	! Construct PTE
  /*0x58*/	sllx		%g1, 32, %g1			! Move priv bits up
  /*0x5c*/	or		%g1, %g5, %g1			! Or in the page

				/* ICACHE line 4 */
  /*0x60*/	or		%g1, (KERN_LOWBITS), %g1	! Set low priv bits
  /*0x64*/	stxa		%g1, [%g0] ASI_DTLB_DATA_IN	! TLB load
  /*0x68*/	retry						! Trap return
4:/*0x6c*/	ldxa		[%g0] ASI_DMMU_TSB_8KB_PTR, %g1	! For PTE offset
  /*0x70*/	ldxa		[%g6 + %g3] ASI_PHYS_USE_EC, %g5	! Load kern PGD
  /*0x74*/	ba,pt		%xcc, 2b			! Go back up top
  /*0x78*/	 srlx		%g1, 1, %g1
  /*0x7c*/	nop

#undef KERN_HIGHBITS
#undef KERN_LOWBITS
