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
#include <asm/pgtable.h>
#include <asm/core_tsunami.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"


/*
 * HACK ALERT! only CPU#0 is used currently
 */

static void
dp264_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16) {
		/* Make CERTAIN none of the bogus ints get enabled */
		*(vulp)TSUNAMI_CSR_DIM0 =
			~(mask) & ~0x0000000000000000UL;
		mb();
		/* ... and read it back to make sure it got written.  */
		*(vulp)TSUNAMI_CSR_DIM0;
	}
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);	/* ISA PIC2 */
	else
		outb(mask, 0x21);	/* ISA PIC1 */
}

static void
dp264_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
        unsigned long pld;
        unsigned int i;
        unsigned long flags;

	__save_and_cli(flags);

        /* Read the interrupt summary register of TSUNAMI */
        pld = (*(vulp)TSUNAMI_CSR_DIR0);

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
		*(vulp)TSUNAMI_CSR_DIR0 = 1UL << i; mb();
		tmp = *(vulp)TSUNAMI_CSR_DIR0;
#endif
        }
	__restore_flags(flags);
}

static void 
dp264_srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq, ack;
	unsigned long flags;

	__save_and_cli(flags);
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
	__restore_flags(flags);
}

static void __init
dp264_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	if (alpha_using_srm)
		alpha_mv.device_interrupt = dp264_srm_device_interrupt;

	/* Note invert on MASK bits.  */
        *(vulp)TSUNAMI_CSR_DIM0 = ~(alpha_irq_mask) & ~0UL; mb();
        *(vulp)TSUNAMI_CSR_DIM0;

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
 *   7	 64 bit PCI option slot 0
 *   8	 64 bit PCI option slot 1
 *   9	 64 bit PCI option slot 2
 * 
 */

static int __init
dp264_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[5][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{    -1,    -1,    -1,    -1,    -1}, /* IdSel 5 ISA Bridge */
		{ 16+ 2, 16+ 2, 16+ 2, 16+ 2, 16+ 2}, /* IdSel 6 SCSI builtin*/
		{ 16+15, 16+15, 16+14, 16+13, 16+12}, /* IdSel 7 slot 0 */
		{ 16+11, 16+11, 16+10, 16+ 9, 16+ 8}, /* IdSel 8 slot 1 */
		{ 16+ 7, 16+ 7, 16+ 6, 16+ 5, 16+ 4}  /* IdSel 9 slot 2 */
	};
	const long min_idsel = 5, max_idsel = 9, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static void __init
dp264_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(dp264_map_irq, common_swizzle);
	SMC669_Init();
}


/*
 * The System Vector
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
