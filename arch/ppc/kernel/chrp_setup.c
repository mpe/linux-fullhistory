/*
 * BK Id: SCCS/s.chrp_setup.c 1.38 11/13/01 21:26:07 paulus
 */
/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/blk.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/adb.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/seq_file.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/gg2.h>
#include <asm/pci-bridge.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/hydra.h>
#include <asm/keyboard.h>
#include <asm/sections.h>
#include <asm/time.h>
#include <asm/btext.h>

#include "local_irq.h"
#include "i8259.h"
#include "open_pic.h"
#include "xics.h"

unsigned long chrp_get_rtc_time(void);
int chrp_set_rtc_time(unsigned long nowtime);
void chrp_calibrate_decr(void);
long chrp_time_init(void);

void chrp_find_bridges(void);
void chrp_event_scan(void);
void rtas_display_progress(char *, unsigned short);
void rtas_indicator_progress(char *, unsigned short);
void btext_progress(char *, unsigned short);

extern unsigned long pmac_find_end_of_memory(void);
extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern unsigned char pckbd_sysrq_xlate[128];
extern void select_adb_keyboard(void);
extern int of_show_percpuinfo(struct seq_file *, int);

extern kdev_t boot_dev;

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern int probingmem;
extern unsigned long loops_per_jiffy;
static int max_width;

#ifdef CONFIG_SMP
extern struct smp_ops_t chrp_smp_ops;
extern struct smp_ops_t xics_smp_ops;
#endif

static const char *gg2_memtypes[4] = {
	"FPM", "SDRAM", "EDO", "BEDO"
};
static const char *gg2_cachesizes[4] = {
	"256 KB", "512 KB", "1 MB", "Reserved"
};
static const char *gg2_cachetypes[4] = {
	"Asynchronous", "Reserved", "Flow-Through Synchronous",
	"Pipelined Synchronous"
};
static const char *gg2_cachemodes[4] = {
	"Disabled", "Write-Through", "Copy-Back", "Transparent Mode"
};

int __chrp
chrp_show_cpuinfo(struct seq_file *m)
{
	int i, sdramen;
	unsigned int t;
	struct device_node *root;
	const char *model = "";

	root = find_path_device("/");
	if (root)
		model = get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);

	/* longtrail (goldengate) stuff */
	if (!strncmp(model, "IBM,LongTrail", 13)) {
		/* VLSI VAS96011/12 `Golden Gate 2' */
		/* Memory banks */
		sdramen = (in_le32((unsigned *)(GG2_PCI_CONFIG_BASE+
						GG2_PCI_DRAM_CTRL))
			   >>31) & 1;
		for (i = 0; i < (sdramen ? 4 : 6); i++) {
			t = in_le32((unsigned *)(GG2_PCI_CONFIG_BASE+
						 GG2_PCI_DRAM_BANK0+
						 i*4));
			if (!(t & 1))
				continue;
			switch ((t>>8) & 0x1f) {
			case 0x1f:
				model = "4 MB";
				break;
			case 0x1e:
				model = "8 MB";
				break;
			case 0x1c:
				model = "16 MB";
				break;
			case 0x18:
				model = "32 MB";
				break;
			case 0x10:
				model = "64 MB";
				break;
			case 0x00:
				model = "128 MB";
				break;
			default:
				model = "Reserved";
				break;
			}
			seq_printf(m, "memory bank %d\t: %s %s\n", i, model,
				   gg2_memtypes[sdramen ? 1 : ((t>>1) & 3)]);
		}
		/* L2 cache */
		t = in_le32((unsigned *)(GG2_PCI_CONFIG_BASE+GG2_PCI_CC_CTRL));
		seq_printf(m, "board l2\t: %s %s (%s)\n",
			   gg2_cachesizes[(t>>7) & 3],
			   gg2_cachetypes[(t>>2) & 3],
			   gg2_cachemodes[t & 3]);
	}
	return 0;
}

/*
 *  Fixes for the National Semiconductor PC78308VUL SuperI/O
 *
 *  Some versions of Open Firmware incorrectly initialize the IRQ settings
 *  for keyboard and mouse
 */
static inline void __init sio_write(u8 val, u8 index)
{
	outb(index, 0x15c);
	outb(val, 0x15d);
}

static inline u8 __init sio_read(u8 index)
{
	outb(index, 0x15c);
	return inb(0x15d);
}

static void __init sio_fixup_irq(const char *name, u8 device, u8 level,
				     u8 type)
{
	u8 level0, type0, active;

	/* select logical device */
	sio_write(device, 0x07);
	active = sio_read(0x30);
	level0 = sio_read(0x70);
	type0 = sio_read(0x71);
	if (level0 != level || type0 != type || !active) {
		printk(KERN_WARNING "sio: %s irq level %d, type %d, %sactive: "
		       "remapping to level %d, type %d, active\n",
		       name, level0, type0, !active ? "in" : "", level, type);
		sio_write(0x01, 0x30);
		sio_write(level, 0x70);
		sio_write(type, 0x71);
	}
}

static void __init sio_init(void)
{
	struct device_node *root;

	if ((root = find_path_device("/")) &&
	    !strncmp(get_property(root, "model", NULL), "IBM,LongTrail", 13)) {
		/* logical device 0 (KBC/Keyboard) */
		sio_fixup_irq("keyboard", 0, 1, 2);
		/* select logical device 1 (KBC/Mouse) */
		sio_fixup_irq("mouse", 1, 12, 2);
	}
}


void __init
chrp_setup_arch(void)
{
	struct device_node *device;

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

#ifdef CONFIG_BLK_DEV_INITRD
	/* this is fine for chrp */
	initrd_below_start_ok = 1;
	
	if (initrd_start)
		ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	else
#endif
		ROOT_DEV = to_kdev_t(0x0802); /* sda2 (sda1 is for the kernel) */

	/* Lookup PCI host bridges */
	chrp_find_bridges();

#ifndef CONFIG_PPC64BRIDGE
	/*
	 *  Temporary fixes for PCI devices.
	 *  -- Geert
	 */
	hydra_init();		/* Mac I/O */

#endif /* CONFIG_PPC64BRIDGE */

	/* Some IBM machines don't have the hydra -- Cort */
	if (!OpenPIC_Addr) {
		struct device_node *root;
		unsigned long *opprop;
		int n;

		root = find_path_device("/");
		opprop = (unsigned long *) get_property
			(root, "platform-open-pic", NULL);
		n = prom_n_addr_cells(root);
		if (opprop != 0) {
			printk("OpenPIC addrs: %lx %lx %lx\n",
			       opprop[n-1], opprop[2*n-1], opprop[3*n-1]);
			OpenPIC_Addr = ioremap(opprop[n-1], 0x40000);
		}
	}

	/*
	 *  Fix the Super I/O configuration
	 */
	sio_init();

	/*
	 *  Setup the console operations
	 */
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	/* Get the event scan rate for the rtas so we know how
	 * often it expects a heartbeat. -- Cort
	 */
	if ( rtas_data ) {
		struct property *p;
		device = find_devices("rtas");
		for ( p = device->properties;
		      p && strncmp(p->name, "rtas-event-scan-rate", 20);
		      p = p->next )
			/* nothing */ ;
		if ( p && *(unsigned long *)p->value ) {
			ppc_md.heartbeat = chrp_event_scan;
			ppc_md.heartbeat_reset = (HZ/(*(unsigned long *)p->value)*30)-1;
			ppc_md.heartbeat_count = 1;
			printk("RTAS Event Scan Rate: %lu (%lu jiffies)\n",
			       *(unsigned long *)p->value, ppc_md.heartbeat_reset );
		}
	}
}

void __chrp
chrp_event_scan(void)
{
	unsigned char log[1024];
	unsigned long ret = 0;
	/* XXX: we should loop until the hardware says no more error logs -- Cort */
	call_rtas( "event-scan", 4, 1, &ret, 0xffffffff, 0,
		   __pa(log), 1024 );
	ppc_md.heartbeat_count = ppc_md.heartbeat_reset;
}
	
void __chrp
chrp_restart(char *cmd)
{
	printk("RTAS system-reboot returned %d\n",
	       call_rtas("system-reboot", 0, 1, NULL));
	for (;;);
}

void __chrp
chrp_power_off(void)
{
	/* allow power on only with power button press */
	printk("RTAS power-off returned %d\n",
	       call_rtas("power-off", 2, 1, NULL,0xffffffff,0xffffffff));
	for (;;);
}

void __chrp
chrp_halt(void)
{
	chrp_power_off();
}

u_int __chrp
chrp_irq_cannonicalize(u_int irq)
{
	if (irq == 2)
		return 9;
	return irq;
}

void __init chrp_init_IRQ(void)
{
	struct device_node *np;
	int i;
	unsigned int *addrp;
	unsigned char* chrp_int_ack_special = 0;
	unsigned char init_senses[NR_IRQS - NUM_8259_INTERRUPTS];
	int nmi_irq = -1;
#if defined(CONFIG_VT) && defined(CONFIG_ADB_KEYBOARD) && defined(XMON)	
	struct device_node *kbd;
#endif

	if (!(np = find_devices("pci"))
	    || !(addrp = (unsigned int *)
		 get_property(np, "8259-interrupt-acknowledge", NULL)))
		printk("Cannot find pci to get ack address\n");
	else
		chrp_int_ack_special = (unsigned char *)
			ioremap(addrp[prom_n_addr_cells(np)-1], 1);
	/* hydra still sets OpenPIC_InitSenses to a static set of values */
	if (OpenPIC_InitSenses == NULL) {
		prom_get_irq_senses(init_senses, NUM_8259_INTERRUPTS, NR_IRQS);
		OpenPIC_InitSenses = init_senses;
		OpenPIC_NumInitSenses = NR_IRQS - NUM_8259_INTERRUPTS;
	}
	openpic_init(1, NUM_8259_INTERRUPTS, chrp_int_ack_special, nmi_irq);
	for ( i = 0 ; i < NUM_8259_INTERRUPTS  ; i++ )
		irq_desc[i].handler = &i8259_pic;
	i8259_init();
#if defined(CONFIG_VT) && defined(CONFIG_ADB_KEYBOARD) && defined(XMON)
	/* see if there is a keyboard in the device tree
	   with a parent of type "adb" */
	for (kbd = find_devices("keyboard"); kbd; kbd = kbd->next)
		if (kbd->parent && kbd->parent->type
		    && strcmp(kbd->parent->type, "adb") == 0)
			break;
	if (kbd)
		request_irq( HYDRA_INT_ADB_NMI, xmon_irq, 0, "XMON break", 0);
#endif
}

void __init
chrp_init2(void)
{
#ifdef CONFIG_NVRAM  
	pmac_nvram_init();
#endif

	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");

	if (ppc_md.progress)
		ppc_md.progress("  Have fun!    ", 0x7777);

#if defined(CONFIG_VT) && (defined(CONFIG_ADB_KEYBOARD) || defined(CONFIG_INPUT))
	/* see if there is a keyboard in the device tree
	   with a parent of type "adb" */
	{
		struct device_node *kbd;

		for (kbd = find_devices("keyboard"); kbd; kbd = kbd->next) {
			if (kbd->parent && kbd->parent->type
			    && strcmp(kbd->parent->type, "adb") == 0) {
				select_adb_keyboard();
				break;
			}
		}
	}
#endif /* CONFIG_VT && (CONFIG_ADB_KEYBOARD || CONFIG_INPUT) */
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE stuff.
 */

static int __chrp
chrp_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
        return check_region(from, extent);
}

static void __chrp
chrp_ide_request_region(ide_ioreg_t from,
			unsigned int extent,
			const char *name)
{
        request_region(from, extent, name);
}

static void __chrp
chrp_ide_release_region(ide_ioreg_t from,
			unsigned int extent)
{
        release_region(from, extent);
}

static void __chrp
chrp_ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
}
#endif

/*
 * One of the main thing these mappings are needed for is so that
 * xmon can get to the serial port early on.  We probably should
 * handle the machines with the mpc106 as well as the python (F50)
 * and the GG2 (longtrail).  Actually we should look in the device
 * tree and do the right thing.
 */
static void __init
chrp_map_io(void)
{
	char *name;

	/*
	 * The code below tends to get removed, please don't take it out.
	 * The F50 needs this mapping and it you take it out I'll track you
	 * down and slap your hands.  If it causes problems please email me.
	 *  -- Cort <cort@fsmlabs.com>
	 */
	name = get_property(find_path_device("/"), "name", NULL);
	if (name && strncmp(name, "IBM-70", 6) == 0
	    && strstr(name, "-F50")) {
		io_block_mapping(0x80000000, 0x80000000, 0x10000000, _PAGE_IO);
		io_block_mapping(0x90000000, 0x90000000, 0x10000000, _PAGE_IO);
		return;
	} else {
		io_block_mapping(0xf8000000, 0xf8000000, 0x04000000, _PAGE_IO);
	}
}

void __init
chrp_init(unsigned long r3, unsigned long r4, unsigned long r5,
	  unsigned long r6, unsigned long r7)
{
#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if ( r6 )
	{
		initrd_start = r6 + KERNELBASE;
		initrd_end = r6 + r7 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;
	isa_io_base = CHRP_ISA_IO_BASE;		/* default value */

	ppc_md.setup_arch     = chrp_setup_arch;
	ppc_md.show_percpuinfo = of_show_percpuinfo;
	ppc_md.show_cpuinfo   = chrp_show_cpuinfo;
	ppc_md.irq_cannonicalize = chrp_irq_cannonicalize;
#ifndef CONFIG_POWER4
	ppc_md.init_IRQ       = chrp_init_IRQ;
	ppc_md.get_irq        = openpic_get_irq;
#else
	ppc_md.init_IRQ	      = xics_init_IRQ;
	ppc_md.get_irq	      = xics_get_irq;
#endif /* CONFIG_POWER4 */

	ppc_md.init           = chrp_init2;

	ppc_md.restart        = chrp_restart;
	ppc_md.power_off      = chrp_power_off;
	ppc_md.halt           = chrp_halt;

	ppc_md.time_init      = chrp_time_init;
	ppc_md.set_rtc_time   = chrp_set_rtc_time;
	ppc_md.get_rtc_time   = chrp_get_rtc_time;
	ppc_md.calibrate_decr = chrp_calibrate_decr;

	ppc_md.find_end_of_memory = pmac_find_end_of_memory;
	ppc_md.setup_io_mappings = chrp_map_io;

#ifdef CONFIG_VT
	/* these are adjusted in chrp_init2 if we have an ADB keyboard */
	ppc_md.kbd_setkeycode    = pckbd_setkeycode;
	ppc_md.kbd_getkeycode    = pckbd_getkeycode;
	ppc_md.kbd_translate     = pckbd_translate;
	ppc_md.kbd_unexpected_up = pckbd_unexpected_up;
	ppc_md.kbd_leds          = pckbd_leds;
	ppc_md.kbd_init_hw       = pckbd_init_hw;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.ppc_kbd_sysrq_xlate	 = pckbd_sysrq_xlate;
	SYSRQ_KEY = 0x54;
#endif /* CONFIG_MAGIC_SYSRQ */
#endif /* CONFIG_VT */

	if (rtas_data) {
		struct device_node *rtas;
		unsigned int *p;

		rtas = find_devices("rtas");
		if (rtas != NULL) {
			if (get_property(rtas, "display-character", NULL)) {
				ppc_md.progress = rtas_display_progress;
				p = (unsigned int *) get_property
				       (rtas, "ibm,display-line-length", NULL);
				if (p)
					max_width = *p;
			} else if (get_property(rtas, "set-indicator", NULL))
				ppc_md.progress = rtas_indicator_progress;
		}
	}
#ifdef CONFIG_BOOTX_TEXT
	if (ppc_md.progress == NULL && boot_text_mapped)
		ppc_md.progress = btext_progress;
#endif

#ifdef CONFIG_SMP
#ifndef CONFIG_POWER4
	ppc_md.smp_ops = &chrp_smp_ops;
#else
	ppc_md.smp_ops = &xics_smp_ops;
#endif /* CONFIG_POWER4 */
#endif /* CONFIG_SMP */

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
        ppc_ide_md.ide_check_region = chrp_ide_check_region;
        ppc_ide_md.ide_request_region = chrp_ide_request_region;
        ppc_ide_md.ide_release_region = chrp_ide_release_region;
        ppc_ide_md.ide_init_hwif = chrp_ide_init_hwif_ports;
#endif

	/*
	 * Print the banner, then scroll down so boot progress
	 * can be printed.  -- Cort 
	 */
	if ( ppc_md.progress ) ppc_md.progress("Linux/PPC "UTS_RELEASE"\n", 0x0);
}

void __chrp
rtas_display_progress(char *s, unsigned short hex)
{
	int width;
	char *os = s;

	if ( call_rtas( "display-character", 1, 1, NULL, '\r' ) )
		return;

	width = max_width;
	while ( *os )
	{
		if ( (*os == '\n') || (*os == '\r') )
			width = max_width;
		else
			width--;
		call_rtas( "display-character", 1, 1, NULL, *os++ );
		/* if we overwrite the screen length */
		if ( width == 0 )
			while ( (*os != 0) && (*os != '\n') && (*os != '\r') )
				os++;
	}

	/*while ( width-- > 0 )*/
	call_rtas( "display-character", 1, 1, NULL, ' ' );
}

void __chrp
rtas_indicator_progress(char *s, unsigned short hex)
{
	call_rtas("set-indicator", 3, 1, NULL, 6, 0, hex);
}

#ifdef CONFIG_BOOTX_TEXT
void
btext_progress(char *s, unsigned short hex)
{
	prom_print(s);
	prom_print("\n");
}
#endif /* CONFIG_BOOTX_TEXT */
