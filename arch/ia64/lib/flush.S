/*
 * Cache flushing routines.
 *
 * Copyright (C) 1999-2000 Hewlett-Packard Co
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/asmmacro.h>
#include <asm/page.h>

	.text
	.psr abi64
	.psr lsb
	.lsb

GLOBAL_ENTRY(ia64_flush_icache_page)
	UNW(.prologue)
	alloc r2=ar.pfs,1,0,0,0
	UNW(.save ar.lc, r3)
	mov r3=ar.lc			// save ar.lc	

	.body

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
END(ia64_flush_icache_page)
