/*
 * linux/arch/m68k/amiga/amifb.c -- Low level implementation of the Amiga frame
 *                                  buffer device
 *
 *    Copyright (C) 1995 Geert Uytterhoeven
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
 *   -  2 Dec 95: AGA version by Geert Uytterhoeven
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
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
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/bootinfo.h>
#include <linux/fb.h>


#undef  CONFIG_AMIFB_OCS
#undef  CONFIG_AMIFB_ECS
#define CONFIG_AMIFB_AGA   /* Only AGA support at the moment */

#define USE_MONO_AMIFB_IF_NON_AGA


/* -------------------- BEGIN: TODO ----------------------------------------- **


   - scan the sources for `TODO'

   - timings and monspecs can be set via the kernel command line (cfr. Atari)

   - OCS and ECS

   - hardware cursor

   - Interlaced screen -> Interlaced sprite/hardware cursor


** -------------------- END: TODO ------------------------------------------- */


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
      - vsstop:   Start of vertical synchronization pulse
      - vtotal:   Last line value (i.e. number of lines = vtotal+1)
      - hcenter:  Start of vertical retrace for interlace

   You can specify the blanking timings independently. Currently I just set
   them equal to the respective synchronization values:

      - hbstrt:   Start of horizontal blank
      - hbstop:   End of horizontal blank
      - vbstrt:   Start of vertical blank
      - vbstop:   Start of vertical blank

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
   frame buffer interface, there must be some dependencies among the Amiga display
   parameters. Here's what I found out:

      - ddfstrt and ddfstop are best aligned to 64 pixels.
      - the chipset needs 64+4 horizontal pixels after the DMA start before the
        first pixel is output, so diwstrt_h = ddfstrt+64+4 if you want to
        display the first pixel on the line too. Increase diwstrt_h for virtual
        screen panning.
      - the display DMA always fetches 64 pixels at a time (*).
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

   (*) This is for fmode = 3. Lower fmodes allow for more freedom w.r.t. the
       timings, but they limit the maximum display depth, and cause more stress
       on the custom chip bus.


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

   aga_decode_var() converts the frame buffer values to the Amiga values. It's
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

   Since broadcast video timings are `fixed' and only depend on the video
   system (PAL/NTSC), hsync_len and vsync_len are not used and must be set to
   zero. All xres/yres and margin values are defined within the `visible
   rectangle' of the display.

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
      - make sure upper_margin+yres+lower_margin = 576 for an interlaced, 288
        for a non-interlaced and 144 for a doublescanned display.
      - make sure (left_margin+xres+right_margin)*pixclock is a reasonable
        approximation to 52.48 탎.

   The settings for a NTSC compatible display are straightforward.

   Note that in a strict sense the PAL and NTSC standards only define the
   encoding of the color part (chrominance) of the video signal and don't say
   anything about horizontal/vertical synchronization nor refresh rates.
   But since Amigas have RGB output, this issue isn't of any importance here.


                                                            -- Geert --

*******************************************************************************/


   /*
    *    Custom Chipset Definitions
    */

#define CUSTOM_OFS(fld) ((long)&((struct CUSTOM*)0)->fld)


   /*
    *    BPLCON0 -- Bitplane Control Register 0
    */

#define BPC0_HIRES      (0x8000)
#define BPC0_BPU2       (0x4000) /* Bit plane used count */
#define BPC0_BPU1       (0x2000)
#define BPC0_BPU0       (0x1000)
#define BPC0_HAM        (0x0800) /* HAM mode */
#define BPC0_DPF        (0x0400) /* Double playfield */
#define BPC0_COLOR      (0x0200) /* Enable colorburst */
#define BPC0_GAUD       (0x0100) /* Genlock audio enable */
#define BPC0_UHRES      (0x0080) /* Ultrahi res enable */
#define BPC0_SHRES      (0x0040) /* Super hi res mode */
#define BPC0_BYPASS     (0x0020) /* Bypass LUT - AGA */
#define BPC0_BPU3       (0x0010) /* AGA */
#define BPC0_LPEN       (0x0008) /* Light pen enable */
#define BPC0_LACE       (0x0004) /* Interlace */
#define BPC0_ERSY       (0x0002) /* External resync */
#define BPC0_ECSENA     (0x0001) /* ECS emulation disable */


   /*
    *    BPLCON2 -- Bitplane Control Register 2
    */

#define BPC2_ZDBPSEL2   (0x4000) /* Bitplane to be used for ZD - AGA */
#define BPC2_ZDBPSEL1   (0x2000)
#define BPC2_ZDBPSEL0   (0x1000)
#define BPC2_ZDBPEN     (0x0800) /* Enable ZD with ZDBPSELx - AGA */
#define BPC2_ZDCTEN     (0x0400) /* Enable ZD with palette bit #31 - AGA */
#define BPC2_KILLEHB    (0x0200) /* Kill EHB mode - AGA */
#define BPC2_RDRAM      (0x0100) /* Color table accesses read, not write - AGA */
#define BPC2_SOGEN      (0x0080) /* SOG output pin high - AGA */
#define BPC2_PF2PRI     (0x0040) /* PF2 priority over PF1 */
#define BPC2_PF2P2      (0x0020) /* PF2 priority wrt sprites */
#define BPC2_PF2P1      (0x0010)
#define BPC2_PF2P0      (0x0008)
#define BPC2_PF1P2      (0x0004) /* ditto PF1 */
#define BPC2_PF1P1      (0x0002)
#define BPC2_PF1P0      (0x0001)


   /*
    *    BPLCON3 -- Bitplane Control Register 3 (AGA)
    */

#define BPC3_BANK2      (0x8000) /* Bits to select color register bank */
#define BPC3_BANK1      (0x4000)
#define BPC3_BANK0      (0x2000)
#define BPC3_PF2OF2     (0x1000) /* Bits for color table offset when PF2 */
#define BPC3_PF2OF1     (0x0800)
#define BPC3_PF2OF0     (0x0400)
#define BPC3_LOCT       (0x0200) /* Color register writes go to low bits */
#define BPC3_SPRES1     (0x0080) /* Sprite resolution bits */
#define BPC3_SPRES0     (0x0040)
#define BPC3_BRDRBLNK   (0x0020) /* Border blanked? */
#define BPC3_BRDRTRAN   (0x0010) /* Border transparent? */
#define BPC3_ZDCLKEN    (0x0004) /* ZD pin is 14 MHz (HIRES) clock output */
#define BPC3_BRDRSPRT   (0x0002) /* Sprites in border? */
#define BPC3_EXTBLKEN   (0x0001) /* BLANK programmable */


   /*
    *    BPLCON4 -- Bitplane Control Register 4 (AGA)
    */

#define BPC4_BPLAM7     (0x8000) /* bitplane color XOR field */
#define BPC4_BPLAM6     (0x4000)
#define BPC4_BPLAM5     (0x2000)
#define BPC4_BPLAM4     (0x1000)
#define BPC4_BPLAM3     (0x0800)
#define BPC4_BPLAM2     (0x0400)
#define BPC4_BPLAM1     (0x0200)
#define BPC4_BPLAM0     (0x0100)
#define BPC4_ESPRM7     (0x0080) /* 4 high bits for even sprite colors */
#define BPC4_ESPRM6     (0x0040)
#define BPC4_ESPRM5     (0x0020)
#define BPC4_ESPRM4     (0x0010)
#define BPC4_OSPRM7     (0x0008) /* 4 high bits for odd sprite colors */
#define BPC4_OSPRM6     (0x0004)
#define BPC4_OSPRM5     (0x0002)
#define BPC4_OSPRM4     (0x0001)


   /*
    *    BEAMCON0 -- Beam Control Register
    */

#define BMC0_HARDDIS    (0x4000) /* Disable hardware limits */
#define BMC0_LPENDIS    (0x2000) /* Disable light pen latch */
#define BMC0_VARVBEN    (0x1000) /* Enable variable vertical blank */
#define BMC0_LOLDIS     (0x0800) /* Disable long/short line toggle */
#define BMC0_CSCBEN     (0x0400) /* Composite sync/blank */
#define BMC0_VARVSYEN   (0x0200) /* Enable variable vertical sync */
#define BMC0_VARHSYEN   (0x0100) /* Enable variable horizontal sync */
#define BMC0_VARBEAMEN  (0x0080) /* Enable variable beam counters */
#define BMC0_DUAL       (0x0080) /* Enable alternate horizontal beam counter */
#define BMC0_PAL        (0x0020) /* Set decodes for PAL */
#define BMC0_VARCSYEN   (0x0010) /* Enable variable composite sync */
#define BMC0_BLANKEN    (0x0008) /* Blank enable (no longer used on AGA) */
#define BMC0_CSYTRUE    (0x0004) /* CSY polarity */
#define BMC0_VSYTRUE    (0x0002) /* VSY polarity */
#define BMC0_HSYTRUE    (0x0001) /* HSY polarity */


   /*
    *    FMODE -- Fetch Mode Control Register (AGA)
    */

#define FMODE_SSCAN2    (0x8000) /* Sprite scan-doubling */
#define FMODE_BSCAN2    (0x4000) /* Use PF2 modulus every other line */
#define FMODE_SPAGEM    (0x0008) /* Sprite page mode */
#define FMODE_SPR32     (0x0004) /* Sprite 32 bit fetch */
#define FMODE_BPAGEM    (0x0002) /* Bitplane page mode */
#define FMODE_BPL32     (0x0001) /* Bitplane 32 bit fetch */


   /*
    *    Tags used to indicate a specific Pixel Clock
    *
    *    clk_shift is the shift value to get the timings in 35 ns units
    */

#define TAG_SHRES       (1)      /* SHRES, clk_shift = TAG_SHRES-1 */
#define TAG_HIRES       (2)      /* HIRES, clk_shift = TAG_HIRES-1 */
#define TAG_LORES       (3)      /* LORES, clk_shift = TAG_LORES-1 */


   /*
    *    Clock Definitions, Maximum Display Depth
    *
    *    These depend on the E-Clock or the Chipset, so they are filled in
    *    dynamically
    */

static u_long pixclock[3];       /* SHRES/HIRES/LORES: index = clk_shift */
static u_short maxdepth[3];      /* SHRES/HIRES/LORES: index = clk_shift */


   /*
    *    Broadcast Video Timings
    *
    *    Horizontal values are in 35 ns (SHRES) units
    *    Vertical values are in non-interlaced scanlines
    */

#define PAL_WINDOW_H    (1472)   /* PAL Window Limits */
#define PAL_WINDOW_V    (288)
#define PAL_DIWSTRT_H   (360)
#define PAL_DIWSTRT_V   (24)

#define NTSC_WINDOW_H   (1472)   /* NTSC Window Limits */
#define NTSC_WINDOW_V   (242)
#define NTSC_DIWSTRT_H  (360)
#define NTSC_DIWSTRT_V  (20)

#define PAL_HTOTAL      (1816)   /* Total line length */
#define NTSC_HTOTAL     (1816)   /* Needed for YWRAP */


   /*
    *    Monitor Specifications
    *
    *    These are typical for a `generic' Amiga monitor (e.g. A1960)
    */

static long vfmin = 50, vfmax = 90, hfmin = 15000, hfmax = 38000;

static u_short pwrsave = 0;      /* VESA suspend mode (not for PAL/NTSC) */


   /*
    *    Various macros
    */

#define up8(x)          (((x)+7) & ~7)
#define down8(x)        ((x) & ~7)
#define div8(x)         ((x)>>3)
#define mod8(x)         ((x) & 7)

#define up16(x)         (((x)+15) & ~15)
#define down16(x)       ((x) & ~15)
#define div16(x)        ((x)>>4)
#define mod16(x)        ((x) & 15)

#define up32(x)         (((x)+31) & ~31)
#define down32(x)       ((x) & ~31)
#define div32(x)        ((x)>>5)
#define mod32(x)        ((x) & 31)

#define up64(x)         (((x)+63) & ~63)
#define down64(x)       ((x) & ~63)
#define div64(x)        ((x)>>6)
#define mod64(x)        ((x) & 63)

#define min(a, b)       ((a) < (b) ? (a) : (b))
#define max(a, b)       ((a) > (b) ? (a) : (b))

#define highw(x)        ((u_long)(x)>>16 & 0xffff)
#define loww(x)         ((u_long)(x) & 0xffff)

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))


   /*
    *    Chip RAM we reserve for the Frame Buffer (must be a multiple of 4K!)
    *
    *    This defines the Maximum Virtual Screen Size
    */

#define VIDEOMEMSIZE_AGA_2M   (1310720)   /* AGA (2MB) : max 1280*1024*256 */
#define VIDEOMEMSIZE_AGA_1M    (393216)   /* AGA (1MB) : max 1024*768*256 */
#define VIDEOMEMSIZE_ECS_2M    (655360)   /* ECS (2MB) : max 1280*1024*16 */
#define VIDEOMEMSIZE_ECS_1M    (393216)   /* ECS (1MB) : max 1024*768*16 */
#define VIDEOMEMSIZE_OCS       (262144)   /* OCS       : max ca. 800*600*16 */


static u_long videomemory;
static u_long videomemorysize;

#define assignchunk(name, type, ptr, size) \
{ \
   (name) = (type)(ptr); \
   ptr += size; \
}


   /*
    *    Copper Instructions
    */

#define CMOVE(val, reg)       (CUSTOM_OFS(reg)<<16 | (val))
#define CMOVE2(val, reg)      ((CUSTOM_OFS(reg)+2)<<16 | (val))
#define CWAIT(x, y)           (((y) & 0xff)<<24 | ((x) & 0xfe)<<16 | 0x0001fffe)
#define CEND                  (0xfffffffe)


typedef union {
   u_long l;
   u_short w[2];
} copins;


   /*
    *    Frame Header Copper List
    */

struct clist_hdr {
   copins bplcon0;
   copins diwstrt;
   copins diwstop;
   copins diwhigh;
   copins sprfix[8];
   copins sprstrtup[16];
   copins wait;
   copins jump;
   copins wait_forever;
};


   /*
    *    Long Frame/Short Frame Copper List
    */

struct clist_dyn {
   copins diwstrt;
   copins diwstop;
   copins diwhigh;
   copins bplcon0;
   copins sprpt[2];              /* Sprite 0 */
   copins rest[64];
};


static struct clist_hdr *clist_hdr;
static struct clist_dyn *clist_lof;
static struct clist_dyn *clist_shf;    /* Only used for Interlace */


   /*
    *    Hardware Cursor
    */

#define CRSR_RATE       (20)     /* Number of frames/flash toggle */

static u_long *lofsprite, *shfsprite, *dummysprite;
static u_short cursormode = FB_CURSOR_FLASH;


   /*
    *    Current Video Mode
    */

struct amiga_fb_par {

   /* General Values */

   int xres;                     /* vmode */
   int yres;                     /* vmode */
   int vxres;                    /* vmode */
   int vyres;                    /* vmode */
   int xoffset;                  /* vmode */
   int yoffset;                  /* vmode */
   u_short bpp;                  /* vmode */
   u_short clk_shift;            /* vmode */
   int vmode;                    /* vmode */
   u_short diwstrt_h;            /* vmode */
   u_short diwstrt_v;            /* vmode */
   u_long next_line;             /* modulo for next line */
   u_long next_plane;            /* modulo for next plane */
   short crsr_x;                 /* movecursor */
   short crsr_y;                 /* movecursor */

   /* OCS Hardware Registers */

   u_long bplpt0;                /* vmode, pan (Note: physical address) */
   u_short bplcon0;              /* vmode */
   u_short bplcon1;              /* vmode, pan */
   u_short bpl1mod;              /* vmode, pan */
   u_short bpl2mod;              /* vmode, pan */
   u_short diwstrt;              /* vmode */
   u_short diwstop;              /* vmode */
   u_short ddfstrt;              /* vmode, pan */
   u_short ddfstop;              /* vmode, pan */

#if defined(CONFIG_AMIFB_ECS) || defined(CONFIG_AMIFB_AGA)
   /* Additional ECS Hardware Registers */

   u_short diwhigh;              /* vmode */
   u_short bplcon3;              /* vmode */
   u_short beamcon0;             /* vmode */
   u_short htotal;               /* vmode */
   u_short hsstrt;               /* vmode */
   u_short hsstop;               /* vmode */
   u_short vtotal;               /* vmode */
   u_short vsstrt;               /* vmode */
   u_short vsstop;               /* vmode */
   u_short hcenter;              /* vmode */
#endif /* defined(CONFIG_AMIFB_ECS) || defined(CONFIG_AMIFB_AGA) */

#if defined(CONFIG_AMIFB_AGA)
   /* Additional AGA Hardware Registers */

   u_short fmode;                /* vmode */
#endif /* defined(CONFIG_AMIFB_AGA) */
};

static struct amiga_fb_par current_par;

static int current_par_valid = 0;
static int currcon = 0;

static struct display disp[MAX_NR_CONSOLES];
static struct fb_info fb_info;

static int node;        /* node of the /dev/fb?current file */


   /*
    *    The minimum period for audio depends on htotal (for OCS/ECS/AGA)
    *    (Imported from arch/m68k/amiga/amisound.c)
    */

extern volatile u_short amiga_audio_min_period;


   /*
    *    Since we can't read the palette on OCS/ECS, and since reading one
    *    single color palette entry requires 5 expensive custom chip bus
    *    accesses on AGA, we keep a copy of the current palette.
    */

#ifdef CONFIG_AMIFB_AGA
static struct { u_char red, green, blue, pad; } palette[256];
#else /* CONFIG_AMIFB_AGA */
static struct { u_char red, green, blue, pad; } palette[32];
#endif /* CONFIG_AMIFB_AGA */


   /*
    *    Latches and Flags for display changes during VBlank
    */

static volatile u_short do_vmode = 0;           /* Change the Video Mode */
static volatile short do_blank = 0;             /* (Un)Blank the Screen (�1) */
static volatile u_short do_movecursor = 0;      /* Move the Cursor */
static volatile u_short full_vmode_change = 1;  /* Full Change or Only Pan */
static volatile u_short is_blanked = 0;         /* Screen is Blanked */


   /*
    *    Switch for Chipset Independency
    */

static struct fb_hwswitch {

   /* Initialization */
   int (*init)(void);

   /* Display Control */
   int (*encode_fix)(struct fb_fix_screeninfo *fix, struct amiga_fb_par *par);
   int (*decode_var)(struct fb_var_screeninfo *var, struct amiga_fb_par *par);
   int (*encode_var)(struct fb_var_screeninfo *var, struct amiga_fb_par *par);
   int (*getcolreg)(u_int regno, u_int *red, u_int *green, u_int *blue,
                    u_int *transp);
   int (*setcolreg)(u_int regno, u_int red, u_int green, u_int blue,
                    u_int transp);
   int (*pan_display)(struct fb_var_screeninfo *var, struct amiga_fb_par *par);

   /* Routines Called by VBlank Interrupt to minimize flicker */
   void (*do_vmode)(void);
   void (*do_blank)(int blank);
   void (*do_movecursor)(void);
   void (*do_flashcursor)(void);
} *fbhw;


   /*
    *    Frame Buffer Name
    *
    *    The rest of the name is filled in by amiga_fb_init
    */

static char amiga_fb_name[16] = "Amiga ";


   /*
    *    Predefined Video Mode Names
    *
    *    The a2024-?? modes don't work yet because there's no A2024 driver.
    */

static char *amiga_fb_modenames[] = {

   /*
    *    Autodetect (Default) Video Mode
    */

   "default",

   /*
    *    AmigaOS Video Modes
    */
    
   "ntsc",              /* 640x200, 15 kHz, 60 Hz (NTSC) */
   "ntsc-lace",         /* 640x400, 15 kHz, 60 Hz interlaced (NTSC) */
   "pal",               /* 640x256, 15 kHz, 50 Hz (PAL) */
   "pal-lace",          /* 640x512, 15 kHz, 50 Hz interlaced (PAL) */
   "multiscan",         /* 640x480, 29 kHz, 57 Hz */
   "multiscan-lace",    /* 640x960, 29 kHz, 57 Hz interlaced */
   "a2024-10",          /* 1024x800, 10 Hz (Not yet supported) */
   "a2024-15",          /* 1024x800, 15 Hz (Not yet supported) */
   "euro36",            /* 640x200, 15 kHz, 72 Hz */
   "euro36-lace",       /* 640x400, 15 kHz, 72 Hz interlaced */
   "euro72",            /* 640x400, 29 kHz, 68 Hz */
   "euro72-lace",       /* 640x800, 29 kHz, 68 Hz interlaced */
   "super72",           /* 800x300, 23 kHz, 70 Hz */
   "super72-lace",      /* 800x600, 23 kHz, 70 Hz interlaced */
   "dblntsc",           /* 640x200, 27 kHz, 57 Hz doublescan */
   "dblntsc-ff",        /* 640x400, 27 kHz, 57 Hz */
   "dblntsc-lace",      /* 640x800, 27 kHz, 57 Hz interlaced */
   "dblpal",            /* 640x256, 27 kHz, 47 Hz doublescan */
   "dblpal-ff",         /* 640x512, 27 kHz, 47 Hz */
   "dblpal-lace",       /* 640x1024, 27 kHz, 47 Hz interlaced */

   /*
    *    VGA Video Modes
    */

   "vga",               /* 640x480, 31 kHz, 60 Hz (VGA) */
   "vga70",             /* 640x400, 31 kHz, 70 Hz (VGA) */

   /*
    *    User Defined Video Modes: to be set after boot up using e.g. fbset
    */

   "user0", "user1", "user2", "user3", "user4", "user5", "user6", "user7"
};


   /*
    *    Predefined Video Mode Definitions
    *
    *    Since the actual pixclock values depend on the E-Clock, we use the
    *    TAG_* values and fill in the real values during initialization.
    *    Thus we assume no one has pixel clocks of 333, 500 or 1000 GHz :-)
    */

static struct fb_var_screeninfo amiga_fb_predefined[] = {

   /*
    *    Autodetect (Default) Video Mode
    */

   { 0, },

   /*
    *    AmigaOS Video Modes
    */
    
   {
      /* ntsc */
      640, 200, 640, 200, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 78, 18, 24, 18, 0, 0,
      FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
   }, {
      /* ntsc-lace */
      640, 400, 640, 400, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 78, 18, 48, 36, 0, 0,
      FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
   }, {
      /* pal */
      640, 256, 640, 256, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 78, 18, 20, 12, 0, 0,
      FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
   }, {
      /* pal-lace */
      640, 512, 640, 512, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 78, 18, 40, 24, 0, 0,
      FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
   }, {
      /* multiscan */
      640, 480, 640, 480, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 164, 92, 9, 9, 80, 8,
      0, FB_VMODE_NONINTERLACED
   }, {
      /* multiscan-lace */
      640, 960, 640, 960, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 164, 92, 18, 18, 80, 16,
      0, FB_VMODE_INTERLACED
   }, {
      /* a2024-10 (Not yet supported) */
      1024, 800, 1024, 800, 0, 0, 2, 0,
      {0, 2, 0}, {0, 2, 0}, {0, 2, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 0, 0, 0, 0, 0, 0,
      0, FB_VMODE_NONINTERLACED
   }, {
      /* a2024-15 (Not yet supported) */
      1024, 800, 1024, 800, 0, 0, 2, 0,
      {0, 2, 0}, {0, 2, 0}, {0, 2, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 0, 0, 0, 0, 0, 0,
      0, FB_VMODE_NONINTERLACED
   }, {
      /* euro36 */
      640, 200, 640, 200, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 92, 124, 6, 6, 52, 5,
      0, FB_VMODE_NONINTERLACED
   }, {
      /* euro36-lace */
      640, 400, 640, 400, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_HIRES, 92, 124, 12, 12, 52, 10,
      0, FB_VMODE_INTERLACED
   }, {
      /* euro72 */
      640, 400, 640, 400, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 164, 92, 9, 9, 80, 8,
      0, FB_VMODE_NONINTERLACED
   }, {
      /* euro72-lace */
      640, 800, 640, 800, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 164, 92, 18, 18, 80, 16,
      0, FB_VMODE_INTERLACED
   }, {
      /* super72 */
      800, 300, 800, 300, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 212, 140, 10, 11, 80, 7,
      0, FB_VMODE_NONINTERLACED
   }, {
      /* super72-lace */
      800, 600, 800, 600, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 212, 140, 20, 22, 80, 14,
      0, FB_VMODE_INTERLACED
   }, {
      /* dblntsc */
      640, 200, 640, 200, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 18, 17, 80, 4,
      0, FB_VMODE_DOUBLE
   }, {
      /* dblntsc-ff */
      640, 400, 640, 400, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 36, 35, 80, 7,
      0, FB_VMODE_NONINTERLACED
   }, {
      /* dblntsc-lace */
      640, 800, 640, 800, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 72, 70, 80, 14,
      0, FB_VMODE_INTERLACED
   }, {
      /* dblpal */
      640, 256, 640, 256, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 14, 13, 80, 4,
      0, FB_VMODE_DOUBLE
   }, {
      /* dblpal-ff */
      640, 512, 640, 512, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 28, 27, 80, 7,
      0, FB_VMODE_NONINTERLACED
   }, {
      /* dblpal-lace */
      640, 1024, 640, 1024, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 196, 124, 56, 54, 80, 14,
      0, FB_VMODE_INTERLACED
   },

   /*
    *    VGA Video Modes
    */

   {
      /* vga */
      640, 480, 640, 480, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 64, 96, 30, 9, 112, 2,
      0, FB_VMODE_NONINTERLACED
   }, {
      /* vga70 */
      640, 400, 640, 400, 0, 0, 4, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, 0, -1, -1, FB_ACCEL_NONE, TAG_SHRES, 64, 96, 35, 12, 112, 2,
      FB_SYNC_VERT_HIGH_ACT | FB_SYNC_COMP_HIGH_ACT, FB_VMODE_NONINTERLACED
   },

   /*
    *    User Defined Video Modes
    */

   { 0, }, { 0, }, { 0, }, { 0, }, { 0, }, { 0, }, { 0, }, { 0, }
};


#define NUM_USER_MODES     (8)
#define NUM_TOTAL_MODES    arraysize(amiga_fb_predefined)
#define NUM_PREDEF_MODES   (NUM_TOTAL_MODES-NUM_USER_MODES)


static int amifb_ilbm = 0;       /* interleaved or normal bitplanes */

static int amifb_inverse = 0;
static int amifb_mode = 0;


   /*
    *    Support for Graphics Boards
    */

#ifdef CONFIG_FB_CYBER        /* Cybervision */
extern int Cyber_probe(void);
extern void Cyber_video_setup(char *options, int *ints);
extern struct fb_info *Cyber_fb_init(long *mem_start);

static int amifb_Cyber = 0;
#endif /* CONFIG_FB_CYBER */


   /*
    *    Some default modes
    */

#define DEFMODE_PAL        "pal"       /* for PAL OCS/ECS */
#define DEFMODE_NTSC       "ntsc"      /* for NTSC OCS/ECS */
#define DEFMODE_AMBER_PAL  "pal-lace"  /* for flicker fixed PAL (A3000) */
#define DEFMODE_AMBER_NTSC "ntsc-lace" /* for flicker fixed NTSC (A3000) */
#define DEFMODE_AGA        "vga70"     /* for AGA */


   /*
    *    Interface used by the world
    */

void amiga_video_setup(char *options, int *ints);

static int amiga_fb_get_fix(struct fb_fix_screeninfo *fix, int con);
static int amiga_fb_get_var(struct fb_var_screeninfo *var, int con);
static int amiga_fb_set_var(struct fb_var_screeninfo *var, int con);
static int amiga_fb_get_cmap(struct fb_cmap *cmap, int kspc, int con);
static int amiga_fb_set_cmap(struct fb_cmap *cmap, int kspc, int con);
static int amiga_fb_pan_display(struct fb_var_screeninfo *var, int con);

static int amiga_fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
                          u_long arg, int con);

static int amiga_fb_get_fix_cursorinfo(struct fb_fix_cursorinfo *fix, int con);
static int amiga_fb_get_var_cursorinfo(struct fb_var_cursorinfo *var, int con);
static int amiga_fb_set_var_cursorinfo(struct fb_var_cursorinfo *var, int con);
static int amiga_fb_get_cursorstate(struct fb_cursorstate *state, int con);
static int amiga_fb_set_cursorstate(struct fb_cursorstate *state, int con);


   /*
    *    Interface to the low level console driver
    */

struct fb_info *amiga_fb_init(long *mem_start);
static int amifb_switch(int con);
static int amifb_updatevar(int con);
static void amifb_blank(int blank);


   /*
    *    Support for OCS
    */

#ifdef CONFIG_AMIFB_OCS
#error "OCS support: not yet implemented"
#endif /* CONFIG_AMIFB_OCS */


   /*
    *    Support for ECS
    */

#ifdef CONFIG_AMIFB_ECS
#error "ECS support: not yet implemented"
#endif /* CONFIG_AMIFB_ECS */


   /*
    *    Support for AGA
    */

#ifdef CONFIG_AMIFB_AGA
static int aga_init(void);
static int aga_encode_fix(struct fb_fix_screeninfo *fix,
                          struct amiga_fb_par *par);
static int aga_decode_var(struct fb_var_screeninfo *var,
                          struct amiga_fb_par *par);
static int aga_encode_var(struct fb_var_screeninfo *var,
                          struct amiga_fb_par *par);
static int aga_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp);
static int aga_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp);
static int aga_pan_display(struct fb_var_screeninfo *var,
                           struct amiga_fb_par *par);
static void aga_do_vmode(void);
static void aga_do_blank(int blank);
static void aga_do_movecursor(void);
static void aga_do_flashcursor(void);

static int aga_get_fix_cursorinfo(struct fb_fix_cursorinfo *fix, int con);
static int aga_get_var_cursorinfo(struct fb_var_cursorinfo *var, int con);
static int aga_set_var_cursorinfo(struct fb_var_cursorinfo *var, int con);
static int aga_get_cursorstate(struct fb_cursorstate *state, int con);
static int aga_set_cursorstate(struct fb_cursorstate *state, int con);

static __inline__ void aga_build_clist_hdr(struct clist_hdr *cop);
static __inline__ void aga_update_clist_hdr(struct clist_hdr *cop,
                                            struct amiga_fb_par *par);
static void aga_build_clist_dyn(struct clist_dyn *cop,
                                struct clist_dyn *othercop, u_short shf,
                                struct amiga_fb_par *par);
#endif /* CONFIG_AMIFB_AGA */


   /*
    *    Internal routines
    */

static u_long chipalloc(u_long size);
static void amiga_fb_get_par(struct amiga_fb_par *par);
static void amiga_fb_set_par(struct amiga_fb_par *par);
static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive);
static struct fb_cmap *get_default_colormap(int bpp);
static int do_fb_get_cmap(struct fb_cmap *cmap, struct fb_var_screeninfo *var,
                          int kspc);
static int do_fb_set_cmap(struct fb_cmap *cmap, struct fb_var_screeninfo *var,
                          int kspc);
static void do_install_cmap(int con);
static void memcpy_fs(int fsfromto, void *to, void *from, int len);
static void copy_cmap(struct fb_cmap *from, struct fb_cmap *to, int fsfromto);
static int alloc_cmap(struct fb_cmap *cmap, int len, int transp);
static void amiga_fb_set_disp(int con);
static void amifb_interrupt(int irq, struct pt_regs *fp, void *dummy);
static char * strtoke(char * s,const char * ct);
static int get_video_mode(const char *name);
static void check_default_mode(void);


#ifdef USE_MONO_AMIFB_IF_NON_AGA

/******************************************************************************
*
* This is the old monochrome frame buffer device. It's invoked if we're running
* on a non-AGA machine, until the color support for OCS/ECS is finished.
*
******************************************************************************/

/*
 * atari/atafb.c -- Low level implementation of Atari frame buffer device
 * amiga/amifb.c -- Low level implementation of Amiga frame buffer device
 *
 *  Copyright (C) 1994 Martin Schaller & Roman Hodek & Geert Uytterhoeven
 *  
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * History:
 *   - 03 Jan 95: Original version my Martin Schaller: The TT driver and
 *                all the device independent stuff
 *   - 09 Jan 95: Roman: I've added the hardware abstraction (hw_switch)
 *                and wrote the Falcon, ST(E), and External drivers
 *                based on the original TT driver.
 *   - 26 Jan 95: Geert: Amiga version
 *   - 19 Feb 95: Hamish: added Jes Sorensen's ECS patches to the Amiga
 *		  frame buffer device.  This provides ECS support and the
 *		  following screen-modes: multiscan, multiscan-lace,
 * 		  super72, super72-lace, dblntsc, dblpal & euro72.
 *		  He suggests that we remove the old AGA screenmodes,
 *		  as they are non-standard, and some of them doesn't work
 *		  under ECS.
 */

static struct mono_mono_amiga_fb_par {
	u_long smem_start;
	u_long smem_len;
	struct geometry *geometry;
	ushort scr_max_height;			/* screen dimensions */
	ushort scr_max_width;
	ushort scr_height;
	ushort scr_width;
	ushort scr_depth;
	int bytes_per_row;			/* offset to one line below */
	ulong fgcol;
	ulong bgcol;
	ulong crsrcol;
	ushort scroll_latch;			/* Vblank support for hardware scroll */
	ushort y_wrap;
	ushort cursor_latch;			/* Hardware cursor */
	ushort *cursor, *dummy;
	ushort cursor_flash;
	ushort cursor_visible;
	ushort diwstrt_v, diwstrt_h;		/* display window control */
	ushort diwstop_v, diwstop_h;
	ushort bplcon0;				/* display mode */
	ushort htotal;
	u_char *bitplane[8];			/* pointers to display bitplanes */
	ulong plane_size;
	ushort *coplist1hdr;			/* List 1 static  component */
	ushort *coplist1dyn;			/* List 1 dynamic component */
	ushort *coplist2hdr;			/* List 2 static  component */
	ushort *coplist2dyn;			/* List 2 dynamic component */
} mono_current_par;


static ushort mono_cursor_data[] =
{
    0x2c81,0x2d00,
    0xf000,0x0000,
    0x0000,0x0000
};


/*
 *  Color definitions
 */

#define FG_COLOR		(0x000000)	/* black */
#define BG_COLOR		(0xaaaaaa)	/* lt. grey */
#define CRSR_COLOR		(0xff0000)	/* bright red */

#define FG_COLOR_INV		BG_COLOR
#define BG_COLOR_INV		FG_COLOR
#define CRSR_COLOR_INV		(0x6677aa)	/* a blue-ish color */

/*
 *  Split 24-bit RGB colors in 12-bit MSB (for OCS/ECS/AGA) and LSB (for AGA)
 */

#define COLOR_MSB(rgb)	(((rgb>>12)&0xf00)|((rgb>>8)&0x0f0)|((rgb>>4)&0x00f))
#define COLOR_LSB(rgb)	(((rgb>>8) &0xf00)|((rgb>>4)&0x0f0)|((rgb)   &0x00f))

/* Cursor definitions */

#define CRSR_FLASH		1	/* Cursor flashing on(1)/off(0) */
#define CRSR_BLOCK		1	/* Block(1) or line(0) cursor */


/* controlling screen blanking (read in VBL handler) */
static int mono_do_blank;
static int mono_do_unblank;
static unsigned short mono_save_bplcon3;

/*
 * mono_ecs_color_zero is used to keep custom.color[0] for the special ECS color-
 * table, as custom.color[0] is cleared at vblank interrupts.
 * -Jes (jds@kom.auc.dk)
 */

static ushort mono_ecs_color_zero;

static struct {
	int right_count;
	int done;
} mono_vblank;


static __inline__ void mono_init_vblank(void)
{
	mono_vblank.right_count = 0;
	mono_vblank.done = 0;
}

/* Geometry structure contains all useful information about given mode.
 *
 * Strictly speaking `scr_max_height' and `scr_max_width' is redundant
 * information with DIWSTRT value provided. Might be useful if modes
 * can be hotwired by user in future. It fits for the moment.
 *
 * At the moment, the code only distinguishes between OCS and AGA. ECS
 * lies somewhere in between - someone more familiar with it could make
 * appropriate modifications so that some advanced display modes are
 * available, without confusing the poor chipset. OCS modes use only the
 * bplcon0, diwstrt, diwstop, ddfstrt, ddfstop registers (a few others could
 * be used as well). -wjr
 *
 * The code now supports ECS as well, except for FMODE all control registers
 * are the same under ECS. A special color-table has to be generated though.
 * -Jes
 */
struct geometry {
    char *modename;	/* Name this thing */
    char isOCS;		/* Is it OCS or ECS/AGA */
    ushort bplcon0;	/* Values for bit plane control register 0 */
    ushort scr_width;
    ushort scr_height;
    ushort scr_depth;
    ushort scr_max_width;     /* Historical, might be useful still */
    ushort scr_max_height;
    ushort diwstrt_h;	/* Where the display window starts */
    ushort diwstrt_v;
    ushort alignment;	/* Pixels per scanline must be a multiple of this */
    /* OCS doesn't need anything past here */
    ushort bplcon2;
    ushort bplcon3;
    /* The rest of these control variable sync */
    ushort htotal;	/* Total hclocks */
    ushort hsstrt;	/* HSYNC start and stop */
    ushort hsstop;
    ushort hbstrt;	/* HBLANK start and stop */
    ushort hbstop;
    ushort vtotal;	/* Total vlines */
    ushort vsstrt;	/* VSYNC, VBLANK ditto */
    ushort vsstop;
    ushort vbstrt;
    ushort vbstop;
    ushort hcenter;	/* Center of line, for interlaced modes */
    ushort beamcon0;	/* Beam control */
    ushort fmode;	/* Memory fetch mode */
};

#define MAX_COP_LIST_ENTS 64
#define COP_MEM_REQ       (MAX_COP_LIST_ENTS*4*2)
#define SPR_MEM_REQ       (24)


static struct geometry mono_modes[] = {
	/* NTSC modes: !!! can't guarantee anything about overscan modes !!! */
	{
		"ntsc-lace", 1,
		BPC0_HIRES | BPC0_LACE,
		640, 400, 1,
		704, 480,
		0x71, 0x18,		/* diwstrt h,v */
		16			/* WORD aligned */
	}, {
		"ntsc", 1,
		BPC0_HIRES,
		640, 200, 1,
		704, 240,
		0x71, 0x18,
		16			/* WORD aligned */
	}, {
		"ntsc-lace-over", 1,
		BPC0_HIRES | BPC0_LACE,
		704, 480, 1,
		704, 480,
		0x71, 0x18,
		16			/* WORD aligned */
	}, {
		"ntsc-over", 1,
		BPC0_HIRES,
		704, 240, 1,
		704, 240,
		0x71, 0x18,
		16			/* WORD aligned */
	},
	/* PAL modes. Warning:
	 * 
	 * PAL overscan causes problems on my machine because maximum diwstop_h
	 * value seems to be ~0x1c2, rather than 0x1e0+ inferred by RKM 1.1
	 * and 0x1d5 inferred by original `amicon.c' source. Is this a hardware
	 * limitation of OCS/pal or 1084?. Or am I doing something stupid here?
	 *
	 * Included a couple of overscan modes that DO work on my machine,
	 * although not particularly useful.
	 */
	{
		"pal-lace", 1,
		BPC0_HIRES | BPC0_LACE,
		640, 512, 1,
		704, 592,
		0x71, 0x18,
		16			/* WORD aligned */
	}, {
		"pal", 1,
		BPC0_HIRES,
		640, 256, 1,
		704, 296,
		0x71, 0x18,
		16			/* WORD aligned */
	}, {
		"pal-lace-over", 1,
		BPC0_HIRES | BPC0_LACE,
		704, 592, 1,
		704, 582,
		0x5b, 0x18,
		16			/* WORD aligned */
	}, {
		"pal-over", 1,
		BPC0_HIRES,
		704, 296, 1,
		704, 296,
		0x5b, 0x18,
		16			/* WORD aligned */

	},
	/* ECS modes, these are real ECS modes */
	{
		"multiscan", 0,
		BPC0_SHRES | BPC0_ECSENA,	/* bplcon0 */
		640, 480, 1,
		640, 480,
		0x0041, 0x002c,			/* diwstrt h,v */
		64,	        		/* 64-bit aligned */
		BPC2_KILLEHB,			/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
			BPC3_BRDRBLNK | BPC3_EXTBLKEN,	/* bplcon3 */

		0x0072,				/* htotal */
		0x000a,				/* hsstrt */
		0x0013,				/* hsstop */
		0x0002,				/* hbstrt */
		0x001c,				/* hbstop */
		0x020c,				/* vtotal */
		0x0008,				/* vsstrt */
		0x0011,				/* vsstop */
		0x0000,				/* vbstrt */
		0x001c,				/* vbstop */
		0x0043,				/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN |
			BMC0_VARHSYEN | BMC0_VARBEAMEN | BMC0_BLANKEN,
						/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32	/* fmode */
	},
	{
		"multiscan-lace", 0,
		BPC0_SHRES | BPC0_LACE | BPC0_ECSENA,	/* bplcon0 */
		640, 960, 1,
		640, 960,
		0x0041, 0x002c,			/* diwstrt h,v */
		64,	        		/* 64-bit aligned */
		BPC2_KILLEHB,			/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
			BPC3_BRDRBLNK | BPC3_EXTBLKEN,	/* bplcon3 */

		0x0072,				/* htotal */
		0x000a,				/* hsstrt */
		0x0013,				/* hsstop */
		0x0002,				/* hbstrt */
		0x001c,				/* hbstop */
		0x020c,				/* vtotal */
		0x0008,				/* vsstrt */
		0x0011,				/* vsstop */
		0x0000,				/* vbstrt */
		0x001c,				/* vbstop */
		0x0043,				/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN |
			BMC0_VARHSYEN | BMC0_VARBEAMEN | BMC0_BLANKEN,
						/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32	/* fmode */
	},
	/* Super 72 - 800x300 72Hz noninterlaced mode. */
	{
		"super72", 0,
		BPC0_SHRES | BPC0_ECSENA,	/* bplcon0 */
		800, 304, 1,			/* need rows%8 == 0 */
		800, 304,			/* (cols too) */
		0x0051, 0x0021,			/* diwstrt h,v */
		64,				/* 64-bit aligned */
		BPC2_KILLEHB,			/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
			BPC3_BRDRBLNK | BPC3_EXTBLKEN,	/* bplcon3 */
		0x0091,				/* htotal */
		0x000a,				/* hsstrt */
		0x0013,				/* hsstop */
		0x0001,				/* hbstrt */
		0x001e,				/* hbstop */
		0x0156,				/* vtotal */
		0x0009,				/* vsstrt */
		0x0012,				/* vsstop */
		0x0000,				/* vbstrt */
		0x001c,				/* vbstop */
		0x0052,				/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN |
			BMC0_VARHSYEN | BMC0_VARBEAMEN | BMC0_BLANKEN,
						/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32	/* fmode */
	},
	/* Super 72 lace - 800x600 72Hz interlaced mode. */
	{
		"super72-lace", 0,
		BPC0_SHRES | BPC0_LACE | BPC0_ECSENA,	/* bplcon0 */
		800, 600, 1,			/* need rows%8 == 0 */
		800, 600,			/* (cols too) */
		0x0051, 0x0021,			/* diwstrt h,v */
		64,     			/* 64-bit aligned */
		BPC2_KILLEHB,			/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
			BPC3_BRDRBLNK | BPC3_EXTBLKEN,
						/* bplcon3 */
		0x0091,				/* htotal */
		0x000a,				/* hsstrt */
		0x0013,				/* hsstop */
		0x0001,				/* hbstrt */
		0x001e,				/* hbstop */
		0x0150,				/* vtotal */
		0x0009,				/* vsstrt */
		0x0012,				/* vsstop */
		0x0000,				/* vbstrt */
		0x001c,				/* vbstop */
		0x0052,				/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN |
			BMC0_VARHSYEN | BMC0_VARBEAMEN | BMC0_BLANKEN,
						/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32	/* fmode */
	},
	/* DblNtsc - 640x400 59Hz noninterlaced mode. */
	{
		"dblntsc", 0,
		BPC0_SHRES | BPC0_ECSENA,	/* bplcon0 */
		640, 400, 1,			/* need rows%8 == 0 */
		640, 400,			/* (cols too) */
		0x0049, 0x0021,			/* diwstrt h,v */
		64,				/* 64-bit aligned */
		BPC2_KILLEHB,			/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
			BPC3_BRDRBLNK | BPC3_EXTBLKEN,
						/* bplcon3 */
		0x0079,				/* htotal */
		0x0007,				/* hsstrt */
		0x0013,				/* hsstop */
		0x0001,				/* hbstrt */
		0x001e,				/* hbstop */
		0x01ec,				/* vtotal */
		0x0008,				/* vsstrt */
		0x0010,				/* vsstop */
		0x0000,				/* vbstrt */
		0x0019,				/* vbstop */
		0x0046,				/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN |
			BMC0_VARHSYEN | BMC0_VARBEAMEN | BMC0_BLANKEN,
						/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32	/* fmode */
	},
	/* DblPal - 640x512 52Hz noninterlaced mode. */
	{
		"dblpal", 0,
		BPC0_SHRES | BPC0_ECSENA,	/* bplcon0 */
		640, 512, 1,			/* need rows%8 == 0 */
		640, 512,			/* (cols too) */
		0x0049, 0x0021,			/* diwstrt h,v */
		64,				/* 64-bit aligned */
		BPC2_KILLEHB,			/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
			BPC3_BRDRBLNK | BPC3_EXTBLKEN,
						/* bplcon3 */
		0x0079,				/* htotal */
		0x0007,				/* hsstrt */
		0x0013,				/* hsstop */
		0x0001,				/* hbstrt */
		0x001e,				/* hbstop */
		0x0234,				/* vtotal */
		0x0008,				/* vsstrt */
		0x0010,				/* vsstop */
		0x0000,				/* vbstrt */
		0x0019,				/* vbstop */
		0x0046,				/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN |
			BMC0_VARHSYEN | BMC0_VARBEAMEN | BMC0_BLANKEN,
						/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32	/* fmode */
	},
	/* Euro72 - productivity - 640x400 71Hz noninterlaced mode. */
	{
		"euro72", 0,
		BPC0_SHRES | BPC0_ECSENA,	/* bplcon0 */
		640, 400, 1,			/* need rows%8 == 0 */
		640, 400,			/* (cols too) */
		0x0041, 0x0021,			/* diwstrt h,v */
		64,				/* 64-bit aligned */
		BPC2_KILLEHB,			/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
			BPC3_BRDRBLNK | BPC3_EXTBLKEN,
						/* bplcon3 */
		0x0071,				/* htotal */
		0x0009,				/* hsstrt */
		0x0013,				/* hsstop */
		0x0001,				/* hbstrt */
		0x001e,				/* hbstop */
		0x01be,				/* vtotal */
		0x0008,				/* vsstrt */
		0x0016,				/* vsstop */
		0x0000,				/* vbstrt */
		0x001f,				/* vbstop */
		0x0041,				/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN |
			BMC0_VARHSYEN | BMC0_VARBEAMEN | BMC0_BLANKEN,
						/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32	/* fmode */
	},
	/* AGA modes */
	{
	/*
	 * A 640x480, 60Hz noninterlaced AGA mode. It would be nice to be
	 * able to have some of these values computed dynamically, but that
	 * requires more knowledge of AGA than I have. At the moment,
	 * the values make it centered on my 1960 monitor. -wjr
	 *
	 * For random reasons to do with the way arguments are parsed,
	 * these names can't start with a digit.
	 *
	 * Don't count on being able to reduce scr_width and scr_height
	 * and ending up with a smaller but well-formed screen - this
	 * doesn't seem to work well at the moment.
	 */
		"aga640x480", 0,
		BPC0_SHRES | BPC0_ECSENA,			/* bplcon0 */
		640, 480, 1,
		640, 480,
		0x0041, 0x002b,						/* diwstrt h,v */
		64,										/* 64-bit aligned */
		BPC2_KILLEHB,							/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
		BPC3_BRDRBLNK | BPC3_EXTBLKEN,	/* bplcon3 */
		0x0071,									/* htotal */
		0x000c,									/* hsstrt */
		0x001c,									/* hsstop */
		0x0008,									/* hbstrt */
		0x001e,									/* hbstop */
		0x020c,									/* vtotal */
		0x0001,									/* vsstrt */
		0x0003,									/* vsstop */
		0x0000,									/* vbstrt */
		0x000f,									/* vbstop */
		0x0046,									/* hcenter */
		BMC0_HARDDIS | BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN |
			BMC0_VARHSYEN | BMC0_VARBEAMEN | BMC0_BLANKEN,	/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32			/* fmode */
	}, {
		/* An 800x600 72Hz interlaced mode. */
		"aga800x600", 0,
		BPC0_SHRES | BPC0_LACE | BPC0_ECSENA,	/* bplcon0 */
		896, 624, 1,							/* need rows%8 == 0 */
		896, 624,								/* (cols too) */
		0x0041, 0x001e,						/* diwstrt h,v */
		64,						/* 64-bit aligned */
		BPC2_KILLEHB,							/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
		BPC3_BRDRBLNK | BPC3_EXTBLKEN,	/* bplcon3 */
		0x0091,									/* htotal */
		0x000e,									/* hsstrt */
		0x001d,									/* hsstop */
		0x000a,									/* hbstrt */
		0x001e,									/* hbstop */
		0x0156,									/* vtotal */
		0x0001,									/* vsstrt */
		0x0003,									/* vsstop */
		0x0000,									/* vbstrt */
		0x000f,									/* vbstop */
		0x0050,									/* hcenter */
		BMC0_HARDDIS | BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN |
		BMC0_VARHSYEN | BMC0_VARBEAMEN | BMC0_BLANKEN,	/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32			/* fmode */
	},
	/*
	 * Additional AGA modes by Geert Uytterhoeven
	 */
	{
		/*
		 * A 720x400, 70 Hz noninterlaced AGA mode (29.27 kHz)
		 */
		"aga720x400", 0,
		BPC0_SHRES | BPC0_ECSENA,			/* bplcon0 */
		720, 400, 1,
		720, 400,
		0x0041, 0x0013,						/* diwstrt h,v */
		64,										/* 64-bit aligned */
		BPC2_KILLEHB,							/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
		BPC3_BRDRBLNK | BPC3_EXTBLKEN,	/* bplcon3 */
		0x0079,									/* htotal */
		0x000e,									/* hsstrt */
		0x0018,									/* hsstop */
		0x0001,									/* hbstrt */
		0x0021,									/* hbstop */
		0x01a2,									/* vtotal */
		0x0003,									/* vsstrt */
		0x0005,									/* vsstop */
		0x0000,									/* vbstrt */
		0x0012,									/* vbstop */
		0x0046,									/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN | BMC0_VARHSYEN |
		BMC0_VARBEAMEN | BMC0_PAL | BMC0_VARCSYEN | BMC0_CSYTRUE |
		BMC0_VSYTRUE,							/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32			/* fmode */
	}, {
		/*
		 * A 640x400, 76 Hz noninterlaced AGA mode (31.89 kHz)
		 */
		"aga640x400", 0,
		BPC0_SHRES | BPC0_ECSENA,			/* bplcon0 */
		640, 400, 1,
		640, 400,
		0x0041, 0x0015,						/* diwstrt h,v */
		64,										/* 64-bit aligned */
		BPC2_KILLEHB,							/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
		BPC3_BRDRBLNK | BPC3_EXTBLKEN,	/* bplcon3 */
		0x006f,									/* htotal */
		0x000d,									/* hsstrt */
		0x0018,									/* hsstop */
		0x0001,									/* hbstrt */
		0x0021,									/* hbstop */
		0x01a4,									/* vtotal */
		0x0003,									/* vsstrt */
		0x0005,									/* vsstop */
		0x0000,									/* vbstrt */
		0x0014,									/* vbstop */
		0x0046,									/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN | BMC0_VARHSYEN |
		BMC0_VARBEAMEN | BMC0_PAL | BMC0_VARCSYEN | BMC0_CSYTRUE |
		BMC0_VSYTRUE,							/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32			/* fmode */
	}, {
		/*
		 * A 640x480, 64 Hz noninterlaced AGA mode (31.89 kHz)
		 */
		"aga640x480a", 0,
		BPC0_SHRES | BPC0_ECSENA,			/* bplcon0 */
		640, 480, 1,
		640, 480,
		0x0041, 0x0015,						/* diwstrt h,v */
		64,										/* 64-bit aligned */
		BPC2_KILLEHB,							/* bplcon2 */
		BPC3_PF2OF1 | BPC3_PF2OF0 | BPC3_SPRES1 | BPC3_SPRES0 |
		BPC3_BRDRBLNK | BPC3_EXTBLKEN,	/* bplcon3 */
		0x006f,									/* htotal */
		0x000e,									/* hsstrt */
		0x0018,									/* hsstop */
		0x0001,									/* hbstrt */
		0x0021,									/* hbstop */
		0x01f4,									/* vtotal */
		0x0003,									/* vsstrt */
		0x0005,									/* vsstop */
		0x0000,									/* vbstrt */
		0x0014,									/* vbstop */
		0x0046,									/* hcenter */
		BMC0_VARVBEN | BMC0_LOLDIS | BMC0_VARVSYEN | BMC0_VARHSYEN |
		BMC0_VARBEAMEN | BMC0_PAL | BMC0_VARCSYEN | BMC0_CSYTRUE |
		BMC0_VSYTRUE,							/* beamcon0 */
		FMODE_BPAGEM | FMODE_BPL32			/* fmode */
	}
};

#define	NMODES	(sizeof(mono_modes) / sizeof(struct geometry))

static struct fb_var_screeninfo mono_mono_amiga_fb_predefined[] = {
	{ /* autodetect */
		0, 0, 0, 0, 0, 0, 0, 0,   		/* xres-grayscale */
		{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 	/* red green blue tran*/
		0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static int mono_num_mono_amiga_fb_predefined= sizeof(mono_mono_amiga_fb_predefined)/sizeof(struct fb_var_screeninfo);



/* Some default modes */
#define OCS_PAL_LOWEND_DEFMODE	5	/* PAL non-laced for 500/2000 */
#define OCS_PAL_3000_DEFMODE		4	/* PAL laced for 3000 */
#define OCS_NTSC_LOWEND_DEFMODE	1	/* NTSC non-laced for 500/2000 */
#define OCS_NTSC_3000_DEFMODE		0	/* NTSC laced for 3000 */
#define AGA_DEFMODE					8	/* 640x480 non-laced for AGA */

static int mono_amifb_inverse = 0;
static int mono_amifb_mode = -1;

static void mono_video_setup (char *options, int *ints)
{
	char *this_opt;
	int i;

	fb_info.fontname[0] = '\0';

	if (!options || !*options)
		return;

	for (this_opt = strtok(options,","); this_opt; this_opt = strtok(NULL,","))
		if (!strcmp (this_opt, "inverse"))
			mono_amifb_inverse = 1;
      else if (!strncmp(this_opt, "font:", 5))
               strcpy(fb_info.fontname, this_opt+5);
		else
			for (i = 0; i < NMODES; i++)
				if (!strcmp(this_opt, mono_modes[i].modename)) {
					mono_amifb_mode = i;
					break;
				}
}

/* Notes about copper scrolling:
 *
 * 1. The VBLANK routine dynamically rewrites a LIVE copper list that is
 *    currently being executed. Don't mess with it unless you know the
 *    complications. Fairly sure that double buffered lists doesn't
 *    make our life any easier.
 *
 * 2. The vblank code starts executing at logical line 0. Display must be
 *    set up and ready to run by line DIWSTRT_V, typically 0x2c, minimum
 *    value is 0x18 for maximum overscan.
 *
 *    Tests on my A500/030 for dynamically generating a 37 element copper
 *    list during the VBLANK period under AmigaDos required between
 *    0x10 and 0x14 scanlines. This should be pathological case, and
 *    should do better under Linux/68k. It is however IMPERATIVE that I am
 *    first in the VBLANK isr chain. Try to keep 'buildclist' as fast as
 *    possible. Don't think that it justifies assembler thou'
 *
 * 3. PAL 640x256 display (no overscan) has copper-wait y positions in range
 *    0x02c -> 0x12c. NTSC overscan uses values > 256 too. However counter
 *    is 8 bit, will wrap. RKM 1.1 suggests use of a WAIT(0x00,0xff),
 *    WAIT(x,y-0x100) pair to handle this case. This is WRONG - must use
 *    WAIT(0xe2,0xff) to ensure that wrap occurred by next copper
 *    instruction. Argghh!
 *
 * 4. RKM 1.1 suggests Copper-wait x positions are in range [0,0xe2].
 *    Horizontal blanking occurs in range 0x0f -> 0x35. Black screen
 *    shown in range 0x04 -> 0x47.
 *
 *    Experiments suggest that using WAIT(0x00,y), we can replace up to
 *    7 bitplane pointers before display fetch start. Using a 
 *    WAIT(0xe0,y-1) instead, we can replace 8 pointers that should be
 *    all that we need for a full AGA display. Should work because of
 *    fetch latency with bitmapped display.
 *
 *    I think that this works. Someone please tell me if something breaks.
 *
 * Is diwstop_h the right value to use for "close to the end of line"?
 * It seems to work for me, at least for the modes I've defined. -wjr
 *
 * I changed the Wait(diwstop_h, 0xff) for 256-line chunk skipping to
 * Wait(diwstop_h-2, 0xff) to make it work with the additional
 * `get-all-you-can-get-out-of-it' AGA modes. Maybe we should derive the
 * wait position from the HTOTAL value? - G.U.
 *
 * The Wait(diwstop_h-2, 0xff) didn't work in Super72 under ECS, instead
 * I changed it to Wait(htotal-4, 0xff). Dunno whether it works under AGA,
 * and don't ask my why it works. I'm trying to get some facts on this issue
 * from Commodore.
 * -Jes
 */

static __inline__ ushort *mono_build_clist_hdr(register struct display *p,
														ushort *cop,
														ushort *othercop)	/* Interlace: List for next frame */
{
	int i;
	ushort diwstrt_v = mono_current_par.diwstrt_v;
	ushort diwstop_h = mono_current_par.diwstop_h;

	if (othercop) {
		*cop++ = CUSTOM_OFS(cop1lc);
		*cop++ = (long)othercop >> 16;
		*cop++ = CUSTOM_OFS(cop1lc) + 2;
		*cop++ = (long)othercop;
	}

	/* Point Sprite 0 at cursor sprite: */
	*cop++ = CUSTOM_OFS(sprpt[0]);
	*cop++ = (ushort)((long)mono_current_par.cursor >> 16);
	*cop++ = CUSTOM_OFS(sprpt[0]) + 2;
	*cop++ = (ushort)((long)mono_current_par.cursor & 0x0000ffff);

	/* Point Sprites 1-7 at dummy sprite: */
	for (i=1; i<8; i++) {
		*cop++ = CUSTOM_OFS(sprpt[i]);
		*cop++ = (ushort)((long)mono_current_par.dummy >> 16);
		*cop++ = CUSTOM_OFS(sprpt[i]) + 2;
		*cop++ = (ushort)((long)mono_current_par.dummy & 0x0000ffff);
	}

	/* Halt copper until we have rebuilt the display list */

	*cop++ = ((diwstrt_v - 2) << 8) | (diwstop_h >> 1) | 0x1;
	*cop++ = 0xfffe;

	return(cop);
}

static __inline__ ushort *mono_build_clist_dyn(register struct display *p,
														ushort *cop,
														int shf)				/* Interlace: Short frame */
{
	ushort diwstrt_v = mono_current_par.diwstrt_v;
	ushort diwstop_h = mono_current_par.diwstop_h;
	ushort y_wrap = mono_current_par.y_wrap;
	ulong offset = y_wrap * mono_current_par.bytes_per_row;
	long scrmem;
	int i;

	/* Set up initial bitplane ptrs */

	for (i = 0 ; i < mono_current_par.scr_depth ; i++) {
		scrmem    = ((long)mono_current_par.bitplane[i]) + offset;

		if (shf)
			scrmem += mono_current_par.bytes_per_row;

		*cop++ = CUSTOM_OFS(bplpt[i]);
		*cop++ = (long)scrmem >> 16;
		*cop++ = CUSTOM_OFS(bplpt[i]) + 2;
		*cop++ = (long)scrmem;
	}

	/* If wrapped frame needed - wait for line then switch bitplXs */

	if (y_wrap) {
		ushort line;
        
		if (mono_current_par.bplcon0 & BPC0_LACE)
			line = diwstrt_v + (mono_current_par.scr_height - y_wrap)/2;
		else
			line = diwstrt_v + mono_current_par.scr_height - y_wrap;

		/* Handle skipping over 256-line chunks */
		while (line > 256) {
			/* Hardware limitation - 8 bit counter    */
			/* Wait(diwstop_h-2, 0xff) */
			if (mono_current_par.bplcon0 & BPC0_SHRES)
				/*
				 * htotal-4 is used in SHRES-mode, as diwstop_h-2 doesn't work under ECS.
				 * Does this work under AGA?
				 * -Jes
				 */
				*cop++ = 0xff00 | ((mono_current_par.htotal-4) | 1);
			else 
				*cop++ = 0xff00 | ((diwstop_h-2) >> 1) | 0x1;

			*cop++ = 0xfffe;
			/* Wait(0, 0) - make sure we're in the new segment */
			*cop++ = 0x0001;
			*cop++ = 0xfffe;
			line -= 256;

			/*
			 * Under ECS we have to keep color[0], as it is part of a special color-table.
			 */

			if (boot_info.bi_amiga.chipset == CS_ECS && mono_current_par.bplcon0 & BPC0_ECSENA) {
				*cop++ = 0x0180;
				*cop++ = mono_ecs_color_zero;
			}
		}
	
		/* Wait(diwstop_h, line - 1) */
		*cop++ = ((line - 1)   << 8) | (diwstop_h >> 1) | 0x1;
		*cop++ = 0xfffe;

		for (i = 0 ; i < mono_current_par.scr_depth ; i++) {
			scrmem = (long)mono_current_par.bitplane[i];
			if (shf)
				scrmem += mono_current_par.bytes_per_row;

				*cop++ = CUSTOM_OFS(bplpt[i]); 
				*cop++ = (long)scrmem >> 16;
				*cop++ = CUSTOM_OFS(bplpt[i]) + 2;
				*cop++ = (long)scrmem;
		}
	}
    
	/* End of Copper list */
	*cop++ = 0xffff;
	*cop++ = 0xfffe;

	return(cop);
}


static __inline__ void mono_build_cursor(register struct display *p)
{
	int vs, hs, ve;
	ushort diwstrt_v = mono_current_par.diwstrt_v;
	ushort diwstrt_h = mono_current_par.diwstrt_h;

	if (mono_current_par.bplcon0 & BPC0_LACE) {
		vs = diwstrt_v + (p->cursor_y * p->fontheight)/2; 
		ve = vs + p->fontheight/2;
	} else {
		vs = diwstrt_v + (p->cursor_y * p->fontheight); 
		ve = vs + p->fontheight;
	}

	if (mono_current_par.bplcon0 & BPC0_ECSENA)
		/*
		 * It's an AGA mode. We'll assume that the sprite was set
		 * into 35ns resolution by the appropriate SPRES bits in bplcon3.
		 */
		hs = diwstrt_h  * 4 + (p->cursor_x * p->fontwidth) - 4;
	else
		hs = diwstrt_h + (p->cursor_x * p->fontwidth) / 2 - 1;

	if (mono_current_par.bplcon0 & BPC0_ECSENA) {
		/* There are some high-order bits on the sprite position */
		*((ulong *) mono_current_par.cursor) =
		((((vs & 0xff) << 24) | ((vs & 0x100) >> 6) |
		((vs & 0x200) >> 3)) |
		(((hs & 0x7f8) << 13) | ((hs & 0x4) >> 2) |
		((hs & 0x3) << 3)) |
		(((ve & 0xff) << 8) | ((ve & 0x100) >> 7) |
		((ve & 0x200) >> 4)));
	} else {
		*((ulong *) mono_current_par.cursor) =
		((vs << 24) | ((vs & 0x00000100) >> 6) |
		((hs & 0x000001fe) << 15) | (hs & 0x00000001) | 
		((ve & 0x000000ff) << 8) | ((ve & 0x00000100) >> 7));
	}
}

static void mono_build_ecs_colors(ushort color1, ushort color2, ushort color3, 
                      ushort color4, ushort *table)
{
/*
 * This function calculates the special ECS color-tables needed when running
 * new screen-modes available under ECS. See the hardware reference manual
 * 3rd edition for details.
 * -Jes
 */
ushort  t;

        t = (color1 & 0x0ccc);
        table[0] = t;
        table[4] = t;
        table[8] = t;
        table[12] = t;
        t = t >> 2;
        table[0] = (table[0] | t);
        table[1] = t;
        table[2] = t;
        table[3] = t;

        t = (color2 & 0x0ccc);
        table[1] = (table[1] | t);
        table[5] = t;
        table[9] = t;
        table[13] = t;
        t = t >> 2;
        table[4] = (table[4] | t);
        table[5] = (table[5] | t);
        table[6] = t;
        table[7] = t;

        t = (color3 & 0x0ccc);
        table[2] = (table[2] | t);
        table[6] = (table[6] | t);
        table[10] = t;
        table[14] = t;
        t = t >> 2;
        table[8] = (table[8] | t);
        table[9] = (table[9] | t);
        table[10] = (table[10] | t);
        table[11] = t;

        t = (color4 & 0x0ccc);
        table[3] = (table[3] | t);
        table[7] = (table[7] | t);
        table[11] = (table[11] | t);
        table[15] = t;
        t = t >> 2;
        table[12] = (table[12] | t);
        table[13] = (table[13] | t);
        table[14] = (table[14] | t);
        table[15] = (table[15] | t);

}

/* mono_display_init():
 *
 *    Fills out (struct display *) given a geometry structure
 */

static void mono_display_init(struct display *p,
                         struct geometry *geom, ushort inverse)
{
	ushort ecs_table[16];
	int    i;
	char   *chipptr;
	ushort diwstrt_v, diwstop_v;
	ushort diwstrt_h, diwstop_h;
	ushort diw_min_h, diw_min_v;
	ushort bplmod, diwstrt, diwstop, diwhigh, ddfstrt, ddfstop;
	ushort cursorheight, cursormask = 0;
	u_long size;

	/* Decide colour scheme */

	if (inverse) {
		mono_current_par.fgcol   = FG_COLOR_INV;
		mono_current_par.bgcol   = BG_COLOR_INV;
		mono_current_par.crsrcol = CRSR_COLOR_INV;
	} else {
		mono_current_par.fgcol   = FG_COLOR;
		mono_current_par.bgcol   = BG_COLOR;
		mono_current_par.crsrcol = CRSR_COLOR;
	}

	/* Define screen geometry */
   
	mono_current_par.scr_max_height = geom->scr_max_height;
	mono_current_par.scr_max_width  = geom->scr_max_width; 
	mono_current_par.scr_height     = geom->scr_height;
	mono_current_par.scr_width      = geom->scr_width;
	mono_current_par.scr_depth      = geom->scr_depth;
	mono_current_par.bplcon0        = geom->bplcon0 | BPC0_COLOR;
	mono_current_par.htotal         = geom->htotal;

	/* htotal was added, as I use it to calc the pal-line. -Jes */

	if (mono_current_par.scr_depth < 8)
		mono_current_par.bplcon0 |= (mono_current_par.scr_depth << 12);
	else {
		/* must be exactly 8 */
		mono_current_par.bplcon0 |= BPC0_BPU3;
	}

	diw_min_v         = geom->diwstrt_v;
	diw_min_h         = geom->diwstrt_h;

	/* We can derive everything else from this, at least for OCS */
	/*
	 * For AGA: we don't use the finer position control available for
	 * diw* yet (could be set by 35ns increments).
	 */

	/* Calculate line and plane size while respecting the alignment restrictions */
	mono_current_par.bytes_per_row  = ((mono_current_par.scr_width+geom->alignment-1)&~(geom->alignment-1)) >> 3;
	mono_current_par.plane_size     = mono_current_par.bytes_per_row * mono_current_par.scr_height;


	/*
	 *		Quick hack for frame buffer mmap():
	 *
	 *		plane_size must be a multiple of the page size
	 */

	mono_current_par.plane_size = PAGE_ALIGN(mono_current_par.plane_size);


	mono_current_par.y_wrap   = 0;                  mono_current_par.scroll_latch = 1;
	p->cursor_x = 0; p->cursor_y = 0; mono_current_par.cursor_latch = 1;

	if (mono_current_par.bplcon0 & BPC0_LACE) {
		bplmod = mono_current_par.bytes_per_row;
		diwstrt_v = diw_min_v + (mono_current_par.scr_max_height - mono_current_par.scr_height)/4;
		diwstop_v = (diwstrt_v + mono_current_par.scr_height/2);
	} else {
		bplmod = 0;
		diwstrt_v = diw_min_v + (mono_current_par.scr_max_height - mono_current_par.scr_height)/2;
		diwstop_v = (diwstrt_v + mono_current_par.scr_height);
	}

	if (mono_current_par.bplcon0 & BPC0_HIRES) {
		diwstrt_h =  diw_min_h + (mono_current_par.scr_max_width - mono_current_par.scr_width)/4;
		diwstop_h = (diwstrt_h + mono_current_par.scr_width/2);
		/* ??? Where did 0x1d5 come from in original code ??? */
	} else if (mono_current_par.bplcon0 & BPC0_SHRES) {
		diwstrt_h =  diw_min_h + (mono_current_par.scr_max_width - mono_current_par.scr_width)/8;
		diwstop_h = (diwstrt_h + mono_current_par.scr_width/4);
	} else {
		diwstrt_h =  diw_min_h + (mono_current_par.scr_max_width - mono_current_par.scr_width)/2;
		diwstop_h = (diwstrt_h + mono_current_par.scr_width);
	}

	if (mono_current_par.bplcon0 & BPC0_HIRES) {
		ddfstrt = (diwstrt_h >> 1) - 4;
		ddfstop = ddfstrt + (4 * (mono_current_par.bytes_per_row>>1)) - 8;
	} else if (mono_current_par.bplcon0 & BPC0_SHRES && boot_info.bi_amiga.chipset == CS_AGA) {
		/* There may be some interaction with FMODE here... -8 is magic. */

		/*
		 * This should be fixed, so it supports all different
		 * FMODE's.  FMODE varies the speed with 1,2 & 4 the
		 * standard ECS speed.  Someone else has to do it, as
		 * I don't have an AGA machine with MMU available
		 * here.
		 *
		 * This particular speed looks like FMODE = 3 to me.
		 * ddfstop should be changed so it depends on FMODE under AGA.
		 * -Jes
		 */
		ddfstrt = (diwstrt_h >> 1) - 8;
		ddfstop = ddfstrt + (2 * (mono_current_par.bytes_per_row>>1)) - 8;
	} else if (mono_current_par.bplcon0 & BPC0_SHRES && boot_info.bi_amiga.chipset == CS_ECS){
		/* 
		 * Normal speed for ECS, should be the same for FMODE = 0
		 * -Jes
		 */
		ddfstrt = (diwstrt_h >> 1) - 2;
		ddfstop = ddfstrt + (2 * (mono_current_par.bytes_per_row>>1)) - 8;
	} else {
		ddfstrt = (diwstrt_h >> 1) - 8;
		ddfstop = ddfstrt + (8 * (mono_current_par.bytes_per_row>>1)) - 8;
	}

	if (mono_current_par.bplcon0 & BPC0_LACE)
		cursorheight = p->fontheight/2;
	else
		cursorheight = p->fontheight;

	/*
	 *		Quick hack for frame buffer mmap():
	 *
	 *		chipptr must be at a page boundary
	 */

	size = mono_current_par.scr_depth*mono_current_par.plane_size+COP_MEM_REQ+SPR_MEM_REQ+4*(cursorheight-1);
	size += PAGE_SIZE-1;
	chipptr = amiga_chip_alloc(size);
	chipptr = (char *)PAGE_ALIGN((u_long)chipptr);

   
	/* locate the bitplanes */
	/* These MUST be 64 bit aligned for full AGA compatibility!! */

	mono_current_par.smem_start = (u_long)chipptr;
	mono_current_par.smem_len = mono_current_par.plane_size*mono_current_par.scr_depth;
	mono_current_par.geometry = geom;

	for (i = 0 ; i < mono_current_par.scr_depth ; i++, chipptr += mono_current_par.plane_size) {
		mono_current_par.bitplane[i] = (u_char *) chipptr;
		memset ((void *)chipptr, 0, mono_current_par.plane_size);  /* and clear */
	}

	/* locate the copper lists */
	mono_current_par.coplist1hdr = (ushort *) chipptr;  chipptr += MAX_COP_LIST_ENTS * 4;
	mono_current_par.coplist2hdr = (ushort *) chipptr;  chipptr += MAX_COP_LIST_ENTS * 4;

	/* locate the sprite data */
	mono_current_par.cursor      = (ushort *) chipptr;  chipptr += 8+4*cursorheight;
	mono_current_par.dummy       = (ushort *) chipptr;  chipptr += 12;

	/* create the sprite data for the cursor image */
	memset((void *)mono_current_par.cursor, 0, 8+4*cursorheight);
	/*
	 * Only AGA supplies hires sprites.
	 */
	if (mono_current_par.bplcon0 & BPC0_ECSENA && boot_info.bi_amiga.chipset == CS_AGA)
		/* AGA cursor is SHIRES, ECS sprites differ */
		for (i = 0; (i < p->fontwidth) && (i < 16); i++)
			cursormask |= 1<<(15-i);
	else
		/* For OCS & ECS sprites are pure LORES 8-< */
		for (i = 0; (i < p->fontwidth/2) && (i < 8); i++)
			cursormask |= 1<<(15-i);

	mono_current_par.cursor[0] = mono_cursor_data[0];
	mono_current_par.cursor[1] = mono_cursor_data[1];

#if (CRSR_BLOCK == 1)
	for (i = 0; i < cursorheight; i++)
#else
	for (i = cursorheight-2; i < cursorheight; i++)
#endif
		mono_current_par.cursor[2+2*i] = cursormask;

	/* set dummy sprite data to a blank sprite */
	memset((void *)mono_current_par.dummy, 0, 12);
  
	/* set cursor flashing */
	mono_current_par.cursor_flash = CRSR_FLASH;

	/* Make the cursor invisible */
	mono_current_par.cursor_visible = 0;
 
	/* Initialise the chipregs */
	mono_current_par.diwstrt_v = diwstrt_v;
	mono_current_par.diwstrt_h = diwstrt_h;
	mono_current_par.diwstop_v = diwstop_v;
	mono_current_par.diwstop_h = diwstop_h;
	diwstrt = ((diwstrt_v << 8) | diwstrt_h);
	diwstop = ((diwstop_v & 0xff) << 8) | (diwstop_h & 0xff);

	custom.bplcon0   = mono_current_par.bplcon0;	/* set the display mode */
	custom.bplcon1   = 0;		/* Needed for horizontal scrolling */
	custom.bplcon2   = 0;
	custom.bpl1mod   = bplmod;
	custom.bpl2mod   = bplmod;
	custom.diwstrt   = diwstrt;
	custom.diwstop   = diwstop;
	custom.ddfstrt   = ddfstrt;
	custom.ddfstop   = ddfstop;

	custom.color[0]  = COLOR_MSB(mono_current_par.bgcol);
	custom.color[1]  = COLOR_MSB(mono_current_par.fgcol);
	custom.color[17] = COLOR_MSB(mono_current_par.crsrcol); /* Sprite 0 color */

	if (boot_info.bi_amiga.chipset == CS_AGA) {
		/* Fill in the LSB of the 24 bit color palette */
		/* Must happen after MSB */
		custom.bplcon3   = geom->bplcon3 | BPC3_LOCT;
		custom.color[0]  = COLOR_LSB(mono_current_par.bgcol);
		custom.color[1]  = COLOR_LSB(mono_current_par.fgcol);
		custom.color[17] = COLOR_LSB(mono_current_par.crsrcol);
		custom.bplcon3   = geom->bplcon3;
	}

	if (boot_info.bi_amiga.chipset == CS_ECS && mono_current_par.bplcon0 & BPC0_ECSENA) {
		/*
		 * Calculation of the special ECS color-tables for
		 * planes and sprites is done in the function
		 * build_ecs_table
		 */

		/*
		 * Calcs a special ECS colortable for the bitplane,
		 * and copies it to the custom registers
		 */
		mono_build_ecs_colors(COLOR_MSB(mono_current_par.bgcol), COLOR_MSB(mono_current_par.fgcol),
				 0, 0, ecs_table); 

#if 0
		for (i = 0; i < 8; i++){
			custom.color[i]   = ecs_table[i*2];
			custom.color[i+8] = ecs_table[i*2+1];
		}
#else
		for (i = 0; i < 16; i++){
			custom.color[i]   = ecs_table[i];
		}
#endif

		mono_ecs_color_zero = ecs_table[0];

		/*
		 * Calcs a special ECS colortable for the cursor
		 * sprite, and copies it to the appropriate custom
		 * registers
		 */
		mono_build_ecs_colors(0, COLOR_MSB(mono_current_par.crsrcol), 0, 0, ecs_table);

		for (i = 0; i < 16; i++){
			custom.color[i+16] = ecs_table[i];
		}
	}

	if (!(geom->isOCS)) {
		/* Need to set up a bunch more regs */
		/* Assumes that diwstrt is in the (0,0) sector, but stop might not be */
		diwhigh = (diwstop_v & 0x700) | ((diwstop_h & 0x100) << 5);

		custom.bplcon2   = geom->bplcon2;
		custom.bplcon3   = geom->bplcon3;
		/* save bplcon3 for blanking */
		mono_save_bplcon3 = geom->bplcon3;

		custom.diwhigh   = diwhigh;	/* must happen AFTER diwstrt, stop */

		custom.htotal	= geom->htotal;
		custom.hsstrt	= geom->hsstrt;
		custom.hsstop	= geom->hsstop;
		custom.hbstrt	= geom->hbstrt;
		custom.hbstop	= geom->hbstop;
		custom.vtotal	= geom->vtotal;
		custom.vsstrt	= geom->vsstrt;
		custom.vsstop	= geom->vsstop;
		custom.vbstrt	= geom->vbstrt;
		custom.vbstop	= geom->vbstop;
		custom.hcenter	= geom->hcenter;
		custom.beamcon0	= geom->beamcon0;
		if (boot_info.bi_amiga.chipset == CS_AGA) {
			custom.fmode	= geom->fmode;
		}
		/* 
		 * fmode does NOT! exist under ECS, weird things might happen
		 */

		/* We could load 8-bit colors here, if we wanted */

		/*
		 *    The minimum period for audio depends on htotal (for OCS/ECS/AGA)
		 */
		if (boot_info.bi_amiga.chipset != CS_STONEAGE)
			amiga_audio_min_period = (geom->htotal>>1)+1;
	}


	/* Build initial copper lists. sprites must be set up, and mono_current_par.diwstrt. */

	if (mono_current_par.bplcon0 & BPC0_LACE) {
		mono_current_par.coplist1dyn = mono_build_clist_hdr(p,mono_current_par.coplist1hdr, mono_current_par.coplist2hdr),
		mono_build_clist_dyn(p, mono_current_par.coplist1dyn, 0);

		mono_current_par.coplist2dyn = mono_build_clist_hdr(p,mono_current_par.coplist2hdr, mono_current_par.coplist1hdr),
		mono_build_clist_dyn(p, mono_current_par.coplist2dyn, 1);
	} else {
		mono_current_par.coplist1dyn = mono_build_clist_hdr(p,mono_current_par.coplist1hdr, NULL),
		mono_build_clist_dyn(p, mono_current_par.coplist1dyn, 0);
	}


	/* Get ready to run first copper list */
	custom.cop1lc = mono_current_par.coplist1hdr;
	custom.copjmp1 = 0;

	/* turn on DMA for bitplane and sprites */
	custom.dmacon = DMAF_SETCLR | DMAF_RASTER | DMAF_COPPER | DMAF_SPRITE;

	if (mono_current_par.bplcon0 & BPC0_LACE) {
		/* Make sure we get the fields in the right order */

		/* wait for LOF frame bit to go low */
		while (custom.vposr & 0x8000)
			;

		/* wait for LOF frame bit to go high */
		while (!(custom.vposr & 0x8000))
			;

		/* start again at the beginning of copper list 1 */
		custom.cop1lc = mono_current_par.coplist1hdr;
		custom.copjmp1 = 0;
	}
}


static void mono_amifb_interrupt(int irq, struct pt_regs *fp, void *data)
{
	register struct display *p = disp;

	static ushort cursorcount = 0;
	static ushort cursorstate = 0;

	/* I *think* that you should only change display lists on long frame.
	 * At least it goes awfully peculiar on my A500 without the following
	 * test. Not really in a position to test this hypothesis, so sorry
	 * for the slow scrolling, all you flicker-fixed souls
	 */

	if (!(mono_current_par.bplcon0 & BPC0_LACE) || (custom.vposr & 0x8000)) {
		if (mono_current_par.scroll_latch || mono_current_par.cursor_latch)
			mono_build_cursor(p);

		if (mono_current_par.scroll_latch)
			if (mono_current_par.bplcon0 & BPC0_LACE) {
				mono_build_clist_dyn(p, mono_current_par.coplist1dyn, 0);
				mono_build_clist_dyn(p, mono_current_par.coplist2dyn, 1);
			} else
				mono_build_clist_dyn(p, mono_current_par.coplist1dyn, 0);
			mono_current_par.scroll_latch = 0;
			mono_current_par.cursor_latch = 0;
	}

	if (!(custom.potgor & (1<<10)))
		mono_vblank.right_count++;

	if (mono_current_par.cursor_visible) {
		if (mono_current_par.cursor_flash) {
			if (cursorcount)
				cursorcount--;
			else {
				cursorcount = CRSR_RATE;
				if ((cursorstate = !cursorstate))
					custom.dmacon = DMAF_SETCLR | DMAF_SPRITE;
				else
					custom.dmacon = DMAF_SPRITE;
			}
		}
	} else
		custom.dmacon = DMAF_SPRITE;

	if (mono_do_blank) {
		custom.dmacon = DMAF_RASTER | DMAF_SPRITE;
		custom.color[0] = 0;
		if (boot_info.bi_amiga.chipset == CS_AGA) {
			/* Fill in the LSB of the 24 bit color palette */
			/* Must happen after MSB */
			custom.bplcon3 = mono_save_bplcon3 | BPC3_LOCT;
			custom.color[0]= 0;
			custom.bplcon3 = mono_save_bplcon3;
		}
		mono_do_blank = 0;
	}

	if (mono_do_unblank) {
		if (mono_current_par.cursor_visible)
			custom.dmacon = DMAF_SETCLR | DMAF_RASTER | DMAF_SPRITE;
		else
			custom.dmacon = DMAF_SETCLR | DMAF_RASTER;
		custom.color[0] = COLOR_MSB(mono_current_par.bgcol);
		if (boot_info.bi_amiga.chipset == CS_AGA) {
			/* Fill in the LSB of the 24 bit color palette */
			/* Must happen after MSB */
			custom.bplcon3 = mono_save_bplcon3 | BPC3_LOCT;
			custom.color[0] = COLOR_LSB(mono_current_par.bgcol);
			custom.bplcon3 = mono_save_bplcon3;
		}
		/* color[0] is set to mono_ecs_color_zero under ECS */
		if (boot_info.bi_amiga.chipset == CS_ECS && mono_current_par.bplcon0 & BPC0_ECSENA) {
			custom.color[0]  = mono_ecs_color_zero;
		}
		mono_do_unblank = 0;
	}

	mono_vblank.done = 1;
}


static int mono_amiga_fb_get_fix(struct fb_fix_screeninfo *fix, int con)
{
	int i;

	strcpy(fix->id, mono_current_par.geometry->modename);
	fix->smem_start = mono_current_par.smem_start;
	fix->smem_len = mono_current_par.smem_len;

	/*
	 *		Only monochrome bitmap at the moment
	 */

	fix->type = FB_TYPE_PACKED_PIXELS;

	fix->type_aux = 0;
	if (mono_amifb_inverse)
		fix->visual = FB_VISUAL_MONO10;
	else
		fix->visual = FB_VISUAL_MONO01;

	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 1;

	fix->line_length = 0;
	for (i = 0; i < arraysize(fix->reserved); i++)
		fix->reserved[i] = 0;
	return(0);
}


static int mono_amiga_fb_get_var(struct fb_var_screeninfo *var, int con)
{
	int i;

	var->xres = mono_current_par.geometry->scr_width;
	var->yres = mono_current_par.geometry->scr_height;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->xoffset = 0;
	var->yoffset = 0;

	var->bits_per_pixel = mono_current_par.geometry->scr_depth;
	var->grayscale = 0;

	if (boot_info.bi_amiga.chipset == CS_AGA) {
		var->red.offset = 0;
		var->red.length = 8;
		var->red.msb_right = 0;
		var->green = var->red;
		var->blue = var->red;
	} else {
		var->red.offset = 0;
		var->red.length = 4;
		var->red.msb_right = 0;
		var->green = var->red;
		var->blue = var->red;
	}

	var->nonstd = 0;
	var->activate = 0;

	var->width = -1;
	var->height = -1;

	var->accel = FB_ACCEL_NONE;

	var->pixclock = 35242;
	var->left_margin = (mono_current_par.geometry->hbstop-mono_current_par.geometry->hsstrt)*8;
	var->right_margin = (mono_current_par.geometry->hsstrt-mono_current_par.geometry->hbstrt)*8;
	var->upper_margin = (mono_current_par.geometry->vbstop-mono_current_par.geometry->vsstrt)*8;
	var->lower_margin = (mono_current_par.geometry->vsstrt-mono_current_par.geometry->vbstrt)*8;
	var->hsync_len = (mono_current_par.geometry->hsstop-mono_current_par.geometry->hsstrt)*8;
	var->vsync_len = (mono_current_par.geometry->vsstop-mono_current_par.geometry->vsstrt)*8;
	var->sync = 0;
	if (mono_current_par.geometry->bplcon0 & BPC0_LACE)
		var->vmode = FB_VMODE_INTERLACED;
	else if ((boot_info.bi_amiga.chipset == CS_AGA) && (mono_current_par.geometry->fmode & FMODE_BSCAN2))
		var->vmode = FB_VMODE_DOUBLE;
	else
		var->vmode = FB_VMODE_NONINTERLACED;

	for (i = 0; i < arraysize(var->reserved); i++)
		var->reserved[i] = 0;

	return(0);
}


static void mono_amiga_fb_set_disp(int con)
{
	struct fb_fix_screeninfo fix;

	mono_amiga_fb_get_fix(&fix, con);
	if (con == -1)
		con = 0;
	disp[con].screen_base = (u_char *)fix.smem_start;
	disp[con].visual = fix.visual;
	disp[con].type = fix.type;
	disp[con].type_aux = fix.type_aux;
	disp[con].ypanstep = fix.ypanstep;
	disp[con].ywrapstep = fix.ywrapstep;
	disp[con].line_length = fix.line_length;
	disp[con].can_soft_blank = 1;
	disp[con].inverse = mono_amifb_inverse;
}


static int mono_amiga_fb_set_var(struct fb_var_screeninfo *var, int con)
{
	/*
	 *		Not yet implemented
	 */
	return 0;				/* The X server needs this */
	return(-EINVAL);
}


static short mono_red_normal[] = {
	((BG_COLOR & 0xff0000)>>8) | ((BG_COLOR & 0xff0000)>>16),
	((FG_COLOR & 0xff0000)>>8) | ((FG_COLOR & 0xff0000)>>16)
};
static short mono_green_normal[] = {
	((BG_COLOR & 0x00ff00)) | ((BG_COLOR & 0x00ff00)>>8),
	((FG_COLOR & 0x00ff00)) | ((FG_COLOR & 0x00ff00)>>8)
};
static short mono_blue_normal[] = {
	((BG_COLOR & 0x0000ff)<<8) | ((BG_COLOR & 0x0000ff)),
	((FG_COLOR & 0x0000ff)<<8) | ((FG_COLOR & 0x0000ff))
};

static short mono_red_inverse[] = {
	((BG_COLOR_INV & 0xff0000)>>8) | ((BG_COLOR_INV & 0xff0000)>>16),
	((FG_COLOR_INV & 0xff0000)>>8) | ((FG_COLOR_INV & 0xff0000)>>16)
};
static short mono_green_inverse[] = {
	((BG_COLOR_INV & 0x00ff00)) | ((BG_COLOR_INV & 0x00ff00)>>8),
	((FG_COLOR_INV & 0x00ff00)) | ((FG_COLOR_INV & 0x00ff00)>>8)
};
static short mono_blue_inverse[] = {
	((BG_COLOR_INV & 0x0000ff)<<8) | ((BG_COLOR_INV & 0x0000ff)),
	((FG_COLOR_INV & 0x0000ff)<<8) | ((FG_COLOR & 0x0000ff))
};

static struct fb_cmap mono_default_cmap_normal = { 0, 2, mono_red_normal, mono_green_normal, mono_blue_normal, NULL };
static struct fb_cmap mono_default_cmap_inverse = { 0, 2, mono_red_inverse, mono_green_inverse, mono_blue_inverse, NULL };

static int mono_amiga_fb_get_cmap(struct fb_cmap *cmap, int kspc, int con)
{
	int i, start;
	unsigned short *red, *green, *blue, *transp;
	unsigned int hred, hgreen, hblue, htransp;
	struct fb_cmap *def_cmap;

	red = cmap->red;
	green = cmap->green;
	blue = cmap->blue;
	transp = cmap->transp;
	start = cmap->start;
	if (start < 0)
		return(-EINVAL);

	if (mono_amifb_inverse)
		def_cmap = &mono_default_cmap_inverse;
	else
		def_cmap = &mono_default_cmap_normal;

	for (i = 0; i < cmap->len; i++) {
		if (i < def_cmap->len) {
			hred = def_cmap->red[i];
			hgreen = def_cmap->green[i];
			hblue = def_cmap->blue[i];
			if (def_cmap->transp)
				htransp = def_cmap->transp[i];
			else
				htransp = 0;
		} else
			hred = hgreen = hblue = htransp = 0;
		if (kspc) {
			*red = hred;
			*green = hgreen;
			*blue = hblue;
			if (transp)
				*transp = htransp;
		} else {
			put_fs_word(hred, red);
			put_fs_word(hgreen, green);
			put_fs_word(hblue, blue);
			if (transp)
				put_fs_word(htransp, transp);
		}
		red++;
		green++;
		blue++;
		if (transp)
			transp++;
	}
	return(0);
}


static int mono_amiga_fb_set_cmap(struct fb_cmap *cmap, int kspc, int con)
{
	/*
	 *		Not yet implemented
	 */
	return(-EINVAL);
}


static int mono_amiga_fb_pan_display(struct fb_var_screeninfo *var, int con)
{
	/*
	 *		Not yet implemented
	 */
	return(-EINVAL);
}


static int mono_amiga_fb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
								  unsigned long arg, int con)
{
	return(-EINVAL);
}

static struct fb_ops mono_amiga_fb_ops = {
	mono_amiga_fb_get_fix, mono_amiga_fb_get_var, mono_amiga_fb_set_var, mono_amiga_fb_get_cmap,
	mono_amiga_fb_set_cmap, mono_amiga_fb_pan_display, mono_amiga_fb_ioctl	
};


static int mono_amifb_switch (int con)
{
	mono_current_par.y_wrap = disp[con].var.yoffset;
	mono_current_par.cursor_latch = 1;
	mono_current_par.scroll_latch = 1;
	return(0);
}


static int mono_amifb_updatevar(int con)
{
	mono_current_par.y_wrap = disp[con].var.yoffset;
	mono_current_par.cursor_latch = 1;
	mono_current_par.scroll_latch = 1;
	return(0);
}


static void mono_amifb_blank(int blank)
{
	if (blank)
		mono_do_blank = 1;
	else
		mono_do_unblank = 1;
}


static struct fb_info *mono_amiga_fb_init(long *mem_start)
{
	int mode = mono_amifb_mode;
	ulong model;
	int inverse_video = mono_amifb_inverse;
	int err;

	err=register_framebuffer("Amiga Builtin", &node, &mono_amiga_fb_ops,  mono_num_mono_amiga_fb_predefined,
									 mono_mono_amiga_fb_predefined);

	model = boot_info.bi_un.bi_ami.model;
	if (mode == -1)
		if (boot_info.bi_amiga.chipset == CS_AGA)
			mode = AGA_DEFMODE;
		else if (model == AMI_3000)
			mode = boot_info.bi_un.bi_ami.vblank == 50 ? OCS_PAL_3000_DEFMODE : OCS_NTSC_3000_DEFMODE;
		else
			mode = boot_info.bi_un.bi_ami.vblank == 50 ? OCS_PAL_LOWEND_DEFMODE : OCS_NTSC_LOWEND_DEFMODE;

	mono_init_vblank();
	mono_display_init(disp, &mono_modes[mode], inverse_video);
	if (!add_isr(IRQ_AMIGA_VERTB, mono_amifb_interrupt, 0, NULL, "frame buffer"))
		panic("Couldn't add vblank interrupt");

	mono_amiga_fb_get_var(&disp[0].var, 0);
	if (mono_amifb_inverse)
		disp[0].cmap = mono_default_cmap_inverse;
	else
		disp[0].cmap = mono_default_cmap_normal;
	mono_amiga_fb_set_disp(-1);

	strcpy(fb_info.modename, "Amiga Builtin ");
	fb_info.disp = disp;
	fb_info.switch_con = &mono_amifb_switch;
	fb_info.updatevar = &mono_amifb_updatevar;
	fb_info.blank = &mono_amifb_blank;	
	strcat(fb_info.modename, mono_modes[mode].modename);

	return(&fb_info);
}
#endif /* USE_MONO_AMIFB_IF_NON_AGA */


/* -------------------- OCS specific routines ------------------------------- */


#ifdef CONFIG_AMIFB_OCS
   /*
    *    Initialization
    *
    *    Allocate the required chip memory.
    *    Set the default video mode for this chipset. If a video mode was
    *    specified on the command line, it will override the default mode.
    */

static int ocs_init(void)
{
   u_long p;

   /*
    *    Disable Display DMA
    */

   custom.dmacon = DMAF_ALL | DMAF_MASTER;

   /*
    *    Set the Default Video Mode
    */

   if (!amifb_mode)
      amifb_mode = get_video_mode(boot_info.bi_un.bi_ami.vblank == 50 ?
                                  DEFMODE_PAL : DEFMODE_NTSC);

   /*
    *    Allocate Chip RAM Structures
    */

   videomemorysize = VIDEOMEMSIZE_OCS;


   ...
   ...
   ...


   /*
    *    Enable Display DMA
    */

   custom.dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER |
                   DMAF_SPRITE;

   return(0);
}
#endif /* CONFIG_AMIFB_OCS */


/* -------------------- ECS specific routines ------------------------------- */


#ifdef CONFIG_AMIFB_ECS
   /*
    *    Initialization
    *
    *    Allocate the required chip memory.
    *    Set the default video mode for this chipset. If a video mode was
    *    specified on the command line, it will override the default mode.
    */

static int ecs_init(void)
{
   u_long p;

   /*
    *    Disable Display DMA
    */

   custom.dmacon = DMAF_ALL | DMAF_MASTER;

   /*
    *    Set the Default Video Mode
    */

   if (!amifb_mode)
      if (AMIGAHW_PRESENT(AMBER_FF))
         amifb_mode = get_video_mode(boot_info.bi_un.bi_ami.vblank == 50 ?
                                     DEFMODE_AMBER_PAL : DEFMODE_AMBER_NTSC);
      else
         amifb_mode = get_video_mode(boot_info.bi_un.bi_ami.vblank == 50 ?
                                     DEFMODE_PAL : DEFMODE_NTSC);

   /*
    *    Allocate Chip RAM Structures
    */

   if (boot_info.bi_amiga.chip_size > 1048576)
      videomemorysize = VIDEOMEMSIZE_ECS_2M;
   else
      videomemorysize = VIDEOMEMSIZE_ECS_1M;


   ...
   ...
   ...


   /*
    *    Enable Display DMA
    */

   custom.dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER |
                   DMAF_SPRITE;

   return(0);
}
#endif /* CONFIG_AMIFB_ECS */


/* -------------------- AGA specific routines ------------------------------- */


#ifdef CONFIG_AMIFB_AGA
   /*
    *    Macros for the conversion from real world values to hardware register
    *    values (and vice versa).
    *
    *    This helps us to keep our attention on the real stuff...
    *
    *    Hardware limits:
    *
    *       parameter     min      max     step
    *       ---------     ---     ----     ----
    *       diwstrt_h       0     2047        1
    *       diwstrt_v       0     2047        1
    *       diwstop_h       0     2047        1
    *       diwstop_v       0     2047        1
    *
    *       ddfstrt         0     2032       16
    *       ddfstop         0     2032       16
    *
    *       htotal          8     2048        8
    *       hsstrt          0     2040        8
    *       hsstop          0     2040        8
    *       vtotal          1     2048        1
    *       vsstrt          0     2047        1
    *       vsstop          0     2047        1
    *       hcenter         0     2040        8
    *
    *       hbstrt          0     2047        1
    *       hbstop          0     2047        1
    *       vbstrt          0     2047        1
    *       vbstop          0     2047        1
    *
    *    Horizontal values are in 35 ns (SHRES) pixels
    *    Vertical values are in scanlines
    */

/* bplcon1 (smooth scrolling) */

#define hscroll2hw(hscroll) \
   (((hscroll)<<12 & 0x3000) | ((hscroll)<<8 & 0xc300) | \
    ((hscroll)<<4 & 0x0c00) | ((hscroll)<<2 & 0x00f0) | ((hscroll)>>2 & 0x000f))

#define hw2hscroll(hscroll) \
   (((hscroll)>>8 & 0x00c3) | ((hscroll)<<2 & 0x003c))

/* diwstrt/diwstop/diwhigh (visible display window) */

#define diwstrt2hw(diwstrt_h, diwstrt_v) \
   (((diwstrt_v)<<8 & 0xff00) | ((diwstrt_h)>>2 & 0x00ff))
#define diwstop2hw(distop_h, diwstop_v) \
   (((diwstop_v)<<8 & 0xff00) | ((diwstop_h)>>2 & 0x00ff))
#define diw2hw_high(diwstrt_h, diwstrt_v, distop_h, diwstop_v) \
   (((diwstop_h)<<3 & 0x2000) | ((diwstop_h)<<11 & 0x1800) | \
    ((diwstop_v) & 0x0700) | ((diwstrt_h)>>5 & 0x0020) | \
    ((diwstrt_h)<<3 & 0x0018) | ((diwstrt_v)>>8 & 0x0007))

#define hw2diwstrt_h(diwstrt, diwhigh) \
   (((diwhigh)<<5 & 0x0400) | ((diwstrt)<<2 & 0x03fc) | ((diwhigh)>>3 & 0x0003))
#define hw2diwstrt_v(diwstrt, diwhigh) \
   (((diwhigh)<<8 & 0x0700) | ((diwstrt)>>8 & 0x00ff))
#define hw2diwstop_h(diwstop, diwhigh) \
   (((diwhigh)>>3 & 0x0400) | ((diwstop)<<2 & 0x03fc) | \
    ((diwhigh)>>11 & 0x0003))
#define hw2diwstop_v(diwstop, diwhigh) \
   (((diwhigh) & 0x0700) | ((diwstop)>>8 & 0x00ff))

/* ddfstrt/ddfstop (display DMA) */

#define ddfstrt2hw(ddfstrt)   (div8(ddfstrt) & 0x00fe)
#define ddfstop2hw(ddfstop)   (div8(ddfstop) & 0x00fe)

#define hw2ddfstrt(ddfstrt)   ((ddfstrt)<<3)
#define hw2ddfstop(ddfstop)   ((ddfstop)<<3)

/* hsstrt/hsstop/htotal/vsstrt/vsstop/vtotal (sync timings) */

#define hsstrt2hw(hsstrt)     (div8(hsstrt))
#define hsstop2hw(hsstop)     (div8(hsstop))
#define htotal2hw(htotal)     (div8(htotal)-1)
#define vsstrt2hw(vsstrt)     (vsstrt)
#define vsstop2hw(vsstop)     (vsstop)
#define vtotal2hw(vtotal)     ((vtotal)-1)

#define hw2hsstrt(hsstrt)     ((hsstrt)<<3)
#define hw2hsstop(hsstop)     ((hsstop)<<3)
#define hw2htotal(htotal)     (((htotal)+1)<<3)
#define hw2vsstrt(vsstrt)     (vsstrt)
#define hw2vsstop(vsstop)     (vsstop)
#define hw2vtotal(vtotal)     ((vtotal)+1)

/* hbstrt/hbstop/vbstrt/vbstop (blanking timings) */

#define hbstrt2hw(hbstrt)     (((hbstrt)<<8 & 0x0700) | ((hbstrt)>>3 & 0x00ff))
#define hbstop2hw(hbstop)     (((hbstop)<<8 & 0x0700) | ((hbstop)>>3 & 0x00ff))
#define vbstrt2hw(vbstrt)     (vbstrt)
#define vbstop2hw(vbstop)     (vbstop)

#define hw2hbstrt(hbstrt)     (((hbstrt)<<3 & 0x07f8) | ((hbstrt)>>8 & 0x0007))
#define hw2hbstop(hbstop)     (((hbstop)<<3 & 0x07f8) | ((hbstop)>>8 & 0x0007))
#define hw2vbstrt(vbstrt)     (vbstrt)
#define hw2vbstop(vbstop)     (vbstop)

/* color */

#define rgb2hw_high(red, green, blue) \
   (((red)<<4 & 0xf00) | ((green) & 0x0f0) | ((blue)>>4 & 0x00f))
#define rgb2hw_low(red, green, blue) \
   (((red)<<8 & 0xf00) | ((green)<<4 & 0x0f0) | ((blue) & 0x00f))

#define hw2red(high, low)     (((high)>>4 & 0xf0) | ((low)>>8 & 0x0f))
#define hw2green(high, low)   (((high) & 0xf0) | ((low)>>4 & 0x0f))
#define hw2blue(high, low)    (((high)<<4 & 0xf0) | ((low) & 0x0f))

/* sprpos/sprctl (sprite positioning) */

#define spr2hw_pos(start_v, start_h) \
   (((start_v)<<8&0xff00) | ((start_h)>>3&0x00ff))
#define spr2hw_ctl(start_v, start_h, stop_v) \
   (((stop_v)<<8&0xff00) | ((start_v)>>3&0x0040) | ((stop_v)>>4&0x0020) | \
    ((start_h)<<3&0x0018) | ((start_v)>>6&0x0004) | ((stop_v)>>7&0x0002) | \
    ((start_h)>>2&0x0001))


   /*
    *    Hardware Cursor
    */

struct aga_cursorsprite {
   u_short sprpos;
   u_short pad1[3];
   u_short sprctl;
   u_short pad2[3];
   union {
      struct {
         u_long data[64*4];
         u_long trailer[4];
      } nonlaced;
      struct {
         u_long data[32*4];
         u_long trailer[4];
      } laced;
   } u;
};

struct aga_dummysprite {
   u_short sprpos;
   u_short pad1[3];
   u_short sprctl;
   u_short pad2[3];
   u_long data[4];
   u_long trailer[4];
};


   /*
    *    Pixel modes for Bitplanes and Sprites
    */

static u_short bplpixmode[3] = {
   BPC0_SHRES,                   /*  35 ns / 28 MHz */
   BPC0_HIRES,                   /*  70 ns / 14 MHz */
   0                             /* 140 ns /  7 MHz */
};

static u_short sprpixmode[3] = {
   BPC3_SPRES1 | BPC3_SPRES0,    /*  35 ns / 28 MHz */
   BPC3_SPRES1,                  /*  70 ns / 14 MHz */
   BPC3_SPRES0                   /* 140 ns /  7 MHz */
};


   /*
    *    Initialization
    *
    *    Allocate the required chip memory.
    *    Set the default video mode for this chipset. If a video mode was
    *    specified on the command line, it will override the default mode.
    */

static int aga_init(void)
{
   u_long p;

   /*
    *    Disable Display DMA
    */

   custom.dmacon = DMAF_ALL | DMAF_MASTER;

   /*
    *    Set the Default Video Mode
    */

   if (!amifb_mode)
      amifb_mode = get_video_mode(DEFMODE_AGA);

   /*
    *    Allocate Chip RAM Structures
    */

   if (boot_info.bi_amiga.chip_size > 1048576)
      videomemorysize = VIDEOMEMSIZE_AGA_2M;
   else
      videomemorysize = VIDEOMEMSIZE_AGA_1M;

   p = chipalloc(videomemorysize+                     /* Bitplanes */
                 sizeof(struct clist_hdr)+            /* Copper Lists */
                 2*sizeof(struct clist_dyn)+
                 2*sizeof(struct aga_cursorsprite)+   /* Sprites */
                 sizeof(struct aga_dummysprite));

   assignchunk(videomemory, u_long, p, videomemorysize);
   assignchunk(clist_hdr, struct clist_hdr *, p, sizeof(struct clist_hdr));
   assignchunk(clist_lof, struct clist_dyn *, p, sizeof(struct clist_dyn));
   assignchunk(clist_shf, struct clist_dyn *, p, sizeof(struct clist_dyn));
   assignchunk(lofsprite, u_long *, p, sizeof(struct aga_cursorsprite));
   assignchunk(shfsprite, u_long *, p, sizeof(struct aga_cursorsprite));
   assignchunk(dummysprite, u_long *, p, sizeof(struct aga_dummysprite));

   /*
    *    Make sure the Copper has something to do
    */

   aga_build_clist_hdr(clist_hdr);

   custom.cop1lc = (u_short *)ZTWO_PADDR(clist_hdr);
   custom.cop2lc = (u_short *)ZTWO_PADDR(&clist_hdr->wait_forever);
   custom.copjmp1 = 0;

   /*
    *    Enable Display DMA
    */

   custom.dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER |
                   DMAF_SPRITE;

   /*
    *    These hardware register values will never be changed later
    */

   custom.bplcon2 = BPC2_KILLEHB | BPC2_PF2P2 | BPC2_PF1P2;
   custom.bplcon4 = BPC4_ESPRM4 | BPC4_OSPRM4;

   return(0);
}


   /*
    *    This function should fill in the `fix' structure based on the
    *    values in the `par' structure.
    */

static int aga_encode_fix(struct fb_fix_screeninfo *fix,
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
   fix->visual = FB_VISUAL_PSEUDOCOLOR;

   if (par->diwstrt_h >= 323)
      fix->xpanstep = 1;
   else
      fix->xpanstep = 64;
   fix->ypanstep = 1;

   if (hw2ddfstrt(par->ddfstrt) >= (par->bpp-1)*64)
      fix->ywrapstep = 1;
   else
      fix->ywrapstep = 0;

   fix->line_length = 0;
   for (i = 0; i < arraysize(fix->reserved); i++)
      fix->reserved[i] = 0;

   return(0);
}


   /*
    *    Get the video params out of `var'. If a value doesn't fit, round
    *    it up, if it's too big, return -EINVAL.
    */

static int aga_decode_var(struct fb_var_screeninfo *var,
                          struct amiga_fb_par *par)
{
   u_short clk_shift, line_shift_incd;
   u_long upper, lower, hslen, vslen;
   int xres_n, yres_n, xoffset_n;                              /* normalized */
   u_long left_n, right_n, upper_n, lower_n, hslen_n, vslen_n; /* normalized */
   u_long diwstrt_h, diwstrt_v, diwstop_h, diwstop_v;
   u_long hsstrt, vsstrt, hsstop, vsstop, htotal, vtotal;
   u_long ddfmin, ddfmax, ddfstrt, ddfstop, hscroll;
   u_long hrate, vrate;
   u_short loopcnt = 0;

   /*
    *    Find a matching Pixel Clock
    */

   for (clk_shift = 0; clk_shift < 3; clk_shift++)
      if (var->pixclock <= pixclock[clk_shift])
         break;
   if (clk_shift >= 3)
      return(-EINVAL);
   par->clk_shift = clk_shift;

   /*
    *    Round up the Geometry Values (if necessary)
    */

   par->xres = max(var->xres, 64);
   par->yres = max(var->yres, 64);
   par->vxres = up64(max(var->xres_virtual, par->xres));
   par->vyres = max(var->yres_virtual, par->yres);

   par->bpp = var->bits_per_pixel;
   if (par->bpp > 8)
      return(-EINVAL);

   if (!var->nonstd) {
      if (par->bpp < 1)
         par->bpp = 1;
   } else if (var->nonstd == FB_NONSTD_HAM)
      par->bpp = par->bpp <= 6 ? 6 : 8;
   else
      return(-EINVAL);

   upper = var->upper_margin;
   lower = var->lower_margin;
   hslen = var->hsync_len;
   vslen = var->vsync_len;

   par->vmode = var->vmode;
   switch (par->vmode & FB_VMODE_MASK) {
      case FB_VMODE_NONINTERLACED:
         line_shift_incd = 1;
         break;
      case FB_VMODE_INTERLACED:
         line_shift_incd = 0;
         if (par->yres & 1)
            par->yres++;               /* even */
         if (upper & 1)
            upper++;                   /* even */
         if (!(lower & 1))
            lower++;                   /* odd */
         if (vslen & 1)
            vslen++;                   /* even */
         break;
      case FB_VMODE_DOUBLE:
         line_shift_incd = 2;
         break;
      default:
         return(-EINVAL);
         break;
   }

   par->xoffset = var->xoffset;
   par->yoffset = var->yoffset;
   if (par->vmode & FB_VMODE_YWRAP) {
      if (par->xoffset || par->yoffset < 0 || par->yoffset >= par->yres)
         return(-EINVAL);
   } else {
      if (par->xoffset < 0 || par->xoffset+par->xres > par->vxres ||
          par->yoffset < 0 || par->yoffset+par->yres > par->vyres)
         return(-EINVAL);
   }

   if (var->sync & FB_SYNC_BROADCAST) {
      if (hslen || vslen)
         return(-EINVAL);
   } else {
      hslen = hslen < 1 ? 1 : hslen;
      vslen = vslen < 1 ? 1 : vslen;
   }

   /*
    *    Check the Memory Requirements
    */

   if (par->vxres*par->vyres*par->bpp > videomemorysize<<3)
      return(-EINVAL);

   /*
    *    Normalize all values:
    *
    *      - horizontal: in 35 ns (SHRES) pixels
    *      - vertical: in non-interlaced scanlines
    */

   xres_n = par->xres<<clk_shift;
   xoffset_n = par->xoffset<<clk_shift;
   yres_n = par->yres<<line_shift_incd>>1;

   left_n = var->left_margin<<clk_shift;
   right_n = var->right_margin<<clk_shift;
   hslen_n = hslen<<clk_shift;
   upper_n = upper<<line_shift_incd>>1;
   lower_n = lower<<line_shift_incd>>1;
   vslen_n = vslen<<line_shift_incd>>1;

   /*
    *    Vertical and Horizontal Timings
    */

   par->bplcon3 = sprpixmode[clk_shift];
aga_calculate_timings:
   if (var->sync & FB_SYNC_BROADCAST) {
      if (upper_n+yres_n+lower_n == PAL_WINDOW_V) {
         /* PAL video mode */
         diwstrt_v = PAL_DIWSTRT_V+upper_n;
         diwstop_v = diwstrt_v+yres_n;
         diwstrt_h = PAL_DIWSTRT_H+left_n;
         diwstop_h = diwstrt_h+xres_n+1;
         par->htotal = htotal2hw(PAL_HTOTAL);
         hrate = 15625;
         vrate = 50;
         par->beamcon0 = BMC0_PAL;
      } else if (upper_n+yres_n+lower_n == NTSC_WINDOW_V) {
         /* NTSC video mode */
         diwstrt_v = NTSC_DIWSTRT_V+upper_n;
         diwstop_v = diwstrt_v+yres_n;
         diwstrt_h = NTSC_DIWSTRT_H+left_n;
         diwstop_h = diwstrt_h+xres_n+1;
         par->htotal = htotal2hw(NTSC_HTOTAL);
         hrate = 15750;
         vrate = 60;
         par->beamcon0 = 0;
      } else
         return(-EINVAL);
   } else {
      /* Programmable video mode */
      vsstrt = lower_n;
      vsstop = vsstrt+vslen_n;
      diwstrt_v = vsstop+upper_n;
      diwstop_v = diwstrt_v+yres_n;
      vtotal = diwstop_v;
      hslen_n = up8(hslen_n);
      htotal = up8(left_n+xres_n+right_n+hslen_n);
      if (vtotal > 2048 || htotal > 2048)
         return(-EINVAL);
      right_n = htotal-left_n-xres_n-hslen_n;
      hsstrt = down8(right_n+4);
      hsstop = hsstrt+hslen_n;
      diwstop_h = htotal+hsstrt-right_n+1;
      diwstrt_h = diwstop_h-xres_n-1;
      hrate = (amiga_masterclock+htotal/2)/htotal;
      vrate = (amiga_masterclock+htotal*vtotal/2)/(htotal*vtotal);
      par->bplcon3 |= BPC3_BRDRBLNK | BPC3_EXTBLKEN;
      par->beamcon0 = BMC0_HARDDIS | BMC0_VARVBEN | BMC0_LOLDIS |
                      BMC0_VARVSYEN | BMC0_VARHSYEN | BMC0_VARBEAMEN |
                      BMC0_PAL | BMC0_VARCSYEN;
      if (var->sync & FB_SYNC_HOR_HIGH_ACT)
         par->beamcon0 |= BMC0_HSYTRUE;
      if (var->sync & FB_SYNC_VERT_HIGH_ACT)
         par->beamcon0 |= BMC0_VSYTRUE;
      if (var->sync & FB_SYNC_COMP_HIGH_ACT)
         par->beamcon0 |= BMC0_CSYTRUE;
      par->htotal = htotal2hw(htotal);
      par->hsstrt = hsstrt2hw(hsstrt);
      par->hsstop = hsstop2hw(hsstop);
      par->vtotal = vtotal2hw(vtotal);
      par->vsstrt = vsstrt2hw(vsstrt);
      par->vsstop = vsstop2hw(vsstop);
      par->hcenter = par->hsstrt+(par->htotal>>1);
   }
   par->diwstrt_v = diwstrt_v;
   par->diwstrt_h = diwstrt_h;
   par->crsr_x = 0;
   par->crsr_y = 0;

   /*
    *    DMA Timings
    */

   ddfmin = down64(xoffset_n);
   ddfmax = up64(xoffset_n+xres_n);
   hscroll = diwstrt_h-68-mod64(xoffset_n);
   ddfstrt = down64(hscroll);
   if (ddfstrt < 128) {
      right_n += (128-hscroll);
      /* Prevent an infinite loop */
      if (loopcnt++)
         return(-EINVAL);
      goto aga_calculate_timings;
   }
   hscroll -= ddfstrt;
   ddfstop = ddfstrt+ddfmax-ddfmin-(64<<clk_shift);

   /*
    *    Bitplane calculations
    */

   if (amifb_ilbm) {
      par->next_plane = div8(par->vxres);
      par->next_line = par->bpp*par->next_plane;
   } else {
      par->next_line = div8(par->vxres);
      par->next_plane = par->vyres*par->next_line;
   }
   par->bplpt0 = ZTWO_PADDR((u_long)videomemory+div8(ddfmin>>clk_shift)+
                            par->yoffset*par->next_line);
   par->bpl1mod = par->next_line-div8((ddfmax-ddfmin)>>clk_shift);
   par->bpl2mod = par->bpl1mod;

   /*
    *    Hardware Register Values
    */

   par->bplcon0 = BPC0_COLOR | BPC0_ECSENA | bplpixmode[clk_shift];
   if (par->bpp == 8)
      par->bplcon0 |= BPC0_BPU3;
   else
      par->bplcon0 |= par->bpp<<12;
   if (var->nonstd == FB_NONSTD_HAM)
      par->bplcon0 |= BPC0_HAM;
   if (var->sync & FB_SYNC_EXT)
      par->bplcon0 |= BPC0_ERSY;
   par->bplcon1 = hscroll2hw(hscroll);
   par->diwstrt = diwstrt2hw(diwstrt_h, diwstrt_v);
   par->diwstop = diwstop2hw(diwstop_h, diwstop_v);
   par->diwhigh = diw2hw_high(diwstrt_h, diwstrt_v, distop_h, diwstop_v);
   par->ddfstrt = ddfstrt2hw(ddfstrt);
   par->ddfstop = ddfstop2hw(ddfstop);
   par->fmode = FMODE_SPAGEM | FMODE_SPR32 | FMODE_BPAGEM | FMODE_BPL32;

   switch (par->vmode & FB_VMODE_MASK) {
      case FB_VMODE_INTERLACED:
         par->bpl1mod += par->next_line;
         par->bpl2mod += par->next_line;
         par->bplcon0 |= BPC0_LACE;
         break;
      case FB_VMODE_DOUBLE:
         par->bpl1mod -= par->next_line;
         par->fmode |= FMODE_SSCAN2 | FMODE_BSCAN2;
         break;
   }

   if (hrate < hfmin || hrate > hfmax || vrate < vfmin || vrate > vfmax)
      return(-EINVAL);

   return(0);
}


   /*
    *    Fill the `var' structure based on the values in `par' and maybe
    *    other values read out of the hardware.
    */

static int aga_encode_var(struct fb_var_screeninfo *var,
                          struct amiga_fb_par *par)
{
   u_short clk_shift, line_shift_incd;
   u_long left, right, upper, lower, hslen, vslen;
   u_short diwstop_h, diwstop_v;
   u_short hsstrt, vsstrt, hsstop, vsstop, htotal;
   int i;

   var->xres = par->xres;
   var->yres = par->yres;
   var->xres_virtual = par->vxres;
   var->yres_virtual = par->vyres;
   var->xoffset = par->xoffset;
   var->yoffset = par->yoffset;

   var->bits_per_pixel = par->bpp;
   var->grayscale = 0;

   var->red.offset = 0;
   var->red.length = 8;
   var->red.msb_right = 0;
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
   var->accel = FB_ACCEL_NONE;

   clk_shift = par->clk_shift;
   var->pixclock = pixclock[clk_shift];

   diwstop_h = hw2diwstop_h(par->diwstop, par->diwhigh);
   if (par->beamcon0 & BMC0_VARBEAMEN) {
      hsstrt = hw2hsstrt(par->hsstrt);
      vsstrt = hw2vsstrt(par->vsstrt);
      hsstop = hw2hsstop(par->hsstop);
      vsstop = hw2vsstop(par->vsstop);
      htotal = hw2htotal(par->htotal);
      left = par->diwstrt_h-hsstop;
      right = htotal+hsstrt-diwstop_h+1;
      hslen = hsstop-hsstrt;
      upper = par->diwstrt_v-vsstop;
      lower = vsstrt;
      vslen = vsstop-vsstrt;
      var->sync = 0;
   } else {
      diwstop_v = hw2diwstop_v(par->diwstop, par->diwhigh);
      if (par->beamcon0 & BMC0_PAL) {
         left = par->diwstrt_h-PAL_DIWSTRT_H;
         right = PAL_DIWSTRT_H+PAL_WINDOW_H-diwstop_h+1;
         upper = par->diwstrt_v-PAL_DIWSTRT_V;
         lower = PAL_DIWSTRT_V+PAL_WINDOW_V-diwstop_v;
      } else {
         left = par->diwstrt_h-NTSC_DIWSTRT_H;
         right = NTSC_DIWSTRT_H+NTSC_WINDOW_H-diwstop_h;
         upper = par->diwstrt_v-NTSC_DIWSTRT_V;
         lower = NTSC_DIWSTRT_V+NTSC_WINDOW_V-diwstop_v;
      }
      hslen = 0;
      vslen = 0;
      var->sync = FB_SYNC_BROADCAST;
   }

   if (par->bplcon0 & BPC0_ERSY)
      var->sync |= FB_SYNC_EXT;
   if (par->beamcon0 & BMC0_HSYTRUE)
      var->sync |= FB_SYNC_HOR_HIGH_ACT;
   if (par->beamcon0 & BMC0_VSYTRUE)
      var->sync |= FB_SYNC_VERT_HIGH_ACT;
   if (par->beamcon0 & BMC0_CSYTRUE)
      var->sync |= FB_SYNC_COMP_HIGH_ACT;

   switch (par->vmode & FB_VMODE_MASK) {
      case FB_VMODE_NONINTERLACED:
         line_shift_incd = 1;
         break;
      case FB_VMODE_INTERLACED:
         line_shift_incd = 0;
         break;
      case FB_VMODE_DOUBLE:
         line_shift_incd = 2;
         break;
   }

   var->left_margin = left>>clk_shift;
   var->right_margin = right>>clk_shift;
   var->upper_margin = upper<<1>>line_shift_incd;
   var->lower_margin = lower<<1>>line_shift_incd;
   var->hsync_len = hslen>>clk_shift;
   var->vsync_len = vslen<<1>>line_shift_incd;
   var->vmode = par->vmode;
   for (i = 0; i < arraysize(var->reserved); i++)
      var->reserved[i] = 0;

   return(0);
}


   /*
    *    Read a single color register and split it into
    *    colors/transparent. Return != 0 for invalid regno.
    */

static int aga_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp)
{
   if (regno > 255)
      return(1);

   *red = palette[regno].red;
   *green = palette[regno].green;
   *blue = palette[regno].blue;
   return(0);
}


   /*
    *    Set a single color register. The values supplied are already
    *    rounded down to the hardware's capabilities (according to the
    *    entries in the var structure). Return != 0 for invalid regno.
    */

static int aga_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp)
{
   u_short bplcon3 = current_par.bplcon3;

   if (regno > 255)
      return(1);

   /*
    *    Update the corresponding Hardware Color Register, unless it's Color
    *    Register 0 and the screen is blanked.
    *
    *    The cli()/sti() pair is here to protect bplcon3 from being changed by
    *    the VBlank interrupt routine.
    */

   cli();
   if (regno || !is_blanked) {
      custom.bplcon3 = bplcon3 | (regno<<8 & 0xe000);
      custom.color[regno&31] = rgb2hw_high(red, green, blue);
      custom.bplcon3 = bplcon3 | (regno<<8 & 0xe000) | BPC3_LOCT;
      custom.color[regno&31] = rgb2hw_low(red, green, blue);
      custom.bplcon3 = bplcon3;
   }
   sti();

   palette[regno].red = red;
   palette[regno].green = green;
   palette[regno].blue = blue;

   return(0);
}


   /*
    *    Pan or Wrap the Display
    *
    *    This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
    *    in `var'.
    */

static int aga_pan_display(struct fb_var_screeninfo *var,
                           struct amiga_fb_par *par)
{
   int xoffset, yoffset, vmode, xres_n, xoffset_n;
   u_short clk_shift, line_shift_incd;
   u_long ddfmin, ddfmax, ddfstrt, ddfstop, hscroll;

   xoffset = var->xoffset;
   yoffset = var->yoffset;
   if (var->vmode & FB_VMODE_YWRAP) {
      if (hw2ddfstrt(par->ddfstrt) < (par->bpp-1)*64 || xoffset ||
          yoffset < 0 || yoffset >= par->yres)
         return(-EINVAL);
      vmode = par->vmode | FB_VMODE_YWRAP;
   } else {
      if (par->diwstrt_h < 323)
         xoffset = up64(xoffset);
      if (xoffset < 0 || xoffset+par->xres > par->vxres ||
          yoffset < 0 || yoffset+par->yres > par->vyres)
         return(-EINVAL);
      vmode = par->vmode & ~FB_VMODE_YWRAP;
   }

   clk_shift = par->clk_shift;
   switch (vmode & FB_VMODE_MASK) {
      case FB_VMODE_NONINTERLACED:
         line_shift_incd = 1;
         break;
      case FB_VMODE_INTERLACED:
         line_shift_incd = 0;
         break;
      case FB_VMODE_DOUBLE:
         line_shift_incd = 2;
         break;
   }
   xres_n = par->xres<<clk_shift;
   xoffset_n = xoffset<<clk_shift;

   /*
    *    DMA timings
    */

   ddfmin = down64(xoffset_n);
   ddfmax = up64(xoffset_n+xres_n);
   hscroll = par->diwstrt_h-68-mod64(xoffset_n);
   ddfstrt = down64(hscroll);
   if (ddfstrt < 128)
      return(-EINVAL);
   hscroll -= ddfstrt;
   ddfstop = ddfstrt+ddfmax-ddfmin-(64<<clk_shift);
   par->bplcon1 = hscroll2hw(hscroll);
   par->ddfstrt = ddfstrt2hw(ddfstrt);
   par->ddfstop = ddfstop2hw(ddfstop);

   /*
    *    Bitplane calculations
    */

   par->bplpt0 = ZTWO_PADDR((u_long)videomemory+div8(ddfmin>>clk_shift)+
                            yoffset*par->next_line);
   par->bpl1mod = par->next_line-div8((ddfmax-ddfmin)>>clk_shift);
   par->bpl2mod = par->bpl1mod;
   switch (vmode & FB_VMODE_MASK) {
      case FB_VMODE_INTERLACED:
         par->bpl1mod += par->next_line;
         par->bpl2mod += par->next_line;
         break;
      case FB_VMODE_DOUBLE:
         par->bpl1mod -= par->next_line;
         break;
   }

   par->xoffset = var->xoffset = xoffset;
   par->yoffset = var->yoffset = yoffset;
   par->vmode = var->vmode = vmode;
   return(0);
}


   /*
    *    Change the video mode (called by VBlank interrupt)
    */

void aga_do_vmode(void)
{
   struct amiga_fb_par *par = &current_par;

   /*
    *    Rebuild the dynamic part of the Copper List and activate the right
    *    Copper List as soon as possible
    *
    *    Make sure we're in a Long Frame if the video mode is interlaced.
    *    This is always the case if we already were in an interlaced mode,
    *    since then the VBlank only calls us during a Long Frame.
    *    But this _is_ necessary if we're switching from a non-interlaced
    *    to an interlaced mode.
    */

   if ((par->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
      custom.vposw = 0x8000;
      aga_build_clist_dyn(clist_lof, clist_shf, 0, par);
      custom.cop2lc = (u_short *)ZTWO_PADDR(clist_lof);
      aga_build_clist_dyn(clist_shf, clist_lof, 1, par);
   } else {
      aga_build_clist_dyn(clist_lof, NULL, 0, par);
      custom.cop2lc = (u_short *)ZTWO_PADDR(clist_lof);
   }

   /*
    *    Update the hardware registers
    */

   if (full_vmode_change) {
      custom.fmode = par->fmode;
      custom.beamcon0 = par->beamcon0;
      if (par->beamcon0 & BMC0_VARBEAMEN) {
         custom.htotal = par->htotal;
         custom.vtotal = par->vtotal;
         custom.hsstrt = par->hsstrt;
         custom.hsstop = par->hsstop;
         custom.hbstrt = par->hsstrt;
         custom.hbstop = par->hsstop;
         custom.vsstrt = par->vsstrt;
         custom.vsstop = par->vsstop;
         custom.vbstrt = par->vsstrt;
         custom.vbstop = par->vsstop;
         custom.hcenter = par->hcenter;
      }
      custom.bplcon3 = par->bplcon3;
      full_vmode_change = 0;

      /*
       *    The minimum period for audio depends on htotal (for OCS/ECS/AGA)
       */

      if (boot_info.bi_amiga.chipset != CS_STONEAGE)
         amiga_audio_min_period = (par->htotal>>1)+1;
   }
   custom.ddfstrt = par->ddfstrt;
   custom.ddfstop = par->ddfstop;
   custom.bpl1mod = par->bpl1mod;
   custom.bpl2mod = par->bpl2mod;
   custom.bplcon1 = par->bplcon1;

   /*
    *    Update the Frame Header Copper List
    */

   aga_update_clist_hdr(clist_hdr, par);
}


   /*
    *    (Un)Blank the screen (called by VBlank interrupt)
    */

void aga_do_blank(int blank)
{
   struct amiga_fb_par *par = &current_par;
   u_short bplcon3 = par->bplcon3;
   u_char red, green, blue;

   if (blank) {
      custom.dmacon = DMAF_RASTER | DMAF_SPRITE;
      red = green = blue = 0;
      if (pwrsave && par->beamcon0 & BMC0_VARBEAMEN) {
         /* VESA suspend mode, switch off HSYNC */
         custom.hsstrt = par->htotal+2;
         custom.hsstop = par->htotal+2;
      }
   } else {
      custom.dmacon = DMAF_SETCLR | DMAF_RASTER;
      red = palette[0].red;
      green = palette[0].green;
      blue = palette[0].blue;
      if (pwrsave && par->beamcon0 & BMC0_VARBEAMEN) {
         custom.hsstrt = par->hsstrt;
         custom.hsstop = par->hsstop;
      }
   }
   custom.bplcon3 = bplcon3;
   custom.color[0] = rgb2hw_high(red, green, blue);
   custom.bplcon3 = bplcon3 | BPC3_LOCT;
   custom.color[0] = rgb2hw_low(red, green, blue);
   custom.bplcon3 = bplcon3;

   is_blanked = blank;
}


   /*
    *    Move the cursor (called by VBlank interrupt)
    */

void aga_do_movecursor(void)
{
   struct amiga_fb_par *par = &current_par;
   struct aga_cursorsprite *sprite = (struct aga_cursorsprite *)lofsprite;
   long hs, vs, ve;
   u_short s1, s2, is_double = 0;

   if (par->crsr_x <= -64 || par->crsr_x >= par->xres || par->crsr_y <= -64 ||
       par->crsr_y >= par->yres)
      hs = vs = ve = 0;
   else {
      hs = par->diwstrt_h-4+(par->crsr_x<<par->clk_shift);
      vs = par->crsr_y;
      ve = min(vs+64, par->yres);
      switch (par->vmode & FB_VMODE_MASK) {
         case FB_VMODE_INTERLACED:
            vs >>= 1;
            ve >>= 1;
            break;
         case FB_VMODE_DOUBLE:
            vs <<= 1;
            ve <<= 1;
            is_double = 1;
            break;
      }
      vs += par->diwstrt_v;
      ve += par->diwstrt_v;
   }
   s1 = spr2hw_pos(vs, hs);
   if (is_double)
      s1 |= 0x80;
   s2 = spr2hw_ctl(vs, hs, ve);
   sprite->sprpos = s1;
   sprite->sprctl = s2;

   /*
    *    TODO: Special cases:
    *    
    *      - Interlaced: fill position in in both lofsprite & shfsprite
    *                    swap lofsprite & shfsprite on odd lines
    *    
    *      - Doublescan: OK?
    *    
    *      - ve <= bottom of display: OK?
    */
}


   /*
    *    Flash the cursor (called by VBlank interrupt)
    */

void aga_do_flashcursor(void)
{
#if 1
   static int cursorcount = 0;
   static int cursorstate = 0;

   switch (cursormode) {
      case FB_CURSOR_OFF:
         custom.dmacon = DMAF_SPRITE;
         break;
      case FB_CURSOR_ON:
         custom.dmacon = DMAF_SETCLR | DMAF_SPRITE;
         break;
      case FB_CURSOR_FLASH:
         if (cursorcount)
            cursorcount--;
         else {
            cursorcount = CRSR_RATE;
            if ((cursorstate = !cursorstate))
               custom.dmacon = DMAF_SETCLR | DMAF_SPRITE;
            else
               custom.dmacon = DMAF_SPRITE;
         }
         break;
   }
#endif
}


#if 1
static int aga_get_fix_cursorinfo(struct fb_fix_cursorinfo *fix, int con)
{
#if 0
   if (ddfstrt >= 192) {
#endif
      fix->crsr_width = 64;
      fix->crsr_height = 64;
      fix->crsr_xsize = 64;
      fix->crsr_ysize = 64;
      fix->crsr_color1 = 17;
      fix->crsr_color2 = 18;
#if 0
   } else {
      fix->crsr_width = 0;
      fix->crsr_height = 0;
      fix->crsr_xsize = 0;
      fix->crsr_ysize = 0;
      fix->crsr_color1 = 0;
      fix->crsr_color2 = 0;
   }
#endif
   return(0);
}


static int aga_get_var_cursorinfo(struct fb_var_cursorinfo *var, int con)
{
   struct aga_cursorsprite *sprite = (struct aga_cursorsprite *)lofsprite;

   /* TODO: interlaced sprites */
   memcpy(var->data, sprite->u.nonlaced.data, sizeof(var->data));
   return(0);
}


static int aga_set_var_cursorinfo(struct fb_var_cursorinfo *var, int con)
{
   struct aga_cursorsprite *sprite = (struct aga_cursorsprite *)lofsprite;

   /* TODO: interlaced sprites */
   memcpy(sprite->u.nonlaced.data, var->data, sizeof(var->data));
   return(0);
}


static int aga_get_cursorstate(struct fb_cursorstate *state, int con)
{
   state->xoffset = current_par.crsr_x;
   state->yoffset = current_par.crsr_y;
   state->mode = cursormode;
   return(0);
}


static int aga_set_cursorstate(struct fb_cursorstate *state, int con)
{
   current_par.crsr_x = state->xoffset;
   current_par.crsr_y = state->yoffset;
   cursormode = state->mode;
   do_movecursor = 1;
   return(0);
}
#endif


   /*
    *    Build the Frame Header Copper List
    */

static __inline__ void aga_build_clist_hdr(struct clist_hdr *cop)
{
   int i, j;
   u_long p;

   cop->bplcon0.l = CMOVE(BPC0_COLOR | BPC0_SHRES | BPC0_ECSENA, bplcon0);
   cop->diwstrt.l = CMOVE(0x0181, diwstrt);
   cop->diwstop.l = CMOVE(0x0281, diwstop);
   cop->diwhigh.l = CMOVE(0x0000, diwhigh);
   for (i = 0; i < 8; i++)
      cop->sprfix[i].l = CMOVE(0, spr[i].pos);
   for (i = 0, j = 0; i < 8; i++) {
      p = ZTWO_PADDR(dummysprite);
      cop->sprstrtup[j++].l = CMOVE(highw(p), sprpt[i]);
      cop->sprstrtup[j++].l = CMOVE2(loww(p), sprpt[i]);
   }
   cop->wait.l = CWAIT(0, 12);         /* Initial value */
   cop->jump.l = CMOVE(0, copjmp2);
   cop->wait_forever.l = CEND;
}


   /*
    *    Update the Frame Header Copper List
    */

static __inline__ void aga_update_clist_hdr(struct clist_hdr *cop,
                                            struct amiga_fb_par *par)
{
   cop->bplcon0.l = CMOVE(~(BPC0_BPU3 | BPC0_BPU2 | BPC0_BPU1 | BPC0_BPU0) &
                          par->bplcon0, bplcon0);
   cop->wait.l = CWAIT(0, par->diwstrt_v-2);
}


   /*
    *    Build the Long Frame/Short Frame Copper List
    */

static void aga_build_clist_dyn(struct clist_dyn *cop,
                                struct clist_dyn *othercop, u_short shf,
                                struct amiga_fb_par *par)
{
   u_long y_wrap, bplpt0, p, line;
   int i, j = 0;

   cop->diwstrt.l = CMOVE(par->diwstrt, diwstrt);
   cop->diwstop.l = CMOVE(par->diwstop, diwstop);
   cop->diwhigh.l = CMOVE(par->diwhigh, diwhigh);
   cop->bplcon0.l = CMOVE(par->bplcon0, bplcon0);

   /* Point Sprite 0 at cursor sprite */

   /* TODO: This should depend on the vertical sprite position too */
   if (shf) {
      p = ZTWO_PADDR(shfsprite);
      cop->sprpt[0].l = CMOVE(highw(p), sprpt[0]);
      cop->sprpt[1].l = CMOVE2(loww(p), sprpt[0]);
   } else {
      p = ZTWO_PADDR(lofsprite);
      cop->sprpt[0].l = CMOVE(highw(p), sprpt[0]);
      cop->sprpt[1].l = CMOVE2(loww(p), sprpt[0]);
   }

   bplpt0 = par->bplpt0;
   if (shf)
      bplpt0 += par->next_line;
   y_wrap = par->vmode & FB_VMODE_YWRAP ? par->yoffset : 0;

   /* Set up initial bitplane ptrs */

   for (i = 0, p = bplpt0; i < par->bpp; i++, p += par->next_plane) {
      cop->rest[j++].l = CMOVE(highw(p), bplpt[i]);
      cop->rest[j++].l = CMOVE2(loww(p), bplpt[i]);
   }

   if (y_wrap) {
      bplpt0 -= y_wrap*par->next_line;
      line = par->yres-y_wrap;
      switch (par->vmode & FB_VMODE_MASK) {
         case FB_VMODE_INTERLACED:
            line >>= 1;
            break;
         case FB_VMODE_DOUBLE:
            line <<= 1;
            break;
      }
      line += par->diwstrt_v;

      /* Handle skipping over 256-line chunks */

      while (line > 256) {
         /* Hardware limitation - 8 bit counter */
         cop->rest[j++].l = CWAIT(par->htotal-4, 255);
         /* Wait(0, 0) - make sure we're in the new segment */
         cop->rest[j++].l = CWAIT(0, 0);
         line -= 256;
      }
      cop->rest[j++].l = CWAIT(par->htotal-11, line-1);

      for (i = 0, p = bplpt0; i < par->bpp; i++, p += par->next_plane) {
         cop->rest[j++].l = CMOVE(highw(p), bplpt[i]);
         cop->rest[j++].l = CMOVE2(loww(p), bplpt[i]);
      }
   }

   if (othercop) {
      p = ZTWO_PADDR(othercop);
      cop->rest[j++].l = CMOVE(highw(p), cop2lc);
      cop->rest[j++].l = CMOVE2(loww(p), cop2lc);
   }

   /* End of Copper list */
   cop->rest[j++].l = CEND;

   if (j > arraysize(cop->rest))
      printk("aga_build_clist_dyn: copper list overflow (%d entries)\n", j);
}
#endif /* CONFIG_AMIFB_AGA */


/* -------------------- Interfaces to hardware functions -------------------- */


#ifdef CONFIG_AMIFB_OCS
static struct fb_hwswitch ocs_switch = {
   ocs_init, ocs_encode_fix, ocs_decode_var, ocs_encode_var, ocs_getcolreg,
   ocs_setcolreg, ocs_pan_display, ocs_do_vmode, ocs_do_blank,
   ocs_do_movecursor, ocs_do_flashcursor
};
#endif /* CONFIG_AMIFB_OCS */

#ifdef CONFIG_AMIFB_ECS
static struct fb_hwswitch ecs_switch = {
   ecs_init, ecs_encode_fix, ecs_decode_var, ecs_encode_var, ecs_getcolreg,
   ecs_setcolreg, ecs_pan_display, ecs_do_vmode, ecs_do_blank,
   ecs_do_movecursor, ecs_do_flashcursor
};
#endif /* CONFIG_AMIFB_ECS */

#ifdef CONFIG_AMIFB_AGA
static struct fb_hwswitch aga_switch = {
   aga_init, aga_encode_fix, aga_decode_var, aga_encode_var, aga_getcolreg,
   aga_setcolreg, aga_pan_display, aga_do_vmode, aga_do_blank,
   aga_do_movecursor, aga_do_flashcursor
};
#endif /* CONFIG_AMIFB_AGA */


/* -------------------- Generic routines ------------------------------------ */


   /*
    *    Allocate, Clear and Align a Block of Chip Memory
    */

static u_long chipalloc(u_long size)
{
   u_long ptr;

   size += PAGE_SIZE-1;
   if (!(ptr = (u_long)amiga_chip_alloc(size)))
      panic("No Chip RAM for frame buffer");
   memset((void *)ptr, 0, size);
   ptr = PAGE_ALIGN(ptr);

   return(ptr);
}


   /*
    *    Fill the hardware's `par' structure.
    */

static void amiga_fb_get_par(struct amiga_fb_par *par)
{
   if (current_par_valid)
      *par = current_par;
   else
      fbhw->decode_var(&amiga_fb_predefined[amifb_mode], par);
}


static void amiga_fb_set_par(struct amiga_fb_par *par)
{
   do_vmode = 0;
   current_par = *par;
   full_vmode_change = 1;
   do_vmode = 1;
   current_par_valid = 1;
}


static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive)
{
   int err, activate;
   struct amiga_fb_par par;

   if ((err = fbhw->decode_var(var, &par)))
      return(err);
   activate = var->activate;
   if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW && isactive)
      amiga_fb_set_par(&par);
   fbhw->encode_var(var, &par);
   var->activate = activate;
   return(0);
}


   /*
    *    Default Colormaps
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


static struct fb_cmap *get_default_colormap(int bpp)
{
   switch (bpp) {
      case 1:
         return(&default_2_colors);
         break;
      case 2:
         return(&default_4_colors);
         break;
      case 3:
         return(&default_8_colors);
         break;
      default:
         return(&default_16_colors);
         break;
   }
}


#define CNVT_TOHW(val,width)     ((((val)<<(width))+0x7fff-(val))>>16)
#define CNVT_FROMHW(val,width)   (((width) ? ((((val)<<16)-(val)) / \
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
      return(-EINVAL);
   for (i = 0; i < cmap->len; i++) {
      if (fbhw->getcolreg(start++, &hred, &hgreen, &hblue, &htransp))
         return(0);
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
         put_fs_word(hred, red);
         put_fs_word(hgreen, green);
         put_fs_word(hblue, blue);
         if (transp)
            put_fs_word(htransp, transp);
      }
      red++;
      green++;
      blue++;
      if (transp)
         transp++;
   }
   return(0);
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
      return(-EINVAL);
   for (i = 0; i < cmap->len; i++) {
      if (kspc) {
         hred = *red;
         hgreen = *green;
         hblue = *blue;
         htransp = transp ? *transp : 0;
      } else {
         hred = get_fs_word(red);
         hgreen = get_fs_word(green);
         hblue = get_fs_word(blue);
         htransp = transp ? get_fs_word(transp) : 0;
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
      if (fbhw->setcolreg(start++, hred, hgreen, hblue, htransp))
         return(0);
   }
   return(0);
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
         memcpy_fromfs(to, from, len);
         return;
      case 2:
         memcpy_tofs(to, from, len);
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
         return(0);
      if (!(cmap->red = kmalloc(size, GFP_ATOMIC)))
         return(-1);
      if (!(cmap->green = kmalloc(size, GFP_ATOMIC)))
         return(-1);
      if (!(cmap->blue = kmalloc(size, GFP_ATOMIC)))
         return(-1);
      if (transp) {
         if (!(cmap->transp = kmalloc(size, GFP_ATOMIC)))
            return(-1);
      } else
         cmap->transp = NULL;
   }
   cmap->start = 0;
   cmap->len = len;
   copy_cmap(get_default_colormap(len), cmap, 0);
   return(0);
}


   /*
    *    Get the Fixed Part of the Display
    */

static int amiga_fb_get_fix(struct fb_fix_screeninfo *fix, int con)
{
   struct amiga_fb_par par;
   int error = 0;

   if (con == -1)
      amiga_fb_get_par(&par);
   else
      error = fbhw->decode_var(&disp[con].var, &par);
   return(error ? error : fbhw->encode_fix(fix, &par));
}


   /*
    *    Get the User Defined Part of the Display
    */

static int amiga_fb_get_var(struct fb_var_screeninfo *var, int con)
{
   struct amiga_fb_par par;
   int error = 0;

   if (con == -1) {
      amiga_fb_get_par(&par);
      error = fbhw->encode_var(var, &par);
   } else
      *var = disp[con].var;
   return(error);
}


static void amiga_fb_set_disp(int con)
{
   struct fb_fix_screeninfo fix;

   amiga_fb_get_fix(&fix, con);
   if (con == -1)
      con = 0;
   disp[con].screen_base = (u_char *)fix.smem_start;
   disp[con].visual = fix.visual;
   disp[con].type = fix.type;
   disp[con].type_aux = fix.type_aux;
   disp[con].ypanstep = fix.ypanstep;
   disp[con].ywrapstep = fix.ywrapstep;
   disp[con].line_length = fix.line_length;
   disp[con].can_soft_blank = 1;
   disp[con].inverse = amifb_inverse;
}


   /*
    *    Set the User Defined Part of the Display
    */

static int amiga_fb_set_var(struct fb_var_screeninfo *var, int con)
{
   int err, oldxres, oldyres, oldvxres, oldvyres, oldbpp;

   if ((err = do_fb_set_var(var, con == currcon)))
      return(err);
   if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
      oldxres = disp[con].var.xres;
      oldyres = disp[con].var.yres;
      oldvxres = disp[con].var.xres_virtual;
      oldvyres = disp[con].var.yres_virtual;
      oldbpp = disp[con].var.bits_per_pixel;
      disp[con].var = *var;
      if (oldxres != var->xres || oldyres != var->yres ||
          oldvxres != var->xres_virtual || oldvyres != var->yres_virtual ||
          oldbpp != var->bits_per_pixel) {
         amiga_fb_set_disp(con);
         (*fb_info.changevar)(con);
         alloc_cmap(&disp[con].cmap, 0, 0);
         do_install_cmap(con);
      }
   }
   var->activate = 0;
   return(0);
}


   /*
    *    Get the Colormap
    */

static int amiga_fb_get_cmap(struct fb_cmap *cmap, int kspc, int con)
{
   if (con == currcon) /* current console? */
      return(do_fb_get_cmap(cmap, &disp[con].var, kspc));
   else if (disp[con].cmap.len) /* non default colormap? */
      copy_cmap(&disp[con].cmap, cmap, kspc ? 0 : 2);
   else
      copy_cmap(get_default_colormap(disp[con].var.bits_per_pixel), cmap,
                kspc ? 0 : 2);
   return(0);
}


   /*
    *    Set the Colormap
    */

static int amiga_fb_set_cmap(struct fb_cmap *cmap, int kspc, int con)
{
   int err;

   if (!disp[con].cmap.len) {       /* no colormap allocated? */
      if ((err = alloc_cmap(&disp[con].cmap, 1<<disp[con].var.bits_per_pixel,
                            0)))
         return(err);
   }
   if (con == currcon)              /* current console? */
      return(do_fb_set_cmap(cmap, &disp[con].var, kspc));
   else
      copy_cmap(cmap, &disp[con].cmap, kspc ? 0 : 1);
   return(0);
}


   /*
    *    Pan or Wrap the Display
    *
    *    This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
    */

static int amiga_fb_pan_display(struct fb_var_screeninfo *var, int con)
{
   int err;
   u_short oldlatch;

   if (var->vmode & FB_VMODE_YWRAP) {
      if (var->xoffset || var->yoffset >= disp[con].var.yres)
         return(-EINVAL);
   } else {
      if (var->xoffset+disp[con].var.xres > disp[con].var.xres_virtual ||
          var->yoffset+disp[con].var.yres > disp[con].var.yres_virtual)
    return(-EINVAL);
   }
   if (con == currcon) {
      cli();
      oldlatch = do_vmode;
      do_vmode = 0;
      sti();
      if ((err = fbhw->pan_display(var, &current_par))) {
         if (oldlatch)
            do_vmode = 1;
         return(err);
      }
      do_vmode = 1;
   }
   disp[con].var.xoffset = var->xoffset;
   disp[con].var.yoffset = var->yoffset;
   if (var->vmode & FB_VMODE_YWRAP)
      disp[con].var.vmode |= FB_VMODE_YWRAP;
   else
      disp[con].var.vmode &= ~FB_VMODE_YWRAP;
   return(0);
}


   /*
    *    Amiga Frame Buffer Specific ioctls
    */

static int amiga_fb_ioctl(struct inode *inode, struct file *file,
                          u_int cmd, u_long arg, int con)
{
   int i;
   struct fb_fix_cursorinfo crsrfix;
   struct fb_var_cursorinfo crsrvar;
   struct fb_cursorstate crsrstate;

   switch (cmd) {
#ifdef CONFIG_AMIFB_AGA
      case FBIOGET_FCURSORINFO:
         i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(crsrfix));
         if (i)
            return(i);
         i = amiga_fb_get_fix_cursorinfo(&crsrfix, con);
         memcpy_tofs((void *)arg, &crsrfix, sizeof(crsrfix));
         return(i);
      case FBIOGET_VCURSORINFO:
         i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(crsrvar));
         if (i)
            return(i);
         i = amiga_fb_get_var_cursorinfo(&crsrvar, con);
         memcpy_tofs((void *)arg, &crsrvar, sizeof(crsrvar));
         return(i);
      case FBIOPUT_VCURSORINFO:
         i = verify_area(VERIFY_READ, (void *)arg, sizeof(crsrvar));
         if (i)
            return(i);
         memcpy_fromfs(&crsrvar, (void *)arg, sizeof(crsrvar));
         i = amiga_fb_set_var_cursorinfo(&crsrvar, con);
         return(i);
      case FBIOGET_CURSORSTATE:
         i = verify_area(VERIFY_WRITE, (void *)arg, sizeof(crsrstate));
         if (i)
            return(i);
         i = amiga_fb_get_cursorstate(&crsrstate, con);
         memcpy_tofs((void *)arg, &crsrstate, sizeof(crsrstate));
         return(i);
      case FBIOPUT_CURSORSTATE:
         i = verify_area(VERIFY_READ, (void *)arg, sizeof(crsrstate));
         if (i)
            return(i);
         memcpy_fromfs(&crsrstate, (void *)arg, sizeof(crsrstate));
         i = amiga_fb_set_cursorstate(&crsrstate, con);
         return(i);
#endif /* CONFIG_AMIFB_AGA */
#if 1
      case FBCMD_GET_CURRENTPAR:
         if ((i = verify_area(VERIFY_WRITE, (void *)arg,
                              sizeof(struct amiga_fb_par))))
            return(i);
         memcpy_tofs((void *)arg, (void *)&current_par,
                     sizeof(struct amiga_fb_par));
         return(0);
         break;
      case FBCMD_SET_CURRENTPAR:
         if ((i = verify_area(VERIFY_READ, (void *)arg,
                              sizeof(struct amiga_fb_par))))
            return(i);
         memcpy_fromfs((void *)&current_par, (void *)arg,
                       sizeof(struct amiga_fb_par));
         return(0);
         break;
#endif
   }
   return(-EINVAL);
}


#ifdef CONFIG_AMIFB_AGA
   /*
    *    Hardware Cursor
    */

static int amiga_fb_get_fix_cursorinfo(struct fb_fix_cursorinfo *fix, int con)
{
   if (boot_info.bi_amiga.chipset == CS_AGA)
      return(aga_get_fix_cursorinfo(fix, con));
   return(-EINVAL);
}


static int amiga_fb_get_var_cursorinfo(struct fb_var_cursorinfo *var, int con)
{
   if (boot_info.bi_amiga.chipset == CS_AGA)
      return(aga_get_var_cursorinfo(var, con));
   return(-EINVAL);
}


static int amiga_fb_set_var_cursorinfo(struct fb_var_cursorinfo *var, int con)
{
   if (boot_info.bi_amiga.chipset == CS_AGA)
      return(aga_set_var_cursorinfo(var, con));
   return(-EINVAL);
}


static int amiga_fb_get_cursorstate(struct fb_cursorstate *state, int con)
{
   if (boot_info.bi_amiga.chipset == CS_AGA)
      return(aga_get_cursorstate(state, con));
   return(-EINVAL);
}


static int amiga_fb_set_cursorstate(struct fb_cursorstate *state, int con)
{
   if (boot_info.bi_amiga.chipset == CS_AGA)
      return(aga_set_cursorstate(state, con));
   return(-EINVAL);
}
#endif /* CONFIG_AMIFB_AGA */


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
    *    Check for a Graphics Board
    */

#ifdef CONFIG_FB_CYBER
   if (options && *options)
      if (!strncmp(options, "cyber", 5) && Cyber_probe()) {
         amifb_Cyber = 1;
         Cyber_video_setup(options, ints);
         return;
      }
#endif /* CONFIG_FB_CYBER */

#ifdef USE_MONO_AMIFB_IF_NON_AGA
   if (boot_info.bi_amiga.chipset != CS_AGA) {
      mono_video_setup(options, ints);
      return;
   }
#endif /* USE_MONO_AMIFB_IF_NON_AGA */

   mcap_spec[0] = '\0';
   fb_info.fontname[0] = '\0';

   if (!options || !*options)
      return;

   for (this_opt = strtok(options, ","); this_opt; this_opt = strtok(NULL, ","))
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
      else if (!strcmp(this_opt, "pwrsave"))
         pwrsave = 1;
      else if (!strncmp(this_opt, "monitorcap:", 11))
         strcpy(mcap_spec, this_opt+11);
      else if (!strncmp(this_opt, "font:", 5))
         strcpy(fb_info.fontname, this_opt+5);
      else
         amifb_mode = get_video_mode(this_opt);

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
    *    Initialization
    */

struct fb_info *amiga_fb_init(long *mem_start)
{
   int err, tag, i;
   struct fb_var_screeninfo *var;

   /*
    *    Check for a Graphics Board
    */

#ifdef CONFIG_FB_CYBER
   if (amifb_Cyber)
      return(Cyber_fb_init(mem_start));
#endif /* CONFIG_FB_CYBER */

   /*
    *    Use the Builtin Chipset
    */

   if (!AMIGAHW_PRESENT(AMI_VIDEO))
      return(NULL);

#ifdef USE_MONO_AMIFB_IF_NON_AGA
   if (boot_info.bi_amiga.chipset != CS_AGA)
      return(mono_amiga_fb_init(mem_start));
#endif /* USE_MONO_AMIFB_IF_NON_AGA */

   switch (boot_info.bi_amiga.chipset) {
#ifdef CONFIG_AMIFB_OCS
      case CS_OCS:
         strcat(amiga_fb_name, "OCS");
default_chipset:
         fbhw = &ocs_switch;
         maxdepth[TAG_SHRES-1] = 0;       /* OCS means no SHRES */
         maxdepth[TAG_HIRES-1] = 4;
         maxdepth[TAG_LORES-1] = 6;
         break;
#endif /* CONFIG_AMIFB_OCS */

#ifdef CONFIG_AMIFB_ECS
      case CS_ECS:
         strcat(amiga_fb_name, "ECS");
         fbhw = &ecs_switch;
         maxdepth[TAG_SHRES-1] = 2;
         maxdepth[TAG_HIRES-1] = 4;
         maxdepth[TAG_LORES-1] = 6;
         break;
#endif /* CONFIG_AMIFB_ECS */

#ifdef CONFIG_AMIFB_AGA
      case CS_AGA:
         strcat(amiga_fb_name, "AGA");
         fbhw = &aga_switch;
         maxdepth[TAG_SHRES-1] = 8;
         maxdepth[TAG_HIRES-1] = 8;
         maxdepth[TAG_LORES-1] = 8;
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
    *    Calculate the Pixel Clock Values for this Machine
    */

   __asm("movel %3,%%d0;"
         "movel #0x00000005,%%d1;"     /*  25E9: SHRES:  35 ns / 28 MHz */
         "movel #0xd21dba00,%%d2;"
         "divul %%d0,%%d1,%%d2;"
         "movel %%d2,%0;"
         "movel #0x0000000b,%%d1;"     /*  50E9: HIRES:  70 ns / 14 MHz */
         "movel #0xa43b7400,%%d2;"
         "divul %%d0,%%d1,%%d2;"
         "movel %%d2,%1;"
         "movel #0x00000017,%%d1;"     /* 100E9: LORES: 140 ns /  7 MHz */
         "movel #0x4876e800,%%d2;"
         "divul %%d0,%%d1,%%d2;"
         "movel %%d2,%2"
         : "=r" (pixclock[TAG_SHRES-1]), "=r" (pixclock[TAG_HIRES-1]),
           "=r" (pixclock[TAG_LORES-1])
         : "r" (amiga_eclock)
         : "%%d0", "%%d1", "%%d2");

   /*
    *    Replace the Tag Values with the Real Pixel Clock Values
    */

   for (i = 0; i < NUM_PREDEF_MODES; i++) {
      tag = amiga_fb_predefined[i].pixclock;
      if (tag == TAG_SHRES || tag == TAG_HIRES || tag == TAG_LORES) {
         amiga_fb_predefined[i].pixclock = pixclock[tag-1];
         if (amiga_fb_predefined[i].bits_per_pixel > maxdepth[tag-1])
            amiga_fb_predefined[i].bits_per_pixel = maxdepth[tag-1];
      }
   }

   err = register_framebuffer(amiga_fb_name, &node, &amiga_fb_ops,
                              NUM_TOTAL_MODES, amiga_fb_predefined);
   if (err < 0)
      panic("Cannot register frame buffer");

   fbhw->init();
   check_default_mode();

   if (!add_isr(IRQ_AMIGA_VERTB, amifb_interrupt, 0, NULL, "frame buffer"))
      panic("Couldn't add vblank interrupt");

   strcpy(fb_info.modename, amiga_fb_name);
   fb_info.disp = disp;
   fb_info.switch_con = &amifb_switch;
   fb_info.updatevar = &amifb_updatevar;
   fb_info.blank = &amifb_blank;

   var = &amiga_fb_predefined[amifb_mode];
   do_fb_set_var(var, 1);
   strcat(fb_info.modename, " ");
   strcat(fb_info.modename, amiga_fb_modenames[amifb_mode]);

   amiga_fb_get_var(&disp[0].var, -1);
   amiga_fb_set_disp(-1);
   do_install_cmap(0);
   return(&fb_info);
}


static int amifb_switch(int con)
{
   /* Do we have to save the colormap? */
   if (disp[currcon].cmap.len)
      do_fb_get_cmap(&disp[currcon].cmap, &disp[currcon].var, 1);

   do_fb_set_var(&disp[con].var, 1);
   currcon = con;
   /* Install new colormap */
   do_install_cmap(con);
   return(0);
}


   /*
    *    Update the `var' structure (called by fbcon.c)
    *
    *    This call looks only at yoffset and the FB_VMODE_YWRAP flag in `var'.
    *    Since it's called by a kernel driver, no range checking is done.
    */

static int amifb_updatevar(int con)
{
   do_vmode = 0;
   current_par.yoffset = disp[con].var.yoffset;
   current_par.vmode = disp[con].var.vmode;
   current_par.bplpt0 = ZTWO_PADDR((u_long)videomemory+
                                   current_par.yoffset*current_par.next_line);
   do_vmode = 1;
   return(0);
}


   /*
    *    Blank the display.
    */

static void amifb_blank(int blank)
{
   do_blank = blank ? 1 : -1;
}


   /*
    *    VBlank Display Interrupt
    */

static void amifb_interrupt(int irq, struct pt_regs *fp, void *dummy)
{
   static int is_laced = 0;

#if 0
   /*
    *    This test should be here, in case current_par isn't initialized yet
    *
    *    Fortunately only do_flashcursor() will be called in that case, and
    *    currently that function doesn't use current_par. But this may change
    *    in future...
    */
   if (!current_par_valid)
      return;
#endif

   /*
    *    If interlaced, only change the display on a long frame
    */

   if (!is_laced || custom.vposr & 0x8000) {
      if (do_vmode) {
         fbhw->do_vmode();
         do_vmode = 0;
         is_laced = (current_par.vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED;
      }
      if (do_movecursor) {
         fbhw->do_movecursor();
         do_movecursor = 0;
      }
   }
   if (do_blank) {
      fbhw->do_blank(do_blank > 0 ? 1 : 0);
      do_blank = 0;
   }
   if (!is_blanked)
      fbhw->do_flashcursor();
}


   /*
    *    A strtok which returns empty strings, too
    */

static char * strtoke(char * s,const char * ct)
{
   char *sbegin, *send;
   static char *ssave = NULL;
  
   sbegin  = s ? s : ssave;
   if (!sbegin)
      return(NULL);
   if (*sbegin == '\0') {
      ssave = NULL;
      return(NULL);
   }
   send = strpbrk(sbegin, ct);
   if (send && *send != '\0')
      *send++ = '\0';
   ssave = send;
   return(sbegin);
}


   /*
    *    Get a Video Modes
    */

static int get_video_mode(const char *name)
{
   int i;

   for (i = 1; i < NUM_PREDEF_MODES; i++)
      if (!strcmp(name, amiga_fb_modenames[i]))
         return(i);
   return(0);
}


   /*
    *    Check the Default Video Mode
    */

static void check_default_mode(void)
{
   struct fb_var_screeninfo var;

   /* First check the user supplied or system default video mode */
   if (amifb_mode) {
      var = amiga_fb_predefined[amifb_mode];
      var.activate = FB_ACTIVATE_TEST;
      if (!do_fb_set_var(&var, 1))
         goto found_video_mode;
   }

   /* Try some other modes... */
   printk("Can't use default video mode. Probing video modes...\n");
   for (amifb_mode = 1; amifb_mode < NUM_PREDEF_MODES; amifb_mode++) {
      var = amiga_fb_predefined[amifb_mode];
      var.activate = FB_ACTIVATE_TEST;
      if (!do_fb_set_var(&var, 1))
         goto found_video_mode;
   }
   panic("Can't find any usable video mode");

found_video_mode:
   amiga_fb_predefined[0] = var;
}
