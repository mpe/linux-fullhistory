/*
 * linux/include/asm-arm/semaphore.h
 */
#ifndef __ASM_PROC_SEMAPHORE_H
#define __ASM_PROC_SEMAPHORE_H

/*
 * This is ugly, but we want the default case to fall through.
 * "__down" is the actual routine that waits...
 */
extern inline void down(struct semaphore * sem)
{
	unsigned int cpsr, temp;

	__asm__ __volatile__ ("
	@ atomic down operation
	mrs	%0, cpsr
	orr	%1, %0, #128		@ disable IRQs
	bic	%0, %0, #0x80000000	@ clear N
	msr	cpsr, %1
	ldr	%1, [%2]
	subs	%1, %1, #1
	orrmi	%0, %0, #0x80000000	@ set N
	str	%1, [%2]
	msr	cpsr, %0
	movmi	r0, %2
	blmi	" SYMBOL_NAME_STR(__down_failed)
		: "=&r" (cpsr), "=&r" (temp)
		: "r" (sem)
		: "r0", "lr", "cc");
}

/*
 * This is ugly, but we want the default case to fall through.
 * "__down_interruptible" is the actual routine that waits...
 */
extern inline int down_interruptible (struct semaphore * sem)
{
	unsigned int cpsr, temp;

	__asm__ __volatile__ ("
	@ atomic down interruptible operation
	mrs	%0, cpsr
	orr	%1, %0, #128		@ disable IRQs
	bic	%0, %0, #0x80000000	@ clear N
	msr	cpsr, %1
	ldr	%1, [%2]
	subs	%1, %1, #1
	orrmi	%0, %0, #0x80000000	@ set N
	str	%1, [%2]
	msr	cpsr, %0
	movmi	r0, %2
	movpl	r0, #0
	blmi	" SYMBOL_NAME_STR(__down_interruptible_failed) "
	mov	%1, r0"
		: "=&r" (cpsr), "=&r" (temp)
		: "r" (sem)
		: "r0", "lr", "cc");

	return temp;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
extern inline void up(struct semaphore * sem)
{
	unsigned int cpsr, temp;

	__asm__ __volatile__ ("
	@ atomic up operation
	mrs	%0, cpsr
	orr	%1, %0, #128		@ disable IRQs
	bic	%0, %0, #0x80000000	@ clear N
	msr	cpsr, %1
	ldr	%1, [%2]
	adds	%1, %1, #1
	orrls	%0, %0, #0x80000000	@ set N
	str	%1, [%2]
	msr	cpsr, %0
	movmi	r0, %2
	blmi	" SYMBOL_NAME_STR(__up_wakeup)
		: "=&r" (cpsr), "=&r" (temp)
		: "r" (sem)
		: "r0", "lr", "cc");
}

#endif
