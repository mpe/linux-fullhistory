#include <asm/system.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>

extern unsigned long high_memory;

static int check_one_table(unsigned long * page_dir)
{
	unsigned long pg_table = *page_dir;

	if (!pg_table)
		return 0;
	if (pg_table >= high_memory || !(pg_table & PAGE_PRESENT)) {
		return 1;
	}
	return 0;
}

/*
 * This function frees up all page tables of a process when it exits.
 */
void check_page_tables(void)
{
	unsigned long pg_dir;
	static int err = 0;

	int stack_level = (long)(&pg_dir)-current->kernel_stack_page;
	if (stack_level < 1500) printk ("** %d ** ",stack_level);
	pg_dir = current->tss.cr3;
	if (mem_map[MAP_NR(pg_dir)] > 1) {
		return;
	}
	if (err == 0){
		unsigned long *page_dir = (unsigned long *) pg_dir;
		unsigned long *base = page_dir;
		int i;
		for (i = 0 ; i < PTRS_PER_PAGE ; i++,page_dir++){
			int notok = check_one_table(page_dir);
			if (notok){
				err++;
				printk ("|%d| ",page_dir-base);
			}
		}
		if (err) printk ("Erreur MM %d\n",err);
	}
}

