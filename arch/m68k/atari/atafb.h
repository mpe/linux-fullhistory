#include <linux/fb.h>
#include <linux/console.h>


struct display
{
    int bytes_per_row;		/* offset to one line below */

    int cursor_x;		/* current cursor position */
    int cursor_y;

    int fgcol;			/* text colors */
    int bgcol;

    struct fb_var_screeninfo var;	/* variable infos */
    struct fb_cmap	cmap;		/* colormap */
  
    /* the following three are copies from fb_fix_screeninfo */
    int visual;
    int type;
    int type_aux;

    u_char *bitplane;	        /* pointer to top of physical screen */

    u_char *screen_base;	/* pointer to top of virtual screen */    

    u_char *fontdata;           /* Font associated to this display */
    int fontheight;
    int fontwidth;

    int inverse;		/* != 0 text black on white as default */
    struct vc_data *conp;	/* pointer to console data */
    struct display_switch *dispsw; /* pointers to depth specific functions */
}; 

struct fb_info
{
    char modename[40];		/* name of the at boottime detected video mode */
    struct display *disp;	/* pointer to display variables */
    int (*changevar)(int);	/* tell the console var has changed */
    int (*switch_con)(int);	/* tell the framebuffer to switch consoles */
    int (*updatevar)(int);	/* tell the framebuffer to update the vars */
    void (*blank)(int);		/* tell the framebuffer to (un)blank the screen */
};

struct fb_info *atafb_init(long *);


