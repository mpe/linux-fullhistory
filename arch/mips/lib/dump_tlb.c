/*
 * Dump R4x00 TLB for debugging purposes.
 *
 * Copyright (C) 1994, 1995 by Waldorf Electronics,
 * written by Ralf Baechle.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/mipsconfig.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable.h>

static char *region_map [] = {
	"u", "s", "k", "!"
};

void
dump_tlb(int first, int last)
{
	int	i;
	int	wired;
	unsigned int pagemask, c0, c1, r;
	unsigned long long entryhi, entrylo0, entrylo1;

	wired = read_32bit_cp0_register(CP0_WIRED);
	printk("Wired: %d", wired);
	
	for(i=first;i<last;i++)
	{
		write_32bit_cp0_register(CP0_INDEX, i);
		__asm__ __volatile__(
			".set\tmips3\n\t"
			".set\tnoreorder\n\t"
			"nop;nop;nop;nop\n\t"
			"tlbr\n\t"
			"nop;nop;nop;nop\n\t"
			".set\treorder\n\t"
			".set\tmips0\n\t");
		pagemask = read_32bit_cp0_register(CP0_PAGEMASK);
		entryhi  = read_64bit_cp0_register(CP0_ENTRYHI);
		entrylo0 = read_64bit_cp0_register(CP0_ENTRYLO0);
		entrylo1 = read_64bit_cp0_register(CP0_ENTRYLO1);

		if((entrylo0|entrylo1) & 2)
		{
			/*
			 * Only print entries in use
			 */
			printk("\nIndex: %2d pgmask=%08x ", i, pagemask);

			r  = entryhi >> 62;
			c0 = (entrylo0 >> 3) & 7;
			c1 = (entrylo1 >> 3) & 7;

			printk("%s vpn2=%08Lx "
			       "[pfn=%06Lx c=%d d=%d v=%d g=%Ld]"
			       "[pfn=%06Lx c=%d d=%d v=%d g=%Ld]",
			       region_map [r], (entryhi >> 13) & 0xffffffff,
			       (entrylo0 >> 6) & 0xffffff, c0,
			       (entrylo0 & 4) ? 1 : 0,
			       (entrylo0 & 2) ? 1 : 0,
			       (entrylo0 & 1),
			       (entrylo1 >> 6) & 0xffffff, c1,
			       (entrylo1 & 4) ? 1 : 0,
			       (entrylo1 & 2) ? 1 : 0,
			       (entrylo1 & 1));
			       
		}
	}
	printk("\n");
}

void
dump_tlb_all(void)
{
	dump_tlb(0, mips_tlb_entries - 1);
}

void
dump_tlb_wired(void)
{
	dump_tlb(0, read_32bit_cp0_register(CP0_WIRED));
}

void
dump_tlb_nonwired(void)
{
	dump_tlb(read_32bit_cp0_register(CP0_WIRED), mips_tlb_entries - 1);
}

void
dump_list_process(struct task_struct *t, void *address)
{
	pgd_t	*page_dir, *pgd;
	pmd_t	*pmd;
	pte_t	*pte, page;
	unsigned int addr;

	addr = (unsigned int) address;

	printk("Addr              == %08x\n", addr);
	printk("tasks->tss.pg_dir == %08x\n", (unsigned int) t->tss.pg_dir);
	printk("tasks->mm.pgd     == %08x\n", (unsigned int) t->mm->pgd);

	page_dir = pgd_offset(t->mm, 0);
	printk("page_dir == %08x\n", (unsigned int) page_dir);

	pgd = pgd_offset(t->mm, addr);
	printk("pgd == %08x, ", (unsigned int) pgd);

	pmd = pmd_offset(pgd, addr);
	printk("pmd == %08x, ", (unsigned int) pmd);

	pte = pte_offset(pmd, addr);
	printk("pte == %08x, ", (unsigned int) pte);

	page = *pte;
	printk("page == %08x\n", (unsigned int) pte_val(page));
}

void
dump_list_current(void *address)
{
	dump_list_process(current, address);
}

unsigned int
vtop(void *address)
{
	pgd_t	*pgd;
	pmd_t	*pmd;
	pte_t	*pte;
	unsigned int addr, paddr;

	addr = (unsigned long) address;
	pgd = pgd_offset(current->mm, addr);
	pmd = pmd_offset(pgd, addr);
	pte = pte_offset(pmd, addr);
	paddr = (KSEG1 | (unsigned int) pte_val(*pte)) & PAGE_MASK;
	paddr |= (addr & ~PAGE_MASK);

	return paddr;
}

void
dump16(unsigned long *p)
{
	int i;

	for(i=0;i<8;i++)
	{
		printk("*%08lx == %08lx, ",
		       (unsigned long)p, (unsigned long)*p++);
		printk("*%08lx == %08lx\n",
		       (unsigned long)p, (unsigned long)*p++);
	}
}
