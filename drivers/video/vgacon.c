/*
 *  linux/drivers/video/vgacon.c -- Low level VGA based console driver
 *
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *
 *  This file is based on the old console.c, vga.c and vesa_blank.c drivers.
 *
 *	Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *
 *	User definable mapping table and font loading by Eugene G. Crosser,
 *	<crosser@pccross.msk.su>
 *
 *	Improved loadable font/UTF-8 support by H. Peter Anvin
 *	Feb-Sep 1995 <peter.anvin@linux.org>
 *
 *	Colour palette handling, by Simon Tatham
 *	17-Jun-95 <sgt20@cam.ac.uk>
 *
 *	if 512 char mode is already enabled don't re-enable it,
 *	because it causes screen to flicker, by Mitja Horvat
 *	5-May-96 <mitja.horvat@guest.arnes.si>
 *
 *	Use 2 outw instead of 4 outb_p to reduce erroneous text
 *	flashing on RHS of screen during heavy console scrolling .
 *	Oct 1996, Paul Gortmaker.
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */




/* KNOWN PROBLEMS/TO DO ===================================================== *
 *
 *	- monochrome attribute encoding (convert abscon <-> VGA style)
 *
 *	- speed up scrolling by changing the screen origin
 *
 *	- add support for palette, loadable fonts and VESA blanking
 *
 * KNOWN PROBLEMS/TO DO ==================================================== */


#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/linux_logo.h>


#define BLANK 0x0020

#define CAN_LOAD_EGA_FONTS	/* undefine if the user must not do this */
#define CAN_LOAD_PALETTE	/* undefine if the user must not do this */

#define dac_reg	(0x3c8)
#define dac_val	(0x3c9)

#ifdef __powerpc__
#define VGA_OFFSET _ISA_MEM_BASE;
#else
#define VGA_OFFSET 0x0
#endif


/*
 *  Interface used by the world
 */

static unsigned long vgacon_startup(unsigned long kmem_start,
				   const char **display_desc);
static void vgacon_init(struct vc_data *conp);
static int vgacon_deinit(struct vc_data *conp);
static int vgacon_clear(struct vc_data *conp, int sy, int sx, int height,
		       int width);
static int vgacon_putc(struct vc_data *conp, int c, int ypos, int xpos);
static int vgacon_putcs(struct vc_data *conp, const char *s, int count,
		       int ypos, int xpos);
static int vgacon_cursor(struct vc_data *conp, int mode);
static int vgacon_scroll(struct vc_data *conp, int t, int b,
			int dir, int count);
static int vgacon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
		       int height, int width);
static int vgacon_switch(struct vc_data *conp);
static int vgacon_blank(int blank);
static int vgacon_get_font(struct vc_data *conp, int *w, int *h, char *data);
static int vgacon_set_font(struct vc_data *conp, int w, int h, char *data);
static int vgacon_set_palette(struct vc_data *conp, unsigned char *table);
static int vgacon_scrolldelta(int lines);


/*
 *  Internal routines
 */

static int vgacon_show_logo(void);


/* Description of the hardware situation */
static unsigned long   vga_video_mem_base;	/* Base of video memory */
static unsigned long   vga_video_mem_term;	/* End of video memory */
static unsigned short  vga_video_port_reg;	/* Video register select port */
static unsigned short  vga_video_port_val;	/* Video register value port */
static unsigned long   vga_video_num_columns;	/* Number of text columns */
static unsigned long   vga_video_num_lines;	/* Number of text lines */
static unsigned long   vga_video_size_row;
static unsigned long   vga_video_screen_size;

static int vga_can_do_color = 0;
static unsigned long vga_default_font_height;	/* Height of default screen font */

static unsigned char vga_video_type;
static unsigned char vga_has_wrapped;		/* all of videomem is data of fg_console */
static unsigned char vga_hardscroll_enabled;
static unsigned char vga_hardscroll_disabled_by_init = 0;



    /*
     *  VGA screen access
     */ 

static inline void vga_writew(unsigned short val, unsigned short * addr)
{
#ifdef __powerpc__
	st_le16(addr, val);
#else
	writew(val, (unsigned long) addr);
#endif /* !__powerpc__ */
}

static inline unsigned short vga_readw(unsigned short * addr)
{
#ifdef __powerpc__
	return ld_le16(addr);
#else
	return readw((unsigned long) addr);
#endif /* !__powerpc__ */	
}

static inline void vga_memsetw(void * s, unsigned short c, unsigned int count)
{
	unsigned short * addr = (unsigned short *) s;

	while (count) {
		count--;
		vga_writew(c, addr++);
	}
}

static inline void vga_memmovew(unsigned short *to, unsigned short *from,
				unsigned int count)
{
	if (to < from) {
	    while (count) {
		    count--;
		    vga_writew(vga_readw(from++), to++);
	    }
	} else {
	    from += count;
	    to += count;
	    while (count) {
		    count--;
		    vga_writew(vga_readw(--from), --to);
	    }
	}
}


/*
 * By replacing the four outb_p with two back to back outw, we can reduce
 * the window of opportunity to see text mislocated to the RHS of the
 * console during heavy scrolling activity. However there is the remote
 * possibility that some pre-dinosaur hardware won't like the back to back
 * I/O. Since the Xservers get away with it, we should be able to as well.
 */
static inline void write_vga(unsigned char reg, unsigned int val)
{
#ifndef SLOW_VGA
	unsigned int v1, v2;

	v1 = reg + (val & 0xff00);
	v2 = reg + 1 + ((val << 8) & 0xff00);
	outw(v1, vga_video_port_reg);
	outw(v2, vga_video_port_reg);
#else
	outb_p(reg, vga_video_port_reg);
	outb_p(val >> 8, vga_video_port_val);
	outb_p(reg+1, vga_video_port_reg);
	outb_p(val & 0xff, vga_video_port_val);
#endif
}


__initfunc(static unsigned long vgacon_startup(unsigned long kmem_start,
					       const char **display_desc))
{
	unsigned short saved;
	unsigned short *p;

	/*
	 *	Find out if there is a graphics card present.
	 *	Are there smarter methods around?
	 */
	p = (unsigned short *)(((ORIG_VIDEO_MODE == 7) ? 0xb0000 : 0xb8000) +
			       + VGA_OFFSET);
	saved = vga_readw(p);
	vga_writew(0xAA55, p);
	if (vga_readw(p) != 0xAA55) {
		vga_writew(saved, p);
		return kmem_start;
	}
	vga_writew(0x55AA, p);
	if (vga_readw(p) != 0x55AA) {
		vga_writew(saved, p);
		return kmem_start;
	}
	vga_writew(saved, p);

	vga_video_num_lines = ORIG_VIDEO_LINES;
	vga_video_num_columns = ORIG_VIDEO_COLS;
	vga_video_size_row = 2 * ORIG_VIDEO_COLS;
	vga_video_screen_size = vga_video_num_lines * vga_video_size_row;

	if (ORIG_VIDEO_MODE == 7)	/* Is this a monochrome display? */
	{
		vga_video_mem_base = 0xb0000 + VGA_OFFSET;
		vga_video_port_reg = 0x3b4;
		vga_video_port_val = 0x3b5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			vga_video_type = VIDEO_TYPE_EGAM;
			vga_video_mem_term = 0xb8000 + VGA_OFFSET;
			*display_desc = "EGA+";
			request_region(0x3b0,16,"ega");
		}
		else
		{
			vga_video_type = VIDEO_TYPE_MDA;
			vga_video_mem_term = 0xb2000 + VGA_OFFSET;
			*display_desc = "*MDA";
			request_region(0x3b0,12,"mda");
			request_region(0x3bf, 1,"mda");
		}
	}
	else				/* If not, it is color. */
	{
		vga_can_do_color = 1;
		vga_video_mem_base = 0xb8000  + VGA_OFFSET;
		vga_video_port_reg	= 0x3d4;
		vga_video_port_val	= 0x3d5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			int i ;

			vga_video_mem_term = 0xc0000 + VGA_OFFSET;

			if (!ORIG_VIDEO_ISVGA) {
				vga_video_type = VIDEO_TYPE_EGAC;
				*display_desc = "EGA";
				request_region(0x3c0,32,"ega");
			} else {
				vga_video_type = VIDEO_TYPE_VGAC;
				*display_desc = "VGA+";
				request_region(0x3c0,32,"vga+");

#ifdef VGA_CAN_DO_64KB
				/*
				 * get 64K rather than 32K of video RAM.
				 * This doesn't actually work on all "VGA"
				 * controllers (it seems like setting MM=01
				 * and COE=1 isn't necessarily a good idea)
				 */
				vga_video_mem_base = 0xa0000  + VGA_OFFSET;
				vga_video_mem_term = 0xb0000  + VGA_OFFSET;
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
			vga_video_type = VIDEO_TYPE_CGA;
			vga_video_mem_term = 0xba000 + VGA_OFFSET;
			*display_desc = "*CGA";
			request_region(0x3d4,2,"cga");
		}
	}

	vga_hardscroll_enabled = (vga_hardscroll_disabled_by_init ? 0 :
	  (vga_video_type == VIDEO_TYPE_EGAC
	    || vga_video_type == VIDEO_TYPE_VGAC
	    || vga_video_type == VIDEO_TYPE_EGAM));
	vga_has_wrapped = 0;

	if (vga_video_type == VIDEO_TYPE_VGAC
	    || vga_video_type == VIDEO_TYPE_EGAC
	    || vga_video_type == VIDEO_TYPE_EGAM)
	{
		vga_default_font_height = ORIG_VIDEO_POINTS;
		video_font_height = ORIG_VIDEO_POINTS;
		/* This may be suboptimal but is a safe bet - go with it */
		video_scan_lines =
			video_font_height * vga_video_num_lines;
	}

	if (!console_show_logo)
	    console_show_logo = vgacon_show_logo;

	return kmem_start;
}


static void vgacon_init(struct vc_data *conp)
{
    conp->vc_cols = vga_video_num_columns;
    conp->vc_rows = vga_video_num_lines;
    conp->vc_can_do_color = vga_can_do_color;
}

static int vgacon_deinit(struct vc_data *conp)
{
    return 0;
}


/* ====================================================================== */

static int vgacon_clear(struct vc_data *conp, int sy, int sx, int height,
			      int width)
{
    int rows;
    unsigned long dest;

    if (console_blanked)
	return 0;

    dest = vga_video_mem_base + sy*vga_video_size_row + sx*2;
    if (sx == 0 && width == vga_video_num_columns)      
	vga_memsetw((void *)dest, conp->vc_video_erase_char, height * width);
    else
        for (rows = height; rows-- ; dest += vga_video_size_row)
	    vga_memsetw((void *)dest, conp->vc_video_erase_char, width);
    return 0;
}


static int vgacon_putc(struct vc_data *conp, int c, int ypos, int xpos)
{
    u_short *p;

    if (console_blanked)
	    return 0;

    p = (u_short *)(vga_video_mem_base+ypos*vga_video_size_row+xpos*2);
    vga_writew(conp->vc_attr << 8 | c, p);
    return 0;
}


static int vgacon_putcs(struct vc_data *conp, const char *s, int count,
		       int ypos, int xpos)
{
    u_short *p;
    u_short sattr;

    if (console_blanked)
	    return 0;

    p = (u_short *)(vga_video_mem_base+ypos*vga_video_size_row+xpos*2);
    sattr = conp->vc_attr << 8;
    while (count--)
	vga_writew(sattr | *s++, p++);
    return 0;
}


static int vgacon_cursor(struct vc_data *conp, int mode)
{
    switch (mode) {
	case CM_ERASE:
	    write_vga(14, (vga_video_mem_term - vga_video_mem_base - 1)>>1);
	    break;

	case CM_MOVE:
	case CM_DRAW:
	    write_vga(14, conp->vc_y*vga_video_num_columns+conp->vc_x);
	    break;
    }
    return 0;
}


static int vgacon_scroll(struct vc_data *conp, int t, int b, int dir, int count)
{
    if (console_blanked)
	return 0;

    vgacon_cursor(conp, CM_ERASE);

    switch (dir) {
	case SM_UP:
	    if (count > conp->vc_rows)	/* Maximum realistic size */
		count = conp->vc_rows;
	    vgacon_bmove(conp, t+count, 0, t, 0, b-t-count, conp->vc_cols);
	    vgacon_clear(conp, b-count, 0, count, conp->vc_cols);
	    break;

	case SM_DOWN:
	    if (count > conp->vc_rows)	/* Maximum realistic size */
		count = conp->vc_rows;
	    /*
	     *  Fixed bmove() should end Arno's frustration with copying?
	     *  Confucius says:
	     *	Man who copies in wrong direction, end up with trashed
	     *	data
	     */
	    vgacon_bmove(conp, t, 0, t+count, 0, b-t-count, conp->vc_cols);
	    vgacon_clear(conp, t, 0, count, conp->vc_cols);
	    break;

	case SM_LEFT:
	    vgacon_bmove(conp, 0, t+count, 0, t, conp->vc_rows, b-t-count);
	    vgacon_clear(conp, 0, b-count, conp->vc_rows, count);
	    break;

	case SM_RIGHT:
	    vgacon_bmove(conp, 0, t, 0, t+count, conp->vc_rows, b-t-count);
	    vgacon_clear(conp, 0, t, conp->vc_rows, count);
	    break;
    }

    return 0;
}


static int vgacon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
		       int height, int width)
{
    unsigned long src, dst;
    int rows;

    if (console_blanked)
	return 0;

    if (sx == 0 && dx == 0 && width == vga_video_num_columns) {
	src = vga_video_mem_base + sy * vga_video_size_row;
	dst = vga_video_mem_base + dy * vga_video_size_row;
	vga_memmovew((unsigned short *)dst, (unsigned short *)src,
		     height * width);
    } else if (dy < sy || (dy == sy && dx < sx)) {
	src = vga_video_mem_base + sy * vga_video_size_row + sx * 2;
	dst = vga_video_mem_base + dy * vga_video_size_row + dx * 2;
	for (rows = height; rows-- ;) {
	    vga_memmovew((unsigned short *)dst, (unsigned short *)src, width);
	    src += vga_video_size_row;
	    dst += vga_video_size_row;
	}
    } else {
	src = vga_video_mem_base + (sy+height-1) * vga_video_size_row + sx * 2;
	dst = vga_video_mem_base + (dy+height-1) * vga_video_size_row + dx * 2;
	for (rows = height; rows-- ;) {
	    vga_memmovew((unsigned short *)dst, (unsigned short *)src, width);
	    src -= vga_video_size_row;
	    dst -= vga_video_size_row;
	}
    }
    return 0;
}


static int vgacon_switch(struct vc_data *conp)
{
    return 0;
}


static int vgacon_blank(int blank)
{
    if (blank) {
	vga_memsetw((void *)vga_video_mem_base, BLANK, vga_video_screen_size/2);
	return 0;
    } else {
	/* Tell console.c that it has to restore the screen itself */
	return(1);
    }
    return 0;
}


static int vgacon_get_font(struct vc_data *conp, int *w, int *h, char *data)
{
    /* TODO */
    return -ENOSYS;
}


static int vgacon_set_font(struct vc_data *conp, int w, int h, char *data)
{
    /* TODO */
    return -ENOSYS;
}

static int vgacon_set_palette(struct vc_data *conp, unsigned char *table)
{
	int i, j ;

	if (vga_video_type != VIDEO_TYPE_VGAC || console_blanked ||
	    vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return -EINVAL;

	for (i=j=0; i<16; i++) {
		outb_p (table[i], dac_reg) ;
		outb_p (vc_cons[fg_console].d->vc_palette[j++]>>2, dac_val) ;
		outb_p (vc_cons[fg_console].d->vc_palette[j++]>>2, dac_val) ;
		outb_p (vc_cons[fg_console].d->vc_palette[j++]>>2, dac_val) ;
	}
	return 0;
}

static int vgacon_scrolldelta(int lines)
{
    /* TODO */
    return -ENOSYS;
}


__initfunc(static int vgacon_show_logo( void ))
{
    int height = 0;
    char *p;

    printk(linux_serial_image);
    for (p = linux_serial_image; *p; p++)
	if (*p == '\n')
	    height++;
    return height;
}



/*
 *  The console `switch' structure for the VGA based console
 */

struct consw vga_con = {
    vgacon_startup, vgacon_init, vgacon_deinit, vgacon_clear, vgacon_putc,
    vgacon_putcs, vgacon_cursor, vgacon_scroll, vgacon_bmove, vgacon_switch,
    vgacon_blank, vgacon_get_font, vgacon_set_font, vgacon_set_palette,
    vgacon_scrolldelta
};
