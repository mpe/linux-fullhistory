/*
 * alpha/entry.S
 *
 * kernel entry-points
 */

#define __ASSEMBLY__
#include <asm/system.h>

#define halt	.long PAL_halt
#define rti	.long RAL_rti

.text
	.set noat
	.align 6
	.ent entInt
entInt:
	subq	$30,144,$30
	stq	$0,0($30)
	stq	$1,8($30)
	stq	$2,16($30)
	stq	$3,24($30)
	stq	$4,32($30)
	stq	$5,40($30)
	stq	$6,48($30)
	stq	$7,56($30)
	stq	$8,64($30)
	stq	$19,64($30)
	stq	$20,72($30)
	stq	$21,80($30)
	stq	$22,88($30)
	stq	$23,96($30)
	stq	$24,104($30)
	stq	$25,112($30)
	stq	$26,120($30)
	stq	$27,128($30)
	stq	$28,136($30)
	lda	$27,do_hw_interrupt
	jsr	$26,($27),do_hw_interrupt
	ldq	$0,0($30)
	ldq	$1,8($30)
	ldq	$2,16($30)
	ldq	$3,24($30)
	ldq	$4,32($30)
	ldq	$5,40($30)
	ldq	$6,48($30)
	ldq	$7,56($30)
	ldq	$8,64($30)
	ldq	$19,64($30)
	ldq	$20,72($30)
	ldq	$21,80($30)
	ldq	$22,88($30)
	ldq	$23,96($30)
	ldq	$24,104($30)
	ldq	$25,112($30)
	ldq	$26,120($30)
	ldq	$27,128($30)
	ldq	$28,136($30)
	addq	$30,144,$30
	rti
	.end entInt
