/* $Id: blockops.S,v 1.14 1998/06/12 14:53:46 jj Exp $
 * blockops.S: UltraSparc block zero optimized routines.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include "VIS.h"
#include <asm/visasm.h>

#define TOUCH(reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7)	\
	fmovd	%reg0, %f48; 	fmovd	%reg1, %f50;		\
	fmovd	%reg2, %f52; 	fmovd	%reg3, %f54;		\
	fmovd	%reg4, %f56; 	fmovd	%reg5, %f58;		\
	fmovd	%reg6, %f60; 	fmovd	%reg7, %f62;

	.text
	.align		32
	.globl		copy_page
	.type		copy_page,@function
copy_page: /* %o0=dest, %o1=src */
	VISEntry
	membar		#LoadStore | #StoreStore | #StoreLoad
	ldda		[%o1] ASI_BLK_P, %f0
	add		%o1, 0x40, %o1
	ldda		[%o1] ASI_BLK_P, %f16
	add		%o1, 0x40, %o1
	sethi		%hi(8192), %o2
1:	TOUCH(f0, f2, f4, f6, f8, f10, f12, f14)
	ldda		[%o1] ASI_BLK_P, %f32
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	stda		%f48, [%o0] ASI_BLK_P
	add		%o0, 0x40, %o0
	TOUCH(f16, f18, f20, f22, f24, f26, f28, f30)
	ldda		[%o1] ASI_BLK_P, %f0
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	stda		%f48, [%o0] ASI_BLK_P
	add		%o0, 0x40, %o0
	TOUCH(f32, f34, f36, f38, f40, f42, f44, f46)
	ldda		[%o1] ASI_BLK_P, %f16
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	stda		%f48, [%o0] ASI_BLK_P
	cmp		%o2, 0x80
	bne,pt		%xcc, 1b
	 add		%o0, 0x40, %o0
	membar		#Sync
	stda		%f0, [%o0] ASI_BLK_P
	add		%o0, 0x40, %o0
	stda		%f16, [%o0] ASI_BLK_P
	membar		#StoreStore | #StoreLoad
	jmpl		%o7 + 0x8, %g0
	 VISExit

	.align		32
	.globl		__bzero_1page
	.type		__bzero_1page,@function
__bzero_1page:	/* %o0=dest */
	VISEntryHalf
	fzero		%f0				! FPA	Group
	mov		32, %o1				! IEU0
	fzero		%f2				! FPA	Group
	faddd		%f0, %f2, %f4			! FPA	Group
	fmuld		%f0, %f2, %f6			! FPM
	faddd		%f0, %f2, %f8			! FPA	Group
	fmuld		%f0, %f2, %f10			! FPM

	faddd		%f0, %f2, %f12			! FPA	Group
	fmuld		%f0, %f2, %f14			! FPM
	wr		%g0, ASI_BLK_P, %asi		! LSU	Group
	membar		#StoreLoad | #StoreStore | #LoadStore	! LSU	Group
1:	stda		%f0, [%o0 + 0x00] %asi		! Store	Group
	stda		%f0, [%o0 + 0x40] %asi		! Store	Group
	stda		%f0, [%o0 + 0x80] %asi		! Store	Group
	stda		%f0, [%o0 + 0xc0] %asi		! Store	Group

	subcc		%o1, 1, %o1			! IEU1
	bne,pt		%icc, 1b			! CTI
	 add		%o0, 0x100, %o0			! IEU0	Group
	membar		#StoreStore | #StoreLoad	! LSU	Group
	jmpl		%o7 + 0x8, %g0			! CTI	Group brk forced
	 VISExitHalf
