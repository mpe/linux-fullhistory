/* $Id: blockops.S,v 1.10 1997/06/24 17:29:10 jj Exp $
 * arch/sparc64/lib/blockops.S: UltraSparc block zero optimized routines.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include "VIS.h"

	.text
	.align		32

	.globl		__bfill64
__bfill64:		/* %o0 = buf, %o1= ptr to pattern */
	wr		%g0, FPRS_FEF, %fprs		! FPU	Group
	ldd		[%o1], %f48			! Load	Group
	wr		%g0, ASI_BLK_P, %asi		! LSU	Group
	membar		#StoreStore | #LoadStore	! LSU	Group
	mov		32, %g2				! IEU0	Group

	/* Cannot perform real arithmatic on the pattern, that can
	 * lead to fp_exception_other ;-)
	 */
	fmovd		%f48, %f50			! FPA	Group
	fmovd		%f48, %f52			! FPA	Group
	fmovd		%f48, %f54			! FPA	Group
	fmovd		%f48, %f56			! FPA	Group
	fmovd		%f48, %f58			! FPA	Group
	fmovd		%f48, %f60			! FPA	Group
	fmovd		%f48, %f62			! FPA	Group

1:	stda		%f48, [%o0 + 0x00] %asi		! Store	Group
	stda		%f48, [%o0 + 0x40] %asi		! Store	Group
	stda		%f48, [%o0 + 0x80] %asi		! Store	Group
	stda		%f48, [%o0 + 0xc0] %asi		! Store	Group
	subcc		%g2, 1, %g2			! IEU1	Group
	bne,pt		%icc, 1b			! CTI
	 add		%o0, 0x100, %o0			! IEU0
	membar		#Sync				! LSU	Group

	jmpl		%o7 + 0x8, %g0			! CTI	Group brk forced
	 wr		%g0, 0, %fprs			! FPU	Group

	.align		32
	.globl		__bzero_1page
__bzero_1page:
	wr		%g0, FPRS_FEF, %fprs		! FPU	Group
	fzero		%f0				! FPA	Group
	mov		32, %g1				! IEU0
	fzero		%f2				! FPA	Group
	faddd		%f0, %f2, %f4			! FPA	Group
	fmuld		%f0, %f2, %f6			! FPM
	faddd		%f0, %f2, %f8			! FPA	Group
	fmuld		%f0, %f2, %f10			! FPM

	faddd		%f0, %f2, %f12			! FPA	Group
	fmuld		%f0, %f2, %f14			! FPM
	wr		%g0, ASI_BLK_P, %asi		! LSU	Group
	membar		#StoreStore | #LoadStore	! LSU	Group
1:	stda		%f0, [%o0 + 0x00] %asi		! Store	Group
	stda		%f0, [%o0 + 0x40] %asi		! Store	Group
	stda		%f0, [%o0 + 0x80] %asi		! Store	Group
	stda		%f0, [%o0 + 0xc0] %asi		! Store	Group

	subcc		%g1, 1, %g1			! IEU1
	bne,pt		%icc, 1b			! CTI
	 add		%o0, 0x100, %o0			! IEU0	Group
	membar		#Sync				! LSU	Group
	jmpl		%o7 + 0x8, %g0			! CTI	Group brk forced
	 wr		%g0, 0, %fprs			! FPU	Group
