/* $Id: r2300.c,v 1.7 1998/10/16 19:22:43 ralf Exp $
 *
 * r2300.c: R2000 and R3000 specific mmu/cache code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/sgialib.h>
#include <asm/mmu_context.h>

extern unsigned long mips_tlb_entries;

/* page functions */
void r2300_clear_page(unsigned long page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"addiu\t$1,%0,%2\n"
		"1:\tsw\t$0,(%0)\n\t"
		"sw\t$0,4(%0)\n\t"
		"sw\t$0,8(%0)\n\t"
		"sw\t$0,12(%0)\n\t"
		"addiu\t%0,32\n\t"
		"sw\t$0,-16(%0)\n\t"
		"sw\t$0,-12(%0)\n\t"
		"sw\t$0,-8(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sw\t$0,-4(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (page)
		:"0" (page),
		 "I" (PAGE_SIZE)
		:"$1","memory");
}

static void r2300_copy_page(unsigned long to, unsigned long from)
{
	unsigned long dummy1, dummy2;
	unsigned long reg1, reg2, reg3, reg4;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"addiu\t$1,%0,%8\n"
		"1:\tlw\t%2,(%1)\n\t"
		"lw\t%3,4(%1)\n\t"
		"lw\t%4,8(%1)\n\t"
		"lw\t%5,12(%1)\n\t"
		"sw\t%2,(%0)\n\t"
		"sw\t%3,4(%0)\n\t"
		"sw\t%4,8(%0)\n\t"
		"sw\t%5,12(%0)\n\t"
		"lw\t%2,16(%1)\n\t"
		"lw\t%3,20(%1)\n\t"
		"lw\t%4,24(%1)\n\t"
		"lw\t%5,28(%1)\n\t"
		"sw\t%2,16(%0)\n\t"
		"sw\t%3,20(%0)\n\t"
		"sw\t%4,24(%0)\n\t"
		"sw\t%5,28(%0)\n\t"
		"addiu\t%0,64\n\t"
		"addiu\t%1,64\n\t"
		"lw\t%2,-32(%1)\n\t"
		"lw\t%3,-28(%1)\n\t"
		"lw\t%4,-24(%1)\n\t"
		"lw\t%5,-20(%1)\n\t"
		"sw\t%2,-32(%0)\n\t"
		"sw\t%3,-28(%0)\n\t"
		"sw\t%4,-24(%0)\n\t"
		"sw\t%5,-20(%0)\n\t"
		"lw\t%2,-16(%1)\n\t"
		"lw\t%3,-12(%1)\n\t"
		"lw\t%4,-8(%1)\n\t"
		"lw\t%5,-4(%1)\n\t"
		"sw\t%2,-16(%0)\n\t"
		"sw\t%3,-12(%0)\n\t"
		"sw\t%4,-8(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sw\t%5,-4(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2),
		 "=&r" (reg1), "=&r" (reg2), "=&r" (reg3), "=&r" (reg4)
		:"0" (to), "1" (from),
		 "I" (PAGE_SIZE));
}

/* Cache operations. */
static inline void r2300_flush_cache_all(void) { }
static void r2300_flush_cache_mm(struct mm_struct *mm) { }
static void r2300_flush_cache_range(struct mm_struct *mm,
				    unsigned long start,
				    unsigned long end)
{
}

static void r2300_flush_cache_page(struct vm_area_struct *vma,
				   unsigned long page)
{
}

static void r2300_flush_page_to_ram(unsigned long page)
{
	/* XXX What we want to do here is perform a displacement
	 * XXX flush because there are circumstances where you do
	 * XXX indeed want to remove stale data from the cache.
	 * XXX (DMA operations for example, where the cache cannot
	 * XXX  "see" this data get changed.)
	 */
}

static void r2300_flush_cache_sigtramp(unsigned long page)
{
}

/* TLB operations. */
static inline void r2300_flush_tlb_all(void)
{
	unsigned long flags;
	int entry;

	save_and_cli(flags);
	write_32bit_cp0_register(CP0_ENTRYLO0, 0);
	for(entry = 0; entry < mips_tlb_entries; entry++) {
		write_32bit_cp0_register(CP0_INDEX, entry);
		write_32bit_cp0_register(CP0_ENTRYHI, ((entry | 0x8) << 12));
		__asm__ __volatile__("tlbwi");
	}
	restore_flags(flags);
}

static void r2300_flush_tlb_mm(struct mm_struct *mm)
{
	if(mm == current->mm)
		r2300_flush_tlb_all();
}

static void r2300_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				  unsigned long end)
{
	if(mm == current->mm)
		r2300_flush_tlb_all();
}

static void r2300_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if(vma->vm_mm == current->mm)
		r2300_flush_tlb_all();
}

/* Load a new root pointer into the TLB. */
static void r2300_load_pgd(unsigned long pg_dir)
{
	unsigned long flags;

	save_and_cli(flags);
	write_32bit_cp0_register(CP0_ENTRYHI, TLB_ROOT);
	write_32bit_cp0_register(CP0_INDEX, 0);
	write_32bit_cp0_register(CP0_ENTRYLO0, ((pg_dir >> 6) | 0x00e0));
	__asm__ __volatile__("tlbwi");
	restore_flags(flags);
}

/*
 * Initialize new page directory with pointers to invalid ptes
 */
static void r2300_pgd_init(unsigned long page)
{
	unsigned long dummy1, dummy2;

	/*
	 * The plain and boring version for the R3000.  No cache flushing
	 * stuff is implemented since the R3000 has physical caches.
	 */
	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\tsw\t%2,(%0)\n\t"
		"sw\t%2,4(%0)\n\t"
		"sw\t%2,8(%0)\n\t"
		"sw\t%2,12(%0)\n\t"
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
		 "1" (USER_PTRS_PER_PGD/8));
}

static void r2300_update_mmu_cache(struct vm_area_struct * vma,
				   unsigned long address, pte_t pte)
{
	r2300_flush_tlb_page(vma, address);
	/*
	 * FIXME: We should also reload a new entry into the TLB to
	 * avoid unnecessary exceptions.
	 */
}

static void r2300_show_regs(struct pt_regs * regs)
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

static void r2300_add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
				  unsigned long entryhi, unsigned long pagemask)
{
        /*
	 * FIXME, to be done
	 */
}

static int r2300_user_mode(struct pt_regs *regs)
{
	return !(regs->cp0_status & 0x4);
}

__initfunc(void ld_mmu_r2300(void))
{
	clear_page = r2300_clear_page;
	copy_page = r2300_copy_page;

	flush_cache_all = r2300_flush_cache_all;
	flush_cache_mm = r2300_flush_cache_mm;
	flush_cache_range = r2300_flush_cache_range;
	flush_cache_page = r2300_flush_cache_page;
	flush_cache_sigtramp = r2300_flush_cache_sigtramp;
	flush_page_to_ram = r2300_flush_page_to_ram;

	flush_tlb_all = r2300_flush_tlb_all;
	flush_tlb_mm = r2300_flush_tlb_mm;
	flush_tlb_range = r2300_flush_tlb_range;
	flush_tlb_page = r2300_flush_tlb_page;
	r3000_asid_setup();

	load_pgd = r2300_load_pgd;
	pgd_init = r2300_pgd_init;
	update_mmu_cache = r2300_update_mmu_cache;

	show_regs = r2300_show_regs;
    
        add_wired_entry = r2300_add_wired_entry;

	user_mode = r2300_user_mode;
	flush_tlb_all();
}
