/* $Id: bootinfo.h,v 1.5 1998/08/19 21:58:10 ralf Exp $
 *
 * bootinfo.h -- Definition of the Linux/MIPS boot information structure
 *
 * Copyright (C) 1995, 1996 by Ralf Baechle, Andreas Busse,
 *                             Stoned Elipot and Paul M. Antoine.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_MIPS_BOOTINFO_H
#define __ASM_MIPS_BOOTINFO_H

/* XXX */
#include <linux/config.h>

/*
 * Values for machgroup
 */
#define MACH_GROUP_UNKNOWN      0 /* whatever... */
#define MACH_GROUP_JAZZ     	1 /* Jazz                                     */
#define MACH_GROUP_DEC          2 /* Digital Equipment                        */
#define MACH_GROUP_ARC		3 /* Wreckstation Tyne, rPC44, possibly other */
#define MACH_GROUP_SNI_RM	4 /* Siemens Nixdorf RM series                */
#define MACH_GROUP_ACN		5
#define MACH_GROUP_SGI          6 /* Silicon Graphics workstations and servers */
#define MACH_GROUP_RESERVED     7 /* No Such Architecture	 	      */

#define GROUP_NAMES { "unknown", "Jazz", "Digital", "ARC", \
                      "SNI", "ACN", "SGI", "NSA" }

/*
 * Valid machtype values for group unknown (low order halfword of mips_machtype)
 */
#define MACH_UNKNOWN		0	/* whatever...			*/

#define GROUP_UNKNOWN_NAMES { "unknown" }

/*
 * Valid machtype values for group JAZZ
 */
#define MACH_ACER_PICA_61	0	/* Acer PICA-61 (PICA1)		*/
#define MACH_MIPS_MAGNUM_4000	1	/* Mips Magnum 4000 "RC4030"	*/
#define MACH_OLIVETTI_M700      2	/* Olivetti M700-10 (-15 ??)    */

#define GROUP_JAZZ_NAMES { "Acer PICA 61", "Mips Magnum 4000", "Olivetti M700" }

/*
 * Valid machtype for group DEC 
 */
/* FIXME: this is a very fuzzy name, and we got a big "name space now" */
/* So otiginal DEC codes can be used -Stoned */
#define MACH_DECSTATION		0	/* DECStation 5000/2x for now */

#define GROUP_DEC_NAMES { "3min" }

/*
 * Valid machtype for group ARC
 */
#define MACH_DESKSTATION_RPC44  0	/* Deskstation rPC44 */
#define MACH_DESKSTATION_TYNE	1	/* Deskstation Tyne */

#define GROUP_ARC_NAMES { "Deskstation rPC44", "Deskstation Tyne" }

/*
 * Valid machtype for group SNI_RM
 */
#define MACH_SNI_RM200_PCI	0	/* RM200/RM300/RM400 PCI series */

#define GROUP_SNI_RM_NAMES { "RM200 PCI" }

/*
 * Valid machtype for group ACN
 */
#define MACH_ACN_MIPS_BOARD	0       /* ACN MIPS single board        */

#define GROUP_ACN_NAMES { "ACN" }

/*
 * Valid machtype for group SGI
 */
#define MACH_SGI_INDY		0	/* R4?K and R5K Indy workstaions */

#define GROUP_SGI_NAMES { "Indy" }

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
#define CPU_R4300		21
#define CPU_R4650		22
#define CPU_R4700		23
#define CPU_R5000		24
#define CPU_R5000A		25
#define CPU_R4640		26
#define CPU_NEVADA		27	/* RM5230, RM5260 */
#define CPU_LAST		27

#define CPU_NAMES { "unknown", "R2000", "R3000", "R3000A", "R3041", "R3051", \
        "R3052", "R3081", "R3081E", "R4000PC", "R4000SC", "R4000MC",         \
        "R4200", "R4400PC", "R4400SC", "R4400MC", "R4600", "R6000",          \
        "R6000A", "R8000", "R10000", "R4300", "R4650", "R4700", "R5000",     \
        "R5000A", "R4640", "Nevada" }

#define CL_SIZE      (80)

#ifndef _LANGUAGE_ASSEMBLY

/*
 * Some machine parameters passed by the bootloaders. 
 */

struct drive_info_struct {
	char dummy[32];
};

/* This is the same as in Milo but renamed for the sake of kernel's */
/* namespace */
typedef struct mips_arc_DisplayInfo {	/* video adapter information */
	unsigned short cursor_x;
	unsigned short cursor_y;
	unsigned short columns;
	unsigned short lines;
} mips_arc_DisplayInfo;

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
	tag_dummy,
 
 	/*
 	 * machine type
 	 */
	tag_machtype,
 
 	/*
 	 * system CPU & FPU
 	 */
	tag_cputype,
 
 	/*
 	 * Installed RAM
 	 */
	tag_memlower,
	tag_memupper,
 
 	/*
 	 * Cache Sizes (0xffffffff = unknown)
 	 */
	tag_icache_size,
	tag_icache_linesize,
	tag_dcache_size,
	tag_dcache_linesize,
	tag_scache_size,
	tag_scache_linesize,
 
 	/*
 	 * TLB Info
 	 */
	tag_tlb_entries,
 
 	/*
 	 * DMA buffer size (Deskstation only)
 	 */
	tag_dma_cache_size,
	tag_dma_cache_base,
 
 	/*
	 * Ramdisk Info 
 	 */
	tag_ramdisk_size,		/* ramdisk size in 1024 byte blocks */
	tag_ramdisk_base,		/* address of the ram disk in mem */
 
 	/*
 	 * Boot flags for the kernel
 	 */
	tag_mount_root_rdonly,
	tag_drive_info,
 
 	/*
 	 * Video ram info (not in tty.h)
 	 */
	tag_vram_base,			/* video ram base address */
       
	tag_command_line,		/* kernel command line parameters */

        /*
         * machine group
         */
	tag_machgroup,

	/*
	 * info on the display from the ARC BIOS
	 */
	tag_arcdisplayinfo,

	/*
	 * tag to pass a complete struct screen_info
	 */
	tag_screen_info
};

/* struct defining a tag */
typedef struct {
	enum bi_tag tag;
	unsigned long size;
} tag;

/* struct to define a tag and it's data */
typedef struct {
	tag t;
	void* d;
} tag_def;

/* macros for parsing tag list */
#define TAGVALPTR(t) ((void*)(((void*)(t)) - ((t)->size)))
#define NEXTTAGPTR(t) ((void*)(TAGVALPTR(t) - (sizeof(tag))))

/* size macros for tag size field */
#define UCHARSIZE (sizeof(unsigned char))
#define ULONGSIZE (sizeof(unsigned long))
#define UINTSIZE  (sizeof(unsigned int))
#define DRVINFOSIZE (sizeof(struct drive_info_struct))
#define CMDLINESIZE (sizeof(char[CL_SIZE])

/*
 * For tag readers aka the kernel
 */
tag *bi_TagFind(enum bi_tag type);
void bi_EarlySnarf(void);

/* For tag creators aka bootloaders */
/* Now implemented in Milo 0.26 */
int bi_TagAdd(enum bi_tag type, unsigned long size, void *data);
int bi_TagAddList(tag_def* taglist);
void bi_TagWalk(void); 


#ifdef CONFIG_SGI
/* screen info will dissapear... soon */
//#define DEFAULT_SCREEN_INFO {0, 0, 0, 0, 0, 158, 0, 0, 0, 62, 0, 16}
#define DEFAULT_SCREEN_INFO {0, 0, 0, 0, 0, 160, 0, 0, 0, 64, 0, 16}
#define DEFAULT_DRIVE_INFO { {0,}}
#else
/* default values for screen_info variable */
#define DEFAULT_SCREEN_INFO {0, 0, 0, 52, 3, 80, 4626, 3, 9, 50}
#endif

/* default values for drive info */
#define DEFAULT_DRIVE_INFO { {0,}}


/*
 * These are the kernel variables initialized from
 * the tag. And they have to be initialized to dummy/default
 * values in setup.c (or whereever suitable) so they are in
 * .data section
 */
extern unsigned long mips_memory_upper;
extern unsigned long mips_cputype;
extern unsigned long mips_machtype;
extern unsigned long mips_machgroup;
extern unsigned long mips_tlb_entries;
extern unsigned long mips_vram_base;
extern unsigned long mips_dma_cache_size;
extern unsigned long mips_dma_cache_base;

#endif /* _LANGUAGE_ASSEMBLY */

#endif /* __ASM_MIPS_BOOTINFO_H */
