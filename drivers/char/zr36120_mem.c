/*
    zr36120_mem.c - Zoran 36120/36125 based framegrabbers

    Copyright (C) 1998-1999 Pauline Middelink <middelin@polyware.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/wrapper.h>
#include <asm/io.h>
#ifdef CONFIG_BIGPHYS_AREA
#include <linux/bigphysarea.h>
#endif

#include "zr36120.h"
#include "zr36120_mem.h"

/* ----------------------------------------------------------------------- */
/* Memory functions							   */
/* shamelessly stolen and adapted from bttv.c				   */
/* ----------------------------------------------------------------------- */

/*
 * convert virtual user memory address to physical address
 * (virt_to_phys only works for kmalloced kernel memory)
 */
inline unsigned long uvirt_to_phys(unsigned long adr)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep, pte;

	pgd = pgd_offset(current->mm, adr);
	if (pgd_none(*pgd))
		return 0;
	pmd = pmd_offset(pgd, adr);
	if (pmd_none(*pmd))
		return 0;
	ptep = pte_offset(pmd, adr/*&(~PGDIR_MASK)*/);
	pte = *ptep;
	/* Note; page_address will panic for us if the page is high */
        if(pte_present(pte))
        	return page_address(pte_page(pte))|(adr&(PAGE_SIZE-1));
	return 0;
}

/*
 * vmalloced address to physical address
 */
inline unsigned long kvirt_to_phys(unsigned long adr)
{
	return uvirt_to_phys(VMALLOC_VMADDR(adr));
}

/*
 * vmalloced address to bus address
 */
inline unsigned long kvirt_to_bus(unsigned long adr)
{
	return virt_to_bus(phys_to_virt(kvirt_to_phys(adr)));
}

inline int order(unsigned long size)
{
	int ordr = 0;
	size = (size+PAGE_SIZE-1)/PAGE_SIZE;
	while (size) {
		size /= 2;
		ordr++;
	}
	return ordr;
}

void* bmalloc(unsigned long size)
{
	void* mem;
#ifdef CONFIG_BIGPHYS_AREA
	mem = bigphysarea_alloc_pages(size/PAGE_SIZE, 1, GFP_KERNEL);
#else
	/*
	 * The following function got a lot of memory at boottime,
	 * so we know its always there...
	 */
	mem = (void*)__get_free_pages(GFP_USER,order(size));
#endif
	if (mem) {
		unsigned long adr = (unsigned long)mem;
		while (size > 0) {
			mem_map_reserve(MAP_NR(phys_to_virt(adr)));
			adr += PAGE_SIZE;
			size -= PAGE_SIZE;
		}
	}
	return mem;
}

void bfree(void* mem, unsigned long size)
{
	if (mem) {
		unsigned long adr = (unsigned long)mem;
		unsigned long siz = size;
		while (siz > 0) {
			mem_map_unreserve(MAP_NR(phys_to_virt(adr)));
			adr += PAGE_SIZE;
			siz -= PAGE_SIZE;
		}
#ifdef CONFIG_BIGPHYS_AREA
		bigphysarea_free_pages(mem);
#else
		free_pages((unsigned long)mem,order(size));
#endif
	}
}
