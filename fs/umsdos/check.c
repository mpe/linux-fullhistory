#ifdef MODULE
#include <linux/module.h>
#endif

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

static int check_one_table(struct pde * page_dir)
{
	if (pgd_none(*page_dir))
		return 0;
	if (pgd_bad(*page_dir))
		return 1;
	return 0;
}

/*
 * This function checks all page tables of "current"
 */
void check_page_tables(void)
{
	struct pgd * pg_dir;
	static int err = 0;

	int stack_level = (long)(&pg_dir)-current->kernel_stack_page;
	if (stack_level < 1500) printk ("** %d ** ",stack_level);
	pg_dir = PAGE_DIR_OFFSET(current, 0);
	if (err == 0) {
		int i;
		for (i = 0 ; i < PTRS_PER_PAGE ; i++,page_dir++){
			int notok = check_one_table(page_dir);
			if (notok){
				err++;
				printk ("|%d:%08lx| ",i, page_dir->pgd);
			}
		}
		if (err) printk ("\nErreur MM %d\n",err);
	}
}
