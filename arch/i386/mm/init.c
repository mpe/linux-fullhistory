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
#include <linux/init.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/dma.h>

const char bad_pmd_string[] = "Bad pmd in pte_alloc: %08lx\n";

extern void die_if_kernel(char *,struct pt_regs *,long);
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
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
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

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

#define X86_CR4_VME		0x0001		/* enable vm86 extensions */
#define X86_CR4_PVI		0x0002		/* virtual interrupts flag enable */
#define X86_CR4_TSD		0x0004		/* disable time stamp at ipl 3 */
#define X86_CR4_DE		0x0008		/* enable debugging extensions */
#define X86_CR4_PSE		0x0010		/* enable page size extensions */
#define X86_CR4_PAE		0x0020		/* enable physical address extensions */
#define X86_CR4_MCE		0x0040		/* Machine check enable */
#define X86_CR4_PGE		0x0080		/* enable global pages */
#define X86_CR4_PCE		0x0100		/* enable performance counters at ipl 3 */

#define X86_FEATURE_FPU		0x0001		/* internal FPU */
#define X86_FEATURE_VME		0x0002		/* vm86 extensions */
#define X86_FEATURE_DE		0x0004		/* debugging extensions */
#define X86_FEATURE_PSE		0x0008		/* Page size extensions */
#define X86_FEATURE_TSC		0x0010		/* Time stamp counter */
#define X86_FEATURE_MSR		0x0020		/* RDMSR/WRMSR */
#define X86_FEATURE_PAE		0x0040		/* Physical address extension */
#define X86_FEATURE_MCE		0x0080		/* Machine check exception */
#define X86_FEATURE_CXS		0x0100		/* cmpxchg8 available */
#define X86_FEATURE_APIC	0x0200		/* internal APIC */
#define X86_FEATURE_10		0x0400
#define X86_FEATURE_11		0x0800
#define X86_FEATURE_MTRR	0x1000		/* memory type registers */
#define X86_FEATURE_PGE		0x2000		/* Global page */
#define X86_FEATURE_MCA		0x4000		/* Machine Check Architecture */
#define X86_FEATURE_CMOV	0x8000		/* Cmov/fcomi */

#ifdef GAS_KNOWS_CR4
#define read_cr4	"movl %%cr4,%%eax"
#define write_cr4	"movl %%eax,%%cr4"
#else
#define read_cr4	".byte 0x0f,0x20,0xe0"
#define write_cr4	".byte 0x0f,0x22,0xe0"
#endif

/*
 * Save the cr4 feature set we're using (ie
 * Pentium 4MB enable and PPro Global page
 * enable), so that any CPU's that boot up
 * after us can get the correct flags.
 */
unsigned long mmu_cr4_features __initdata = 0;

static inline void set_in_cr4(unsigned long mask)
{
	mmu_cr4_features |= mask;
	__asm__(read_cr4 "\n\t"
		"orl %0,%%eax\n\t"
		write_cr4
		: : "irg" (mask)
		:"ax");
}

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
__initfunc(unsigned long paging_init(unsigned long start_mem, unsigned long end_mem))
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
	address = PAGE_OFFSET;
	pg_dir = swapper_pg_dir;
	/* unmap the original low memory mappings */
	pgd_val(pg_dir[0]) = 0;

	/* Map whole memory from 0xC0000000 */
	pg_dir += 768;
	while (address < end_mem) {
		/*
		 * If we're running on a Pentium CPU, we can use the 4MB
		 * page tables. 
		 *
		 * The page tables we create span up to the next 4MB
		 * virtual memory boundary, but that's OK as we won't
		 * use that memory anyway.
		 */
		if (x86_capability & X86_FEATURE_PSE) {
			unsigned long __pe;

			set_in_cr4(X86_CR4_PSE);
			wp_works_ok = 1;
			__pe = _KERNPG_TABLE + _PAGE_4M + __pa(address);
			/* Make it "global" too if supported */
			if (x86_capability & X86_FEATURE_PGE) {
				set_in_cr4(X86_CR4_PGE);
				__pe += _PAGE_GLOBAL;
			}
			pgd_val(*pg_dir) = __pe;
			pg_dir++;
			address += 4*1024*1024;
			continue;
		}
		/*
		 * We're on a [34]86, use normal page tables.
		 * pg_table is physical at this point
		 */
		pg_table = (pte_t *) (PAGE_MASK & pgd_val(*pg_dir));
		if (!pg_table) {
			pg_table = (pte_t *) __pa(start_mem);
			start_mem += PAGE_SIZE;
		}

		pgd_val(*pg_dir) = _PAGE_TABLE | (unsigned long) pg_table;
		pg_dir++;
		/* now change pg_table to kernel virtual addresses */
		pg_table = (pte_t *) __va(pg_table);
		for (tmp = 0 ; tmp < PTRS_PER_PTE ; tmp++,pg_table++) {
			pte_t pte = mk_pte(address, PAGE_KERNEL);
			if (address >= end_mem)
				pte_val(pte) = 0;
			set_pte(pg_table, pte);
			address += PAGE_SIZE;
		}
	}
	local_flush_tlb();
	return free_area_init(start_mem, end_mem);
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	unsigned long start_low_mem = PAGE_SIZE;
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	int initpages = 0;
	unsigned long tmp;

	end_mem &= PAGE_MASK;
	high_memory = (void *) end_mem;
	max_mapnr = num_physpages = MAP_NR(end_mem);

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* mark usable pages in the mem_map[] */
	start_low_mem = PAGE_ALIGN(start_low_mem)+PAGE_OFFSET;

#ifdef __SMP__
	/*
	 * But first pinch a few for the stack/trampoline stuff
	 *	FIXME: Don't need the extra page at 4K, but need to fix
	 *	trampoline before removing it. (see the GDT stuff)
	 *
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
	while (start_low_mem < 0x9f000+PAGE_OFFSET) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(start_low_mem)].flags);
		start_low_mem += PAGE_SIZE;
	}

	while (start_mem < end_mem) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(start_mem)].flags);
		start_mem += PAGE_SIZE;
	}
	for (tmp = PAGE_OFFSET ; tmp < end_mem ; tmp += PAGE_SIZE) {
		if (tmp >= MAX_DMA_ADDRESS)
			clear_bit(PG_DMA, &mem_map[MAP_NR(tmp)].flags);
		if (PageReserved(mem_map+MAP_NR(tmp))) {
			if (tmp >= (unsigned long) &_text && tmp < (unsigned long) &_edata) {
				if (tmp < (unsigned long) &_etext)
					codepages++;
				else
					datapages++;
			} else if (tmp >= (unsigned long) &__init_begin
				   && tmp < (unsigned long) &__init_end)
				initpages++;
			else if (tmp >= (unsigned long) &__bss_start
				 && tmp < (unsigned long) start_mem)
				datapages++;
			else
				reservedpages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(tmp)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start || (tmp < initrd_start || tmp >=
		    initrd_end))
#endif
			free_page(tmp);
	}
	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init)\n",
		(unsigned long) nr_free_pages << (PAGE_SHIFT-10),
		max_mapnr << (PAGE_SHIFT-10),
		codepages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10),
		initpages << (PAGE_SHIFT-10));
/* test if the WP bit is honoured in supervisor mode */
	if (wp_works_ok < 0) {
		unsigned char tmp_reg;
		unsigned long old = pg0[0];
		printk("Checking if this processor honours the WP bit even in supervisor mode... ");
		pg0[0] = pte_val(mk_pte(PAGE_OFFSET, PAGE_READONLY));
		local_flush_tlb();
		current->mm->mmap->vm_start += PAGE_SIZE;
		__asm__ __volatile__(
			"movb %0,%1 ; movb %1,%0"
			:"=m" (*(char *) __va(0)),
			 "=q" (tmp_reg)
			:/* no inputs */
			:"memory");
		pg0[0] = old;
		local_flush_tlb();
		current->mm->mmap->vm_start -= PAGE_SIZE;
		if (wp_works_ok < 0) {
			wp_works_ok = 0;
			printk("No.\n");
		} else
			printk("Ok.\n");
	}
	return;
}

void free_initmem(void)
{
	unsigned long addr;
	
	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
		free_page(addr);
	}
	printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = max_mapnr;
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
