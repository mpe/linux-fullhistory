/* $Id: suncons.c,v 1.80 1998/04/13 07:27:01 davem Exp $
 * suncons.c: Sparc platform console generic layer.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/vt.h>
#include <linux/selection.h>
#include <linux/proc_fs.h>
#include <linux/malloc.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sbus.h>
#include <asm/fbio.h>

#include "fb.h"

#include <asm/linux_logo.h>

fbinfo_t *fbinfo;
int fbinfos;
unsigned int linux_logo_colors __initdata = LINUX_LOGO_COLORS;
char logo_banner[] __initdata = linux_logo_banner;
#ifdef CONFIG_PCI
static int cons_type __initdata = 0;
#endif

extern struct console vt_console_driver;

/* Infrastructure. */

static void nop_memsetw(void *s, unsigned short c, unsigned int count)
{
}

static void nop_memcpyw(unsigned short *to, unsigned short *from, unsigned int count)
{
}

static void nop_scr_writew(unsigned short val, unsigned short *addr)
{
}

static unsigned short nop_scr_readw(unsigned short *addr)
{
	return 0;
}

static void nop_get_scrmem(int a)
{
}

static void nop_set_scrmem(int a, long b)
{
}

static void nop_set_origin(unsigned short offset)
{
}

static void nop_hide_cursor(void)
{
}

static void nop_set_cursor(int c)
{
}

static int nop_set_get_font(char *a, int b, int c)
{
	return 0;
}

static int nop_con_adjust_height(unsigned long arg)
{
	return -EINVAL;
}

static int nop_set_get_cmap(unsigned char *arg, int a)
{
	return 0;
}

static void nop_set_palette(void)
{
}

static void nop_set_other_palette(int a)
{
}

static void nop_console_restore_palette(void)
{
}

static void nop_con_type_init(const char **display_desc)
{
}

static void nop_con_type_init_finish(void)
{
}

static void nop_vesa_blank(void)
{
}

static void nop_vesa_unblank(void)
{
}

static void nop_set_vesa_blanking(const unsigned long arg)
{
}

static void nop_vesa_powerdown(void)
{
}

static void nop_clear_screen(void)
{
}

static void nop_render_screen(void)
{
}

static void nop_clear_margin(void)
{
}

struct suncons_operations suncons_ops = {
	nop_memsetw,
	nop_memcpyw,
	nop_scr_writew,
	nop_scr_readw,
	nop_get_scrmem,
	nop_set_scrmem,
	nop_set_origin,
	nop_hide_cursor,
	nop_set_cursor,
	nop_set_get_font,
	nop_con_adjust_height,
	nop_set_get_cmap,
	nop_set_palette,
	nop_set_other_palette,
	nop_console_restore_palette,
	nop_con_type_init,
	nop_con_type_init_finish,
	nop_vesa_blank,
	nop_vesa_unblank,
	nop_set_vesa_blanking,
	nop_vesa_powerdown,
	nop_clear_screen,
	nop_render_screen,
	nop_clear_margin
};

/* Entry points. */

void get_scrmem(int a)
{
	suncons_ops.get_scrmem(a);
}

void set_scrmem(int a, long b)
{
	suncons_ops.set_scrmem(a, b);
}

void __set_origin(unsigned short offset)
{
	suncons_ops.set_origin(offset);
}

void hide_cursor(void)
{
	suncons_ops.hide_cursor();
}

void set_cursor(int currcons)
{
	suncons_ops.set_cursor(currcons);
}

int set_get_font(char *arg, int set, int ch512)
{
	return suncons_ops.set_get_font(arg, set, ch512);
}

int con_adjust_height(unsigned long fontheight)
{
	return suncons_ops.con_adjust_height(fontheight);
}

int set_get_cmap(unsigned char *arg, int set)
{
	return suncons_ops.set_get_cmap(arg, set);
}

void set_palette(void)
{
	suncons_ops.set_palette();
}

void set_other_palette(int n)
{
	suncons_ops.set_other_palette(n);
}

void console_restore_palette(void)
{
	suncons_ops.console_restore_palette();
}

void con_type_init(const char **disp_desc)
{
	return suncons_ops.con_type_init(disp_desc);
}

void con_type_init_finish(void)
{
	suncons_ops.con_type_init_finish();
}

void vesa_blank(void)
{
	suncons_ops.vesa_blank();
}

void vesa_unblank(void)
{
	suncons_ops.vesa_unblank();
}

void set_vesa_blanking(const unsigned long arg)
{
	suncons_ops.set_vesa_blanking(arg);
}

void vesa_powerdown(void)
{
	suncons_ops.vesa_powerdown();
}

void render_screen(void)
{
	suncons_ops.render_screen();
}

/*
 * We permutate the colors, so we match the PROM's idea of
 * black and white.
 */
unsigned char reverse_color_table[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0
};

unsigned char sparc_color_table[] = {
	15, 0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11
};

/* Probing engine. */

char *console_fb_path = NULL;
void (*fb_restore_palette)(fbinfo_t *fbinfo) = NULL;

unsigned long
get_phys (unsigned long addr)
{
	return __get_phys(addr);
}

extern int sbus_console_probe(void);
extern int serial_console;

__initfunc(static void finish_console_init(void))
{
	static int confinish_has_run = 0;
	int i, j;

	if(confinish_has_run != 0) {
		printk("finish_console_init: Someone tries to run me twice.\n");
		return;
	}
	for(i = FRAME_BUFFERS; i > 1; i--)
		if(fbinfo[i - 1].type.fb_type != FBTYPE_NOTYPE)
			break;
	fbinfos = i;

	for(j = 0; j < i; j++)
		if (fbinfo[j].postsetup)
			(*fbinfo[j].postsetup)(fbinfo+j);

	suncons_ops.clear_screen();

	for(j = 1; j < i; j++)
		if(fbinfo[j].type.fb_type != FBTYPE_NOTYPE) {
			fbinfo[j].clear_fb(j);
			fbinfo[j].set_other_palette(j);
		}
#if defined(CONFIG_PROC_FS) && \
    ( defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE) )
	for (j = 0; j < i; j++)
		if (fbinfo[j].type.fb_type != FBTYPE_NOTYPE)
			proc_openprom_regdev (&fbinfo[j].proc_entry);
#endif

	confinish_has_run = 1;
}

__initfunc(int con_is_present(void))
{
	return serial_console ? 0 : 1;
}

#ifdef CONFIG_PCI
extern int pci_console_probe(void);
extern void pci_console_inithook(void);

__initfunc(void pci_console_init(void))
{
	/* Nothing to do in this case. */
	if (!con_is_present())
		return;
		
	if (!cons_type) {
		/* Some console was already found on SBUS or UPA */
		return;
	}

	if(pci_console_probe()) {
		prom_printf("Could not probe PCI console, bailing out...\n");
		prom_halt();
	}

	finish_console_init();

	con_type_init_finish();
}

#endif /* CONFIG_PCI */

__initfunc(void sun_console_init(void))
{
	int i;

	/* Nothing to do in this case. */
	if (!con_is_present())
		return;

	fbinfo = kmalloc(sizeof(fbinfo_t) * FRAME_BUFFERS, GFP_ATOMIC);
	memset(fbinfo, 0, FRAME_BUFFERS * sizeof(fbinfo_t));
	fbinfos = 0;

	for (i = 0; i < FRAME_BUFFERS; i++)
		fbinfo [i].type.fb_type = FBTYPE_NOTYPE;

	if(sbus_console_probe()) {
#ifdef CONFIG_PCI
		cons_type = 1;
		return pci_console_inithook();
#else
		/* XXX We need to write PROM console fallback driver... */
		prom_printf("Could not probe SBUS console, bailing out...\n");
		prom_halt();
#endif
	}
	return finish_console_init();
}
