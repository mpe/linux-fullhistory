/* $Id: dtlb_prot.S,v 1.15 1998/01/14 17:14:46 jj Exp $
 * dtlb_prot.S:	Data TLB protection code, this is included directly
 *              into the trap table.
 *
 * Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

	/* We know kernel never takes protection trap,
	 * this makes this routine a lot easier than it
	 * would be otherwise.
	 */

#define MODIFIED_BITS	(_PAGE_WRITE | _PAGE_W | _PAGE_MODIFIED | _PAGE_ACCESSED)

				/* ICACHE line 1 */
  /*0x00*/	ldxa		[%g0] ASI_DMMU, %g1		! Get TAG_TARGET
  /*0x04*/	srlx		%g1, 10, %g3			! Position PGD offset
  /*0x08*/	and		%g1, 0xffe, %g4			! Mask PMD offset
  /*0x0c*/	and		%g3, 0xffc, %g3			! Mask PGD offset
  /*0x10*/	add		%g4, %g4, %g4			! Position PMD offset
  /*0x14*/	lduwa		[%g7 + %g3] ASI_PHYS_USE_EC, %g5	! Load PGD
  /*0x18*/	lduwa		[%g5 + %g4] ASI_PHYS_USE_EC, %g4	! Load PMD
  /*0x1c*/	ldxa		[%g0] ASI_DMMU_TSB_8KB_PTR, %g1		! For PTE offset

				/* ICACHE line 2 */
  /*0x20*/	srlx		%g1, 1, %g1				! PTE offset
  /*0x24*/	ldxa		[%g4 + %g1] ASI_PHYS_USE_EC, %g3	! Load PTE
  /*0x28*/	andcc		%g3, _PAGE_WRITE, %g0			! Writable?
  /*0x2c*/	be,pt		%xcc, sparc64_dtlb_prot_catch		! Nope...
  /*0x30*/	 or		%g3, (MODIFIED_BITS), %g3		! Yes it is
  /*0x34*/	mov		TLB_TAG_ACCESS, %g5			! Get the page
  /*0x38*/	add		%g1, %g4, %g1				! to get a tmpreg
  /*0x3c*/	ldxa		[%g5] ASI_DMMU, %g4			! From MMU

				/* ICACHE line 3 */
  /*0x40*/	mov		TLB_SFSR, %g5				! read SFSR
  /*0x44*/	srlx		%g4, 13, %g4				!   Prepare...
  /*0x48*/	ldxa		[%g5] ASI_DMMU, %g5			! from DMMU for
  /*0x4c*/	sllx		%g4, 13, %g4				!   ...and mask page
  /*0x50*/	and		%g5, 0x10, %g5				! context bit
  /*0x54*/	or		%g4, %g5, %g4				! for prot trap
1:/*0x58*/	stxa		%g0, [%g4] ASI_DMMU_DEMAP		! TLB flush page
  /*0x5c*/	membar		#Sync					! Synchronize

				/* ICACHE line 4 */
  /*0x60*/	stxa		%g3, [%g1] ASI_PHYS_USE_EC		! Update sw PTE
  /*0x64*/	stxa		%g3, [%g0] ASI_DTLB_DATA_IN		! TLB load
  /*0x68*/	retry							! Trap return
  /*0x6c*/	nop
  /*0x70*/	nop
  /*0x74*/	nop
  /*0x78*/	nop
  /*0x7c*/	nop

#undef MODIFIED_BITS
