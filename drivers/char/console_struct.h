/*
 * console_struct.h
 *
 * Data structure and defines shared between console.c, vga.c and tga.c
 */

#define NPAR 16

struct vc_data {
	unsigned long	vc_screenbuf_size;
	unsigned short	vc_video_erase_char;	/* Background erase character */
	unsigned char	vc_attr;		/* Current attributes */
	unsigned char	vc_def_color;		/* Default colors */
	unsigned char	vc_color;		/* Foreground & background */
	unsigned char	vc_s_color;		/* Saved foreground & background */
	unsigned char	vc_ulcolor;		/* Colour for underline mode */
	unsigned char	vc_halfcolor;		/* Colour for half intensity mode */
	unsigned long	vc_origin;		/* Used for EGA/VGA fast scroll	*/
	unsigned long	vc_scr_end;		/* Used for EGA/VGA fast scroll	*/
	unsigned long	vc_pos;
	unsigned long	vc_x,vc_y;
	unsigned long	vc_top,vc_bottom;
	unsigned long	vc_state;
	unsigned long	vc_npar,vc_par[NPAR];
	unsigned long	vc_video_mem_start;	/* Start of video RAM		*/
	unsigned long	vc_video_mem_end;	/* End of video RAM (sort of)	*/
	unsigned long	vc_saved_x;
	unsigned long	vc_saved_y;
	/* mode flags */
	unsigned long	vc_charset	: 1;	/* Character set G0 / G1 */
	unsigned long	vc_s_charset	: 1;	/* Saved character set */
	unsigned long	vc_disp_ctrl	: 1;	/* Display chars < 32? */
	unsigned long	vc_toggle_meta	: 1;	/* Toggle high bit? */
	unsigned long	vc_decscnm	: 1;	/* Screen Mode */
	unsigned long	vc_decom	: 1;	/* Origin Mode */
	unsigned long	vc_decawm	: 1;	/* Autowrap Mode */
	unsigned long	vc_deccm	: 1;	/* Cursor Visible */
	unsigned long	vc_decim	: 1;	/* Insert Mode */
	unsigned long	vc_deccolm	: 1;	/* 80/132 Column Mode */
	/* attribute flags */
	unsigned long	vc_intensity	: 2;	/* 0=half-bright, 1=normal, 2=bold */
	unsigned long	vc_underline	: 1;
	unsigned long	vc_blink	: 1;
	unsigned long	vc_reverse	: 1;
	unsigned long	vc_s_intensity	: 2;	/* saved rendition */
	unsigned long	vc_s_underline	: 1;
	unsigned long	vc_s_blink	: 1;
	unsigned long	vc_s_reverse	: 1;
	/* misc */
	unsigned long	vc_ques		: 1;
	unsigned long	vc_need_wrap	: 1;
	unsigned long	vc_has_scrolled : 1;	/* Info for unblank_screen */
	unsigned long	vc_kmalloced	: 1;	/* kfree_s() needed */
	unsigned long	vc_report_mouse : 2;
	unsigned char	vc_utf		: 1;	/* Unicode UTF-8 encoding */
	unsigned char	vc_utf_count;
		 long	vc_utf_char;
	unsigned long	vc_tab_stop[5];		/* Tab stops. 160 columns. */
	unsigned char   vc_palette[16*3];       /* Colour palette for VGA+ */
	unsigned short * vc_translate;
	unsigned char 	vc_G0_charset;
	unsigned char 	vc_G1_charset;
	unsigned char 	vc_saved_G0;
	unsigned char 	vc_saved_G1;
	unsigned int	vc_bell_pitch;		/* Console bell pitch */
	unsigned int	vc_bell_duration;	/* Console bell duration */
	/* additional information is in vt_kern.h */
};

struct vc {
	struct vc_data *d;

	/* might add  scrmem, vt_struct, kbd  at some time,
	   to have everything in one place - the disadvantage
	   would be that vc_cons etc can no longer be static */
};

extern struct vc vc_cons [MAX_NR_CONSOLES];

#define screenbuf_size	(vc_cons[currcons].d->vc_screenbuf_size)
#define origin		(vc_cons[currcons].d->vc_origin)
#define scr_end	(vc_cons[currcons].d->vc_scr_end)
#define pos		(vc_cons[currcons].d->vc_pos)
#define top		(vc_cons[currcons].d->vc_top)
#define bottom		(vc_cons[currcons].d->vc_bottom)
#define x		(vc_cons[currcons].d->vc_x)
#define y		(vc_cons[currcons].d->vc_y)
#define vc_state	(vc_cons[currcons].d->vc_state)
#define npar		(vc_cons[currcons].d->vc_npar)
#define par		(vc_cons[currcons].d->vc_par)
#define ques		(vc_cons[currcons].d->vc_ques)
#define attr		(vc_cons[currcons].d->vc_attr)
#define saved_x	(vc_cons[currcons].d->vc_saved_x)
#define saved_y	(vc_cons[currcons].d->vc_saved_y)
#define translate	(vc_cons[currcons].d->vc_translate)
#define G0_charset	(vc_cons[currcons].d->vc_G0_charset)
#define G1_charset	(vc_cons[currcons].d->vc_G1_charset)
#define saved_G0	(vc_cons[currcons].d->vc_saved_G0)
#define saved_G1	(vc_cons[currcons].d->vc_saved_G1)
#define utf		(vc_cons[currcons].d->vc_utf)
#define utf_count	(vc_cons[currcons].d->vc_utf_count)
#define utf_char	(vc_cons[currcons].d->vc_utf_char)
#define video_mem_start (vc_cons[currcons].d->vc_video_mem_start)
#define video_mem_end	(vc_cons[currcons].d->vc_video_mem_end)
#define video_erase_char (vc_cons[currcons].d->vc_video_erase_char)
#define disp_ctrl	(vc_cons[currcons].d->vc_disp_ctrl)
#define toggle_meta	(vc_cons[currcons].d->vc_toggle_meta)
#define decscnm	(vc_cons[currcons].d->vc_decscnm)
#define decom		(vc_cons[currcons].d->vc_decom)
#define decawm		(vc_cons[currcons].d->vc_decawm)
#define deccm		(vc_cons[currcons].d->vc_deccm)
#define decim		(vc_cons[currcons].d->vc_decim)
#define deccolm	(vc_cons[currcons].d->vc_deccolm)
#define need_wrap	(vc_cons[currcons].d->vc_need_wrap)
#define has_scrolled	(vc_cons[currcons].d->vc_has_scrolled)
#define kmalloced	(vc_cons[currcons].d->vc_kmalloced)
#define report_mouse	(vc_cons[currcons].d->vc_report_mouse)
#define color		(vc_cons[currcons].d->vc_color)
#define s_color	(vc_cons[currcons].d->vc_s_color)
#define def_color	(vc_cons[currcons].d->vc_def_color)
#define 	foreground	(color & 0x0f)
#define 	background	(color & 0xf0)
#define charset	(vc_cons[currcons].d->vc_charset)
#define s_charset	(vc_cons[currcons].d->vc_s_charset)
#define	intensity	(vc_cons[currcons].d->vc_intensity)
#define	underline	(vc_cons[currcons].d->vc_underline)
#define	blink		(vc_cons[currcons].d->vc_blink)
#define	reverse		(vc_cons[currcons].d->vc_reverse)
#define	s_intensity	(vc_cons[currcons].d->vc_s_intensity)
#define	s_underline	(vc_cons[currcons].d->vc_s_underline)
#define	s_blink		(vc_cons[currcons].d->vc_s_blink)
#define	s_reverse	(vc_cons[currcons].d->vc_s_reverse)
#define	ulcolor		(vc_cons[currcons].d->vc_ulcolor)
#define	halfcolor	(vc_cons[currcons].d->vc_halfcolor)
#define tab_stop	(vc_cons[currcons].d->vc_tab_stop)
#define palette		(vc_cons[currcons].d->vc_palette)
#define bell_pitch	(vc_cons[currcons].d->vc_bell_pitch)
#define bell_duration	(vc_cons[currcons].d->vc_bell_duration)

#define vcmode		(vt_cons[currcons]->vc_mode)
#define structsize	(sizeof(struct vc_data) + sizeof(struct vt_struct))
