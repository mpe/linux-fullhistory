/* smp.h: Sparc64 specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_SMP_H
#define _SPARC64_SMP_H

#include <asm/asi.h>

#ifndef __ASSEMBLY__
/* PROM provided per-processor information we need
 * to start them all up.
 */

struct prom_cpuinfo {
	int prom_node;
	int mid;
};

extern int linux_num_cpus;	/* number of CPUs probed  */
extern struct prom_cpuinfo linux_cpus[NCPUS];

#endif /* !(__ASSEMBLY__) */

#ifdef __SMP__

#ifndef __ASSEMBLY__

/* Per processor Sparc parameters we need. */

struct cpuinfo_sparc {
	unsigned long udelay_val; /* that's it */
};

extern struct cpuinfo_sparc cpu_data[NR_CPUS];

typedef __volatile__ unsigned char klock_t;
extern klock_t kernel_flag;

#define KLOCK_HELD       0xff
#define KLOCK_CLEAR      0x00

/*
 *	Private routines/data
 */
 
extern int smp_found_cpus;
extern unsigned char boot_cpu_id;
extern unsigned int cpu_present_map;
extern __volatile__ unsigned long smp_invalidate_needed[NR_CPUS];
extern __volatile__ unsigned long kernel_counter;
extern __volatile__ unsigned char active_kernel_processor;
extern void smp_message_irq(void);
extern unsigned long ipi_count;
extern __volatile__ unsigned long kernel_counter;
extern __volatile__ unsigned long syscall_count;

extern void print_lock_state(void);

typedef void (*smpfunc_t)(unsigned long, unsigned long, unsigned long,
		       unsigned long, unsigned long);

/*
 *	General functions that each host system must provide.
 */

extern void smp_callin(void);
extern void smp_boot_cpus(void);
extern void smp_store_cpu_info(int id);
extern void smp_cross_call(smpfunc_t func, unsigned long arg1, unsigned long arg2,
			   unsigned long arg3, unsigned long arg4, unsigned long arg5);

extern __inline__ void xc0(smpfunc_t func) { smp_cross_call(func, 0, 0, 0, 0, 0); }
extern __inline__ void xc1(smpfunc_t func, unsigned long arg1)
{ smp_cross_call(func, arg1, 0, 0, 0, 0); }
extern __inline__ void xc2(smpfunc_t func, unsigned long arg1, unsigned long arg2)
{ smp_cross_call(func, arg1, arg2, 0, 0, 0); }
extern __inline__ void xc3(smpfunc_t func, unsigned long arg1, unsigned long arg2,
			   unsigned long arg3)
{ smp_cross_call(func, arg1, arg2, arg3, 0, 0); }
extern __inline__ void xc4(smpfunc_t func, unsigned long arg1, unsigned long arg2,
			   unsigned long arg3, unsigned long arg4)
{ smp_cross_call(func, arg1, arg2, arg3, arg4, 0); }
extern __inline__ void xc5(smpfunc_t func, unsigned long arg1, unsigned long arg2,
			   unsigned long arg3, unsigned long arg4, unsigned long arg5)
{ smp_cross_call(func, arg1, arg2, arg3, arg4, arg5); }

extern __volatile__ int cpu_number_map[NR_CPUS];
extern __volatile__ int cpu_logical_map[NR_CPUS];

extern __inline__ int smp_processor_id(void)
{
	int cpuid;

	/* Get MID from UPA Config register, and use that. */
	__asm__ __volatile__("
		ldxa	[%g0] %1, %0
		srlx	%0, 17, %0
		and	%0, 0x1f, %0
	" : "=r" cpuid
	  : "i" (ASI_UPA_CONFIG));

	return cpuid;
}


extern __volatile__ unsigned long smp_proc_in_lock[NR_CPUS]; /* for computing process time */
extern __volatile__ int smp_process_available;

extern __inline__ __volatile__ void inc_smp_counter(volatile int *ctr)
{
	unsigned long temp0, temp1;

	__asm__ __volatile__("
	lduw		[%2], %0
1:
	add		%0, 1, %1
	cas		[%2], %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduw		[%2], %0
2:
	membar		#StoreStore | #StoreLoad
" 	: "=&r" (temp0), "=&r" (temp1), "=r" (ctr)
	: "ir" (i), "r" (ctr)
	: "cc");
}

extern __inline__ __volatile__ void dec_smp_counter(volatile int *ctr)
{
	unsigned long temp0, temp1;

	__asm__ __volatile__("
	lduw		[%2], %0
1:
	sub		%0, 1, %1
	cas		[%2], %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduw		[%2], %0
2:
	membar		#StoreStore | #StoreLoad
" 	: "=&r" (temp0), "=&r" (temp1), "=r" (ctr)
	: "ir" (i), "r" (ctr)
	: "cc");
}

extern __inline__ __volatile__ int read_smp_counter(volatile int *ctr)
{
	return *ctr;
}

#endif /* !(__ASSEMBLY__) */

/* Sparc specific messages. */
#define MSG_CROSS_CALL         0x0005       /* run func on cpus */

/* Empirical PROM processor mailbox constants.  If the per-cpu mailbox
 * contains something other than one of these then the ipi is from
 * Linux's active_kernel_processor.  This facility exists so that
 * the boot monitor can capture all the other cpus when one catches
 * a watchdog reset or the user enters the monitor using L1-A keys.
 */
#define MBOX_STOPCPU          0xFB
#define MBOX_IDLECPU          0xFC
#define MBOX_IDLECPU2         0xFD
#define MBOX_STOPCPU2         0xFE


#define NO_PROC_ID            0xFF

#define PROC_CHANGE_PENALTY     20

#define SMP_FROM_INT		1
#define SMP_FROM_SYSCALL	2

#endif /* !(__SMP__) */

#endif /* !(_SPARC64_SMP_H) */
