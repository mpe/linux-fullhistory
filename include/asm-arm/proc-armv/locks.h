/*
 * linux/include/asm-arm/proc-armv/locks.h
 *
 * Copyright (C) 2000 Russell King
 *
 * Interrupt safe locking assembler. 
 */
#ifndef __ASM_PROC_LOCKS_H
#define __ASM_PROC_LOCKS_H

#define __down_op(ptr,fail)			\
	({					\
	__asm__ __volatile__(			\
	"@ down_op\n"				\
"	mrs	r0, cpsr\n"			\
"	orr	lr, r0, #128\n"			\
"	msr	cpsr_c, lr\n"			\
"	ldr	lr, [%0]\n"			\
"	subs	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
"	msr	cpsr_c, r0\n"			\
"	movmi	r0, %0\n"			\
"	blmi	" SYMBOL_NAME_STR(fail)		\
	:					\
	: "r" (ptr), "I" (1)			\
	: "r0", "lr", "cc");			\
	})

#define __down_op_ret(ptr,fail)			\
	({					\
		unsigned int ret;		\
	__asm__ __volatile__(			\
	"@ down_op_ret\n"			\
"	mrs	r0, cpsr\n"			\
"	orr	lr, r0, #128\n"			\
"	msr	cpsr_c, lr\n"			\
"	ldr	lr, [%1]\n"			\
"	subs	lr, lr, %2\n"			\
"	str	lr, [%1]\n"			\
"	msr	cpsr_c, r0\n"			\
"	movmi	r0, %1\n"			\
"	movpl	r0, #0\n"			\
"	blmi	" SYMBOL_NAME_STR(fail) "\n"	\
"	mov	%0, r0"				\
	: "=&r" (ret)				\
	: "r" (ptr), "I" (1)			\
	: "r0", "lr", "cc");			\
	ret;					\
	})

#define __up_op(ptr,wake)			\
	({					\
	__asm__ __volatile__(			\
	"@ up_op\n"				\
"	mrs	r0, cpsr\n"			\
"	orr	lr, r0, #128\n"			\
"	msr	cpsr_c, lr\n"			\
"	ldr	lr, [%0]\n"			\
"	adds	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
"	msr	cpsr_c, r0\n"			\
"	movle	r0, %0\n"			\
"	blle	" SYMBOL_NAME_STR(wake)		\
	:					\
	: "r" (ptr), "I" (1)			\
	: "r0", "lr", "cc");			\
	})

/*
 * The value 0x01000000 supports up to 128 processors and
 * lots of processes.  BIAS must be chosen such that sub'ing
 * BIAS once per CPU will result in the long remaining
 * negative.
 */
#define RW_LOCK_BIAS	0x01000000

#define __down_op_write(ptr,fail)		\
	({					\
	__asm__ __volatile__(			\
	"@ down_op_write\n"			\
"	mrs	r0, cpsr\n"			\
"	orr	lr, r0, #128\n"			\
"	msr	cpsr_c, lr\n"			\
"	ldr	lr, [%0]\n"			\
"	subs	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
"	msr	cpsr_c, r0\n"			\
"	movne	r0, %0\n"			\
"	blne	" SYMBOL_NAME_STR(fail)		\
	:					\
	: "r" (ptr), "I" (RW_LOCK_BIAS)		\
	: "r0", "lr", "cc");			\
	})

#define __up_op_write(ptr,wake)			\
	({					\
	__asm__ __volatile__(			\
	"@ up_op_read\n"			\
"	mrs	r0, cpsr\n"			\
"	orr	lr, r0, #128\n"			\
"	msr	cpsr_c, lr\n"			\
"	ldr	lr, [%0]\n"			\
"	adds	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
"	msr	cpsr_c, r0\n"			\
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
"	mrs	r0, cpsr\n"			\
"	orr	lr, r0, #128\n"			\
"	msr	cpsr_c, lr\n"			\
"	ldr	lr, [%0]\n"			\
"	adds	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
"	msr	cpsr_c, r0\n"			\
"	moveq	r0, %0\n"			\
"	bleq	" SYMBOL_NAME_STR(wake)		\
	:					\
	: "r" (ptr), "I" (1)			\
	: "r0", "lr", "cc");			\
	})

#endif
