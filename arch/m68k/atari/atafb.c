/*
 * atari/atafb.c -- Low level implementation of Atari frame buffer device
 *
 *  Copyright (C) 1994 Martin Schaller & Roman Hodek
 *  
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * History:
 *   - 03 Jan 95: Original version by Martin Schaller: The TT driver and
 *                all the device independent stuff
 *   - 09 Jan 95: Roman: I've added the hardware abstraction (hw_switch)
 *                and wrote the Falcon, ST(E), and External drivers
 *                based on the original TT driver.
 *   - 07 May 95: Martin: Added colormap operations for the external driver
 *   - 21 May 95: Martin: Added support for overscan
 *		  Andreas: some bug fixes for this
 *   -    Jul 95: Guenther Kelleter <guenther@pool.informatik.rwth-aachen.de>:
 *                Programmable Falcon video modes
 *                (thanks to Christian Cartus for documentation
 *                of VIDEL registers).
 *   - 27 Dec 95: Guenther: Implemented user definable video modes "user[0-7]"
 *                on minor 24...31. "user0" may be set on commandline by
 *                "R<x>;<y>;<depth>". (Makes sense only on Falcon)
 *                Video mode switch on Falcon now done at next VBL interrupt
 *                to avoid the annoying right shift of the screen.
 *
 *
 * To do:
 *   - For the Falcon it is not possible to set random video modes on
 *     SM124 and SC/TV, only the bootup resolution is supported.
 *
 */

#define ATAFB_TT
#define ATAFB_STE
#define ATAFB_EXT
#define ATAFB_FALCON

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>

#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/irq.h>

#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/bootinfo.h>

#include <linux/fb.h>
#include <asm/atarikb.h>

#define SWITCH_ACIA 0x01		/* modes for switch on OverScan */
#define SWITCH_SND6 0x40
#define SWITCH_SND7 0x80
#define SWITCH_NONE 0x00


#define arraysize(x)			(sizeof(x)/sizeof(*(x)))

#define up(x, r) (((x) + (r) - 1) & ~((r)-1))


static int default_par=0;	/* default resolution (0=none) */

static int node;		/* node of the /dev/fb?current file */

static unsigned long default_mem_req=0;

static int hwscroll=-1;

static int use_hwscroll = 1;

static int sttt_xres=640,st_yres=400,tt_yres=480;
static int sttt_xres_virtual=640,sttt_yres_virtual=400;
static int ovsc_offset=0, ovsc_addlen=0;
int        ovsc_switchmode=0;

#ifdef ATAFB_FALCON
static int pwrsave = 0;	/* use VESA suspend mode instead of blanking only? */
#endif

static struct atari_fb_par {
	unsigned long screen_base;
	int vyres;
	union {
		struct {
			int mode;
			int sync;
		} tt, st;
		struct falcon_hw {
			/* Here are fields for storing a video mode, as direct
			 * parameters for the hardware.
			 */
			short sync;
			short line_width;
			short line_offset;
			short st_shift;
			short f_shift;
			short vid_control;
			short vid_mode;
			short xoffset;
			short hht, hbb, hbe, hdb, hde, hss;
			short vft, vbb, vbe, vdb, vde, vss;
			/* auxiliary information */
			short mono;
			short ste_mode;
			short bpp;
		} falcon;
		/* Nothing needed for external mode */
	} hw;
} current_par;

/* Don't calculate an own resolution, and thus don't change the one found when
 * booting (currently used for the Falcon to keep settings for internal video
 * hardware extensions (e.g. ScreenBlaster)  */
static int DontCalcRes = 0; 

#define HHT hw.falcon.hht
#define HBB hw.falcon.hbb
#define HBE hw.falcon.hbe
#define HDB hw.falcon.hdb
#define HDE hw.falcon.hde
#define HSS hw.falcon.hss
#define VFT hw.falcon.vft
#define VBB hw.falcon.vbb
#define VBE hw.falcon.vbe
#define VDB hw.falcon.vdb
#define VDE hw.falcon.vde
#define VSS hw.falcon.vss
#define VCO_CLOCK25		0x04
#define VCO_CSYPOS		0x10
#define VCO_VSYPOS		0x20
#define VCO_HSYPOS		0x40
#define VCO_SHORTOFFS	0x100
#define VMO_DOUBLE		0x01
#define VMO_INTER		0x02
#define VMO_PREMASK		0x0c

static struct fb_info fb_info;

static unsigned long screen_base;	/* base address of screen */
static unsigned long real_screen_base;	/* (only for Overscan) */

static int screen_len;

static int current_par_valid=0; 

static int currcon=0;

static int mono_moni=0;

static struct display disp[MAX_NR_CONSOLES];


#ifdef ATAFB_EXT
/* external video handling */

static unsigned			external_xres;
static unsigned			external_yres;
static unsigned			external_depth;
static int				external_pmode;
static unsigned long	external_addr = 0;
static unsigned long	external_len;
static unsigned long	external_vgaiobase = 0;
static unsigned int		external_bitspercol = 6;

/* 
++JOE <joe@amber.dinoco.de>: 
added card type for external driver, is only needed for
colormap handling.
*/

enum cardtype { IS_VGA, IS_MV300 };
static enum cardtype external_card_type = IS_VGA;

/*
The MV300 mixes the color registers. So we need an array of munged
indices in order to acces the correct reg.
*/
static int MV300_reg_1bit[2]={0,1};
static int MV300_reg_4bit[16]={
0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
static int MV300_reg_8bit[256]={
0, 128, 64, 192, 32, 160, 96, 224, 16, 144, 80, 208, 48, 176, 112, 240, 
8, 136, 72, 200, 40, 168, 104, 232, 24, 152, 88, 216, 56, 184, 120, 248, 
4, 132, 68, 196, 36, 164, 100, 228, 20, 148, 84, 212, 52, 180, 116, 244, 
12, 140, 76, 204, 44, 172, 108, 236, 28, 156, 92, 220, 60, 188, 124, 252, 
2, 130, 66, 194, 34, 162, 98, 226, 18, 146, 82, 210, 50, 178, 114, 242, 
10, 138, 74, 202, 42, 170, 106, 234, 26, 154, 90, 218, 58, 186, 122, 250, 
6, 134, 70, 198, 38, 166, 102, 230, 22, 150, 86, 214, 54, 182, 118, 246, 
14, 142, 78, 206, 46, 174, 110, 238, 30, 158, 94, 222, 62, 190, 126, 254, 
1, 129, 65, 193, 33, 161, 97, 225, 17, 145, 81, 209, 49, 177, 113, 241, 
9, 137, 73, 201, 41, 169, 105, 233, 25, 153, 89, 217, 57, 185, 121, 249, 
5, 133, 69, 197, 37, 165, 101, 229, 21, 149, 85, 213, 53, 181, 117, 245, 
13, 141, 77, 205, 45, 173, 109, 237, 29, 157, 93, 221, 61, 189, 125, 253, 
3, 131, 67, 195, 35, 163, 99, 227, 19, 147, 83, 211, 51, 179, 115, 243, 
11, 139, 75, 203, 43, 171, 107, 235, 27, 155, 91, 219, 59, 187, 123, 251, 
7, 135, 71, 199, 39, 167, 103, 231, 23, 151, 87, 215, 55, 183, 119, 247, 
15, 143, 79, 207, 47, 175, 111, 239, 31, 159, 95, 223, 63, 191, 127, 255 }; 

static int *MV300_reg = MV300_reg_8bit;

/*
And on the MV300 it's difficult to read out the hardware palette. So we
just keep track of the set colors in our own array here, and use that!
*/

struct { unsigned char red,green,blue,pad; } MV300_color[256];
#endif /* ATAFB_EXT */


int inverse=0;

extern int fontheight_8x8;
extern int fontwidth_8x8;
extern unsigned char fontdata_8x8[];

extern int fontheight_8x16;
extern int fontwidth_8x16;
extern unsigned char fontdata_8x16[];

/* import first 16 colors from fbcon.c */
extern unsigned short packed16_cmap[16];


/* ++roman: This structure abstracts from the underlying hardware (ST(e),
 * TT, or Falcon.
 *
 * int (*detect)( void )
 *   This function should detect the current video mode settings and
 *   store them in atari_fb_predefined[0] for later reference by the
 *   user. Return the index+1 of an equivalent predefined mode or 0
 *   if there is no such.
 * 
 * int (*encode_fix)( struct fb_fix_screeninfo *fix,
 *                    struct atari_fb_par *par )
 *   This function should fill in the 'fix' structure based on the
 *   values in the 'par' structure.
 *   
 * int (*decode_var)( struct fb_var_screeninfo *var,
 *                    struct atari_fb_par *par )
 *   Get the video params out of 'var'. If a value doesn't fit, round
 *   it up, if it's too big, return EINVAL.
 *   Round up in the following order: bits_per_pixel, xres, yres, 
 *   xres_virtual, yres_virtual, xoffset, yoffset, grayscale, bitfields, 
 *   horizontal timing, vertical timing.
 *
 * int (*encode_var)( struct fb_var_screeninfo *var,
 *                    struct atari_fb_par *par );
 *   Fill the 'var' structure based on the values in 'par' and maybe
 *   other values read out of the hardware.
 *   
 * void (*get_par)( struct atari_fb_par *par )
 *   Fill the hardware's 'par' structure.
 *   
 * void (*set_par)( struct atari_fb_par *par )
 *   Set the hardware according to 'par'.
 *   
 * int (*setcolreg)( unsigned regno, unsigned red,
 *                   unsigned green, unsigned blue,
 *                   unsigned transp )
 *   Set a single color register. The values supplied are already
 *   rounded down to the hardware's capabilities (according to the
 *   entries in the var structure). Return != 0 for invalid regno.
 *
 * int (*getcolreg)( unsigned regno, unsigned *red,
 *                   unsigned *green, unsigned *blue,
 *                   unsigned *transp )
 *   Read a single color register and split it into
 *   colors/transparent. Return != 0 for invalid regno.
 *
 * void (*set_screen_base)( unsigned long s_base )
 *   Set the base address of the displayed frame buffer. Only called
 *   if yres_virtual > yres or xres_virtual > xres.
 *
 * int (*blank)( int blank_mode )
 *   Blank the screen if blank_mode != 0, else unblank. If NULL then blanking
 *   is done by setting the CLUT to black. Return != 0 if un-/blanking
 *   failed due to e.g. video mode which doesn't support it.
 */

static struct fb_hwswitch {
	int  (*detect)( void );
	int  (*encode_fix)( struct fb_fix_screeninfo *fix,
						struct atari_fb_par *par );
	int  (*decode_var)( struct fb_var_screeninfo *var,
						struct atari_fb_par *par );
	int  (*encode_var)( struct fb_var_screeninfo *var,
						struct atari_fb_par *par );
	void (*get_par)( struct atari_fb_par *par );
	void (*set_par)( struct atari_fb_par *par );
	int  (*getcolreg)( unsigned regno, unsigned *red,
					   unsigned *green, unsigned *blue,
					   unsigned *transp );
	int  (*setcolreg)( unsigned regno, unsigned red,
					   unsigned green, unsigned blue,
					   unsigned transp );
	void (*set_screen_base)( unsigned long s_base );
	int  (*blank)( int blank_mode );
	int  (*pan_display)( struct fb_var_screeninfo *var,
						 struct atari_fb_par *par);
} *fbhw;

static char *autodetect_names[] = {"autodetect", NULL};
static char *stlow_names[] = {"stlow", NULL};
static char *stmid_names[] = {"stmid", "default5", NULL};
static char *sthigh_names[] = {"sthigh", "default4", NULL};
static char *ttlow_names[] = {"ttlow", NULL};
static char *ttmid_names[]= {"ttmid", "default1", NULL};
static char *tthigh_names[]= {"tthigh", "default2", NULL};
static char *vga2_names[] = {"vga2", NULL};
static char *vga4_names[] = {"vga4", NULL};
static char *vga16_names[] = {"vga16", "default3", NULL};
static char *vga256_names[] = {"vga256", NULL};
static char *falh2_names[] = {"falh2", NULL};
static char *falh16_names[] = {"falh16", NULL};
static char *user0_names[] = {"user0", NULL};
static char *user1_names[] = {"user1", NULL};
static char *user2_names[] = {"user2", NULL};
static char *user3_names[] = {"user3", NULL};
static char *user4_names[] = {"user4", NULL};
static char *user5_names[] = {"user5", NULL};
static char *user6_names[] = {"user6", NULL};
static char *user7_names[] = {"user7", NULL};
static char *dummy_names[] = {"dummy", NULL};

char **fb_var_names[] = {
	/* Writing the name arrays directly in this array (via "(char *[]){...}")
	 * crashes gcc 2.5.8 (sigsegv) if the inner array
	 * contains more than two items. I've also seen that all elements
	 * were identical to the last (my cross-gcc) :-(*/
	autodetect_names,
	stlow_names,
	stmid_names,
	sthigh_names,
	ttlow_names,
	ttmid_names,
	tthigh_names,
	vga2_names,
	vga4_names,
	vga16_names,
	vga256_names,
	falh2_names,
	falh16_names,
	dummy_names, dummy_names, dummy_names, dummy_names,
	dummy_names, dummy_names, dummy_names, dummy_names,
	dummy_names, dummy_names,
	user0_names,
	user1_names,
	user2_names,
	user3_names,
	user4_names,
	user5_names,
	user6_names,
	user7_names,
	NULL
	/* ,NULL */ /* this causes a sigsegv on my gcc-2.5.8 */
};

struct fb_var_screeninfo atari_fb_predefined[] = {
 	{ /* autodetect */
	  0, 0, 0, 0, 0, 0, 0, 0,   		/* xres-grayscale */
	  {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
 	{ /* st low */
	  320, 200, 320, 200, 0, 0, 4, 0,   		/* xres-grayscale */
	  {0, 4, 0}, {0, 4, 0}, {0, 4, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* st mid */
	  640, 200, 640, 200, 0, 0, 2, 0,		/* xres-grayscale */
	  {0, 4, 0}, {0, 4, 0}, {0, 4, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* st high */
	  640, 400, 640, 400, 0, 0, 1, 0,		/* xres-grayscale */
	  {0, 4, 0}, {0, 4, 0}, {0, 4, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* tt low */
	  320, 480, 320, 480, 0, 0, 8, 0,		/* xres-grayscale */
	  {0, 4, 0}, {0, 4, 0}, {0, 4, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* tt mid */
	  640, 480, 640, 480, 0, 0, 4, 0,		/* xres-grayscale */
	  {0, 4, 0}, {0, 4, 0}, {0, 4, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* tt high */
	  1280, 960, 1280, 960, 0, 0, 1, 0,		/* xres-grayscale */
	  {0, 4, 0}, {0, 4, 0}, {0, 4, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* vga2 */
	  640, 480, 640, 480, 0, 0, 1, 0,   		/* xres-grayscale */
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* vga4 */
	  640, 480, 640, 480, 0, 0, 2, 0,		/* xres-grayscale */
	  {0, 4, 0}, {0, 4, 0}, {0, 4, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* vga16 */
	  640, 480, 640, 480, 0, 0, 4, 0,		/* xres-grayscale */
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* vga256 */
	  640, 480, 640, 480, 0, 0, 8, 0,		/* xres-grayscale */
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* falh2 */
	  896, 608, 896, 608, 0, 0, 1, 0,		/* xres-grayscale */
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* falh16 */
	  896, 608, 896, 608, 0, 0, 4, 0,		/* xres-grayscale */
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0}, 	/* red green blue tran*/
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	/* Minor 14..23 free for more standard video modes */
	{ 0, },
	{ 0, },
	{ 0, },
	{ 0, },
	{ 0, },
	{ 0, },
	{ 0, },
	{ 0, },
	{ 0, },
	{ 0, },
	/* Minor 24..31 reserved for user defined video modes */
	{ /* user0, initialized to Rx;y;d from commandline, if supplied */
	  0, 0, 0, 0, 0, 0, 0, 0,
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0},
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* user1 */
	  0, 0, 0, 0, 0, 0, 0, 0,
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0},
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* user2 */
	  0, 0, 0, 0, 0, 0, 0, 0,
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0},
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* user3 */
	  0, 0, 0, 0, 0, 0, 0, 0,
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0},
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* user4 */
	  0, 0, 0, 0, 0, 0, 0, 0,
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0},
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* user5 */
	  0, 0, 0, 0, 0, 0, 0, 0,
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0},
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* user6 */
	  0, 0, 0, 0, 0, 0, 0, 0,
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0},
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ /* user7 */
	  0, 0, 0, 0, 0, 0, 0, 0,
	  {0, 6, 0}, {0, 6, 0}, {0, 6, 0}, {0, 0, 0},
	  0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0 }
};

int num_atari_fb_predefined=arraysize(atari_fb_predefined);


static int
get_video_mode(char *vname)
{
    char ***name_list;
    char **name;
    int i;
    name_list=fb_var_names;
    for (i = 0 ; i < num_atari_fb_predefined ; i++) {
	name=*(name_list++);
	if (! name || ! *name)
	    break;
	while (*name) {
	    if (! strcmp(vname, *name))
		return i+1;
	    name++;
	}
    }
    return 0;
}



/* ------------------- TT specific functions ---------------------- */

#ifdef ATAFB_TT

static int tt_encode_fix( struct fb_fix_screeninfo *fix,
						  struct atari_fb_par *par )

{
	int mode, i;

	strcpy(fix->id,"Atari Builtin");
	fix->smem_start=real_screen_base;
	fix->smem_len = screen_len;
	fix->type=FB_TYPE_INTERLEAVED_PLANES;
	fix->type_aux=2;
	fix->visual=FB_VISUAL_PSEUDOCOLOR;
	mode = par->hw.tt.mode & TT_SHIFTER_MODEMASK;
	if (mode == TT_SHIFTER_TTHIGH || mode == TT_SHIFTER_STHIGH) {
		fix->type=FB_TYPE_PACKED_PIXELS;
		fix->type_aux=0;
		if (mode == TT_SHIFTER_TTHIGH)
			fix->visual=FB_VISUAL_MONO01;
	}
	fix->xpanstep=0;
	fix->ypanstep=1;
	fix->ywrapstep=0;
	fix->line_length = 0;
	for (i=0; i<arraysize(fix->reserved); i++)
		fix->reserved[i]=0;
	return 0;
}


static int tt_decode_var( struct fb_var_screeninfo *var,
						  struct atari_fb_par *par )
{
	int xres=var->xres;
	int yres=var->yres;
	int bpp=var->bits_per_pixel;
	int linelen;

	if (mono_moni) {
		if (bpp > 1 || xres > sttt_xres*2 || yres >tt_yres*2)
			return -EINVAL;
		par->hw.tt.mode=TT_SHIFTER_TTHIGH;
		xres=sttt_xres*2;
		yres=tt_yres*2;
		bpp=1;
	} else {
		if (bpp > 8 || xres > sttt_xres || yres > tt_yres)
			return -EINVAL;
		if (bpp > 4) {
			if (xres > sttt_xres/2 || yres > tt_yres)
				return -EINVAL;
			par->hw.tt.mode=TT_SHIFTER_TTLOW;
			xres=sttt_xres/2;
			yres=tt_yres;
			bpp=8;
		}
		else if (bpp > 2) {
			if (xres > sttt_xres || yres > tt_yres)
				return -EINVAL;
			if (xres > sttt_xres/2 || yres > st_yres/2) {
				par->hw.tt.mode=TT_SHIFTER_TTMID;
				xres=sttt_xres;
				yres=tt_yres;
				bpp=4;
			}
			else {
				par->hw.tt.mode=TT_SHIFTER_STLOW;
				xres=sttt_xres/2;
				yres=st_yres/2;
				bpp=4;
			}
		}
		else if (bpp > 1) {
			if (xres > sttt_xres || yres > st_yres/2)
				return -EINVAL;
			par->hw.tt.mode=TT_SHIFTER_STMID;
			xres=sttt_xres;
			yres=st_yres/2;
			bpp=2;
		}
		else if (var->xres > sttt_xres || var->yres > st_yres) {
			return -EINVAL;
		}
		else {
			par->hw.tt.mode=TT_SHIFTER_STHIGH;
			xres=sttt_xres;
			yres=st_yres;
			bpp=1;
		}
	}
	if (var->sync & FB_SYNC_EXT)
		par->hw.tt.sync=0;
	else
		par->hw.tt.sync=1;
	linelen=xres*bpp/8;
	if ((var->yoffset + yres)*linelen > screen_len && screen_len)
		return -EINVAL;
	par->screen_base=screen_base+ var->yoffset*linelen;
	return 0;
}

static int tt_encode_var( struct fb_var_screeninfo *var,
						  struct atari_fb_par *par )
{
	int linelen, i;
	var->red.offset=0;
	var->red.length=4;
	var->red.msb_right=0;
	var->grayscale=0;

	var->pixclock=31041;
	var->left_margin=120;		/* these may be incorrect 	*/
	var->right_margin=100;
	var->upper_margin=8;
	var->lower_margin=16;
	var->hsync_len=140;
	var->vsync_len=30;

	var->height=-1;
	var->width=-1;

	if (par->hw.tt.sync & 1)
		var->sync=0;
	else
		var->sync=FB_SYNC_EXT;

	switch (par->hw.tt.mode & TT_SHIFTER_MODEMASK) {
	case TT_SHIFTER_STLOW:
		var->xres=sttt_xres/2;
		var->xres_virtual=sttt_xres_virtual/2;
		var->yres=st_yres/2;
		var->bits_per_pixel=4;
		break;
	case TT_SHIFTER_STMID:
		var->xres=sttt_xres;
		var->xres_virtual=sttt_xres_virtual;
		var->yres=st_yres/2;
		var->bits_per_pixel=2;
		break;
	case TT_SHIFTER_STHIGH:
		var->xres=sttt_xres;
		var->xres_virtual=sttt_xres_virtual;
		var->yres=st_yres;
		var->bits_per_pixel=1;
		break;
	case TT_SHIFTER_TTLOW:
		var->xres=sttt_xres/2;
		var->xres_virtual=sttt_xres_virtual/2;
		var->yres=tt_yres;
		var->bits_per_pixel=8;
		break;
	case TT_SHIFTER_TTMID:
		var->xres=sttt_xres;
		var->xres_virtual=sttt_xres_virtual;
		var->yres=tt_yres;
		var->bits_per_pixel=4;
		break;
	case TT_SHIFTER_TTHIGH:
		var->red.length=0;
		var->xres=sttt_xres*2;
		var->xres_virtual=sttt_xres_virtual*2;
		var->yres=tt_yres*2;
		var->bits_per_pixel=1;
		break;
	}		
	var->blue=var->green=var->red;
	var->transp.offset=0;
	var->transp.length=0;
	var->transp.msb_right=0;
	linelen=var->xres_virtual * var->bits_per_pixel / 8;
	if (! use_hwscroll)
		var->yres_virtual=var->yres;
	else if (screen_len)
		var->yres_virtual=screen_len/linelen;
	else {
		if (hwscroll < 0)
			var->yres_virtual = 2 * var->yres;
		else
			var->yres_virtual=var->yres+hwscroll * 16;
	}
	var->xoffset=0;
	if (screen_base)
		var->yoffset=(par->screen_base - screen_base)/linelen;
	else
		var->yoffset=0;
	var->nonstd=0;
	var->activate=0;
	var->vmode=FB_VMODE_NONINTERLACED;
	for (i=0; i<arraysize(var->reserved); i++)
		var->reserved[i]=0;
	return 0;
}


static void tt_get_par( struct atari_fb_par *par )
{
	unsigned long addr;
	par->hw.tt.mode=shifter_tt.tt_shiftmode;
	par->hw.tt.sync=shifter.syncmode;
	addr = ((shifter.bas_hi & 0xff) << 16) |
	       ((shifter.bas_md & 0xff) << 8)  |
	       ((shifter.bas_lo & 0xff));
	par->screen_base = PTOV(addr);
}

static void tt_set_par( struct atari_fb_par *par )
{
	shifter_tt.tt_shiftmode=par->hw.tt.mode;
	shifter.syncmode=par->hw.tt.sync;
	/* only set screen_base if really necessary */
	if (current_par.screen_base != par->screen_base)
		fbhw->set_screen_base(par->screen_base);
}


static int tt_getcolreg( unsigned regno, unsigned *red,
						 unsigned *green, unsigned *blue,
						 unsigned *transp )
{
	if ((shifter_tt.tt_shiftmode & TT_SHIFTER_MODEMASK) == TT_SHIFTER_STHIGH)
		regno += 254;
	if (regno > 255)
		return 1;
	*blue = tt_palette[regno];
	*green = (*blue >> 4) & 0xf;
	*red = (*blue >> 8) & 0xf;
	*blue &= 0xf;
	*transp = 0;
	return 0;
}


static int tt_setcolreg( unsigned regno, unsigned red,
						 unsigned green, unsigned blue,
						 unsigned transp )
{
	if ((shifter_tt.tt_shiftmode & TT_SHIFTER_MODEMASK) == TT_SHIFTER_STHIGH)
		regno += 254;
	if (regno > 255)
		return 1;
	tt_palette[regno] = (red << 8) | (green << 4) | blue;
	if ((shifter_tt.tt_shiftmode & TT_SHIFTER_MODEMASK) ==
		TT_SHIFTER_STHIGH && regno == 254)
		tt_palette[0] = 0;
	return 0;
}

						  
static int tt_detect( void )

{	struct atari_fb_par par;

	/* Determine the connected monitor: The DMA sound must be
	 * disabled before reading the MFP GPIP, because the Sound
	 * Done Signal and the Monochrome Detect are XORed together!
	 *
	 * Even on a TT, we should look if there is a DMA sound. It was
	 * announced that the Eagle is TT compatible, but only the PCM is
	 * missing...
	 */
	if (ATARIHW_PRESENT(PCM_8BIT)) { 
		tt_dmasnd.ctrl = DMASND_CTRL_OFF;
		udelay(20);	/* wait a while for things to settle down */
	}
	mono_moni = (mfp.par_dt_reg & 0x80) == 0;

	tt_get_par(&par);
	tt_encode_var(&atari_fb_predefined[0], &par);

	return 1;
}

#endif /* ATAFB_TT */

/* ------------------- Falcon specific functions ---------------------- */

#ifdef ATAFB_FALCON

static int mon_type;		/* Falcon connected monitor */
static int f030_bus_width;	/* Falcon ram bus width (for vid_control) */
#define F_MON_SM	0
#define F_MON_SC	1
#define F_MON_VGA	2
#define F_MON_TV	3

/* Multisync monitor capabilities */
/* Atari-TOS defaults if no boot option present */
static long vfmin=58, vfmax=62, hfmin=31000, hfmax=32000;

static struct pixel_clock {
	unsigned long f;	/* f/[Hz] */
	unsigned long t;	/* t/[ps] (=1/f) */
	short right, hsync, left;	/* standard timing in clock cycles, not pixel */
		/* hsync initialized in falcon_detect() */
	short sync_mask;	/* or-mask for hw.falcon.sync to set this clock */
	short control_mask; /* ditto, for hw.falcon.vid_control */
}
f25  = {25175000, 39722, 18, 0, 42, 0x0, VCO_CLOCK25},
f32  = {32000000, 31250, 18, 0, 42, 0x0, 0},
fext = {       0,     0, 18, 0, 42, 0x1, 0};

/* VIDEL-prescale values [mon_type][pixel_length from VCO] */
static short vdl_prescale[4][3] = {{4,2,1}, {4,2,1}, {4,2,2}, {4,2,1}};

/* Default hsync timing [mon_type] in picoseconds */
static long h_syncs[4] = {3000000, 4700000, 4000000, 4700000};


static inline int hxx_prescale(struct falcon_hw *hw)
{
	return hw->ste_mode ? 16 :
		   vdl_prescale[mon_type][hw->vid_mode >> 2 & 0x3];
}

static int falcon_encode_fix( struct fb_fix_screeninfo *fix,
							  struct atari_fb_par *par )
{
	int i;

	strcpy(fix->id, "Atari Builtin");
	fix->smem_start = real_screen_base;
	fix->smem_len = screen_len;
	fix->type = FB_TYPE_INTERLEAVED_PLANES;
	fix->type_aux = 2;
	fix->visual = FB_VISUAL_PSEUDOCOLOR;
	if (par->hw.falcon.mono) {
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->type_aux = 0;
	}
	else if (par->hw.falcon.f_shift & 0x100) {
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->type_aux = 0;
		fix->visual = FB_VISUAL_TRUECOLOR;  /* is this ok or should this be DIRECTCOLOR? */
	}
	if (par->hw.falcon.mono)
		/* no smooth scrolling possible with longword aligned video mem */
		fix->xpanstep = 32;
	else
		fix->xpanstep = 1;
	fix->ypanstep = 1;
	fix->ywrapstep = 0;
	fix->line_length = 0;
	for (i=0; i<arraysize(fix->reserved); i++)
		fix->reserved[i]=0;
	return 0;
}


static int falcon_decode_var( struct fb_var_screeninfo *var,
							  struct atari_fb_par *par )
{
	int use_default_timing = 0;
	int bpp = var->bits_per_pixel;
	int xres = var->xres;
	int yres = var->yres;
	int xres_virtual = var->xres_virtual;
	int yres_virtual = var->yres_virtual;
	int left_margin, right_margin, hsync_len;
	int upper_margin, lower_margin, vsync_len;
	int linelen;
	int interlace = 0, doubleline = 0;
	struct pixel_clock *pclock;
	int plen; /* width of pixel in clock cycles */
	int xstretch;
	int prescale;
	int longoffset = 0;
	int hfreq, vfreq;

/*
	Get the video params out of 'var'. If a value doesn't fit, round
	it up, if it's too big, return EINVAL.
	Round up in the following order: bits_per_pixel, xres, yres, 
	xres_virtual, yres_virtual, xoffset, yoffset, grayscale, bitfields, 
	horizontal timing, vertical timing.

	There is a maximum of screen resolution determined by pixelclock
	and minimum frame rate -- (X+hmarg.)*(Y+vmarg.)*vfmin <= pixelclock.
	In interlace mode this is     "     *    "     *vfmin <= pixelclock.
	Additional constraints: hfreq.
	Frequency range for multisync monitors is given via command line.
	For TV and SM124 both frequencies are fixed.

	X % 16 == 0 to fit 8x?? font (except 1 bitplane modes must use X%32==0)
	Y % 16 == 0 to fit 8x16 font
	Y % 8 == 0 if Y<400

	Currently interlace and doubleline mode in var are ignored. 
	On SM124 and TV only the standard resolutions can be used.
*/

	/* Reject uninitialized mode */
	if (!xres || !yres || !bpp)
		return -EINVAL;

	if (mon_type == F_MON_SM && bpp != 1) {
		return -EINVAL;
	}
	else if (bpp <= 1) {
		bpp = 1;
		par->hw.falcon.f_shift = 0x400;
		par->hw.falcon.st_shift = 0x200;
	}
	else if (bpp <= 2) {
		bpp = 2;
		par->hw.falcon.f_shift = 0x000;
		par->hw.falcon.st_shift = 0x100;
	}
	else if (bpp <= 4) {
		bpp = 4;
		par->hw.falcon.f_shift = 0x000;
		par->hw.falcon.st_shift = 0x000;
	}
	else if (bpp <= 8) {
		bpp = 8;
		par->hw.falcon.f_shift = 0x010;
	}
	else if (bpp <= 16) {
		bpp = 16; /* packed pixel mode */
		par->hw.falcon.f_shift = 0x100; /* hicolor, no overlay */
	}
	else
		return -EINVAL;
	par->hw.falcon.bpp = bpp;

	if (mon_type != F_MON_VGA || DontCalcRes) {
		/* Skip all calculations, VGA multisync only yet */
		struct fb_var_screeninfo *myvar = &atari_fb_predefined[0];
		
		if (bpp > myvar->bits_per_pixel ||
			var->xres > myvar->xres ||
			var->yres > myvar->yres)
			return -EINVAL;
		fbhw->get_par(par);	/* Current par will be new par */
		goto set_screen_base;	/* Don't forget this */
	}

	/* Only some fixed resolutions < 640x480 */
	if (xres <= 320)
		xres = 320;
	else if (xres <= 640 && bpp != 16)
		xres = 640;
	if (yres <= 200)
		yres = 200;
	else if (yres <= 240)
		yres = 240;
	else if (yres <= 400)
		yres = 400;
	else if (yres <= 480)
		yres = 480;

	/* 2 planes must use STE compatibility mode */
	par->hw.falcon.ste_mode = bpp==2;
	par->hw.falcon.mono = bpp==1;

	/* Total and visible scanline length must be a multiple of one longword,
	 * this and the console fontwidth yields the alignment for xres and
	 * xres_virtual.
	 *
	 * Special case in STE mode: blank and graphic positions don't align,
	 * avoid trash at right margin
	 */
	if (par->hw.falcon.ste_mode)
		xres = (xres + 63) & ~63;
	else if (bpp == 1)
		xres = (xres + 31) & ~31;
	else
		xres = (xres + 15) & ~15;
	if (yres >= 400)
		yres = (yres + 15) & ~15;
	else
		yres = (yres + 7) & ~7;

	if (xres_virtual < xres)
		xres_virtual = xres;
	else if (bpp == 1)
		xres_virtual = (xres_virtual + 31) & ~31;
	else
		xres_virtual = (xres_virtual + 15) & ~15;
	/* <=0 : yres_virtual determined by screensize */
	if (yres_virtual < yres && yres_virtual > 0)
		yres_virtual = yres;

	par->hw.falcon.line_width = bpp * xres / 16;
	par->hw.falcon.line_offset = bpp * (xres_virtual - xres) / 16;

	/* single or double pixel width */
	xstretch = (xres == 320) ? 2 : 1;

	/* Default values are used for vert./hor. timing if no pixelclock given. */
	if (var->pixclock == 0)
		use_default_timing = 1;

#if 0 /* currently unused */
	if (mon_type == F_MON_SM) {
		if (xres != 640 && yres != 400)
			return -EINVAL;
		plen = 1;
		pclock = &f32;
		/* SM124-mode is special */
		par->hw.falcon.ste_mode = 1;
		par->hw.falcon.f_shift = 0x000;
		par->hw.falcon.st_shift = 0x200;
		left_margin = hsync_len = 128 / plen;
		right_margin = 0;
		/* TODO set all margins */
	}
	else if (mon_type == F_MON_SC || mon_type == F_MON_TV) {
		plen = 2 * xstretch;
		pclock = &f32;
		hsync_len = 150 / plen;
		if (yres > 240)
			interlace = 1;
		/* TODO set margins */
	}
	else
#endif
	{	/* F_MON_VGA */
		if (bpp == 16)
			xstretch = 2; /* hicolor only double pixel width */
		if (use_default_timing) {
			int linesize;

			/* Choose master pixelclock depending on hor. timing */
			plen = 1 * xstretch;
			if ((plen * xres + f25.right+f25.hsync+f25.left) * hfmin < f25.f)
				pclock = &f25;
			else if ((plen * xres + f32.right+f32.hsync+f32.left) * hfmin < f32.f)
				pclock = &f32;
			else if ((plen * xres + fext.right+fext.hsync+fext.left) * hfmin < fext.f
			         && fext.f)
				pclock = &fext;
			else
				return -EINVAL;

			left_margin = pclock->left / plen;
			right_margin = pclock->right / plen;
			hsync_len = pclock->hsync / plen;
			linesize = left_margin + xres + right_margin + hsync_len;
			upper_margin = 31;
			lower_margin = 11;
			vsync_len = 3;
		}
		else {
#if 0 /* TODO enable this (untested yet) */
			/* Round down pixelclock */
			int i; unsigned long pcl=0;
			for (i=1; i<=4; i*=2) {
				if (f25.t*i<=var->pixclock && pcl<f25.t*i) {
					pcl=f25.t*i; pclock=&f25;
				}
				if (f32.t*i<=var->pixclock && pcl<f32.t*i) {
					pcl=f32.t*i; pclock=&f32;
				}
				if (fext.t && fext.t*i<=var->pixclock && pcl<fext.t*i) {
					pcl=fext.t*i; pclock=&fext;
				}
			}
			if (!pcl)
				return -EINVAL;
			plen = pcl / pclock->t;

#else
			if (var->pixclock == f25.t || var->pixclock == 2*f25.t)
				pclock = &f25;
			else if (var->pixclock == f32.t || var->pixclock == 2*f32.t)
				pclock = &f32;
			else if ((var->pixclock == fext.t || var->pixclock == 2*fext.t) && fext.t) {
				pclock = &fext;
			}
			else
				return -EINVAL;
			plen = var->pixclock / pclock->t;
#endif

			left_margin = var->left_margin;
			right_margin = var->right_margin;
			hsync_len = var->hsync_len;
			upper_margin = var->upper_margin;
			lower_margin = var->lower_margin;
			vsync_len = var->vsync_len;
			if (var->vmode & FB_VMODE_INTERLACED) {
				/* # lines in half frame */
				upper_margin = (upper_margin + 1) / 2;
				lower_margin = (lower_margin + 1) / 2;
				vsync_len = (vsync_len + 1) / 2;
			}
		}
		if (pclock == &fext)
			longoffset = 1; /* VIDEL doesn't synchronize on short offset */
	}
	/* Is video bus bandwidth (32MB/s) too low for this resolution? */
	/* this is definitely wrong if bus clock != 32MHz */
	if (pclock->f / plen / 8 * bpp > 32000000L)
		return -EINVAL;

	if (vsync_len < 1)
		vsync_len = 1;

	/* include sync lengths in right/lower margin for all calculations */
	right_margin += hsync_len;
	lower_margin += vsync_len;

	/* ! In all calculations of margins we use # of lines in half frame
	 * (which is a full frame in non-interlace mode), so we can switch
	 * between interlace and non-interlace without messing around
	 * with these.
	 */
  again:
	/* Set base_offset 128 and video bus width */
	par->hw.falcon.vid_control = mon_type | f030_bus_width;
	if (!longoffset)
		par->hw.falcon.vid_control |= VCO_SHORTOFFS;	/* base_offset 64 */
	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		par->hw.falcon.vid_control |= VCO_HSYPOS;
	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		par->hw.falcon.vid_control |= VCO_VSYPOS;
	/* Pixelclock */
	par->hw.falcon.vid_control |= pclock->control_mask;
	/* External or internal clock */
	par->hw.falcon.sync = pclock->sync_mask | 0x2;
	/* Pixellength and prescale */
	par->hw.falcon.vid_mode = (2/plen) << 2;
	if (doubleline)
		par->hw.falcon.vid_mode |= VMO_DOUBLE;
	if (interlace)
		par->hw.falcon.vid_mode |= VMO_INTER;

	/*********************
	Horizontal timing: unit = [master clock cycles]
	unit of hxx-registers: [master clock cycles * prescale]
	Hxx-registers are 9-bit wide

	1 line = ((hht + 2) * 2 * prescale) clock cycles

	graphic output = hdb & 0x200 ?
	       ((hht+2)*2 - hdb + hde) * prescale - hdboff + hdeoff:
	       ( hht + 2  - hdb + hde) * prescale - hdboff + hdeoff
	(this must be a multiple of plen*128/bpp, on VGA pixels
	 to the right may be cut off with a bigger right margin)

	start of graphics relative to start of 1st halfline = hdb & 0x200 ?
	       (hdb - hht - 2) * prescale + hdboff :
	       hdb * prescale + hdboff

	end of graphics relative to start of 1st halfline =
	       (hde + hht + 2) * prescale + hdeoff
	*********************/
	/* Calculate VIDEL registers */
	{
	int hdb_off, hde_off, base_off;
	int gstart, gend1, gend2, align;

	prescale = hxx_prescale(&par->hw.falcon);
	base_off = par->hw.falcon.vid_control & VCO_SHORTOFFS ? 64 : 128;

	/* Offsets depend on video mode */
	/* Offsets are in clock cycles, divide by prescale to
	 * calculate hd[be]-registers
	 */
	if (par->hw.falcon.f_shift & 0x100) {
		align = 1;
		hde_off = 0;
		hdb_off = (base_off + 16 * plen) + prescale;
	}
	else {
		align = 128 / bpp;
		hde_off = ((128 / bpp + 2) * plen);
		if (par->hw.falcon.ste_mode)
			hdb_off = (64 + base_off + (128 / bpp + 2) * plen) + prescale;
		else
			hdb_off = (base_off + (128 / bpp + 18) * plen) + prescale;
	}

	gstart = (prescale/2 + plen * left_margin) / prescale;
	/* gend1 is for hde (gend-gstart multiple of align), shifter's xres */
	gend1 = gstart + ((xres + align-1) / align)*align * plen / prescale;
	/* gend2 is for hbb, visible xres (rest to gend1 is cut off by hblank) */
	gend2 = gstart + xres * plen / prescale;
	par->HHT = plen * (left_margin + xres + right_margin) /
			   (2 * prescale) - 2;
/*	par->HHT = (gend2 + plen * right_margin / prescale) / 2 - 2;*/

	par->HDB = gstart - hdb_off/prescale;
	par->HBE = gstart;
	if (par->HDB < 0) par->HDB += par->HHT + 2 + 0x200;
	par->HDE = gend1 - par->HHT - 2 - hde_off/prescale;
	par->HBB = gend2 - par->HHT - 2;
#if 0
	/* One more Videl constraint: data fetch of two lines must not overlap */
	if (par->HDB & 0x200  &&  par->HDB & ~0x200 - par->HDE <= 5) {
		/* if this happens increase margins, decrease hfreq. */
	}
#endif
	if (hde_off % prescale)
		par->HBB++;		/* compensate for non matching hde and hbb */
	par->HSS = par->HHT + 2 - plen * hsync_len / prescale;
	if (par->HSS < par->HBB)
		par->HSS = par->HBB;
	}

	/*  check hor. frequency */
	hfreq = pclock->f / ((par->HHT+2)*prescale*2);
	if (hfreq > hfmax && mon_type!=F_MON_VGA) {
		/* ++guenther:   ^^^^^^^^^^^^^^^^^^^ can't remember why I did this */
		/* Too high -> enlarge margin */
		left_margin += 1;
		right_margin += 1;
		goto again;
	}
	if (hfreq > hfmax || hfreq < hfmin)
		return -EINVAL;

	/* Vxx-registers */
	/* All Vxx must be odd in non-interlace, since frame starts in the middle
	 * of the first displayed line!
	 * One frame consists of VFT+1 half lines. VFT+1 must be even in
	 * non-interlace, odd in interlace mode for synchronisation.
	 * Vxx-registers are 11-bit wide
	 */
	par->VBE = (upper_margin * 2 + 1); /* must begin on odd halfline */
	par->VDB = par->VBE;
	par->VDE = yres;
	if (!interlace) par->VDE <<= 1;
	if (doubleline) par->VDE <<= 1;  /* VDE now half lines per (half-)frame */
	par->VDE += par->VDB;
	par->VBB = par->VDE;
	par->VFT = par->VBB + (lower_margin * 2 - 1) - 1;
	par->VSS = par->VFT+1 - (vsync_len * 2 - 1);
	/* vbb,vss,vft must be even in interlace mode */
	if (interlace) {
		par->VBB++;
		par->VSS++;
		par->VFT++;
	}

	/* V-frequency check, hope I didn't create any loop here. */
	/* Interlace and doubleline are mutually exclusive. */
	vfreq = (hfreq * 2) / (par->VFT + 1);
	if      (vfreq > vfmax && !doubleline && !interlace) {
		/* Too high -> try again with doubleline */
		doubleline = 1;
		goto again;
	}
	else if (vfreq < vfmin && !interlace && !doubleline) {
		/* Too low -> try again with interlace */
		interlace = 1;
		goto again;
	}
	else if (vfreq < vfmin && doubleline) {
		/* Doubleline too low -> clear doubleline and enlarge margins */
		int lines;
		doubleline = 0;
		for (lines=0; (hfreq*2)/(par->VFT+1+4*lines-2*yres)>vfmax; lines++)
			;
		upper_margin += lines;
		lower_margin += lines;
		goto again;
	}
	else if (vfreq > vfmax && interlace) {
		/* Interlace, too high -> enlarge margins */
		int lines;
		for (lines=0; (hfreq*2)/(par->VFT+1+4*lines)>vfmax; lines++)
			;
		upper_margin += lines;
		lower_margin += lines;
		goto again;
	}
	else if (vfreq < vfmin || vfreq > vfmax)
		return -EINVAL;

  set_screen_base:
	linelen = xres_virtual * bpp / 8;
	if ((var->yoffset + yres)*linelen > screen_len && screen_len)
		return -EINVAL;
	if (var->yres_virtual * linelen > screen_len && screen_len)
		return -EINVAL;
	if (var->yres * linelen > screen_len && screen_len)
		return -EINVAL;
	par->vyres = yres_virtual;
	par->screen_base = screen_base + var->yoffset * linelen;
	par->hw.falcon.xoffset = 0;

	return 0;
}

static int falcon_encode_var( struct fb_var_screeninfo *var,
							  struct atari_fb_par *par )
{
/* !!! only for VGA !!! */
	int linelen, i;
	int prescale, plen;
	int hdb_off, hde_off, base_off;
	struct falcon_hw *hw = &par->hw.falcon;

	/* possible frequencies: 25.175 or 32MHz */
	var->pixclock = hw->sync & 0x1 ? fext.t :
	                hw->vid_control & VCO_CLOCK25 ? f25.t : f32.t;

	var->height=-1;
	var->width=-1;

	var->sync=0;
	if (hw->vid_control & VCO_HSYPOS)
		var->sync |= FB_SYNC_HOR_HIGH_ACT;
	if (hw->vid_control & VCO_VSYPOS)
		var->sync |= FB_SYNC_VERT_HIGH_ACT;

	var->vmode = FB_VMODE_NONINTERLACED;
	if (hw->vid_mode & VMO_INTER)
		var->vmode |= FB_VMODE_INTERLACED;
	if (hw->vid_mode & VMO_DOUBLE)
		var->vmode |= FB_VMODE_DOUBLE;
	
	/* visible y resolution:
	 * Graphics display starts at line VDB and ends at line
	 * VDE. If interlace mode off unit of VC-registers is
	 * half lines, else lines.
	 */
	var->yres = hw->vde - hw->vdb;
	if (!(var->vmode & FB_VMODE_INTERLACED))
		var->yres >>= 1;
	if (var->vmode & FB_VMODE_DOUBLE)
		var->yres >>= 1;

	/* to get bpp, we must examine f_shift and st_shift.
	 * f_shift is valid if any of bits no. 10, 8 or 4
	 * is set. Priority in f_shift is: 10 ">" 8 ">" 4, i.e.
	 * if bit 10 set then bit 8 and bit 4 don't care...
	 * If all these bits are 0 get display depth from st_shift
	 * (as for ST and STE)
	 */
	if (hw->f_shift & 0x400)		/* 2 colors */
		var->bits_per_pixel = 1;
	else if (hw->f_shift & 0x100)	/* hicolor */
		var->bits_per_pixel = 16;
	else if (hw->f_shift & 0x010)	/* 8 bitplanes */
		var->bits_per_pixel = 8;
	else if (hw->st_shift == 0)
		var->bits_per_pixel = 4;
	else if (hw->st_shift == 0x100)
		var->bits_per_pixel = 2;
	else /* if (hw->st_shift == 0x200) */
		var->bits_per_pixel = 1;

	var->xres = hw->line_width * 16 / var->bits_per_pixel;
	var->xres_virtual = var->xres + hw->line_offset * 16 / var->bits_per_pixel;
	if (hw->xoffset)
		var->xres_virtual += 16;

	if (var->bits_per_pixel == 16) {
		var->red.offset=11;
		var->red.length=5;
		var->red.msb_right=0;
		var->green.offset=5;
		var->green.length=6;
		var->green.msb_right=0;
		var->blue.offset=0;
		var->blue.length=5;
		var->blue.msb_right=0;
	}
	else {
		var->red.offset=0;
		var->red.length = hw->ste_mode ? 4 : 6;
		var->red.msb_right=0;
		var->grayscale=0;
		var->blue=var->green=var->red;
	}
	var->transp.offset=0;
	var->transp.length=0;
	var->transp.msb_right=0;

	linelen = var->xres_virtual * var->bits_per_pixel / 8;
	if (screen_len)
		if (par->vyres)
			var->yres_virtual = par->vyres;
		else
			var->yres_virtual=screen_len/linelen;
	else {
		if (hwscroll < 0)
			var->yres_virtual = 2 * var->yres;
		else
			var->yres_virtual=var->yres+hwscroll * 16;
	}
	var->xoffset=0; /* TODO change this */

	/* hdX-offsets */
	prescale = hxx_prescale(hw);
	plen = 4 >> (hw->vid_mode >> 2 & 0x3);
	base_off = hw->vid_control & VCO_SHORTOFFS ? 64 : 128;
	if (hw->f_shift & 0x100) {
		hde_off = 0;
		hdb_off = (base_off + 16 * plen) + prescale;
	}
	else {
		hde_off = ((128 / var->bits_per_pixel + 2) * plen);
		if (hw->ste_mode)
			hdb_off = (64 + base_off + (128 / var->bits_per_pixel + 2) * plen)
					 + prescale;
		else
			hdb_off = (base_off + (128 / var->bits_per_pixel + 18) * plen)
					 + prescale;
	}

	/* Right margin includes hsync */
	var->left_margin = hdb_off + prescale * ((hw->hdb & 0x1ff) -
					   (hw->hdb & 0x200 ? 2+hw->hht : 0));
	if (hw->ste_mode || mon_type!=F_MON_VGA)
		var->right_margin = prescale * (hw->hht + 2 - hw->hde) - hde_off;
	else
		/* can't use this in ste_mode, because hbb is +1 off */
		var->right_margin = prescale * (hw->hht + 2 - hw->hbb);
	var->hsync_len = prescale * (hw->hht + 2 - hw->hss);

	/* Lower margin includes vsync */
	var->upper_margin = hw->vdb / 2 ;  /* round down to full lines */
	var->lower_margin = (hw->vft+1 - hw->vde + 1) / 2; /* round up */
	var->vsync_len    = (hw->vft+1 - hw->vss + 1) / 2; /* round up */
	if (var->vmode & FB_VMODE_INTERLACED) {
		var->upper_margin *= 2;
		var->lower_margin *= 2;
		var->vsync_len *= 2;
	}

	var->pixclock *= plen;
	var->left_margin /= plen;
	var->right_margin /= plen;
	var->hsync_len /= plen;

	var->right_margin -= var->hsync_len;
	var->lower_margin -= var->vsync_len;

	if (screen_base)
		var->yoffset=(par->screen_base - screen_base)/linelen;
	else
		var->yoffset=0;
	var->nonstd=0;	/* what is this for? */
	var->activate=0;
	for (i=0; i<arraysize(var->reserved); i++)
		var->reserved[i]=0;
	return 0;
}


static int f_change_mode = 0;
static struct falcon_hw f_new_mode;
static int f_pan_display = 0;

static void falcon_get_par( struct atari_fb_par *par )
{
	unsigned long addr;
	struct falcon_hw *hw = &par->hw.falcon;

	hw->line_width = shifter_f030.scn_width;
	hw->line_offset = shifter_f030.off_next;
	hw->st_shift = videl.st_shift & 0x300;
	hw->f_shift = videl.f_shift;
	hw->vid_control = videl.control;
	hw->vid_mode = videl.mode;
	hw->sync = shifter.syncmode & 0x1;
	hw->xoffset = videl.xoffset & 0xf;
	hw->hht = videl.hht;
	hw->hbb = videl.hbb;
	hw->hbe = videl.hbe;
	hw->hdb = videl.hdb;
	hw->hde = videl.hde;
	hw->hss = videl.hss;
	hw->vft = videl.vft;
	hw->vbb = videl.vbb;
	hw->vbe = videl.vbe;
	hw->vdb = videl.vdb;
	hw->vde = videl.vde;
	hw->vss = videl.vss;

	addr = (shifter.bas_hi & 0xff) << 16 |
	       (shifter.bas_md & 0xff) << 8  |
	       (shifter.bas_lo & 0xff);
	par->screen_base = PTOV(addr);

	/* derived parameters */
	hw->ste_mode = (hw->f_shift & 0x510)==0 && hw->st_shift==0x100;
	hw->mono = (hw->f_shift & 0x400) ||
	           ((hw->f_shift & 0x510)==0 && hw->st_shift==0x200);
}

static void falcon_set_par( struct atari_fb_par *par )
{
	f_change_mode = 0;

	/* only set screen_base if really necessary */
	if (current_par.screen_base != par->screen_base)
		fbhw->set_screen_base(par->screen_base);

	/* Don't touch any other registers if we keep the default resolution */
	if (DontCalcRes)
		return;

	/* Tell vbl-handler to change video mode.
	 * We change modes only on next VBL, to avoid desynchronisation
	 * (a shift to the right and wrap around by a random number of pixels
	 * in all monochrome modes).
	 * This seems to work on my Falcon.
	 */
	f_new_mode = par->hw.falcon;
	f_change_mode = 1;
}


static void falcon_vbl_switcher( int irq, struct pt_regs *fp, void *dummy )
{
	struct falcon_hw *hw = &f_new_mode;

	if (f_change_mode) {
		f_change_mode = 0;

		if (hw->sync & 0x1) {
			/* Enable external pixelclock. This code only for ScreenWonder */
			*(volatile unsigned short*)0xffff9202 = 0xffbf;
		}
		else {
			/* Turn off external clocks. Read sets all output bits to 1. */
			*(volatile unsigned short*)0xffff9202;
		}
		shifter.syncmode = hw->sync;

		videl.hht = hw->hht;
		videl.hbb = hw->hbb;
		videl.hbe = hw->hbe;
		videl.hdb = hw->hdb;
		videl.hde = hw->hde;
		videl.hss = hw->hss;
		videl.vft = hw->vft;
		videl.vbb = hw->vbb;
		videl.vbe = hw->vbe;
		videl.vdb = hw->vdb;
		videl.vde = hw->vde;
		videl.vss = hw->vss;

		/*f030_sreg[2] = 0;*/

		videl.f_shift = 0; /* write enables Falcon palette, 0: 4 planes */
		if (hw->ste_mode) {
			videl.st_shift = hw->st_shift; /* write enables STE palette */
		}
		else {
			/* IMPORTANT:
			 * set st_shift 0, so we can tell the screen-depth if f_shift==0.
			 * Writing 0 to f_shift enables 4 plane Falcon mode but
			 * doesn't set st_shift. st_shift!=0 (!=4planes) is impossible
			 * with Falcon palette.
			 */
			videl.st_shift = 0;
			/* now back to Falcon palette mode */
			videl.f_shift = hw->f_shift;
		}
		/* writing to st_shift changed scn_width and vid_mode */
		videl.xoffset = hw->xoffset;
		shifter_f030.scn_width = hw->line_width;
		shifter_f030.off_next = hw->line_offset;
		videl.control = hw->vid_control;
		videl.mode = hw->vid_mode;
	}
	if (f_pan_display) {
		f_pan_display = 0;
		videl.xoffset = current_par.hw.falcon.xoffset;
		shifter_f030.off_next = current_par.hw.falcon.line_offset;
	}
}


static int falcon_pan_display( struct fb_var_screeninfo *var,
							   struct atari_fb_par *par )
{
	int xoffset;

	if (disp[currcon].var.bits_per_pixel == 1)
		var->xoffset = up(var->xoffset, 32);
	par->hw.falcon.xoffset = var->xoffset & 15;
	par->hw.falcon.line_offset = disp[currcon].var.bits_per_pixel *
	       	  (disp[currcon].var.xres_virtual - disp[currcon].var.xres) / 16;
	if (par->hw.falcon.xoffset)
		par->hw.falcon.line_offset -= disp[currcon].var.bits_per_pixel;
	xoffset = var->xoffset - par->hw.falcon.xoffset;

	par->screen_base
		= screen_base + (var->yoffset * disp[currcon].var.xres_virtual +
				xoffset) * disp[currcon].var.bits_per_pixel / 8;
	if (fbhw->set_screen_base)
		fbhw->set_screen_base (par->screen_base);
	else
		return -EINVAL; /* shouldn't happen */
	f_pan_display = 1;
	return 0;
}


static int falcon_getcolreg( unsigned regno, unsigned *red,
				 unsigned *green, unsigned *blue,
				 unsigned *transp )
{	unsigned long col;
	
	if (regno > 255)
		return 1;
	/* This works in STE-mode (with 4bit/color) since f030_col-registers
	 * hold up to 6bit/color.
	 * Even with hicolor r/g/b=5/6/5 bit!
	 */
	col = f030_col[regno];
	*red = (col >> 26) & 0x3f;
	*green = (col >> 18) & 0x3f;
	*blue = (col >> 2) & 0x3f;
	*transp = 0;
	return 0;
}


static int falcon_setcolreg( unsigned regno, unsigned red,
							 unsigned green, unsigned blue,
							 unsigned transp )
{
	if (regno > 255)
		return 1;
	f030_col[regno] = (red << 26) | (green << 18) | (blue << 2);
	if (regno < 16) {
		shifter_tt.color_reg[regno] =
			(((red & 0xe) >> 1) | ((red & 1) << 3) << 8) |
			(((green & 0xe) >> 1) | ((green & 1) << 3) << 4) |
			((blue & 0xe) >> 1) | ((blue & 1) << 3);
		packed16_cmap[regno] = (red << 11) | (green << 5) | blue;
	}
	return 0;
}


static int falcon_blank( int blank_mode )
{
/* ++guenther: we can switch off graphics by changing VDB and VDE,
 * so VIDEL doesn't hog the bus while saving.
 * (this affects usleep()).
 */
	if (mon_type == F_MON_SM)	/* this doesn't work on SM124 */
		return 1;
	if (blank_mode) {
		/* disable graphics output (this speeds up the CPU) ... */
		videl.vdb = current_par.VFT + 1;
		/* ... and blank all lines */
		videl.hbe = current_par.HHT + 2;
		/* VESA suspend mode, switch off HSYNC */
		if (pwrsave && mon_type == F_MON_VGA)
			videl.hss = current_par.HHT + 2;
	}
	else {
		videl.vdb = current_par.VDB;
		videl.hbe = current_par.HBE;
		videl.hss = current_par.HSS;
	}
	return 0;
}

 
static int falcon_detect( void )
{
	struct atari_fb_par par;
	unsigned char fhw;

	/* Determine connected monitor and set monitor parameters */
	fhw = *(unsigned char*)0xffff8006;
	mon_type = fhw >> 6 & 0x3;
	/* bit 1 of fhw: 1=32 bit ram bus, 0=16 bit */
	f030_bus_width = fhw << 6 & 0x80;
	switch (mon_type) {
	case F_MON_SM:
		vfmin = 70;
		vfmax = 72;
		hfmin = 35713;
		hfmax = 35715;
		break;
	case F_MON_SC:
	case F_MON_TV:
		vfmin = 50;
		vfmax = 60;
		hfmin = 15624;
		hfmax = 15626;
		break;
	}
	/* initialize hsync-len */
	f25.hsync = h_syncs[mon_type] / f25.t;
	f32.hsync = h_syncs[mon_type] / f32.t;
	if (fext.t)
		fext.hsync = h_syncs[mon_type] / fext.t;

	falcon_get_par(&par);
	falcon_encode_var(&atari_fb_predefined[0], &par);

	/* Detected mode is always the "autodetect" slot */
	return 1;
}

#endif /* ATAFB_FALCON */

/* ------------------- ST(E) specific functions ---------------------- */

#ifdef ATAFB_STE

static int stste_encode_fix( struct fb_fix_screeninfo *fix,
							 struct atari_fb_par *par )

{
	int mode, i;

	strcpy(fix->id,"Atari Builtin");
	fix->smem_start=real_screen_base;
	fix->smem_len=screen_len;
	fix->type=FB_TYPE_INTERLEAVED_PLANES;
	fix->type_aux=2;
	fix->visual=FB_VISUAL_PSEUDOCOLOR;
	mode = par->hw.st.mode & 3;
	if (mode == ST_HIGH) {
		fix->type=FB_TYPE_PACKED_PIXELS;
		fix->type_aux=0;
		fix->visual=FB_VISUAL_MONO10;
	}
	fix->xpanstep = 0;
	if (ATARIHW_PRESENT(EXTD_SHIFTER))
		fix->ypanstep = 1;
	else
		fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = 0;
	for (i=0; i<arraysize(fix->reserved); i++)
		fix->reserved[i]=0;
	return 0;
}


static int stste_decode_var( struct fb_var_screeninfo *var,
						  struct atari_fb_par *par )
{
	int xres=var->xres;
	int yres=var->yres;
	int bpp=var->bits_per_pixel;
	int linelen;

	if (mono_moni) {
		if (bpp > 1 || xres > sttt_xres || yres > st_yres)
			return -EINVAL;
		par->hw.st.mode=ST_HIGH;
		xres=sttt_xres;
		yres=st_yres;
		bpp=1;
	} else {
		if (bpp > 4 || xres > sttt_xres || yres > st_yres)
			return -EINVAL;
		if (bpp > 2) {
			if (xres > sttt_xres/2 || yres > st_yres/2)
				return -EINVAL;
			par->hw.st.mode=ST_LOW;
			xres=sttt_xres/2;
			yres=st_yres/2;
			bpp=4;
		}
		else if (bpp > 1) {
			if (xres > sttt_xres || yres > st_yres/2)
				return -EINVAL;
			par->hw.st.mode=ST_MID;
			xres=sttt_xres;
			yres=st_yres/2;
			bpp=2;
		}
		else
			return -EINVAL;
	}
	if (var->sync & FB_SYNC_EXT)
		par->hw.st.sync=(par->hw.st.sync & ~1) | 1;
	else
		par->hw.st.sync=(par->hw.st.sync & ~1);
	linelen=xres*bpp/8;
	if ((var->yoffset + yres)*linelen > screen_len && screen_len)
		return -EINVAL;
	par->screen_base=screen_base+ var->yoffset*linelen;
	return 0;
}

static int stste_encode_var( struct fb_var_screeninfo *var,
						  struct atari_fb_par *par )
{
	int linelen, i;
	var->red.offset=0;
	var->red.length = ATARIHW_PRESENT(EXTD_SHIFTER) ? 4 : 3;
	var->red.msb_right=0;
	var->grayscale=0;

	var->pixclock=31041;
	var->left_margin=120;		/* these are incorrect */
	var->right_margin=100;
	var->upper_margin=8;
	var->lower_margin=16;
	var->hsync_len=140;
	var->vsync_len=30;

	var->height=-1;
	var->width=-1;

	if (!(par->hw.st.sync & 1))
		var->sync=0;
	else
		var->sync=FB_SYNC_EXT;

	switch (par->hw.st.mode & 3) {
	case ST_LOW:
		var->xres=sttt_xres/2;
		var->yres=st_yres/2;
		var->bits_per_pixel=4;
		break;
	case ST_MID:
		var->xres=sttt_xres;
		var->yres=st_yres/2;
		var->bits_per_pixel=2;
		break;
	case ST_HIGH:
		var->xres=sttt_xres;
		var->yres=st_yres;
		var->bits_per_pixel=1;
		break;
	}		
	var->blue=var->green=var->red;
	var->transp.offset=0;
	var->transp.length=0;
	var->transp.msb_right=0;
	var->xres_virtual=sttt_xres_virtual;
	linelen=var->xres_virtual * var->bits_per_pixel / 8;
	ovsc_addlen=linelen*(sttt_yres_virtual - st_yres);
	
	if (! use_hwscroll)
		var->yres_virtual=var->yres;
	else if (screen_len)
		var->yres_virtual=screen_len/linelen;
	else {
		if (hwscroll < 0)
			var->yres_virtual = 2 * var->yres;
		else
			var->yres_virtual=var->yres+hwscroll * 16;
	}
	var->xoffset=0;
	if (screen_base)
		var->yoffset=(par->screen_base - screen_base)/linelen;
	else
		var->yoffset=0;
	var->nonstd=0;
	var->activate=0;
	var->vmode=FB_VMODE_NONINTERLACED;
	for (i=0; i<arraysize(var->reserved); i++)
		var->reserved[i]=0;
	return 0;
}


static void stste_get_par( struct atari_fb_par *par )
{
	unsigned long addr;
	par->hw.st.mode=shifter_tt.st_shiftmode;
	par->hw.st.sync=shifter.syncmode;
	addr = ((shifter.bas_hi & 0xff) << 16) |
	       ((shifter.bas_md & 0xff) << 8);
	if (ATARIHW_PRESENT(EXTD_SHIFTER))
		addr |= (shifter.bas_lo & 0xff);
	par->screen_base = PTOV(addr);
}

static void stste_set_par( struct atari_fb_par *par )
{
	shifter_tt.st_shiftmode=par->hw.st.mode;
	shifter.syncmode=par->hw.st.sync;
	/* only set screen_base if really necessary */
	if (current_par.screen_base != par->screen_base)
		fbhw->set_screen_base(par->screen_base);
}


static int stste_getcolreg( unsigned regno, unsigned *red,
							unsigned *green, unsigned *blue,
							unsigned *transp )
{	unsigned col;
	
	if (regno > 15)
		return 1;
	col = shifter_tt.color_reg[regno];
	if (ATARIHW_PRESENT(EXTD_SHIFTER)) {
		*red = ((col >> 7) & 0xe) | ((col >> 11) & 1);
		*green = ((col >> 3) & 0xe) | ((col >> 7) & 1);
		*blue = ((col << 1) & 0xe) | ((col >> 3) & 1);
	}
	else {
		*red = (col >> 8) & 0x7;
		*green = (col >> 4) & 0x7;
		*blue = col & 0x7;
	}
	*transp = 0;
	return 0;
}


static int stste_setcolreg( unsigned regno, unsigned red,
						 unsigned green, unsigned blue,
						 unsigned transp )
{
	if (regno > 15)
		return 1;
	if (ATARIHW_PRESENT(EXTD_SHIFTER))
		shifter_tt.color_reg[regno] =
			(((red & 0xe) >> 1) | ((red & 1) << 3) << 8) |
			(((green & 0xe) >> 1) | ((green & 1) << 3) << 4) |
			((blue & 0xe) >> 1) | ((blue & 1) << 3);
	else
		shifter_tt.color_reg[regno] =
			((red & 0x7) << 8) |
			((green & 0x7) << 4) |
			(blue & 0x7);
	return 0;
}

						  
static int stste_detect( void )

{	struct atari_fb_par par;

	/* Determine the connected monitor: The DMA sound must be
	 * disabled before reading the MFP GPIP, because the Sound
	 * Done Signal and the Monochrome Detect are XORed together!
	 */
	if (ATARIHW_PRESENT(PCM_8BIT)) {
		tt_dmasnd.ctrl = DMASND_CTRL_OFF;
		udelay(20);	/* wait a while for things to settle down */
	}
	mono_moni = (mfp.par_dt_reg & 0x80) == 0;

	stste_get_par(&par);
	stste_encode_var(&atari_fb_predefined[0], &par);

	if (!ATARIHW_PRESENT(EXTD_SHIFTER))
		use_hwscroll = 0;
	return 1;
}

static void stste_set_screen_base(unsigned long s_base)
{
	unsigned long addr;
	addr= VTOP(s_base);
	/* Setup Screen Memory */
	shifter.bas_hi=(unsigned char) ((addr & 0xff0000) >> 16);
  	shifter.bas_md=(unsigned char) ((addr & 0x00ff00) >> 8);
	if (ATARIHW_PRESENT(EXTD_SHIFTER))
		shifter.bas_lo=(unsigned char)  (addr & 0x0000ff);
}

#endif /* ATAFB_STE */

/* Switching the screen size should be done during vsync, otherwise
 * the margins may get messed up. This is a well known problem of
 * the ST's video system.
 *
 * Unfortunately there is hardly any way to find the vsync, as the
 * vertical blank interrupt is no longer in time on machines with
 * overscan type modifications.
 *
 * We can, however, use Timer B to safely detect the black shoulder,
 * but then we've got to guess an appropriate delay to find the vsync.
 * This might not work on every machine.
 *
 * martin_rogge @ ki.maus.de, 8th Aug 1995
 */

#define LINE_DELAY  (mono_moni ? 30 : 70)
#define SYNC_DELAY  (mono_moni ? 1500 : 2000)

/* SWITCH_ACIA may be used for Falcon (ScreenBlaster III internal!) */
static void st_ovsc_switch(int switchmode)
{
    unsigned long flags;
    register unsigned char old, new;

    if ((switchmode & (SWITCH_ACIA | SWITCH_SND6 | SWITCH_SND7)) == 0)
	return;
    save_flags(flags);
    cli();

    mfp.tim_ct_b = 0x10;
    mfp.active_edge |= 8;
    mfp.tim_ct_b = 0;
    mfp.tim_dt_b = 0xf0;
    mfp.tim_ct_b = 8;
    while (mfp.tim_dt_b > 1)	/* TOS does it this way, don't ask why */
	;
    new = mfp.tim_dt_b;
    do {
	udelay(LINE_DELAY);
	old = new;
	new = mfp.tim_dt_b;
    } while (old != new);
    mfp.tim_ct_b = 0x10;
    udelay(SYNC_DELAY);

    if (switchmode == SWITCH_ACIA)
	acia.key_ctrl = (ACIA_DIV64|ACIA_D8N1S|ACIA_RHTID|ACIA_RIE);
    else {
	sound_ym.rd_data_reg_sel = 14;
	sound_ym.wd_data = sound_ym.rd_data_reg_sel | switchmode;
    }
    restore_flags(flags);
}

/* ------------------- External Video ---------------------- */

#ifdef ATAFB_EXT

static int ext_encode_fix( struct fb_fix_screeninfo *fix,
						   struct atari_fb_par *par )

{
	int i;

	strcpy(fix->id,"Unknown Extern");
	fix->smem_start=external_addr;
	fix->smem_len=(external_len + PAGE_SIZE -1) & PAGE_MASK;
	if (external_depth == 1) {
		fix->type = FB_TYPE_PACKED_PIXELS;
		/* The letters 'n' and 'i' in the "atavideo=external:" stand
		 * for "normal" and "inverted", rsp., in the monochrome case */
		fix->visual =
			(external_pmode == FB_TYPE_INTERLEAVED_PLANES ||
			 external_pmode == FB_TYPE_PACKED_PIXELS) ?
				FB_VISUAL_MONO10 :
					FB_VISUAL_MONO01;
	}
	else {
		switch (external_pmode) {
			/* All visuals are STATIC, because we don't know how to change
			 * colors :-(
			 */
		    case -1:              /* truecolor */
			fix->type=FB_TYPE_PACKED_PIXELS;
			fix->visual=FB_VISUAL_TRUECOLOR;
			break;
		    case FB_TYPE_PACKED_PIXELS:
			fix->type=FB_TYPE_PACKED_PIXELS;
			fix->visual=FB_VISUAL_STATIC_PSEUDOCOLOR;
			break;
		    case FB_TYPE_PLANES:
			fix->type=FB_TYPE_PLANES;
			fix->visual=FB_VISUAL_STATIC_PSEUDOCOLOR;
			break;
		    case FB_TYPE_INTERLEAVED_PLANES:
			fix->type=FB_TYPE_INTERLEAVED_PLANES;
			fix->type_aux=2;
			fix->visual=FB_VISUAL_STATIC_PSEUDOCOLOR;
			break;
		}
	}
	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = 0;
	for (i=0; i<arraysize(fix->reserved); i++)
		fix->reserved[i]=0;
	return 0;
}


static int ext_decode_var( struct fb_var_screeninfo *var,
						   struct atari_fb_par *par )
{
	struct fb_var_screeninfo *myvar = &atari_fb_predefined[0];
	
	if (var->bits_per_pixel > myvar->bits_per_pixel ||
		var->xres > myvar->xres ||
		var->yres > myvar->yres ||
		var->xoffset > 0 ||
		var->yoffset > 0)
		return -EINVAL;
	return 0;
}


static int ext_encode_var( struct fb_var_screeninfo *var,
						   struct atari_fb_par *par )
{
	int i;

	var->red.offset=0;
	var->red.length=(external_pmode == -1) ? external_depth/3 : 
			(external_vgaiobase ? external_bitspercol : 0);
	var->red.msb_right=0;
	var->grayscale=0;

	var->pixclock=31041;
	var->left_margin=120;		/* these are surely incorrect 	*/
	var->right_margin=100;
	var->upper_margin=8;
	var->lower_margin=16;
	var->hsync_len=140;
	var->vsync_len=30;

	var->height=-1;
	var->width=-1;

	var->sync=0;

	var->xres = external_xres;
	var->yres = external_yres;
	var->bits_per_pixel = external_depth;
	
	var->blue=var->green=var->red;
	var->transp.offset=0;
	var->transp.length=0;
	var->transp.msb_right=0;
	var->xres_virtual=var->xres;
	var->yres_virtual=var->yres;
	var->xoffset=0;
	var->yoffset=0;
	var->nonstd=0;
	var->activate=0;
	var->vmode=FB_VMODE_NONINTERLACED;
	for (i=0; i<arraysize(var->reserved); i++)
		var->reserved[i]=0;
	return 0;
}


static void ext_get_par( struct atari_fb_par *par )
{
	par->screen_base = external_addr;
}

static void ext_set_par( struct atari_fb_par *par )
{
}

#define OUTB(port,val) \
	*((unsigned volatile char *) ((port)+external_vgaiobase))=(val)
#define INB(port) \
	(*((unsigned volatile char *) ((port)+external_vgaiobase)))
#define DACDelay 				\
	do {					\
		unsigned char tmp=INB(0x3da);	\
		tmp=INB(0x3da);			\
	} while (0)

static int ext_getcolreg( unsigned regno, unsigned *red,
						  unsigned *green, unsigned *blue,
						  unsigned *transp )

{	unsigned char colmask = (1 << external_bitspercol) - 1;
		
	if (! external_vgaiobase)
		return 1;

	switch (external_card_type) {
	  case IS_VGA:
	    OUTB(0x3c7, regno);
	    DACDelay;
	    *red=INB(0x3c9) & colmask;
	    DACDelay;
	    *green=INB(0x3c9) & colmask;
	    DACDelay;
	    *blue=INB(0x3c9) & colmask;
	    DACDelay;
	    return 0;
	    
	  case IS_MV300:
	    *red = MV300_color[regno].red;
	    *green = MV300_color[regno].green;
	    *blue = MV300_color[regno].blue;
	    *transp=0;
	    return 0;

	  default:
	    return 1;
	  }
}
	
static int ext_setcolreg( unsigned regno, unsigned red,
						  unsigned green, unsigned blue,
						  unsigned transp )

{	unsigned char colmask = (1 << external_bitspercol) - 1;

	if (! external_vgaiobase)
		return 1;

	switch (external_card_type) {
	  case IS_VGA:
	    OUTB(0x3c8, regno);
	    DACDelay;
	    OUTB(0x3c9, red & colmask);
	    DACDelay;
	    OUTB(0x3c9, green & colmask);
	    DACDelay;
	    OUTB(0x3c9, blue & colmask);
	    DACDelay;
	    return 0;

	  case IS_MV300:
	    MV300_color[regno].red = red;
	    MV300_color[regno].green = green;
	    MV300_color[regno].blue = blue;
	    OUTB((MV300_reg[regno] << 2)+1, red);
	    OUTB((MV300_reg[regno] << 2)+1, green);
	    OUTB((MV300_reg[regno] << 2)+1, blue);
	    return 0;

	  default:
	    return 1;
	  }
}
	

static int ext_detect( void )

{
	struct fb_var_screeninfo *myvar = &atari_fb_predefined[0];
	struct atari_fb_par dummy_par;

	myvar->xres = external_xres;
	myvar->yres = external_yres;
	myvar->bits_per_pixel = external_depth;
	ext_encode_var(myvar, &dummy_par);
	return 1;
}

#endif /* ATAFB_EXT */

/* ------ This is the same for most hardware types -------- */

static void set_screen_base(unsigned long s_base)
{
	unsigned long addr;
	addr= VTOP(s_base);
	/* Setup Screen Memory */
	shifter.bas_hi=(unsigned char) ((addr & 0xff0000) >> 16);
  	shifter.bas_md=(unsigned char) ((addr & 0x00ff00) >> 8);
  	shifter.bas_lo=(unsigned char)  (addr & 0x0000ff);
}


static int pan_display( struct fb_var_screeninfo *var,
                        struct atari_fb_par *par )
{
	if (var->xoffset)
		return -EINVAL;
	par->screen_base
		= screen_base + (var->yoffset * disp[currcon].var.xres_virtual
						 * disp[currcon].var.bits_per_pixel / 8);
	if (fbhw->set_screen_base)
		fbhw->set_screen_base (par->screen_base);
	else
		return -EINVAL;
	return 0;
}


/* ------------ Interfaces to hardware functions ------------ */


#ifdef ATAFB_TT
struct fb_hwswitch tt_switch = {
	tt_detect, tt_encode_fix, tt_decode_var, tt_encode_var,
	tt_get_par, tt_set_par, tt_getcolreg, tt_setcolreg,
	set_screen_base, NULL, pan_display
};
#endif

#ifdef ATAFB_FALCON
struct fb_hwswitch falcon_switch = {
	falcon_detect, falcon_encode_fix, falcon_decode_var, falcon_encode_var,
	falcon_get_par, falcon_set_par, falcon_getcolreg,
	falcon_setcolreg, set_screen_base, falcon_blank, falcon_pan_display
};
#endif

#ifdef ATAFB_STE
struct fb_hwswitch st_switch = {
	stste_detect, stste_encode_fix, stste_decode_var, stste_encode_var,
	stste_get_par, stste_set_par, stste_getcolreg, stste_setcolreg,
	stste_set_screen_base, NULL, pan_display
};
#endif

#ifdef ATAFB_EXT
struct fb_hwswitch ext_switch = {
	ext_detect, ext_encode_fix, ext_decode_var, ext_encode_var,
	ext_get_par, ext_set_par, ext_getcolreg, ext_setcolreg, NULL, NULL, NULL
};
#endif



static void atari_fb_get_par( struct atari_fb_par *par )
{
	if (current_par_valid) {
		*par=current_par;
	}
	else
		fbhw->get_par(par);
}


static void atari_fb_set_par( struct atari_fb_par *par )
{
	fbhw->set_par(par);
	current_par=*par;
	current_par_valid=1;
}



/* =========================================================== */
/* ============== Hardware Independent Functions ============= */
/* =========================================================== */


/* used for hardware scrolling */

static int
fb_update_var(int con)
{
	int off=disp[con].var.yoffset*disp[con].var.xres_virtual*
			disp[con].var.bits_per_pixel>>3;

	current_par.screen_base=screen_base + off;

	if (fbhw->set_screen_base)
		fbhw->set_screen_base(current_par.screen_base);
	return 0;
}

static int
do_fb_set_var(struct fb_var_screeninfo *var, int isactive)
{
	int err,activate;
	struct atari_fb_par par;
	if ((err=fbhw->decode_var(var, &par)))
		return err;
	activate=var->activate;
	if (((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) && isactive)
		atari_fb_set_par(&par);
	fbhw->encode_var(var, &par);
	var->activate=activate;
	return 0;
}

/* Functions for handling colormap */

/* there seems to be a bug in gcc 2.5.8 which inhibits using an other solution */
/* I always get a sigsegv */

static short red16[]=
	{ 0x0000,0x0000,0x0000,0x0000,0xc000,0xc000,0xc000,0xc000,
	  0x8000,0x0000,0x0000,0x0000,0xffff,0xffff,0xffff,0xffff};
static short green16[]=
	{ 0x0000,0x0000,0xc000,0xc000,0x0000,0x0000,0xc000,0xc000,
	  0x8000,0x0000,0xffff,0xffff,0x0000,0x0000,0xffff,0xffff};
static short blue16[]=
	{ 0x0000,0xc000,0x0000,0xc000,0x0000,0xc000,0x0000,0xc000,
	  0x8000,0xffff,0x0000,0xffff,0x0000,0xffff,0x0000,0xffff};

static short red4[]=
	{ 0x0000,0xc000,0x8000,0xffff};
static short green4[]=
	{ 0x0000,0xc000,0x8000,0xffff};
static short blue4[]=
	{ 0x0000,0xc000,0x8000,0xffff};

static short red2[]=
	{ 0x0000,0xffff};
static short green2[]=
	{ 0x0000,0xffff};
static short blue2[]=
	{ 0x0000,0xffff};

struct fb_cmap default_16_colors = { 0, 16, red16, green16, blue16, NULL };
struct fb_cmap default_4_colors = { 0, 4, red4, green4, blue4, NULL };
struct fb_cmap default_2_colors = { 0, 2, red2, green2, blue2, NULL };

static struct fb_cmap *
get_default_colormap(int bpp)
{
	if (bpp == 1)
		return &default_2_colors;
	if (bpp == 2)
		return &default_4_colors;
	return &default_16_colors;
}

#define CNVT_TOHW(val,width)	(((val) << (width)) + 0x7fff - (val)) >> 16
#define CNVT_FROMHW(val,width) ((width)?((((val) << 16) - (val)) / ((1<<(width))-1)):0)


static int
do_fb_get_cmap(struct fb_cmap *cmap, struct fb_var_screeninfo *var, int kspc)
{
	int i,start;
	unsigned short *red,*green,*blue,*transp;
	unsigned int hred,hgreen,hblue,htransp;

	red=cmap->red;
	green=cmap->green;
	blue=cmap->blue;
	transp=cmap->transp;
	start=cmap->start;
	if (start < 0)
		return EINVAL;
	for (i=0 ; i < cmap->len ; i++) {
		if (fbhw->getcolreg(start++, &hred, &hgreen, &hblue, &htransp))
			return 0;
		hred=CNVT_FROMHW(hred,var->red.length);
		hgreen=CNVT_FROMHW(hgreen,var->green.length);
		hblue=CNVT_FROMHW(hblue,var->blue.length);
		htransp=CNVT_FROMHW(htransp,var->transp.length);
		if (kspc) {
			*red=hred;
			*green=hgreen;
			*blue=hblue;
			if (transp) *transp=htransp;
		}
		else {
			put_fs_word(hred, red);
			put_fs_word(hgreen, green);
			put_fs_word(hblue, blue);
			if (transp) put_fs_word(htransp, transp);
		}
		red++;
		green++;
		blue++;
		if (transp) transp++;
	}
	return 0;
}

static int
do_fb_set_cmap(struct fb_cmap *cmap, struct fb_var_screeninfo *var, int kspc)
{
	int i,start;
	unsigned short *red,*green,*blue,*transp;
	unsigned int hred,hgreen,hblue,htransp;

	red=cmap->red;
	green=cmap->green;
	blue=cmap->blue;
	transp=cmap->transp;
	start=cmap->start;

	if (start < 0)
		return -EINVAL;
	for (i=0 ; i < cmap->len ; i++) {
		if (kspc) {
			hred=*red;
			hgreen=*green;
			hblue=*blue;
			htransp=(transp) ? *transp : 0;
		}
		else {
			hred=get_fs_word(red);
			hgreen=get_fs_word(green);
			hblue=get_fs_word(blue);
			htransp=(transp)?get_fs_word(transp):0;
		}
		hred=CNVT_TOHW(hred,var->red.length);
		hgreen=CNVT_TOHW(hgreen,var->green.length);
		hblue=CNVT_TOHW(hblue,var->blue.length);
		htransp=CNVT_TOHW(htransp,var->transp.length);
		red++;
		green++;
		blue++;
		if (transp) transp++;
		if (fbhw->setcolreg(start++, hred, hgreen, hblue, htransp))
			return 0;
	}
	return 0;
}

static void
do_install_cmap(int con)
{
	if (con != currcon)
		return;
	if (disp[con].cmap.len)
		do_fb_set_cmap(&disp[con].cmap, &(disp[con].var), 1);
	else
		do_fb_set_cmap(get_default_colormap(
				disp[con].var.bits_per_pixel), &(disp[con].var), 1);		
}

static void
memcpy_fs(int fsfromto, void *to, void *from, int len)
{
	switch (fsfromto) {
	case 0:
		memcpy(to,from,len);
		return;
	case 1:
		memcpy_fromfs(to,from,len);
		return;
	case 2:
		memcpy_tofs(to,from,len);
		return;
	}
}

static void
copy_cmap(struct fb_cmap *from, struct fb_cmap *to, int fsfromto)
{
	int size;
	int tooff=0, fromoff=0;

	if (to->start > from->start)
		fromoff=to->start-from->start;
	else
		tooff=from->start-to->start;			
	size=to->len-tooff;
	if (size > from->len-fromoff)
		size=from->len-fromoff;
	if (size < 0)
		return;
	size*=sizeof(unsigned short);
	memcpy_fs(fsfromto, to->red+tooff, from->red+fromoff, size);
	memcpy_fs(fsfromto, to->green+tooff, from->green+fromoff, size);
	memcpy_fs(fsfromto, to->blue+tooff, from->blue+fromoff, size);
	if (from->transp && to->transp)
		memcpy_fs(fsfromto, to->transp+tooff, from->transp+fromoff, size);
}
 
static int
alloc_cmap(struct fb_cmap *cmap,int len,int transp)
{
	int size=len*sizeof(unsigned short);
	if (cmap->len != len) {
		if (cmap->red)
			kfree(cmap->red);
		if (cmap->green)
			kfree(cmap->green);
		if (cmap->blue)
			kfree(cmap->blue);
		if (cmap->transp)
			kfree(cmap->transp);
		cmap->red=cmap->green=cmap->blue=cmap->transp=NULL;
		cmap->len=0;
		if (! len)
			return 0;
		if (! (cmap->red=kmalloc(size, GFP_ATOMIC)))
			return -1;
		if (! (cmap->green=kmalloc(size, GFP_ATOMIC)))
			return -1;
		if (! (cmap->blue=kmalloc(size, GFP_ATOMIC)))
			return -1;
		if (transp) {
			if (! (cmap->transp=kmalloc(size, GFP_ATOMIC)))
				return -1;
		}
		else
			cmap->transp=NULL;
	}
	cmap->start=0;
	cmap->len=len;
	copy_cmap(get_default_colormap(len), cmap, 0);
	return 0;
}	

static int
atari_fb_get_fix(struct fb_fix_screeninfo *fix, int con)
{
	struct atari_fb_par par;
	if (con == -1)
		atari_fb_get_par(&par);
	else
		fbhw->decode_var(&disp[con].var,&par);
	return fbhw->encode_fix(fix, &par);
}
	
static int
atari_fb_get_var(struct fb_var_screeninfo *var, int con)
{
	struct atari_fb_par par;
	if (con == -1) {
		atari_fb_get_par(&par);
		fbhw->encode_var(var, &par);
	}
	else
		*var=disp[con].var;
	return 0;
}

static void
atari_fb_set_disp(int con)
{
	struct fb_fix_screeninfo fix;

	atari_fb_get_fix(&fix, con);
	if (con == -1)
		con=0;
	disp[con].screen_base = (u_char *)fix.smem_start;
	disp[con].visual = fix.visual;
	disp[con].type = fix.type;
	disp[con].type_aux = fix.type_aux;
	disp[con].ypanstep = fix.ypanstep;
	disp[con].ywrapstep = fix.ywrapstep;
	disp[con].line_length = fix.line_length;
	if (fix.visual != FB_VISUAL_PSEUDOCOLOR &&
		fix.visual != FB_VISUAL_DIRECTCOLOR)
		disp[con].can_soft_blank = 0;
	else
		disp[con].can_soft_blank = 1;
	disp[con].inverse =
	    (fix.visual == FB_VISUAL_MONO01 ? !inverse : inverse);
}

static int
atari_fb_set_var(struct fb_var_screeninfo *var, int con)
{
	int err,oldxres,oldyres,oldbpp,oldxres_virtual,oldyoffset;
	if ((err=do_fb_set_var(var, con==currcon)))
		return err;
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		oldxres=disp[con].var.xres;
		oldyres=disp[con].var.yres;
		oldxres_virtual=disp[con].var.xres_virtual;
		oldbpp=disp[con].var.bits_per_pixel;
		oldyoffset=disp[con].var.yoffset;
		disp[con].var=*var;
		if (oldxres != var->xres || oldyres != var->yres 
		    || oldxres_virtual != var->xres_virtual
		    || oldbpp != var->bits_per_pixel
		    || oldyoffset != var->yoffset) {
			atari_fb_set_disp(con);
			(*fb_info.changevar)(con);
			alloc_cmap(&disp[con].cmap, 0, 0);
			do_install_cmap(con);
		}
	}
	var->activate=0;
	return 0;
}



static int
atari_fb_get_cmap(struct fb_cmap *cmap, int kspc, int con)
{
	if (con == currcon) /* current console ? */
		return do_fb_get_cmap(cmap, &(disp[con].var), kspc);
	else
		if (disp[con].cmap.len) /* non default colormap ? */
			copy_cmap(&disp[con].cmap, cmap, kspc ? 0 : 2);
		else
			copy_cmap(get_default_colormap(
			    disp[con].var.bits_per_pixel), cmap, kspc ? 0 : 2);
	return 0;
}

static int
atari_fb_set_cmap(struct fb_cmap *cmap, int kspc, int con)
{
	int err;
	if (! disp[con].cmap.len) { /* no colormap allocated ? */
		if ((err = alloc_cmap(&disp[con].cmap, 
					1 << disp[con].var.bits_per_pixel, 0)))
		return err;
	}
	if (con == currcon) /* current console ? */
		return do_fb_set_cmap(cmap, &(disp[con].var), kspc);
	else
		copy_cmap(cmap, &disp[con].cmap, kspc ? 0 : 1);
	return 0;
}

static int
atari_fb_pan_display(struct fb_var_screeninfo *var, int con)
{
	int xoffset = var->xoffset;
	int yoffset = var->yoffset;
	int err;

	if (   xoffset < 0 || xoffset + disp[con].var.xres > disp[con].var.xres_virtual
	    || yoffset < 0 || yoffset + disp[con].var.yres > disp[con].var.yres_virtual)
		return -EINVAL;

	if (con == currcon) {
		if (fbhw->pan_display) {
			if ((err = fbhw->pan_display(var, &current_par)))
				return err;
		}
		else
			return -EINVAL;
	}
	disp[con].var.xoffset = var->xoffset;
	disp[con].var.yoffset = var->yoffset;
	return 0;
}

static int
atari_fb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	       unsigned long arg, int con)
{
	int i;

	switch (cmd) {
#ifdef FBCMD_GET_CURRENTPAR
	case FBCMD_GET_CURRENTPAR:
		if ((i = verify_area(VERIFY_WRITE, (void *)arg,
							 sizeof(struct atari_fb_par))))
			return i;
		memcpy_tofs((void *)arg, (void *)&current_par,
					sizeof(struct atari_fb_par));
		return 0;
#endif
#ifdef FBCMD_SET_CURRENTPAR
	case FBCMD_SET_CURRENTPAR:
		if ((i = verify_area(VERIFY_READ, (void *)arg,
							 sizeof(struct atari_fb_par))))
			return i;
		memcpy_fromfs((void *)&current_par, (void *)arg,
					sizeof(struct atari_fb_par));
		atari_fb_set_par(&current_par);
		return 0;
#endif
	}
	return -EINVAL;
}

static struct fb_ops atari_fb_ops = {
	atari_fb_get_fix, atari_fb_get_var, atari_fb_set_var, atari_fb_get_cmap,
	atari_fb_set_cmap, atari_fb_pan_display, atari_fb_ioctl	
};

static void
check_default_par( int detected_mode )
{
	char default_name[10];
	int i;
	struct fb_var_screeninfo var;
	unsigned long min_mem;

	/* First try the user supplied mode */
	if (default_par) {
		var=atari_fb_predefined[default_par-1];
		var.activate = FB_ACTIVATE_TEST;
		if (do_fb_set_var(&var,1))
			default_par=0;		/* failed */
	}
	/* Next is the autodetected one */
	if (! default_par) {
		var=atari_fb_predefined[detected_mode-1]; /* autodetect */
		var.activate = FB_ACTIVATE_TEST;
		if (!do_fb_set_var(&var,1))
			default_par=detected_mode;
	}
	/* If that also failed, try some default modes... */
	if (! default_par) {
		/* try default1, default2... */
		for (i=1 ; i < 10 ; i++) {
			sprintf(default_name,"default%d",i);
			default_par=get_video_mode(default_name);
			if (! default_par)
				panic("can't set default video mode\n");
			var=atari_fb_predefined[default_par-1];
			var.activate = FB_ACTIVATE_TEST;
			if (! do_fb_set_var(&var,1))
				break;	/* ok */
		}
	}
	min_mem=var.xres_virtual * var.yres_virtual * var.bits_per_pixel/8;
	if (default_mem_req < min_mem)
		default_mem_req=min_mem;
}

static int
atafb_switch(int con)
{
	/* Do we have to save the colormap ? */
	if (disp[currcon].cmap.len)
		do_fb_get_cmap(&disp[currcon].cmap, &(disp[currcon].var), 1);
	do_fb_set_var(&disp[con].var,1);
	currcon=con;
	/* Install new colormap */
	do_install_cmap(con);
	return 0;
}

static void
atafb_blank(int blank)
{
	unsigned short black[16];
	struct fb_cmap cmap;
	if (fbhw->blank && !fbhw->blank(blank))
		return;
	if (blank) {
		memset(black, 0, 16*sizeof(unsigned short));
		cmap.red=black;
		cmap.green=black;
		cmap.blue=black;
		cmap.transp=NULL;
		cmap.start=0;
		cmap.len=16;
		do_fb_set_cmap(&cmap, &(disp[currcon].var), 1);
	}
	else
		do_install_cmap(currcon);
}

struct fb_info *
atari_fb_init(long *mem_start)
{
	int err;
	int pad;
	int detected_mode;
	unsigned long mem_req;
	struct fb_var_screeninfo *var;
	
	err=register_framebuffer("Atari Builtin", &node, &atari_fb_ops, 
			num_atari_fb_predefined, atari_fb_predefined);
	if (err < 0)
		panic ("Cannot register frame buffer\n");
	do {
#ifdef ATAFB_EXT
		if (external_addr) {
			fbhw = &ext_switch;
			break;
		}
#endif
#ifdef ATAFB_TT
		if (ATARIHW_PRESENT(TT_SHIFTER)) {
			fbhw = &tt_switch;
			break;
		}
#endif
#ifdef ATAFB_FALCON
		if (ATARIHW_PRESENT(VIDEL_SHIFTER)) {
			fbhw = &falcon_switch;
			add_isr(IRQ_AUTO_4, falcon_vbl_switcher, IRQ_TYPE_PRIO, NULL,
					"framebuffer/modeswitch");
			break;
		}
#endif
#ifdef ATAFB_STE
		if (ATARIHW_PRESENT(STND_SHIFTER) ||
		    ATARIHW_PRESENT(EXTD_SHIFTER)) {
			fbhw = &st_switch;
			break;
		}
		fbhw = &st_switch;
		printk("Cannot determine video hardware; defaulting to ST(e)\n");
#else /* ATAFB_STE */
		/* no default driver included */
		/* Nobody will ever see this message :-) */
		panic("Cannot initialize video hardware\n");
#endif
	} while (0);
	detected_mode = fbhw->detect();
	check_default_par(detected_mode);
#ifdef ATAFB_EXT
	if (!external_addr) {
#endif /* ATAFB_EXT */
		mem_req = default_mem_req + ovsc_offset +
			ovsc_addlen;
		mem_req = ((mem_req + PAGE_SIZE - 1) & PAGE_MASK) + PAGE_SIZE;
		screen_base = (unsigned long) atari_stram_alloc(mem_req, mem_start);
		memset((char *) screen_base, 0, mem_req);
		pad = ((screen_base + PAGE_SIZE-1) & PAGE_MASK) - screen_base;
		screen_base+=pad;
		real_screen_base=screen_base+ovsc_offset;
		screen_len = (mem_req - pad - ovsc_offset) & PAGE_MASK;
		st_ovsc_switch(ovsc_switchmode);
		if (m68k_is040or060) {
			/* On a '040+, the cache mode of video RAM must be set to
			 * write-through also for internal video hardware! */
			cache_push( VTOP(screen_base), screen_len );
			kernel_set_cachemode( screen_base, screen_len,
								  KERNELMAP_NO_COPYBACK );
		}
#ifdef ATAFB_EXT
	}
	else {
		/* Map the video memory (physical address given) to somewhere
		 * in the kernel address space.
		 */
		*mem_start = (*mem_start+PAGE_SIZE-1) & ~(PAGE_SIZE-1);
		external_addr = kernel_map(external_addr, external_len,
									KERNELMAP_NO_COPYBACK, mem_start);
		if (external_vgaiobase)
			external_vgaiobase = kernel_map(external_vgaiobase,
				0x10000, KERNELMAP_NOCACHE_SER, mem_start);
		screen_base      =
		real_screen_base = external_addr;
		screen_len       = external_len & PAGE_MASK;
		memset ((char *) screen_base, 0, external_len);
	}
#endif /* ATAFB_EXT */

	strcpy(fb_info.modename, "Atari Builtin ");
	fb_info.disp=disp;
	fb_info.switch_con=&atafb_switch;
	fb_info.updatevar=&fb_update_var;
	fb_info.blank=&atafb_blank;
	var=atari_fb_predefined+default_par-1;
	do_fb_set_var(var,1);
	strcat(fb_info.modename,fb_var_names[default_par-1][0]);

	atari_fb_get_var(&disp[0].var, -1);
	atari_fb_set_disp(-1);
	printk("Determined %dx%d, depth %d\n",
	       disp[0].var.xres, disp[0].var.yres, disp[0].var.bits_per_pixel );
	do_install_cmap(0);
	return &fb_info;
}

/* a strtok which returns empty strings, too */

static char * strtoke(char * s,const char * ct)
{
  char *sbegin, *send;
  static char *ssave = NULL;
  
  sbegin  = s ? s : ssave;
  if (!sbegin) {
	  return NULL;
  }
  if (*sbegin == '\0') {
    ssave = NULL;
    return NULL;
  }
  send = strpbrk(sbegin, ct);
  if (send && *send != '\0')
    *send++ = '\0';
  ssave = send;
  return sbegin;
}

void atari_video_setup( char *options, int *ints )
{
    char *this_opt;
    int temp;
    char ext_str[80], int_str[100];
    char mcap_spec[80];
    char user_mode[80];

	ext_str[0]          =
	int_str[0]          =
	mcap_spec[0]        =
	user_mode[0]        =
	fb_info.fontname[0] = '\0';

    if (!options || !*options)
		return;
     
    for(this_opt=strtok(options,","); this_opt; this_opt=strtok(NULL,",")) {
	if (!*this_opt) continue;
	if ((temp=get_video_mode(this_opt)))
		default_par=temp;
	else if (! strcmp(this_opt, "inverse"))
		inverse=1;
	else if (!strncmp(this_opt, "font:", 5))
	   strcpy(fb_info.fontname, this_opt+5);
	else if (! strncmp(this_opt, "hwscroll_",9)) {
		hwscroll=simple_strtoul(this_opt+9, NULL, 10);
		if (hwscroll < 0)
			hwscroll = 0;
		if (hwscroll > 200)
			hwscroll = 200;
	}
	else if (! strncmp(this_opt, "sw_",3)) {
		if (! strcmp(this_opt+3, "acia"))
			ovsc_switchmode = SWITCH_ACIA;
		else if (! strcmp(this_opt+3, "snd6"))
			ovsc_switchmode = SWITCH_SND6;
		else if (! strcmp(this_opt+3, "snd7"))
			ovsc_switchmode = SWITCH_SND7;
		else ovsc_switchmode = SWITCH_NONE;
	}
#ifdef ATAFB_EXT
	else if (!strcmp(this_opt,"mv300")) {
		external_bitspercol = 8;
		external_card_type = IS_MV300;
	}
	else if (!strncmp(this_opt,"external:",9))
		strcpy(ext_str, this_opt+9);
#endif
	else if (!strncmp(this_opt,"internal:",9))
		strcpy(int_str, this_opt+9);
#ifdef ATAFB_FALCON
	else if (!strcmp(this_opt, "pwrsave"))
		pwrsave = 1;
	else if (!strncmp(this_opt, "eclock:", 7)) {
		fext.f = simple_strtoul(this_opt+7, NULL, 10);
		/* external pixelclock in kHz --> ps */
		fext.t = (2000000000UL/fext.f+1)/2;
		fext.f *= 1000;
	}
	else if (!strncmp(this_opt, "monitorcap:", 11))
		strcpy(mcap_spec, this_opt+11);
#endif
	else if (!strcmp(this_opt, "keep"))
		DontCalcRes = 1;
	else if (!strncmp(this_opt, "R", 1))
		strcpy(user_mode, this_opt+1);
    }

    if (*int_str) {
	/* Format to config extended internal video hardware like OverScan:
	"<switch-type>,internal:<xres>;<yres>;<xres_max>;<yres_max>;<offset>"
	Explanation:
	<switch-type> type to switch on higher resolution
			sw_acia : via keyboard ACIA
			sw_snd6 : via bit 6 of the soundchip port
			sw_snd7 : via bit 7 of the soundchip port
	<xres>: x-resolution 
	<yres>: y-resolution
	The following are only needed if you have an overscan which
	needs a black border:
	<xres_max>: max. length of a line in pixels your OverScan hardware would allow
	<yres_max>: max. number of lines your OverScan hardware would allow
	<offset>: Offset from physical beginning to visible beginning
		  of screen in bytes
	*/
	int xres;
	char *p;

	if (!(p = strtoke(int_str, ";")) ||!*p) goto int_invalid;
	xres = simple_strtoul(p, NULL, 10);
	if (!(p = strtoke(NULL, ";")) || !*p) goto int_invalid;
	sttt_xres=xres;
	tt_yres=st_yres=simple_strtoul(p, NULL, 10);
	if ((p=strtoke(NULL, ";")) && *p) {
		sttt_xres_virtual=simple_strtoul(p, NULL, 10);
	}
	if ((p=strtoke(NULL, ";")) && *p) {
		sttt_yres_virtual=simple_strtoul(p, NULL, 0);
	}
	if ((p=strtoke(NULL, ";")) && *p) {
		ovsc_offset=simple_strtoul(p, NULL, 0);
	}

	if (ovsc_offset || (sttt_yres_virtual != st_yres))
		use_hwscroll=0;
    }
    else 
      int_invalid:	ovsc_switchmode = SWITCH_NONE;

#ifdef ATAFB_EXT
    if (*ext_str) {
	int		xres, yres, depth, planes;
	unsigned long addr, len;
	char *p;

	/* Format is: <xres>;<yres>;<depth>;<plane organ.>;
	 *            <screen mem addr>
	 *	      [;<screen mem length>[;<vgaiobase>[;<colorreg-type>]]]
	 */
	if (!(p = strtoke(ext_str, ";")) ||!*p) goto ext_invalid;
	xres = simple_strtoul(p, NULL, 10);
	if (xres <= 0) goto ext_invalid;

	if (!(p = strtoke(NULL, ";")) ||!*p) goto ext_invalid;
	yres = simple_strtoul(p, NULL, 10);
	if (yres <= 0) goto ext_invalid;

	if (!(p = strtoke(NULL, ";")) ||!*p) goto ext_invalid;
	depth = simple_strtoul(p, NULL, 10);
	if (depth != 1 && depth != 2 && depth != 4 && depth != 8 &&
		depth != 16 && depth != 24) goto ext_invalid;

	if (!(p = strtoke(NULL, ";")) ||!*p) goto ext_invalid;
	if (*p == 'i')
		planes = FB_TYPE_INTERLEAVED_PLANES;
	else if (*p == 'p')
		planes = FB_TYPE_PACKED_PIXELS;
	else if (*p == 'n')
		planes = FB_TYPE_PLANES;
	else if (*p == 't')
		planes = -1; /* true color */
	else
		goto ext_invalid;


	if (!(p = strtoke(NULL, ";")) ||!*p) goto ext_invalid;
	addr = simple_strtoul(p, NULL, 0);

	if (!(p = strtoke(NULL, ";")) ||!*p)
		len = xres*yres*depth/8;
	else
		len = simple_strtoul(p, NULL, 0);

	if ((p = strtoke(NULL, ";")) && *p)
		external_vgaiobase=simple_strtoul(p, NULL, 0);

	if ((p = strtoke(NULL, ";")) && *p) {
		external_bitspercol = simple_strtoul(p, NULL, 0);
		if (external_bitspercol > 8)
			external_bitspercol = 8;
		else if (external_bitspercol < 1)
			external_bitspercol = 1;
	}
	
	if ((p = strtoke(NULL, ";")) && *p) {
		if (!strcmp(this_opt, "vga"))
			external_card_type = IS_VGA;
		if (!strcmp(this_opt, "mv300"))
			external_card_type = IS_MV300;
	}

	external_xres  = xres;
	external_yres  = yres;
	external_depth = depth;
	external_pmode = planes;
	external_addr  = addr;
	external_len   = len;
		
	if (external_card_type == IS_MV300)
	  switch (external_depth) {
	    case 1:
	      MV300_reg = MV300_reg_1bit;
	      break;
	    case 4:
	      MV300_reg = MV300_reg_4bit;
	      break;
	    case 8:
	      MV300_reg = MV300_reg_8bit;
	      break;
	    }

      ext_invalid:
	;
    }
#endif /* ATAFB_EXT */

#ifdef ATAFB_FALCON
    if (*mcap_spec) {
	char *p;
	int vmin, vmax, hmin, hmax;

	/* Format for monitor capabilities is: <Vmin>;<Vmax>;<Hmin>;<Hmax>
	 * <V*> vertical freq. in Hz
	 * <H*> horizontal freq. in kHz
	 */
	if (!(p = strtoke(mcap_spec, ";")) || !*p) goto cap_invalid;
	vmin = simple_strtoul(p, NULL, 10);
	if (vmin <= 0) goto cap_invalid;
	if (!(p = strtoke(NULL, ";")) || !*p) goto cap_invalid;
	vmax = simple_strtoul(p, NULL, 10);
	if (vmax <= 0 || vmax <= vmin) goto cap_invalid;
	if (!(p = strtoke(NULL, ";")) || !*p) goto cap_invalid;
	hmin = 1000 * simple_strtoul(p, NULL, 10);
	if (hmin <= 0) goto cap_invalid;
	if (!(p = strtoke(NULL, "")) || !*p) goto cap_invalid;
	hmax = 1000 * simple_strtoul(p, NULL, 10);
	if (hmax <= 0 || hmax <= hmin) goto cap_invalid;

	vfmin = vmin;
	vfmax = vmax;
	hfmin = hmin;
	hfmax = hmax;
      cap_invalid:
	;
    }
#endif

	if (*user_mode) {
		/* Format of user defined video mode is: <xres>;<yres>;<depth>
		 */
		char *p;
		int xres, yres, depth, temp;

		if (!(p = strtoke(user_mode, ";")) || !*p) goto user_invalid;
		xres = simple_strtoul(p, NULL, 10);
		if (!(p = strtoke(NULL, ";")) || !*p) goto user_invalid;
		yres = simple_strtoul(p, NULL, 10);
		if (!(p = strtoke(NULL, "")) || !*p) goto user_invalid;
		depth = simple_strtoul(p, NULL, 10);
		if ((temp=get_video_mode("user0"))) {
			default_par=temp;
			atari_fb_predefined[default_par-1].xres = xres;
			atari_fb_predefined[default_par-1].yres = yres;
			atari_fb_predefined[default_par-1].bits_per_pixel = depth;
		}

	  user_invalid:
		;
	}
}
