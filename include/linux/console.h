/*
 *  linux/include/linux/console.h
 *
 *  Copyright (C) 1993        Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Changed:
 * 10-Mar-94: Arno Griffioen: Conversion for vt100 emulator port from PC LINUX
 */

#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#define NPAR 16

struct vc_data {
	unsigned long	vc_screenbuf_size;
	unsigned short	vc_num;			/* Console number */
	unsigned short	vc_video_erase_char;	/* Background erase character */
	unsigned char	vc_attr;		/* Current attributes */
	unsigned char	vc_def_color;		/* Default colors */
	unsigned char	vc_color;		/* Foreground & background */
	unsigned char	vc_s_color;		/* Saved foreground & background */
	unsigned char	vc_ulcolor;		/* Colour for underline mode */
	unsigned char	vc_halfcolor;		/* Colour for half intensity mode */
	unsigned long	vc_origin;		/* Used for EGA/VGA fast scroll	*/
	unsigned long	vc_scr_end;		/* Used for EGA/VGA fast scroll	*/
	unsigned short	*vc_pos;
	unsigned long	vc_x,vc_y;
	unsigned long	vc_top,vc_bottom;
	unsigned long	vc_rows,vc_cols;
	unsigned long	vc_size_row;
	unsigned long	vc_state;
	unsigned long	vc_npar,vc_par[NPAR];
	unsigned short	*vc_video_mem_start;
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
	unsigned long	vc_utf_char;
	unsigned long	vc_tab_stop[5];		/* Tab stops. 160 columns. */
	unsigned short  *vc_translate;
	unsigned char 	vc_G0_charset;
	unsigned char 	vc_G1_charset;
	unsigned char 	vc_saved_G0;
	unsigned char 	vc_saved_G1;
	unsigned int	vc_bell_pitch;		/* Console bell pitch */
	unsigned int	vc_bell_duration;	/* Console bell duration */
	struct consw 	*vc_sw;
	/* additional information is in vt_kern.h */
};

/*
 * this is what the terminal answers to a ESC-Z or csi0c query.
 */
#define VT100ID "\033[?1;2c"
#define VT102ID "\033[?6c"

/* DPC: 1994-04-13 !!! con_putcs is new entry !!! */

struct consw {
    unsigned long (*con_startup)(unsigned long, char **);
    void   (*con_init)(struct vc_data *);
    int    (*con_deinit)(struct vc_data *);
    int    (*con_clear)(struct vc_data *, int, int, int, int);
    int    (*con_putc)(struct vc_data *, int, int, int);
    int    (*con_putcs)(struct vc_data *, const char *, int, int, int);
    int    (*con_cursor)(struct vc_data *, int);
    int    (*con_scroll)(struct vc_data *, int, int, int, int);
    int    (*con_bmove)(struct vc_data *, int, int, int, int, int, int);
    int    (*con_switch)(struct vc_data *);
    int    (*con_blank)(int);
    int    (*con_get_font)(struct vc_data *, int *, int *, char *);
    int    (*con_set_font)(struct vc_data *, int, int, char *);
};

extern struct consw *conswitchp;

/* flag bits */
#define CON_INITED  (1)

/* scroll */
#define SM_UP       (1)
#define SM_DOWN     (2)
#define SM_LEFT     (3)
#define SM_RIGHT    (4)

/* cursor */
#define CM_DRAW     (1)
#define CM_ERASE    (2)
#define CM_MOVE     (3)

#endif /* linux/console.h */
