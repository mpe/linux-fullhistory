/*
 * This module contains the PowerPC interrupt fielders
 * set of code at specific locations, based on function
 */

#include <linux/sys.h>
#include "../kernel/ppc_asm.tmpl"

_TEXT()

/*
 * Compute IP checksums
 *   _ip_fast_csum(buf, len) -- Optimized for IP header
 *   _ip_compute_csum(buf, len)
 */

_GLOBAL(_ip_fast_csum)
	li	r0,0
	addic	r0,r0,0		/* Clear initial carry */
	lwz	r4,0(r3)
	lwz	r5,4(r3)
	adde	r0,r0,r4
	lwz	r4,8(r3)
	adde	r0,r0,r5
	lwz	r5,12(r3)
	adde	r0,r0,r4
	lwz	r4,16(r3)
	adde	r0,r0,r5
	adde	r0,r0,r4
	mr	r3,r0
	andi.	r3,r3,0xFFFF
	srwi	r0,r0,16
	adde	r3,r3,r0
	andis.	r0,r3,1
	beq	10f
	addi	r3,r3,1
10:	not	r3,r3
	andi.	r3,r3,0xFFFF
	blr

_GLOBAL(_ip_compute_csum)
	li	r0,0
	addic	r0,r0,0
finish_ip_csum:	
	subi	r3,r3,4
	andi.	r5,r3,2		/* Align buffer to longword boundary */
	beq	10f
	lhz	r5,4(r3)
	adde	r0,r0,r5
	addi	r3,r3,2
	subi	r4,r4,2
10:	cmpi	0,r4,16		/* unrolled loop - 16 bytes at a time */
	blt	20f
	lwz	r5,4(r3)
	lwz	r6,8(r3)
	adde	r0,r0,r5
	lwz	r5,12(r3)
	adde	r0,r0,r6
	lwzu	r6,16(r3)
	adde	r0,r0,r5
	adde	r0,r0,r6
	subi	r4,r4,16
	b	10b
20:	cmpi	0,r4,4
	blt	30f
	lwzu	r5,4(r3)
	adde	r0,r0,r5
	subi	r4,r4,4
	b	20b
30:	cmpi	0,r4,2
	blt	40f
	lhz	r5,4(r3)
	addi	r3,r3,2
	adde	r0,r0,r5
	subi	r4,r4,2
40:	cmpi	0,r4,1
	bne	50f
	lbz	r5,4(r3)
	slwi	r5,r5,8		/* Upper byte of word */
	adde	r0,r0,r5
50:	mr	r3,r0
	andi.	r3,r3,0xFFFF
	srwi	r0,r0,16
	adde	r3,r3,r0
	andis.	r0,r3,1
	beq	60f
	addi	r3,r3,1
60:	not	r3,r3
	andi.	r3,r3,0xFFFF
	blr

_GLOBAL(_udp_check)
	addc	r0,r5,r6	/* Add in header fields */
	adde	r0,r0,r7
	b	finish_ip_csum	

_GLOBAL(_tcp_check)
	addc	r0,r5,r6	/* Add in header fields */
	adde	r0,r0,r7
	b	finish_ip_csum	

_GLOBAL(_csum_partial)
	li	r0,0
	addc	r0,r5,r0
	b	finish_ip_csum	

/*
 * Compute 16 bit sum:
 *   _csum_tcpudp_magic(int saddr, int daddr, int sum, int proto)
 */	
_GLOBAL(_csum_tcpudp_magic)
	addc	r0,r3,r4
	adde	r0,r0,r5
	adde	r0,r0,r6
	mr	r3,r0
	andi.	r3,r3,0xFFFF
	srwi	r0,r0,16
	adde	r3,r3,r0
	andis.	r0,r3,1			/* Carry out of 16 bits? */
	beq	10f
	addi	r3,r3,1
10:	not	r3,r3
	andi.	r3,r3,0xFFFF
	blr

