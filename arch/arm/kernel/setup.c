/*
 *  linux/arch/arm/kernel/setup.c
 *
 *  Copyright (C) 1995-1998 Russell King
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

/* Work out which CPUs to support */
#ifdef CONFIG_ARCH_ACORN
#define SUPPORT_CPU_ARM6
#define SUPPORT_CPU_ARM7
#define SUPPORT_CPU_SA110
#else
#define SUPPORT_CPU_SA110
#endif
#ifdef CONFIG_CPU_ARM6
#define SUPPORT_CPU_ARM6
#endif
#ifdef CONFIG_CPU_ARM7
#define SUPPORT_CPU_ARM7
#endif
#ifdef CONFIG_CPU_SA110
#define SUPPORT_CPU_SA110
#endif

#ifndef CONFIG_CMDLINE
#define CONFIG_CMDLINE	"root=/dev/nfs rw"
#endif
#define MEM_SIZE	(16*1024*1024)
#define COMMAND_LINE_SIZE 256

struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info = {
 orig_video_lines:	30,
 orig_video_cols:	80,
 orig_video_mode:	0,
 orig_video_ega_bx:	0,
 orig_video_isVGA:	1,
 orig_video_points:	8
};
struct processor processor;
unsigned char aux_device_present;

extern const struct processor arm2_processor_functions;
extern const struct processor arm250_processor_functions;
extern const struct processor arm3_processor_functions;
extern const struct processor arm6_processor_functions;
extern const struct processor arm7_processor_functions;
extern const struct processor sa110_processor_functions;

char elf_platform[ELF_PLATFORM_SIZE];

const struct armversions armidlist[] = {
  /*-- Match -- --- Mask -- -- Manu --  Processor  uname -m   --- ELF STUFF ---
	--- processor asm funcs --- */
#if defined(CONFIG_CPU_26)
  { 0x41560200, 0xfffffff0, "ARM/VLSI",	"arm2"	 , "armv1"  , "v1", 0,
	&arm2_processor_functions   },
  { 0x41560250, 0xfffffff0, "ARM/VLSI",	"arm250" , "armv2"  , "v2", HWCAP_SWP,
	&arm250_processor_functions },
  { 0x41560300, 0xfffffff0, "ARM/VLSI",	"arm3"	 , "armv2"  , "v2", HWCAP_SWP,
	&arm3_processor_functions   },
#elif defined(CONFIG_CPU_32)
#ifdef SUPPORT_CPU_ARM6
  { 0x41560600, 0xfffffff0, "ARM/VLSI",	"arm6"	 , "armv3"  , "v3", HWCAP_SWP,
	&arm6_processor_functions   },
  { 0x41560610, 0xfffffff0, "ARM/VLSI",	"arm610" , "armv3"  , "v3", HWCAP_SWP,
	&arm6_processor_functions   },
#endif
#ifdef SUPPORT_CPU_ARM7
  { 0x41007000, 0xffffff00, "ARM/VLSI",	"arm7"	 , "armv3"  , "v3", HWCAP_SWP,
	&arm7_processor_functions   },
  /* ARM710 IDs are non-standard */
  { 0x41007100, 0xfff8ff00, "ARM/VLSI",	"arm710" , "armv3"  , "v3", HWCAP_SWP,
	&arm7_processor_functions   },
#endif
#ifdef SUPPORT_CPU_SA110
  { 0x4401a100, 0xfffffff0, "DEC",	"sa110"	 , "armv4"  , "v3", HWCAP_SWP|HWCAP_HALF,
	&sa110_processor_functions  },
#endif
#endif
  { 0x00000000, 0x00000000, "***", "unknown", "unknown", "**", 0, NULL }
};

/*
 * From head-armv.S
 */
unsigned int processor_id;
unsigned int machine_type;
int armidindex;

extern int root_mountflags;
extern int _etext, _edata, _end;

/*-------------------------------------------------------------------------
 * Early initialisation routines for various configurable items in the
 * kernel.  Each one either supplies a setup_ function, or defines this
 * symbol to be empty if not configured.
 */

/*
 * Risc-PC specific initialisation
 */
#ifdef CONFIG_ARCH_RPC

#include <asm/arch/mmu.h>

unsigned int vram_half_sam;

static void
setup_rpc(struct param_struct *params)
{
	extern void init_dram_banks(const struct param_struct *params);

	init_dram_banks(params);

	switch (params->u1.s.pages_in_vram) {
	case 256:
		vram_half_sam = 1024;
		break;
	case 512:
	default:
		vram_half_sam = 2048;
	}
}
#else
#define setup_rpc(x)
#endif

#ifdef PARAMS_BASE

#ifdef CONFIG_ARCH_ACORN
int memc_ctrl_reg;
int number_ide_drives;
int number_mfm_drives;
#endif

static struct param_struct *params = (struct param_struct *)PARAMS_BASE;

__initfunc(static char *
setup_params(unsigned long *mem_end_p))
{
	ROOT_DEV	  = to_kdev_t(params->u1.s.rootdev);
	ORIG_X		  = params->u1.s.video_x;
	ORIG_Y		  = params->u1.s.video_y;
	ORIG_VIDEO_COLS	  = params->u1.s.video_num_cols;
	ORIG_VIDEO_LINES  = params->u1.s.video_num_rows;

#ifdef CONFIG_ARCH_ACORN
#ifndef CONFIG_FB
	{
		extern int bytes_per_char_h;
		extern int bytes_per_char_v;

		bytes_per_char_h  = params->u1.s.bytes_per_char_h;
		bytes_per_char_v  = params->u1.s.bytes_per_char_v;
	}
#endif
	memc_ctrl_reg	  = params->u1.s.memc_control_reg;
	number_ide_drives = (params->u1.s.adfsdrives >> 6) & 3;
	number_mfm_drives = (params->u1.s.adfsdrives >> 3) & 3;

	setup_rpc(params);

	if (!(params->u1.s.flags & FLAG_READONLY))
		root_mountflags &= ~MS_RDONLY;
#endif
#ifdef CONFIG_BLK_DEV_RAM
	{
		extern int rd_doload;
		extern int rd_prompt;
		extern int rd_image_start;

		rd_image_start = params->u1.s.rd_start;
		rd_prompt = (params->u1.s.flags & FLAG_RDPROMPT) == 0;
		rd_doload = (params->u1.s.flags & FLAG_RDLOAD) == 0;
	}
#endif

#ifdef CONFIG_ARCH_ACORN
	*mem_end_p = GET_MEMORY_END(params);
#elif defined(CONFIG_ARCH_EBSA285)
	*mem_end_p = PAGE_OFFSET + params->u1.s.page_size * params->u1.s.nr_pages;
#else
	*mem_end_p = PAGE_OFFSET + MEM_SIZE;
#endif

	return params->commandline;
}

#else

static char default_command_line[] __initdata = CONFIG_CMDLINE;

__initfunc(static char *
setup_params(unsigned long *mem_end_p))
{
	ROOT_DEV	  = 0x00ff;

#ifdef CONFIG_BLK_DEV_RAM
	{
		extern int rd_doload;
		extern int rd_prompt;
		extern int rd_image_start;

		rd_image_start = 0;
		rd_prompt = 1;
		rd_doload = 1;
	}
#endif

	*mem_end_p = PAGE_OFFSET + MEM_SIZE;

	return default_command_line;
}
#endif

/*
 * initial ram disk
 */
#ifdef CONFIG_BLK_DEV_INITRD
__initfunc(static void
setup_initrd(const struct param_struct *params))
{
	if (params->u1.s.initrd_start) {
		initrd_start = params->u1.s.initrd_start;
		initrd_end   = initrd_start + params->u1.s.initrd_size;
	} else {
		initrd_start = 0;
		initrd_end   = 0;
	}
}

__initfunc(static void
check_initrd(unsigned long mem_start, unsigned long mem_end))
{
	if (initrd_end > mem_end) {
		printk ("initrd extends beyond end of memory "
			"(0x%08lx > 0x%08lx) - disabling initrd\n",
			initrd_end, mem_end);
		initrd_start = 0;
	}
}

#else
#define setup_initrd(p)
#define check_initrd(ms,me)
#endif

__initfunc(void
setup_processor(void))
{
	armidindex = 0;

	while ((armidlist[armidindex].id ^ processor_id) &
	       armidlist[armidindex].mask)
		armidindex += 1;

	if (armidlist[armidindex].id == 0) {
#ifdef CONFIG_ARCH_ACORN
		int i;

		for (i = 0; i < 3200; i++)
			((unsigned long *)SCREEN2_BASE)[i] = 0x77113322;
#endif
		while (1);
	}

	processor = *armidlist[armidindex].proc;
	processor._proc_init();
}

static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

__initfunc(static void
setup_mem(char *cmd_line, unsigned long *mem_start, unsigned long *mem_end))
{
	char c, *to = command_line;
	int len = 0;

	*mem_start = (unsigned long)&_end;

	for (;;) {
		if (cmd_line[0] == ' ' &&
		    cmd_line[1] == 'm' &&
		    cmd_line[2] == 'e' &&
		    cmd_line[3] == 'm' &&
		    cmd_line[4] == '=') {
			*mem_end = simple_strtoul(cmd_line+5, &cmd_line, 0);
			switch(*cmd_line) {
			case 'M':
			case 'm':
				*mem_end <<= 10;
			case 'K':
			case 'k':
				*mem_end <<= 10;
				cmd_line++;
			}
			*mem_end = *mem_end + PAGE_OFFSET;
		}
		c = *cmd_line++;
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*to++ = c;
	}

	*to = '\0';
}

__initfunc(void
setup_arch(char **cmdline_p, unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	static unsigned char smptrap;
	unsigned long memory_end;
	char endian = 'l';
	char *from;

	if (smptrap == 1)
		return;
	smptrap = 1;

	setup_processor();

	from = setup_params(&memory_end);
	setup_initrd(params);

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	setup_mem(from, memory_start_p, &memory_end);
	check_initrd(*memory_start_p, memory_end);

	init_task.mm->start_code = TASK_SIZE;
	init_task.mm->end_code	 = TASK_SIZE + (unsigned long) &_etext;
	init_task.mm->end_data	 = TASK_SIZE + (unsigned long) &_edata;
	init_task.mm->brk	 = TASK_SIZE + (unsigned long) &_end;

	*cmdline_p = command_line;
	*memory_end_p = memory_end;

	sprintf(system_utsname.machine, "%s%c", armidlist[armidindex].arch_vsn, endian);
	sprintf(elf_platform, "%s%c", armidlist[armidindex].elf_vsn, endian);

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

static const struct {
	char *machine_name;
	char *bus_name;
} machine_desc[] = {
	{ "DEC-EBSA110",	"DEC"		},
	{ "Acorn-RiscPC",	"Acorn"		},
	{ "Nexus-NexusPCI",	"PCI"		},
	{ "DEC-EBSA285",	"PCI"		},
	{ "Corel-Netwinder",	"PCI/ISA"	},
	{ "Chalice-CATS",	"PCI"		},
	{ "unknown-TBOX",	"PCI"		}
};

#if defined(CONFIG_ARCH_ARC)
#define HARDWARE "Acorn-Archimedes"
#define IO_BUS	 "Acorn"
#elif defined(CONFIG_ARCH_A5K)
#define HARDWARE "Acorn-A5000"
#define IO_BUS	 "Acorn"
#endif

#if defined(CONFIG_CPU_ARM2)
#define OPTIMISATION "ARM2"
#elif defined(CONFIG_CPU_ARM3)
#define OPTIMISATION "ARM3"
#elif defined(CONFIG_CPU_ARM6)
#define OPTIMISATION "ARM6"
#elif defined(CONFIG_CPU_ARM7)
#define OPTIMISATION "ARM7"
#elif defined(CONFIG_CPU_SA110)
#define OPTIMISATION "StrongARM"
#else
#define OPTIMISATION "unknown"
#endif

int get_cpuinfo(char * buffer)
{
	int len;

	len = sprintf(buffer,
		"Processor\t: %s %s rev %d\n"
		"BogoMips\t: %lu.%02lu\n"
		"Hardware\t: %s\n"
		"Optimisation\t: %s\n"
		"IO Bus\t\t: %s\n",
		armidlist[armidindex].manu,
		armidlist[armidindex].name,
		(int)processor_id & 15,
		(loops_per_sec+2500) / 500000,
		((loops_per_sec+2500) / 5000) % 100,
#ifdef HARDWARE
		HARDWARE,
#else
		machine_desc[machine_type].machine_name,
#endif
		OPTIMISATION,
#ifdef IO_BUS
		IO_BUS
#else
		machine_desc[machine_type].bus_name
#endif
		);
	return len;
}
