/*
 * alpha/entry.S
 *
 * kernel entry-points
 */

#include <asm/system.h>

#define halt	.long PAL_halt
#define rti	.long PAL_rti

/*
 * This defines the normal kernel pt-regs layout.
 *
 * regs 9-15 preserved by C code
 * regs 16-18 saved by PAL-code
 * regs 29-30 saved and set up by PAL-code
 */
#define SAVE_ALL			\
	subq	$30,160,$30;		\
	stq	$0,0($30);		\
	stq	$1,8($30);		\
	stq	$2,16($30);		\
	stq	$3,24($30);		\
	stq	$4,32($30);		\
	stq	$5,40($30);		\
	stq	$6,48($30);		\
	stq	$7,56($30);		\
	stq	$8,64($30);		\
	stq	$19,72($30);		\
	stq	$20,80($30);		\
	stq	$21,88($30);		\
	stq	$22,96($30);		\
	stq	$23,104($30);		\
	stq	$24,112($30);		\
	stq	$25,120($30);		\
	stq	$26,128($30);		\
	stq	$27,136($30);		\
	stq	$28,144($30)

#define RESTORE_ALL			\
	ldq	$0,0($30);		\
	ldq	$1,8($30);		\
	ldq	$2,16($30);		\
	ldq	$3,24($30);		\
	ldq	$4,32($30);		\
	ldq	$5,40($30);		\
	ldq	$6,48($30);		\
	ldq	$7,56($30);		\
	ldq	$8,64($30);		\
	ldq	$19,72($30);		\
	ldq	$20,80($30);		\
	ldq	$21,88($30);		\
	ldq	$22,96($30);		\
	ldq	$23,104($30);		\
	ldq	$24,112($30);		\
	ldq	$25,120($30);		\
	ldq	$26,128($30);		\
	ldq	$27,136($30);		\
	ldq	$28,144($30);		\
	addq	$30,160,$30

.text
.set noat

.align 5
.globl	entInt
.ent	entInt
entInt:
	SAVE_ALL
/* start atomic operation with respect to software interrupts */
	lda	$0,intr_count
	ldq	$1,0($0)
	addq	$1,1,$1
	stq	$1,0($0)
/* set up the arguments to the C interrupt handler */
	bis	$30,$30,$19
	lda	$27,do_entInt
	jsr	$26,($27),do_entInt
/* ok, check if we need to do software interrupts */
1:	lda	$0,intr_count
	ldq	$1,0($0)
	subq	$1,1,$1
	bne	$1,2f		/* interrupt within interrupt: return now */
	lda	$2,bh_active
	ldq	$3,0($2)
	lda	$2,bh_mask
	ldq	$2,0($2)
	and	$2,$3,$2
	bne	$2,3f
2:	stq	$1,0($0)
	RESTORE_ALL
	rti
.align 5
3:	lda	$27,do_bottom_half
	jsr	$26,($27),do_bottom_half
	br	$31,1b
.end entInt

.align 5
.globl	entMM
.ent	entMM
entMM:
	SAVE_ALL
	bis	$30,$30,$19
	lda	$27,do_page_fault
	jsr	$26,($27),do_page_fault
	RESTORE_ALL
	rti
.end entMM

.align 5
.globl	entArith
.ent	entArith
entArith:
	SAVE_ALL
	bis	$30,$30,$19
	lda	$27,do_entArith
	jsr	$26,($27),do_entArith
	RESTORE_ALL
	rti
.end entArith

.align 5
.globl	entIF
.ent	entIF
entIF:
	SAVE_ALL
	bis	$30,$30,$19
	lda	$27,do_entIF
	jsr	$26,($27),do_entIF
	RESTORE_ALL
	rti
.end entIF

.align 5
.globl	entUna
.ent	entUna
entUna:
	SAVE_ALL
	bis	$30,$30,$19
	lda	$27,do_entUna
	jsr	$26,($27),do_entUna
	RESTORE_ALL
	rti
.end entUna

.align 5
.globl	entSys
.ent	entSys
entSys:
	SAVE_ALL
	bis	$30,$30,$19
	lda	$27,do_entSys
	jsr	$26,($27),do_entSys
	RESTORE_ALL
	rti
.end entSys

	.align 5
	.globl sys_call_table
sys_call_table:
	.quad 0
