/*
** asm-m68k/amigahw.h -- This header defines some macros and pointers for
**                    the various Amiga custom hardware registers.
**                    The naming conventions used here conform to those
**                    used in the Amiga Hardware Reference Manual, 3rd Edition
**
** Copyright 1992 by Greg Harp
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created: 9/24/92 by Greg Harp
*/

#ifndef _M68K_AMIGAHW_H
#define _M68K_AMIGAHW_H

    /*
     *  Different Amiga models
     */

extern u_long amiga_model;

#define AMI_UNKNOWN	(0)
#define AMI_500		(1)
#define AMI_500PLUS	(2)
#define AMI_600		(3)
#define AMI_1000	(4)
#define AMI_1200	(5)
#define AMI_2000	(6)
#define AMI_2500	(7)
#define AMI_3000	(8)
#define AMI_3000T	(9)
#define AMI_3000PLUS	(10)
#define AMI_4000	(11)
#define AMI_4000T	(12)
#define AMI_CDTV	(13)
#define AMI_CD32	(14)
#define AMI_DRACO	(15)


    /*
     *  Chipsets
     */

extern u_long amiga_chipset;

#define CS_STONEAGE	(0)
#define CS_OCS		(1)
#define CS_ECS		(2)
#define CS_AGA		(3)


    /*
     *  Miscellaneous
     */

extern u_long amiga_eclock;		/* 700 kHz E Peripheral Clock */
extern u_long amiga_masterclock;	/* 28 MHz Master Clock */
extern u_long amiga_colorclock;		/* 3.5 MHz Color Clock */
extern u_long amiga_chip_size;		/* Chip RAM Size (bytes) */
extern u_char amiga_vblank;		/* VBLANK Frequency */
extern u_char amiga_psfreq;		/* Power Supply Frequency */


#define AMIGAHW_DECLARE(name)	unsigned name : 1
#define AMIGAHW_SET(name)	(amiga_hw_present.name = 1)
#define AMIGAHW_PRESENT(name)	(amiga_hw_present.name)

struct amiga_hw_present {
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
    AMIGAHW_DECLARE(ZORRO3);		/* Zorro III */
};

extern struct amiga_hw_present amiga_hw_present;

struct CUSTOM {
    u_short bltddat;
    u_short dmaconr;
    u_short vposr;
    u_short vhposr;
    u_short dskdatr;
    u_short joy0dat;
    u_short joy1dat;
    u_short clxdat;
    u_short adkconr;
    u_short pot0dat;
    u_short pot1dat;
    u_short potgor;
    u_short serdatr;
    u_short dskbytr;
    u_short intenar;
    u_short intreqr;
    u_char  *dskptr;
    u_short dsklen;
    u_short dskdat;
    u_short refptr;
    u_short vposw;
    u_short vhposw;
    u_short copcon;
    u_short serdat;
    u_short serper;
    u_short potgo;
    u_short joytest;
    u_short strequ;
    u_short strvbl;
    u_short strhor;
    u_short strlong;
    u_short bltcon0;
    u_short bltcon1;
    u_short bltafwm;
    u_short bltalwm;
    u_char  *bltcpt;
    u_char  *bltbpt;
    u_char  *bltapt;
    u_char  *bltdpt;
    u_short bltsize;
    u_char  pad2d;
    u_char  bltcon0l;
    u_short bltsizv;
    u_short bltsizh;
    u_short bltcmod;
    u_short bltbmod;
    u_short bltamod;
    u_short bltdmod;
    u_short spare2[4];
    u_short bltcdat;
    u_short bltbdat;
    u_short bltadat;
    u_short spare3[3];
    u_short deniseid;
    u_short dsksync;
    u_short *cop1lc;
    u_short *cop2lc;
    u_short copjmp1;
    u_short copjmp2;
    u_short copins;
    u_short diwstrt;
    u_short diwstop;
    u_short ddfstrt;
    u_short ddfstop;
    u_short dmacon;
    u_short clxcon;
    u_short intena;
    u_short intreq;
    u_short adkcon;
    struct {
	u_short	*audlc;
	u_short audlen;
	u_short audper;
	u_short audvol;
	u_short auddat;
	u_short audspare[2];
    } aud[4];
    u_char  *bplpt[8];
    u_short bplcon0;
    u_short bplcon1;
    u_short bplcon2;
    u_short bplcon3;
    u_short bpl1mod;
    u_short bpl2mod;
    u_short bplcon4;
    u_short clxcon2;
    u_short bpldat[8];
    u_char  *sprpt[8];
    struct {
	u_short pos;
	u_short ctl;
	u_short dataa;
	u_short datab;
    } spr[8];
    u_short color[32];
    u_short htotal;
    u_short hsstop;
    u_short hbstrt;
    u_short hbstop;
    u_short vtotal;
    u_short vsstop;
    u_short vbstrt;
    u_short vbstop;
    u_short sprhstrt;
    u_short sprhstop;
    u_short bplhstrt;
    u_short bplhstop;
    u_short hhposw;
    u_short hhposr;
    u_short beamcon0;
    u_short hsstrt;
    u_short vsstrt;
    u_short hcenter;
    u_short diwhigh;
    u_short spare4[11];
    u_short fmode;
};

/*
 * DMA register bits
 */
#define DMAF_SETCLR		(0x8000)
#define DMAF_AUD0		(0x0001)
#define DMAF_AUD1		(0x0002)
#define DMAF_AUD2		(0x0004)
#define DMAF_AUD3		(0x0008)
#define DMAF_DISK		(0x0010)
#define DMAF_SPRITE		(0x0020)
#define DMAF_BLITTER		(0x0040)
#define DMAF_COPPER		(0x0080)
#define DMAF_RASTER		(0x0100)
#define DMAF_MASTER		(0x0200)
#define DMAF_BLITHOG		(0x0400)
#define DMAF_BLTNZERO		(0x2000)
#define DMAF_BLTDONE		(0x4000)
#define DMAF_ALL		(0x01FF)

struct CIA {
    u_char pra; 		char pad0[0xff];
    u_char prb; 		char pad1[0xff];
    u_char ddra;		char pad2[0xff];
    u_char ddrb;		char pad3[0xff];
    u_char talo;		char pad4[0xff];
    u_char tahi;		char pad5[0xff];
    u_char tblo;		char pad6[0xff];
    u_char tbhi;		char pad7[0xff];
    u_char todlo;		char pad8[0xff];
    u_char todmid;		char pad9[0xff];
    u_char todhi;		char pada[0x1ff];
    u_char sdr; 		char padb[0xff];
    u_char icr; 		char padc[0xff];
    u_char cra; 		char padd[0xff];
    u_char crb; 		char pade[0xff];
};

#define zTwoBase (0x80000000)
#define ZTWO_PADDR(x) (((unsigned long)(x))-zTwoBase)
#define ZTWO_VADDR(x) (((unsigned long)(x))+zTwoBase)

#define CUSTOM_PHYSADDR     (0xdff000)
#define custom ((*(volatile struct CUSTOM *)(zTwoBase+CUSTOM_PHYSADDR)))

#define CIAA_PHYSADDR	  (0xbfe001)
#define CIAB_PHYSADDR	  (0xbfd000)
#define ciaa   ((*(volatile struct CIA *)(zTwoBase + CIAA_PHYSADDR)))
#define ciab   ((*(volatile struct CIA *)(zTwoBase + CIAB_PHYSADDR)))

#define CHIP_PHYSADDR	    (0x000000)
#define chipaddr ((unsigned long)(zTwoBase + CHIP_PHYSADDR))
void amiga_chip_init (void);
void *amiga_chip_alloc (long size);
void amiga_chip_free (void *);
unsigned long amiga_chip_avail( void ); /*MILAN*/

struct tod3000 {
  unsigned int  :28, second2:4;	/* lower digit */
  unsigned int  :28, second1:4;	/* upper digit */
  unsigned int  :28, minute2:4;	/* lower digit */
  unsigned int  :28, minute1:4;	/* upper digit */
  unsigned int  :28, hour2:4;	/* lower digit */
  unsigned int  :28, hour1:4;	/* upper digit */
  unsigned int  :28, weekday:4;
  unsigned int  :28, day2:4;	/* lower digit */
  unsigned int  :28, day1:4;	/* upper digit */
  unsigned int  :28, month2:4;	/* lower digit */
  unsigned int  :28, month1:4;	/* upper digit */
  unsigned int  :28, year2:4;	/* lower digit */
  unsigned int  :28, year1:4;	/* upper digit */
  unsigned int  :28, cntrl1:4;	/* control-byte 1 */
  unsigned int  :28, cntrl2:4;	/* control-byte 2 */  
  unsigned int  :28, cntrl3:4;	/* control-byte 3 */
};
#define TOD3000_CNTRL1_HOLD	0
#define TOD3000_CNTRL1_FREE	9
#define TOD_3000 ((struct tod3000 *)(zTwoBase+0xDC0000))

struct tod2000 {
  unsigned int  :28, second2:4;	/* lower digit */
  unsigned int  :28, second1:4;	/* upper digit */
  unsigned int  :28, minute2:4;	/* lower digit */
  unsigned int  :28, minute1:4;	/* upper digit */
  unsigned int  :28, hour2:4;	/* lower digit */
  unsigned int  :28, hour1:4;	/* upper digit */
  unsigned int  :28, day2:4;	/* lower digit */
  unsigned int  :28, day1:4;	/* upper digit */
  unsigned int  :28, month2:4;	/* lower digit */
  unsigned int  :28, month1:4;	/* upper digit */
  unsigned int  :28, year2:4;	/* lower digit */
  unsigned int  :28, year1:4;	/* upper digit */
  unsigned int  :28, weekday:4;
  unsigned int  :28, cntrl1:4;	/* control-byte 1 */
  unsigned int  :28, cntrl2:4;	/* control-byte 2 */  
  unsigned int  :28, cntrl3:4;	/* control-byte 3 */
};

#define TOD2000_CNTRL1_HOLD	(1<<0)
#define TOD2000_CNTRL1_BUSY	(1<<1)
#define TOD2000_CNTRL3_24HMODE	(1<<2)
#define TOD2000_HOUR1_PM	(1<<2)
#define TOD_2000 ((struct tod2000 *)(zTwoBase+0xDC0000))

#endif /* __ASMm68k_AMIGAHW_H */
