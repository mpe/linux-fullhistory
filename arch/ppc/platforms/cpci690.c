/*
 * arch/ppc/platforms/cpci690.c
 *
 * Board setup routines for the Force CPCI690 board.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2003 (c) MontaVista Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This programr
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/mv643xx.h>
#include <asm/bootinfo.h>
#include <asm/machdep.h>
#include <asm/todc.h>
#include <asm/time.h>
#include <asm/mv64x60.h>
#include <platforms/cpci690.h>

#define BOARD_VENDOR	"Force"
#define BOARD_MACHINE	"CPCI690"

/* Set IDE controllers into Native mode? */
#define SET_PCI_IDE_NATIVE

static struct mv64x60_handle	bh;
static u32 cpci690_br_base;

static const unsigned int cpu_7xx[16] = { /* 7xx & 74xx (but not 745x) */
	18, 15, 14, 2, 4, 13, 5, 9, 6, 11, 8, 10, 16, 12, 7, 0
};

TODC_ALLOC();

static int __init
cpci690_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller	*hose = pci_bus_to_hose(dev->bus->number);

	if (hose->index == 0) {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 * 	   A   B   C   D
		 */
		{
			{ 90, 91, 88, 89}, /* IDSEL 30/20 - Sentinel */
		};

		const long min_idsel = 20, max_idsel = 20, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
	else {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 * 	   A   B   C   D
		 */
		{
			{ 93, 94, 95, 92}, /* IDSEL 28/18 - PMC slot 2 */
			{  0,  0,  0,  0}, /* IDSEL 29/19 - Not used */
			{ 94, 95, 92, 93}, /* IDSEL 30/20 - PMC slot 1 */
		};

		const long min_idsel = 18, max_idsel = 20, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
}

static int
cpci690_get_bus_speed(void)
{
	return 133333333;
}

static int
cpci690_get_cpu_speed(void)
{
	unsigned long	hid1;

	hid1 = mfspr(HID1) >> 28;
	return cpci690_get_bus_speed() * cpu_7xx[hid1]/2;
}

#define	KB	(1024UL)
#define	MB	(1024UL * KB)
#define	GB	(1024UL * MB)

unsigned long __init
cpci690_find_end_of_memory(void)
{
	u32		mem_ctlr_size;
	static u32	board_size;
	static u8	first_time = 1;

	if (first_time) {
		/* Using cpci690_set_bat() mapping ==> virt addr == phys addr */
		switch (in_8((u8 *) (cpci690_br_base +
			CPCI690_BR_MEM_CTLR)) & 0x07) {
		case 0x01:
			board_size = 256*MB;
			break;
		case 0x02:
			board_size = 512*MB;
			break;
		case 0x03:
			board_size = 768*MB;
			break;
		case 0x04:
			board_size = 1*GB;
			break;
		case 0x05:
			board_size = 1*GB + 512*MB;
			break;
		case 0x06:
			board_size = 2*GB;
			break;
		default:
			board_size = 0xffffffff; /* use mem ctlr size */
		} /* switch */

		mem_ctlr_size =  mv64x60_get_mem_size(CONFIG_MV64X60_NEW_BASE,
			MV64x60_TYPE_GT64260A);

		/* Check that mem ctlr & board reg agree.  If not, pick MIN. */
		if (board_size != mem_ctlr_size) {
			printk(KERN_WARNING "Board register & memory controller"
				"mem size disagree (board reg: 0x%lx, "
				"mem ctlr: 0x%lx)\n",
				(ulong)board_size, (ulong)mem_ctlr_size);
			board_size = min(board_size, mem_ctlr_size);
		}

		first_time = 0;
	} /* if */

	return board_size;
}

static void __init
cpci690_setup_bridge(void)
{
	struct mv64x60_setup_info	si;
	int				i;

	memset(&si, 0, sizeof(si));

	si.phys_reg_base = CONFIG_MV64X60_NEW_BASE;

	si.pci_0.enable_bus = 1;
	si.pci_0.pci_io.cpu_base = CPCI690_PCI0_IO_START_PROC_ADDR;
	si.pci_0.pci_io.pci_base_hi = 0;
	si.pci_0.pci_io.pci_base_lo = CPCI690_PCI0_IO_START_PCI_ADDR;
	si.pci_0.pci_io.size = CPCI690_PCI0_IO_SIZE;
	si.pci_0.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_0.pci_mem[0].cpu_base = CPCI690_PCI0_MEM_START_PROC_ADDR;
	si.pci_0.pci_mem[0].pci_base_hi = CPCI690_PCI0_MEM_START_PCI_HI_ADDR;
	si.pci_0.pci_mem[0].pci_base_lo = CPCI690_PCI0_MEM_START_PCI_LO_ADDR;
	si.pci_0.pci_mem[0].size = CPCI690_PCI0_MEM_SIZE;
	si.pci_0.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_0.pci_cmd_bits = 0;
	si.pci_0.latency_timer = 0x80;

	si.pci_1.enable_bus = 1;
	si.pci_1.pci_io.cpu_base = CPCI690_PCI1_IO_START_PROC_ADDR;
	si.pci_1.pci_io.pci_base_hi = 0;
	si.pci_1.pci_io.pci_base_lo = CPCI690_PCI1_IO_START_PCI_ADDR;
	si.pci_1.pci_io.size = CPCI690_PCI1_IO_SIZE;
	si.pci_1.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_mem[0].cpu_base = CPCI690_PCI1_MEM_START_PROC_ADDR;
	si.pci_1.pci_mem[0].pci_base_hi = CPCI690_PCI1_MEM_START_PCI_HI_ADDR;
	si.pci_1.pci_mem[0].pci_base_lo = CPCI690_PCI1_MEM_START_PCI_LO_ADDR;
	si.pci_1.pci_mem[0].size = CPCI690_PCI1_MEM_SIZE;
	si.pci_1.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_cmd_bits = 0;
	si.pci_1.latency_timer = 0x80;

	for (i=0; i<MV64x60_CPU2MEM_WINDOWS; i++) {
		si.cpu_prot_options[i] = 0;
		si.cpu_snoop_options[i] = GT64260_CPU_SNOOP_WB;
		si.pci_0.acc_cntl_options[i] =
			GT64260_PCI_ACC_CNTL_DREADEN |
			GT64260_PCI_ACC_CNTL_RDPREFETCH |
			GT64260_PCI_ACC_CNTL_RDLINEPREFETCH |
			GT64260_PCI_ACC_CNTL_RDMULPREFETCH |
			GT64260_PCI_ACC_CNTL_SWAP_NONE |
			GT64260_PCI_ACC_CNTL_MBURST_32_BTYES;
		si.pci_0.snoop_options[i] = GT64260_PCI_SNOOP_WB;
		si.pci_1.acc_cntl_options[i] =
			GT64260_PCI_ACC_CNTL_DREADEN |
			GT64260_PCI_ACC_CNTL_RDPREFETCH |
			GT64260_PCI_ACC_CNTL_RDLINEPREFETCH |
			GT64260_PCI_ACC_CNTL_RDMULPREFETCH |
			GT64260_PCI_ACC_CNTL_SWAP_NONE |
			GT64260_PCI_ACC_CNTL_MBURST_32_BTYES;
		si.pci_1.snoop_options[i] = GT64260_PCI_SNOOP_WB;
	}

        /* Lookup PCI host bridges */
        if (mv64x60_init(&bh, &si))
                printk(KERN_ERR "Bridge initialization failed.\n");

	pci_dram_offset = 0; /* System mem at same addr on PCI & cpu bus */
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = cpci690_map_irq;
	ppc_md.pci_exclude_device = mv64x60_pci_exclude_device;

	mv64x60_set_bus(&bh, 0, 0);
	bh.hose_a->first_busno = 0;
	bh.hose_a->last_busno = 0xff;
	bh.hose_a->last_busno = pciauto_bus_scan(bh.hose_a, 0);

	bh.hose_b->first_busno = bh.hose_a->last_busno + 1;
	mv64x60_set_bus(&bh, 1, bh.hose_b->first_busno);
	bh.hose_b->last_busno = 0xff;
	bh.hose_b->last_busno = pciauto_bus_scan(bh.hose_b,
		bh.hose_b->first_busno);

	return;
}

static void __init
cpci690_setup_peripherals(void)
{
	/* Set up windows to CPLD, RTC/TODC, IPMI. */
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_0_WIN, CPCI690_BR_BASE,
		CPCI690_BR_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_0_WIN);
	cpci690_br_base = (u32)ioremap(CPCI690_BR_BASE, CPCI690_BR_SIZE);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN, CPCI690_TODC_BASE,
		CPCI690_TODC_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_1_WIN);
	TODC_INIT(TODC_TYPE_MK48T35, 0, 0,
			ioremap(CPCI690_TODC_BASE, CPCI690_TODC_SIZE), 8);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_2_WIN, CPCI690_IPMI_BASE,
		CPCI690_IPMI_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_2_WIN);

	mv64x60_set_bits(&bh, MV64x60_PCI0_ARBITER_CNTL, (1<<31));
	mv64x60_set_bits(&bh, MV64x60_PCI1_ARBITER_CNTL, (1<<31));

        mv64x60_set_bits(&bh, MV64x60_CPU_MASTER_CNTL, (1<<9)); /* Only 1 cpu */

	/*
	 * Turn off timer/counters.  Not turning off watchdog timer because
	 * can't read its reg on the 64260A so don't know if we'll be enabling
	 * or disabling.
	 */
	mv64x60_clr_bits(&bh, MV64x60_TIMR_CNTR_0_3_CNTL,
			((1<<0) | (1<<8) | (1<<16) | (1<<24)));
	mv64x60_clr_bits(&bh, GT64260_TIMR_CNTR_4_7_CNTL,
			((1<<0) | (1<<8) | (1<<16) | (1<<24)));

	/*
	 * Set MPSC Multiplex RMII
	 * NOTE: ethernet driver modifies bit 0 and 1
	 */
	mv64x60_write(&bh, GT64260_MPP_SERIAL_PORTS_MULTIPLEX, 0x00001102);

#define GPP_EXTERNAL_INTERRUPTS \
		((1<<24) | (1<<25) | (1<<26) | (1<<27) | \
		 (1<<28) | (1<<29) | (1<<30) | (1<<31))
	/* PCI interrupts are inputs */
	mv64x60_clr_bits(&bh, MV64x60_GPP_IO_CNTL, GPP_EXTERNAL_INTERRUPTS);
	/* PCI interrupts are active low */
	mv64x60_set_bits(&bh, MV64x60_GPP_LEVEL_CNTL, GPP_EXTERNAL_INTERRUPTS);

	/* Clear any pending interrupts for these inputs and enable them. */
	mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, ~GPP_EXTERNAL_INTERRUPTS);
	mv64x60_set_bits(&bh, MV64x60_GPP_INTR_MASK, GPP_EXTERNAL_INTERRUPTS);

	/* Route MPP interrupt inputs to GPP */
	mv64x60_write(&bh, MV64x60_MPP_CNTL_2, 0x00000000);
	mv64x60_write(&bh, MV64x60_MPP_CNTL_3, 0x00000000);

	return;
}

static void __init
cpci690_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("cpci690_setup_arch: enter", 0);
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

	if (ppc_md.progress)
		ppc_md.progress("cpci690_setup_arch: Enabling L2 cache", 0);

	/* Enable L2 and L3 caches (if 745x) */
	_set_L2CR(_get_L2CR() | L2CR_L2E);
	_set_L3CR(_get_L3CR() | L3CR_L3E);

	if (ppc_md.progress)
		ppc_md.progress("cpci690_setup_arch: Initializing bridge", 0);

	cpci690_setup_bridge();		/* set up PCI bridge(s) */
	cpci690_setup_peripherals();	/* set up chip selects/GPP/MPP etc */

	if (ppc_md.progress)
		ppc_md.progress("cpci690_setup_arch: bridge init complete", 0);

	printk(KERN_INFO "%s %s port (C) 2003 MontaVista Software, Inc. "
		"(source@mvista.com)\n", BOARD_VENDOR, BOARD_MACHINE);

	if (ppc_md.progress)
		ppc_md.progress("cpci690_setup_arch: exit", 0);

	return;
}

/* Platform device data fixup routines. */
#if defined(CONFIG_SERIAL_MPSC)
static void __init
cpci690_fixup_mpsc_pdata(struct platform_device *pdev)
{
	struct mpsc_pdata *pdata;

	pdata = (struct mpsc_pdata *)pdev->dev.platform_data;

	pdata->max_idle = 40;
	pdata->default_baud = 9600;
	pdata->brg_clk_src = 8;
	pdata->brg_clk_freq = 133000000;

	return;
}

static int __init
cpci690_platform_notify(struct device *dev)
{
	static struct {
		char	*bus_id;
		void	((*rtn)(struct platform_device *pdev));
	} dev_map[] = {
		{ MPSC_CTLR_NAME "0", cpci690_fixup_mpsc_pdata },
		{ MPSC_CTLR_NAME "1", cpci690_fixup_mpsc_pdata },
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
#endif

static void
cpci690_reset_board(void)
{
	u32	i = 10000;

	local_irq_disable();
	out_8((u8 *)(cpci690_br_base + CPCI690_BR_SW_RESET), 0x11);

	while (i != 0) i++;
	panic("restart failed\n");
}

static void
cpci690_restart(char *cmd)
{
	cpci690_reset_board();
}

static void
cpci690_halt(void)
{
	while (1);
	/* NOTREACHED */
}

static void
cpci690_power_off(void)
{
	cpci690_halt();
	/* NOTREACHED */
}

static int
cpci690_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: " BOARD_VENDOR "\n");
	seq_printf(m, "machine\t\t: " BOARD_MACHINE "\n");
	seq_printf(m, "cpu MHz\t\t: %d\n", cpci690_get_cpu_speed()/1000/1000);
	seq_printf(m, "bus MHz\t\t: %d\n", cpci690_get_bus_speed()/1000/1000);

	return 0;
}

static void __init
cpci690_calibrate_decr(void)
{
	ulong freq;

	freq = cpci690_get_bus_speed()/4;

	printk(KERN_INFO "time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq/1000000, freq%1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	return;
}

static __inline__ void
cpci690_set_bat(u32 addr, u32 size)
{
	addr &= 0xfffe0000;
	size &= 0x1ffe0000;
	size = ((size >> 17) - 1) << 2;

	mb();
	mtspr(DBAT1U, addr | size | 0x2); /* Vs == 1; Vp == 0 */
	mtspr(DBAT1L, addr | 0x2a); /* WIMG bits == 0101; PP == r/w access */
	mb();

	return;
}

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
static void __init
cpci690_map_io(void)
{
	io_block_mapping(CONFIG_MV64X60_NEW_BASE, CONFIG_MV64X60_NEW_BASE,
		128 * KB, _PAGE_IO);
}
#endif

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
#ifdef CONFIG_BLK_DEV_INITRD
	initrd_start=initrd_end=0;
	initrd_below_start_ok=0;
#endif /* CONFIG_BLK_DEV_INITRD */

	parse_bootinfo(find_bootinfo());

	loops_per_jiffy = cpci690_get_cpu_speed() / HZ;

	isa_mem_base = 0;

	ppc_md.setup_arch = cpci690_setup_arch;
	ppc_md.show_cpuinfo = cpci690_show_cpuinfo;
	ppc_md.init_IRQ = gt64260_init_irq;
	ppc_md.get_irq = gt64260_get_irq;
	ppc_md.restart = cpci690_restart;
	ppc_md.power_off = cpci690_power_off;
	ppc_md.halt = cpci690_halt;
	ppc_md.find_end_of_memory = cpci690_find_end_of_memory;
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
	ppc_md.calibrate_decr = cpci690_calibrate_decr;

	/*
	 * Need to map in board regs (used by cpci690_find_end_of_memory())
	 * and the bridge's regs (used by progress);
	 */
	cpci690_set_bat(CPCI690_BR_BASE, 32 * MB);
	cpci690_br_base = CPCI690_BR_BASE;

#ifdef	CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.setup_io_mappings = cpci690_map_io;
	ppc_md.progress = mv64x60_mpsc_progress;
	mv64x60_progress_init(CONFIG_MV64X60_NEW_BASE);
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */
#ifdef	CONFIG_KGDB
	ppc_md.setup_io_mappings = cpci690_map_io;
	ppc_md.early_serial_map = cpci690_early_serial_map;
#endif	/* CONFIG_KGDB */

#if defined(CONFIG_SERIAL_MPSC)
	platform_notify = cpci690_platform_notify;
#endif

	return;
}
