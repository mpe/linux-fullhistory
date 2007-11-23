/* $Id: andes.c,v 1.7 2000/03/13 22:43:25 kanoj Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998, 1999 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/r10kcache.h>
#include <asm/system.h>
#include <asm/sgialib.h>
#include <asm/mmu_context.h>

/* CP0 hazard avoidance.  I think we can drop this for the R10000.  */
#define BARRIER __asm__ __volatile__(".set noreorder\n\t" \
				     "nop; nop; nop; nop; nop; nop;\n\t" \
				     ".set reorder\n\t")

/* R10000 has no Create_Dirty type cacheops.  */
static void andes_clear_page(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tsd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (page)
		:"0" (page), "I" (PAGE_SIZE)
		:"$1", "memory");
}

static void andes_copy_page(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%6\n"
		"1:\tld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"ld\t%2,16(%1)\n\t"
		"ld\t%3,24(%1)\n\t"
		"sd\t%2,16(%0)\n\t"
		"sd\t%3,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"ld\t%2,-16(%1)\n\t"
		"ld\t%3,-8(%1)\n\t"
		"sd\t%2,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%3,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2)
		:"0" (to), "1" (from), "I" (PAGE_SIZE));
}

/* Cache operations.  These are only used with the virtual memory system,
   not for non-coherent I/O so it's ok to ignore the secondary caches.  */
static void
andes_flush_cache_all(void)
{
	blast_dcache32(); blast_icache64();
}

static void
andes_flush_cache_mm(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		andes_flush_cache_all();
	}
}

static void
andes_flush_cache_range(struct mm_struct *mm, unsigned long start,
                        unsigned long end)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
		unsigned long flags;

#ifdef DEBUG_CACHE
		printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
		save_and_cli(flags);
		blast_dcache32(); blast_icache64();
		restore_flags(flags);
	}
}

static void
andes_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int text;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_PRESENT))
		goto out;

	text = (vma->vm_flags & VM_EXEC);
	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if ((mm == current->mm) && (pte_val(*ptep) & _PAGE_VALID)) {
		blast_dcache32_page(page);
		if(text)
			blast_icache64_page(page);
	} else {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (CKSEG0 + (page & (dcache_size - 1)));
		blast_dcache32_page_indexed(page);
		if(text)
			blast_icache64_page_indexed(page);
	}
out:
	restore_flags(flags);
}

/* Hoo hum...  will this ever be called for an address that is not in CKSEG0
   and not cacheable?  */
static void
andes_flush_page_to_ram(struct page * page)
{
	unsigned long addr = page_address(page) & PAGE_MASK;

	if ((addr >= K0BASE_NONCOH && addr < (0xb0UL << 56))
	    || (addr >= KSEG0 && addr < KSEG1)
	    || (addr >= KSEG2)) {
#ifdef DEBUG_CACHE
		printk("cram[%08lx]", addr);
#endif
		blast_dcache32_page(addr);
	}
}

static void
andes_flush_cache_sigtramp(unsigned long addr)
{
	unsigned long daddr, iaddr;

	daddr = addr & ~(dc_lsize - 1);
	protected_writeback_dcache_line(daddr);
	protected_writeback_dcache_line(daddr + dc_lsize);
	iaddr = addr & ~(ic_lsize - 1);
	protected_flush_icache_line(iaddr);
	protected_flush_icache_line(iaddr + ic_lsize);
}

#define NTLB_ENTRIES       64
#define NTLB_ENTRIES_HALF  32

/* TLB operations.
   XXX These should work fine on R10k without the BARRIERs.  */
static inline void
andes_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	unsigned long entry;

#ifdef DEBUG_TLB
	printk("[tlball]");
#endif

	__save_and_cli(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = get_entryhi() & 0xff;
	set_entryhi(CKSEG0);
	set_entrylo0(0);
	set_entrylo1(0);
	BARRIER;

	entry = get_wired();

	/* Blast 'em all away. */
	while(entry < NTLB_ENTRIES) {
		set_index(entry);
		BARRIER;
		tlb_write_indexed();
		BARRIER;
		entry++;
	}
	BARRIER;
	set_entryhi(old_ctx);
	__restore_flags(flags);
}

static void andes_flush_tlb_mm(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
		unsigned long flags;

#ifdef DEBUG_TLB
		printk("[tlbmm<%d>]", mm->context);
#endif
		__save_and_cli(flags);
		get_new_cpu_mmu_context(mm, smp_processor_id());
		if(mm == current->mm)
			set_entryhi(CPU_CONTEXT(smp_processor_id(), mm) & 0xff);
		__restore_flags(flags);
	}
}

static void
andes_flush_tlb_range(struct mm_struct *mm, unsigned long start,
                      unsigned long end)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
		unsigned long flags;
		int size;

#ifdef DEBUG_TLB
		printk("[tlbrange<%02x,%08lx,%08lx>]", (mm->context & 0xff),
		       start, end);
#endif
		__save_and_cli(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		size = (size + 1) >> 1;
		if(size <= NTLB_ENTRIES_HALF) {
			int oldpid = (get_entryhi() & 0xff);
			int newpid = (CPU_CONTEXT(smp_processor_id(), mm) & 0xff);

			start &= (PAGE_MASK << 1);
			end += ((PAGE_SIZE << 1) - 1);
			end &= (PAGE_MASK << 1);
			while(start < end) {
				int idx;

				set_entryhi(start | newpid);
				start += (PAGE_SIZE << 1);
				BARRIER;
				tlb_probe();
				BARRIER;
				idx = get_index();
				set_entrylo0(0);
				set_entrylo1(0);
				set_entryhi(KSEG0);
				BARRIER;
				if(idx < 0)
					continue;
				tlb_write_indexed();
				BARRIER;
			}
			set_entryhi(oldpid);
		} else {
			get_new_cpu_mmu_context(mm, smp_processor_id());
			if(mm == current->mm)
				set_entryhi(CPU_CONTEXT(smp_processor_id(), mm) & 
									0xff);
		}
		__restore_flags(flags);
	}
}

static void
andes_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if (CPU_CONTEXT(smp_processor_id(), vma->vm_mm) != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

#ifdef DEBUG_TLB
		printk("[tlbpage<%d,%08lx>]", vma->vm_mm->context, page);
#endif
		newpid = (CPU_CONTEXT(smp_processor_id(), vma->vm_mm) & 0xff);
		page &= (PAGE_MASK << 1);
		__save_and_cli(flags);
		oldpid = (get_entryhi() & 0xff);
		set_entryhi(page | newpid);
		BARRIER;
		tlb_probe();
		BARRIER;
		idx = get_index();
		set_entrylo0(0);
		set_entrylo1(0);
		set_entryhi(KSEG0);
		if(idx < 0)
			goto finish;
		BARRIER;
		tlb_write_indexed();

	finish:
		BARRIER;
		set_entryhi(oldpid);
		__restore_flags(flags);
	}
}

/* XXX Simplify this.  On the R10000 writing a TLB entry for an virtual
   address that already exists will overwrite the old entry and not result
   in TLB malfunction or TLB shutdown.  */
static void andes_update_mmu_cache(struct vm_area_struct * vma,
                                   unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx, pid;

	__save_and_cli(flags);
	pid = get_entryhi() & 0xff;

	if((pid != (CPU_CONTEXT(smp_processor_id(), vma->vm_mm) & 0xff)) ||
	   (CPU_CONTEXT(smp_processor_id(), vma->vm_mm) == 0)) {
		printk("update_mmu_cache: Wheee, bogus tlbpid mmpid=%d 
			tlbpid=%d\n", (int) (CPU_CONTEXT(smp_processor_id(), 
			vma->vm_mm) & 0xff), pid);
	}

	address &= (PAGE_MASK << 1);
	set_entryhi(address | (pid));
	pgdp = pgd_offset(vma->vm_mm, address);
	BARRIER;
	tlb_probe();
	BARRIER;
	pmdp = pmd_offset(pgdp, address);
	idx = get_index();
	ptep = pte_offset(pmdp, address);
	BARRIER;
	set_entrylo0(pte_val(*ptep++) >> 6);
	set_entrylo1(pte_val(*ptep) >> 6);
	set_entryhi(address | (pid));
	BARRIER;
	if(idx < 0) {
		tlb_write_random();
	} else {
		tlb_write_indexed();
	}
	BARRIER;
	set_entryhi(pid);
	BARRIER;
	__restore_flags(flags);
}

static int
andes_user_mode(struct pt_regs *regs)
{
	return (regs->cp0_status & ST0_KSU) == KSU_USER;
}

static void andes_show_regs(struct pt_regs *regs)
{
	printk("Cpu %d\n", smp_processor_id());
	/* Saved main processor registers. */
	printk("$0      : %016lx %016lx %016lx %016lx\n",
	       0UL, regs->regs[1], regs->regs[2], regs->regs[3]);
	printk("$4      : %016lx %016lx %016lx %016lx\n",
               regs->regs[4], regs->regs[5], regs->regs[6], regs->regs[7]);
	printk("$8      : %016lx %016lx %016lx %016lx\n",
	       regs->regs[8], regs->regs[9], regs->regs[10], regs->regs[11]);
	printk("$12     : %016lx %016lx %016lx %016lx\n",
               regs->regs[12], regs->regs[13], regs->regs[14], regs->regs[15]);
	printk("$16     : %016lx %016lx %016lx %016lx\n",
	       regs->regs[16], regs->regs[17], regs->regs[18], regs->regs[19]);
	printk("$20     : %016lx %016lx %016lx %016lx\n",
               regs->regs[20], regs->regs[21], regs->regs[22], regs->regs[23]);
	printk("$24     : %016lx %016lx\n",
	       regs->regs[24], regs->regs[25]);
	printk("$28     : %016lx %016lx %016lx %016lx\n",
	       regs->regs[28], regs->regs[29], regs->regs[30], regs->regs[31]);
	printk("Hi      : %016lx\n", regs->hi);
	printk("Lo      : %016lx\n", regs->lo);

	/* Saved cp0 registers. */
	printk("epc     : %016lx\nbadvaddr: %016lx\n",
	       regs->cp0_epc, regs->cp0_badvaddr);
	printk("Status  : %08x\nCause   : %08x\n",
	       (unsigned int) regs->cp0_status, (unsigned int) regs->cp0_cause);
}

void __init ld_mmu_andes(void)
{
	printk("CPU revision is: %08x\n", read_32bit_cp0_register(CP0_PRID));

	printk("Primary instruction cache %dkb, linesize %d bytes\n",
	       icache_size >> 10, ic_lsize);
	printk("Primary data cache %dkb, linesize %d bytes\n",
	       dcache_size >> 10, dc_lsize);
	printk("Secondary cache sized at %ldK, linesize %ld\n",
	       scache_size() >> 10, sc_lsize());

	_clear_page = andes_clear_page;
	_copy_page = andes_copy_page;

	_flush_cache_all = andes_flush_cache_all;
	_flush_cache_mm = andes_flush_cache_mm;
	_flush_cache_range = andes_flush_cache_range;
	_flush_cache_page = andes_flush_cache_page;
	_flush_cache_sigtramp = andes_flush_cache_sigtramp;
	_flush_page_to_ram = andes_flush_page_to_ram;

	_flush_tlb_all = andes_flush_tlb_all;
	_flush_tlb_mm = andes_flush_tlb_mm;
	_flush_tlb_range = andes_flush_tlb_range;
	_flush_tlb_page = andes_flush_tlb_page;
    
	update_mmu_cache = andes_update_mmu_cache;

	_show_regs = andes_show_regs;
	_user_mode = andes_user_mode;

        flush_cache_all();
        write_32bit_cp0_register(CP0_WIRED, 0);

	/*
	 * You should never change this register:
	 *   - On R4600 1.7 the tlbp never hits for pages smaller than
	 *     the value in the c0_pagemask register.
	 *   - The entire mm handling assumes the c0_pagemask register to
	 *     be set for 4kb pages.
	 */
	write_32bit_cp0_register(CP0_PAGEMASK, PM_4K);

        /* From this point on the ARC firmware is dead.  */
	_flush_tlb_all();

        /* Did I tell you that ARC SUCKS?  */
}
