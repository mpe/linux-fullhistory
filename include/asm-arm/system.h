#ifndef __ASM_ARM_SYSTEM_H
#define __ASM_ARM_SYSTEM_H

#include <linux/kernel.h>
#include <asm/proc-fns.h>

extern void arm_malalignedptr(const char *, void *, volatile void *);
extern void arm_invalidptr(const char *, int);

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

#define tas(ptr) (xchg((ptr),1))

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.
 *
 * `next' and `prev' should be struct task_struct, but it isn't always defined
 */
#define switch_to(prev,next) processor._switch_to(prev,next)

/*
 * Include processor dependent parts
 */
#include <asm/proc/system.h>
#include <asm/arch/system.h>

#define mb() __asm__ __volatile__ ("" : : : "memory")
#define nop() __asm__ __volatile__("mov r0,r0\n\t");

extern asmlinkage void __backtrace(void);

#endif

