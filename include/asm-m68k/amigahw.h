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

#ifndef _ASMm68k_AMIGAHW_H_
#define _ASMm68k_AMIGAHW_H_

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

#if 1
#define zTwoBase (0x80000000)
#define ZTWO_PADDR(x) (((unsigned long)(x))-zTwoBase)
#define ZTWO_VADDR(x) (((unsigned long)(x))+zTwoBase)
#else
#define zTwoBase 0
#define ZTWO_PADDR(x) (x)
#define ZTWO_VADDR(x) (x)
#endif

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

#endif /* asm-m68k/amigahw.h */
