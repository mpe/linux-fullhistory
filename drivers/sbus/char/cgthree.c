/* $Id: cgthree.c,v 1.10 1996/12/23 10:16:07 ecd Exp $
 * cgtree.c: cg3 frame buffer driver
 *
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
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

/* The cg3 palette is loaded with 4 color values at each time  */
/* so you end up with: (rgb)(r), (gb)(rg), (b)(rgb), and so on */
static void
cg3_loadcmap (fbinfo_t *fb, int index, int count)
{
	struct bt_regs *bt = fb->info.cg3.bt;
	int *i, steps;

	i = (((int *)fb->color_map) + D4M3(index));
	steps = D4M3(index+count-1) - D4M3(index)+3;
#if 0	
	if (fb->info.cg3.cgrdi) {
		*(volatile u8 *)bt->addr = (u8)(D4M4(index));
	} else
#endif	
	bt->addr = D4M4(index);
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
			map_offset = get_phys ((uint) fb->base);
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

__initfunc(void cg3_setup (fbinfo_t *fb, int slot, uint cg3, int cg3_io, struct linux_sbus_device *sbdp))
{
	struct cg3_info *cg3info = (struct cg3_info *) &fb->info.cg3;

	if (strstr (sbdp->prom_name, "cgRDI")) {
		char buffer[40];
		char *p;
		int w, h;
		
		prom_getstring (sbdp->prom_node, "params", buffer, sizeof(buffer));
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

	/* Map the card registers */
	cg3info->bt = sparc_alloc_io ((void *) cg3+CG3_REGS, 0,
		 sizeof (struct bt_regs),"cg3_bt", cg3_io, 0);
	/* cgRDI actually has 32 bytes of regs, but I don't understand
	   those bitfields yet (guess it is some interrupt stuff etc. */
	
	if (!fb->base){
		fb->base=(uint) sparc_alloc_io ((void*) cg3+CG3_RAM, 0,
				    fb->type.fb_size, "cg3_ram", cg3_io, 0);
	}
}

