/*
 * cons_newport.c: Newport graphics console code for the SGI.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: cons_newport.c,v 1.1 1998/01/10 19:05:47 ecd Exp $
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/version.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/sgialib.h>
#include <asm/ptrace.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/consolemap.h>
#include <linux/selection.h>
#include <linux/console_struct.h>

#include "gconsole.h"
#include "newport.h"
#include "graphics.h" /* Just for now */
#include <asm/gfx.h>
#include <asm/ng1.h>

#if 0
#include "linux_logo.h"
#endif

#define BMASK(c) (c << 24)

#define RENDER(regs, cp) do { \
(regs)->go.zpattern = BMASK((cp)[0x0]); (regs)->go.zpattern = BMASK((cp)[0x1]); \
(regs)->go.zpattern = BMASK((cp)[0x2]); (regs)->go.zpattern = BMASK((cp)[0x3]); \
(regs)->go.zpattern = BMASK((cp)[0x4]); (regs)->go.zpattern = BMASK((cp)[0x5]); \
(regs)->go.zpattern = BMASK((cp)[0x6]); (regs)->go.zpattern = BMASK((cp)[0x7]); \
(regs)->go.zpattern = BMASK((cp)[0x8]); (regs)->go.zpattern = BMASK((cp)[0x9]); \
(regs)->go.zpattern = BMASK((cp)[0xa]); (regs)->go.zpattern = BMASK((cp)[0xb]); \
(regs)->go.zpattern = BMASK((cp)[0xc]); (regs)->go.zpattern = BMASK((cp)[0xd]); \
(regs)->go.zpattern = BMASK((cp)[0xe]); (regs)->go.zpattern = BMASK((cp)[0xf]); \
} while(0)        

#define REVERSE_RENDER(regs, cp) do { \
(regs)->go.zpattern = BMASK((~(cp)[0x0])); (regs)->go.zpattern = BMASK((~(cp)[0x1])); \
(regs)->go.zpattern = BMASK((~(cp)[0x2])); (regs)->go.zpattern = BMASK((~(cp)[0x3])); \
(regs)->go.zpattern = BMASK((~(cp)[0x4])); (regs)->go.zpattern = BMASK((~(cp)[0x5])); \
(regs)->go.zpattern = BMASK((~(cp)[0x6])); (regs)->go.zpattern = BMASK((~(cp)[0x7])); \
(regs)->go.zpattern = BMASK((~(cp)[0x8])); (regs)->go.zpattern = BMASK((~(cp)[0x9])); \
(regs)->go.zpattern = BMASK((~(cp)[0xa])); (regs)->go.zpattern = BMASK((~(cp)[0xb])); \
(regs)->go.zpattern = BMASK((~(cp)[0xc])); (regs)->go.zpattern = BMASK((~(cp)[0xd])); \
(regs)->go.zpattern = BMASK((~(cp)[0xe])); (regs)->go.zpattern = BMASK((~(cp)[0xf])); \
} while(0)        

extern int default_red[16], default_grn[16], default_blu[16];
extern unsigned char video_type;

static int cursor_pos = -1;
struct newport_regs *npregs;

#define TESTVAL 0xdeadbeef
#define XSTI_TO_FXSTART(val) (((val) & 0xffff) << 11)

static inline void
newport_disable_video(void)
{
	unsigned short treg;

	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg & ~(VC2_CTRL_EVIDEO)));
}

static inline void
newport_enable_video(void)
{
	unsigned short treg;

	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg | VC2_CTRL_EVIDEO));
}

static inline void
newport_disable_cursor(void)
{
	unsigned short treg;

	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg & ~(VC2_CTRL_ECDISP)));
}

#if 0
static inline void
newport_enable_cursor(void)
{
	unsigned short treg;

	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg | VC2_CTRL_ECDISP));
}
#endif

static inline void
newport_init_cmap(void)
{
	unsigned short i;

	for(i = 0; i < 16; i++) {
		newport_bfwait();
		newport_cmap_setaddr(npregs, color_table[i]);
		newport_cmap_setrgb(npregs,
				    default_red[i],
				    default_grn[i],
				    default_blu[i]);
	}
}

#if 0
static inline void
newport_init_cursor(void)
{
	unsigned char cursor[256];
	unsigned short *cookie;
	int i;

	for(i = 0; i < 256; i++)
		cursor[i] = 0x0;
	for(i = 211; i < 256; i+=4) {
		cursor[i] = 0xff;
#if 0
		cursor[(i + 128) << 2] = 0xff;
		cursor[((i + 128) << 2) + 1] = 0xff;
#endif
	}

	/* Load the SRAM on the VC2 for this new GLYPH. */
	cookie = (unsigned short *) cursor;
	newport_vc2_set(npregs, VC2_IREG_RADDR, VC2_CGLYPH_ADDR);
	npregs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_RAM |
			       NPORT_DMODE_W2 | VC2_PROTOCOL);
	for(i = 0; i < 128; i++) {
		newport_bfwait();
		npregs->set.dcbdata0.hwords.s1 = *cookie++;
	}

	/* Place the cursor at origin. */
	newport_vc2_set(npregs, VC2_IREG_CURSX, 0);
	newport_vc2_set(npregs, VC2_IREG_CURSY, 0);
	newport_enable_cursor();
}
#endif

static inline void
newport_clear_screen(void)
{
	newport_wait();
	npregs->set.wrmask = 0xffffffff;
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			      NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX |
			      NPORT_DMODE0_STOPY);
	npregs->set.colori = 0;
	npregs->set.xystarti = 0;
	npregs->go.xyendi = (((1280 + 63) << 16)|(1024));
	newport_bfwait();
}

static inline void
newport_render_version(void)
{
#if 0
	unsigned short *ush;
	int currcons = 0;
	char *p;

	ush = (unsigned short *) video_mem_base + video_num_columns * 2 + 20;
	for (p = "SGI/Linux version " UTS_RELEASE; *p; p++, ush++) {
		*ush = (attr << 8) + *p;
		newport_blitc (*ush, (unsigned long) ush);
	}
#endif
}

#if 0
static inline void
newport_render_logo(void)
{
	int i, xpos, ypos;
	unsigned char *bmap;

	xpos = 8;
	ypos = 18;

	newport_wait();
	npregs->set.colori = 9;
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			      NPORT_DMODE0_STOPX | NPORT_DMODE0_ZPENAB |
			      NPORT_DMODE0_L32);

	for(i = 0; i < 80; i+=8) {
		/* Set coordinates for bitmap operation. */
		npregs->set.xystarti = ((xpos + i) << 16) | ypos;
		npregs->set.xyendi = (((xpos + i) + 7) << 16);
		newport_wait();

		bmap = linux_logo + (i * 80);
		RENDER(npregs, bmap); bmap += 0x10;
		RENDER(npregs, bmap); bmap += 0x10;
		RENDER(npregs, bmap); bmap += 0x10;
		RENDER(npregs, bmap); bmap += 0x10;
		RENDER(npregs, bmap);
	}
	prom_getchar();
	prom_imode();
}
#endif

static inline void
newport_render_background(int xpos, int ypos, int ci)
{
	newport_wait();
	npregs->set.wrmask = 0xffffffff;
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			      NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX |
			      NPORT_DMODE0_STOPY);
	npregs->set.colori = ci;
	npregs->set.xystarti = (xpos << 16) | ypos;
	npregs->go.xyendi = ((xpos + 7) << 16) | (ypos + 15);
}

void
newport_set_origin(unsigned short offset)
{
	/* maybe this works... */
	__origin = offset;
}

void
newport_hide_cursor(void)
{
	int xpos, ypos, idx;
	unsigned long flags;

	if(vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return;
	save_and_cli(flags);

	idx = cursor_pos;
	if(idx == -1) {
		restore_flags(flags);
		return;
	}
	xpos = 8 + ((idx % video_num_columns) << 3);
	ypos = 18 + ((idx / video_num_columns) << 4);
	newport_render_background(xpos, ypos, 0);
	restore_flags(flags);
}

void
newport_set_cursor(int currcons)
{
	int xpos, ypos, idx, oldpos;
	unsigned short *sp, *osp, cattr;
	unsigned long flags;
	unsigned char *p;

	if (currcons != fg_console || console_blanked || vcmode == KD_GRAPHICS)
		return;

	if (__real_origin != __origin)
		__set_origin(__real_origin);

	save_and_cli(flags);

	idx = (pos - video_mem_base) >> 1;
	sp = (unsigned short *) pos;
	oldpos = cursor_pos;
	cursor_pos = idx;
	if(!deccm) {
		hide_cursor();
		restore_flags(flags);
		return;
	}
	xpos = 8 + ((idx % video_num_columns) << 3);
	ypos = 18 + ((idx / video_num_columns) << 4);
	if(oldpos != -1) {
		int oxpos, oypos;

		/* Restore old location. */
		osp = (unsigned short *) ((oldpos << 1) + video_mem_base);
		oxpos = 8 + ((oldpos % video_num_columns) << 3);
		oypos = 18 + ((oldpos / video_num_columns) << 4);
		cattr = *osp;
		newport_render_background(oxpos, oypos, (cattr & 0xf000) >> 12);
		p = &vga_font[(cattr & 0xff) << 4];
		newport_wait();
		npregs->set.colori = (cattr & 0x0f00) >> 8;
		npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
				      NPORT_DMODE0_STOPX | NPORT_DMODE0_ZPENAB |
				      NPORT_DMODE0_L32);
		npregs->set.xystarti = (oxpos << 16) | oypos;
		npregs->set.xyendi = ((oxpos + 7) << 16);
		newport_wait();
		RENDER(npregs, p);
	}
	cattr = *sp;
	newport_render_background(xpos, ypos, (cattr & 0xf000) >> 12);
	p = &vga_font[(cattr & 0xff) << 4];
	newport_wait();
	npregs->set.colori = (cattr & 0x0f00) >> 8;
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			      NPORT_DMODE0_STOPX | NPORT_DMODE0_ZPENAB |
			      NPORT_DMODE0_L32);
	npregs->set.xystarti = (xpos << 16) | ypos;
	npregs->set.xyendi = ((xpos + 7) << 16);
	newport_wait();
	REVERSE_RENDER(npregs, p);
	restore_flags (flags);
	return;
}

void
newport_get_scrmem(int currcons)
{
	memcpyw((unsigned short *)vc_scrbuf[currcons],
		(unsigned short *)origin, video_screen_size);
	origin = video_mem_start = (unsigned long)vc_scrbuf[currcons];
	scr_end = video_mem_end = video_mem_start + video_screen_size;
	pos = origin + y*video_size_row + (x<<1);
}

void
newport_set_scrmem(int currcons, long offset)
{
	if (video_mem_term - video_mem_base < offset + video_screen_size)
		offset = 0;
	memcpyw((unsigned short *)(video_mem_base + offset),
		(unsigned short *) origin, video_screen_size);
	video_mem_start = video_mem_base;
	video_mem_end = video_mem_term;
	origin = video_mem_base + offset;
	scr_end = origin + video_screen_size;
	pos = origin + y*video_size_row + (x<<1);
	has_wrapped = 0;
}

int
newport_set_get_cmap(unsigned char * arg, int set)
{
	unsigned short ent;
	int i;

	i = verify_area(set ? VERIFY_READ : VERIFY_WRITE, (void *)arg, 16*3);
	if (i)
		return i;

	for (i=0; i<16; i++) {
		if (set) {
			__get_user(default_red[i], arg++);
			__get_user(default_grn[i], arg++);
			__get_user(default_blu[i], arg++);
		} else {
			__put_user (default_red[i], arg++);
			__put_user (default_grn[i], arg++);
			__put_user (default_blu[i], arg++);
		}
	}
	if (set) {
		for (i=0; i<MAX_NR_CONSOLES; i++) {
			if (vc_cons_allocated(i)) {
				int j, k;
				for (j = k = 0; j<16; j++) {
					vc_cons[i].d->vc_palette[k++] =
						default_red[j];
					vc_cons[i].d->vc_palette[k++] =
						default_grn[j];
					vc_cons[i].d->vc_palette[k++] =
						default_blu[j];
				}
			}
		}
		if(console_blanked || vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
			return 0;
		for(ent = 0; ent < 16; ent++) {
			newport_bfwait();
			newport_cmap_setaddr(npregs, ent);
			newport_cmap_setrgb(npregs,
					    default_red[ent],
					    default_grn[ent],
					    default_blu[ent]);
		}
	}

	return 0;
}

void
newport_blitc(unsigned short charattr, unsigned long addr)
{
	int idx, xpos, ypos;
	unsigned char *p;

	idx = (addr - (video_mem_base + (__origin<<1))) >> 1;
	xpos = 8 + ((idx % video_num_columns) << 3);
	ypos = 18 + ((idx / video_num_columns) << 4);

	p = &vga_font[(charattr & 0xff) << 4];
	charattr = (charattr >> 8) & 0xff;

	newport_render_background(xpos, ypos, (charattr & 0xf0) >> 4);

	/* Set the color and drawing mode. */
	newport_wait();
	npregs->set.colori = charattr & 0xf;
	npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			      NPORT_DMODE0_STOPX | NPORT_DMODE0_ZPENAB |
			      NPORT_DMODE0_L32);

	/* Set coordinates for bitmap operation. */
	npregs->set.xystarti = (xpos << 16) | ypos;
	npregs->set.xyendi = ((xpos + 7) << 16);
	newport_wait();

	/* Go, baby, go... */
	RENDER(npregs, p);
}

void
newport_memsetw(void * s, unsigned short c, unsigned int count)
{
	unsigned short * addr = (unsigned short *) s;

	count /= 2;
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS) {
		while (count) {
			count--;
			*addr++ = c;
		}
		return;
	}
	if ((unsigned long) addr + count > video_mem_term ||
	    (unsigned long) addr < video_mem_base) {
	    	if ((unsigned long) addr + count <= video_mem_term ||
	    	    (unsigned long) addr > video_mem_base) {
			while (count) {
				count--;
				*addr++ = c;
			}
			return;
	    	} else {
			while (count) {
				count--;
				scr_writew(c, addr++);
			}
		}
	} else {
		while (count) {
			count--;
			if (*addr != c) {
				newport_blitc(c, (unsigned long)addr);
				*addr++ = c;
			} else
				addr++;
		}
	}
}

void
newport_memcpyw(unsigned short *to, unsigned short *from, unsigned int count)
{
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS) {
		memcpy(to, from, count);
		return;
	}
	if ((unsigned long) to + count > video_mem_term ||
	    (unsigned long) to < video_mem_base) {
	    	if ((unsigned long) to + count <= video_mem_term ||
	    	    (unsigned long) to > video_mem_base)
	    	    	memcpy(to, from, count);
	    	else {
	    		count /= 2;
			while (count) {
				count--;
				scr_writew(scr_readw(from++), to++);
			}
		}
	} else {
		count /= 2;
		while (count) {
			count--;
			if (*to != *from) {
				newport_blitc(*from, (unsigned long)to);
				*to++ = *from++;
			} else {
				from++;
				to++;
			}
		}
	}
}

struct console_ops newport_console = {
	newport_set_origin,
	newport_hide_cursor,
	newport_set_cursor,
	newport_get_scrmem,
	newport_set_scrmem,
	newport_set_get_cmap,
	newport_blitc,
	newport_memsetw,
	newport_memcpyw
};

/* Currently hard-coded values that are the same as those found on my system */
struct ng1_info newport_board_info = {
	{ "NG1", "" /* what is the label? */, 1280, 1024, sizeof (struct ng1_info) },
	6,			/* boardrev */
	1,			/* rex3rev */
	0,			/* vc2rev */
	2,			/* monitor type */
        0,			/* videoinstalled */
	3,			/* mcrev */
	24,			/* bitplanes */
	0,			/* xmap9rev */
	2,			/* cmaprev */
	{ 256, 1280, 1024, 76},	/* ng1_vof_info */
	13,			/* paneltype */
	0
};

void
newport_reset (void)
{
	newport_wait();
	newport_enable_video();

	/* Init the cursor disappear. */
	newport_wait();
#if 0
	newport_init_cursor();
#else
	newport_disable_cursor();
#endif

	newport_init_cmap();

	/* Clear the screen. */
	newport_clear_screen();
}

/* right now the newport does not do anything at all */
struct graphics_ops newport_graphic_ops = {
	0,			      /* owner */
	0,			      /* current user */
	(void *) &newport_board_info, /* board info */
	sizeof (struct ng1_info),     /* size of our data structure */
	0, 0,			      /* g_regs, g_regs_size */
	newport_save, newport_restore, /* g_save_context, g_restore_context */
	newport_reset, newport_ioctl /* g_reset_console, g_ioctl */
};

struct graphics_ops *
newport_probe (int slot, const char **name)
{
	struct newport_regs *p;

	npregs = (struct newport_regs *) (KSEG1 + 0x1f0f0000);
	
	p = npregs;
	p->cset.config = NPORT_CFG_GD0;

	if(newport_wait()) {
		prom_printf("whoops, timeout, no NEWPORT there?");
		return 0;
	}

	p->set.xstarti = TESTVAL; if(p->set._xstart.i != XSTI_TO_FXSTART(TESTVAL)) {
		prom_printf("newport_probe: read back wrong value ;-(\n");
		return 0;
	}

	if (slot == 0){
		register_gconsole (&newport_console);
		video_type = VIDEO_TYPE_SGI;
		can_do_color = 1;
		*name = "NEWPORT";
	}

	newport_reset ();
	newport_render_version();
#if 0
	newport_render_logo();
#endif
	newport_graphic_ops.g_regs = 0x1f0f0000;
	newport_graphic_ops.g_regs_size = sizeof (struct newport_regs);
	return &newport_graphic_ops;
}
