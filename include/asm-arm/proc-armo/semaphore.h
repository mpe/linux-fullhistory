/*
 * linux/include/asm-arm/proc-armo/semaphore.h
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
	mov	r0, pc
	orr	lr, r0, #0x08000000
	teqp	lr, #0
	ldr	lr, [%0]
	and	r0, r0, #0x0c000003
	subs	lr, lr, #1
	str	lr, [%0]
	orrmi	r0, r0, #0x80000000	@ set N
	teqp	r0, #0
	movmi	r0, %0
	blmi	" SYMBOL_NAME_STR(__down_failed)
		:
		: "r" (sem)
		: "r0", "lr", "cc");
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
	mov	r0, pc
	orr	lr, r0, #0x08000000
	teqp	lr, #0
	ldr	lr, [%1]
	and	r0, r0, #0x0c000003
	subs	lr, lr, #1
	str	lr, [%1]
	orrmi	r0, r0, #0x80000000	@ set N
	teqp	r0, #0
	movmi	r0, %1
	movpl	r0, #0
	blmi	" SYMBOL_NAME_STR(__down_interruptible_failed) "
	mov	%0, r0"
		: "=r" (result)
		: "r" (sem)
		: "r0", "lr", "cc");
	return result;
}

extern inline int down_trylock(struct semaphore * sem)
{
	int result;
	__asm__ __volatile__ ("
	@ atomic down operation
	mov	r0, pc
	orr	lr, r0, #0x08000000
	teqp	lr, #0
	ldr	lr, [%1]
	and	r0, r0, #0x0c000003
	subs	lr, lr, #1
	str	lr, [%1]
	orrmi	r0, r0, #0x80000000	@ set N
	teqp	r0, #0
	movmi	r0, %1
	movpl	r0, #0
	blmi	" SYMBOL_NAME_STR(__down_trylock_failed) "
	mov	%0, r0"
		: "=r" (result)
		: "r" (sem)
		: "r0", "lr", "cc");
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
	mov	r0, pc
	orr	lr, r0, #0x08000000
	teqp	lr, #0
	ldr	lr, [%0]
	and	r0, r0, #0x0c000003
	adds	lr, lr, #1
	str	lr, [%0]
	orrle	r0, r0, #0x80000000	@ set N
	teqp	r0, #0
	movmi	r0, %0
	blmi	" SYMBOL_NAME_STR(__up_wakeup)
		:
		: "r" (sem)
		: "r0", "lr", "cc");
}

#endif
