/*
 *	linux/arch/alpha/kernel/sys_sable.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the Sable and Sable-Gamma systems.
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
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_t2.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"


/*
 *   For SABLE, which is really baroque, we manage 40 IRQ's, but the
 *   hardware really only supports 24, not via normal ISA PIC,
 *   but cascaded custom 8259's, etc.
 *	 0-7  (char at 536)
 *	 8-15 (char at 53a)
 *	16-23 (char at 53c)
 */

/* Note that the vector reported by the SRM PALcode corresponds to the
   interrupt mask bits, but we have to manage via more normal IRQs.  */

static struct 
{
	char irq_to_mask[40];
	char mask_to_irq[40];
	unsigned long shadow_mask;
} sable_irq_swizzle = {
	{
		-1,  6, -1,  8, 15, 12,  7,  9,	/* pseudo PIC  0-7  */
		-1, 16, 17, 18,  3, -1, 21, 22,	/* pseudo PIC  8-15 */
		-1, -1, -1, -1, -1, -1, -1, -1,	/* pseudo EISA 0-7  */
		-1, -1, -1, -1, -1, -1, -1, -1,	/* pseudo EISA 8-15 */
		2,  1,  0,  4,  5, -1, -1, -1,	/* pseudo PCI */
	},
	{
		34, 33, 32, 12, 35, 36,  1,  6,	/* mask 0-7  */
		3,  7, -1, -1,  5, -1, -1,  4,	/* mask 8-15  */
		9, 10, 11, -1, -1, 14, 15, -1,	/* mask 16-23  */
	},
	0
};


static void 
sable_update_irq_hw(unsigned long irq, unsigned long unused_mask, int unmask_p)
{
	unsigned long bit, mask;

	/* The "irq" argument is really the irq, but we need it to
	   be the mask bit number.  Convert it now.  */

	irq = sable_irq_swizzle.irq_to_mask[irq];
	bit = 1UL << irq;
	mask = sable_irq_swizzle.shadow_mask | bit;
	if (unmask_p)
		mask &= ~bit;
	sable_irq_swizzle.shadow_mask = mask;

	/* The "irq" argument is now really the mask bit number.  */
	if (irq <= 7)
		outb(mask, 0x537);
	else if (irq <= 15)
		outb(mask >> 8, 0x53b);
	else
		outb(mask >> 16, 0x53d);
}

static void
sable_ack_irq(unsigned long irq)
{
	/* Note that the "irq" here is really the mask bit number */
	switch (irq) {
	case 0 ... 7:
		outb(0xE0 | (irq - 0), 0x536);
		outb(0xE0 | 1, 0x534); /* slave 0 */
		break;
	case 8 ... 15:
		outb(0xE0 | (irq - 8), 0x53a);
		outb(0xE0 | 3, 0x534); /* slave 1 */
		break;
	case 16 ... 24:
		outb(0xE0 | (irq - 16), 0x53c);
		outb(0xE0 | 4, 0x534); /* slave 2 */
		break;
	}
}

static void 
sable_srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	/* Note that the vector reported by the SRM PALcode corresponds
	   to the interrupt mask bits, but we have to manage via more
	   normal IRQs.  */

	int irq, ack;

	ack = irq = (vector - 0x800) >> 4;

	irq = sable_irq_swizzle.mask_to_irq[(ack)];
#if 0
	if (irq == 5 || irq == 9 || irq == 10 || irq == 11 ||
	    irq == 14 || irq == 15)
		printk("srm_device_interrupt: vector=0x%lx  ack=0x%x"
		       "  irq=0x%x\n", vector, ack, irq);
#endif

	handle_irq(irq, ack, regs);
}

static void __init
sable_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	outb(alpha_irq_mask      , 0x537);	/* slave 0 */
	outb(alpha_irq_mask >>  8, 0x53b);	/* slave 1 */
	outb(alpha_irq_mask >> 16, 0x53d);	/* slave 2 */
	outb(0x44, 0x535);		/* enable cascades in master */
}


/*
 * PCI Fixup configuration for ALPHA SABLE (2100) - 2100A is different ??
 *
 * Summary Registers (536/53a/53c):
 * Bit      Meaning
 *-----------------
 * 0        PCI slot 0
 * 1        NCR810 (builtin)
 * 2        TULIP (builtin)
 * 3        mouse
 * 4        PCI slot 1
 * 5        PCI slot 2
 * 6        keyboard
 * 7        floppy
 * 8        COM2
 * 9        parallel port
 *10        EISA irq 3
 *11        EISA irq 4
 *12        EISA irq 5
 *13        EISA irq 6
 *14        EISA irq 7
 *15        COM1
 *16        EISA irq 9
 *17        EISA irq 10
 *18        EISA irq 11
 *19        EISA irq 12
 *20        EISA irq 13
 *21        EISA irq 14
 *22        NC
 *23        IIC
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  0       TULIP
 *  1       SCSI
 *  2       PCI-EISA bridge
 *  3       none
 *  4       none
 *  5       none
 *  6       PCI on board slot 0
 *  7       PCI on board slot 1
 *  8       PCI on board slot 2
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */
/*
 * NOTE: the IRQ assignments below are arbitrary, but need to be consistent
 * with the values in the irq swizzling tables above.
 */

static int __init
sable_map_irq(struct pci_dev *dev, int slot, int pin)
{
        static char irq_tab[9][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{ 32+0,  32+0,  32+0,  32+0,  32+0},  /* IdSel 0,  TULIP  */
		{ 32+1,  32+1,  32+1,  32+1,  32+1},  /* IdSel 1,  SCSI   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 2,  SIO   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 3,  none   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 4,  none   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 5,  none   */
		{ 32+2,  32+2,  32+2,  32+2,  32+2},  /* IdSel 6,  slot 0 */
		{ 32+3,  32+3,  32+3,  32+3,  32+3},  /* IdSel 7,  slot 1 */
		{ 32+4,  32+4,  32+4,  32+4,  32+4},  /* IdSel 8,  slot 2 */
        };
	const long min_idsel = 0, max_idsel = 8, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

void __init
sable_pci_fixup(void)
{
	layout_all_busses(EISA_DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
        common_pci_fixup(sable_map_irq, common_swizzle);
}


/*
 * The System Vectors
 *
 * In order that T2_HAE_ADDRESS should be a constant, we play
 * these games with GAMMA_BIAS.
 */

#if defined(CONFIG_ALPHA_GENERIC) || !defined(CONFIG_ALPHA_GAMMA)
#undef GAMMA_BIAS
#define GAMMA_BIAS 0
struct alpha_machine_vector sable_mv __initmv = {
	vector_name:		"Sable",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_T2_IO,
	DO_T2_BUS,
	machine_check:		t2_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		40,
	irq_probe_mask:		_PROBE_MASK(40),
	update_irq_hw:		sable_update_irq_hw,
	ack_irq:		sable_ack_irq,
	device_interrupt:	sable_srm_device_interrupt,

	init_arch:		t2_init_arch,
	init_irq:		sable_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		sable_pci_fixup,
	kill_arch:		generic_kill_arch,

	sys: { t2: {
	    gamma_bias:		0
	} }
};
ALIAS_MV(sable)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_GAMMA)
#undef GAMMA_BIAS
#define GAMMA_BIAS _GAMMA_BIAS
struct alpha_machine_vector sable_gamma_mv __initmv = {
	vector_name:		"Sable-Gamma",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_T2_IO,
	DO_T2_BUS,
	machine_check:		t2_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		40,
	irq_probe_mask:		_PROBE_MASK(40),
	update_irq_hw:		sable_update_irq_hw,
	ack_irq:		sable_ack_irq,
	device_interrupt:	sable_srm_device_interrupt,

	init_arch:		t2_init_arch,
	init_irq:		sable_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		sable_pci_fixup,
	kill_arch:		generic_kill_arch,

	sys: { t2: {
	    gamma_bias:		_GAMMA_BIAS
	} }
};
ALIAS_MV(sable_gamma)
#endif
