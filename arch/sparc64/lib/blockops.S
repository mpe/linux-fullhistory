/* $Id: blockops.S,v 1.16 1998/10/20 03:09:04 jj Exp $
 * blockops.S: UltraSparc block zero optimized routines.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include "VIS.h"
#include <asm/visasm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/asm_offsets.h>

#define TOUCH(reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7)	\
	fmovd	%reg0, %f48; 	fmovd	%reg1, %f50;		\
	fmovd	%reg2, %f52; 	fmovd	%reg3, %f54;		\
	fmovd	%reg4, %f56; 	fmovd	%reg5, %f58;		\
	fmovd	%reg6, %f60; 	fmovd	%reg7, %f62;

#define	TLBTEMP_BASE	(8 * 1024 * 1024)
#define	DCACHE_SIZE	(PAGE_SIZE * 2)
#define TLBTEMP_ENT1	(61 << 3)
#define TLBTEMP_ENT2	(62 << 3)
#define TLBTEMP_ENTSZ	(1 << 3)

	.text
	.align		32
	.globl		copy_page
	.type		copy_page,@function
copy_page: /* %o0=dest, %o1=src */
	VISEntry
	ldx		[%g6 + AOFF_task_mm], %o2
	sub		%o0, %g4, %g1
	sethi		%uhi(_PAGE_VALID), %g3
	sub		%o1, %g4, %g2
	sllx		%g3, 32, %g3
	ldx		[%o2 + AOFF_mm_segments], %o0
	or		%g3, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W), %g3
	or		%g1, %g3, %g1
	or		%g2, %g3, %g2
	mov		TLB_TAG_ACCESS, %o2
	sethi		%hi(TLBTEMP_BASE), %o3
	sethi		%hi(DCACHE_SIZE), %o1
	add		%o0, %o3, %o0
	add		%o0, %o1, %o1
	sethi		%hi(TLBTEMP_ENT1), %o3
	rdpr		%pstate, %g3
	wrpr		%g3, PSTATE_IE, %pstate
	ldxa		[%o3] ASI_DTLB_TAG_READ, %o4
	ldxa		[%o3] ASI_DTLB_DATA_ACCESS, %o5
	stxa		%o0, [%o2] ASI_DMMU
	stxa		%g1, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync
	add		%o3, (TLBTEMP_ENTSZ), %o3
	ldxa		[%o3] ASI_DTLB_TAG_READ, %g5
	ldxa		[%o3] ASI_DTLB_DATA_ACCESS, %g7
	stxa		%o1, [%o2] ASI_DMMU
	stxa		%g2, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync

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
	membar		#Sync
	VISExit

	mov		TLB_TAG_ACCESS, %o2
	stxa		%g5, [%o2] ASI_DMMU
	stxa		%g7, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync
	sub		%o3, (TLBTEMP_ENTSZ), %o3
	stxa		%o4, [%o2] ASI_DMMU
	stxa		%o5, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync
	jmpl		%o7 + 0x8, %g0
	 wrpr		%g3, 0x0, %pstate

	.align		32
	.globl		clear_page
	.type		clear_page,@function
clear_page:		/* %o0=dest */
	VISEntryHalf
	ldx		[%g6 + AOFF_task_mm], %o2
	sub		%o0, %g4, %g1
	sethi		%uhi(_PAGE_VALID), %g3
	sllx		%g3, 32, %g3
	or		%g3, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W), %g3
	ldx		[%o2 + AOFF_mm_segments], %o0
	or		%g1, %g3, %g1
	mov		TLB_TAG_ACCESS, %o2
	sethi		%hi(TLBTEMP_BASE), %o3
	add		%o0, %o3, %o0
	sethi		%hi(TLBTEMP_ENT2), %o3
	rdpr		%pstate, %g3
	wrpr		%g3, PSTATE_IE, %pstate
	ldxa		[%o3] ASI_DTLB_TAG_READ, %g5
	ldxa		[%o3] ASI_DTLB_DATA_ACCESS, %g7
	stxa		%o0, [%o2] ASI_DMMU
	stxa		%g1, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync

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
	membar		#Sync				! LSU	Group
	VISExitHalf

	stxa		%g5, [%o2] ASI_DMMU
	stxa		%g7, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync
	jmpl		%o7 + 0x8, %g0
	 wrpr		%g3, 0x0, %pstate
