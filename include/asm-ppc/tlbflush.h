/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 * include/asm-ppc/tlbflush.h
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifdef __KERNEL__
#ifndef _PPC_TLBFLUSH_H
#define _PPC_TLBFLUSH_H

#include <linux/config.h>
#include <linux/mm.h>
#include <asm/processor.h>

extern void _tlbie(unsigned long address);
extern void _tlbia(void);

#if defined(CONFIG_4xx)
#define __tlbia()	asm volatile ("tlbia; sync" : : : "memory")

static inline void flush_tlb_all(void)
	{ __tlbia(); }
static inline void flush_tlb_mm(struct mm_struct *mm)
	{ __tlbia(); }
static inline void flush_tlb_page(struct vm_area_struct *vma,
				unsigned long vmaddr)
	{ _tlbie(vmaddr); }
static inline void flush_tlb_range(struct mm_struct *mm,
				unsigned long start, unsigned long end)
	{ __tlbia(); }
static inline void flush_tlb_kernel_range(unsigned long start,
				unsigned long end)
	{ __tlbia(); }
#define update_mmu_cache(vma, addr, pte)	do { } while (0)

#elif defined(CONFIG_8xx)
#define __tlbia()	asm volatile ("tlbia; sync" : : : "memory")

static inline void flush_tlb_all(void)
	{ __tlbia(); }
static inline void flush_tlb_mm(struct mm_struct *mm)
	{ __tlbia(); }
static inline void flush_tlb_page(struct vm_area_struct *vma,
				unsigned long vmaddr)
	{ _tlbie(vmaddr); }
static inline void flush_tlb_range(struct mm_struct *mm,
				unsigned long start, unsigned long end)
	{ __tlbia(); }
static inline void flush_tlb_kernel_range(unsigned long start,
				unsigned long end)
	{ __tlbia(); }
#define update_mmu_cache(vma, addr, pte)	do { } while (0)

#else	/* 6xx, 7xx, 7xxx cpus */
struct mm_struct;
struct vm_area_struct;
extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);

/*
 * This gets called at the end of handling a page fault, when
 * the kernel has put a new PTE into the page table for the process.
 * We use it to put a corresponding HPTE into the hash table
 * ahead of time, instead of waiting for the inevitable extra
 * hash-table miss exception.
 */
extern void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t);
#endif

/*
 * This is called in munmap when we have freed up some page-table
 * pages.  We don't need to do anything here, there's nothing special
 * about our page-table pages.  -- paulus
 */
static inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
}

#endif _PPC_TLBFLUSH_H
#endif __KERNEL__
