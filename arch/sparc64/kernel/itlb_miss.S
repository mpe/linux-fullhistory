/* $Id: itlb_miss.S,v 1.12 1998/01/14 17:14:47 jj Exp $
 * itlb_miss.S:	Instruction TLB miss code, this is included directly
 *              into the trap table.
 *
 * Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

/* Gratuitous comment. */

				/* ICACHE line 1 */
  /*0x00*/	ldxa		[%g0] ASI_IMMU, %g1		! Get TAG_TARGET
  /*0x04*/	srlx		%g1, 10, %g3			! Position PGD offset
  /*0x08*/	andcc		%g1, %g2, %g0			! Test CONTEXT bits
  /*0x0c*/	and		%g3, 0xffc, %g3			! Mask PGD offset
  /*0x10*/	and		%g1, 0xffe, %g4			! Mask PMD offset
  /*0x14*/	ldxa		[%g0] ASI_IMMU_TSB_8KB_PTR, %g1	! For PTE offset
  /*0x18*/	be,pn		%xcc, 3f			! Context 0 == kernel
  /*0x1c*/	 add		%g4, %g4, %g4			! Position PMD offset

				/* ICACHE line 2 */
  /*0x20*/	lduwa		[%g7 + %g3] ASI_PHYS_USE_EC, %g5	! Load user PGD
  /*0x24*/	srlx		%g1, 1, %g1				! PTE offset
  /*0x28*/	lduwa		[%g5 + %g4] ASI_PHYS_USE_EC, %g3	! Load PMD
  /*0x2c*/	ldxa		[%g3 + %g1] ASI_PHYS_USE_EC, %g5	! Load PTE
  /*0x30*/	brgez,pn	%g5, sparc64_itlb_refbit_catch		! Valid set?
  /*0x34*/	 nop							! delay
  /*0x38*/	stxa		%g5, [%g0] ASI_ITLB_DATA_IN		! TLB load
  /*0x3c*/	retry							! Trap return

3:				/* ICACHE line 3 */
  /*0x40*/	lduwa		[%g6 + %g3] ASI_PHYS_USE_EC, %g5	! Load kern PGD
  /*0x44*/	srlx		%g1, 1, %g1				! PTE offset
  /*0x48*/	lduwa		[%g5 + %g4] ASI_PHYS_USE_EC, %g3	! Load PMD
  /*0x4c*/	ldxa		[%g3 + %g1] ASI_PHYS_USE_EC, %g5	! Load PTE
  /*0x50*/	brgez,pn	%g5, sparc64_itlb_refbit_catch		! Valid set?
  /*0x54*/	 nop							! delay
  /*0x58*/	stxa		%g5, [%g0] ASI_ITLB_DATA_IN		! TLB load
  /*0x5c*/	retry							! Trap return

				/* ICACHE line 4 */
  /*0x60*/	nop
  /*0x64*/	nop
  /*0x68*/	nop
  /*0x6c*/	nop
  /*0x70*/	nop
  /*0x74*/	nop
  /*0x78*/	nop
  /*0x7c*/	nop
