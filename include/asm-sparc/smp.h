/* smp.h: Sparc specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_SMP_H
#define _SPARC_SMP_H

#include <asm/bitops.h>
#include <asm/ptrace.h>

/* Per processor Sparc parameters. */

struct cpuinfo_sparc {
	unsigned char impl;
	unsigned char vers;
	unsigned long udelay_val;
};

extern struct cpuinfo_sparc cpu_data[NR_CPUS];

typedef klock_t volatile unsigned char;
extern klock_t kernel_lock;

#define KLOCK_HELD       0xff
#define KLOCK_CLEAR      0x00

struct sparc_ipi_invalidate {
	struct mm_struct *mm;
	unsigned long addr;   /* page for inv_pg, start for inv_rnge */
	unsigned long end;    /* Used for inv_rnge only. */
};

struct sparc_ipimsg {
	union {
		/* Add more here as we need them. */
		struct sparc_ipi_invalidate invmsg;
	};
};

extern void smp_scan_prom_for_cpus(unsigned long, unsigned long);
extern unsigned long smp_alloc_memory(unsigned long mem_base);
extern unsigned long *kernel_stacks[NR_CPUS];
extern unsigned char boot_cpu_id;
extern unsigned long cpu_present_map;
extern volatile unsigned long smp_invalidate_needed;
extern unsigned long kernel_counter;
extern volatile unsigned char active_kernel_processor;
extern void smp_message_irq(int cpl, struct pt_regs *regs);
extern void smp_reschedule_irq(int cpl, struct pt_regs *regs);
extern void smp_invalidate_rcv(void);
extern volatils unsigned long syscall_count;

extern void (*smp_invalidate_all)(void);
extern void (*smp_invalidate_mm)(struct mm_struct *);
extern void (*smp_invalidate_range)(struct mm_struct *, unsigned long, unsigned long);
extern void (*smp_invalidate_page)(struct vm_area_struct *, unsigned long);

extern void smp_callin(void);
extern void smp_boot_cpus(void);
extern void smp_store_cpu_info(int id);

extern _inline_ int smp_processor_id(void)
{
	int cpuid;

	__asm__ __volatile__("rd %%tbr, %0\n\t"
			     "srl %0, 24, %0\n\t"
			     "and %0, 3, %0\n\t" :
			     "=&r" (cpuid) :
			     "0" (cpuid));
	return cpuid;
}

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

#define PROC_CHANGE_PENALTY   0x23

#endif /* !(_SPARC_SMP_H) */
