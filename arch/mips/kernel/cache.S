/*
 * arch/mips/kernel/cache.S
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle
 *
 * Flush instruction/data caches
 *
 * Parameters: a0 - starting address to flush
 *             a1 - size of area to be flushed
 *             a2 - which caches to be flushed
 *
 * FIXME:      - ignores parameters
 *             - doesn't know about second level caches
 *             - only knows how to handle the R4600
 */

#include <asm/mipsconfig.h>
#include <asm/regdef.h>
#include <asm/segment.h>

#define PAGE_SIZE	0x1000

#define CACHELINES	512             /* number of cachelines    */

		.set	noreorder
		.globl	_sys_cacheflush
		.text
_sys_cacheflush:
		/*
		 * Writeback/invalidate the data cache
		 */
		li	t0,KSEG0
		li	t1,CACHELINES
1:		cache	1,0(t0)
		cache	1,32(t0)
		cache	1,64(t0)
		cache	1,96(t0)
		cache	1,128(t0)
		cache	1,160(t0)
		cache	1,192(t0)
		cache	1,224(t0)
		cache	1,256(t0)
		cache	1,288(t0)
		cache	1,320(t0)
		cache	1,352(t0)
		cache	1,384(t0)
		cache	1,416(t0)
		cache	1,448(t0)
		cache	1,480(t0)
		subu    t1,t1,1
		bnez	t1,1b
		addiu	t0,t0,512			# delay slot

		/*
		 * Flush the instruction cache
		 */
		lui	t0,0x8000
		li	t1,CACHELINES
1:		cache	0,0(t0)	
		cache	0,32(t0)
		cache	0,64(t0)
		cache	0,96(t0)
		cache	0,128(t0)
		cache	0,160(t0)
		cache	0,192(t0)
		cache	0,224(t0)
		cache	0,256(t0)
		cache	0,288(t0)
		cache	0,320(t0)
		cache	0,352(t0)
		cache	0,384(t0)
		cache	0,416(t0)
		cache	0,448(t0)
		cache	0,480(t0)
		subu	t1,t1,1
		bnez	t1,1b
		addiu	t0,t0,512			# delay slot

		j	ra
		nop
