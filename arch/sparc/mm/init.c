/*  $Id: init.c,v 1.60 1998/09/13 04:30:31 davem Exp $
 *  linux/arch/sparc/mm/init.c
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Eddie C. Dost (ecd@skynet.be)
 *  Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif
#include <linux/init.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/vac-ops.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/vaddrs.h>

/* Turn this off if you suspect some place in some physical memory hole
   might get into page tables (something would be broken very much). */
   
#define FREE_UNUSED_MEM_MAP

extern void show_net_buffers(void);

struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];
unsigned long sparc_unmapped_base;

struct pgtable_cache_struct pgt_quicklists;

/* References to section boundaries */
extern char __init_begin, __init_end, etext;

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
pte_t *__bad_pagetable(void)
{
	memset((void *) EMPTY_PGT, 0, PAGE_SIZE);
	return (pte_t *) EMPTY_PGT;
}

pte_t __bad_page(void)
{
	memset((void *) EMPTY_PGE, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte((unsigned long) EMPTY_PGE, PAGE_SHARED));
}

void show_mem(void)
{
	int free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;
	struct page *page, *end;

	printk("\nMem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	for (page = mem_map, end = mem_map + max_mapnr;
	     page < end; page++) {
		if (PageSkip(page)) {
			if (page->next_hash < page)
				break;
			page = page->next_hash;
		}
		total++;
		if (PageReserved(page))
			reserved++;
		else if (PageSwapCache(page))
			cached++;
		else if (!atomic_read(&page->count))
			free++;
		else
			shared += atomic_read(&page->count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld page tables cached\n",pgtable_cache_size);
	if (sparc_cpu_model == sun4m || sparc_cpu_model == sun4d)
		printk("%ld page dirs cached\n", pgd_cache_size);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

extern pgprot_t protection_map[16];

__initfunc(unsigned long sparc_context_init(unsigned long start_mem, int numctx))
{
	int ctx;

	ctx_list_pool = (struct ctx_list *) start_mem;
	start_mem += (numctx * sizeof(struct ctx_list));
	for(ctx = 0; ctx < numctx; ctx++) {
		struct ctx_list *clist;

		clist = (ctx_list_pool + ctx);
		clist->ctx_number = ctx;
		clist->ctx_mm = 0;
	}
	ctx_free.next = ctx_free.prev = &ctx_free;
	ctx_used.next = ctx_used.prev = &ctx_used;
	for(ctx = 0; ctx < numctx; ctx++)
		add_to_free_ctxlist(ctx_list_pool + ctx);
	return start_mem;
}

/*
 * paging_init() sets up the page tables: We call the MMU specific
 * init routine based upon the Sun model type on the Sparc.
 *
 */
extern unsigned long sun4c_paging_init(unsigned long, unsigned long);
extern unsigned long srmmu_paging_init(unsigned long, unsigned long);
extern unsigned long device_scan(unsigned long);

__initfunc(unsigned long 
paging_init(unsigned long start_mem, unsigned long end_mem))
{
	switch(sparc_cpu_model) {
	case sun4c:
	case sun4e:
	case sun4:
		start_mem = sun4c_paging_init(start_mem, end_mem);
		sparc_unmapped_base = 0xe0000000;
		BTFIXUPSET_SETHI(sparc_unmapped_base, 0xe0000000);
		break;
	case sun4m:
	case sun4d:
		start_mem = srmmu_paging_init(start_mem, end_mem);
		sparc_unmapped_base = 0x50000000;
		BTFIXUPSET_SETHI(sparc_unmapped_base, 0x50000000);
		break;

	case ap1000:
#if CONFIG_AP1000
		start_mem = apmmu_paging_init(start_mem, end_mem);
		sparc_unmapped_base = 0x50000000;
		BTFIXUPSET_SETHI(sparc_unmapped_base, 0x50000000);
		break;
#endif

	default:
		prom_printf("paging_init: Cannot init paging on this Sparc\n");
		prom_printf("paging_init: sparc_cpu_model = %d\n", sparc_cpu_model);
		prom_printf("paging_init: Halting...\n");
		prom_halt();
	};

	/* Initialize the protection map with non-constant, MMU dependent values. */
	protection_map[0] = PAGE_NONE;
	protection_map[1] = PAGE_READONLY;
	protection_map[2] = PAGE_COPY;
	protection_map[3] = PAGE_COPY;
	protection_map[4] = PAGE_READONLY;
	protection_map[5] = PAGE_READONLY;
	protection_map[6] = PAGE_COPY;
	protection_map[7] = PAGE_COPY;
	protection_map[8] = PAGE_NONE;
	protection_map[9] = PAGE_READONLY;
	protection_map[10] = PAGE_SHARED;
	protection_map[11] = PAGE_SHARED;
	protection_map[12] = PAGE_READONLY;
	protection_map[13] = PAGE_READONLY;
	protection_map[14] = PAGE_SHARED;
	protection_map[15] = PAGE_SHARED;
	btfixup();
	return device_scan(start_mem);
}

struct cache_palias *sparc_aliases;

extern void srmmu_frob_mem_map(unsigned long);

int physmem_mapped_contig __initdata = 1;

__initfunc(static void taint_real_pages(unsigned long start_mem, unsigned long end_mem))
{
	unsigned long addr, tmp2 = 0;

	if(physmem_mapped_contig) {
		for(addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
			if(addr >= KERNBASE && addr < start_mem)
				addr = start_mem;
			for(tmp2=0; sp_banks[tmp2].num_bytes != 0; tmp2++) {
				unsigned long phys_addr = (addr - PAGE_OFFSET);
				unsigned long base = sp_banks[tmp2].base_addr;
				unsigned long limit = base + sp_banks[tmp2].num_bytes;

				if((phys_addr >= base) && (phys_addr < limit) &&
				   ((phys_addr + PAGE_SIZE) < limit))
					mem_map[MAP_NR(addr)].flags &= ~(1<<PG_reserved);
			}
		}
	} else {
		if((sparc_cpu_model == sun4m) || (sparc_cpu_model == sun4d)) {
			srmmu_frob_mem_map(start_mem);
		} else {
			for(addr = start_mem; addr < end_mem; addr += PAGE_SIZE)
				mem_map[MAP_NR(addr)].flags &= ~(1<<PG_reserved);
		}
	}
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	int codepages = 0;
	int datapages = 0;
	int initpages = 0; 
	unsigned long addr;
	struct page *page, *end;

	/* Saves us work later. */
	memset((void *) ZERO_PAGE, 0, PAGE_SIZE);

	end_mem &= PAGE_MASK;
	max_mapnr = MAP_NR(end_mem);
	high_memory = (void *) end_mem;

	start_mem = PAGE_ALIGN(start_mem);
	num_physpages = 0;

	addr = KERNBASE;
	while(addr < start_mem) {
#ifdef CONFIG_BLK_DEV_INITRD
		if (initrd_below_start_ok && addr >= initrd_start && addr < initrd_end)
			mem_map[MAP_NR(addr)].flags &= ~(1<<PG_reserved);
		else
#endif	
			mem_map[MAP_NR(addr)].flags |= (1<<PG_reserved);
		addr += PAGE_SIZE;
	}

	taint_real_pages(start_mem, end_mem);
	
#ifdef FREE_UNUSED_MEM_MAP
	end = mem_map + max_mapnr;
	for (page = mem_map; page < end; page++) {
		if (PageSkip(page)) {
			unsigned long low, high;

			low = PAGE_ALIGN((unsigned long)(page+1));
			if (page->next_hash < page)
				high = ((unsigned long)end) & PAGE_MASK;
			else
				high = ((unsigned long)page->next_hash) & PAGE_MASK;
			while (low < high) {
				mem_map[MAP_NR(low)].flags &= ~(1<<PG_reserved);
				low += PAGE_SIZE;
			}
		}
	}
#endif
	
	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
		if (PageSkip(mem_map + MAP_NR(addr))) {
			unsigned long next = mem_map[MAP_NR(addr)].next_hash - mem_map;

			next = (next << PAGE_SHIFT) + PAGE_OFFSET;
			if (next < addr || next >= end_mem)
				break;
			addr = next;
		}
		num_physpages++;
		if(PageReserved(mem_map + MAP_NR(addr))) {
			if ((addr < (unsigned long) &etext) && (addr >= KERNBASE))
				codepages++;
			else if((addr >= (unsigned long)&__init_begin && addr < (unsigned long)&__init_end))
				initpages++;
			else if((addr < start_mem) && (addr >= KERNBASE))
				datapages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start ||
		    (addr < initrd_start || addr >= initrd_end))
#endif
			free_page(addr);
	}

	printk("Memory: %dk available (%dk kernel code, %dk data, %dk init) [%08lx,%08lx]\n",
	       nr_free_pages << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), 
	       initpages << (PAGE_SHIFT-10),
	       (unsigned long)PAGE_OFFSET, end_mem);

	freepages.min = nr_free_pages >> 7;
	if(freepages.min < 16)
		freepages.min = 16;
	freepages.low = freepages.min + (freepages.min >> 1);
	freepages.high = freepages.min + freepages.min;
}

void free_initmem (void)
{
	unsigned long addr;
	
	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
		free_page(addr);
	}
}

void si_meminfo(struct sysinfo *val)
{
	struct page *page, *end;

	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	for (page = mem_map, end = mem_map + max_mapnr;
	     page < end; page++) {
		if (PageSkip(page)) {
			if (page->next_hash < page)
				break;
			page = page->next_hash;
		}
		if (PageReserved(page))
			continue;
		val->totalram++;
		if (!atomic_read(&page->count))
			continue;
		val->sharedram += atomic_read(&page->count) - 1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
}
