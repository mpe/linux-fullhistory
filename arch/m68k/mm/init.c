/*
 *  linux/arch/m68k/mm/init.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/init.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/machdep.h>
#include <asm/io.h>
#ifdef CONFIG_ATARI
#include <asm/atari_stram.h>
#endif

#undef DEBUG

extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;
	if(pgtable_cache_size > high) {
		do {
			if(pmd_quicklist)
				freed += free_pmd_slow(get_pmd_fast());
			if(pte_quicklist)
				free_pte_slow(get_pte_fast()), freed++;
		} while(pgtable_cache_size > low);
	}
	return freed;
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
static unsigned long empty_bad_page_table;

pte_t *__bad_pagetable(void)
{
    memset((void *)empty_bad_page_table, 0, PAGE_SIZE);
    return (pte_t *)empty_bad_page_table;
}

static unsigned long empty_bad_page;

pte_t __bad_page(void)
{
    memset ((void *)empty_bad_page, 0, PAGE_SIZE);
    return pte_mkdirty(mk_pte(empty_bad_page, PAGE_SHARED));
}

unsigned long empty_zero_page;

void show_mem(void)
{
    unsigned long i;
    int free = 0, total = 0, reserved = 0, nonshared = 0, shared = 0;
    int cached = 0;

    printk("\nMem-info:\n");
    show_free_areas();
    printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
    i = max_mapnr;
    while (i-- > 0) {
	total++;
	if (PageReserved(mem_map+i))
	    reserved++;
	else if (PageSwapCache(mem_map+i))
	    cached++;
	else if (!atomic_read(&mem_map[i].count))
	    free++;
	else if (atomic_read(&mem_map[i].count) == 1)
	    nonshared++;
	else
	    shared += atomic_read(&mem_map[i].count) - 1;
    }
    printk("%d pages of RAM\n",total);
    printk("%d free pages\n",free);
    printk("%d reserved pages\n",reserved);
    printk("%d pages nonshared\n",nonshared);
    printk("%d pages shared\n",shared);
    printk("%d pages swap cached\n",cached);
    printk("%ld pages in page table cache\n",pgtable_cache_size);
    show_buffers();
#ifdef CONFIG_NET
    show_net_buffers();
#endif
}

#ifndef mm_cachebits
/*
 * Bits to add to page descriptors for "normal" caching mode.
 * For 68020/030 this is 0.
 * For 68040, this is _PAGE_CACHE040 (cachable, copyback)
 */
unsigned long mm_cachebits = 0;
#endif

static pte_t *__init kernel_page_table(unsigned long *memavailp)
{
	pte_t *ptablep;

	ptablep = (pte_t *)*memavailp;
	*memavailp += PAGE_SIZE;

	clear_page((unsigned long)ptablep);
	flush_page_to_ram((unsigned long) ptablep);
	flush_tlb_kernel_page((unsigned long) ptablep);
	nocache_page ((unsigned long)ptablep);

	return ptablep;
}

static pmd_t *last_pgtable __initdata = NULL;

static pmd_t *__init kernel_ptr_table(unsigned long *memavailp)
{
	if (!last_pgtable) {
		unsigned long pmd, last;
		int i;

		last = (unsigned long)kernel_pg_dir;
		for (i = 0; i < PTRS_PER_PGD; i++) {
			if (!pgd_val(kernel_pg_dir[i]))
				continue;
			pmd = pgd_page(kernel_pg_dir[i]);
			if (pmd > last)
				last = pmd;
		}

		last_pgtable = (pmd_t *)last;
#ifdef DEBUG
		printk("kernel_ptr_init: %p\n", last_pgtable);
#endif
	}

	if (((unsigned long)(last_pgtable + PTRS_PER_PMD) & ~PAGE_MASK) == 0) {
		last_pgtable = (pmd_t *)*memavailp;
		*memavailp += PAGE_SIZE;

		clear_page((unsigned long)last_pgtable);
		flush_page_to_ram((unsigned long)last_pgtable);
		flush_tlb_kernel_page((unsigned long)last_pgtable);
		nocache_page((unsigned long)last_pgtable);
	} else
		last_pgtable += PTRS_PER_PMD;

	return last_pgtable;
}

static unsigned long __init
map_chunk (unsigned long addr, long size, unsigned long *memavailp)
{
#define PTRTREESIZE (256*1024)
#define ROOTTREESIZE (32*1024*1024)
	static unsigned long virtaddr = 0;
	unsigned long physaddr;
	pgd_t *pgd_dir;
	pmd_t *pmd_dir;
	pte_t *pte_dir;

	physaddr = (addr | m68k_supervisor_cachemode |
		    _PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_DIRTY);
	if (CPU_IS_040_OR_060)
		physaddr |= _PAGE_GLOBAL040;

	while (size > 0) {
#ifdef DEBUG
		if (!(virtaddr & (PTRTREESIZE-1)))
			printk ("\npa=%#lx va=%#lx ", physaddr & PAGE_MASK,
				virtaddr);
#endif
		pgd_dir = pgd_offset_k(virtaddr);
		if (virtaddr && CPU_IS_020_OR_030) {
			if (!(virtaddr & (ROOTTREESIZE-1)) &&
			    size >= ROOTTREESIZE) {
#ifdef DEBUG
				printk ("[very early term]");
#endif
				pgd_val(*pgd_dir) = physaddr;
				size -= ROOTTREESIZE;
				virtaddr += ROOTTREESIZE;
				physaddr += ROOTTREESIZE;
				continue;
			}
		}
		if (!pgd_present(*pgd_dir)) {
			pmd_dir = kernel_ptr_table(memavailp);
#ifdef DEBUG
			printk ("[new pointer %p]", pmd_dir);
#endif
			pgd_set(pgd_dir, pmd_dir);
		} else
			pmd_dir = pmd_offset(pgd_dir, virtaddr);

		if (CPU_IS_020_OR_030) {
			if (virtaddr) {
#ifdef DEBUG
				printk ("[early term]");
#endif
				pmd_dir->pmd[(virtaddr/PTRTREESIZE) & 15] = physaddr;
				physaddr += PTRTREESIZE;
			} else {
				int i;
#ifdef DEBUG
				printk ("[zero map]");
#endif
				pte_dir = (pte_t *)kernel_ptr_table(memavailp);
				pmd_dir->pmd[0] = virt_to_phys(pte_dir) |
					_PAGE_TABLE | _PAGE_ACCESSED;
				pte_val(*pte_dir++) = 0;
				physaddr += PAGE_SIZE;
				for (i = 1; i < 64; physaddr += PAGE_SIZE, i++)
					pte_val(*pte_dir++) = physaddr;
			}
			size -= PTRTREESIZE;
			virtaddr += PTRTREESIZE;
		} else {
			if (!pmd_present(*pmd_dir)) {
#ifdef DEBUG
				printk ("[new table]");
#endif
				pte_dir = kernel_page_table(memavailp);
				pmd_set(pmd_dir, pte_dir);
			}
			pte_dir = pte_offset(pmd_dir, virtaddr);

			if (virtaddr) {
				if (!pte_present(*pte_dir))
					pte_val(*pte_dir) = physaddr;
			} else
				pte_val(*pte_dir) = 0;
			size -= PAGE_SIZE;
			virtaddr += PAGE_SIZE;
			physaddr += PAGE_SIZE;
		}

	}
#ifdef DEBUG
	printk("\n");
#endif

	return virtaddr;
}

extern unsigned long free_area_init(unsigned long, unsigned long);
extern void init_pointer_table(unsigned long ptable);

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 */
unsigned long __init paging_init(unsigned long start_mem,
				 unsigned long end_mem)
{
	int chunk;
	unsigned long mem_avail = 0;

#ifdef DEBUG
	{
		extern unsigned long availmem;
		printk ("start of paging_init (%p, %lx, %lx, %lx)\n",
			kernel_pg_dir, availmem, start_mem, end_mem);
	}
#endif

	/* Fix the cache mode in the page descriptors for the 680[46]0.  */
	if (CPU_IS_040_OR_060) {
		int i;
#ifndef mm_cachebits
		mm_cachebits = _PAGE_CACHE040;
#endif
		for (i = 0; i < 16; i++)
			pgprot_val(protection_map[i]) |= _PAGE_CACHE040;
	}
	/* Fix the PAGE_NONE value. */
	if (CPU_IS_040_OR_060) {
		/* On the 680[46]0 we can use the _PAGE_SUPER bit.  */
		pgprot_val(protection_map[0]) |= _PAGE_SUPER;
		pgprot_val(protection_map[VM_SHARED]) |= _PAGE_SUPER;
	} else {
		/* Otherwise we must fake it. */
		pgprot_val(protection_map[0]) &= ~_PAGE_PRESENT;
		pgprot_val(protection_map[0]) |= _PAGE_FAKE_SUPER;
		pgprot_val(protection_map[VM_SHARED]) &= ~_PAGE_PRESENT;
		pgprot_val(protection_map[VM_SHARED]) |= _PAGE_FAKE_SUPER;
	}

	/*
	 * Map the physical memory available into the kernel virtual
	 * address space.  It may allocate some memory for page
	 * tables and thus modify availmem.
	 */

	for (chunk = 0; chunk < m68k_num_memory; chunk++) {
		mem_avail = map_chunk (m68k_memory[chunk].addr,
				       m68k_memory[chunk].size, &start_mem);

	}

	flush_tlb_all();
#ifdef DEBUG
	printk ("memory available is %ldKB\n", mem_avail >> 10);
	printk ("start_mem is %#lx\nvirtual_end is %#lx\n",
		start_mem, end_mem);
#endif

	/*
	 * initialize the bad page table and bad page to point
	 * to a couple of allocated pages
	 */
	empty_bad_page_table = start_mem;
	start_mem += PAGE_SIZE;
	empty_bad_page = start_mem;
	start_mem += PAGE_SIZE;
	empty_zero_page = start_mem;
	start_mem += PAGE_SIZE;
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	/* 
	 * allocate the "swapper" page directory and
	 * record in task 0 (swapper) tss 
	 */
	init_mm.pgd = (pgd_t *)kernel_ptr_table(&start_mem);
	memset (init_mm.pgd, 0, sizeof(pgd_t)*PTRS_PER_PGD);

	/* setup CPU root pointer for swapper task */
	task[0]->tss.crp[0] = 0x80000000 | _PAGE_TABLE;
	task[0]->tss.crp[1] = virt_to_phys(init_mm.pgd);

#ifdef DEBUG
	printk ("task 0 pagedir at %p virt, %#lx phys\n",
		swapper_pg_dir, task[0]->tss.crp[1]);
#endif

	if (CPU_IS_040_OR_060)
		asm __volatile__ (".chip 68040\n\t"
				  "movec %0,%%urp\n\t"
				  ".chip 68k"
				  : /* no outputs */
				  : "r" (task[0]->tss.crp[1]));
	else
		asm __volatile__ (".chip 68030\n\t"
				  "pmove %0,%%crp\n\t"
				  ".chip 68k"
				  : /* no outputs */
				  : "m" (task[0]->tss.crp[0]));
#ifdef DEBUG
	printk ("set crp\n");
#endif

	/*
	 * Set up SFC/DFC registers (user data space)
	 */
	set_fs (USER_DS);

#ifdef DEBUG
	printk ("before free_area_init\n");
#endif
	return PAGE_ALIGN(free_area_init(start_mem, end_mem));
}

void __init mem_init(unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
	unsigned long tmp;
	int i;

	end_mem &= PAGE_MASK;
	high_memory = (void *) end_mem;
	max_mapnr = num_physpages = MAP_NR(end_mem);

	tmp = start_mem = PAGE_ALIGN(start_mem);
	while (tmp < end_mem) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(tmp)].flags);
		tmp += PAGE_SIZE;
	}

#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI)
		atari_stram_reserve_pages( start_mem );
#endif

	for (tmp = 0 ; tmp < end_mem ; tmp += PAGE_SIZE) {
		if (virt_to_phys ((void *)tmp) >= mach_max_dma_address)
			clear_bit(PG_DMA, &mem_map[MAP_NR(tmp)].flags);
		if (PageReserved(mem_map+MAP_NR(tmp))) {
			if (tmp >= (unsigned long)&_text
			    && tmp < (unsigned long)&_edata) {
				if (tmp < (unsigned long) &_etext)
					codepages++;
				else
					datapages++;
			} else if (tmp >= (unsigned long) &__init_begin
				   && tmp < (unsigned long) &__init_end)
				initpages++;
			else
				datapages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(tmp)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start ||
		    (tmp < (initrd_start & PAGE_MASK) || tmp >= initrd_end))
#endif
			free_page(tmp);
	}

	/* insert pointer tables allocated so far into the tablelist */
	init_pointer_table((unsigned long)kernel_pg_dir);
	for (i = 0; i < PTRS_PER_PGD; i++) {
		if (pgd_val(kernel_pg_dir[i]))
			init_pointer_table(pgd_page(kernel_pg_dir[i]));
	}

	printk("Memory: %luk/%luk available (%dk kernel code, %dk data, %dk init)\n",
	       (unsigned long) nr_free_pages << (PAGE_SHIFT-10),
	       max_mapnr << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10),
	       initpages << (PAGE_SHIFT-10));
}

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)&__init_begin;
	for (; addr < (unsigned long)&__init_end; addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
		free_page(addr);
	}
}

void si_meminfo(struct sysinfo *val)
{
    unsigned long i;

    i = max_mapnr;
    val->totalram = 0;
    val->sharedram = 0;
    val->freeram = nr_free_pages << PAGE_SHIFT;
    val->bufferram = buffermem;
    while (i-- > 0) {
	if (PageReserved(mem_map+i))
	    continue;
	val->totalram++;
	if (!atomic_read(&mem_map[i].count))
	    continue;
	val->sharedram += atomic_read(&mem_map[i].count) - 1;
    }
    val->totalram <<= PAGE_SHIFT;
    val->sharedram <<= PAGE_SHIFT;
    return;
}
