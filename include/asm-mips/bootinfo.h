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
 * Valid values for machtype field
 */
#define MACH_UNKNOWN		0		/* whatever... */
#define MACH_DESKSTATION_TYNE	1		/* Deskstation Tyne    */
#define MACH_ACER_PICA_61	2		/* Acer PICA-61 (PICA1) */
#define MACH_MIPS_MAGNUM_4000	3		/* Mips Magnum 4000 (aka RC4030) */

/*
 * Valid values for cputype field
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

#define CPU_NAMES { "UNKNOWN", "R2000", "R3000", "R3000A", "R3041", "R3051", \
        "R3052", "R3081", "R3081E", "R4000PC", "R4000SC", "R4000MC",         \
        "R4200", "R4400PC", "R4400SC", "R4400MC", "R4600", "R6000",          \
        "R6000A", "R8000", "R10000" }

#define CL_SIZE      (80)

#ifndef __ASSEMBLY__

/*
 * Some machine parameters passed by MILO. Note that bootinfo
 * *must* be in the data segment since the kernel clears the
 * bss segment directly after startup.
 */

struct drive_info_struct {
	char dummy[32];
	};

struct bootinfo {
	unsigned long machtype;			/* machine type */
	unsigned long cputype;			/* system CPU & FPU */

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
	unsigned long ramdisk_size;		/* ramdisk size in 1024 byte blocks */
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

extern struct bootinfo boot_info;

/*
 * Defaults, may be overwritten by milo. We initialize
 * them to make sure that both boot_info and screen_info
 * are in the .data segment since the .bss segment is
 * cleared during startup.
 */
#define BOOT_INFO { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"" }

#else /* !__ASSEMBLY__ */

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
#define OFFSET_BOOTINFO_VRAM_BASE         60
#define OFFSET_BOOTINFO_COMMAND_LINE      64

#endif /* __ASSEMBLY__ */

#endif /* __ASM_MIPS_BOOTINFO_H */
