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

extern unsigned long video_num_columns;
extern unsigned long video_num_lines;
extern unsigned long video_size_row;
extern unsigned char video_type;
extern unsigned long video_mem_base;
extern unsigned long video_mem_term;
extern unsigned long video_screen_size;
extern unsigned short video_port_reg;
extern unsigned short video_port_val;

extern int console_blanked;
extern int can_do_color;

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

extern void getconsxy(int currcons, char *p);
extern void putconsxy(int currcons, char *p);


/* how to access screen memory */

#include <linux/config.h>

#ifdef CONFIG_TGA_CONSOLE

extern int tga_blitc(unsigned int, unsigned long);
extern unsigned long video_mem_term;

/*
 * TGA console screen memory access
 * 
 * TGA is *not* a character/attribute cell device; font bitmaps must be rendered
 * to the screen pixels.
 *
 * The "unsigned short * addr" is *ALWAYS* a kernel virtual address, either
 * of the VC's backing store, or the "shadow screen" memory where the screen
 * contents are kept, as the TGA frame buffer is *not* char/attr cells.
 *
 * We must test for an Alpha kernel virtual address that falls within
 *  the "shadow screen" memory. This condition indicates we really want 
 *  to write to the screen, so, we do... :-)
 *
 * NOTE also: there's only *TWO* operations: to put/get a character/attribute.
 *  All the others needed by VGA support go away, as Not Applicable for TGA.
 */
static inline void scr_writew(unsigned short val, unsigned short * addr)
{
	/*
	 * always deposit the char/attr, then see if it was to "screen" mem.
	 * if so, then render the char/attr onto the real screen.
	 */
        *addr = val;
        if ((unsigned long)addr < video_mem_term &&
	    (unsigned long)addr >= video_mem_base) {
                tga_blitc(val, (unsigned long) addr);
        }
}

static inline unsigned short scr_readw(unsigned short * addr)
{
	return *addr;
}
#else /* CONFIG_TGA_CONSOLE */

/*
 * normal VGA console access
 *
 */ 

#ifdef __alpha__

#include <asm/io.h> 

/*
 * NOTE: "(long) addr < 0" tests for an Alpha kernel virtual address; this
 *  indicates a VC's backing store; otherwise, it's a bus memory address, for
 *  the VGA's screen memory, so we do the Alpha "swizzle"... :-)
 */
static inline void scr_writeb(unsigned char val, unsigned char * addr)
{
	if ((long) addr < 0)
		*addr = val;
	else
		writeb(val, (unsigned long) addr);
}

static inline unsigned char scr_readb(unsigned char * addr)
{
	if ((long) addr < 0)
		return *addr;
	return readb((unsigned long) addr);
}

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

#else /* __alpha__ */
/*
 * normal VGA console access
 * 
 * NOTE: these do normal PC-style frame buffer accesses
 */
static inline void scr_writeb(unsigned char val, unsigned char * addr)
{
	*addr = val;
}

static inline unsigned char scr_readb(unsigned char * addr)
{
	return *addr;
}

static inline void scr_writew(unsigned short val, unsigned short * addr)
{
	*addr = val;
}

static inline unsigned short scr_readw(unsigned short * addr)
{
	return *addr;
}

#endif /* __alpha__ */
#endif /* CONFIG_TGA_CONSOLE */

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
