/*
 *	linux/arch/alpha/kernel/sys_rx164.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the RX164 (PCA56+POLARIS).
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
#include <asm/core_polaris.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"


static void
rx164_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16) {
		unsigned int temp;
		pcibios_write_config_dword(0, 0, 0x74, ~mask >> 16);
		pcibios_read_config_dword(0, 0, 0x74, &temp);
	}
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);	/* ISA PIC2 */
	else
		outb(mask, 0x21);	/* ISA PIC1 */
}

static void
rx164_srm_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
#if 0
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
#endif
}

static void
rx164_isa_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
        unsigned long pic;

        /*
         * It seems to me that the probability of two or more *device*
         * interrupts occurring at almost exactly the same time is
         * pretty low.  So why pay the price of checking for
         * additional interrupts here if the common case can be
         * handled so much easier?
         */
        /* 
         *  The first read of the PIC gives you *all* interrupting lines.
         *  Therefore, read the mask register and and out those lines
         *  not enabled.  Note that some documentation has 21 and a1 
         *  write only.  This is not true.
         */
        pic = inb(0x20) | (inb(0xA0) << 8);     /* read isr */
        pic &= ~alpha_irq_mask;                 /* apply mask */
        pic &= 0xFFFB;                          /* mask out cascade & hibits */

        while (pic) {
                int j = ffz(~pic);
                pic &= pic - 1;
                handle_irq(j, j, regs);
        }
}

static void 
rx164_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld;
	int i;

        /* Read the interrupt summary register.  On Polaris,
         * this is the DIRR register in PCI config space (offset 0x84)
         */
        pld = 0;
        pcibios_read_config_dword(0, 0, 0x84, (unsigned int *)&pld);

#if 0
        printk("PLD 0x%lx\n", pld);
#endif

        if (pld & 0xffffffff00000000UL) pld &= 0x00000000ffffffffUL;

        /*
         * Now for every possible bit set, work through them and call
         * the appropriate interrupt handler.
         */
        while (pld) {
                i = ffz(~pld);
                pld &= pld - 1; /* clear least bit set */
                if (i == 20) {
                        rx164_isa_device_interrupt(vector, regs);
                } else {
                        handle_irq(16+i, 16+i, regs);
                }
        }
}

static void
rx164_init_irq(void)
{
	unsigned int temp;

        STANDARD_INIT_IRQ_PROLOG;

        pcibios_write_config_dword(0, 0, 0x74, (~alpha_irq_mask >> 16));
        pcibios_read_config_dword(0, 0, 0x74, &temp);

        enable_irq(16 + 20);    /* enable ISA interrupts */
	enable_irq(2);		/* enable cascade */
}
/* The RX164 changed its interrupt routing between pass1 and pass2...
 *
 * PASS1:
 *
 *      Slot    IDSEL   INTA    INTB    INTC    INTD    
 *      0       6       5       10      15      20
 *      1       7       4       9       14      19
 *      2       5       3       8       13      18
 *      3       9       2       7       12      17
 *      4       10      1       6       11      16
 *
 * PASS2:
 *      Slot    IDSEL   INTA    INTB    INTC    INTD    
 *      0       5       1       7       12      17
 *      1       6       2       8       13      18
 *      2       8       3       9       14      19
 *      3       9       4       10      15      20
 *      4       10      5       11      16      6
 *      
 */

/*
 * IdSel       
 *   5  32 bit PCI option slot 0
 *   6  64 bit PCI option slot 1
 *   7  PCI-ISA bridge
 *   7  64 bit PCI option slot 2
 *   9  32 bit PCI option slot 3
 *  10  PCI-PCI bridge
 * 
 */

static int __init
rx164_map_irq(struct pci_dev *dev, int slot, int pin)
{
#if 0
        char irq_tab_pass1[6][5] = {
          /*INT   INTA  INTB  INTC   INTD */
          { 16+3, 16+3, 16+8, 16+13, 16+18},      /* IdSel 5,  slot 2 */
          { 16+5, 16+5, 16+10, 16+15, 16+20},     /* IdSel 6,  slot 0 */
          { 16+4, 16+4, 16+9, 16+14, 16+19},      /* IdSel 7,  slot 1 */
          { -1,     -1,    -1,    -1,   -1},      /* IdSel 8, PCI/ISA bridge */
          { 16+2, 16+2, 16+7, 16+12, 16+17},      /* IdSel 9,  slot 3 */
          { 16+1, 16+1, 16+6, 16+11, 16+16},      /* IdSel 10, slot 4 */
        };
#endif
        char irq_tab[6][5] = {
          /*INT   INTA  INTB  INTC   INTD */
          { 16+0, 16+0, 16+6, 16+11, 16+16},      /* IdSel 5,  slot 0 */
          { 16+1, 16+1, 16+7, 16+12, 16+17},      /* IdSel 6,  slot 1 */
          { -1,     -1,    -1,    -1,   -1},      /* IdSel 7, PCI/ISA bridge */
          { 16+2, 16+2, 16+8, 16+13, 16+18},      /* IdSel 8,  slot 2 */
          { 16+3, 16+3, 16+9, 16+14, 16+19},      /* IdSel 9,  slot 3 */
          { 16+4, 16+4, 16+10, 16+15, 16+5},      /* IdSel 10, PCI-PCI */
        };
	const long min_idsel = 5, max_idsel = 10, irqs_per_slot = 5;
        /* JRP - Need to figure out how to distinguish pass1 from pass2,
         * and use the correct table...
         */
	return COMMON_TABLE_LOOKUP;
}

void __init
rx164_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(rx164_map_irq, common_swizzle);
}


/*
 * The System Vector
 */

struct alpha_machine_vector rx164_mv __initmv = {
	vector_name:		"RX164",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_POLARIS_IO,
	DO_POLARIS_BUS,
	machine_check:		polaris_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		40,
	irq_probe_mask:		_PROBE_MASK(40),
	update_irq_hw:		rx164_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	rx164_device_interrupt,

	init_arch:		polaris_init_arch,
	init_irq:		rx164_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		rx164_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(rx164)
