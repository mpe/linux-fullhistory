/*
** asm/bootinfo.h -- Definition of the Linux/m68k boot information structure
**
** Copyright 1992 by Greg Harp
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created 09/29/92 by Greg Harp
**
** 5/2/94 Roman Hodek:
**   Added bi_atari part of the machine dependent union bi_un; for now it
**   contains just a model field to distinguish between TT and Falcon.
** 26/7/96 Roman Zippel:
**   Renamed to setup.h; added some useful macros to allow gcc some
**   optimizations if possible.
** 5/10/96 Geert Uytterhoeven:
**   Redesign of the boot information structure; renamed to bootinfo.h again
** 27/11/96 Geert Uytterhoeven:
**   Backwards compatibility with bootinfo interface version 1.0
*/

#ifndef _M68K_BOOTINFO_H
#define _M68K_BOOTINFO_H


    /*
     *  Bootinfo definitions
     *
     *  This is an easily parsable and extendable structure containing all
     *  information to be passed from the bootstrap to the kernel.
     *
     *  This way I hope to keep all future changes back/forewards compatible.
     *  Thus, keep your fingers crossed...
     *
     *  This structure is copied right after the kernel bss by the bootstrap
     *  routine.
     */

#ifndef __ASSEMBLY__

struct bi_record {
    unsigned short tag;			/* tag ID */
    unsigned short size;		/* size of record (in bytes) */
    unsigned long data[0];		/* data */
};

#else /* __ASSEMBLY__ */

BIR_tag		= 0
BIR_size	= BIR_tag+2
BIR_data	= BIR_size+2

#endif /* __ASSEMBLY__ */


    /*
     *  Tag Definitions
     *
     *  Machine independent tags start counting from 0x0000
     *  Machine dependent tags start counting from 0x8000
     */

#define BI_LAST			0x0000	/* last record (sentinel) */
#define BI_MACHTYPE		0x0001	/* machine type (u_long) */
#define BI_CPUTYPE		0x0002	/* cpu type (u_long) */
#define BI_FPUTYPE		0x0003	/* fpu type (u_long) */
#define BI_MMUTYPE		0x0004	/* mmu type (u_long) */
#define BI_MEMCHUNK		0x0005	/* memory chunk address and size */
					/* (struct mem_info) */
#define BI_RAMDISK		0x0006	/* ramdisk address and size */
					/* (struct mem_info) */
#define BI_COMMAND_LINE		0x0007	/* kernel command line parameters */
					/* (string) */

    /*
     *  Amiga-specific tags
     */

#define BI_AMIGA_MODEL		0x8000	/* model (u_long) */
#define BI_AMIGA_AUTOCON	0x8001	/* AutoConfig device */
					/* (struct ConfigDev) */
#define BI_AMIGA_CHIP_SIZE	0x8002	/* size of Chip RAM (u_long) */
#define BI_AMIGA_VBLANK		0x8003	/* VBLANK frequency (u_char) */
#define BI_AMIGA_PSFREQ		0x8004	/* power supply frequency (u_char) */
#define BI_AMIGA_ECLOCK		0x8005	/* EClock frequency (u_long) */
#define BI_AMIGA_CHIPSET	0x8006	/* native chipset present (u_long) */
#define BI_AMIGA_SERPER		0x8007	/* serial port period (u_short) */

    /*
     *  Atari-specific tags
     */

#define BI_ATARI_MCH_COOKIE	0x8000	/* _MCH cookie from TOS (u_long) */


    /*
     * Stuff for bootinfo interface versioning
     *
     * At the start of kernel code, a 'struct bootversion' is located.
     * bootstrap checks for a matching version of the interface before booting
     * a kernel, to avoid user confusion if kernel and bootstrap don't work
     * together :-)
     *
     * If incompatible changes are made to the bootinfo interface, the major
     * number below should be stepped (and the minor reset to 0) for the
     * appropriate machine. If a change is backward-compatible, the minor
     * should be stepped. "Backwards-compatible" means that booting will work,
     * but certain features may not.
     */

#define BOOTINFOV_MAGIC			0x4249561A	/* 'BIV^Z' */
#define MK_BI_VERSION(major,minor)	(((major)<<16)+(minor))
#define BI_VERSION_MAJOR(v)		(((v) >> 16) & 0xffff)
#define BI_VERSION_MINOR(v)		((v) & 0xffff)

#ifndef __ASSEMBLY__

struct bootversion {
    unsigned short branch;
    unsigned long magic;
    struct {
	unsigned long machtype;
	unsigned long version;
    } machversions[0];
};

#endif /* __ASSEMBLY__ */

#define AMIGA_BOOTI_VERSION    MK_BI_VERSION( 2, 0 )
#define ATARI_BOOTI_VERSION    MK_BI_VERSION( 2, 0 )


#ifdef BOOTINFO_COMPAT_1_0

    /*
     *  Backwards compatibility with bootinfo interface version 1.0
     */

#define COMPAT_AMIGA_BOOTI_VERSION    MK_BI_VERSION( 1, 0 )
#define COMPAT_ATARI_BOOTI_VERSION    MK_BI_VERSION( 1, 0 )

#include <linux/zorro.h>

#define COMPAT_NUM_AUTO    16

struct compat_bi_Amiga {
    int model;
    int num_autocon;
    struct ConfigDev autocon[COMPAT_NUM_AUTO];
    unsigned long chip_size;
    unsigned char vblank;
    unsigned char psfreq;
    unsigned long eclock;
    unsigned long chipset;
    unsigned long hw_present;
};

struct compat_bi_Atari {
    unsigned long hw_present;
    unsigned long mch_cookie;
};

struct compat_mem_info {
    unsigned long addr;
    unsigned long size;
};

#define COMPAT_NUM_MEMINFO  4

#define COMPAT_CPUB_68020 0
#define COMPAT_CPUB_68030 1
#define COMPAT_CPUB_68040 2
#define COMPAT_CPUB_68060 3
#define COMPAT_FPUB_68881 5
#define COMPAT_FPUB_68882 6
#define COMPAT_FPUB_68040 7
#define COMPAT_FPUB_68060 8

#define COMPAT_CPU_68020    (1<<COMPAT_CPUB_68020)
#define COMPAT_CPU_68030    (1<<COMPAT_CPUB_68030)
#define COMPAT_CPU_68040    (1<<COMPAT_CPUB_68040)
#define COMPAT_CPU_68060    (1<<COMPAT_CPUB_68060)
#define COMPAT_CPU_MASK     (31)
#define COMPAT_FPU_68881    (1<<COMPAT_FPUB_68881)
#define COMPAT_FPU_68882    (1<<COMPAT_FPUB_68882)
#define COMPAT_FPU_68040    (1<<COMPAT_FPUB_68040)
#define COMPAT_FPU_68060    (1<<COMPAT_FPUB_68060)
#define COMPAT_FPU_MASK     (0xfe0)

#define COMPAT_CL_SIZE      (256)

struct compat_bootinfo {
    unsigned long machtype;
    unsigned long cputype;
    struct compat_mem_info memory[COMPAT_NUM_MEMINFO];
    int num_memory;
    unsigned long ramdisk_size;
    unsigned long ramdisk_addr;
    char command_line[COMPAT_CL_SIZE];
    union {
	struct compat_bi_Amiga bi_ami;
	struct compat_bi_Atari bi_ata;
    } bi_un;
};

#define bi_amiga	bi_un.bi_ami
#define bi_atari	bi_un.bi_ata

#endif /* BOOTINFO_COMPAT_1_0 */


#endif /* _M68K_BOOTINFO_H */
