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
	__asm__ __volatile__ ("
	@ atomic down operation
	mrs	r0, cpsr
	orr	r1, r0, #128		@ disable IRQs
	bic	r0, r0, #0x80000000	@ clear N
	msr	cpsr, r1
	ldr	r1, [%0]
	subs	r1, r1, #1
	str	r1, [%0]
	orrmi	r0, r0, #0x80000000	@ set N
	msr	cpsr, r0
	movmi	r0, %0
	blmi	" SYMBOL_NAME_STR(__down)
		: : "r" (sem) : "r0", "r1", "r2", "r3", "ip", "lr", "cc");
}

/*
 * This is ugly, but we want the default case to fall through.
 * "__down_interruptible" is the actual routine that waits...
 */
extern inline int down_interruptible (struct semaphore * sem)
{
	int result;
	__asm__ __volatile__ ("
	@ atomic down operation
	mrs	r0, cpsr
	orr	r1, r0, #128		@ disable IRQs
	bic	r0, r0, #0x80000000	@ clear N
	msr	cpsr, r1
	ldr	r1, [%1]
	subs	r1, r1, #1
	str	r1, [%1]
	orrmi	r0, r0, #0x80000000	@ set N
	msr	cpsr, r0
	movmi	r0, %1
	movpl	r0, #0
	blmi	" SYMBOL_NAME_STR(__down_interruptible) "
	mov	%0, r0"
		: "=r" (result)
		: "r" (sem)
		: "r0", "r1", "r2", "r3", "ip", "lr", "cc");
	return result;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
extern inline void up(struct semaphore * sem)
{
	__asm__ __volatile__ ("
	@ atomic up operation
	mrs	r0, cpsr
	orr	r1, r0, #128		@ disable IRQs
	bic	r0, r0, #0x80000000	@ clear N
	msr	cpsr, r1
	ldr	r1, [%0]
	adds	r1, r1, #1
	str	r1, [%0]
	orrls	r0, r0, #0x80000000	@ set N
	msr	cpsr, r0
	movmi	r0, %0
	blmi	" SYMBOL_NAME_STR(__up)
		: : "r" (sem) : "r0", "r1", "r2", "r3", "ip", "lr", "cc");
}

#endif
