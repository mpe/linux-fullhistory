/* $Id: pcicons.c,v 1.13 1998/04/01 06:55:11 ecd Exp $
 * pcicons.c: PCI specific probing and console operations layer.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>

#ifdef CONFIG_PCI

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/major.h>
#include <linux/timer.h>
#include <linux/version.h>

#include <asm/uaccess.h>
#include <asm/oplib.h>
#include <asm/sbus.h>
#include <asm/pbm.h>
#include <asm/fbio.h>
#include <asm/smp.h>

#include <linux/kd.h>
#include <linux/console_struct.h>
#include <linux/selection.h>
#include <linux/vt_kern.h>

#include "pcicons.h"
#include "fb.h"

static int x_margin = 0;
static int y_margin = 0;
static int skip_bytes;

static __u64 *cursor_screen_pos;
static __u64 cursor_bits[2];
static int cursor_pos = -1;

extern int serial_console;
extern struct console vt_console_driver;

static void pci_install_consops(void);
static int (*fbuf_offset)(int);

static int color_fbuf_offset_1152_128(int cindex)
{
	register int i = (cindex >> 7);
	/* (1152 * CHAR_HEIGHT) == 100.1000.0000.0000 */
	return skip_bytes + (i << 14) + (i << 11) + ((cindex & 127) << 3);
}

static int color_fbuf_offset_1024_128(int cindex)
{
	register int i = (cindex >> 7);
	/* (1024 * CHAR_HEIGHT) == 100.0000.0000.0000 */
	return skip_bytes + (i << 14) + ((cindex & 127) << 3);
}

static __u32 expand_bits_8[16] = {
	0x00000000,
	0x000000ff,
	0x0000ff00,
	0x0000ffff,
	0x00ff0000,
	0x00ff00ff,
	0x00ffff00,
	0x00ffffff,
	0xff000000,
	0xff0000ff,
	0xff00ff00,
	0xff00ffff,
	0xffff0000,
	0xffff00ff,
	0xffffff00,
	0xffffffff
};

static void pci_blitc(unsigned int charattr, unsigned long addr)
{
	static int cached_attr = -1;
	static __u32 fg, bg;
	fbinfo_t *fb = &fbinfo[0];
	__u32 *screen;
	unsigned char attrib;
	unsigned char *fp;
	unsigned long flags;
	int i, idx;

	if ((charattr & 0xff00) != cached_attr) {
		cached_attr = charattr;
		attrib = CHARATTR_TO_SUNCOLOR(charattr);
		fg = attrib & 0x0f;
		fg |= fg << 8;
		fg |= fg << 16;
		bg = attrib >> 4;
		bg |= bg << 8;
		bg |= bg << 16;
		fg ^= bg;
	}

	idx = (addr - video_mem_base) >> 1;
	save_flags(flags); cli();
	if (cursor_pos == idx)
		cursor_pos = -1;
	restore_flags(flags);

	screen = (__u32 *)(fb->base + fbuf_offset(idx));
	fp = &vga_font[(charattr & 0xff) << 4];

	for(i = 0; i < 16; i++) {
		int bits = *fp++;

		screen[0] = (expand_bits_8[bits >> 4] & fg) ^ bg;
		screen[1] = (expand_bits_8[bits & 0x0f] & fg) ^ bg;
		screen = (__u32 *) (((unsigned long)screen) + fb->linebytes);
	}
}


static void pci_memsetw(void *s, unsigned short c, unsigned int count)
{
	unsigned short *p = (unsigned short *)s;

	count >>= 1;
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS) {
		while (count) {
			--count;
			*p++ = c;
		}
		return;
	}
	if ((unsigned long)(p + count) > video_mem_base &&
	    (unsigned long)p < video_mem_term) {
		for (; p < (unsigned short *)video_mem_base && count; count--)
			*p++ = c;
		for (; p < (unsigned short *)video_mem_term && count; count--) {
			if (*p != c) {
				*p = c;
				pci_blitc(c, (unsigned long)p);
			}
			++p;
		}
	}
	for (; count; count--)
		*p++ = c;
}

static void pci_memcpyw(unsigned short *dst, unsigned short *src,
			unsigned int count)
{
	unsigned short c;

	count >>= 1;
	if ((unsigned long)(dst + count) > video_mem_base &&
	    (unsigned long)dst < video_mem_term) {
		for (; dst < (unsigned short *)video_mem_base && count; count--)
			*dst++ = *src++;
		for (; dst < (unsigned short *)video_mem_term && count;
		     count--) {
			c = *src++;
			if (*dst != c) {
				*dst = c;
				pci_blitc(c, (unsigned long)dst);
			}
			++dst;
		}
	}
	for (; count; count--)
		*dst++ = *src++;
}

static void pci_scr_writew(unsigned short val, unsigned short *addr)
{
	if (*addr != val) {
		*addr = val;
		if ((unsigned long)addr < video_mem_term &&
                    (unsigned long)addr >= video_mem_base &&
                    vt_cons[fg_console]->vc_mode == KD_TEXT)
                        pci_blitc(val, (unsigned long) addr);
	}
}

static unsigned short pci_scr_readw(unsigned short *addr)
{
	return *addr;
}

static void pci_get_scrmem(int currcons)
{
	struct vc_data *vcd = vc_cons[currcons].d;
	
	memcpyw((unsigned short *)vc_scrbuf[currcons],
		(unsigned short *)vcd->vc_origin, video_screen_size);
	vcd->vc_origin = vcd->vc_video_mem_start = 
		(unsigned long)vc_scrbuf[currcons];
	vcd->vc_scr_end = vcd->vc_video_mem_end = 
		vcd->vc_video_mem_start + video_screen_size;
	vcd->vc_pos = 
		vcd->vc_origin + vcd->vc_y * video_size_row + (vcd->vc_x << 1);
}

static void pci_set_scrmem(int currcons, long offset)
{
	struct vc_data *vcd = vc_cons[currcons].d;
	
	if (video_mem_term - video_mem_base < offset + video_screen_size)
		offset = 0;
	memcpyw((unsigned short *)(video_mem_base + offset),
		(unsigned short *) vcd->vc_origin, video_screen_size);
	vcd->vc_video_mem_start = video_mem_base;
	vcd->vc_video_mem_end = video_mem_term;
	vcd->vc_origin = video_mem_base + offset;
	vcd->vc_scr_end = vcd->vc_origin + video_screen_size;
	vcd->vc_pos = 
		vcd->vc_origin + vcd->vc_y * video_size_row + (vcd->vc_x << 1);
}

static void pci_invert_cursor(int cpos)
{
	fbinfo_t *fb = &fbinfo[0];
	__u64 *screen;

	if (cpos == -1) {
		screen = cursor_screen_pos;
		*screen = cursor_bits[0];
		screen = (__u64 *)((unsigned long)screen + fb->linebytes);
		*screen = cursor_bits[1];
		return;
	}

	screen = (__u64 *)(fb->base + fbuf_offset(cpos) + 14 * fb->linebytes);
	cursor_screen_pos = screen;

	cursor_bits[0] = *screen;
	*screen = 0x0000000000000000;
	screen = (__u64 *)((unsigned long)screen + fb->linebytes);
	cursor_bits[1] = *screen;
	*screen = 0x0000000000000000;
}

static void pci_hide_cursor(void)
{
	unsigned long flags;

	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return;

	save_flags(flags); cli();
	if (cursor_pos != -1)
		pci_invert_cursor(-1);
	restore_flags(flags);
}

static void pci_set_cursor(int currcons)
{
	unsigned long flags;
	int old_cursor;

	if (currcons != fg_console || console_blanked || 
	    vt_cons[currcons]->vc_mode == KD_GRAPHICS)
		return;

	save_flags(flags); cli();
	if (!vc_cons[currcons].d->vc_deccm) {
		pci_hide_cursor();
	} else {
		old_cursor = cursor_pos;
		cursor_pos = 
			(vc_cons[currcons].d->vc_pos - video_mem_base) >> 1;
		if (old_cursor != -1)
			pci_invert_cursor(-1);
		pci_invert_cursor(cursor_pos);
	}
	restore_flags(flags);
}

static void pci_set_palette(void)
{
	fbinfo_t *fb = &fbinfo[0];

	if (console_blanked || vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return;

	if (fb->loadcmap) {
		int i, j;

		for (i = 0; i < 16; i++) {
			j = sparc_color_table[i];
			fb->color_map CM(i, 0) = default_red[j];
			fb->color_map CM(i, 1) = default_grn[j];
			fb->color_map CM(i, 2) = default_blu[j];
		}
		fb->loadcmap(fb, 0, 16);
	}
}

static void pci_set_other_palette(int n)
{
	fbinfo_t *fb = &fbinfo[n];

	if (!n) {
		pci_set_palette();
		return;
	}

	if (fb->loadcmap) {
		fb->color_map CM(0, 0) = 0;
		fb->color_map CM(0, 1) = 0;
		fb->color_map CM(0, 2) = 0;
		fb->loadcmap(fb, 0, 1);
	}
}

static void pci_restore_palette(void)
{
	if (fb_restore_palette)
		fb_restore_palette(&fbinfo[0]);
}

static int pci_set_get_font(char *arg, int set, int ch512)
{
	int i, line;

	if (!arg)
		return -EINVAL;

	if (!set) {
		if (clear_user(arg, sizeof(vga_font)))
			return -EFAULT;
		for (i = 0; i < 256; i++) {
			for (line = 0; line < CHAR_HEIGHT; line++) {
				unsigned char value;

				value = vga_font[i * CHAR_HEIGHT + line];
				__put_user_ret(value, (arg + (i * 32 + line)),
					       -EFAULT);
			}
		}
		return 0;
	}

	if (verify_area(VERIFY_READ, arg, 256 * CHAR_HEIGHT))
		return -EFAULT;
	for (i = 0; i < 256; i++) {
		for (line = 0; line < CHAR_HEIGHT; line++) {
			unsigned char value;
			__get_user_ret(value, (arg + (i + 32 + line)), -EFAULT);
			vga_font[i * CHAR_HEIGHT + line] = value;
		}
	}
	return 0;
}

static int pci_con_adjust_height(unsigned long fontheight)
{
	return -EINVAL;
}

static int pci_set_get_cmap(unsigned char *arg, int set)
{
	int i;

	if (set)
		i = VERIFY_READ;
	else
		i = VERIFY_WRITE;

	if (verify_area(i, arg, (16 * 3 * sizeof(unsigned char))))
		return -EFAULT;

	for (i = 0; i < 16; i++) {
		if (set) {
			__get_user_ret(default_red[i], (arg + 0), -EFAULT);
			__get_user_ret(default_grn[i], (arg + 1), -EFAULT);
			__get_user_ret(default_blu[i], (arg + 2), -EFAULT);
		} else {
			__put_user_ret(default_red[i], (arg + 0), -EFAULT);
			__put_user_ret(default_grn[i], (arg + 1), -EFAULT);
			__put_user_ret(default_blu[i], (arg + 2), -EFAULT);
		}
		arg += 3;
	}

	if (set) {
		for (i = 0; i < MAX_NR_CONSOLES; i++) {
			if (vc_cons_allocated(i)) {
				int j, k;

				for (j = k = 0; j < 16; j++) {
					vc_cons[i].d->vc_palette[k++] =
							default_red[i];
					vc_cons[i].d->vc_palette[k++] =
							default_grn[i];
					vc_cons[i].d->vc_palette[k++] =
							default_blu[i];
				}
			}
		}
		pci_set_palette();
	}
	return -EINVAL;
}

static void pci_clear_screen(void)
{
	fbinfo_t *fb = &fbinfo[0];

	if (fb->base)
		memset((void *)fb->base,
		       (fb->type.fb_depth == 1) ?
		       ~(0) : reverse_color_table[0],
		       (fb->type.fb_depth * fb->type.fb_height
					 * fb->type.fb_width) / 8);
	memset((char *)video_mem_base, 0, (video_mem_term - video_mem_base));
}

static void pci_clear_fb(int n)
{
	fbinfo_t *fb = &fbinfo[n];

#if 0
	if (!n) {
		pci_clear_screen();
	} else
#endif
	if (fb->base) {
		memset((void *)fb->base,
		       (fb->type.fb_depth == 1) ?
		       ~(0) : reverse_color_table[0],
		       (fb->type.fb_depth * fb->type.fb_height
					 * fb->type.fb_width) / 8);
	}
}

static void pci_render_screen(void)
{
	int count;
	unsigned short *p;

	count = video_num_columns * video_num_lines;
	p = (unsigned short *)video_mem_base;

	for (; count--; p++)
		pci_blitc(*p, (unsigned long)p);
}

static void pci_clear_margin(void)
{
	fbinfo_t *fb = &fbinfo[0];
	unsigned long p;
	int h, he;

	memset((void *)fb->base,
	       (fb->type.fb_depth == 1) ? ~(0) : reverse_color_table[0],
	       skip_bytes - (x_margin << 1));
	memset((void *)(fb->base + fb->linebytes * fb->type.fb_height
	       - skip_bytes + (x_margin << 1)),
	       (fb->type.fb_depth == 1) ? ~(0) : reverse_color_table[0],
	       skip_bytes - (x_margin << 1));
	he = fb->type.fb_height - 2 * y_margin;
	if (fb->type.fb_depth == 1) {
		for (p = fb->base + skip_bytes - (x_margin << 1), h = 0;
		     h < he; p += fb->linebytes, h++)
			memset((void *)p, ~(0), (x_margin << 1));
	} else {
		for (p = fb->base + skip_bytes - (x_margin << 1), h = 0;
		     h < he; p += fb->linebytes, h++)
			memset((void *)p, reverse_color_table[0],
			       (x_margin << 1));
	}

	if (fb->switch_from_graph)
		fb->switch_from_graph();
}

static unsigned long
pci_postsetup(fbinfo_t *fb, unsigned long memory_start)
{
	fb->color_map = (char *)memory_start;
	pci_set_palette();
	return memory_start + fb->type.fb_cmsize * 3;
}

__initfunc(static unsigned long
pci_con_type_init(unsigned long kmem_start, const char **display_desc))
{
	can_do_color = 1;

	video_type = VIDEO_TYPE_SUNPCI;
	*display_desc = "SUNPCI";

	if(!serial_console) {
		/* If we fall back to PROM then our output
		 * have to remain readable.
		 */
		prom_putchar('\033');
		prom_putchar('[');
		prom_putchar('H');

		/*
		 * Fake the screen memory with some CPU memory
		 */
		video_mem_base = kmem_start;
		kmem_start += video_screen_size;
		video_mem_term = kmem_start;
	}

	return kmem_start;
}

__initfunc(static void pci_con_type_init_finish(void))
{
	fbinfo_t *fb = &fbinfo[0];
	unsigned char *p = (unsigned char *)fb->base + skip_bytes;
	char q[2] = { 0, 5 };
	unsigned short *ush;
	int currcons = 0;
	int cpu, ncpus;
	int i;

	if (serial_console)
		return;

	ncpus = linux_num_cpus;
	if (ncpus > 4)
		ncpus = 4;

#if 0
	if (fb->draw_penguin)
		fb->draw_penguin(x_margin, y_margin, ncpus);
	else
#endif
	if (fb->type.fb_depth == 8 && fb->loadcmap) {
		for (i = 0; i < linux_logo_colors; i++) {
			fb->color_map CM(i + 32, 0) = linux_logo_red[i];
			fb->color_map CM(i + 32, 1) = linux_logo_green[i];
			fb->color_map CM(i + 32, 2) = linux_logo_blue[i];
		}
		fb->loadcmap(fb, 32, linux_logo_colors);
		for (i = 0; i < 80; i++, p += fb->linebytes) {
			for (cpu = 0; cpu < ncpus; cpu++)
				memcpy(p + (cpu * 88), linux_logo + 80 * i, 80);
		}
	} else if (fb->type.fb_depth == 1) {
		for (i = 0; i < 80; i++, p += fb->linebytes) {
			for (cpu = 0; cpu < ncpus; cpu++)
				memcpy(p + (cpu * 11),
				       linux_logo_bw + 10 * i, 10);
		}
	}
	putconsxy(0, q);

	ush = (unsigned short *)video_mem_base + video_num_columns * 2 + 20
				+ 10 * (ncpus - 1);

	for (p = logo_banner; *p; p++, ush++) {
		*ush = (vc_cons[currcons].d->vc_attr << 8) + *p;
		pci_blitc(*ush, (unsigned long)ush);
	}

	for (i = 0; i < 5; i++) {
		ush = (unsigned short *)video_mem_base + i * video_num_columns;
		memset(ush, 0xff, 20);
	}

	register_console(&vt_console_driver);
}

unsigned long pcivga_iobase = 0;
unsigned long pcivga_membase = 0;

static struct {
	int depth;
	int resx, resy;
	int x_margin, y_margin;
} scr_def[] = {
	{ 8, 1152, 900, 64, 18 },
	{ 8, 1024, 768, 0, 0 },
	{ 0 }
};

extern int mach64_init(fbinfo_t *fb);

__initfunc(int pci_console_probe(void))
{
	fbinfo_t *fb = &fbinfo[0];
	char *p;
	int i;

	if (1
#if 1
	    && mach64_init(fb)
#endif
	    && 1) {
		return -ENODEV;
	}
	fbinfos++;

	fb->clear_fb = pci_clear_fb;
	fb->set_other_palette = pci_set_other_palette;
	fb->postsetup = pci_postsetup;
	fb->blanked = 0;

	fb->type.fb_height = prom_getintdefault(fb->prom_node, "height", 900);
	fb->type.fb_width = prom_getintdefault(fb->prom_node, "width", 1152);
	fb->type.fb_depth = prom_getintdefault(fb->prom_node, "depth", 8);
	fb->linebytes = prom_getintdefault(fb->prom_node, "linebytes", 1152);
	fb->type.fb_size = PAGE_ALIGN(fb->linebytes * fb->type.fb_height);

	fb->proc_entry.rdev = MKDEV(GRAPHDEV_MAJOR, 0);
	fb->proc_entry.mode = S_IFCHR | S_IRUSR | S_IWUSR;
	prom_getname(fb->prom_node, fb->proc_entry.name, 32 - 3);
	p = strchr(fb->proc_entry.name, 0);
	sprintf(p, ":%d", 0);

	for (i = 0; scr_def[i].depth; i++) {
		if ((scr_def[i].resx != fb->type.fb_width) ||
		    (scr_def[i].resy != fb->type.fb_height) ||
		    (scr_def[i].depth != fb->type.fb_depth))
			continue;
		x_margin = scr_def[i].x_margin;
		y_margin = scr_def[i].y_margin;
		skip_bytes = y_margin * fb->linebytes + x_margin;
		switch (fb->type.fb_width) {
			case 1152:
				fbuf_offset = color_fbuf_offset_1152_128;
				break;
			case 1024:
				fbuf_offset = color_fbuf_offset_1024_128;
				break;
			default:
				prom_printf("can't handle console width %d\n",
					    fb->type.fb_width);
				prom_halt();
		}
	}

	pci_install_consops();
	return fb_init();
}

__initfunc(void pci_console_inithook(void))
{
	extern char *console_fb_path;
	char prop[16];
	int node = 0;
	int width, height, depth, linebytes;
	int x_margin, y_margin;
	int i, len;

	if (console_fb_path) {
		char *p;
		for (p = console_fb_path; *p && *p != ' '; p++) ;
		*p = 0;
		node = prom_pathtoinode(console_fb_path);
	}
	if (!node) {
		node = prom_inst2pkg(prom_stdout);
		if (!node) {
			prom_printf("can't find output-device node\n");
			prom_halt();
		}
		len = prom_getproperty(node, "device_type", prop, sizeof(prop));
		if (len < 0) {
			prom_printf("output-device doesn't have"
				    " device_type property\n");
			prom_halt();
		}
		if (len != sizeof("display") ||
		    strncmp("display", prop, sizeof("display"))) {
			prom_printf("output-device is %s"
				    " not \"display\"\n", prop);
			prom_halt();
		}
	}

	depth = prom_getintdefault(node, "depth", 8);
	width = prom_getintdefault(node, "width", 1152);
	height = prom_getintdefault(node, "height", 900);
	linebytes = prom_getintdefault(node, "linebytes", 1152);

	for (i = 0; scr_def[i].depth; i++) {
		if ((scr_def[i].resx != width) ||
		    (scr_def[i].resy != height) ||
		    (scr_def[i].depth != depth))
			continue;
		x_margin = scr_def[i].x_margin;
		y_margin = scr_def[i].y_margin;

		ORIG_VIDEO_COLS = width / 8 - 2 * x_margin / depth;
		ORIG_VIDEO_LINES = (height - 2 * y_margin) / 16;
	}

	suncons_ops.con_type_init = pci_con_type_init;
}

__initfunc(static void pci_install_consops(void))
{
	suncons_ops.memsetw = pci_memsetw;
	suncons_ops.memcpyw = pci_memcpyw;
	suncons_ops.scr_writew = pci_scr_writew;
	suncons_ops.scr_readw = pci_scr_readw;

	suncons_ops.get_scrmem = pci_get_scrmem;
	suncons_ops.set_scrmem = pci_set_scrmem;

	suncons_ops.hide_cursor = pci_hide_cursor;
	suncons_ops.set_cursor = pci_set_cursor;
	suncons_ops.set_get_font = pci_set_get_font;
	suncons_ops.con_adjust_height = pci_con_adjust_height;
	suncons_ops.set_get_cmap = pci_set_get_cmap;
	suncons_ops.set_palette = pci_set_palette;
	suncons_ops.set_other_palette = pci_set_other_palette;
	suncons_ops.console_restore_palette = pci_restore_palette;

	suncons_ops.con_type_init = pci_con_type_init;
	suncons_ops.con_type_init_finish = pci_con_type_init_finish;

	suncons_ops.clear_screen = pci_clear_screen;
	suncons_ops.render_screen = pci_render_screen;
	suncons_ops.clear_margin = pci_clear_margin;
}

#endif /* CONFIG_PCI */
