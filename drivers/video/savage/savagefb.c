/*
 * linux/drivers/video/savagefb.c -- S3 Savage Framebuffer Driver
 *
 * Copyright (c) 2001-2002  Denis Oliver Kropp <dok@directfb.org>
 *                          Sven Neumann <neo@directfb.org>
 *
 *
 * Card specific code is based on XFree86's savage driver.
 * Framebuffer framework code is based on code of cyber2000fb and tdfxfb.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * 0.4.0 (neo)
 *  - hardware accelerated clear and move
 *
 * 0.3.2 (dok)
 *  - wait for vertical retrace before writing to cr67
 *    at the beginning of savagefb_set_par
 *  - use synchronization registers cr23 and cr26
 *
 * 0.3.1 (dok)
 *  - reset 3D engine
 *  - don't return alpha bits for 32bit format
 *
 * 0.3.0 (dok)
 *  - added WaitIdle functions for all Savage types
 *  - do WaitIdle before mode switching
 *  - code cleanup
 *
 * 0.2.0 (dok)
 *  - first working version
 *
 *
 * TODO
 * - clock validations in decode_var
 *
 * BUGS
 * - white margin on bootup
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/console.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include "savagefb.h"


#define SAVAGEFB_VERSION "0.4.0_2.6"

/* --------------------------------------------------------------------- */


static char *mode_option __initdata = NULL;
static int   paletteEnabled = 0;

#ifdef MODULE

MODULE_AUTHOR("(c) 2001-2002  Denis Oliver Kropp <dok@directfb.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FBDev driver for S3 Savage PCI/AGP Chips");

#endif


/* --------------------------------------------------------------------- */

static void vgaHWSeqReset (struct savagefb_par *par, int start)
{
	if (start)
		VGAwSEQ (0x00, 0x01);		/* Synchronous Reset */
	else
		VGAwSEQ (0x00, 0x03);		/* End Reset */
}

static void vgaHWProtect (struct savagefb_par *par, int on)
{
	unsigned char tmp;

	if (on) {
		/*
		 * Turn off screen and disable sequencer.
		 */
		tmp = VGArSEQ (0x01);

		vgaHWSeqReset (par, 1);	        /* start synchronous reset */
		VGAwSEQ (0x01, tmp | 0x20);	/* disable the display */

		VGAenablePalette();
	} else {
		/*
		 * Reenable sequencer, then turn on screen.
		 */

		tmp = VGArSEQ (0x01);

		VGAwSEQ (0x01, tmp & ~0x20);	/* reenable display */
		vgaHWSeqReset (par, 0);	        /* clear synchronous reset */

		VGAdisablePalette();
	}
}

static void vgaHWRestore (struct savagefb_par  *par)
{
	int i;

	VGAwMISC (par->MiscOutReg);

	for (i = 1; i < 5; i++)
		VGAwSEQ (i, par->Sequencer[i]);

	/* Ensure CRTC registers 0-7 are unlocked by clearing bit 7 or
	   CRTC[17] */
	VGAwCR (17, par->CRTC[17] & ~0x80);

	for (i = 0; i < 25; i++)
		VGAwCR (i, par->CRTC[i]);

	for (i = 0; i < 9; i++)
		VGAwGR (i, par->Graphics[i]);

	VGAenablePalette();

	for (i = 0; i < 21; i++)
		VGAwATTR (i, par->Attribute[i]);

	VGAdisablePalette();
}

static void vgaHWInit (struct fb_var_screeninfo *var,
		       struct savagefb_par            *par,
		       struct xtimings                *timings)
{
	par->MiscOutReg = 0x23;

	if (!(timings->sync & FB_SYNC_HOR_HIGH_ACT))
		par->MiscOutReg |= 0x40;

	if (!(timings->sync & FB_SYNC_VERT_HIGH_ACT))
		par->MiscOutReg |= 0x80;

	/*
	 * Time Sequencer
	 */
	par->Sequencer[0x00] = 0x00;
	par->Sequencer[0x01] = 0x01;
	par->Sequencer[0x02] = 0x0F;
	par->Sequencer[0x03] = 0x00;          /* Font select */
	par->Sequencer[0x04] = 0x0E;          /* Misc */

	/*
	 * CRTC Controller
	 */
	par->CRTC[0x00] = (timings->HTotal >> 3) - 5;
	par->CRTC[0x01] = (timings->HDisplay >> 3) - 1;
	par->CRTC[0x02] = (timings->HSyncStart >> 3) - 1;
	par->CRTC[0x03] = (((timings->HSyncEnd >> 3)  - 1) & 0x1f) | 0x80;
	par->CRTC[0x04] = (timings->HSyncStart >> 3);
	par->CRTC[0x05] = ((((timings->HSyncEnd >> 3) - 1) & 0x20) << 2) |
		(((timings->HSyncEnd >> 3)) & 0x1f);
	par->CRTC[0x06] = (timings->VTotal - 2) & 0xFF;
	par->CRTC[0x07] = (((timings->VTotal - 2) & 0x100) >> 8) |
		(((timings->VDisplay - 1) & 0x100) >> 7) |
		((timings->VSyncStart & 0x100) >> 6) |
		(((timings->VSyncStart - 1) & 0x100) >> 5) |
		0x10 |
		(((timings->VTotal - 2) & 0x200) >> 4) |
		(((timings->VDisplay - 1) & 0x200) >> 3) |
		((timings->VSyncStart & 0x200) >> 2);
	par->CRTC[0x08] = 0x00;
	par->CRTC[0x09] = (((timings->VSyncStart - 1) & 0x200) >> 4) | 0x40;

	if (timings->dblscan)
		par->CRTC[0x09] |= 0x80;

	par->CRTC[0x0a] = 0x00;
	par->CRTC[0x0b] = 0x00;
	par->CRTC[0x0c] = 0x00;
	par->CRTC[0x0d] = 0x00;
	par->CRTC[0x0e] = 0x00;
	par->CRTC[0x0f] = 0x00;
	par->CRTC[0x10] = timings->VSyncStart & 0xff;
	par->CRTC[0x11] = (timings->VSyncEnd & 0x0f) | 0x20;
	par->CRTC[0x12] = (timings->VDisplay - 1) & 0xff;
	par->CRTC[0x13] = var->xres_virtual >> 4;
	par->CRTC[0x14] = 0x00;
	par->CRTC[0x15] = (timings->VSyncStart - 1) & 0xff;
	par->CRTC[0x16] = (timings->VSyncEnd - 1) & 0xff;
	par->CRTC[0x17] = 0xc3;
	par->CRTC[0x18] = 0xff;

	/*
	 * are these unnecessary?
	 * vgaHWHBlankKGA(mode, regp, 0, KGA_FIX_OVERSCAN|KGA_ENABLE_ON_ZERO);
	 * vgaHWVBlankKGA(mode, regp, 0, KGA_FIX_OVERSCAN|KGA_ENABLE_ON_ZERO);
	 */

	/*
	 * Graphics Display Controller
	 */
	par->Graphics[0x00] = 0x00;
	par->Graphics[0x01] = 0x00;
	par->Graphics[0x02] = 0x00;
	par->Graphics[0x03] = 0x00;
	par->Graphics[0x04] = 0x00;
	par->Graphics[0x05] = 0x40;
	par->Graphics[0x06] = 0x05;   /* only map 64k VGA memory !!!! */
	par->Graphics[0x07] = 0x0F;
	par->Graphics[0x08] = 0xFF;


	par->Attribute[0x00]  = 0x00; /* standard colormap translation */
	par->Attribute[0x01]  = 0x01;
	par->Attribute[0x02]  = 0x02;
	par->Attribute[0x03]  = 0x03;
	par->Attribute[0x04]  = 0x04;
	par->Attribute[0x05]  = 0x05;
	par->Attribute[0x06]  = 0x06;
	par->Attribute[0x07]  = 0x07;
	par->Attribute[0x08]  = 0x08;
	par->Attribute[0x09]  = 0x09;
	par->Attribute[0x0a] = 0x0A;
	par->Attribute[0x0b] = 0x0B;
	par->Attribute[0x0c] = 0x0C;
	par->Attribute[0x0d] = 0x0D;
	par->Attribute[0x0e] = 0x0E;
	par->Attribute[0x0f] = 0x0F;
	par->Attribute[0x10] = 0x41;
	par->Attribute[0x11] = 0xFF;
	par->Attribute[0x12] = 0x0F;
	par->Attribute[0x13] = 0x00;
	par->Attribute[0x14] = 0x00;
}

/* -------------------- Hardware specific routines ------------------------- */

/*
 * Hardware Acceleration for SavageFB
 */

/* Wait for fifo space */
static void
savage3D_waitfifo(struct savagefb_par *par, int space)
{
	int slots = MAXFIFO - space;

	while ((savage_in32(0x48C00) & 0x0000ffff) > slots);
}

static void
savage4_waitfifo(struct savagefb_par *par, int space)
{
	int slots = MAXFIFO - space;

	while ((savage_in32(0x48C60) & 0x001fffff) > slots);
}

static void
savage2000_waitfifo(struct savagefb_par *par, int space)
{
	int slots = MAXFIFO - space;

	while ((savage_in32(0x48C60) & 0x0000ffff) > slots);
}

/* Wait for idle accelerator */
static void
savage3D_waitidle(struct savagefb_par *par)
{
	while ((savage_in32(0x48C00) & 0x0008ffff) != 0x80000);
}

static void
savage4_waitidle(struct savagefb_par *par)
{
	while ((savage_in32(0x48C60) & 0x00a00000) != 0x00a00000);
}

static void
savage2000_waitidle(struct savagefb_par *par)
{
	while ((savage_in32(0x48C60) & 0x009fffff));
}


static void
SavageSetup2DEngine (struct savagefb_par  *par)
{
	unsigned long GlobalBitmapDescriptor;

	GlobalBitmapDescriptor = 1 | 8 | BCI_BD_BW_DISABLE;
	BCI_BD_SET_BPP (GlobalBitmapDescriptor, par->depth);
	BCI_BD_SET_STRIDE (GlobalBitmapDescriptor, par->vwidth);

	switch(par->chip) {
	case S3_SAVAGE3D:
	case S3_SAVAGE_MX:
		/* Disable BCI */
		savage_out32(0x48C18, savage_in32(0x48C18) & 0x3FF0);
		/* Setup BCI command overflow buffer */
		savage_out32(0x48C14, (par->cob_offset >> 11) | (par->cob_index << 29));
		/* Program shadow status update. */
		savage_out32(0x48C10, 0x78207220);
		savage_out32(0x48C0C, 0);
		/* Enable BCI and command overflow buffer */
		savage_out32(0x48C18, savage_in32(0x48C18) | 0x0C);
		break;
	case S3_SAVAGE4:
	case S3_PROSAVAGE:
	case S3_SUPERSAVAGE:
		/* Disable BCI */
		savage_out32(0x48C18, savage_in32(0x48C18) & 0x3FF0);
		/* Program shadow status update */
		savage_out32(0x48C10, 0x00700040);
		savage_out32(0x48C0C, 0);
		/* Enable BCI without the COB */
		savage_out32(0x48C18, savage_in32(0x48C18) | 0x08);
		break;
	case S3_SAVAGE2000:
		/* Disable BCI */
		savage_out32(0x48C18, 0);
		/* Setup BCI command overflow buffer */
		savage_out32(0x48C18, (par->cob_offset >> 7) | (par->cob_index));
		/* Disable shadow status update */
		savage_out32(0x48A30, 0);
		/* Enable BCI and command overflow buffer */
		savage_out32(0x48C18, savage_in32(0x48C18) | 0x00280000 );
		break;
	    default:
		break;
	}
	/* Turn on 16-bit register access. */
	vga_out8(0x3d4, 0x31);
	vga_out8(0x3d5, 0x0c);

	/* Set stride to use GBD. */
	vga_out8 (0x3d4, 0x50);
	vga_out8 (0x3d5, vga_in8 (0x3d5 ) | 0xC1);

	/* Enable 2D engine. */
	vga_out8 (0x3d4, 0x40 );
	vga_out8 (0x3d5, 0x01 );

	savage_out32 (MONO_PAT_0, ~0);
	savage_out32 (MONO_PAT_1, ~0);

	/* Setup plane masks */
	savage_out32 (0x8128, ~0 ); /* enable all write planes */
	savage_out32 (0x812C, ~0 ); /* enable all read planes */
	savage_out16 (0x8134, 0x27 );
	savage_out16 (0x8136, 0x07 );

	/* Now set the GBD */
	par->bci_ptr = 0;
	par->SavageWaitFifo (par, 4);

	BCI_SEND( BCI_CMD_SETREG | (1 << 16) | BCI_GBD1 );
	BCI_SEND( 0 );
	BCI_SEND( BCI_CMD_SETREG | (1 << 16) | BCI_GBD2 );
	BCI_SEND( GlobalBitmapDescriptor );
}


static void SavageCalcClock(long freq, int min_m, int min_n1, int max_n1,
			    int min_n2, int max_n2, long freq_min,
			    long freq_max, unsigned int *mdiv,
			    unsigned int *ndiv, unsigned int *r)
{
	long diff, best_diff;
	unsigned int m;
	unsigned char n1, n2, best_n1=16+2, best_n2=2, best_m=125+2;

	if (freq < freq_min / (1 << max_n2)) {
		printk (KERN_ERR "invalid frequency %ld Khz\n", freq);
		freq = freq_min / (1 << max_n2);
	}
	if (freq > freq_max / (1 << min_n2)) {
		printk (KERN_ERR "invalid frequency %ld Khz\n", freq);
		freq = freq_max / (1 << min_n2);
	}

	/* work out suitable timings */
	best_diff = freq;

	for (n2=min_n2; n2<=max_n2; n2++) {
		for (n1=min_n1+2; n1<=max_n1+2; n1++) {
			m = (freq * n1 * (1 << n2) + HALF_BASE_FREQ) /
				BASE_FREQ;
			if (m < min_m+2 || m > 127+2)
				continue;
			if ((m * BASE_FREQ >= freq_min * n1) &&
			    (m * BASE_FREQ <= freq_max * n1)) {
				diff = freq * (1 << n2) * n1 - BASE_FREQ * m;
				if (diff < 0)
					diff = -diff;
				if (diff < best_diff) {
					best_diff = diff;
					best_m = m;
					best_n1 = n1;
					best_n2 = n2;
				}
			}
		}
	}

	*ndiv = best_n1 - 2;
	*r = best_n2;
	*mdiv = best_m - 2;
}

static int common_calc_clock(long freq, int min_m, int min_n1, int max_n1,
			     int min_n2, int max_n2, long freq_min,
			     long freq_max, unsigned char *mdiv,
			     unsigned char *ndiv)
{
	long diff, best_diff;
	unsigned int m;
	unsigned char n1, n2;
	unsigned char best_n1 = 16+2, best_n2 = 2, best_m = 125+2;

	best_diff = freq;

	for (n2 = min_n2; n2 <= max_n2; n2++) {
		for (n1 = min_n1+2; n1 <= max_n1+2; n1++) {
			m = (freq * n1 * (1 << n2) + HALF_BASE_FREQ) /
				BASE_FREQ;
			if (m < min_m + 2 || m > 127+2)
				continue;
			if((m * BASE_FREQ >= freq_min * n1) &&
			   (m * BASE_FREQ <= freq_max * n1)) {
				diff = freq * (1 << n2) * n1 - BASE_FREQ * m;
				if(diff < 0)
					diff = -diff;
				if(diff < best_diff) {
					best_diff = diff;
					best_m = m;
					best_n1 = n1;
					best_n2 = n2;
				}
			}
		}
	}

	if(max_n1 == 63)
		*ndiv = (best_n1 - 2) | (best_n2 << 6);
	else
		*ndiv = (best_n1 - 2) | (best_n2 << 5);

	*mdiv = best_m - 2;

	return 0;
}

#ifdef SAVAGEFB_DEBUG
/* This function is used to debug, it prints out the contents of s3 regs */

static void SavagePrintRegs(void)
{
	unsigned char i;
	int vgaCRIndex = 0x3d4;
	int vgaCRReg = 0x3d5;

	printk(KERN_DEBUG "SR    x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE "
	       "xF" );

	for( i = 0; i < 0x70; i++ ) {
		if( !(i % 16) )
			printk(KERN_DEBUG "\nSR%xx ", i >> 4 );
		vga_out8( 0x3c4, i );
		printk(KERN_DEBUG " %02x", vga_in8(0x3c5) );
	}

	printk(KERN_DEBUG "\n\nCR    x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC "
	       "xD xE xF" );

	for( i = 0; i < 0xB7; i++ ) {
		if( !(i % 16) )
			printk(KERN_DEBUG "\nCR%xx ", i >> 4 );
		vga_out8( vgaCRIndex, i );
		printk(KERN_DEBUG " %02x", vga_in8(vgaCRReg) );
	}

	printk(KERN_DEBUG "\n\n");
}
#endif

/* --------------------------------------------------------------------- */

static void savage_get_default_par(struct savagefb_par *par)
{
	unsigned char cr3a, cr53, cr66;

	vga_out16 (0x3d4, 0x4838);
	vga_out16 (0x3d4, 0xa039);
	vga_out16 (0x3c4, 0x0608);

	vga_out8 (0x3d4, 0x66);
	cr66 = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr66 | 0x80);
	vga_out8 (0x3d4, 0x3a);
	cr3a = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr3a | 0x80);
	vga_out8 (0x3d4, 0x53);
	cr53 = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr53 & 0x7f);

	vga_out8 (0x3d4, 0x66);
	vga_out8 (0x3d5, cr66);
	vga_out8 (0x3d4, 0x3a);
	vga_out8 (0x3d5, cr3a);

	vga_out8 (0x3d4, 0x66);
	vga_out8 (0x3d5, cr66);
	vga_out8 (0x3d4, 0x3a);
	vga_out8 (0x3d5, cr3a);

	/* unlock extended seq regs */
	vga_out8 (0x3c4, 0x08);
	par->SR08 = vga_in8 (0x3c5);
	vga_out8 (0x3c5, 0x06);

	/* now save all the extended regs we need */
	vga_out8 (0x3d4, 0x31);
	par->CR31 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x32);
	par->CR32 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x34);
	par->CR34 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x36);
	par->CR36 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x3a);
	par->CR3A = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x40);
	par->CR40 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x42);
	par->CR42 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x45);
	par->CR45 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x50);
	par->CR50 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x51);
	par->CR51 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x53);
	par->CR53 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x58);
	par->CR58 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x60);
	par->CR60 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x66);
	par->CR66 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x67);
	par->CR67 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x68);
	par->CR68 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x69);
	par->CR69 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x6f);
	par->CR6F = vga_in8 (0x3d5);

	vga_out8 (0x3d4, 0x33);
	par->CR33 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x86);
	par->CR86 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x88);
	par->CR88 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x90);
	par->CR90 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x91);
	par->CR91 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0xb0);
	par->CRB0 = vga_in8 (0x3d5) | 0x80;

	/* extended mode timing regs */
	vga_out8 (0x3d4, 0x3b);
	par->CR3B = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x3c);
	par->CR3C = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x43);
	par->CR43 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x5d);
	par->CR5D = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x5e);
	par->CR5E = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x65);
	par->CR65 = vga_in8 (0x3d5);

	/* save seq extended regs for DCLK PLL programming */
	vga_out8 (0x3c4, 0x0e);
	par->SR0E = vga_in8 (0x3c5);
	vga_out8 (0x3c4, 0x0f);
	par->SR0F = vga_in8 (0x3c5);
	vga_out8 (0x3c4, 0x10);
	par->SR10 = vga_in8 (0x3c5);
	vga_out8 (0x3c4, 0x11);
	par->SR11 = vga_in8 (0x3c5);
	vga_out8 (0x3c4, 0x12);
	par->SR12 = vga_in8 (0x3c5);
	vga_out8 (0x3c4, 0x13);
	par->SR13 = vga_in8 (0x3c5);
	vga_out8 (0x3c4, 0x29);
	par->SR29 = vga_in8 (0x3c5);

	vga_out8 (0x3c4, 0x15);
	par->SR15 = vga_in8 (0x3c5);
	vga_out8 (0x3c4, 0x30);
	par->SR30 = vga_in8 (0x3c5);
	vga_out8 (0x3c4, 0x18);
	par->SR18 = vga_in8 (0x3c5);

	/* Save flat panel expansion regsters. */
	if (par->chip == S3_SAVAGE_MX) {
		int i;

		for (i = 0; i < 8; i++) {
			vga_out8 (0x3c4, 0x54+i);
			par->SR54[i] = vga_in8 (0x3c5);
		}
	}

	vga_out8 (0x3d4, 0x66);
	cr66 = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr66 | 0x80);
	vga_out8 (0x3d4, 0x3a);
	cr3a = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr3a | 0x80);

	/* now save MIU regs */
	if (par->chip != S3_SAVAGE_MX) {
		par->MMPR0 = savage_in32(FIFO_CONTROL_REG);
		par->MMPR1 = savage_in32(MIU_CONTROL_REG);
		par->MMPR2 = savage_in32(STREAMS_TIMEOUT_REG);
		par->MMPR3 = savage_in32(MISC_TIMEOUT_REG);
	}

	vga_out8 (0x3d4, 0x3a);
	vga_out8 (0x3d5, cr3a);
	vga_out8 (0x3d4, 0x66);
	vga_out8 (0x3d5, cr66);
}

static void savage_update_var(struct fb_var_screeninfo *var, struct fb_videomode *modedb)
{
	var->xres = var->xres_virtual = modedb->xres;
	var->yres = modedb->yres;
        if (var->yres_virtual < var->yres)
	    var->yres_virtual = var->yres;
        var->xoffset = var->yoffset = 0;
        var->pixclock = modedb->pixclock;
        var->left_margin = modedb->left_margin;
        var->right_margin = modedb->right_margin;
        var->upper_margin = modedb->upper_margin;
        var->lower_margin = modedb->lower_margin;
        var->hsync_len = modedb->hsync_len;
        var->vsync_len = modedb->vsync_len;
        var->sync = modedb->sync;
        var->vmode = modedb->vmode;
}

static int savagefb_check_var (struct fb_var_screeninfo   *var,
			       struct fb_info *info)
{
	struct savagefb_par *par = (struct savagefb_par *)info->par;
	int memlen, vramlen, mode_valid = 0;

	DBG("savagefb_check_var");

	var->transp.offset = 0;
	var->transp.length = 0;
	switch (var->bits_per_pixel) {
	case 8:
		var->red.offset = var->green.offset =
			var->blue.offset = 0;
		var->red.length = var->green.length =
			var->blue.length = var->bits_per_pixel;
		break;
	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		break;
	case 32:
		var->transp.offset = 24;
		var->transp.length = 8;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;

	default:
		return -EINVAL;
	}

	if (!info->monspecs.hfmax || !info->monspecs.vfmax ||
	    !info->monspecs.dclkmax || !fb_validate_mode(var, info))
		mode_valid = 1;

	/* calculate modeline if supported by monitor */
	if (!mode_valid && info->monspecs.gtf) {
		if (!fb_get_mode(FB_MAXTIMINGS, 0, var, info))
			mode_valid = 1;
	}

	if (!mode_valid) {
		struct fb_videomode *mode;

		mode = fb_find_best_mode(var, &info->modelist);
		if (mode) {
			savage_update_var(var, mode);
			mode_valid = 1;
		}
	}

	if (!mode_valid && info->monspecs.modedb_len)
		return -EINVAL;

	/* Is the mode larger than the LCD panel? */
	if (par->SavagePanelWidth &&
	    (var->xres > par->SavagePanelWidth ||
	     var->yres > par->SavagePanelHeight)) {
		printk (KERN_INFO "Mode (%dx%d) larger than the LCD panel "
			"(%dx%d)\n", var->xres,  var->yres,
			par->SavagePanelWidth,
			par->SavagePanelHeight);
		return -1;
	}

	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;

	vramlen = info->fix.smem_len;

	memlen = var->xres_virtual * var->bits_per_pixel *
		var->yres_virtual / 8;
	if (memlen > vramlen) {
		var->yres_virtual = vramlen * 8 /
			(var->xres_virtual * var->bits_per_pixel);
		memlen = var->xres_virtual * var->bits_per_pixel *
			var->yres_virtual / 8;
	}

	/* we must round yres/xres down, we already rounded y/xres_virtual up
	   if it was possible. We should return -EINVAL, but I disagree */
	if (var->yres_virtual < var->yres)
		var->yres = var->yres_virtual;
	if (var->xres_virtual < var->xres)
		var->xres = var->xres_virtual;
	if (var->xoffset + var->xres > var->xres_virtual)
		var->xoffset = var->xres_virtual - var->xres;
	if (var->yoffset + var->yres > var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres;

	return 0;
}


static int savagefb_decode_var (struct fb_var_screeninfo   *var,
				struct savagefb_par        *par)
{
	struct xtimings timings;
	int width, dclk, i, j; /*, refresh; */
	unsigned int m, n, r;
	unsigned char tmp = 0;
	unsigned int pixclock = var->pixclock;

	DBG("savagefb_decode_var");

	memset (&timings, 0, sizeof(timings));

	if (!pixclock) pixclock = 10000;	/* 10ns = 100MHz */
	timings.Clock = 1000000000 / pixclock;
	if (timings.Clock < 1) timings.Clock = 1;
	timings.dblscan = var->vmode & FB_VMODE_DOUBLE;
	timings.interlaced = var->vmode & FB_VMODE_INTERLACED;
	timings.HDisplay = var->xres;
	timings.HSyncStart = timings.HDisplay + var->right_margin;
	timings.HSyncEnd = timings.HSyncStart + var->hsync_len;
	timings.HTotal = timings.HSyncEnd + var->left_margin;
	timings.VDisplay = var->yres;
	timings.VSyncStart = timings.VDisplay + var->lower_margin;
	timings.VSyncEnd = timings.VSyncStart + var->vsync_len;
	timings.VTotal = timings.VSyncEnd + var->upper_margin;
	timings.sync = var->sync;


	par->depth  = var->bits_per_pixel;
	par->vwidth = var->xres_virtual;

	if (var->bits_per_pixel == 16  &&  par->chip == S3_SAVAGE3D) {
		timings.HDisplay *= 2;
		timings.HSyncStart *= 2;
		timings.HSyncEnd *= 2;
		timings.HTotal *= 2;
	}

	/*
	 * This will allocate the datastructure and initialize all of the
	 * generic VGA registers.
	 */
	vgaHWInit (var, par, &timings);

	/* We need to set CR67 whether or not we use the BIOS. */

	dclk = timings.Clock;
	par->CR67 = 0x00;

	switch( var->bits_per_pixel ) {
	case 8:
		if( (par->chip == S3_SAVAGE2000) && (dclk >= 230000) )
			par->CR67 = 0x10;	/* 8bpp, 2 pixels/clock */
		else
			par->CR67 = 0x00;	/* 8bpp, 1 pixel/clock */
		break;
	case 15:
		if ( S3_SAVAGE_MOBILE_SERIES(par->chip) ||
		     ((par->chip == S3_SAVAGE2000) && (dclk >= 230000)) )
			par->CR67 = 0x30;	/* 15bpp, 2 pixel/clock */
		else
			par->CR67 = 0x20;	/* 15bpp, 1 pixels/clock */
		break;
	case 16:
		if( S3_SAVAGE_MOBILE_SERIES(par->chip) ||
		    ((par->chip == S3_SAVAGE2000) && (dclk >= 230000)) )
			par->CR67 = 0x50;	/* 16bpp, 2 pixel/clock */
		else
			par->CR67 = 0x40;	/* 16bpp, 1 pixels/clock */
		break;
	case 24:
		par->CR67 = 0x70;
		break;
	case 32:
		par->CR67 = 0xd0;
		break;
	}

	/*
	 * Either BIOS use is disabled, or we failed to find a suitable
	 * match.  Fall back to traditional register-crunching.
	 */

	vga_out8 (0x3d4, 0x3a);
	tmp = vga_in8 (0x3d5);
	if (1 /*FIXME:psav->pci_burst*/)
		par->CR3A = (tmp & 0x7f) | 0x15;
	else
		par->CR3A = tmp | 0x95;

	par->CR53 = 0x00;
	par->CR31 = 0x8c;
	par->CR66 = 0x89;

	vga_out8 (0x3d4, 0x58);
	par->CR58 = vga_in8 (0x3d5) & 0x80;
	par->CR58 |= 0x13;

	par->SR15 = 0x03 | 0x80;
	par->SR18 = 0x00;
	par->CR43 = par->CR45 = par->CR65 = 0x00;

	vga_out8 (0x3d4, 0x40);
	par->CR40 = vga_in8 (0x3d5) & ~0x01;

	par->MMPR0 = 0x010400;
	par->MMPR1 = 0x00;
	par->MMPR2 = 0x0808;
	par->MMPR3 = 0x08080810;

	SavageCalcClock (dclk, 1, 1, 127, 0, 4, 180000, 360000, &m, &n, &r);
	/* m = 107; n = 4; r = 2; */

	if (par->MCLK <= 0) {
		par->SR10 = 255;
		par->SR11 = 255;
	} else {
		common_calc_clock (par->MCLK, 1, 1, 31, 0, 3, 135000, 270000,
				   &par->SR11, &par->SR10);
		/*      par->SR10 = 80; // MCLK == 286000 */
		/*      par->SR11 = 125; */
	}

	par->SR12 = (r << 6) | (n & 0x3f);
	par->SR13 = m & 0xff;
	par->SR29 = (r & 4) | (m & 0x100) >> 5 | (n & 0x40) >> 2;

	if (var->bits_per_pixel < 24)
		par->MMPR0 -= 0x8000;
	else
		par->MMPR0 -= 0x4000;

	if (timings.interlaced)
		par->CR42 = 0x20;
	else
		par->CR42 = 0x00;

	par->CR34 = 0x10; /* display fifo */

	i = ((((timings.HTotal >> 3) - 5) & 0x100) >> 8) |
		((((timings.HDisplay >> 3) - 1) & 0x100) >> 7) |
		((((timings.HSyncStart >> 3) - 1) & 0x100) >> 6) |
		((timings.HSyncStart & 0x800) >> 7);

	if ((timings.HSyncEnd >> 3) - (timings.HSyncStart >> 3) > 64)
		i |= 0x08;
	if ((timings.HSyncEnd >> 3) - (timings.HSyncStart >> 3) > 32)
		i |= 0x20;

	j = (par->CRTC[0] + ((i & 0x01) << 8) +
	     par->CRTC[4] + ((i & 0x10) << 4) + 1) / 2;

	if (j - (par->CRTC[4] + ((i & 0x10) << 4)) < 4) {
		if (par->CRTC[4] + ((i & 0x10) << 4) + 4 <=
		    par->CRTC[0] + ((i & 0x01) << 8))
			j = par->CRTC[4] + ((i & 0x10) << 4) + 4;
		else
			j = par->CRTC[0] + ((i & 0x01) << 8) + 1;
	}

	par->CR3B = j & 0xff;
	i |= (j & 0x100) >> 2;
	par->CR3C = (par->CRTC[0] + ((i & 0x01) << 8)) / 2;
	par->CR5D = i;
	par->CR5E = (((timings.VTotal - 2) & 0x400) >> 10) |
		(((timings.VDisplay - 1) & 0x400) >> 9) |
		(((timings.VSyncStart) & 0x400) >> 8) |
		(((timings.VSyncStart) & 0x400) >> 6) | 0x40;
	width = (var->xres_virtual * ((var->bits_per_pixel+7) / 8)) >> 3;
	par->CR91 = par->CRTC[19] = 0xff & width;
	par->CR51 = (0x300 & width) >> 4;
	par->CR90 = 0x80 | (width >> 8);
	par->MiscOutReg |= 0x0c;

	/* Set frame buffer description. */

	if (var->bits_per_pixel <= 8)
		par->CR50 = 0;
	else if (var->bits_per_pixel <= 16)
		par->CR50 = 0x10;
	else
		par->CR50 = 0x30;

	if (var->xres_virtual <= 640)
		par->CR50 |= 0x40;
	else if (var->xres_virtual == 800)
		par->CR50 |= 0x80;
	else if (var->xres_virtual == 1024)
		par->CR50 |= 0x00;
	else if (var->xres_virtual == 1152)
		par->CR50 |= 0x01;
	else if (var->xres_virtual == 1280)
		par->CR50 |= 0xc0;
	else if (var->xres_virtual == 1600)
		par->CR50 |= 0x81;
	else
		par->CR50 |= 0xc1;	/* Use GBD */

	if( par->chip == S3_SAVAGE2000 )
		par->CR33 = 0x08;
	else
		par->CR33 = 0x20;

	par->CRTC[0x17] = 0xeb;

	par->CR67 |= 1;

	vga_out8(0x3d4, 0x36);
	par->CR36 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x68);
	par->CR68 = vga_in8 (0x3d5);
	par->CR69 = 0;
	vga_out8 (0x3d4, 0x6f);
	par->CR6F = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x86);
	par->CR86 = vga_in8 (0x3d5);
	vga_out8 (0x3d4, 0x88);
	par->CR88 = vga_in8 (0x3d5) | 0x08;
	vga_out8 (0x3d4, 0xb0);
	par->CRB0 = vga_in8 (0x3d5) | 0x80;

	return 0;
}

/* --------------------------------------------------------------------- */

/*
 *    Set a single color register. Return != 0 for invalid regno.
 */
static int savagefb_setcolreg(unsigned        regno,
			      unsigned        red,
			      unsigned        green,
			      unsigned        blue,
			      unsigned        transp,
			      struct fb_info *info)
{
	struct savagefb_par *par = (struct savagefb_par *)info->par;

	if (regno >= NR_PALETTE)
		return -EINVAL;

	par->palette[regno].red    = red;
	par->palette[regno].green  = green;
	par->palette[regno].blue   = blue;
	par->palette[regno].transp = transp;

	switch (info->var.bits_per_pixel) {
	case 8:
		vga_out8 (0x3c8, regno);

		vga_out8 (0x3c9, red   >> 10);
		vga_out8 (0x3c9, green >> 10);
		vga_out8 (0x3c9, blue  >> 10);
		break;

	case 16:
		if (regno < 16)
			((u32 *)info->pseudo_palette)[regno] =
				((red   & 0xf800)      ) |
				((green & 0xfc00) >>  5) |
				((blue  & 0xf800) >> 11);
		break;

	case 24:
		if (regno < 16)
			((u32 *)info->pseudo_palette)[regno] =
				((red    & 0xff00) <<  8) |
				((green  & 0xff00)      ) |
				((blue   & 0xff00) >>  8);
		break;
	case 32:
		if (regno < 16)
			((u32 *)info->pseudo_palette)[regno] =
				((transp & 0xff00) << 16) |
				((red    & 0xff00) <<  8) |
				((green  & 0xff00)      ) |
				((blue   & 0xff00) >>  8);
		break;

	default:
		return 1;
	}

	return 0;
}

static void savagefb_set_par_int (struct savagefb_par  *par)
{
	unsigned char tmp, cr3a, cr66, cr67;

	DBG ("savagefb_set_par_int");

	par->SavageWaitIdle (par);

	vga_out8 (0x3c2, 0x23);

	vga_out16 (0x3d4, 0x4838);
	vga_out16 (0x3d4, 0xa539);
	vga_out16 (0x3c4, 0x0608);

	vgaHWProtect (par, 1);

	/*
	 * Some Savage/MX and /IX systems go nuts when trying to exit the
	 * server after WindowMaker has displayed a gradient background.  I
	 * haven't been able to find what causes it, but a non-destructive
	 * switch to mode 3 here seems to eliminate the issue.
	 */

	VerticalRetraceWait();
	vga_out8 (0x3d4, 0x67);
	cr67 = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr67/*par->CR67*/ & ~0x0c); /* no STREAMS yet */

	vga_out8 (0x3d4, 0x23);
	vga_out8 (0x3d5, 0x00);
	vga_out8 (0x3d4, 0x26);
	vga_out8 (0x3d5, 0x00);

	/* restore extended regs */
	vga_out8 (0x3d4, 0x66);
	vga_out8 (0x3d5, par->CR66);
	vga_out8 (0x3d4, 0x3a);
	vga_out8 (0x3d5, par->CR3A);
	vga_out8 (0x3d4, 0x31);
	vga_out8 (0x3d5, par->CR31);
	vga_out8 (0x3d4, 0x32);
	vga_out8 (0x3d5, par->CR32);
	vga_out8 (0x3d4, 0x58);
	vga_out8 (0x3d5, par->CR58);
	vga_out8 (0x3d4, 0x53);
	vga_out8 (0x3d5, par->CR53 & 0x7f);

	vga_out16 (0x3c4, 0x0608);

	/* Restore DCLK registers. */

	vga_out8 (0x3c4, 0x0e);
	vga_out8 (0x3c5, par->SR0E);
	vga_out8 (0x3c4, 0x0f);
	vga_out8 (0x3c5, par->SR0F);
	vga_out8 (0x3c4, 0x29);
	vga_out8 (0x3c5, par->SR29);
	vga_out8 (0x3c4, 0x15);
	vga_out8 (0x3c5, par->SR15);

	/* Restore flat panel expansion regsters. */
	if( par->chip == S3_SAVAGE_MX ) {
		int i;

		for( i = 0; i < 8; i++ ) {
			vga_out8 (0x3c4, 0x54+i);
			vga_out8 (0x3c5, par->SR54[i]);
		}
	}

	vgaHWRestore (par);

	/* extended mode timing registers */
	vga_out8 (0x3d4, 0x53);
	vga_out8 (0x3d5, par->CR53);
	vga_out8 (0x3d4, 0x5d);
	vga_out8 (0x3d5, par->CR5D);
	vga_out8 (0x3d4, 0x5e);
	vga_out8 (0x3d5, par->CR5E);
	vga_out8 (0x3d4, 0x3b);
	vga_out8 (0x3d5, par->CR3B);
	vga_out8 (0x3d4, 0x3c);
	vga_out8 (0x3d5, par->CR3C);
	vga_out8 (0x3d4, 0x43);
	vga_out8 (0x3d5, par->CR43);
	vga_out8 (0x3d4, 0x65);
	vga_out8 (0x3d5, par->CR65);

	/* restore the desired video mode with cr67 */
	vga_out8 (0x3d4, 0x67);
	/* following part not present in X11 driver */
	cr67 = vga_in8 (0x3d5) & 0xf;
	vga_out8 (0x3d5, 0x50 | cr67);
	udelay (10000);
	vga_out8 (0x3d4, 0x67);
	/* end of part */
	vga_out8 (0x3d5, par->CR67 & ~0x0c);

	/* other mode timing and extended regs */
	vga_out8 (0x3d4, 0x34);
	vga_out8 (0x3d5, par->CR34);
	vga_out8 (0x3d4, 0x40);
	vga_out8 (0x3d5, par->CR40);
	vga_out8 (0x3d4, 0x42);
	vga_out8 (0x3d5, par->CR42);
	vga_out8 (0x3d4, 0x45);
	vga_out8 (0x3d5, par->CR45);
	vga_out8 (0x3d4, 0x50);
	vga_out8 (0x3d5, par->CR50);
	vga_out8 (0x3d4, 0x51);
	vga_out8 (0x3d5, par->CR51);

	/* memory timings */
	vga_out8 (0x3d4, 0x36);
	vga_out8 (0x3d5, par->CR36);
	vga_out8 (0x3d4, 0x60);
	vga_out8 (0x3d5, par->CR60);
	vga_out8 (0x3d4, 0x68);
	vga_out8 (0x3d5, par->CR68);
	vga_out8 (0x3d4, 0x69);
	vga_out8 (0x3d5, par->CR69);
	vga_out8 (0x3d4, 0x6f);
	vga_out8 (0x3d5, par->CR6F);

	vga_out8 (0x3d4, 0x33);
	vga_out8 (0x3d5, par->CR33);
	vga_out8 (0x3d4, 0x86);
	vga_out8 (0x3d5, par->CR86);
	vga_out8 (0x3d4, 0x88);
	vga_out8 (0x3d5, par->CR88);
	vga_out8 (0x3d4, 0x90);
	vga_out8 (0x3d5, par->CR90);
	vga_out8 (0x3d4, 0x91);
	vga_out8 (0x3d5, par->CR91);

	if (par->chip == S3_SAVAGE4) {
		vga_out8 (0x3d4, 0xb0);
		vga_out8 (0x3d5, par->CRB0);
	}

	vga_out8 (0x3d4, 0x32);
	vga_out8 (0x3d5, par->CR32);

	/* unlock extended seq regs */
	vga_out8 (0x3c4, 0x08);
	vga_out8 (0x3c5, 0x06);

	/* Restore extended sequencer regs for MCLK. SR10 == 255 indicates
	 * that we should leave the default SR10 and SR11 values there.
	 */
	if (par->SR10 != 255) {
		vga_out8 (0x3c4, 0x10);
		vga_out8 (0x3c5, par->SR10);
		vga_out8 (0x3c4, 0x11);
		vga_out8 (0x3c5, par->SR11);
	}

	/* restore extended seq regs for dclk */
	vga_out8 (0x3c4, 0x0e);
	vga_out8 (0x3c5, par->SR0E);
	vga_out8 (0x3c4, 0x0f);
	vga_out8 (0x3c5, par->SR0F);
	vga_out8 (0x3c4, 0x12);
	vga_out8 (0x3c5, par->SR12);
	vga_out8 (0x3c4, 0x13);
	vga_out8 (0x3c5, par->SR13);
	vga_out8 (0x3c4, 0x29);
	vga_out8 (0x3c5, par->SR29);

	vga_out8 (0x3c4, 0x18);
	vga_out8 (0x3c5, par->SR18);

	/* load new m, n pll values for dclk & mclk */
	vga_out8 (0x3c4, 0x15);
	tmp = vga_in8 (0x3c5) & ~0x21;

	vga_out8 (0x3c5, tmp | 0x03);
	vga_out8 (0x3c5, tmp | 0x23);
	vga_out8 (0x3c5, tmp | 0x03);
	vga_out8 (0x3c5, par->SR15);
	udelay (100);

	vga_out8 (0x3c4, 0x30);
	vga_out8 (0x3c5, par->SR30);
	vga_out8 (0x3c4, 0x08);
	vga_out8 (0x3c5, par->SR08);

	/* now write out cr67 in full, possibly starting STREAMS */
	VerticalRetraceWait();
	vga_out8 (0x3d4, 0x67);
	vga_out8 (0x3d5, par->CR67);

	vga_out8 (0x3d4, 0x66);
	cr66 = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr66 | 0x80);
	vga_out8 (0x3d4, 0x3a);
	cr3a = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr3a | 0x80);

	if (par->chip != S3_SAVAGE_MX) {
		VerticalRetraceWait();
		savage_out32 (FIFO_CONTROL_REG, par->MMPR0);
		par->SavageWaitIdle (par);
		savage_out32 (MIU_CONTROL_REG, par->MMPR1);
		par->SavageWaitIdle (par);
		savage_out32 (STREAMS_TIMEOUT_REG, par->MMPR2);
		par->SavageWaitIdle (par);
		savage_out32 (MISC_TIMEOUT_REG, par->MMPR3);
	}

	vga_out8 (0x3d4, 0x66);
	vga_out8 (0x3d5, cr66);
	vga_out8 (0x3d4, 0x3a);
	vga_out8 (0x3d5, cr3a);

	SavageSetup2DEngine (par);
	vgaHWProtect (par, 0);
}

static void savagefb_update_start (struct savagefb_par      *par,
				   struct fb_var_screeninfo *var)
{
	int base;

	base = ((var->yoffset * var->xres_virtual + (var->xoffset & ~1))
		* ((var->bits_per_pixel+7) / 8)) >> 2;

	/* now program the start address registers */
	vga_out16(0x3d4, (base & 0x00ff00) | 0x0c);
	vga_out16(0x3d4, ((base & 0x00ff) << 8) | 0x0d);
	vga_out8 (0x3d4, 0x69);
	vga_out8 (0x3d5, (base & 0x7f0000) >> 16);
}


static void savagefb_set_fix(struct fb_info *info)
{
	info->fix.line_length = info->var.xres_virtual *
		info->var.bits_per_pixel / 8;

	if (info->var.bits_per_pixel == 8)
		info->fix.visual      = FB_VISUAL_PSEUDOCOLOR;
	else
		info->fix.visual      = FB_VISUAL_TRUECOLOR;
}

#if defined(CONFIG_FB_SAVAGE_ACCEL)
static void savagefb_set_clip(struct fb_info *info)
{
    struct savagefb_par *par = (struct savagefb_par *)info->par;
    int cmd;

    cmd = BCI_CMD_NOP | BCI_CMD_CLIP_NEW;
    par->bci_ptr = 0;
    par->SavageWaitFifo(par,3);
    BCI_SEND(cmd);
    BCI_SEND(BCI_CLIP_TL(0, 0));
    BCI_SEND(BCI_CLIP_BR(0xfff, 0xfff));
}
#endif

static int savagefb_set_par (struct fb_info *info)
{
	struct savagefb_par *par = (struct savagefb_par *)info->par;
	struct fb_var_screeninfo *var = &info->var;
	int err;

	DBG("savagefb_set_par");
	err = savagefb_decode_var (var, par);
	if (err)
		return err;

	if (par->dacSpeedBpp <= 0) {
		if (var->bits_per_pixel > 24)
			par->dacSpeedBpp = par->clock[3];
		else if (var->bits_per_pixel >= 24)
			par->dacSpeedBpp = par->clock[2];
		else if ((var->bits_per_pixel > 8) && (var->bits_per_pixel < 24))
			par->dacSpeedBpp = par->clock[1];
		else if (var->bits_per_pixel <= 8)
			par->dacSpeedBpp = par->clock[0];
	}

	/* Set ramdac limits */
	par->maxClock = par->dacSpeedBpp;
	par->minClock = 10000;

	savagefb_set_par_int (par);
	savagefb_update_start (par, var);
	fb_set_cmap (&info->cmap, info);
	savagefb_set_fix(info);
	savagefb_set_clip(info);

	SavagePrintRegs();
	return 0;
}

/*
 *    Pan or Wrap the Display
 */
static int savagefb_pan_display (struct fb_var_screeninfo *var,
				 struct fb_info           *info)
{
	struct savagefb_par *par = (struct savagefb_par *)info->par;
	u_int y_bottom;

	y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += var->yres;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;
	if (y_bottom > info->var.yres_virtual)
		return -EINVAL;

	savagefb_update_start (par, var);

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;

	return 0;
}


static struct fb_ops savagefb_ops = {
	.owner          = THIS_MODULE,
	.fb_check_var   = savagefb_check_var,
	.fb_set_par     = savagefb_set_par,
	.fb_setcolreg   = savagefb_setcolreg,
	.fb_pan_display = savagefb_pan_display,
#if defined(CONFIG_FB_SAVAGE_ACCEL)
	.fb_fillrect    = savagefb_fillrect,
	.fb_copyarea    = savagefb_copyarea,
	.fb_imageblit   = savagefb_imageblit,
	.fb_sync        = savagefb_sync,
#else
	.fb_fillrect    = cfb_fillrect,
	.fb_copyarea    = cfb_copyarea,
	.fb_imageblit   = cfb_imageblit,
#endif
	.fb_cursor      = soft_cursor,
};

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo __devinitdata savagefb_var800x600x8 = {
	.accel_flags =	FB_ACCELF_TEXT,
	.xres =		800,
	.yres =		600,
	.xres_virtual =  800,
	.yres_virtual =  600,
	.bits_per_pixel = 8,
	.pixclock =	25000,
	.left_margin =	88,
	.right_margin =	40,
	.upper_margin =	23,
	.lower_margin =	1,
	.hsync_len =	128,
	.vsync_len =	4,
	.sync =		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode =	FB_VMODE_NONINTERLACED
};

static void savage_enable_mmio (struct savagefb_par *par)
{
	unsigned char val;

	DBG ("savage_enable_mmio\n");

	val = vga_in8 (0x3c3);
	vga_out8 (0x3c3, val | 0x01);
	val = vga_in8 (0x3cc);
	vga_out8 (0x3c2, val | 0x01);

	if (par->chip >= S3_SAVAGE4) {
		vga_out8 (0x3d4, 0x40);
		val = vga_in8 (0x3d5);
		vga_out8 (0x3d5, val | 1);
	}
}


void savage_disable_mmio (struct savagefb_par *par)
{
	unsigned char val;

	DBG ("savage_disable_mmio\n");

	if(par->chip >= S3_SAVAGE4 ) {
		vga_out8 (0x3d4, 0x40);
		val = vga_in8 (0x3d5);
		vga_out8 (0x3d5, val | 1);
	}
}


static int __devinit savage_map_mmio (struct fb_info *info)
{
	struct savagefb_par *par = (struct savagefb_par *)info->par;
	DBG ("savage_map_mmio");

	if (S3_SAVAGE3D_SERIES (par->chip))
		par->mmio.pbase = pci_resource_start (par->pcidev, 0) +
			SAVAGE_NEWMMIO_REGBASE_S3;
	else
		par->mmio.pbase = pci_resource_start (par->pcidev, 0) +
			SAVAGE_NEWMMIO_REGBASE_S4;

	par->mmio.len = SAVAGE_NEWMMIO_REGSIZE;

	par->mmio.vbase = ioremap (par->mmio.pbase, par->mmio.len);
	if (!par->mmio.vbase) {
		printk ("savagefb: unable to map memory mapped IO\n");
		return -ENOMEM;
	} else
		printk (KERN_INFO "savagefb: mapped io at %p\n",
			par->mmio.vbase);

	info->fix.mmio_start = par->mmio.pbase;
	info->fix.mmio_len   = par->mmio.len;

	par->bci_base = (u32 __iomem *)(par->mmio.vbase + BCI_BUFFER_OFFSET);
	par->bci_ptr  = 0;

	savage_enable_mmio (par);

	return 0;
}

static void __devinit savage_unmap_mmio (struct fb_info *info)
{
	struct savagefb_par *par = (struct savagefb_par *)info->par;
	DBG ("savage_unmap_mmio");

	savage_disable_mmio(par);

	if (par->mmio.vbase) {
		iounmap ((void __iomem *)par->mmio.vbase);
		par->mmio.vbase = NULL;
	}
}

static int __devinit savage_map_video (struct fb_info *info,
				       int video_len)
{
	struct savagefb_par *par = (struct savagefb_par *)info->par;
	int resource;

	DBG("savage_map_video");

	if (S3_SAVAGE3D_SERIES (par->chip))
		resource = 0;
	else
		resource = 1;

	par->video.pbase = pci_resource_start (par->pcidev, resource);
	par->video.len   = video_len;
	par->video.vbase = ioremap (par->video.pbase, par->video.len);

	if (!par->video.vbase) {
		printk ("savagefb: unable to map screen memory\n");
		return -ENOMEM;
	} else
		printk (KERN_INFO "savagefb: mapped framebuffer at %p, "
			"pbase == %x\n", par->video.vbase, par->video.pbase);

	info->fix.smem_start = par->video.pbase;
	info->fix.smem_len   = par->video.len - par->cob_size;
	info->screen_base    = par->video.vbase;

#ifdef CONFIG_MTRR
	par->video.mtrr = mtrr_add (par->video.pbase, video_len,
				     MTRR_TYPE_WRCOMB, 1);
#endif
	return 0;
}

static void __devinit savage_unmap_video (struct fb_info *info)
{
	struct savagefb_par *par = (struct savagefb_par *)info->par;

	DBG("savage_unmap_video");

	if (par->video.vbase) {
#ifdef CONFIG_MTRR
		mtrr_del (par->video.mtrr, par->video.pbase, par->video.len);
#endif

		iounmap (par->video.vbase);
		par->video.vbase = NULL;
		info->screen_base = NULL;
	}
}

static int __devinit savage_init_hw (struct savagefb_par *par)
{
	unsigned char config1, m, n, n1, n2, sr8, cr3f, cr66 = 0, tmp;

	static unsigned char RamSavage3D[] = { 8, 4, 4, 2 };
	static unsigned char RamSavage4[] =  { 2, 4, 8, 12, 16, 32, 64, 32 };
	static unsigned char RamSavageMX[] = { 2, 8, 4, 16, 8, 16, 4, 16 };
	static unsigned char RamSavageNB[] = { 0, 2, 4, 8, 16, 32, 2, 2 };

	int videoRam, videoRambytes;

	DBG("savage_init_hw");

	/* unprotect CRTC[0-7] */
	vga_out8(0x3d4, 0x11);
	tmp = vga_in8(0x3d5);
	vga_out8(0x3d5, tmp & 0x7f);

	/* unlock extended regs */
	vga_out16(0x3d4, 0x4838);
	vga_out16(0x3d4, 0xa039);
	vga_out16(0x3c4, 0x0608);

	vga_out8(0x3d4, 0x40);
	tmp = vga_in8(0x3d5);
	vga_out8(0x3d5, tmp & ~0x01);

	/* unlock sys regs */
	vga_out8(0x3d4, 0x38);
	vga_out8(0x3d5, 0x48);

	/* Unlock system registers. */
	vga_out16(0x3d4, 0x4838);

	/* Next go on to detect amount of installed ram */

	vga_out8(0x3d4, 0x36);            /* for register CR36 (CONFG_REG1), */
	config1 = vga_in8(0x3d5);           /* get amount of vram installed */

	/* Compute the amount of video memory and offscreen memory. */

	switch  (par->chip) {
	case S3_SAVAGE3D:
		videoRam = RamSavage3D[ (config1 & 0xC0) >> 6 ] * 1024;
		break;

	case S3_SAVAGE4:
		/*
		 * The Savage4 has one ugly special case to consider.  On
		 * systems with 4 banks of 2Mx32 SDRAM, the BIOS says 4MB
		 * when it really means 8MB.  Why do it the same when you
		 * can do it different...
		 */
		vga_out8(0x3d4, 0x68);	/* memory control 1 */
		if( (vga_in8(0x3d5) & 0xC0) == (0x01 << 6) )
			RamSavage4[1] = 8;

		/*FALLTHROUGH*/

	case S3_SAVAGE2000:
		videoRam = RamSavage4[ (config1 & 0xE0) >> 5 ] * 1024;
		break;

	case S3_SAVAGE_MX:
	case S3_SUPERSAVAGE:
		videoRam = RamSavageMX[ (config1 & 0x0E) >> 1 ] * 1024;
		break;

	case S3_PROSAVAGE:
		videoRam = RamSavageNB[ (config1 & 0xE0) >> 5 ] * 1024;
		break;

	default:
		/* How did we get here? */
		videoRam = 0;
		break;
	}

	videoRambytes = videoRam * 1024;

	printk (KERN_INFO "savagefb: probed videoram:  %dk\n", videoRam);

	/* reset graphics engine to avoid memory corruption */
	vga_out8 (0x3d4, 0x66);
	cr66 = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr66 | 0x02);
	udelay (10000);

	vga_out8 (0x3d4, 0x66);
	vga_out8 (0x3d5, cr66 & ~0x02);	/* clear reset flag */
	udelay (10000);


	/*
	 * reset memory interface, 3D engine, AGP master, PCI master,
	 * master engine unit, motion compensation/LPB
	 */
	vga_out8 (0x3d4, 0x3f);
	cr3f = vga_in8 (0x3d5);
	vga_out8 (0x3d5, cr3f | 0x08);
	udelay (10000);

	vga_out8 (0x3d4, 0x3f);
	vga_out8 (0x3d5, cr3f & ~0x08);	/* clear reset flags */
	udelay (10000);

	/* Savage ramdac speeds */
	par->numClocks = 4;
	par->clock[0] = 250000;
	par->clock[1] = 250000;
	par->clock[2] = 220000;
	par->clock[3] = 220000;

	/* detect current mclk */
	vga_out8(0x3c4, 0x08);
	sr8 = vga_in8(0x3c5);
	vga_out8(0x3c5, 0x06);
	vga_out8(0x3c4, 0x10);
	n = vga_in8(0x3c5);
	vga_out8(0x3c4, 0x11);
	m = vga_in8(0x3c5);
	vga_out8(0x3c4, 0x08);
	vga_out8(0x3c5, sr8);
	m &= 0x7f;
	n1 = n & 0x1f;
	n2 = (n >> 5) & 0x03;
	par->MCLK = ((1431818 * (m+2)) / (n1+2) / (1 << n2) + 50) / 100;
	printk (KERN_INFO "savagefb: Detected current MCLK value of %d kHz\n",
		par->MCLK);

	/* Check LCD panel parrmation */

	if (par->chip == S3_SAVAGE_MX) {
		unsigned char cr6b = VGArCR( 0x6b );

		int panelX = (VGArSEQ (0x61) +
			      ((VGArSEQ (0x66) & 0x02) << 7) + 1) * 8;
		int panelY = (VGArSEQ (0x69) +
			      ((VGArSEQ (0x6e) & 0x70) << 4) + 1);

		char * sTechnology = "Unknown";

		/* OK, I admit it.  I don't know how to limit the max dot clock
		 * for LCD panels of various sizes.  I thought I copied the
		 * formula from the BIOS, but many users have parrmed me of
		 * my folly.
		 *
		 * Instead, I'll abandon any attempt to automatically limit the
		 * clock, and add an LCDClock option to XF86Config.  Some day,
		 * I should come back to this.
		 */

		enum ACTIVE_DISPLAYS { /* These are the bits in CR6B */
			ActiveCRT = 0x01,
			ActiveLCD = 0x02,
			ActiveTV = 0x04,
			ActiveCRT2 = 0x20,
			ActiveDUO = 0x80
		};

		if ((VGArSEQ (0x39) & 0x03) == 0) {
			sTechnology = "TFT";
		} else if ((VGArSEQ (0x30) & 0x01) == 0) {
			sTechnology = "DSTN";
		} else 	{
			sTechnology = "STN";
		}

		printk (KERN_INFO "savagefb: %dx%d %s LCD panel detected %s\n",
			panelX, panelY, sTechnology,
			cr6b & ActiveLCD ? "and active" : "but not active");

		if( cr6b & ActiveLCD ) 	{
			/*
			 * If the LCD is active and panel expansion is enabled,
			 * we probably want to kill the HW cursor.
			 */

			printk (KERN_INFO "savagefb: Limiting video mode to "
				"%dx%d\n", panelX, panelY );

			par->SavagePanelWidth = panelX;
			par->SavagePanelHeight = panelY;

		}
	}

	savage_get_default_par (par);

	if( S3_SAVAGE4_SERIES(par->chip) ) {
		/*
		 * The Savage4 and ProSavage have COB coherency bugs which
		 * render the buffer useless.  We disable it.
		 */
		par->cob_index = 2;
		par->cob_size = 0x8000 << par->cob_index;
		par->cob_offset = videoRambytes;
	} else {
		/* We use 128kB for the COB on all chips. */

		par->cob_index  = 7;
		par->cob_size   = 0x400 << par->cob_index;
		par->cob_offset = videoRambytes - par->cob_size;
	}

	return videoRambytes;
}

static int __devinit savage_init_fb_info (struct fb_info *info,
					  struct pci_dev *dev,
					  const struct pci_device_id *id)
{
	struct savagefb_par *par = (struct savagefb_par *)info->par;
	int err = 0;

	par->pcidev  = dev;

	info->fix.type	   = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux	   = 0;
	info->fix.xpanstep	   = 2;
	info->fix.ypanstep	   = 1;
	info->fix.ywrapstep   = 0;
	info->fix.accel       = id->driver_data;

	switch (info->fix.accel) {
	case FB_ACCEL_SUPERSAVAGE:
		par->chip = S3_SUPERSAVAGE;
		snprintf (info->fix.id, 16, "SuperSavage");
		break;
	case FB_ACCEL_SAVAGE4:
		par->chip = S3_SAVAGE4;
		snprintf (info->fix.id, 16, "Savage4");
		break;
	case FB_ACCEL_SAVAGE3D:
		par->chip = S3_SAVAGE3D;
		snprintf (info->fix.id, 16, "Savage3D");
		break;
	case FB_ACCEL_SAVAGE3D_MV:
		par->chip = S3_SAVAGE3D;
		snprintf (info->fix.id, 16, "Savage3D-MV");
		break;
	case FB_ACCEL_SAVAGE2000:
		par->chip = S3_SAVAGE2000;
		snprintf (info->fix.id, 16, "Savage2000");
		break;
	case FB_ACCEL_SAVAGE_MX_MV:
		par->chip = S3_SAVAGE_MX;
		snprintf (info->fix.id, 16, "Savage/MX-MV");
		break;
	case FB_ACCEL_SAVAGE_MX:
		par->chip = S3_SAVAGE_MX;
		snprintf (info->fix.id, 16, "Savage/MX");
		break;
	case FB_ACCEL_SAVAGE_IX_MV:
		par->chip = S3_SAVAGE_MX;
		snprintf (info->fix.id, 16, "Savage/IX-MV");
		break;
	case FB_ACCEL_SAVAGE_IX:
		par->chip = S3_SAVAGE_MX;
		snprintf (info->fix.id, 16, "Savage/IX");
		break;
	case FB_ACCEL_PROSAVAGE_PM:
		par->chip = S3_PROSAVAGE;
		snprintf (info->fix.id, 16, "ProSavagePM");
		break;
	case FB_ACCEL_PROSAVAGE_KM:
		par->chip = S3_PROSAVAGE;
		snprintf (info->fix.id, 16, "ProSavageKM");
		break;
	case FB_ACCEL_S3TWISTER_P:
		par->chip = S3_PROSAVAGE;
		snprintf (info->fix.id, 16, "TwisterP");
		break;
	case FB_ACCEL_S3TWISTER_K:
		par->chip = S3_PROSAVAGE;
		snprintf (info->fix.id, 16, "TwisterK");
		break;
	case FB_ACCEL_PROSAVAGE_DDR:
		par->chip = S3_PROSAVAGE;
		snprintf (info->fix.id, 16, "ProSavageDDR");
		break;
	case FB_ACCEL_PROSAVAGE_DDRK:
		par->chip = S3_PROSAVAGE;
		snprintf (info->fix.id, 16, "ProSavage8");
		break;
	}

	if (S3_SAVAGE3D_SERIES(par->chip)) {
		par->SavageWaitIdle = savage3D_waitidle;
		par->SavageWaitFifo = savage3D_waitfifo;
	} else if (S3_SAVAGE4_SERIES(par->chip) ||
		   S3_SUPERSAVAGE == par->chip) {
		par->SavageWaitIdle = savage4_waitidle;
		par->SavageWaitFifo = savage4_waitfifo;
	} else {
		par->SavageWaitIdle = savage2000_waitidle;
		par->SavageWaitFifo = savage2000_waitfifo;
	}

	info->var.nonstd      = 0;
	info->var.activate    = FB_ACTIVATE_NOW;
	info->var.width       = -1;
	info->var.height      = -1;
	info->var.accel_flags = 0;

	info->fbops          = &savagefb_ops;
	info->flags          = FBINFO_DEFAULT |
		               FBINFO_HWACCEL_YPAN |
		               FBINFO_HWACCEL_XPAN |
	                       FBINFO_MISC_MODESWITCHLATE;

	info->pseudo_palette = par->pseudo_palette;

#if defined(CONFIG_FB_SAVAGE_ACCEL)
	/* FIFO size + padding for commands */
	info->pixmap.addr = kmalloc(8*1024, GFP_KERNEL);

	err = -ENOMEM;
	if (info->pixmap.addr) {
		memset(info->pixmap.addr, 0, 8*1024);
		info->pixmap.size = 8*1024;
		info->pixmap.scan_align = 4;
		info->pixmap.buf_align = 4;
		info->pixmap.access_align = 4;

		fb_alloc_cmap (&info->cmap, NR_PALETTE, 0);
		info->flags |= FBINFO_HWACCEL_COPYAREA |
	                       FBINFO_HWACCEL_FILLRECT |
		               FBINFO_HWACCEL_IMAGEBLIT;

		err = 0;
	}
#endif
	return err;
}

/* --------------------------------------------------------------------- */

static int __devinit savagefb_probe (struct pci_dev* dev,
				     const struct pci_device_id* id)
{
	struct fb_info *info;
	struct savagefb_par *par;
	u_int h_sync, v_sync;
	int err, lpitch;
	int video_len;

	DBG("savagefb_probe");
	SavagePrintRegs();

	info = framebuffer_alloc(sizeof(struct savagefb_par), &dev->dev);
	if (!info)
		return -ENOMEM;
	par = info->par;
	err = pci_enable_device(dev);
	if (err)
		goto failed_enable;

	if (pci_request_regions(dev, "savagefb")) {
		printk(KERN_ERR "cannot request PCI regions\n");
		goto failed_enable;
	}

	err = -ENOMEM;

	if (savage_init_fb_info(info, dev, id))
		goto failed_init;

	err = savage_map_mmio(info);
	if (err)
		goto failed_mmio;

	video_len = savage_init_hw(par);
	if (video_len < 0) {
		err = video_len;
		goto failed_mmio;
	}

	err = savage_map_video(info, video_len);
	if (err)
		goto failed_video;

	INIT_LIST_HEAD(&info->modelist);
#if defined(CONFIG_FB_SAVAGE_I2C)
	savagefb_create_i2c_busses(info);
	savagefb_probe_i2c_connector(par, &par->edid);
	fb_edid_to_monspecs(par->edid, &info->monspecs);
	fb_videomode_to_modelist(info->monspecs.modedb,
				 info->monspecs.modedb_len,
				 &info->modelist);
#endif
	info->var = savagefb_var800x600x8;

	if (mode_option) {
		fb_find_mode(&info->var, info, mode_option,
			     info->monspecs.modedb, info->monspecs.modedb_len,
			     NULL, 8);
	} else if (info->monspecs.modedb != NULL) {
		struct fb_monspecs *specs = &info->monspecs;
		struct fb_videomode modedb;

		if (info->monspecs.misc & FB_MISC_1ST_DETAIL) {
			int i;

			for (i = 0; i < specs->modedb_len; i++) {
				if (specs->modedb[i].flag & FB_MODE_IS_FIRST) {
					modedb = specs->modedb[i];
					break;
				}
			}
		} else {
			/* otherwise, get first mode in database */
			modedb = specs->modedb[0];
		}

		savage_update_var(&info->var, &modedb);
	}

	/* maximize virtual vertical length */
	lpitch = info->var.xres_virtual*((info->var.bits_per_pixel + 7) >> 3);
	info->var.yres_virtual = info->fix.smem_len/lpitch;

	if (info->var.yres_virtual < info->var.yres)
		goto failed;

#if defined(CONFIG_FB_SAVAGE_ACCEL)
	/*
	 * The clipping coordinates are masked with 0xFFF, so limit our
	 * virtual resolutions to these sizes.
	 */
	if (info->var.yres_virtual > 0x1000)
		info->var.yres_virtual = 0x1000;

	if (info->var.xres_virtual > 0x1000)
		info->var.xres_virtual = 0x1000;
#endif
	savagefb_check_var(&info->var, info);
	savagefb_set_fix(info);

	/*
	 * Calculate the hsync and vsync frequencies.  Note that
	 * we split the 1e12 constant up so that we can preserve
	 * the precision and fit the results into 32-bit registers.
	 *  (1953125000 * 512 = 1e12)
	 */
	h_sync = 1953125000 / info->var.pixclock;
	h_sync = h_sync * 512 / (info->var.xres + info->var.left_margin +
				 info->var.right_margin +
				 info->var.hsync_len);
	v_sync = h_sync / (info->var.yres + info->var.upper_margin +
			   info->var.lower_margin + info->var.vsync_len);

	printk(KERN_INFO "savagefb v" SAVAGEFB_VERSION ": "
	       "%dkB VRAM, using %dx%d, %d.%03dkHz, %dHz\n",
	       info->fix.smem_len >> 10,
	       info->var.xres, info->var.yres,
	       h_sync / 1000, h_sync % 1000, v_sync);


	fb_destroy_modedb(info->monspecs.modedb);
	info->monspecs.modedb = NULL;

	err = register_framebuffer (info);
	if (err < 0)
		goto failed;

	printk (KERN_INFO "fb: S3 %s frame buffer device\n",
		info->fix.id);

	/*
	 * Our driver data
	 */
	pci_set_drvdata(dev, info);

	return 0;

 failed:
#ifdef CONFIG_FB_SAVAGE_I2C
	savagefb_delete_i2c_busses(info);
#endif
	fb_alloc_cmap (&info->cmap, 0, 0);
	savage_unmap_video(info);
 failed_video:
	savage_unmap_mmio (info);
 failed_mmio:
	kfree(info->pixmap.addr);
 failed_init:
	pci_release_regions(dev);
 failed_enable:
	framebuffer_release(info);

	return err;
}

static void __devexit savagefb_remove (struct pci_dev *dev)
{
	struct fb_info *info =
		(struct fb_info *)pci_get_drvdata(dev);

	DBG("savagefb_remove");

	if (info) {
		/*
		 * If unregister_framebuffer fails, then
		 * we will be leaving hooks that could cause
		 * oopsen laying around.
		 */
		if (unregister_framebuffer (info))
			printk (KERN_WARNING "savagefb: danger danger! "
				"Oopsen imminent!\n");

#ifdef CONFIG_FB_SAVAGE_I2C
		savagefb_delete_i2c_busses(info);
#endif
		fb_alloc_cmap (&info->cmap, 0, 0);
		savage_unmap_video (info);
		savage_unmap_mmio (info);
		kfree(info->pixmap.addr);
		pci_release_regions(dev);
		framebuffer_release(info);

		/*
		 * Ensure that the driver data is no longer
		 * valid.
		 */
		pci_set_drvdata(dev, NULL);
	}
}

static int savagefb_suspend (struct pci_dev* dev, u32 state)
{
	struct fb_info *info =
		(struct fb_info *)pci_get_drvdata(dev);
	struct savagefb_par *par = (struct savagefb_par *)info->par;

	DBG("savagefb_suspend");
	printk(KERN_DEBUG "state: %u\n", state);

	acquire_console_sem();
	fb_set_suspend(info, state);
	savage_disable_mmio(par);
	release_console_sem();

	pci_disable_device(dev);
	pci_set_power_state(dev, state);

	return 0;
}

static int savagefb_resume (struct pci_dev* dev)
{
	struct fb_info *info =
		(struct fb_info *)pci_get_drvdata(dev);
	struct savagefb_par *par = (struct savagefb_par *)info->par;

	DBG("savage_resume");

	pci_set_power_state(dev, 0);
	pci_restore_state(dev);
	if(pci_enable_device(dev))
		DBG("err");

	SavagePrintRegs();

	acquire_console_sem();

	savage_enable_mmio(par);
	savage_init_hw(par);
	savagefb_set_par (info);

	fb_set_suspend (info, 0);
	release_console_sem();

	return 0;
}


static struct pci_device_id savagefb_devices[] __devinitdata = {
	{PCI_VENDOR_ID_S3, PCI_CHIP_SUPSAV_MX128,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SUPERSAVAGE},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SUPSAV_MX64,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SUPERSAVAGE},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SUPSAV_MX64C,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SUPERSAVAGE},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SUPSAV_IX128SDR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SUPERSAVAGE},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SUPSAV_IX128DDR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SUPERSAVAGE},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SUPSAV_IX64SDR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SUPERSAVAGE},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SUPSAV_IX64DDR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SUPERSAVAGE},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SUPSAV_IXCSDR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SUPERSAVAGE},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SUPSAV_IXCDDR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SUPERSAVAGE},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE4,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SAVAGE4},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE3D,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SAVAGE3D},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE3D_MV,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SAVAGE3D_MV},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE2000,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SAVAGE2000},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE_MX_MV,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SAVAGE_MX_MV},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE_MX,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SAVAGE_MX},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE_IX_MV,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SAVAGE_IX_MV},

	{PCI_VENDOR_ID_S3, PCI_CHIP_SAVAGE_IX,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_SAVAGE_IX},

	{PCI_VENDOR_ID_S3, PCI_CHIP_PROSAVAGE_PM,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_PROSAVAGE_PM},

	{PCI_VENDOR_ID_S3, PCI_CHIP_PROSAVAGE_KM,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_PROSAVAGE_KM},

	{PCI_VENDOR_ID_S3, PCI_CHIP_S3TWISTER_P,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_S3TWISTER_P},

	{PCI_VENDOR_ID_S3, PCI_CHIP_S3TWISTER_K,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_S3TWISTER_K},

	{PCI_VENDOR_ID_S3, PCI_CHIP_PROSAVAGE_DDR,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_PROSAVAGE_DDR},

	{PCI_VENDOR_ID_S3, PCI_CHIP_PROSAVAGE_DDRK,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_PROSAVAGE_DDRK},

	{0, 0, 0, 0, 0, 0, 0}
};

MODULE_DEVICE_TABLE(pci, savagefb_devices);

static struct pci_driver savagefb_driver = {
	.name =     "savagefb",
	.id_table = savagefb_devices,
	.probe =    savagefb_probe,
	.suspend =  savagefb_suspend,
	.resume =   savagefb_resume,
	.remove =   __devexit_p(savagefb_remove)
};

/* **************************** exit-time only **************************** */

static void __exit savage_done (void)
{
	DBG("savage_done");
	pci_unregister_driver (&savagefb_driver);
}


/* ************************* init in-kernel code ************************** */

int __init savagefb_setup(char *options)
{
#ifndef MODULE
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		mode_option = this_opt;
	}
#endif /* !MODULE */
	return 0;
}

int __init savagefb_init(void)
{
	char *option;

	DBG("savagefb_init");

	if (fb_get_options("savagefb", &option))
		return -ENODEV;

	savagefb_setup(option);
	return pci_register_driver (&savagefb_driver);

}

module_init(savagefb_init);
module_exit(savage_done);
