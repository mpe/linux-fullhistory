/*
 * arch/mips/lib/strlen_user.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1996, 1998 by Ralf Baechle
 *
 * $Id: strlen_user.S,v 1.3 1998/05/03 11:13:45 ralf Exp $
 */
#include <asm/asm.h>
#include <asm/offset.h>
#include <asm/regdef.h>
#include <asm/sgidefs.h>

#define EX(insn,reg,addr,handler)			\
9:	insn	reg, addr;				\
	.section __ex_table,"a";			\
	PTR	9b, handler;				\
	.previous

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */
LEAF(__strlen_user_nocheck_asm)
	lw	v0, THREAD_CURDS($28)			# pointer ok?
	subu	v0, zero, v0
	and	v0, a0
	nor	v0, zero, v0
	beqz	v0, fault
EXPORT(__strlen_user_asm)
	move		v0, a0
1:	EX(lb, t0, (v0), fault)
	LONG_ADDIU	v0, 1
	bnez		t0, 1b
	LONG_SUBU	v0, a0
	jr		ra
	END(__strlen_user_nocheck_asm)

	.section	__ex_table,"a"
	PTR		1b, fault
	.previous

fault:	move		v0, zero
	jr		ra
