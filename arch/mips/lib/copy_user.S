/*
 * arch/mips/lib/copy_user.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1996, 1997 by Ralf Baechle
 *
 * Less stupid user_copy implementation for 32 bit MIPS CPUs.
 *
 * $Id: copy_user.S,v 1.2 1997/08/11 04:26:12 ralf Exp $
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>

#define BLOCK_SIZE	16

#define EX(addr,handler)                        \
		.section	__ex_table,"a"; \
		PTR		addr, handler;  \
		.previous
#define UEX(addr,handler)                       \
		EX(addr,handler);               \
		EX(addr+4,handler)

		.set		noreorder
		.set		noat

/* ---------------------------------------------------------------------- */

/*
 * Bad.  We can't fix the alignment for both address parts.
 * Align the source address and copy slowly ...
 */
not_even_the_same_alignment:
		LONG_SUBU	v1,zero,a1
		andi		v1,3
		sltu		t0,v0,v1
		MOVN(v1,v0,t0)
		beqz		v1,src_aligned
		 LONG_ADDU	v1,a0
1:		lb		$1,(a1)
		EX(1b, fault)
		LONG_ADDIU	a1,1
2:		sb		$1,(a0)
		EX(2b, fault)
		LONG_ADDIU	a0,1
		bne		a0,v1,1b
		 LONG_SUBU	v0,1
src_aligned:

/*
 * Ok.  We've fixed the alignment of the copy src for this case.
 * Now let's copy in the usual BLOCK_SIZE byte blocks using unaligned
 * stores.
 * XXX Align the destination address.  This is better if the __copy_user
 *     encounters an access fault because we never have to deal with an
 *     only partially modified destination word.  This is required to
 *     keep the semantics of the result of copy_*_user().
 */
		ori		v1,v0,BLOCK_SIZE-1
		xori		v1,BLOCK_SIZE-1
		beqz		v1,copy_left_over
		 nop
		LONG_SUBU	v0,v1
		LONG_ADDU	v1,a0

1:		lw		t0,(a1)		# Can cause tlb fault
		EX(1b, fault)
2:		lw		t1,4(a1)	# Can cause tlb fault
		EX(2b, fault)
2:		lw		t2,8(a1)	# Can cause tlb fault
		EX(2b, fault)
2:		lw		t3,12(a1)	# Can cause tlb fault
		EX(2b, fault)
2:		usw		t0,(a0)		# Can cause tlb faults
		UEX(2b, fault)
2:		usw		t1,4(a0)	# Can cause tlb faults
		UEX(2b, fault_plus_4)
2:		usw		t2,8(a0)	# Can cause tlb faults
		UEX(2b, fault_plus_8)
2:		usw		t3,12(a0)	# Can cause tlb faults
		UEX(2b, fault_plus_12)
		LONG_ADDIU	a0,BLOCK_SIZE
		bne		a0,v1,1b
		 LONG_ADDIU	a1,BLOCK_SIZE
9:
		b		copy_left_over	# < BLOCK_SIZE bytes left
		 nop

/* ---------------------------------------------------------------------- */

not_w_aligned:
/*
 * Ok, src or destination are not 4-byte aligned.
 * Try to fix that.  Do at least both addresses have the same alignment?
 */
		xor		t0,a0,a1
		andi		t0,3
		bnez		t0,not_even_the_same_alignment
		nop					# delay slot

/*
 * Ok, we can fix the alignment for both operands and go back to the
 * fast path.  We have to copy at least one byte, on average 3 bytes
 * bytewise.
 */
		LONG_SUBU	v1,zero,a0
		andi		v1,3
		sltu		t0,v0,v1
		MOVN(v1,v0,t0)
		beqz		v1,__copy_user
		 LONG_ADDU	v1,a0
1:		lb		$1,(a1)
		EX(1b, fault)
		LONG_ADDIU	a1,1
2:		sb		$1,(a0)
		EX(2b, fault)
		LONG_ADDIU	a0,1
		bne		a0,v1,1b
		 LONG_SUBU	v0,1
		b		align4
		 nop

/* ---------------------------------------------------------------------- */

LEAF(__copy_user)
		or		t1,a0,a1
		andi		t1,3
		bnez		t1,not_w_aligned	# not word alignment
		 move		v0,a2

align4:
		ori		v1,v0,BLOCK_SIZE-1
		xori		v1,BLOCK_SIZE-1
		beqz		v1,copy_left_over
		 nop
		LONG_SUBU	v0,v1
		LONG_ADDU	v1,a0

1:		lw		t0,(a1)		# Can cause tlb fault
		EX(1b, fault)
2:		lw		t1,4(a1)	# Can cause tlb fault
		EX(2b, fault)
2:		lw		t2,8(a1)	# Can cause tlb fault
		EX(2b, fault)
2:		lw		t3,12(a1)	# Can cause tlb fault
		EX(2b, fault)
2:		sw		t0,(a0)		# Can cause tlb fault
		EX(2b, fault)
2:		sw		t1,4(a0)	# Can cause tlb fault
		EX(2b, fault_plus_4)
2:		sw		t2,8(a0)	# Can cause tlb fault
		EX(2b, fault_plus_8)
2:		sw		t3,12(a0)	# Can cause tlb fault
		EX(2b, fault_plus_12)
		LONG_ADDIU	a0,BLOCK_SIZE
		bne		a0,v1,1b
		 LONG_ADDIU	a1,BLOCK_SIZE
9:

/*
 * XXX Tune me ...
 */
copy_left_over:
		beqz		v0,3f
		 nop
1:		lb		$1,(a1)
		EX(1b, fault)
		LONG_ADDIU	a1,1
2:		sb		$1,(a0)
		EX(2b, fault)
		LONG_SUBU	v0,1
		bnez		v0,1b
		 LONG_ADDIU	a0,1
3:

done:		jr		ra
		 nop

		END(__copy_user)
		.set		at
		.set		reorder

/* ---------------------------------------------------------------------- */

/*
 * Access fault.  The number of not copied bytes is in v0.  We have to
 * correct the number of the not copied bytes in v0 in case of a access
 * fault in an unrolled loop, then return.
 */

fault:			jr	ra
fault_plus_4:		LONG_ADDIU	v0,4
			jr	ra
fault_plus_8:		LONG_ADDIU	v0,8
			jr	ra
fault_plus_12:		LONG_ADDIU	v0,12
			jr	ra
