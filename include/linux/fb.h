#ifndef _LINUX_FB_H
#define _LINUX_FB_H

/* Definitions of frame buffers						*/

/* ioctls
   0x46 is 'F'								*/
#define FBIOGET_VSCREENINFO 	0x4600
#define FBIOPUT_VSCREENINFO 	0x4601
#define FBIOGET_FSCREENINFO 	0x4602
#define FBIOGETCMAP		0x4604
#define FBIOPUTCMAP		0x4605
#define FBIOPAN_DISPLAY         0x4606

#define FB_TYPE_PACKED_PIXELS		0	/* Packed Pixels	*/
#define FB_TYPE_PLANES			1	/* Non interleaved planes */
#define FB_TYPE_INTERLEAVED_PLANES	2	/* Interleaved planes	*/

#define FB_VISUAL_MONO01		0	/* Monochr. 1=Black 0=White */
#define FB_VISUAL_MONO10		1	/* Monochr. 1=White 0=Black */
#define FB_VISUAL_TRUECOLOR		2	/* True color	*/
#define FB_VISUAL_PSEUDOCOLOR		3	/* Pseudo color (like atari) */
#define FB_VISUAL_DIRECTCOLOR		4	/* Direct color */
#define FB_VISUAL_STATIC_PSEUDOCOLOR	5	/* Pseudo color readonly */
#define FB_VISUAL_STATIC_DIRECTCOLOR	6	/* Direct color readonly */

struct fb_fix_screeninfo {
	char id[16];			/* identification string eg "TT Builtin" */
	unsigned long smem_start;	/* Start of frame buffer mem */
	unsigned long smem_len;		/* Length of frame buffer mem */	
	int type;			/* see FB_TYPE_* 		*/
	int type_aux;			/* Interleave for interleaved Planes */
	int visual;			/* see FB_VISUAL_*  		*/ 
	u_short xpanstep;               /* zero if no hardware panning  */
        u_short ypanstep;               /* zero if no hardware panning  */
        u_short ywrapstep;              /* zero if no hardware ywrap    */
        u_long line_length;             /* length of a line in bytes    */
        short reserved[9];              /* Reserved for future compatibility */
};

struct fb_bitfield {
	int offset;			/* beginning of bitfield	*/
	int length;			/* length of bitfield		*/
	int msb_right;			/* != 0 : Most significant bit is */ 
					/* right */ 
};

#define FB_NONSTD_HAM		1	/* Hold-And-Modify (HAM)        */

#define FB_ACTIVATE_NOW		0	/* set values immediately (or vbl)*/
#define FB_ACTIVATE_NXTOPEN	1	/* activate on next open	*/
#define FB_ACTIVATE_TEST	2	/* don't set, round up impossible */
#define FB_ACTIVATE_MASK       15
					/* values			*/
#define FB_ACTIVATE_VBL	       16	/* activate values on next vbl  */
#define FB_CHANGE_CMAP_VBL     32	/* change colormap on vbl	*/

#define FB_ACCEL_NONE		0	/* no hardware accelerator	*/
#define FB_ACCEL_ATARIBLITT	1	/* Atari Blitter		*/
#define FB_ACCEL_AMIGABLITT	2	/* Amiga Blitter                */
#define FB_ACCEL_CYBERVISION	3	/* Cybervision64 (S3 Trio64)    */

#define FB_SYNC_HOR_HIGH_ACT	1	/* horizontal sync high active	*/
#define FB_SYNC_VERT_HIGH_ACT	2	/* vertical sync high active	*/
#define FB_SYNC_EXT		4	/* external sync		*/
#define FB_SYNC_COMP_HIGH_ACT	8	/* composite sync high active   */
#define FB_SYNC_BROADCAST	16	/* broadcast video timings      */
					/* vtotal = 144d/288n/576i => PAL  */
					/* vtotal = 121d/242n/484i => NTSC */

#define FB_VMODE_NONINTERLACED  0	/* non interlaced */
#define FB_VMODE_INTERLACED 	1	/* interlaced	*/
#define FB_VMODE_DOUBLE		2	/* double scan */
#define FB_VMODE_MASK		255

#define FB_VMODE_YWRAP		256	/* ywrap instead of panning     */
#define FB_VMODE_SMOOTH_XPAN	512	/* smooth xpan possible (internally used) */
#define FB_VMODE_CONUPDATE	512	/* don't update x/yoffset	*/

struct fb_var_screeninfo {
	int xres;			/* visible resolution		*/
	int yres;
	int xres_virtual;		/* virtual resolution		*/
	int yres_virtual;
	int xoffset;			/* offset from virtual to visible */
	int yoffset;			/* resolution			*/

	int bits_per_pixel;		/* guess what 			*/
	int grayscale;			/* != 0 Graylevels instead of colors */

	struct fb_bitfield red;		/* bitfield in fb mem if true color, */
	struct fb_bitfield green;	/* else only length is significant */
	struct fb_bitfield blue;
	struct fb_bitfield transp;	/* transparency			*/	

	int nonstd;			/* != 0 Non standard pixel format */

	int activate;			/* see FB_ACTIVATE_* 		*/

	int height;			/* height of picture in mm    */
	int width;			/* width of picture in mm     */

	int accel;			/* see FB_ACCEL_*		*/

	/* Timing: All values in pixclocks, except pixclock (of course) */
	unsigned long pixclock;		/* pixel clock in ps (pico seconds) */
	unsigned long left_margin;	/* time from sync to picture	*/
	unsigned long right_margin;	/* time from picture to sync	*/
	unsigned long upper_margin;	/* time from sync to picture	*/
	unsigned long lower_margin;
	unsigned long hsync_len;	/* length of horizontal sync	*/
	unsigned long vsync_len;	/* length of vertical sync	*/
	int sync;			/* see FB_SYNC_*		*/
	int vmode;			/* see FB_VMODE_*		*/
	int reserved[6];		/* Reserved for future compatibility */
};

struct fb_cmap {
	int start;			/* First entry	*/
	int len;			/* Number of entries */
	unsigned short *red;		/* Red values	*/
	unsigned short *green;
	unsigned short *blue;
	unsigned short *transp;		/* transparency, can be NULL */
};

#ifdef __KERNEL__

#include <linux/fs.h>

struct fb_ops {
	/* get non settable parameters	*/
	int (*fb_get_fix) (struct fb_fix_screeninfo *, int); 
	/* get settable parameters	*/
	int (*fb_get_var) (struct fb_var_screeninfo *, int);		
	/* set settable parameters	*/
	int (*fb_set_var) (struct fb_var_screeninfo *, int);		
	/* get colormap			*/
	int (*fb_get_cmap) (struct fb_cmap *, int, int);
	/* set colormap			*/
	int (*fb_set_cmap) (struct fb_cmap *, int, int);
	/* pan display                   */
        int (*fb_pan_display) (struct fb_var_screeninfo *, int);
        /* perform fb specific ioctl	*/
	int (*fb_ioctl)(struct inode *, struct file *, unsigned int,
			unsigned long, int);
};

int register_framebuffer(char *, int *, struct fb_ops *, int, 
			 struct fb_var_screeninfo *);
int unregister_framebuffer(int);

   /*
    *    This is the interface between the low-level console driver and the
    *    low-level frame buffer device
    */

struct display {
   /* Filled in by the frame buffer device */

   struct fb_var_screeninfo var;    /* variable infos. yoffset and vmode */
                                    /* are updated by fbcon.c */
   struct fb_cmap cmap;             /* colormap */
   u_char *screen_base;             /* pointer to top of virtual screen */    
   int visual;
   int type;                        /* see FB_TYPE_* */
   int type_aux;                    /* Interleave for interleaved Planes */
   u_short ypanstep;                /* zero if no hardware ypan */
   u_short ywrapstep;               /* zero if no hardware ywrap */
   u_long line_length;              /* length of a line in bytes */
   u_short can_soft_blank;          /* zero if no hardware blanking */
   u_short inverse;                 /* != 0 text black on white as default */

#if 0
   struct fb_fix_cursorinfo fcrsr;
   struct fb_var_cursorinfo *vcrsr;
   struct fb_cursorstate crsrstate;
#endif

   /* Filled in by the low-level console driver */

   struct vc_data *conp;            /* pointer to console data */
   int vrows;                       /* number of virtual rows */
   int cursor_x;                    /* current cursor position */
   int cursor_y;
   int fgcol;                       /* text colors */
   int bgcol;
   u_long next_line;                /* offset to one line below */
   u_long next_plane;               /* offset to next plane */
   u_char *fontdata;                /* Font associated to this display */
   int fontheight;
   int fontwidth;
   int userfont;                    /* != 0 if fontdata kmalloc()ed */
   struct display_switch *dispsw;   /* low level operations */
   u_short scrollmode;              /* Scroll Method */
   short yscroll;                   /* Hardware scrolling */
}; 


struct fb_info {
   char modename[40];               /* at boottime detected video mode */
   struct display *disp;            /* pointer to display variables */
   char fontname[40];               /* default font name */
   int (*changevar)(int);           /* tell console var has changed */
   int (*switch_con)(int);          /* tell fb to switch consoles */
   int (*updatevar)(int);           /* tell fb to update the vars */
   void (*blank)(int);              /* tell fb to (un)blank the screen */
};

#endif /* __KERNEL__ */

#if 1

#define FBCMD_GET_CURRENTPAR	    0xDEAD0005
#define FBCMD_SET_CURRENTPAR        0xDEAD8005

#endif


#if 1 /* Preliminary */

   /*
    *    Hardware Cursor
    */

#define FBIOGET_FCURSORINFO     0x4607
#define FBIOGET_VCURSORINFO     0x4608
#define FBIOPUT_VCURSORINFO     0x4609
#define FBIOGET_CURSORSTATE     0x460A
#define FBIOPUT_CURSORSTATE     0x460B


struct fb_fix_cursorinfo {
	u_short crsr_width;		/* width and height of the cursor in */
	u_short crsr_height;		/* pixels (zero if no cursor)	*/
	u_short crsr_xsize;		/* cursor size in display pixels */
	u_short crsr_ysize;
	u_short crsr_color1;		/* colormap entry for cursor color1 */
	u_short crsr_color2;		/* colormap entry for cursor color2 */
};

struct fb_var_cursorinfo {
        u_short width;
        u_short height;
        u_short xspot;
        u_short yspot;
        u_char data[1];                 /* field with [height][width]        */
};

struct fb_cursorstate {
	short xoffset;
	short yoffset;
	u_short mode;
};

#define FB_CURSOR_OFF		0
#define FB_CURSOR_ON		1
#define FB_CURSOR_FLASH		2

#define FBCMD_DRAWLINE		0x4621
#define FBCMD_MOVE		0x4622

#define FB_LINE_XOR	1
#define FB_LINE_BOX	2
#define FB_LINE_FILLED	4

struct fb_line {
	int start_x;
	int start_y;
	int end_x;
	int end_y;
	int color;
	int option;
};

struct fb_move {
	int src_x;
	int src_y;
	int dest_x;
	int dest_y;
	int height;
	int width;
};

#endif /* Preliminary */


#endif /* _LINUX_FB_H */
