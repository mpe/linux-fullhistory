/*
 * linux/arch/m68k/amiga/amifb.c -- Low level implementation of the Amiga frame
 *                                  buffer device
 *
 *    Copyright (C) 1995 Geert Uytterhoeven
 *
 *          with work by Roman Zippel
 *
 *
 * This file is based on the Atari frame buffer device (atafb.c):
 *
 *    Copyright (C) 1994 Martin Schaller
 *                       Roman Hodek
 *
 *          with work by Andreas Schwab
 *                       Guenther Kelleter
 *
 * and on the original Amiga console driver (amicon.c):
 *
 *    Copyright (C) 1993 Hamish Macdonald
 *                       Greg Harp
 *    Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *          with work by William Rucklidge (wjr@cs.cornell.edu)
 *                       Geert Uytterhoeven
 *                       Jes Sorensen (jds@kom.auc.dk)
 *
 *
 * History:
 *
 *   - 24 Jul 96: Copper generates now vblank interrupt and
 *                VESA Power Saving Protocol is fully implemented
 *   - 14 Jul 96: Rework and hopefully last ECS bugs fixed
 *   -  7 Mar 96: Hardware sprite support by Roman Zippel
 *   - 18 Feb 96: OCS and ECS support by Roman Zippel
 *                Hardware functions completely rewritten
 *   -  2 Dec 95: AGA version by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>

#define DEBUG

#if !defined(CONFIG_AMIFB_OCS) && !defined(CONFIG_AMIFB_ECS) && !defined(CONFIG_AMIFB_AGA)
#define CONFIG_AMIFB_OCS   /* define at least one fb driver, this will change later */
#endif

#if !defined(CONFIG_AMIFB_OCS)
#  define IS_OCS (0)
#elif defined(CONFIG_AMIFB_ECS) || defined(CONFIG_AMIFB_AGA)
#  define IS_OCS (chipset == TAG_OCS)
#else
#  define CONFIG_AMIFB_OCS_ONLY
#  define IS_OCS (1)
#endif

#if !defined(CONFIG_AMIFB_ECS)
#  define IS_ECS (0)
#elif defined(CONFIG_AMIFB_OCS) || defined(CONFIG_AMIFB_AGA)
#  define IS_ECS (chipset == TAG_ECS)
#else
#  define CONFIG_AMIFB_ECS_ONLY
#  define IS_ECS (1)
#endif

#if !defined(CONFIG_AMIFB_AGA)
#  define IS_AGA (0)
#elif defined(CONFIG_AMIFB_OCS) || defined(CONFIG_AMIFB_ECS)
#  define IS_AGA (chipset == TAG_AGA)
#else
#  define CONFIG_AMIFB_AGA_ONLY
#  define IS_AGA (1)
#endif

/*******************************************************************************


   Generic video timings
   ---------------------

   Timings used by the frame buffer interface:

   +----------+---------------------------------------------+----------+-------+
   |          |                ^                            |          |       |
   |          |                |upper_margin                |          |       |
   |          |                �                            |          |       |
   +----------###############################################----------+-------+
   |          #                ^                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |   left   #                |                            #  right   | hsync |
   |  margin  #                |       xres                 #  margin  |  len  |
   |<-------->#<---------------+--------------------------->#<-------->|<----->|
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |yres                        #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                |                            #          |       |
   |          #                �                            #          |       |
   +----------###############################################----------+-------+
   |          |                ^                            |          |       |
   |          |                |lower_margin                |          |       |
   |          |                �                            |          |       |
   +----------+---------------------------------------------+----------+-------+
   |          |                ^                            |          |       |
   |          |                |vsync_len                   |          |       |
   |          |                �                            |          |       |
   +----------+---------------------------------------------+----------+-------+


   Amiga video timings
   -------------------

   The Amiga native chipsets uses another timing scheme:

      - hsstrt:   Start of horizontal synchronization pulse
      - hsstop:   End of horizontal synchronization pulse
      - htotal:   Last value on the line (i.e. line length = htotal+1)
      - vsstrt:   Start of vertical synchronization pulse
      - vsstop:   End of vertical synchronization pulse
      - vtotal:   Last line value (i.e. number of lines = vtotal+1)
      - hcenter:  Start of vertical retrace for interlace

   You can specify the blanking timings independently. Currently I just set
   them equal to the respective synchronization values:

      - hbstrt:   Start of horizontal blank
      - hbstop:   End of horizontal blank
      - vbstrt:   Start of vertical blank
      - vbstop:   End of vertical blank

   Horizontal values are in color clock cycles (280 ns), vertical values are in
   scanlines.

   (0, 0) is somewhere in the upper-left corner :-)


   Amiga visible window definitions
   --------------------------------

   Currently I only have values for AGA, SHRES (28 MHz dotclock). Feel free to
   make corrections and/or additions.

   Within the above synchronization specifications, the visible window is
   defined by the following parameters (actual register resolutions may be
   different; all horizontal values are normalized with respect to the pixel
   clock):

      - diwstrt_h:   Horizontal start of the visible window
      - diwstop_h:   Horizontal stop+1(*) of the visible window
      - diwstrt_v:   Vertical start of the visible window
      - diwstop_v:   Vertical stop of the visible window
      - ddfstrt:     Horizontal start of display DMA
      - ddfstop:     Horizontal stop of display DMA
      - hscroll:     Horizontal display output delay

   Sprite positioning:

      - sprstrt_h:   Horizontal start-4 of sprite
      - sprstrt_v:   Vertical start of sprite

   (*) Even Commodore did it wrong in the AGA monitor drivers by not adding 1.

   Horizontal values are in dotclock cycles (35 ns), vertical values are in
   scanlines.

   (0, 0) is somewhere in the upper-left corner :-)


   Dependencies (AGA, SHRES (35 ns dotclock))
   -------------------------------------------

   Since there are much more parameters for the Amiga display than for the
   frame buffer interface, there must be some dependencies among the Amiga
   display parameters. Here's what I found out:

      - ddfstrt and ddfstop are best aligned to 64 pixels.
      - the chipset needs 64+4 horizontal pixels after the DMA start before the
        first pixel is output, so diwstrt_h = ddfstrt+64+4 if you want to
        display the first pixel on the line too. Increase diwstrt_h for virtual
        screen panning.
      - the display DMA always fetches 64 pixels at a time (fmode = 3).
      - ddfstop is ddfstrt+#pixels-64.
      - diwstop_h = diwstrt_h+xres+1. Because of the additional 1 this can be 1
        more than htotal.
      - hscroll simply adds a delay to the display output. Smooth horizontal
        panning needs an extra 64 pixels on the left to prefetch the pixels that
        `fall off' on the left.
      - if ddfstrt < 192, the sprite DMA cycles are all stolen by the bitplane
        DMA, so it's best to make the DMA start as late as possible.
      - you really don't want to make ddfstrt < 128, since this will steal DMA
        cycles from the other DMA channels (audio, floppy and Chip RAM refresh).
      - I make diwstop_h and diwstop_v as large as possible.

   General dependencies
   --------------------

      - all values are SHRES pixel (35ns)

                  table 1:fetchstart  table 2:prefetch    table 3:fetchsize
                  ------------------  ----------------    -----------------
   Pixclock     # SHRES|HIRES|LORES # SHRES|HIRES|LORES # SHRES|HIRES|LORES
   -------------#------+-----+------#------+-----+------#------+-----+------
   Bus width 1x #   16 |  32 |  64  #   16 |  32 |  64  #   64 |  64 |  64
   Bus width 2x #   32 |  64 | 128  #   32 |  64 |  64  #   64 |  64 | 128
   Bus width 4x #   64 | 128 | 256  #   64 |  64 |  64  #   64 | 128 | 256

      - chipset needs 4 pixels before the first pixel is output
      - ddfstrt must be aligned to fetchstart (table 1)
      - chipset needs also prefetch (table 2) to get first pixel data, so
        ddfstrt = ((diwstrt_h-4) & -fetchstart) - prefetch
      - for horizontal panning decrease diwstrt_h
      - the length of a fetchline must be aligned to fetchsize (table 3)
      - if fetchstart is smaller than fetchsize, then ddfstrt can a little bit
        moved to optimize use of dma (usefull for OCS/ECS overscan displays)
      - ddfstop is ddfstrt+ddfsize-fetchsize
      - If C= didn't change anything for AGA, then at following positions the
        dma bus is allready used:
        ddfstrt <  48 -> memory refresh
                <  96 -> disk dma
                < 160 -> audio dma
                < 192 -> sprite 0 dma
                < 416 -> sprite dma (32 per sprite)
      - in accordance with the hardware reference manual a hardware stop is at
        192, but AGA (ECS?) can go below this.

   DMA priorities
   --------------

   Since there are limits on the earliest start value for display DMA and the
   display of sprites, I use the following policy on horizontal panning and
   the hardware cursor:

      - if you want to start display DMA too early, you loose the ability to
        do smooth horizontal panning (xpanstep 1 -> 64).
      - if you want to go even further, you loose the hardware cursor too.

   IMHO a hardware cursor is more important for X than horizontal scrolling,
   so that's my motivation.


   Implementation
   --------------

   ami_decode_var() converts the frame buffer values to the Amiga values. It's
   just a `straightforward' implementation of the above rules.


   Standard VGA timings
   --------------------

               xres  yres    left  right  upper  lower    hsync    vsync
               ----  ----    ----  -----  -----  -----    -----    -----
      80x25     720   400      27     45     35     12      108        2
      80x30     720   480      27     45     30      9      108        2

   These were taken from a XFree86 configuration file, recalculated for a 28 MHz
   dotclock (Amigas don't have a 25 MHz dotclock) and converted to frame buffer
   generic timings.

   As a comparison, graphics/monitor.h suggests the following:

               xres  yres    left  right  upper  lower    hsync    vsync
               ----  ----    ----  -----  -----  -----    -----    -----

      VGA       640   480      52    112     24     19    112 -      2 +
      VGA70     640   400      52    112     27     21    112 -      2 -


   Sync polarities
   ---------------

      VSYNC    HSYNC    Vertical size    Vertical total
      -----    -----    -------------    --------------
        +        +           Reserved          Reserved
        +        -                400               414
        -        +                350               362
        -        -                480               496

   Source: CL-GD542X Technical Reference Manual, Cirrus Logic, Oct 1992


   Broadcast video timings
   -----------------------

   According to the CCIR and RETMA specifications, we have the following values:

   CCIR -> PAL
   -----------

      - a scanline is 64 탎 long, of which 52.48 탎 are visible. This is about
        736 visible 70 ns pixels per line.
      - we have 625 scanlines, of which 575 are visible (interlaced); after
        rounding this becomes 576.

   RETMA -> NTSC
   -------------

      - a scanline is 63.5 탎 long, of which 53.5 탎 are visible.  This is about
        736 visible 70 ns pixels per line.
      - we have 525 scanlines, of which 485 are visible (interlaced); after
        rounding this becomes 484.

   Thus if you want a PAL compatible display, you have to do the following:

      - set the FB_SYNC_BROADCAST flag to indicate that standard broadcast
        timings are to be used.
      - make sure upper_margin+yres+lower_margin+vsync_len = 625 for an
        interlaced, 312 for a non-interlaced and 156 for a doublescanned
        display.
      - make sure left_margin+xres+right_margin+hsync_len = 1816 for a SHRES,
        908 for a HIRES and 454 for a LORES display.
      - the left visible part begins at 360 (SHRES; HIRES:180, LORES:90),
        left_margin+2*hsync_len must be greater or equal.
      - the upper visible part begins at 48 (interlaced; non-interlaced:24,
        doublescanned:12), upper_margin+2*vsync_len must be greater or equal.
      - ami_encode_var() calculates margins with a hsync of 5320 ns and a vsync
        of 4 scanlines

   The settings for a NTSC compatible display are straightforward.

   Note that in a strict sense the PAL and NTSC standards only define the
   encoding of the color part (chrominance) of the video signal and don't say
   anything about horizontal/vertical synchronization nor refresh rates.


                                                            -- Geert --

*******************************************************************************/


	/*
	 * Custom Chipset Definitions
	 */

#define CUSTOM_OFS(fld) ((long)&((struct CUSTOM*)0)->fld)

	/*
	 * BPLCON0 -- Bitplane Control Register 0
	 */

#define BPC0_HIRES	(0x8000)
#define BPC0_BPU2	(0x4000) /* Bit plane used count */
#define BPC0_BPU1	(0x2000)
#define BPC0_BPU0	(0x1000)
#define BPC0_HAM	(0x0800) /* HAM mode */
#define BPC0_DPF	(0x0400) /* Double playfield */
#define BPC0_COLOR	(0x0200) /* Enable colorburst */
#define BPC0_GAUD	(0x0100) /* Genlock audio enable */
#define BPC0_UHRES	(0x0080) /* Ultrahi res enable */
#define BPC0_SHRES	(0x0040) /* Super hi res mode */
#define BPC0_BYPASS	(0x0020) /* Bypass LUT - AGA */
#define BPC0_BPU3	(0x0010) /* AGA */
#define BPC0_LPEN	(0x0008) /* Light pen enable */
#define BPC0_LACE	(0x0004) /* Interlace */
#define BPC0_ERSY	(0x0002) /* External resync */
#define BPC0_ECSENA	(0x0001) /* ECS enable */

	/*
	 * BPLCON2 -- Bitplane Control Register 2
	 */

#define BPC2_ZDBPSEL2	(0x4000) /* Bitplane to be used for ZD - AGA */
#define BPC2_ZDBPSEL1	(0x2000)
#define BPC2_ZDBPSEL0	(0x1000)
#define BPC2_ZDBPEN	(0x0800) /* Enable ZD with ZDBPSELx - AGA */
#define BPC2_ZDCTEN	(0x0400) /* Enable ZD with palette bit #31 - AGA */
#define BPC2_KILLEHB	(0x0200) /* Kill EHB mode - AGA */
#define BPC2_RDRAM	(0x0100) /* Color table accesses read, not write - AGA */
#define BPC2_SOGEN	(0x0080) /* SOG output pin high - AGA */
#define BPC2_PF2PRI	(0x0040) /* PF2 priority over PF1 */
#define BPC2_PF2P2	(0x0020) /* PF2 priority wrt sprites */
#define BPC2_PF2P1	(0x0010)
#define BPC2_PF2P0	(0x0008)
#define BPC2_PF1P2	(0x0004) /* ditto PF1 */
#define BPC2_PF1P1	(0x0002)
#define BPC2_PF1P0	(0x0001)

	/*
	 * BPLCON3 -- Bitplane Control Register 3 (AGA)
	 */

#define BPC3_BANK2	(0x8000) /* Bits to select color register bank */
#define BPC3_BANK1	(0x4000)
#define BPC3_BANK0	(0x2000)
#define BPC3_PF2OF2	(0x1000) /* Bits for color table offset when PF2 */
#define BPC3_PF2OF1	(0x0800)
#define BPC3_PF2OF0	(0x0400)
#define BPC3_LOCT	(0x0200) /* Color register writes go to low bits */
#define BPC3_SPRES1	(0x0080) /* Sprite resolution bits */
#define BPC3_SPRES0	(0x0040)
#define BPC3_BRDRBLNK	(0x0020) /* Border blanked? */
#define BPC3_BRDRTRAN	(0x0010) /* Border transparent? */
#define BPC3_ZDCLKEN	(0x0004) /* ZD pin is 14 MHz (HIRES) clock output */
#define BPC3_BRDRSPRT	(0x0002) /* Sprites in border? */
#define BPC3_EXTBLKEN	(0x0001) /* BLANK programmable */

	/*
	 * BPLCON4 -- Bitplane Control Register 4 (AGA)
	 */

#define BPC4_BPLAM7	(0x8000) /* bitplane color XOR field */
#define BPC4_BPLAM6	(0x4000)
#define BPC4_BPLAM5	(0x2000)
#define BPC4_BPLAM4	(0x1000)
#define BPC4_BPLAM3	(0x0800)
#define BPC4_BPLAM2	(0x0400)
#define BPC4_BPLAM1	(0x0200)
#define BPC4_BPLAM0	(0x0100)
#define BPC4_ESPRM7	(0x0080) /* 4 high bits for even sprite colors */
#define BPC4_ESPRM6	(0x0040)
#define BPC4_ESPRM5	(0x0020)
#define BPC4_ESPRM4	(0x0010)
#define BPC4_OSPRM7	(0x0008) /* 4 high bits for odd sprite colors */
#define BPC4_OSPRM6	(0x0004)
#define BPC4_OSPRM5	(0x0002)
#define BPC4_OSPRM4	(0x0001)

	/*
	 * BEAMCON0 -- Beam Control Register
	 */

#define BMC0_HARDDIS	(0x4000) /* Disable hardware limits */
#define BMC0_LPENDIS	(0x2000) /* Disable light pen latch */
#define BMC0_VARVBEN	(0x1000) /* Enable variable vertical blank */
#define BMC0_LOLDIS	(0x0800) /* Disable long/short line toggle */
#define BMC0_CSCBEN	(0x0400) /* Composite sync/blank */
#define BMC0_VARVSYEN	(0x0200) /* Enable variable vertical sync */
#define BMC0_VARHSYEN	(0x0100) /* Enable variable horizontal sync */
#define BMC0_VARBEAMEN	(0x0080) /* Enable variable beam counters */
#define BMC0_DUAL	(0x0040) /* Enable alternate horizontal beam counter */
#define BMC0_PAL	(0x0020) /* Set decodes for PAL */
#define BMC0_VARCSYEN	(0x0010) /* Enable variable composite sync */
#define BMC0_BLANKEN	(0x0008) /* Blank enable (no longer used on AGA) */
#define BMC0_CSYTRUE	(0x0004) /* CSY polarity */
#define BMC0_VSYTRUE	(0x0002) /* VSY polarity */
#define BMC0_HSYTRUE	(0x0001) /* HSY polarity */


	/*
	 * FMODE -- Fetch Mode Control Register (AGA)
	 */

#define FMODE_SSCAN2	(0x8000) /* Sprite scan-doubling */
#define FMODE_BSCAN2	(0x4000) /* Use PF2 modulus every other line */
#define FMODE_SPAGEM	(0x0008) /* Sprite page mode */
#define FMODE_SPR32	(0x0004) /* Sprite 32 bit fetch */
#define FMODE_BPAGEM	(0x0002) /* Bitplane page mode */
#define FMODE_BPL32	(0x0001) /* Bitplane 32 bit fetch */

	/*
	 * Tags used to indicate a specific Pixel Clock
	 *
	 * clk_shift is the shift value to get the timings in 35 ns units
	 */

enum { TAG_SHRES, TAG_HIRES, TAG_LORES };

	/*
	 * Tags used to indicate the specific chipset
	 */

enum { TAG_OCS, TAG_ECS, TAG_AGA };

	/*
	 * Tags used to indicate the memory bandwidth
	 */

enum { TAG_FMODE_1, TAG_FMODE_2, TAG_FMODE_4 };


	/*
	 * Clock Definitions, Maximum Display Depth
	 *
	 * These depend on the E-Clock or the Chipset, so they are filled in
	 * dynamically
	 */

static u_long pixclock[3];	/* SHRES/HIRES/LORES: index = clk_shift */
static u_short maxdepth[3];	/* SHRES/HIRES/LORES: index = clk_shift */
static u_short maxfmode, chipset;


	/*
	 * Broadcast Video Timings
	 *
	 * Horizontal values are in 35 ns (SHRES) units
	 * Vertical values are in interlaced scanlines
	 */

#define PAL_DIWSTRT_H	(360)	/* PAL Window Limits */
#define PAL_DIWSTRT_V	(48)
#define PAL_HTOTAL	(1816)
#define PAL_VTOTAL	(625)

#define NTSC_DIWSTRT_H	(360)	/* NTSC Window Limits */
#define NTSC_DIWSTRT_V	(40)
#define NTSC_HTOTAL	(1816)
#define NTSC_VTOTAL	(525)


	/*
	 * Monitor Specifications
	 *
	 * These are typical for a `generic' Amiga monitor (e.g. A1960)
	 */

static long vfmin = 50, vfmax = 90, hfmin = 15000, hfmax = 38000;


	/*
	 * Various macros
	 */

#define up2(v)		(((v)+1) & -2)
#define down2(v)	((v) & -2)
#define div2(v)		((v)>>1)
#define mod2(v)		((v) & 1)

#define up4(v)		(((v)+3) & -4)
#define down4(v)	((v) & -4)
#define mul4(v)		((v)<<2)
#define div4(v)		((v)>>2)
#define mod4(v)		((v) & 3)

#define up8(v)		(((v)+7) & -8)
#define down8(v)	((v) & -8)
#define div8(v)		((v)>>3)
#define mod8(v)		((v) & 7)

#define up16(v)		(((v)+15) & -16)
#define down16(v)	((v) & -16)
#define div16(v)	((v)>>4)
#define mod16(v)	((v) & 15)

#define up32(v)		(((v)+31) & -32)
#define down32(v)	((v) & -32)
#define div32(v)	((v)>>5)
#define mod32(v)	((v) & 31)

#define up64(v)		(((v)+63) & -64)
#define down64(v)	((v) & -64)
#define div64(v)	((v)>>6)
#define mod64(v)	((v) & 63)

#define upx(x,v)	(((v)+(x)-1) & -(x))
#define downx(x,v)	((v) & -(x))
#define modx(x,v)	((v) & ((x)-1))

/* if x1 is not a constant, this macro won't make real sense :-) */
#define DIVUL(x1, x2) ({int res; asm("divul %1,%2,%3": "=d" (res): \
	"d" (x2), "d" ((long)((x1)/0x100000000ULL)), "0" ((long)(x1))); res;})

#define min(a, b)	((a) < (b) ? (a) : (b))
#define max(a, b)	((a) > (b) ? (a) : (b))

#define highw(x)	((u_long)(x)>>16 & 0xffff)
#define loww(x)		((u_long)(x) & 0xffff)

#define arraysize(x)	(sizeof(x)/sizeof(*(x)))

#define VBlankOn()	custom.intena = IF_SETCLR|IF_COPER
#define VBlankOff()	custom.intena = IF_COPER


	/*
	 * Chip RAM we reserve for the Frame Buffer
	 *
	 * This defines the Maximum Virtual Screen Size
	 * (Setable per kernel options?)
	 */

#define VIDEOMEMSIZE_AGA_2M	(1310720) /* AGA (2MB) : max 1280*1024*256  */
#define VIDEOMEMSIZE_AGA_1M	(786432)  /* AGA (1MB) : max 1024*768*256   */
#define VIDEOMEMSIZE_ECS_2M	(655360)  /* ECS (2MB) : max 1280*1024*16   */
#define VIDEOMEMSIZE_ECS_1M	(393216)  /* ECS (1MB) : max 1024*768*16    */
#define VIDEOMEMSIZE_OCS	(262144)  /* OCS       : max ca. 800*600*16 */

#define SPRITEMEMSIZE		(64*64/4) /* max 64*64*4 */
#define DUMMYSPRITEMEMSIZE	(8)

#define CHIPRAM_SAFETY_LIMIT	(16384)

static u_long videomemory, spritememory;
static u_long videomemorysize;

	/*
	 * This is the earliest allowed start of fetching display data.
	 * Only if you really want no hardware cursor and audio,
	 * set this to 128, but let it better at 192
	 */

static u_long min_fstrt = 192;

#define assignchunk(name, type, ptr, size) \
{ \
	(name) = (type)(ptr); \
	ptr += size; \
}


	/*
	 * Copper Instructions
	 */

#define CMOVE(val, reg)		(CUSTOM_OFS(reg)<<16 | (val))
#define CMOVE2(val, reg)	((CUSTOM_OFS(reg)+2)<<16 | (val))
#define CWAIT(x, y)		(((y) & 0x1fe)<<23 | ((x) & 0x7f0)<<13 | 0x0001fffe)
#define CEND			(0xfffffffe)


typedef union {
	u_long l;
	u_short w[2];
} copins;

static struct copdisplay {
	copins *init;
	copins *wait;
	copins *list[2][2];
	copins *rebuild[2];
} copdisplay;

static u_short currentcop = 0;

	/*
	 * Hardware Cursor
	 */

static int cursorrate = 20;	/* Number of frames/flash toggle */
static u_short cursorstate = -1;
static u_short cursormode = FB_CURSOR_OFF;

static u_short *lofsprite, *shfsprite, *dummysprite;

	/*
	 * Current Video Mode
	 */

static struct amiga_fb_par {

	/* General Values */

	int xres;		/* vmode */
	int yres;		/* vmode */
	int vxres;		/* vmode */
	int vyres;		/* vmode */
	int xoffset;		/* vmode */
	int yoffset;		/* vmode */
	u_short bpp;		/* vmode */
	u_short clk_shift;	/* vmode */
	u_short line_shift;	/* vmode */
	int vmode;		/* vmode */
	u_short diwstrt_h;	/* vmode */
	u_short diwstop_h;	/* vmode */
	u_short diwstrt_v;	/* vmode */
	u_short diwstop_v;	/* vmode */
	u_long next_line;	/* modulo for next line */
	u_long next_plane;	/* modulo for next plane */

	/* Cursor Values */

	struct {
		short crsr_x;	/* movecursor */
		short crsr_y;	/* movecursor */
		short spot_x;
		short spot_y;
		u_short height;
		u_short width;
		u_short fmode;
	} crsr;

	/* OCS Hardware Registers */

	u_long bplpt0;		/* vmode, pan (Note: physical address) */
	u_long bplpt0wrap;	/* vmode, pan (Note: physical address) */
	u_short ddfstrt;
	u_short ddfstop;
	u_short bpl1mod;
	u_short bpl2mod;
	u_short bplcon0;	/* vmode */
	u_short bplcon1;	/* vmode */
	u_short htotal;		/* vmode */
	u_short vtotal;		/* vmode */

	/* Additional ECS Hardware Registers */

	u_short bplcon3;	/* vmode */
	u_short beamcon0;	/* vmode */
	u_short hsstrt;		/* vmode */
	u_short hsstop;		/* vmode */
	u_short hbstrt;		/* vmode */
	u_short hbstop;		/* vmode */
	u_short vsstrt;		/* vmode */
	u_short vsstop;		/* vmode */
	u_short vbstrt;		/* vmode */
	u_short vbstop;		/* vmode */
	u_short hcenter;	/* vmode */

	/* Additional AGA Hardware Registers */

	u_short fmode;		/* vmode */
} currentpar;

static int currcon = 0;

static struct display disp[MAX_NR_CONSOLES];
static struct fb_info fb_info;

static int node;		/* node of the /dev/fb?current file */

	/*
	 * The minimum period for audio depends on htotal (for OCS/ECS/AGA)
	 * (Imported from arch/m68k/amiga/amisound.c)
	 */

extern volatile u_short amiga_audio_min_period;

	/*
	 * Since we can't read the palette on OCS/ECS, and since reading one
	 * single color palette entry require 5 expensive custom chip bus accesses
	 * on AGA, we keep a copy of the current palette.
	 */

#if defined(CONFIG_AMIFB_AGA)
static struct { u_char red, green, blue, pad; } palette[256];
#else
static struct { u_char red, green, blue, pad; } palette[32];
#endif

#if defined(CONFIG_AMIFB_ECS)
static u_short ecs_palette[32];
#endif

	/*
	 * Latches for Display Changes during VBlank
	 */

static u_short do_vmode_full = 0;	/* Change the Video Mode */
static u_short do_vmode_pan = 0;	/* Update the Video Mode */
static short do_blank = 0;		/* (Un)Blank the Screen (�1) */
static u_short do_cursor = 0;		/* Move the Cursor */


	/*
	 * Various Flags
	 */

static u_short is_blanked = 0;		/* Screen is Blanked */
static u_short is_lace = 0;		/* Screen is laced */

	/*
	 * Frame Buffer Name
	 *
	 * The rest of the name is filled in during initialization
	 */

static char amiga_fb_name[16] = "Amiga ";

	/*
	 * Predefined Video Mode Names
	 *
	 * The a2024-?? modes don't work yet because there's no A2024 driver.
	 */

static char *amiga_fb_modenames[] = {

	/*
	 * Autodetect (Default) Video Mode
	 */

	"default",

	/*
	 * AmigaOS Video Modes
	 */

	"ntsc",			/* 640x200, 15 kHz, 60 Hz (NTSC) */
	"ntsc-lace",		/* 640x400, 15 kHz, 60 Hz interlaced (NTSC) */
	"pal",			/* 640x256, 15 kHz, 50 Hz (PAL) */
	"pal-lace",		/* 640x512, 15 kHz, 50 Hz interlaced (PAL) */
	"multiscan",		/* 640x480, 29 kHz, 57 Hz */
	"multiscan-lace",	/* 640x960, 29 kHz, 57 Hz interlaced */
	"a2024-10",		/* 1024x800, 10 Hz (Not yet supported) */
	"a2024-15",		/* 1024x800, 15 Hz (Not yet supported) */
	"euro36",		/* 640x200, 15 kHz, 72 Hz */
	"euro36-lace",		/* 640x400, 15 kHz, 72 Hz interlaced */
	"euro72",		/* 640x400, 29 kHz, 68 Hz */
	"euro72-lace",		/* 640x800, 29 kHz, 68 Hz interlaced */
	"super72",		/* 800x300, 23 kHz, 70 Hz */
	"super72-lace",		/* 800x600, 23 kHz, 70 Hz interlaced */
	"dblntsc",		/* 640x200, 27 kHz, 57 Hz doublescan */
	"dblntsc-ff",		/* 640x400, 27 kHz, 57 Hz */
	"dblntsc-lace",		/* 640x800, 27 kHz, 57 Hz interlaced */
	"dblpal",		/* 640x256, 27 kHz, 47 Hz doublescan */
	"dblpal-ff",		/* 640x512, 27 kHz, 47 Hz */
	"dblpal-lace",		/* 640x1024, 27 kHz, 47 Hz interlaced */

	/*
	 * VGA Video Modes
	 */

	"vga",			/* 640x480, 31 kHz, 60 Hz (VGA) */
	"vga70",		/* 640x400, 31 kHz, 70 Hz (VGA) */

	/*
	 * User Defined Video Modes: to be set after boot up using e.g. fbset
	 */

	"user0", "user1", "user2", "user3", "user4", "user5", "user6", "user7"
};

static struct fb_var_screeninfo amiga_fb_predefined[] = {

	/*
	 * Autodetect (Default) Video Mode
	 */

	{ 0, },

	/*
	 * AmigaOS Video Modes
	 */

	{
		/* ntsc */
		640, 200, 640, 200, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 106, 86, 44, 16, 76, 2,
		FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* ntsc-lace */
		640, 400, 640, 400, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 106, 86, 88, 33, 76, 4,
		FB_SYNC_BROADCAST, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* pal */
		640, 256, 640, 256, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 106, 86, 40, 14, 76, 2,
		FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* pal-lace */
		640, 512, 640, 512, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 106, 86, 80, 29, 76, 4,
		FB_SYNC_BROADCAST, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* multiscan */
		640, 480, 640, 480, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 96, 112, 29, 8, 72, 8,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP

	}, {
		/* multiscan-lace */
		640, 960, 640, 960, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 96, 112, 58, 16, 72, 16,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* a2024-10 (Not yet supported) */
		1024, 800, 1024, 800, 0, 0, 2, 0,
		{0, 2, 0}, {0, 2, 0}, {0, 2, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 0, 0, 0, 0, 0, 0,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* a2024-15 (Not yet supported) */
		1024, 800, 1024, 800, 0, 0, 2, 0,
		{0, 2, 0}, {0, 2, 0}, {0, 2, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 0, 0, 0, 0, 0, 0,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* euro36 */
		640, 200, 640, 200, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 92, 124, 6, 6, 52, 5,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* euro36-lace */
		640, 400, 640, 400, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 92, 124, 12, 12, 52, 10,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* euro72 */
		640, 400, 640, 400, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 164, 92, 9, 9, 80, 8,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* euro72-lace */
		640, 800, 640, 800, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 164, 92, 18, 18, 80, 16,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* super72 */
		800, 300, 800, 300, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 212, 140, 10, 11, 80, 7,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* super72-lace */
		800, 600, 800, 600, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 212, 140, 20, 22, 80, 14,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* dblntsc */
		640, 200, 640, 200, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 18, 17, 80, 4,
		0, FB_VMODE_DOUBLE | FB_VMODE_YWRAP
	}, {
		/* dblntsc-ff */
		640, 400, 640, 400, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 36, 35, 80, 7,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* dblntsc-lace */
		640, 800, 640, 800, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 72, 70, 80, 14,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	}, {
		/* dblpal */
		640, 256, 640, 256, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 14, 13, 80, 4,
		0, FB_VMODE_DOUBLE | FB_VMODE_YWRAP
	}, {
		/* dblpal-ff */
		640, 512, 640, 512, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 28, 27, 80, 7,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* dblpal-lace */
		640, 1024, 640, 1024, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 56, 54, 80, 14,
		0, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
	},

	/*
	 * VGA Video Modes
	 */

	{
		/* vga */
		640, 480, 640, 480, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 64, 96, 30, 9, 112, 2,
		0, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	}, {
		/* vga70 */
		640, 400, 640, 400, 0, 0, 4, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 64, 96, 35, 12, 112, 2,
		FB_SYNC_VERT_HIGH_ACT | FB_SYNC_COMP_HIGH_ACT, FB_VMODE_NONINTERLACED | FB_VMODE_YWRAP
	},

	/*
	 * User Defined Video Modes
	 */

	{ 0, }, { 0, }, { 0, }, { 0, }, { 0, }, { 0, }, { 0, }, { 0, }
};

#define NUM_USER_MODES   (8)
#define NUM_TOTAL_MODES  arraysize(amiga_fb_predefined)
#define NUM_PREDEF_MODES (NUM_TOTAL_MODES-NUM_USER_MODES)

static int amifb_ilbm = 0;	/* interleaved or normal bitplanes */

static int amifb_inverse = 0;
static int amifb_usermode = 0;

	/*
	 * Some default modes
	 */

#define DEFMODE_PAL        "pal"	/* for PAL OCS/ECS */
#define DEFMODE_NTSC       "ntsc"	/* for NTSC OCS/ECS */
#define DEFMODE_AMBER_PAL  "pal-lace"	/* for flicker fixed PAL (A3000) */
#define DEFMODE_AMBER_NTSC "ntsc-lace"	/* for flicker fixed NTSC (A3000) */
#define DEFMODE_AGA        "vga70"	/* for AGA */

	/*
	 * Macros for the conversion from real world values to hardware register
	 * values
	 *
	 * This helps us to keep our attention on the real stuff...
	 *
	 * Hardware limits for AGA:
	 *
	 *	parameter  min    max  step
	 *	---------  ---   ----  ----
	 *	diwstrt_h    0   2047     1
	 *	diwstrt_v    0   2047     1
	 *	diwstop_h    0   4095     1
	 *	diwstop_v    0   4095     1
	 *
	 *	ddfstrt      0   2032    16
	 *	ddfstop      0   2032    16
	 *
	 *	htotal       8   2048     8
	 *	hsstrt       0   2040     8
	 *	hsstop       0   2040     8
	 *	vtotal       1   4096     1
	 *	vsstrt       0   4095     1
	 *	vsstop       0   4095     1
	 *	hcenter      0   2040     8
	 *
	 *	hbstrt       0   2047     1
	 *	hbstop       0   2047     1
	 *	vbstrt       0   4095     1
	 *	vbstop       0   4095     1
	 *
	 * Horizontal values are in 35 ns (SHRES) pixels
	 * Vertical values are in half scanlines
	 */

/* bplcon1 (smooth scrolling) */

#define hscroll2hw(hscroll) \
	(((hscroll)<<12 & 0x3000) | ((hscroll)<<8 & 0xc300) | \
	 ((hscroll)<<4 & 0x0c00) | ((hscroll)<<2 & 0x00f0) | ((hscroll)>>2 & 0x000f))

/* diwstrt/diwstop/diwhigh (visible display window) */

#define diwstrt2hw(diwstrt_h, diwstrt_v) \
	(((diwstrt_v)<<7 & 0xff00) | ((diwstrt_h)>>2 & 0x00ff))
#define diwstop2hw(diwstop_h, diwstop_v) \
	(((diwstop_v)<<7 & 0xff00) | ((diwstop_h)>>2 & 0x00ff))
#define diwhigh2hw(diwstrt_h, diwstrt_v, diwstop_h, diwstop_v) \
	(((diwstop_h)<<3 & 0x2000) | ((diwstop_h)<<11 & 0x1800) | \
	 ((diwstop_v)>>1 & 0x0700) | ((diwstrt_h)>>5 & 0x0020) | \
	 ((diwstrt_h)<<3 & 0x0018) | ((diwstrt_v)>>9 & 0x0007))

/* ddfstrt/ddfstop (display DMA) */

#define ddfstrt2hw(ddfstrt)	div8(ddfstrt)
#define ddfstop2hw(ddfstop)	div8(ddfstop)

/* hsstrt/hsstop/htotal/vsstrt/vsstop/vtotal/hcenter (sync timings) */

#define hsstrt2hw(hsstrt)	(div8(hsstrt))
#define hsstop2hw(hsstop)	(div8(hsstop))
#define htotal2hw(htotal)	(div8(htotal)-1)
#define vsstrt2hw(vsstrt)	(div2(vsstrt))
#define vsstop2hw(vsstop)	(div2(vsstop))
#define vtotal2hw(vtotal)	(div2(vtotal)-1)
#define hcenter2hw(htotal)	(div8(htotal))

/* hbstrt/hbstop/vbstrt/vbstop (blanking timings) */

#define hbstrt2hw(hbstrt)	(((hbstrt)<<8 & 0x0700) | ((hbstrt)>>3 & 0x00ff))
#define hbstop2hw(hbstop)	(((hbstop)<<8 & 0x0700) | ((hbstop)>>3 & 0x00ff))
#define vbstrt2hw(vbstrt)	(div2(vbstrt))
#define vbstop2hw(vbstop)	(div2(vbstop))

/* colour */

#define rgb2hw8_high(red, green, blue) \
	(((red)<<4 & 0xf00) | ((green) & 0x0f0) | ((blue)>>4 & 0x00f))
#define rgb2hw8_low(red, green, blue) \
	(((red)<<8 & 0xf00) | ((green)<<4 & 0x0f0) | ((blue) & 0x00f))
#define rgb2hw4(red, green, blue) \
	(((red)<<8 & 0xf00) | ((green)<<4 & 0x0f0) | ((blue) & 0x00f))
#define rgb2hw2(red, green, blue) \
	(((red)<<10 & 0xc00) | ((green)<<6 & 0x0c0) | ((blue)<<2 & 0x00c))

/* sprpos/sprctl (sprite positioning) */

#define spr2hw_pos(start_v, start_h) \
	(((start_v)<<7&0xff00) | ((start_h)>>3&0x00ff))
#define spr2hw_ctl(start_v, start_h, stop_v) \
	(((stop_v)<<7&0xff00) | ((start_v)>>4&0x0040) | ((stop_v)>>5&0x0020) | \
	 ((start_h)<<3&0x0018) | ((start_v)>>7&0x0004) | ((stop_v)>>8&0x0002) | \
	 ((start_h)>>2&0x0001))

/* get current vertical position of beam */
#define get_vbpos()	((u_short)((*(u_long volatile *)&custom.vposr >> 7) & 0xffe))

	/*
	 * Copper Initialisation List
	 */

#define COPINITSIZE (sizeof(copins)*40)

enum {
	cip_bplcon0
};

	/*
	 * Long Frame/Short Frame Copper List
	 * Don't change the order, build_copper()/rebuild_copper() rely on this
	 */

#define COPLISTSIZE (sizeof(copins)*64)

enum {
	cop_wait, cop_bplcon0,
	cop_spr0ptrh, cop_spr0ptrl,
	cop_diwstrt, cop_diwstop,
	cop_diwhigh,
};

	/*
	 * Pixel modes for Bitplanes and Sprites
	 */

static u_short bplpixmode[3] = {
	BPC0_SHRES,			/*  35 ns */
	BPC0_HIRES,			/*  70 ns */
	0				/* 140 ns */
};

static u_short sprpixmode[3] = {
	BPC3_SPRES1 | BPC3_SPRES0,	/*  35 ns */
	BPC3_SPRES1,			/*  70 ns */
	BPC3_SPRES0			/* 140 ns */
};

	/*
	 * Fetch modes for Bitplanes and Sprites
	 */

static u_short bplfetchmode[3] = {
	0,				/* 1x */
	FMODE_BPL32,			/* 2x */
	FMODE_BPAGEM | FMODE_BPL32	/* 4x */
};

static u_short sprfetchmode[3] = {
	0,				/* 1x */
	FMODE_SPR32,			/* 2x */
	FMODE_SPAGEM | FMODE_SPR32	/* 4x */
};

	/*
	 * Default Colormaps
	 */

static u_short red2[] =
	{ 0x0000, 0xc000 };
static u_short green2[] =
	{ 0x0000, 0xc000 };
static u_short blue2[] =
	{ 0x0000, 0xc000 };

static u_short red4[] =
	{ 0x0000, 0xc000, 0x8000, 0xffff };
static u_short green4[] =
	{ 0x0000, 0xc000, 0x8000, 0xffff };
static u_short blue4[] =
	{ 0x0000, 0xc000, 0x8000, 0xffff };

static u_short red8[] =
	{ 0x0000, 0x0000, 0x0000, 0x0000, 0xc000, 0xc000, 0xc000, 0xc000 };
static u_short green8[] =
	{ 0x0000, 0x0000, 0xc000, 0xc000, 0x0000, 0x0000, 0xc000, 0xc000 };
static u_short blue8[] =
	{ 0x0000, 0xc000, 0x0000, 0xc000, 0x0000, 0xc000, 0x0000, 0xc000 };

static u_short red16[] =
	{ 0x0000, 0x0000, 0x0000, 0x0000, 0xc000, 0xc000, 0xc000, 0xc000,
	  0x8000, 0x0000, 0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff };
static u_short green16[] =
	{ 0x0000, 0x0000, 0xc000, 0xc000, 0x0000, 0x0000, 0xc000, 0xc000,
	  0x8000, 0x0000, 0xffff, 0xffff, 0x0000, 0x0000, 0xffff, 0xffff };
static u_short blue16[] =
	{ 0x0000, 0xc000, 0x0000, 0xc000, 0x0000, 0xc000, 0x0000, 0xc000,
	  0x8000, 0xffff, 0x0000, 0xffff, 0x0000, 0xffff, 0x0000, 0xffff };


static struct fb_cmap default_2_colors =
	{ 0, 2, red2, green2, blue2, NULL };
static struct fb_cmap default_8_colors =
	{ 0, 8, red8, green8, blue8, NULL };
static struct fb_cmap default_4_colors =
	{ 0, 4, red4, green4, blue4, NULL };
static struct fb_cmap default_16_colors =
	{ 0, 16, red16, green16, blue16, NULL };

	/*
	 * Interface used by the world
	 */

void amiga_video_setup(char *options, int *ints);

static int amiga_fb_get_fix(struct fb_fix_screeninfo *fix, int con);
static int amiga_fb_get_var(struct fb_var_screeninfo *var, int con);
static int amiga_fb_set_var(struct fb_var_screeninfo *var, int con);
static int amiga_fb_pan_display(struct fb_var_screeninfo *var, int con);
static int amiga_fb_get_cmap(struct fb_cmap *cmap, int kspc, int con);
static int amiga_fb_set_cmap(struct fb_cmap *cmap, int kspc, int con);
static int amiga_fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
                          u_long arg, int con);

static int amiga_fb_get_fix_cursorinfo(struct fb_fix_cursorinfo *fix, int con);
static int amiga_fb_get_var_cursorinfo(struct fb_var_cursorinfo *var, u_char *data, int con);
static int amiga_fb_set_var_cursorinfo(struct fb_var_cursorinfo *var, u_char *data, int con);
static int amiga_fb_get_cursorstate(struct fb_cursorstate *state, int con);
static int amiga_fb_set_cursorstate(struct fb_cursorstate *state, int con);

	/*
	 * Interface to the low level console driver
	 */

struct fb_info *amiga_fb_init(long *mem_start);
static int amifbcon_switch(int con);
static int amifbcon_updatevar(int con);
static void amifbcon_blank(int blank);
static int amifbcon_setcmap(struct fb_cmap *cmap, int con);

	/*
	 * Internal routines
	 */

static struct fb_cmap *get_default_colormap(int bpp);
static int do_fb_get_cmap(struct fb_cmap *cmap, struct fb_var_screeninfo *var,
                          int kspc);
static int do_fb_set_cmap(struct fb_cmap *cmap, struct fb_var_screeninfo *var,
                          int kspc);
static void do_install_cmap(int con);
static void memcpy_fs(int fsfromto, void *to, void *from, int len);
static void copy_cmap(struct fb_cmap *from, struct fb_cmap *to, int fsfromto);
static int alloc_cmap(struct fb_cmap *cmap, int len, int transp);
static int flash_cursor(void);
static void amifb_interrupt(int irq, void *dev_id, struct pt_regs *fp);
static void get_video_mode(const char *name);
static void check_default_mode(void);
static u_long chipalloc(u_long size);
static char *strtoke(char *s,const char *ct);

	/*
	 * Hardware routines
	 */

static int ami_encode_fix(struct fb_fix_screeninfo *fix,
                          struct amiga_fb_par *par);
static int ami_decode_var(struct fb_var_screeninfo *var,
                          struct amiga_fb_par *par);
static int ami_encode_var(struct fb_var_screeninfo *var,
                          struct amiga_fb_par *par);
static void ami_get_par(struct amiga_fb_par *par);
static void ami_set_var(struct fb_var_screeninfo *var);
#ifdef DEBUG
static void ami_set_par(struct amiga_fb_par *par);
#endif
static void ami_pan_var(struct fb_var_screeninfo *var);
static int ami_update_par(void);
static int ami_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp);
static int ami_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp);
static void ami_update_display(void);
static void ami_init_display(void);
static void ami_do_blank(void);
static int ami_get_fix_cursorinfo(struct fb_fix_cursorinfo *fix, int con);
static int ami_get_var_cursorinfo(struct fb_var_cursorinfo *var, u_char *data, int con);
static int ami_set_var_cursorinfo(struct fb_var_cursorinfo *var, u_char *data, int con);
static int ami_get_cursorstate(struct fb_cursorstate *state, int con);
static int ami_set_cursorstate(struct fb_cursorstate *state, int con);
static void ami_set_sprite(void);
static void ami_init_copper(void);
static void ami_reinit_copper(void);
static void ami_build_copper(void);
static void ami_rebuild_copper(void);


	/*
	 * External references
	 */

extern unsigned short ami_intena_vals[];

	/*
	 * Support for Graphics Boards
	 */

#ifdef CONFIG_FB_CYBER			/* Cybervision */
extern int Cyber_probe(void);
extern void Cyber_video_setup(char *options, int *ints);
extern struct fb_info *Cyber_fb_init(long *mem_start);

static int amifb_Cyber = 0;
#endif

#ifdef CONFIG_FB_RETINAZ3			/* RetinaZ3 */
extern int retz3_probe(void);
extern void retz3_video_setup(char *options, int *ints);
extern struct fb_info *retz3_fb_init(long *mem_start);

static int amifb_retz3 = 0;
#endif

#ifdef CONFIG_GSP_RESOLVER			/* DMI Resolver */
extern int resolver_probe(void);
extern void resolver_video_setup(char *options, int *ints);
extern struct fb_info *resolver_fb_init(long *mem_start);

static int amifb_resolver = 0;
#endif

static struct fb_ops amiga_fb_ops = {
	amiga_fb_get_fix, amiga_fb_get_var, amiga_fb_set_var, amiga_fb_get_cmap,
	amiga_fb_set_cmap, amiga_fb_pan_display, amiga_fb_ioctl
};

void amiga_video_setup(char *options, int *ints)
{
	char *this_opt;
	int i;
	char mcap_spec[80];

	/*
	 * Check for a Graphics Board
	 */

#ifdef CONFIG_FB_CYBER
	if (options && *options)
		if (!strncmp(options, "cyber", 5) && Cyber_probe()) {
			amifb_Cyber = 1;
			Cyber_video_setup(options, ints);
			return;
		}
#endif
#ifdef CONFIG_FB_RETINAZ3
	if (options && *options)
		if (!strncmp(options, "retz3", 5) && retz3_probe()) {
			amifb_retz3 = 1;
			retz3_video_setup(options, ints);
			return;
		}
#endif
#ifdef CONFIG_GSP_RESOLVER
	if (options && *options)
		if (!strncmp(options, "resolver", 5) && resolver_probe()) {
			amifb_resolver = 1;
			resolver_video_setup(options, ints);
			return;
		}
#endif

	mcap_spec[0] = '\0';
	fb_info.fontname[0] = '\0';

	if (!options || !*options)
		return;

	for (this_opt = strtok(options, ","); this_opt; this_opt = strtok(NULL, ",")) {
		char *p;

		if (!strcmp(this_opt, "inverse")) {
			amifb_inverse = 1;
			for (i = 0; i < 16; i++) {
				red16[i] = ~red16[i];
				green16[i] = ~green16[i];
				blue16[i] = ~blue16[i];
			}
			for (i = 0; i < 8; i++) {
				red8[i] = ~red8[i];
				green8[i] = ~green8[i];
				blue8[i] = ~blue8[i];
			}
			for (i = 0; i < 4; i++) {
				red4[i] = ~red4[i];
				green4[i] = ~green4[i];
				blue4[i] = ~blue4[i];
			}
			for (i = 0; i < 2; i++) {
				red2[i] = ~red2[i];
				green2[i] = ~green2[i];
				blue2[i] = ~blue2[i];
			}
		} else if (!strcmp(this_opt, "ilbm"))
			amifb_ilbm = 1;
		else if (!strncmp(this_opt, "monitorcap:", 11))
			strcpy(mcap_spec, this_opt+11);
		else if (!strncmp(this_opt, "font:", 5))
			strcpy(fb_info.fontname, this_opt+5);
		else if (!strncmp(this_opt, "fstart:", 7))
			min_fstrt = simple_strtoul(this_opt+7, NULL, 0);
		else if (!strncmp(this_opt, "depth:", 6))
			amiga_fb_predefined[0].bits_per_pixel =
				simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "size:", 5)) {
			p = this_opt + 5;
			if (*p != ';')
				amiga_fb_predefined[0].xres =
					simple_strtoul(p, NULL, 0);
			if (!(p = strchr(p, ';')))
				continue;
			if (*++p != ';')
				amiga_fb_predefined[0].yres =
					simple_strtoul(p, NULL, 0);
			if (!(p = strchr(p, ';')))
				continue;
			if (*++p != ';')
				amiga_fb_predefined[0].xres_virtual =
					simple_strtoul(p, NULL, 0);
			if (!(p = strchr(p, ';')))
				continue;
			if (*++p != ';')
				amiga_fb_predefined[0].yres_virtual =
					simple_strtoul(p, NULL, 0);
			if (!(p = strchr(p, ';')))
				continue;
			if (*++p)
				amiga_fb_predefined[0].bits_per_pixel =
					simple_strtoul(p, NULL, 0);
		} else if (!strncmp(this_opt, "timing:", 7)) {
			p = this_opt + 7;
			if (*p != ';')
				amiga_fb_predefined[0].left_margin =
					simple_strtoul(p, NULL, 0);
			if (!(p = strchr(p, ';')))
				continue;
			if (*++p != ';')
				amiga_fb_predefined[0].right_margin =
					simple_strtoul(p, NULL, 0);
			if (!(p = strchr(p, ';')))
				continue;
			if (*++p != ';')
				amiga_fb_predefined[0].upper_margin =
					simple_strtoul(p, NULL, 0);
			if (!(p = strchr(p, ';')))
				continue;
			if (*++p)
				amiga_fb_predefined[0].lower_margin =
					simple_strtoul(p, NULL, 0);
		} else if (!strncmp(this_opt, "sync:", 5)) {
			p = this_opt + 5;
			if (*p != ';')
				amiga_fb_predefined[0].hsync_len =
					simple_strtoul(p, NULL, 0);
			if (!(p = strchr(p, ';')))
				continue;
			if (*++p)
				amiga_fb_predefined[0].vsync_len =
					simple_strtoul(p, NULL, 0);
		} else
			get_video_mode(this_opt);
	}

	if (min_fstrt < 48)
		min_fstrt = 48;

	if (*mcap_spec) {
		char *p;
		int vmin, vmax, hmin, hmax;

	/* Format for monitor capabilities is: <Vmin>;<Vmax>;<Hmin>;<Hmax>
	 * <V*> vertical freq. in Hz
	 * <H*> horizontal freq. in kHz
	 */

		if (!(p = strtoke(mcap_spec, ";")) || !*p)
			goto cap_invalid;
		vmin = simple_strtoul(p, NULL, 10);
		if (vmin <= 0)
			goto cap_invalid;
		if (!(p = strtoke(NULL, ";")) || !*p)
			goto cap_invalid;
		vmax = simple_strtoul(p, NULL, 10);
		if (vmax <= 0 || vmax <= vmin)
			goto cap_invalid;
		if (!(p = strtoke(NULL, ";")) || !*p)
			goto cap_invalid;
		hmin = 1000 * simple_strtoul(p, NULL, 10);
		if (hmin <= 0)
			goto cap_invalid;
		if (!(p = strtoke(NULL, "")) || !*p)
			goto cap_invalid;
		hmax = 1000 * simple_strtoul(p, NULL, 10);
		if (hmax <= 0 || hmax <= hmin)
			goto cap_invalid;

		vfmin = vmin;
		vfmax = vmax;
		hfmin = hmin;
		hfmax = hmax;
cap_invalid:
		;
	}
}

	/*
	 * Get the Fixed Part of the Display
	 */

static int amiga_fb_get_fix(struct fb_fix_screeninfo *fix, int con)
{
	struct amiga_fb_par par;

	if (con == -1)
		ami_get_par(&par);
	else {
		int err;

		if ((err = ami_decode_var(&disp[con].var, &par)))
			return err;
	}
	return ami_encode_fix(fix, &par);
}

	/*
	 * Get the User Defined Part of the Display
	 */

static int amiga_fb_get_var(struct fb_var_screeninfo *var, int con)
{
	int err = 0;

	if (con == -1) {
		struct amiga_fb_par par;

		ami_get_par(&par);
		err = ami_encode_var(var, &par);
	} else
		*var = disp[con].var;
	return err;
}

	/*
	 * Set the User Defined Part of the Display
	 */

static int amiga_fb_set_var(struct fb_var_screeninfo *var, int con)
{
	int err, activate = var->activate;
	int oldxres, oldyres, oldvxres, oldvyres, oldbpp;
	struct amiga_fb_par par;


	/*
	 * FB_VMODE_CONUPDATE and FB_VMODE_SMOOTH_XPAN are equal!
	 * as FB_VMODE_SMOOTH_XPAN is only used internally
	 */

	if (var->vmode & FB_VMODE_CONUPDATE) {
		var->vmode |= FB_VMODE_YWRAP;
		var->xoffset = disp[con].var.xoffset;
		var->yoffset = disp[con].var.yoffset;
	}
	if ((err = ami_decode_var(var, &par)))
		return err;
	ami_encode_var(var, &par);
	if ((activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		oldxres = disp[con].var.xres;
		oldyres = disp[con].var.yres;
		oldvxres = disp[con].var.xres_virtual;
		oldvyres = disp[con].var.yres_virtual;
		oldbpp = disp[con].var.bits_per_pixel;
		disp[con].var = *var;
		if (oldxres != var->xres || oldyres != var->yres ||
		    oldvxres != var->xres_virtual || oldvyres != var->yres_virtual ||
		    oldbpp != var->bits_per_pixel) {
			struct fb_fix_screeninfo fix;

			ami_encode_fix(&fix, &par);
			disp[con].screen_base = (u_char *)fix.smem_start;
			disp[con].visual = fix.visual;
			disp[con].type = fix.type;
			disp[con].type_aux = fix.type_aux;
			disp[con].ypanstep = fix.ypanstep;
			disp[con].ywrapstep = fix.ywrapstep;
			disp[con].line_length = fix.line_length;
			disp[con].can_soft_blank = 1;
			disp[con].inverse = amifb_inverse;
			if (fb_info.changevar)
				(*fb_info.changevar)(con);
		}
		if (oldbpp != var->bits_per_pixel) {
			if ((err = alloc_cmap(&disp[con].cmap, 0, 0)))
				return err;
			do_install_cmap(con);
		}
		if (con == currcon)
			ami_set_var(&disp[con].var);
	}
	return 0;
}

	/*
	 * Pan or Wrap the Display
	 *
	 * This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
	 */

static int amiga_fb_pan_display(struct fb_var_screeninfo *var, int con)
{
	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset<0 || var->yoffset >= disp[con].var.yres_virtual || var->xoffset)
			return -EINVAL;
	} else {
		/*
		 * TODO: There will be problems when xpan!=1, so some columns
		 * on the right side will never be seen
		 */
		if (var->xoffset+disp[con].var.xres > upx(16<<maxfmode, disp[con].var.xres_virtual) ||
		    var->yoffset+disp[con].var.yres > disp[con].var.yres_virtual)
			return -EINVAL;
	}
	if (con == currcon)
		ami_pan_var(var);
	disp[con].var.xoffset = var->xoffset;
	disp[con].var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		disp[con].var.vmode |= FB_VMODE_YWRAP;
	else
		disp[con].var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}

	/*
	 * Get the Colormap
	 */

static int amiga_fb_get_cmap(struct fb_cmap *cmap, int kspc, int con)
{
	if (con == currcon) /* current console? */
		return do_fb_get_cmap(cmap, &disp[con].var, kspc);
	else if (disp[con].cmap.len) /* non default colormap? */
		copy_cmap(&disp[con].cmap, cmap, kspc ? 0 : 2);
	else
		copy_cmap(get_default_colormap(disp[con].var.bits_per_pixel),
		          cmap, kspc ? 0 : 2);
	return 0;
}

	/*
	 * Set the Colormap
	 */

static int amiga_fb_set_cmap(struct fb_cmap *cmap, int kspc, int con)
{
	int err;

	if (!disp[con].cmap.len) {		/* no colormap allocated? */
		if ((err = alloc_cmap(&disp[con].cmap,
		                      1<<disp[con].var.bits_per_pixel, 0)))
			return err;
	}
	if (con == currcon)			/* current console? */
		return do_fb_set_cmap(cmap, &disp[con].var, kspc);
	else
		copy_cmap(cmap, &disp[con].cmap, kspc ? 0 : 1);
	return 0;
}

	/*
	 * Amiga Frame Buffer Specific ioctls
	 */

static int amiga_fb_ioctl(struct inode *inode, struct file *file,
                          u_int cmd, u_long arg, int con)
{
	int i;

	switch (cmd) {
		case FBIOGET_FCURSORINFO : {
			struct fb_fix_cursorinfo crsrfix;
			
			i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(crsrfix));
			if (!i) {
				i = amiga_fb_get_fix_cursorinfo(&crsrfix, con);
				copy_to_user((void *)arg, &crsrfix, sizeof(crsrfix));
			}
			return i;
		}
		case FBIOGET_VCURSORINFO : {
			struct fb_var_cursorinfo crsrvar;

			i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(crsrvar));
			if (!i) {
				i = amiga_fb_get_var_cursorinfo(&crsrvar,
					((struct fb_var_cursorinfo *)arg)->data, con);
				copy_to_user((void *)arg, &crsrvar, sizeof(crsrvar));
			}
			return i;
		}
		case FBIOPUT_VCURSORINFO : {
			struct fb_var_cursorinfo crsrvar;

			i = verify_area(VERIFY_READ, (void *)arg, sizeof(crsrvar));
			if (!i) {
				copy_from_user(&crsrvar, (void *)arg, sizeof(crsrvar));
				i = amiga_fb_set_var_cursorinfo(&crsrvar,
					((struct fb_var_cursorinfo *)arg)->data, con);
			}
			return i;
		}
		case FBIOGET_CURSORSTATE : {
			struct fb_cursorstate crsrstate;

			i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(crsrstate));
			if (!i) {
				i = amiga_fb_get_cursorstate(&crsrstate, con);
				copy_to_user((void *)arg, &crsrstate, sizeof(crsrstate));
			}
			return i;
		}
		case FBIOPUT_CURSORSTATE : {
			struct fb_cursorstate crsrstate;

			i = verify_area(VERIFY_READ, (void *)arg, sizeof(crsrstate));
			if (!i) {
				copy_from_user(&crsrstate, (void *)arg, sizeof(crsrstate));
				i = amiga_fb_set_cursorstate(&crsrstate, con);
			}
			return i;
		}
#ifdef DEBUG
		case FBCMD_GET_CURRENTPAR : {
			struct amiga_fb_par par;

			i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(struct amiga_fb_par));
			if (!i) {
				ami_get_par(&par);
				copy_to_user((void *)arg, &par, sizeof(struct amiga_fb_par));
			}
			return i;
		}
		case FBCMD_SET_CURRENTPAR : {
			struct amiga_fb_par par;

			i = verify_area(VERIFY_READ, (void *)arg, sizeof(struct amiga_fb_par));
			if (!i) {
				copy_from_user(&par, (void *)arg, sizeof(struct amiga_fb_par));
				ami_set_par(&par);
			}
			return i;
		}
#endif */ DEBUG */
	}
	return -EINVAL;
}

	/*
	 * Hardware Cursor
	 */

static int amiga_fb_get_fix_cursorinfo(struct fb_fix_cursorinfo *fix, int con)
{
	return ami_get_fix_cursorinfo(fix, con);
}

static int amiga_fb_get_var_cursorinfo(struct fb_var_cursorinfo *var, u_char *data, int con)
{
	return ami_get_var_cursorinfo(var, data, con);
}

static int amiga_fb_set_var_cursorinfo(struct fb_var_cursorinfo *var, u_char *data, int con)
{
	return ami_set_var_cursorinfo(var, data, con);
}

static int amiga_fb_get_cursorstate(struct fb_cursorstate *state, int con)
{
	return ami_get_cursorstate(state, con);
}

static int amiga_fb_set_cursorstate(struct fb_cursorstate *state, int con)
{
	return ami_set_cursorstate(state, con);
}

	/*
	 * Initialisation
	 */

__initfunc(struct fb_info *amiga_fb_init(long *mem_start))
{
	int err, tag, i;
	u_long chipptr;

	/*
	 * Check for a Graphics Board
	 */

#ifdef CONFIG_FB_CYBER
	if (amifb_Cyber)
		return Cyber_fb_init(mem_start);
#endif
#ifdef CONFIG_FB_RETINAZ3
	if (amifb_retz3){
		custom.dmacon = DMAF_MASTER | DMAF_RASTER | DMAF_COPPER |
				DMAF_BLITTER | DMAF_SPRITE;
		return retz3_fb_init(mem_start);
	}
#endif
#ifdef CONFIG_GSP_RESOLVER
	if (amifb_resolver){
		custom.dmacon = DMAF_MASTER | DMAF_RASTER | DMAF_COPPER |
				DMAF_BLITTER | DMAF_SPRITE;
		return NULL;
	}
#endif

	/*
	 * Use the Builtin Chipset
	 */

	if (!AMIGAHW_PRESENT(AMI_VIDEO))
		return NULL;

	custom.dmacon = DMAF_ALL | DMAF_MASTER;

	switch (amiga_chipset) {
#ifdef CONFIG_AMIFB_OCS
		case CS_OCS:
			strcat(amiga_fb_name, "OCS");
default_chipset:
			chipset = TAG_OCS;
			maxdepth[TAG_SHRES] = 0;	/* OCS means no SHRES */
			maxdepth[TAG_HIRES] = 4;
			maxdepth[TAG_LORES] = 6;
			maxfmode = TAG_FMODE_1;
			if (!amifb_usermode)		/* Set the Default Video Mode */
				get_video_mode(amiga_vblank == 50 ?
				               DEFMODE_PAL : DEFMODE_NTSC);
			videomemorysize = VIDEOMEMSIZE_OCS;
			break;
#endif /* CONFIG_AMIFB_OCS */

#ifdef CONFIG_AMIFB_ECS
		case CS_ECS:
			strcat(amiga_fb_name, "ECS");
			chipset = TAG_ECS;
			maxdepth[TAG_SHRES] = 2;
			maxdepth[TAG_HIRES] = 4;
			maxdepth[TAG_LORES] = 6;
			maxfmode = TAG_FMODE_1;
			if (!amifb_usermode) {		/* Set the Default Video Mode */
				if (AMIGAHW_PRESENT(AMBER_FF))
					get_video_mode(amiga_vblank == 50 ?
					               DEFMODE_AMBER_PAL : DEFMODE_AMBER_NTSC);
				else
					get_video_mode(amiga_vblank == 50 ?
					               DEFMODE_PAL : DEFMODE_NTSC);
			}
			if (amiga_chip_avail()-CHIPRAM_SAFETY_LIMIT >
			    VIDEOMEMSIZE_ECS_1M)
				videomemorysize = VIDEOMEMSIZE_ECS_2M;
			else
				videomemorysize = VIDEOMEMSIZE_ECS_1M;
			break;
#endif /* CONFIG_AMIFB_ECS */

#ifdef CONFIG_AMIFB_AGA
		case CS_AGA:
			strcat(amiga_fb_name, "AGA");
			chipset = TAG_AGA;
			maxdepth[TAG_SHRES] = 8;
			maxdepth[TAG_HIRES] = 8;
			maxdepth[TAG_LORES] = 8;
			maxfmode = TAG_FMODE_4;
			if (!amifb_usermode)		/* Set the Default Video Mode */
				get_video_mode(DEFMODE_AGA);
			if (amiga_chip_avail()-CHIPRAM_SAFETY_LIMIT >
			    VIDEOMEMSIZE_AGA_1M)
				videomemorysize = VIDEOMEMSIZE_AGA_2M;
			else
				videomemorysize = VIDEOMEMSIZE_AGA_1M;
			break;
#endif /* CONFIG_AMIFB_AGA */

		default:
#ifdef CONFIG_AMIFB_OCS
			printk("Unknown graphics chipset, defaulting to OCS\n");
			strcat(amiga_fb_name, "Unknown");
			goto default_chipset;
#else /* CONFIG_AMIFB_OCS */
			panic("Unknown graphics chipset, no default driver");
#endif /* CONFIG_AMIFB_OCS */
			break;
	}

	/*
	 * Calculate the Pixel Clock Values for this Machine
	 */

	pixclock[TAG_SHRES] = DIVUL(25E9, amiga_eclock);	/* SHRES:  35 ns / 28 MHz */
	pixclock[TAG_HIRES] = DIVUL(50E9, amiga_eclock);	/* HIRES:  70 ns / 14 MHz */
	pixclock[TAG_LORES] = DIVUL(100E9, amiga_eclock); 	/* LORES: 140 ns /  7 MHz */

	/*
	 * Replace the Tag Values with the Real Pixel Clock Values
	 */

	for (i = 0; i < NUM_PREDEF_MODES; i++) {
		tag = amiga_fb_predefined[i].pixclock;
		if (tag == TAG_SHRES || tag == TAG_HIRES || tag == TAG_LORES) {
			amiga_fb_predefined[i].pixclock = pixclock[tag];
			if (amiga_fb_predefined[i].bits_per_pixel > maxdepth[tag])
				amiga_fb_predefined[i].bits_per_pixel = maxdepth[tag];
		}
	}

	err = register_framebuffer(amiga_fb_name, &node, &amiga_fb_ops,
	                           NUM_TOTAL_MODES, amiga_fb_predefined);
	if (err < 0)
		panic("Cannot register frame buffer");

	chipptr = chipalloc(videomemorysize+
	                    SPRITEMEMSIZE+
	                    DUMMYSPRITEMEMSIZE+
	                    COPINITSIZE+
	                    4*COPLISTSIZE);

	assignchunk(videomemory, u_long, chipptr, videomemorysize);
	assignchunk(spritememory, u_long, chipptr, SPRITEMEMSIZE);
	assignchunk(dummysprite, u_short *, chipptr, DUMMYSPRITEMEMSIZE);
	assignchunk(copdisplay.init, copins *, chipptr, COPINITSIZE);
	assignchunk(copdisplay.list[0][0], copins *, chipptr, COPLISTSIZE);
	assignchunk(copdisplay.list[0][1], copins *, chipptr, COPLISTSIZE);
	assignchunk(copdisplay.list[1][0], copins *, chipptr, COPLISTSIZE);
	assignchunk(copdisplay.list[1][1], copins *, chipptr, COPLISTSIZE);

	memset(dummysprite, 0, DUMMYSPRITEMEMSIZE);

	/*
	 * Enable Display DMA
	 */

	custom.dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER |
	                DMAF_BLITTER | DMAF_SPRITE;

	/*
	 * Make sure the Copper has something to do
	 */

	ami_init_copper();

	check_default_mode();

	if (request_irq(IRQ_AMIGA_AUTO_3, amifb_interrupt, IRQ_FLG_LOCK,
	                "fb vertb handler", NULL))
		panic("Couldn't add vblank interrupt\n");
	ami_intena_vals[IRQ_AMIGA_VERTB] = IF_COPER;
	ami_intena_vals[IRQ_AMIGA_COPPER] = 0;
	custom.intena = IF_VERTB;
	custom.intena = IF_SETCLR | IF_COPER;

	strcpy(fb_info.modename, amiga_fb_name);
	fb_info.changevar = NULL;
	fb_info.disp = disp;
	fb_info.switch_con = &amifbcon_switch;
	fb_info.updatevar = &amifbcon_updatevar;
	fb_info.blank = &amifbcon_blank;
	fb_info.setcmap = &amifbcon_setcmap;

	amiga_fb_set_var(&amiga_fb_predefined[0], 0);

	return &fb_info;
}

static int amifbcon_switch(int con)
{
	/* Do we have to save the colormap? */
	if (disp[currcon].cmap.len)
		do_fb_get_cmap(&disp[currcon].cmap, &disp[currcon].var, 1);

	currcon = con;
	ami_set_var(&disp[con].var);
	/* Install new colormap */
	do_install_cmap(con);
	return 0;
}

	/*
	 * Update the `var' structure (called by amicon.c)
	 */

static int amifbcon_updatevar(int con)
{
	ami_pan_var(&disp[con].var);
	return 0;
}

	/*
	 * Blank the display.
	 */

static void amifbcon_blank(int blank)
{
	do_blank = blank ? blank : -1;
}

	/*
	 * Set the colormap
	 */

static int amifbcon_setcmap(struct fb_cmap *cmap, int con)
{
	return(amiga_fb_set_cmap(cmap, 1, con));
}

/* ---------------------------- Generic routines ---------------------------- */

static struct fb_cmap *get_default_colormap(int bpp)
{
	switch (bpp) {
		case 1:
			return &default_2_colors;
			break;
		case 2:
			return &default_4_colors;
			break;
		case 3:
			return &default_8_colors;
			break;
		default:
			return &default_16_colors;
			break;
	}
}

#define CNVT_TOHW(val,width)	((((val)<<(width))+0x7fff-(val))>>16)
#define CNVT_FROMHW(val,width)	(((width) ? ((((val)<<16)-(val)) / \
                                            ((1<<(width))-1)) : 0))

static int do_fb_get_cmap(struct fb_cmap *cmap, struct fb_var_screeninfo *var,
                          int kspc)
{
	int i, start;
	u_short *red, *green, *blue, *transp;
	u_int hred, hgreen, hblue, htransp;

	red = cmap->red;
	green = cmap->green;
	blue = cmap->blue;
	transp = cmap->transp;
	start = cmap->start;
	if (start < 0)
		return -EINVAL;
	for (i = 0; i < cmap->len; i++) {
		if (ami_getcolreg(start++, &hred, &hgreen, &hblue, &htransp))
			return 0;
		hred = CNVT_FROMHW(hred, var->red.length);
		hgreen = CNVT_FROMHW(hgreen, var->green.length);
		hblue = CNVT_FROMHW(hblue, var->blue.length);
		htransp = CNVT_FROMHW(htransp, var->transp.length);
		if (kspc) {
			*red = hred;
			*green = hgreen;
			*blue = hblue;
			if (transp)
				*transp = htransp;
		} else {
			put_user(hred, red);
			put_user(hgreen, green);
			put_user(hblue, blue);
			if (transp)
				put_user(htransp, transp);
		}
		red++;
		green++;
		blue++;
		if (transp)
			transp++;
	}
	return 0;
}

static int do_fb_set_cmap(struct fb_cmap *cmap, struct fb_var_screeninfo *var,
                          int kspc)
{
	int i, start;
	u_short *red, *green, *blue, *transp;
	u_int hred, hgreen, hblue, htransp;

	red = cmap->red;
	green = cmap->green;
	blue = cmap->blue;
	transp = cmap->transp;
	start = cmap->start;

	if (start < 0)
		return -EINVAL;
	for (i = 0; i < cmap->len; i++) {
		if (kspc) {
			hred = *red;
			hgreen = *green;
			hblue = *blue;
			htransp = transp ? *transp : 0;
		} else {
			get_user(hred, red);
			get_user(hgreen, green);
			get_user(hblue, blue);
			if (transp)
				get_user(htransp, transp);
			else
				htransp = 0;
		}
		hred = CNVT_TOHW(hred, var->red.length);
		hgreen = CNVT_TOHW(hgreen, var->green.length);
		hblue = CNVT_TOHW(hblue, var->blue.length);
		htransp = CNVT_TOHW(htransp, var->transp.length);
		red++;
		green++;
		blue++;
		if (transp)
			transp++;
		if (ami_setcolreg(start++, hred, hgreen, hblue, htransp))
			return 0;
	}
	return 0;
}

static void do_install_cmap(int con)
{
	if (con != currcon)
		return;
	if (disp[con].cmap.len)
		do_fb_set_cmap(&disp[con].cmap, &disp[con].var, 1);
	else
		do_fb_set_cmap(get_default_colormap(disp[con].var.bits_per_pixel),
		                                    &disp[con].var, 1);
}

static void memcpy_fs(int fsfromto, void *to, void *from, int len)
{
	switch (fsfromto) {
		case 0:
			memcpy(to, from, len);
			return;
		case 1:
			copy_from_user(to, from, len);
			return;
		case 2:
			copy_to_user(to, from, len);
			return;
	}
}

static void copy_cmap(struct fb_cmap *from, struct fb_cmap *to, int fsfromto)
{
	int size;
	int tooff = 0, fromoff = 0;

	if (to->start > from->start)
		fromoff = to->start-from->start;
	else
		tooff = from->start-to->start;
	size = to->len-tooff;
	if (size > from->len-fromoff)
		size = from->len-fromoff;
	if (size < 0)
		return;
	size *= sizeof(u_short);
	memcpy_fs(fsfromto, to->red+tooff, from->red+fromoff, size);
	memcpy_fs(fsfromto, to->green+tooff, from->green+fromoff, size);
	memcpy_fs(fsfromto, to->blue+tooff, from->blue+fromoff, size);
	if (from->transp && to->transp)
		memcpy_fs(fsfromto, to->transp+tooff, from->transp+fromoff, size);
}

static int alloc_cmap(struct fb_cmap *cmap, int len, int transp)
{
	int size = len*sizeof(u_short);

	if (cmap->len != len) {
		if (cmap->red)
			kfree(cmap->red);
		if (cmap->green)
			kfree(cmap->green);
		if (cmap->blue)
			kfree(cmap->blue);
		if (cmap->transp)
			kfree(cmap->transp);
		cmap->red = cmap->green = cmap->blue = cmap->transp = NULL;
		cmap->len = 0;
		if (!len)
			return 0;
		if (!(cmap->red = kmalloc(size, GFP_ATOMIC)))
			return -1;
		if (!(cmap->green = kmalloc(size, GFP_ATOMIC)))
			return -1;
		if (!(cmap->blue = kmalloc(size, GFP_ATOMIC)))
			return -1;
		if (transp) {
			if (!(cmap->transp = kmalloc(size, GFP_ATOMIC)))
				return -1;
		} else
			cmap->transp = NULL;
	}
	cmap->start = 0;
	cmap->len = len;
	copy_cmap(get_default_colormap(len), cmap, 0);
	return 0;
}

static int flash_cursor(void)
{
	static int cursorcount = 1;

	if (cursormode == FB_CURSOR_FLASH) {
		if (!--cursorcount) {
			cursorstate = -cursorstate;
			cursorcount = cursorrate;
			if (!is_blanked)
				return 1;
		}
	}
	return 0;
}

	/*
	 * VBlank Display Interrupt
	 */

static void amifb_interrupt(int irq, void *dev_id, struct pt_regs *fp)
{
	u_short ints = custom.intreqr & custom.intenar;
	static struct irq_server server = {0, 0};
	unsigned long flags;

	if (ints & IF_BLIT) {
		custom.intreq = IF_BLIT;
		amiga_do_irq(IRQ_AMIGA_BLIT, fp);
	}

	if (ints & IF_COPER) {
		custom.intreq = IF_COPER;
		if (do_vmode_pan || do_vmode_full)
			ami_update_display();

		if (do_vmode_full)
			ami_init_display();

		if (do_vmode_pan) {
			flash_cursor();
			ami_rebuild_copper();
			do_cursor = do_vmode_pan = 0;
		} else if (do_cursor) {
			flash_cursor();
			ami_set_sprite();
			do_cursor = 0;
		} else {
			if (flash_cursor())
				ami_set_sprite();
		}

		save_flags(flags);
		cli();
		if (get_vbpos() < down2(currentpar.diwstrt_v - 6))
			custom.copjmp2 = 0;
		restore_flags(flags);

		if (do_blank) {
			ami_do_blank();
			do_blank = 0;
		}

		if (do_vmode_full) {
			ami_reinit_copper();
			do_vmode_full = 0;
		}
		amiga_do_irq_list(IRQ_AMIGA_VERTB, fp, &server);
	}

	if (ints & IF_VERTB) {
		printk("%s: Warning: IF_VERTB was enabled\n", __FUNCTION__);
		custom.intena = IF_VERTB;
	}
}

	/*
	 * Get a Video Mode
	 */

static void get_video_mode(const char *name)
{
	int i;

	for (i = 1; i < NUM_PREDEF_MODES; i++) {
		if (!strcmp(name, amiga_fb_modenames[i])) {
			amiga_fb_predefined[0] = amiga_fb_predefined[i];
			amifb_usermode = i;
			return;
		}
	}
}

	/*
	 * Probe the  Video Modes
	 */

static void check_default_mode(void)
{
	struct amiga_fb_par par;
	int mode;

	for (mode = 0; mode < NUM_PREDEF_MODES; mode++) {
		if (!ami_decode_var(&amiga_fb_predefined[mode], &par)) {
			if (mode)
				amiga_fb_predefined[0] = amiga_fb_predefined[mode];
			return;
		}
		if (!mode)
			printk("Can't use default video mode. Probing video modes...\n");
	}
	panic("Can't find any usable video mode");
}

	/*
	 * Allocate, Clear and Align a Block of Chip Memory
	 */

static u_long chipalloc(u_long size)
{
	u_long ptr;

	size += PAGE_SIZE-1;
	if (!(ptr = (u_long)amiga_chip_alloc(size)))
		panic("No Chip RAM for frame buffer");
	memset((void *)ptr, 0, size);
	ptr = PAGE_ALIGN(ptr);

	return ptr;
}

	/*
	 * A strtok which returns empty strings, too
	 */

static char *strtoke(char *s,const char *ct)
{
	char *sbegin, *send;
	static char *ssave = NULL;

	sbegin  = s ? s : ssave;
	if (!sbegin)
		return NULL;
	if (*sbegin == '\0') {
		ssave = NULL;
		return NULL;
	}
	send = strpbrk(sbegin, ct);
	if (send && *send != '\0')
		*send++ = '\0';
	ssave = send;
	return sbegin;
}

/* --------------------------- Hardware routines --------------------------- */

	/*
	 * This function should fill in the `fix' structure based on the
	 * values in the `par' structure.
	 */

static int ami_encode_fix(struct fb_fix_screeninfo *fix,
                          struct amiga_fb_par *par)
{
	int i;

	strcpy(fix->id, amiga_fb_name);
	fix->smem_start = videomemory;
	fix->smem_len = videomemorysize;

	if (amifb_ilbm) {
		fix->type = FB_TYPE_INTERLEAVED_PLANES;
		fix->type_aux = par->next_line;
	} else {
		fix->type = FB_TYPE_PLANES;
		fix->type_aux = 0;
	}
	fix->line_length = div8(upx(16<<maxfmode, par->vxres));
	fix->visual = FB_VISUAL_PSEUDOCOLOR;

	if (par->vmode & FB_VMODE_YWRAP) {
		fix->ywrapstep = 1;
		fix->xpanstep = fix->ypanstep = 0;
	} else {
		fix->ywrapstep = 0;
		if (par->vmode &= FB_VMODE_SMOOTH_XPAN)
			fix->xpanstep = 1;
		else
			fix->xpanstep = 16<<maxfmode;
		fix->ypanstep = 1;
	}

	for (i = 0; i < arraysize(fix->reserved); i++)
		fix->reserved[i] = 0;

	return 0;
}

	/*
	 * Get the video params out of `var'. If a value doesn't fit, round
	 * it up, if it's too big, return -EINVAL.
	 */

static int ami_decode_var(struct fb_var_screeninfo *var,
                          struct amiga_fb_par *par)
{
	u_short clk_shift, line_shift;
	u_long maxfetchstop, fstrt, fsize, fconst, xres_n, yres_n;
	u_long hrate = 0, vrate = 0;

	/*
	 * Find a matching Pixel Clock
	 */

	for (clk_shift = TAG_SHRES; clk_shift < TAG_LORES; clk_shift++)
		if (var->pixclock <= pixclock[clk_shift])
			break;
	if (clk_shift >= TAG_LORES)
		return -EINVAL;
	par->clk_shift = clk_shift;

	/*
	 * Check the Geometry Values
	 */

	if ((par->xres = var->xres) < 64)
		return -EINVAL;
	if ((par->yres = var->yres) < 64)
		return -EINVAL;
	if ((par->vxres = var->xres_virtual) < 64)
		return -EINVAL;
	if ((par->vyres = var->yres_virtual) < 64)
		return -EINVAL;

	par->bpp = var->bits_per_pixel;
	if (!var->nonstd) {
		if (par->bpp <= 0 || par->bpp > maxdepth[clk_shift])
			return -EINVAL;
	} else if (var->nonstd == FB_NONSTD_HAM) {
		if (par->bpp != 6)
			if (par->bpp != 8 || !IS_AGA)
				return -EINVAL;
	} else
		return -EINVAL;

	/*
	 * FB_VMODE_SMOOTH_XPAN will be cleared, if one of the folloing
	 * checks failed and smooth scrolling is not possible
	 */

	par->vmode = var->vmode | FB_VMODE_SMOOTH_XPAN;
	switch (par->vmode & FB_VMODE_MASK) {
		case FB_VMODE_INTERLACED:
			line_shift = 0;
			break;
		case FB_VMODE_NONINTERLACED:
			line_shift = 1;
			break;
		case FB_VMODE_DOUBLE:
			if (!IS_AGA)
				return -EINVAL;
			line_shift = 2;
			break;
		default:
			return -EINVAL;
			break;
	}
	par->line_shift = line_shift;

	/*
	 * Vertical and Horizontal Timings
	 */

	xres_n = par->xres<<clk_shift;
	yres_n = par->yres<<line_shift;
	par->htotal = down8((var->left_margin+par->xres+var->right_margin+var->hsync_len)<<clk_shift);
	par->vtotal = down2(((var->upper_margin+par->yres+var->lower_margin+var->vsync_len)<<line_shift)+1);

	if (IS_AGA)
		par->bplcon3 = sprpixmode[clk_shift];
	else
		par->bplcon3 = 0;
	if (var->sync & FB_SYNC_BROADCAST) {
		par->diwstop_h = par->htotal-((var->right_margin-var->hsync_len)<<clk_shift);
		if (IS_AGA)
			par->diwstop_h += mod4(var->hsync_len);
		else
			par->diwstop_h = down4(par->diwstop_h);
		par->diwstrt_h = par->diwstop_h - xres_n;
		par->diwstop_v = par->vtotal-((var->lower_margin-var->vsync_len)<<line_shift);
		par->diwstrt_v = par->diwstop_v - yres_n;
		if (par->diwstop_h >= par->htotal+8 || par->diwstop_v > par->vtotal)
			return -EINVAL;
		if (!IS_OCS) {
			/* Initialize sync with some reasonable values for pwrsave */
			par->hsstrt = 160;
			par->hsstop = 320;
			par->vsstrt = 30;
			par->vsstop = 34;
		} else {
			par->hsstrt = 0;
			par->hsstop = 0;
			par->vsstrt = 0;
			par->vsstop = 0;
		}
		if (par->vtotal > (PAL_VTOTAL+NTSC_VTOTAL)/2) {
			/* PAL video mode */
			if (par->htotal != PAL_HTOTAL)
				return -EINVAL;
			if (par->diwstrt_h < PAL_DIWSTRT_H)
				return -EINVAL;
			if (par->diwstrt_v < PAL_DIWSTRT_V)
				return -EINVAL;
			hrate = 15625;
			vrate = 50;
			if (!IS_OCS) {
				par->beamcon0 = BMC0_PAL;
				par->bplcon3 |= BPC3_BRDRBLNK;
			} else if (AMIGAHW_PRESENT(AGNUS_HR_PAL) || 
			           AMIGAHW_PRESENT(AGNUS_HR_NTSC)) {
				par->beamcon0 = BMC0_PAL;
				par->hsstop = 1;
			} else if (amiga_vblank != 50)
				return -EINVAL;
		} else {
			/* NTSC video mode
			 * In the AGA chipset seems to be hardware bug with BPC3_BRDRBLNK
			 * and NTSC activated, so than better let diwstop_h <= 1812
			 */
			if (par->htotal != NTSC_HTOTAL)
				return -EINVAL;
			if (par->diwstrt_h < NTSC_DIWSTRT_H)
				return -EINVAL;
			if (par->diwstrt_v < NTSC_DIWSTRT_V)
				return -EINVAL;
			hrate = 15750;
			vrate = 60;
			if (!IS_OCS) {
				par->beamcon0 = 0;
				par->bplcon3 |= BPC3_BRDRBLNK;
			} else if (AMIGAHW_PRESENT(AGNUS_HR_PAL) || 
			           AMIGAHW_PRESENT(AGNUS_HR_NTSC)) {
				par->beamcon0 = 0;
				par->hsstop = 1;
			} else if (amiga_vblank != 60)
				return -EINVAL;
		}
		if (IS_OCS) {
			if (par->diwstrt_h >= 1024 || par->diwstop_h < 1024 ||
			    par->diwstrt_v >=  512 || par->diwstop_v <  256)
				return -EINVAL;
		}
	} else if (!IS_OCS) {
		/* Programmable video mode */
		par->hsstrt = var->right_margin<<clk_shift;
		par->hsstop = (var->right_margin+var->hsync_len)<<clk_shift;
		par->diwstop_h = par->htotal - mod8(par->hsstrt) + 8 - (1 << clk_shift);
		if (!IS_AGA)
			par->diwstop_h = down4(par->diwstop_h) - 16;
		par->diwstrt_h = par->diwstop_h - xres_n;
		par->hbstop = par->diwstrt_h + 4;
		par->hbstrt = par->diwstop_h + 4;
		if (par->hbstrt >= par->htotal + 8)
			par->hbstrt -= par->htotal;
		par->hcenter = par->hsstrt + (par->htotal >> 1);
		par->vsstrt = var->lower_margin<<line_shift;
		par->vsstop = (var->lower_margin+var->vsync_len)<<line_shift;
		par->diwstop_v = par->vtotal;
		if ((par->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED)
			par->diwstop_v -= 2;
		par->diwstrt_v = par->diwstop_v - yres_n;
		par->vbstop = par->diwstrt_v - 2;
		par->vbstrt = par->diwstop_v - 2;
		if (par->vtotal > 2048 || par->htotal > 2048)
			return -EINVAL;
		par->bplcon3 |= BPC3_EXTBLKEN;
		par->beamcon0 = BMC0_HARDDIS | BMC0_VARVBEN | BMC0_LOLDIS |
		                BMC0_VARVSYEN | BMC0_VARHSYEN | BMC0_VARBEAMEN |
		                BMC0_PAL | BMC0_VARCSYEN;
		if (var->sync & FB_SYNC_HOR_HIGH_ACT)
			par->beamcon0 |= BMC0_HSYTRUE;
		if (var->sync & FB_SYNC_VERT_HIGH_ACT)
			par->beamcon0 |= BMC0_VSYTRUE;
		if (var->sync & FB_SYNC_COMP_HIGH_ACT)
			par->beamcon0 |= BMC0_CSYTRUE;
		hrate = (amiga_masterclock+par->htotal/2)/par->htotal;
		vrate = div2(par->vtotal) * par->htotal;
		vrate = (amiga_masterclock+vrate/2)/vrate;
	} else
		return -EINVAL;

	/*
	 * Checking the DMA timing
	 */

	fconst = 16<<maxfmode<<clk_shift;

	/*
	 * smallest window start value without turn off other dma cycles
	 * than sprite1-7, unless you change min_fstrt
	 */


	fsize = ((maxfmode+clk_shift <= 1) ? fconst : 64);
	fstrt = downx(fconst, par->diwstrt_h-4) - fsize;
	if (fstrt < min_fstrt)
		return -EINVAL;

	/*
	 * smallest window start value where smooth scrolling is possible
	 */

	fstrt = downx(fconst, par->diwstrt_h-fconst+(1<<clk_shift)-4) - fsize;
	if (fstrt < min_fstrt)
		par->vmode &= ~FB_VMODE_SMOOTH_XPAN;

	maxfetchstop = down16(par->htotal - 80);

	fstrt = downx(fconst, par->diwstrt_h-4) - 64 - fconst;
	fsize = upx(fconst, xres_n + modx(fconst, downx(1<<clk_shift, par->diwstrt_h-4)));
	if (fstrt + fsize > maxfetchstop)
		par->vmode &= ~FB_VMODE_SMOOTH_XPAN;

	fsize = upx(fconst, xres_n);
	if (fstrt + fsize > maxfetchstop)
		return -EINVAL;

	if (maxfmode + clk_shift <= 1) {
		fsize = up64(xres_n + fconst - 1);
		if (min_fstrt + fsize - 64 > maxfetchstop)
			par->vmode &= ~FB_VMODE_SMOOTH_XPAN;

		fsize = up64(xres_n);
		if (min_fstrt + fsize - 64 > maxfetchstop)
			return -EINVAL;

		fsize -= 64;
	} else
		fsize -= fconst;

	/*
	 * Check if there is enough time to update the bitplane pointers for ywrap
	 */

	if (par->htotal-fsize-64 < par->bpp*64)
		par->vmode &= ~FB_VMODE_YWRAP;

	/*
	 * Bitplane calculations and check the Memory Requirements
	 */

	if (amifb_ilbm) {
		par->next_plane = div8(upx(16<<maxfmode, par->vxres));
		par->next_line = par->bpp*par->next_plane;
		if (par->next_line * par->vyres > videomemorysize)
			return -EINVAL;
	} else {
		par->next_line = div8(upx(16<<maxfmode, par->vxres));
		par->next_plane = par->vyres*par->next_line;
		if (par->next_plane * par->bpp > videomemorysize)
			return -EINVAL;
	}

	/*
	 * Hardware Register Values
	 */

	par->bplcon0 = BPC0_COLOR | bplpixmode[clk_shift];
	if (!IS_OCS)
		par->bplcon0 |= BPC0_ECSENA;
	if (par->bpp == 8)
		par->bplcon0 |= BPC0_BPU3;
	else
		par->bplcon0 |= par->bpp<<12;
	if (var->nonstd == FB_NONSTD_HAM)
		par->bplcon0 |= BPC0_HAM;
	if (var->sync & FB_SYNC_EXT)
		par->bplcon0 |= BPC0_ERSY;

	if (IS_AGA)
		par->fmode = bplfetchmode[maxfmode];

	switch (par->vmode & FB_VMODE_MASK) {
		case FB_VMODE_INTERLACED:
			par->bplcon0 |= BPC0_LACE;
			break;
		case FB_VMODE_DOUBLE:
			if (IS_AGA)
				par->fmode |= FMODE_SSCAN2 | FMODE_BSCAN2;
			break;
	}

	if (!((par->vmode ^ var->vmode) & FB_VMODE_YWRAP)) {
		par->xoffset = var->xoffset;
		par->yoffset = var->yoffset;
		if (par->vmode & FB_VMODE_YWRAP) {
			if (par->xoffset || par->yoffset < 0 || par->yoffset >= par->vyres)
				par->xoffset = par->yoffset = 0;
		} else {
			if (par->xoffset < 0 || par->xoffset > upx(16<<maxfmode, par->vxres-par->xres) ||
			    par->yoffset < 0 || par->yoffset > par->vyres-par->yres)
				par->xoffset = par->yoffset = 0;
		}
	} else
		par->xoffset = par->yoffset = 0;

	par->crsr.crsr_x = par->crsr.crsr_y = 0;
	par->crsr.spot_x = par->crsr.spot_y = 0;
	par->crsr.height = par->crsr.width = 0;

	if (hrate < hfmin || hrate > hfmax || vrate < vfmin || vrate > vfmax)
		return -EINVAL;

	return 0;
}

	/*
	 * Fill the `var' structure based on the values in `par' and maybe
	 * other values read out of the hardware.
	 */

static int ami_encode_var(struct fb_var_screeninfo *var,
                          struct amiga_fb_par *par)
{
	u_short clk_shift, line_shift;
	int i;

	clk_shift = par->clk_shift;
	line_shift = par->line_shift;

	var->xres = par->xres;
	var->yres = par->yres;
	var->xres_virtual = par->vxres;
	var->yres_virtual = par->vyres;
	var->xoffset = par->xoffset;
	var->yoffset = par->yoffset;

	var->bits_per_pixel = par->bpp;
	var->grayscale = 0;

	if (IS_AGA) {
		var->red.offset = 0;
		var->red.length = 8;
		var->red.msb_right = 0;
	} else {
		if (clk_shift == TAG_SHRES) {
			var->red.offset = 0;
			var->red.length = 2;
			var->red.msb_right = 0;
		} else {
			var->red.offset = 0;
			var->red.length = 4;
			var->red.msb_right = 0;
		}
	}
	var->blue = var->green = var->red;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->transp.msb_right = 0;

	if (par->bplcon0 & BPC0_HAM)
		var->nonstd = FB_NONSTD_HAM;
	else
		var->nonstd = 0;
	var->activate = 0;

	var->height = -1;
	var->width = -1;
	var->accel = 0;

	var->pixclock = pixclock[clk_shift];

	if (IS_AGA && par->fmode & FMODE_BSCAN2)
		var->vmode = FB_VMODE_DOUBLE;
	else if (par->bplcon0 & BPC0_LACE)
		var->vmode = FB_VMODE_INTERLACED;
	else
		var->vmode = FB_VMODE_NONINTERLACED;

	if (!IS_OCS && par->beamcon0 & BMC0_VARBEAMEN) {
		var->hsync_len = (par->hsstop-par->hsstrt)>>clk_shift;
		var->right_margin = par->hsstrt>>clk_shift;
		var->left_margin = (par->htotal>>clk_shift) - var->xres - var->right_margin - var->hsync_len;
		var->vsync_len = (par->vsstop-par->vsstrt)>>line_shift;
		var->lower_margin = par->vsstrt>>line_shift;
		var->upper_margin = (par->vtotal>>line_shift) - var->yres - var->lower_margin - var->vsync_len;
		var->sync = 0;
		if (par->beamcon0 & BMC0_HSYTRUE)
			var->sync |= FB_SYNC_HOR_HIGH_ACT;
		if (par->beamcon0 & BMC0_VSYTRUE)
			var->sync |= FB_SYNC_VERT_HIGH_ACT;
		if (par->beamcon0 & BMC0_CSYTRUE)
			var->sync |= FB_SYNC_COMP_HIGH_ACT;
	} else {
		var->sync = FB_SYNC_BROADCAST;
		var->hsync_len = (152>>clk_shift) + mod4(par->diwstop_h);
		var->right_margin = ((par->htotal - down4(par->diwstop_h))>>clk_shift) + var->hsync_len;
		var->left_margin = (par->htotal>>clk_shift) - var->xres - var->right_margin - var->hsync_len;
		var->vsync_len = 4>>line_shift;
		var->lower_margin = ((par->vtotal - par->diwstop_v)>>line_shift) + var->vsync_len;
		var->upper_margin = (((par->vtotal - 2)>>line_shift) + 1) - var->yres -
		                    var->lower_margin - var->vsync_len;
	}

	if (par->bplcon0 & BPC0_ERSY)
		var->sync |= FB_SYNC_EXT;
	if (par->vmode & FB_VMODE_YWRAP)
		var->vmode |= FB_VMODE_YWRAP;

	for (i = 0; i < arraysize(var->reserved); i++)
		var->reserved[i] = 0;

	return 0;
}

	/*
	 * Get current hardware setting
	 */

static void ami_get_par(struct amiga_fb_par *par)
{
	*par = currentpar;
}

	/*
	 * Set new videomode
	 */

static void ami_set_var(struct fb_var_screeninfo *var)
{
	do_vmode_pan = 0;
	do_vmode_full = 0;
	ami_decode_var(var, &currentpar);
	ami_build_copper();
	do_vmode_full = 1;
}

#ifdef DEBUG
static void ami_set_par(struct amiga_fb_par *par)
{
	do_vmode_pan = 0;
	do_vmode_full = 0;
	currentpar = *par;
	ami_build_copper();
	do_vmode_full = 1;
}
#endif

	/*
	 * Pan or Wrap the Display
	 *
	 * This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
	 * in `var'.
	 */

static void ami_pan_var(struct fb_var_screeninfo *var)
{
	struct amiga_fb_par *par = &currentpar;

	par->xoffset = var->xoffset;
	par->yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		par->vmode |= FB_VMODE_YWRAP;
	else
		par->vmode &= ~FB_VMODE_YWRAP;

	do_vmode_pan = 0;
	ami_update_par();
	do_vmode_pan = 1;
}

	/*
	 * Update hardware
	 */

static int ami_update_par(void)
{
	struct amiga_fb_par *par = &currentpar;
	short clk_shift, vshift, fstrt, fsize, fstop, fconst,  shift, move, mod;

	clk_shift = par->clk_shift;

	if (!(par->vmode & FB_VMODE_SMOOTH_XPAN))
		par->xoffset = upx(16<<maxfmode, par->xoffset);

	fconst = 16<<maxfmode<<clk_shift;
	vshift = modx(16<<maxfmode, par->xoffset);
	fstrt = par->diwstrt_h - (vshift<<clk_shift) - 4;
	fsize = (par->xres+vshift)<<clk_shift;
	shift = modx(fconst, fstrt);
	move = downx(2<<maxfmode, div8(par->xoffset));
	if (maxfmode + clk_shift > 1) {
		fstrt = downx(fconst, fstrt) - 64;
		fsize = upx(fconst, fsize);
		fstop = fstrt + fsize - fconst;
	} else {
		mod = fstrt = downx(fconst, fstrt) - fconst;
		fstop = fstrt + upx(fconst, fsize) - 64;
		fsize = up64(fsize);
		fstrt = fstop - fsize + 64;
		if (fstrt < min_fstrt) {
			fstop += min_fstrt - fstrt;
			fstrt = min_fstrt;
		}
		move = move - div8((mod-fstrt)>>clk_shift);
	}
	mod = par->next_line - div8(fsize>>clk_shift);
	par->ddfstrt = fstrt;
	par->ddfstop = fstop;
	par->bplcon1 = hscroll2hw(shift);
	par->bpl2mod = mod;
	if (par->bplcon0 & BPC0_LACE)
		par->bpl2mod += par->next_line;
	if (IS_AGA && (par->fmode & FMODE_BSCAN2))
		par->bpl1mod = -div8(fsize>>clk_shift);
	else
		par->bpl1mod = par->bpl2mod;

	if (par->yoffset) {
		par->bplpt0 = ZTWO_PADDR((u_long)videomemory + par->next_line*par->yoffset + move);
		if (par->vmode & FB_VMODE_YWRAP) {
			if (par->yoffset > par->vyres-par->yres) {
				par->bplpt0wrap = ZTWO_PADDR((u_long)videomemory + move);
				if (par->bplcon0 & BPC0_LACE && mod2(par->diwstrt_v+par->vyres-par->yoffset))
					par->bplpt0wrap += par->next_line;
			}
		}
	} else
		par->bplpt0 = ZTWO_PADDR((u_long)videomemory + move);

	if (par->bplcon0 & BPC0_LACE && mod2(par->diwstrt_v))
		par->bplpt0 += par->next_line;

	return 0;
}

	/*
	 * Read a single color register and split it into
	 * colors/transparent. Return != 0 for invalid regno.
	 */

static int ami_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp)
{
	if (IS_AGA) {
		if (regno > 255)
			return 1;
	} else {
		if (regno > 31)
			return 1;
	}

	*red = palette[regno].red;
	*green = palette[regno].green;
	*blue = palette[regno].blue;
	return 0;
}


	/*
	 * Set a single color register. The values supplied are already
	 * rounded down to the hardware's capabilities (according to the
	 * entries in the var structure). Return != 0 for invalid regno.
	 */

static int ami_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp)
{
#if defined(CONFIG_AMIFB_AGA)
	u_short bplcon3 = currentpar.bplcon3;

	if (IS_AGA) {
		if (regno > 255)
			return 1;
	} else
#endif
		if (regno > 31)
			return 1;

	/*
	 * Update the corresponding Hardware Color Register, unless it's Color
	 * Register 0 and the screen is blanked.
	 *
	 * VBlank is switched off to protect bplcon3 or ecs_palette[] from
	 * being changed by ami_do_blank() during the VBlank.
	 */

	palette[regno].red = red;
	palette[regno].green = green;
	palette[regno].blue = blue;

	if (regno || !is_blanked) {
#if defined(CONFIG_AMIFB_AGA)
		if (IS_AGA) {
			VBlankOff();
			custom.bplcon3 = bplcon3 | (regno<<8 & 0xe000);
			custom.color[regno&31] = rgb2hw8_high(red, green, blue);
			custom.bplcon3 = bplcon3 | (regno<<8 & 0xe000) | BPC3_LOCT;
			custom.color[regno&31] = rgb2hw8_low(red, green, blue);
			custom.bplcon3 = bplcon3;
			VBlankOn();
		} else
#endif
		{
#if defined(CONFIG_AMIFB_ECS)
			if (currentpar.bplcon0 & BPC0_SHRES) {
				u_short color, mask;
				int i;

				mask = 0x3333;
				color = rgb2hw2(red, green, blue);
				VBlankOff();
				for (i = regno+12; i >= (int)regno; i -= 4)
					custom.color[i] = ecs_palette[i] = (ecs_palette[i] & mask) | color;
				mask <<=2; color >>= 2;
				regno = down16(regno)+mul4(mod4(regno));
				for (i = regno+3; i >= (int)regno; i--)
					custom.color[i] = ecs_palette[i] = (ecs_palette[i] & mask) | color;
				VBlankOn();
			} else
#endif
				custom.color[regno] = rgb2hw4(red, green, blue);
		}
	}
	return 0;
}

static void ami_update_display(void)
{
	struct amiga_fb_par *par = &currentpar;

	custom.bplcon1 = par->bplcon1;
	custom.bpl1mod = par->bpl1mod;
	custom.bpl2mod = par->bpl2mod;
	custom.ddfstrt = ddfstrt2hw(par->ddfstrt);
	custom.ddfstop = ddfstop2hw(par->ddfstop);
}

	/*
	 * Change the video mode (called by VBlank interrupt)
	 */

static void ami_init_display(void)
{
	struct amiga_fb_par *par = &currentpar;

	custom.bplcon0 = par->bplcon0 & ~BPC0_LACE;
	custom.bplcon2 = (IS_OCS ? 0 : BPC2_KILLEHB) | BPC2_PF2P2 | BPC2_PF1P2;
	if (!IS_OCS) {
		custom.bplcon3 = par->bplcon3;
		if (IS_AGA)
			custom.bplcon4 = BPC4_ESPRM4 | BPC4_OSPRM4;
		if (par->beamcon0 & BMC0_VARBEAMEN) {
			custom.htotal = htotal2hw(par->htotal);
			custom.hbstrt = hbstrt2hw(par->hbstrt);
			custom.hbstop = hbstop2hw(par->hbstop);
			custom.hsstrt = hsstrt2hw(par->hsstrt);
			custom.hsstop = hsstop2hw(par->hsstop);
			custom.hcenter = hcenter2hw(par->hcenter);
			custom.vtotal = vtotal2hw(par->vtotal);
			custom.vbstrt = vbstrt2hw(par->vbstrt);
			custom.vbstop = vbstop2hw(par->vbstop);
			custom.vsstrt = vsstrt2hw(par->vsstrt);
			custom.vsstop = vsstop2hw(par->vsstop);
		}
	}
	if (!IS_OCS || par->hsstop)
		custom.beamcon0 = par->beamcon0;
	if (IS_AGA)
		custom.fmode = par->fmode;

	/*
	 * The minimum period for audio depends on htotal
	 */

	amiga_audio_min_period = div16(par->htotal);

	is_lace = par->bplcon0 & BPC0_LACE ? 1 : 0;
#if 1
	if (is_lace) {
		if (custom.vposr & 0x8000)
			custom.cop2lc = (u_short *)ZTWO_PADDR(copdisplay.list[currentcop][1]);
		else
			custom.cop2lc = (u_short *)ZTWO_PADDR(copdisplay.list[currentcop][0]);
	} else {
		custom.vposw = custom.vposr | 0x8000;
		custom.cop2lc = (u_short *)ZTWO_PADDR(copdisplay.list[currentcop][1]);
	}
#else
	custom.vposw = custom.vposr | 0x8000;
	custom.cop2lc = (u_short *)ZTWO_PADDR(copdisplay.list[currentcop][1]);
#endif
}

	/*
	 * (Un)Blank the screen (called by VBlank interrupt)
	 */

static void ami_do_blank(void)
{
	struct amiga_fb_par *par = &currentpar;
#if defined(CONFIG_AMIFB_AGA)
	u_short bplcon3 = par->bplcon3;
#endif
	u_char red, green, blue;

	if (do_blank > 0) {
		custom.dmacon = DMAF_RASTER | DMAF_SPRITE;
		red = green = blue = 0;
		if (!IS_OCS && do_blank > 1) {
			switch (do_blank) {
				case 2 : /* suspend vsync */
					custom.hsstrt = hsstrt2hw(par->hsstrt);
					custom.hsstop = hsstop2hw(par->hsstop);
					custom.vsstrt = vsstrt2hw(par->vtotal+4);
					custom.vsstop = vsstop2hw(par->vtotal+4);
					break;
				case 3 : /* suspend hsync */
					custom.hsstrt = hsstrt2hw(par->htotal+16);
					custom.hsstop = hsstop2hw(par->htotal+16);
					custom.vsstrt = vsstrt2hw(par->vsstrt);
					custom.vsstop = vsstrt2hw(par->vsstop);
					break;
				case 4 : /* powerdown */
					custom.hsstrt = hsstrt2hw(par->htotal+16);
					custom.hsstop = hsstop2hw(par->htotal+16);
					custom.vsstrt = vsstrt2hw(par->vtotal+4);
					custom.vsstop = vsstop2hw(par->vtotal+4);
					break;
			}
			if (!(par->beamcon0 & BMC0_VARBEAMEN)) {
				custom.htotal = htotal2hw(par->htotal);
				custom.vtotal = vtotal2hw(par->vtotal);
				custom.beamcon0 = BMC0_HARDDIS | BMC0_VARBEAMEN |
				                  BMC0_VARVSYEN | BMC0_VARHSYEN | BMC0_VARCSYEN;
			}
		}
	} else {
		custom.dmacon = DMAF_SETCLR | DMAF_RASTER | DMAF_SPRITE;
		red = palette[0].red;
		green = palette[0].green;
		blue = palette[0].blue;
		if (!IS_OCS) {
			custom.hsstrt = hsstrt2hw(par->hsstrt);
			custom.hsstop = hsstop2hw(par->hsstop);
			custom.vsstrt = vsstrt2hw(par->vsstrt);
			custom.vsstop = vsstop2hw(par->vsstop);
			custom.beamcon0 = par->beamcon0;
		}
	}
#if defined(CONFIG_AMIFB_AGA)
	if (IS_AGA) {
		custom.bplcon3 = bplcon3;
		custom.color[0] = rgb2hw8_high(red, green, blue);
		custom.bplcon3 = bplcon3 | BPC3_LOCT;
		custom.color[0] = rgb2hw8_low(red, green, blue);
		custom.bplcon3 = bplcon3;
	} else
#endif
	{
#if defined(CONFIG_AMIFB_ECS)
		if (par->bplcon0 & BPC0_SHRES) {
			u_short color, mask;
			int i;

			mask = 0x3333;
			color = rgb2hw2(red, green, blue);
			for (i = 12; i >= 0; i -= 4)
				custom.color[i] = ecs_palette[i] = (ecs_palette[i] & mask) | color;
			mask <<=2; color >>= 2;
			for (i = 3; i >= 0; i--)
				custom.color[i] = ecs_palette[i] = (ecs_palette[i] & mask) | color;
		} else
#endif
			custom.color[0] = rgb2hw4(red, green, blue);
	}
	is_blanked = do_blank > 0 ? do_blank : 0;
}

	/*
	 * Flash the cursor (called by VBlank interrupt)
	 */

static int ami_get_fix_cursorinfo(struct fb_fix_cursorinfo *fix, int con)
{
	struct amiga_fb_par *par = &currentpar;

	fix->crsr_width = fix->crsr_xsize = par->crsr.width;
	fix->crsr_height = fix->crsr_ysize = par->crsr.height;
	fix->crsr_color1 = 17;
	fix->crsr_color2 = 18;
	return 0;
}

static int ami_get_var_cursorinfo(struct fb_var_cursorinfo *var, u_char *data, int con)
{
	struct amiga_fb_par *par = &currentpar;
	register u_short *lspr, *sspr;
	register u_long datawords asm ("d2");
	register short delta;
	register u_char color;
	short height, width, bits, words;
	int i, size, alloc;

	size = par->crsr.height*par->crsr.width;
	alloc = var->height*var->width;
	var->height = par->crsr.height;
	var->width = par->crsr.width;
	var->xspot = par->crsr.spot_x;
	var->yspot = par->crsr.spot_y;
	if (size > var->height*var->width)
		return -ENAMETOOLONG;
	if ((i = verify_area(VERIFY_WRITE, (void *)data, size)))
		return i;
	delta = 1<<par->crsr.fmode;
	lspr = lofsprite + (delta<<1);
	if (par->bplcon0 & BPC0_LACE)
		sspr = shfsprite + (delta<<1);
	else
		sspr = 0;
	for (height = (short)var->height-1; height >= 0; height--) {
		bits = 0; words = delta; datawords = 0;
		for (width = (short)var->width-1; width >= 0; width--) {
			if (bits == 0) {
				bits = 16; --words;
				asm volatile ("movew %1@(%3:w:2),%0 ; swap %0 ; movew %1@+,%0"
					: "=d" (datawords), "=a" (lspr) : "1" (lspr), "d" (delta));
			}
			--bits;
			asm volatile (
				"clrb %0 ; swap %1 ; lslw #1,%1 ; roxlb #1,%0 ; "
				"swap %1 ; lslw #1,%1 ; roxlb #1,%0"
				: "=d" (color), "=d" (datawords) : "1" (datawords));
			put_user(color, data++);
		}
		if (bits > 0) {
			--words; ++lspr;
		}
		while (--words >= 0)
			++lspr;
		asm volatile ("lea %0@(%4:w:2),%0 ; tstl %1 ; jeq 1f ; exg %0,%1\n1:"
			: "=a" (lspr), "=a" (sspr) : "0" (lspr), "1" (sspr), "d" (delta));
	}
	return 0;
}

static int ami_set_var_cursorinfo(struct fb_var_cursorinfo *var, u_char *data, int con)
{
	struct amiga_fb_par *par = &currentpar;
	register u_short *lspr, *sspr;
	register u_long datawords asm ("d2");
	register short delta;
	u_short fmode;
	short height, width, bits, words;
	int i;

	if (!var->width)
		return -EINVAL;
	else if (var->width <= 16)
		fmode = TAG_FMODE_1;
	else if (var->width <= 32)
		fmode = TAG_FMODE_2;
	else if (var->width <= 64)
		fmode = TAG_FMODE_4;
	else
		return -EINVAL;
	if (fmode > maxfmode)
		return -EINVAL;
	if (!var->height)
		return -EINVAL;
	if ((i = verify_area(VERIFY_READ, (void *)data, var->width*var->height)))
		return i;
	delta = 1<<fmode;
	lofsprite = shfsprite = (u_short *)spritememory;
	lspr = lofsprite + (delta<<1);
	if (par->bplcon0 & BPC0_LACE) {
		if (((var->height+4)<<fmode<<2) > SPRITEMEMSIZE)
			return -EINVAL;
		memset(lspr, 0, (var->height+4)<<fmode<<2);
		shfsprite += ((var->height+5)&-2)<<fmode;
		sspr = shfsprite + (delta<<1);
	} else {
		if (((var->height+2)<<fmode<<2) > SPRITEMEMSIZE)
			return -EINVAL;
		memset(lspr, 0, (var->height+2)<<fmode<<2);
		sspr = 0;
	}
	for (height = (short)var->height-1; height >= 0; height--) {
		bits = 16; words = delta; datawords = 0;
		for (width = (short)var->width-1; width >= 0; width--) {
			unsigned long tdata = 0;
			get_user(tdata, (char *)data);
			data++;
			asm volatile (
				"lsrb #1,%2 ; roxlw #1,%0 ; swap %0 ; "
				"lsrb #1,%2 ; roxlw #1,%0 ; swap %0"
				: "=d" (datawords)
				: "0" (datawords), "d" (tdata));
			if (--bits == 0) {
				bits = 16; --words;
				asm volatile ("swap %2 ; movew %2,%0@(%3:w:2) ; swap %2 ; movew %2,%0@+"
					: "=a" (lspr) : "0" (lspr), "d" (datawords), "d" (delta));
			}
		}
		if (bits < 16) {
			--words;
			asm volatile (
				"swap %2 ; lslw %4,%2 ; movew %2,%0@(%3:w:2) ; "
				"swap %2 ; lslw %4,%2 ; movew %2,%0@+"
				: "=a" (lspr) : "0" (lspr), "d" (datawords), "d" (delta), "d" (bits));
		}
		while (--words >= 0)
			asm volatile ("moveql #0,%%d0 ; movew %%d0,%0@(%2:w:2) ; movew %%d0,%0@+"
				: "=a" (lspr) : "0" (lspr), "d" (delta) : "d0");
		asm volatile ("lea %0@(%4:w:2),%0 ; tstl %1 ; jeq 1f ; exg %0,%1\n1:"
			: "=a" (lspr), "=a" (sspr) : "0" (lspr), "1" (sspr), "d" (delta));
	}
	par->crsr.height = var->height;
	par->crsr.width = var->width;
	par->crsr.spot_x = var->xspot;
	par->crsr.spot_y = var->yspot;
	par->crsr.fmode = fmode;
	if (IS_AGA) {
		par->fmode &= ~(FMODE_SPAGEM | FMODE_SPR32);
		par->fmode |= sprfetchmode[fmode];
		custom.fmode = par->fmode;
	}
	return 0;
}

static int ami_get_cursorstate(struct fb_cursorstate *state, int con)
{
	struct amiga_fb_par *par = &currentpar;

	state->xoffset = par->crsr.crsr_x;
	state->yoffset = par->crsr.crsr_y;
	state->mode = cursormode;
	return 0;
}

static int ami_set_cursorstate(struct fb_cursorstate *state, int con)
{
	struct amiga_fb_par *par = &currentpar;

	par->crsr.crsr_x = state->xoffset;
	par->crsr.crsr_y = state->yoffset;
	if ((cursormode = state->mode) == FB_CURSOR_OFF)
		cursorstate = -1;
	do_cursor = 1;
	return 0;
}

static void ami_set_sprite(void)
{
	struct amiga_fb_par *par = &currentpar;
	copins *copl, *cops;
	u_short hs, vs, ve;
	u_long pl, ps, pt;
	short mx, my;

	cops = copdisplay.list[currentcop][0];
	copl = copdisplay.list[currentcop][1];
	ps = pl = ZTWO_PADDR(dummysprite);
	mx = par->crsr.crsr_x-par->crsr.spot_x;
	my = par->crsr.crsr_y-par->crsr.spot_y;
	if (!(par->vmode & FB_VMODE_YWRAP)) {
		mx -= par->xoffset;
		my -= par->yoffset;
	}
	if (!is_blanked && cursorstate > 0 && par->crsr.height > 0 &&
	    mx > -(short)par->crsr.width && mx < par->xres &&
	    my > -(short)par->crsr.height && my < par->yres) {
		pl = ZTWO_PADDR(lofsprite);
		hs = par->diwstrt_h + (mx<<par->clk_shift) - 4;
		vs = par->diwstrt_v + (my<<par->line_shift);
		ve = vs + (par->crsr.height<<par->line_shift);
		if (par->bplcon0 & BPC0_LACE) {
			ps = ZTWO_PADDR(shfsprite);
			lofsprite[0] = spr2hw_pos(vs, hs);
			shfsprite[0] = spr2hw_pos(vs+1, hs);
			if (mod2(vs)) {
				lofsprite[1<<par->crsr.fmode] = spr2hw_ctl(vs, hs, ve);
				shfsprite[1<<par->crsr.fmode] = spr2hw_ctl(vs+1, hs, ve+1);
				pt = pl; pl = ps; ps = pt;
			} else {
				lofsprite[1<<par->crsr.fmode] = spr2hw_ctl(vs, hs, ve+1);
				shfsprite[1<<par->crsr.fmode] = spr2hw_ctl(vs+1, hs, ve);
			}
		} else {
			lofsprite[0] = spr2hw_pos(vs, hs) | (IS_AGA && (par->fmode & FMODE_BSCAN2) ? 0x80 : 0);
			lofsprite[1<<par->crsr.fmode] = spr2hw_ctl(vs, hs, ve);
		}
	}
	copl[cop_spr0ptrh].w[1] = highw(pl);
	copl[cop_spr0ptrl].w[1] = loww(pl);
	if (par->bplcon0 & BPC0_LACE) {
		cops[cop_spr0ptrh].w[1] = highw(ps);
		cops[cop_spr0ptrl].w[1] = loww(ps);
	}
}

	/*
	 * Initialise the Copper Initialisation List
	 */

static void ami_init_copper(void)
{
	copins *cop = copdisplay.init;
	u_long p;
	int i;

	if (!IS_OCS) {
		(cop++)->l = CMOVE(BPC0_COLOR | BPC0_SHRES | BPC0_ECSENA, bplcon0);
		(cop++)->l = CMOVE(0x0181, diwstrt);
		(cop++)->l = CMOVE(0x0281, diwstop);
		(cop++)->l = CMOVE(0x0000, diwhigh);
	} else
		(cop++)->l = CMOVE(BPC0_COLOR, bplcon0);
	p = ZTWO_PADDR(dummysprite);
	for (i = 0; i < 8; i++) {
		(cop++)->l = CMOVE(0, spr[i].pos);
		(cop++)->l = CMOVE(highw(p), sprpt[i]);
		(cop++)->l = CMOVE2(loww(p), sprpt[i]);
	}

	(cop++)->l = CMOVE(IF_SETCLR | IF_COPER, intreq);
	copdisplay.wait = cop;
	(cop++)->l = CEND;
	(cop++)->l = CMOVE(0, copjmp2);
	cop->l = CEND;

	custom.cop1lc = (u_short *)ZTWO_PADDR(copdisplay.init);
	custom.copjmp1 = 0;
}

static void ami_reinit_copper(void)
{
	struct amiga_fb_par *par = &currentpar;

	copdisplay.init[cip_bplcon0].w[1] = ~(BPC0_BPU3 | BPC0_BPU2 | BPC0_BPU1 | BPC0_BPU0) & par->bplcon0;
	copdisplay.wait->l = CWAIT(32, par->diwstrt_v-4);
}

	/*
	 * Build the Copper List
	 */

static void ami_build_copper(void)
{
	struct amiga_fb_par *par = &currentpar;
	copins *copl, *cops;
	u_long p;

	currentcop = 1 - currentcop;

	copl = copdisplay.list[currentcop][1];

	(copl++)->l = CWAIT(0, 10);
	(copl++)->l = CMOVE(par->bplcon0, bplcon0);
	(copl++)->l = CMOVE(0, sprpt[0]);
	(copl++)->l = CMOVE2(0, sprpt[0]);

	if (par->bplcon0 & BPC0_LACE) {
		cops = copdisplay.list[currentcop][0];

		(cops++)->l = CWAIT(0, 10);
		(cops++)->l = CMOVE(par->bplcon0, bplcon0);
		(cops++)->l = CMOVE(0, sprpt[0]);
		(cops++)->l = CMOVE2(0, sprpt[0]);

		(copl++)->l = CMOVE(diwstrt2hw(par->diwstrt_h, par->diwstrt_v+1), diwstrt);
		(copl++)->l = CMOVE(diwstop2hw(par->diwstop_h, par->diwstop_v+1), diwstop);
		(cops++)->l = CMOVE(diwstrt2hw(par->diwstrt_h, par->diwstrt_v), diwstrt);
		(cops++)->l = CMOVE(diwstop2hw(par->diwstop_h, par->diwstop_v), diwstop);
		if (!IS_OCS) {
			(copl++)->l = CMOVE(diwhigh2hw(par->diwstrt_h, par->diwstrt_v+1,
			                    par->diwstop_h, par->diwstop_v+1), diwhigh);
			(cops++)->l = CMOVE(diwhigh2hw(par->diwstrt_h, par->diwstrt_v,
			                    par->diwstop_h, par->diwstop_v), diwhigh);
#if 0
			if (par->beamcon0 & BMC0_VARBEAMEN) {
				(copl++)->l = CMOVE(vtotal2hw(par->vtotal), vtotal);
				(copl++)->l = CMOVE(vbstrt2hw(par->vbstrt+1), vbstrt);
				(copl++)->l = CMOVE(vbstop2hw(par->vbstop+1), vbstop);
				(cops++)->l = CMOVE(vtotal2hw(par->vtotal), vtotal);
				(cops++)->l = CMOVE(vbstrt2hw(par->vbstrt), vbstrt);
				(cops++)->l = CMOVE(vbstop2hw(par->vbstop), vbstop);
			}
#endif
		}
		p = ZTWO_PADDR(copdisplay.list[currentcop][0]);
		(copl++)->l = CMOVE(highw(p), cop2lc);
		(copl++)->l = CMOVE2(loww(p), cop2lc);
		p = ZTWO_PADDR(copdisplay.list[currentcop][1]);
		(cops++)->l = CMOVE(highw(p), cop2lc);
		(cops++)->l = CMOVE2(loww(p), cop2lc);
		copdisplay.rebuild[0] = cops;
	} else {
		(copl++)->l = CMOVE(diwstrt2hw(par->diwstrt_h, par->diwstrt_v), diwstrt);
		(copl++)->l = CMOVE(diwstop2hw(par->diwstop_h, par->diwstop_v), diwstop);
		if (!IS_OCS) {
			(copl++)->l = CMOVE(diwhigh2hw(par->diwstrt_h, par->diwstrt_v,
			                    par->diwstop_h, par->diwstop_v), diwhigh);
#if 0
			if (par->beamcon0 & BMC0_VARBEAMEN) {
				(copl++)->l = CMOVE(vtotal2hw(par->vtotal), vtotal);
				(copl++)->l = CMOVE(vbstrt2hw(par->vbstrt), vbstrt);
				(copl++)->l = CMOVE(vbstop2hw(par->vbstop), vbstop);
			}
#endif
		}
	}
	copdisplay.rebuild[1] = copl;

	ami_update_par();
	ami_rebuild_copper();
}

	/*
	 * Rebuild the Copper List
	 *
	 * We only change the things that are not static
	 */

static void ami_rebuild_copper(void)
{
	struct amiga_fb_par *par = &currentpar;
	copins *copl, *cops;
	u_short line, h_end1, h_end2;
	short i;
	u_long p;

	if (IS_AGA && maxfmode + par->clk_shift == 0)
		h_end1 = par->diwstrt_h-64;
	else
		h_end1 = par->htotal-32;
	h_end2 = par->ddfstop+64;

	ami_set_sprite();

	copl = copdisplay.rebuild[1];
	p = par->bplpt0;
	if (par->vmode & FB_VMODE_YWRAP) {
		if ((par->vyres-par->yoffset) != 1 || !mod2(par->diwstrt_v)) {
			if (par->yoffset > par->vyres-par->yres) {
				for (i = 0; i < (short)par->bpp; i++, p += par->next_plane) {
					(copl++)->l = CMOVE(highw(p), bplpt[i]);
					(copl++)->l = CMOVE2(loww(p), bplpt[i]);
				}
				line = par->diwstrt_v + ((par->vyres-par->yoffset)<<par->line_shift) - 1;
				while (line >= 512) {
					(copl++)->l = CWAIT(h_end1, 510);
					line -= 512;
				}
				if (line >= 510 && IS_AGA && maxfmode + par->clk_shift == 0)
					(copl++)->l = CWAIT(h_end1, line);
				else
					(copl++)->l = CWAIT(h_end2, line);
				p = par->bplpt0wrap;
			}
		} else p = par->bplpt0wrap;
	}
	for (i = 0; i < (short)par->bpp; i++, p += par->next_plane) {
		(copl++)->l = CMOVE(highw(p), bplpt[i]);
		(copl++)->l = CMOVE2(loww(p), bplpt[i]);
	}
	copl->l = CEND;

	if (par->bplcon0 & BPC0_LACE) {
		cops = copdisplay.rebuild[0];
		p = par->bplpt0;
		if (mod2(par->diwstrt_v))
			p -= par->next_line;
		else
			p += par->next_line;
		if (par->vmode & FB_VMODE_YWRAP) {
			if ((par->vyres-par->yoffset) != 1 || mod2(par->diwstrt_v)) {
				if (par->yoffset > par->vyres-par->yres+1) {
					for (i = 0; i < (short)par->bpp; i++, p += par->next_plane) {
						(cops++)->l = CMOVE(highw(p), bplpt[i]);
						(cops++)->l = CMOVE2(loww(p), bplpt[i]);
					}
					line = par->diwstrt_v + ((par->vyres-par->yoffset)<<par->line_shift) - 2;
					while (line >= 512) {
						(cops++)->l = CWAIT(h_end1, 510);
						line -= 512;
					}
					if (line > 510 && IS_AGA && maxfmode + par->clk_shift == 0)
						(cops++)->l = CWAIT(h_end1, line);
					else
						(cops++)->l = CWAIT(h_end2, line);
					p = par->bplpt0wrap;
					if (mod2(par->diwstrt_v+par->vyres-par->yoffset))
						p -= par->next_line;
					else
						p += par->next_line;
				}
			} else p = par->bplpt0wrap - par->next_line;
		}
		for (i = 0; i < (short)par->bpp; i++, p += par->next_plane) {
			(cops++)->l = CMOVE(highw(p), bplpt[i]);
			(cops++)->l = CMOVE2(loww(p), bplpt[i]);
		}
		cops->l = CEND;
	}
}
