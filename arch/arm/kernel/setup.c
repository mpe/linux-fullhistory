/*
 *  linux/arch/arm/kernel/setup.c
 *
 *  Copyright (C) 1995, 1996, 1997 Russell King
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
#include <asm/arch/mmu.h>

struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info = {
  0, 0,  0,  0, 0, 0,  0,  0,  0,  0, 1, 8
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
  { 0x41560600, 0xfffffff0, "ARM/VLSI",	"arm6"	 , "armv3"  , "v3", HWCAP_SWP,
	&arm6_processor_functions   },
  { 0x41560610, 0xfffffff0, "ARM/VLSI",	"arm610" , "armv3"  , "v3", HWCAP_SWP,
	&arm6_processor_functions   },
  { 0x41007000, 0xffffff00, "ARM/VLSI",	"arm7"	 , "armv3"  , "v3", HWCAP_SWP,
	&arm7_processor_functions   },
  /* ARM710 IDs are non-standard */
  { 0x41007100, 0xfff8ff00, "ARM/VLSI",	"arm710" , "armv3"  , "v3", HWCAP_SWP,
	&arm7_processor_functions   },
  { 0x4401a100, 0xfffffff0, "DEC",	"sa110"	 , "armv4"  , "v3", HWCAP_SWP|HWCAP_HALF,
	&sa110_processor_functions  },
#endif
  { 0x00000000, 0x00000000, "***", "unknown", "unknown", "**", 0, NULL }
};

static const struct param_struct *params = (struct param_struct *)PARAMS_BASE;

unsigned long arm_id;
unsigned int vram_half_sam;
int armidindex;
int memc_ctrl_reg;
int number_ide_drives;
int number_mfm_drives;

extern int bytes_per_char_h;
extern int bytes_per_char_v;
extern int root_mountflags;
extern int _etext, _edata, _end;
extern unsigned long real_end_mem;

/*-------------------------------------------------------------------------
 * Early initialisation routines for various configurable items in the
 * kernel.  Each one either supplies a setup_ function, or defines this
 * symbol to be empty if not configured.
 */

/*
 * Risc-PC specific initialisation
 */
#ifdef CONFIG_ARCH_RPC

extern void
init_dram_banks(const struct param_struct *params);

static void
setup_rpc(const struct param_struct *params)
{
	init_dram_banks(params);

	switch (params->u1.s.pages_in_vram) {
	case 256:
		vram_half_sam = 1024;
		break;
	case 512:
	default:
		vram_half_sam = 2048;
	}

	/*
	 * Set ROM speed to maximum
	 */
	outb (0x1d, IOMD_ROMCR0);
}
#else
#define setup_rpc(x)
#endif

/*
 * ram disk
 */
#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */

static void
setup_ramdisk(const struct param_struct *params)
{
	rd_image_start	= params->u1.s.rd_start;
	rd_prompt	= (params->u1.s.flags & FLAG_RDPROMPT) == 0;
	rd_doload	= (params->u1.s.flags & FLAG_RDLOAD) == 0;
}
#else
#define setup_ramdisk(p)
#endif

/*
 * initial ram disk
 */
#ifdef CONFIG_BLK_DEV_INITRD
static void
setup_initrd(const struct param_struct *params, unsigned long memory_end)
{
	if (params->u1.s.initrd_start) {
		initrd_start = params->u1.s.initrd_start;
		initrd_end   = initrd_start + params->u1.s.initrd_size;
	} else {
		initrd_start = 0;
		initrd_end   = 0;
	}

	if (initrd_end > memory_end) {
		printk ("initrd extends beyond end of memory "
			"(0x%08lx > 0x%08lx) - disabling initrd\n",
			initrd_end, memory_end);
		initrd_start = 0;
	}
}
#else
#define setup_initrd(p,m)
#endif

static inline void
get_processor_type(void)
{
	for (armidindex = 0; ; armidindex ++)
		if (!((armidlist[armidindex].id ^ arm_id) &
		      armidlist[armidindex].mask))
			break;

	if (armidlist[armidindex].id == 0) {
		int i;

		for (i = 0; i < 3200; i++)
			((unsigned long *)SCREEN2_BASE)[i] = 0x77113322;

		while (1);
	}
	processor = *armidlist[armidindex].proc;
}

#define COMMAND_LINE_SIZE 256

/* Can this be initdata?  --pb
 *  command_line can be, saved_command_line can't though
 */
static char command_line[COMMAND_LINE_SIZE] __initdata = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

__initfunc(void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	static unsigned char smptrap;
	unsigned long memory_start, memory_end;
	char endian = 'l', c = ' ', *to = command_line;
	char *from;
	int len = 0;

	if (smptrap == 1)
		return;
	smptrap = 1;

	get_processor_type ();
	processor._proc_init ();

#ifndef CONFIG_FB
	bytes_per_char_h  = params->u1.s.bytes_per_char_h;
	bytes_per_char_v  = params->u1.s.bytes_per_char_v;
#endif
	from		  = (char *)params->commandline;
	ROOT_DEV	  = to_kdev_t (params->u1.s.rootdev);
	ORIG_X		  = params->u1.s.video_x;
	ORIG_Y		  = params->u1.s.video_y;
	ORIG_VIDEO_COLS	  = params->u1.s.video_num_cols;
	ORIG_VIDEO_LINES  = params->u1.s.video_num_rows;
	memc_ctrl_reg	  = params->u1.s.memc_control_reg;
	number_ide_drives = (params->u1.s.adfsdrives >> 6) & 3;
	number_mfm_drives = (params->u1.s.adfsdrives >> 3) & 3;

	setup_rpc(params);
	setup_ramdisk(params);

	if (!(params->u1.s.flags & FLAG_READONLY))
		root_mountflags &= ~MS_RDONLY;

	memory_start = MAPTOPHYS((unsigned long)&_end);
	memory_end   = GET_MEMORY_END(params);

	init_task.mm->start_code = TASK_SIZE;
	init_task.mm->end_code	 = TASK_SIZE + (unsigned long) &_etext;
	init_task.mm->end_data	 = TASK_SIZE + (unsigned long) &_edata;
	init_task.mm->brk	 = TASK_SIZE + (unsigned long) &_end;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	for (;;) {
		if (c == ' ' &&
		    from[0] == 'm' &&
		    from[1] == 'e' &&
		    from[2] == 'm' &&
		    from[3] == '=') {
			memory_end = simple_strtoul(from+4, &from, 0);
			if (*from == 'K' || *from == 'k') {
				memory_end = memory_end << 10;
				from++;
			} else if (*from == 'M' || *from == 'm') {
				memory_end = memory_end << 20;
				from++;
			}
			memory_end = memory_end + PAGE_OFFSET;
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
	*memory_start_p = memory_start;
	*memory_end_p = memory_end;

	setup_initrd(params, memory_end);

	sprintf(system_utsname.machine, "%s%c", armidlist[armidindex].arch_vsn, endian);
	sprintf(elf_platform, "%s%c", armidlist[armidindex].elf_vsn, endian);

#ifdef CONFIG_FB
	conswitchp = &fb_con;
#endif
}

#if defined(CONFIG_ARCH_ARC)
#define HARDWARE "Acorn-Archimedes"
#define IO_BUS	 "Acorn"
#elif defined(CONFIG_ARCH_A5K)
#define HARDWARE "Acorn-A5000"
#define IO_BUS	 "Acorn"
#elif defined(CONFIG_ARCH_RPC)
#define HARDWARE "Acorn-RiscPC"
#define IO_BUS	 "Acorn"
#elif defined(CONFIG_ARCH_EBSA110)
#define HARDWARE "DEC-EBSA110"
#define IO_BUS	 "DEC"
#elif defined(CONFIG_ARCH_EBSA285)
#define HARDWARE "DEC-EBSA285"
#define IO_BUS   "PCI"
#elif defined(CONFIG_ARCH_NEXUSPCI)
#define HARDWARE "Nexus-NexusPCI"
#define IO_BUS   "PCI"
#elif defined(CONFIG_ARCH_VNC)
#define HARDWARE "Corel-VNC"
#define IO_BUS   "PCI"
#else
#define HARDWARE "unknown"
#define IO_BUS   "unknown"
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
		"IO Bus\t: %s\n",
		armidlist[armidindex].manu,
		armidlist[armidindex].name,
		(int)arm_id & 15,
		(loops_per_sec+2500) / 500000,
		((loops_per_sec+2500) / 5000) % 100,
		HARDWARE,
		OPTIMISATION,
		IO_BUS);
	return len;
}
