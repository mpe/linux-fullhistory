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
#ifdef CONFIG_ATARI
#include <asm/atari_stram.h>
#endif

extern void die_if_kernel(char *,struct pt_regs *,long);
extern void init_kpointer_table(void);
extern void show_net_buffers(void);

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
	if (PageSwapCache(mem_map+i))
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

pte_t *kernel_page_table (unsigned long *memavailp)
{
	pte_t *ptablep;

	if (memavailp) {
		ptablep = (pte_t *)*memavailp;
		*memavailp += PAGE_SIZE;
	}
	else
		ptablep = (pte_t *)__get_free_page(GFP_KERNEL);

	flush_page_to_ram((unsigned long) ptablep);
	flush_tlb_kernel_page((unsigned long) ptablep);
	nocache_page ((unsigned long)ptablep);

	return ptablep;
}

__initfunc(static unsigned long
map_chunk (unsigned long addr, unsigned long size, unsigned long *memavailp))
{
#define ONEMEG	(1024*1024)
#define L3TREESIZE (256*1024)

	static unsigned long mem_mapped = 0;
	static unsigned long virtaddr = 0;
	static pte_t *ktablep = NULL;
	unsigned long *kpointerp;
	unsigned long physaddr;
	extern pte_t *kpt;
	int pindex;   /* index into pointer table */
	pgd_t *page_dir = pgd_offset_k (virtaddr);

	if (!pgd_present (*page_dir)) {
		/* we need a new pointer table */
		kpointerp = (unsigned long *) get_kpointer_table ();
		pgd_set (page_dir, (pmd_t *) kpointerp);
		memset (kpointerp, 0, PTRS_PER_PMD * sizeof (pmd_t));
	}
	else
		kpointerp = (unsigned long *) pgd_page (*page_dir);

	/*
	 * pindex is the offset into the pointer table for the
	 * descriptors for the current virtual address being mapped.
	 */
	pindex = (virtaddr >> 18) & 0x7f;

#ifdef DEBUG
	printk ("mm=%ld, kernel_pg_dir=%p, kpointerp=%p, pindex=%d\n",
		mem_mapped, kernel_pg_dir, kpointerp, pindex);
#endif

	/*
	 * if this is running on an '040, we already allocated a page
	 * table for the first 4M.  The address is stored in kpt by
	 * arch/head.S
	 *
	 */
	if (CPU_IS_040_OR_060 && mem_mapped == 0)
		ktablep = kpt;

	for (physaddr = addr;
	     physaddr < addr + size;
	     mem_mapped += L3TREESIZE, virtaddr += L3TREESIZE) {

#ifdef DEBUG
		printk ("pa=%#lx va=%#lx ", physaddr, virtaddr);
#endif

		if (pindex > 127 && mem_mapped >= 32*ONEMEG) {
			/* we need a new pointer table every 32M */
#ifdef DEBUG
			printk ("[new pointer]");
#endif

			kpointerp = (unsigned long *)get_kpointer_table ();
			pgd_set(pgd_offset_k(virtaddr), (pmd_t *)kpointerp);
			pindex = 0;
		}

		if (CPU_IS_040_OR_060) {
			int i;
			unsigned long ktable;

			/* Don't map the first 4 MB again. The pagetables
			 * for this range have already been initialized
			 * in boot/head.S. Otherwise the pages used for
			 * tables would be reinitialized to copyback mode.
			 */

			if (mem_mapped < 4 * ONEMEG)
			{
#ifdef DEBUG
				printk ("Already initialized\n");
#endif
				physaddr += L3TREESIZE;
				pindex++;
				continue;
			}
#ifdef DEBUG
			printk ("[setup table]");
#endif

			/*
			 * 68040, use page tables pointed to by the
			 * kernel pointer table.
			 */

			if ((pindex & 15) == 0) {
				/* Need new page table every 4M on the '040 */
#ifdef DEBUG
				printk ("[new table]");
#endif
				ktablep = kernel_page_table (memavailp);
			}

			ktable = VTOP(ktablep);

			/*
			 * initialize section of the page table mapping
			 * this 256K portion.
			 */
			for (i = 0; i < 64; i++) {
				pte_val(ktablep[i]) = physaddr | _PAGE_PRESENT
					| _PAGE_CACHE040 | _PAGE_GLOBAL040
					| _PAGE_ACCESSED;
				physaddr += PAGE_SIZE;
			}
			ktablep += 64;

			/*
			 * make the kernel pointer table point to the
			 * kernel page table.  Each entries point to a
			 * 64 entry section of the page table.
			 */

			kpointerp[pindex++] = ktable | _PAGE_TABLE | _PAGE_ACCESSED;
		} else {
			/*
			 * 68030, use early termination page descriptors.
			 * Each one points to 64 pages (256K).
			 */
#ifdef DEBUG
			printk ("[early term] ");
#endif
			if (virtaddr == 0UL) {
				/* map the first 256K using a 64 entry
				 * 3rd level page table.
				 * UNMAP the first entry to trap
				 * zero page (NULL pointer) references
				 */
				int i;
				unsigned long *tbl;
				
				tbl = (unsigned long *)get_kpointer_table();

				kpointerp[pindex++] = VTOP(tbl) | _PAGE_TABLE |_PAGE_ACCESSED;

				for (i = 0; i < 64; i++, physaddr += PAGE_SIZE)
					tbl[i] = physaddr | _PAGE_PRESENT | _PAGE_ACCESSED;
				
				/* unmap the zero page */
				tbl[0] = 0;
			} else {
				/* not the first 256K */
				kpointerp[pindex++] = physaddr | _PAGE_PRESENT | _PAGE_ACCESSED;
#ifdef DEBUG
				printk ("%lx=%lx ", VTOP(&kpointerp[pindex-1]),
					kpointerp[pindex-1]);
#endif
				physaddr += 64 * PAGE_SIZE;
			}
		}
#ifdef DEBUG
		printk ("\n");
#endif
	}

	return mem_mapped;
}

extern unsigned long free_area_init(unsigned long, unsigned long);

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 */
__initfunc(unsigned long paging_init(unsigned long start_mem,
				     unsigned long end_mem))
{
	int chunk;
	unsigned long mem_avail = 0;

#ifdef DEBUG
	{
		extern pte_t *kpt;
		printk ("start of paging_init (%p, %p, %lx, %lx, %lx)\n",
			kernel_pg_dir, kpt, availmem, start_mem, end_mem);
	}
#endif

	init_kpointer_table();

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

#if 0
	/* 
	 * allocate the "swapper" page directory and
	 * record in task 0 (swapper) tss 
	 */
	swapper_pg_dir = (pgd_t *)get_kpointer_table();

	init_mm.pgd = swapper_pg_dir;
#endif

	memset (swapper_pg_dir, 0, sizeof(pgd_t)*PTRS_PER_PGD);

	/* setup CPU root pointer for swapper task */
	task[0]->tss.crp[0] = 0x80000000 | _PAGE_TABLE;
	task[0]->tss.crp[1] = VTOP (swapper_pg_dir);

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

	return PAGE_ALIGN(free_area_init (start_mem, end_mem));
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
	unsigned long tmp;

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
		if (VTOP (tmp) >= mach_max_dma_address)
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
