/*  $Id: init.c,v 1.2 1997/01/02 14:14:42 jj Exp $
 *  arch/sparc64/mm/init.c
 *
 *  Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

extern void show_net_buffers(void);

struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];

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
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("\nMem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map + i))
			reserved++;
		else if (!mem_map[i].count)
			free++;
		else
			shared += mem_map[i].count-1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

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

/* paging_init() sets up the page tables */

__initfunc(unsigned long 
paging_init(unsigned long start_mem, unsigned long end_mem))
{
}

extern int min_free_pages;
extern int free_pages_low;
extern int free_pages_high;

__initfunc(static void taint_real_pages(unsigned long start_mem, unsigned long end_mem))
{
	unsigned long addr, tmp2 = 0;

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
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	int codepages = 0;
	int datapages = 0;
	unsigned long tmp2, addr;
	extern char etext;

	/* Saves us work later. */
	memset((void *) ZERO_PAGE, 0, PAGE_SIZE);

	end_mem &= PAGE_MASK;
	max_mapnr = MAP_NR(end_mem);
	high_memory = (void *) end_mem;

	start_mem = PAGE_ALIGN(start_mem);
	num_physpages = (start_mem - KERNBASE) >> PAGE_SHIFT;

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
	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
		if(PageReserved(mem_map + MAP_NR(addr))) {
			if ((addr < (unsigned long) &etext) && (addr >= KERNBASE))
				codepages++;
			else if((addr < start_mem) && (addr >= KERNBASE))
				datapages++;
			continue;
		}
		mem_map[MAP_NR(addr)].count = 1;
		num_physpages++;
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start ||
		    (addr < initrd_start || addr >= initrd_end))
#endif
			free_page(addr);
	}

	tmp2 = nr_free_pages << PAGE_SHIFT;

	printk("Memory: %luk available (%dk kernel code, %dk data) [%08lx,%08lx]\n",
	       tmp2 >> 10,
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), PAGE_OFFSET, end_mem);

	min_free_pages = nr_free_pages >> 7;
	if(min_free_pages < 16)
		min_free_pages = 16;
	free_pages_low = min_free_pages + (min_free_pages >> 1);
	free_pages_high = min_free_pages + min_free_pages;
}

void free_initmem (void)
{
	extern char __init_begin, __init_end;
	unsigned long addr;
	
	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		mem_map[MAP_NR(addr)].count = 1;
		free_page(addr);
	}
	printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
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
		if (PageReserved(mem_map + i))
			continue;
		val->totalram++;
		if (!mem_map[i].count)
			continue;
		val->sharedram += mem_map[i].count-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
}
