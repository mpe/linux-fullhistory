/*
 * Cache flushing routines.
 *
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/page.h>

	.text
	.psr abi64
	.psr lsb
	.lsb

	.align 16
	.global ia64_flush_icache_page
	.proc ia64_flush_icache_page
ia64_flush_icache_page:
	alloc r2=ar.pfs,1,0,0,0
	mov r3=ar.lc			// save ar.lc	
	mov r8=PAGE_SIZE/64-1		// repeat/until loop
	;;
	mov ar.lc=r8
	add r8=32,in0
	;;
.Loop1:	fc in0				// issuable on M0 only
	add in0=64,in0
	fc r8
	add r8=64,r8
	br.cloop.sptk.few .Loop1
	;;
	sync.i
	;;
	srlz.i
	;;	
	mov ar.lc=r3			// restore ar.lc
	br.ret.sptk.few rp
	.endp ia64_flush_icache_page
