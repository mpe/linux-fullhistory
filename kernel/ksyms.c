/* 
 * Herein lies all the functions/variables that are "exported" for linkage
 * with dynamically loaded kernel modules.
 *			Jon.
 */

#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/malloc.h>

#define X(name)	{ (void *) &name, "_" #name }

#ifdef CONFIG_FTAPE
extern char * ftape_big_buffer;
extern void (*do_floppy)(void);
#endif

struct {
	void *addr;
	const char *name;
} symbol_table[] = {
	/* process memory management */
	X(wp_works_ok),
	X(__verify_write),
	X(do_mmap),
	X(do_munmap),

	/* internal kernel memory management */
	X(__get_free_pages),
	X(free_pages),
	X(kmalloc),
	X(kfree_s),
	X(vmalloc),
	X(vfree),

	/* filesystem internal functions */
	X(getname),
	X(putname),
	X(__iget),
	X(iput),
	X(namei),
	X(lnamei),

	/* device registration */
	X(register_chrdev),
	X(unregister_chrdev),
	X(register_blkdev),
	X(unregister_blkdev),

	/* interrupt handling */
	X(request_irq),
	X(free_irq),

	/* process management */
	X(wake_up),
	X(wake_up_interruptible),
	X(schedule),
	X(current),
	X(jiffies),
	X(xtime),

	/* misc */
	X(printk),
	X(sprintf),
	X(vsprintf),

#ifdef CONFIG_FTAPE
	/* The next labels are needed for ftape driver.  */
	X(ftape_big_buffer),
	X(do_floppy),
#endif
};

int symbol_table_size = sizeof (symbol_table) / sizeof (symbol_table[0]);
