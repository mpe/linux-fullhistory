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
extern struct prom_cpuinfo linux_cpus[NR_CPUS];

#endif /* !(__ASSEMBLY__) */

#ifdef __SMP__

#ifndef __ASSEMBLY__

/* Per processor Sparc parameters we need. */

struct cpuinfo_sparc {
	unsigned long udelay_val; /* that's it */
};

extern struct cpuinfo_sparc cpu_data[NR_CPUS];

struct klock_info {
	unsigned char kernel_flag;
	unsigned char akp;
};

extern struct klock_info klock_info;

#define KLOCK_HELD       0xff
#define KLOCK_CLEAR      0x00

/*
 *	Private routines/data
 */
 
extern int smp_found_cpus;
extern unsigned char boot_cpu_id;
extern unsigned long cpu_present_map;
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

extern __volatile__ int cpu_number_map[NR_CPUS];
extern __volatile__ int cpu_logical_map[NR_CPUS];

extern __inline__ int hard_smp_processor_id(void)
{
	unsigned long upaconfig;

	__asm__ __volatile__("ldxa	[%%g0] %1, %0"
			     : "=r" (upaconfig)
			     : "i" (ASI_UPA_CONFIG));
	return ((upaconfig >> 17) & 0x1f);
}

#define smp_processor_id() (current->processor)

extern __volatile__ unsigned long smp_proc_in_lock[NR_CPUS]; /* for computing process time */
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

#define PROC_CHANGE_PENALTY     20

#define SMP_FROM_INT		1
#define SMP_FROM_SYSCALL	2

#endif /* !(__SMP__) */

#define NO_PROC_ID            0xFF

#endif /* !(_SPARC64_SMP_H) */
