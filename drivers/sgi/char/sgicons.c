/*
 * sgicons.c: Setting up and registering console I/O on the SGI.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * This implement a virtual console interface.
 *
 * This should be replaced with Gert's all-singing all-dancing
 * graphics console code in the future
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include "gconsole.h"

/* To make psaux code cleaner */
int aux_device_present = 0xaa;

/* This is the system graphics console (the first adapter found) */
struct console_ops *gconsole = 0;
struct console_ops *real_gconsole = 0;

void
enable_gconsole (void)
{
	if (!gconsole)
		gconsole = real_gconsole;
}

void
disable_gconsole (void)
{
	if (gconsole){
		real_gconsole = gconsole;
		gconsole = 0;
	}
}

void
register_gconsole (struct console_ops *gc)
{
	if (gconsole)
		return;
	gconsole = gc;
}

void
__set_origin (unsigned short offset)
{
	if (gconsole)
		(*gconsole->set_origin)(offset);
}

void
hide_cursor (void)
{

	if (gconsole)
		(*gconsole->hide_cursor)();
}

void
set_cursor (int currcons)
{
	if (gconsole)
		(*gconsole->set_cursor)(currcons);
}

void
get_scrmem (int currcons)
{
	if (gconsole)
		(*gconsole->get_scrmem)(currcons);
}

void
set_scrmem (int currcons, long offset)
{
	if (gconsole)
		(*gconsole->set_scrmem)(currcons, offset);
}

int
set_get_cmap (unsigned char *arg, int set)
{
	if (gconsole)
		return (*gconsole->set_get_cmap)(arg, set);
	return 0;
}

void
blitc (unsigned short charattr, unsigned long addr)
{
	if (gconsole)
		(*gconsole->blitc)(charattr, addr);
}

void
memsetw (void *s, unsigned short c, unsigned int count)
{
	if (gconsole)
		(*gconsole->memsetw)(s, c, count);
}

void
memcpyw (unsigned short *to, unsigned short *from, unsigned int count)
{
	if (gconsole)
		(*gconsole->memcpyw)(to, from, count);
}

int
con_adjust_height (unsigned long fontheight)
{
	return -EINVAL;
}

int
set_get_font (char *arg, int set, int ch512)
{
	int error, i, line;

	if (!arg)
		return -EINVAL;
	error = verify_area (set ? VERIFY_READ : VERIFY_WRITE, (void *) arg,
			     ch512 ? 2* cmapsz : cmapsz);
	if (error)
		return error;

	/* download the current font */
	if (!set) {
		memset (arg, 0, cmapsz);
		for (i = 0; i < 256; i++) {
			for (line = 0; line < CHAR_HEIGHT; line++)
				__put_user (vga_font [i], arg+(i*32+line));
		}
		return 0;
	}
	
        /* set the font */
	for (i = 0; i < 256; i++) {
		for (line = 0; line < CHAR_HEIGHT; line++) {
			__get_user(vga_font [i*CHAR_HEIGHT + line],
			           arg + (i * 32 + line));
		}
	}
	return 0;
}

/*
 * dummy routines for the VESA blanking code, which is VGA only,
 * so we don't have to carry that stuff around for the Sparc...  */
void vesa_blank(void) { }
void vesa_unblank(void) { }
void set_vesa_blanking(const unsigned long arg) { }
void vesa_powerdown(void) { }
void set_palette (void) { }

extern unsigned long video_mem_base, video_screen_size, video_mem_term;

__initfunc(unsigned long con_type_init(unsigned long start_mem, const char **name))
{
	extern int serial_console;

	if (serial_console)
		*name = "NONE";
	else {
		gfx_init (name);
		printk("Video screen size is %08lx at %08lx\n",
		       video_screen_size, start_mem);
		video_mem_base = start_mem;
		start_mem += (video_screen_size * 2);
		video_mem_term = start_mem;
	}
	return start_mem;
}

__initfunc(void con_type_init_finish(void))
{
}
