/*  string.h:  Efficient string functions in sparc-assembly for
               the linux kernel.

    Copyright 1994 (c) David S. Miller (davem@caip.rutgers.edu)
*/


/* If we are smart we will use only the output and global registers 
   as that will allow us to avoid a window save which would be nice.
*/

/* Believe it or not the following strlen is not optimized enough!
   In the future I may play games with doing word reads and reducing
   the per-word comparisons to *one*, yes I have seen it done.
*/
	.align 4
	.globl _strlen
_strlen:
	mov	%o0, %g3	! leaf-procedure optimization, here
	ldsb	[%g3], %g2	! I only use the register sent to me
	cmp	%g2, 0		! and the globals. Now, this routine
	be	1f		! is callable from boot code.
	nop
	add	%o0, 1, %o0
0:	ldsb	[%o0], %g2
	cmp	%g2, 0
	bne,a	0b		! annulling branch, yuck
	add	%o0, 1, %o0

1:	retl
	sub	%o0, %g3, %o0	! since %g3 holds the original pointer
				! and %o0 is at the end byte, we can
				! subtract and the result is strlen.

/* String concatenate function. I am too lazy to honor the third count
   argument at this time. Once again, this could be optimized so much
   more to use word accesses instead of slooow byte loads.
*/
	.align 4
	.globl _strcat
_strcat:
	mov	%o0, %g4
	ldsb	[%g4], %g3
	cmp	%g3, 0
	be,a	2f
	ldub	[%o1], %g3
	add	%o0, 1, %o0
	
0:	ldsb	[%o0], %g3
	cmp	%g3, 0
	bne,a	0b
	add	%o0, 1, %o0

1:	ldub	[%o1], %g3

2:	add	%o1, 1, %o1
	stb	%g3, [%o0]
	cmp	%g3, 0
	bne	1b
	add	%o0, 1, %o0
	retl
	mov	%g4, %o0

/* Aieee, this code is starting to give me a headache. I shouldn't
   have tried to do this in one sitting :-(
*/

	.align 4
	.globl _strcmp
_strcmp:	b	2f
		ldsb	[%o1], %g4

0:		sll	%o2, 24, %g3
		cmp	%g3, 0
		bne	1f
		add	%o0, 1, %o0
		b	3f
		or	%g0, %g0, %o0

1:		ldsb	[%o1], %g4

2:		ldsb	[%o0], %g3
		add	%o1, 1, %o1
		cmp	%g3, %g4
		be	0b
		mov	%g3, %o2
		ldub	[%o2], %g3
		ldub	[%o1-1], %o0		! oh man, no joke
		sub	%g2, %o0, %o0

3:		retl
		nop

/* Ok, strcpy() should be easy enough. Maybe I catch some sleep after 
   this one....
*/
	.align 4
	.globl _strcpy
_strcpy:	ldub	[%o1], %g3
		mov	%o0, %g4
		cmp	%g3, 0
		be	1f
		stb	%g3, [%g4]

0:		add	%o1, 1, %o1
		ldub	[%o1], %g3
		add	%o0, 1, %o0
		cmp	%g3, 0
		bne	0b
		stb	%g3, [%o0]

1:		retl
		mov	%g4, %o0

	
