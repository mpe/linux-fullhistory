/*
 * selection.h
 *
 * Interface between console.c, tty_io.c, vt.c, vc_screen.c and selection.c
 */

extern int sel_cons;

extern void clear_selection(void);
extern int set_selection(const unsigned long arg, struct tty_struct *tty, int user);
extern int paste_selection(struct tty_struct *tty);
extern int sel_loadlut(const unsigned long arg);
extern int mouse_reporting(void);
extern void mouse_report(struct tty_struct * tty, int butt, int mrx, int mry);

#define video_num_columns	(vc_cons[currcons].d->vc_cols)
#define video_num_lines		(vc_cons[currcons].d->vc_rows)
#define video_size_row		(vc_cons[currcons].d->vc_size_row)
#define video_screen_size	(vc_cons[currcons].d->vc_screenbuf_size)
#define can_do_color		(vc_cons[currcons].d->vc_can_do_color)

extern int console_blanked;

extern unsigned long video_font_height;
extern unsigned long video_scan_lines;
extern unsigned long default_font_height;
extern int video_font_is_default;

extern unsigned char color_table[];
extern int default_red[];
extern int default_grn[];
extern int default_blu[];

extern unsigned short __real_origin;
extern unsigned short __origin;
extern unsigned char has_wrapped;

extern unsigned short *vc_scrbuf[MAX_NR_CONSOLES];

extern void do_unblank_screen(void);
extern unsigned short *screen_pos(int currcons, int w_offset, int viewed);
extern unsigned short screen_word(int currcons, int offset, int viewed);
extern int scrw2glyph(unsigned short scr_word);
extern void complement_pos(int currcons, int offset);
extern void invert_screen(int currcons, int offset, int count, int shift);

#define reverse_video_char(a)	(((a) & 0x88) | ((((a) >> 4) | ((a) << 4)) & 0x77))
#define reverse_video_short(a)	(((a) & 0x88ff) | \
	(((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4))
/* this latter line used to have masks 0xf000 and 0x0f00, but selection
   requires a self-inverse operation; moreover, the old version looks wrong */
#define reverse_video_short_mono(a)	((a) ^ 0x800)
#define complement_video_short(a)	((a) ^ (can_do_color ? 0x7700 : 0x800))

extern void getconsxy(int currcons, char *p);
extern void putconsxy(int currcons, char *p);


/* how to access screen memory */

static inline void scr_writew(unsigned short val, unsigned short *addr)
{
	/* simply store the value in the "shadow screen" memory */
	*addr = val;
}

static inline unsigned short scr_readw(unsigned short * addr)
{
	return *addr;
}

static inline void memsetw(void * s, unsigned short c, unsigned int count)
{
	unsigned short * addr = (unsigned short *) s;

	count /= 2;
	while (count) {
		count--;
		scr_writew(c, addr++);
	}
}

static inline void memcpyw(unsigned short *to, unsigned short *from,
			   unsigned int count)
{
	count /= 2;
	while (count) {
		count--;
		scr_writew(scr_readw(from++), to++);
	}
}
