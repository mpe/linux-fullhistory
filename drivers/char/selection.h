/*
 * selection.h
 *
 * Interface between console.c, tty_io.c, vt.c, vc_screen.c and selection.c
 */
extern int sel_cons;

extern void clear_selection(void);
extern int set_selection(const unsigned long arg, struct tty_struct *tty);
extern int paste_selection(struct tty_struct *tty);
extern int sel_loadlut(const unsigned long arg);
extern int mouse_reporting(void);
extern void mouse_report(struct tty_struct * tty, int butt, int mrx, int mry);

extern unsigned long video_num_columns;
extern unsigned long video_num_lines;
extern unsigned long video_size_row;

extern void do_unblank_screen(void);
extern unsigned short *screen_pos(int currcons, int w_offset, int viewed);
extern unsigned short screen_word(int currcons, int offset, int viewed);
extern void complement_pos(int currcons, int offset);
extern void invert_screen(int currcons, int offset, int count, int shift);

#define reverse_video_char(a)	(((a) & 0x88) | ((((a) >> 4) | ((a) << 4)) & 0x77))
#define reverse_video_short(a)	(((a) & 0x88ff) | \
	(((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4))
/* this latter line used to have masks 0xf000 and 0x0f00, but selection
   requires a self-inverse operation; moreover, the old version looks wrong */

extern void getconsxy(int currcons, char *p);
extern void putconsxy(int currcons, char *p);

/* how to access screen memory */
#ifdef __alpha__

#include <asm/io.h> 

static inline void scr_writew(unsigned short val, unsigned short * addr)
{
	if ((long) addr < 0)
		*addr = val;
	else
		writew(val, (unsigned long) addr);
}

static inline unsigned short scr_readw(unsigned short * addr)
{
	if ((long) addr < 0)
		return *addr;
	return readw((unsigned long) addr);
}

#else

static inline void scr_writew(unsigned short val, unsigned short * addr)
{
	*addr = val;
}

static inline unsigned short scr_readw(unsigned short * addr)
{
	return *addr;
}

#endif
