/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		MIPS specific IP/TCP/UDP checksumming routines
 *
 * Authors:	Ralf Baechle, <ralf@waldorf-gmbh.de>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <net/checksum.h>
#include <asm/string.h>

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */

unsigned int csum_partial(const unsigned char *buff, int len, unsigned int sum)
{
	unsigned long	scratch1;
	unsigned long	scratch2;

	/*
	 * The GCC generated code for handling carry bits makes
	 * it strongly desirable to do this in assembler!
	 */
    __asm__("
	.set	noreorder
	.set	noat
	andi	$1,%5,2		# Check alignment
	beqz	$1,2f		# Branch if ok
	subu	$1,%4,2		# delay slot, Alignment uses up two bytes
	bgez	$1,1f		# Jump if we had at least two bytes
	move	%4,$1		# delay slot
	j	4f
	addiu	%4,2		# delay slot; len was < 2.  Deal with it

1:	lw	%2,(%5)
	addiu	%4,2
	addu	%0,%2
	sltu	$1,%0,%2
	addu	%0,$1

2:	move	%1,%4
	srl	%1,%1,5
	beqz	%1,2f
	sll	%1,%1,5		# delay slot

	addu	%1,%5
1:	lw	%2,0(%5)
	addu	%5,32
	addu	%0,%2
	sltu	$1,%0,%2

	lw	%2,-28(%5)
	addu	%0,$1
	addu	%0,%2
	sltu	$1,%0,%2

	lw	%2,-24(%5)
	addu	%0,$1
	addu	%0,%2
	sltu	$1,%0,%2

	lw	%2,-20(%5)
	addu	%0,$1
	addu	%0,%2
	sltu	$1,%0,%2

	lw	%2,-16(%5)
	addu	%0,$1
	addu	%0,%2
	sltu	$1,%0,%2

	lw	%2,-12(%5)
	addu	%0,$1
	addu	%0,%2
	sltu	$1,%0,%2

	lw	%2,-8(%5)
	addu	%0,$1
	addu	%0,%2
	sltu	$1,%0,%2

	lw	%2,-4(%5)
	addu	%0,$1
	addu	%0,%2
	sltu	$1,%0,%2

	bne	%5,%1,1b
	addu	%0,$1		# delay slot

2:	andi	%1,%4,0x1c
	srl	%1,%1,2
	beqz	%1,4f
	addu	%1,%5		# delay slot
3:	lw	%2,0(%5)
	addu	%5,4
	addu	%0,%2
	sltu	$1,%0,%2
	bne	%5,%1,3b
	addu	%0,$1		# delay slot

4:	andi	$1,%3,2
	beqz	$1,5f
	move	%2,$0		# delay slot
	lhu	%2,(%5)
	addiu	%5,2

5:	andi	$1,%3,1
	beqz	$1,6f
	sll	%1,16		# delay slot
	lbu	%1,(%5)
	nop			# NOP ALERT (spit, gasp)
6:	or	%2,%1
	addu	%0,%2
	sltu	$1,%0,%2
	addu	%0,$1
7:	.set	at
	.set	reorder"
	: "=r"(sum), "=r" (scratch1), "=r" (scratch2)
	: "0"(sum), "r"(len), "r"(buff)
	: "$1");

	return sum;
}

/*
 * copy from fs while checksumming, otherwise like csum_partial
 */
unsigned int csum_partial_copy(const char *src, char *dst, 
				  int len, int sum)
{
	/*
	 * It's 2:30 am and I don't feel like doing it real ...
	 * This is lots slower than the real thing (tm)
	 */
	sum = csum_partial(src, len, sum);
	memcpy(dst, src, len);

	return sum;
}
