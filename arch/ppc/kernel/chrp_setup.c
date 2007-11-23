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
#include <linux/malloc.h>
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
#include <linux/openpic.h>
#include <linux/version.h>
#include <linux/adb.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ide.h>

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
#include <asm/init.h>

#include "time.h"
#include "local_irq.h"
#include "i8259.h"
#include "open_pic.h"

extern volatile unsigned char *chrp_int_ack_special;

unsigned long chrp_get_rtc_time(void);
int chrp_set_rtc_time(unsigned long nowtime);
void chrp_calibrate_decr(void);
void chrp_time_init(void);

void chrp_setup_pci_ptrs(void);
extern void chrp_progress(char *, unsigned short);
void chrp_event_scan(void);

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern unsigned char pckbd_sysrq_xlate[128];
extern int mackbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int mackbd_getkeycode(unsigned int scancode);
extern int mackbd_translate(unsigned char scancode, unsigned char *keycode,
			    char raw_mode);
extern char mackbd_unexpected_up(unsigned char keycode);
extern void mackbd_leds(unsigned char leds);
extern void mackbd_init_hw(void);
extern unsigned char mackbd_sysrq_xlate[128];

kdev_t boot_dev;

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern int probingmem;
extern unsigned long loops_per_sec;

unsigned long empty_zero_page[1024];

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
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
chrp_get_cpuinfo(char *buffer)
{
	int i, len, sdramen;
	unsigned int t;
	struct device_node *root;
	const char *model = "";

	root = find_path_device("/");
	if (root)
		model = get_property(root, "model", NULL);
	len = sprintf(buffer,"machine\t\t: CHRP %s\n", model);

	/* longtrail (goldengate) stuff */
	if ( !strncmp( model, "IBM,LongTrail", 13 ) )
	{
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
			len += sprintf(buffer+len, "memory bank %d\t: %s %s\n", i, model,
				       gg2_memtypes[sdramen ? 1 : ((t>>1) & 3)]);
		}
		/* L2 cache */
		t = in_le32((unsigned *)(GG2_PCI_CONFIG_BASE+GG2_PCI_CC_CTRL));
		len += sprintf(buffer+len, "board l2\t: %s %s (%s)\n",
			       gg2_cachesizes[(t>>7) & 3],
			       gg2_cachetypes[(t>>2) & 3],
			       gg2_cachemodes[t & 3]);
	}
	return len;
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
	struct device_node *root;
	
	root = find_path_device("/");
	if (root &&
	    !strncmp(get_property(root, "model", NULL), "IBM,LongTrail", 13 ) )
	{
		/* select logical device */
		sio_write(device, 0x07);
		active = sio_read(0x30);
		level0 = sio_read(0x70);
		type0 = sio_read(0x71);
		printk("sio: %s irq level %d, type %d, %sactive: ", name, level0, type0,
		       !active ? "in" : "");
		if (level0 == level && type0 == type && active)
			printk("OK\n");
		else {
			printk("remapping to level %d, type %d, active\n", level, type);
			sio_write(0x01, 0x30);
			sio_write(level, 0x70);
			sio_write(type, 0x71);
		}
	}

}

static void __init sio_init(void)
{
	/* logical device 0 (KBC/Keyboard) */
	sio_fixup_irq("keyboard", 0, 1, 2);
	/* select logical device 1 (KBC/Mouse) */
	sio_fixup_irq("mouse", 1, 12, 2);
}


void __init
chrp_setup_arch(void)
{
	extern char cmd_line[];
	struct device_node *device;

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_sec = 50000000;

#ifdef CONFIG_BLK_DEV_INITRD
	/* this is fine for chrp */
	initrd_below_start_ok = 1;
	
	if (initrd_start)
		ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	else
#endif
		ROOT_DEV = to_kdev_t(0x0802); /* sda2 (sda1 is for the kernel) */
	printk("Boot arguments: %s\n", cmd_line);
	
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");

	/* PCI bridge config space access area -
	 * appears to be not in devtree on longtrail. */
	ioremap(GG2_PCI_CONFIG_BASE, 0x80000);
	/*
	 *  Temporary fixes for PCI devices.
	 *  -- Geert
	 */
	hydra_init();		/* Mac I/O */

	/* Some IBM machines don't have the hydra -- Cort */
	if ( !OpenPIC )
	{
		OpenPIC = (struct OpenPIC *)*(unsigned long *)get_property(
			find_path_device("/"), "platform-open-pic", NULL);
		OpenPIC = ioremap((unsigned long)OpenPIC, sizeof(struct OpenPIC));
	}

	/*
	 *  Fix the Super I/O configuration
	 */
	sio_init();
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif
	pmac_find_bridges();

	/* Get the event scan rate for the rtas so we know how
	 * often it expects a heartbeat. -- Cort
	 */
	if ( rtas_data )
	{
		struct property *p;
		device = find_devices("rtas");
		for ( p = device->properties;
		      p && strncmp(p->name, "rtas-event-scan-rate", 20);
		      p = p->next )
			/* nothing */ ;
		if ( p && *(unsigned long *)p->value )
		{
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
	{
		return 9;
	}
	else
	{
		return irq;
	}
}

int __chrp chrp_get_irq( struct pt_regs *regs )
{
        int irq;

        irq = openpic_irq( smp_processor_id() );
        if (irq == IRQ_8259_CASCADE)
        {
                /*
                 * This magic address generates a PCI IACK cycle.
                 */
		if ( chrp_int_ack_special )
			irq = *chrp_int_ack_special;
		else
			irq = i8259_irq( smp_processor_id() );
		openpic_eoi( smp_processor_id() );
        }
        if (irq == OPENPIC_VEC_SPURIOUS)
                /*
                 * Spurious interrupts should never be
                 * acknowledged
                 */
		irq = -1;
	/*
	 * I would like to openpic_eoi here but there seem to be timing problems
	 * between the openpic ack and the openpic eoi.
	 *   -- Cort
	 */
	return irq;
}

void __chrp chrp_post_irq(struct pt_regs* regs, int irq)
{
	/*
	 * If it's an i8259 irq then we've already done the
	 * openpic irq.  So we just check to make sure the controller
	 * is an openpic and if it is then eoi
	 *
	 * We do it this way since our irq_desc[irq].handler can change
	 * with RTL and no longer be open_pic -- Cort
	 */
	if ( irq >= open_pic_irq_offset)
		openpic_eoi( smp_processor_id() );
}

void __init chrp_init_IRQ(void)
{
	struct device_node *np;
	int i;

	if ( !(np = find_devices("pci") ) )
		printk("Cannot find pci to get ack address\n");
	else
	{
		chrp_int_ack_special = (volatile unsigned char *)
			(*(unsigned long *)get_property(np,
							"8259-interrupt-acknowledge", NULL));
	}
	open_pic_irq_offset = 16;
	for ( i = 16 ; i < NR_IRQS ; i++ )
		irq_desc[i].handler = &open_pic;
	openpic_init(1);
	enable_irq(IRQ_8259_CASCADE);
	for ( i = 0 ; i < 16  ; i++ )
		irq_desc[i].handler = &i8259_pic;
	i8259_init();
#ifdef CONFIG_XMON
	request_irq(openpic_to_irq(HYDRA_INT_ADB_NMI),
		    xmon_irq, 0, "NMI", 0);
#endif	/* CONFIG_XMON */
#ifdef CONFIG_SMP
	request_irq(openpic_to_irq(OPENPIC_VEC_IPI),
		    openpic_ipi_action, 0, "IPI0", 0);
#endif	/* CONFIG_SMP */
}

void __init
chrp_init2(void)
{
#ifdef CONFIG_NVRAM  
	pmac_nvram_init();
#endif
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE stuff.
 */
unsigned int chrp_ide_irq = 0;
int chrp_ide_ports_known = 0;
ide_ioreg_t chrp_ide_regbase[MAX_HWIFS];
ide_ioreg_t chrp_idedma_regbase;

void __chrp
chrp_ide_probe(void)
{
        struct pci_dev *pdev = pci_find_device(PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_82C105, NULL);

        chrp_ide_ports_known = 1;

        if(pdev) {
                chrp_ide_regbase[0]=pdev->resource[0].start;
                chrp_ide_regbase[1]=pdev->resource[2].start;
                chrp_idedma_regbase=pdev->resource[4].start;
                chrp_ide_irq=pdev->irq;
        }
}

void __chrp
chrp_ide_insw(ide_ioreg_t port, void *buf, int ns)
{
	ide_insw(port+_IO_BASE, buf, ns);
}

void __chrp
chrp_ide_outsw(ide_ioreg_t port, void *buf, int ns)
{
	ide_outsw(port+_IO_BASE, buf, ns);
}

int __chrp
chrp_ide_default_irq(ide_ioreg_t base)
{
        if (chrp_ide_ports_known == 0)
	        chrp_ide_probe();
	return chrp_ide_irq;
}

ide_ioreg_t __chrp
chrp_ide_default_io_base(int index)
{
        if (chrp_ide_ports_known == 0)
	        chrp_ide_probe();
	return chrp_ide_regbase[index];
}

int __chrp
chrp_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
        return check_region(from, extent);
}

void __chrp
chrp_ide_request_region(ide_ioreg_t from,
			unsigned int extent,
			const char *name)
{
        request_region(from, extent, name);
}

void __chrp
chrp_ide_release_region(ide_ioreg_t from,
			unsigned int extent)
{
        release_region(from, extent);
}

void __chrp
chrp_ide_fix_driveid(struct hd_driveid *id)
{
        ppc_generic_ide_fix_driveid(id);
}

void __chrp
chrp_ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = 0;
	}
	if (irq != NULL)
		hw->irq = chrp_ide_irq;
}

#endif

void __init
	   chrp_init(unsigned long r3, unsigned long r4, unsigned long r5,
		     unsigned long r6, unsigned long r7)
{
	chrp_setup_pci_ptrs();
#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if ( r6 )
	{
		initrd_start = r6 + KERNELBASE;
		initrd_end = r6 + r7 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */

        /* pci_dram_offset/isa_io_base/isa_mem_base set by setup_pci_ptrs() */
	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	ppc_md.setup_arch     = chrp_setup_arch;
	ppc_md.setup_residual = NULL;
	ppc_md.get_cpuinfo    = chrp_get_cpuinfo;
	ppc_md.irq_cannonicalize = chrp_irq_cannonicalize;
	ppc_md.init_IRQ       = chrp_init_IRQ;
	ppc_md.get_irq        = chrp_get_irq;
	ppc_md.post_irq	      = chrp_post_irq;
		
	ppc_md.init           = chrp_init2;

	ppc_md.restart        = chrp_restart;
	ppc_md.power_off      = chrp_power_off;
	ppc_md.halt           = chrp_halt;

	ppc_md.time_init      = chrp_time_init;
	ppc_md.set_rtc_time   = chrp_set_rtc_time;
	ppc_md.get_rtc_time   = chrp_get_rtc_time;
	ppc_md.calibrate_decr = chrp_calibrate_decr;

#ifdef CONFIG_VT
#ifdef CONFIG_MAC_KEYBOARD
	if (adb_driver == NULL)
	{
#endif /* CONFIG_MAC_KEYBOAD */
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
#ifdef CONFIG_MAC_KEYBOARD
	}
	else
	{
		ppc_md.kbd_setkeycode    = mackbd_setkeycode;
		ppc_md.kbd_getkeycode    = mackbd_getkeycode;
		ppc_md.kbd_translate     = mackbd_translate;
		ppc_md.kbd_unexpected_up = mackbd_unexpected_up;
		ppc_md.kbd_leds          = mackbd_leds;
		ppc_md.kbd_init_hw       = mackbd_init_hw;
#ifdef CONFIG_MAGIC_SYSRQ
		ppc_md.ppc_kbd_sysrq_xlate	 = mackbd_sysrq_xlate;
		SYSRQ_KEY = 0x69;
#endif /* CONFIG_MAGIC_SYSRQ */
	}
#endif /* CONFIG_MAC_KEYBOARD */
#endif /* CONFIG_VT */
	if ( rtas_data )
		ppc_md.progress = chrp_progress;
	
#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
        ppc_ide_md.insw = chrp_ide_insw;
        ppc_ide_md.outsw = chrp_ide_outsw;
        ppc_ide_md.default_irq = chrp_ide_default_irq;
        ppc_ide_md.default_io_base = chrp_ide_default_io_base;
        ppc_ide_md.ide_check_region = chrp_ide_check_region;
        ppc_ide_md.ide_request_region = chrp_ide_request_region;
        ppc_ide_md.ide_release_region = chrp_ide_release_region;
        ppc_ide_md.fix_driveid = chrp_ide_fix_driveid;
        ppc_ide_md.ide_init_hwif = chrp_ide_init_hwif_ports;

        ppc_ide_md.io_base = _IO_BASE;
#endif
	/*
	 * Print the banner, then scroll down so boot progress
	 * can be printed.  -- Cort 
	 */
	if ( ppc_md.progress ) ppc_md.progress("Linux/PPC "UTS_RELEASE"\n", 0x0);
}

void __chrp
chrp_progress(char *s, unsigned short hex)
{
	extern unsigned int rtas_data;
	int max_width, width;
	struct device_node *root;
	char *os = s;
	unsigned long *p;

	if ( (root = find_path_device("/rtas")) &&
	     (p = (unsigned long *)get_property(root,
						"ibm,display-line-length",
						NULL)) )
		max_width = *p;
	else
		max_width = 0x10;

	if ( (_machine != _MACH_chrp) || !rtas_data )
		return;
	if ( call_rtas( "display-character", 1, 1, NULL, '\r' ) )
	{
		/* assume no display-character RTAS method - use hex display */
		return;
	}

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

