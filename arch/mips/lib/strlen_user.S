/*
 * arch/mips/lib/strlen_user.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1996 by Ralf Baechle
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/sgidefs.h>

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */
LEAF(__strlen_user)
		move		v0,zero
1:		lb		t0,(a0)
		LONG_ADDIU	v0,1
		LONG_ADDIU	a0,1
		bnez		t0,1b
		jr		ra
		END(__strlen_user)

		.section	__ex_table,"a"
		PTR		1b,fault
		.previous

fault:		move		v0,zero
		jr		ra
