/*
 * Setup pointers to hardware dependand routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997 by Ralf Baechle
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/jazz.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/vector.h>
#include <asm/io.h>

/*
 * Initial irq handlers.
 */
static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2  = { no_action, 0, 0, "cascade", NULL, NULL};

extern asmlinkage void jazz_handle_int(void);
extern asmlinkage void jazz_fd_cacheflush(const void *addr, size_t size);
extern struct feature jazz_feature;

extern void jazz_machine_restart(char *command);
extern void jazz_machine_halt(void);
extern void jazz_machine_power_off(void);

__initfunc(static void jazz_irq_setup(void))
{
        set_except_vector(0, jazz_handle_int);
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE,
			  JAZZ_IE_ETHERNET |
			  JAZZ_IE_SERIAL1  |
			  JAZZ_IE_SERIAL2  |
 			  JAZZ_IE_PARALLEL |
			  JAZZ_IE_FLOPPY);
	r4030_read_reg16(JAZZ_IO_IRQ_SOURCE); /* clear pending IRQs */
	r4030_read_reg32(JAZZ_R4030_INVAL_ADDR); /* clear error bits */
	set_cp0_status(ST0_IM, IE_IRQ4 | IE_IRQ3 | IE_IRQ2 | IE_IRQ1);
	/* set the clock to 100 Hz */
	r4030_write_reg32(JAZZ_TIMER_INTERVAL, 9);
	request_region(0x20, 0x20, "pic1");
	request_region(0xa0, 0x20, "pic2");
	setup_x86_irq(2, &irq2);
}

__initfunc(void jazz_setup(void))
{
	irq_setup = jazz_irq_setup;
	fd_cacheflush = jazz_fd_cacheflush;
	feature = &jazz_feature;			// Will go away
	isa_slot_offset = 0xe3000000;
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
	/* The RTC is outside the port address space */

	_machine_restart = jazz_machine_restart;
	_machine_halt = jazz_machine_halt;
	_machine_power_off = jazz_machine_power_off;
}
