/* $Id: cgthree.c,v 1.18 1997/04/16 17:51:09 jj Exp $
 * cgtree.c: cg3 frame buffer driver
 *
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 Eddie C. Dost   (ecd@skynet.be)
 *
 * Support for cgRDI added, Nov/96, jj.
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


/* Control Register Constants */
#define CG3_CR_ENABLE_INTS	0x80
#define CG3_CR_ENABLE_VIDEO	0x40
#define CG3_CR_ENABLE_TIMING	0x20
#define CG3_CR_ENABLE_CURCMP	0x10
#define CG3_CR_XTAL_MASK	0x0c
#define CG3_CR_DIVISOR_MASK	0x03

/* Status Register Constants */
#define CG3_SR_PENDING_INT	0x80
#define CG3_SR_RES_MASK		0x70
#define CG3_SR_1152_900_76_A	0x40
#define CG3_SR_1152_900_76_B	0x60
#define CG3_SR_ID_MASK		0x0f
#define CG3_SR_ID_COLOR		0x01
#define CG3_SR_ID_MONO		0x02
#define CG3_SR_ID_MONO_ECL	0x03


enum cg3_type {
	CG3_AT_66HZ = 0,
	CG3_AT_76HZ,
	CG3_RDI
};


struct cg3_regs {
	struct bt_regs	cmap;
	volatile u8	control;
	volatile u8	status;
	volatile u8	cursor_start;
	volatile u8	cursor_end;
	volatile u8	h_blank_start;
	volatile u8	h_blank_end;
	volatile u8	h_sync_start;
	volatile u8	h_sync_end;
	volatile u8	comp_sync_end;
	volatile u8	v_blank_start_high;
	volatile u8	v_blank_start_low;
	volatile u8	v_blank_end;
	volatile u8	v_sync_start;
	volatile u8	v_sync_end;
	volatile u8	xfer_holdoff_start;
	volatile u8	xfer_holdoff_end;
};

/* The cg3 palette is loaded with 4 color values at each time  */
/* so you end up with: (rgb)(r), (gb)(rg), (b)(rgb), and so on */
static void
cg3_loadcmap (fbinfo_t *fb, int index, int count)
{
	struct bt_regs *bt = &fb->info.cg3.regs->cmap;
	int *i, steps;

	i = (((int *)fb->color_map) + D4M3(index));
	steps = D4M3(index+count-1) - D4M3(index)+3;

	*(volatile u8 *)&bt->addr = (u8)D4M4(index);
	while (steps--)
		bt->color_map = *i++;
}

/* The cg3 is presumed to emulate a cg4, I guess older programs will want that
 * addresses above 0x4000000 are for cg3, below that it's cg4 emulation.
 */
static int
cg3_mmap (struct inode *inode, struct file *file, struct vm_area_struct *vma,
	  long base, fbinfo_t *fb)
{
	uint  size, page, r, map_size;
	uint map_offset = 0;
	
	size = vma->vm_end - vma->vm_start;
        if (vma->vm_offset & ~PAGE_MASK)
                return -ENXIO;

	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= FB_MMAP_VM_FLAGS; 
	
	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		switch (vma->vm_offset+page){
		case CG3_MMAP_OFFSET:
			map_size = size-page;
			map_offset = get_phys ((unsigned long) fb->base);
			if (map_size > fb->type.fb_size)
				map_size = fb->type.fb_size;
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
cg3_blank (fbinfo_t *fb)
{
	fb->info.cg3.regs->control &= ~CG3_CR_ENABLE_VIDEO;
}

static void
cg3_unblank (fbinfo_t *fb)
{
        fb->info.cg3.regs->control |= CG3_CR_ENABLE_VIDEO;
}


static u8 cg3regvals_66hz[] __initdata = {	/* 1152 x 900, 66 Hz */
	0x14, 0xbb,	0x15, 0x2b,	0x16, 0x04,	0x17, 0x14,
	0x18, 0xae,	0x19, 0x03,	0x1a, 0xa8,	0x1b, 0x24,
	0x1c, 0x01,	0x1d, 0x05,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

static u8 cg3regvals_76hz[] __initdata = {	/* 1152 x 900, 76 Hz */
	0x14, 0xb7,	0x15, 0x27,	0x16, 0x03,	0x17, 0x0f,
	0x18, 0xae,	0x19, 0x03,	0x1a, 0xae,	0x1b, 0x2a,
	0x1c, 0x01,	0x1d, 0x09,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x24,	0
};

static u8 cg3regvals_rdi[] __initdata = {	/* 640 x 480, cgRDI */
	0x14, 0x70,	0x15, 0x20,	0x16, 0x08,	0x17, 0x10,
	0x18, 0x06,	0x19, 0x02,	0x1a, 0x31,	0x1b, 0x51,
	0x1c, 0x06,	0x1d, 0x0c,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x22,	0
};

static u8 *cg3_regvals[] __initdata = {
	cg3regvals_66hz, cg3regvals_76hz, cg3regvals_rdi
};

static u_char cg3_dacvals[] __initdata = {
	4, 0xff,	5, 0x00,	6, 0x70,	7, 0x00,	0
};


__initfunc(void cg3_setup (fbinfo_t *fb, int slot, u32 cg3, int cg3_io,
			   struct linux_sbus_device *sbdp))
{
	struct cg3_info *cg3info = (struct cg3_info *) &fb->info.cg3;

	if (strstr (sbdp->prom_name, "cgRDI")) {
		char buffer[40];
		char *p;
		int w, h;
		
		prom_getstring (sbdp->prom_node, "params",
				buffer, sizeof(buffer));
		if (*buffer) {
			w = simple_strtoul (buffer, &p, 10);
			if (w && *p == 'x') {
				h = simple_strtoul (p + 1, &p, 10);
				if (h && *p == '-') {
					fb->type.fb_width = w;
					fb->type.fb_height = h;
				}
			}
		}
		printk ("cgRDI%d at 0x%8.8x\n", slot, cg3);
		cg3info->cgrdi = 1;
	} else {
		printk ("cgthree%d at 0x%8.8x\n", slot, cg3);
		cg3info->cgrdi = 0;
	}
	
	/* Fill in parameters we left out */
	fb->type.fb_cmsize = 256;
	fb->mmap = cg3_mmap;
	fb->loadcmap = cg3_loadcmap;
	fb->postsetup = sun_cg_postsetup;
	fb->ioctl = 0; /* no special ioctls */
	fb->reset = 0;
	fb->blank = cg3_blank;
	fb->unblank = cg3_unblank;

	/* Map the card registers */
	cg3info->regs = sparc_alloc_io (cg3+CG3_REGS, 0,
		 sizeof (struct cg3_regs),"cg3_regs", cg3_io, 0);

	if (!prom_getbool(sbdp->prom_node, "width")) {
		/* Ugh, broken PROM didn't initialize us.
		 * Let's deal with this ourselves.
		 */
		u8 status, mon;
		enum cg3_type type;
		u8 *p;

		if (cg3info->cgrdi) {
			type = CG3_RDI;
		} else {
			status = cg3info->regs->status;
			if ((status & CG3_SR_ID_MASK) == CG3_SR_ID_COLOR) {
				mon = status & CG3_SR_RES_MASK;
				if (mon == CG3_SR_1152_900_76_A ||
				    mon == CG3_SR_1152_900_76_B)
					type = CG3_AT_76HZ;
				else
					type = CG3_AT_66HZ;
			} else {
				prom_printf("cgthree: can't handle SR %02x\n",
					    status);
				prom_halt();
				return; /* fool gcc. */
			}
		}

		for (p = cg3_regvals[type]; *p; p += 2)
			((u8 *)cg3info->regs)[p[0]] = p[1];

		for (p = cg3_dacvals; *p; p += 2) {
			*(volatile u8 *)&cg3info->regs->cmap.addr = p[0];
			*(volatile u8 *)&cg3info->regs->cmap.control = p[1];
		}
	}

	if (!fb->base){
		fb->base=(unsigned long) sparc_alloc_io (cg3+CG3_RAM, 0,
				         fb->type.fb_size, "cg3_ram", cg3_io, 0);
	}
}

