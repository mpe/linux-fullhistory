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
#include <linux/major.h>
#include <linux/utsname.h>
#include <linux/blk.h>
#include <linux/init.h>
#include <linux/console.h>

#include <asm/elf.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/procinfo.h>
#include <asm/segment.h>
#include <asm/setup.h>
#include <asm/system.h>

#define MEM_SIZE	(16*1024*1024)
#define COMMAND_LINE_SIZE 256

#ifndef CONFIG_CMDLINE
#define CONFIG_CMDLINE ""
#endif

#ifndef PARAMS_BASE
#define PARAMS_BASE NULL
#endif

extern void reboot_setup(char *str, int *ints);
extern void fpe_init(void);
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
extern int _etext, _edata, _end;

unsigned char aux_device_present;

         char elf_platform[ELF_PLATFORM_SIZE];
unsigned int  elf_hwcap;

/*
 * From head-armv.S
 */
unsigned int processor_id;
unsigned int __machine_arch_type;
#ifdef MULTI_CPU
struct processor processor;
#endif
#ifdef CONFIG_ARCH_ACORN
int memc_ctrl_reg;
int number_mfm_drives;
unsigned int vram_size;
#endif

static struct proc_info_item proc_info;
static union { char c[4]; unsigned long l; } endian_test __initdata = { { 'l', '?', '?', 'b' } };
#define ENDIANNESS ((char)endian_test.l)

/*-------------------------------------------------------------------------
 * Early initialisation routines for various configurable items in the
 * kernel.  Each one either supplies a setup_ function, or defines this
 * symbol to be empty if not configured.
 */

/*
 * initial ram disk
 */
#ifdef CONFIG_BLK_DEV_INITRD
static void __init
check_initrd(unsigned long mem_start, unsigned long mem_end)
{
	if (initrd_end > mem_end) {
		printk ("initrd extends beyond end of memory "
			"(0x%08lx > 0x%08lx) - disabling initrd\n",
			initrd_end, mem_end);
		initrd_start = 0;
	}
}

#else
#define check_initrd(ms,me)
#endif

void __init
setup_processor(void)
{
	armidindex = 0;

	while ((armidlist[armidindex].id ^ processor_id) &
	       armidlist[armidindex].mask)
		armidindex += 1;

	if (armidlist[armidindex].id == 0)
		while (1);

	processor = *armidlist[armidindex].proc;
	processor._proc_init();
}

static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;
static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

static void __init
setup_mem(char *cmd_line, unsigned long *mem_start, unsigned long *mem_sz)
{
	char c = ' ', *to = command_line;
	int len = 0;

	*mem_start = (unsigned long)&_end;

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
setup_ram(int doload, int prompt, int image_start)
{
#ifdef CONFIG_BLK_DEV_RAM
	extern int rd_doload;
	extern int rd_prompt;
	extern int rd_image_start;

	rd_image_start = image_start;
	rd_prompt = prompt;
	rd_doload = doload;
#endif
}

/*
 * initial ram disk
 */
static void __init
setup_initrd(unsigned int start, unsigned int size)
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

#ifdef CONFIG_ARCH_ACORN
int memc_ctrl_reg;
int number_mfm_drives;
unsigned int vram_size;
#endif

#ifndef PARAMS_BASE
#define PARAMS_BASE NULL
#endif

static union { char c[4]; unsigned long l; } endian_test __initdata = { { 'l', '?', '?', 'b' } };
#define ENDIANNESS ((char)endian_test.l)

void __init
setup_arch(char **cmdline_p, unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	struct param_struct *params = (struct param_struct *)PARAMS_BASE;
	static unsigned char smptrap;
	unsigned long memory_end = 0;
	char *from = NULL;

	if (smptrap == 1)
		return;
	smptrap = 1;

#if defined(CONFIG_ARCH_ARC)
	__machine_arch_type = MACH_TYPE_ARCHIMEDES;
#elif defined(CONFIG_ARCH_A5K)
	__machine_arch_type = MACH_TYPE_A5K;
#endif

	setup_processor();

	init_mm.start_code = TASK_SIZE;
	init_mm.end_code   = TASK_SIZE + (unsigned long) &_etext;
	init_mm.end_data   = TASK_SIZE + (unsigned long) &_edata;
	init_mm.brk	   = TASK_SIZE + (unsigned long) &_end;

	/*
	 * Add your machine dependencies here
	 */
	switch (machine_arch_type) {
	case MACH_TYPE_EBSA110:
		/* EBSA110 locks if we execute 'wait for interrupt' */
		disable_hlt();
		if (params && params->u1.s.page_size != 4096)
			params = NULL;
		break;

	case MACH_TYPE_RISCPC:
		/* RiscPC can't handle half-word loads and stores */
		elf_hwcap &= ~HWCAP_HALF;
		break;

	case MACH_TYPE_EBSA285:
		if (params) {
			ORIG_X		 = params->u1.s.video_x;
			ORIG_Y		 = params->u1.s.video_y;
			ORIG_VIDEO_COLS  = params->u1.s.video_num_cols;
			ORIG_VIDEO_LINES = params->u1.s.video_num_rows;
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
		/* CATS must use soft-reboot */
		reboot_setup("s", NULL);
		break;

	case MACH_TYPE_NETWINDER:
		/*
		 * to be fixed in a future NeTTrom
		 */
		if (params->u1.s.page_size == 4096) {
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
				params->u1.s.video_x = 0;
				params->u1.s.video_y = 0;
				params->u1.s.video_num_cols = 80;
				params->u1.s.video_num_rows = 30;
			}
		} else {
			printk("Warning: no NeTTrom parameter page detected, using "
			       "compiled-in settings\n");
			params = NULL;
		}
		break;

	default:
		break;
	}

	if (params) {
		memory_end	  = params->u1.s.page_size *
				    params->u1.s.nr_pages;

		ROOT_DEV	  = to_kdev_t(params->u1.s.rootdev);

		setup_ram((params->u1.s.flags & FLAG_RDLOAD) == 0,
			  (params->u1.s.flags & FLAG_RDPROMPT) == 0,
			  params->u1.s.rd_start);

		setup_initrd(params->u1.s.initrd_start,
			     params->u1.s.initrd_size);

		if (!(params->u1.s.flags & FLAG_READONLY))
			root_mountflags &= ~MS_RDONLY;

#ifdef CONFIG_ARCH_ACORN
#ifdef CONFIG_ARCH_RPC
		{
			extern void init_dram_banks(struct param_struct *);
			init_dram_banks(params);
		}
#endif

		memc_ctrl_reg	  = params->u1.s.memc_control_reg;
		number_mfm_drives = (params->u1.s.adfsdrives >> 3) & 3;
		vram_size	  = 0;

		switch (params->u1.s.pages_in_vram) {
		case 512:
			vram_size += PAGE_SIZE * 256;
		case 256:
			vram_size += PAGE_SIZE * 256;
		default:
			break;
		}

		memory_end -= vram_size;
#endif

		from = params->commandline;
	} else {
		ROOT_DEV	  = to_kdev_t(0x00ff);

		setup_ram(1, 1, 0);
		setup_initrd(0, 0);
	}

	if (!memory_end)
		memory_end = MEM_SIZE;

	if (!from)
		from = default_command_line;

#ifdef CONFIG_NWFPE
	fpe_init();
#endif

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	setup_mem(from, memory_start_p, &memory_end);

	memory_end += PAGE_OFFSET;

	check_initrd(*memory_start_p, memory_end);

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

	*cmdline_p = command_line;
	*memory_end_p = memory_end;
}

static const char *machine_desc[] = {
	"EBSA110",
	"Acorn-RiscPC",
	"unknown",
	"Nexus-FTV/PCI",
	"EBSA285",
	"Rebel-NetWinder",
	"Chalice-CATS",
	"unknown-TBOX",
	"co-EBSA285",
	"CL-PS7110",
	"Acorn-Archimedes",
	"Acorn-A5000"
};

int get_cpuinfo(char * buffer)
{
	int len;

	len = sprintf(buffer,
		"Processor\t: %s %s rev %d (%s)\n"
		"BogoMips\t: %lu.%02lu\n"
		"Hardware\t: %s\n",
		proc_info.manufacturer,
		proc_info.cpu_name,
		(int)processor_id & 15,
		elf_platform,
		(loops_per_sec+2500) / 500000,
		((loops_per_sec+2500) / 5000) % 100,
		machine_desc[machine_arch_type]);
	return len;
}
