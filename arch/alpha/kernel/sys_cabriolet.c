/*
 *	linux/arch/alpha/kernel/sys_cabriolet.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the Cabriolet (AlphaPC64), EB66+, and EB164,
 * PC164 and LX164.
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
#include <asm/core_cia.h>
#include <asm/core_lca.h>
#include <asm/core_pyxis.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"


static void
cabriolet_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16)
		outl(alpha_irq_mask >> 16, 0x804);
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);
	else
		outb(mask, 0x21);
}


/* Under SRM console, we must use the CSERVE PALcode routine to manage
   the interrupt mask for us.  Otherwise, the kernel/HW get out of
   sync with what the PALcode thinks it needs to deliver/ignore.  */

static void
cabriolet_srm_update_irq_hw(unsigned long irq, unsigned long mask, int unmaskp)
{
	if (irq >= 16) {
		if (unmaskp)
			cserve_ena(irq - 16);
		else
			cserve_dis(irq - 16);
	}
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);
	else
		outb(mask, 0x21);
}

static void 
cabriolet_device_interrupt(unsigned long v, struct pt_regs *r)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary registers */
	pld = inb(0x804) | (inb(0x805) << 8) | (inb(0x806) << 16);

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1;	/* clear least bit set */
		if (i == 4) {
			isa_device_interrupt(v, r);
		} else {
			handle_irq(16 + i, 16 + i, r);
		}
	}
}

static void
cabriolet_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	if (alpha_using_srm) {
		alpha_mv.update_irq_hw = cabriolet_srm_update_irq_hw;
		alpha_mv.device_interrupt = srm_device_interrupt;
	}
	else {
		outl(alpha_irq_mask >> 16, 0x804);
	}

	enable_irq(16 + 4);		/* enable SIO cascade */
	enable_irq(2);			/* enable cascade */
}


/*
 * The EB66+ is very similar to the EB66 except that it does not have
 * the on-board NCR and Tulip chips.  In the code below, I have used
 * slot number to refer to the id select line and *not* the slot
 * number used in the EB66+ documentation.  However, in the table,
 * I've given the slot number, the id select line and the Jxx number
 * that's printed on the board.  The interrupt pins from the PCI slots
 * are wired into 3 interrupt summary registers at 0x804, 0x805 and
 * 0x806 ISA.
 *
 * In the table, -1 means don't assign an IRQ number.  This is usually
 * because it is the Saturn IO (SIO) PCI/ISA Bridge Chip.
 */

static inline int __init
eb66p_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[5][5] __initlocaldata = {
		/*INT  INTA  INTB  INTC   INTD */
		{16+0, 16+0, 16+5,  16+9, 16+13},  /* IdSel 6,  slot 0, J25 */
		{16+1, 16+1, 16+6, 16+10, 16+14},  /* IdSel 7,  slot 1, J26 */
		{  -1,   -1,   -1,    -1,    -1},  /* IdSel 8,  SIO         */
		{16+2, 16+2, 16+7, 16+11, 16+15},  /* IdSel 9,  slot 2, J27 */
		{16+3, 16+3, 16+8, 16+12,  16+6}   /* IdSel 10, slot 3, J28 */
	};
	const long min_idsel = 6, max_idsel = 10, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static inline void __init
eb66p_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, APECS_AND_LCA_DEFAULT_MEM_BASE);
	common_pci_fixup(eb66p_map_irq, common_swizzle);
	enable_ide(0x398);
}


/*
 * The AlphaPC64 is very similar to the EB66+ except that its slots
 * are numbered differently.  In the code below, I have used slot
 * number to refer to the id select line and *not* the slot number
 * used in the AlphaPC64 documentation.  However, in the table, I've
 * given the slot number, the id select line and the Jxx number that's
 * printed on the board.  The interrupt pins from the PCI slots are
 * wired into 3 interrupt summary registers at 0x804, 0x805 and 0x806
 * ISA.
 *
 * In the table, -1 means don't assign an IRQ number.  This is usually
 * because it is the Saturn IO (SIO) PCI/ISA Bridge Chip.
 */

static inline int __init
cabriolet_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[5][5] __initlocaldata = {
		/*INT   INTA  INTB  INTC   INTD */
		{ 16+2, 16+2, 16+7, 16+11, 16+15}, /* IdSel 5,  slot 2, J21 */
		{ 16+0, 16+0, 16+5,  16+9, 16+13}, /* IdSel 6,  slot 0, J19 */
		{ 16+1, 16+1, 16+6, 16+10, 16+14}, /* IdSel 7,  slot 1, J20 */
		{   -1,   -1,   -1,    -1,    -1}, /* IdSel 8,  SIO         */
		{ 16+3, 16+3, 16+8, 16+12, 16+16}  /* IdSel 9,  slot 3, J22 */
	};
	const long min_idsel = 5, max_idsel = 9, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static inline void __init
cabriolet_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, APECS_AND_LCA_DEFAULT_MEM_BASE);
	common_pci_fixup(cabriolet_map_irq, common_swizzle);
	enable_ide(0x398);
}

static inline void __init
eb164_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(cabriolet_map_irq, common_swizzle);
	enable_ide(0x398);
}


/*
 * The PC164 and LX164 have 19 PCI interrupts, four from each of the four
 * PCI slots, the SIO, PCI/IDE, and USB.
 * 
 * Each of the interrupts can be individually masked. This is
 * accomplished by setting the appropriate bit in the mask register.
 * A bit is set by writing a "1" to the desired position in the mask
 * register and cleared by writing a "0". There are 3 mask registers
 * located at ISA address 804h, 805h and 806h.
 * 
 * An I/O read at ISA address 804h, 805h, 806h will return the
 * state of the 11 PCI interrupts and not the state of the MASKED
 * interrupts.
 * 
 * Note: A write to I/O 804h, 805h, and 806h the mask register will be
 * updated.
 * 
 * 
 * 				ISA DATA<7:0>
 * ISA     +--------------------------------------------------------------+
 * ADDRESS |   7   |   6   |   5   |   4   |   3   |   2  |   1   |   0   |
 *         +==============================================================+
 * 0x804   | INTB0 |  USB  |  IDE  |  SIO  | INTA3 |INTA2 | INTA1 | INTA0 |
 *         +--------------------------------------------------------------+
 * 0x805   | INTD0 | INTC3 | INTC2 | INTC1 | INTC0 |INTB3 | INTB2 | INTB1 |
 *         +--------------------------------------------------------------+
 * 0x806   | Rsrv  | Rsrv  | Rsrv  | Rsrv  | Rsrv  |INTD3 | INTD2 | INTD1 |
 *         +--------------------------------------------------------------+
 *         * Rsrv = reserved bits
 *         Note: The mask register is write-only.
 * 
 * IdSel	
 *   5	 32 bit PCI option slot 2
 *   6	 64 bit PCI option slot 0
 *   7	 64 bit PCI option slot 1
 *   8	 Saturn I/O
 *   9	 32 bit PCI option slot 3
 *  10	 USB
 *  11	 IDE
 * 
 */

static inline int __init
alphapc164_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[7][5] __initlocaldata = {
		/*INT   INTA  INTB   INTC   INTD */
		{ 16+2, 16+2, 16+9,  16+13, 16+17}, /* IdSel  5, slot 2, J20 */
		{ 16+0, 16+0, 16+7,  16+11, 16+15}, /* IdSel  6, slot 0, J29 */
		{ 16+1, 16+1, 16+8,  16+12, 16+16}, /* IdSel  7, slot 1, J26 */
		{   -1,   -1,   -1,    -1,    -1},  /* IdSel  8, SIO */
		{ 16+3, 16+3, 16+10, 16+14, 16+18}, /* IdSel  9, slot 3, J19 */
		{ 16+6, 16+6, 16+6,  16+6,  16+6},  /* IdSel 10, USB */
		{ 16+5, 16+5, 16+5,  16+5,  16+5}   /* IdSel 11, IDE */
	};
	const long min_idsel = 5, max_idsel = 11, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static inline void __init
alphapc164_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(alphapc164_map_irq, common_swizzle);
	SMC93x_Init();
}

/*
 * The System Vector
 */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_CABRIOLET)
struct alpha_machine_vector cabriolet_mv __initmv = {
	vector_name:		"Cabriolet",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_APECS_IO,
	DO_APECS_BUS,
	machine_check:		apecs_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		35,
	irq_probe_mask:		_PROBE_MASK(35),
	update_irq_hw:		cabriolet_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	cabriolet_device_interrupt,

	init_arch:		apecs_init_arch,
	init_irq:		cabriolet_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		cabriolet_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(cabriolet)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_EB164)
struct alpha_machine_vector eb164_mv __initmv = {
	vector_name:		"EB164",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_CIA_IO,
	DO_CIA_BUS,
	machine_check:		cia_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		35,
	irq_probe_mask:		_PROBE_MASK(35),
	update_irq_hw:		cabriolet_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	cabriolet_device_interrupt,

	init_arch:		cia_init_arch,
	init_irq:		cabriolet_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		eb164_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(eb164)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_EB66P)
struct alpha_machine_vector eb66p_mv __initmv = {
	vector_name:		"EB66+",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_LCA_IO,
	DO_LCA_BUS,
	machine_check:		lca_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		35,
	irq_probe_mask:		_PROBE_MASK(35),
	update_irq_hw:		cabriolet_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	cabriolet_device_interrupt,

	init_arch:		lca_init_arch,
	init_irq:		cabriolet_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		eb66p_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(eb66p)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_LX164)
struct alpha_machine_vector lx164_mv __initmv = {
	vector_name:		"LX164",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_PYXIS_IO,
	DO_PYXIS_BUS,
	machine_check:		pyxis_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		35,
	irq_probe_mask:		_PROBE_MASK(35),
	update_irq_hw:		cabriolet_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	cabriolet_device_interrupt,

	init_arch:		pyxis_init_arch,
	init_irq:		cabriolet_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		alphapc164_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(lx164)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_PC164)
struct alpha_machine_vector pc164_mv __initmv = {
	vector_name:		"PC164",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_CIA_IO,
	DO_CIA_BUS,
	machine_check:		cia_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		35,
	irq_probe_mask:		_PROBE_MASK(35),
	update_irq_hw:		cabriolet_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	cabriolet_device_interrupt,

	init_arch:		cia_init_arch,
	init_irq:		cabriolet_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		alphapc164_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(pc164)
#endif

