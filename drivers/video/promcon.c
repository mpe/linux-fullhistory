/* $Id: promcon.c,v 1.6 1998/07/19 12:49:26 mj Exp $
 * Console driver utilizing PROM sun terminal emulation
 *
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998  Jakub Jelinek  (jj@ultra.linux.cz)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/selection.h>
#include <linux/fb.h>

#include <asm/oplib.h>

static short pw = 80 - 1, ph = 34 - 1;
static short px, py;

#define PROMCON_COLOR 1

#if PROMCON_COLOR
#define inverted(s)	((((s) & 0x7700) == 0x0700) ? 0 : 1)
#else
#define inverted(s)	(((s) & 0x0800) ? 1 : 0)
#endif

static __inline__ void
promcon_puts(char *buf, int cnt)
{
	prom_printf("%*.*s", cnt, cnt, buf);
}

static int
promcon_start(struct vc_data *conp, char *b)
{
	unsigned short *s = (unsigned short *)
			(conp->vc_origin + py * conp->vc_size_row + (px << 1));

	if (px == pw) {
		unsigned short *t = s - 1;

		if (inverted(*s) && inverted(*t))
			return sprintf(b, "\b\033[7m%c\b\033[@%c\033[m",
				       *s, *t);
		else if (inverted(*s))
			return sprintf(b, "\b\033[7m%c\033[m\b\033[@%c",
				       *s, *t);
		else if (inverted(*t))
			return sprintf(b, "\b%c\b\033[@\033[7m%c\033[m",
				       *s, *t);
		else
			return sprintf(b, "\b%c\b\033[@%c", *s, *t);
	}

	if (inverted(*s))
		return sprintf(b, "\033[7m%c\033[m\b", *s);
	else
		return sprintf(b, "%c\b", *s);
}

static int
promcon_end(struct vc_data *conp, char *b)
{
	unsigned short *s = (unsigned short *)
			(conp->vc_origin + py * conp->vc_size_row + (px << 1));
	char *p = b;

	b += sprintf(b, "\033[%d;%dH", py + 1, px + 1);

	if (px == pw) {
		unsigned short *t = s - 1;

		if (inverted(*s) && inverted(*t))
			b += sprintf(b, "\b%c\b\033[@\033[7m%c\033[m", *s, *t);
		else if (inverted(*s))
			b += sprintf(b, "\b%c\b\033[@%c", *s, *t);
		else if (inverted(*t))
			b += sprintf(b, "\b\033[7m%c\b\033[@%c\033[m", *s, *t);
		else
			b += sprintf(b, "\b\033[7m%c\033[m\b\033[@%c", *s, *t);
		return b - p;
	}

	if (inverted(*s))
		b += sprintf(b, "%c\b", *s);
	else
		b += sprintf(b, "\033[7m%c\033[m\b", *s);
	return b - p;
}

__initfunc(const char *promcon_startup(void))
{
	const char *display_desc = "PROM";
	int node;
	char buf[40];
	
	node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "options");
	if (prom_getproperty(node,  "screen-#columns", buf, 40) != -1) {
		pw = simple_strtoul(buf, NULL, 0);
		if (pw < 10 || pw > 256)
			pw = 80;
		pw--;
	}
	if (prom_getproperty(node,  "screen-#rows", buf, 40) != -1) {
		ph = simple_strtoul(buf, NULL, 0);
		if (ph < 10 || ph > 256)
			ph = 34;
		ph--;
	}
	promcon_puts("\033[H\033[J", 6);
	return display_desc;
}

static void
promcon_init(struct vc_data *conp, int init)
{
	conp->vc_can_do_color = PROMCON_COLOR;
	conp->vc_cols = pw + 1;
	conp->vc_rows = ph + 1;
}

static int
promcon_switch(struct vc_data *conp)
{
	return 1;
}

static unsigned short *
promcon_repaint_line(unsigned short *s, unsigned char *buf, unsigned char **bp)
{
	int cnt = pw + 1;
	int attr = -1;
	unsigned char *b = *bp;

	while (cnt--) {
		if (attr != inverted(*s)) {
			attr = inverted(*s);
			if (attr) {
				strcpy (b, "\033[7m");
				b += 4;
			} else {
				strcpy (b, "\033[m");
				b += 3;
			}
		}
		*b++ = *s++;
		if (b - buf >= 224) {
			promcon_puts(buf, b - buf);
			b = buf;
		}
	}
	*bp = b;
	return s;
}

static void
promcon_putcs(struct vc_data *conp, const unsigned short *s,
	      int count, int y, int x)
{
	unsigned char buf[256], *b = buf;
	unsigned short attr = *s;
	unsigned char save;
	int i, last = 0;

	if (console_blanked)
		return;
	
	if (count <= 0)
		return;

	b += promcon_start(conp, b);

	if (x + count >= pw + 1) {
		if (count == 1) {
			x -= 1;
			save = *(unsigned short *)(conp->vc_origin
						   + y * conp->vc_size_row
						   + (x << 1));

			if (px != x || py != y) {
				b += sprintf(b, "\033[%d;%dH", y + 1, x + 1);
				px = x;
				py = y;
			}

			if (inverted(attr))
				b += sprintf(b, "\033[7m%c\033[m", *s++);
			else
				b += sprintf(b, "%c", *s++);

			strcpy(b, "\b\033[@");
			b += 4;

			if (inverted(save))
				b += sprintf(b, "\033[7m%c\033[m", save);
			else
				b += sprintf(b, "%c", save);

			px++;

			b += promcon_end(conp, b);
			promcon_puts(buf, b - buf);
			return;
		} else {
			last = 1;
			count = pw - x - 1;
		}
	}

	if (inverted(attr)) {
		strcpy(b, "\033[7m");
		b += 4;
	}

	if (px != x || py != y) {
		b += sprintf(b, "\033[%d;%dH", y + 1, x + 1);
		px = x;
		py = y;
	}

	for (i = 0; i < count; i++) {
		if (b - buf >= 224) {
			promcon_puts(buf, b - buf);
			b = buf;
		}
		*b++ = *s++;
	}

	px += count;

	if (last) {
		save = *s++;
		b += sprintf(b, "%c\b\033[@%c", *s++, save);
		px++;
	}

	if (inverted(attr)) {
		strcpy(b, "\033[m");
		b += 3;
	}

	b += promcon_end(conp, b);
	promcon_puts(buf, b - buf);
}

static void
promcon_putc(struct vc_data *conp, int c, int y, int x)
{
	unsigned short s = c;

	if (console_blanked)
		return;
	
	promcon_putcs(conp, &s, 1, y, x);
}

static void
promcon_clear(struct vc_data *conp, int sy, int sx, int height, int width)
{
	unsigned char buf[256], *b = buf;
	int i, j;

	if (console_blanked)
		return;
	
	b += promcon_start(conp, b);

	if (!sx && width == pw + 1) {

		if (!sy && height == ph + 1) {
			strcpy(b, "\033[H\033[J");
			b += 6;
			b += promcon_end(conp, b);
			promcon_puts(buf, b - buf);
			return;
		} else if (sy + height == ph + 1) {
			b += sprintf(b, "\033[%dH\033[J", sy + 1);
			b += promcon_end(conp, b);
			promcon_puts(buf, b - buf);
			return;
		}

		b += sprintf(b, "\033[%dH", sy + 1);
		for (i = 1; i < height; i++) {
			strcpy(b, "\033[K\n");
			b += 4;
		}

		strcpy(b, "\033[K");
		b += 3;

		b += promcon_end(conp, b);
		promcon_puts(buf, b - buf);
		return;

	} else if (sx + width == pw + 1) {

		b += sprintf(b, "\033[%d;%dH", sy + 1, sx + 1);
		for (i = 1; i < height; i++) {
			strcpy(b, "\033[K\n");
			b += 4;
		}

		strcpy(b, "\033[K");
		b += 3;

		b += promcon_end(conp, b);
		promcon_puts(buf, b - buf);
		return;
	}

	for (i = sy + 1; i <= sy + height; i++) {
		b += sprintf(b, "\033[%d;%dH", i, sx + 1);
		for (j = 0; j < width; j++)
			*b++ = ' ';
		if (b - buf + width >= 224) {
			promcon_puts(buf, b - buf);
			b = buf;
		}
	}

	b += promcon_end(conp, b);
	promcon_puts(buf, b - buf);
}
                        
static void
promcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
	      int height, int width)
{
	char buf[256], *b = buf;

	if (console_blanked)
		return;
	
	b += promcon_start(conp, b);
	if (sy == dy && height == 1) {
		if (dx > sx && dx + width == conp->vc_cols)
			b += sprintf(b, "\033[%d;%dH\033[%d@\033[%d;%dH",
				     sy + 1, sx + 1, dx - sx, py + 1, px + 1);
		else if (dx < sx && sx + width == conp->vc_cols)
			b += sprintf(b, "\033[%d;%dH\033[%dP\033[%d;%dH",
				     dy + 1, dx + 1, sx - dx, py + 1, px + 1);

		b += promcon_end(conp, b);
		promcon_puts(buf, b - buf);
		return;
	}

	/*
	 * FIXME: What to do here???
	 * Current console.c should not call it like that ever.
	 */
	prom_printf("\033[7mFIXME: bmove not handled\033[m\n");
}

static void
promcon_cursor(struct vc_data *conp, int mode)
{
	char buf[32], *b = buf;

	switch (mode) {
	case CM_ERASE:
		break;

	case CM_MOVE:
	case CM_DRAW:
		b += promcon_start(conp, b);
		if (px != conp->vc_x || py != conp->vc_y) {
			px = conp->vc_x;
			py = conp->vc_y;
			b += sprintf(b, "\033[%d;%dH", py + 1, px + 1);
		}
		promcon_puts(buf, b - buf);
		break;
	}
}

static int
promcon_font_op(struct vc_data *conp, struct console_font_op *op)
{
	return -ENOSYS;
}
        
static int
promcon_blank(struct vc_data *conp, int blank)
{
	if (blank) {
		promcon_puts("\033[H\033[J\033[7m \033[m\b", 15);
		return 0;
	} else {
		/* Let console.c redraw */
		return 1;
	}
}

static int
promcon_scroll(struct vc_data *conp, int t, int b, int dir, int count)
{
	unsigned char buf[256], *p = buf;
	unsigned short *s;
	int i;

	if (console_blanked)
		return 0;
	
	p += promcon_start(conp, p);

	switch (dir) {
	case SM_UP:
		if (b == ph + 1) {
			p += sprintf(p, "\033[%dH\033[%dM", t + 1, count);
			px = 0;
			py = t;
			p += promcon_end(conp, p);
			promcon_puts(buf, p - buf);
			break;
		}

		s = (unsigned short *)(conp->vc_origin
				       + (t + count) * conp->vc_size_row);

		p += sprintf(p, "\033[%dH", t + 1);

		for (i = t; i < b - count; i++)
			s = promcon_repaint_line(s, buf, &p);

		for (; i < b - 1; i++) {
			strcpy(p, "\033[K\n");
			p += 4;
			if (p - buf >= 224) {
				promcon_puts(buf, p - buf);
				p = buf;
			}
		}

		strcpy(p, "\033[K");
		p += 3;

		p += promcon_end(conp, p);
		promcon_puts(buf, p - buf);
		break;

	case SM_DOWN:
		if (b == ph + 1) {
			p += sprintf(p, "\033[%dH\033[%dL", t + 1, count);
			px = 0;
			py = t;
			p += promcon_end(conp, p);
			promcon_puts(buf, p - buf);
			break;
		}

		s = (unsigned short *)(conp->vc_origin + t * conp->vc_size_row);

		p += sprintf(p, "\033[%dH", t + 1);

		for (i = t; i < t + count; i++) {
			strcpy(p, "\033[K\n");
			p += 4;
			if (p - buf >= 224) {
				promcon_puts(buf, p - buf);
				p = buf;
			}
		}

		for (; i < b; i++)
			s = promcon_repaint_line(s, buf, &p);

		p += promcon_end(conp, p);
		promcon_puts(buf, p - buf);
		break;
	}

	return 0;
}

/*
 *  The console 'switch' structure for the VGA based console
 */

static int promcon_dummy(void)
{
        return 0;
}

#define DUMMY (void *) promcon_dummy

struct consw prom_con = {
	con_startup:		promcon_startup,
	con_init:		promcon_init,
	con_deinit:		DUMMY,
	con_clear:		promcon_clear,
	con_putc:		promcon_putc,
	con_putcs:		promcon_putcs,
	con_cursor:		promcon_cursor,
	con_scroll:		promcon_scroll,
	con_bmove:		promcon_bmove,
	con_switch:		promcon_switch,
	con_blank:		promcon_blank,
	con_font_op:		promcon_font_op,
	con_set_palette:	DUMMY,
	con_scrolldelta:	DUMMY,
	con_set_origin:		NULL,
	con_save_screen:	NULL,
	con_build_attr:		NULL,
	con_invert_region:	NULL,
};
