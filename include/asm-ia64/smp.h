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

#define IPI_DEFAULT_BASE_ADDR	0xfee00000
#define XTP_OFFSET		0x1e0008

#define smp_processor_id() (current->processor)

extern unsigned long cpu_present_map;
extern unsigned long cpu_online_map;
extern unsigned long ipi_base_addr;
extern int bootstrap_processor;
extern volatile int __cpu_number_map[NR_CPUS];
extern volatile int __cpu_logical_map[NR_CPUS];

#define cpu_number_map(i)	__cpu_number_map[i]
#define cpu_logical_map(i)	__cpu_logical_map[i]

#if defined(CONFIG_KDB)
extern volatile unsigned long smp_kdb_wait;
#endif  /* CONFIG_KDB */

extern unsigned long ap_wakeup_vector;

/*
 * XTP control functions:
 *    min_xtp   :  route all interrupts to this CPU
 *    normal_xtp:  nominal XTP value
 *    raise_xtp :  Route all interrupts away from this CPU
 *    max_xtp   :  never deliver interrupts to this CPU.
 */

/* 
 * This turns off XTP based interrupt routing.  There is a bug in the handling of 
 * IRQ_INPROGRESS when the same vector appears on more than one CPU. 
 */
extern int use_xtp;

extern __inline void 
min_xtp(void)
{
	if (use_xtp)
		writeb(0x80, ipi_base_addr | XTP_OFFSET); /* XTP to min */
}

extern __inline void
normal_xtp(void)
{
	if (use_xtp)
		writeb(0x8e, ipi_base_addr | XTP_OFFSET); /* XTP normal */
}

extern __inline void
max_xtp(void) 
{
	if (use_xtp)
		writeb(0x8f, ipi_base_addr | XTP_OFFSET); /* Set XTP to max... */
}

extern __inline unsigned int 
hard_smp_processor_id(void)
{
	struct {
		unsigned long reserved : 16;
		unsigned long eid : 8;
		unsigned long id  : 8;
		unsigned long ignored : 32;
	} lid;

	__asm__ __volatile__ ("mov %0=cr.lid" : "=r" (lid));

	/*
	 * Damn.  IA64 CPU ID's are 16 bits long, Linux expect the hard id to be 
	 * in the range 0..31.  So, return the low-order bits of the bus-local ID 
	 * only and hope it's less than 32. This needs to be fixed...
	 */
	return (lid.id & 0x0f);
}

#define NO_PROC_ID 0xffffffff
#define PROC_CHANGE_PENALTY 20

extern void __init init_smp_config (void);
extern void smp_do_timer (struct pt_regs *regs);

#endif /* CONFIG_SMP */
#endif /* _ASM_IA64_SMP_H */
