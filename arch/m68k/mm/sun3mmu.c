/* 
 * linux/arch/m68k/mm/sun3mmu.c
 *
 * Implementations of mm routines specific to the sun3 MMU.
 *
 * Moved here 8/20/1999 Sam Creasey
 *
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

extern void mmu_emu_init (void);

extern unsigned long free_area_init(unsigned long, unsigned long);

const char bad_pmd_string[] = "Bad pmd in pte_alloc: %08lx\n";

extern unsigned long empty_bad_page_table;
extern unsigned long empty_bad_page;

void free_initmem(void)
{
}
/* For the sun3 we try to follow the i386 paging_init() more closely */
/* start_mem and end_mem have PAGE_OFFSET added already */
/* now sets up tables using sun3 PTEs rather than i386 as before. --m */
unsigned long __init paging_init(unsigned long start_mem,
				 unsigned long end_mem)
{
	pgd_t * pg_dir;
	pte_t * pg_table;
	int i;
	unsigned long address;

#ifdef TEST_VERIFY_AREA
	wp_works_ok = 0;
#endif
	start_mem = PAGE_ALIGN(start_mem);
	empty_bad_page_table = start_mem;
	start_mem += PAGE_SIZE;
	empty_bad_page = start_mem;
	start_mem += PAGE_SIZE;
	empty_zero_page = start_mem;
	start_mem += PAGE_SIZE;
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	address = PAGE_OFFSET;
	pg_dir = swapper_pg_dir;
	memset (swapper_pg_dir, 0, sizeof (swapper_pg_dir));
	memset (kernel_pg_dir,  0, sizeof (kernel_pg_dir));

	/* Map whole memory from PAGE_OFFSET (0x0E000000) */
	pg_dir += PAGE_OFFSET >> PGDIR_SHIFT; 

	while (address < end_mem) {
		pg_table = (pte_t *) __pa (start_mem);
		start_mem += PTRS_PER_PTE * sizeof (pte_t);
		pgd_val(*pg_dir) = (unsigned long) pg_table;
		pg_dir++;

		/* now change pg_table to kernel virtual addresses */
		pg_table = (pte_t *) __va ((unsigned long) pg_table);
		for (i=0; i<PTRS_PER_PTE; ++i, ++pg_table) {
			pte_t pte = mk_pte (address, PAGE_INIT);
			if (address >= end_mem)
				pte_val (pte) = 0;
			set_pte (pg_table, pte);
			address += PAGE_SIZE;
		}
	}

	mmu_emu_init();
	
	current->mm = NULL;

	return PAGE_ALIGN(free_area_init(start_mem, end_mem));
}


