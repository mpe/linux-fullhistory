/*
 * bootinfo.h -- Definition of the Linux/MIPS boot information structure
 *
 * Copyright (C) 1994 by Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 *
 * Based on Linux/68k linux/include/linux/bootstrap.h
 * Copyright (C) 1992 by Greg Harp
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_MIPS_BOOTINFO_H
#define __ASM_MIPS_BOOTINFO_H

/*
 * Valid machtype values
 */
#define MACH_UNKNOWN		0		/* whatever...               */
#define MACH_DESKSTATION_RPC44  1               /* Deskstation rPC44         */
#define MACH_DESKSTATION_TYNE	2		/* Deskstation Tyne          */
#define MACH_ACER_PICA_61	3		/* Acer PICA-61 (PICA1)      */
#define MACH_MIPS_MAGNUM_4000	4		/* Mips Magnum 4000 "RC4030" */
#define MACH_OLIVETTI_M700      5               /* Olivetti M700 */
#define MACH_LAST               5

#define MACH_NAMES { "unknown", "Deskstation rPC44", "Deskstation Tyne", \
	"Acer PICA 61", "Mips Magnum 4000", "Olivetti M700" }

/*
 * Valid cputype values
 */
#define CPU_UNKNOWN		0
#define CPU_R2000		1
#define CPU_R3000		2
#define CPU_R3000A		3
#define CPU_R3041		4
#define CPU_R3051		5
#define CPU_R3052		6
#define CPU_R3081		7
#define CPU_R3081E		8
#define CPU_R4000PC		9
#define CPU_R4000SC		10
#define CPU_R4000MC		11
#define CPU_R4200		12
#define CPU_R4400PC		13
#define CPU_R4400SC		14
#define CPU_R4400MC		15
#define CPU_R4600		16
#define CPU_R6000		17
#define CPU_R6000A		18
#define CPU_R8000		19
#define CPU_R10000		20
#define CPU_LAST                20

#define CPU_NAMES { "unknown", "R2000", "R3000", "R3000A", "R3041", "R3051", \
        "R3052", "R3081", "R3081E", "R4000PC", "R4000SC", "R4000MC",         \
        "R4200", "R4400PC", "R4400SC", "R4400MC", "R4600", "R6000",          \
        "R6000A", "R8000", "R10000" }

#define CL_SIZE      (80)

#ifndef __LANGUAGE_ASSEMBLY__

/*
 * Some machine parameters passed by MILO. Note that bootinfo
 * *must* be in the data segment since the kernel clears the
 * bss segment directly after startup.
 */

struct drive_info_struct {
	char dummy[32];
	};

struct bootinfo {
	/*
	 * machine type
	 */
	unsigned long machtype;

	/*
	 * system CPU & FPU
	 */
	unsigned long cputype;

	/*
	 * Installed RAM
	 */
	unsigned long memlower;
	unsigned long memupper;

	/*
	 * Cache Sizes (0xffffffff = unknown)
	 */
	unsigned long icache_size;
	unsigned long icache_linesize;
	unsigned long dcache_size;
	unsigned long dcache_linesize;
	unsigned long scache_size;
	unsigned long scache_linesize;

	/*
	 * TLB Info
	 */
	unsigned long tlb_entries;

	/*
	 * DMA buffer size (Deskstation only)
	 */
	unsigned long dma_cache_size;
	unsigned long dma_cache_base;

	/*
	 * Ramdisk Info
	 */
	unsigned long ramdisk_flags;		/* ramdisk flags */
	unsigned long ramdisk_base;		/* address of the ram disk in mem */

	/*
	 * Boot flags for the kernel
	 */
	unsigned long mount_root_rdonly;
	struct drive_info_struct drive_info;

	/*
	 * Video ram info (not in tty.h)
	 */
	unsigned long vram_base;		/* video ram base address */
      
	char command_line[CL_SIZE];		/* kernel command line parameters */
  
};

#if 0
/*
 * New style bootinfo
 *
 * Add new tags only at the end of the enum; *never* remove any tags
 * or you'll break compatibility!
 */
enum bi_tag {	
	/*
	 * not a real tag
	 */
	dummy,

	/*
	 * machine type
	 */
	machtype,

	/*
	 * system CPU & FPU
	 */
	cputype,

	/*
	 * Installed RAM
	 */
	memlower,
	memupper,

	/*
	 * Cache Sizes (0xffffffff = unknown)
	 */
	icache_size,
	icache_linesize,
	dcache_size,
	dcache_linesize,
	scache_size,
	scache_linesize,

	/*
	 * TLB Info
	 */
	tlb_entries,

	/*
	 * DMA buffer size (Deskstation only)
	 */
	dma_cache_size,
	dma_cache_base,

	/*
	 * Ramdisk Info
	 */
	ramdisk_size,		/* ramdisk size in 1024 byte blocks */
	ramdisk_base,		/* address of the ram disk in mem */

	/*
	 * Boot flags for the kernel
	 */
	mount_root_rdonly,
	drive_info,

	/*
	 * Video ram info (not in tty.h)
	 */
	vram_base,		/* video ram base address */
      
	command_line		/* kernel command line parameters */
  
};

typedef struct {
	bi_tag		tag;
	unsigned long	size;
} tag;
#endif

extern struct bootinfo boot_info;

/*
 * Defaults, may be overwritten by milo. We initialize
 * them to make sure that both boot_info and screen_info
 * are in the .data segment since the .bss segment is
 * cleared during startup.
 */
#define BOOT_INFO { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, {{0,}}, 0, "" }
#define SCREEN_INFO {0, 0, {0, }, 52, 3, 80, 4626, 3, 9, 50}

#else /* !__LANGUAGE_ASSEMBLY__ */

/*
 * Same structure, but as offsets for usage within assembler source.
 * Don't mess with struct bootinfo without changing offsets too!
 */

#define OFFSET_BOOTINFO_MACHTYPE           0
#define OFFSET_BOOTINFO_CPUTYPE            4
#define OFFSET_BOOTINFO_MEMLOWER           8
#define OFFSET_BOOTINFO_MEMUPPER          12
#define OFFSET_BOOTINFO_ICACHE_SIZE       16
#define OFFSET_BOOTINFO_ICACHE_LINESIZE   20
#define OFFSET_BOOTINFO_DCACHE_SIZE       24
#define OFFSET_BOOTINFO_DCACHE_LINESIZE   28
#define OFFSET_BOOTINFO_SCACHE_SIZE       32
#define OFFSET_BOOTINFO_SCACHE_LINESIZE   36
#define OFFSET_BOOTINFO_TLB_ENTRIES       40
#define OFFSET_BOOTINFO_DMA_CACHE_SIZE    44
#define OFFSET_BOOTINFO_DMA_CACHE_BASE    48
#define OFFSET_BOOTINFO_RAMDISK_SIZE      52
#define OFFSET_BOOTINFO_RAMDISK_BASE      56
#define OFFSET_BOOTINFO_MOUNT_RD_ONLY     60
#define OFFSET_BOOTINFO_DRIVE_INFO        64
#define OFFSET_BOOTINFO_VRAM_BASE         96
#define OFFSET_BOOTINFO_COMMAND_LINE      100

#endif /* __LANGUAGE_ASSEMBLY__ */

#endif /* __ASM_MIPS_BOOTINFO_H */
