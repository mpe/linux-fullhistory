/*-*- linux-c -*-
 *  linux/drivers/video/i810_accel.c -- Hardware Acceleration
 *
 *      Copyright (C) 2001 Antonino Daplas<adaplas@pol.net>
 *      All Rights Reserved      
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */
#include <linux/kernel.h>
#include <linux/fb.h>

#include "i810_regs.h"
#include "i810.h"

static u32 i810fb_rop[] = {
	COLOR_COPY_ROP, /* ROP_COPY */
	XOR_ROP         /* ROP_XOR  */
};

/* Macros */
#define PUT_RING(n) {                                        \
	i810_writel(par->cur_tail, par->iring.virtual, n);   \
        par->cur_tail += 4;                                  \
        par->cur_tail &= RING_SIZE_MASK;                     \
}                                                                      

extern inline void flush_cache(void);
extern int reinit_agp(struct fb_info *info);

/************************************************************/

/* BLT Engine Routines */
static inline void i810_report_error(u8 *mmio)
{
	printk("IIR     : 0x%04x\n"
	       "EIR     : 0x%04x\n"
	       "PGTBL_ER: 0x%04x\n"
	       "IPEIR   : 0x%04x\n"
	       "IPEHR   : 0x%04x\n",
	       i810_readw(IIR, mmio),
	       i810_readb(EIR, mmio),
	       i810_readl(PGTBL_ER, mmio),
	       i810_readl(IPEIR, mmio), 
	       i810_readl(IPEHR, mmio));
}

/**
 * wait_for_space - check ring buffer free space
 * @space: amount of ringbuffer space needed in bytes
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * The function waits until a free space from the ringbuffer
 * is available 
 */	
static inline int wait_for_space(struct i810fb_par *par, u32 space)
{
	u32 head, count = WAIT_COUNT, tail;
	u8 *mmio = par->mmio_start_virtual;

	tail = par->cur_tail;
	while (count--) {
		head = i810_readl(IRING + 4, mmio) & RBUFFER_HEAD_MASK;	
		if ((tail == head) || 
		    (tail > head && 
		     (par->iring.size - tail + head) >= space) || 
		    (tail < head && (head - tail) >= space)) {
			return 0;	
		}
	}
	printk("ringbuffer lockup!!!\n");
	i810_report_error(mmio); 
	par->dev_flags |= LOCKUP;
	return 1;
}



/** 
 * wait_for_engine_idle - waits for all hardware engines to finish
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * This waits for lring(0), iring(1), and batch(3), etc to finish and
 * waits until ringbuffer is empty.
 */
static inline int wait_for_engine_idle(struct i810fb_par *par)
{
	u8 *mmio = par->mmio_start_virtual;
	int count = WAIT_COUNT;

	while((i810_readw(INSTDONE, mmio) & 0x7B) != 0x7B && --count); 
	if (count) return 0;

	printk("accel engine lockup!!!\n");
	printk("INSTDONE: 0x%04x\n", i810_readl(INSTDONE, mmio));
	i810_report_error(mmio); 
	par->dev_flags |= LOCKUP;
	return 1;
}

/* begin_iring - prepares the ringbuffer 
 * @space: length of sequence in dwords
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Checks/waits for sufficent space in ringbuffer of size
 * space.  Returns the tail of the buffer
 */ 
static inline u32 begin_iring(struct i810fb_par *par, u32 space)
{
	if (par->dev_flags & ALWAYS_SYNC) 
		wait_for_engine_idle(par);
	return wait_for_space(par, space);
}

/**
 * end_iring - advances the buffer
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * This advances the tail of the ringbuffer, effectively
 * beginning the execution of the graphics instruction sequence.
 */
static inline void end_iring(struct i810fb_par *par)
{
	u8 *mmio = par->mmio_start_virtual;

	i810_writel(IRING, mmio, par->cur_tail);
}

/**
 * source_copy_blit - BLIT transfer operation
 * @dwidth: width of rectangular graphics data
 * @dheight: height of rectangular graphics data
 * @dpitch: bytes per line of destination buffer
 * @xdir: direction of copy (left to right or right to left)
 * @src: address of first pixel to read from
 * @dest: address of first pixel to write to
 * @from: source address
 * @where: destination address
 * @rop: raster operation
 * @blit_bpp: pixel format which can be different from the 
 *            framebuffer's pixelformat
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * This is a BLIT operation typically used when doing
 * a 'Copy and Paste'
 */
static inline void source_copy_blit(int dwidth, int dheight, int dpitch, 
				    int xdir, int src, int dest, int rop, 
				    int blit_bpp, struct i810fb_par *par)
{
	if (begin_iring(par, 24 + IRING_PAD)) return;
	PUT_RING(BLIT | SOURCE_COPY_BLIT | 4);
	PUT_RING(xdir | rop << 16 | dpitch | DYN_COLOR_EN | blit_bpp);
	PUT_RING(dheight << 16 | dwidth);
	PUT_RING(dest);
	PUT_RING(dpitch);
	PUT_RING(src);
	end_iring(par);
}	

/**
 * color_blit - solid color BLIT operation
 * @width: width of destination
 * @height: height of destination
 * @pitch: pixels per line of the buffer
 * @dest: address of first pixel to write to
 * @where: destination
 * @rop: raster operation
 * @what: color to transfer
 * @blit_bpp: pixel format which can be different from the 
 *            framebuffer's pixelformat
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * A BLIT operation which can be used for  color fill/rectangular fill
 */
static inline void color_blit(int width, int height, int pitch,  int dest, 
			      int rop, int what, int blit_bpp, 
			      struct i810fb_par *par)
{
	if (begin_iring(par, 24 + IRING_PAD)) return;
	PUT_RING(BLIT | COLOR_BLT | 3);
	PUT_RING(rop << 16 | pitch | SOLIDPATTERN | DYN_COLOR_EN | blit_bpp);
	PUT_RING(height << 16 | width);
	PUT_RING(dest);
	PUT_RING(what);
	PUT_RING(NOP);
	end_iring(par);
}
 
/**
 * mono_src_copy_imm_blit - color expand from system memory to framebuffer
 * @dwidth: width of destination
 * @dheight: height of destination
 * @dpitch: pixels per line of the buffer
 * @dsize: size of bitmap in double words
 * @dest: address of first byte of pixel;
 * @rop: raster operation
 * @blit_bpp: pixelformat to use which can be different from the 
 *            framebuffer's pixelformat
 * @src: address of image data
 * @bg: backgound color
 * @fg: forground color
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * A color expand operation where the  source data is placed in the 
 * ringbuffer itself. Useful for drawing text. 
 *
 * REQUIREMENT:
 * The end of a scanline must be padded to the next word.
 */
static inline void mono_src_copy_imm_blit(int dwidth, int dheight, int dpitch,
					  int dsize, int blit_bpp, int rop,
					  int dest, const u32 *src, int bg,
					  int fg, struct i810fb_par *par)
{
	u32 i, *s = (u32 *) src;

	if (begin_iring(par, 24 + (dsize << 2) + IRING_PAD)) return;
	PUT_RING(BLIT | MONO_SOURCE_COPY_IMMEDIATE | (4 + dsize));
	PUT_RING(DYN_COLOR_EN | blit_bpp | rop << 16 | dpitch);
	PUT_RING(dheight << 16 | dwidth);
	PUT_RING(dest);
	PUT_RING(bg);
	PUT_RING(fg);
	for (i = dsize; i--; ) 
		PUT_RING(*s++);

	end_iring(par);
}

/**
 * mono_src_copy_blit - color expand from video memory to framebuffer
 * @dwidth: width of destination
 * @dheight: height of destination
 * @dpitch: pixels per line of the buffer
 * @qsize: size of bitmap in quad words
 * @dest: address of first byte of pixel;
 * @rop: raster operation
 * @blit_bpp: pixelformat to use which can be different from the 
 *            framebuffer's pixelformat
 * @src: address of image data
 * @bg: backgound color
 * @fg: forground color
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * A color expand operation where the  source data is in video memory. 
 * Useful for drawing text. 
 *
 * REQUIREMENT:
 * The end of a scanline must be padded to the next word.
 */
static inline void mono_src_copy_blit(int dwidth, int dheight, int dpitch, 
				      int qsize, int blit_bpp, int rop, 
				      int dest, int src, int bg,
				      int fg, struct i810fb_par *par)
{
	if (begin_iring(par, 32 + IRING_PAD)) return;

	PUT_RING(BLIT | MONO_SOURCE_COPY_BLIT | 6);
	PUT_RING(DYN_COLOR_EN | blit_bpp | rop << 16 | dpitch | 1 << 27);
	PUT_RING(dheight << 16 | dwidth);
	PUT_RING(dest);
	PUT_RING(qsize - 1);
	PUT_RING(src);
	PUT_RING(bg);
	PUT_RING(fg);

	end_iring(par);
}

static u32 get_buffer_offset(u32 size, struct i810fb_par *par) 
{
	u32 offset;

	if (par->pixmap_offset + size > par->pixmap.size) {
		wait_for_engine_idle(par);
		par->pixmap_offset = 0;
	}

	offset = par->pixmap_offset;
	par->pixmap_offset += size;

	return offset;
}

/**
 * i810fb_iring_enable - enables/disables the ringbuffer
 * @mode: enable or disable
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Enables or disables the ringbuffer, effectively enabling or
 * disabling the instruction/acceleration engine.
 */
static inline void i810fb_iring_enable(struct i810fb_par *par, u32 mode)
{
	u32 tmp;
	u8 *mmio = par->mmio_start_virtual;

	tmp = i810_readl(IRING + 12, mmio);
	if (mode == OFF) 
		tmp &= ~1;
	else 
		tmp |= 1;
	flush_cache();
	i810_writel(IRING + 12, mmio, tmp);
}       

void i810fb_fillrect(struct fb_info *p, struct fb_fillrect *rect)
{
	struct i810fb_par *par = (struct i810fb_par *) p->par;
	u32 dx, dy, width, height, dest, rop = 0, color = 0;

	if (!p->var.accel_flags || par->dev_flags & LOCKUP) {
		cfb_fillrect(p, rect);
		return;
	}

	if (par->depth == 4) {
		wait_for_engine_idle(par);
		cfb_fillrect(p, rect);
		return;
	}
			
	if (par->depth == 1) 
		color = rect->color;
	else 
		color = ((u32 *) (p->pseudo_palette))[rect->color];

	rop = i810fb_rop[rect->rop];

	dx = rect->dx * par->depth;
	width = rect->width * par->depth;
	dy = rect->dy;
	height = rect->height;

	dest = p->fix.smem_start +  (dy * p->fix.line_length) + dx;
	color_blit(width, height, p->fix.line_length, dest, rop, color, 
		   par->blit_bpp, par);
}
	
void i810fb_copyarea(struct fb_info *p, struct fb_copyarea *region) 
{
	struct i810fb_par *par = (struct i810fb_par *) p->par;
	u32 sx, sy, dx, dy, pitch, width, height, src, dest, xdir;

	if (!p->var.accel_flags || par->dev_flags & LOCKUP) {
		cfb_copyarea(p, region);
		return;
	}

	if (par->depth == 4) {
		wait_for_engine_idle(par);
		cfb_copyarea(p, region);
		return;
	}

	dx = region->dx * par->depth;
	sx = region->sx * par->depth;
	width = region->width * par->depth;
	sy = region->sy;
	dy = region->dy;
	height = region->height;

	if (dx <= sx) {
		xdir = INCREMENT;
	}
	else {
		xdir = DECREMENT;
		sx += width - 1;
		dx += width - 1;
	}
	if (dy <= sy) {
		pitch = p->fix.line_length;
	}
	else {
		pitch = (-(p->fix.line_length)) & 0xFFFF;
		sy += height - 1;
		dy += height - 1;
	}
	src = p->fix.smem_start + (sy * p->fix.line_length) + sx;
	dest = p->fix.smem_start + (dy * p->fix.line_length) + dx;

	source_copy_blit(width, height, pitch, xdir, src, dest,
			 PAT_COPY_ROP, par->blit_bpp, par);
}

/*
 * Blitting is done at 8x8 pixel-array at a time.  If map is not 
 * monochrome or not a multiple of 8x8 pixels, cfb_imageblit will
 * be called instead.  
 */
void i810fb_imageblit(struct fb_info *p, struct fb_image *image)
{
	struct i810fb_par *par = (struct i810fb_par *) p->par;
	u32 fg = 0, bg = 0, s_pitch, d_pitch, size, offset, dst, i, j;
	u8 *s_addr, *d_addr;
	
	if (!p->var.accel_flags || par->dev_flags & LOCKUP) {
		cfb_imageblit(p, image);
		return;
	}

	if (par->depth == 4 || image->depth != 1) {
		wait_for_engine_idle(par);
		cfb_imageblit(p, image);
		return;
	}

	switch (p->var.bits_per_pixel) {
	case 8:
		fg = image->fg_color;
		bg = image->bg_color;
		break;
	case 16:
		fg = ((u32 *)(p->pseudo_palette))[image->fg_color];
		bg = ((u32 *)(p->pseudo_palette))[image->bg_color];
		break;
	case 24:
		fg = ((u32 *)(p->pseudo_palette))[image->fg_color];
		bg = ((u32 *)(p->pseudo_palette))[image->bg_color];
		break;
	}	
	
	dst = p->fix.smem_start + (image->dy * p->fix.line_length) + 
		(image->dx * par->depth);

	s_pitch = image->width/8;
	d_pitch = (s_pitch + 1) & ~1;
	
	size = d_pitch * image->height;

	if (image->width & 15) {
		offset = get_buffer_offset(size, par);
		
		d_addr = par->pixmap.virtual + offset;
		s_addr = image->data;
		
		for (i = image->height; i--; ) {
			for (j = 0; j < s_pitch; j++) 
				i810_writeb(j, d_addr, s_addr[j]);
			s_addr += s_pitch;
			d_addr += d_pitch;
		}
		
		mono_src_copy_blit(image->width * par->depth, image->height, 
				   p->fix.line_length, size/8, par->blit_bpp,
				   PAT_COPY_ROP, dst, 
				   par->pixmap.physical + offset,
				   bg, fg, par);
	}
	/*
	 * immediate blit if width is a multiple of 16 (hardware requirement)
	 */
	else {
		mono_src_copy_imm_blit(image->width * par->depth, 
				       image->height, p->fix.line_length, 
				       size/4, par->blit_bpp,
				       PAT_COPY_ROP, dst, (u32 *) image->data, 
				       bg, fg, par);
	} 
}

int i810fb_sync(struct fb_info *p)
{
	struct i810fb_par *par = (struct i810fb_par *) p->par;
	
	if (!p->var.accel_flags)
		return 0;

	return wait_for_engine_idle(par);
}


/**
 * i810fb_init_ringbuffer - initialize the ringbuffer
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Initializes the ringbuffer by telling the device the
 * size and location of the ringbuffer.  It also sets 
 * the head and tail pointers = 0
 */
void i810fb_init_ringbuffer(struct i810fb_par *par)
{
	u32 tmp1, tmp2;
	u8 *mmio = par->mmio_start_virtual;
	
	wait_for_engine_idle(par);
	i810fb_iring_enable(par, OFF);
	i810_writel(IRING, mmio, 0);
	i810_writel(IRING + 4, mmio, 0);
	par->cur_tail = 0;

	tmp2 = i810_readl(IRING + 8, mmio) & ~RBUFFER_START_MASK; 
	tmp1 = par->iring.physical;
	i810_writel(IRING + 8, mmio, tmp2 | tmp1);

	tmp1 = i810_readl(IRING + 12, mmio);
	tmp1 &= ~RBUFFER_SIZE_MASK;
	tmp2 = (par->iring.size - I810_PAGESIZE) & RBUFFER_SIZE_MASK;
	i810_writel(IRING + 12, mmio, tmp1 | tmp2);
	i810fb_iring_enable(par, ON);
}
