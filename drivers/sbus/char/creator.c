/* $Id: creator.c,v 1.15 1998/04/13 07:26:55 davem Exp $
 * creator.c: Creator/Creator3D frame buffer driver
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>
#include <linux/config.h>

#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/fbio.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

/* These must be included after asm/fbio.h */
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/console_struct.h>
#include "fb.h"
#include "cg_common.h"

#define	FFB_SFB8R_VOFF		0x00000000
#define	FFB_SFB8G_VOFF		0x00400000
#define	FFB_SFB8B_VOFF		0x00800000
#define	FFB_SFB8X_VOFF		0x00c00000
#define	FFB_SFB32_VOFF		0x01000000
#define	FFB_SFB64_VOFF		0x02000000
#define	FFB_FBC_REGS_VOFF	0x04000000
#define	FFB_BM_FBC_REGS_VOFF	0x04002000
#define	FFB_DFB8R_VOFF		0x04004000
#define	FFB_DFB8G_VOFF		0x04404000
#define	FFB_DFB8B_VOFF		0x04804000
#define	FFB_DFB8X_VOFF		0x04c04000
#define	FFB_DFB24_VOFF		0x05004000
#define	FFB_DFB32_VOFF		0x06004000
#define	FFB_DFB422A_VOFF	0x07004000	/* DFB 422 mode write to A */
#define	FFB_DFB422AD_VOFF	0x07804000	/* DFB 422 mode with line doubling */
#define	FFB_DFB24B_VOFF		0x08004000	/* DFB 24bit mode write to B */
#define	FFB_DFB422B_VOFF	0x09004000	/* DFB 422 mode write to B */
#define	FFB_DFB422BD_VOFF	0x09804000	/* DFB 422 mode with line doubling */
#define	FFB_SFB16Z_VOFF		0x0a004000	/* 16bit mode Z planes */
#define	FFB_SFB8Z_VOFF		0x0a404000	/* 8bit mode Z planes */
#define	FFB_SFB422_VOFF		0x0ac04000	/* SFB 422 mode write to A/B */
#define	FFB_SFB422D_VOFF	0x0b404000	/* SFB 422 mode with line doubling */
#define	FFB_FBC_KREGS_VOFF	0x0bc04000
#define	FFB_DAC_VOFF		0x0bc06000
#define	FFB_PROM_VOFF		0x0bc08000
#define	FFB_EXP_VOFF		0x0bc18000

#define	FFB_SFB8R_POFF		0x04000000
#define	FFB_SFB8G_POFF		0x04400000
#define	FFB_SFB8B_POFF		0x04800000
#define	FFB_SFB8X_POFF		0x04c00000
#define	FFB_SFB32_POFF		0x05000000
#define	FFB_SFB64_POFF		0x06000000
#define	FFB_FBC_REGS_POFF	0x00600000
#define	FFB_BM_FBC_REGS_POFF	0x00600000
#define	FFB_DFB8R_POFF		0x01000000
#define	FFB_DFB8G_POFF		0x01400000
#define	FFB_DFB8B_POFF		0x01800000
#define	FFB_DFB8X_POFF		0x01c00000
#define	FFB_DFB24_POFF		0x02000000
#define	FFB_DFB32_POFF		0x03000000
#define	FFB_FBC_KREGS_POFF	0x00610000
#define	FFB_DAC_POFF		0x00400000
#define	FFB_PROM_POFF		0x00000000
#define	FFB_EXP_POFF		0x00200000

#define FFB_Y_BYTE_ADDR_SHIFT          11
#define FFB_Y_ADDR_SHIFT               13

#define FFB_PPC_ACE_DISABLE             1
#define FFB_PPC_ACE_AUX_ADD             3
#define FFB_PPC_ACE_SHIFT              18
#define FFB_PPC_DCE_DISABLE             2
#define FFB_PPC_DCE_SHIFT              16
#define FFB_PPC_ABE_DISABLE             2
#define FFB_PPC_ABE_SHIFT              14
#define FFB_PPC_VCE_DISABLE             1
#define FFB_PPC_VCE_2D                  2
#define FFB_PPC_VCE_SHIFT              12
#define FFB_PPC_APE_DISABLE             2
#define FFB_PPC_APE_SHIFT              10
#define FFB_PPC_CS_VARIABLE             2
#define FFB_PPC_CS_SHIFT                0

#define FFB_FBC_WB_A                    1
#define FFB_FBC_WB_SHIFT               29
#define FFB_FBC_PGE_MASK                3
#define FFB_FBC_BE_SHIFT                4
#define FFB_FBC_GE_SHIFT                2
#define FFB_FBC_RE_SHIFT                0

#define FFB_ROP_NEW                  0x83
#define FFB_ROP_RGB_SHIFT               0

#define FFB_UCSR_FIFO_MASK     0x00000fff
#define FFB_UCSR_RP_BUSY       0x02000000

struct ffb_fbc {
	u8		xxx1[0x60];
	volatile u32	by;
	volatile u32	bx;
	u32		xxx2;
	u32		xxx3;
	volatile u32	bh;
	volatile u32	bw;
	u8		xxx4[0x188];
	volatile u32	ppc;
	u32		xxx5;
	volatile u32	fg;
	volatile u32	bg;
	u8		xxx6[0x44];
	volatile u32	fbc;
	volatile u32	rop;
	u8		xxx7[0x34];
	volatile u32	pmask;
	u8		xxx8[12];
	volatile u32	clip0min;
	volatile u32	clip0max;
	volatile u32	clip1min;
	volatile u32	clip1max;
	volatile u32	clip2min;
	volatile u32	clip2max;
	volatile u32	clip3min;
	volatile u32	clip3max;
	u8		xxx9[0x3c];
	volatile u32	unk1;
	volatile u32	unk2;
	u8		xxx10[0x10];
	volatile u32	fontxy;
	volatile u32	fontw;
	volatile u32	fontinc;
	volatile u32	font;
	u8		xxx11[0x4dc];
	volatile u32	unk3;
	u8		xxx12[0xfc];
	volatile u32	ucsr;
};

struct ffb_dac {
	volatile u32	type;
	volatile u32	value;
	volatile u32	type2;
	volatile u32	value2;
};

static void
ffb_restore_palette (fbinfo_t *fbinfo)
{
}

static void ffb_blitc(unsigned short, int, int);
static void ffb_setw(int, int, unsigned short, int);
static void ffb_cpyw(int, int, unsigned short *, int);
static void ffb_fill(int, int, int *);
static void ffb_penguin(int,int,int);

static struct {
	unsigned long voff;
	unsigned long poff;
	unsigned long size;
} ffbmmap [] = {
	{ FFB_SFB8R_VOFF,		FFB_SFB8R_POFF,		0x0400000 },
	{ FFB_SFB8G_VOFF,		FFB_SFB8G_POFF,		0x0400000 },
	{ FFB_SFB8B_VOFF,		FFB_SFB8B_POFF,		0x0400000 },
	{ FFB_SFB8X_VOFF,		FFB_SFB8X_POFF,		0x0400000 },
	{ FFB_SFB32_VOFF,		FFB_SFB32_POFF,		0x1000000 },
	{ FFB_SFB64_VOFF,		FFB_SFB64_POFF,		0x2000000 },
	{ FFB_FBC_REGS_VOFF,		FFB_FBC_REGS_POFF,	0x0002000 },
	{ FFB_BM_FBC_REGS_VOFF,		FFB_BM_FBC_REGS_POFF,	0x0002000 },
	{ FFB_DFB8R_VOFF,		FFB_DFB8R_POFF,		0x0400000 },
	{ FFB_DFB8G_VOFF,		FFB_DFB8G_POFF,		0x0400000 },
	{ FFB_DFB8B_VOFF,		FFB_DFB8B_POFF,		0x0400000 },
	{ FFB_DFB8X_VOFF,		FFB_DFB8X_POFF,		0x0400000 },
	{ FFB_DFB24_VOFF,		FFB_DFB24_POFF,		0x1000000 },
	{ FFB_DFB32_VOFF,		FFB_DFB32_POFF,		0x1000000 },
	{ FFB_FBC_KREGS_VOFF,		FFB_FBC_KREGS_POFF,	0x0002000 },
	{ FFB_DAC_VOFF,			FFB_DAC_POFF,		0x0002000 },
	{ FFB_PROM_VOFF,		FFB_PROM_POFF,		0x0010000 },
	{ FFB_EXP_VOFF,			FFB_EXP_POFF,		0x0002000 }
};

/* Ugh: X wants to mmap a bunch of cute stuff at the same time :-( */
/* So, we just mmap the things that are being asked for */
static int
ffb_mmap (struct inode *inode, struct file *file, struct vm_area_struct *vma,
	  long base, fbinfo_t *fb)
{
	uint size, page, r, map_size;
	unsigned long map_offset = 0;
	int i;
	int alignment;
	struct vm_area_struct *vmm = NULL;

	size = vma->vm_end - vma->vm_start;
        if (vma->vm_offset & ~PAGE_MASK)
                return -ENXIO;

	/* Try to align RAM */
#define ALIGNMENT 0x400000
	map_offset = vma->vm_offset + size;
	alignment = 0;
	if (vma->vm_offset < FFB_FBC_REGS_VOFF) {
		vmm = find_vma(current->mm, vma->vm_start);
		alignment = ALIGNMENT - ((vma->vm_start - vma->vm_offset) & (ALIGNMENT - 1));
	} else if (vma->vm_offset >= FFB_DFB8R_VOFF && (vma->vm_offset & (ALIGNMENT - 1)) == 0x4000) {
		vmm = find_vma(current->mm, vma->vm_start);
		alignment = ALIGNMENT - (vma->vm_start & (ALIGNMENT - 1));
	}
	if (alignment == ALIGNMENT) alignment = 0;
	if (alignment && (!vmm || vmm->vm_start >= vma->vm_end + alignment)) {
		vma->vm_start += alignment;
		vma->vm_end += alignment;
	}
#undef ALIGNMENT

	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= FB_MMAP_VM_FLAGS;
	
	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		map_size = 0;
		for (i = 0; i < sizeof (ffbmmap) / sizeof (ffbmmap[0]); i++)
			if (ffbmmap[i].voff == vma->vm_offset+page) {
				map_size = ffbmmap[i].size;
				map_offset = (fb->info.ffb.physbase + ffbmmap[i].poff) & _PAGE_PADDR;
			}

		if (!map_size){
			page += PAGE_SIZE;
			continue;
		}
		if (page + map_size > size)
			map_size = size - page;
		r = io_remap_page_range (vma->vm_start+page,
					 map_offset,
					 map_size, vma->vm_page_prot, 0);
		if (r)
			return -EAGAIN;
		page += map_size;
	}

	vma->vm_file = file;
	file->f_count++;
        return 0;
}

/* XXX write body of these two... */
static inline int
ffb_wid_get (fbinfo_t *fb, struct fb_wid_list *wl)
{
	struct fb_wid_item *wi;
	struct fb_wid_list wlt;
	struct fb_wid_item wit[30];
	char *km = NULL;
	int i, j;
	int err;
	
#ifdef CONFIG_SPARC32_COMPAT
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		if (copy_from_user (&wlt, wl, 2 * sizeof (__u32)) ||
		    __get_user ((long)wlt.wl_list, (((__u32 *)wl)+2)))
			return -EFAULT;
	} else
#endif
	if (copy_from_user (&wlt, wl, sizeof (wlt)))
		return -EFAULT;
	if (wlt.wl_count <= 30) {
		if (copy_from_user (wit, wlt.wl_list, wlt.wl_count * sizeof(*wi)))
			return -EFAULT;
		wi = wit;
	} else if (wlt.wl_count > 120)
		return -EINVAL;
	else {
		wi = (struct fb_wid_item *) km = kmalloc (wlt.wl_count * sizeof (*wi), GFP_KERNEL);
		if (!wi) return -ENOMEM;
		if (copy_from_user (wi, wlt.wl_list, wlt.wl_count * sizeof(*wi))) {
			kfree (wi);
			return -EFAULT;
		}
	}
	for (i = 0; i < wlt.wl_count; i++, wi++) {
		switch (wi->wi_type) {
		case FB_WID_DBL_8: j = (wi->wi_index & 0xf) + 0x40; break;
		case FB_WID_DBL_24: j = wi->wi_index & 0x3f; break;
		default: return -EINVAL;
		}
		wi->wi_attrs = 0xffff;
		for (j = 0; j < 32; j++)
			wi->wi_values [j] = 0;
	}
	err = 0;
	if (copy_to_user (wlt.wl_list, km ? km : (char *)wit, wlt.wl_count * sizeof (*wi)))
		err = -EFAULT;
	if (km)
		kfree (km);
	return err;
}

static inline int
ffb_wid_put (fbinfo_t *fb, struct fb_wid_list *wl)
{
	struct fb_wid_item *wi;
	struct fb_wid_list wlt;
	struct fb_wid_item wit[30];
	char *km = NULL;
	int i, j;
	
#ifdef CONFIG_SPARC32_COMPAT
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		if (copy_from_user (&wlt, wl, 2 * sizeof (__u32)) ||
		    __get_user ((long)wlt.wl_list, (((__u32 *)wl)+2)))
			return -EFAULT;
	} else
#endif
	if (copy_from_user (&wlt, wl, sizeof (wlt)))
		return -EFAULT;
	if (wlt.wl_count <= 30) {
		if (copy_from_user (wit, wlt.wl_list, wlt.wl_count * sizeof(*wi)))
			return -EFAULT;
		wi = wit;
	} else if (wlt.wl_count > 120)
		return -EINVAL;
	else {
		wi = (struct fb_wid_item *) km = kmalloc (wlt.wl_count * sizeof (*wi), GFP_KERNEL);
		if (!wi) return -ENOMEM;
		if (copy_from_user (wi, wlt.wl_list, wlt.wl_count * sizeof(*wi))) {
			kfree (wi);
			return -EFAULT;
		}
	}
	for (i = 0; i < wlt.wl_count; i++, wi++) {
		switch (wi->wi_type) {
		case FB_WID_DBL_8: j = (wi->wi_index & 0xf) + 0x40; break;
		case FB_WID_DBL_24: j = wi->wi_index & 0x3f; break;
		default: return -EINVAL;
		}
		/* x =	wi->wi_values [j] */;
	}
	if (km)
		kfree (km);
	return 0;
}

static inline void 
ffb_curs_enable (fbinfo_t *fb, int enable)
{
	struct ffb_dac *dac = fb->info.ffb.dac;
	
	dac->type2 = 0x100;
	if (fb->info.ffb.dac_rev <= 2)
		dac->value2 = enable ? 3 : 0;
	else
		dac->value2 = enable ? 0 : 3;
}

static void
ffb_setcursormap (fbinfo_t *fb, unsigned char *red,
				 unsigned char *green,
				 unsigned char *blue)
{
	struct ffb_dac *dac = fb->info.ffb.dac;
	
	ffb_curs_enable (fb, 0);
	dac->type2 = 0x102;
	dac->value2 = (red[0] | (green[0]<<8) | (blue[0]<<16));
	dac->value2 = (red[1] | (green[1]<<8) | (blue[1]<<16));
}

/* Set cursor shape */
static void
ffb_setcurshape (fbinfo_t *fb)
{
	struct ffb_dac *dac = fb->info.ffb.dac;
	int i, j;

	ffb_curs_enable (fb, 0);
	for (j = 0; j < 2; j++) {
		dac->type2 = j ? 0 : 0x80;
		for (i = 0; i < 0x40; i++) {
			if (fb->cursor.size.fbx <= 32) {
				dac->value2 = fb->cursor.bits [j][i];
				dac->value2 = 0;
			} else {
				dac->value2 = fb->cursor.bits [j][2*i];
				dac->value2 = fb->cursor.bits [j][2*i+1];
			}
		}
	}	
}

/* Load cursor information */
static void
ffb_setcursor (fbinfo_t *fb)
{
	struct ffb_dac *dac = fb->info.ffb.dac;
	struct cg_cursor *c = &fb->cursor;

	dac->type2 = 0x104;
/* Should this be just 0x7ff?? Should I do some margin handling and setcurshape
   in that case? */
	dac->value2 = (((c->cpos.fby - c->chot.fby) & 0xffff) << 16)
	              |((c->cpos.fbx - c->chot.fbx) & 0xffff);
	ffb_curs_enable (fb, fb->cursor.enable);
}

static void
ffb_blank (fbinfo_t *fb)
{
/* XXX Write this */
}

static void
ffb_unblank (fbinfo_t *fb)
{
/* XXX Write this */
}

static int ffb_clutstore (fbinfo_t *fb, int offset, int count)
{
	int i;
	u32 *clut = fb->info.ffb.clut + offset;
	struct ffb_dac *dac = fb->info.ffb.dac;
	
	dac->type = 0x2000 | offset;
	for (i = 0; i < count; i++)
		dac->value = *clut++;	/* Feed the colors in :)) */
	return 0;
}

static int ffb_clutpost (fbinfo_t *fb, struct fb_clut *fc)
{
	int i;
	u32 *clut;
	struct fb_clut fct;
	u8 red[256], green[256], blue[256];

#ifdef CONFIG_SPARC32_COMPAT
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		if (copy_from_user (&fct, fc, 3 * sizeof (__u32)) ||
		    __get_user ((long)fct.red, &(((struct fb_clut32 *)fc)->red)) ||
		    __get_user ((long)fct.green, &(((struct fb_clut32 *)fc)->green)) ||
		    __get_user ((long)fct.blue, &(((struct fb_clut32 *)fc)->blue)))
			return -EFAULT;
	} else
#endif
	if (copy_from_user (&fct, fc, sizeof (struct fb_clut)))
		return -EFAULT;
	i = fct.offset + fct.count;
	if (fct.clutid || i <= 0 || i > 256) return -EINVAL;
	if (copy_from_user (red, fct.red, fct.count) ||
	    copy_from_user (green, fct.green, fct.count) ||
	    copy_from_user (blue, fct.blue, fct.count))
		return -EFAULT;
	clut = fb->info.ffb.clut + fct.offset;
	for (i = 0; i < fct.count; i++)
		*clut++ = ((red[i])|(green[i]<<8)|(blue[i]<<16));
	return ffb_clutstore (fb, fct.offset, fct.count);
}

static int ffb_clutread (fbinfo_t *fb, struct fb_clut *fc)
{
/* XXX write this */
	return 0;
}

static void
ffb_loadcmap (fbinfo_t *fb, int index, int count)
{
	u32 *clut = fb->info.ffb.clut + index;
	int i, j = count;
	
	for (i = index; j--; i++)
		*clut++ = ((fb->color_map CM(i,0))) |
			  ((fb->color_map CM(i,1)) << 8) |
			  ((fb->color_map CM(i,2)) << 16);
	ffb_clutstore (fb, index, count);
}

/* Handle ffb-specific ioctls */
static int
ffb_ioctl (struct inode *inode, struct file *file, unsigned cmd, unsigned long arg, fbinfo_t *fb)
{
	switch (cmd) {
	case FBIO_WID_GET:
		return ffb_wid_get (fb, (struct fb_wid_list *)arg);
	case FBIO_WID_PUT:
		return ffb_wid_put (fb, (struct fb_wid_list *)arg);
	case FFB_CLUTPOST:
		return ffb_clutpost (fb, (struct fb_clut *)arg);
	case FFB_CLUTREAD:
		return ffb_clutread (fb, (struct fb_clut *)arg);
		
	default:
		return -ENOSYS;
	}
}

void
ffb_reset (fbinfo_t *fb)
{
	if (fb == &fbinfo[0])
		sbus_hw_hide_cursor ();
}

__initfunc(static void ffb_postsetup (fbinfo_t *fb))
{
	fb->info.ffb.clut = kmalloc(256 * 4, GFP_ATOMIC);
        fb->color_map = kmalloc(256 * 3, GFP_ATOMIC);
}

__initfunc(void creator_setup (fbinfo_t *fb, int slot, int ffb_node, unsigned long ffb, int ffb_io))
{
	struct ffb_info *ffbinfo;
	struct linux_prom64_registers regs[2*PROMREG_MAX];
	int type;

	if (prom_getproperty(ffb_node, "reg", (void *) regs, sizeof(regs)) <= 0)
		return;
	ffb = (long)__va(regs[0].phys_addr);
	printk ("creator%d at 0x%016lx ", slot, ffb);
	
	fb->base = ffb;	/* ??? */
	
	/* Fill in parameters we left out */
	fb->type.fb_cmsize = 256;
	fb->mmap = ffb_mmap;
	fb->loadcmap = ffb_loadcmap;
	fb->reset = ffb_reset;
	fb->blank = ffb_blank;
	fb->unblank = ffb_unblank;
	fb->setcursor = ffb_setcursor;
	fb->setcursormap = ffb_setcursormap;
	fb->setcurshape = ffb_setcurshape;
	fb->postsetup = ffb_postsetup;
	fb->blitc = ffb_blitc;
	fb->setw = ffb_setw;
	fb->cpyw = ffb_cpyw;
	fb->fill = ffb_fill;
	fb->draw_penguin = ffb_penguin;
	fb->ioctl = ffb_ioctl;
	fb->cursor.hwsize.fbx = 64;
	fb->cursor.hwsize.fby = 64;
	fb->type.fb_depth = 24;
	
	ffbinfo = (struct ffb_info *) &fb->info.ffb;
	
	ffbinfo->physbase = ffb;

	ffbinfo->fbc = (struct ffb_fbc *)(ffb + FFB_FBC_REGS_POFF);
	ffbinfo->dac = (struct ffb_dac *)(ffb + FFB_DAC_POFF);

	ffbinfo->dac->type = 0x8000;
	ffbinfo->dac_rev = (ffbinfo->dac->value >> 0x1c);
	
	if (slot == sun_prom_console_id)
		fb_restore_palette = ffb_restore_palette;
		
	type = prom_getintdefault (ffb_node, "board_type", 8);

	/* Initialize Brooktree DAC */

	printk("TYPE %d DAC %d\n", type, ffbinfo->dac_rev);
		
	if (slot && sun_prom_console_id == slot)
		return;

	/* Reset the ffb */
	ffb_reset(fb);
	
	if (!slot) {
		/* Enable Video */
		ffb_unblank(fb);
	} else {
		ffb_blank(fb);
	}
}

extern unsigned char vga_font[];

#define FFB_BLITC_START(attr) \
	{ \
		register struct ffb_fbc *ffb = fbinfo[0].info.ffb.fbc; \
		register u32 *clut = fbinfo[0].info.ffb.clut; \
		int i; \
		ffb->ppc = 0x203; \
		ffb->fg = clut[attr & 0xf]; \
		ffb->fbc = 0x2000707f; \
		ffb->rop = 0x83; \
		ffb->pmask = 0xffffffff; \
		ffb->bg = clut[attr>>4];
#define FFB_BLITC_BODY4(count,x,y,start,action) \
		while (count >= 4) { \
			count -= 4; \
		    	ffb->fontw = 32; \
		    	ffb->fontinc = 0x10000; \
		    	ffb->fontxy = (y << 16) + x; \
		    	x += 32; \
		    	start; \
		    	for (i = 0; i < CHAR_HEIGHT; i++) { \
		    		action; \
		    	} \
		}
#define FFB_BLITC_BODY1(x,y,action) \
		    	ffb->fontw = 8; \
		    	ffb->fontinc = 0x10000; \
		    	ffb->fontxy = (y << 16) + x; \
		    	x += 8; \
		    	for (i = 0; i < CHAR_HEIGHT; i++) { \
		    		action; \
		    	}
#define FFB_BLITC_END \
	}
	
static void ffb_blitc(unsigned short charattr, int xoff, int yoff)
{
	unsigned char attrib = CHARATTR_TO_SUNCOLOR(charattr);
	unsigned char *p = &vga_font[((unsigned char)charattr) << 4];
	FFB_BLITC_START(attrib)
	FFB_BLITC_BODY1(xoff, yoff, ffb->font=((*p++) << 24))
	FFB_BLITC_END
}

static void ffb_setw(int xoff, int yoff, unsigned short c, int count)
{
	unsigned char attrib = CHARATTR_TO_SUNCOLOR(c);
	unsigned char *p = &vga_font[((unsigned char)c) << 4];
	register unsigned char *q;
	register uint l;
	FFB_BLITC_START(attrib)
	if (count >= 4) {
		FFB_BLITC_BODY4(count, xoff, yoff, q = p, 
			l = *q++;
			l |= l << 8;
			l |= l << 16;
			ffb->font=l)
	}
	while (count) {
		count--;
		q = p;
		FFB_BLITC_BODY1(xoff, yoff, ffb->font=((*q++) << 24));
	}
	FFB_BLITC_END
}

static void ffb_cpyw(int xoff, int yoff, unsigned short *p, int count)
{
	unsigned char attrib = CHARATTR_TO_SUNCOLOR(*p);
	unsigned char *p1, *p2, *p3, *p4;
	FFB_BLITC_START(attrib)
	if (count >= 4) {
		FFB_BLITC_BODY4(count, xoff, yoff, 
			p1 = &vga_font[((unsigned char)*p++) << 4];
			p2 = &vga_font[((unsigned char)*p++) << 4];
			p3 = &vga_font[((unsigned char)*p++) << 4];
			p4 = &vga_font[((unsigned char)*p++) << 4], 
			ffb->font=((uint)*p4++) | ((((uint)*p3++) | ((((uint)*p2++) | (((uint)*p1++) << 8)) << 8)) << 8))
	}
	while (count) {
		count--;
		p1 = &vga_font[((unsigned char)*p++) << 4];
		FFB_BLITC_BODY1(xoff, yoff, ffb->font=((*p1++) << 24));
	}
	FFB_BLITC_END
}

#if 0
#define FFB_FILL_START(attr) \
	{ \
		register struct ffb_fbc *ffb = fbinfo[0].info.ffb.fbc; \
		register u32 *clut = fbinfo[0].info.ffb.clut; \
		ffb->ppc =0x1803; \
		ffb->fg = clut[attr & 0xf]; \
		ffb->fbc = 0x2000707f; \
		ffb->rop = 0x83; \
		ffb->pmask = 0xffffffff; \
		ffb->unk2 = 8;
#define FFB_FILL_END \
	}
#else
#define FFB_FILL_START(attr) \
	{ \
		register struct ffb_fbc *ffb = fbinfo[0].info.ffb.fbc; \
		ffb->ppc = 0x1803; \
		ffb->fg = 0; \
		ffb->fbc = 0x2000707f; \
		ffb->rop = 0x83; \
		ffb->pmask = 0xffffffff; \
		ffb->unk2 = 8;
#define FFB_FILL_END \
	}
#endif	
	
static void ffb_fill(int attrib, int count, int *boxes)
{
	attrib = 5;
	FFB_FILL_START(attrib)
	while (count-- > 0) {
		ffb->by = boxes[1];
		ffb->bx = boxes[0];
		ffb->bh = boxes[3];
		ffb->bw = boxes[2];
		boxes += 4;
	}
	FFB_FILL_END
}

__initfunc(void ffb_penguin(int x_margin, int y_margin, int ncpus))
{
	int i, j, k;
	u32 *p, *q;
	unsigned char *r;
	unsigned char c;
	
	p = (u32 *)(fbinfo[0].info.ffb.physbase + FFB_DFB24_POFF + y_margin*8192 + x_margin*4);
	for (i = 0; i < 80; i++, p += 2048) {
		q = p;
		for (j = 0; j < ncpus; j++) {
			r = linux_logo + 80 * i;
			for (k = 0; k < 80; k++, r++) {
				c = *r - 32;
				*q++ = (linux_logo_red[c]) | 
				       (linux_logo_green[c]<<8) | 
				       (linux_logo_blue[c]<<16);
			}
			q += 8;
		}
	}
}
