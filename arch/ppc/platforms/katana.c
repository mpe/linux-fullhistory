/*
 * arch/ppc/platforms/katana.c
 *
 * Board setup routines for the Artesyn Katana 750 based boards.
 *
 * Tim Montgomery <timm@artesyncp.com>
 *
 * Based on code done by Rabeeh Khoury - rabeeh@galileo.co.il
 * Based on code done by - Mark A. Greer <mgreer@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
/*
 * Supports the Artesyn 750i, 752i, and 3750.  The 752i is virtually identical
 * to the 750i except that it has an mv64460 bridge.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/mv643xx.h>
#ifdef CONFIG_BOOTIMG
#include <linux/bootimg.h>
#endif
#include <asm/page.h>
#include <asm/time.h>
#include <asm/smp.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/mv64x60.h>
#include <platforms/katana.h>

static struct		mv64x60_handle bh;
static katana_id_t	katana_id;
static u32		cpld_base;
static u32		sram_base;

/* PCI Interrupt routing */
static int __init
katana_irq_lookup_750i(unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 *       A   B   C   D
		 */
		/* IDSEL 4  (PMC 1) */
		{ KATANA_PCI_INTB_IRQ_750i, KATANA_PCI_INTC_IRQ_750i,
			KATANA_PCI_INTD_IRQ_750i, KATANA_PCI_INTA_IRQ_750i },
		/* IDSEL 5  (PMC 2) */
		{ KATANA_PCI_INTC_IRQ_750i, KATANA_PCI_INTD_IRQ_750i,
			KATANA_PCI_INTA_IRQ_750i, KATANA_PCI_INTB_IRQ_750i },
		/* IDSEL 6 (T8110) */
		{KATANA_PCI_INTD_IRQ_750i, 0, 0, 0 },
	};
	const long min_idsel = 4, max_idsel = 6, irqs_per_slot = 4;

	return PCI_IRQ_TABLE_LOOKUP;
}

static int __init
katana_irq_lookup_3750(unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 *       A   B   C   D
		 */
		{ KATANA_PCI_INTA_IRQ_3750, 0, 0, 0 }, /* IDSEL 3 (BCM5691) */
		{ KATANA_PCI_INTB_IRQ_3750, 0, 0, 0 }, /* IDSEL 4 (MV64360 #2)*/
		{ KATANA_PCI_INTC_IRQ_3750, 0, 0, 0 }, /* IDSEL 5 (MV64360 #3)*/
	};
	const long min_idsel = 3, max_idsel = 5, irqs_per_slot = 4;

	return PCI_IRQ_TABLE_LOOKUP;
}

static int __init
katana_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	switch (katana_id) {
	case KATANA_ID_750I:
	case KATANA_ID_752I:
		return katana_irq_lookup_750i(idsel, pin);

	case KATANA_ID_3750:
		return katana_irq_lookup_3750(idsel, pin);

	default:
		printk(KERN_ERR "Bogus board ID\n");
		return 0;
	}
}

/* Board info retrieval routines */
void __init
katana_get_board_id(void)
{
	switch (in_8((volatile char *)(cpld_base + KATANA_CPLD_PRODUCT_ID))) {
	case KATANA_PRODUCT_ID_3750:
		katana_id = KATANA_ID_3750;
		break;

	case KATANA_PRODUCT_ID_750i:
		katana_id = KATANA_ID_750I;
		break;

	case KATANA_PRODUCT_ID_752i:
		katana_id = KATANA_ID_752I;
		break;

	default:
		printk(KERN_ERR "Unsupported board\n");
	}
}

int __init
katana_get_proc_num(void)
{
	u16		val;
	u8		save_exclude;
	static int	proc = -1;
	static u8	first_time = 1;

	if (first_time) {
		if (katana_id != KATANA_ID_3750)
			proc = 0;
		else {
			save_exclude = mv64x60_pci_exclude_bridge;
			mv64x60_pci_exclude_bridge = 0;

			early_read_config_word(bh.hose_a, 0,
				PCI_DEVFN(0,0), PCI_DEVICE_ID, &val);

			mv64x60_pci_exclude_bridge = save_exclude;

			switch(val) {
			case PCI_DEVICE_ID_KATANA_3750_PROC0:
				proc = 0;
				break;

			case PCI_DEVICE_ID_KATANA_3750_PROC1:
				proc = 1;
				break;

			case PCI_DEVICE_ID_KATANA_3750_PROC2:
				proc = 2;
				break;

			default:
				printk(KERN_ERR "Bogus Device ID\n");
			}
		}

		first_time = 0;
	}

	return proc;
}

static inline int
katana_is_monarch(void)
{
	return in_8((volatile char *)(cpld_base + KATANA_CPLD_BD_CFG_3)) &
		KATANA_CPLD_BD_CFG_3_MONARCH;
}

static void __init
katana_enable_ipmi(void)
{
	u8 reset_out;

	/* Enable access to IPMI ctlr by clearing IPMI PORTSEL bit in CPLD */
	reset_out = in_8((volatile char *)(cpld_base + KATANA_CPLD_RESET_OUT));
	reset_out &= ~KATANA_CPLD_RESET_OUT_PORTSEL;
	out_8((volatile void *)(cpld_base + KATANA_CPLD_RESET_OUT), reset_out);
	return;
}

static unsigned long
katana_bus_freq(void)
{
	u8 bd_cfg_0;

	bd_cfg_0 = in_8((volatile char *)(cpld_base + KATANA_CPLD_BD_CFG_0));

	switch (bd_cfg_0 & KATANA_CPLD_BD_CFG_0_SYSCLK_MASK) {
	case KATANA_CPLD_BD_CFG_0_SYSCLK_200:
		return 200000000;
		break;

	case KATANA_CPLD_BD_CFG_0_SYSCLK_166:
		return 166666666;
		break;

	case KATANA_CPLD_BD_CFG_0_SYSCLK_133:
		return 133333333;
		break;

	case KATANA_CPLD_BD_CFG_0_SYSCLK_100:
		return 100000000;
		break;

	default:
		return 133333333;
		break;
	}
}

/* Bridge & platform setup routines */
void __init
katana_intr_setup(void)
{
	/* MPP 8, 9, and 10 */
	mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_1, 0xfff);

	/* MPP 14 */
	if ((katana_id == KATANA_ID_750I) || (katana_id == KATANA_ID_752I))
		mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_1, 0x0f000000);

	/*
	 * Define GPP 8,9,and 10 interrupt polarity as active low
	 * input signal and level triggered
	 */
	mv64x60_set_bits(&bh, MV64x60_GPP_LEVEL_CNTL, 0x700);
	mv64x60_clr_bits(&bh, MV64x60_GPP_IO_CNTL, 0x700);

	if ((katana_id == KATANA_ID_750I) || (katana_id == KATANA_ID_752I)) {
		mv64x60_set_bits(&bh, MV64x60_GPP_LEVEL_CNTL, (1<<14));
		mv64x60_clr_bits(&bh, MV64x60_GPP_IO_CNTL, (1<<14));
	}

	/* Config GPP intr ctlr to respond to level trigger */
	mv64x60_set_bits(&bh, MV64x60_COMM_ARBITER_CNTL, (1<<10));

	/* Erranum FEr PCI-#8 */
	mv64x60_clr_bits(&bh, MV64x60_PCI0_CMD, (1<<5) | (1<<9));
	mv64x60_clr_bits(&bh, MV64x60_PCI1_CMD, (1<<5) | (1<<9));

	/*
	 * Dismiss and then enable interrupt on GPP interrupt cause
	 * for CPU #0
	 */
	mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, ~0x700);
	mv64x60_set_bits(&bh, MV64x60_GPP_INTR_MASK, 0x700);

	if ((katana_id == KATANA_ID_750I) || (katana_id == KATANA_ID_752I)) {
		mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, ~(1<<14));
		mv64x60_set_bits(&bh, MV64x60_GPP_INTR_MASK, (1<<14));
	}

	/*
	 * Dismiss and then enable interrupt on CPU #0 high cause reg
	 * BIT25 summarizes GPP interrupts 8-15
	 */
	mv64x60_set_bits(&bh, MV64360_IC_CPU0_INTR_MASK_HI, (1<<25));
	return;
}

void __init
katana_setup_peripherals(void)
{
	u32 base, size_0, size_1;

	/* Set up windows for boot CS, soldered & socketed flash, and CPLD */
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN,
		 KATANA_BOOT_WINDOW_BASE, KATANA_BOOT_WINDOW_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);

	/* Assume firmware set up window sizes correctly for dev 0 & 1 */
	mv64x60_get_32bit_window(&bh, MV64x60_CPU2DEV_0_WIN, &base, &size_0);

	if (size_0 > 0) {
		mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_0_WIN,
			 KATANA_SOLDERED_FLASH_BASE, size_0, 0);
		bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_0_WIN);
	}

	mv64x60_get_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN, &base, &size_1);

	if (size_1 > 0) {
		mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN,
			 (KATANA_SOLDERED_FLASH_BASE + size_0), size_1, 0);
		bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_1_WIN);
	}

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_2_WIN,
		 KATANA_SOCKET_BASE, KATANA_SOCKETED_FLASH_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_2_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_3_WIN,
		 KATANA_CPLD_BASE, KATANA_CPLD_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_3_WIN);
	cpld_base = (u32)ioremap(KATANA_CPLD_BASE, KATANA_CPLD_SIZE);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2SRAM_WIN,
		 KATANA_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);
	sram_base = (u32)ioremap(KATANA_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE);

	/* Set up Enet->SRAM window */
	mv64x60_set_32bit_window(&bh, MV64x60_ENET2MEM_4_WIN,
		KATANA_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE, 0x2);
	bh.ci->enable_window_32bit(&bh, MV64x60_ENET2MEM_4_WIN);

	/* Give enet r/w access to memory region */
	mv64x60_set_bits(&bh, MV64360_ENET2MEM_ACC_PROT_0, (0x3 << (4 << 1)));
	mv64x60_set_bits(&bh, MV64360_ENET2MEM_ACC_PROT_1, (0x3 << (4 << 1)));
	mv64x60_set_bits(&bh, MV64360_ENET2MEM_ACC_PROT_2, (0x3 << (4 << 1)));

	mv64x60_clr_bits(&bh, MV64x60_PCI1_PCI_DECODE_CNTL, (1 << 3));
	mv64x60_clr_bits(&bh, MV64x60_TIMR_CNTR_0_3_CNTL,
			 ((1 << 0) | (1 << 8) | (1 << 16) | (1 << 24)));

	/* Must wait until window set up before retrieving board id */
	katana_get_board_id();

	/* Enumerate pci bus (must know board id before getting proc number) */
	if (katana_get_proc_num() == 0)
		bh.hose_b->last_busno = pciauto_bus_scan(bh.hose_b, 0);

#if defined(CONFIG_NOT_COHERENT_CACHE)
	mv64x60_write(&bh, MV64360_SRAM_CONFIG, 0x00160000);
#else
	mv64x60_write(&bh, MV64360_SRAM_CONFIG, 0x001600b2);
#endif

	/*
	 * Setting the SRAM to 0. Note that this generates parity errors on
	 * internal data path in SRAM since it's first time accessing it
	 * while after reset it's not configured.
	 */
	memset((void *)sram_base, 0, MV64360_SRAM_SIZE);

	/* Only processor zero [on 3750] is an PCI interrupt controller */
	if (katana_get_proc_num() == 0)
		katana_intr_setup();

	return;
}

static void __init
katana_setup_bridge(void)
{
	struct mv64x60_setup_info si;
	int i;

	memset(&si, 0, sizeof(si));

	si.phys_reg_base = KATANA_BRIDGE_REG_BASE;

	si.pci_1.enable_bus = 1;
	si.pci_1.pci_io.cpu_base = KATANA_PCI1_IO_START_PROC_ADDR;
	si.pci_1.pci_io.pci_base_hi = 0;
	si.pci_1.pci_io.pci_base_lo = KATANA_PCI1_IO_START_PCI_ADDR;
	si.pci_1.pci_io.size = KATANA_PCI1_IO_SIZE;
	si.pci_1.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_mem[0].cpu_base = KATANA_PCI1_MEM_START_PROC_ADDR;
	si.pci_1.pci_mem[0].pci_base_hi = KATANA_PCI1_MEM_START_PCI_HI_ADDR;
	si.pci_1.pci_mem[0].pci_base_lo = KATANA_PCI1_MEM_START_PCI_LO_ADDR;
	si.pci_1.pci_mem[0].size = KATANA_PCI1_MEM_SIZE;
	si.pci_1.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_cmd_bits = 0;
	si.pci_1.latency_timer = 0x80;

	for (i = 0; i < MV64x60_CPU2MEM_WINDOWS; i++) {
#if defined(CONFIG_NOT_COHERENT_CACHE)
		si.cpu_prot_options[i] = 0;
		si.enet_options[i] = MV64360_ENET2MEM_SNOOP_NONE;
		si.mpsc_options[i] = MV64360_MPSC2MEM_SNOOP_NONE;
		si.idma_options[i] = MV64360_IDMA2MEM_SNOOP_NONE;

		si.pci_1.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_NONE |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_128_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_256_BYTES;
#else
		si.cpu_prot_options[i] = 0;
		si.enet_options[i] = MV64360_ENET2MEM_SNOOP_NONE; /* errata */
		si.mpsc_options[i] = MV64360_MPSC2MEM_SNOOP_NONE; /* errata */
		si.idma_options[i] = MV64360_IDMA2MEM_SNOOP_NONE; /* errata */

		si.pci_1.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_WB |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_32_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_32_BYTES;
#endif
	}

	/* Lookup PCI host bridges */
	if (mv64x60_init(&bh, &si))
		printk(KERN_WARNING "Bridge initialization failed.\n");

	pci_dram_offset = 0; /* sys mem at same addr on PCI & cpu bus */
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = katana_map_irq;
	ppc_md.pci_exclude_device = mv64x60_pci_exclude_device;

	mv64x60_set_bus(&bh, 1, 0);
	bh.hose_b->first_busno = 0;
	bh.hose_b->last_busno = 0xff;

	return;
}

static void __init
katana_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("katana_setup_arch: enter", 0);

	set_tb(0, 0);

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef   CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

	/*
	 * Set up the L2CR register.
	 *
	 * 750FX has only L2E, L2PE (bits 2-8 are reserved)
	 * DD2.0 has bug that requires the L2 to be in WRT mode
	 * avoid dirty data in cache
	 */
	if (PVR_REV(mfspr(PVR)) == 0x0200) {
		printk(KERN_INFO "DD2.0 detected. Setting L2 cache"
			"to Writethrough mode\n");
		_set_L2CR(L2CR_L2E | L2CR_L2PE | L2CR_L2WT);
	}
	else
		_set_L2CR(L2CR_L2E | L2CR_L2PE);

	if (ppc_md.progress)
		ppc_md.progress("katana_setup_arch: calling setup_bridge", 0);

	katana_setup_bridge();
	katana_setup_peripherals();
	katana_enable_ipmi();

	printk(KERN_INFO "Artesyn Communication Products, LLC - Katana(TM)\n");
	if (ppc_md.progress)
		ppc_md.progress("katana_setup_arch: exit", 0);
	return;
}

/* Platform device data fixup routines. */
#if defined(CONFIG_SERIAL_MPSC)
static void __init
katana_fixup_mpsc_pdata(struct platform_device *pdev)
{
	struct mpsc_pdata *pdata;

	pdata = (struct mpsc_pdata *)pdev->dev.platform_data;

	pdata->max_idle = 40;
	pdata->default_baud = KATANA_DEFAULT_BAUD;
	pdata->brg_clk_src = KATANA_MPSC_CLK_SRC;
	pdata->brg_clk_freq = KATANA_MPSC_CLK_FREQ;

	return;
}
#endif

#if defined(CONFIG_MV643XX_ETH)
static void __init
katana_fixup_eth_pdata(struct platform_device *pdev)
{
	struct mv64xxx_eth_platform_data *eth_pd;
	static u16 phy_addr[] = {
		KATANA_ETH0_PHY_ADDR,
		KATANA_ETH1_PHY_ADDR,
		KATANA_ETH2_PHY_ADDR,
	};
	int	rx_size = KATANA_ETH_RX_QUEUE_SIZE * MV64340_ETH_DESC_SIZE;
	int	tx_size = KATANA_ETH_TX_QUEUE_SIZE * MV64340_ETH_DESC_SIZE;

	eth_pd = pdev->dev.platform_data;
	eth_pd->force_phy_addr = 1;
	eth_pd->phy_addr = phy_addr[pdev->id];
	eth_pd->tx_queue_size = KATANA_ETH_TX_QUEUE_SIZE;
	eth_pd->rx_queue_size = KATANA_ETH_RX_QUEUE_SIZE;
	eth_pd->tx_sram_addr = mv643xx_sram_alloc(tx_size);

	if (eth_pd->tx_sram_addr)
		eth_pd->tx_sram_size = tx_size;
	else
		printk(KERN_ERR "mv643xx_sram_alloc failed\n");

	eth_pd->rx_sram_addr = mv643xx_sram_alloc(rx_size);
	if (eth_pd->rx_sram_addr)
		eth_pd->rx_sram_size = rx_size;
	else
		printk(KERN_ERR "mv643xx_sram_alloc failed\n");
}
#endif

static int __init
katana_platform_notify(struct device *dev)
{
	static struct {
		char	*bus_id;
		void	((*rtn)(struct platform_device *pdev));
	} dev_map[] = {
#if defined(CONFIG_SERIAL_MPSC)
		{ MPSC_CTLR_NAME "0", katana_fixup_mpsc_pdata },
		{ MPSC_CTLR_NAME "1", katana_fixup_mpsc_pdata },
#endif
#if defined(CONFIG_MV643XX_ETH)
		{ MV64XXX_ETH_NAME "0", katana_fixup_eth_pdata },
		{ MV64XXX_ETH_NAME "1", katana_fixup_eth_pdata },
		{ MV64XXX_ETH_NAME "2", katana_fixup_eth_pdata },
#endif
	};
	struct platform_device	*pdev;
	int	i;

	if (dev && dev->bus_id)
		for (i=0; i<ARRAY_SIZE(dev_map); i++)
			if (!strncmp(dev->bus_id, dev_map[i].bus_id,
				BUS_ID_SIZE)) {

				pdev = container_of(dev,
					struct platform_device, dev);
				dev_map[i].rtn(pdev);
			}

	return 0;
}

static void
katana_restart(char *cmd)
{
	volatile ulong i = 10000000;

	/* issue hard reset to the reset command register */
	out_8((volatile char *)(cpld_base + KATANA_CPLD_RST_CMD),
		KATANA_CPLD_RST_CMD_HR);

	while (i-- > 0) ;
	panic("restart failed\n");
}

static void
katana_halt(void)
{
	while (1) ;
	/* NOTREACHED */
}

static void
katana_power_off(void)
{
	katana_halt();
	/* NOTREACHED */
}

static int
katana_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Artesyn Communication Products, LLC\n");

	seq_printf(m, "board\t\t: ");

	switch (katana_id) {
	case KATANA_ID_3750:
		seq_printf(m, "Katana 3750\n");
		break;

	case KATANA_ID_750I:
		seq_printf(m, "Katana 750i\n");
		break;

	case KATANA_ID_752I:
		seq_printf(m, "Katana 752i\n");
		break;

	default:
		seq_printf(m, "Unknown\n");
		break;
	}

	seq_printf(m, "product ID\t: 0x%x\n",
		   in_8((volatile char *)(cpld_base + KATANA_CPLD_PRODUCT_ID)));
	seq_printf(m, "hardware rev\t: 0x%x\n",
		   in_8((volatile char *)(cpld_base+KATANA_CPLD_HARDWARE_VER)));
	seq_printf(m, "PLD rev\t\t: 0x%x\n",
		   in_8((volatile char *)(cpld_base + KATANA_CPLD_PLD_VER)));
	seq_printf(m, "PLB freq\t: %ldMhz\n", katana_bus_freq() / 1000000);
	seq_printf(m, "PCI\t\t: %sMonarch\n", katana_is_monarch()? "" : "Non-");

	return 0;
}

static void __init
katana_calibrate_decr(void)
{
	ulong freq;

	freq = katana_bus_freq() / 4;

	printk(KERN_INFO "time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq / 1000000, freq % 1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	return;
}

unsigned long __init
katana_find_end_of_memory(void)
{
	return mv64x60_get_mem_size(KATANA_BRIDGE_REG_BASE,
		MV64x60_TYPE_MV64360);
}

static inline void
katana_set_bat(void)
{
	mb();
	mtspr(DBAT2U, 0xf0001ffe);
	mtspr(DBAT2L, 0xf000002a);
	mb();

	return;
}

#if defined(CONFIG_SERIAL_TEXT_DEBUG) && defined(CONFIG_SERIAL_MPSC_CONSOLE)
static void __init
katana_map_io(void)
{
	io_block_mapping(0xf8100000, 0xf8100000, 0x00020000, _PAGE_IO);
}
#endif

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	isa_mem_base = 0;

	ppc_md.setup_arch = katana_setup_arch;
	ppc_md.show_cpuinfo = katana_show_cpuinfo;
	ppc_md.init_IRQ = mv64360_init_irq;
	ppc_md.get_irq = mv64360_get_irq;
	ppc_md.restart = katana_restart;
	ppc_md.power_off = katana_power_off;
	ppc_md.halt = katana_halt;
	ppc_md.find_end_of_memory = katana_find_end_of_memory;
	ppc_md.calibrate_decr = katana_calibrate_decr;

#if defined(CONFIG_SERIAL_TEXT_DEBUG) && defined(CONFIG_SERIAL_MPSC_CONSOLE)
	ppc_md.setup_io_mappings = katana_map_io;
	ppc_md.progress = mv64x60_mpsc_progress;
	mv64x60_progress_init(KATANA_BRIDGE_REG_BASE);
#endif

#if defined(CONFIG_SERIAL_MPSC) || defined(CONFIG_MV643XX_ETH)
	platform_notify = katana_platform_notify;
#endif

	katana_set_bat(); /* Need for katana_find_end_of_memory and progress */
	return;
}
