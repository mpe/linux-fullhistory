/*
 * fixmap.h: compile-time virtual memory allocation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ingo Molnar
 */

#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#include <asm/page.h>
#include <linux/kernel.h>

/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process. We allocate these special  addresses
 * from the end of virtual memory (0xfffff000) backwards.
 * Also this lets us do fail-safe vmalloc(), we
 * can guarantee that these special addresses and
 * vmalloc()-ed addresses never overlap.
 *
 * these 'compile-time allocated' memory buffers are
 * fixed-size 4k pages. (or larger if used with an increment
 * bigger than 1) use fixmap_set(idx,phys) to associate
 * physical memory with fixmap indices.
 *
 * TLB entries of such buffers will not be flushed across
 * task switches.
 */

enum fixed_addresses {
/*
 * on UP currently we will have no trace of the fixmap mechanizm,
 * no page table allocations, etc. This might change in the
 * future, say framebuffers for the console driver(s) could be
 * fix-mapped?
 */
#if __SMP__
	FIX_APIC_BASE 			= 1,	/* 0xfffff000 */
	FIX_IO_APIC_BASE 		= 2,	/* 0xffffe000 */
#endif
	__end_of_fixed_addresses
};

extern void set_fixmap (enum fixed_addresses idx, unsigned long phys);

/*
 * used by vmalloc.c:
 */
#define FIXADDR_START (0UL-((__end_of_fixed_addresses-1)<<PAGE_SHIFT))

/*
 * 'index to address' translation. If anyone tries to use the idx
 * directly without tranlation, we catch the bug with a NULL-deference
 * kernel oops. Illegal ranges of incoming indices are caught too.
 */
extern inline unsigned long fix_to_virt(const unsigned int idx)
{
	/*
	 * this branch gets completely eliminated after inlining,
	 * except when someone tries to use fixaddr indices in an
	 * illegal way. (such as mixing up address types or using
	 * out-of-range indices)
	 */
	if ((!idx) || (idx >= __end_of_fixed_addresses))
		panic("illegal fixaddr index!");

        return (0UL-(unsigned long)(idx<<PAGE_SHIFT));
}

#endif
