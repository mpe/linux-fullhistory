/* $Id: r6000.c,v 1.6 1998/10/16 19:22:44 ralf Exp $
 *
 * r6000.c: MMU and cache routines for the R6000 processors.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/cacheops.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/sgialib.h>
#include <asm/mmu_context.h>

__asm__(".set mips3"); /* because we know... */

/* Cache operations. XXX Write these dave... */
static inline void r6000_flush_cache_all(void)
{
	/* XXX */
}

static void r6000_flush_cache_mm(struct mm_struct *mm)
{
	/* XXX */
}

static void r6000_flush_cache_range(struct mm_struct *mm,
				    unsigned long start,
				    unsigned long end)
{
	/* XXX */
}

static void r6000_flush_cache_page(struct vm_area_struct *vma,
				   unsigned long page)
{
	/* XXX */
}

static void r6000_flush_page_to_ram(unsigned long page)
{
	/* XXX */
}

static void r6000_flush_cache_sigtramp(unsigned long page)
{
	/* XXX */
}

/* TLB operations. XXX Write these dave... */
static inline void r6000_flush_tlb_all(void)
{
	/* XXX */
}

static void r6000_flush_tlb_mm(struct mm_struct *mm)
{
	/* XXX */
}

static void r6000_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				  unsigned long end)
{
	/* XXX */
}

static void r6000_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	/* XXX */
}

static void r6000_load_pgd(unsigned long pg_dir)
{
}

static void r6000_pgd_init(unsigned long page)
{
	unsigned long dummy1, dummy2;

	/*
	 * This version is optimized for the R6000.  We generate dirty lines
	 * in the datacache, overwrite these lines with zeros and then flush
	 * the cache.  Sounds horribly complicated but is just a trick to
	 * avoid unnecessary loads of from memory and uncached stores which
	 * are very expensive.  Not tested yet as the R6000 is a rare CPU only
	 * available in SGI machines and I don't have one.
	 */
	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\t"
		"cache\t%5,(%0)\n\t"
		"sw\t%2,(%0)\n\t"
		"sw\t%2,4(%0)\n\t"
		"sw\t%2,8(%0)\n\t"
		"sw\t%2,12(%0)\n\t"
		"cache\t%5,16(%0)\n\t"
		"sw\t%2,16(%0)\n\t"
		"sw\t%2,20(%0)\n\t"
		"sw\t%2,24(%0)\n\t"
		"sw\t%2,28(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,32\n\t"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2)
		:"r" ((unsigned long) invalid_pte_table),
		 "0" (page),
		 "1" (USER_PTRS_PER_PGD/8),
		 "i" (Create_Dirty_Excl_D));
}

static void r6000_update_mmu_cache(struct vm_area_struct * vma,
				   unsigned long address, pte_t pte)
{
	r6000_flush_tlb_page(vma, address);
	/*
	 * FIXME: We should also reload a new entry into the TLB to
	 * avoid unnecessary exceptions.
	 */
}

static void r6000_show_regs(struct pt_regs * regs)
{
	/*
	 * Saved main processor registers
	 */
	printk("$0 : %08x %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       0, (unsigned long) regs->regs[1], (unsigned long) regs->regs[2],
	       (unsigned long) regs->regs[3], (unsigned long) regs->regs[4],
	       (unsigned long) regs->regs[5], (unsigned long) regs->regs[6],
	       (unsigned long) regs->regs[7]);
	printk("$8 : %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       (unsigned long) regs->regs[8], (unsigned long) regs->regs[9],
	       (unsigned long) regs->regs[10], (unsigned long) regs->regs[11],
               (unsigned long) regs->regs[12], (unsigned long) regs->regs[13],
	       (unsigned long) regs->regs[14], (unsigned long) regs->regs[15]);
	printk("$16: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       (unsigned long) regs->regs[16], (unsigned long) regs->regs[17],
	       (unsigned long) regs->regs[18], (unsigned long) regs->regs[19],
               (unsigned long) regs->regs[20], (unsigned long) regs->regs[21],
	       (unsigned long) regs->regs[22], (unsigned long) regs->regs[23]);
	printk("$24: %08lx %08lx                   %08lx %08lx %08lx %08lx\n",
	       (unsigned long) regs->regs[24], (unsigned long) regs->regs[25],
	       (unsigned long) regs->regs[28], (unsigned long) regs->regs[29],
               (unsigned long) regs->regs[30], (unsigned long) regs->regs[31]);

	/*
	 * Saved cp0 registers
	 */
	printk("epc  : %08lx\nStatus: %08x\nCause : %08x\n",
	       (unsigned long) regs->cp0_epc, (unsigned int) regs->cp0_status,
	       (unsigned int) regs->cp0_cause);
}

static void r6000_add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
				  unsigned long entryhi, unsigned long pagemask)
{
        /* XXX */
}

static int r6000_user_mode(struct pt_regs *regs)
{
	return !(regs->cp0_status & 0x4);
}

__initfunc(void ld_mmu_r6000(void))
{
	flush_cache_all = r6000_flush_cache_all;
	flush_cache_mm = r6000_flush_cache_mm;
	flush_cache_range = r6000_flush_cache_range;
	flush_cache_page = r6000_flush_cache_page;
	flush_cache_sigtramp = r6000_flush_cache_sigtramp;
	flush_page_to_ram = r6000_flush_page_to_ram;

	flush_tlb_all = r6000_flush_tlb_all;
	flush_tlb_mm = r6000_flush_tlb_mm;
	flush_tlb_range = r6000_flush_tlb_range;
	flush_tlb_page = r6000_flush_tlb_page;
	r6000_asid_setup();

	load_pgd = r6000_load_pgd;
	pgd_init = r6000_pgd_init;
	update_mmu_cache = r6000_update_mmu_cache;

	show_regs = r6000_show_regs;
    
        add_wired_entry = r6000_add_wired_entry;

	user_mode = r6000_user_mode;

	flush_cache_all();
	flush_tlb_all();
}
