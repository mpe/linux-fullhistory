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
#include <linux/bootmem.h>

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
extern int root_mountflags;
extern int _stext, _text, _etext, _edata, _end;

unsigned int processor_id;
unsigned int __machine_arch_type;
unsigned int vram_size;
unsigned int system_rev;
unsigned int system_serial_low;
unsigned int system_serial_high;
unsigned int elf_hwcap;

#ifdef CONFIG_ARCH_ACORN
unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;
#endif

struct meminfo meminfo;

#ifdef MULTI_CPU
struct processor processor;
#endif

struct drive_info_struct { char dummy[32]; } drive_info;

struct screen_info screen_info = {
 orig_video_lines:	30,
 orig_video_cols:	80,
 orig_video_mode:	0,
 orig_video_ega_bx:	0,
 orig_video_isVGA:	1,
 orig_video_points:	8
};

unsigned char aux_device_present;
char elf_platform[ELF_PLATFORM_SIZE];
char saved_command_line[COMMAND_LINE_SIZE];

static struct proc_info_item proc_info;
static char command_line[COMMAND_LINE_SIZE] = { 0, };

static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;
static union { char c[4]; unsigned long l; } endian_test __initdata = { { 'l', '?', '?', 'b' } };
#define ENDIANNESS ((char)endian_test.l)

/*
 * Standard memory resources
 */
static struct resource mem_res[] = {
	{ "System RAM",  0,     0,     IORESOURCE_MEM | IORESOURCE_BUSY },
	{ "Video RAM",   0,     0,     IORESOURCE_MEM			},
	{ "Kernel code", 0,     0,     IORESOURCE_MEM			},
	{ "Kernel data", 0,     0,     IORESOURCE_MEM			}
};

#define system_ram  mem_res[0]
#define video_ram   mem_res[1]
#define kernel_code mem_res[2]
#define kernel_data mem_res[3]

static struct resource io_res[] = {
	{ "reserved",    0x3bc, 0x3be, IORESOURCE_IO | IORESOURCE_BUSY },
	{ "reserved",    0x378, 0x37f, IORESOURCE_IO | IORESOURCE_BUSY },
	{ "reserved",    0x278, 0x27f, IORESOURCE_IO | IORESOURCE_BUSY }
};

#define lp0 io_res[0]
#define lp1 io_res[1]
#define lp2 io_res[2]

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

static unsigned long __init memparse(char *ptr, char **retptr)
{
	unsigned long ret = simple_strtoul(ptr, retptr, 0);

	switch (**retptr) {
	case 'M':
	case 'm':
		ret <<= 10;
	case 'K':
	case 'k':
		ret <<= 10;
		(*retptr)++;
	default:
		break;
	}
	return ret;
}

/*
 * Initial parsing of the command line.  We need to pick out the
 * memory size.  We look for mem=size@start, where start and size
 * are "size[KkMm]"
 */
static void __init
parse_cmdline(char **cmdline_p, char *from)
{
	char c = ' ', *to = command_line;
	int usermem = 0, len = 0;

	for (;;) {
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			unsigned long size, start;

			if (to != command_line)
				to -= 1;

			/* If the user specifies memory size, we
			 * blow away any automatically generated
			 * size.
			 */
			if (usermem == 0) {
				usermem = 1;
				meminfo.nr_banks = 0;
			}

			start = 0;
			size  = memparse(from + 4, &from);
			if (*from == '@')
				start = memparse(from + 1, &from);

			meminfo.bank[meminfo.nr_banks].start = start;
			meminfo.bank[meminfo.nr_banks].size  = size;
			meminfo.nr_banks += 1;
		}
		c = *from++;
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*to++ = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
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
	if (start == 0)
		size = 0;
	initrd_start = start;
	initrd_end   = start + size;
#endif
}

/*
 * Work out our memory regions.  Note that "pfn" is the physical page number
 * relative to the first physical page, not the physical address itself.
 */
static void __init setup_bootmem(void)
{
	unsigned int end_pfn, bootmem_end;
	int bank;

	/*
	 * Calculate the end of memory.
	 */
	for (bank = 0; bank < meminfo.nr_banks; bank++) {
		if (meminfo.bank[bank].size) {
			unsigned long end;

			end = meminfo.bank[bank].start +
			      meminfo.bank[bank].size;
			if (meminfo.end < end)
				meminfo.end = end;
		}
	}

	bootmem_end = __pa(PAGE_ALIGN((unsigned long)&_end));
	end_pfn     = meminfo.end >> PAGE_SHIFT;

	/*
	 * Initialise the boot-time allocator
	 */
	bootmem_end += init_bootmem(bootmem_end >> PAGE_SHIFT, end_pfn, PHYS_OFFSET);

	/*
	 * Register all available RAM with the bootmem allocator.
	 * The address is relative to the start of physical memory.
	 */
	for (bank = 0; bank < meminfo.nr_banks; bank ++)
		free_bootmem(meminfo.bank[bank].start, meminfo.bank[bank].size);

	/*
	 * reserve the following regions:
	 *  physical page 0 - it contains the exception vectors
	 *  kernel and the bootmem structure
	 *  swapper page directory (if any)
	 *  initrd (if any)
	 */
	reserve_bootmem(0, PAGE_SIZE);
#ifdef CONFIG_CPU_32
	reserve_bootmem(__pa(swapper_pg_dir), PTRS_PER_PGD * sizeof(void *));
#endif
	reserve_bootmem(__pa(&_stext), bootmem_end - __pa(&_stext));
#ifdef CONFIG_BLK_DEV_INITRD
	if (__pa(initrd_end) > (end_pfn << PAGE_SHIFT)) {
		printk ("initrd extends beyond end of memory "
			"(0x%08lx > 0x%08x) - disabling initrd\n",
			__pa(initrd_end), end_pfn << PAGE_SHIFT);
		initrd_start = 0;
	}

	if (initrd_start)
		reserve_bootmem(__pa(initrd_start),
				initrd_end - initrd_start);
#endif
}

static void __init request_standard_resources(void)
{
	kernel_code.start  = __virt_to_bus((unsigned long) &_text);
	kernel_code.end    = __virt_to_bus((unsigned long) &_etext - 1);
	kernel_data.start  = __virt_to_bus((unsigned long) &_etext);
	kernel_data.end    = __virt_to_bus((unsigned long) &_edata - 1);
	system_ram.start   = __virt_to_bus(PAGE_OFFSET);
	system_ram.end     = __virt_to_bus(meminfo.end + PAGE_OFFSET - 1);

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
		request_resource(&ioport_resource, &lp0);
	if (machine_is_riscpc())
		request_resource(&ioport_resource, &lp1);
	if (machine_is_ebsa110() || machine_is_netwinder())
		request_resource(&ioport_resource, &lp2);
}

void __init setup_arch(char **cmdline_p)
{
	struct param_struct *params = (struct param_struct *)PARAMS_BASE;
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

		switch (params->u1.s.pages_in_vram) {
		case 512:
			vram_size += PAGE_SIZE * 256;
		case 256:
			vram_size += PAGE_SIZE * 256;
		default:
			break;
		}
		{
			int i;

			for (i = 0; i < 4; i++) {
				meminfo.bank[i].start = i << 26;
				meminfo.bank[i].size  =
					params->u1.s.pages_in_bank[i] *
					params->u1.s.page_size;
			}
			meminfo.nr_banks = 4;
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
		ORIG_VIDEO_LINES  = 25;
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
		if (meminfo.nr_banks == 0) {
			meminfo.nr_banks      = 1;
			meminfo.bank[0].start = 0;
			meminfo.bank[0].size  = params->u1.s.nr_pages << PAGE_SHIFT;
		}
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

	if (meminfo.nr_banks == 0) {
		meminfo.nr_banks      = 1;
		meminfo.bank[0].start = 0;
		meminfo.bank[0].size  = MEM_SIZE;
	}

	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code   = (unsigned long) &_etext;
	init_mm.end_data   = (unsigned long) &_edata;
	init_mm.brk	   = (unsigned long) &_end;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';
	parse_cmdline(cmdline_p, from);
	setup_bootmem();
	request_standard_resources();

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
