/*
 *  arch/mips/mm/init.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Ported to MIPS by Ralf Baechle
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
#include <asm/mipsconfig.h>

extern unsigned long pg0[1024];		/* page table for 0-4MB for everybody */

extern void deskstation_tyne_dma_init(void);
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
pte_t * __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		"1:\tsw\t%2,(%0)\n\t"
		"subu\t%1,%1,1\n\t"
		"bne\t$0,%1,1b\n\t"
		"addiu\t%0,%0,1\n\t"
		".set\treorder"
		:"=r" (dummy),
		 "=r" (dummy)
		:"r" (pte_val(BAD_PAGE)),
		 "0" ((long) empty_bad_page_table),
		 "1" (PTRS_PER_PAGE));

	return (pte_t *) empty_bad_page_table;
}

pte_t __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		"1:\tsw\t$0,(%0)\n\t"
		"subu\t%1,%1,1\n\t"
		"bne\t$0,%1,1b\n\t"
		"addiu\t%0,%0,1\n\t"
		".set\treorder"
		:"=r" (dummy),
		 "=r" (dummy)
		:"0" ((long) empty_bad_page),
		 "1" (PTRS_PER_PAGE));

	return pte_mkdirty(mk_pte((unsigned long) empty_bad_page, PAGE_SHARED));
}

unsigned long __zero_page(void)
{
	extern char empty_zero_page[PAGE_SIZE];
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		"1:\tsw\t$0,(%0)\n\t"
		"subu\t%1,%1,1\n\t"
		"bne\t$0,%1,1b\n\t"
		"addiu\t%0,%0,1\n\t"
		".set\treorder"
		:"=r" (dummy),
		 "=r" (dummy)
		:"0" ((long) empty_zero_page),
		 "1" (PTRS_PER_PAGE));

	return (unsigned long) empty_zero_page;
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

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	pgd_t * pg_dir;
	pte_t * pg_table;
	unsigned long tmp;
	unsigned long address;

	start_mem = PAGE_ALIGN(start_mem);
	address = 0;
	pg_dir = swapper_pg_dir;
	while (address < end_mem) {
		if (pgd_none(pg_dir[0])) {
			pgd_set(pg_dir, (pte_t *) start_mem);
			start_mem += PAGE_SIZE;
		}
		/*
		 * also map it in at 0x00000000 for init
		 */
		pg_table = (pte_t *) pgd_page(pg_dir[0]);
		pgd_set(pg_dir, pg_table);
		pg_dir++;
		for (tmp = 0 ; tmp < PTRS_PER_PAGE ; tmp++,pg_table++) {
			if (address < end_mem)
				*pg_table = mk_pte(address, PAGE_SHARED);
			else
				pte_clear(pg_table);
			address += PAGE_SIZE;
		}
	}
#if KERNELBASE == KSEG0
	cacheflush();
#endif
	invalidate();
	return free_area_init(start_mem, end_mem);
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	unsigned long tmp;
	extern int etext;

	end_mem &= PAGE_MASK;
	high_memory = end_mem;

	/* mark usable pages in the mem_map[] */
	start_mem = PAGE_ALIGN(start_mem);

	while (start_mem < high_memory) {
		mem_map[MAP_NR(start_mem)] = 0;
		start_mem += PAGE_SIZE;
	}
#ifdef CONFIG_DESKSTATION_TYNE
	deskstation_tyne_dma_init();
#endif
#ifdef CONFIG_SCSI
	scsi_mem_init(high_memory);
#endif
#ifdef CONFIG_SOUND
	sound_mem_init();
#endif
	for (tmp = 0 ; tmp < high_memory ; tmp += PAGE_SIZE) {
		if (mem_map[MAP_NR(tmp)]) {
			/*
			 * We don't have any reserved pages on the
			 * MIPS systems supported until now
			 */
			if (0)
				reservedpages++;
			else if (tmp < ((unsigned long) &etext - KERNELBASE))
				codepages++;
			else
				datapages++;
			continue;
		}
		mem_map[MAP_NR(tmp)] = 1;
		free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data)\n",
		tmp >> 10,
		high_memory >> 10,
		codepages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10));
	pg0[0] = pte_val(mk_pte(0, PAGE_READONLY));

	invalidate();
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
