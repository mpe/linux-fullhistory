/* $Id: cgsix.c,v 1.32 1997/06/14 15:26:08 davem Exp $
 * cgsix.c: cgsix frame buffer driver
 *
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
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

/* These must be included after asm/fbio.h */
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/console_struct.h>
#include "fb.h"
#include "cg_common.h"

/* Offset of interesting structures in the OBIO space */
/*
 * Brooktree is the video dac and is funny to program on the cg6.
 * (it's even funnier on the cg3)
 * The FBC could be the the frame buffer control
 * The FHC could is the frame buffer hardware control.
 */
#define CG6_ROM_OFFSET       0x0
#define CG6_BROOKTREE_OFFSET 0x200000
#define CG6_DHC_OFFSET       0x240000
#define CG6_ALT_OFFSET       0x280000
#define CG6_FHC_OFFSET       0x300000
#define CG6_THC_OFFSET       0x301000
#define CG6_FBC_OFFSET       0x700000
#define CG6_TEC_OFFSET       0x701000
#define CG6_RAM_OFFSET       0x800000

/* FHC definitions */
#define CG6_FHC_FBID_SHIFT           24
#define CG6_FHC_FBID_MASK            255
#define CG6_FHC_REV_SHIFT            20
#define CG6_FHC_REV_MASK             15
#define CG6_FHC_FROP_DISABLE         (1 << 19)
#define CG6_FHC_ROW_DISABLE          (1 << 18)
#define CG6_FHC_SRC_DISABLE          (1 << 17)
#define CG6_FHC_DST_DISABLE          (1 << 16)
#define CG6_FHC_RESET                (1 << 15)
#define CG6_FHC_LITTLE_ENDIAN        (1 << 13)
#define CG6_FHC_RES_MASK             (3 << 11)
#define CG6_FHC_1024                 (0 << 11)
#define CG6_FHC_1152                 (1 << 11)
#define CG6_FHC_1280                 (2 << 11)
#define CG6_FHC_1600                 (3 << 11)
#define CG6_FHC_CPU_MASK             (3 << 9)
#define CG6_FHC_CPU_SPARC            (0 << 9)
#define CG6_FHC_CPU_68020            (1 << 9)
#define CG6_FHC_CPU_386              (2 << 9)
#define CG6_FHC_TEST		     (1 << 8)
#define CG6_FHC_TEST_X_SHIFT	     4
#define CG6_FHC_TEST_X_MASK	     15
#define CG6_FHC_TEST_Y_SHIFT	     0
#define CG6_FHC_TEST_Y_MASK	     15

/* FBC mode definitions */
#define CG6_FBC_BLIT_IGNORE		0x00000000
#define CG6_FBC_BLIT_NOSRC		0x00100000
#define CG6_FBC_BLIT_SRC		0x00200000
#define CG6_FBC_BLIT_ILLEGAL		0x00300000
#define CG6_FBC_BLIT_MASK		0x00300000

#define CG6_FBC_VBLANK			0x00080000

#define CG6_FBC_MODE_IGNORE		0x00000000
#define CG6_FBC_MODE_COLOR8		0x00020000
#define CG6_FBC_MODE_COLOR1		0x00040000
#define CG6_FBC_MODE_HRMONO		0x00060000
#define CG6_FBC_MODE_MASK		0x00060000

#define CG6_FBC_DRAW_IGNORE		0x00000000
#define CG6_FBC_DRAW_RENDER		0x00008000
#define CG6_FBC_DRAW_PICK		0x00010000
#define CG6_FBC_DRAW_ILLEGAL		0x00018000
#define CG6_FBC_DRAW_MASK		0x00018000

#define CG6_FBC_BWRITE0_IGNORE		0x00000000
#define CG6_FBC_BWRITE0_ENABLE		0x00002000
#define CG6_FBC_BWRITE0_DISABLE		0x00004000
#define CG6_FBC_BWRITE0_ILLEGAL		0x00006000
#define CG6_FBC_BWRITE0_MASK		0x00006000

#define CG6_FBC_BWRITE1_IGNORE		0x00000000
#define CG6_FBC_BWRITE1_ENABLE		0x00000800
#define CG6_FBC_BWRITE1_DISABLE		0x00001000
#define CG6_FBC_BWRITE1_ILLEGAL		0x00001800
#define CG6_FBC_BWRITE1_MASK		0x00001800

#define CG6_FBC_BREAD_IGNORE		0x00000000
#define CG6_FBC_BREAD_0			0x00000200
#define CG6_FBC_BREAD_1			0x00000400
#define CG6_FBC_BREAD_ILLEGAL		0x00000600
#define CG6_FBC_BREAD_MASK		0x00000600

#define CG6_FBC_BDISP_IGNORE		0x00000000
#define CG6_FBC_BDISP_0			0x00000080
#define CG6_FBC_BDISP_1			0x00000100
#define CG6_FBC_BDISP_ILLEGAL		0x00000180
#define CG6_FBC_BDISP_MASK		0x00000180

#define CG6_FBC_INDEX_MOD		0x00000040
#define CG6_FBC_INDEX_MASK		0x00000030

/* THC definitions */
#define CG6_THC_MISC_REV_SHIFT       16
#define CG6_THC_MISC_REV_MASK        15
#define CG6_THC_MISC_RESET           (1 << 12)
#define CG6_THC_MISC_VIDEO           (1 << 10)
#define CG6_THC_MISC_SYNC            (1 << 9)
#define CG6_THC_MISC_VSYNC           (1 << 8)
#define CG6_THC_MISC_SYNC_ENAB       (1 << 7)
#define CG6_THC_MISC_CURS_RES        (1 << 6)
#define CG6_THC_MISC_INT_ENAB        (1 << 5)
#define CG6_THC_MISC_INT             (1 << 4)
#define CG6_THC_MISC_INIT            0x9f

/* The contents are unknown */
struct cg6_tec {
	volatile int tec_matrix;
	volatile int tec_clip;
	volatile int tec_vdc;
};

struct cg6_thc {
        uint thc_pad0[512];
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

struct cg6_fbc {
	u32		xxx0[1];
	volatile u32	mode;
	volatile u32	clip;
	u32		xxx1[1];	    
	volatile u32	s;
	volatile u32	draw;
	volatile u32	blit;
	volatile u32	font;
	u32		xxx2[24];
	volatile u32	x0, y0, z0, color0;
	volatile u32	x1, y1, z1, color1;
	volatile u32	x2, y2, z2, color2;
	volatile u32	x3, y3, z3, color3;
	volatile u32	offx, offy;
	u32		xxx3[2];
	volatile u32	incx, incy;
	u32		xxx4[2];
	volatile u32	clipminx, clipminy;
	u32		xxx5[2];
	volatile u32	clipmaxx, clipmaxy;
	u32		xxx6[2];
	volatile u32	fg;
	volatile u32	bg;
	volatile u32	alu;
	volatile u32	pm;
	volatile u32	pixelm;
	u32		xxx7[2];
	volatile u32	patalign;
	volatile u32	pattern[8];
	u32		xxx8[432];
	volatile u32	apointx, apointy, apointz;
	u32		xxx9[1];
	volatile u32	rpointx, rpointy, rpointz;
	u32		xxx10[5];
	volatile u32	pointr, pointg, pointb, pointa;
	volatile u32	alinex, aliney, alinez;
	u32		xxx11[1];
	volatile u32	rlinex, rliney, rlinez;
	u32		xxx12[5];
	volatile u32	liner, lineg, lineb, linea;
	volatile u32	atrix, atriy, atriz;
	u32		xxx13[1];
	volatile u32	rtrix, rtriy, rtriz;
	u32		xxx14[5];
	volatile u32	trir, trig, trib, tria;
	volatile u32	aquadx, aquady, aquadz;
	u32		xxx15[1];
	volatile u32	rquadx, rquady, rquadz;
	u32		xxx16[5];
	volatile u32	quadr, quadg, quadb, quada;
	volatile u32	arectx, arecty, arectz;
	u32		xxx17[1];
	volatile u32	rrectx, rrecty, rrectz;
	u32		xxx18[5];
	volatile u32	rectr, rectg, rectb, recta;
};

static void
cg6_restore_palette (fbinfo_t *fbinfo)
{
	volatile struct bt_regs *bt;

	bt = fbinfo->info.cg6.bt;
	bt->addr = 0;
	bt->color_map = 0xffffffff;
	bt->color_map = 0xffffffff;
	bt->color_map = 0xffffffff;
}

static void cg6_blitc(unsigned short, int, int);
static void cg6_setw(int, int, unsigned short, int);
static void cg6_cpyw(int, int, unsigned short *, int);

#if 0
static void cg6_fill(int, int, int *);
#endif

/* Ugh: X wants to mmap a bunch of cute stuff at the same time :-( */
/* So, we just mmap the things that are being asked for */
static int
cg6_mmap (struct inode *inode, struct file *file, struct vm_area_struct *vma,
	  long base, fbinfo_t *fb)
{
	uint size, page, r, map_size;
	unsigned long map_offset = 0;
	
	size = vma->vm_end - vma->vm_start;
        if (vma->vm_offset & ~PAGE_MASK)
                return -ENXIO;

	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= FB_MMAP_VM_FLAGS;
	
	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		switch (vma->vm_offset+page){
		case CG6_TEC:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.cg6.tec) & PAGE_MASK;
			break;
		case CG6_FBC:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.cg6.fbc);
			break;
		case CG6_FHC:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.cg6.fhc);
			break;
		case CG6_THC:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.cg6.thc) & PAGE_MASK;
			break;
		case CG6_BTREGS:
			map_size = PAGE_SIZE;
			map_offset = get_phys ((unsigned long)fb->info.cg6.bt);
			break;

		/* For Ultra, make sure the following two are right.
		 * The above two happen to work out (for example FBC and
		 * TEC will get mapped by one I/O page mapping because
		 * of the 8192 byte page size, same for FHC/THC.  -DaveM
		 */

		case CG6_DHC:
			map_size = /* PAGE_SIZE * 40 */ (4096 * 40);
			map_offset = get_phys ((unsigned long)fb->info.cg6.dhc);
			break;
		case CG6_ROM:
			map_size = /* PAGE_SIZE * 16 */ (4096 * 16);
			map_offset = get_phys ((unsigned long)fb->info.cg6.rom);
			break;
		case CG6_RAM:
			map_size = size-page;
			map_offset = get_phys ((unsigned long) fb->base);
			if (map_size < fb->type.fb_size)
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
        atomic_inc(&inode->i_count);
        return 0;
}

static void
cg6_loadcmap (fbinfo_t *fb, int index, int count)
{
	struct bt_regs *bt = fb->info.cg6.bt;
	int i;
	
	bt->addr = index << 24;
	for (i = index; count--; i++){
		bt->color_map = fb->color_map CM(i,0) << 24;
		bt->color_map = fb->color_map CM(i,1) << 24;
		bt->color_map = fb->color_map CM(i,2) << 24;
	}
}

static void
cg6_setcursormap (fbinfo_t *fb, unsigned char *red,
				 unsigned char *green,
				 unsigned char *blue)
{
	struct bt_regs *bt = fb->info.cg6.bt;
	
	bt->addr = 1 << 24;
	bt->cursor = red[0] << 24;
	bt->cursor = green[0] << 24;
	bt->cursor = blue[0] << 24;
	bt->addr = 3 << 24;
	bt->cursor = red[1] << 24;
	bt->cursor = green[1] << 24;
	bt->cursor = blue[1] << 24;
}

/* Load cursor information */
static void
cg6_setcursor (fbinfo_t *fb)
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
	fb->info.cg6.thc->thc_cursxy = v;
}

/* Set cursor shape */
static void
cg6_setcurshape (fbinfo_t *fb)
{
	struct cg6_thc *thc = fb->info.cg6.thc;
	int i;

        for (i = 0; i < 32; i++){
                thc->thc_cursmask [i] = fb->cursor.bits[0][i];
                thc->thc_cursbits [i] = fb->cursor.bits[1][i];
        }
}

static void
cg6_blank (fbinfo_t *fb)
{
	fb->info.cg6.thc->thc_misc &= ~CG6_THC_MISC_VIDEO;
}

static void
cg6_unblank (fbinfo_t *fb)
{
	fb->info.cg6.thc->thc_misc |= CG6_THC_MISC_VIDEO;
}

void
cg6_reset (fbinfo_t *fb)
{
	struct cg6_info *cg6 = &(fb->info.cg6);
	unsigned int rev, conf;

	if (fb->setcursor)
		sun_hw_hide_cursor ();
	/* Turn off stuff in the Transform Engine. */
	cg6->tec->tec_matrix = 0;
	cg6->tec->tec_clip = 0;
	cg6->tec->tec_vdc = 0;

	/* Take care of bugs in old revisions. */
	rev = (*(cg6->fhc) >> CG6_FHC_REV_SHIFT) & CG6_FHC_REV_MASK;
	if (rev < 5) {
		conf = (*(cg6->fhc) & CG6_FHC_RES_MASK) |
			CG6_FHC_CPU_68020 | CG6_FHC_TEST |
			(11 << CG6_FHC_TEST_X_SHIFT) |
			(11 << CG6_FHC_TEST_Y_SHIFT);
		if (rev < 2)
			conf |= CG6_FHC_DST_DISABLE;
		*(cg6->fhc) = conf;
	}

	/* Set things in the FBC. */
	cg6->fbc->mode &= ~(CG6_FBC_BLIT_MASK | CG6_FBC_MODE_MASK |
			    CG6_FBC_DRAW_MASK | CG6_FBC_BWRITE0_MASK |
			    CG6_FBC_BWRITE1_MASK | CG6_FBC_BREAD_MASK |
			    CG6_FBC_BDISP_MASK);
	cg6->fbc->mode |= (CG6_FBC_BLIT_SRC | CG6_FBC_MODE_COLOR8 |
			   CG6_FBC_DRAW_RENDER | CG6_FBC_BWRITE0_ENABLE |
			   CG6_FBC_BWRITE1_DISABLE | CG6_FBC_BREAD_0 |
			   CG6_FBC_BDISP_0);
	cg6->fbc->clip = 0;
	cg6->fbc->offx = 0;
	cg6->fbc->offy = 0;
	cg6->fbc->clipminx = 0;
	cg6->fbc->clipminy = 0;
	cg6->fbc->clipmaxx = fb->type.fb_width - 1;
	cg6->fbc->clipmaxy = fb->type.fb_height - 1;
	/* Enable cursor in Brooktree DAC. */
	cg6->bt->addr = 0x06 << 24;
	cg6->bt->control |= 0x03 << 24;
}

__initfunc(void cg6_setup (fbinfo_t *fb, int slot, u32 cg6, int cg6_io))
{
	struct cg6_info *cg6info;
	unsigned int rev, cpu, conf;

	printk ("cgsix%d at 0x%8.8x ", slot, cg6);
	
	/* Fill in parameters we left out */
	fb->type.fb_cmsize = 256;
	fb->mmap = cg6_mmap;
	fb->loadcmap = cg6_loadcmap;
	fb->reset = cg6_reset;
	fb->blank = cg6_blank;
	fb->unblank = cg6_unblank;
	fb->setcursor = cg6_setcursor;
	fb->setcursormap = cg6_setcursormap;
	fb->setcurshape = cg6_setcurshape;
	fb->postsetup = sun_cg_postsetup;
	fb->blitc = cg6_blitc;
	fb->setw = cg6_setw;
	fb->cpyw = cg6_cpyw;
	
	cg6info = (struct cg6_info *) &fb->info.cg6;

	/* Map the hardware registers */
	cg6info->bt = sparc_alloc_io (cg6+CG6_BROOKTREE_OFFSET, 0,
		 sizeof (struct bt_regs), "cgsix_dac", cg6_io, 0);
	cg6info->fhc = sparc_alloc_io (cg6+CG6_FHC_OFFSET, 0,
		 sizeof (int), "cgsix_fhc", cg6_io, 0);
#if PAGE_SHIFT <= 12		 
	cg6info->thc = sparc_alloc_io (cg6+CG6_THC_OFFSET, 0,
		 sizeof (struct cg6_thc), "cgsix_thc", cg6_io, 0);
#else
	cg6info->thc = (struct cg6_thc *)(((char *)cg6info->fhc)+0x1000);
#endif
	cg6info->fbc = sparc_alloc_io (cg6+CG6_FBC_OFFSET, 0,
		 0x1000, "cgsix_fbc", cg6_io, 0);
#if PAGE_SHIFT <= 12		 
	cg6info->tec = sparc_alloc_io (cg6+CG6_TEC_OFFSET, 0,
		 sizeof (struct cg6_tec), "cgsix_tec", cg6_io, 0);
#else
	cg6info->tec = (struct cg6_tec *)(((char *)cg6info->fbc)+0x1000);
#endif
	cg6info->dhc = sparc_alloc_io (cg6+CG6_DHC_OFFSET, 0,
		 0x40000, "cgsix_dhc", cg6_io, 0);
	cg6info->rom = sparc_alloc_io (cg6+CG6_ROM_OFFSET, 0,
		 0x10000, "cgsix_rom", cg6_io, 0);
	if (!fb->base) {
		fb->base = (unsigned long)
			sparc_alloc_io (cg6+CG6_RAM_OFFSET, 0,
					fb->type.fb_size, "cgsix_ram", cg6_io, 0);
	}
	if (slot == sun_prom_console_id)
		fb_restore_palette = cg6_restore_palette;

	/* Initialize Brooktree DAC */
	cg6info->bt->addr = 0x04 << 24;		/* color planes */
	cg6info->bt->control = 0xff << 24;
	cg6info->bt->addr = 0x05 << 24;
	cg6info->bt->control = 0x00 << 24;
	cg6info->bt->addr = 0x06 << 24;		/* overlay plane */
	cg6info->bt->control = 0x73 << 24;
	cg6info->bt->addr = 0x07 << 24;
	cg6info->bt->control = 0x00 << 24;

	printk("TEC Rev %x ",
		(cg6info->thc->thc_misc >> CG6_THC_MISC_REV_SHIFT) &
		CG6_THC_MISC_REV_MASK);
		
	/* Get FHC Revision */
	conf = *(cg6info->fhc);

	cpu = conf & CG6_FHC_CPU_MASK;
	printk("CPU ");
	if (cpu == CG6_FHC_CPU_SPARC)
		printk("sparc ");
	else if (cpu == CG6_FHC_CPU_68020)
		printk("68020 ");
	else
		printk("386 ");

	rev = conf >> CG6_FHC_REV_SHIFT & CG6_FHC_REV_MASK;
	printk("Rev %x\n", rev);

	if (slot && sun_prom_console_id == slot)
		return;

	/* Reset the cg6 */
	cg6_reset(fb);
	
	if (!slot) {
		/* Enable Video */
		cg6_unblank(fb);
	} else {
		cg6_blank(fb);
	}
}

extern unsigned char vga_font[];

#define GX_BLITC_START(attr) \
	{ \
		register struct cg6_fbc *gx = fbinfo[0].info.cg6.fbc; \
		register uint i; \
		do { \
			i = gx->s; \
		} while (i & 0x10000000); \
		gx->fg = attr & 0xf; \
		gx->bg = (attr>>4); \
		gx->mode = 0x140000; \
		gx->alu = 0xe880fc30; \
		gx->pixelm = ~(0); \
		gx->s = 0; \
		gx->clip = 0; \
		gx->pm = 0xff;
#define GX_BLITC_BODY4(count,x,y,start,action) \
		while (count >= 4) { \
			count -= 4; \
		    	gx->incx = 0; \
		    	gx->incy = 1; \
		    	gx->x0 = x; \
		    	gx->x1 = (x += 32) - 1; \
		    	gx->y0 = y; \
		    	start; \
		    	for (i = 0; i < CHAR_HEIGHT; i++) { \
		    		action; \
		    	} \
		}
#define GX_BLITC_BODY1(x,y,action) \
		    	gx->incx = 0; \
		    	gx->incy = 1; \
		    	gx->x0 = x; \
		    	gx->x1 = (x += 8) - 1; \
		    	gx->y0 = y; \
		    	for (i = 0; i < CHAR_HEIGHT; i++) { \
		    		action; \
		    	}
#define GX_BLITC_END \
	}
	
static void cg6_blitc(unsigned short charattr, int xoff, int yoff)
{
	unsigned char attrib = CHARATTR_TO_SUNCOLOR(charattr);
	unsigned char *p = &vga_font[((unsigned char)charattr) << 4];
	GX_BLITC_START(attrib)
	GX_BLITC_BODY1(xoff, yoff, gx->font=((*p++) << 24))
	GX_BLITC_END
}

static void cg6_setw(int xoff, int yoff, unsigned short c, int count)
{
	unsigned char attrib = CHARATTR_TO_SUNCOLOR(c);
	unsigned char *p = &vga_font[((unsigned char)c) << 4];
	register unsigned char *q;
	register uint l;
	GX_BLITC_START(attrib)
	if (count >= 4) {
		GX_BLITC_BODY4(count, xoff, yoff, q = p, 
			l = *q++;
			l |= l << 8;
			l |= l << 16;
			gx->font=l)
	}
	while (count) {
		count--;
		q = p;
		GX_BLITC_BODY1(xoff, yoff, gx->font=((*q++) << 24));
	}
	GX_BLITC_END
}

static void cg6_cpyw(int xoff, int yoff, unsigned short *p, int count)
{
	unsigned char attrib = CHARATTR_TO_SUNCOLOR(*p);
	unsigned char *p1, *p2, *p3, *p4;
	GX_BLITC_START(attrib)
	if (count >= 4) {
		GX_BLITC_BODY4(count, xoff, yoff, 
			p1 = &vga_font[((unsigned char)*p++) << 4];
			p2 = &vga_font[((unsigned char)*p++) << 4];
			p3 = &vga_font[((unsigned char)*p++) << 4];
			p4 = &vga_font[((unsigned char)*p++) << 4], 
			gx->font=((uint)*p4++) | ((((uint)*p3++) | ((((uint)*p2++) | (((uint)*p1++) << 8)) << 8)) << 8))
	}
	while (count) {
		count--;
		p1 = &vga_font[((unsigned char)*p++) << 4];
		GX_BLITC_BODY1(xoff, yoff, gx->font=((*p1++) << 24));
	}
	GX_BLITC_END
}

#define GX_FILL_START(attr) \
	{ \
		register struct cg6_fbc *gx = fbinfo[0].info.cg6.fbc; \
		register uint i; \
		do { \
			i = gx->s; \
		} while (i & 0x10000000); \
		gx->fg = attr & 0xf; \
		gx->bg = 0; \
		gx->pixelm = ~(0); \
		gx->s = 0; \
		gx->alu = 0xea80ff00; \
		gx->pm = ~(0); \
		gx->clip = 0;
#define GX_FILL_END \
	}
	
#if 0
static void cg6_fill(int attrib, int count, int *boxes)
{
	register int r;
	
	attrib = 5;
	GX_FILL_START(attrib)
	while (count-- > 0) {
		gx->arecty = boxes[1];
		gx->arectx = boxes[0];
		gx->arecty = boxes[3];
		gx->arecty = boxes[2];
		boxes += 4;
		do {
			r = gx->draw;
		} while (r < 0 && (r & 0x20000000) );
	}
	GX_FILL_END
}
#endif
