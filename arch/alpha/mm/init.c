/*
 *  linux/arch/alpha/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/hwrpb.h>

extern void scsi_mem_init(unsigned long);
extern void sound_mem_init(void);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

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
pmd_t * __bad_pagetable(void)
{
	memset((void *) EMPTY_PGT, 0, PAGE_SIZE);
	return (pmd_t *) EMPTY_PGT;
}

pte_t __bad_page(void)
{
	memset((void *) EMPTY_PGE, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte((unsigned long) EMPTY_PGE, PAGE_SHARED));
}

unsigned long __zero_page(void)
{
	memset((void *) ZERO_PGE, 0, PAGE_SIZE);
	return (unsigned long) ZERO_PGE;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = high_memory >> PAGE_SHIFT;
	while (i-- > 0) {
		total++;
		if (mem_map[i] & MAP_PAGE_RESERVED)
			reserved++;
		else if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
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

extern unsigned long free_area_init(unsigned long, unsigned long);

static void load_PCB(struct thread_struct * pcb)
{
	__asm__ __volatile__(
		"stq $30,0(%0)\n\t"
		"bis %0,%0,$16\n\t"
		".long %1"
		: /* no outputs */
		: "r" (pcb), "i" (PAL_swpctx)
		: "$0", "$1", "$16", "$22", "$23", "$24", "$25");
}

/*
 * paging_init() sets up the page tables: in the alpha version this actually
 * unmaps the bootup page table (as we're now in KSEG, so we don't need it).
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	int i;
	unsigned long newptbr;
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;

	/* initialize mem_map[] */
	start_mem = free_area_init(start_mem, end_mem);

	/* find free clusters, update mem_map[] accordingly */
	memdesc = (struct memdesc_struct *) (INIT_HWRPB->mddt_offset + (unsigned long) INIT_HWRPB);
	cluster = memdesc->cluster;
	for (i = memdesc->numclusters ; i > 0; i--, cluster++) {
		unsigned long pfn, nr;
		if (cluster->usage & 1)
			continue;
		pfn = cluster->start_pfn;
		nr = cluster->numpages;

		/* non-volatile memory. We might want to mark this for later */
		if (cluster->usage & 2)
			continue;

		while (nr--)
			mem_map[pfn++] = 0;
	}

	/* unmap the console stuff: we don't need it, and we don't want it */
	/* Also set up the real kernel PCB while we're at it.. */
	memset((void *) ZERO_PGE, 0, PAGE_SIZE);
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	newptbr = ((unsigned long) swapper_pg_dir - PAGE_OFFSET) >> PAGE_SHIFT;
	pgd_val(swapper_pg_dir[1023]) = (newptbr << 32) | pgprot_val(PAGE_KERNEL);
	init_task.tss.ptbr = newptbr;
	load_PCB(&init_task.tss);

	invalidate_all();
	return start_mem;
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long tmp;

	end_mem &= PAGE_MASK;
	high_memory = end_mem;
	start_mem = PAGE_ALIGN(start_mem);

	/*
	 * Mark the pages used by the kernel as reserved..
	 */
	tmp = KERNEL_START;
	while (tmp < start_mem) {
		mem_map[MAP_NR(tmp)] = MAP_PAGE_RESERVED;
		tmp += PAGE_SIZE;
	}


#ifdef CONFIG_SCSI
	scsi_mem_init(high_memory);
#endif
#ifdef CONFIG_SOUND
	sound_mem_init();
#endif

	for (tmp = PAGE_OFFSET ; tmp < high_memory ; tmp += PAGE_SIZE) {
		if (mem_map[MAP_NR(tmp)])
			continue;
		mem_map[MAP_NR(tmp)] = 1;
		free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk available\n", tmp >> 10);
	return;
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = high_memory >> PAGE_SHIFT;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (mem_map[i] & MAP_PAGE_RESERVED)
			continue;
		val->totalram++;
		if (!mem_map[i])
			continue;
		val->sharedram += mem_map[i]-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}
