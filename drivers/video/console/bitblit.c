/*
 *  linux/drivers/video/console/bitblit.c -- BitBlitting Operation
 *
 *  Originally from the 'accel_*' routines in drivers/video/console/fbcon.c
 *
 *      Copyright (C) 2004 Antonino Daplas <adaplas @pol.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <asm/types.h>
#include "fbcon.h"

/*
 * Accelerated handlers.
 */
#define FBCON_ATTRIBUTE_UNDERLINE 1
#define FBCON_ATTRIBUTE_REVERSE   2
#define FBCON_ATTRIBUTE_BOLD      4

static inline int real_y(struct display *p, int ypos)
{
	int rows = p->vrows;

	ypos += p->yscroll;
	return ypos < rows ? ypos : ypos - rows;
}


static inline int get_attribute(struct fb_info *info, u16 c)
{
	int attribute = 0;

	if (fb_get_color_depth(&info->var) == 1) {
		if (attr_underline(c))
			attribute |= FBCON_ATTRIBUTE_UNDERLINE;
		if (attr_reverse(c))
			attribute |= FBCON_ATTRIBUTE_REVERSE;
		if (attr_bold(c))
			attribute |= FBCON_ATTRIBUTE_BOLD;
	}

	return attribute;
}

static inline void update_attr(u8 *dst, u8 *src, int attribute,
			       struct vc_data *vc)
{
	int i, offset = (vc->vc_font.height < 10) ? 1 : 2;
	int width = (vc->vc_font.width + 7) >> 3;
	unsigned int cellsize = vc->vc_font.height * width;
	u8 c;

	offset = cellsize - (offset * width);
	for (i = 0; i < cellsize; i++) {
		c = src[i];
		if (attribute & FBCON_ATTRIBUTE_UNDERLINE && i >= offset)
			c = 0xff;
		if (attribute & FBCON_ATTRIBUTE_BOLD)
			c |= c >> 1;
		if (attribute & FBCON_ATTRIBUTE_REVERSE)
			c = ~c;
		dst[i] = c;
	}
}

static void bit_bmove(struct vc_data *vc, struct fb_info *info, int sy,
		      int sx, int dy, int dx, int height, int width)
{
	struct fb_copyarea area;

	area.sx = sx * vc->vc_font.width;
	area.sy = sy * vc->vc_font.height;
	area.dx = dx * vc->vc_font.width;
	area.dy = dy * vc->vc_font.height;
	area.height = height * vc->vc_font.height;
	area.width = width * vc->vc_font.width;

	info->fbops->fb_copyarea(info, &area);
}

static void bit_clear(struct vc_data *vc, struct fb_info *info, int sy,
		      int sx, int height, int width)
{
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(bgshift, vc);
	region.dx = sx * vc->vc_font.width;
	region.dy = sy * vc->vc_font.height;
	region.width = width * vc->vc_font.width;
	region.height = height * vc->vc_font.height;
	region.rop = ROP_COPY;

	info->fbops->fb_fillrect(info, &region);
}

static void bit_putcs(struct vc_data *vc, struct fb_info *info,
		      const unsigned short *s, int count, int yy, int xx,
		      int fg, int bg)
{
	void (*move_unaligned)(struct fb_info *info, struct fb_pixmap *buf,
			       u8 *dst, u32 d_pitch, u8 *src, u32 idx,
			       u32 height, u32 shift_high, u32 shift_low,
			       u32 mod);
	void (*move_aligned)(struct fb_info *info, struct fb_pixmap *buf,
			     u8 *dst, u32 d_pitch, u8 *src, u32 s_pitch,
			     u32 height);
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	unsigned int width = (vc->vc_font.width + 7) >> 3;
	unsigned int cellsize = vc->vc_font.height * width;
	unsigned int maxcnt = info->pixmap.size/cellsize;
	unsigned int scan_align = info->pixmap.scan_align - 1;
	unsigned int buf_align = info->pixmap.buf_align - 1;
	unsigned int shift_low = 0, mod = vc->vc_font.width % 8;
	unsigned int shift_high = 8, pitch, cnt, size, k;
	unsigned int idx = vc->vc_font.width >> 3;
	unsigned int attribute = get_attribute(info, scr_readw(s));
	struct fb_image image;
	u8 *src, *dst, *buf = NULL;

	if (attribute) {
		buf = kmalloc(cellsize, GFP_KERNEL);
		if (!buf)
			return;
	}

	image.fg_color = fg;
	image.bg_color = bg;

	image.dx = xx * vc->vc_font.width;
	image.dy = yy * vc->vc_font.height;
	image.height = vc->vc_font.height;
	image.depth = 1;

	if (info->pixmap.outbuf && info->pixmap.inbuf) {
		move_aligned = fb_iomove_buf_aligned;
		move_unaligned = fb_iomove_buf_unaligned;
	} else {
		move_aligned = fb_sysmove_buf_aligned;
		move_unaligned = fb_sysmove_buf_unaligned;
	}
	while (count) {
		if (count > maxcnt)
			cnt = k = maxcnt;
		else
			cnt = k = count;

		image.width = vc->vc_font.width * cnt;
		pitch = ((image.width + 7) >> 3) + scan_align;
		pitch &= ~scan_align;
		size = pitch * image.height + buf_align;
		size &= ~buf_align;
		dst = fb_get_buffer_offset(info, &info->pixmap, size);
		image.data = dst;
		if (mod) {
			while (k--) {
				src = vc->vc_font.data + (scr_readw(s++)&
							  charmask)*cellsize;

				if (attribute) {
					update_attr(buf, src, attribute, vc);
					src = buf;
				}

				move_unaligned(info, &info->pixmap, dst, pitch,
					       src, idx, image.height,
					       shift_high, shift_low, mod);
				shift_low += mod;
				dst += (shift_low >= 8) ? width : width - 1;
				shift_low &= 7;
				shift_high = 8 - shift_low;
			}
		} else {
			while (k--) {
				src = vc->vc_font.data + (scr_readw(s++)&
							  charmask)*cellsize;

				if (attribute) {
					update_attr(buf, src, attribute, vc);
					src = buf;
				}

				move_aligned(info, &info->pixmap, dst, pitch,
					     src, idx, image.height);
				dst += width;
			}
		}
		info->fbops->fb_imageblit(info, &image);
		image.dx += cnt * vc->vc_font.width;
		count -= cnt;
	}

	/* buf is always NULL except when in monochrome mode, so in this case
	   it's a gain to check buf against NULL even though kfree() handles
	   NULL pointers just fine */
	if (unlikely(buf))
		kfree(buf);
}

static void bit_clear_margins(struct vc_data *vc, struct fb_info *info,
			      int bottom_only)
{
	int bgshift = (vc->vc_hi_font_mask) ? 13 : 12;
	unsigned int cw = vc->vc_font.width;
	unsigned int ch = vc->vc_font.height;
	unsigned int rw = info->var.xres - (vc->vc_cols*cw);
	unsigned int bh = info->var.yres - (vc->vc_rows*ch);
	unsigned int rs = info->var.xres - rw;
	unsigned int bs = info->var.yres - bh;
	struct fb_fillrect region;

	region.color = attr_bgcol_ec(bgshift, vc);
	region.rop = ROP_COPY;

	if (rw && !bottom_only) {
		region.dx = info->var.xoffset + rs;
		region.dy = 0;
		region.width = rw;
		region.height = info->var.yres_virtual;
		info->fbops->fb_fillrect(info, &region);
	}

	if (bh) {
		region.dx = info->var.xoffset;
		region.dy = info->var.yoffset + bs;
		region.width = rs;
		region.height = bh;
		info->fbops->fb_fillrect(info, &region);
	}
}

static void bit_cursor(struct vc_data *vc, struct fb_info *info,
		       struct display *p, int mode, int softback_lines, int fg, int bg)
{
	struct fb_cursor cursor;
	struct fbcon_ops *ops = (struct fbcon_ops *) info->fbcon_par;
	unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;
	int w = (vc->vc_font.width + 7) >> 3, c;
	int y = real_y(p, vc->vc_y);
	int attribute, use_sw = (vc->vc_cursor_type & 0x10);
	char *src;

	cursor.set = 0;

	if (softback_lines) {
		if (y + softback_lines >= vc->vc_rows) {
			mode = CM_ERASE;
			ops->cursor_flash = 0;
			return;
		} else
			y += softback_lines;
	}

 	c = scr_readw((u16 *) vc->vc_pos);
	attribute = get_attribute(info, c);
	src = vc->vc_font.data + ((c & charmask) * (w * vc->vc_font.height));

	if (ops->cursor_state.image.data != src ||
	    ops->cursor_reset) {
	    ops->cursor_state.image.data = src;
	    cursor.set |= FB_CUR_SETIMAGE;
	}

	if (attribute) {
		u8 *dst;

		dst = kmalloc(w * vc->vc_font.height, GFP_ATOMIC);
		if (!dst)
			return;
		kfree(ops->cursor_data);
		ops->cursor_data = dst;
		update_attr(dst, src, attribute, vc);
		src = dst;
	}

	if (ops->cursor_state.image.fg_color != fg ||
	    ops->cursor_state.image.bg_color != bg ||
	    ops->cursor_reset) {
		ops->cursor_state.image.fg_color = fg;
		ops->cursor_state.image.bg_color = bg;
		cursor.set |= FB_CUR_SETCMAP;
	}

	if ((ops->cursor_state.image.dx != (vc->vc_font.width * vc->vc_x)) ||
	    (ops->cursor_state.image.dy != (vc->vc_font.height * y)) ||
	    ops->cursor_reset) {
		ops->cursor_state.image.dx = vc->vc_font.width * vc->vc_x;
		ops->cursor_state.image.dy = vc->vc_font.height * y;
		cursor.set |= FB_CUR_SETPOS;
	}

	if (ops->cursor_state.image.height != vc->vc_font.height ||
	    ops->cursor_state.image.width != vc->vc_font.width ||
	    ops->cursor_reset) {
		ops->cursor_state.image.height = vc->vc_font.height;
		ops->cursor_state.image.width = vc->vc_font.width;
		cursor.set |= FB_CUR_SETSIZE;
	}

	if (ops->cursor_state.hot.x || ops->cursor_state.hot.y ||
	    ops->cursor_reset) {
		ops->cursor_state.hot.x = cursor.hot.y = 0;
		cursor.set |= FB_CUR_SETHOT;
	}

	if (cursor.set & FB_CUR_SETSIZE ||
	    vc->vc_cursor_type != p->cursor_shape ||
	    ops->cursor_state.mask == NULL ||
	    ops->cursor_reset) {
		char *mask = kmalloc(w*vc->vc_font.height, GFP_ATOMIC);
		int cur_height, size, i = 0;
		u8 msk = 0xff;

		if (!mask)
			return;

		kfree(ops->cursor_state.mask);
		ops->cursor_state.mask = mask;

		p->cursor_shape = vc->vc_cursor_type;
		cursor.set |= FB_CUR_SETSHAPE;

		switch (p->cursor_shape & CUR_HWMASK) {
		case CUR_NONE:
			cur_height = 0;
			break;
		case CUR_UNDERLINE:
			cur_height = (vc->vc_font.height < 10) ? 1 : 2;
			break;
		case CUR_LOWER_THIRD:
			cur_height = vc->vc_font.height/3;
			break;
		case CUR_LOWER_HALF:
			cur_height = vc->vc_font.height >> 1;
			break;
		case CUR_TWO_THIRDS:
			cur_height = (vc->vc_font.height << 1)/3;
			break;
		case CUR_BLOCK:
		default:
			cur_height = vc->vc_font.height;
			break;
		}
		size = (vc->vc_font.height - cur_height) * w;
		while (size--)
			mask[i++] = ~msk;
		size = cur_height * w;
		while (size--)
			mask[i++] = msk;
	}

	switch (mode) {
	case CM_ERASE:
		ops->cursor_state.enable = 0;
		break;
	case CM_DRAW:
	case CM_MOVE:
	default:
		ops->cursor_state.enable = (use_sw) ? 0 : 1;
		break;
	}

	cursor.image.data = src;
	cursor.image.fg_color = ops->cursor_state.image.fg_color;
	cursor.image.bg_color = ops->cursor_state.image.bg_color;
	cursor.image.dx = ops->cursor_state.image.dx;
	cursor.image.dy = ops->cursor_state.image.dy;
	cursor.image.height = ops->cursor_state.image.height;
	cursor.image.width = ops->cursor_state.image.width;
	cursor.hot.x = ops->cursor_state.hot.x;
	cursor.hot.y = ops->cursor_state.hot.y;
	cursor.mask = ops->cursor_state.mask;
	cursor.enable = ops->cursor_state.enable;
	cursor.image.depth = 1;
	cursor.rop = ROP_XOR;

	info->fbops->fb_cursor(info, &cursor);

	ops->cursor_reset = 0;
}

void fbcon_set_bitops(struct fbcon_ops *ops)
{
	ops->bmove = bit_bmove;
	ops->clear = bit_clear;
	ops->putcs = bit_putcs;
	ops->clear_margins = bit_clear_margins;
	ops->cursor = bit_cursor;
}

EXPORT_SYMBOL(fbcon_set_bitops);

MODULE_AUTHOR("Antonino Daplas <adaplas@pol.net>");
MODULE_DESCRIPTION("Bit Blitting Operation");
MODULE_LICENSE("GPL");

