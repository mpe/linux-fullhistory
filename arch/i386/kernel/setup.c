/*
 *  linux/arch/i386/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Enhanced CPU type detection by Mike Jagdis, Patrick St. Jean
 *  and Martin Mares, November 1997.
 *
 *  Force Cyrix 6x86(MX) and M II processors to report MTRR capability
 *  and fix against Cyrix "coma bug" by
 *      Zoltan Boszormenyi <zboszor@mol.hu> February 1999.
 * 
 *  Force Centaur C6 processors to report MTRR capability.
 *      Bart Hartgers <bart@etpmod.phys.tue.nl>, May 1999.
 *
 *  Intel Mobile Pentium II detection fix. Sean Gilley, June 1999.
 *
 *  IDT Winchip tweaks, misc clean ups.
 *	Dave Jones <dave@powertweak.com>, August 1999
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *
 *  Better detection of Centaur/IDT WinChip models.
 *      Bart Hartgers <bart@etpmod.phys.tue.nl>, August 1999.
 *
 *  Memory region support
 *	David Parsons <orc@pell.chi.il.us>, July-August 1999
 *
 *  Cleaned up cache-detection code
 *	Dave Jones <dave@powertweak.com>, October 1999
 *
 *	Added proper L2 cache detection for Coppermine
 *	Dragan Stancevic <visitor@valinux.com>, October 1999
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

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
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/apm_bios.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <asm/processor.h>
#include <linux/console.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/cobalt.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <asm/e820.h>
#include <asm/dma.h>

/*
 * Machine setup..
 */

char ignore_irq13 = 0;		/* set if exception 16 works */
struct cpuinfo_x86 boot_cpu_data = { 0, 0, 0, 0, -1, 1, 0, 0, -1 };

unsigned long mmu_cr4_features __initdata = 0;

/*
 * Bus types ..
 */
int EISA_bus = 0;
int MCA_bus = 0;

/* for MCA, but anyone else can use it if they want */
unsigned int machine_id = 0;
unsigned int machine_submodel_id = 0;
unsigned int BIOS_revision = 0;
unsigned int mca_pentium_flag = 0;

/*
 * Setup options
 */
struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info;
struct apm_bios_info apm_bios_info;
struct sys_desc_table_struct {
	unsigned short length;
	unsigned char table[0];
};

struct e820map e820 = { 0 };

unsigned char aux_device_present;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern int root_mountflags;
extern int _text, _etext, _edata, _end;
extern unsigned long cpu_hz;

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	((unsigned char *)empty_zero_page)
#define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
#define EXT_MEM_K (*(unsigned short *) (PARAM+2))
#define ALT_MEM_K (*(unsigned long *) (PARAM+0x1e0))
#define E820_MAP_NR (*(char*) (PARAM+E820NR))
#define E820_MAP    ((unsigned long *) (PARAM+E820MAP))
#define APM_BIOS_INFO (*(struct apm_bios_info *) (PARAM+0x40))
#define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
#define SYS_DESC_TABLE (*(struct sys_desc_table_struct*)(PARAM+0xa0))
#define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))
#define RAMDISK_FLAGS (*(unsigned short *) (PARAM+0x1F8))
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
#define KERNEL_START (*(unsigned long *) (PARAM+0x214))
#define INITRD_START (*(unsigned long *) (PARAM+0x218))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x21c))
#define COMMAND_LINE ((char *) (PARAM+2048))
#define COMMAND_LINE_SIZE 256

#define RAMDISK_IMAGE_START_MASK  	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000	

#ifdef	CONFIG_VISWS
char visws_board_type = -1;
char visws_board_rev = -1;

#define	PIIX_PM_START		0x0F80

#define	SIO_GPIO_START		0x0FC0

#define	SIO_PM_START		0x0FC8

#define	PMBASE			PIIX_PM_START
#define	GPIREG0			(PMBASE+0x30)
#define	GPIREG(x)		(GPIREG0+((x)/8))
#define	PIIX_GPI_BD_ID1		18
#define	PIIX_GPI_BD_REG		GPIREG(PIIX_GPI_BD_ID1)

#define	PIIX_GPI_BD_SHIFT	(PIIX_GPI_BD_ID1 % 8)

#define	SIO_INDEX	0x2e
#define	SIO_DATA	0x2f

#define	SIO_DEV_SEL	0x7
#define	SIO_DEV_ENB	0x30
#define	SIO_DEV_MSB	0x60
#define	SIO_DEV_LSB	0x61

#define	SIO_GP_DEV	0x7

#define	SIO_GP_BASE	SIO_GPIO_START
#define	SIO_GP_MSB	(SIO_GP_BASE>>8)
#define	SIO_GP_LSB	(SIO_GP_BASE&0xff)

#define	SIO_GP_DATA1	(SIO_GP_BASE+0)

#define	SIO_PM_DEV	0x8

#define	SIO_PM_BASE	SIO_PM_START
#define	SIO_PM_MSB	(SIO_PM_BASE>>8)
#define	SIO_PM_LSB	(SIO_PM_BASE&0xff)
#define	SIO_PM_INDEX	(SIO_PM_BASE+0)
#define	SIO_PM_DATA	(SIO_PM_BASE+1)

#define	SIO_PM_FER2	0x1

#define	SIO_PM_GP_EN	0x80

static void
visws_get_board_type_and_rev(void)
{
	int raw;

	visws_board_type = (char)(inb_p(PIIX_GPI_BD_REG) & PIIX_GPI_BD_REG)
							 >> PIIX_GPI_BD_SHIFT;
/*
 * Get Board rev.
 * First, we have to initialize the 307 part to allow us access
 * to the GPIO registers.  Let's map them at 0x0fc0 which is right
 * after the PIIX4 PM section.
 */
	outb_p(SIO_DEV_SEL, SIO_INDEX);
	outb_p(SIO_GP_DEV, SIO_DATA);	/* Talk to GPIO regs. */
    
	outb_p(SIO_DEV_MSB, SIO_INDEX);
	outb_p(SIO_GP_MSB, SIO_DATA);	/* MSB of GPIO base address */

	outb_p(SIO_DEV_LSB, SIO_INDEX);
	outb_p(SIO_GP_LSB, SIO_DATA);	/* LSB of GPIO base address */

	outb_p(SIO_DEV_ENB, SIO_INDEX);
	outb_p(1, SIO_DATA);		/* Enable GPIO registers. */
    
/*
 * Now, we have to map the power management section to write
 * a bit which enables access to the GPIO registers.
 * What lunatic came up with this shit?
 */
	outb_p(SIO_DEV_SEL, SIO_INDEX);
	outb_p(SIO_PM_DEV, SIO_DATA);	/* Talk to GPIO regs. */

	outb_p(SIO_DEV_MSB, SIO_INDEX);
	outb_p(SIO_PM_MSB, SIO_DATA);	/* MSB of PM base address */
    
	outb_p(SIO_DEV_LSB, SIO_INDEX);
	outb_p(SIO_PM_LSB, SIO_DATA);	/* LSB of PM base address */

	outb_p(SIO_DEV_ENB, SIO_INDEX);
	outb_p(1, SIO_DATA);		/* Enable PM registers. */
    
/*
 * Now, write the PM register which enables the GPIO registers.
 */
	outb_p(SIO_PM_FER2, SIO_PM_INDEX);
	outb_p(SIO_PM_GP_EN, SIO_PM_DATA);
    
/*
 * Now, initialize the GPIO registers.
 * We want them all to be inputs which is the
 * power on default, so let's leave them alone.
 * So, let's just read the board rev!
 */
	raw = inb_p(SIO_GP_DATA1);
	raw &= 0x7f;	/* 7 bits of valid board revision ID. */

	if (visws_board_type == VISWS_320) {
		if (raw < 0x6) {
			visws_board_rev = 4;
		} else if (raw < 0xc) {
			visws_board_rev = 5;
		} else {
			visws_board_rev = 6;
	
		}
	} else if (visws_board_type == VISWS_540) {
			visws_board_rev = 2;
		} else {
			visws_board_rev = raw;
		}

		printk("Silicon Graphics %s (rev %d)\n",
			visws_board_type == VISWS_320 ? "320" :
			(visws_board_type == VISWS_540 ? "540" :
					"unknown"),
					visws_board_rev);
	}
#endif


static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

struct resource standard_io_resources[] = {
	{ "dma1", 0x00, 0x1f, IORESOURCE_BUSY },
	{ "pic1", 0x20, 0x3f, IORESOURCE_BUSY },
	{ "timer", 0x40, 0x5f, IORESOURCE_BUSY },
	{ "keyboard", 0x60, 0x6f, IORESOURCE_BUSY },
	{ "dma page reg", 0x80, 0x8f, IORESOURCE_BUSY },
	{ "pic2", 0xa0, 0xbf, IORESOURCE_BUSY },
	{ "dma2", 0xc0, 0xdf, IORESOURCE_BUSY },
	{ "fpu", 0xf0, 0xff, IORESOURCE_BUSY }
};

#define STANDARD_IO_RESOURCES (sizeof(standard_io_resources)/sizeof(struct resource))

static struct resource code_resource = { "Kernel code", 0x100000, 0 };
static struct resource data_resource = { "Kernel data", 0, 0 };
static struct resource vram_resource = { "Video RAM area", 0xa0000, 0xbffff, IORESOURCE_BUSY };

/* System ROM resources */
#define MAXROMS 6
static struct resource rom_resources[MAXROMS] = {
	{ "System ROM", 0xF0000, 0xFFFFF, IORESOURCE_BUSY },
	{ "Video ROM", 0xc0000, 0xc7fff, IORESOURCE_BUSY }
};

#define romsignature(x) (*(unsigned short *)(x) == 0xaa55)

static void __init probe_roms(void)
{
	int roms = 1;
	unsigned long base;
	unsigned char *romstart;

	request_resource(&iomem_resource, rom_resources+0);

	/* Video ROM is standard at C000:0000 - C7FF:0000, check signature */
	for (base = 0xC0000; base < 0xE0000; base += 2048) {
		romstart = bus_to_virt(base);
		if (!romsignature(romstart))
			continue;
		request_resource(&iomem_resource, rom_resources + roms);
		roms++;
		break;
	}

	/* Extension roms at C800:0000 - DFFF:0000 */
	for (base = 0xC8000; base < 0xE0000; base += 2048) {
		unsigned long length;

		romstart = bus_to_virt(base);
		if (!romsignature(romstart))
			continue;
		length = romstart[2] * 512;
		if (length) {
			unsigned int i;
			unsigned char chksum;

			chksum = 0;
			for (i = 0; i < length; i++)
				chksum += romstart[i];

			/* Good checksum? */
			if (!chksum) {
				rom_resources[roms].start = base;
				rom_resources[roms].end = base + length - 1;
				rom_resources[roms].name = "Extension ROM";
				rom_resources[roms].flags = IORESOURCE_BUSY;

				request_resource(&iomem_resource, rom_resources + roms);
				roms++;
				if (roms >= MAXROMS)
					return;
			}
		}
	}

	/* Final check for motherboard extension rom at E000:0000 */
	base = 0xE0000;
	romstart = bus_to_virt(base);

	if (romsignature(romstart)) {
		rom_resources[roms].start = base;
		rom_resources[roms].end = base + 65535;
		rom_resources[roms].name = "Extension ROM";
		rom_resources[roms].flags = IORESOURCE_BUSY;

		request_resource(&iomem_resource, rom_resources + roms);
	}
}

unsigned long __init memparse(char *ptr, char **retptr)
{
	unsigned long ret;

	ret = simple_strtoul(ptr, retptr, 0);

	if (**retptr == 'K' || **retptr == 'k') {
		ret <<= 10;
		(*retptr)++;
	}
	else if (**retptr == 'M' || **retptr == 'm') {
		ret <<= 20;
		(*retptr)++;
	}
	return ret;
} /* memparse */


void __init add_memory_region(unsigned long start,
                                  unsigned long size, int type)
{
	int x = e820.nr_map;

	if (x == E820MAX) {
	    printk("Ooops! Too many entries in the memory map!\n");
	    return;
	}

	e820.map[x].addr = start;
	e820.map[x].size = size;
	e820.map[x].type = type;
	e820.nr_map++;
} /* add_memory_region */


/*
 * Do NOT EVER look at the BIOS memory size location.
 * It does not work on many machines.
 */
#define LOWMEMSIZE()	(0x9f000)

void __init setup_memory_region(void)
{
#define E820_DEBUG	1
#ifdef E820_DEBUG
	int i;
#endif

	/*
	 * If we're lucky and live on a modern system, the setup code
	 * will have given us a memory map that we can use to properly
	 * set up memory.  If we aren't, we'll fake a memory map.
	 *
	 * We check to see that the memory map contains at least 2 elements
	 * before we'll use it, because the detection code in setup.S may
	 * not be perfect and most every PC known to man has two memory
	 * regions: one from 0 to 640k, and one from 1mb up.  (The IBM
	 * thinkpad 560x, for example, does not cooperate with the memory
	 * detection code.)
	 */
	if (E820_MAP_NR > 1) {
		/* got a memory map; copy it into a safe place.
		 */
		e820.nr_map = E820_MAP_NR;
		if (e820.nr_map > E820MAX)
			e820.nr_map = E820MAX;
		memcpy(e820.map, E820_MAP, e820.nr_map * sizeof e820.map[0]);
#ifdef E820_DEBUG
		for (i=0; i < e820.nr_map; i++) {
			printk("e820: %08x @ %08x ", (int)e820.map[i].size,
						(int)e820.map[i].addr);
			switch (e820.map[i].type) {
			case E820_RAM:	printk("(usable)\n");
					break;
			case E820_RESERVED:
					printk("(reserved)\n");
					break;
			case E820_ACPI:
					printk("(ACPI data)\n");
					break;
			case E820_NVS:
					printk("(ACPI NVS)\n");
					break;
			default:	printk("type %lu\n", e820.map[i].type);
					break;
			}
		}
#endif
	}
	else {
		/* otherwise fake a memory map; one section from 0k->640k,
		 * the next section from 1mb->appropriate_mem_k
		 */
		unsigned long mem_size;

		mem_size = (ALT_MEM_K < EXT_MEM_K) ? EXT_MEM_K : ALT_MEM_K;

		add_memory_region(0, LOWMEMSIZE(), E820_RAM);
		add_memory_region(HIGH_MEMORY, mem_size << 10, E820_RAM);
  	}
} /* setup_memory_region */


static inline void parse_mem_cmdline (char ** cmdline_p)
{
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;
	int usermem = 0;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	for (;;) {
		/*
		 * "mem=nopentium" disables the 4MB page tables.
		 * "mem=XXX[kKmM]" defines a memory region from HIGH_MEM
		 * to <mem>, overriding the bios size.
		 * "mem=XXX[KkmM]@XXX[KkmM]" defines a memory region from
		 * <start> to <start>+<mem>, overriding the bios size.
		 */
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			if (to != command_line)
				to--;
			if (!memcmp(from+4, "nopentium", 9)) {
				from += 9+4;
				boot_cpu_data.x86_capability &= ~X86_FEATURE_PSE;
			} else {
				/* If the user specifies memory size, we
				 * blow away any automatically generated
				 * size
				 */
				unsigned long start_at, mem_size;
 
				if (usermem == 0) {
					/* first time in: zap the whitelist
					 * and reinitialize it with the
					 * standard low-memory region.
					 */
					e820.nr_map = 0;
					usermem = 1;
					add_memory_region(0, LOWMEMSIZE(), E820_RAM);
				}
				mem_size = memparse(from+4, &from);
				if (*from == '@')
					start_at = memparse(from+1, &from);
				else {
					start_at = HIGH_MEMORY;
					mem_size -= HIGH_MEMORY;
				}
				add_memory_region(start_at, mem_size, E820_RAM);
			}
		}
		c = *(from++);
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
}

void __init setup_arch(char **cmdline_p)
{
	unsigned long bootmap_size;
	unsigned long start_pfn, max_pfn, max_low_pfn;
	int i;

#ifdef CONFIG_VISWS
	visws_get_board_type_and_rev();
#endif

 	ROOT_DEV = to_kdev_t(ORIG_ROOT_DEV);
 	drive_info = DRIVE_INFO;
 	screen_info = SCREEN_INFO;
	apm_bios_info = APM_BIOS_INFO;
	if( SYS_DESC_TABLE.length != 0 ) {
		MCA_bus = SYS_DESC_TABLE.table[3] &0x2;
		machine_id = SYS_DESC_TABLE.table[0];
		machine_submodel_id = SYS_DESC_TABLE.table[1];
		BIOS_revision = SYS_DESC_TABLE.table[2];
	}
	aux_device_present = AUX_DEVICE_INFO;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif
	setup_memory_region();

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	code_resource.start = virt_to_bus(&_text);
	code_resource.end = virt_to_bus(&_etext)-1;
	data_resource.start = virt_to_bus(&_etext);
	data_resource.end = virt_to_bus(&_edata)-1;

	parse_mem_cmdline(cmdline_p);

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((x) << PAGE_SHIFT)

/*
 * 128MB for vmalloc and initrd
 */
#define VMALLOC_RESERVE	(unsigned long)(128 << 20)
#define MAXMEM		(unsigned long)(-PAGE_OFFSET-VMALLOC_RESERVE)
#define MAXMEM_PFN	PFN_DOWN(MAXMEM)
#define MAX_NONPAE_PFN	(1 << 20)

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	start_pfn = PFN_UP(__pa(&_end));

	/*
	 * Find the highest page frame number we have available
	 */
	max_pfn = 0;
	for (i = 0; i < e820.nr_map; i++) {
		unsigned long start, end;
		/* RAM? */
		if (e820.map[i].type != E820_RAM)
			continue;
		start = PFN_UP(e820.map[i].addr);
		end = PFN_DOWN(e820.map[i].addr + e820.map[i].size);
		if (start >= end)
			continue;
		if (end > max_pfn)
			max_pfn = end;
	}

	/*
	 * Determine low and high memory ranges:
	 */
	max_low_pfn = max_pfn;
	if (max_low_pfn > MAXMEM_PFN) {
		max_low_pfn = MAXMEM_PFN;
#ifndef CONFIG_HIGHMEM
		/* Maximum memory usable is what is directly addressable */
		printk(KERN_WARNING "Warning only %ldMB will be used.\n",
					MAXMEM>>20);
		if (max_pfn > MAX_NONPAE_PFN)
			printk(KERN_WARNING "Use a PAE enabled kernel.\n");
		else
			printk(KERN_WARNING "Use a HIGHMEM enabled kernel.\n");
#else /* !CONFIG_HIGHMEM */
#ifndef CONFIG_X86_PAE
		if (max_pfn > MAX_NONPAE_PFN) {
			max_pfn = MAX_NONPAE_PFN;
			printk(KERN_WARNING "Warning only 4GB will be used.\n");
			printk(KERN_WARNING "Use a PAE enabled kernel.\n");
		}
#endif /* !CONFIG_X86_PAE */
#endif /* !CONFIG_HIGHMEM */
	}

#ifdef CONFIG_HIGHMEM
	highstart_pfn = highend_pfn = max_pfn;
	if (max_pfn > MAXMEM_PFN) {
		highstart_pfn = MAXMEM_PFN;
		printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
			pages_to_mb(highend_pfn - highstart_pfn));
	}
#endif
	/*
	 * Initialize the boot-time allocator (with low memory only):
	 */
	bootmap_size = init_bootmem(start_pfn, max_low_pfn);

	/*
	 * Register fully available low RAM pages with the bootmem allocator.
	 */
	for (i = 0; i < e820.nr_map; i++) {
		unsigned long curr_pfn, last_pfn, size;
 		/*
		 * Reserve usable low memory
		 */
		if (e820.map[i].type != E820_RAM)
			continue;
		/*
		 * We are rounding up the start address of usable memory:
		 */
		curr_pfn = PFN_UP(e820.map[i].addr);
		if (curr_pfn >= max_low_pfn)
			continue;
		/*
		 * ... and at the end of the usable range downwards:
		 */
		last_pfn = PFN_DOWN(e820.map[i].addr + e820.map[i].size);

		if (last_pfn > max_low_pfn)
			last_pfn = max_low_pfn;

		/*
		 * .. finally, did all the rounding and playing
		 * around just make the area go away?
		 */
		if (last_pfn <= curr_pfn)
			continue;

		size = last_pfn - curr_pfn;
		free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(size));
	}
	/*
	 * Reserve the bootmem bitmap itself as well. We do this in two
	 * steps (first step was init_bootmem()) because this catches
	 * the (very unlikely) case of us accidentally initializing the
	 * bootmem allocator with an invalid RAM area.
	 */
	reserve_bootmem(HIGH_MEMORY, (PFN_PHYS(start_pfn) +
			 bootmap_size + PAGE_SIZE-1) - (HIGH_MEMORY));

	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem(0, PAGE_SIZE);

#ifdef __SMP__
	/*
	 * But first pinch a few for the stack/trampoline stuff
	 * FIXME: Don't need the extra page at 4K, but need to fix
	 * trampoline before removing it. (see the GDT stuff)
	 */
	reserve_bootmem(PAGE_SIZE, PAGE_SIZE);
	smp_alloc_memory(); /* AP processor realmode stacks in low memory*/
#endif

#ifdef __SMP__
	/*
	 *	Save possible boot-time SMP configuration:
	 */
	init_smp_config();
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE < (max_low_pfn << PAGE_SHIFT)) {
			reserve_bootmem(INITRD_START, INITRD_SIZE);
			initrd_start =
				INITRD_START ? INITRD_START + PAGE_OFFSET : 0;
			initrd_end = initrd_start+INITRD_SIZE;
		}
		else {
			printk("initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    INITRD_START + INITRD_SIZE,
			    max_low_pfn << PAGE_SHIFT);
			initrd_start = 0;
		}
	}
#endif

	/*
	 * Request address space for all standard RAM and ROM resources
	 * and also for regions reported as reserved by the e820.
	 */
	probe_roms();
	for (i = 0; i < e820.nr_map; i++) {
		struct resource *res;
		if (e820.map[i].addr + e820.map[i].size > 0x100000000ULL)
			continue;
		res = alloc_bootmem_low(sizeof(struct resource));
		switch (e820.map[i].type) {
		case E820_RAM:	res->name = "System RAM"; break;
		case E820_ACPI:	res->name = "ACPI Tables"; break;
		case E820_NVS:	res->name = "ACPI Non-volatile Storage"; break;
		default:	res->name = "reserved";
		}
		res->start = e820.map[i].addr;
		res->end = res->start + e820.map[i].size - 1;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		request_resource(&iomem_resource, res);
		if (e820.map[i].type == E820_RAM) {
			/*
			 *  We dont't know which RAM region contains kernel data,
			 *  so we try it repeatedly and let the resource manager
			 *  test it.
			 */
			request_resource(res, &code_resource);
			request_resource(res, &data_resource);
		}
	}
	request_resource(&iomem_resource, &vram_resource);

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource, standard_io_resources+i);

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

static int __init get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, *v;

	/* Actually we must have cpuid or we could never have
	 * figured out that this was AMD from the vendor info :-).
	 */

	cpuid(0x80000000, &n, &dummy, &dummy, &dummy);
	if (n < 4)
		return 0;
	cpuid(0x80000001, &dummy, &dummy, &dummy, &(c->x86_capability));
	v = (unsigned int *) c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;
	/*  Set MTRR capability flag if appropriate  */
	if(boot_cpu_data.x86 !=5)
		return 1;
	if((boot_cpu_data.x86_model == 9) ||
	   ((boot_cpu_data.x86_model == 8) && 
	    (boot_cpu_data.x86_mask >= 8)))
		c->x86_capability |= X86_FEATURE_MTRR;

	return 1;
}

static int __init amd_model(struct cpuinfo_x86 *c)
{
	u32 l, h;
	unsigned long flags;
	int mbytes = max_mapnr >> (20-PAGE_SHIFT);
	
	int r=get_model_name(c);
	
	/*
	 *	Now do the cache operations. 
	 */
	 
	switch(c->x86)
	{
		case 5:
			if( c->x86_model < 6 )
			{
				/* Anyone with a K5 want to fill this in */				
				break;
			}
			
			/* K6 with old style WHCR */
			if( c->x86_model < 8 ||
				(c->x86_model== 8 && c->x86_mask < 8))
			{
				/* We can only write allocate on the low 508Mb */
				if(mbytes>508)
					mbytes=508;
					
				rdmsr(0xC0000082, l, h);
				if((l&0x0000FFFF)==0)
				{		
					l=(1<<0)|(mbytes/4);
					save_flags(flags);
					__cli();
					__asm__ __volatile__ ("wbinvd": : :"memory");
					wrmsr(0xC0000082, l, h);
					restore_flags(flags);
					printk(KERN_INFO "Enabling old style K6 write allocation for %d Mb\n",
						mbytes);
					
				}
				break;
			}
			if (c->x86_model == 8 || c->x86_model == 9)
			{
				/* The more serious chips .. */
				
				if(mbytes>4092)
					mbytes=4092;
				rdmsr(0xC0000082, l, h);
				if((l&0xFFFF0000)==0)
				{
					l=((mbytes>>2)<<22)|(1<<16);
					save_flags(flags);
					__cli();
					__asm__ __volatile__ ("wbinvd": : :"memory");
					wrmsr(0xC0000082, l, h);
					restore_flags(flags);
					printk(KERN_INFO "Enabling new style K6 write allocation for %d Mb\n",
						mbytes);
				}
				break;
			}
			break;
		case 6:	/* An Athlon. We can trust the BIOS probably */
		{
			
			u32 ecx, edx, dummy;
			cpuid(0x80000005, &dummy, &dummy, &ecx, &edx);
			printk("L1 I Cache: %dK  L1 D Cache: %dK\n",
				ecx>>24, edx>>24);
			cpuid(0x80000006, &dummy, &dummy, &ecx, &edx);
			printk("L2 Cache: %dK\n", ecx>>16);
			c->x86_cache_size = ecx>>16;
			break;
		}
		
	}
	return r;
}
			

/*
 * Read Cyrix DEVID registers (DIR) to get more detailed info. about the CPU
 */
static inline void do_cyrix_devid(unsigned char *dir0, unsigned char *dir1)
{
	unsigned char ccr2, ccr3;

	/* we test for DEVID by checking whether CCR3 is writable */
	cli();
	ccr3 = getCx86(CX86_CCR3);
	setCx86(CX86_CCR3, ccr3 ^ 0x80);
	getCx86(0xc0);   /* dummy to change bus */

	if (getCx86(CX86_CCR3) == ccr3) {       /* no DEVID regs. */
		ccr2 = getCx86(CX86_CCR2);
		setCx86(CX86_CCR2, ccr2 ^ 0x04);
		getCx86(0xc0);  /* dummy */

		if (getCx86(CX86_CCR2) == ccr2) /* old Cx486SLC/DLC */
			*dir0 = 0xfd;
		else {                          /* Cx486S A step */
			setCx86(CX86_CCR2, ccr2);
			*dir0 = 0xfe;
		}
	}
	else {
		setCx86(CX86_CCR3, ccr3);  /* restore CCR3 */

		/* read DIR0 and DIR1 CPU registers */
		*dir0 = getCx86(CX86_DIR0);
		*dir1 = getCx86(CX86_DIR1);
	}
	sti();
}

/*
 * Cx86_dir0_msb is a HACK needed by check_cx686_cpuid/slop in bugs.h in
 * order to identify the Cyrix CPU model after we're out of setup.c
 */
unsigned char Cx86_dir0_msb __initdata = 0;

static char Cx86_model[][9] __initdata = {
	"Cx486", "Cx486", "5x86 ", "6x86", "MediaGX ", "6x86MX ",
	"M II ", "Unknown"
};
static char Cx486_name[][5] __initdata = {
	"SLC", "DLC", "SLC2", "DLC2", "SRx", "DRx",
	"SRx2", "DRx2"
};
static char Cx486S_name[][4] __initdata = {
	"S", "S2", "Se", "S2e"
};
static char Cx486D_name[][4] __initdata = {
	"DX", "DX2", "?", "?", "?", "DX4"
};
static char Cx86_cb[] __initdata = "?.5x Core/Bus Clock";
static char cyrix_model_mult1[] __initdata = "12??43";
static char cyrix_model_mult2[] __initdata = "12233445";

static void __init cyrix_model(struct cpuinfo_x86 *c)
{
	unsigned char dir0, dir0_msn, dir0_lsn, dir1 = 0;
	char *buf = c->x86_model_id;
	const char *p = NULL;

	do_cyrix_devid(&dir0, &dir1);

	Cx86_dir0_msb = dir0_msn = dir0 >> 4; /* identifies CPU "family"   */
	dir0_lsn = dir0 & 0xf;                /* model or clock multiplier */

	/* common case step number/rev -- exceptions handled below */
	c->x86_model = (dir1 >> 4) + 1;
	c->x86_mask = dir1 & 0xf;

	/* Now cook; the original recipe is by Channing Corn, from Cyrix.
	 * We do the same thing for each generation: we work out
	 * the model, multiplier and stepping.  Black magic included,
	 * to make the silicon step/rev numbers match the printed ones.
	 */
	 
	switch (dir0_msn) {
		unsigned char tmp;

	case 0: /* Cx486SLC/DLC/SRx/DRx */
		p = Cx486_name[dir0_lsn & 7];
		break;

	case 1: /* Cx486S/DX/DX2/DX4 */
		p = (dir0_lsn & 8) ? Cx486D_name[dir0_lsn & 5]
			: Cx486S_name[dir0_lsn & 3];
		break;

	case 2: /* 5x86 */
		Cx86_cb[2] = cyrix_model_mult1[dir0_lsn & 5];
		p = Cx86_cb+2;
		break;

	case 3: /* 6x86/6x86L */
		Cx86_cb[1] = ' ';
		Cx86_cb[2] = cyrix_model_mult1[dir0_lsn & 5];
		if (dir1 > 0x21) { /* 686L */
			Cx86_cb[0] = 'L';
			p = Cx86_cb;
			(c->x86_model)++;
		} else             /* 686 */
			p = Cx86_cb+1;
		/* Emulate MTRRs using Cyrix's ARRs. */
		c->x86_capability |= X86_FEATURE_MTRR;
		/* 6x86's contain this bug */
		c->coma_bug = 1;
		break;

	case 4: /* MediaGX/GXm */
		/*
		 *	Life sometimes gets weiiiiiiiird if we use this
		 *	on the MediaGX. So we turn it off for now. 
		 */
		
#ifdef CONFIG_PCI
		/* It isnt really a PCI quirk directly, but the cure is the
		   same. The MediaGX has deep magic SMM stuff that handles the
		   SB emulation. It thows away the fifo on disable_dma() which
		   is wrong and ruins the audio. */
		
		printk(KERN_INFO "Working around Cyrix MediaGX virtual DMA bug.\n");
		isa_dma_bridge_buggy = 1;
#endif		
		/* GXm supports extended cpuid levels 'ala' AMD */
		if (c->cpuid_level == 2) {
			get_model_name(c);  /* get CPU marketing name */
			c->x86_capability&=~X86_FEATURE_TSC;
			return;
		}
		else {  /* MediaGX */
			Cx86_cb[2] = (dir0_lsn & 1) ? '3' : '4';
			p = Cx86_cb+2;
			c->x86_model = (dir1 & 0x20) ? 1 : 2;
			c->x86_capability&=~X86_FEATURE_TSC;
		}
		break;

        case 5: /* 6x86MX/M II */
		if (dir1 > 7) dir0_msn++;  /* M II */
		else c->coma_bug = 1;      /* 6x86MX, it has the bug. */
		tmp = (!(dir0_lsn & 7) || dir0_lsn & 1) ? 2 : 0;
		Cx86_cb[tmp] = cyrix_model_mult2[dir0_lsn & 7];
		p = Cx86_cb+tmp;
        	if (((dir1 & 0x0f) > 4) || ((dir1 & 0xf0) == 0x20))
			(c->x86_model)++;
		/* Emulate MTRRs using Cyrix's ARRs. */
		c->x86_capability |= X86_FEATURE_MTRR;
		break;

	case 0xf:  /* Cyrix 486 without DEVID registers */
		switch (dir0_lsn) {
		case 0xd:  /* either a 486SLC or DLC w/o DEVID */
			dir0_msn = 0;
			p = Cx486_name[(c->hard_math) ? 1 : 0];
			break;

		case 0xe:  /* a 486S A step */
			dir0_msn = 0;
			p = Cx486S_name[0];
			break;
		}
		break;

	default:  /* unknown (shouldn't happen, we know everyone ;-) */
		dir0_msn = 7;
		break;
	}
	strcpy(buf, Cx86_model[dir0_msn & 7]);
	if (p) strcat(buf, p);
	return;
}

static void __init centaur_model(struct cpuinfo_x86 *c)
{
	enum {
		ECX8=1<<1,
		EIERRINT=1<<2,
		DPM=1<<3,
		DMCE=1<<4,
		DSTPCLK=1<<5,
		ELINEAR=1<<6,
		DSMC=1<<7,
		DTLOCK=1<<8,
		EDCTLB=1<<8,
		EMMX=1<<9,
		DPDC=1<<11,
		EBRPRED=1<<12,
		DIC=1<<13,
		DDC=1<<14,
		DNA=1<<15,
		ERETSTK=1<<16,
		E2MMX=1<<19,
		EAMD3D=1<<20,
	};

	char *name;
	u32  fcr_set=0;
	u32  fcr_clr=0;
	u32  lo,hi,newlo;
	u32  aa,bb,cc,dd;

	switch(c->x86_model) {
	case 4:
		name="C6";
		fcr_set=ECX8|DSMC|EDCTLB|EMMX|ERETSTK;
		fcr_clr=DPDC;
		break;
	case 8:
		switch(c->x86_mask) {
		default:
			name="2";
			break;
		case 7 ... 9:
			name="2A";
			break;
		case 10 ... 15:
			name="2B";
			break;
		}
		fcr_set=ECX8|DSMC|DTLOCK|EMMX|EBRPRED|ERETSTK|E2MMX|EAMD3D;
		fcr_clr=DPDC;
		break;
	case 9:
		name="3";
		fcr_set=ECX8|DSMC|DTLOCK|EMMX|EBRPRED|ERETSTK|E2MMX|EAMD3D;
		fcr_clr=DPDC;
		break;
	case 10:
		name="4";
		/* no info on the WC4 yet */
		break;
	default:
		name="??";
	}

	/* get FCR  */
	rdmsr(0x107, lo, hi);

	newlo=(lo|fcr_set) & (~fcr_clr);

	if (newlo!=lo) {
		printk("Centaur FCR was 0x%X now 0x%X\n", lo, newlo );
		wrmsr(0x107, newlo, hi );
	} else {
		printk("Centaur FCR is 0x%X\n",lo);
	}

	/* Emulate MTRRs using Centaur's MCR. */
	c->x86_capability |= X86_FEATURE_MTRR;
	/* Report CX8 */
	c->x86_capability |= X86_FEATURE_CX8;
	/* Set 3DNow! on Winchip 2 and above. */
	if (c->x86_model >=8)
		c->x86_capability |= X86_FEATURE_AMD3D;
	/* See if we can find out some more. */
	cpuid(0x80000000,&aa,&bb,&cc,&dd);
	if (aa>=0x80000005) { /* Yes, we can. */
		cpuid(0x80000005,&aa,&bb,&cc,&dd);
		/* Add L1 data and code cache sizes. */
		c->x86_cache_size = (cc>>24)+(dd>>24);
	}
	sprintf( c->x86_model_id, "WinChip %s", name );
}

void __init get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;

	if (!strcmp(v, "GenuineIntel"))
		c->x86_vendor = X86_VENDOR_INTEL;
	else if (!strcmp(v, "AuthenticAMD"))
		c->x86_vendor = X86_VENDOR_AMD;
	else if (!strcmp(v, "CyrixInstead"))
		c->x86_vendor = X86_VENDOR_CYRIX;
	else if (!strcmp(v, "UMC UMC UMC "))
		c->x86_vendor = X86_VENDOR_UMC;
	else if (!strcmp(v, "CentaurHauls"))
		c->x86_vendor = X86_VENDOR_CENTAUR;
	else if (!strcmp(v, "NexGenDriven"))
		c->x86_vendor = X86_VENDOR_NEXGEN;
	else
		c->x86_vendor = X86_VENDOR_UNKNOWN;
}

struct cpu_model_info {
	int vendor;
	int x86;
	char *model_names[16];
};

static struct cpu_model_info cpu_models[] __initdata = {
	{ X86_VENDOR_INTEL,	4,
	  { "486 DX-25/33", "486 DX-50", "486 SX", "486 DX/2", "486 SL", 
	    "486 SX/2", NULL, "486 DX/2-WB", "486 DX/4", "486 DX/4-WB", NULL, 
	    NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_INTEL,	5,
	  { "Pentium 60/66 A-step", "Pentium 60/66", "Pentium 75 - 200",
	    "OverDrive PODP5V83", "Pentium MMX", NULL, NULL,
	    "Mobile Pentium 75 - 200", "Mobile Pentium MMX", NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_INTEL,	6,
	  { "Pentium Pro A-step", "Pentium Pro", NULL, "Pentium II (Klamath)", 
	    NULL, "Pentium II (Deschutes)", "Mobile Pentium II", "Pentium III (Katmai)",
	    "Pentium III (Coppermine)", NULL, NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_AMD,	4,
	  { NULL, NULL, NULL, "486 DX/2", NULL, NULL, NULL, "486 DX/2-WB",
	    "486 DX/4", "486 DX/4-WB", NULL, NULL, NULL, NULL, "Am5x86-WT",
	    "Am5x86-WB" }},
	{ X86_VENDOR_AMD,	5,
	  { "K5/SSA5", "K5",
	    "K5", "K5", NULL, NULL,
	    "K6", "K6", "K6-2",
	    "K6-3", NULL, NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_AMD,	6,
	  { "Athlon", "Athlon",
	    NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_UMC,	4,
	  { NULL, "U5D", "U5S", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_NEXGEN,	5,
	  { "Nx586", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL, NULL }},
};

void __init identify_cpu(struct cpuinfo_x86 *c)
{
	int i;
	char *p = NULL;

	c->loops_per_sec = loops_per_sec;
	c->x86_cache_size = -1;

	get_cpu_vendor(c);

	if (c->x86_vendor == X86_VENDOR_UNKNOWN &&
	    c->cpuid_level < 0)
		return;

	if (c->x86_vendor == X86_VENDOR_CYRIX) {
		cyrix_model(c);
		return;
	}

	if (c->x86_vendor == X86_VENDOR_AMD && amd_model(c))
		return;

	if (c->x86_vendor == X86_VENDOR_CENTAUR) {
		centaur_model(c);
		return;
	}
		
	if (c->cpuid_level > 0 && c->x86_vendor == X86_VENDOR_INTEL)
	{
		if(c->x86_capability&(1<<18))
		{
			/* Disable processor serial number on Intel Pentium III 
			   from code by Phil Karn */
			unsigned long lo,hi;
			rdmsr(0x119,lo,hi);
			lo |= 0x200000;
			wrmsr(0x119,lo,hi);
			printk(KERN_INFO "Pentium-III serial number disabled.\n");
		}
	}

	if (c->cpuid_level > 1) {
		/* supports eax=2  call */
		int edx, dummy;

		cpuid(2, &dummy, &dummy, &dummy, &edx);

		/* We need only the LSB */
		edx &= 0xff;

		switch (edx) {
			case 0x40:
				c->x86_cache_size = 0;
				break;

			case 0x41:
				c->x86_cache_size = 128;
				break;

			case 0x42:
			case 0x82: /*Detect 256-Kbyte cache on Coppermine*/
				c->x86_cache_size = 256;
				break;

			case 0x43:
				c->x86_cache_size = 512;
				break;

			case 0x44:
				c->x86_cache_size = 1024;
				break;

			case 0x45:
				c->x86_cache_size = 2048;
				break;

			default:
				c->x86_cache_size = 0;
				break;
		}
	}

	for (i = 0; i < sizeof(cpu_models)/sizeof(struct cpu_model_info); i++) {
		if (cpu_models[i].vendor == c->x86_vendor &&
		    cpu_models[i].x86 == c->x86) {
			if (c->x86_model <= 16)
				p = cpu_models[i].model_names[c->x86_model];

			/* Names for the Pentium II Celeron processors 
                           detectable only by also checking the cache size */
			if ((cpu_models[i].vendor == X86_VENDOR_INTEL)
			    && (cpu_models[i].x86 == 6))
			{
			 	if(c->x86_model == 5 &&  c->x86_cache_size == 0)
					p = "Celeron (Covington)";						
				else if(c->x86_model == 6 && c->x86_cache_size == 128)
                            		p = "Celeron (Mendocino)"; 
 			  	else if(c->x86_model == 5 && c->x86_cache_size == 256)
					p = "Celeron (Dixon)";
			}
		}
	}

	if (p) {
		strcpy(c->x86_model_id, p);
		return;
	}

	sprintf(c->x86_model_id, "%02x/%02x", c->x86_vendor, c->x86_model);
}

/*
 *	Perform early boot up checks for a valid TSC. See arch/i386/kernel/time.c
 */
 
void __init dodgy_tsc(void)
{
	get_cpu_vendor(&boot_cpu_data);
	
	if(boot_cpu_data.x86_vendor != X86_VENDOR_CYRIX)
	{
		return;
	}
	cyrix_model(&boot_cpu_data);
}
	
	

static char *cpu_vendor_names[] __initdata = {
	"Intel", "Cyrix", "AMD", "UMC", "NexGen", "Centaur" };


void __init print_cpu_info(struct cpuinfo_x86 *c)
{
	char *vendor = NULL;

	if (c->x86_vendor < sizeof(cpu_vendor_names)/sizeof(char *))
		vendor = cpu_vendor_names[c->x86_vendor];
	else if (c->cpuid_level >= 0)
		vendor = c->x86_vendor_id;

	if (vendor)
		printk("%s ", vendor);

	if (!c->x86_model_id[0])
		printk("%d86", c->x86);
	else
		printk("%s", c->x86_model_id);

	if (c->x86_mask || c->cpuid_level>=0) 
		printk(" stepping %02x\n", c->x86_mask);
}

/*
 *	Get CPU information for use by the procfs.
 */

int get_cpuinfo(char * buffer)
{
	char *p = buffer;
	int sep_bug;
	static char *x86_cap_flags[] = {
	        "fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
	        "cx8", "apic", "10", "sep", "mtrr", "pge", "mca", "cmov",
	        "pat", "17", "psn", "19", "20", "21", "22", "mmx",
	        "24", "kni", "26", "27", "28", "29", "30", "31"
	};
	struct cpuinfo_x86 *c = cpu_data;
	int i, n;

	for(n=0; n<NR_CPUS; n++, c++) {
#ifdef __SMP__
		if (!(cpu_online_map & (1<<n)))
			continue;
#endif
		p += sprintf(p,"processor\t: %d\n"
			       "vendor_id\t: %s\n"
			       "cpu family\t: %c\n"
			       "model\t\t: %d\n"
			       "model name\t: %s\n",
			       n,
			       c->x86_vendor_id[0] ? c->x86_vendor_id : "unknown",
			       c->x86 + '0',
			       c->x86_model,
			       c->x86_model_id[0] ? c->x86_model_id : "unknown");

		if (c->x86_mask || c->cpuid_level >= 0)
			p += sprintf(p, "stepping\t: %d\n", c->x86_mask);
		else
			p += sprintf(p, "stepping\t: unknown\n");

		if (c->x86_capability & X86_FEATURE_TSC) {
			p += sprintf(p, "cpu MHz\t\t: %lu.%06lu\n",
				cpu_hz / 1000000, (cpu_hz % 1000000));
		}

		/* Cache size */
		if (c->x86_cache_size >= 0)
			p += sprintf(p, "cache size\t: %d KB\n", c->x86_cache_size);
		
		/* Modify the capabilities according to chip type */
		switch (c->x86_vendor) {

		    case X86_VENDOR_CYRIX:
			x86_cap_flags[24] = "cxmmx";
			break;

		    case X86_VENDOR_AMD:
			if (c->x86 == 5 && c->x86_model == 6)
				x86_cap_flags[10] = "sep";
			if (c->x86 < 6)
				x86_cap_flags[16] = "fcmov";
			x86_cap_flags[22] = "mmxext";
			x86_cap_flags[30] = "3dnowext";
			x86_cap_flags[31] = "3dnow";
			break;

		    case X86_VENDOR_INTEL:
			x86_cap_flags[17] = "pse36";
			x86_cap_flags[18] = "psn";
			x86_cap_flags[24] = "osfxsr";
			break;

		    case X86_VENDOR_CENTAUR:
			if (c->x86_model >=8)	/* Only Winchip2 and above */
			    x86_cap_flags[31] = "3dnow";
			break;

		    default:
			/* Unknown CPU manufacturer. Transmeta ? :-) */
			break;
		}

		sep_bug = c->x86_vendor == X86_VENDOR_INTEL &&
			  c->x86 == 0x06 &&
			  c->cpuid_level >= 0 &&
			  (c->x86_capability & X86_FEATURE_SEP) &&
			  c->x86_model < 3 &&
			  c->x86_mask < 3;
	
		p += sprintf(p, "fdiv_bug\t: %s\n"
			        "hlt_bug\t\t: %s\n"
			        "sep_bug\t\t: %s\n"
			        "f00f_bug\t: %s\n"
			        "coma_bug\t: %s\n"
			        "fpu\t\t: %s\n"
			        "fpu_exception\t: %s\n"
			        "cpuid level\t: %d\n"
			        "wp\t\t: %s\n"
			        "flags\t\t:",
			     c->fdiv_bug ? "yes" : "no",
			     c->hlt_works_ok ? "no" : "yes",
			     sep_bug ? "yes" : "no",
			     c->f00f_bug ? "yes" : "no",
			     c->coma_bug ? "yes" : "no",
			     c->hard_math ? "yes" : "no",
			     (c->hard_math && ignore_irq13) ? "yes" : "no",
			     c->cpuid_level,
			     c->wp_works_ok ? "yes" : "no");

		for ( i = 0 ; i < 32 ; i++ )
			if ( c->x86_capability & (1 << i) )
				p += sprintf(p, " %s", x86_cap_flags[i]);
		p += sprintf(p, "\nbogomips\t: %lu.%02lu\n\n",
			     (c->loops_per_sec+2500)/500000,
			     ((c->loops_per_sec+2500)/5000) % 100);
	}
	return p - buffer;
}

int cpus_initialized = 0;
unsigned long cpu_initialized = 0;

/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 */
void cpu_init (void)
{
	int nr = smp_processor_id();
	struct tss_struct * t = &init_tss[nr];

	if (test_and_set_bit(nr,&cpu_initialized)) {
		printk("CPU#%d already initialized!\n", nr);
		for (;;) __sti();
	}
	cpus_initialized++;
	printk("Initializing CPU#%d\n", nr);

	if (boot_cpu_data.x86_capability & X86_FEATURE_PSE)
		clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	__asm__ __volatile__("lgdt %0": "=m" (gdt_descr));
	__asm__ __volatile__("lidt %0": "=m" (idt_descr));

	/*
	 * Delete NT
	 */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");

	/*
	 * set up and load the per-CPU TSS and LDT
	 */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	t->esp0 = current->thread.esp0;
	set_tss_desc(nr,t);
	gdt_table[__TSS(nr)].b &= 0xfffffdff;
	load_TR(nr);
	load_LDT(&init_mm);

	/*
	 * Clear all 6 debug registers:
	 */

#define CD(register) __asm__("movl %0,%%db" #register ::"r"(0) );

	CD(0); CD(1); CD(2); CD(3); /* no db4 and db5 */; CD(6); CD(7);

#undef CD

	/*
	 * Force FPU initialization:
	 */
	current->flags &= ~PF_USEDFPU;
	current->used_math = 0;
	stts();
}
