/* $Id: rwlock.S,v 1.1 1999/07/03 22:11:06 davem Exp $
 * rwlocks.S: These things are too big to do inline.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

	.text
	.align	64

	/* The non-contention read lock usage is 2 cache lines. */

	.globl	__read_lock, __read_unlock
	/* g1=lock, g3=retpc, g5/g7=scratch */
__read_lock:
	ldsw		[%g1], %g5
	brlz,pn		%g5, __read_wait_for_writer
4:	 add		%g5, 1, %g7
	cas		[%g1], %g5, %g7
	cmp		%g5, %g7
	bne,pn		%icc, __read_lock
	 membar		#StoreLoad | #StoreStore
99:	jmpl		%g3 + 8, %g0
	 nop
__read_unlock:
	lduw		[%g1], %g5
	sub		%g5, 1, %g7
	cas		[%g1], %g5, %g7
	cmp		%g5, %g7
	be,pt		%xcc, 99b
	 membar		#StoreLoad | #StoreStore
	b,a,pt		%xcc, __read_unlock

__read_wait_for_writer:
	ldsw		[%g1], %g5
	brlz,pt		%g5, __read_wait_for_writer
	 membar		#LoadLoad
	b,a,pt		%xcc, 4b
__write_wait_for_writer:
	ldsw		[%g1], %g5
	brlz,pt		%g5, __write_wait_for_writer
	 membar		#LoadLoad
	b,a,pt		%xcc, 4f

	/* Similarly, 2 cache lines for non-contention write locks. */

	.align		64
	.globl		__write_unlock
	/* g1=lock, g3=retpc, g2/g5/g7=scratch */
__write_unlock:
	sethi		%hi(0x80000000), %g2
1:	lduw		[%g1], %g5
	andn		%g5, %g2, %g7
	cas		[%g1], %g5, %g7
	cmp		%g5, %g7
	be,pt		%icc, 99b
	 membar		#StoreLoad | #StoreStore
	b,a,pt		%xcc, 1b

	.globl		__write_lock
__write_lock:
	sethi		%hi(0x80000000), %g2
1:	ldsw		[%g1], %g5
4:	brnz,pn		%g5, 5f
	 or		%g5, %g2, %g7
	cas		[%g1], %g5, %g7
	cmp		%g5, %g7
	be,pt		%icc, 99b
	 membar		#StoreLoad | #StoreStore

	b,a,pt		%xcc, 1b
5:	brlz		%g5, __write_wait_for_writer
	or		%g5, %g2, %g7
	cas		[%g1], %g5, %g7
	cmp		%g5, %g7
	bne,pn		%icc, 5b
8:	 ldsw		[%g1], %g5
	cmp		%g5, %g2
	be,pn		%icc, 99b
	 membar		#LoadLoad
	b,a,pt		%xcc, 99b

