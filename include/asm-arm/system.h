#ifndef __ASM_ARM_SYSTEM_H
#define __ASM_ARM_SYSTEM_H

#include <linux/kernel.h>

#ifdef __KERNEL__

#include <linux/config.h>

/* information about the system we're running on */
extern unsigned int system_rev;
extern unsigned int system_serial_low;
extern unsigned int system_serial_high;

/* The type of machine we're running on */
extern unsigned int __machine_arch_type;

/* see arch/arm/kernel/setup.c for a description of these */
#define MACH_TYPE_EBSA110	0
#define MACH_TYPE_RISCPC	1
#define MACH_TYPE_NEXUSPCI	3
#define MACH_TYPE_EBSA285	4
#define MACH_TYPE_NETWINDER	5
#define MACH_TYPE_CATS		6
#define MACH_TYPE_TBOX		7
#define MACH_TYPE_CO285		8
#define MACH_TYPE_CLPS7110	9
#define MACH_TYPE_ARCHIMEDES	10
#define MACH_TYPE_A5K		11
#define MACH_TYPE_ETOILE	12
#define MACH_TYPE_LACIE_NAS	13
#define MACH_TYPE_CLPS7500	14
#define MACH_TYPE_SHARK		15
#define MACH_TYPE_SA1100	16

/*
 * Sort out a definition for machine_arch_type
 * The rules are:
 * 1. If one architecture is selected, then all machine_is_xxx()
 *    are constant.
 * 2. If two or more architectures are selected, then the selected
 *    machine_is_xxx() are variable, and the unselected machine_is_xxx()
 *    are constant zero.
 */
#ifdef CONFIG_ARCH_EBSA110
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_EBSA110
# endif
# define machine_is_ebsa110()	(machine_arch_type == MACH_TYPE_EBSA110)
#else
# define machine_is_ebsa110()	(0)
#endif

#ifdef CONFIG_ARCH_RPC
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_RISCPC
# endif
# define machine_is_riscpc()	(machine_arch_type == MACH_TYPE_RISCPC)
#else
# define machine_is_riscpc()	(0)
#endif

#ifdef CONFIG_ARCH_EBSA285
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_EBSA285
# endif
# define machine_is_ebsa285()	(machine_arch_type == MACH_TYPE_EBSA285)
#else
# define machine_is_ebsa285()	(0)
#endif

#ifdef CONFIG_ARCH_NETWINDER
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_NETWINDER
# endif
# define machine_is_netwinder()	(machine_arch_type == MACH_TYPE_NETWINDER)
#else
# define machine_is_netwinder()	(0)
#endif

#ifdef CONFIG_CATS
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_CATS
# endif
# define machine_is_cats()	(machine_arch_type == MACH_TYPE_CATS)
#else
# define machine_is_cats()	(0)
#endif

#ifdef CONFIG_ARCH_CO285
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_CO285
# endif
# define machine_is_co285()	(machine_arch_type == MACH_TYPE_CO285)
#else
# define machine_is_co285()	(0)
#endif

#ifdef CONFIG_ARCH_SA1100
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_SA1100
# endif
# define machine_is_sa1100()	(machine_arch_type == MACH_TYPE_SA1100
#else
# define machine_is_sa1100()	(0)
#endif

#ifndef machine_arch_type
#define machine_arch_type	__machine_arch_type
#endif

#include <asm/proc-fns.h>

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

#define tas(ptr) (xchg((ptr),1))

extern void arm_malalignedptr(const char *, void *, volatile void *);
extern asmlinkage void __backtrace(void);

/*
 * Include processor dependent parts
 */
#include <asm/proc/system.h>

#define mb() __asm__ __volatile__ ("" : : : "memory")
#define rmb() mb()
#define wmb() mb()
#define nop() __asm__ __volatile__("mov\tr0,r0\t@ nop\n\t");

#define prepare_to_switch()    do { } while(0)

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.
 * The `mb' is to tell GCC not to cache `current' across this call.
 */
extern struct task_struct *__switch_to(struct task_struct *prev, struct task_struct *next);

#define switch_to(prev,next,last)		\
	do {			 		\
		last = __switch_to(prev,next);	\
		mb();				\
	} while (0)

#endif

#endif
