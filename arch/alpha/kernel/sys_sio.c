/*
 *	linux/arch/alpha/kernel/sys_sio.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code for all boards that route the PCI interrupts through the SIO
 * PCI/ISA bridge.  This includes Noname (AXPpci33), Multia (UDB),
 * Kenetics's Platform 2000, Avanti (AlphaStation), XL, and AlphaBook1.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/compiler.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
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
sio_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 8)
		outb(mask >> 8, 0xA1);
	else
		outb(mask, 0x21);
}

static void __init
sio_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	if (alpha_using_srm)
		alpha_mv.device_interrupt = srm_device_interrupt;
		
	enable_irq(2);			/* enable cascade */
}

static inline void __init
xl_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	/*
	 * Set up the PCI->physical memory translation windows.  For
	 * the XL we *must* use both windows, in order to maximize the
	 * amount of physical memory that can be used to DMA from the
	 * ISA bus, and still allow PCI bus devices access to all of
	 * host memory.
	 *
	 * See <asm/apecs.h> for window bases and sizes.
	 *
	 * This restriction due to the true XL motherboards' 82379AB SIO
	 * PCI<->ISA bridge chip which passes only 27 bits of address...
	 */

	*(vuip)APECS_IOC_PB1R = 1<<19 | (APECS_XL_DMA_WIN1_BASE & 0xfff00000U);
	*(vuip)APECS_IOC_PM1R = (APECS_XL_DMA_WIN1_SIZE - 1) & 0xfff00000U;
	*(vuip)APECS_IOC_TB1R = 0;

	*(vuip)APECS_IOC_PB2R = 1<<19 | (APECS_XL_DMA_WIN2_BASE & 0xfff00000U);
	*(vuip)APECS_IOC_PM2R = (APECS_XL_DMA_WIN2_SIZE - 1) & 0xfff00000U;
	*(vuip)APECS_IOC_TB2R = 0;

	/*
	 * Finally, clear the HAXR2 register, which gets used for PCI
	 * Config Space accesses. That is the way we want to use it,
	 * and we do not want to depend on what ARC or SRM might have
	 * left behind...
	 */

	*(vuip)APECS_IOC_HAXR2 = 0; mb();
}

static inline void __init
alphabook1_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	/* The AlphaBook1 has LCD video fixed at 800x600,
	   37 rows and 100 cols. */
	screen_info.orig_y = 37;
	screen_info.orig_video_cols = 100;
	screen_info.orig_video_lines = 37;

	lca_init_arch(mem_start, mem_end);
}


/*
 * sio_route_tab selects irq routing in PCI/ISA bridge so that:
 *		PIRQ0 -> irq 15
 *		PIRQ1 -> irq  9
 *		PIRQ2 -> irq 10
 *		PIRQ3 -> irq 11
 *
 * This probably ought to be configurable via MILO.  For
 * example, sound boards seem to like using IRQ 9.
 *
 * This is NOT how we should do it. PIRQ0-X should have
 * their own IRQ's, the way intel uses the IO-APIC irq's.
 */
static unsigned long sio_route_tab __initdata = 0;

static void __init
sio_pci_fixup(int (*map_irq)(struct pci_dev *dev, int sel, int pin),
	      unsigned long new_route_tab)
{
	unsigned int route_tab;

	/* Examine or update the PCI routing table.  */
        pcibios_read_config_dword(0, PCI_DEVFN(7, 0), 0x60, &route_tab);

	sio_route_tab = route_tab; 
	if (PCI_MODIFY) {
		sio_route_tab = new_route_tab;
		pcibios_write_config_dword(0, PCI_DEVFN(7, 0), 0x60,
					   new_route_tab);
	}

	/* Update all the IRQs.  */
	common_pci_fixup(map_irq, common_swizzle);
}

static unsigned int __init
sio_collect_irq_levels(void)
{
	unsigned int level_bits = 0;
	struct pci_dev *dev;

	/* Iterate through the devices, collecting IRQ levels.  */
	for (dev = pci_devices; dev; dev = dev->next) {
		if ((dev->class >> 16 == PCI_BASE_CLASS_BRIDGE) &&
		    (dev->class >> 8 != PCI_CLASS_BRIDGE_PCMCIA))
			continue;

		if (dev->irq)
			level_bits |= (1 << dev->irq);
	}
	return level_bits;
}

static void __init
sio_fixup_irq_levels(unsigned int level_bits)
{
	unsigned int old_level_bits;

	/*
	 * Now, make all PCI interrupts level sensitive.  Notice:
	 * these registers must be accessed byte-wise.  inw()/outw()
	 * don't work.
	 *
	 * Make sure to turn off any level bits set for IRQs 9,10,11,15,
	 *  so that the only bits getting set are for devices actually found.
	 * Note that we do preserve the remainder of the bits, which we hope
	 *  will be set correctly by ARC/SRM.
	 *
	 * Note: we at least preserve any level-set bits on AlphaBook1
	 */
	old_level_bits = inb(0x4d0) | (inb(0x4d1) << 8);

	level_bits |= (old_level_bits & 0x71ff);

	outb((level_bits >> 0) & 0xff, 0x4d0);
	outb((level_bits >> 8) & 0xff, 0x4d1);
}

static inline int __init
noname_map_irq(struct pci_dev *dev, int slot, int pin)
{
	/*
	 * The Noname board has 5 PCI slots with each of the 4
	 * interrupt pins routed to different pins on the PCI/ISA
	 * bridge (PIRQ0-PIRQ3).  The table below is based on
	 * information available at:
	 *
	 *   http://ftp.digital.com/pub/DEC/axppci/ref_interrupts.txt
	 *
	 * I have no information on the Avanti interrupt routing, but
	 * the routing seems to be identical to the Noname except
	 * that the Avanti has an additional slot whose routing I'm
	 * unsure of.
	 *
	 * pirq_tab[0] is a fake entry to deal with old PCI boards
	 * that have the interrupt pin number hardwired to 0 (meaning
	 * that they use the default INTA line, if they are interrupt
	 * driven at all).
	 */
	static char irq_tab[][5] __initlocaldata = {
		/*INT A   B   C   D */
		{ 3,  3,  3,  3,  3}, /* idsel  6 (53c810) */ 
		{-1, -1, -1, -1, -1}, /* idsel  7 (SIO: PCI/ISA bridge) */
		{ 2,  2, -1, -1, -1}, /* idsel  8 (Hack: slot closest ISA) */
		{-1, -1, -1, -1, -1}, /* idsel  9 (unused) */
		{-1, -1, -1, -1, -1}, /* idsel 10 (unused) */
		{ 0,  0,  2,  1,  0}, /* idsel 11 KN25_PCI_SLOT0 */
		{ 1,  1,  0,  2,  1}, /* idsel 12 KN25_PCI_SLOT1 */
		{ 2,  2,  1,  0,  2}, /* idsel 13 KN25_PCI_SLOT2 */
		{ 0,  0,  0,  0,  0}, /* idsel 14 AS255 TULIP */
	};
	const long min_idsel = 6, max_idsel = 14, irqs_per_slot = 5;
	int irq = COMMON_TABLE_LOOKUP, tmp;
	tmp = __kernel_extbl(sio_route_tab, irq);
	return irq >= 0 ? tmp : -1;
}

static inline void __init
noname_pci_fixup(void)
{
	/*
	 * For UDB, the only available PCI slot must not map to IRQ 9,
	 * since that's the builtin MSS sound chip. That PCI slot
	 * will map to PIRQ1 (for INTA at least), so we give it IRQ 15
	 * instead.
	 *
	 * Unfortunately we have to do this for NONAME as well, since
	 * they are co-indicated when the platform type "Noname" is
	 * selected... :-(
	 */
	layout_all_busses(DEFAULT_IO_BASE, APECS_AND_LCA_DEFAULT_MEM_BASE);
	sio_pci_fixup(noname_map_irq, 0x0b0a0f0d);
	sio_fixup_irq_levels(sio_collect_irq_levels());
        enable_ide(0x26e);
}

static inline void __init
avanti_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, APECS_AND_LCA_DEFAULT_MEM_BASE);
	sio_pci_fixup(noname_map_irq, 0x0b0a0e0f);
	sio_fixup_irq_levels(sio_collect_irq_levels());
        enable_ide(0x26e);
}

static inline void __init
xl_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, XL_DEFAULT_MEM_BASE);
	sio_pci_fixup(noname_map_irq, 0x0b0a090f);
	sio_fixup_irq_levels(sio_collect_irq_levels());
        enable_ide(0x26e);
}

static inline int __init
p2k_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[][5] __initlocaldata = {
		/*INT A   B   C   D */
		{ 0,  0, -1, -1, -1}, /* idsel  6 (53c810) */
		{-1, -1, -1, -1, -1}, /* idsel  7 (SIO: PCI/ISA bridge) */
		{ 1,  1,  2,  3,  0}, /* idsel  8 (slot A) */
		{ 2,  2,  3,  0,  1}, /* idsel  9 (slot B) */
		{-1, -1, -1, -1, -1}, /* idsel 10 (unused) */
		{-1, -1, -1, -1, -1}, /* idsel 11 (unused) */
		{ 3,  3, -1, -1, -1}, /* idsel 12 (CMD0646) */
	};
	const long min_idsel = 6, max_idsel = 12, irqs_per_slot = 5;
	int irq = COMMON_TABLE_LOOKUP, tmp;
	tmp = __kernel_extbl(sio_route_tab, irq);
	return irq >= 0 ? tmp : -1;
}

static inline void __init
p2k_pci_fixup(void)
{
	layout_all_busses(DEFAULT_IO_BASE, APECS_AND_LCA_DEFAULT_MEM_BASE);
	sio_pci_fixup(p2k_map_irq, 0x0b0a090f);
	sio_fixup_irq_levels(sio_collect_irq_levels());
        enable_ide(0x26e);
}

static inline void __init
alphabook1_pci_fixup(void)
{
	struct pci_dev *dev;
	unsigned char orig, config;

	layout_all_busses(DEFAULT_IO_BASE, APECS_AND_LCA_DEFAULT_MEM_BASE);

        /* For the AlphaBook1, NCR810 SCSI is 14, PCMCIA controller is 15. */
	sio_pci_fixup(noname_map_irq, 0x0e0f0a0a);

	/*
	 * On the AlphaBook1, the PCMCIA chip (Cirrus 6729)
	 * is sensitive to PCI bus bursts, so we must DISABLE
	 * burst mode for the NCR 8xx SCSI... :-(
	 *
	 * Note that the NCR810 SCSI driver must preserve the
	 * setting of the bit in order for this to work.  At the
	 * moment (2.0.29), ncr53c8xx.c does NOT do this, but
	 * 53c7,8xx.c DOES.
	 */
	for (dev = pci_devices; dev; dev = dev->next) {
                if (dev->vendor == PCI_VENDOR_ID_NCR &&
                    (dev->device == PCI_DEVICE_ID_NCR_53C810 ||
                     dev->device == PCI_DEVICE_ID_NCR_53C815 ||
                     dev->device == PCI_DEVICE_ID_NCR_53C820 ||
                     dev->device == PCI_DEVICE_ID_NCR_53C825)) {
			unsigned int io_port;
			unsigned char ctest4;

			pcibios_read_config_dword(dev->bus->number,
						  dev->devfn,
						  PCI_BASE_ADDRESS_0,
						  &io_port);
			io_port &= PCI_BASE_ADDRESS_IO_MASK;
			ctest4 = inb(io_port+0x21);
			if (!(ctest4 & 0x80)) {
				printk("AlphaBook1 NCR init: setting"
				       " burst disable\n");
				outb(ctest4 | 0x80, io_port+0x21);
			}
                }
	}

	/* Do not set *ANY* level triggers for AlphaBook1. */
	sio_fixup_irq_levels(0);

	/* Make sure that register PR1 indicates 1Mb mem */
	outb(0x0f, 0x3ce); orig = inb(0x3cf);   /* read PR5  */
	outb(0x0f, 0x3ce); outb(0x05, 0x3cf);   /* unlock PR0-4 */
	outb(0x0b, 0x3ce); config = inb(0x3cf); /* read PR1 */
	if ((config & 0xc0) != 0xc0) {
		printk("AlphaBook1 VGA init: setting 1Mb memory\n");
		config |= 0xc0;
		outb(0x0b, 0x3ce); outb(config, 0x3cf); /* write PR1 */
	}
	outb(0x0f, 0x3ce); outb(orig, 0x3cf); /* (re)lock PR0-4 */
}


/*
 * The System Vectors
 */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_BOOK1)
struct alpha_machine_vector alphabook1_mv __initmv = {
	vector_name:		"AlphaBook1",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_LCA_IO,
	DO_LCA_BUS,
	machine_check:		lca_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		16,
	irq_probe_mask:		_PROBE_MASK(16),
	update_irq_hw:		sio_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	isa_device_interrupt,

	init_arch:		alphabook1_init_arch,
	init_irq:		sio_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		alphabook1_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(alphabook1)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_AVANTI)
struct alpha_machine_vector avanti_mv __initmv = {
	vector_name:		"Avanti",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_APECS_IO,
	DO_APECS_BUS,
	machine_check:		apecs_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		16,
	irq_probe_mask:		_PROBE_MASK(16),
	update_irq_hw:		sio_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	isa_device_interrupt,

	init_arch:		apecs_init_arch,
	init_irq:		sio_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		avanti_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(avanti)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_NONAME)
struct alpha_machine_vector noname_mv __initmv = {
	vector_name:		"Noname",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_LCA_IO,
	DO_LCA_BUS,
	machine_check:		lca_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		16,
	irq_probe_mask:		_PROBE_MASK(16),
	update_irq_hw:		sio_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	srm_device_interrupt,

	init_arch:		lca_init_arch,
	init_irq:		sio_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		noname_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(noname)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_P2K)
struct alpha_machine_vector p2k_mv __initmv = {
	vector_name:		"Platform2000",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_LCA_IO,
	DO_LCA_BUS,
	machine_check:		lca_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		16,
	irq_probe_mask:		P2K_PROBE_MASK,
	update_irq_hw:		sio_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	srm_device_interrupt,

	init_arch:		lca_init_arch,
	init_irq:		sio_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		p2k_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(p2k)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_XL)
struct alpha_machine_vector xl_mv __initmv = {
	vector_name:		"XL",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_APECS_IO,
	BUS(apecs_xl),
	machine_check:		apecs_machine_check,
	max_dma_address:	ALPHA_XL_MAX_DMA_ADDRESS,

	nr_irqs:		16,
	irq_probe_mask:		_PROBE_MASK(16),
	update_irq_hw:		sio_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	isa_device_interrupt,

	init_arch:		xl_init_arch,
	init_irq:		sio_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		xl_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(xl)
#endif
