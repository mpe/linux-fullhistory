/*
 * SMP Support
 *
 * Copyright (C) 1999 VA Linux Systems 
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 */
#ifndef _ASM_IA64_SMP_H
#define _ASM_IA64_SMP_H

#include <linux/config.h>

#ifdef CONFIG_SMP

#include <linux/init.h>
#include <linux/threads.h>
#include <linux/kernel.h>

#include <asm/ptrace.h>
#include <asm/io.h>

#define XTP_OFFSET		0x1e0008

#define SMP_IRQ_REDIRECTION     (1 << 0)
#define SMP_IPI_REDIRECTION     (1 << 1)

#define smp_processor_id()	(current->processor)

struct smp_boot_data {
	int cpu_count;
	int cpu_map[NR_CPUS];
};

extern unsigned long cpu_present_map;
extern unsigned long cpu_online_map;
extern unsigned long ipi_base_addr;
extern int bootstrap_processor;
extern volatile int __cpu_number_map[NR_CPUS];
extern volatile int __cpu_logical_map[NR_CPUS];
extern unsigned char smp_int_redirect;
extern char no_int_routing;

#define cpu_number_map(i)	__cpu_number_map[i]
#define cpu_logical_map(i)	__cpu_logical_map[i]

extern unsigned long ap_wakeup_vector;

/*
 * XTP control functions:
 *    min_xtp   :  route all interrupts to this CPU
 *    normal_xtp:  nominal XTP value
 *    max_xtp   :  never deliver interrupts to this CPU.
 */

extern __inline void 
min_xtp(void)
{
	if (smp_int_redirect & SMP_IRQ_REDIRECTION)
		writeb(0x00, ipi_base_addr | XTP_OFFSET); /* XTP to min */
}

extern __inline void
normal_xtp(void)
{
	if (smp_int_redirect & SMP_IRQ_REDIRECTION)
		writeb(0x08, ipi_base_addr | XTP_OFFSET); /* XTP normal */
}

extern __inline void
max_xtp(void) 
{
	if (smp_int_redirect & SMP_IRQ_REDIRECTION)
		writeb(0x0f, ipi_base_addr | XTP_OFFSET); /* Set XTP to max */
}

extern __inline__ unsigned int 
hard_smp_processor_id(void)
{
	struct {
		unsigned long reserved : 16;
		unsigned long eid : 8;
		unsigned long id  : 8;
		unsigned long ignored : 32;
	} lid;

	__asm__ ("mov %0=cr.lid" : "=r" (lid));

#ifdef LARGE_CPU_ID_OK
	return lid.eid << 8 | lid.id;
#else
	if (((lid.id << 8) | lid.eid) > NR_CPUS)
		printk("WARNING: SMP ID %d > NR_CPUS\n", (lid.id << 8) | lid.eid);
	return lid.id;
#endif
}

#define NO_PROC_ID		(-1)
#define PROC_CHANGE_PENALTY	20

extern void __init init_smp_config (void);
extern void smp_do_timer (struct pt_regs *regs);

#endif /* CONFIG_SMP */
#endif /* _ASM_IA64_SMP_H */
