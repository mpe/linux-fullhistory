/* 
 * Herein lies all the functions/variables that are "exported" for linkage
 * with dynamically loaded kernel modules.
 *			Jon.
 */

#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>

#define X(name)	{ (void *) &name, #name }

struct {
	void *addr;
	const char *name;
} symbol_table[] = {
	X(register_chrdev),
	X(unregister_chrdev),
	X(register_blkdev),
	X(unregister_blkdev),
	X(wake_up_interruptible),

	X(wp_works_ok),
	X(__verify_write),

	X(current),
	X(jiffies),
	X(printk),
	X(schedule),

#ifdef CONFIG_FTAPE
	/* The next labels are needed for ftape driver.  */
	X(ftape_big_buffer),
	X(do_floppy),
#endif

};

int symbol_table_size = sizeof (symbol_table) / sizeof (symbol_table[0]);
