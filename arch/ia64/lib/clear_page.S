/*
 *
 * Optimized version of the standard clearpage() function
 *
 * Based on comments from ddd. Try not to overflow the write buffer.
 *
 * Inputs:
 *	in0:	address of page
 * 
 * Output:
 * 	none
 *
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/page.h>

	.text
	.psr abi64
	.psr lsb
	.lsb

	.align 32
	.global clear_page
	.proc clear_page
clear_page:
	alloc r11=ar.pfs,1,0,0,0
	mov r16=ar.lc		// slow
	mov r17=PAGE_SIZE/32-1	// -1 = repeat/until
	;;
	adds r18=16,in0
	mov ar.lc=r17
	;;
1:	stf.spill.nta [in0]=f0,32
	stf.spill.nta [r18]=f0,32
	br.cloop.dptk.few 1b
	;;
	mov ar.lc=r16		// restore lc
	br.ret.sptk.few rp

	.endp clear_page
