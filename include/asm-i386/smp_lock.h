#ifndef __I386_SMPLOCK_H
#define __I386_SMPLOCK_H

#ifndef __SMP__

#define lock_kernel()		do { } while(0)
#define unlock_kernel()		do { } while(0)

#else

/* Locking the kernel */
extern __inline__ void lock_kernel(void)
{
	int cpu = smp_processor_id();

	__asm__ __volatile__("
	pushfl
	cli
	cmpl	$0, %0
	jne	0f
	movl	$0f, %%eax
	jmp	__lock_kernel
0:
	incl	%0
	popfl
"	:
	: "m" (current_set[cpu]->lock_depth), "d" (cpu)
	: "ax", "memory");
}

extern __inline__ void unlock_kernel(void)
{
	__asm__ __volatile__("
	pushfl
	cli
	decl	%0
	jnz	1f
	movb	%1, active_kernel_processor
	lock
	btrl	$0, kernel_flag
1:
	popfl
"	: /* no outputs */
	: "m" (current->lock_depth), "i" (NO_PROC_ID)
	: "ax", "memory");
}

#endif /* __SMP__ */

#endif /* __I386_SMPLOCK_H */
