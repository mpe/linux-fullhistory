/* $Id: weitek.c,v 1.14 1997/07/17 02:21:53 davem Exp $
 * weitek.c: Tadpole P9100/P9000 console driver
 *
 * Copyright (C) 1996 David Redman (djhr@tadpole.co.uk)
 */

#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>

#include <asm/openprom.h>
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

/*
 * mmap info
 */
#define WEITEK_VRAM_OFFSET	0
#define WEITEK_VRAM_SIZE	(2*1024*1024)	/* maximum */
#define WEITEK_GX_REG_OFFSET	WEITEK_VRAM_SIZE
#define WEITEK_GX_REG_SIZE	8192
#define WEITEK_VID_REG_OFFSET	(WEITEK_GX_REG_OFFSET+WEITEK_GX_REG_SIZE)
#define WEITEK_VID_REG_SIZE	0x1000

#define CONTROL_OFFSET	0
#define RAMDAC_OFFSET	(CONTROL_OFFSET+0x200)

#if 0
static int
weitek_mmap(struct inode *inode, struct file *file, struct vm_area_struct *vma,
	    long base, fbinfo_t *fb)
{
	unsigned int size, page, r, map_size;
	unsigned long map_offset = 0;
	
	size = vma->vm_end - vma->vm_start;
	if (vma->vm_offset & ~PAGE_MASK)
		return -ENXIO;
	
	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= FB_MMAP_VM_FLAGS; 
	
	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		switch (vma->vm_offset+page){
		case WEITEK_VRAM_OFFSET:
			map_size = size-page;
			map_offset = get_phys ((unsigned long) fb->base);
			if (map_size > fb->type.fb_size)
				map_size = fb->type.fb_size;
			break;
		case WEITEK_GX_REG_OFFSET:
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
		if (r)
			return -EAGAIN;
		page += map_size;
	}

	vma->vm_dentry = dget(file->f_dentry);
	return 0;
}
#endif

#if 0
static void
weitek_loadcmap (void *fbinfo, int index, int count)
{
	printk("weitek_cmap: unimplemented!\n");
}
#endif

__initfunc(void weitek_setup(fbinfo_t *fb, int slot, u32 addr, int io))
{
	printk ("weitek%d at 0x%8.8x\n", slot, addr);
	
	/* Fill in parameters we left out */
	fb->type.fb_type	= FBTYPE_NOTSUN1;
	fb->type.fb_cmsize	= 256;
	fb->mmap		= 0; /* weitek_mmap; */
	fb->loadcmap		= 0; /* unimplemented */
	fb->ioctl		= 0; /* no special ioctls */
	fb->reset		= 0; /* no special reset */
	
	/* Map the card registers */
	if (!fb->base){
	    prom_printf ("Missing mapping routine and no address found\n");
	}
}
