/*
 * arch/mips/lib/strncpy_user.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1996 by Ralf Baechle
 */
#include <linux/errno.h>

#include <asm/asm.h>
#include <asm/regdef.h>

/*
 * Returns: -EFAULT if exception before terminator, N if the entire
 * buffer filled, else strlen.
 */

/*
 * Ugly special case have to check: we might get passed a user space
 * pointer which wraps into the kernel space ...
 */

LEAF(__strncpy_from_user)
		move		v0,zero
		move		v1,a1
		.set		noreorder
1:		lbu		t0,(v1)
		LONG_ADDIU	v1,1
		beqz		t0,2f
		sb		t0,(a0)		# delay slot
		LONG_ADDIU	v0,1
		bne		v0,a2,1b
		LONG_ADDIU	a0,1		# delay slot
		.set		reorder
2:		LONG_ADDU	t0,a1,v0
		xor		t0,a1
		bltz		t0,fault
		jr		ra		# return n
		END(__strncpy_from_user)

fault:		li		v0,-EFAULT
		jr		ra

		.section	__ex_table,"a"
		PTR		1b,fault
		.previous
