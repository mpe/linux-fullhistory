/* $Id: init.c,v 1.13 1998/10/16 19:22:42 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1998 by Ralf Baechle
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/jazzdma.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#ifdef CONFIG_SGI
#include <asm/sgialib.h>
#endif
#include <asm/mmu_context.h>

/*
 * Define this to effectivly disable the userpage colouring shit.
 */
#define CONF_GIVE_A_SHIT_ABOUT_COLOURS

extern void deskstation_tyne_dma_init(void);
extern void show_net_buffers(void);

void __bad_pte_kernel(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
	pmd_val(*pmd) = BAD_PAGETABLE;
}

void __bad_pte(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_val(*pmd) = BAD_PAGETABLE;
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *page;

	page = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (page) {
			clear_page((unsigned long)page);
			pmd_val(*pmd) = (unsigned long)page;
			return page + offset;
		}
		pmd_val(*pmd) = BAD_PAGETABLE;
		return NULL;
	}
	free_page((unsigned long)page);
	if (pmd_bad(*pmd)) {
		__bad_pte_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *page;

	page = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (page) {
			clear_page((unsigned long)page);
			pmd_val(*pmd) = (unsigned long)page;
			return page + offset;
		}
		pmd_val(*pmd) = BAD_PAGETABLE;
		return NULL;
	}
	free_page((unsigned long)page);
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}


asmlinkage int sys_cacheflush(void *addr, int bytes, int cache)
{
	/* XXX Just get it working for now... */
	flush_cache_all();
	return 0;
}

/*
 * We have upto 8 empty zeroed pages so we can map one of the right colour
 * when needed.  This is necessary only on R4000 / R4400 SC and MC versions
 * where we have to avoid VCED / VECI exceptions for good performance at
 * any price.  Since page is never written to after the initialization we
 * don't have to care about aliases on other CPUs.
 */
unsigned long empty_zero_page, zero_page_mask;

static inline unsigned long setup_zero_pages(void)
{
	unsigned long order, size, pg;

	switch (mips_cputype) {
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4400SC:
	case CPU_R4400MC:
		order = 3;
	default:
		order = 0;
	}

	empty_zero_page = __get_free_pages(GFP_KERNEL, order);
	if (!empty_zero_page)
		panic("Oh boy, that early out of memory?");

	pg = MAP_NR(empty_zero_page);
	while(pg < MAP_NR(empty_zero_page) + (1 << order)) {
		set_bit(PG_reserved, &mem_map[pg].flags);
		pg++;
	}

	size = PAGE_SIZE << order;
	zero_page_mask = (size - 1) & PAGE_MASK;
	memset((void *)empty_zero_page, 0, size);

	return size;
}

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;

	if(pgtable_cache_size > high) {
		do {
			if(pgd_quicklist)
				free_pgd_slow(get_pgd_fast()), freed++;
			if(pmd_quicklist)
				free_pmd_slow(get_pmd_fast()), freed++;
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
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pte_t * __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];
	unsigned long page;
	unsigned long dummy1, dummy2;
#if (_MIPS_ISA == _MIPS_ISA_MIPS3) || (_MIPS_ISA == _MIPS_ISA_MIPS4)
	unsigned long dummy3;
#endif

	page = (unsigned long) empty_bad_page_table;
	/*
	 * As long as we only save the low 32 bit of the 64 bit wide
	 * R4000 registers on interrupt we cannot use 64 bit memory accesses
	 * to the main memory.
	 */
#if (_MIPS_ISA == _MIPS_ISA_MIPS3) || (_MIPS_ISA == _MIPS_ISA_MIPS4)
        /*
         * Use 64bit code even for Linux/MIPS 32bit on R4000
         */
	__asm__ __volatile__(
		".set\tnoreorder\n"
		".set\tnoat\n\t"
		".set\tmips3\n\t"
		"dsll32\t$1,%2,0\n\t"
		"dsrl32\t%2,$1,0\n\t"
		"or\t%2,$1\n"
		"1:\tsd\t%2,(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,8\n\t"
		".set\tmips0\n\t"
		".set\tat\n"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2),
		 "=r" (dummy3)
		:"0" (page),
		 "1" (PAGE_SIZE/8),
		 "2" (pte_val(BAD_PAGE)));
#else /* (_MIPS_ISA == _MIPS_ISA_MIPS1) || (_MIPS_ISA == _MIPS_ISA_MIPS2) */
	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\tsw\t%2,(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,4\n\t"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2)
		:"r" (pte_val(BAD_PAGE)),
		 "0" (page),
		 "1" (PAGE_SIZE/4));
#endif

	return (pte_t *)page;
}

pte_t __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];
	unsigned long page = (unsigned long)empty_bad_page;

	clear_page(page);
	return pte_mkdirty(mk_pte(page, PAGE_SHARED));
}

#ifdef __SMP__
spinlock_t user_page_lock = SPIN_LOCK_UNLOCKED;
#endif
struct upcache user_page_cache[8] __attribute__((aligned(32)));
static unsigned long user_page_order;
unsigned long user_page_colours;

unsigned long get_user_page_slow(int which)
{
	unsigned long chunk;
	struct upcache *up = &user_page_cache[0];
	struct page *p, *res;
	int i;

	do {
		chunk = __get_free_pages(GFP_KERNEL, user_page_order);
	} while(chunk==0);

	p = mem_map + MAP_NR(chunk);
	res = p + which;
	spin_lock(&user_page_lock);
	for (i=user_page_colours; i>=0; i--,p++,up++,chunk+=PAGE_SIZE) {
		atomic_set(&p->count, 1);
		p->age = PAGE_INITIAL_AGE;

		if (p != res) {
			if(up->count < USER_PAGE_WATER) {
				p->next = up->list;
				up->list = p;
				up->count++;
			} else
				free_pages(chunk, 0);
		}
	}
	spin_unlock(&user_page_lock);

	return page_address(res);
}

static inline void user_page_setup(void)
{
	unsigned long assoc = 0;
	unsigned long dcache_log, icache_log, cache_log;
	unsigned long config = read_32bit_cp0_register(CP0_CONFIG);

	switch(mips_cputype) {
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4400SC:
	case CPU_R4400MC:
		cache_log = 3;	/* => 32k, sucks  */
		break;

        case CPU_R4600:                 /* two way set associative caches?  */
        case CPU_R4700:
        case CPU_R5000:
        case CPU_NEVADA:
		assoc = 1;
		/* fall through */
	default:
		/* use bigger cache  */
		icache_log = (config >> 9) & 7;
		dcache_log = (config >> 6) & 7;
		if (dcache_log > icache_log)
			cache_log = dcache_log;
		else
			cache_log = icache_log;
	}

#ifdef CONF_GIVE_A_SHIT_ABOUT_COLOURS
	cache_log = assoc = 0;
#endif

	user_page_order = cache_log - assoc;
	user_page_colours = (1 << (cache_log - assoc)) - 1;
}

void show_mem(void)
{
	int i, free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld pages in page table cache\n",pgtable_cache_size);
	printk("%d free pages\n", free);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

extern unsigned long free_area_init(unsigned long, unsigned long);

__initfunc(unsigned long paging_init(unsigned long start_mem, unsigned long end_mem))
{
	/* Initialize the entire pgd.  */
	pgd_init((unsigned long)swapper_pg_dir);
	pgd_init((unsigned long)swapper_pg_dir + PAGE_SIZE / 2);
	return free_area_init(start_mem, end_mem);
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	int codepages = 0;
	int datapages = 0;
	unsigned long tmp;
	extern int _etext, _ftext;

#ifdef CONFIG_MIPS_JAZZ
	if (mips_machgroup == MACH_GROUP_JAZZ)
		start_mem = vdma_init(start_mem, end_mem);
#endif

	end_mem &= PAGE_MASK;
	max_mapnr = num_physpages = MAP_NR(end_mem);
	high_memory = (void *)end_mem;

	/* mark usable pages in the mem_map[] */
	start_mem = PAGE_ALIGN(start_mem);

	for(tmp = MAP_NR(start_mem);tmp < max_mapnr;tmp++)
		clear_bit(PG_reserved, &mem_map[tmp].flags);


#ifdef CONFIG_SGI
	prom_fixup_mem_map(start_mem, (unsigned long)high_memory);
#endif

	for (tmp = PAGE_OFFSET; tmp < end_mem; tmp += PAGE_SIZE) {
		/*
		 * This is only for PC-style DMA.  The onboard DMA
		 * of Jazz and Tyne machines is completly different and
		 * not handled via a flag in mem_map_t.
		 */
		if (tmp >= MAX_DMA_ADDRESS)
			clear_bit(PG_DMA, &mem_map[MAP_NR(tmp)].flags);
		if (PageReserved(mem_map+MAP_NR(tmp))) {
			if ((tmp < (unsigned long) &_etext) &&
			    (tmp >= (unsigned long) &_ftext))
				codepages++;
			else if ((tmp < start_mem) &&
				 (tmp > (unsigned long) &_etext))
				datapages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(tmp)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start || (tmp < initrd_start || tmp >=
		    initrd_end))
#endif
			free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;

	/* Setup zeroed pages.  */
	tmp -= setup_zero_pages();

	printk("Memory: %luk/%luk available (%dk kernel code, %dk data)\n",
		tmp >> 10,
		max_mapnr << (PAGE_SHIFT-10),
		codepages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10));

	/* Initialize allocator for colour matched mapped pages.  */
	user_page_setup();
}

extern char __init_begin, __init_end;

void free_initmem(void)
{
	unsigned long addr;
        
	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
		free_page(addr);
	}
	printk("Freeing unused kernel memory: %dk freed\n",
	       (&__init_end - &__init_begin) >> 10);
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = MAP_NR(high_memory);
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
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

/* Fixup an immediate instruction  */
__initfunc(static void __i_insn_fixup(unsigned int **start, unsigned int **stop,
                         unsigned int i_const))
{
	unsigned int **p, *ip;

	for (p = start;p < stop; p++) {
		ip = *p;
		*ip = (*ip & 0xffff0000) | i_const;
	}
}

#define i_insn_fixup(section, const)					  \
do {									  \
	extern unsigned int *__start_ ## section;			  \
	extern unsigned int *__stop_ ## section;			  \
	__i_insn_fixup(&__start_ ## section, &__stop_ ## section, const); \
} while(0)

/* Caller is assumed to flush the caches before the first context switch.  */
__initfunc(void __asid_setup(unsigned int inc, unsigned int mask,
                             unsigned int version_mask,
                             unsigned int first_version))
{
	i_insn_fixup(__asid_inc, inc);
	i_insn_fixup(__asid_mask, mask);
	i_insn_fixup(__asid_version_mask, version_mask);
	i_insn_fixup(__asid_first_version, first_version);

	asid_cache = first_version;
}
