/* $Id: promfb.c,v 1.3 1998/07/08 07:36:49 ecd Exp $
 *
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
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
#include <linux/fb.h>

#include "promfb.h"

extern int prom_stdout;

#define PROM_FONT_HEIGHT	22
#define PROM_FONT_WIDTH		12

void promfb_init(void);
void promfb_setup(char *options, int *ints);


static int
promfb_open(struct fb_info *info, int user)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int
promfb_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
promfb_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	struct fb_info_promfb *fb = promfbinfo(info);

	memcpy(fix, &fb->fix, sizeof(struct fb_fix_screeninfo));
	return 0;
}

static int
promfb_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	struct fb_info_promfb *fb = promfbinfo(info);

	memcpy(var, &fb->var, sizeof(struct fb_var_screeninfo));
	return 0;
}

static int
promfb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	return -EINVAL;
}

static int
promfb_get_cmap(struct fb_cmap *cmap, int kspc, int con, struct fb_info *info)
{
	return 0;
}

static int
promfb_set_cmap(struct fb_cmap *cmap, int kspc, int con, struct fb_info *info)
{
	return -EINVAL;
}

static int
promfb_pan_display(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	if (var->xoffset || var->yoffset)
		return -EINVAL;
	return 0;
}

static int
promfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
	     u_long arg, int con, struct fb_info *info)
{
	return -EINVAL;
}

static int
promfb_mmap(struct fb_info *info, struct file *file, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static struct fb_ops promfb_ops = {
	promfb_open,
	promfb_release,
	promfb_get_fix,
	promfb_get_var,
	promfb_set_var,
	promfb_get_cmap,
	promfb_set_cmap,
	promfb_pan_display,
	promfb_ioctl,
	promfb_mmap
};


static int
promfbcon_switch(int con, struct fb_info *info)
{
	return 0;
}

static int
promfbcon_updatevar(int con, struct fb_info *info)
{
	return 0;
}

static void
promfbcon_blank(int blank, struct fb_info *info)
{
	/* nothing */
}


static void
prom_start(struct fb_info_promfb *fb, struct display *d)
{
	unsigned short *dst = fb->data + d->next_line * fb->cury + fb->curx;

	if (fb->curx == fb->maxx && fb->cury == fb->maxy)
		return;

	if (*dst & 0x0800)
		prom_printf("\033[7m%c\033[m\033[%d;%dH",
			    *dst, fb->cury + 1, fb->curx + 1);
	else
		prom_printf("%c\033[%d;%dH",
			    *dst, fb->cury + 1, fb->curx + 1);
}

static void
prom_stop(struct fb_info_promfb *fb, struct display *d)
{
	unsigned short *dst = fb->data + d->next_line * fb->cury + fb->curx;

	if (fb->curx == fb->maxx && fb->cury == fb->maxy)
		return;

	if (*dst & 0x0800)
		prom_printf("\033[%d;%dH%c\033[%d;%dH",
			    fb->cury + 1, fb->curx + 1, *dst,
			    fb->cury + 1, fb->curx + 1);
	else
		prom_printf("\033[%d;%dH\033[7m%c\033[m\033[%d;%dH",
			    fb->cury + 1, fb->curx + 1, *dst,
			    fb->cury + 1, fb->curx + 1);
}

static void
fbcon_prom_setup(struct display *d)
{
	d->next_line = d->line_length;
	d->next_plane = 0;
}

static void
fbcon_prom_putc(struct vc_data *con, struct display *d, int c, int y, int x)
{
	struct fb_info_promfb *fb = promfbinfod(d);
	unsigned short *dst;

	dst = fb->data + y * d->line_length + x;
	if (*dst != (c & 0xffff)) {
		*dst = c & 0xffff;

		prom_start(fb, d);

		if (fb->curx != x || fb->cury != y) {
			prom_printf("\033[%d;%dH", y + 1, x + 1);
			fb->curx = x;
			fb->cury = y;
		}

		if (x == fb->maxx && y == fb->maxy)
			/* Sorry */ return;

		if (c & 0x0800)
			prom_printf("\033[7m%c\033[m", c);
		else
			prom_putchar(c);

		prom_stop(fb, d);
	}
}

static void
fbcon_prom_putcs(struct vc_data *con, struct display *d,
		 const unsigned short *s, int count, int y, int x)
{
	struct fb_info_promfb *fb = promfbinfod(d);
	unsigned short *dst;
	unsigned short attr = *s;
	int i;

	dst = fb->data + y * d->line_length + x;

	prom_start(fb, d);

	if (attr & 0x0800)
		prom_printf("\033[7m");

	for (i = 0; i < count; i++) {
		if (*dst != *s) {
			*dst = *s;

			if (fb->curx != x + i || fb->cury != y) {
				prom_printf("\033[%d;%dH", y + 1, x + i + 1);
				fb->curx = x + i;
				fb->cury = y;
			}

			if (fb->curx == fb->maxx && fb->cury == fb->maxy)
				/* Sorry */ goto out;

			prom_putchar(*s);

			if (i < count - 1)
				fb->curx++;
		}
		dst++;
		s++;
	}
out:
	if (attr & 0x0800)
		prom_printf("\033[m");

	prom_stop(fb, d);
}

static void
fbcon_prom_clear(struct vc_data *con, struct display *d, int sy, int sx,
		 int h, int w)
{
	int x, y;

	for (y = sy; y < sy + h; y++)
		for (x = sx; x < sx + w; x++)
			fbcon_prom_putc(con, d, con->vc_video_erase_char, y, x);
}

static void
fbcon_prom_bmove(struct display *d, int sy, int sx, int dy, int dx,
		 int h, int w)
{
	struct fb_info_promfb *fb = promfbinfod(d);
	int linesize = d->next_line;
	unsigned short *src, *dst;
	int x, y;

	if (dx == 0 && sx == 0 && dy == 0 && sy == 1 &&
	    w == linesize && h == fb->maxy) {
		memcpy(fb->data, fb->data + linesize, 2 * linesize * fb->maxy);
		prom_start(fb, d);
		prom_printf("\033[%d;%dH", fb->maxy + 1, fb->maxx + 1);
		prom_putchar(*(fb->data + linesize * fb->maxy + fb->maxx));
		fb->curx = 0;
		fb->cury = fb->maxy;
		prom_stop(fb, d);
	} else if (dy < sy || (dy == sy && dx < sx)) {
		src = fb->data + sy * linesize + sx;
		dst = fb->data + dy * linesize + dx;
		for (y = dy; y < dy + h; y++) {
			for (x = dx; x < dx + w; x++) {
				if (*src != *dst)
					fbcon_prom_putc(d->conp, d, *src, y, x);
				src++;
				dst++;
			}
		}
	} else {
		src = fb->data + (sy + h - 1) * linesize + sx + w - 1;
		dst = fb->data + (dy + h - 1) * linesize + dx + w - 1;
		for (y = dy + h - 1; y >= dy; y--) {
			for (x = dx + w - 1; x >= dx; x--) {
				if (*src != *dst)
					fbcon_prom_putc(d->conp, d, *src, y, x);
				src--;
				dst--;
			}
		}
	}
}

static void
fbcon_prom_revc(struct display *d, int x, int y)
{
	struct fb_info_promfb *fb = promfbinfod(d);
	unsigned short *dst = fb->data + y * d->next_line + x;
	unsigned short val = *dst ^ 0x0800;

	fbcon_prom_putc(d->conp, d, val, y, x);
}

static void
fbcon_prom_cursor(struct display *d, int mode, int x, int y)
{
	struct fb_info_promfb *fb = promfbinfod(d);

	switch (mode) {
		case CM_ERASE:
			break;

		case CM_MOVE:
		case CM_DRAW:
			prom_start(fb, d);
			if (fb->curx != x || fb->cury != y) {
				prom_printf("\033[%d;%dH", y + 1, x + 1);
				fb->curx = x;
				fb->cury = y;
			}
			break;
	}
}

static struct display_switch fbcon_promfb = {
	fbcon_prom_setup,
	fbcon_prom_bmove,
	fbcon_prom_clear,
	fbcon_prom_putc,
	fbcon_prom_putcs,
	fbcon_prom_revc,
	fbcon_prom_cursor
};


struct promfb_size {
	int xres, yres;
	int width, height;
	int x_margin, y_margin;
} promfb_sizes[] __initdata = {
	{ 1024, 768, 80, 34, 32, 10 },
	{ 80 * PROM_FONT_WIDTH, 24 * PROM_FONT_HEIGHT, 80, 24, 0, 0 },
	{ 0 }
};

__initfunc(void promfb_init(void))
{
	extern int con_is_present(void);
	struct fb_info_promfb *fb;
	struct fb_fix_screeninfo *fix;
	struct fb_var_screeninfo *var;
	struct display *disp;
	struct promfb_size *size;
	int i, w, h;

	if (!con_is_present())
		return;

	fb = kmalloc(sizeof(struct fb_info_promfb), GFP_ATOMIC);
	if (!fb) {
		prom_printf("Could not allocate promfb structure\n");
		return;
	}

	memset(fb, 0, sizeof(struct fb_info_promfb));
	fix = &fb->fix;
	var = &fb->var;
	disp = &fb->disp;

	fb->node = prom_inst2pkg(prom_stdout);
	w = prom_getintdefault(fb->node, "width", 80 * PROM_FONT_WIDTH);
	h = prom_getintdefault(fb->node, "height", 24 * PROM_FONT_HEIGHT);

	size = promfb_sizes;
	while (size->xres) {
		if (size->xres == w && size->yres == h)
			break;
		size++;
	}
	if (!size->xres) {
		size->xres = w;
		size->yres = h;
		size->width = w / PROM_FONT_WIDTH;
		size->height = h / PROM_FONT_HEIGHT;
		size->x_margin = (w - size->width * PROM_FONT_WIDTH) >> 1;
		size->y_margin = (h - size->width * PROM_FONT_HEIGHT) >> 1;
	}

	fb->data = kmalloc(2 * size->width * size->height, GFP_ATOMIC);
	if (!fb->data) {
		kfree(fb);
		return;
	}

	fb->maxx = size->width - 1;
	fb->maxy = size->height - 1;

	strcpy(fb->info.modename, "PROM pseudo");
	fb->info.node = -1;
	fb->info.fbops = &promfb_ops;
	fb->info.disp = disp;
	fb->info.fontname[0] = '\0';
	fb->info.changevar = NULL;
	fb->info.switch_con = promfbcon_switch;
	fb->info.updatevar = promfbcon_updatevar;
	fb->info.blank = promfbcon_blank;

	strcpy(fix->id, "PROM pseudo");
	fix->type = FB_TYPE_TEXT;
	fix->type_aux = FB_AUX_TEXT_MDA;
	fix->visual = FB_VISUAL_MONO01;
	fix->xpanstep = 0;
	fix->ypanstep = 16;
	fix->ywrapstep = 0;
	fix->line_length = size->width;
	fix->accel = FB_ACCEL_NONE;

	var->xres = size->width * 8;
	var->yres = size->height * 16;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->bits_per_pixel = 1;
	var->xoffset = 0;
	var->yoffset = 0;
	var->activate = 0;
	var->height = -1;
	var->width = -1;
	var->accel_flags = FB_ACCELF_TEXT;
	var->vmode = FB_VMODE_NONINTERLACED;

	disp->var = *var;
	disp->visual = fix->visual;
	disp->type = fix->type;
	disp->type_aux = fix->type_aux;
	disp->ypanstep = fix->ypanstep;
	disp->ywrapstep = fix->ywrapstep;
	disp->line_length = fix->line_length;
	disp->can_soft_blank = 1;
	disp->dispsw = &fbcon_promfb;

	for (i = 0; i < fb->maxy * size->width + fb->maxx; i++)
		fb->data[i] = ' ';
	prom_printf("\033[H\033[J");

	if (register_framebuffer(&fb->info) < 0) {
		kfree(fb->data);
		kfree(fb);
		return;
	}

	printk("fb%d: %s frame buffer device\n", GET_FB_IDX(fb->info.node),
	       fb->info.modename);
	MOD_INC_USE_COUNT;
}

__initfunc(void promfb_setup(char *options, int *ints)) {}

void
promfb_cleanup(struct fb_info *info)
{
	struct fb_info_promfb *fb = promfbinfo(info);

	unregister_framebuffer(info);
	kfree(fb->data);
	kfree(fb);
}

#ifdef MODULE
int init_module(void)
{
	promfb_init();
	return 0
}

void cleanup_module(void)
{
	promfb_cleanup();
}
#endif /* MODULE */

