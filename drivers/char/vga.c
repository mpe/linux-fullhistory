/*
 *  linux/drivers/char/vga.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *			1995  Jay Estabrook
 */

/*
 *	vga.c
 *
 * This module exports the console low-level io support for VGA
 *
 *     'int con_get_font(char *data)'
 *     'int con_set_font(char *data, int ch512)'
 *     'int con_adjust_height(int fontheight)'
 *
 *     'int con_get_cmap(char *)'
 *     'int con_set_cmap(char *)'
 *
 *     'int reset_palette(int currcons)'
 *     'void set_palette(void)'
 *
 * User definable mapping table and font loading by Eugene G. Crosser,
 * <crosser@pccross.msk.su>
 *
 * Improved loadable font/UTF-8 support by H. Peter Anvin 
 * Feb-Sep 1995 <peter.anvin@linux.org>
 *
 * Colour palette handling, by Simon Tatham
 * 17-Jun-95 <sgt20@cam.ac.uk>
 *
 * if 512 char mode is already enabled don't re-enable it,
 * because it causes screen to flicker, by Mitja Horvat
 * 5-May-96 <mitja.horvat@guest.arnes.si>
 *
 */

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#include "kbd_kern.h"
#include "vt_kern.h"
#include "consolemap.h"
#include "selection.h"
#include "console_struct.h"

#define CAN_LOAD_EGA_FONTS    /* undefine if the user must not do this */
#define CAN_LOAD_PALETTE      /* undefine if the user must not do this */

#define dac_reg (0x3c8)
#define dac_val (0x3c9)


void
set_palette (void)
{
	int i, j ;

	if (video_type != VIDEO_TYPE_VGAC || console_blanked ||
	    vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return ;

	for (i=j=0; i<16; i++) {
		outb_p (color_table[i], dac_reg) ;
		outb_p (vc_cons[fg_console].d->vc_palette[j++]>>2, dac_val) ;
		outb_p (vc_cons[fg_console].d->vc_palette[j++]>>2, dac_val) ;
		outb_p (vc_cons[fg_console].d->vc_palette[j++]>>2, dac_val) ;
	}
}

void
__set_origin(unsigned short offset)
{
	unsigned long flags;

	clear_selection();

	save_flags(flags); cli();
	__origin = offset;
	outb_p(12, video_port_reg);
	outb_p(offset >> 8, video_port_val);
	outb_p(13, video_port_reg);
	outb_p(offset, video_port_val);
	restore_flags(flags);
}

/*
 * Put the cursor just beyond the end of the display adaptor memory.
 */
void
hide_cursor(void)
{
  /* This is inefficient, we could just put the cursor at 0xffff,
     but perhaps the delays due to the inefficiency are useful for
     some hardware... */
	outb_p(14, video_port_reg);
	outb_p(0xff&((video_mem_term-video_mem_base)>>9), video_port_val);
	outb_p(15, video_port_reg);
	outb_p(0xff&((video_mem_term-video_mem_base)>>1), video_port_val);
}

void
set_cursor(int currcons)
{
	unsigned long flags;

	if (currcons != fg_console || console_blanked || vcmode == KD_GRAPHICS)
		return;
	if (__real_origin != __origin)
		__set_origin(__real_origin);
	save_flags(flags); cli();
	if (deccm) {
		outb_p(14, video_port_reg);
		outb_p(0xff&((pos-video_mem_base)>>9), video_port_val);
		outb_p(15, video_port_reg);
		outb_p(0xff&((pos-video_mem_base)>>1), video_port_val);
	} else
		hide_cursor();
	restore_flags(flags);
}

unsigned long
con_type_init(unsigned long kmem_start, const char **display_desc)
{
	if (ORIG_VIDEO_MODE == 7)	/* Is this a monochrome display? */
	{
		video_mem_base = 0xb0000;
		video_port_reg = 0x3b4;
		video_port_val = 0x3b5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAM;
			video_mem_term = 0xb8000;
			*display_desc = "EGA+";
			request_region(0x3b0,16,"ega");
		}
		else
		{
			video_type = VIDEO_TYPE_MDA;
			video_mem_term = 0xb2000;
			*display_desc = "*MDA";
			request_region(0x3b0,12,"mda");
			request_region(0x3bf, 1,"mda");
		}
	}
	else				/* If not, it is color. */
	{
		can_do_color = 1;
		video_mem_base = 0xb8000;
		video_port_reg	= 0x3d4;
		video_port_val	= 0x3d5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			int i ;

			video_mem_term = 0xc0000;

			if (!ORIG_VIDEO_ISVGA) {
				video_type = VIDEO_TYPE_EGAC;
				*display_desc = "EGA";
				request_region(0x3c0,32,"ega");
			} else {
				video_type = VIDEO_TYPE_VGAC;
				*display_desc = "VGA+";
				request_region(0x3c0,32,"vga+");

#ifdef VGA_CAN_DO_64KB
				/*
				 * get 64K rather than 32K of video RAM.
				 * This doesn't actually work on all "VGA"
				 * controllers (it seems like setting MM=01
				 * and COE=1 isn't necessarily a good idea)
				 */
				video_mem_base = 0xa0000 ;
				video_mem_term = 0xb0000 ;
				outb_p (6, 0x3ce) ;
				outb_p (6, 0x3cf) ;
#endif

				/*
				 * Normalise the palette registers, to point
				 * the 16 screen colours to the first 16
				 * DAC entries.
				 */

				for (i=0; i<16; i++) {
					inb_p (0x3da) ;
					outb_p (i, 0x3c0) ;
					outb_p (i, 0x3c0) ;
				}
				outb_p (0x20, 0x3c0) ;

				/* now set the DAC registers back to their
				 * default values */

				for (i=0; i<16; i++) {
					outb_p (color_table[i], 0x3c8) ;
					outb_p (default_red[i], 0x3c9) ;
					outb_p (default_grn[i], 0x3c9) ;
					outb_p (default_blu[i], 0x3c9) ;
				}
			}
		}
		else
		{
			video_type = VIDEO_TYPE_CGA;
			video_mem_term = 0xba000;
			*display_desc = "*CGA";
			request_region(0x3d4,2,"cga");
		}
	}
	return kmem_start;
}

void
get_scrmem(int currcons)
{
	memcpyw((unsigned short *)vc_scrbuf[currcons],
		(unsigned short *)origin, video_screen_size);
	origin = video_mem_start = (unsigned long)vc_scrbuf[currcons];
	scr_end = video_mem_end = video_mem_start + video_screen_size;
	pos = origin + y*video_size_row + (x<<1);
}

void
set_scrmem(int currcons, long offset)
{
#ifdef CONFIG_HGA
  /* This works with XFree86 1.2, 1.3 and 2.0
     This code could be extended and made more generally useful if we could
     determine the actual video mode. It appears that this should be
     possible on a genuine Hercules card, but I (WM) haven't been able to
     read from any of the required registers on my clone card.
     */
	/* This code should work with Hercules and MDA cards. */
	if (video_type == VIDEO_TYPE_MDA)
	  {
	    if (vcmode == KD_TEXT)
	      {
		/* Ensure that the card is in text mode. */
		int	i;
		static char herc_txt_tbl[12] = {
		  0x61,0x50,0x52,0x0f,0x19,6,0x19,0x19,2,0x0d,0x0b,0x0c };
		outb_p(0, 0x3bf);  /* Back to power-on defaults */
		outb_p(0, 0x3b8);  /* Blank the screen, select page 0, etc */
		for ( i = 0 ; i < 12 ; i++ )
		  {
		    outb_p(i, 0x3b4);
		    outb_p(herc_txt_tbl[i], 0x3b5);
		  }
	      }
#define HGA_BLINKER_ON 0x20
#define HGA_SCREEN_ON  8
	    /* Make sure that the hardware is not blanked */
	    outb_p(HGA_BLINKER_ON | HGA_SCREEN_ON, 0x3b8);
	  }
#endif CONFIG_HGA

	if (video_mem_term - video_mem_base < offset + video_screen_size)
	  offset = 0;	/* strange ... */
	memcpyw((unsigned short *)(video_mem_base + offset),
		(unsigned short *) origin, video_screen_size);
	video_mem_start = video_mem_base;
	video_mem_end = video_mem_term;
	origin = video_mem_base + offset;
	scr_end = origin + video_screen_size;
	pos = origin + y*video_size_row + (x<<1);
	has_wrapped = 0;
}

/*
 * PIO_FONT support.
 *
 * The font loading code goes back to the codepage package by
 * Joel Hoffman (joel@wam.umd.edu). (He reports that the original
 * reference is: "From: p. 307 of _Programmer's Guide to PC & PS/2
 * Video Systems_ by Richard Wilton. 1987.  Microsoft Press".)
 *
 * Change for certain monochrome monitors by Yury Shevchuck
 * (sizif@botik.yaroslavl.su).
 */

#define colourmap ((char *)0xa0000)
/* Pauline Middelink <middelin@polyware.iaf.nl> reports that we
   should use 0xA0000 for the bwmap as well.. */
#define blackwmap ((char *)0xa0000)
#define cmapsz 8192
#define attrib_port (0x3c0)
#define seq_port_reg (0x3c4)
#define seq_port_val (0x3c5)
#define gr_port_reg (0x3ce)
#define gr_port_val (0x3cf)

int
set_get_font(char * arg, int set, int ch512)
{
#ifdef CAN_LOAD_EGA_FONTS
	static int ch512enabled = 0;
	int i;
	char *charmap;
	int beg;
	unsigned short video_port_status = video_port_reg + 6;
	int font_select = 0x00;

	/* no use to "load" CGA... */

	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_VGAC) {
		charmap = colourmap;
		beg = 0x0e;
#ifdef VGA_CAN_DO_64KB
		if (video_type == VIDEO_TYPE_VGAC)
			beg = 0x06;
#endif
	} else if (video_type == VIDEO_TYPE_EGAM) {
		charmap = blackwmap;
		beg = 0x0a;
	} else
		return -EINVAL;
	
	if (arg)
	  {
	    i = verify_area(set ? VERIFY_READ : VERIFY_WRITE, (void *)arg,
			    ch512 ? 2*cmapsz : cmapsz);
	    if (i)
	      return i;
	  }
	else
	  ch512 = 0;		/* Default font is always 256 */

#ifdef BROKEN_GRAPHICS_PROGRAMS
	/*
	 * All fonts are loaded in slot 0 (0:1 for 512 ch)
	 */

	if (!arg)
	  return -EINVAL;	/* Return to default font not supported */

	video_font_is_default = 0;
	font_select = ch512 ? 0x04 : 0x00;
#else	
	/*
	 * The default font is kept in slot 0 and is never touched.
	 * A custom font is loaded in slot 2 (256 ch) or 2:3 (512 ch)
	 */

	if (set)
	  {
	    video_font_is_default = !arg;
	    font_select = arg ? (ch512 ? 0x0e : 0x0a) : 0x00;
	  }

	if ( !video_font_is_default )
	  charmap += 4*cmapsz;
#endif

	cli();
	outb_p( 0x00, seq_port_reg );   /* First, the sequencer */
	outb_p( 0x01, seq_port_val );   /* Synchronous reset */
	outb_p( 0x02, seq_port_reg );
	outb_p( 0x04, seq_port_val );   /* CPU writes only to map 2 */
	outb_p( 0x04, seq_port_reg );
	outb_p( 0x07, seq_port_val );   /* Sequential addressing */
	outb_p( 0x00, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* Clear synchronous reset */

	outb_p( 0x04, gr_port_reg );    /* Now, the graphics controller */
	outb_p( 0x02, gr_port_val );    /* select map 2 */
	outb_p( 0x05, gr_port_reg );
	outb_p( 0x00, gr_port_val );    /* disable odd-even addressing */
	outb_p( 0x06, gr_port_reg );
	outb_p( 0x00, gr_port_val );    /* map start at A000:0000 */
	sti();
	
	if (arg)
	  {
	    if (set)
	      for (i=0; i<cmapsz ; i++)
		scr_writeb(get_user(arg + i), charmap + i);
	    else
	      for (i=0; i<cmapsz ; i++)
		put_user(scr_readb(charmap + i), arg + i);
	    
	    
	/*
	 * In 512-character mode, the character map is not contiguous if
	 * we want to remain EGA compatible -- which we do
	 */

	    if (ch512)
	      {
		charmap += 2*cmapsz;
		arg += cmapsz;
		if (set)
		  for (i=0; i<cmapsz ; i++)
		    *(charmap+i) = get_user(arg+i);
		else
		  for (i=0; i<cmapsz ; i++)
		    put_user(*(charmap+i), arg+i);
	      }
	  }
	
	cli();
	outb_p( 0x00, seq_port_reg );   /* First, the sequencer */
	outb_p( 0x01, seq_port_val );   /* Synchronous reset */
	outb_p( 0x02, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* CPU writes to maps 0 and 1 */
	outb_p( 0x04, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* odd-even addressing */
	if (set)
	  {
	    outb_p( 0x03, seq_port_reg ); /* Character Map Select */
	    outb_p( font_select, seq_port_val );
	  }
	outb_p( 0x00, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* clear synchronous reset */

	outb_p( 0x04, gr_port_reg );    /* Now, the graphics controller */
	outb_p( 0x00, gr_port_val );    /* select map 0 for CPU */
	outb_p( 0x05, gr_port_reg );
	outb_p( 0x10, gr_port_val );    /* enable even-odd addressing */
	outb_p( 0x06, gr_port_reg );
	outb_p( beg, gr_port_val );     /* map starts at b800:0 or b000:0 */

	/* if 512 char mode is already enabled don't re-enable it. */
	if ((set)&&(ch512!=ch512enabled))	/* attribute controller */
	  {
	    ch512enabled=ch512;
	    /* 256-char: enable intensity bit
	       512-char: disable intensity bit */
	    inb_p( video_port_status );	/* clear address flip-flop */
	    outb_p ( 0x12, attrib_port ); /* color plane enable register */
	    outb_p ( ch512 ? 0x07 : 0x0f, attrib_port );
	    /* Wilton (1987) mentions the following; I don't know what
	       it means, but it works, and it appears necessary */
	    inb_p( video_port_status );
	    outb_p ( 0x20, attrib_port );
	  }
	sti();

	return 0;
#else
	return -EINVAL;
#endif
}

/*
 * Adjust the screen to fit a font of a certain height
 *
 * Returns < 0 for error, 0 if nothing changed, and the number
 * of lines on the adjusted console if changed.
 */
int
con_adjust_height(unsigned long fontheight)
{
	int rows, maxscan;
	unsigned char ovr, vde, fsr, curs, cure;

	if (fontheight > 32 || (video_type != VIDEO_TYPE_VGAC &&
	    video_type != VIDEO_TYPE_EGAC && video_type != VIDEO_TYPE_EGAM))
		return -EINVAL;

	if ( fontheight == video_font_height || fontheight == 0 )
		return 0;

	video_font_height = fontheight;

	rows = video_scan_lines/fontheight;	/* Number of video rows we end up with */
	maxscan = rows*fontheight - 1;		/* Scan lines to actually display-1 */

	/* Reprogram the CRTC for the new font size
	   Note: the attempt to read the overflow register will fail
	   on an EGA, but using 0xff for the previous value appears to
	   be OK for EGA text modes in the range 257-512 scan lines, so I
	   guess we don't need to worry about it.

	   The same applies for the spill bits in the font size and cursor
	   registers; they are write-only on EGA, but it appears that they
	   are all don't care bits on EGA, so I guess it doesn't matter. */

	cli();
	outb_p( 0x07, video_port_reg );		/* CRTC overflow register */
	ovr = inb_p(video_port_val);
	outb_p( 0x09, video_port_reg );		/* Font size register */
	fsr = inb_p(video_port_val);
	outb_p( 0x0a, video_port_reg );		/* Cursor start */
	curs = inb_p(video_port_val);
	outb_p( 0x0b, video_port_reg );		/* Cursor end */
	cure = inb_p(video_port_val);
	sti();

	vde = maxscan & 0xff;			/* Vertical display end reg */
	ovr = (ovr & 0xbd) +			/* Overflow register */
	      ((maxscan & 0x100) >> 7) +
	      ((maxscan & 0x200) >> 3);
	fsr = (fsr & 0xe0) + (fontheight-1);    /*  Font size register */
	curs = (curs & 0xc0) + fontheight - (fontheight < 10 ? 2 : 3);
	cure = (cure & 0xe0) + fontheight - (fontheight < 10 ? 1 : 2);

	cli();
	outb_p( 0x07, video_port_reg );		/* CRTC overflow register */
	outb_p( ovr, video_port_val );
	outb_p( 0x09, video_port_reg );		/* Font size */
	outb_p( fsr, video_port_val );
	outb_p( 0x0a, video_port_reg );		/* Cursor start */
	outb_p( curs, video_port_val );
	outb_p( 0x0b, video_port_reg );		/* Cursor end */
	outb_p( cure, video_port_val );
	outb_p( 0x12, video_port_reg );		/* Vertical display limit */
	outb_p( vde, video_port_val );
	sti();

	if ( rows == video_num_lines ) {
	  /* Change didn't affect number of lines -- no need to scare
	     the rest of the world */
	  return 0;
	}

	vc_resize(rows, 0);			/* Adjust console size */

	return rows;
}

int
set_get_cmap(unsigned char * arg, int set) {
#ifdef CAN_LOAD_PALETTE
	int i;

	/* no use to set colourmaps in less than colour VGA */

	if (video_type != VIDEO_TYPE_VGAC)
		return -EINVAL;

	i = verify_area(set ? VERIFY_READ : VERIFY_WRITE, (void *)arg, 16*3);
	if (i)
		return i;

	for (i=0; i<16; i++) {
		if (set) {
			default_red[i] = get_user(arg++) ;
			default_grn[i] = get_user(arg++) ;
			default_blu[i] = get_user(arg++) ;
		} else {
			put_user (default_red[i], arg++) ;
			put_user (default_grn[i], arg++) ;
			put_user (default_blu[i], arg++) ;
		}
	}
	if (set) {
		for (i=0; i<MAX_NR_CONSOLES; i++)
			if (vc_cons_allocated(i)) {
				int j, k ;
				for (j=k=0; j<16; j++) {
					vc_cons[i].d->vc_palette[k++] = default_red[j];
					vc_cons[i].d->vc_palette[k++] = default_grn[j];
					vc_cons[i].d->vc_palette[k++] = default_blu[j];
				}
			}
		set_palette() ;
	}

	return 0;
#else
	return -EINVAL;
#endif
}
