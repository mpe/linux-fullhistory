/* smp.h: Sparc specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_SMP_H
#define _SPARC_SMP_H

#ifndef __ASSEMBLY__
/* PROM provided per-processor information we need
 * to start them all up.
 */

struct prom_cpuinfo {
	int prom_node;
	int mid;
};
#endif /* !(__ASSEMBLY__) */

#ifdef __SMP__

#ifndef __ASSEMBLY__

extern struct prom_cpuinfo linux_cpus[NCPUS];

/* Per processor Sparc parameters we need. */

struct cpuinfo_sparc {
	unsigned long udelay_val; /* that's it */
};

extern struct cpuinfo_sparc cpu_data[NR_CPUS];

typedef volatile unsigned char klock_t;
extern klock_t kernel_flag;

#define KLOCK_HELD       0xff
#define KLOCK_CLEAR      0x00

/*
 *	Private routines/data
 */
 
extern int smp_found_cpus;
extern unsigned char boot_cpu_id;
extern unsigned long cpu_present_map;
extern volatile unsigned long smp_invalidate_needed[NR_CPUS];
extern volatile unsigned long kernel_counter;
extern volatile unsigned char active_kernel_processor;
extern void smp_message_irq(void);
extern unsigned long ipi_count;
extern volatile unsigned long kernel_counter;
extern volatile unsigned long syscall_count;

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
extern void smp_capture(void);
extern void smp_release(void);

extern inline void xc0(smpfunc_t func) { smp_cross_call(func, 0, 0, 0, 0, 0); }
extern inline void xc1(smpfunc_t func, unsigned long arg1)
{ smp_cross_call(func, arg1, 0, 0, 0, 0); }
extern inline void xc2(smpfunc_t func, unsigned long arg1, unsigned long arg2)
{ smp_cross_call(func, arg1, arg2, 0, 0, 0); }
extern inline void xc3(smpfunc_t func, unsigned long arg1, unsigned long arg2,
		       unsigned long arg3)
{ smp_cross_call(func, arg1, arg2, arg3, 0, 0); }
extern inline void xc4(smpfunc_t func, unsigned long arg1, unsigned long arg2,
		       unsigned long arg3, unsigned long arg4)
{ smp_cross_call(func, arg1, arg2, arg3, arg4, 0); }
extern inline void xc5(smpfunc_t func, unsigned long arg1, unsigned long arg2,
		       unsigned long arg3, unsigned long arg4, unsigned long arg5)
{ smp_cross_call(func, arg1, arg2, arg3, arg4, arg5); }

extern volatile int cpu_number_map[NR_CPUS];
extern volatile int cpu_logical_map[NR_CPUS];

extern __inline int smp_processor_id(void)
{
	int cpuid;

	__asm__ __volatile__("rd %%tbr, %0\n\t"
			     "srl %0, 12, %0\n\t"
			     "and %0, 3, %0\n\t" :
			     "=&r" (cpuid));
	return cpuid;
}


extern volatile unsigned long smp_proc_in_lock[NR_CPUS]; /* for computing process time */
extern volatile int smp_process_available;

extern inline int smp_swap(volatile int *addr, int value)
{
	__asm__ __volatile__("swap [%2], %0\n\t" :
			     "=&r" (value) :
			     "0" (value), "r" (addr));
	return value;
}

extern inline volatile void inc_smp_counter(volatile int *ctr)
{
	int tmp;

	while((tmp = smp_swap(ctr, -1)) == -1)
		;
	smp_swap(ctr, (tmp + 1));
}

extern inline volatile void dec_smp_counter(volatile int *ctr)
{
	int tmp;

	while((tmp = smp_swap(ctr, -1)) == -1)
		;
	smp_swap(ctr, (tmp - 1));
}

extern inline volatile int read_smp_counter(volatile int *ctr)
{
	int value;

	while((value = *ctr) == -1)
		;
	return value;
}

#endif /* !(__ASSEMBLY__) */

/* Sparc specific messages. */
#define MSG_CAPTURE            0x0004       /* Park a processor. */
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

#endif /* !(_SPARC_SMP_H) */
