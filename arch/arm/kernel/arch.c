/*
 * linux/arch/arm/kernel/arch.c
 *
 * Architecture specifics
 */
#include <linux/config.h>
#include <linux/tty.h>
#include <linux/init.h>

#include <asm/elf.h>
#include <asm/setup.h>
#include <asm/system.h>

#include "arch.h"

extern unsigned int system_rev;
extern unsigned int system_serial_low;
extern unsigned int system_serial_high;

unsigned int vram_size;
#ifdef CONFIG_ARCH_ACORN
unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;
#endif

extern void setup_initrd(unsigned int start, unsigned int size);
extern void setup_ramdisk(int doload, int prompt, int start, unsigned int rd_sz);

/*
 * Architecture specific fixups.  This is where any
 * parameters in the params struct are fixed up, or
 * any additional architecture specific information
 * is pulled from the params struct.
 */
static void __init
fixup_acorn(struct machine_desc *desc, struct param_struct *params,
	    char **cmdline, struct meminfo *mi)
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
			mi->bank[i].start = PHYS_OFFSET + (i << 26);
			mi->bank[i].size  =
				params->u1.s.pages_in_bank[i] *
				params->u1.s.page_size;
		}
		mi->nr_banks = 4;
	}
	memc_ctrl_reg	  = params->u1.s.memc_control_reg;
	number_mfm_drives = (params->u1.s.adfsdrives >> 3) & 3;
#endif
}

static void __init
fixup_ebsa285(struct machine_desc *desc, struct param_struct *params,
	      char **cmdline, struct meminfo *mi)
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
		char **cmdline, struct meminfo *mi)
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
	   char **cmdline, struct meminfo *mi)
{
	ORIG_VIDEO_LINES  = 25;
	ORIG_VIDEO_POINTS = 16;
	ORIG_Y = 24;
}

static void __init
fixup_coebsa285(struct machine_desc *desc, struct param_struct *params,
		char **cmdline, struct meminfo *mi)
{
#if 0
	extern unsigned long boot_memory_end;
	extern char boot_command_line[];

	mi->nr_banks      = 1;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size  = boot_memory_end;

	*cmdline = boot_command_line;
#endif
}

static void __init
fixup_sa1100(struct machine_desc *desc, struct param_struct *params,
	     char **cmdline, struct meminfo *mi)
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
		mi->bank[i].start = mem_desc[i].phys_start;
		mi->bank[i].size = mem_desc[i].length;
	}
	mi->nr_banks = i;

#if defined(CONFIG_SA1100_BRUTUS)
	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( __phys_to_virt(0xd8000000), 3*1024*1024 );
#elif defined(CONFIG_SA1100_EMPEG)
	ROOT_DEV = MKDEV( 3, 1 );  /* /dev/hda1 */
	setup_ramdisk( 1, 0, 0, 4096 );
	setup_initrd( 0xd0000000+((1024-320)*1024), (320*1024) );
#elif defined(CONFIG_SA1100_THINCLIENT)
	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( __phys_to_virt(0xc0800000), 4*1024*1024 );
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
static struct machine_desc machine_desc[] __attribute__ ((__section__ (".arch.info"))) = {
#ifdef CONFIG_ARCH_EBSA110
	{
		MACH_TYPE_EBSA110,
		"EBSA110",	/* RMK			*/
		0x00000400,
		NO_VIDEO,
		1, 0, 1, 0, 1,
		NULL
	},
#endif
#ifdef CONFIG_ARCH_RPC
	{
		MACH_TYPE_RISCPC,
		"Acorn-RiscPC",	/* RMK			*/
		0x10000100,
		NO_VIDEO,
		1, 1, 0, 0, 0,
		fixup_acorn
	},
#endif
#ifdef CONFIG_ARCH_NEXUSPCI
	{
		MACH_TYPE_NEXUSPCI,
		"FTV/PCI",	/* Philip Blundell	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	},
#endif
#ifdef CONFIG_ARCH_EBSA285
	{
		MACH_TYPE_EBSA285,
		"EBSA285",		/* RMK			*/
		0x00000100,
		0x000a0000, 0x000bffff,
		0, 0, 0, 0, 0,
		fixup_ebsa285
	},
#endif
#ifdef CONFIG_ARCH_NETWINDER
	{
		MACH_TYPE_NETWINDER,
		"Rebel-NetWinder",	/* RMK			*/
		0x00000100,
		0x000a0000, 0x000bffff,
		1, 0, 1, 0, 0,
		fixup_netwinder
	},
#endif
#ifdef CONFIG_ARCH_CATS
	{
		MACH_TYPE_CATS,
		"Chalice-CATS",	/* Philip Blundell	*/
		NO_PARAMS,
		0x000a0000, 0x000bffff,
		0, 0, 0, 0, 1,
		fixup_cats
	},
#endif
#ifdef CONFIG_ARCH_TBOX
	{
		MACH_TYPE_TBOX,
		"unknown-TBOX",	/* Philip Blundell	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	},
#endif
#ifdef CONFIG_ARCH_CO285
	{
		MACH_TYPE_CO285,
		"co-EBSA285",	/* Mark van Doesburg	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_coebsa285
	},
#endif
#ifdef CONFIG_ARCH_CLPS7110
	{
		MACH_TYPE_CLPS7110,
		"CL-PS7110",	/* Werner Almesberger	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	},
#endif
#ifdef CONFIG_ARCH_ARC
	{
		MACH_TYPE_ARCHIMEDES,
		"Acorn-Archimedes",/* RMK/DAG		*/
		0x0207c000,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_acorn
	},
#endif
#ifdef CONFIG_ARCH_A5K
	{
		MACH_TYPE_A5K,
		"Acorn-A5000",	/* RMK/PB		*/
		0x0207c000,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_acorn
	},
#endif
#ifdef CONFIG_ARCH_ETOILE
	{
		MACH_TYPE_ETOILE,
		"Etoile",		/* Alex de Vries	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	},
#endif
#ifdef CONFIG_ARCH_LACIE_NAS
	{
		MACH_TYPE_LACIE_NAS,
		"LaCie_NAS",	/* Benjamin Herrenschmidt */
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	},
#endif
#ifdef CONFIG_ARCH_CLPS7500
	{
		MACH_TYPE_CLPS7500,
		"CL-PS7500",	/* Philip Blundell	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	},
#endif
#ifdef CONFIG_ARCH_SHARK
	{
		MACH_TYPE_SHARK,
		"Shark",		/* Alexander Schulz	*/
		NO_PARAMS,
		0x06000000, 0x06000000+0x001fffff,
		0, 0, 0, 0, 0,
		NULL
	},
#endif
#ifdef CONFIG_ARCH_SA1100
	{
		MACH_TYPE_SA1100,
		"SA1100-based",	/* Nicolas Pitre	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_sa1100
	},
#endif
#ifdef CONFIG_ARCH_PERSONAL_SERVER
	{
		MACH_TYPE_PERSONAL_SERVER,
		"Compaq Personal Server",
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}
#endif
};
