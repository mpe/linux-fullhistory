/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 * This file contains the routines for handling the MMU on those
 * PowerPC implementations where the MMU substantially follows the
 * architecture specification.  This includes the 6xx, 7xx, 7xxx,
 * 8260, and POWER3 implementations but excludes the 8xx and 4xx.
 * Although the iSeries hardware does comply with the architecture
 * specification, the need to work through the hypervisor makes
 * things sufficiently different that it is handled elsewhere.
 *  -- paulus
 * 
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/prom.h>
#include <asm/mmu.h>
#include <asm/machdep.h>

#include "mmu_decl.h"
#include "mem_pieces.h"

PTE *Hash, *Hash_end;
unsigned long Hash_size, Hash_mask;
unsigned long _SDR1;

union ubat {			/* BAT register values to be loaded */
	BAT	bat;
#ifdef CONFIG_PPC64BRIDGE
	u64	word[2];
#else
	u32	word[2];
#endif	
} BATS[4][2];			/* 4 pairs of IBAT, DBAT */

struct batrange {		/* stores address ranges mapped by BATs */
	unsigned long start;
	unsigned long limit;
	unsigned long phys;
} bat_addrs[4];

/*
 * Return PA for this VA if it is mapped by a BAT, or 0
 */
unsigned long v_mapped_by_bats(unsigned long va)
{
	int b;
	for (b = 0; b < 4; ++b)
		if (va >= bat_addrs[b].start && va < bat_addrs[b].limit)
			return bat_addrs[b].phys + (va - bat_addrs[b].start);
	return 0;
}

/*
 * Return VA for a given PA or 0 if not mapped
 */
unsigned long p_mapped_by_bats(unsigned long pa)
{
	int b;
	for (b = 0; b < 4; ++b)
		if (pa >= bat_addrs[b].phys
	    	    && pa < (bat_addrs[b].limit-bat_addrs[b].start)
		              +bat_addrs[b].phys)
			return bat_addrs[b].start+(pa-bat_addrs[b].phys);
	return 0;
}

void __init bat_mapin_ram(void)
{
	unsigned long tot, bl, done;
	unsigned long max_size = (256<<20);
	unsigned long align;

	/* Set up BAT2 and if necessary BAT3 to cover RAM. */

	/* Make sure we don't map a block larger than the
	   smallest alignment of the physical address. */
	/* alignment of ram_phys_base */
	align = ~(ram_phys_base-1) & ram_phys_base;
	/* set BAT block size to MIN(max_size, align) */
	if (align && align < max_size)
		max_size = align;

	tot = total_lowmem;
	for (bl = 128<<10; bl < max_size; bl <<= 1) {
		if (bl * 2 > tot)
			break;
	}

	setbat(2, KERNELBASE, ram_phys_base, bl, _PAGE_KERNEL);
	done = (unsigned long)bat_addrs[2].limit - KERNELBASE + 1;
	if ((done < tot) && !bat_addrs[3].limit) {
		/* use BAT3 to cover a bit more */
		tot -= done;
		for (bl = 128<<10; bl < max_size; bl <<= 1)
			if (bl * 2 > tot)
				break;
		setbat(3, KERNELBASE+done, ram_phys_base+done, bl, 
		       _PAGE_KERNEL);
	}
}

/*
 * Set up one of the I/D BAT (block address translation) register pairs.
 * The parameters are not checked; in particular size must be a power
 * of 2 between 128k and 256M.
 */
void __init setbat(int index, unsigned long virt, unsigned long phys,
		   unsigned int size, int flags)
{
	unsigned int bl;
	int wimgxpp;
	union ubat *bat = BATS[index];

#ifdef CONFIG_SMP
	if ((flags & _PAGE_NO_CACHE) == 0)
		flags |= _PAGE_COHERENT;
#endif
	bl = (size >> 17) - 1;
	if (PVR_VER(mfspr(PVR)) != 1) {
		/* 603, 604, etc. */
		/* Do DBAT first */
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT | _PAGE_GUARDED);
		wimgxpp |= (flags & _PAGE_RW)? BPP_RW: BPP_RX;
		bat[1].word[0] = virt | (bl << 2) | 2; /* Vs=1, Vp=0 */
		bat[1].word[1] = phys | wimgxpp;
#ifndef CONFIG_KGDB /* want user access for breakpoints */
		if (flags & _PAGE_USER)
#endif
			bat[1].bat.batu.vp = 1;
		if (flags & _PAGE_GUARDED) {
			/* G bit must be zero in IBATs */
			bat[0].word[0] = bat[0].word[1] = 0;
		} else {
			/* make IBAT same as DBAT */
			bat[0] = bat[1];
		}
	} else {
		/* 601 cpu */
		if (bl > BL_8M)
			bl = BL_8M;
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT);
		wimgxpp |= (flags & _PAGE_RW)?
			((flags & _PAGE_USER)? PP_RWRW: PP_RWXX): PP_RXRX;
		bat->word[0] = virt | wimgxpp | 4;	/* Ks=0, Ku=1 */
		bat->word[1] = phys | bl | 0x40;	/* V=1 */
	}

	bat_addrs[index].start = virt;
	bat_addrs[index].limit = virt + ((bl + 1) << 17) - 1;
	bat_addrs[index].phys = phys;
}

/*
 * Initialize the hash table and patch the instructions in hashtable.S.
 */
void __init MMU_init_hw(void)
{
	int Hash_bits, mb, mb2;
	unsigned int hmask;

	extern unsigned int hash_page_patch_A[];
	extern unsigned int hash_page_patch_B[], hash_page_patch_C[];
	extern unsigned int hash_page[];
	extern unsigned int flush_hash_patch_A[], flush_hash_patch_B[];

#ifdef CONFIG_PPC64BRIDGE
	/* The hash table has already been allocated and initialized
	   in prom.c */
	Hash_mask = (Hash_size >> 7) - 1;
	hmask = Hash_mask >> 9;
	Hash_bits = __ilog2(Hash_size) - 7;
	mb = 25 - Hash_bits;
	if (Hash_bits > 16)
		Hash_bits = 16;
	mb2 = 25 - Hash_bits;

	/* Remove the hash table from the available memory */
	if (Hash)
		reserve_phys_mem(__pa(Hash), Hash_size);

#else /* CONFIG_PPC64BRIDGE */
	unsigned int h;

	if ((cur_cpu_spec[0]->cpu_features & CPU_FTR_HPTE_TABLE) == 0)
		return;
	if ( ppc_md.progress ) ppc_md.progress("hash:enter", 0x105);
	/*
	 * Allow 64k of hash table for every 16MB of memory,
	 * up to a maximum of 2MB.
	 */
	for (h = 64<<10; h < total_memory / 256 && h < (2<<20); h *= 2)
		;
	Hash_size = h;
	Hash_mask = (h >> 6) - 1;
	hmask = Hash_mask >> 10;
	Hash_bits = __ilog2(h) - 6;
	mb = 26 - Hash_bits;
	if (Hash_bits > 16)
		Hash_bits = 16;
	mb2 = 26 - Hash_bits;

	if ( ppc_md.progress ) ppc_md.progress("hash:find piece", 0x322);
	/* Find some memory for the hash table. */
	if ( Hash_size ) {
		Hash = mem_pieces_find(Hash_size, Hash_size);
		cacheable_memzero(Hash, Hash_size);
		_SDR1 = __pa(Hash) | (Hash_mask >> 10);
	} else
		Hash = 0;
#endif /* CONFIG_PPC64BRIDGE */

	printk("Total memory = %ldMB; using %ldkB for hash table (at %p)\n",
	       total_memory >> 20, Hash_size >> 10, Hash);
	if (Hash_size) {
		if ( ppc_md.progress ) ppc_md.progress("hash:patch", 0x345);
		Hash_end = (PTE *) ((unsigned long)Hash + Hash_size);

		/*
		 * Patch up the instructions in hashtable.S:create_hpte
		 */
		hash_page_patch_A[0] = (hash_page_patch_A[0] & ~0xffff)
			| ((unsigned int)(Hash) >> 16);
		hash_page_patch_A[1] = (hash_page_patch_A[1] & ~0x7c0)
			| (mb << 6);
		hash_page_patch_A[2] = (hash_page_patch_A[2] & ~0x7c0)
			| (mb2 << 6);
		hash_page_patch_B[0] = (hash_page_patch_B[0] & ~0xffff)
			| hmask;
		hash_page_patch_C[0] = (hash_page_patch_C[0] & ~0xffff)
			| hmask;
		/*
		 * Ensure that the locations we've patched have been written
		 * out from the data cache and invalidated in the instruction
		 * cache, on those machines with split caches.
		 */
		flush_icache_range((unsigned long) &hash_page_patch_A[0],
				   (unsigned long) &hash_page_patch_C[1]);
		/*
		 * Patch up the instructions in hashtable.S:flush_hash_page
		 */
		flush_hash_patch_A[0] = (flush_hash_patch_A[0] & ~0xffff)
			| ((unsigned int)(Hash) >> 16);
		flush_hash_patch_A[1] = (flush_hash_patch_A[1] & ~0x7c0)
			| (mb << 6);
		flush_hash_patch_A[2] = (flush_hash_patch_A[2] & ~0x7c0)
			| (mb2 << 6);
		flush_hash_patch_B[0] = (flush_hash_patch_B[0] & ~0xffff)
			| hmask;
		flush_icache_range((unsigned long) &flush_hash_patch_A[0],
				   (unsigned long) &flush_hash_patch_B[1]);
	}
	else {
		Hash_end = 0;
		/*
		 * Put a blr (procedure return) instruction at the
		 * start of hash_page, since we can still get DSI
		 * exceptions on a 603.
		 */
		hash_page[0] = 0x4e800020;
		flush_icache_range((unsigned long) &hash_page[0],
				   (unsigned long) &hash_page[1]);
	}

	if ( ppc_md.progress ) ppc_md.progress("hash:done", 0x205);
}

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a PTE in the linux page tables.
 * We use it to preload an HPTE into the hash table corresponding to
 * the updated linux PTE.
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long address,
		      pte_t pte)
{
	struct mm_struct *mm;
	pmd_t *pmd;
	pte_t *ptep;
	static int nopreload;

	if (Hash == 0 || nopreload)
		return;
	mm = (address < TASK_SIZE)? vma->vm_mm: &init_mm;
	pmd = pmd_offset(pgd_offset(mm, address), address);
	if (!pmd_none(*pmd)) {
		ptep = pte_offset(pmd, address);
		add_hash_page(mm->context, address, ptep);
	}
}
