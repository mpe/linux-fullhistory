/*
 *	linux/arch/alpha/kernel/sys_takara.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the TAKARA.
 */

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
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_cia.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"


/*
 * WARNING WARNING WARNING
 *
 * This port is missing an update_irq_hw implementation.
 */

static void
takara_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned intstatus;

	/*
	 * The PALcode will have passed us vectors 0x800 or 0x810,
	 * which are fairly arbitrary values and serve only to tell
	 * us whether an interrupt has come in on IRQ0 or IRQ1. If
	 * it's IRQ1 it's a PCI interrupt; if it's IRQ0, it's
	 * probably ISA, but PCI interrupts can come through IRQ0
	 * as well if the interrupt controller isn't in accelerated
	 * mode.
	 *
	 * OTOH, the accelerator thing doesn't seem to be working
	 * overly well, so what we'll do instead is try directly
	 * examining the Master Interrupt Register to see if it's a
	 * PCI interrupt, and if _not_ then we'll pass it on to the
	 * ISA handler.
	 */

	intstatus = inw(0x500) & 15;
	if (intstatus) {
		/*
		 * This is a PCI interrupt. Check each bit and
		 * despatch an interrupt if it's set.
		 */

		if (intstatus & 8) handle_irq(16+3, 16+3, regs);
		if (intstatus & 4) handle_irq(16+2, 16+2, regs);
		if (intstatus & 2) handle_irq(16+1, 16+1, regs);
		if (intstatus & 1) handle_irq(16+0, 16+0, regs);
	} else
		isa_device_interrupt (vector, regs);
}

static void __init
takara_init_irq(void)
{
	unsigned int ctlreg;

	STANDARD_INIT_IRQ_PROLOG;

	ctlreg = inl(0x500);
	ctlreg &= ~0x8000;     /* return to non-accelerated mode */
	outw(ctlreg >> 16, 0x502);
	outw(ctlreg & 0xFFFF, 0x500);
	ctlreg = 0x05107c00;   /* enable the PCI interrupt register */
	outw(ctlreg >> 16, 0x502);
	outw(ctlreg & 0xFFFF, 0x500);
	enable_irq(2);
}


/*
 * The Takara has PCI devices 1, 2, and 3 configured to slots 20,
 * 19, and 18 respectively, in the default configuration. They can
 * also be jumpered to slots 8, 7, and 6 respectively, which is fun
 * because the SIO ISA bridge can also be slot 7. However, the SIO
 * doesn't explicitly generate PCI-type interrupts, so we can
 * assign it whatever the hell IRQ we like and it doesn't matter.
 */

static int __init
takara_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[15][5] __initlocaldata = {
		{ 16+3, 16+3, 16+3, 16+3, 16+3},   /* slot  6 == device 3 */
		{ 16+2, 16+2, 16+2, 16+2, 16+2},   /* slot  7 == device 2 */
		{ 16+1, 16+1, 16+1, 16+1, 16+1},   /* slot  8 == device 1 */
		{   -1,   -1,   -1,   -1,   -1},   /* slot  9 == nothing */
		{   -1,   -1,   -1,   -1,   -1},   /* slot 10 == nothing */
		{   -1,   -1,   -1,   -1,   -1},   /* slot 11 == nothing */
		{   -1,   -1,   -1,   -1,   -1},   /* slot 12 == nothing */
		{   -1,   -1,   -1,   -1,   -1},   /* slot 13 == nothing */
		{   -1,   -1,   -1,   -1,   -1},   /* slot 14 == nothing */
		{   -1,   -1,   -1,   -1,   -1},   /* slot 15 == nothing */
		{   -1,   -1,   -1,   -1,   -1},   /* slot 16 == nothing */
		{   -1,   -1,   -1,   -1,   -1},   /* slot 17 == nothing */
		{ 16+3, 16+3, 16+3, 16+3, 16+3},   /* slot 18 == device 3 */
		{ 16+2, 16+2, 16+2, 16+2, 16+2},   /* slot 19 == device 2 */
		{ 16+1, 16+1, 16+1, 16+1, 16+1},   /* slot 20 == device 1 */
	};
	const long min_idsel = 6, max_idsel = 20, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static void __init
takara_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(takara_map_irq, common_swizzle);
	enable_ide(0x26e);
}


/*
 * The System Vector
 */

struct alpha_machine_vector takara_mv __initmv = {
	vector_name:		"Takara",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_CIA_IO,
	DO_CIA_BUS,
	machine_check:		cia_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		20,
	irq_probe_mask:		_PROBE_MASK(20),
	update_irq_hw:		NULL,
	ack_irq:		generic_ack_irq,
	device_interrupt:	takara_device_interrupt,

	init_arch:		cia_init_arch,
	init_irq:		takara_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		takara_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(takara)
