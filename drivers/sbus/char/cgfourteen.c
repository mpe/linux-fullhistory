/* $Id: cgfourteen.c,v 1.17 1996/12/23 10:16:00 ecd Exp $
 * cgfourteen.c: Sun SparcStation console support.
 *
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * TODO:
 *
 * Add the ioctls for CLUT manipulation.
 * Map only the amount requested, not a constant amount.
 * XBGR mapping.
 * Add the interrupt handler.
*/

#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>

#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/fbio.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

#include "../../char/vt_kern.h"
#include "../../char/selection.h"
#include "../../char/console_struct.h"
#include "fb.h"

#define CG14_MCR_INTENABLE_SHIFT	7
#define CG14_MCR_INTENABLE_MASK		0x80
#define CG14_MCR_VIDENABLE_SHIFT	6
#define CG14_MCR_VIDENABLE_MASK		0x40
#define CG14_MCR_PIXMODE_SHIFT		4
#define CG14_MCR_PIXMODE_MASK		0x30
#define CG14_MCR_TMR_SHIFT		2
#define CG14_MCR_TMR_MASK		0x0c
#define CG14_MCR_TMENABLE_SHIFT		1
#define CG14_MCR_TMENABLE_MASK		0x02
#define CG14_MCR_RESET_SHIFT		0
#define CG14_MCR_RESET_MASK		0x01
#define CG14_REV_REVISION_SHIFT		4
#define CG14_REV_REVISION_MASK		0xf0
#define CG14_REV_IMPL_SHIFT		0
#define CG14_REV_IMPL_MASK		0x0f
#define CG14_VBR_FRAMEBASE_SHIFT	12
#define CG14_VBR_FRAMEBASE_MASK		0x00fff000
#define CG14_VMCR1_SETUP_SHIFT		0
#define CG14_VMCR1_SETUP_MASK		0x000001ff
#define CG14_VMCR1_VCONFIG_SHIFT	9
#define CG14_VMCR1_VCONFIG_MASK		0x00000e00
#define CG14_VMCR2_REFRESH_SHIFT	0
#define CG14_VMCR2_REFRESH_MASK		0x00000001
#define CG14_VMCR2_TESTROWCNT_SHIFT	1
#define CG14_VMCR2_TESTROWCNT_MASK	0x00000002
#define CG14_VMCR2_FBCONFIG_SHIFT	2
#define CG14_VMCR2_FBCONFIG_MASK	0x0000000c
#define CG14_VCR_REFRESHREQ_SHIFT	0
#define CG14_VCR_REFRESHREQ_MASK	0x000003ff
#define CG14_VCR1_REFRESHENA_SHIFT	10
#define CG14_VCR1_REFRESHENA_MASK	0x00000400
#define CG14_VCA_CAD_SHIFT		0
#define CG14_VCA_CAD_MASK		0x000003ff
#define CG14_VCA_VERS_SHIFT		10
#define CG14_VCA_VERS_MASK		0x00000c00
#define CG14_VCA_RAMSPEED_SHIFT		12
#define CG14_VCA_RAMSPEED_MASK		0x00001000
#define CG14_VCA_8MB_SHIFT		13
#define CG14_VCA_8MB_MASK		0x00002000

#define CG14_MCR_PIXMODE_8		0
#define CG14_MCR_PIXMODE_16		2
#define CG14_MCR_PIXMODE_32		3

struct cg14_regs{
	volatile u8 mcr;	/* Master Control Reg */
	volatile u8 ppr;	/* Packed Pixel Reg */
	volatile u8 tms[2];	/* Test Mode Status Regs */
	volatile u8 msr;	/* Master Status Reg */
	volatile u8 fsr;	/* Fault Status Reg */
	volatile u8 rev;	/* Revision & Impl */
	volatile u8 ccr;	/* Clock Control Reg */
	volatile u32 tmr;	/* Test Mode Read Back */
	volatile u8 mod;	/* Monitor Operation Data Reg */
	volatile u8 acr;	/* Aux Control */
	u8 xxx0[6];
	volatile u16 hct;	/* Hor Counter */
	volatile u16 vct;	/* Vert Counter */
	volatile u16 hbs;	/* Hor Blank Start */
	volatile u16 hbc;	/* Hor Blank Clear */
	volatile u16 hss;	/* Hor Sync Start */
	volatile u16 hsc;	/* Hor Sync Clear */
	volatile u16 csc;	/* Composite Sync Clear */
	volatile u16 vbs;	/* Vert Blank Start */
	volatile u16 vbc;	/* Vert Blank Clear */
	volatile u16 vss;	/* Vert Sync Start */
	volatile u16 vsc;	/* Vert Sync Clear */
	volatile u16 xcs;
	volatile u16 xcc;
	volatile u16 fsa;	/* Fault Status Address */
	volatile u16 adr;	/* Address Registers */
	u8 xxx1[0xce];
	volatile u8 pcg[0x100]; /* Pixel Clock Generator */
	volatile u32 vbr;	/* Frame Base Row */
	volatile u32 vmcr;	/* VBC Master Control */
	volatile u32 vcr;	/* VBC refresh */
	volatile u32 vca;	/* VBC Config */
};

#define CG14_CCR_ENABLE	0x04
#define CG14_CCR_SELECT 0x02	/* HW/Full screen */

struct cg14_cursor {
	volatile u32 cpl0[32];	/* Enable plane 0 */
	volatile u32 cpl1[32];  /* Color selection plane */
	volatile u8 ccr;	/* Cursor Control Reg */
	u8 xxx0[3];
	volatile u16 cursx;	/* Cursor x,y position */
	volatile u16 cursy;	/* Cursor x,y position */
	volatile u32 color0;
	volatile u32 color1;
	u32 xxx1[0x1bc];
	volatile u32 cpl0i[32];	/* Enable plane 0 autoinc */
	volatile u32 cpl1i[32]; /* Color selection autoinc */
};

struct cg14_dac {
	volatile u8 addr;	/* Address Register */
	u8 xxx0[255];
	volatile u8 glut;	/* Gamma table */
	u8 xxx1[255];
	volatile u8 select;	/* Register Select */
	u8 xxx2[255];
	volatile u8 mode;	/* Mode Register */
};

struct cg14_xlut{
	volatile u8 x_xlut [256];
	volatile u8 x_xlutd [256];
	u8 xxx0[0x600];
	volatile u8 x_xlut_inc [256];
	volatile u8 x_xlutd_inc [256];
};

/* Color look up table (clut) */
/* Each one of these arrays hold the color lookup table (for 256
 * colors) for each MDI page (I assume then there should be 4 MDI
 * pages, I still wonder what they are.  I have seen NeXTStep split
 * the screen in four parts, while operating in 24 bits mode.  Each
 * integer holds 4 values: alpha value (transparency channel, thanks
 * go to John Stone (johns@umr.edu) from OpenBSD), red, green and blue
 *
 * I currently use the clut instead of the Xlut
 */
struct cg14_clut {
	unsigned int c_clut [256];
	unsigned int c_clutd [256];    /* i wonder what the 'd' is for */
	unsigned int c_clut_inc [256];
	unsigned int c_clutd_inc [256];
};

static int
cg14_mmap (struct inode *inode, struct file *file,
	   struct vm_area_struct *vma, long base, fbinfo_t *fb)
{
	uint size, page, r, map_size;
	uint map_offset = 0;
	uint ram_size = fb->info.cg14.ramsize;

	printk ("RAMSIZE=%d\n", ram_size);
	size = vma->vm_end - vma->vm_start;
        if (vma->vm_offset & ~PAGE_MASK)
                return -ENXIO;

	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= FB_MMAP_VM_FLAGS; 
	
	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		switch (vma->vm_offset+page){
		case CG3_MMAP_OFFSET-0x7000:
		        printk ("Wee!  They are mapping the register, report this to miguel@gnu.ai.mit.edu\n");
			printk ("Mapping fb->info.regs!\n");
			map_size = 0x7000;
			map_offset = get_phys ((uint) fb->info.cg14.regs);
			break;
			
		case CG3_MMAP_OFFSET:
			map_size = size-page;
			map_offset = get_phys ((uint) fb->base);
			break;

		case MDI_PLANAR_X16_MAP:
			map_size = ram_size/2;
			map_offset = get_phys ((uint) fb->base) | 0x2000000;
			break;
			
		case MDI_PLANAR_C16_MAP:
			map_size = ram_size/2;
			map_offset = get_phys ((uint) fb->base) | 0x2800000;
			break;
			
		case MDI_CHUNKY_XBGR_MAP:
			map_size = 0;
			printk ("Woo Woo: XBGR not there yet\n");
			break;
			
		case MDI_CHUNKY_BGR_MAP:
			map_size = ram_size;
			map_offset = get_phys ((uint) fb->base) | 0x1000000;
			break;
			
		case MDI_PLANAR_X32_MAP:
			map_size = ram_size/4;
			map_offset = get_phys ((uint) fb->base) | 0x3000000;
			break;
		case MDI_PLANAR_B32_MAP:
			map_size = ram_size/4;
			map_offset = get_phys ((uint) fb->base) | 0x3400000;
			break;
		case MDI_PLANAR_G32_MAP:
			map_size = ram_size/4;
			map_offset = get_phys ((uint) fb->base) | 0x3800000;
			break;
		case MDI_PLANAR_R32_MAP:
			map_size = ram_size/4;
			map_offset = get_phys ((uint) fb->base) | 0x3c00000;
			break;

		case MDI_CURSOR_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((uint) fb->info.cg14.cursor_regs);
			break;
			
		case CG14_REGS:
		        printk ("Wee!  They are mapping the register, report this to miguel@gnu.ai.mit.edu\n");
			map_size = PAGE_SIZE;
			map_offset = get_phys ((uint) fb->info.cg14.regs);
			break;
			
		case CG14_XLUT:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((uint) fb->info.cg14.regs+0x3000);
			break;
			
		case CG14_CLUT1:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((uint) fb->info.cg14.regs+0x4000);
			break;
			
		case CG14_CLUT2:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((uint) fb->info.cg14.regs+0x5000);
			break;
			
		default:
			map_size = 0;
			break;
		}
		if (!map_size){
			page += PAGE_SIZE;
			continue;
		}
		if (page + map_size > size)
			map_size = size - page;
		r = io_remap_page_range (vma->vm_start+page,
					 map_offset,
					 map_size, vma->vm_page_prot,
					 fb->space);
		if (r) return -EAGAIN;
		page += map_size;
	}
        vma->vm_inode = inode;
        inode->i_count++;
        return 0;
}

static void
cg14_cmap (fbinfo_t *fb, int index, int count)
{
	struct cg14_clut *clut = fb->info.cg14.clut;
	int i;
	
	for (i = index; count--; i++){
		clut->c_clut [i] = 
			(fb->color_map CM(i,2) << 16) |
			(fb->color_map CM(i,1) << 8) |
			(fb->color_map CM(i,0));
	}
}

static void
cg14_setcursormap (fbinfo_t *fb, unsigned char *red,
                                 unsigned char *green,
                                 unsigned char *blue)
{
        struct cg14_cursor *cur = fb->info.cg14.cursor_regs;

	cur->color0 = ((red[0]) | (green[0] << 8) | (blue[0] << 16));
	cur->color1 = ((red[1]) | (green[1] << 8) | (blue[1] << 16));
}

/* Load cursor information */
static void
cg14_setcursor (fbinfo_t *fb)
{
        struct cg_cursor *c = &fb->cursor;
	struct cg14_cursor *cur = fb->info.cg14.cursor_regs;

        if (c->enable)
		cur->ccr |= CG14_CCR_ENABLE;
        cur->cursx = ((c->cpos.fbx - c->chot.fbx) & 0xfff);
        cur->cursy = ((c->cpos.fby - c->chot.fby) & 0xfff);
}

/* Set cursor shape */
static void
cg14_setcurshape (fbinfo_t *fb)
{
        struct cg14_cursor *cur = fb->info.cg14.cursor_regs;
        int i;

        for (i = 0; i < 32; i++){
                cur->cpl0 [i] = fb->cursor.bits[0][i];
                cur->cpl1 [i] = fb->cursor.bits[1][i];
        }
}

/* These ones are for putting the video card on 16/32 bpp */
static int
cg14_ioctl (struct inode *inode, struct file *file, unsigned cmd, unsigned long arg, fbinfo_t *fb)
{
	switch (cmd){
	case MDI_RESET: {
		volatile unsigned char *control = &(fb->info.cg14.regs->mcr);
		*control = (*control & ~CG14_MCR_PIXMODE_MASK);
	}
	break;

	case MDI_GET_CFGINFO: {
		int error;
		struct mdi_cfginfo *mdii;
		
		error = verify_area (VERIFY_WRITE, (void *) arg,
				     sizeof (struct mdi_cfginfo));
		if (error)
			return error;

		mdii = (struct mdi_cfginfo *) arg;
#if 0
		__put_user_ret(2, &mdii->mdi_ncluts, -EFAULT);
		switch (fb->info.cg14.regs->rev & CG14_REV_IMPL_MASK){
		case 0:
		case 2:
		    break;
		    
		case 1:
		case 3:
		    __put_user_ret(3, &mdii->mdi_ncluts, -EFAULT);
		    break;
		default:
		    printk ("Unknown implementation number\n");
		}
#endif
		__put_user_ret(FBTYPE_MDICOLOR, &mdii->mdi_type, -EFAULT);
		__put_user_ret(fb->type.fb_height, &mdii->mdi_height, -EFAULT);
		__put_user_ret(fb->type.fb_width, &mdii->mdi_width, -EFAULT);
		__put_user_ret(fb->info.cg14.video_mode, &mdii->mdi_mode, -EFAULT);
		__put_user_ret(72, &mdii->mdi_pixfreq, -EFAULT); /* FIXME */
		__put_user_ret(fb->info.cg14.ramsize, &mdii->mdi_size, -EFAULT);
	}
	break;

	case MDI_SET_PIXELMODE: {
		int newmode;
		volatile u8 *control;
		
		get_user_ret(newmode, (int *)arg, -EFAULT);
		control = &(fb->info.cg14.regs->mcr);
		switch (newmode){
		case MDI_32_PIX:
			*control = (*control & ~CG14_MCR_PIXMODE_MASK) |
				(CG14_MCR_PIXMODE_32 << CG14_MCR_PIXMODE_SHIFT);
			break;
		case MDI_16_PIX:
			*control = (*control & ~CG14_MCR_PIXMODE_MASK) | 0x20;
			break;
		case MDI_8_PIX:
			*control = (*control & ~CG14_MCR_PIXMODE_MASK);
			break;

		default:
			return -ENOSYS;
		}
		fb->info.cg14.video_mode = newmode;
	}
	break;
	
	} /* switch */
	return 0;
}

static void
cg14_switch_from_graph (void)
{
	fbinfo_t *fb = &(fbinfo [0]);
	struct cg14_info *cg14info = (struct cg14_info *) &fb->info.cg14;

	/* Set the 8-bpp mode */
	if (fb->open && fb->mmaped){
		volatile char *mcr = (char *)(&cg14info->regs->mcr);

		fb->info.cg14.video_mode = 8;
		*mcr = (*mcr & ~(CG14_MCR_PIXMODE_MASK));
	}
}

void
cg14_reset (fbinfo_t *fb)
{
	volatile char *mcr = &(fb->info.cg14.regs->mcr);

	*mcr = (*mcr & ~(CG14_MCR_PIXMODE_MASK));
}

__initfunc(void cg14_setup (fbinfo_t *fb, int slot, int con_node, uint cg14, int cg14_io))
{
	struct cg14_info *cg14info;
	uint bases [2];
	uint cg14regs = 0;
	struct cg14_regs *regs = 0;

	if (!cg14) {
		prom_getproperty (con_node, "address", (char *) &bases[0], 8);
		cg14 = bases[1];
		cg14regs = bases[0];
		fb->base = cg14;
		fb->info.cg14.regs = (struct cg14_regs *) cg14regs;
		regs = (struct cg14_regs *) cg14regs;
	}
	
	if (!cg14regs){
		printk ("The PROM does not have mapped the frame buffer or the registers\n"
			"Mr. Penguin can't use that");
		prom_halt ();
	}
	
	fb->type.fb_cmsize = 256;
	fb->mmap =  cg14_mmap;
	fb->loadcmap = cg14_cmap;
	fb->setcursor = cg14_setcursor;
	fb->setcursormap = cg14_setcursormap;
	fb->setcurshape = cg14_setcurshape;
	fb->ioctl = cg14_ioctl;
	fb->switch_from_graph = cg14_switch_from_graph;
	fb->postsetup = sun_cg_postsetup;
	fb->reset = cg14_reset;
	fb->blank = 0;
	fb->unblank = 0;
	fb->info.cg14.video_mode = 8;
	fb->emulations [1] = FBTYPE_SUN3COLOR;
	fb->type.fb_depth = 24;
	cg14info = (struct cg14_info *) &fb->info.cg14;
	cg14info->clut = (void *) (cg14regs + CG14_CLUT1);
	cg14info->cursor_regs = (void *) (cg14regs + CG14_CURSORREGS);

	/* If the bit is turned on, the card has 8 mb of ram, otherwise just 4 */
	cg14info->ramsize = (regs->vca & CG14_VCA_8MB_MASK ? 8 : 4) * 1024 * 1024;
	printk ("cgfourteen%d at 0x%8.8x with %d megs of RAM rev=%d, impl=%d\n",
		slot, cg14, cg14info->ramsize/(1024*1024), regs->rev >> 4, regs->rev & 0xf);
}
