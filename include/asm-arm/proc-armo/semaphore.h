/*
 * linux/include/asm-arm/proc-armo/locks.h
 *
 * Copyright (C) 2000 Russell King
 *
 * Interrupt safe locking assembler.
 */
#ifndef __ASM_PROC_LOCKS_H
#define __ASM_PROC_LOCKS_H

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
	blmi	" SYMBOL_NAME_STR(fail)		\
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
"	ldr	lr, [%1]\m"			\
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
	mov	r0, pc\n"			\
	orr	lr, r0, #0x08000000\n"		\
	teqp	lr, #0\n"			\
	ldr	lr, [%0]\n"			\
	and	r0, r0, #0x0c000003\n"		\
	adds	lr, lr, #1\n"			\
	str	lr, [%0]\n"			\
	orrle	r0, r0, #0x80000000	@ set N\n" \
	teqp	r0, #0\n"			\
	movmi	r0, %0\n"			\
	blmi	" SYMBOL_NAME_STR(wake)		\
	:					\
	: "r" (ptr)				\
	: "r0", "lr", "cc");			\
	})

#endif
