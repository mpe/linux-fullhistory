/* $Id: tcx.c,v 1.12 1997/04/14 17:04:51 jj Exp $
 * tcx.c: SUNW,tcx 24/8bit frame buffer driver
 *
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 */

#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>

#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/fbio.h>
#include <asm/pgtable.h>

#include "../../char/vt_kern.h"
#include "../../char/selection.h"
#include "../../char/console_struct.h"
#include "fb.h"
#include "cg_common.h"

/* Offset of interesting structures in the tcx registers */
#define TCX_RAM8BIT_OFFSET	0
#define TCX_CONTROLPLANE_OFFSET 4
#define TCX_BROOKTREE_OFFSET 	8
#define TCX_THC_OFFSET		9
#define TCX_TEC_OFFSET		7

/* THC definitions */
#define TCX_THC_MISC_REV_SHIFT       16
#define TCX_THC_MISC_REV_MASK        15
#define TCX_THC_MISC_RESET           (1 << 12)
#define TCX_THC_MISC_VIDEO           (1 << 10)
#define TCX_THC_MISC_SYNC            (1 << 9)
#define TCX_THC_MISC_VSYNC           (1 << 8)
#define TCX_THC_MISC_SYNC_ENAB       (1 << 7)
#define TCX_THC_MISC_CURS_RES        (1 << 6)
#define TCX_THC_MISC_INT_ENAB        (1 << 5)
#define TCX_THC_MISC_INT             (1 << 4)
#define TCX_THC_MISC_INIT            0x9f
#define TCX_THC_REV_REV_SHIFT        20
#define TCX_THC_REV_REV_MASK         15
#define TCX_THC_REV_MINREV_SHIFT     28
#define TCX_THC_REV_MINREV_MASK      15

/* The contents are unknown */
struct tcx_tec {
	volatile int tec_matrix;
	volatile int tec_clip;
	volatile int tec_vdc;
};

struct tcx_thc {
	volatile uint thc_rev;
        uint thc_pad0[511];
	volatile uint thc_hs;		/* hsync timing */
	volatile uint thc_hsdvs;
	volatile uint thc_hd;
	volatile uint thc_vs;		/* vsync timing */
	volatile uint thc_vd;
	volatile uint thc_refresh;
	volatile uint thc_misc;
	uint thc_pad1[56];
	volatile uint thc_cursxy;	/* cursor x,y position (16 bits each) */
	volatile uint thc_cursmask[32];	/* cursor mask bits */
	volatile uint thc_cursbits[32];	/* what to show where mask enabled */
};

static void
tcx_restore_palette (fbinfo_t *fbinfo)
{
	volatile struct bt_regs *bt;

	bt = fbinfo->info.tcx.bt;
	bt->addr = 0;
	bt->color_map = 0xffffffff;
	bt->color_map = 0xffffffff;
	bt->color_map = 0xffffffff;
}

static void
tcx_set_control_plane (fbinfo_t *fb)
{
	register uint *p, *pend;
	
	p = fb->info.tcx.tcx_cplane;
	if (!p) return;
	for (pend = p + (fb->info.tcx.tcx_sizes [TCX_CONTROLPLANE_OFFSET] >> 2); p < pend; p++)
		*p &= 0xffffff;
}

static void
tcx_switch_from_graph (void)
{
	fbinfo_t *fb = &(fbinfo [0]);

	/* Reset control plane to 8bit mode if necessary */
	if (fb->open && fb->mmaped)
		tcx_set_control_plane (fb);
}

/* Ugh: X wants to mmap a bunch of cute stuff at the same time :-( */
/* So, we just mmap the things that are being asked for */
static int
tcx_mmap (struct inode *inode, struct file *file, struct vm_area_struct *vma,
	  long base, fbinfo_t *fb)
{
	uint size, page, r, map_size;
	uint map_offset = 0, i;
	long offsets[13] = { -1, TCX_RAM24BIT, TCX_UNK3, TCX_UNK4,
			     -1, TCX_UNK6, TCX_UNK7,
			     -1, -1, -1, TCX_UNK2, TCX_DHC, TCX_ALT };
			     
	size = vma->vm_end - vma->vm_start;
        if (vma->vm_offset & ~PAGE_MASK)
                return -ENXIO;

	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= FB_MMAP_VM_FLAGS;
	
	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		switch (vma->vm_offset+page){
		case TCX_RAM8BIT:
			map_size = fb->type.fb_size;
			map_offset = get_phys ((unsigned long) fb->base);
			break;
		case TCX_TEC:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.tcx.tec);
			break;
		case TCX_BTREGS:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.tcx.bt);
			break;
		case TCX_THC:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.tcx.thc);
			break;
		case TCX_CONTROLPLANE:
			if (fb->info.tcx.tcx_cplane) {
				map_size = fb->info.tcx.tcx_sizes [TCX_CONTROLPLANE_OFFSET];
				map_offset = get_phys ((unsigned long)fb->info.tcx.tcx_cplane);
			} else
				map_size = 0;
			break;
		default:
			map_size = 0;
			for (i = 0; i < 13; i++)
				if (offsets [i] == vma->vm_offset+page) {
					if ((map_size = fb->info.tcx.tcx_sizes [i]))
						map_offset = fb->info.tcx.tcx_offsets [i];
					break;
				}
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
tcx_loadcmap (fbinfo_t *fb, int index, int count)
{
	struct bt_regs *bt = fb->info.tcx.bt;
	int i;
	
	bt->addr = index << 24;
	for (i = index; count--; i++){
		bt->color_map = fb->color_map CM(i,0) << 24;
		bt->color_map = fb->color_map CM(i,1) << 24;
		bt->color_map = fb->color_map CM(i,2) << 24;
	}
	bt->addr = 0;
}

static void
tcx_setcursormap (fbinfo_t *fb, unsigned char *red,
				 unsigned char *green,
				 unsigned char *blue)
{
	struct bt_regs *bt = fb->info.tcx.bt;

	/* Note the 2 << 24 is different from cg6's 1 << 24 */
	bt->addr = 2 << 24;
	bt->cursor = red[0] << 24;
	bt->cursor = green[0] << 24;
	bt->cursor = blue[0] << 24;
	bt->addr = 3 << 24;
	bt->cursor = red[1] << 24;
	bt->cursor = green[1] << 24;
	bt->cursor = blue[1] << 24;
	bt->addr = 0;
}

/* Load cursor information */
static void
tcx_setcursor (fbinfo_t *fb)
{
	uint v;
	struct cg_cursor *c = &fb->cursor;

	if (c->enable){
		v = ((c->cpos.fbx - c->chot.fbx) << 16)
		   |((c->cpos.fby - c->chot.fby) & 0xffff);
	} else {
		/* Magic constant to turn off the cursor */
		v = ((65536-32) << 16) | (65536-32);
	}
	fb->info.tcx.thc->thc_cursxy = v;
}

/* Set cursor shape */
static void
tcx_setcurshape (fbinfo_t *fb)
{
	struct tcx_thc *thc = fb->info.tcx.thc;
	int i;
	
	for (i = 0; i < 32; i++){
		thc->thc_cursmask [i] = fb->cursor.bits[0][i];
		thc->thc_cursbits [i] = fb->cursor.bits[1][i];
	}
}

static void
tcx_blank (fbinfo_t *fb)
{
	fb->info.tcx.thc->thc_misc &= ~TCX_THC_MISC_VIDEO;
}

static void
tcx_unblank (fbinfo_t *fb)
{
	fb->info.tcx.thc->thc_misc |= TCX_THC_MISC_VIDEO;
}

void
tcx_reset (fbinfo_t *fb)
{
	struct tcx_info *tcx = &(fb->info.tcx);

	if (fb->setcursor)
		sun_hw_hide_cursor ();
	/* Reset control plane to 8bit mode if necessary */
	if (fb->open && fb->mmaped)
		tcx_set_control_plane (fb);

	/* Turn off stuff in the Transform Engine. */
	tcx->tec->tec_matrix = 0;
	tcx->tec->tec_clip = 0;
	tcx->tec->tec_vdc = 0;

	/* Enable cursor in Brooktree DAC. */
	tcx->bt->addr = 0x06 << 24;
	tcx->bt->control |= 0x03 << 24;
}

__initfunc(void tcx_setup (fbinfo_t *fb, int slot, int node, u32 tcx, struct linux_sbus_device *sbdp))
{
	struct tcx_info *tcxinfo;
	int i;

	printk ("tcx%d at 0x%8.8x ", slot, tcx);
	
	/* Fill in parameters we left out */
	fb->type.fb_cmsize = 256;
	fb->mmap = tcx_mmap;
	fb->loadcmap = tcx_loadcmap;
	fb->reset = tcx_reset;
	fb->blank = tcx_blank;
	fb->unblank = tcx_unblank;
	fb->emulations [1] = FBTYPE_SUN3COLOR;
	fb->emulations [2] = FBTYPE_MEMCOLOR;
	fb->switch_from_graph = tcx_switch_from_graph;
	fb->postsetup = sun_cg_postsetup;
	
	tcxinfo = (struct tcx_info *) &fb->info.tcx;
	
	memset (tcxinfo, 0, sizeof(struct tcx_info));
	
	for (i = 0; i < 13; i++)
		tcxinfo->tcx_offsets [i] = (long)(sbdp->reg_addrs [i].phys_addr);
		
	/* Map the hardware registers */
	tcxinfo->bt = sparc_alloc_io((u32)tcxinfo->tcx_offsets [TCX_BROOKTREE_OFFSET], 0,
		 sizeof (struct bt_regs),"tcx_dac", fb->space, 0);
	tcxinfo->thc = sparc_alloc_io((u32)tcxinfo->tcx_offsets [TCX_THC_OFFSET], 0,
		 sizeof (struct tcx_thc), "tcx_thc", fb->space, 0);
	tcxinfo->tec = sparc_alloc_io((u32)tcxinfo->tcx_offsets [TCX_TEC_OFFSET], 0,
		 sizeof (struct tcx_tec), "tcx_tec", fb->space, 0);
	if (!fb->base){
		fb->base = (uint)
			sparc_alloc_io((u32)tcxinfo->tcx_offsets [TCX_RAM8BIT_OFFSET], 0,
				       fb->type.fb_size, "tcx_ram", fb->space, 0);
	}
	
	if (prom_getbool (node, "hw-cursor")) {
		fb->setcursor = tcx_setcursor;
		fb->setcursormap = tcx_setcursormap;
		fb->setcurshape = tcx_setcurshape;
	}
		
	if (!slot) {
		fb_restore_palette = tcx_restore_palette;
	}
	
	i = fb->type.fb_size;
	tcxinfo->tcx_sizes[2] = i << 3;
	tcxinfo->tcx_sizes[3] = i << 3;
	tcxinfo->tcx_sizes[10] = 0x20000;
	tcxinfo->tcx_sizes[11] = PAGE_SIZE;
	tcxinfo->tcx_sizes[12] = PAGE_SIZE;
	
	if (prom_getbool (node, "tcx-8-bit"))
		tcxinfo->lowdepth = 1;
		
	if (!tcxinfo->lowdepth) {
		tcxinfo->tcx_sizes[1] = i << 2;
		tcxinfo->tcx_sizes[4] = i << 2;
		tcxinfo->tcx_sizes[5] = i << 3;
		tcxinfo->tcx_sizes[6] = i << 3;
		fb->type.fb_depth = 24;
		tcxinfo->tcx_cplane =
		   sparc_alloc_io((u32)tcxinfo->tcx_offsets[TCX_CONTROLPLANE_OFFSET], 0,
				  tcxinfo->tcx_sizes [TCX_CONTROLPLANE_OFFSET],
				  "tcx_cplane", fb->space, 0);
	}
	
	/* Initialize Brooktree DAC */
	tcxinfo->bt->addr = 0x04 << 24;		/* color planes */
	tcxinfo->bt->control = 0xff << 24;
	tcxinfo->bt->addr = 0x05 << 24;
	tcxinfo->bt->control = 0x00 << 24;
	tcxinfo->bt->addr = 0x06 << 24;		/* overlay plane */
	tcxinfo->bt->control = 0x73 << 24;
	tcxinfo->bt->addr = 0x07 << 24;
	tcxinfo->bt->control = 0x00 << 24;

	printk("Rev %d.%d %s\n",
		(tcxinfo->thc->thc_rev >> TCX_THC_REV_REV_SHIFT) & TCX_THC_REV_REV_MASK,
		(tcxinfo->thc->thc_rev >> TCX_THC_REV_MINREV_SHIFT) & TCX_THC_REV_MINREV_MASK,
		tcxinfo->lowdepth ? "8-bit only" : "24-bit depth");
	
	/* Reset the tcx */
	tcx_reset(fb);

	if (!slot)	
	/* Enable Video */
		tcx_unblank (fb);
	else
		tcx_blank (fb);
}
