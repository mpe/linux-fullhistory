/*
 * linux/include/asm-arm/proc-armo/locks.h
 *
 * Copyright (C) 2000 Russell King
 * Fixes for 26 bit machines, (C) 2000 Dave Gilbert
 *
 * Interrupt safe locking assembler. 
 *
 */
#ifndef __ASM_PROC_LOCKS_H
#define __ASM_PROC_LOCKS_H

/* Decrements by 1, fails if value < 0 */
#define __down_op(ptr,fail)			\
	({					\
	__asm__ __volatile__ (			\
	"@ atomic down operation\n"		\
"	mov	r0, pc\n"			\
"	orr	lr, r0, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
"	ldr	lr, [%0]\n"			\
"	and	r0, r0, #0x0c000003\n"		\
"	subs	lr, lr, #1\n"			\
"	str	lr, [%0]\n"			\
"	orrmi	r0, r0, #0x80000000	@ set N\n" \
"	teqp	r0, #0\n"			\
"	movmi	r0, %0\n"			\
"	blmi	" SYMBOL_NAME_STR(fail)		\
	:					\
	: "r" (ptr)				\
	: "r0", "lr", "cc");			\
	})

#define __down_op_ret(ptr,fail)			\
	({					\
		unsigned int result;		\
	__asm__ __volatile__ (			\
"	@ down_op_ret\n"			\
"	mov	r0, pc\n"			\
"	orr	lr, r0, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
"	ldr	lr, [%1]\n"			\
"	and	r0, r0, #0x0c000003\n"		\
"	subs	lr, lr, #1\n"			\
"	str	lr, [%1]\n"			\
"	orrmi	r0, r0, #0x80000000	@ set N\n" \
"	teqp	r0, #0\n"			\
"	movmi	r0, %1\n"			\
"	movpl	r0, #0\n"			\
"	blmi	" SYMBOL_NAME_STR(fail) "\n"	\
"	mov	%0, r0"				\
	: "=&r" (result)			\
	: "r" (ptr)				\
	: "r0", "lr", "cc");			\
	result;					\
	})

#define __up_op(ptr,wake)			\
	({					\
	__asm__ __volatile__ (			\
	"@ up_op\n"				\
"	mov	r0, pc\n"			\
"	orr	lr, r0, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
"	ldr	lr, [%0]\n"			\
"	and	r0, r0, #0x0c000003\n"		\
"	adds	lr, lr, #1\n"			\
"	str	lr, [%0]\n"			\
"	orrle	r0, r0, #0x80000000	@ set N - should this be mi ??? DAG ! \n" \
"	teqp	r0, #0\n"			\
"	movmi	r0, %0\n"			\
"	blmi	" SYMBOL_NAME_STR(wake)		\
	:					\
	: "r" (ptr)				\
	: "r0", "lr", "cc");			\
	})

/*
 * The value 0x01000000 supports up to 128 processors and
 * lots of processes.  BIAS must be chosen such that sub'ing
 * BIAS once per CPU will result in the long remaining
 * negative.
 */
#define RW_LOCK_BIAS      0x01000000
#define RW_LOCK_BIAS_STR "0x01000000"

/* Decrements by RW_LOCK_BIAS rather than 1, fails if value != 0 */
#define __down_op_write(ptr,fail)		\
	({					\
	__asm__ __volatile__(			\
	"@ down_op_write\n"			\
"	mov	r0, pc\n"			\
"	orr	lr, r0, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
"	and	r0, r0, #0x0c000003\n"		\
\
"	ldr	lr, [%0]\n"			\
"	subs	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
\
" orreq r0, r0, #0x40000000 @ set Z \n"\
"	teqp	r0, #0\n"			\
"	movne	r0, %0\n"			\
"	blne	" SYMBOL_NAME_STR(fail)		\
	:					\
	: "r" (ptr), "I" (RW_LOCK_BIAS)		\
	: "r0", "lr", "cc");			\
	})

/* Increments by RW_LOCK_BIAS, wakes if value >= 0 */
#define __up_op_write(ptr,wake)			\
	({					\
	__asm__ __volatile__(			\
	"@ up_op_read\n"			\
"	mov	r0, pc\n"			\
"	orr	lr, r0, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
\
"	ldr	lr, [%0]\n"			\
"	and	r0, r0, #0x0c000003\n"		\
"	adds	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
\
" orrcs r0, r0, #0x20000000 @ set C\n" \
"	teqp	r0, #0\n"			\
"	movcs	r0, %0\n"			\
"	blcs	" SYMBOL_NAME_STR(wake)		\
	:					\
	: "r" (ptr), "I" (RW_LOCK_BIAS)		\
	: "r0", "lr", "cc");			\
	})

#define __down_op_read(ptr,fail)		\
	__down_op(ptr, fail)

#define __up_op_read(ptr,wake)			\
	({					\
	__asm__ __volatile__(			\
	"@ up_op_read\n"			\
"	mov	r0, pc\n"			\
"	orr	lr, r0, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
\
"	ldr	lr, [%0]\n"			\
"	and	r0, r0, #0x0c000003\n"		\
"	adds	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
\
" orreq r0, r0, #0x40000000 @ Set Z \n" \
"	teqp	r0, #0\n"			\
"	moveq	r0, %0\n"			\
"	bleq	" SYMBOL_NAME_STR(wake)		\
	:					\
	: "r" (ptr), "I" (1)			\
	: "r0", "lr", "cc");			\
	})

#endif
