/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
	.text

/*
 * Use the BAT0 registers to map the 1st 8MB of RAM to
 * the address given as the 1st argument.
 */
	.globl	setup_bats
setup_bats:
	mr	4,3
	mfpvr	3
	rlwinm	3,3,16,16,31		/* r3 = 1 for 601, 4 for 604 */
	cmpi	0,3,1
	bne	4f
	ori	4,4,4			/* set up BAT registers for 601 */
	li	5,0x7f
	mtibatu	3,4
	mtibatl	3,5
	isync
	blr
4:	ori	4,4,0xfe		/* set up BAT registers for 604 */
	li	5,2
	mtdbatl	3,5
	mtdbatu	3,4
	mtibatl	3,5
	mtibatu	3,4
	isync
	blr

/*
 * Flush the dcache and invalidate the icache for a range of addresses.
 *
 * flush_cache(addr, len)
 */
	.global	flush_cache
flush_cache:
	addi	4,4,0x1f	/* len = (len + 0x1f) / 0x20 */
	rlwinm.	4,4,27,5,31
	mtctr	4
	beqlr
1:	dcbf	0,3
	icbi	0,3
	addi	3,3,0x20
	bdnz	1b
	sync
	isync
	blr
