/*
** asm/bootinfo.h -- Definition of the Linux/68K boot information structure
**
** Copyright 1992 by Greg Harp
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file README.legal in the main directory of this archive
** for more details.
**
** Created 09/29/92 by Greg Harp
**
** 5/2/94 Roman Hodek:
**   Added bi_atari part of the machine dependent union bi_un; for now it
**	 contains just a model field to distinguish between TT and Falcon.
*/

#ifndef BOOTINFO_H
#define BOOTINFO_H

#ifndef __ASSEMBLY__

#include <asm/zorro.h>

/*
 * Amiga specific part of bootinfo structure.
 */

#define NUM_AUTO    16

#define AMIGAHW_DECLARE(name)	unsigned name : 1
#define AMIGAHW_SET(name)	(boot_info.bi_amiga.hw_present.name = 1)
#define AMIGAHW_PRESENT(name)	(boot_info.bi_amiga.hw_present.name)

struct bi_Amiga {
  int model;				/* Amiga Model (3000?) */
  int num_autocon;			/* # of autoconfig devices found */
  struct ConfigDev autocon[NUM_AUTO];	/* up to 16 autoconfig devices */
  unsigned long chip_size;		/* size of chip memory (bytes) */
  unsigned char vblank; 		/* VBLANK frequency */
  unsigned char psfreq; 		/* power supply frequency */
  unsigned long eclock; 		/* EClock frequency */
  unsigned long chipset;		/* native chipset present */
  struct {
    /* video hardware */
    AMIGAHW_DECLARE(AMI_VIDEO);		/* Amiga Video */
    AMIGAHW_DECLARE(AMI_BLITTER);	/* Amiga Blitter */
    AMIGAHW_DECLARE(AMBER_FF);		/* Amber Flicker Fixer */
    /* sound hardware */
    AMIGAHW_DECLARE(AMI_AUDIO);		/* Amiga Audio */
    /* disk storage interfaces */
    AMIGAHW_DECLARE(AMI_FLOPPY);	/* Amiga Floppy */
    AMIGAHW_DECLARE(A3000_SCSI);	/* SCSI (wd33c93, A3000 alike) */
    AMIGAHW_DECLARE(A4000_SCSI);	/* SCSI (ncr53c710, A4000T alike) */
    AMIGAHW_DECLARE(A1200_IDE);		/* IDE (A1200 alike) */
    AMIGAHW_DECLARE(A4000_IDE);		/* IDE (A4000 alike) */
    AMIGAHW_DECLARE(CD_ROM);		/* CD ROM drive */
    /* other I/O hardware */
    AMIGAHW_DECLARE(AMI_KEYBOARD);	/* Amiga Keyboard */
    AMIGAHW_DECLARE(AMI_MOUSE);		/* Amiga Mouse */
    AMIGAHW_DECLARE(AMI_SERIAL);	/* Amiga Serial */
    AMIGAHW_DECLARE(AMI_PARALLEL);	/* Amiga Parallel */
    /* real time clocks */
    AMIGAHW_DECLARE(A2000_CLK);		/* Hardware Clock (A2000 alike) */
    AMIGAHW_DECLARE(A3000_CLK);		/* Hardware Clock (A3000 alike) */
    /* supporting hardware */
    AMIGAHW_DECLARE(CHIP_RAM);		/* Chip RAM */
    AMIGAHW_DECLARE(PAULA);		/* Paula (8364) */
    AMIGAHW_DECLARE(DENISE);		/* Denise (8362) */
    AMIGAHW_DECLARE(DENISE_HR);		/* Denise (8373) */
    AMIGAHW_DECLARE(LISA);		/* Lisa (8375) */
    AMIGAHW_DECLARE(AGNUS_PAL);		/* Normal/Fat PAL Agnus (8367/8371) */
    AMIGAHW_DECLARE(AGNUS_NTSC);	/* Normal/Fat NTSC Agnus (8361/8370) */
    AMIGAHW_DECLARE(AGNUS_HR_PAL);	/* Fat Hires PAL Agnus (8372) */
    AMIGAHW_DECLARE(AGNUS_HR_NTSC);	/* Fat Hires NTSC Agnus (8372) */
    AMIGAHW_DECLARE(ALICE_PAL);		/* PAL Alice (8374) */
    AMIGAHW_DECLARE(ALICE_NTSC);	/* NTSC Alice (8374) */
    AMIGAHW_DECLARE(MAGIC_REKICK);	/* A3000 Magic Hard Rekick */
    AMIGAHW_DECLARE(ZORRO);		/* Zorro AutoConfig */
  } hw_present;
};


/* Atari specific part of bootinfo */

/*
 * Define several Hardware-Chips for indication so that for the ATARI we do
 * no longer decide whether it is a Falcon or other machine . It's just
 * important what hardware the machine uses
 */

/* ++roman 08/08/95: rewritten from ORing constants to a C bitfield */

#define ATARIHW_DECLARE(name)	unsigned name : 1
#define ATARIHW_SET(name)	(boot_info.bi_atari.hw_present.name = 1)
#define ATARIHW_PRESENT(name)	(boot_info.bi_atari.hw_present.name)

struct bi_Atari {
  struct {
    /* video hardware */
    ATARIHW_DECLARE(STND_SHIFTER);	/* ST-Shifter - no base low ! */
    ATARIHW_DECLARE(EXTD_SHIFTER);	/* STe-Shifter - 24 bit address */
    ATARIHW_DECLARE(TT_SHIFTER);	/* TT-Shifter */
    ATARIHW_DECLARE(VIDEL_SHIFTER);	/* Falcon-Shifter */
    /* sound hardware */
    ATARIHW_DECLARE(YM_2149);		/* Yamaha YM 2149 */
    ATARIHW_DECLARE(PCM_8BIT);		/* PCM-Sound in STe-ATARI */
    ATARIHW_DECLARE(CODEC);		/* CODEC Sound (Falcon) */
    /* disk storage interfaces */
    ATARIHW_DECLARE(TT_SCSI);		/* Directly mapped NCR5380 */
    ATARIHW_DECLARE(ST_SCSI);		/* NCR5380 via ST-DMA (Falcon) */
    ATARIHW_DECLARE(ACSI);		/* Standard ACSI like in STs */
    ATARIHW_DECLARE(IDE);		/* IDE Interface */
    ATARIHW_DECLARE(FDCSPEED);		/* 8/16 MHz switch for FDC */
    /* other I/O hardware */
    ATARIHW_DECLARE(ST_MFP);		/* The ST-MFP (there should
					   be no Atari without
					   it... but who knows?) */
    ATARIHW_DECLARE(TT_MFP);		/* 2nd MFP */
    ATARIHW_DECLARE(SCC);		/* Serial Communications Contr. */
    ATARIHW_DECLARE(ST_ESCC);		/* SCC Z83230 in an ST */
    ATARIHW_DECLARE(ANALOG_JOY);	/* Paddle Interface for STe
					   and Falcon */
    ATARIHW_DECLARE(MICROWIRE);		/* Microwire Interface */
    /* DMA */
    ATARIHW_DECLARE(STND_DMA);		/* 24 Bit limited ST-DMA */
    ATARIHW_DECLARE(EXTD_DMA);		/* 32 Bit ST-DMA */
    ATARIHW_DECLARE(SCSI_DMA);		/* DMA for the NCR5380 */
    ATARIHW_DECLARE(SCC_DMA);		/* DMA for the SCC */
    /* real time clocks */
    ATARIHW_DECLARE(TT_CLK);		/* TT compatible clock chip */
    ATARIHW_DECLARE(MSTE_CLK);		/* Mega ST(E) clock chip */
    /* supporting hardware */
    ATARIHW_DECLARE(SCU);		/* System Control Unit */
    ATARIHW_DECLARE(BLITTER);		/* Blitter */
    ATARIHW_DECLARE(VME);		/* VME Bus */
  } hw_present;
  unsigned long mch_cookie;		/* _MCH cookie from TOS */
};

/* mch_cookie values (upper word) */
#define	ATARI_MCH_ST		0
#define	ATARI_MCH_STE		1
#define	ATARI_MCH_TT		2
#define	ATARI_MCH_FALCON	3

/*
 * CPU and FPU types
 */
#define CPU_68020    (1)
#define CPU_68030    (2)
#define CPU_68040    (4)
#define CPU_68060    (8)
#define CPU_MASK     (31)
#define FPU_68881    (32)
#define FPU_68882    (64)
#define FPU_68040    (128)	/* Internal FPU */
#define FPU_68060    (256)	/* Internal FPU */

struct mem_info {
  unsigned long addr;		/* physical address of memory chunk */
  unsigned long size;		/* length of memory chunk (in bytes) */
};

#define NUM_MEMINFO  4

#endif /* __ASSEMBLY__ */

#define MACH_AMIGA   1
#define MACH_ATARI   2
#define MACH_MAC     3

#ifndef __ASSEMBLY__

#define MACH_IS_AMIGA	(boot_info.machtype == MACH_AMIGA)
#define MACH_IS_ATARI	(boot_info.machtype == MACH_ATARI)

#define CL_SIZE      (256)

struct bootinfo {
  unsigned long machtype;		/* machine type */
  unsigned long cputype;		/* system CPU & FPU */
  struct mem_info memory[NUM_MEMINFO];	/* memory description */
  int num_memory;			/* # of memory blocks found */
  unsigned long ramdisk_size;		/* ramdisk size in 1024 byte blocks */
  unsigned long ramdisk_addr;		/* address of the ram disk in mem */
  char command_line[CL_SIZE];		/* kernel command line parameters */
  union {
    struct bi_Amiga bi_ami; 	/* Amiga specific information */
    struct bi_Atari bi_ata; 	/* Atari specific information */
  } bi_un;
};
#define bi_amiga bi_un.bi_ami
#define bi_atari bi_un.bi_ata
#define bi_mac	 bi_un.bi_mac

extern struct bootinfo
    boot_info;

#endif /* __ASSEMBLY__ */


/*
 * Stuff for bootinfo interface versioning
 *
 * At the start of kernel code, a 'struct bootversion' is located. bootstrap
 * checks for a matching version of the interface before booting a kernel, to
 * avoid user confusion if kernel and bootstrap don't work together :-)
 *
 * If incompatible changes are made to the bootinfo interface, the major
 * number below should be stepped (and the minor reset to 0) for the
 * appropriate machine. If a change is backward-compatible, the minor should
 * be stepped. "Backwards-compatible" means that booting will work, but
 * certain features may not.
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

#define AMIGA_BOOTI_VERSION    MK_BI_VERSION( 1, 0 )
#define ATARI_BOOTI_VERSION    MK_BI_VERSION( 1, 0 )

#endif /* BOOTINFO_H */
