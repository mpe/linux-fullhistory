/*
 *  linux/include/linux/console_compat.h -- Abstract console wrapper
 *
 *	Copyright (C) 1998 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _LINUX_CONSOLE_COMPAT_H_
#define _LINUX_CONSOLE_COMPAT_H_

#include <linux/config.h>

#undef video_num_columns
#undef video_num_lines
#undef video_size_row
#undef video_type
#undef video_mem_base
#undef video_mem_term
#undef video_screen_size
#undef can_do_color
#undef scr_writew
#undef scr_readw
#undef memsetw
#undef memcpyw
#undef set_cursor
#undef hide_cursor
#undef set_get_cmap
#undef set_palette
#undef set_get_font
#undef set_vesa_blanking
#undef vesa_blank
#undef vesa_powerdown
#undef con_adjust_height
#undef con_type_init
#undef con_type_init_finish

#define video_num_columns	compat_video_num_columns
#define video_num_lines		compat_video_num_lines
#define video_size_row		compat_video_size_row
#define video_type		compat_video_type
#define video_mem_base		compat_video_mem_base
#define video_mem_term		compat_video_mem_term
#define video_screen_size	compat_video_screen_size
#define can_do_color		compat_can_do_color
#define scr_writew		compat_scr_writew
#define scr_readw		compat_scr_readw
#define memsetw			compat_memsetw
#define memcpyw			compat_memcpyw
#define set_cursor		compat_set_cursor
#define hide_cursor		compat_hide_cursor
#define set_get_cmap		compat_set_get_cmap
#define set_palette		compat_set_palette
#define set_get_font		compat_set_get_font
#define set_vesa_blanking	compat_set_vesa_blanking
#define vesa_blank		compat_vesa_blank
#define vesa_powerdown		compat_vesa_powerdown
#define con_adjust_height	compat_con_adjust_height
#define con_type_init		compat_con_type_init
#define con_type_init_finish	compat_con_type_init_finish

extern unsigned long compat_video_num_columns; 
extern unsigned long compat_video_num_lines;
extern unsigned long compat_video_size_row;    
extern unsigned char compat_video_type;
extern unsigned long compat_video_mem_base;
extern unsigned long compat_video_mem_term;
extern unsigned long compat_video_screen_size;
extern int compat_can_do_color;
extern void compat_set_cursor(int currcons);
extern void compat_hide_cursor(void);
extern int compat_set_get_cmap(unsigned char *arg, int set);
extern void compat_set_palette(void);
extern int compat_set_get_font(unsigned char * arg, int set, int ch512);
extern void compat_set_vesa_blanking(unsigned long arg);
extern void compat_vesa_blank(void);
extern void compat_vesa_powerdown(void);
extern int compat_con_adjust_height(unsigned long fontheight);
extern void compat_con_type_init(const char **);
extern void compat_con_type_init_finish(void);

#if defined(CONFIG_SUN_CONSOLE)
	void (*memsetw)(void *, unsigned short, unsigned int);
	void (*memcpyw)(unsigned short *, unsigned short *, unsigned int);
	void (*scr_writew)(unsigned short, unsigned short *);
	unsigned short (*scr_readw)(unsigned short *);
	void (*get_scrmem)(int);
	void (*set_scrmem)(int, long);
	void (*set_origin)(unsigned short);
	void (*hide_cursor)(void);
	void (*set_cursor)(int);
	int (*set_get_font)(char *, int, int);
	int (*con_adjust_height)(unsigned long);
	int (*set_get_cmap)(unsigned char *, int);
	void (*set_palette)(void);
	void (*set_other_palette)(int);
	void (*console_restore_palette)(void);
	void (*con_type_init)(const char **);
	void (*con_type_init_finish)(void);

	/* VESA powerdown methods */
	void (*vesa_blank)(void);
	void (*vesa_unblank)(void);
	void (*set_vesa_blanking)(const unsigned long);
	void (*vesa_powerdown)(void);

	void (*clear_screen)(void);
	void (*render_screen)(void);
	void (*clear_margin)(void);
};

#define compat_memsetw(s,c,count)	suncons_ops.memsetw((s),(c),(count))
#define compat_memcpyw(to,from,count)	suncons_ops.memcpyw((to),(from),(count))
#define compat_scr_writew(val,addr)	suncons_ops.scr_writew((val),(addr))
#define compat_scr_readw(addr)		suncons_ops.scr_readw((addr))

#elif defined(CONFIG_PMAC_CONSOLE)
extern void pmac_blitc(unsigned, unsigned long);
extern void compat_memsetw(unsigned short *p, unsigned short c, unsigned count);
extern void compat_memcpyw(unsigned short *to, unsigned short *from, unsigned count);

static inline void compat_scr_writew(unsigned short val, unsigned short * addr)
{
	if ((unsigned long)addr < video_mem_term &&
	    (unsigned long)addr >= video_mem_base) {
		if (*addr != val) {
			*addr = val;
			pmac_blitc(val, (unsigned long) addr);
		}
	} else
		*addr = val;
}

static inline unsigned short compat_scr_readw(unsigned short * addr)
{
	return *addr;
}
#endif

#if !defined(CONFIG_SUN_CONSOLE) && !defined(CONFIG_PMAC_CONSOLE)
static inline void compat_memsetw(void * s, unsigned short c, unsigned int count)
{
	unsigned short * addr = (unsigned short *) s;

	count /= 2;
	while (count) {
		count--;
		scr_writew(c, addr++);
	}
}

static inline void compat_memcpyw(unsigned short *to, unsigned short *from,
				  unsigned int count)
{
	count /= 2;
	while (count) {
		count--;
		scr_writew(scr_readw(from++), to++);
	}
}
#endif

#endif /* _LINUX_CONSOLE_COMPAT_H_ */
