/* 
 * smp.h: PPC64 specific SMP code.
 *
 * Original was a copy of sparc smp.h.  Now heavily modified
 * for PPC.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996-2001 Cort Dougan <cort@fsmlabs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef __KERNEL__
#ifndef _PPC64_SMP_H
#define _PPC64_SMP_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>

#ifndef __ASSEMBLY__

#include <asm/paca.h>

extern int boot_cpuid;

#ifdef CONFIG_SMP

extern void smp_send_debugger_break(int cpu);
struct pt_regs;
extern void smp_message_recv(int, struct pt_regs *);


#define smp_processor_id() (get_paca()->paca_index)
#define hard_smp_processor_id() (get_paca()->hw_cpu_id)

extern cpumask_t cpu_sibling_map[NR_CPUS];

/* Since OpenPIC has only 4 IPIs, we use slightly different message numbers.
 *
 * Make sure this matches openpic_request_IPIs in open_pic.c, or what shows up
 * in /proc/interrupts will be wrong!!! --Troy */
#define PPC_MSG_CALL_FUNCTION   0
#define PPC_MSG_RESCHEDULE      1
/* This is unused now */
#if 0
#define PPC_MSG_MIGRATE_TASK    2
#endif
#define PPC_MSG_DEBUGGER_BREAK  3

extern cpumask_t irq_affinity[];

void smp_init_iSeries(void);
void smp_init_pSeries(void);

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
extern void cpu_die(void) __attribute__((noreturn));
extern int query_cpu_stopped(unsigned int pcpu);
#endif /* !(CONFIG_SMP) */

#define get_hard_smp_processor_id(CPU) (paca[(CPU)].hw_cpu_id)
#define set_hard_smp_processor_id(CPU, VAL) \
	do { (paca[(CPU)].hw_cpu_id = (VAL)); } while (0)

extern int smt_enabled_at_boot;

#endif /* __ASSEMBLY__ */

#endif /* !(_PPC64_SMP_H) */
#endif /* __KERNEL__ */
