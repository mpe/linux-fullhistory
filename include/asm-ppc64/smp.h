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

#ifdef CONFIG_SMP

extern void smp_message_pass(int target, int msg, unsigned long data, int wait);
extern void smp_send_tlb_invalidate(int);
extern void smp_send_xmon_break(int cpu);
struct pt_regs;
extern void smp_message_recv(int, struct pt_regs *);


#define smp_processor_id() (get_paca()->xPacaIndex)
#define hard_smp_processor_id() (get_paca()->xHwProcNum)

/*
 * Retrieve the state of a CPU:
 * online:          CPU is in a normal run state
 * possible:        CPU is a candidate to be made online
 * available:       CPU is candidate for the 'possible' pool
 *                  Used to get SMT threads started at boot time.
 * present_at_boot: CPU was available at boot time.  Used in DLPAR
 *                  code to handle special cases for processor start up.
 */
extern cpumask_t cpu_present_at_boot;
extern cpumask_t cpu_online_map;
extern cpumask_t cpu_possible_map;
extern cpumask_t cpu_available_map;

#define cpu_present_at_boot(cpu) cpu_isset(cpu, cpu_present_at_boot)
#define cpu_online(cpu)          cpu_isset(cpu, cpu_online_map) 
#define cpu_possible(cpu)        cpu_isset(cpu, cpu_possible_map) 
#define cpu_available(cpu)       cpu_isset(cpu, cpu_available_map) 

/* Since OpenPIC has only 4 IPIs, we use slightly different message numbers.
 *
 * Make sure this matches openpic_request_IPIs in open_pic.c, or what shows up
 * in /proc/interrupts will be wrong!!! --Troy */
#define PPC_MSG_CALL_FUNCTION   0
#define PPC_MSG_RESCHEDULE      1
#define PPC_MSG_MIGRATE_TASK    2
#define PPC_MSG_XMON_BREAK      3

void smp_init_iSeries(void);
void smp_init_pSeries(void);

#endif /* !(CONFIG_SMP) */
#endif /* __ASSEMBLY__ */

#define get_hard_smp_processor_id(CPU) (paca[(CPU)].xHwProcNum)
#define set_hard_smp_processor_id(CPU, VAL) do { (paca[(CPU)].xHwProcNum = VAL); } while (0)

#endif /* !(_PPC64_SMP_H) */
#endif /* __KERNEL__ */
