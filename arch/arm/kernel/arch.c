/*
 * linux/arch/arm/kernel/arch.c
 *
 * Architecture specific fixups.  This is where any
 * parameters in the params struct are fixed up, or
 * any additional architecture specific information
 * is pulled from the params struct.
 */
#include <linux/config.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>

#include <asm/dec21285.h>
#include <asm/elf.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include "arch.h"

unsigned int vram_size;

extern void setup_initrd(unsigned int start, unsigned int size);
extern void setup_ramdisk(int doload, int prompt, int start, unsigned int rd_sz);

#ifdef CONFIG_ARCH_ACORN

unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;

static void __init
fixup_acorn(struct machine_desc *desc, struct param_struct *params,
	    char **cmdline, struct meminfo *mi)
{
	if (machine_is_riscpc()) {
		int i;

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
			mi->bank[i].node  = 0;
			mi->bank[i].size  =
				params->u1.s.pages_in_bank[i] *
				params->u1.s.page_size;
		}
		mi->nr_banks = 4;
	}
	memc_ctrl_reg	  = params->u1.s.memc_control_reg;
	number_mfm_drives = (params->u1.s.adfsdrives >> 3) & 3;
}

#ifdef CONFIG_ARCH_RPC
MACHINE_START(RISCPC, "Acorn-RiscPC")
	MAINTAINER("Russell King")
	BOOT_MEM(0x10000000, 0x03000000, 0xe0000000)
	BOOT_PARAMS(0x10000100)
	DISABLE_PARPORT(0)
	DISABLE_PARPORT(1)
	FIXUP(fixup_acorn)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_ARC
MACHINE_START(ARCHIMEDES, "Acorn-Archimedes")
	MAINTAINER("Dave Gilbert")
	BOOT_PARAMS(0x0207c000)
	FIXUP(fixup_acorn)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_A5K
MACHINE_START(A5K, "Acorn-A5000")
	MAINTAINER("Russell King")
	BOOT_PARAMS(0x0207c000)
	FIXUP(fixup_acorn)
MACHINE_END
#endif
#endif

#ifdef CONFIG_ARCH_EBSA285

static void __init
fixup_ebsa285(struct machine_desc *desc, struct param_struct *params,
	      char **cmdline, struct meminfo *mi)
{
	ORIG_X		 = params->u1.s.video_x;
	ORIG_Y		 = params->u1.s.video_y;
	ORIG_VIDEO_COLS  = params->u1.s.video_num_cols;
	ORIG_VIDEO_LINES = params->u1.s.video_num_rows;
}

MACHINE_START(EBSA285, "EBSA285")
	MAINTAINER("Russell King")
	BOOT_MEM(0x00000000, DC21285_ARMCSR_BASE, 0xfe000000)
	BOOT_PARAMS(0x00000100)
	VIDEO(0x000a0000, 0x000bffff)
	FIXUP(fixup_ebsa285)
MACHINE_END
#endif

#ifdef CONFIG_ARCH_NETWINDER
/*
 * Older NeTTroms either do not provide a parameters
 * page, or they don't supply correct information in
 * the parameter page.
 */
static void __init
fixup_netwinder(struct machine_desc *desc, struct param_struct *params,
		char **cmdline, struct meminfo *mi)
{
#ifdef CONFIG_ISAPNP
	extern int isapnp_disable;

	/*
	 * We must not use the kernels ISAPnP code
	 * on the NetWinder - it will reset the settings
	 * for the WaveArtist chip and render it inoperable.
	 */
	isapnp_disable = 1;
#endif

	if (params->u1.s.nr_pages != 0x02000 &&
	    params->u1.s.nr_pages != 0x04000 &&
	    params->u1.s.nr_pages != 0x08000 &&
	    params->u1.s.nr_pages != 0x10000) {
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

MACHINE_START(NETWINDER, "Rebel-NetWinder")
	MAINTAINER("Russell King/Rebel.com")
	BOOT_MEM(0x00000000, DC21285_ARMCSR_BASE, 0xfe000000)
	BOOT_PARAMS(0x00000100)
	VIDEO(0x000a0000, 0x000bffff)
	DISABLE_PARPORT(0)
	DISABLE_PARPORT(2)
	FIXUP(fixup_netwinder)
MACHINE_END
#endif

#ifdef CONFIG_ARCH_CATS
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

MACHINE_START(CATS, "Chalice-CATS")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x00000000, DC21285_ARMCSR_BASE, 0xfe000000)
	SOFT_REBOOT
	FIXUP(fixup_cats)
MACHINE_END
#endif

#ifdef CONFIG_ARCH_CO285

static void __init
fixup_coebsa285(struct machine_desc *desc, struct param_struct *params,
		char **cmdline, struct meminfo *mi)
{
	extern unsigned long boot_memory_end;
	extern char boot_command_line[];

	mi->nr_banks      = 1;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size  = boot_memory_end;
	mi->bank[0].node  = 0;

	*cmdline = boot_command_line;
}

MACHINE_START(CO285, "co-EBSA285")
	MAINTAINER("Mark van Doesburg")
	BOOT_MEM(0x00000000, DC21285_ARMCSR_BASE, 0x7cf00000)
	FIXUP(fixup_coebsa285)
MACHINE_END
#endif

#ifdef CONFIG_ARCH_SA1100

static void victor_power_off(void)
{
	/* switch off power supply */
	mdelay(2000);
	GPCR = GPIO_GPIO23;
	while (1);
}


static void xp860_power_off(void)
{
	GPDR |= GPIO_GPIO20;
	GPSR = GPIO_GPIO20;
	mdelay(1000);
	GPCR = GPIO_GPIO20;
	while(1);
}


extern void select_sa1100_io_desc(void);
#define SET_BANK(__nr,__start,__size) \
	mi->bank[__nr].start = (__start), \
	mi->bank[__nr].size = (__size), \
	mi->bank[__nr].node = (((unsigned)(__start) - PHYS_OFFSET) >> 27)
static void __init
fixup_sa1100(struct machine_desc *desc, struct param_struct *params,
	     char **cmdline, struct meminfo *mi)
{
	select_sa1100_io_desc();

	if (machine_is_assabet()) {
		/* 
		 * On Assabet, we must probe for the Neponset board *before*
		 * paging_init() has occured to actually determine the amount
		 * of RAM available.
		 */
		extern void map_sa1100_gpio_regs(void);
		extern void get_assabet_scr(void);
		map_sa1100_gpio_regs();
		get_assabet_scr();

		SET_BANK( 0, 0xc0000000, 32*1024*1024 );
		mi->nr_banks = 1;

		if (machine_has_neponset()) {
			printk("Neponset expansion board detected\n");
			/* 
			 * Note that Neponset RAM is slower...
			 * and still untested. 
			 * This would be a candidate for
			 * _real_ NUMA support. 
			 */
			//SET_BANK( 1, 0xd0000000, 32*1024*1024 );
			//mi->nr_banks = 2;
		}

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk( 1, 0, 0, 8192 );
		setup_initrd( 0xc0800000, 3*1024*1024 );
	}

	else if (machine_is_brutus()) {
		SET_BANK( 0, 0xc0000000, 4*1024*1024 );
		SET_BANK( 1, 0xc8000000, 4*1024*1024 );
		SET_BANK( 2, 0xd0000000, 4*1024*1024 );
		SET_BANK( 3, 0xd8000000, 4*1024*1024 );
		mi->nr_banks = 4;

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk( 1, 0, 0, 8192 );
		setup_initrd( __phys_to_virt(0xd8000000), 3*1024*1024 );
	}

        else if (machine_is_cerf()) {
                // 16Meg Ram.
                SET_BANK( 0, 0xc0000000, 8*1024*1024 );
                SET_BANK( 1, 0xc8000000, 8*1024*1024 );			// comment this out for 8MB Cerfs
                mi->nr_banks = 2;

                ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
                setup_ramdisk(1,  0, 0, 8192);
                // Save 2Meg for RAMDisk
                setup_initrd(0xc0500000, 3*1024*1024);
        }

	else if (machine_is_empeg()) {
		SET_BANK( 0, 0xc0000000, 4*1024*1024 );
		SET_BANK( 1, 0xc8000000, 4*1024*1024 );
		mi->nr_banks = 2;

		ROOT_DEV = MKDEV( 3, 1 );  /* /dev/hda1 */
		setup_ramdisk( 1, 0, 0, 4096 );
		setup_initrd( 0xd0000000+((1024-320)*1024), (320*1024) );
	}

	else if (machine_is_lart()) {
		/*
		 * Note that LART is a special case - it doesn't use physical
		 * address line A23 on the DRAM, so we effectively have 4 * 8MB
		 * in two SA1100 banks.
		 */
		SET_BANK( 0, 0xc0000000, 8*1024*1024 );
		SET_BANK( 1, 0xc1000000, 8*1024*1024 );
		SET_BANK( 2, 0xc8000000, 8*1024*1024 );
		SET_BANK( 3, 0xc9000000, 8*1024*1024 );
		mi->nr_banks = 4;

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk(1, 0, 0, 8192);
		setup_initrd(0xc0400000, 4*1024*1024);
	}

	else if (machine_is_thinclient() || machine_is_graphicsclient()) {
		SET_BANK( 0, 0xc0000000, 16*1024*1024 );
		mi->nr_banks = 1;

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk( 1, 0, 0, 8192 );
		setup_initrd( __phys_to_virt(0xc0800000), 4*1024*1024 );
	}

	else if (machine_is_nanoengine()) {
		SET_BANK( 0, 0xc0000000, 32*1024*1024 );
		mi->nr_banks = 1;

		ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
		setup_ramdisk( 1, 0, 0, 8192 );
		setup_initrd( __phys_to_virt(0xc0800000), 4*1024*1024 );

		/* Get command line parameters passed from the loader (if any) */
		if( *((char*)0xc0000100) )
			*cmdline = ((char *)0xc0000100);
	}
	else if (machine_is_tifon()) {
		SET_BANK( 0, 0xc0000000, 16*1024*1024 );
		SET_BANK( 1, 0xc8000000, 16*1024*1024 );
		mi->nr_banks = 2;

		ROOT_DEV = MKDEV(UNNAMED_MAJOR, 0);
		setup_ramdisk(1, 0, 0, 4096);
		setup_initrd( 0xd0000000 + 0x1100004, 0x140000 );
	}

	else if (machine_is_victor()) {
		SET_BANK( 0, 0xc0000000, 4*1024*1024 );
		mi->nr_banks = 1;

		ROOT_DEV = MKDEV( 60, 2 );

		/* Get command line parameters passed from the loader (if any) */
		if( *((char*)0xc0000000) )
			strcpy( *cmdline, ((char *)0xc0000000) );

		/* power off if any problem */
		strcat( *cmdline, " panic=1" );

		pm_power_off = victor_power_off;
	}

	else if (machine_is_xp860()) {
		SET_BANK( 0, 0xc0000000, 32*1024*1024 );
		mi->nr_banks = 1;

		pm_power_off = xp860_power_off;
	}
}

#ifdef CONFIG_SA1100_ASSABET
MACHINE_START(ASSABET, "Intel-Assabet")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_BITSY
MACHINE_START(BITSY, "Compaq Bitsy")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_BRUTUS
MACHINE_START(BRUTUS, "Intel Brutus (SA1100 eval board)")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_CERF
MACHINE_START(CERF, "Intrinsyc CerfBoard")
	MAINTAINER("Pieter Truter")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_EMPEG
MACHINE_START(EMPEG, "empeg MP3 Car Audio Player")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_GRAPHICSCLIENT
MACHINE_START(GRAPHICSCLIENT, "ADS GraphicsClient")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_ITSY
MACHINE_START(ITSY, "Compaq Itsy")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_LART
MACHINE_START(LART, "LART")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_NANOENGINE
MACHINE_START(NANOENGINE, "BSE nanoEngine")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_PLEB
MACHINE_START(PLEB, "PLEB")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_THINCLIENT
MACHINE_START(THINCLIENT, "ADS ThinClient")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_TIFON
MACHINE_START(TIFON, "Tifon")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_VICTOR
MACHINE_START(VICTOR, "VisuAide Victor")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#ifdef CONFIG_SA1100_XP860
MACHINE_START(XP860, "XP860")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_sa1100)
MACHINE_END
#endif
#endif

#ifdef CONFIG_ARCH_L7200

static void __init
fixup_l7200(struct machine_desc *desc, struct param_struct *params,
             char **cmdline, struct meminfo *mi)
{
        mi->nr_banks      = 1;
        mi->bank[0].start = PHYS_OFFSET;
        mi->bank[0].size  = (32*1024*1024);
        mi->bank[0].node  = 0;

        ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
        setup_ramdisk( 1, 0, 0, 8192 );
        setup_initrd( __phys_to_virt(0xf1000000), 0x00162b0d);
}

MACHINE_START(L7200, "LinkUp Systems L7200SDB")
	MAINTAINER("Steve Hill")
	BOOT_MEM(0xf0000000, 0x80040000, 0xd0000000)
	FIXUP(fixup_l7200)
MACHINE_END
#endif

#ifdef CONFIG_ARCH_EBSA110
MACHINE_START(EBSA110, "EBSA110")
	MAINTAINER("Russell King")
	BOOT_MEM(0x00000000, 0xe0000000, 0xe0000000)
	BOOT_PARAMS(0x00000400)
	DISABLE_PARPORT(0)
	DISABLE_PARPORT(2)
	SOFT_REBOOT
MACHINE_END
#endif
#ifdef CONFIG_ARCH_NEXUSPCI
MACHINE_START(NEXUSPCI, "FTV/PCI")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x40000000, 0x10000000, 0xe0000000)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_TBOX
MACHINE_START(TBOX, "unknown-TBOX")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x80000000, 0x00400000, 0xe0000000)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_CLPS7110
MACHINE_START(CLPS7110, "CL-PS7110")
	MAINTAINER("Werner Almesberger")
MACHINE_END
#endif
#ifdef CONFIG_ARCH_ETOILE
MACHINE_START(ETOILE, "Etoile")
	MAINTAINER("Alex de Vries")
MACHINE_END
#endif
#ifdef CONFIG_ARCH_LACIE_NAS
MACHINE_START(LACIE_NAS, "LaCie_NAS")
	MAINTAINER("Benjamin Herrenschmidt")
MACHINE_END
#endif
#ifdef CONFIG_ARCH_CLPS7500
MACHINE_START(CLPS7500, "CL-PS7500")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x10000000, 0x03000000, 0xe0000000)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_SHARK
MACHINE_START(SHARK, "Shark")
	MAINTAINER("Alexander Schulz")
	BOOT_MEM(0x08000000, 0x40000000, 0xe0000000)
	VIDEO(0x06000000, 0x061fffff)
MACHINE_END
#endif
#ifdef CONFIG_ARCH_PERSONAL_SERVER
MACHINE_START(PERSONAL_SERVER, "Compaq Personal Server")
	MAINTAINER("Jamey Hicks / George France")
	BOOT_MEM(0x00000000, DC21285_ARMCSR_BASE, 0xfe000000)
	BOOT_PARAMS(0x00000100)
MACHINE_END
#endif
