/* 
 * Herein lies all the functions/variables that are "exported" for linkage
 * with dynamically loaded kernel modules.
 *			Jon.
 *
 * Stacked module support and unified symbol table added by
 * Bjorn Ekwall <bj0rn@blox.se>
 */

#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/ptrace.h>
#include <linux/sys.h>
#include <linux/utsname.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/module.h>
#include <linux/termios.h>
#include <linux/tqueue.h>
#include <linux/tty.h>
#include <linux/serial.h>
#ifdef CONFIG_INET
#include <linux/netdevice.h>
#endif

#include <asm/irq.h>
  
extern void *sys_call_table;

/* must match struct internal_symbol !!! */
#define X(name)	{ (void *) &name, "_" #name }

#ifdef CONFIG_FTAPE
extern char * ftape_big_buffer;
extern void (*do_floppy)(void);
#endif

extern int request_dma(unsigned int dmanr, char * deviceID);
extern void free_dma(unsigned int dmanr);

extern int do_execve(char * filename, char ** argv, char ** envp,
		struct pt_regs * regs);
extern int do_signal(unsigned long oldmask, struct pt_regs * regs);

extern void (* iABI_hook)(struct pt_regs * regs);

struct symbol_table symbol_table = { 0, 0, 0, /* for stacked module support */
	{
	/* stackable module support */
	X(rename_module_symbol),

	/* system info variables */
	X(EISA_bus),
	X(wp_works_ok),

	/* process memory management */
	X(verify_area),
	X(do_mmap),
	X(do_munmap),
	X(zeromap_page_range),
	X(unmap_page_range),
	X(insert_vm_struct),
	X(merge_segments),

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
	X(open_namei),

	/* device registration */
	X(register_chrdev),
	X(unregister_chrdev),
	X(register_blkdev),
	X(unregister_blkdev),
	X(tty_register_driver),
	X(tty_unregister_driver),
	X(tty_std_termios),

	/* block device driver support */
	X(block_read),
	X(block_write),
	X(block_fsync),
	X(wait_for_request),
	X(blksize_size),
	X(blk_dev),
	
	/* Module creation of serial units */
	X(register_serial),
	X(unregister_serial),

	/* filesystem registration */
	X(register_filesystem),
	X(unregister_filesystem),

	/* executable format registration */
	X(register_binfmt),
	X(unregister_binfmt),

	/* execution environment registration */
	X(lookup_exec_domain),
	X(register_exec_domain),
	X(unregister_exec_domain),

	/* interrupt handling */
	X(request_irq),
	X(free_irq),
	X(enable_irq),
	X(disable_irq),
	X(bh_active),
	X(bh_mask),
	X(add_timer),
	X(del_timer),

	/* dma handling */
	X(request_dma),
	X(free_dma),

	/* process management */
	X(wake_up),
	X(wake_up_interruptible),
	X(sleep_on),
	X(interruptible_sleep_on),
	X(schedule),
	X(current),
	X(jiffies),
	X(xtime),

	/* misc */
	X(panic),
	X(printk),
	X(sprintf),
	X(vsprintf),
	X(system_utsname),
	X(sys_call_table),

	/* Signal interfaces */
	X(do_signal),
	X(send_sig),

	/* Program loader interfaces */
	X(change_ldt),
	X(copy_strings),
	X(create_tables),
	X(do_execve),
	X(flush_old_exec),
	X(open_inode),
	X(read_exec),

	/* Miscellaneous access points */
	X(si_meminfo),

#ifdef CONFIG_FTAPE
	/* The next labels are needed for ftape driver.  */
	X(ftape_big_buffer),
	X(do_floppy),
#endif
#ifdef CONFIG_INET
	/* support for loadable net drivers */
	X(register_netdev),
	X(unregister_netdev),
	X(ether_setup),
	X(alloc_skb),
	X(kfree_skb),
	X(dev_kfree_skb),
	X(snarf_region),
	X(netif_rx),
	X(dev_rint),
	X(dev_tint),
	X(irq2dev_map),
#endif

	/********************************************************
	 * Do not add anything below this line,
	 * as the stacked modules depend on this!
	 */
	{ NULL, NULL } /* mark end of table */
	},
	{ { NULL, NULL } /* no module refs */ }
};

/*
int symbol_table_size = sizeof (symbol_table) / sizeof (symbol_table[0]);
*/
