/*
 * console_struct.h
 *
 * Data structure and defines shared between console.c, vga.c and tga.c
 */

/*
 * You can set here how should the cursor look by default.
 * In case you set CONFIG_SOFTCURSOR, this might be really interesting.
 */
#define CUR_DEFAULT CUR_UNDERLINE

#define NPAR 16

struct vc_data {
	unsigned short	vc_num;			/* Console number */
	unsigned long	vc_cols;
	unsigned long	vc_rows;
	unsigned long	vc_size_row;
	struct consw	*vc_sw;
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
	unsigned long	vc_x,vc_y;
	unsigned long	vc_top,vc_bottom;
	unsigned long	vc_state;
	unsigned long	vc_npar,vc_par[NPAR];
	unsigned long	vc_pos;
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
	unsigned long	vc_can_do_color	: 1;
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
	unsigned int	vc_cursor_type;
	/* additional information is in vt_kern.h */
};

struct vc {
	struct vc_data *d;

	/* might add  scrmem, vt_struct, kbd  at some time,
	   to have everything in one place - the disadvantage
	   would be that vc_cons etc can no longer be static */
};

extern struct vc vc_cons [MAX_NR_CONSOLES];

#define CUR_DEF		0
#define CUR_NONE	1
#define CUR_UNDERLINE	2
#define CUR_LOWER_THIRD	3
#define CUR_LOWER_HALF	4
#define CUR_TWO_THIRDS	5
#define CUR_BLOCK	6
#define CUR_HWMASK	0x0f
#define CUR_SWMASK	0xfff0
