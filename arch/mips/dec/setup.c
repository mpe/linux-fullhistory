/*
 * Setup pointers to hardware-dependent routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by Ralf Baechle
 */
#include <asm/ptrace.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/vector.h>

extern struct feature decstation_feature;

static void
dec_irq_setup(void)
{
	/* FIXME: should set up the clock as per above? */
	pmax_printf("Please write the IRQ setup code for the DECStation!\n");
}

void (*board_time_init)(struct irqaction *irq);

static void dec_time_init(struct irqaction *irq)
{
	pmax_printf("Please write the time init code for the DECStation!\n");
}

extern void dec_machine_restart(char *command);
extern void dec_machine_halt(void);
extern void dec_machine_power_off(void).

void
decstation_setup(void)
{
	irq_setup = dec_irq_setup;
	board_time_init = dec_time_init;
	/* FIXME: Setup fd_cacheflush */
	feature = &decstation_feature;		/* FIXME: Will go away */

	_machine_restart = dec_machine_restart;
	_machine_halt = dec_machine_halt;
	_machine_power_off = dec_machine_power_off;
}
