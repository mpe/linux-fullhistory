/*
 *	linux/arch/alpha/kernel/sys_jensen.c
 *
 *	Copyright (C) 1995 Linus Torvalds
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the Jensen.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/jensen.h>
#undef  __EXTERN_INLINE

#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>

#include "proto.h"
#include "irq.h"
#include "machvec.h"


static void
jensen_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 8)
		outb(mask >> 8, 0xA1);
	else
		outb(mask, 0x21);
}

/*
 * Jensen is special: the vector is 0x8X0 for EISA interrupt X, and
 * 0x9X0 for the local motherboard interrupts..
 *
 *	0x660 - NMI
 *
 *	0x800 - IRQ0  interval timer (not used, as we use the RTC timer)
 *	0x810 - IRQ1  line printer (duh..)
 *	0x860 - IRQ6  floppy disk
 *	0x8E0 - IRQ14 SCSI controller
 *
 *	0x900 - COM1
 *	0x920 - COM2
 *	0x980 - keyboard
 *	0x990 - mouse
 *
 * PCI-based systems are more sane: they don't have the local
 * interrupts at all, and have only normal PCI interrupts from
 * devices.  Happily it's easy enough to do a sane mapping from the
 * Jensen..  Note that this means that we may have to do a hardware
 * "ack" to a different interrupt than we report to the rest of the
 * world.
 */

static void
handle_nmi(struct pt_regs * regs)
{
	printk("Whee.. NMI received. Probable hardware error\n");
	printk("61=%02x, 461=%02x\n", inb(0x61), inb(0x461));
}

static void 
jensen_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq, ack;

	ack = irq = (vector - 0x800) >> 4;

	switch (vector) {
	case 0x660: handle_nmi(regs); return;

	/* local device interrupts: */
	case 0x900: irq = 4, ack = -1; break;		/* com1 -> irq 4 */
	case 0x920: irq = 3, ack = -1; break;		/* com2 -> irq 3 */
	case 0x980: irq = 1, ack = -1; break;		/* kbd -> irq 1 */
	case 0x990: irq = 9, ack = -1; break;		/* mouse -> irq 9 */
	default:
		if (vector > 0x900) {
			printk("Unknown local interrupt %lx\n", vector);
		}

		/* irq1 is supposed to be the keyboard, silly Jensen
		   (is this really needed??) */
		if (irq == 1)
			irq = 7;
		break;
	}

	handle_irq(irq, ack, regs);
}

static void
jensen_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	enable_irq(2);			/* enable cascade */
}

static void
jensen_machine_check (u64 vector, u64 la, struct pt_regs *regs)
{
	printk(KERN_CRIT "Machine check\n");
}


/*
 * The System Vector
 */

struct alpha_machine_vector jensen_mv __initmv = {
	vector_name:		"Jensen",
	DO_EV4_MMU,
	IO_LITE(JENSEN,jensen,jensen),
	BUS(jensen),
	machine_check:		jensen_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,
	rtc_port: 0x170,

	nr_irqs:		16,
	irq_probe_mask:		_PROBE_MASK(16),
	update_irq_hw:		jensen_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	jensen_device_interrupt,

	init_arch:		NULL,
	init_irq:		jensen_init_irq,
	init_pit:		generic_init_pit,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(jensen)
