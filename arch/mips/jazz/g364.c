/*
 *  linux/drivers/char/g364.c
 *
 *  Copyright (C) 1996  Wayne Hodgen
 *
 *  Based on and using chunks of Jay Estabrooks tga.c
 *
 * This module exports the console io support for Inmos's G364 controller
 * used in Mips Magnums and clones. Based on the hardware desc for the
 * Olivetti M700-10 ie. an Inmos G364 based card in a dedicated video slot,
 * 2MB dual ported VRAM with a 64 bit data path, 256 color lookup table,
 * palette of 16.7M and a user definable 64x64 hardware cursor.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/consolemap.h>
#include <linux/selection.h>
#include <linux/console_struct.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/types.h>

extern void register_console(void (*proc)(const char *));
extern void console_print(const char *);
unsigned video_res_x;

/* 
 * Various defines for the G364
 */
#define G364_MEM_BASE   0xe0800000
#define G364_PORT_BASE  0xe0200000
#define ID_REG 		0xe0200000  	/* Read only */
#define BOOT_REG 	0xe0280000
#define TIMING_REG 	0xe0280108 	/* to 0x080170 - DON'T TOUCH! */
#define MASK_REG 	0xe0280200
#define CTLA_REG 	0xe0280300
#define CURS_TOGGLE 	0x800000
#define BIT_PER_PIX	0x700000	/* bits 22 to 20 of Control A */
#define DELAY_SAMPLE    0x080000
#define PORT_INTER	0x040000
#define PIX_PIPE_DEL	0x030000	/* bits 17 and 16 of Control A */
#define PIX_PIPE_DEL2	0x008000	/* same as above - don't ask me why */
#define TR_CYCLE_TOG	0x004000
#define VRAM_ADR_INC	0x003000	/* bits 13 and 12 of Control A */
#define BLANK_OFF	0x000800
#define FORCE_BLANK	0x000400
#define BLK_FUN_SWTCH	0x000200
#define BLANK_IO	0x000100
#define BLANK_LEVEL	0x000080
#define A_VID_FORM	0x000040
#define D_SYNC_FORM	0x000020
#define FRAME_FLY_PAT	0x000010
#define OP_MODE		0x000008
#define INTL_STAND	0x000004
#define SCRN_FORM	0x000002
#define ENABLE_VTG	0x000001	
#define TOP_REG 	0xe0280400
#define CURS_PAL_REG 	0xe0280508 	/* to 0x080518 */
#define CHKSUM_REG 	0xe0280600 	/* to 0x080610 - unused */
#define CURS_POS_REG 	0xe0280638
#define CLR_PAL_REG 	0xe0280800	/* to 0x080ff8 */
#define CURS_PAT_REG 	0xe0281000	/* to 0x081ff8 */
#define MON_ID_REG 	0xe0300000 	/* unused */
#define RESET_REG 	0xe0380000  	/* Write only */

/*
 * built-in font management constants
 *
 * NOTE: the built-in font is 8x16, and the video resolution
 * is either 1280x1024 @ 60Hz or 1024x768 @ 60 or 78Hz.
 */
#define FONTSIZE_X 	8 	/*  8 pixels wide */
#define FONTSIZE_Y 	16 	/* 16 pixels high */

unsigned char g364_font[] = {
#include "g364.fnt"
};

u32 g364_cursor[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0xffff0000,0,0,0,0xffff0000,0,0,0,0xffff0000,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

#ifdef CONFIG_REMOTE_DEBUG
/* #define DEBUG_G364 */

extern int putDebugChar(char c);

void
putDebugString(char *d_str)
{
	while (*d_str != '\0') {
		putDebugChar(*d_str);
		d_str++;
	}
	if (*--d_str != '\n')
		putDebugChar('\n');
}
#endif

void g364_clear_screen(void);

int cursor_initialised=0;

unsigned long
con_type_init(unsigned long kmem_start, const char **display_desc)
{
        can_do_color = 1;

        /*
        * fake the screen memory with some CPU memory
        */
        video_mem_base = kmem_start;
        kmem_start += video_screen_size;
        video_mem_term = kmem_start;
        video_type = VIDEO_TYPE_MIPS_G364;
	video_res_x = video_num_columns * FONTSIZE_X;

        *display_desc = "G364";

	return kmem_start;
}

con_type_init_finish(void)
{
}

void
__set_origin(unsigned short offset)
{
  /*
   * should not be called, but if so, do nothing...
   */
}

/*
 * Hide the cursor from view, during blanking, usually...
 */
void
hide_cursor(void)
{
/*	*(unsigned int *) CTLA_REG &= ~CURS_TOGGLE; */
}

void
init_g364_cursor(void)
{
	volatile unsigned int *ptr = (unsigned int *) CURS_PAL_REG;

        *ptr |= 0x00ffffff;
	ptr[2] |= 0x00ffffff;
	ptr[4] |= 0x00ffffff;

	memcpy((unsigned int *)CURS_PAT_REG, &g364_cursor, 1024);
	cursor_initialised = 1;
}

/*
 * Set the cursor on.
 */
void
set_cursor(int currcons)
{
/*
	if (!cursor_initialised)
		init_g364_cursor();

	if (console_blanked)
		return;

	*(unsigned int *) CTLA_REG |= CURS_TOGGLE;
*/
}

/*
 * NOTE: get_scrmem() and set_scrmem() are here only because
 * the VGA version of set_scrmem() has some direct VGA references.
 */
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
        if (video_mem_term - video_mem_base < offset + video_screen_size)
          offset = 0;   /* strange ... */
        memcpyw((unsigned short *)(video_mem_base + offset),
                (unsigned short *) origin, video_screen_size);
        video_mem_start = video_mem_base;
        video_mem_end = video_mem_term;
        origin = video_mem_base + offset;
        scr_end = origin + video_screen_size;
        pos = origin + y*video_size_row + (x<<1);
}

/*
 * Fill out later
 */
void
set_palette(void)
{
	int i, j;
	volatile unsigned int *ptr = (volatile unsigned int *) CLR_PAL_REG;

	for (i = 0; i < 16; i++,ptr+=2) {
		j = color_table[i];
		*ptr = ((default_red[j] << 16) |
			 (default_grn[j] << 8) |
			 (default_blu[j]));
	}
}

/*
 * NOTE:
 * this is here, and not in console.c, because the VGA version
 * tests the controller type to see if color can be done. We *KNOW*
 * that we can do color on the G364.
 *
 */

int
set_get_cmap(unsigned char * arg, int set)
{
	int i;

	for (i=0; i<16; i++) {
		if (set) {
			if (!access_ok(VERIFY_READ, (void *)arg, 16*3)) goto fault;
			if (__get_user(default_red[i], arg++)) goto fault;
			if (__get_user(default_grn[i], arg++)) goto fault;
			if (__get_user(default_blu[i], arg++)) goto fault;
                } else {
			if (!access_ok(VERIFY_WRITE, (void *)arg, 16*3)) goto fault;
                        if (__put_user(default_red[i], arg++)) goto fault;
                        if (__put_user(default_grn[i], arg++)) goto fault;
                        if (__put_user(default_blu[i], arg++)) goto fault;
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

fault:
	return -EFAULT;
}

/*
 * Adjust the screen to fit a font of a certain height
 *
 * Returns < 0 for error, 0 if nothing changed, and the number
 * of lines on the adjusted console if changed.
 *
 * for now, we only support the built-in font...
 */
int
con_adjust_height(unsigned long fontheight)
{
        return -EINVAL;
}

/*
 * PIO_FONT support.
 *
 * for now, we will use/allow *only* our built-in font...
 */
int
set_get_font(char * arg, int set, int ch512)
{
        return -EINVAL;
}

/*
 * print a character to a graphics console.
 */
void
g364_blitc(unsigned short charattr, unsigned long addr)
{
  int row, col, temp;
  register unsigned long long *dst, *font_row;
  register int i;
  char c;

  /*
   * calculate (row,col) from addr and video_mem_base
   */
  temp = (addr - video_mem_base) >> 1;
  col = temp % 128;
  row = (temp - col) / 128;

  /*
   * calculate destination address
   */
  dst = (unsigned long long *) ( G364_MEM_BASE
                           + ( row * video_res_x * FONTSIZE_Y )
                           + ( col * FONTSIZE_X ) );

  c = charattr & 0x00ff;
  if (c == 0x20) {
    for (i=0; i < FONTSIZE_Y; i++, dst += video_num_columns)
      *dst = 0x00000000;
  } else {
    font_row = (unsigned long long *) &g364_font[(c << 7)];
    for (i=0; i < FONTSIZE_Y; i++, font_row++, dst += video_num_columns)
      *dst = *font_row;
  }
}

/*
 * print a character to a graphics console. Colour version, slower!
 */
void
g364_blitc_colour(unsigned short charattr, unsigned long addr)
{
  int row, col, temp, c, attrib;
  register unsigned int fgmask, bgmask;
  register unsigned long long *dst, *font_row;
  register int i, stride;

  c = charattr & 0x00ff;
  attrib = (charattr >> 8) & 0x00ff;

  /*
   * extract foreground and background indices
   * NOTE: we always treat blink/underline bits as color for now...
   */
  fgmask = attrib & 0x0f;
  bgmask = (attrib >> 4) & 0x0f;

  /* i = (c & 0xff) << 7;  NOTE: assumption of 128 bytes per character bitmap */

  /*
   * calculate (row,col) from addr and video_mem_base
   */
  temp = (addr - video_mem_base) >> 1;
  col = temp % 128;
  row = (temp - col) / 128;
  stride = video_res_x / 8;

  /*
   * calculate destination address
   */
  dst = (unsigned long long *) ( G364_MEM_BASE
                           + ( row * video_res_x * FONTSIZE_Y )
                           + ( col * FONTSIZE_X ) );

  font_row = (unsigned long long *) &g364_font[((c & 0xff) << 7)];

  for (i=0; i < FONTSIZE_Y; i++, font_row++, dst += stride) {
    *dst = *font_row;
  }
}

/*
 * dummy routines for the VESA blanking code, which is VGA only,
 * so we don't have to carry that stuff around for the G364...
 */
void
vesa_powerdown(void)
{
}

void
vesa_blank(void)
{
}

void
vesa_unblank(void)
{
}

void
set_vesa_blanking(const unsigned long arg)
{
}
