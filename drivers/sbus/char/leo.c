/* $Id: leo.c,v 1.20 1997/07/15 09:48:46 jj Exp $
 * leo.c: SUNW,leo 24/8bit frame buffer driver
 *
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 Michal Rehacek (Michal.Rehacek@st.mff.cuni.cz)
 */
 
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>

#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/fbio.h>
#include <asm/pgtable.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

/* These must be included after asm/fbio.h */
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/console_struct.h>
#include "fb.h"
#include "cg_common.h"

#define LEO_OFF_LC_SS0_KRN	0x00200000
#define LEO_OFF_LC_SS0_USR	0x00201000
#define LEO_OFF_LC_SS1_KRN	0x01200000
#define LEO_OFF_LC_SS1_USR	0x01201000
#define LEO_OFF_LD_SS0		0x00400000
#define LEO_OFF_LD_SS1		0x01400000
#define LEO_OFF_LD_GBL		0x00401000
#define LEO_OFF_LX_KRN		0x00600000
#define LEO_OFF_LX_CURSOR	0x00601000
#define LEO_OFF_SS0		0x00800000
#define LEO_OFF_SS1		0x01800000
#define LEO_OFF_UNK		0x00602000
#define LEO_OFF_UNK2		0x00000000

#define LEO_CUR_ENABLE		0x00000080
#define LEO_CUR_UPDATE		0x00000030
#define LEO_CUR_PROGRESS	0x00000006
#define LEO_CUR_UPDATECMAP	0x00000003

#define LEO_CUR_TYPE_MASK	0x00000000
#define LEO_CUR_TYPE_IMAGE	0x00000020
#define LEO_CUR_TYPE_CMAP	0x00000050

struct leo_cursor {
	u8		xxx0[16];
	volatile u32	cur_type;
	volatile u32	cur_misc;
	volatile u32	cur_cursxy;
	volatile u32	cur_data;
};

#define LEO_KRN_TYPE_CLUT0	0x00001000
#define LEO_KRN_TYPE_CLUT1	0x00001001
#define LEO_KRN_TYPE_CLUT2	0x00001002
#define LEO_KRN_TYPE_WID	0x00001003
#define LEO_KRN_TYPE_UNK	0x00001006
#define LEO_KRN_TYPE_VIDEO	0x00002003
#define LEO_KRN_TYPE_CLUTDATA	0x00004000
#define LEO_KRN_CSR_ENABLE	0x00000008
#define LEO_KRN_CSR_PROGRESS	0x00000004
#define LEO_KRN_CSR_UNK		0x00000002
#define LEO_KRN_CSR_UNK2	0x00000001

struct leo_lx_krn {
	volatile u32	krn_type;
	volatile u32	krn_csr;
	volatile u32	krn_value;
};

struct leo_lc_ss0_krn {
	volatile u32 	misc;
	u8		xxx0[0x800-4];
	volatile u32	rev;
};

struct leo_lc_ss0_usr {
	volatile u32	csr;
	volatile u32	attrs;
	volatile u32 	fontc;
	volatile u32	fontc2;
	volatile u32	extent;
	volatile u32	src;
	u32		xxx1[1];
	volatile u32	copy;
	volatile u32	fill;
};

struct leo_lc_ss1_krn {
	u8	unknown;
};

struct leo_lc_ss1_usr {
	u8	unknown;
};

struct leo_ld_ss0 {
	u8		xxx0[0xe00];
	u32		xxx1[2];
	volatile u32	unk;
	u32		xxx2[1];
	volatile u32	unk2;
	volatile u32	unk3;
	u32		xxx3[2];
	volatile u32	fg;
	volatile u32	bg;
	u8		xxx4[0x05c];
	volatile u32	planemask;
	volatile u32	rop;
};

#define LEO_SS1_MISC_ENABLE	0x00000001
#define LEO_SS1_MISC_STEREO	0x00000002
struct leo_ld_ss1 {
	u8		xxx0[0xef4];
	volatile u32	ss1_misc;
};

struct leo_ld_gbl {
	u8	unknown;
};

static void leo_blitc(unsigned short, int, int);
static void leo_setw(int, int, unsigned short, int);
static void leo_cpyw(int, int, unsigned short *, int);
static void leo_fill(int, int, int *);

static void
leo_restore_palette (fbinfo_t *fb)
{
	fb->info.leo.ld_ss1->ss1_misc &= ~(LEO_SS1_MISC_ENABLE);
}

/* Ugh: X wants to mmap a bunch of cute stuff at the same time :-( */
/* So, we just mmap the things that are being asked for */
static int
leo_mmap (struct inode *inode, struct file *file, struct vm_area_struct *vma,
	  long base, fbinfo_t *fb)
{
	uint size, page, r, map_size = 0;
	unsigned long map_offset = 0;
			     
	size = vma->vm_end - vma->vm_start;
        if (vma->vm_offset & ~PAGE_MASK)
                return -ENXIO;

	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= FB_MMAP_VM_FLAGS;
	
	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		switch (vma->vm_offset+page){
		case LEO_SS0_MAP:
			map_size = 0x800000;
			map_offset = get_phys ((unsigned long)fb->base);
			break;
		case LEO_LC_SS0_USR_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.leo.lc_ss0_usr);
			break;
		case LEO_LD_SS0_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.leo.ld_ss0);
			break;
		case LEO_LX_CURSOR_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.leo.cursor);
			break;
		case LEO_SS1_MAP:	
			map_size = 0x800000;
			map_offset = fb->info.leo.offset + LEO_OFF_SS1;
			break;
		case LEO_LC_SS1_USR_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.leo.lc_ss1_usr);
			break;
		case LEO_LD_SS1_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.leo.ld_ss1);
			break;
		case LEO_UNK_MAP:	
			map_size = PAGE_SIZE;
			map_offset = fb->info.leo.offset + LEO_OFF_UNK;
			break;
		case LEO_LX_KRN_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.leo.lx_krn);
			break;
		case LEO_LC_SS0_KRN_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.leo.lc_ss0_krn);
			break;
		case LEO_LC_SS1_KRN_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.leo.lc_ss1_krn);
			break;
		case LEO_LD_GBL_MAP:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.leo.ld_gbl);
			break;
		case LEO_UNK2_MAP:
			map_size = 0x100000;
			map_offset = fb->info.leo.offset + LEO_OFF_UNK2;
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
		if (r)
			return -EAGAIN;
		page += map_size;
	}

	vma->vm_dentry = dget(file->f_dentry);
        return 0;
}

static void
leo_setcursormap (fbinfo_t *fb, unsigned char *red,
				 unsigned char *green,
				 unsigned char *blue)
{
	struct leo_cursor *l = fb->info.leo.cursor;
	int i;

	for (i = 0; (l->cur_misc & LEO_CUR_PROGRESS) && i < 300000; i++)
		udelay (1); /* Busy wait at most 0.3 sec */
	if (i == 300000) return; /* Timed out - should we print some message? */
	l->cur_type = LEO_CUR_TYPE_CMAP;
	l->cur_data = (red[0] | (green[0]<<8) | (blue[0]<<16));
	l->cur_data = (red[1] | (green[1]<<8) | (blue[1]<<16));
	l->cur_misc = LEO_CUR_UPDATECMAP;
}

/* Load cursor information */
static void
leo_setcursor (fbinfo_t *fb)
{
	struct cg_cursor *c = &fb->cursor;
	struct leo_cursor *l = fb->info.leo.cursor;

	l->cur_misc &= ~LEO_CUR_ENABLE;
	l->cur_cursxy = ((c->cpos.fbx - c->chot.fbx) & 0x7ff)
			|(((c->cpos.fby - c->chot.fby) & 0x7ff) << 11);
	l->cur_misc |= LEO_CUR_UPDATE;
	if (c->enable)
		l->cur_misc |= LEO_CUR_ENABLE;
}

/* Set cursor shape */
static void
leo_setcurshape (fbinfo_t *fb)
{
	int i, j, k;
	u32 m, n, mask;
	struct leo_cursor *l = fb->info.leo.cursor;

	l->cur_misc &= ~LEO_CUR_ENABLE;
	for (k = 0; k < 2; k ++) {
		l->cur_type = (k * LEO_CUR_TYPE_IMAGE); /* LEO_CUR_TYPE_MASK is 0 */
		for (i = 0; i < 32; i++) {
			mask = 0;
			m = fb->cursor.bits[k][i];
			/* mask = m with reversed bit order */
			for (j = 0, n = 1; j < 32; j++, n <<= 1)
				if (m & n)
					mask |= (0x80000000 >> j);
			l->cur_data = mask;
		}
	}
	l->cur_misc |= LEO_CUR_ENABLE;
}

static void
leo_blank (fbinfo_t *fb)
{
	fb->info.leo.lx_krn->krn_type = LEO_KRN_TYPE_VIDEO;
	fb->info.leo.lx_krn->krn_csr &= ~LEO_KRN_CSR_ENABLE;
}

static void
leo_unblank (fbinfo_t *fb)
{
	fb->info.leo.lx_krn->krn_type = LEO_KRN_TYPE_VIDEO;
	if (!(fb->info.leo.lx_krn->krn_csr & LEO_KRN_CSR_ENABLE))
		fb->info.leo.lx_krn->krn_csr |= LEO_KRN_CSR_ENABLE;
}

static int leo_wait (struct leo_lx_krn *lx_krn)
{
	int i;
	for (i = 0; (lx_krn->krn_csr & LEO_KRN_CSR_PROGRESS) && i < 300000; i++)
		udelay (1); /* Busy wait at most 0.3 sec */
	if (i == 300000) return -EFAULT; /* Timed out - should we print some message? */
	return 0;
}

static int
leo_wid_get (fbinfo_t *fb, struct fb_wid_list *wl)
{
	struct leo_lx_krn *lx_krn = fb->info.leo.lx_krn;
	struct fb_wid_item *wi;
	int i, j;
	u32 l;
	
	lx_krn->krn_type = LEO_KRN_TYPE_WID;
	i = leo_wait (lx_krn);
	if (i) return i;
	lx_krn->krn_csr &= ~LEO_KRN_CSR_UNK2;
	lx_krn->krn_csr |= LEO_KRN_CSR_UNK;
	lx_krn->krn_type = LEO_KRN_TYPE_WID;
	i = leo_wait (lx_krn);
	if (i) return i;
	for (i = 0, wi = wl->wl_list; i < wl->wl_count; i++, wi++) {
		switch (wi->wi_type) {
		case FB_WID_DBL_8: j = (wi->wi_index & 0xf) + 0x40; break;
		case FB_WID_DBL_24: j = wi->wi_index & 0x3f; break;
		default: return -EINVAL;
		}
		wi->wi_attrs = 0xffff;
		lx_krn->krn_type = 0x5800 + j;
		l = lx_krn->krn_value;
		for (j = 0; j < 32; j++)
			wi->wi_values [j] = l;
	}
	return 0;
}

static int
leo_wid_put (fbinfo_t *fb, struct fb_wid_list *wl)
{
	struct leo_lx_krn *lx_krn = fb->info.leo.lx_krn;
	struct fb_wid_item *wi;
	int i, j;
	
	lx_krn->krn_type = LEO_KRN_TYPE_WID;
	i = leo_wait (lx_krn);
	if (i) return i;
	for (i = 0, wi = wl->wl_list; i < wl->wl_count; i++, wi++) {
		switch (wi->wi_type) {
		case FB_WID_DBL_8: j = (wi->wi_index & 0xf) + 0x40; break;
		case FB_WID_DBL_24: j = wi->wi_index & 0x3f; break;
		default: return -EINVAL;
		}
		lx_krn->krn_type = 0x5800 + j;
		lx_krn->krn_value = wi->wi_values[0];
	}
	return 0;
}

static int leo_clutstore (fbinfo_t *fb, int clutid)
{
	int i;
	u32 *clut = fb->info.leo.cluts [clutid];
	struct leo_lx_krn *lx_krn = fb->info.leo.lx_krn;
	
	lx_krn->krn_type = LEO_KRN_TYPE_CLUT0 + clutid;
	i = leo_wait (lx_krn);
	if (i) return i;
	lx_krn->krn_type = LEO_KRN_TYPE_CLUTDATA;
	for (i = 0; i < 256; i++)
		lx_krn->krn_value = *clut++; /* Throw colors there :)) */
	lx_krn->krn_type = LEO_KRN_TYPE_CLUT0 + clutid;
	lx_krn->krn_csr |= (LEO_KRN_CSR_UNK|LEO_KRN_CSR_UNK2);
	return 0;
}

static int leo_clutpost (fbinfo_t *fb, struct fb_clut *lc)
{
	int xlate = 0, i;
	u32 *clut;
	u8 *xlut = fb->info.leo.xlut;
	
	switch (lc->clutid) {
	case 0:
	case 1:
	case 2: break;
	case 3: return -EINVAL; /* gamma clut - not yet implemented */
	case 4: return -EINVAL; /* degamma clut - not yet implemented */
	default: return -EINVAL;
	}
	clut = fb->info.leo.cluts [lc->clutid] + lc->offset;
	for (i = 0; i < lc->count; i++)
		*clut++ = xlate ? 
		((xlut[(u8)(lc->red[i])])|(xlut[(u8)(lc->green[i])]<<8)|(xlut[(u8)(lc->blue[i])]<<16)) : 
		(((u8)(lc->red[i]))|(((u8)(lc->green[i]))<<8)|(((u8)(lc->blue[i]))<<16));
	return leo_clutstore (fb, lc->clutid);
}

static int leo_clutread (fbinfo_t *fb, struct fb_clut *lc)
{
	int i;
	u32 u;
	struct leo_lx_krn *lx_krn = fb->info.leo.lx_krn;

	if (lc->clutid >= 3) return -EINVAL;	
	lx_krn->krn_type = LEO_KRN_TYPE_CLUT0 + lc->clutid;
	i = leo_wait (lx_krn);
	if (i) return i;
	lx_krn->krn_csr &= ~LEO_KRN_CSR_UNK2;
	lx_krn->krn_csr |= LEO_KRN_CSR_UNK;
	i = leo_wait (lx_krn);
	if (i) return i;
	lx_krn->krn_type = LEO_KRN_TYPE_CLUTDATA;
	for (i = 0; i < lc->offset; i++)
		u = lx_krn->krn_value;
	for (i = 0; i < lc->count; i++) {
		u = lx_krn->krn_value;
		lc->red [i] = u;
		lc->green [i] = (u >> 8);
		lc->blue [i] = (u >> 16);
	}
	return 0;
}

static void
leo_loadcmap (fbinfo_t *fb, int index, int count)
{
	u32 *clut = ((u32 *)fb->info.leo.cluts [0]) + index;
	int i;
	
	for (i = index; count--; i++)
		*clut++ = ((fb->color_map CM(i,0))) |
			  ((fb->color_map CM(i,1)) << 8) |
			  ((fb->color_map CM(i,2)) << 16);
	leo_clutstore (fb, 0);
}

/* Handle leo-specific ioctls */
static int
leo_ioctl (struct inode *inode, struct file *file, unsigned cmd, unsigned long arg, fbinfo_t *fb)
{
	int i;
	
	switch (cmd) {
	case FBIO_WID_GET:
		i = verify_area (VERIFY_READ, (void *)arg, sizeof (struct fb_wid_list));
		if (i) return i;
		if (((struct fb_wid_list *)arg)->wl_count != 1 ||
		    !((struct fb_wid_list *)arg)->wl_list) return -EINVAL;
		i = verify_area (VERIFY_WRITE, (void *)(((struct fb_wid_list *)arg)->wl_list), 
			((struct fb_wid_list *)arg)->wl_count * sizeof (struct fb_wid_item));
		if (i) return i;
		return leo_wid_get (fb, (struct fb_wid_list *)arg);
	case FBIO_WID_PUT:
		i = verify_area (VERIFY_READ, (void *)arg, sizeof (struct fb_wid_list));
		if (i) return i;
		if (((struct fb_wid_list *)arg)->wl_count != 1 ||
		    !((struct fb_wid_list *)arg)->wl_list) return -EINVAL;
		i = verify_area (VERIFY_WRITE, (void *)(((struct fb_wid_list *)arg)->wl_list), 
			((struct fb_wid_list *)arg)->wl_count * sizeof (struct fb_wid_item));
		if (i) return i;
		return leo_wid_put (fb, (struct fb_wid_list *)arg);
	case LEO_CLUTPOST:
		i = verify_area (VERIFY_READ, (void *)arg, sizeof (struct fb_clut));
		if (i) return i;
		i = ((struct fb_clut *)arg)->offset + ((struct fb_clut *)arg)->count;
		if (i <= 0 || i > 256) return -EINVAL;
		i = verify_area (VERIFY_READ, ((struct fb_clut *)arg)->red, ((struct fb_clut *)arg)->count);
		if (i) return i;
		i = verify_area (VERIFY_READ, ((struct fb_clut *)arg)->green, ((struct fb_clut *)arg)->count);
		if (i) return i;
		i = verify_area (VERIFY_READ, ((struct fb_clut *)arg)->blue, ((struct fb_clut *)arg)->count);
		if (i) return i;
		return leo_clutpost (fb, (struct fb_clut *)arg);
	case LEO_CLUTREAD:
		i = verify_area (VERIFY_READ, (void *)arg, sizeof (struct fb_clut));
		if (i) return i;
		i = ((struct fb_clut *)arg)->offset + ((struct fb_clut *)arg)->count;
		if (i <= 0 || i > 256) return -EINVAL;
		i = verify_area (VERIFY_WRITE, ((struct fb_clut *)arg)->red, ((struct fb_clut *)arg)->count);
		if (i) return i;
		i = verify_area (VERIFY_WRITE, ((struct fb_clut *)arg)->green, ((struct fb_clut *)arg)->count);
		if (i) return i;
		i = verify_area (VERIFY_WRITE, ((struct fb_clut *)arg)->blue, ((struct fb_clut *)arg)->count);
		if (i) return i;
		return leo_clutread (fb, (struct fb_clut *)arg);
		
	default:
		return -ENOSYS;
	}
}

static void
leo_reset (fbinfo_t *fb)
{
	if (fb->setcursor)
		sun_hw_hide_cursor ();
}


__initfunc(static unsigned long leo_postsetup (fbinfo_t *fb, unsigned long memory_start))
{
	fb->info.leo.cluts[0] = (u32 *)(memory_start);
	fb->info.leo.cluts[1] = (u32 *)(memory_start+256*4);
	fb->info.leo.cluts[2] = (u32 *)(memory_start+256*4*2);
	fb->info.leo.xlut = (u8 *)(memory_start+256*4*3);
	fb->color_map = (u8 *)(memory_start+256*4*3+256);
	return memory_start + (256*4*3) + 256 + 256*3;
}

__initfunc(void leo_setup (fbinfo_t *fb, int slot, u32 leo, int leo_io))
{
	struct leo_info *leoinfo;
	int i;
	struct fb_wid_item wi;
	struct fb_wid_list wl;
	
	printk ("leo%d at 0x%8.8x ", slot, leo);
	
	/* Fill in parameters we left out */
	fb->type.fb_size = 0x800000; /* 8MB */
	fb->type.fb_cmsize = 256;
	fb->mmap = leo_mmap;
	fb->loadcmap = leo_loadcmap;
	fb->postsetup = leo_postsetup;
	fb->ioctl = (void *)leo_ioctl;
	fb->reset = leo_reset;
	fb->blank = leo_blank;
	fb->unblank = leo_unblank;
	fb->setcursor = leo_setcursor;
	fb->setcursormap = leo_setcursormap;
	fb->setcurshape = leo_setcurshape;
	fb->blitc = leo_blitc;
	fb->setw = leo_setw;
	fb->cpyw = leo_cpyw;
	fb->fill = leo_fill;
	fb->base_depth = 0;
	
	leoinfo = (struct leo_info *) &fb->info.leo;
	
	memset (leoinfo, 0, sizeof(struct leo_info));

	leoinfo->offset = leo;
	/* Map the hardware registers */
	leoinfo->lc_ss0_krn = sparc_alloc_io(leo + LEO_OFF_LC_SS0_KRN, 0,
		 PAGE_SIZE,"leo_lc_ss0_krn", fb->space, 0);
	leoinfo->lc_ss0_usr = sparc_alloc_io(leo + LEO_OFF_LC_SS0_USR, 0,
		 PAGE_SIZE,"leo_lc_ss0_usr", fb->space, 0);
	leoinfo->lc_ss1_krn = sparc_alloc_io(leo + LEO_OFF_LC_SS1_KRN, 0,
		 PAGE_SIZE,"leo_lc_ss1_krn", fb->space, 0);
	leoinfo->lc_ss1_usr = sparc_alloc_io(leo + LEO_OFF_LC_SS1_USR, 0,
		 PAGE_SIZE,"leo_lc_ss1_usr", fb->space, 0);
	leoinfo->ld_ss0 = sparc_alloc_io(leo + LEO_OFF_LD_SS0, 0,
		 PAGE_SIZE,"leo_ld_ss0", fb->space, 0);
	leoinfo->ld_ss1 = sparc_alloc_io(leo + LEO_OFF_LD_SS1, 0,
		 PAGE_SIZE,"leo_ld_ss1", fb->space, 0);
	leoinfo->ld_gbl = sparc_alloc_io(leo + LEO_OFF_LD_GBL, 0,
		 PAGE_SIZE,"leo_ld_gbl", fb->space, 0);
	leoinfo->lx_krn = sparc_alloc_io(leo + LEO_OFF_LX_KRN, 0,
		 PAGE_SIZE,"leo_lx_krn", fb->space, 0);
	leoinfo->cursor = sparc_alloc_io(leo + LEO_OFF_LX_CURSOR, 0,
		 sizeof(struct leo_cursor),"leo_lx_crsr", fb->space, 0);
	fb->base = (long)sparc_alloc_io(leo + LEO_OFF_SS0, 0,
		 0x800000,"leo_ss0", fb->space, 0);
	
	leoinfo->ld_ss0->unk = 0xffff;
	leoinfo->ld_ss0->unk2 = 0;
	leoinfo->ld_ss0->unk3 = (fb->type.fb_width - 1)	| ((fb->type.fb_height - 1) << 16);
	wl.wl_count = 1;
	wl.wl_list = &wi;
	wi.wi_type = FB_WID_DBL_8;
	wi.wi_index = 0;
	wi.wi_values [0] = 0x2c0;
	leo_wid_put (fb, &wl);
	wi.wi_index = 1;
	wi.wi_values [0] = 0x30;
	leo_wid_put (fb, &wl);
	wi.wi_index = 2;
	wi.wi_values [0] = 0x20;
	leo_wid_put (fb, &wl);
	
	leoinfo->ld_ss1->ss1_misc |= LEO_SS1_MISC_ENABLE;
	
	leoinfo->ld_ss0->fg = 0x30703;
	leoinfo->ld_ss0->planemask = 0xff000000;
	leoinfo->ld_ss0->rop = 0xd0840;
	leoinfo->lc_ss0_usr->extent = (fb->type.fb_width-1) | ((fb->type.fb_height-1) << 11);
	i = leoinfo->lc_ss0_usr->attrs;
	leoinfo->lc_ss0_usr->fill = (0) | ((0) << 11) | ((i & 3) << 29) | ((i & 8) ? 0x80000000 : 0);
	do {
		i = leoinfo->lc_ss0_usr->csr;
	} while (i & 0x20000000);
	
	if (slot == sun_prom_console_id)
		fb_restore_palette = leo_restore_palette;
	
	printk("Cmd Rev %d\n",
		(leoinfo->lc_ss0_krn->rev >> 28));
	
	/* Reset the leo */
	leo_reset(fb);
	
	if (!slot)	
	/* Enable Video */
		leo_unblank (fb);
	else if (slot != sun_prom_console_id)
		leo_blank (fb);
}

extern unsigned char vga_font [];

#define GX_BLITC_START(attr,x,y,count) \
	{ \
		register struct leo_lc_ss0_usr *us = fbinfo[0].info.leo.lc_ss0_usr; \
		register struct leo_ld_ss0 *ss = fbinfo[0].info.leo.ld_ss0; \
		register u32 i; \
		do { \
			i = us->csr; \
		} while (i & 0x20000000); \
		ss->fg = (attr & 0xf) << 24; \
		ss->bg = (attr >> 4) << 24; \
		ss->rop = 0x310040; \
		ss->planemask = 0xff000000; \
		us->fontc2 = 0xFFFFFFFE; \
		us->attrs = 4; \
		us->fontc = 0xFF000000;
#define GX_BLITC_END \
	}

static void leo_blitc(unsigned short charattr, int xoff, int yoff)
{
	unsigned char attrib = CHARATTR_TO_SUNCOLOR(charattr);
	unsigned char *p = &vga_font[((unsigned char)charattr) << 4];
	u32 *u = ((u32 *)fbinfo[0].base) + (yoff << 11) + xoff;
	GX_BLITC_START(attrib, xoff, yoff, 1)
	for (i = 0; i < CHAR_HEIGHT; i++, u += 2048)
		*u = (*p++) << 24;
	GX_BLITC_END
}

static void leo_setw(int xoff, int yoff, unsigned short c, int count)
{
	unsigned char attrib = CHARATTR_TO_SUNCOLOR(c);
	unsigned char *p = &vga_font[((unsigned char)c) << 4];
	register unsigned char *q;
	u32 *u = ((u32 *)fbinfo[0].base) + (yoff << 11) + xoff;
	GX_BLITC_START(attrib, xoff, yoff, count)
	while (count-- > 0) {
		q = p;
		for (i = 0; i < CHAR_HEIGHT; i++, u += 2048)
			*u = (*q++) << 24;
		u += 8 - (CHAR_HEIGHT * 2048);
	}
	GX_BLITC_END
}

static void leo_cpyw(int xoff, int yoff, unsigned short *p, int count)
{
	unsigned char attrib = CHARATTR_TO_SUNCOLOR(*p);
	register unsigned char *q;
	u32 *u = ((u32 *)fbinfo[0].base) + (yoff << 11) + xoff;
	GX_BLITC_START(attrib, xoff, yoff, count)
	while (count-- > 0) {
		q = &vga_font[((unsigned char)*p++) << 4];
		for (i = 0; i < CHAR_HEIGHT; i++, u += 2048)
			*u = (*q++) << 24;
		u += 8 - (CHAR_HEIGHT * 2048);
	}
	GX_BLITC_END
}

static void leo_fill(int attrib, int count, int *boxes)
{
	register struct leo_lc_ss0_usr *us = fbinfo[0].info.leo.lc_ss0_usr;
	register struct leo_ld_ss0 *ss = fbinfo[0].info.leo.ld_ss0;
	register u32 i;
	do {
		i = us->csr;
	} while (i & 0x20000000);
	ss->unk = 0xffff;
	ss->unk2 = 0;
	ss->unk3 = (fbinfo[0].type.fb_width - 1) | ((fbinfo[0].type.fb_height - 1) << 16);
	ss->fg = ((attrib & 0xf)<<24) | 0x030703;
	ss->planemask = 0xff000000;
	ss->rop = 0xd0840;
	while (count-- > 0) {
		us->extent = ((boxes[2] - boxes[0] - 1) & 0x7ff) | (((boxes[3] - boxes[1] - 1) & 0x7ff) << 11);
		i = us->attrs;
		us->fill = (boxes[0] & 0x7ff) | ((boxes[1] & 0x7ff) << 11) | ((i & 3) << 29) | ((i & 8) ? 0x80000000 : 0);
	}
}
