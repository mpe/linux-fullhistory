/*
 *  linux/arch/arm/kernel/setup.c
 *
 *  Copyright (C) 1995-1999 Russell King
 */

/*
 * This file obtains various parameters about the system that the kernel
 * is running on.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/blk.h>
#include <linux/console.h>
#include <linux/init.h>

#include <asm/elf.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/procinfo.h>
#include <asm/setup.h>
#include <asm/system.h>

#ifndef MEM_SIZE
#define MEM_SIZE	(16*1024*1024)
#endif

#ifndef CONFIG_CMDLINE
#define CONFIG_CMDLINE ""
#endif

#ifndef PARAMS_BASE
#define PARAMS_BASE NULL
#endif

extern void reboot_setup(char *str, int *ints);
extern void disable_hlt(void);

struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info = {
 orig_video_lines:	30,
 orig_video_cols:	80,
 orig_video_mode:	0,
 orig_video_ega_bx:	0,
 orig_video_isVGA:	1,
 orig_video_points:	8
};

extern int root_mountflags;
extern int _text, _etext, _edata, _end;

unsigned char aux_device_present;
         char elf_platform[ELF_PLATFORM_SIZE];
unsigned int  elf_hwcap;

/*
 * From head-armv.S
 */
unsigned int processor_id;
unsigned int __machine_arch_type;
unsigned int vram_size;
unsigned int system_rev;
unsigned int system_serial_low;
unsigned int system_serial_high;
#ifdef MULTI_CPU
struct processor processor;
#endif
#ifdef CONFIG_ARCH_ACORN
unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;
#endif

static struct proc_info_item proc_info;
static union { char c[4]; unsigned long l; } endian_test __initdata = { { 'l', '?', '?', 'b' } };
#define ENDIANNESS ((char)endian_test.l)

/*-------------------------------------------------------------------------
 * Early initialisation routines for various configurable items in the
 * kernel.  Each one either supplies a setup_ function, or defines this
 * symbol to be empty if not configured.
 */

static void __init setup_processor(void)
{
	extern struct proc_info_list __proc_info_begin, __proc_info_end;
	struct proc_info_list *list;

	/*
	 * locate processor in the list of supported processor
	 * types.  The linker builds this table for us from the
	 * entries in arch/arm/mm/proc-*.S
	 */
	for (list = &__proc_info_begin; list < &__proc_info_end ; list++)
		if ((processor_id & list->cpu_mask) == list->cpu_val)
			break;

	/*
	 * If processor type is unrecognised, then we
	 * can do nothing...
	 */
	if (list >= &__proc_info_end) {
		printk("CPU configuration botched (ID %08x), unable "
		       "to continue.\n", processor_id);
		while (1);
	}

	proc_info = *list->info;

#ifdef MULTI_CPU
	processor = *list->proc;
#endif

	sprintf(system_utsname.machine, "%s%c", list->arch_name, ENDIANNESS);
	sprintf(elf_platform, "%s%c", list->elf_name, ENDIANNESS);
	elf_hwcap = list->elf_hwcap;

	cpu_proc_init();
}

static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;
static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

static void __init
setup_mem(char *cmd_line, unsigned long *mem_sz)
{
	char c = ' ', *to = command_line;
	int len = 0;

	if (!*mem_sz)
		*mem_sz = MEM_SIZE;

	for (;;) {
		if (c == ' ') {
			if (cmd_line[0] == 'm' &&
			    cmd_line[1] == 'e' &&
			    cmd_line[2] == 'm' &&
			    cmd_line[3] == '=') {
				*mem_sz = simple_strtoul(cmd_line+4, &cmd_line, 0);
				switch(*cmd_line) {
				case 'M':
				case 'm':
					*mem_sz <<= 10;
				case 'K':
				case 'k':
					*mem_sz <<= 10;
					cmd_line++;
				}
			}
			/* if there are two spaces, remove one */
			if (*cmd_line == ' ') {
				cmd_line++;
				continue;
			}
		}
		c = *cmd_line++;
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*to++ = c;
	}

	*to = '\0';

	/* remove trailing spaces */
	while (*--to == ' ' && to != command_line)
		*to = '\0';
}

static void __init
setup_ramdisk(int doload, int prompt, int image_start, unsigned int rd_sz)
{
#ifdef CONFIG_BLK_DEV_RAM
	extern int rd_doload;
	extern int rd_prompt;
	extern int rd_image_start;
	extern int rd_size;

	rd_image_start = image_start;
	rd_prompt = prompt;
	rd_doload = doload;

	if (rd_sz)
		rd_size = rd_sz;
#endif
}

/*
 * initial ram disk
 */
static void __init setup_initrd(unsigned int start, unsigned int size)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (start) {
		initrd_start = start;
		initrd_end   = start + size;
	} else {
		initrd_start = 0;
		initrd_end   = 0;
	}
#endif
}

static void __init check_initrd(unsigned long mem_end)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_end > mem_end) {
		printk ("initrd extends beyond end of memory "
			"(0x%08lx > 0x%08lx) - disabling initrd\n",
			initrd_end, mem_end);
		initrd_start = 0;
	}
#endif
}

/*
 * Standard memory resources
 */
static struct resource system_ram  = { "System RAM",  0, 0, IORESOURCE_MEM | IORESOURCE_BUSY };
static struct resource video_ram   = { "Video RAM",   0, 0, IORESOURCE_MEM };
static struct resource kernel_code = { "Kernel code", 0, 0, IORESOURCE_MEM };
static struct resource kernel_data = { "Kernel data", 0, 0, IORESOURCE_MEM };
static struct resource lpt1 = { "reserved", 0x3bc, 0x3be, IORESOURCE_IO | IORESOURCE_BUSY };
static struct resource lpt2 = { "reserved", 0x378, 0x37f, IORESOURCE_IO | IORESOURCE_BUSY };
static struct resource lpt3 = { "reserved", 0x278, 0x27f, IORESOURCE_IO | IORESOURCE_BUSY };

static void __init request_standard_resources(unsigned long end)
{
	kernel_code.start  = __virt_to_bus((unsigned long) &_text);
	kernel_code.end    = __virt_to_bus((unsigned long) &_etext - 1);
	kernel_data.start  = __virt_to_bus((unsigned long) &_etext);
	kernel_data.end    = __virt_to_bus((unsigned long) &_edata - 1);
	system_ram.start   = __virt_to_bus(PAGE_OFFSET);
	system_ram.end     = __virt_to_bus(end - 1);

	request_resource(&iomem_resource, &system_ram);
	request_resource(&system_ram, &kernel_code);
	request_resource(&system_ram, &kernel_data);
	if (video_ram.start != video_ram.end)
		request_resource(&iomem_resource, &video_ram);

	/*
	 * Some machines don't have the possibility of ever
	 * possessing LPT1 (lp0) and LPT3 (lp2)
	 */
	if (machine_is_ebsa110() || machine_is_riscpc() ||
	    machine_is_netwinder())
		request_resource(&ioport_resource, &lpt1);
	if (machine_is_riscpc())
		request_resource(&ioport_resource, &lpt2);
	if (machine_is_ebsa110() || machine_is_netwinder())
		request_resource(&ioport_resource, &lpt3);
}

void __init setup_arch(char **cmdline_p, unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	struct param_struct *params = (struct param_struct *)PARAMS_BASE;
	unsigned long memory_end = 0;
	char *from = default_command_line;

#if defined(CONFIG_ARCH_ARC)
	__machine_arch_type = MACH_TYPE_ARCHIMEDES;
#elif defined(CONFIG_ARCH_A5K)
	__machine_arch_type = MACH_TYPE_A5K;
#endif

	setup_processor();

	/*
	 * Defaults
	 */
	ROOT_DEV = MKDEV(0, 255);
	setup_ramdisk(1, 1, 0, 0);

	/*
	 * Add your machine dependencies here
	 */
	switch (machine_arch_type) {
	case MACH_TYPE_EBSA110:
		/* EBSA110 locks if we execute 'wait for interrupt' */
		disable_hlt();
		if (params && params->u1.s.page_size != PAGE_SIZE)
			params = NULL;
		break;

#ifdef CONFIG_ARCH_ACORN
#ifdef CONFIG_ARCH_RPC
	case MACH_TYPE_RISCPC:
		/* RiscPC can't handle half-word loads and stores */
		elf_hwcap &= ~HWCAP_HALF;
		{
			extern void init_dram_banks(struct param_struct *);
			init_dram_banks(params);
		}

		switch (params->u1.s.pages_in_vram) {
		case 512:
			vram_size += PAGE_SIZE * 256;
		case 256:
			vram_size += PAGE_SIZE * 256;
		default:
			break;
		}
#endif
	case MACH_TYPE_ARCHIMEDES:
	case MACH_TYPE_A5K:
		memc_ctrl_reg	  = params->u1.s.memc_control_reg;
		number_mfm_drives = (params->u1.s.adfsdrives >> 3) & 3;
		break;
#endif

	case MACH_TYPE_EBSA285:
		if (params) {
			ORIG_X		 = params->u1.s.video_x;
			ORIG_Y		 = params->u1.s.video_y;
			ORIG_VIDEO_COLS  = params->u1.s.video_num_cols;
			ORIG_VIDEO_LINES = params->u1.s.video_num_rows;
			video_ram.start  = 0x0a0000;
			video_ram.end    = 0x0bffff;
		}
		break;

	case MACH_TYPE_CO285:
		{
#if 0
			extern unsigned long boot_memory_end;
			extern char boot_command_line[];

			from = boot_command_line;
			memory_end = boot_memory_end;
#endif
			params = NULL;
		}
		break;

	case MACH_TYPE_CATS:
		/* CATS uses soft-reboot by default, since hard reboots
		 * fail on early boards.
		 */
		reboot_setup("s", NULL);
		params = NULL;
		ORIG_VIDEO_LINES = 25;
		ORIG_VIDEO_POINTS = 16;
		ORIG_Y = 24;
		video_ram.start = 0x0a0000;
		video_ram.end   = 0x0bffff;
		break;

	case MACH_TYPE_NETWINDER:
		/*
		 * to be fixed in a future NeTTrom
		 */
		if (params->u1.s.page_size == PAGE_SIZE) {
			if (params->u1.s.nr_pages != 0x2000 &&
			    params->u1.s.nr_pages != 0x4000) {
				printk("Warning: bad NeTTrom parameters detected, using defaults\n");
				/*
				 * This stuff doesn't appear to be initialised
				 * properly by NeTTrom 2.0.6 and 2.0.7
				 */
				params->u1.s.nr_pages = 0x2000;	/* 32MB */
				params->u1.s.ramdisk_size = 0;
				params->u1.s.flags = FLAG_READONLY;
				params->u1.s.initrd_start = 0;
				params->u1.s.initrd_size = 0;
				params->u1.s.rd_start = 0;
			}
		} else {
			printk("Warning: no NeTTrom parameter page detected, using "
			       "compiled-in settings\n");
			params = NULL;
		}
		video_ram.start = 0x0a0000;
		video_ram.end   = 0x0bffff;
		break;

	default:
		break;
	}

	if (params && params->u1.s.page_size != PAGE_SIZE) {
		printk("Warning: wrong page size configuration, "
		       "trying to continue\n");
		params = NULL;
	}

	if (params) {
		memory_end	   = PAGE_SIZE * params->u1.s.nr_pages;
		ROOT_DEV	   = to_kdev_t(params->u1.s.rootdev);
		system_rev	   = params->u1.s.system_rev;
		system_serial_low  = params->u1.s.system_serial_low;
		system_serial_high = params->u1.s.system_serial_high;

		setup_ramdisk((params->u1.s.flags & FLAG_RDLOAD) == 0,
			      (params->u1.s.flags & FLAG_RDPROMPT) == 0,
			      params->u1.s.rd_start,
			      params->u1.s.ramdisk_size);

		setup_initrd(params->u1.s.initrd_start,
			     params->u1.s.initrd_size);

		if (!(params->u1.s.flags & FLAG_READONLY))
			root_mountflags &= ~MS_RDONLY;

		from = params->commandline;
	}

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	setup_mem(from, &memory_end);

	memory_end += PAGE_OFFSET;

	*cmdline_p         = command_line;
	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code   = (unsigned long) &_etext;
	init_mm.end_data   = (unsigned long) &_edata;
	init_mm.brk	   = (unsigned long) &_end;
	*memory_start_p    = (unsigned long) &_end;
	*memory_end_p      = memory_end;

	request_standard_resources(memory_end);
	check_initrd(memory_end);

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

static const char *machine_desc[] = {
	/* Machine name		Allocater			*/
	"EBSA110",		/* RMK				*/
	"Acorn-RiscPC",		/* RMK				*/
	"unknown",
	"Nexus-FTV/PCI",	/* Philip Blundell		*/
	"EBSA285",		/* RMK				*/
	"Rebel-NetWinder",	/* RMK				*/
	"Chalice-CATS",		/* Philip Blundell		*/
	"unknown-TBOX",		/* Philip Blundell		*/
	"co-EBSA285",		/* Mark van Doesburg		*/
	"CL-PS7110",		/* Werner Almesberger		*/
	"Acorn-Archimedes",	/* RMK/DAG			*/
	"Acorn-A5000",		/* RMK/PB			*/
	"Etoile",		/* Alex de Vries		*/
	"LaCie_NAS",		/* Benjamin Herrenschmidt	*/
	"CL-PS7500",		/* Philip Blundell		*/
	"Shark"			/* Alexander Schulz		*/
};

int get_cpuinfo(char * buffer)
{
	char *p = buffer;

	p += sprintf(p, "Processor\t: %s %s rev %d (%s)\n",
		     proc_info.manufacturer, proc_info.cpu_name,
		     (int)processor_id & 15, elf_platform);

	p += sprintf(p, "BogoMIPS\t: %lu.%02lu\n",
		     (loops_per_sec+2500) / 500000,
		     ((loops_per_sec+2500) / 5000) % 100);

	p += sprintf(p, "Hardware\t: %s\n",
		     machine_desc[machine_arch_type]);

	p += sprintf(p, "Revision\t: %04x\n",
		     system_rev);

	p += sprintf(p, "Serial\t\t: %08x%08x\n",
		     system_serial_high,
		     system_serial_low);

	return p - buffer;
}
