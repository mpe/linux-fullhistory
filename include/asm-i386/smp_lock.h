#ifndef __I386_SMPLOCK_H
#define __I386_SMPLOCK_H

#define __STR(x) #x

#ifndef __SMP__

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task, cpu, depth)	((depth) = 1)
#define reacquire_kernel_lock(task, cpu, depth)	do { } while(0)

#else

#include <asm/hardirq.h>

/* Release global kernel lock and global interrupt lock */
#define release_kernel_lock(task, cpu, depth) \
do { \
	if ((depth = (task)->lock_depth) != 0) { \
		__cli(); \
		(task)->lock_depth = 0; \
		active_kernel_processor = NO_PROC_ID; \
		clear_bit(0,&kernel_flag); \
	} \
	release_irqlock(cpu); \
	__sti(); \
} while (0)

/* Re-acquire the kernel lock */
#define reacquire_kernel_lock(task, cpu, depth) \
do { if (depth) __asm__ __volatile__( \
	"cli\n\t" \
	"movl $0f,%%eax\n\t" \
	"jmp __lock_kernel\n" \
	"0:\t" \
	"movl %2,%0\n\t" \
	"sti" \
	: "=m" (task->lock_depth) \
	: "d" (cpu), "c" (depth) \
	: "ax"); \
} while (0)
	

/* Locking the kernel */
extern __inline__ void lock_kernel(void)
{
	int cpu = smp_processor_id();

	if (local_irq_count[cpu]) {
		__label__ l1;
l1:		printk("lock from interrupt context at %p\n", &&l1);
	}
	if (cpu == global_irq_holder) {
		__label__ l2;
l2:		printk("Ugh at %p\n", &&l2);
		sti();
	}

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
	movb	%1, " __STR(active_kernel_processor) "
	lock
	btrl	$0, " __STR(kernel_flag) "
1:
	popfl
"	: /* no outputs */
	: "m" (current->lock_depth), "i" (NO_PROC_ID)
	: "ax", "memory");
}

#endif /* __SMP__ */

#endif /* __I386_SMPLOCK_H */
