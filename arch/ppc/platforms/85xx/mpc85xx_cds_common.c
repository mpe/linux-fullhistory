/*
 * arch/ppc/platform/85xx/mpc85xx_cds_common.c
 *
 * MPC85xx CDS board specific routines
 *
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/serial.h>
#include <linux/module.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/fsl_devices.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/todc.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/open_pic.h>
#include <asm/bootinfo.h>
#include <asm/pci-bridge.h>
#include <asm/mpc85xx.h>
#include <asm/irq.h>
#include <asm/immap_85xx.h>
#include <asm/immap_cpm2.h>
#include <asm/ppc_sys.h>
#include <asm/kgdb.h>

#include <mm/mmu_decl.h>
#include <syslib/cpm2_pic.h>
#include <syslib/ppc85xx_common.h>
#include <syslib/ppc85xx_setup.h>


#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
#endif

extern unsigned long total_memory;      /* in mm/init */

unsigned char __res[sizeof (bd_t)];

static int cds_pci_slot = 2;
static volatile u8 * cadmus;

/* Internal interrupts are all Level Sensitive, and Positive Polarity */

static u_char mpc85xx_cds_openpic_initsenses[] __initdata = {
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  0: L2 Cache */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  1: ECM */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  2: DDR DRAM */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  3: LBIU */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  4: DMA 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  5: DMA 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  6: DMA 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  7: DMA 3 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  8: PCI/PCI-X */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal  9: RIO Inbound Port Write Error */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 10: RIO Doorbell Inbound */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 11: RIO Outbound Message */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 12: RIO Inbound Message */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 13: TSEC 0 Transmit */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 14: TSEC 0 Receive */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 15: Unused */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 16: Unused */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 17: Unused */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 18: TSEC 0 Receive/Transmit Error */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 19: TSEC 1 Transmit */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 20: TSEC 1 Receive */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 21: Unused */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 22: Unused */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 23: Unused */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 24: TSEC 1 Receive/Transmit Error */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 25: Fast Ethernet */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 26: DUART */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 27: I2C */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 28: Performance Monitor */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 29: Unused */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 30: CPM */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Internal 31: Unused */
#if defined(CONFIG_PCI)
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 0: PCI1 slot */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 1: PCI1 slot */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 2: PCI1 slot */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 3: PCI1 slot */
#else
	0x0,						/* External  0: */
	0x0,						/* External  1: */
	0x0,						/* External  2: */
	0x0,						/* External  3: */
#endif
	0x0,						/* External  4: */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External  5: PHY */
	0x0,						/* External  6: */
	0x0,						/* External  7: */
	0x0,						/* External  8: */
	0x0,						/* External  9: */
	0x0,						/* External 10: */
#if defined(CONFIG_85xx_PCI2) && defined(CONFIG_PCI)
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 11: PCI2 slot 0 */
#else
	0x0,						/* External 11: */
#endif
};

/* ************************************************************************ */
int
mpc85xx_cds_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;
	bd_t *binfo = (bd_t *) __res;
	unsigned int freq;

	/* get the core frequency */
	freq = binfo->bi_intfreq;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");
	seq_printf(m, "Machine\t\t: CDS - MPC%s (%x)\n", cur_ppc_sys_spec->ppc_sys_name, cadmus[CM_VER]);
	seq_printf(m, "clock\t\t: %dMHz\n", freq / 1000000);
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));

	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));

	return 0;
}

#ifdef CONFIG_CPM2
static void cpm2_cascade(int irq, void *dev_id, struct pt_regs *regs)
{
	while((irq = cpm2_get_irq(regs)) >= 0)
		__do_IRQ(irq, regs);
}

static struct irqaction cpm2_irqaction = {
	.handler = cpm2_cascade,
	.flags = SA_INTERRUPT,
	.mask = CPU_MASK_NONE,
	.name = "cpm2_cascade",
};
#endif /* CONFIG_CPM2 */

void __init
mpc85xx_cds_init_IRQ(void)
{
	bd_t *binfo = (bd_t *) __res;

	/* Determine the Physical Address of the OpenPIC regs */
	phys_addr_t OpenPIC_PAddr = binfo->bi_immr_base + MPC85xx_OPENPIC_OFFSET;
	OpenPIC_Addr = ioremap(OpenPIC_PAddr, MPC85xx_OPENPIC_SIZE);
	OpenPIC_InitSenses = mpc85xx_cds_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof (mpc85xx_cds_openpic_initsenses);

	/* Skip reserved space and internal sources */
	openpic_set_sources(0, 32, OpenPIC_Addr + 0x10200);
	/* Map PIC IRQs 0-11 */
	openpic_set_sources(32, 12, OpenPIC_Addr + 0x10000);

	/* we let openpic interrupts starting from an offset, to
	 * leave space for cascading interrupts underneath.
	 */
	openpic_init(MPC85xx_OPENPIC_IRQ_OFFSET);

#ifdef CONFIG_CPM2
	/* Setup CPM2 PIC */
        cpm2_init_IRQ();

	setup_irq(MPC85xx_IRQ_CPM, &cpm2_irqaction);
#endif

	return;
}

#ifdef CONFIG_PCI
/*
 * interrupt routing
 */
int
mpc85xx_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller *hose = pci_bus_to_hose(dev->bus->number);

	if (!hose->index)
	{
		/* Handle PCI1 interrupts */
		char pci_irq_table[][4] =
			/*
			 *      PCI IDSEL/INTPIN->INTLINE
			 *        A      B      C      D
			 */

			/* Note IRQ assignment for slots is based on which slot the elysium is
			 * in -- in this setup elysium is in slot #2 (this PIRQA as first
			 * interrupt on slot */
		{
			{ 0, 1, 2, 3 }, /* 16 - PMC */
			{ 3, 0, 0, 0 }, /* 17 P2P (Tsi320) */
			{ 0, 1, 2, 3 }, /* 18 - Slot 1 */
			{ 1, 2, 3, 0 }, /* 19 - Slot 2 */
			{ 2, 3, 0, 1 }, /* 20 - Slot 3 */
			{ 3, 0, 1, 2 }, /* 21 - Slot 4 */
		};

		const long min_idsel = 16, max_idsel = 21, irqs_per_slot = 4;
		int i, j;

		for (i = 0; i < 6; i++)
			for (j = 0; j < 4; j++)
				pci_irq_table[i][j] =
					((pci_irq_table[i][j] + 5 -
					  cds_pci_slot) & 0x3) + PIRQ0A;

		return PCI_IRQ_TABLE_LOOKUP;
	} else {
		/* Handle PCI2 interrupts (if we have one) */
		char pci_irq_table[][4] =
		{
			/*
			 * We only have one slot and one interrupt
			 * going to PIRQA - PIRQD */
			{ PIRQ1A, PIRQ1A, PIRQ1A, PIRQ1A }, /* 21 - slot 0 */
		};

		const long min_idsel = 21, max_idsel = 21, irqs_per_slot = 4;

		return PCI_IRQ_TABLE_LOOKUP;
	}
}

#define ARCADIA_HOST_BRIDGE_IDSEL	17
#define ARCADIA_2ND_BRIDGE_IDSEL	3

extern int mpc85xx_pci1_last_busno;

int
mpc85xx_exclude_device(u_char bus, u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
#ifdef CONFIG_85xx_PCI2
	if (mpc85xx_pci1_last_busno)
		if (bus == (mpc85xx_pci1_last_busno + 1) && PCI_SLOT(devfn) == 0)
			return PCIBIOS_DEVICE_NOT_FOUND;
#endif
	/* We explicitly do not go past the Tundra 320 Bridge */
	if (bus == 1)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((bus == 0) && (PCI_SLOT(devfn) == ARCADIA_2ND_BRIDGE_IDSEL))
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}
#endif /* CONFIG_PCI */

TODC_ALLOC();

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init
mpc85xx_cds_setup_arch(void)
{
	bd_t *binfo = (bd_t *) __res;
	unsigned int freq;
	struct gianfar_platform_data *pdata;

	/* get the core frequency */
	freq = binfo->bi_intfreq;

	printk("mpc85xx_cds_setup_arch\n");

#ifdef CONFIG_CPM2
	cpm2_reset();
#endif

	cadmus = ioremap(CADMUS_BASE, CADMUS_SIZE);
	cds_pci_slot = ((cadmus[CM_CSR] >> 6) & 0x3) + 1;
	printk("CDS Version = %x in PCI slot %d\n", cadmus[CM_VER], cds_pci_slot);

	/* Setup TODC access */
	TODC_INIT(TODC_TYPE_DS1743,
			0,
			0,
			ioremap(CDS_RTC_ADDR, CDS_RTC_SIZE),
			8);

	/* Set loops_per_jiffy to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	loops_per_jiffy = freq / HZ;

#ifdef CONFIG_PCI
	/* setup PCI host bridges */
	mpc85xx_setup_hose();
#endif

#ifdef CONFIG_SERIAL_8250
	mpc85xx_early_serial_map();
#endif

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	/* Invalidate the entry we stole earlier the serial ports
	 * should be properly mapped */
	invalidate_tlbcam_entry(NUM_TLBCAMS - 1);
#endif

	/* setup the board related information for the enet controllers */
	pdata = (struct gianfar_platform_data *) ppc_sys_get_pdata(MPC85xx_TSEC1);
	pdata->board_flags = FSL_GIANFAR_BRD_HAS_PHY_INTR;
	pdata->interruptPHY = MPC85xx_IRQ_EXT5;
	pdata->phyid = 0;
	/* fixup phy address */
	pdata->phy_reg_addr += binfo->bi_immr_base;
	memcpy(pdata->mac_addr, binfo->bi_enetaddr, 6);

	pdata = (struct gianfar_platform_data *) ppc_sys_get_pdata(MPC85xx_TSEC2);
	pdata->board_flags = FSL_GIANFAR_BRD_HAS_PHY_INTR;
	pdata->interruptPHY = MPC85xx_IRQ_EXT5;
	pdata->phyid = 1;
	/* fixup phy address */
	pdata->phy_reg_addr += binfo->bi_immr_base;
	memcpy(pdata->mac_addr, binfo->bi_enet1addr, 6);


#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef  CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
	ROOT_DEV = Root_HDA1;
#endif
}

/* ************************************************************************ */
void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
              unsigned long r6, unsigned long r7)
{
	/* parse_bootinfo must always be called first */
	parse_bootinfo(find_bootinfo());

	/*
	 * If we were passed in a board information, copy it into the
	 * residual data area.
	 */
	if (r3) {
		memcpy((void *) __res, (void *) (r3 + KERNELBASE),
				sizeof (bd_t));

	}
#ifdef CONFIG_SERIAL_TEXT_DEBUG
	{
		bd_t *binfo = (bd_t *) __res;
		struct uart_port p;

		/* Use the last TLB entry to map CCSRBAR to allow access to DUART regs */
		settlbcam(NUM_TLBCAMS - 1, binfo->bi_immr_base,
				binfo->bi_immr_base, MPC85xx_CCSRBAR_SIZE, _PAGE_IO, 0);

		memset(&p, 0, sizeof (p));
		p.iotype = SERIAL_IO_MEM;
		p.membase = (void *) binfo->bi_immr_base + MPC85xx_UART0_OFFSET;
		p.uartclk = binfo->bi_busfreq;

		gen550_init(0, &p);

		memset(&p, 0, sizeof (p));
		p.iotype = SERIAL_IO_MEM;
		p.membase = (void *) binfo->bi_immr_base + MPC85xx_UART1_OFFSET;
		p.uartclk = binfo->bi_busfreq;

		gen550_init(1, &p);
	}
#endif

#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured in, and there's a valid
	 * starting address for it, set it up.
	 */
	if (r4) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	/* Copy the kernel command line arguments to a safe place. */

	if (r6) {
		*(char *) (r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *) (r6 + KERNELBASE));
	}

	identify_ppc_sys_by_id(mfspr(SPRN_SVR));

	/* setup the PowerPC module struct */
	ppc_md.setup_arch = mpc85xx_cds_setup_arch;
	ppc_md.show_cpuinfo = mpc85xx_cds_show_cpuinfo;

	ppc_md.init_IRQ = mpc85xx_cds_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;

	ppc_md.restart = mpc85xx_restart;
	ppc_md.power_off = mpc85xx_power_off;
	ppc_md.halt = mpc85xx_halt;

	ppc_md.find_end_of_memory = mpc85xx_find_end_of_memory;

	ppc_md.calibrate_decr = mpc85xx_calibrate_decr;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;

#if defined(CONFIG_SERIAL_8250) && defined(CONFIG_SERIAL_TEXT_DEBUG)
	ppc_md.progress = gen550_progress;
#endif /* CONFIG_SERIAL_8250 && CONFIG_SERIAL_TEXT_DEBUG */

	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_cds_init(): exit", 0);

	return;
}
