/* $Id: bwtwo.c,v 1.16 1997/06/04 08:27:26 davem Exp $
 * bwtwo.c: bwtwo console driver
 *
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1997 Eddie C. Dost   (ecd@skynet.be)
 */

#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>

#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/fbio.h>
#include <asm/pgtable.h>

/* These must be included after asm/fbio.h */
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/console_struct.h>

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
	__volatile__ __u8		h_blank_start;
	__volatile__ __u8		h_blank_end;
	__volatile__ __u8		h_sync_start;
	__volatile__ __u8		h_sync_end;
	__volatile__ __u8		comp_sync_end;
	__volatile__ __u8		v_blank_start_high;
	__volatile__ __u8		v_blank_start_low;
	__volatile__ __u8		v_blank_end;
	__volatile__ __u8		v_sync_start;
	__volatile__ __u8		v_sync_end;
	__volatile__ __u8		xfer_holdoff_start;
	__volatile__ __u8		xfer_holdoff_end;
};

/* Status Register Constants */
#define BWTWO_SR_RES_MASK	0x70
#define BWTWO_SR_1600_1280	0x50
#define BWTWO_SR_1152_900_76_A	0x40
#define BWTWO_SR_1152_900_76_B	0x60
#define BWTWO_SR_ID_MASK	0x0f
#define BWTWO_SR_ID_MONO	0x02
#define BWTWO_SR_ID_MONO_ECL	0x03
#define BWTWO_SR_ID_MSYNC	0x04

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
	map_offset = get_phys ((unsigned long) fb->base);
	r = io_remap_page_range (vma->vm_start, map_offset, map_size,
				 vma->vm_page_prot, fb->space);
	if (r) return -EAGAIN;
	vma->vm_inode = inode;
	atomic_inc(&inode->i_count);
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


static u8 bw2regs_1600[] __initdata = {
	0x14, 0x8b,	0x15, 0x28,	0x16, 0x03,	0x17, 0x13,
	0x18, 0x7b,	0x19, 0x05,	0x1a, 0x34,	0x1b, 0x2e,
	0x1c, 0x00,	0x1d, 0x0a,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x21,	0
};

static u8 bw2regs_ecl[] __initdata = {
	0x14, 0x65,	0x15, 0x1e,	0x16, 0x04,	0x17, 0x0c,
	0x18, 0x5e,	0x19, 0x03,	0x1a, 0xa7,	0x1b, 0x23,
	0x1c, 0x00,	0x1d, 0x08,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

static u8 bw2regs_analog[] __initdata = {
	0x14, 0xbb,	0x15, 0x2b,	0x16, 0x03,	0x17, 0x13,
	0x18, 0xb0,	0x19, 0x03,	0x1a, 0xa6,	0x1b, 0x22,
	0x1c, 0x01,	0x1d, 0x05,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

static u8 bw2regs_76hz[] __initdata = {
	0x14, 0xb7,	0x15, 0x27,	0x16, 0x03,	0x17, 0x0f,
	0x18, 0xae,	0x19, 0x03,	0x1a, 0xae,	0x1b, 0x2a,
	0x1c, 0x01,	0x1d, 0x09,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x24,	0
};

static u8 bw2regs_66hz[] __initdata = {
	0x14, 0xbb,	0x15, 0x2b,	0x16, 0x04,	0x17, 0x14,
	0x18, 0xae,	0x19, 0x03,	0x1a, 0xa8,	0x1b, 0x24,
	0x1c, 0x01,	0x1d, 0x05,	0x1e, 0xff,	0x1f, 0x01,
	0x10, 0x20,	0
};

__initfunc(void bwtwo_setup (fbinfo_t *fb, int slot, u32 bwtwo, int bw2_io,
			     struct linux_sbus_device *sbdp))
{
	printk ("bwtwo%d at 0x%8.8x\n", slot, bwtwo);
	fb->type.fb_cmsize = 0;
	fb->mmap = bwtwo_mmap;
	fb->loadcmap = 0;
	fb->ioctl = 0;
	fb->reset = 0;
	fb->blank = bwtwo_blank;
	fb->unblank = bwtwo_unblank;

	fb->info.bwtwo.regs =
		sparc_alloc_io (bwtwo + BWTWO_REGISTER_OFFSET,
				0, sizeof (struct bwtwo_regs),
		"bwtwo_regs", bw2_io, 0);

	if (!prom_getbool(sbdp->prom_node, "width")) {
		/* Ugh, broken PROM didn't initialize us.
		 * Let's deal with this ourselves.
		 */
		u8 status, mon;
		u8 *p;

		status = fb->info.bwtwo.regs->status;
		mon = status & BWTWO_SR_RES_MASK;
		switch (status & BWTWO_SR_ID_MASK) {
			case BWTWO_SR_ID_MONO_ECL:
				if (mon == BWTWO_SR_1600_1280) {
					p = bw2regs_1600;
					fb->type.fb_width = 1600;
					fb->type.fb_height = 1280;
				} else {
					p = bw2regs_ecl;
				}
				break;
			case BWTWO_SR_ID_MONO:
				p = bw2regs_analog;
				break;
			case BWTWO_SR_ID_MSYNC:
				if (mon == BWTWO_SR_1152_900_76_A ||
				    mon == BWTWO_SR_1152_900_76_B) {
					p = bw2regs_76hz;
				} else {
					p = bw2regs_66hz;
				}
				break;
			default:
				prom_printf("bwtwo: can't handle SR %02x\n",
					    status);
				prom_halt();
				return; /* fool gcc. */
		}
		for ( ; *p; p += 2)
			((u8 *)fb->info.bwtwo.regs)[p[0]] = p[1];
	}

	if(!fb->base)
		fb->base = (unsigned long) sparc_alloc_io(bwtwo, 0,
		  ((fb->type.fb_depth*fb->type.fb_height*fb->type.fb_width)/8),
		  "bwtwo_fbase", bw2_io, 0);

	if (slot && sun_prom_console_id != slot)
		bwtwo_blank (fb);
}

