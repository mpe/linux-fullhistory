/*
 *	linux/arch/alpha/kernel/sys_eb64p.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the EB64+ and EB66.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_apecs.h>
#include <asm/core_lca.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"


static void
eb64p_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16)
		if (irq >= 24)
			outb(mask >> 24, 0x27);
		else
			outb(mask >> 16, 0x26);
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);
	else
		outb(mask, 0x21);
}

static void 
eb64p_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary registers */
	pld = inb(0x26) | (inb(0x27) << 8);
	/*
	 * Now, for every possible bit set, work through
	 * them and call the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1;	/* clear least bit set */

		if (i == 5) {
			isa_device_interrupt(vector, regs);
		} else {
			handle_irq(16 + i, 16 + i, regs);
		}
	}
}

static void __init
eb64p_init_irq(void)
{
#ifdef CONFIG_ALPHA_GENERIC
	/*
	 * CABRIO SRM may not set variation correctly, so here we test
	 * the high word of the interrupt summary register for the RAZ
	 * bits, and hope that a true EB64+ would read all ones...
	 */
	if (inw(0x806) != 0xffff) {
		extern struct alpha_machine_vector cabriolet_mv;
#if 1
		printk("eb64p_init_irq: resetting for CABRIO\n");
#endif
		alpha_mv = cabriolet_mv;
		alpha_mv.init_irq();
		return;
	}
#endif /* GENERIC */

	STANDARD_INIT_IRQ_PROLOG;

	outb(alpha_irq_mask >> 16, 0x26);
	outb(alpha_irq_mask >> 24, 0x27);
	enable_irq(16 + 5);		/* enable SIO cascade */
	enable_irq(2);			/* enable cascade */
}

/*
 * PCI Fixup configuration.
 *
 * There are two 8 bit external summary registers as follows:
 *
 * Summary @ 0x26:
 * Bit      Meaning
 * 0        Interrupt Line A from slot 0
 * 1        Interrupt Line A from slot 1
 * 2        Interrupt Line B from slot 0
 * 3        Interrupt Line B from slot 1
 * 4        Interrupt Line C from slot 0
 * 5        Interrupt line from the two ISA PICs
 * 6        Tulip (slot 
 * 7        NCR SCSI
 *
 * Summary @ 0x27
 * Bit      Meaning
 * 0        Interrupt Line C from slot 1
 * 1        Interrupt Line D from slot 0
 * 2        Interrupt Line D from slot 1
 * 3        RAZ
 * 4        RAZ
 * 5        RAZ
 * 6        RAZ
 * 7        RAZ
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  5       NCR SCSI controller
 *  6       PCI on board slot 0
 *  7       PCI on board slot 1
 *  8       Intel SIO PCI-ISA bridge chip
 *  9       Tulip - DECchip 21040 Ethernet controller
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */

static int __init
eb64p_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[5][5] __initlocaldata = {
		/*INT  INTA  INTB  INTC   INTD */
		{16+7, 16+7, 16+7, 16+7,  16+7},  /* IdSel 5,  slot ?, ?? */
		{16+0, 16+0, 16+2, 16+4,  16+9},  /* IdSel 6,  slot ?, ?? */
		{16+1, 16+1, 16+3, 16+8, 16+10},  /* IdSel 7,  slot ?, ?? */
		{  -1,   -1,   -1,   -1,    -1},  /* IdSel 8,  SIO */
		{16+6, 16+6, 16+6, 16+6,  16+6},  /* IdSel 9,  TULIP */
	};
	const long min_idsel = 5, max_idsel = 9, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static void __init
eb64p_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, APECS_AND_LCA_DEFAULT_MEM_BASE);
	common_pci_fixup(eb64p_map_irq, common_swizzle);
}


/*
 * The System Vector
 */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_EB64P)
struct alpha_machine_vector eb64p_mv __initmv = {
	vector_name:		"EB64+",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_APECS_IO,
	DO_APECS_BUS,
	machine_check:		apecs_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		32,
	irq_probe_mask:		_PROBE_MASK(32),
	update_irq_hw:		eb64p_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	eb64p_device_interrupt,

	init_arch:		apecs_init_arch,
	init_irq:		eb64p_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		eb64p_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(eb64p)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_EB66)
struct alpha_machine_vector eb66_mv __initmv = {
	vector_name:		"EB66",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_LCA_IO,
	DO_LCA_BUS,
	machine_check:		lca_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		32,
	irq_probe_mask:		_PROBE_MASK(32),
	update_irq_hw:		eb64p_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	eb64p_device_interrupt,

	init_arch:		lca_init_arch,
	init_irq:		eb64p_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		eb64p_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(eb66)
#endif
