/*
 *	linux/arch/alpha/kernel/sys_alcor.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the ALCOR and XLT (XL-300/366/433).
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
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/core_cia.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"


static void 
alcor_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16) {
		/* On Alcor, at least, lines 20..30 are not connected and can
		   generate spurrious interrupts if we turn them on while IRQ
		   probing.  So explicitly mask them out. */
		mask |= 0x7ff000000000UL;

		/* Note inverted sense of mask bits: */
		*(vuip)GRU_INT_MASK = ~(mask >> 16);
		mb();
	}
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);	/* ISA PIC2 */
	else
		outb(mask, 0x21);	/* ISA PIC1 */
}

static void
alcor_ack_irq(unsigned long irq)
{
	if (irq < 16) {
		/* Ack the interrupt making it the lowest priority */
		/*  First the slave .. */
		if (irq > 7) {
			outb(0xE0 | (irq - 8), 0xa0);
			irq = 2;
		}
		/* .. then the master */
		outb(0xE0 | irq, 0x20);

		/* On ALCOR/XLT, need to dismiss interrupt via GRU. */
		*(vuip)GRU_INT_CLEAR = 0x80000000; mb();
		*(vuip)GRU_INT_CLEAR = 0x00000000; mb();
	}
}

static void
alcor_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary register of the GRU */
	pld = (*(vuip)GRU_INT_REQ) & GRU_INT_REQ_BITS;

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i == 31) {
			isa_device_interrupt(vector, regs);
		} else {
			handle_irq(16 + i, 16 + i, regs);
		}
	}
}

static void
alcor_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	if (alpha_using_srm)
		alpha_mv.device_interrupt = srm_device_interrupt;

	*(vuip)GRU_INT_MASK  = ~(alpha_irq_mask >> 16); mb(); /* invert */
	*(vuip)GRU_INT_EDGE  = 0U; mb();		/* all are level */
	*(vuip)GRU_INT_HILO  = 0x80000000U; mb();	/* ISA only HI */
	*(vuip)GRU_INT_CLEAR = 0UL; mb();		/* all clear */

	enable_irq(16 + 31);		/* enable (E)ISA PIC cascade */
	enable_irq(2);			/* enable cascade */
}


/*
 * PCI Fixup configuration.
 *
 * Summary @ GRU_INT_REQ:
 * Bit      Meaning
 * 0        Interrupt Line A from slot 2
 * 1        Interrupt Line B from slot 2
 * 2        Interrupt Line C from slot 2
 * 3        Interrupt Line D from slot 2
 * 4        Interrupt Line A from slot 1
 * 5        Interrupt line B from slot 1
 * 6        Interrupt Line C from slot 1
 * 7        Interrupt Line D from slot 1
 * 8        Interrupt Line A from slot 0
 * 9        Interrupt Line B from slot 0
 *10        Interrupt Line C from slot 0
 *11        Interrupt Line D from slot 0
 *12        Interrupt Line A from slot 4
 *13        Interrupt Line B from slot 4
 *14        Interrupt Line C from slot 4
 *15        Interrupt Line D from slot 4
 *16        Interrupt Line D from slot 3
 *17        Interrupt Line D from slot 3
 *18        Interrupt Line D from slot 3
 *19        Interrupt Line D from slot 3
 *20-30     Reserved
 *31        EISA interrupt
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  6       built-in TULIP (XLT only)
 *  7       PCI on board slot 0
 *  8       PCI on board slot 3
 *  9       PCI on board slot 4
 * 10       PCEB (PCI-EISA bridge)
 * 11       PCI on board slot 2
 * 12       PCI on board slot 1
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */

static int __init
alcor_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[7][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		/* note: IDSEL 17 is XLT only */
		{16+13, 16+13, 16+13, 16+13, 16+13},	/* IdSel 17,  TULIP  */
		{ 16+8,  16+8,  16+9, 16+10, 16+11},	/* IdSel 18,  slot 0 */
		{16+16, 16+16, 16+17, 16+18, 16+19},	/* IdSel 19,  slot 3 */
		{16+12, 16+12, 16+13, 16+14, 16+15},	/* IdSel 20,  slot 4 */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 21,  PCEB   */
		{ 16+0,  16+0,  16+1,  16+2,  16+3},	/* IdSel 22,  slot 2 */
		{ 16+4,  16+4,  16+5,  16+6,  16+7},	/* IdSel 23,  slot 1 */
	};
	const long min_idsel = 6, max_idsel = 12, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static void __init
alcor_pci_fixup(void)
{
	layout_all_busses(EISA_DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(alcor_map_irq, common_swizzle);
}


static void
alcor_kill_arch (int mode, char *reboot_cmd)
{
	/* Who said DEC engineer's have no sense of humor? ;-)  */
	if (alpha_using_srm) {
		*(vuip) GRU_RESET = 0x0000dead;
		mb();
	}

	generic_kill_arch(mode, reboot_cmd);
}


/*
 * The System Vectors
 */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_ALCOR)
struct alpha_machine_vector alcor_mv __initmv = {
	vector_name:		"Alcor",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_CIA_IO,
	DO_CIA_BUS,
	machine_check:		cia_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		48,
	irq_probe_mask:		ALCOR_PROBE_MASK,
	update_irq_hw:		alcor_update_irq_hw,
	ack_irq:		alcor_ack_irq,
	device_interrupt:	alcor_device_interrupt,

	init_arch:		cia_init_arch,
	init_irq:		alcor_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		alcor_pci_fixup,
	kill_arch:		alcor_kill_arch,

	sys: { cia: {
	    gru_int_req_bits:	ALCOR_GRU_INT_REQ_BITS
	}}
};
ALIAS_MV(alcor)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_XLT)
struct alpha_machine_vector xlt_mv __initmv = {
	vector_name:		"XLT",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_CIA_IO,
	DO_CIA_BUS,
	machine_check:		cia_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		48,
	irq_probe_mask:		ALCOR_PROBE_MASK,
	update_irq_hw:		alcor_update_irq_hw,
	ack_irq:		alcor_ack_irq,
	device_interrupt:	alcor_device_interrupt,

	init_arch:		cia_init_arch,
	init_irq:		alcor_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		alcor_pci_fixup,
	kill_arch:		alcor_kill_arch,

	sys: { cia: {
	    gru_int_req_bits:	XLT_GRU_INT_REQ_BITS
	}}
};
ALIAS_MV(xlt)
#endif
