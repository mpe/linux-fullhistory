! Fast memmove/memcpy/bcopy
! Copyright Australian National University, 1995
! This file may be used under the terms of the GNU Public License
! Author: Paul Mackerras, September 95
! Minor beautifications David S. Miller

#include <asm/cprefix.h>

	.globl	C_LABEL(bcopy)
C_LABEL(bcopy):
	mov	%o0,%o3
	mov	%o1,%o0
	mov	%o3,%o1

	.globl	C_LABEL(amemmove)
C_LABEL(amemmove):
	.globl	C_LABEL(memmove)
	.globl	C_LABEL(memcpy)
C_LABEL(memmove):
C_LABEL(memcpy):
	save	%sp,-96,%sp
	mov	%i0,%l7

	cmp	%i0,%i1		! check for dest within source area
	bleu,a	1f
	andcc	%i0,3,%l1
	add	%i1,%i2,%l0
	cmp	%i0,%l0
	blu,a	Lback
	mov	%l0,%i1

	! copying forwards
	! first get dest to be word-aligned
	andcc	%i0,3,%l1
1:
	be,a	Lwalign		! if dest already word-aligned
	cmp	%i2,4
	mov	4,%l2
	sub	%l2,%l1,%l2	! #bytes until word-aligned
	subcc	%i2,%l2,%i2
	ble,a	Lend		! not copying enough to get past word bdry
	addcc	%i2,%l2,%i2

1:
	ldub	[%i1],%o0	! copy single bytes until word-aligned
	add	%i1,1,%i1
	subcc	%l2,1,%l2
	stb	%o0,[%i0]
	bgt	1b
	add	%i0,1,%i0
	cmp	%i2,4

Lwalign:			! dest now word aligned
	blt,a	Lend
	orcc	%i2,%g0,%g0

	andcc	%i1,3,%l0
	be,a	Ldoword		! if dest word aligned wrt src
	andcc	%i0,4,%g0

	! yucky cases where we have to shift

	mov	4,%l2
	sub	%l2,%l0,%l2	! address adjustment, used at Lendn
	sll	%l0,3,%l0	! bit offset = shift left count
	sll	%l2,3,%l1	! shift right count
	add	%i1,%l2,%i1	! round up to next word
	ld	[%i1-4],%o0	! get first word

	andcc	%i0,4,%g0	! get destination double-word aligned
	be,a	1f
	andcc	%i1,4,%g0
	ld	[%i1],%o1	! by constructing and storing one word
	add	%i0,4,%i0
	add	%i1,4,%i1
	sub	%i2,4,%i2
	sll	%o0,%l0,%o0
	srl	%o1,%l1,%l6
	or	%o0,%l6,%o0
	st	%o0,[%i0-4]
	mov	%o1,%o0

	andcc	%i1,4,%g0	! now construct & store pairs of double-words
1:
	bne,a	3f		! if source now not double-word aligned
	subcc	%i2,4,%i2
	subcc	%i2,16,%i2
	blt	2f
	mov	%o0,%o1
4:
	ldd	[%i1],%o2
	sll	%o1,%l0,%o4
	ldd	[%i1+8],%o0
	add	%i0,16,%i0
	add	%i1,16,%i1
	subcc	%i2,16,%i2
	srl	%o2,%l1,%l6
	or	%l6,%o4,%o4
	sll	%o2,%l0,%o5
	srl	%o3,%l1,%l6
	or	%l6,%o5,%o5
	std	%o4,[%i0-16]
	sll	%o3,%l0,%o4
	srl	%o0,%l1,%l6
	or	%l6,%o4,%o4
	sll	%o0,%l0,%o5
	srl	%o1,%l1,%l6
	or	%l6,%o5,%o5
	bge	4b
	std	%o4,[%i0-8]
2:
	addcc	%i2,12,%i2
	blt,a	Lendn
	addcc	%i2,4,%i2
5:
	ld	[%i1],%o2
	add	%i0,4,%i0
	add	%i1,4,%i1
	subcc	%i2,4,%i2
	sll	%o1,%l0,%o0
	srl	%o2,%l1,%o1
	or	%o1,%o0,%o0
	st	%o0,[%i0-4]
	bge	5b
	mov	%o2,%o1
	ba	Lendn
	addcc	%i2,4,%i2

3:
	blt,a	Lendn
	addcc	%i2,4,%i2
	ld	[%i1],%o1
	add	%i1,4,%i1
	subcc	%i2,16,%i2
	blt,a	8f
	addcc	%i2,16,%i2
7:
	ldd	[%i1],%o2
	sll	%o0,%l0,%o4
	srl	%o1,%l1,%l6
	or	%l6,%o4,%o4
	sll	%o1,%l0,%o5
	ldd	[%i1+8],%o0
	add	%i0,16,%i0
	add	%i1,16,%i1
	subcc	%i2,16,%i2
	srl	%o2,%l1,%l6
	or	%l6,%o5,%o5
	std	%o4,[%i0-16]
	sll	%o2,%l0,%o4
	srl	%o3,%l1,%l6
	or	%l6,%o4,%o4
	sll	%o3,%l0,%o5
	srl	%o0,%l1,%l6
	or	%l6,%o5,%o5
	bge	7b
	std	%o4,[%i0-8]
	addcc	%i2,16,%i2
8:
	sll	%o0,%l0,%o4
	srl	%o1,%l1,%l6
	or	%l6,%o4,%o4
	st	%o4,[%i0]
	add	%i0,4,%i0
	subcc	%i2,4,%i2
	blt,a	Lendn
	addcc	%i2,4,%i2
	mov	%o1,%o0
	ld	[%i1],%o1
	ba	8b
	add	%i1,4,%i1


Ldoword:
	! here both dest and src are word-aligned
	! make dest double-word aligned
	be,a	1f
	andcc	%i1,4,%g0
	ld	[%i1],%o0
	add	%i0,4,%i0
	add	%i1,4,%i1
	sub	%i2,4,%i2
	st	%o0,[%i0-4]
	cmp	%i2,4
	blt,a	Lend
	orcc	%i2,%g0,%g0
	andcc	%i1,4,%g0

1:
	be,a	Ldodble		! if source double-word aligned now
	subcc	%i2,32,%i2
	ld	[%i1],%o5
	add	%i1,4,%i1
	subcc	%i2,36,%i2
	blt,a	3f
	add	%i2,32,%i2
2:
	ldd	[%i1],%o2
	add	%i1,32,%i1
	subcc	%i2,32,%i2
	mov	%o5,%o0
	ldd	[%i1-24],%o4
	mov	%o2,%o1
	std	%o0,[%i0]
	mov	%o3,%o2
	ldd	[%i1-16],%o0
	mov	%o4,%o3
	std	%o2,[%i0+8]
	mov	%o5,%o2
	ldd	[%i1-8],%o4
	mov	%o0,%o3
	std	%o2,[%i0+16]
	mov	%o1,%o0
	mov	%o4,%o1
	std	%o0,[%i0+24]
	bge	2b
	add	%i0,32,%i0
	add	%i2,32,%i2
3:
	st	%o5,[%i0]
	add	%i0,4,%i0
	subcc	%i2,4,%i2
	blt,a	Lend
	addcc	%i2,4,%i2
	ld	[%i1],%o5
	ba	3b
	add	%i1,4,%i1

Ldodble:
	! dest and source are both double-word aligned
	blt,a	2f
	addcc	%i2,28,%i2
1:
	ldd	[%i1],%o0	! copy sets of 4 double-words
	subcc	%i2,32,%i2
	ldd	[%i1+8],%o2
	add	%i1,32,%i1
	ldd	[%i1-16],%o4
	add	%i0,32,%i0
	std	%o0,[%i0-32]
	ldd	[%i1-8],%o0
	std	%o2,[%i0-24]
	std	%o4,[%i0-16]
	bge	1b
	std	%o0,[%i0-8]
	addcc	%i2,28,%i2
2:
	blt,a	Lend
	addcc	%i2,4,%i2
3:
	ld	[%i1],%o0	! copy words
	add	%i1,4,%i1
	add	%i0,4,%i0
	subcc	%i2,4,%i2
	bge	3b
	st	%o0,[%i0-4]
	ba	Lend
	addcc	%i2,4,%i2

Lendn:
	sub	%i1,%l2,%i1
Lend:
	ble	Lout
	nop
1:
	ldub	[%i1],%o0
	add	%i1,1,%i1
	subcc	%i2,1,%i2
	stb	%o0,[%i0]
	bgt	1b
	add	%i0,1,%i0

	ba	Lout
	nop

Lback:	! Here we have to copy backwards
	add	%i0,%i2,%i0
	! first get dest to be word-aligned
	andcc	%i0,3,%l2	! #bytes until word-aligned
	be,a	Lbwal		! if dest already word-aligned
	cmp	%i2,4
	subcc	%i2,%l2,%i2
	ble,a	Lbend		! not copying enough to get past word bdry
	addcc	%i2,%l2,%i2

1:
	ldub	[%i1-1],%o0	! copy single bytes until word-aligned
	sub	%i1,1,%i1
	subcc	%l2,1,%l2
	stb	%o0,[%i0-1]
	bgt	1b
	sub	%i0,1,%i0
	cmp	%i2,4

Lbwal:				! dest now word aligned
	blt,a	Lbend
	orcc	%i2,%g0,%g0

	andcc	%i1,3,%l2
	be,a	Lbword		! if dest word aligned wrt src
	andcc	%i0,4,%g0

	! yucky cases where we have to shift
	! note %l2 used below at Lbendn

	mov	4,%l0
	sub	%l0,%l2,%l0	! # bytes to right of src in word
	sll	%l0,3,%l0	! bit offset = shift right count
	sll	%l2,3,%l1	! shift left count
	sub	%i1,%l2,%i1	! round down to word boundary
	ld	[%i1],%o1	! get first word

	andcc	%i0,4,%g0	! get destination double-word aligned
	be,a	1f
	andcc	%i1,4,%g0
	ld	[%i1-4],%o0	! by constructing and storing one word
	sub	%i0,4,%i0
	sub	%i1,4,%i1
	sub	%i2,4,%i2
	srl	%o1,%l0,%o1
	sll	%o0,%l1,%l6
	or	%o1,%l6,%o1
	st	%o1,[%i0]
	mov	%o0,%o1

	andcc	%i1,4,%g0	! now construct & store pairs of double-words
1:
	bne,a	3f		! if source now not double-word aligned
	subcc	%i2,4,%i2
	subcc	%i2,16,%i2
	blt	2f
	mov	%o1,%o0
4:
	ldd	[%i1-8],%o2
	srl	%o0,%l0,%o5
	ldd	[%i1-16],%o0
	sub	%i0,16,%i0
	sub	%i1,16,%i1
	subcc	%i2,16,%i2
	sll	%o3,%l1,%l6
	or	%l6,%o5,%o5
	srl	%o3,%l0,%o4
	sll	%o2,%l1,%l6
	or	%l6,%o4,%o4
	std	%o4,[%i0+8]
	srl	%o2,%l0,%o5
	sll	%o1,%l1,%l6
	or	%l6,%o5,%o5
	srl	%o1,%l0,%o4
	sll	%o0,%l1,%l6
	or	%l6,%o4,%o4
	bge	4b
	std	%o4,[%i0]
2:
	addcc	%i2,12,%i2
	blt,a	Lbendn
	addcc	%i2,4,%i2
5:
	ld	[%i1-4],%o2
	sub	%i0,4,%i0
	sub	%i1,4,%i1
	subcc	%i2,4,%i2
	srl	%o0,%l0,%o0
	sll	%o2,%l1,%o1
	or	%o1,%o0,%o0
	st	%o0,[%i0]
	bge	5b
	mov	%o2,%o0
	ba	Lbendn
	addcc	%i2,4,%i2

3:
	blt,a	Lbendn
	addcc	%i2,4,%i2
	ld	[%i1-4],%o0
	sub	%i1,4,%i1
	subcc	%i2,16,%i2
	blt,a	8f
	addcc	%i2,16,%i2
7:
	ldd	[%i1-8],%o2
	srl	%o1,%l0,%o5
	sll	%o0,%l1,%l6
	or	%l6,%o5,%o5
	srl	%o0,%l0,%o4
	ldd	[%i1-16],%o0
	sub	%i0,16,%i0
	sub	%i1,16,%i1
	subcc	%i2,16,%i2
	sll	%o3,%l1,%l6
	or	%l6,%o4,%o4
	std	%o4,[%i0+8]
	srl	%o3,%l0,%o5
	sll	%o2,%l1,%l6
	or	%l6,%o5,%o5
	srl	%o2,%l0,%o4
	sll	%o1,%l1,%l6
	or	%l6,%o4,%o4
	bge	7b
	std	%o4,[%i0]
	addcc	%i2,16,%i2
8:
	srl	%o1,%l0,%o5
	sll	%o0,%l1,%l6
	or	%l6,%o5,%o5
	st	%o5,[%i0-4]
	sub	%i0,4,%i0
	subcc	%i2,4,%i2
	blt,a	Lbendn
	addcc	%i2,4,%i2
	mov	%o0,%o1
	ld	[%i1-4],%o0
	ba	8b
	sub	%i1,4,%i1


Lbword:
	! here both dest and src are word-aligned
	! make dest double-word aligned
	be,a	1f
	andcc	%i1,4,%g0
	ld	[%i1-4],%o0
	sub	%i0,4,%i0
	sub	%i1,4,%i1
	sub	%i2,4,%i2
	st	%o0,[%i0]
	cmp	%i2,4
	blt,a	Lbend
	orcc	%i2,%g0,%g0
	andcc	%i1,4,%g0

1:
	be,a	Lbdble		! if source double-word aligned now
	subcc	%i2,32,%i2
	ld	[%i1-4],%o4
	sub	%i1,4,%i1
	subcc	%i2,36,%i2
	blt,a	3f
	add	%i2,32,%i2
2:
	ldd	[%i1-8],%o2
	sub	%i1,32,%i1
	subcc	%i2,32,%i2
	mov	%o4,%o1
	ldd	[%i1+16],%o4
	mov	%o3,%o0
	std	%o0,[%i0-8]
	mov	%o2,%o3
	ldd	[%i1+8],%o0
	mov	%o5,%o2
	std	%o2,[%i0-16]
	mov	%o4,%o3
	ldd	[%i1],%o4
	mov	%o1,%o2
	std	%o2,[%i0-24]
	mov	%o0,%o1
	mov	%o5,%o0
	std	%o0,[%i0-32]
	bge	2b
	sub	%i0,32,%i0
	add	%i2,32,%i2
3:
	st	%o4,[%i0-4]
	sub	%i0,4,%i0
	subcc	%i2,4,%i2
	blt,a	Lbend
	addcc	%i2,4,%i2
	ld	[%i1-4],%o4
	ba	3b
	sub	%i1,4,%i1

Lbdble:
	! dest and source are both double-word aligned
	blt,a	2f
	addcc	%i2,28,%i2
1:
	ldd	[%i1-8],%o0	! copy sets of 4 double-words
	subcc	%i2,32,%i2
	ldd	[%i1-16],%o2
	sub	%i1,32,%i1
	ldd	[%i1+8],%o4
	sub	%i0,32,%i0
	std	%o0,[%i0+24]
	ldd	[%i1],%o0
	std	%o2,[%i0+16]
	std	%o4,[%i0+8]
	bge	1b
	std	%o0,[%i0]
	addcc	%i2,28,%i2
2:
	blt,a	Lbend
	addcc	%i2,4,%i2
3:
	ld	[%i1-4],%o0	! copy words
	sub	%i1,4,%i1
	sub	%i0,4,%i0
	subcc	%i2,4,%i2
	bge	3b
	st	%o0,[%i0]
	ba	Lbend
	addcc	%i2,4,%i2

Lbendn:
	add	%i1,%l2,%i1
Lbend:
	ble	Lout
	nop
1:
	ldub	[%i1-1],%o0
	sub	%i1,1,%i1
	subcc	%i2,1,%i2
	stb	%o0,[%i0-1]
	bgt	1b
	sub	%i0,1,%i0

Lout:
	ret
	restore	%l7,0,%o0


