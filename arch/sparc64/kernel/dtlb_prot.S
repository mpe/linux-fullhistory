/* $Id: dtlb_prot.S,v 1.10 1997/03/25 09:47:13 davem Exp $
 * dtlb_prot.S:	Data TLB protection code, this is included directly
 *              into the trap table.
 *
 * Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

	/* We know kernel never takes protection trap,
	 * this makes this routine a lot easier than it
	 * would be otherwise.
	 */

#define MODIFIED_BITS	(_PAGE_WRITE | _PAGE_W | _PAGE_MODIFIED | _PAGE_ACCESSED)

				/* ICACHE line 1 */
  /*0x00*/	ldxa		[%g0] ASI_DMMU, %g1		! Get TAG_TARGET
  /*0x04*/	srlx		%g1, 8, %g3			! Position PGD offset
  /*0x08*/	sllx		%g1, 2, %g4			! Position PMD offset
  /*0x0c*/	and		%g3, %g2, %g3			! Mask PGD offset
  /*0x10*/	and		%g4, %g2, %g3			! Mask PMD offset
  /*0x14*/	ldxa		[%g7 + %g3] ASI_PHYS_USE_EC, %g5	! Load PGD
  /*0x18*/	ldxa		[%g5 + %g3] ASI_PHYS_USE_EC, %g4	! Load PMD
  /*0x1c*/	ldxa		[%g0] ASI_DMMU_TSB_8KB_PTR, %g1		! For PTE offset

				/* ICACHE line 2 */
  /*0x20*/	srlx		%g1, 1, %g1				! PTE offset
  /*0x24*/	ldxa		[%g4 + %g1] ASI_PHYS_USE_EC, %g3	! Load PTE
  /*0x28*/	andcc		%g3, _PAGE_WRITE, %g0			! Writable?
  /*0x2c*/	be,pt		%xcc, sparc64_dtlb_fault		! Nope...
  /*0x30*/	 or		%g3, (MODIFIED_BITS), %g3		! Yes it is
  /*0x34*/	mov		TLB_TAG_ACCESS, %g5			! Get the page
  /*0x38*/	ldxa		[%g5] ASI_DMMU, %g1			! From MMU
  /*0x3c*/	add		%g2, 7, %g5				! Compute mask

				/* ICACHE line 3 */
  /*0x40*/	andn		%g1, %g5, %g1				! Mask page
  /*0x44*/	or		%g1, 0x10, %g1				! 2ndary Context
  /*0x48*/	stxa		%g0, [%g1] ASI_DMMU_DEMAP		! TLB flush page
  /*0x4c*/	membar		#Sync					! Synchronize
  /*0x50*/	stxa		%g3, [%g4 + %g1] ASI_PHYS_USE_EC	! Update sw PTE
  /*0x54*/	stxa		%g3, [%g0] ASI_DTLB_DATA_IN		! TLB load
  /*0x58*/	retry							! Trap return
  /*0x5c*/	nop

				/* ICACHE line 4 */
  /*0x60*/	nop
  /*0x64*/	nop
  /*0x68*/	nop
  /*0x6c*/	nop
  /*0x70*/	nop
  /*0x74*/	nop
  /*0x78*/	nop
  /*0x7c*/	nop

#undef MODIFIED_BITS
