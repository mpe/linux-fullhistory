! Taken from newlib-1.8.0

!
! Fast SH memset
!
! by Toshiyasu Morita (tm@netcom.com)
!
! Entry: r4: destination pointer
!        r5: fill value
!        r6: byte count
!
! Exit:  r0-r3: trashed
!
#include <linux/linkage.h>

ENTRY(memset)
	mov	r4,r3	! Save return value
	
	mov	r6,r0	! Check explicitly for zero
	cmp/eq	#0,r0
	bt	L_exit

	mov	#12,r0	! Check for small number of bytes
	cmp/gt	r6,r0
	bt	L_store_byte_loop

	neg	r4,r0	! Align destination
	add	#4,r0
	and	#3,r0
	tst	r0,r0
	bt	L_dup_bytes
	.balignw 4,0x0009
L_align_loop:
	mov.b	r5,@r4
	add	#-1,r6
	add	#1,r4
	dt	r0
	bf	L_align_loop

L_dup_bytes:	
	extu.b	r5,r5	! Duplicate bytes across longword
	swap.b	r5,r0
	or	r0,r5
	swap.w	r5,r0
	or	r0,r5

	mov	r6,r2	! Calculate number of double longwords
	shlr2	r2
	shlr	r2

	.balignw 4,0x0009
L_store_long_loop:
	mov.l	r5,@r4	! Store double longs to memory
	dt	r2
	mov.l	r5,@(4,r4)
	add	#8,r4
	bf	L_store_long_loop

	mov	#7,r0
	and	r0,r6
	tst	r6,r6
	bt	L_exit
	.balignw 4,0x0009
L_store_byte_loop:
	mov.b	r5,@r4	! Store bytes to memory
	add	#1,r4
	dt	r6
	bf	L_store_byte_loop

L_exit:
	rts
	 mov	r3,r0
