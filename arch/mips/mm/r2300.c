/*
 * r2300.c: R2000 and R3000 specific mmu/cache code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * with a lot of changes to make this thing work for R3000s
 * Copyright (C) 1998 Harald Koerfgen
 * Copyright (C) 1998 Gleb Raiko & Vladimir Roganov
 *
 * $Id: r2300.c,v 1.8 1999/04/11 17:13:56 harald Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/system.h>
#include <asm/sgialib.h>
#include <asm/mipsregs.h>
#include <asm/io.h>
/*
 * Temporarily disabled
 *
#include <asm/wbflush.h>
 */

/*
 * According to the paper written by D. Miller about Linux cache & TLB
 * flush implementation, DMA/Driver coherence should be done at the 
 * driver layer.  Thus, normally, we don't need flush dcache for R3000.
 * Define this if driver does not handle cache consistency during DMA ops.
 */
#undef DO_DCACHE_FLUSH

/*
 *  Unified cache space description structure
 */
static struct cache_space {
	unsigned long ca_flags;  /* Cache space access flags */
	int           size;      /* Cache space size */
} icache, dcache;

#undef DEBUG_TLB
#undef DEBUG_CACHE

extern unsigned long mips_tlb_entries;

#define NTLB_ENTRIES       64  /* Fixed on all R23000 variants... */

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

__initfunc(static unsigned long size_cache(unsigned long ca_flags))
{
	unsigned long flags, status, dummy, size;
	volatile unsigned long *p;

	p = (volatile unsigned long *) KSEG0;

	save_and_cli(flags);

	/* isolate cache space */
	write_32bit_cp0_register(CP0_STATUS, (ca_flags|flags)&~ST0_IEC);

	*p = 0xa5a55a5a;
	dummy = *p;
	status = read_32bit_cp0_register(CP0_STATUS);

	if (dummy != 0xa5a55a5a || (status & (1<<19))) {
		size = 0;
	} else {
		for (size = 512; size <= 0x40000; size <<= 1)
			*(p + size) = 0;
		*p = -1;
		for (size = 512; 
		     (size <= 0x40000) && (*(p + size) == 0); 
		     size <<= 1)
			;
		if (size > 0x40000)
			size = 0;
	}
	restore_flags(flags);

	return size * sizeof(*p);
}

__initfunc(static void probe_dcache(void))
{
	dcache.size = size_cache(dcache.ca_flags = ST0_DE);
	printk("Data cache %dkb\n", dcache.size >> 10);
}

__initfunc(static void probe_icache(void))
{
	icache.size = size_cache(icache.ca_flags = ST0_DE|ST0_CE);
	printk("Instruction cache %dkb\n", icache.size >> 10);
}

static inline unsigned long get_phys_page (unsigned long page,
					   struct mm_struct *mm)
{
        page &= PAGE_MASK;
	if (page >= KSEG0 && page < KSEG1) {
		/*
		 *  We already have physical address
		 */
		return page;
	} else {
		if (!mm) {
			printk ("get_phys_page: vaddr without mm\n");
			return 0;
		} else {
			/* 
			 *  Find a physical page using mm_struct
			 */
			pgd_t *page_dir;
			pmd_t *page_middle;
			pte_t *page_table, pte;

			unsigned long address = page;

			page_dir = pgd_offset(mm, address);
			if (pgd_none(*page_dir))
				return 0; 
			page_middle = pmd_offset(page_dir, address);
			if (pmd_none(*page_middle))
				return 0; 
			page_table = pte_offset(page_middle, address);
			pte = *page_table;
			if (!pte_present(pte))
				return 0; 
			return pte_page(pte);
	}
	}
}

static inline void flush_cache_space_page(struct cache_space *space,
					  unsigned long page)
{
	register unsigned long i, flags, size = space->size;
	register volatile unsigned char *p = (volatile unsigned char*) page;

#ifndef DO_DCACHE_FLUSH
	if (space == &dcache)
		return;
#endif
	if (size > PAGE_SIZE)
		size = PAGE_SIZE;

	save_and_cli(flags);

	/* isolate cache space */
	write_32bit_cp0_register(CP0_STATUS, (space->ca_flags|flags)&~ST0_IEC);

	for (i = 0; i < size; i += 64) {
		asm ( 	"sb\t$0,(%0)\n\t"
			"sb\t$0,4(%0)\n\t"
			"sb\t$0,8(%0)\n\t"
			"sb\t$0,12(%0)\n\t"
			"sb\t$0,16(%0)\n\t"
			"sb\t$0,20(%0)\n\t"
			"sb\t$0,24(%0)\n\t"
			"sb\t$0,28(%0)\n\t"
 		        "sb\t$0,32(%0)\n\t"
 			"sb\t$0,36(%0)\n\t"
 			"sb\t$0,40(%0)\n\t"
 			"sb\t$0,44(%0)\n\t"
 			"sb\t$0,48(%0)\n\t"
 			"sb\t$0,52(%0)\n\t"
 			"sb\t$0,56(%0)\n\t"
 			"sb\t$0,60(%0)\n\t"
			: : "r" (p) );
		p += 64;
	}

	restore_flags(flags);
}

static inline void flush_cache_space_all(struct cache_space *space)
{
	unsigned long page = KSEG0;
	int size = space->size;

#ifndef DO_DCACHE_FLUSH
	if (space == &dcache)
		return;
#endif
	while(size > 0) { 
		flush_cache_space_page(space, page);
		page += PAGE_SIZE; size -= PAGE_SIZE;
        }
}

static inline void r2300_flush_cache_all(void)
{
	flush_cache_space_all(&dcache);
	flush_cache_space_all(&icache);
}
 
static void r2300_flush_cache_mm(struct mm_struct *mm)
{
	if(mm->context == 0) 
		return;
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
	/*
	 *  This function is called not offen, so it looks
	 *  enough good to flush all caches than scan mm_struct,
	 *  count pages to flush (and, very probably, flush more
	 *  than cache space size :-)
 */
	flush_cache_all();
}

static void r2300_flush_cache_range(struct mm_struct *mm,
				    unsigned long start,
				    unsigned long end)
{
	/*
	 *  In general, we need to flush both i- & d- caches here.
	 *  Optimization: if cache space is less than given range,
	 *  it is more quickly to flush all cache than all pages in range.
	 */

	unsigned long page;
	int icache_done = 0, dcache_done = 0;

	if(mm->context == 0) 
		return;
#ifdef DEBUG_CACHE
	printk("crange[%d]", (int)mm->context);
#endif
	if (end - start >= icache.size) {
		flush_cache_space_all(&icache);
		icache_done = 1;
	}
	if (end - start >= dcache.size) {
		flush_cache_space_all(&dcache);
		dcache_done = 1;
	}
	if (icache_done && dcache_done)
		return;

	for (page = start; page < end; page += PAGE_SIZE) {
		unsigned long phys_page = get_phys_page(page, mm);
		
		if (phys_page) {
			if (!icache_done) 
				flush_cache_space_page(&icache, phys_page);
			if (!dcache_done) 
				flush_cache_space_page(&dcache, phys_page);
		}
	}
}

static void r2300_flush_cache_page(struct vm_area_struct *vma,
				   unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if(mm->context == 0)
		return;
#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	/*
	 *  User changes page, so we need to check:
         *     is icache page flush needed ?
	 *  It looks we don't need to flush dcache,
	 *  due it is write-transparent on R3000
	 */
	if (vma->vm_flags & VM_EXEC) {
		unsigned long phys_page = get_phys_page(page, vma->vm_mm);
		if (phys_page)
			flush_cache_space_page(&icache, phys_page); 
	}
}

static void r2300_flush_page_to_ram(unsigned long page)
{
	/*
	 *  We need to flush both i- & d- caches :-(
	 */
	unsigned long phys_page = get_phys_page(page, NULL);
#ifdef DEBUG_CACHE
	printk("cram[%08lx]", page);
#endif
	if (phys_page) {
		flush_cache_space_page(&icache, phys_page);
		flush_cache_space_page(&dcache, phys_page);
	}
}

static void r3k_dma_cache_wback_inv(unsigned long start, unsigned long size)
{
	register unsigned long i, flags;
	register volatile unsigned char *p = (volatile unsigned char*) start;

/*
 * Temporarily disabled
	wbflush();
	 */

	/*
	 * Invalidate dcache
	 */
	if (size < 64)
		size = 64;

	if (size > dcache.size)
		size = dcache.size;

	save_and_cli(flags);

	/* isolate cache space */
	write_32bit_cp0_register(CP0_STATUS, (ST0_DE|flags)&~ST0_IEC);

	for (i = 0; i < size; i += 64) {
		asm ( 	"sb\t$0,(%0)\n\t"
			"sb\t$0,4(%0)\n\t"
			"sb\t$0,8(%0)\n\t"
			"sb\t$0,12(%0)\n\t"
			"sb\t$0,16(%0)\n\t"
			"sb\t$0,20(%0)\n\t"
			"sb\t$0,24(%0)\n\t"
			"sb\t$0,28(%0)\n\t"
 		        "sb\t$0,32(%0)\n\t"
 			"sb\t$0,36(%0)\n\t"
 			"sb\t$0,40(%0)\n\t"
 			"sb\t$0,44(%0)\n\t"
 			"sb\t$0,48(%0)\n\t"
 			"sb\t$0,52(%0)\n\t"
 			"sb\t$0,56(%0)\n\t"
 			"sb\t$0,60(%0)\n\t"
			: : "r" (p) );
		p += 64;
	}

	restore_flags(flags);
}

static void r2300_flush_cache_sigtramp(unsigned long page)
{
	/*
	 *  We need only flush i-cache here
	 *
	 *  This function receives virtual address (from signal.c),
	 *  but this moment we have needed mm_struct in 'current'
	 */
	unsigned long phys_page = get_phys_page(page, current->mm);
#ifdef DEBUG_CACHE
		printk("csigtramp[%08lx]", page);
#endif
	if (phys_page)
		flush_cache_space_page(&icache, phys_page);  
}

/* TLB operations. */
static inline void r2300_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

#ifdef DEBUG_TLB
	printk("[tlball]");
#endif

	save_and_cli(flags);
	old_ctx = (get_entryhi() & 0xfc0);
	write_32bit_cp0_register(CP0_ENTRYLO0, 0);
	for(entry = 0; entry < NTLB_ENTRIES; entry++) {
		write_32bit_cp0_register(CP0_INDEX, entry << 8);
		write_32bit_cp0_register(CP0_ENTRYHI, ((entry | 0x80000) << 12));
		__asm__ __volatile__("tlbwi");
	}
	set_entryhi(old_ctx);
	restore_flags(flags);
}

static void r2300_flush_tlb_mm(struct mm_struct *mm)
{
	if(mm->context != 0) {
		unsigned long flags;

#ifdef DEBUG_TLB
		printk("[tlbmm<%d>]", mm->context);
#endif
		save_and_cli(flags);
		get_new_mmu_context(mm, asid_cache);
	if(mm == current->mm)
			set_entryhi(mm->context & 0xfc0);
		restore_flags(flags);
	}
}

static void r2300_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				  unsigned long end)
{
	if(mm->context != 0) {
		unsigned long flags;
		int size;

#ifdef DEBUG_TLB
		printk("[tlbrange<%02x,%08lx,%08lx>]", (mm->context & 0xfc0),
		       start, end);
#endif
		save_and_cli(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		if(size <= NTLB_ENTRIES) {
			int oldpid = (get_entryhi() & 0xfc0);
			int newpid = (mm->context & 0xfc0);

			start &= PAGE_MASK;
			end += (PAGE_SIZE - 1);
			end &= PAGE_MASK;
			while(start < end) {
				int idx;

				set_entryhi(start | newpid);
				start += PAGE_SIZE;
				tlb_probe();
				idx = get_index();
				set_entrylo0(0);
				set_entryhi(KSEG0);
				if(idx < 0)
					continue;
				tlb_write_indexed();
			}
			set_entryhi(oldpid);
		} else {
			get_new_mmu_context(mm, asid_cache);
	if(mm == current->mm)
				set_entryhi(mm->context & 0xfc0);
		}
		restore_flags(flags);
	}
}

static void r2300_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if(vma->vm_mm->context != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

#ifdef DEBUG_TLB
		printk("[tlbpage<%d,%08lx>]", vma->vm_mm->context, page);
#endif
		newpid = (vma->vm_mm->context & 0xfc0);
		page &= PAGE_MASK;
		save_and_cli(flags);
		oldpid = (get_entryhi() & 0xfc0);
		set_entryhi(page | newpid);
		tlb_probe();
		idx = get_index();
		set_entrylo0(0);
		set_entryhi(KSEG0);
		if(idx < 0)
			goto finish;
		tlb_write_indexed();

finish:
		set_entryhi(oldpid);
		restore_flags(flags);
	}
}

/* Load a new root pointer into the TLB. */
static void r2300_load_pgd(unsigned long pg_dir)
{
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
		 "1" (PAGE_SIZE/(sizeof(pmd_t)*8)));
}

static void r2300_update_mmu_cache(struct vm_area_struct * vma,
				   unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx, pid;

	pid = (get_entryhi() & 0xfc0);

#ifdef DEBUG_TLB
	if((pid != (vma->vm_mm->context & 0xfc0)) || (vma->vm_mm->context == 0)) {
		printk("update_mmu_cache: Wheee, bogus tlbpid mmpid=%d tlbpid=%d\n",
		       (int) (vma->vm_mm->context & 0xfc0), pid);
	}
#endif

	save_and_cli(flags);
	address &= PAGE_MASK;
	set_entryhi(address | (pid));
	pgdp = pgd_offset(vma->vm_mm, address);
	tlb_probe();
	pmdp = pmd_offset(pgdp, address);
	idx = get_index();
	ptep = pte_offset(pmdp, address);
	set_entrylo0(pte_val(*ptep));
	set_entryhi(address | (pid));
	if(idx < 0) {
		tlb_write_random();
#if 0
		printk("[MISS]");
#endif
	} else {
		tlb_write_indexed();
#if 0
		printk("[HIT]");
#endif
	}
#if 0
	if(!strcmp(current->comm, "args")) {
		printk("<");
		for(idx = 0; idx < NTLB_ENTRIES; idx++) {
			set_index(idx);
			tlb_read();
			address = get_entryhi();
			if((address & 0xfc0) != 0)
				printk("[%08lx]", address);
		}
		printk(">\n");
	}
#endif
	set_entryhi(pid);
	restore_flags(flags);
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
printk("r2300_add_wired_entry");
        /*
	 * FIXME, to be done
	 */
}

static int r2300_user_mode(struct pt_regs *regs)
{
	return !(regs->cp0_status & ST0_KUP);
}

__initfunc(void ld_mmu_r2300(void))
{
	printk("CPU revision is: %08x\n", read_32bit_cp0_register(CP0_PRID));

	clear_page = r2300_clear_page;
	copy_page = r2300_copy_page;

	probe_icache();
	probe_dcache();

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

        dma_cache_wback_inv = r3k_dma_cache_wback_inv;

	load_pgd = r2300_load_pgd;
	pgd_init = r2300_pgd_init;
	update_mmu_cache = r2300_update_mmu_cache;
	r3000_asid_setup();

	show_regs = r2300_show_regs;
    
        add_wired_entry = r2300_add_wired_entry;

	user_mode = r2300_user_mode;

	flush_tlb_all();
}
