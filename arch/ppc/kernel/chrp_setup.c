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
#ifdef CONFIG_ABSTRACT_CONSOLE
#include <linux/console.h>
#endif

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>
#include <asm/prom.h>
#include <asm/gg2.h>

extern void hydra_init(void);
extern void w83c553f_init(void);

/* for the mac fs */
kdev_t boot_dev;

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern int probingmem;
extern unsigned long loops_per_sec;

unsigned long empty_zero_page[1024];
extern unsigned char aux_device_present;

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

#if 0
#ifdef CONFIG_BLK_DEV_IDE
int chrp_ide_ports_known;
ide_ioreg_t chrp_ide_regbase[MAX_HWIFS];
ide_ioreg_t chrp_idedma_regbase; /* one for both channels */
unsigned int chrp_ide_irq;

void chrp_ide_probe(void)
{
}

void chrp_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
	int i;

	*p = 0;
	if (base == 0)
		return;
	for (i = 0; i < 8; ++i)
		*p++ = base + i * 0x10;
	*p = base + 0x160;
	if (irq != NULL) {
		*irq = chrp_ide_irq;
	}
}
#endif /* CONFIG_BLK_DEV_IDE */
#endif

int
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

	/* VLSI VAS96011/12 `Golden Gate 2' */
	/* Memory banks */
	sdramen = (in_le32((unsigned *)(GG2_PCI_CONFIG_BASE+GG2_PCI_DRAM_CTRL))
		   >>31) & 1;
	for (i = 0; i < (sdramen ? 4 : 6); i++) {
	    t = in_le32((unsigned *)(GG2_PCI_CONFIG_BASE+GG2_PCI_DRAM_BANK0+
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
		       gg2_cachesizes[(t>>7) & 3], gg2_cachetypes[(t>>2) & 3],
		       gg2_cachemodes[t & 3]);
	return len;
}

    /*
     *  Fixes for the National Semiconductor PC78308VUL SuperI/O
     *
     *  Some versions of Open Firmware incorrectly initialize the IRQ settings
     *  for keyboard and mouse
     */

__initfunc(static inline void sio_write(u8 val, u8 index))
{
    outb(index, 0x15c);
    outb(val, 0x15d);
}

__initfunc(static inline u8 sio_read(u8 index))
{
    outb(index, 0x15c);
    return inb(0x15d);
}

__initfunc(static void sio_init(void))
{
    u8 irq, type;

    /* select logical device 0 (KBC/Keyboard) */
    sio_write(0, 0x07);
    irq = sio_read(0x70);
    type = sio_read(0x71);
    printk("sio: Keyboard irq %d, type %d: ", irq, type);
    if (irq == 1 && type == 3)
	printk("OK\n");
    else {
	printk("remapping to irq 1, type 3\n");
	sio_write(1, 0x70);
	sio_write(3, 0x71);
    }

    /* select logical device 1 (KBC/Mouse) */
    sio_write(1, 0x07);
    irq = sio_read(0x70);
    type = sio_read(0x71);
    printk("sio: Mouse irq %d, type %d: ", irq, type);
    if (irq == 12 && type == 3)
	printk("OK\n");
    else {
	printk("remapping to irq 12, type 3\n");
	sio_write(12, 0x70);
	sio_write(3, 0x71);
    }
}


__initfunc(void
chrp_setup_arch(unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	extern char cmd_line[];

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_sec = 50000000;
	
	aux_device_present = 0xaa;

	ROOT_DEV = to_kdev_t(0x0802); /* sda2 (sda1 is for the kernel) */
	
#ifdef CONFIG_BLK_DEV_RAM
	/* initrd_start and size are setup by boot/head.S and kernel/head.S */
	if ( initrd_start )
	{
		if (initrd_end > *memory_end_p)
		{
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_end,*memory_end_p);
			initrd_start = 0;
		}
	}
#endif

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
	w83c553f_init();	/* PCI-ISA bridge and IDE */

	/*
	 *  Fix the Super I/O configuration
	 */
	sio_init();

#ifdef CONFIG_FB
	/* Frame buffer device based console */
	conswitchp = &fb_con;
#endif
}
