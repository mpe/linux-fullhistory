/*
 *	linux/arch/alpha/kernel/sys_dp264.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the DP264 (EV6+TSUNAMI).
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
#include <asm/pci.h>
#include <asm/pgtable.h>
#include <asm/core_tsunami.h>
#include <asm/hwrpb.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"

#define dev2hose(d) (bus2hose[(d)->bus->number]->pci_hose_index)

/*
 * HACK ALERT! only CPU#0 is used currently
 */

static void
dp264_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16) {
		volatile unsigned long *csr;

		if (TSUNAMI_bootcpu < 2)
			if (!TSUNAMI_bootcpu)
				csr = &TSUNAMI_cchip->dim0.csr;
			else
				csr = &TSUNAMI_cchip->dim1.csr;
		else
			if (TSUNAMI_bootcpu == 2)
				csr = &TSUNAMI_cchip->dim2.csr;
			else
				csr = &TSUNAMI_cchip->dim3.csr;
		
		*csr = ~mask;
		mb();
		*csr;
	}
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);	/* ISA PIC2 */
	else
		outb(mask, 0x21);	/* ISA PIC1 */
}

static void
dp264_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
#if 1
	printk("dp264_device_interrupt: NOT IMPLEMENTED YET!! \n");
#else
        unsigned long pld;
        unsigned int i;

        /* Read the interrupt summary register of TSUNAMI */
        pld = TSUNAMI_cchip->dir0.csr;

        /*
         * Now for every possible bit set, work through them and call
         * the appropriate interrupt handler.
         */
        while (pld) {
                i = ffz(~pld);
                pld &= pld - 1; /* clear least bit set */
                if (i == 55) {
                        isa_device_interrupt(vector, regs);
		} else { /* if not timer int */
                        handle_irq(16 + i, 16 + i, regs);
                }
#if 0
		TSUNAMI_cchip->dir0.csr = 1UL << i; mb();
		tmp = TSUNAMI_cchip->dir0.csr;
#endif
        }
#endif
}

static void 
dp264_srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq, ack;

	ack = irq = (vector - 0x800) >> 4;

        /*
         * The DP264 SRM console reports PCI interrupts with a vector
	 * 0x100 *higher* than one might expect, as PCI IRQ 0 (ie bit 0)
	 * shows up as IRQ 16, etc, etc. We adjust it down by 16 to have
	 * it line up with the actual bit numbers from the DIM registers,
	 * which is how we manage the interrupts/mask. Sigh...
         */
        if (irq >= 32)
                ack = irq = irq - 16;

	handle_irq(irq, ack, regs);
}

static void __init
dp264_init_irq(void)
{
	volatile unsigned long *csr;

	outb(0, DMA1_RESET_REG);
	outb(0, DMA2_RESET_REG);
	outb(DMA_MODE_CASCADE, DMA2_MODE_REG);
	outb(0, DMA2_MASK_REG);

	if (alpha_using_srm)
		alpha_mv.device_interrupt = dp264_srm_device_interrupt;

	if (TSUNAMI_bootcpu < 2)
		if (!TSUNAMI_bootcpu)
			csr = &TSUNAMI_cchip->dim0.csr;
		else
			csr = &TSUNAMI_cchip->dim1.csr;
	else
		if (TSUNAMI_bootcpu == 2)
			csr = &TSUNAMI_cchip->dim2.csr;
		else
			csr = &TSUNAMI_cchip->dim3.csr;
		
	/* Note invert on MASK bits.  */
        *csr = ~(alpha_irq_mask);
	mb();
        *csr;

        enable_irq(55);     /* Enable CYPRESS interrupt controller (ISA).  */
	enable_irq(2);
}


/*
 * PCI Fixup configuration.
 *
 * Summary @ TSUNAMI_CSR_DIM0:
 * Bit      Meaning
 * 0-17     Unused
 *18        Interrupt SCSI B (Adaptec 7895 builtin)
 *19        Interrupt SCSI A (Adaptec 7895 builtin)
 *20        Interrupt Line D from slot 2 PCI0
 *21        Interrupt Line C from slot 2 PCI0
 *22        Interrupt Line B from slot 2 PCI0
 *23        Interrupt Line A from slot 2 PCI0
 *24        Interrupt Line D from slot 1 PCI0
 *25        Interrupt Line C from slot 1 PCI0
 *26        Interrupt Line B from slot 1 PCI0
 *27        Interrupt Line A from slot 1 PCI0
 *28        Interrupt Line D from slot 0 PCI0
 *29        Interrupt Line C from slot 0 PCI0
 *30        Interrupt Line B from slot 0 PCI0
 *31        Interrupt Line A from slot 0 PCI0
 *
 *32        Interrupt Line D from slot 3 PCI1
 *33        Interrupt Line C from slot 3 PCI1
 *34        Interrupt Line B from slot 3 PCI1
 *35        Interrupt Line A from slot 3 PCI1
 *36        Interrupt Line D from slot 2 PCI1
 *37        Interrupt Line C from slot 2 PCI1
 *38        Interrupt Line B from slot 2 PCI1
 *39        Interrupt Line A from slot 2 PCI1
 *40        Interrupt Line D from slot 1 PCI1
 *41        Interrupt Line C from slot 1 PCI1
 *42        Interrupt Line B from slot 1 PCI1
 *43        Interrupt Line A from slot 1 PCI1
 *44        Interrupt Line D from slot 0 PCI1
 *45        Interrupt Line C from slot 0 PCI1
 *46        Interrupt Line B from slot 0 PCI1
 *47        Interrupt Line A from slot 0 PCI1
 *48-52     Unused
 *53        PCI0 NMI (from Cypress)
 *54        PCI0 SMI INT (from Cypress)
 *55        PCI0 ISA Interrupt (from Cypress)
 *56-60     Unused
 *61        PCI1 Bus Error
 *62        PCI0 Bus Error
 *63        Reserved
 *
 * IdSel	
 *   5	 Cypress Bridge I/O
 *   6	 SCSI Adaptec builtin
 *   7	 64 bit PCI option slot 0 (all busses)
 *   8	 64 bit PCI option slot 1 (all busses)
 *   9	 64 bit PCI option slot 2 (all busses)
 *  10	 64 bit PCI option slot 3 (not bus 0)
 */

static int __init
dp264_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[6][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 5 ISA Bridge */
		{ 16+ 3, 16+ 3, 16+ 2, 16+ 2, 16+ 2}, /* IdSel 6 SCSI builtin*/
		{ 16+15, 16+15, 16+14, 16+13, 16+12}, /* IdSel 7 slot 0 */
		{ 16+11, 16+11, 16+10, 16+ 9, 16+ 8}, /* IdSel 8 slot 1 */
		{ 16+ 7, 16+ 7, 16+ 6, 16+ 5, 16+ 4}, /* IdSel 9 slot 2 */
		{ 16+ 3, 16+ 3, 16+ 2, 16+ 1, 16+ 0}  /* IdSel 10 slot 3 */
	};
	const long min_idsel = 5, max_idsel = 10, irqs_per_slot = 5;
	int irq = COMMON_TABLE_LOOKUP;

	if (irq >= 0)
		irq += 16 * dev2hose(dev);

	return irq;
}

static int __init
monet_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[13][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{    45,    45,    45,    45,    45}, /* IdSel 3 21143 PCI1 */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 4 unused */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 5 unused */
		{    47,    47,    47,    47,    47}, /* IdSel 6 SCSI PCI1 */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 7 ISA Bridge */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 8 P2P PCI1 */
#if 1
		{    28,    28,    29,    30,    31}, /* IdSel 14 slot 4 PCI2*/
		{    24,    24,    25,    26,    27}, /* IdSel 15 slot 5 PCI2*/
#else
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 9 unused */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 10 unused */
#endif
		{    40,    40,    41,    42,    43}, /* IdSel 11 slot 1 PCI0*/
		{    36,    36,    37,    38,    39}, /* IdSel 12 slot 2 PCI0*/
		{    32,    32,    33,    34,    35}, /* IdSel 13 slot 3 PCI0*/
		{    28,    28,    29,    30,    31}, /* IdSel 14 slot 4 PCI2*/
		{    24,    24,    25,    26,    27}  /* IdSel 15 slot 5 PCI2*/
};
	const long min_idsel = 3, max_idsel = 15, irqs_per_slot = 5;
	int irq = COMMON_TABLE_LOOKUP;

	return irq;
}

static int __init
monet_swizzle(struct pci_dev *dev, int *pinp)
{
        int slot, pin = *pinp;

        /* Check first for the built-in bridge on hose 1. */
        if (dev2hose(dev) == 1 && PCI_SLOT(dev->bus->self->devfn) == 8) {
	  slot = PCI_SLOT(dev->devfn);
        }
        else
        {
                /* Must be a card-based bridge.  */
                do {
			/* Check for built-in bridge on hose 1. */
                        if (dev2hose(dev) == 1 &&
			    PCI_SLOT(dev->bus->self->devfn) == 8) {
				slot = PCI_SLOT(dev->devfn);
				break;
                        }
                        pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn)) ;

                        /* Move up the chain of bridges.  */
                        dev = dev->bus->self;
                        /* Slot of the next bridge.  */
                        slot = PCI_SLOT(dev->devfn);
                } while (dev->bus->self);
        }
        *pinp = pin;
        return slot;
}

static int __init
webbrick_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[13][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 7 ISA Bridge */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 8 unused */
		{    29,    29,    29,    29,    29}, /* IdSel 9 21143 #1 */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 10 unused */
		{    30,    30,    30,    30,    30}, /* IdSel 11 21143 #2 */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 12 unused */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 13 unused */
		{    47,    47,    46,    45,    44}, /* IdSel 14 slot 0 */
		{    39,    39,    38,    37,    36}, /* IdSel 15 slot 1 */
		{    43,    43,    42,    41,    40}, /* IdSel 16 slot 2 */
		{    35,    35,    34,    33,    32}, /* IdSel 17 slot 3 */
};
	const long min_idsel = 7, max_idsel = 17, irqs_per_slot = 5;
	int irq = COMMON_TABLE_LOOKUP;

	return irq;
}

static void __init
dp264_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(dp264_map_irq, common_swizzle);
	SMC669_Init();
}

static void __init
monet_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(monet_map_irq, monet_swizzle);
	/* es1888_init(); */ /* later? */
	SMC669_Init();
}

static void __init
webbrick_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(webbrick_map_irq, common_swizzle);
	SMC669_Init();
}


/*
 * The System Vectors
 */

struct alpha_machine_vector dp264_mv __initmv = {
	vector_name:		"DP264",
	DO_EV6_MMU,
	DO_DEFAULT_RTC,
	DO_TSUNAMI_IO,
	DO_TSUNAMI_BUS,
	machine_check:		tsunami_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		64,
	irq_probe_mask:		_PROBE_MASK(64),
	update_irq_hw:		dp264_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	dp264_device_interrupt,

	init_arch:		tsunami_init_arch,
	init_irq:		dp264_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		dp264_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(dp264)

struct alpha_machine_vector monet_mv __initmv = {
	vector_name:		"Monet",
	DO_EV6_MMU,
	DO_DEFAULT_RTC,
	DO_TSUNAMI_IO,
	DO_TSUNAMI_BUS,
	machine_check:		tsunami_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		64,
	irq_probe_mask:		_PROBE_MASK(64),
	update_irq_hw:		dp264_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	dp264_device_interrupt,

	init_arch:		tsunami_init_arch,
	init_irq:		dp264_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		monet_pci_fixup,
	kill_arch:		generic_kill_arch,
};

struct alpha_machine_vector webbrick_mv __initmv = {
	vector_name:		"Webbrick",
	DO_EV6_MMU,
	DO_DEFAULT_RTC,
	DO_TSUNAMI_IO,
	DO_TSUNAMI_BUS,
	machine_check:		tsunami_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		64,
	irq_probe_mask:		_PROBE_MASK(64),
	update_irq_hw:		dp264_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	dp264_device_interrupt,

	init_arch:		tsunami_init_arch,
	init_irq:		dp264_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		webbrick_pci_fixup,
	kill_arch:		generic_kill_arch,
};
/* No alpha_mv alias for webbrick, since we compile it in unconditionally
   with DP264; setup_arch knows how to cope.  */
