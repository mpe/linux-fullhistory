/*
 *  linux/arch/i386/mm/init.c
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
#include <linux/swap.h>
#include <linux/smp.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/dma.h>

/*
 * The SMP kernel can't handle the 4MB page table optimizations yet
 */
#ifdef __SMP__
#undef USE_PENTIUM_MM
#endif

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

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (pte_val(BAD_PAGE)),
		 "D" ((long) empty_bad_page_table),
		 "c" (PAGE_SIZE/4)
		:"di","cx");
	return (pte_t *) empty_bad_page_table;
}

pte_t __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl":
		:"a" (0),
		 "D" ((long) empty_bad_page),
		 "c" (PAGE_SIZE/4)
		:"di","cx");
	return pte_mkdirty(mk_pte((unsigned long) empty_bad_page, PAGE_SHARED));
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
		if (PageReserved(mem_map+i))
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

/*
 * Physical page 0 is special; it's not touched by Linux since BIOS
 * and SMM (for laptops with [34]86/SL chips) may need it.  It is read
 * and write protected to detect null pointer references in the
 * kernel.
 * It may also hold the MP configuration table when we are booting SMP.
 */
#if 0
	memset((void *) 0, 0, PAGE_SIZE);
#endif
#ifdef __SMP__
	if (!smp_scan_config(0x0,0x400))	/* Scan the bottom 1K for a signature */
	{
		/*
		 *	FIXME: Linux assumes you have 640K of base ram.. this continues
		 *	the error...
		 */
		if (!smp_scan_config(639*0x400,0x400))	/* Scan the top 1K of base RAM */
			smp_scan_config(0xF0000,0x10000);	/* Scan the 64K of bios */
	}
	/*
	 *	If it is an SMP machine we should know now, unless the configuration
	 *	is in an EISA/MCA bus machine with an extended bios data area. I don't
	 *	have such a machine so someone else can fill in the check of the EBDA
	 *	here.
	 */
/*	smp_alloc_memory(8192); */
#endif
#ifdef TEST_VERIFY_AREA
	wp_works_ok = 0;
#endif
	start_mem = PAGE_ALIGN(start_mem);
	address = 0;
	pg_dir = swapper_pg_dir;
	while (address < end_mem) {
#ifdef USE_PENTIUM_MM
		/*
		 * This will create page tables that
		 * span up to the next 4MB virtual
		 * memory boundary, but that's ok,
		 * we won't use that memory anyway.
		 */
		if (x86_capability & 8) {
#ifdef GAS_KNOWS_CR4
			__asm__("movl %%cr4,%%eax\n\t"
				"orl $16,%%eax\n\t"
				"movl %%eax,%%cr4"
				: : :"ax");
#else
			__asm__(".byte 0x0f,0x20,0xe0\n\t"
				"orl $16,%%eax\n\t"
				".byte 0x0f,0x22,0xe0"
				: : :"ax");
#endif
			wp_works_ok = 1;
			pgd_val(pg_dir[0]) = _PAGE_TABLE | _PAGE_4M | address;
			pgd_val(pg_dir[768]) = _PAGE_TABLE | _PAGE_4M | address;
			pg_dir++;
			address += 4*1024*1024;
			continue;
		}
#endif
		/* map the memory at virtual addr 0xC0000000 */
		pg_table = (pte_t *) (PAGE_MASK & pgd_val(pg_dir[768]));
		if (!pg_table) {
			pg_table = (pte_t *) start_mem;
			start_mem += PAGE_SIZE;
		}

		/* also map it temporarily at 0x0000000 for init */
		pgd_val(pg_dir[0])   = _PAGE_TABLE | (unsigned long) pg_table;
		pgd_val(pg_dir[768]) = _PAGE_TABLE | (unsigned long) pg_table;
		pg_dir++;
		for (tmp = 0 ; tmp < PTRS_PER_PTE ; tmp++,pg_table++) {
			if (address < end_mem)
				set_pte(pg_table, mk_pte(address, PAGE_SHARED));
			else
				pte_clear(pg_table);
			address += PAGE_SIZE;
		}
	}
	flush_tlb();
	return free_area_init(start_mem, end_mem);
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long start_low_mem = PAGE_SIZE;
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	unsigned long tmp;
	extern int _etext;

	end_mem &= PAGE_MASK;
	high_memory = end_mem;

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* mark usable pages in the mem_map[] */
	start_low_mem = PAGE_ALIGN(start_low_mem);

#ifdef __SMP__
	/*
	 * But first pinch a few for the stack/trampoline stuff
	 */
	start_low_mem += PAGE_SIZE;				/* 32bit startup code */
	start_low_mem = smp_alloc_memory(start_low_mem); 	/* AP processor stacks */
#endif
	start_mem = PAGE_ALIGN(start_mem);

	/*
	 * IBM messed up *AGAIN* in their thinkpad: 0xA0000 -> 0x9F000.
	 * They seem to have done something stupid with the floppy
	 * controller as well..
	 */
	while (start_low_mem < 0x9f000) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(start_low_mem)].flags);
		start_low_mem += PAGE_SIZE;
	}

	while (start_mem < high_memory) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(start_mem)].flags);
		start_mem += PAGE_SIZE;
	}
	for (tmp = 0 ; tmp < high_memory ; tmp += PAGE_SIZE) {
		if (tmp >= MAX_DMA_ADDRESS)
			clear_bit(PG_DMA, &mem_map[MAP_NR(tmp)].flags);
		if (PageReserved(mem_map+MAP_NR(tmp))) {
			if (tmp >= 0xA0000 && tmp < 0x100000)
				reservedpages++;
			else if (tmp < (unsigned long) &_etext)
				codepages++;
			else
				datapages++;
			continue;
		}
		mem_map[MAP_NR(tmp)].count = 1;
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start || (tmp < initrd_start || tmp >=
		    initrd_end))
#endif
			free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data)\n",
		tmp >> 10,
		high_memory >> 10,
		codepages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10));
/* test if the WP bit is honoured in supervisor mode */
	if (wp_works_ok < 0) {
		pg0[0] = pte_val(mk_pte(0, PAGE_READONLY));
		flush_tlb();
		__asm__ __volatile__("movb 0,%%al ; movb %%al,0": : :"ax", "memory");
		pg0[0] = 0;
		flush_tlb();
		if (wp_works_ok < 0)
			wp_works_ok = 0;
	}
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
		if (PageReserved(mem_map+i))
			continue;
		val->totalram++;
		if (!mem_map[i].count)
			continue;
		val->sharedram += mem_map[i].count-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}
