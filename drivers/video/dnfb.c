#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/apollohw.h>
#include <linux/fb.h>
#include <linux/module.h>

#include <video/fbcon-mfb.h>


/* apollo video HW definitions */

/*
 * Control Registers.   IOBASE + $x
 *
 * Note: these are the Memory/IO BASE definitions for a mono card set to the
 * alternate address
 *
 * Control 3A and 3B serve identical functions except that 3A
 * deals with control 1 and 3b deals with Color LUT reg.
 */

#define AP_IOBASE       0x5d80          /* Base address of 1 plane board. */
#define AP_STATUS       0x5d80          /* Status register.  Read */
#define AP_WRITE_ENABLE 0x5d80          /* Write Enable Register Write */
#define AP_DEVICE_ID    0x5d81          /* Device ID Register. Read */
#define AP_ROP_1        0x5d82          /* Raster Operation reg. Write Word */
#define AP_DIAG_MEM_REQ 0x5d84          /* Diagnostic Memory Request. Write Word */
#define AP_CONTROL_0    0x5d88          /* Control Register 0.  Read/Write */
#define AP_CONTROL_1    0x5d8a          /* Control Register 1.  Read/Write */
#define AP_CONTROL_3A   0x5d8e          /* Control Register 3a. Read/Write */
#define AP_CONTROL_2    0x5d8c          /* Control Register 2. Read/Write */


#define FRAME_BUFFER_START 0x0FA0000
#define FRAME_BUFFER_LEN 0x40000

/* CREG 0 */
#define VECTOR_MODE 0x40 /* 010x.xxxx */
#define DBLT_MODE   0x80 /* 100x.xxxx */
#define NORMAL_MODE 0xE0 /* 111x.xxxx */
#define SHIFT_BITS  0x1F /* xxx1.1111 */
        /* other bits are Shift value */

/* CREG 1 */
#define AD_BLT      0x80 /* 1xxx.xxxx */
#define NORMAL      0x80 /* 1xxx.xxxx */   /* What is happening here ?? */
#define INVERSE     0x00 /* 0xxx.xxxx */   /* Clearing this reverses the screen */
#define PIX_BLT     0x00 /* 0xxx.xxxx */

#define AD_HIBIT        0x40 /* xIxx.xxxx */

#define ROP_EN          0x10 /* xxx1.xxxx */
#define DST_EQ_SRC      0x00 /* xxx0.xxxx */
#define nRESET_SYNC     0x08 /* xxxx.1xxx */
#define SYNC_ENAB       0x02 /* xxxx.xx1x */

#define BLANK_DISP      0x00 /* xxxx.xxx0 */
#define ENAB_DISP       0x01 /* xxxx.xxx1 */

#define NORM_CREG1      (nRESET_SYNC | SYNC_ENAB | ENAB_DISP) /* no reset sync */

/* CREG 2 */

/*
 * Following 3 defines are common to 1, 4 and 8 plane.
 */

#define S_DATA_1s   0x00 /* 00xx.xxxx */ /* set source to all 1's -- vector drawing */
#define S_DATA_PIX  0x40 /* 01xx.xxxx */ /* takes source from ls-bits and replicates over 16 bits */
#define S_DATA_PLN  0xC0 /* 11xx.xxxx */ /* normal, each data access =16-bits in
 one plane of image mem */

/* CREG 3A/CREG 3B */
#       define RESET_CREG 0x80 /* 1000.0000 */

/* ROP REG  -  all one nibble */
/*      ********* NOTE : this is used r0,r1,r2,r3 *********** */
#define ROP(r2,r3,r0,r1) ( (U_SHORT)((r0)|((r1)<<4)|((r2)<<8)|((r3)<<12)) )
#define DEST_ZERO               0x0
#define SRC_AND_DEST    0x1
#define SRC_AND_nDEST   0x2
#define SRC                             0x3
#define nSRC_AND_DEST   0x4
#define DEST                    0x5
#define SRC_XOR_DEST    0x6
#define SRC_OR_DEST             0x7
#define SRC_NOR_DEST    0x8
#define SRC_XNOR_DEST   0x9
#define nDEST                   0xA
#define SRC_OR_nDEST    0xB
#define nSRC                    0xC
#define nSRC_OR_DEST    0xD
#define SRC_NAND_DEST   0xE
#define DEST_ONE                0xF

#define SWAP(A) ((A>>8) | ((A&0xff) <<8))

#if 0
#define outb(a,d) *(char *)(a)=(d)
#define outw(a,d) *(unsigned short *)a=d
#endif


/* frame buffer operations */

static int dnfb_open(struct fb_info *info, int user);
static int dnfb_release(struct fb_info *info, int user);
static int dnfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			struct fb_info *info);
static int dnfb_get_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int dnfb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int dnfb_get_cmap(struct fb_cmap *cmap,int kspc,int con,
			 struct fb_info *info);
static int dnfb_set_cmap(struct fb_cmap *cmap,int kspc,int con,
			 struct fb_info *info);
static int dnfb_pan_display(struct fb_var_screeninfo *var, int con,
			    struct fb_info *info);
static int dnfb_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg, int con,
		      struct fb_info *info);

static int dnfbcon_switch(int con, struct fb_info *info);
static int dnfbcon_updatevar(int con, struct fb_info *info);
static void dnfbcon_blank(int blank, struct fb_info *info);

static void dnfb_set_disp(int con, struct fb_info *info);

static struct display disp[MAX_NR_CONSOLES];
static struct fb_info fb_info;
static struct fb_ops dnfb_ops = { 
	dnfb_open,dnfb_release, dnfb_get_fix, dnfb_get_var, dnfb_set_var,
	dnfb_get_cmap, dnfb_set_cmap, dnfb_pan_display, dnfb_ioctl
};

static int currcon=0;

static char dnfb_name[]="Apollo";

static int dnfb_open(struct fb_info *info, int user)
{
        /*
         * Nothing, only a usage count for the moment
         */

        MOD_INC_USE_COUNT;
        return(0);
}

static int dnfb_release(struct fb_info *info, int user)
{
        MOD_DEC_USE_COUNT;
        return(0);
}

static int dnfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			struct fb_info *info)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id,"Apollo Mono");
	fix->smem_start=(char*)(FRAME_BUFFER_START+IO_BASE);
	fix->smem_len=FRAME_BUFFER_LEN;
	fix->type=FB_TYPE_PACKED_PIXELS;
	fix->type_aux=0;
	fix->visual=FB_VISUAL_MONO10;
	fix->xpanstep=0;
	fix->ypanstep=0;
	fix->ywrapstep=0;
        fix->line_length=256;

	return 0;

}
        
static int dnfb_get_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
	memset(var, 0, sizeof(struct fb_var_screeninfo));
	var->xres=1280;
	var->yres=1024;
	var->xres_virtual=2048;
	var->yres_virtual=1024;
	var->xoffset=0;
	var->yoffset=0;
	var->bits_per_pixel=1;
	var->grayscale=0;
	var->nonstd=0;
	var->activate=0;
	var->height=-1;
	var->width=-1;
	var->pixclock=0;
	var->left_margin=0;
	var->right_margin=0;
	var->hsync_len=0;
	var->vsync_len=0;
	var->sync=0;
	var->vmode=FB_VMODE_NONINTERLACED;

	return 0;

}

static int dnfb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
	if(var->xres!=1280) 
		return -EINVAL;
	if(var->yres!=1024)
		return -EINVAL;
	if(var->xres_virtual!=2048)
		return -EINVAL;
	if(var->yres_virtual!=1024)
		return -EINVAL;
	if(var->xoffset!=0)
		return -EINVAL;
	if(var->yoffset!=0)
		return -EINVAL;
	if(var->bits_per_pixel!=1)
		return -EINVAL;
	if(var->grayscale!=0)
		return -EINVAL;
	if(var->nonstd!=0)
		return -EINVAL;
	if(var->activate!=0)
		return -EINVAL;
	if(var->pixclock!=0)
		return -EINVAL;
	if(var->left_margin!=0)
		return -EINVAL;
	if(var->right_margin!=0)
		return -EINVAL;
	if(var->hsync_len!=0)
		return -EINVAL;
	if(var->vsync_len!=0)
		return -EINVAL;
	if(var->sync!=0)
		return -EINVAL;
	if(var->vmode!=FB_VMODE_NONINTERLACED)
		return -EINVAL;

	return 0;

}

static int dnfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	printk("get cmap not supported\n");

	return -EINVAL;
}

static int dnfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	printk("set cmap not supported\n");

	return -EINVAL;

}

static int dnfb_pan_display(struct fb_var_screeninfo *var, int con,
			    struct fb_info *info)
{
	printk("panning not supported\n");

	return -EINVAL;

}

static int dnfb_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg, int con,
		      struct fb_info *info)
{
	return -EINVAL;
}

static void dnfb_set_disp(int con, struct fb_info *info)
{
  struct fb_fix_screeninfo fix;

  dnfb_get_fix(&fix, con, info);
  if(con==-1) 
    con=0;

   disp[con].screen_base = fix.smem_start;
   disp[con].visual = fix.visual;
   disp[con].type = fix.type;
   disp[con].type_aux = fix.type_aux;
   disp[con].ypanstep = fix.ypanstep;
   disp[con].ywrapstep = fix.ywrapstep;
   disp[con].can_soft_blank = 1;
   disp[con].inverse = 0;
   disp[con].line_length = fix.line_length;
#ifdef FBCON_HAS_MFB
   disp[con].dispsw = &fbcon_mfb;
#else
   disp[con].dispsw = &fbcon_dummy;
#endif
}
  
void dnfb_init(void)
{
	fb_info.changevar=NULL;
	strcpy(&fb_info.modename[0],dnfb_name);
	fb_info.fontname[0]=0;
	fb_info.disp=disp;
	fb_info.switch_con=&dnfbcon_switch;
	fb_info.updatevar=&dnfbcon_updatevar;
	fb_info.blank=&dnfbcon_blank;	
	fb_info.node = -1;
	fb_info.fbops = &dnfb_ops;
	fb_info.flags = FBINFO_FLAG_DEFAULT;
	
        outb(RESET_CREG, AP_CONTROL_3A);
        outw(0x0, AP_WRITE_ENABLE);
        outb(NORMAL_MODE,AP_CONTROL_0); 
        outb((AD_BLT | DST_EQ_SRC | NORM_CREG1), AP_CONTROL_1);
        outb(S_DATA_PLN, AP_CONTROL_2);
        outw(SWAP(0x3),AP_ROP_1);

        dnfb_get_var(&disp[0].var, 0, &fb_info);
	dnfb_set_disp(-1, &fb_info);

	if (register_framebuffer(&fb_info) < 0)
		panic("unable to register apollo frame buffer\n");
 
        printk("fb%d: apollo frame buffer alive and kicking !\n",
	       GET_FB_IDX(fb_info.node));
}	

	
static int dnfbcon_switch(int con, struct fb_info *info)
{ 
	currcon=con;
	
	return 0;

}

static int dnfbcon_updatevar(int con, struct fb_info *info)
{
	return 0;
}

static void dnfbcon_blank(int blank, struct fb_info *info)
{
	if(blank)  {
        	outb(0, AP_CONTROL_3A);
		outb((AD_BLT | DST_EQ_SRC | NORM_CREG1) & ~ENAB_DISP,
		     AP_CONTROL_1);
	}
	else {
	        outb(1, AP_CONTROL_3A);
        	outb((AD_BLT | DST_EQ_SRC | NORM_CREG1), AP_CONTROL_1);
	}
}
