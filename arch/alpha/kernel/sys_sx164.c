/*
 *	linux/arch/alpha/kernel/sys_sx164.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the SX164 (PCA56+PYXIS).
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
#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_pyxis.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"


static void
sx164_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16) {
		/* Make CERTAIN none of the bogus ints get enabled */
		*(vulp)PYXIS_INT_MASK =
			~((long)mask >> 16) & ~0x000000000000003bUL;
		mb();
		/* ... and read it back to make sure it got written.  */
		*(vulp)PYXIS_INT_MASK;
	}
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);	/* ISA PIC2 */
	else
		outb(mask, 0x21);	/* ISA PIC1 */
}

static void
sx164_srm_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16) {
		if (unmask_p)
			cserve_ena(irq - 16);
		else
			cserve_dis(irq - 16);
	}
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);	/* ISA PIC2 */
	else
		outb(mask, 0x21);	/* ISA PIC1 */
}

static void 
sx164_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld, tmp;
	unsigned int i;

	/* Read the interrupt summary register of PYXIS */
	pld = *(vulp)PYXIS_INT_REQ;

	/*
	 * For now, AND off any bits we are not interested in:
	 *  HALT (2), timer (6), ISA Bridge (7)
	 * then all the PCI slots/INTXs (8-23)
	 */
	/* Maybe HALT should only be used for SRM console boots? */
	pld &= 0x0000000000ffffc0UL;

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i == 7) {
			isa_device_interrupt(vector, regs);
		} else if (i == 6) {
			continue;
		} else {
			/* if not timer int */
			handle_irq(16 + i, 16 + i, regs);
		}
		*(vulp)PYXIS_INT_REQ = 1UL << i; mb();
		tmp = *(vulp)PYXIS_INT_REQ;
	}
}

static void
sx164_init_irq(void)
{
	outb(0, DMA1_RESET_REG);
	outb(0, DMA2_RESET_REG);
	outb(DMA_MODE_CASCADE, DMA2_MODE_REG);
	outb(0, DMA2_MASK_REG);

	if (alpha_using_srm) {
		alpha_mv.update_irq_hw = sx164_srm_update_irq_hw;
		alpha_mv.device_interrupt = srm_device_interrupt;
	}
	else {
		/* Note invert on MASK bits. */
		*(vulp)PYXIS_INT_MASK  = ~((long)alpha_irq_mask >> 16);
		mb();
		*(vulp)PYXIS_INT_MASK;
	}

	enable_irq(16 + 6);	/* enable timer */
	enable_irq(16 + 7);	/* enable ISA PIC cascade */
	enable_irq(2);		/* enable cascade */
}

/*
 * PCI Fixup configuration.
 *
 * Summary @ PYXIS_INT_REQ:
 * Bit      Meaning
 * 0        RSVD
 * 1        NMI
 * 2        Halt/Reset switch
 * 3        MBZ
 * 4        RAZ
 * 5        RAZ
 * 6        Interval timer (RTC)
 * 7        PCI-ISA Bridge
 * 8        Interrupt Line A from slot 3
 * 9        Interrupt Line A from slot 2
 *10        Interrupt Line A from slot 1
 *11        Interrupt Line A from slot 0
 *12        Interrupt Line B from slot 3
 *13        Interrupt Line B from slot 2
 *14        Interrupt Line B from slot 1
 *15        Interrupt line B from slot 0
 *16        Interrupt Line C from slot 3
 *17        Interrupt Line C from slot 2
 *18        Interrupt Line C from slot 1
 *19        Interrupt Line C from slot 0
 *20        Interrupt Line D from slot 3
 *21        Interrupt Line D from slot 2
 *22        Interrupt Line D from slot 1
 *23        Interrupt Line D from slot 0
 *
 * IdSel       
 *   5  32 bit PCI option slot 2
 *   6  64 bit PCI option slot 0
 *   7  64 bit PCI option slot 1
 *   8  Cypress I/O
 *   9  32 bit PCI option slot 3
 * 
 */

static int __init
sx164_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[5][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{ 16+ 9, 16+ 9, 16+13, 16+17, 16+21}, /* IdSel 5 slot 2 J17 */
		{ 16+11, 16+11, 16+15, 16+19, 16+23}, /* IdSel 6 slot 0 J19 */
		{ 16+10, 16+10, 16+14, 16+18, 16+22}, /* IdSel 7 slot 1 J18 */
		{    -1,    -1,    -1,	  -1,    -1}, /* IdSel 8 SIO        */
		{ 16+ 8, 16+ 8, 16+12, 16+16, 16+20}  /* IdSel 9 slot 3 J15 */
	};
	const long min_idsel = 5, max_idsel = 9, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

void __init
sx164_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(sx164_map_irq, common_swizzle);
	SMC669_Init();
}


/*
 * The System Vector
 */

struct alpha_machine_vector sx164_mv __initmv = {
	vector_name:		"SX164",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_PYXIS_IO,
	DO_PYXIS_BUS,
	machine_check:		pyxis_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		40,
	irq_probe_mask:		_PROBE_MASK(40),
	update_irq_hw:		sx164_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	sx164_device_interrupt,

	init_arch:		pyxis_init_arch,
	init_irq:		sx164_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		sx164_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(sx164)
