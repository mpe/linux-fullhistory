/* $Id: bwtwo.c,v 1.8 1996/11/23 19:54:05 ecd Exp $
 * bwtwo.c: bwtwo console driver
 *
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */

#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/malloc.h>

#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/fbio.h>
#include <asm/pgtable.h>

#include "../../char/vt_kern.h"
#include "../../char/selection.h"
#include "../../char/console_struct.h"
#include "fb.h"
#include "cg_common.h"

/* OBio addresses for the bwtwo registers */
#define BWTWO_REGISTER_OFFSET 0x400000

struct bwtwo_regs {
	__volatile__ struct bt_regs	bt;
	__volatile__ __u8		control;
	__volatile__ __u8		status;
	__volatile__ __u8		cursor_start;
	__volatile__ __u8		cursor_end;
	__volatile__ __u8		vcontrol[12];
};

/* Control Register Constants */
#define BWTWO_CTL_ENABLE_INTS   0x80
#define BWTWO_CTL_ENABLE_VIDEO  0x40
#define BWTWO_CTL_ENABLE_TIMING 0x20
#define BWTWO_CTL_ENABLE_CURCMP 0x10
#define BWTWO_CTL_XTAL_MASK     0x0C
#define BWTWO_CTL_DIVISOR_MASK  0x03

/* Status Register Constants */
#define BWTWO_STAT_PENDING_INT  0x80
#define BWTWO_STAT_MSENSE_MASK  0x70
#define BWTWO_STAT_ID_MASK      0x0f


static int
bwtwo_mmap (struct inode *inode, struct file *file, struct vm_area_struct *vma,
	    long base, fbinfo_t *fb)
{
	uint size, map_offset, r;
	int map_size;
	
	map_size = size = vma->vm_end - vma->vm_start;
	
	if (vma->vm_offset & ~PAGE_MASK)
		return -ENXIO;

	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= FB_MMAP_VM_FLAGS;

	/* This routine should also map the register if asked for,
	 * but we don't do that yet.
	 */
	map_offset = get_phys ((uint) fb->base);
	r = io_remap_page_range (vma->vm_start, map_offset, map_size,
				 vma->vm_page_prot, fb->space);
	if (r) return -EAGAIN;
	vma->vm_inode = inode;
	inode->i_count++;
	return 0;
}

static void
bwtwo_blank (fbinfo_t *fb)
{
	fb->info.bwtwo.regs->control &= ~BWTWO_CTL_ENABLE_VIDEO;
}

static void
bwtwo_unblank (fbinfo_t *fb)
{
	fb->info.bwtwo.regs->control |= BWTWO_CTL_ENABLE_VIDEO;
}

__initfunc(void bwtwo_setup (fbinfo_t *fb, int slot, uint bwtwo, int bw2_io))
{
	printk ("bwtwo%d at 0x%8.8x\n", slot, bwtwo);
	fb->type.fb_cmsize = 0;
	fb->mmap = bwtwo_mmap;
	fb->loadcmap = 0;
	fb->ioctl = 0;
	fb->reset = 0;
	fb->blank = bwtwo_blank;
	fb->unblank = bwtwo_unblank;
	fb->info.bwtwo.regs = sparc_alloc_io ((void *)bwtwo +
		BWTWO_REGISTER_OFFSET, 0, sizeof (struct bwtwo_regs),
		"bwtwo_regs", bw2_io, 0);
	if(!fb->base)
		fb->base = (unsigned long) sparc_alloc_io((void *)bwtwo, 0,
		  ((fb->type.fb_depth*fb->type.fb_height*fb->type.fb_width)/8),
		  "bwtwo_fbase", bw2_io, 0);

	if (slot && sun_prom_console_id != slot)
		bwtwo_blank (fb);
}

