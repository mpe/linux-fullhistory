/*
 *  linux/arch/arm/kernel/setup.c
 *
 *  Copyright (C) 1995-1999 Russell King
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

extern void reboot_setup(char *str);
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

struct machine_desc {
	const char	*name;		/* architecture name	*/
	unsigned int	param_offset;	/* parameter page	*/
	unsigned int	video_start;	/* start of video RAM	*/
	unsigned int	video_end;	/* end of video RAM	*/
	unsigned int	reserve_lp0 :1;	/* never has lp0	*/
	unsigned int	reserve_lp1 :1;	/* never has lp1	*/
	unsigned int	reserve_lp2 :1;	/* never has lp2	*/
	unsigned int	broken_hlt  :1;	/* hlt is broken	*/
	unsigned int	soft_reboot :1;	/* soft reboot		*/
	void		(*fixup)(struct machine_desc *,
				 struct param_struct *, char **);
};

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
static const char *machine_name;
static char command_line[COMMAND_LINE_SIZE] = { 0, };

static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;
static union { char c[4]; unsigned long l; } endian_test __initdata = { { 'l', '?', '?', 'b' } };
#define ENDIANNESS ((char)endian_test.l)

/*
 * Standard memory resources
 */
static struct resource mem_res[] = {
	{ "Video RAM",   0,     0,     IORESOURCE_MEM			},
	{ "Kernel code", 0,     0,     IORESOURCE_MEM			},
	{ "Kernel data", 0,     0,     IORESOURCE_MEM			}
};

#define video_ram   mem_res[0]
#define kernel_code mem_res[1]
#define kernel_data mem_res[2]

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

	printk("Processor: %s %s revision %d\n",
	       proc_info.manufacturer, proc_info.cpu_name,
	       (int)processor_id & 15);
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

			start = PHYS_OFFSET;
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
	extern int rd_doload, rd_prompt, rd_image_start, rd_size;

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

#define O_PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define V_PFN_DOWN(x)	O_PFN_DOWN(__pa(x))

#define O_PFN_UP(x)	(PAGE_ALIGN(x) >> PAGE_SHIFT)
#define V_PFN_UP(x)	O_PFN_UP(__pa(x))

#define PFN_SIZE(x)	((x) >> PAGE_SHIFT)
#define PFN_RANGE(s,e)	PFN_SIZE(PAGE_ALIGN((unsigned long)(e)) - \
				(((unsigned long)(s)) & PAGE_MASK))

/*
 * FIXME: These can be removed when Ingo's cleanup patch goes in
 */
#define free_bootmem(s,sz)	free_bootmem((s)<<PAGE_SHIFT, (sz)<<PAGE_SHIFT)
#define reserve_bootmem(s,sz)	reserve_bootmem((s)<<PAGE_SHIFT, (sz)<<PAGE_SHIFT)

static unsigned int __init find_bootmap_pfn(unsigned int bootmap_pages)
{
	unsigned int start_pfn, bank, bootmap_pfn;

	start_pfn   = V_PFN_UP(&_end);
	bootmap_pfn = 0;

	/*
	 * FIXME: We really want to avoid allocating the bootmap
	 * over the top of the initrd.
	 */
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
		if (__pa(initrd_end) > meminfo.end) {
			printk ("initrd extends beyond end of memory "
				"(0x%08lx > 0x%08lx) - disabling initrd\n",
				__pa(initrd_end), meminfo.end);
			initrd_start = 0;
			initrd_end   = 0;
		}
	}
#endif

	for (bank = 0; bank < meminfo.nr_banks; bank ++) {
		unsigned int start, end;

		if (meminfo.bank[bank].size == 0)
			continue;

		start = O_PFN_UP(meminfo.bank[bank].start);
		end   = O_PFN_DOWN(meminfo.bank[bank].size +
				   meminfo.bank[bank].start);

		if (end < start_pfn)
			continue;

		if (start < start_pfn)
			start = start_pfn;

		if (end <= start)
			continue;

		if (end - start >= bootmap_pages) {
			bootmap_pfn = start;
			break;
		}
	}

	if (bootmap_pfn == 0)
		BUG();

	return bootmap_pfn;
}

/*
 * Initialise the bootmem allocator.
 */
static void __init setup_bootmem(void)
{
	unsigned int end_pfn, start_pfn, bootmap_pages, bootmap_pfn;
	unsigned int i;

	/*
	 * Calculate the  physical address of the top of memory.
	 */
	meminfo.end = 0;
	for (i = 0; i < meminfo.nr_banks; i++) {
		unsigned long end;

		if (meminfo.bank[i].size != 0) {
			end = meminfo.bank[i].start + meminfo.bank[i].size;
			if (meminfo.end < end)
				meminfo.end = end;
		}
	}

	start_pfn     = O_PFN_UP(PHYS_OFFSET);
	end_pfn	      = O_PFN_DOWN(meminfo.end);
	bootmap_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
	bootmap_pfn   = find_bootmap_pfn(bootmap_pages);

	/*
	 * Initialise the boot-time allocator
	 */
	init_bootmem_start(bootmap_pfn, start_pfn, end_pfn);

	/*
	 * Register all available RAM with the bootmem allocator.
	 */
	for (i = 0; i < meminfo.nr_banks; i++)
		if (meminfo.bank[i].size)
			free_bootmem(O_PFN_UP(meminfo.bank[i].start),
				     PFN_SIZE(meminfo.bank[i].size));

	/*
	 * Register the reserved regions with bootmem
	 */
	reserve_bootmem(bootmap_pfn, bootmap_pages);
	reserve_bootmem(V_PFN_DOWN(&_stext), PFN_RANGE(&_stext, &_end));

#ifdef CONFIG_CPU_32
	/*
	 * Reserve the page tables.  These are already in use.
	 */
	reserve_bootmem(V_PFN_DOWN(swapper_pg_dir),
			PFN_SIZE(PTRS_PER_PGD * sizeof(void *)));
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		reserve_bootmem(V_PFN_DOWN(initrd_start),
				PFN_RANGE(initrd_start, initrd_end));
#endif
}

static void __init request_standard_resources(struct machine_desc *mdesc)
{
	struct resource *res;
	int i;

	kernel_code.start  = __virt_to_bus(init_mm.start_code);
	kernel_code.end    = __virt_to_bus(init_mm.end_code - 1);
	kernel_data.start  = __virt_to_bus(init_mm.end_code);
	kernel_data.end    = __virt_to_bus(init_mm.brk - 1);

	for (i = 0; i < meminfo.nr_banks; i++) {
		unsigned long virt_start, virt_end;

		if (meminfo.bank[i].size == 0)
			continue;

		virt_start = __phys_to_virt(meminfo.bank[i].start);
		virt_end   = virt_start + meminfo.bank[i].size - 1;

		res = alloc_bootmem_low(sizeof(*res));
		res->name  = "System RAM";
		res->start = __virt_to_bus(virt_start);
		res->end   = __virt_to_bus(virt_end);
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;

		request_resource(&iomem_resource, res);

		if (kernel_code.start >= res->start &&
		    kernel_code.end <= res->end)
			request_resource(res, &kernel_code);
		if (kernel_data.start >= res->start &&
		    kernel_data.end <= res->end)
			request_resource(res, &kernel_data);
	}

	if (mdesc->video_start) {
		video_ram.start = mdesc->video_start;
		video_ram.end   = mdesc->video_end;
		request_resource(&iomem_resource, &video_ram);
	}

	/*
	 * Some machines don't have the possibility of ever
	 * possessing lp0, lp1 or lp2
	 */
	if (mdesc->reserve_lp0)
		request_resource(&ioport_resource, &lp0);
	if (mdesc->reserve_lp1)
		request_resource(&ioport_resource, &lp1);
	if (mdesc->reserve_lp2)
		request_resource(&ioport_resource, &lp2);
}

/*
 * Architecture specific fixups.  This is where any
 * parameters in the params struct are fixed up, or
 * any additional architecture specific information
 * is pulled from the params struct.
 */
static void __init
fixup_acorn(struct machine_desc *desc, struct param_struct *params,
	    char **cmdline)
{
#ifdef CONFIG_ARCH_ACORN
	int i;

	if (machine_is_riscpc()) {
		/*
		 * RiscPC can't handle half-word loads and stores
		 */
		elf_hwcap &= ~HWCAP_HALF;

		switch (params->u1.s.pages_in_vram) {
		case 512:
			vram_size += PAGE_SIZE * 256;
		case 256:
			vram_size += PAGE_SIZE * 256;
		default:
			break;
		}

		if (vram_size) {
			desc->video_start = 0x02000000;
			desc->video_end   = 0x02000000 + vram_size;
		}

		for (i = 0; i < 4; i++) {
			meminfo.bank[i].start = PHYS_OFFSET + (i << 26);
			meminfo.bank[i].size  =
				params->u1.s.pages_in_bank[i] *
				params->u1.s.page_size;
		}
		meminfo.nr_banks = 4;
	}
	memc_ctrl_reg	  = params->u1.s.memc_control_reg;
	number_mfm_drives = (params->u1.s.adfsdrives >> 3) & 3;
#endif
}

static void __init
fixup_ebsa285(struct machine_desc *desc, struct param_struct *params,
	      char **cmdline)
{
	ORIG_X		 = params->u1.s.video_x;
	ORIG_Y		 = params->u1.s.video_y;
	ORIG_VIDEO_COLS  = params->u1.s.video_num_cols;
	ORIG_VIDEO_LINES = params->u1.s.video_num_rows;
}

/*
 * Older NeTTroms either do not provide a parameters
 * page, or they don't supply correct information in
 * the parameter page.
 */
static void __init
fixup_netwinder(struct machine_desc *desc, struct param_struct *params,
		char **cmdline)
{
	if (params->u1.s.nr_pages != 0x2000 &&
	    params->u1.s.nr_pages != 0x4000) {
		printk(KERN_WARNING "Warning: bad NeTTrom parameters "
		       "detected, using defaults\n");

		params->u1.s.nr_pages = 0x2000;	/* 32MB */
		params->u1.s.ramdisk_size = 0;
		params->u1.s.flags = FLAG_READONLY;
		params->u1.s.initrd_start = 0;
		params->u1.s.initrd_size = 0;
		params->u1.s.rd_start = 0;
	}
}

/*
 * CATS uses soft-reboot by default, since
 * hard reboots fail on early boards.
 */
static void __init
fixup_cats(struct machine_desc *desc, struct param_struct *params,
	   char **cmdline)
{
	ORIG_VIDEO_LINES  = 25;
	ORIG_VIDEO_POINTS = 16;
	ORIG_Y = 24;
}

static void __init
fixup_coebsa285(struct machine_desc *desc, struct param_struct *params,
		char **cmdline)
{
#if 0
	extern unsigned long boot_memory_end;
	extern char boot_command_line[];

	meminfo.nr_banks      = 1;
	meminfo.bank[0].start = PHYS_OFFSET;
	meminfo.bank[0].size  = boot_memory_end;

	*cmdline = boot_command_line;
#endif
}

static void __init
fixup_sa1100(struct machine_desc *desc, struct param_struct *params,
		char **cmdline)
{
#ifdef CONFIG_ARCH_SA1100
	int i;
	extern struct mem_desc {
		unsigned long phys_start;
		unsigned long length;
	} mem_desc[];
	extern unsigned int mem_desc_size;

	for( i = 0; i < mem_desc_size; i++ ) {
		if( i >= NR_BANKS ) {
			printk( __FUNCTION__ 
				": mem_desc too large for meminfo structure\n");
			break;
		}
		meminfo.bank[i].start = mem_desc[i].phys_start;
		meminfo.bank[i].size = mem_desc[i].length;
	}
	meminfo.nr_banks = i;

#if defined(CONFIG_SA1100_BRUTUS)
	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( __phys_to_virt(0xd8000000), 0x00400000 );
#elif defined(CONFIG_SA1100_EMPEG)
	ROOT_DEV = MKDEV( 3, 1 );  /* /dev/hda1 */
	setup_ramdisk( 1, 0, 0, 4096 );
	setup_initrd( 0xd0000000+((1024-320)*1024), (320*1024) );
#elif defined(CONFIG_SA1100_TIFON)
	ROOT_DEV = MKDEV(UNNAMED_MAJOR, 0);
	setup_ramdisk(1, 0, 0, 4096);
	setup_initrd( 0xd0000000 + 0x1100004, 0x140000 );
#elif defined(CONFIG_SA1100_VICTOR)
	ROOT_DEV = MKDEV( 60, 2 );

	/* Get command line parameters passed from the loader (if any) */
	if( *((char*)0xc0000000) )
		strcpy( default_command_line, ((char *)0xc0000000) );

	/* power off if any problem */
	strcat( default_command_line, " panic=1" );
#elif defined(CONFIG_SA1100_LART)
	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk(1, 0, 0, 8192);
	setup_initrd(0xc0400000, 0x00400000);
#endif
#endif
}

#define NO_PARAMS	0
#define NO_VIDEO	0, 0

/*
 * This is the list of all architectures supported by
 * this kernel.  This should be integrated with the list
 * in head-armv.S.
 */
static struct machine_desc machine_desc[] __initdata = {
	{ "EBSA110",		/* RMK			*/
		0x00000400,
		NO_VIDEO,
		1, 0, 1, 1, 1,
		NULL
	}, { "Acorn-RiscPC",	/* RMK			*/
		0x10000100,
		NO_VIDEO,
		1, 1, 0, 0, 0,
		fixup_acorn
	}, { "unknown",
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "FTV/PCI",		/* Philip Blundell	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "EBSA285",		/* RMK			*/
		0x00000100,
		0x000a0000, 0x000bffff,
		0, 0, 0, 0, 0,
		fixup_ebsa285
	}, { "Rebel-NetWinder",	/* RMK			*/
		0x00000100,
		0x000a0000, 0x000bffff,
		1, 0, 1, 0, 0,
		fixup_netwinder
	}, { "Chalice-CATS",	/* Philip Blundell	*/
		NO_PARAMS,
		0x000a0000, 0x000bffff,
		0, 0, 0, 0, 1,
		fixup_cats
	}, { "unknown-TBOX",	/* Philip Blundell	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "co-EBSA285",	/* Mark van Doesburg	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_coebsa285
	}, { "CL-PS7110",	/* Werner Almesberger	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "Acorn-Archimedes",/* RMK/DAG		*/
		0x0207c000,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_acorn
	}, { "Acorn-A5000",	/* RMK/PB		*/
		0x0207c000,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_acorn
	}, { "Etoile",		/* Alex de Vries	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "LaCie_NAS",	/* Benjamin Herrenschmidt */
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "CL-PS7500",	/* Philip Blundell	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "Shark",		/* Alexander Schulz	*/
		NO_PARAMS,
		/* do you really mean 0x200000? */
		0x06000000, 0x06000000+0x00200000,
		0, 0, 0, 0, 0,
		NULL
	}, { "SA1100-based",	/* Nicolas Pitre	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_sa1100
	}
};

void __init setup_arch(char **cmdline_p)
{
	struct param_struct *params = NULL;
	struct machine_desc *mdesc;
	char *from = default_command_line;

#if defined(CONFIG_ARCH_ARC)
	__machine_arch_type = MACH_TYPE_ARCHIMEDES;
#elif defined(CONFIG_ARCH_A5K)
	__machine_arch_type = MACH_TYPE_A5K;
#endif

	setup_processor();

	ROOT_DEV = MKDEV(0, 255);

	mdesc = machine_desc + machine_arch_type;
	machine_name = mdesc->name;

	if (mdesc->broken_hlt)
		disable_hlt();

	if (mdesc->soft_reboot)
		reboot_setup("s");

	if (mdesc->param_offset)
		params = phys_to_virt(mdesc->param_offset);

	if (mdesc->fixup)
		mdesc->fixup(mdesc, params, &from);

	if (params && params->u1.s.page_size != PAGE_SIZE) {
		printk(KERN_WARNING "Warning: bad configuration page, "
		       "trying to continue\n");
		params = NULL;
	}

	if (params) {
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
		meminfo.bank[0].start = PHYS_OFFSET;
		if (params)
			meminfo.bank[0].size = params->u1.s.nr_pages << PAGE_SHIFT;
		else
			meminfo.bank[0].size = MEM_SIZE;
	}

	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code   = (unsigned long) &_etext;
	init_mm.end_data   = (unsigned long) &_edata;
	init_mm.brk	   = (unsigned long) &_end;

	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';
	parse_cmdline(cmdline_p, from);
	setup_bootmem();
	request_standard_resources(mdesc);

	paging_init();

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

int get_cpuinfo(char * buffer)
{
	char *p = buffer;

	p += sprintf(p, "Processor\t: %s %s rev %d (%s)\n",
		     proc_info.manufacturer, proc_info.cpu_name,
		     (int)processor_id & 15, elf_platform);

	p += sprintf(p, "BogoMIPS\t: %lu.%02lu\n",
		     (loops_per_sec+2500) / 500000,
		     ((loops_per_sec+2500) / 5000) % 100);

	p += sprintf(p, "Hardware\t: %s\n", machine_name);

	p += sprintf(p, "Revision\t: %04x\n",
		     system_rev);

	p += sprintf(p, "Serial\t\t: %08x%08x\n",
		     system_serial_high,
		     system_serial_low);

	return p - buffer;
}
