/* $Id: setup.c,v 1.20 2000/03/05 02:44:41 gniibe Exp $
 *
 *  linux/arch/sh/kernel/setup.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *
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
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <linux/bootmem.h>
#include <linux/console.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>
#ifdef CONFIG_SH_EARLY_PRINTK
#include <asm/sh_bios.h>
#endif

/*
 * Machine setup..
 */

struct sh_cpuinfo boot_cpu_data = { CPU_SH_NONE, 0, 0, 0, };

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern void fpu_init(void);
extern int root_mountflags;
extern int _text, _etext, _edata, _end;

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	((unsigned char *)empty_zero_page)

#define MOUNT_ROOT_RDONLY (*(unsigned long *) (PARAM+0x000))
#define RAMDISK_FLAGS (*(unsigned long *) (PARAM+0x004))
#define ORIG_ROOT_DEV (*(unsigned long *) (PARAM+0x008))
#define LOADER_TYPE (*(unsigned long *) (PARAM+0x00c))
#define INITRD_START (*(unsigned long *) (PARAM+0x010))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x014))
/* ... */
#define COMMAND_LINE ((char *) (PARAM+0x100))
#define COMMAND_LINE_SIZE 256

#define RAMDISK_IMAGE_START_MASK  	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000	

static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

struct resource standard_io_resources[] = {
	{ "dma1", 0x00, 0x1f },
	{ "pic1", 0x20, 0x3f },
	{ "timer", 0x40, 0x5f },
	{ "keyboard", 0x60, 0x6f },
	{ "dma page reg", 0x80, 0x8f },
	{ "pic2", 0xa0, 0xbf },
	{ "dma2", 0xc0, 0xdf },
	{ "fpu", 0xf0, 0xff }
};

#define STANDARD_IO_RESOURCES (sizeof(standard_io_resources)/sizeof(struct resource))

/* System RAM - interrupted by the 640kB-1M hole */
#define code_resource (ram_resources[3])
#define data_resource (ram_resources[4])
static struct resource ram_resources[] = {
	{ "System RAM", 0x000000, 0x09ffff, IORESOURCE_BUSY },
	{ "System RAM", 0x100000, 0x100000, IORESOURCE_BUSY },
	{ "Video RAM area", 0x0a0000, 0x0bffff },
	{ "Kernel code", 0x100000, 0 },
	{ "Kernel data", 0, 0 }
};

static unsigned long memory_start, memory_end;

#ifdef CONFIG_SH_EARLY_PRINTK
/*
 *	Print a string through the BIOS
 */
static void sh_console_write(struct console *co, const char *s,
				 unsigned count)
{
    	sh_bios_console_write(s, count);
}

/*
 *	Receive character from the serial port
 */
static int sh_console_wait_key(struct console *co)
{
	/* Not implemented yet */
	return 0;
}

static kdev_t sh_console_device(struct console *c)
{
    	/* TODO: this is totally bogus */
	/* return MKDEV(SCI_MAJOR, SCI_MINOR_START + c->index); */
	return 0;
}

/*
 *	Setup initial baud/bits/parity. We do two things here:
 *	- construct a cflag setting for the first rs_open()
 *	- initialize the serial port
 *	Return non-zero if we didn't find a serial port.
 */
static int __init sh_console_setup(struct console *co, char *options)
{
	int	cflag = CREAD | HUPCL | CLOCAL;

	/*
	 *	Now construct a cflag setting.
	 *  	TODO: this is a totally bogus cflag, as we have
	 *  	no idea what serial settings the BIOS is using, or
	 *  	even if its using the serial port at all.
	 */
    	cflag |= B115200 | CS8 | /*no parity*/0;

	co->cflag = cflag;

	return 0;
}

static struct console sh_console = {
	"bios",
	sh_console_write,
	NULL,
	sh_console_device,
	sh_console_wait_key,
	NULL,
	sh_console_setup,
	CON_PRINTBUFFER,
	-1,
	0,
	NULL
};

void sh_console_init(void)
{
	register_console(&sh_console);
}

void sh_console_unregister(void)
{
	unregister_console(&sh_console);
}

#endif


static inline void parse_mem_cmdline (char ** cmdline_p)
{
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	memory_start = (unsigned long)PAGE_OFFSET+__MEMORY_START;
	/* Default is 4Mbyte. */
	memory_end = (unsigned long)PAGE_OFFSET+0x00400000+__MEMORY_START;

	for (;;) {
		/*
		 * "mem=XXX[kKmM]" defines a size of memory.
		 */
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			if (to != command_line)
				to--;
			{
				unsigned long mem_size;

				mem_size = memparse(from+4, &from);
				memory_end = memory_start + mem_size;
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

#ifdef CONFIG_SH_EARLY_PRINTK
	sh_console_init();
#endif
	
	ROOT_DEV = to_kdev_t(ORIG_ROOT_DEV);

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long)&_text;
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
	 * Find the highest page frame number we have available
	 */
	max_pfn = PFN_DOWN(__pa(memory_end));

	/*
	 * Determine low and high memory ranges:
	 */
	max_low_pfn = max_pfn;

 	/*
	 * Partially used pages are not usable - thus
	 * we are rounding upwards:
 	 */
	start_pfn = PFN_UP(__pa(&_end));
	/*
	 * Find a proper area for the bootmem bitmap. After this
	 * bootstrap step all allocations (until the page allocator
	 * is intact) must be done via bootmem_alloc().
	 */
	bootmap_size = init_bootmem_node(0, start_pfn,
					 __MEMORY_START>>PAGE_SHIFT, 
					 max_low_pfn);

	/*
	 * Register fully available low RAM pages with the bootmem allocator.
	 */
	{
		unsigned long curr_pfn, last_pfn, pages;

		/*
		 * We are rounding up the start address of usable memory:
		 */
		curr_pfn = PFN_UP(__MEMORY_START);
		/*
		 * ... and at the end of the usable range downwards:
		 */
		last_pfn = PFN_DOWN(__pa(memory_end));

		if (last_pfn > max_low_pfn)
			last_pfn = max_low_pfn;

		pages = last_pfn - curr_pfn;
		free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(pages));
	}

	/*
	 * Reserve the kernel text and
	 * Reserve the bootmem bitmap. We do this in two steps (first step
	 * was init_bootmem()), because this catches the (definitely buggy)
	 * case of us accidentally initializing the bootmem allocator with
	 * an invalid RAM area.
	 */
	reserve_bootmem(__MEMORY_START+PAGE_SIZE, (PFN_PHYS(start_pfn) + 
			bootmap_size + PAGE_SIZE-1) - __MEMORY_START);

	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem(__MEMORY_START, PAGE_SIZE);

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (max_low_pfn << PAGE_SHIFT)) {
			reserve_bootmem(INITRD_START+__MEMORY_START, INITRD_SIZE);
			initrd_start =
				INITRD_START ? INITRD_START + PAGE_OFFSET + __MEMORY_START : 0;
			initrd_end = initrd_start + INITRD_SIZE;
		} else {
			printk("initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
				    INITRD_START + INITRD_SIZE,
				    max_low_pfn << PAGE_SHIFT);
			initrd_start = 0;
		}
	}
#endif

#if 0
	/*
	 * Request the standard RAM and ROM resources -
	 * they eat up PCI memory space
	 */
	request_resource(&iomem_resource, ram_resources+0);
	request_resource(&iomem_resource, ram_resources+1);
	request_resource(&iomem_resource, ram_resources+2);
	request_resource(ram_resources+1, &code_resource);
	request_resource(ram_resources+1, &data_resource);
	probe_roms();

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource, standard_io_resources+i);
#endif

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

#if defined(__SH4__)
	/* We already grab/initialized FPU in head.S.  Make it consisitent. */
	init_task.used_math = 1;
	init_task.flags |= PF_USEDFPU;
#endif
	paging_init();
}

/*
 *	Get CPU information for use by the procfs.
 */

int get_cpuinfo(char *buffer)
{
	char *p = buffer;

#if defined(__sh3__)
	p += sprintf(p,"cpu family\t: SH-3\n"
		       "cache size\t: 8K-byte\n");
#elif defined(__SH4__)
	p += sprintf(p,"cpu family\t: SH-4\n"
		       "cache size\t: 8K-byte/16K-byte\n");
#endif
	p += sprintf(p, "bogomips\t: %lu.%02lu\n\n",
		     (loops_per_sec+2500)/500000,
		     ((loops_per_sec+2500)/5000) % 100);

#define PRINT_CLOCK(name, value) \
	p += sprintf(p, name " clock: %d.%02dMHz\n", \
		     ((value) / 1000000), ((value) % 1000000)/10000)
	
	PRINT_CLOCK("CPU", boot_cpu_data.cpu_clock);
	PRINT_CLOCK("Bus", boot_cpu_data.bus_clock);
	PRINT_CLOCK("Peripheral module", boot_cpu_data.module_clock);

	return p - buffer;
}
