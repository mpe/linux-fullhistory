/*
 * alpha/head.S
 *
 * initial boot stuff..
 */

#include <asm/system.h>

#undef halt
#define halt .long PAL_halt

/*
 * NOTE! The console bootstrap will load us at 0x20000000, but this image
 * is linked to run at START_ADDR, so the first thing we do is to move
 * ourself up to the right address.. We'd better be position-independent
 * at that stage :-)
 */
	.set noreorder
	.globl	__start
	.ent	__start
__start:
	bis	$31,$31,$31
	br	$1,$200
	.long START_ADDR, START_ADDR >> 32	/* strange bug in the assembler.. duh */
	.long START_SIZE, START_SIZE >> 32
$200:	ldq	$30,0($1)		/* new stack - below this */
	lda	$2,-8($1)		/* __start */
	bis	$30,$30,$3		/* new address */
	subq	$3,$2,$6		/* difference */
	ldq	$4,8($1)		/* size */
$201:	subq	$4,8,$4
	ldq	$5,0($2)
	addq	$2,8,$2
	stq	$5,0($3)
	addq	$3,8,$3
	bne	$4,$201
	br	$1,$202
$202:	addq	$1,$6,$1
	addq	$1,12,$1		/* $203 in the new address space */
	jmp	$31,($1),$203
$203:	br	$27,$100
$100:	ldgp	$29,0($27)
	lda	$27,start_kernel
	jsr	$26,($27),start_kernel
	halt
	.end __start

	.align 5
	.globl	wrent
	.ent	wrent
wrent:
	.long PAL_wrent
	ret ($26)
	.end wrent

	.align 5
	.globl	wrkgp
	.ent	wrkgp
wrkgp:
	.long PAL_wrkgp
	ret ($26)
	.end wrkgp

	.align 5
	.globl	switch_to_osf_pal
	.ent	switch_to_osf_pal
switch_to_osf_pal:
	subq	$30,128,$30
	stq	$26,0($30)
	stq	$1,8($30)
	stq	$2,16($30)
	stq	$3,24($30)
	stq	$4,32($30)
	stq	$5,40($30)
	stq	$6,48($30)
	stq	$7,56($30)
	stq	$8,64($30)
	stq	$9,72($30)
	stq	$10,80($30)
	stq	$11,88($30)
	stq	$12,96($30)
	stq	$13,104($30)
	stq	$14,112($30)
	stq	$15,120($30)

	stq	$30,0($17)	/* save KSP in PCB */

	bis	$30,$30,$20	/* a4 = KSP */
	br	$17,__do_swppal

	ldq	$26,0($30)
	ldq	$1,8($30)
	ldq	$2,16($30)
	ldq	$3,24($30)
	ldq	$4,32($30)
	ldq	$5,40($30)
	ldq	$6,48($30)
	ldq	$7,56($30)
	ldq	$8,64($30)
	ldq	$9,72($30)
	ldq	$10,80($30)
	ldq	$11,88($30)
	ldq	$12,96($30)
	ldq	$13,104($30)
	ldq	$14,112($30)
	ldq	$15,120($30)
	addq	$30,128,$30
	ret ($26)

__do_swppal:
	.long	PAL_swppal
	.end	switch_to_osf_pal
