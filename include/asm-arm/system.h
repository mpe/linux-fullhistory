#ifndef __ASM_ARM_SYSTEM_H
#define __ASM_ARM_SYSTEM_H

#include <linux/kernel.h>

#ifdef __KERNEL__

#include <linux/config.h>

/* information about the system we're running on */
extern unsigned int system_rev;
extern unsigned int system_serial_low;
extern unsigned int system_serial_high;
extern unsigned int mem_fclk_21285;

/* The type of machine we're running on */
extern unsigned int __machine_arch_type;

/* see arch/arm/kernel/arch.c for a description of these */
#define MACH_TYPE_EBSA110		0
#define MACH_TYPE_RISCPC		1
#define MACH_TYPE_NEXUSPCI		3
#define MACH_TYPE_EBSA285		4
#define MACH_TYPE_NETWINDER		5
#define MACH_TYPE_CATS			6
#define MACH_TYPE_TBOX			7
#define MACH_TYPE_CO285			8
#define MACH_TYPE_CLPS7110		9
#define MACH_TYPE_ARCHIMEDES		10
#define MACH_TYPE_A5K			11
#define MACH_TYPE_ETOILE		12
#define MACH_TYPE_LACIE_NAS		13
#define MACH_TYPE_CLPS7500		14
#define MACH_TYPE_SHARK			15
#define MACH_TYPE_BRUTUS		16
#define MACH_TYPE_PERSONAL_SERVER	17
#define MACH_TYPE_SA1100		18	/* unused/too general */
#define MACH_TYPE_L7200			19
#define MACH_TYPE_SA1110		20	/* unused/too general */
#define MACH_TYPE_INTEGRATOR		21
#define MACH_TYPE_BITSY			22
#define MACH_TYPE_IXP1200		23
#define MACH_TYPE_THINCLIENT		24
#define MACH_TYPE_ASSABET		25
#define MACH_TYPE_VICTOR		26
#define MACH_TYPE_LART			27
#define MACH_TYPE_RANGER		28
#define MACH_TYPE_GRAPHICSCLIENT	29
#define MACH_TYPE_XP860			30

/*
 * Sort out a definition for machine_arch_type
 * The rules are:
 * 1. If one architecture is selected, then all machine_is_xxx()
 *    are constant.
 * 2. If two or more architectures are selected, then the selected
 *    machine_is_xxx() are variable, and the unselected machine_is_xxx()
 *    are constant zero.
 *
 * In general, you should use machine_is_xxxx() in your code, not:
 *  -  switch (machine_arch_type) { }
 *  -  if (machine_arch_type = xxxx)
 *  -  __machine_arch_type
 *
 * Please note that these are kept in numeric order (ie, the same
 * order as the list above).
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

#ifdef CONFIG_ARCH_CATS
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

#ifdef CONFIG_ARCH_ARC
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_ARCHIMEDES
# endif
# define machine_is_arc()	(machine_arch_type == MACH_TYPE_ARCHIMEDES)
#else
# define machine_is_arc()	(0)
#endif

#ifdef CONFIG_ARCH_A5K
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_A5K
# endif
# define machine_is_a5k()	(machine_arch_type == MACH_TYPE_A5K)
#else
# define machine_is_a5k()	(0)
#endif

#ifdef CONFIG_ARCH_CLPS7500
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_CLPS7500
# endif
# define machine_is_clps7500()	(machine_arch_type == MACH_TYPE_CLPS7500)
#else
# define machine_is_clps7500()	(0)
#endif

#ifdef CONFIG_ARCH_SHARK
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_SHARK
# endif
# define machine_is_shark()	(machine_arch_type == MACH_TYPE_SHARK)
#else
# define machine_is_shark()	(0)
#endif

#ifdef CONFIG_SA1100_BRUTUS
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_BRUTUS
# endif
# define machine_is_brutus()	(machine_arch_type == MACH_TYPE_BRUTUS)
#else
# define machine_is_brutus()	(0)
#endif

#ifdef CONFIG_ARCH_PERSONAL_SERVER
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_PERSONAL_SERVER
# endif
# define machine_is_personal_server()	(machine_arch_type == MACH_TYPE_PERSONAL_SERVER)
#else
# define machine_is_personal_server()	(0)
#endif

#ifdef CONFIG_ARCH_L7200
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_L7200
# endif
# define machine_is_l7200()	(machine_arch_type == MACH_TYPE_L7200)
#else
# define machine_is_l7200()	(0)
#endif

#ifdef CONFIG_SA1100_BITSY
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_BITSY
# endif
# define machine_is_bitsy()	(machine_arch_type == MACH_TYPE_BITSY)
#else
# define machine_is_bitsy()	(0)
#endif

#ifdef CONFIG_SA1100_THINCLIENT
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_THINCLIENT
# endif
# define machine_is_thinclient()	(machine_arch_type == MACH_TYPE_THINCLIENT)
#else
# define machine_is_thinclient()	(0)
#endif

#ifdef CONFIG_SA1100_ASSABET
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_ASSABET
# endif
# define machine_is_assabet()	(machine_arch_type == MACH_TYPE_ASSABET)
#else
# define machine_is_assabet()	(0)
#endif

#ifdef CONFIG_SA1100_VICTOR
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_VICTOR
# endif
# define machine_is_victor()	(machine_arch_type == MACH_TYPE_VICTOR)
#else
# define machine_is_victor()	(0)
#endif

#ifdef CONFIG_SA1100_LART
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_LART
# endif
# define machine_is_lart()	(machine_arch_type == MACH_TYPE_LART)
#else
# define machine_is_lart()	(0)
#endif

#ifdef CONFIG_SA1100_GRAPHICSCLIENT
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_GRAPHICSCLIENT
# endif
# define machine_is_grpahicsclient() \
				(machine_arch_type == MACH_TYPE_GRAPHICSCLIENT)
#else
# define machine_is_graphicsclient() \
				(0)
#endif

#ifdef CONFIG_SA1100_XP860
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_XP860
# endif
# define machine_is_xp860()	(machine_arch_type == MACH_TYPE_XP860)
#else
# define machine_is_xp860()	(0)
#endif

/*
 * The following are currently unregistered
 */
#ifdef CONFIG_SA1100_ITSY
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_ITSY
# endif
# define machine_is_itsy()	(machine_arch_type == MACH_TYPE_ITSY)
#else
# define machine_is_itsy()	(0)
#endif

#ifdef CONFIG_SA1100_EMPEG
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_EMPEG
# endif
# define machine_is_empeg()	(machine_arch_type == MACH_TYPE_EMPEG)
#else
# define machine_is_empeg()	(0)
#endif

#ifdef CONFIG_SA1100_TIFON
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_TIFON
# endif
# define machine_is_tifon()	(machine_arch_type == MACH_TYPE_TIFON)
#else
# define machine_is_tifon()	(0)
#endif

#ifdef CONFIG_SA1100_PLEB
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_PLEB
# endif
# define machine_is_pleb()	(machine_arch_type == MACH_TYPE_PLEB)
#else
# define machine_is_pleb()	(0)
#endif

#ifdef CONFIG_SA1100_PENNY
# ifdef machine_arch_type
#  undef machine_arch_type
#  define machine_arch_type	__machine_arch_type
# else
#  define machine_arch_type	MACH_TYPE_PENNY
# endif
# define machine_is_penny()	(machine_arch_type == MACH_TYPE_PENNY)
#else
# define machine_is_penny()	(0)
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
