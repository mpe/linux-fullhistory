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
#include <linux/binfmts.h>
#include <linux/ptrace.h>
#include <linux/sys.h>
#include <linux/utsname.h>
#include <linux/interrupt.h>
#include <linux/binfmts.h>
#ifdef CONFIG_INET
#include <linux/netdevice.h>
#endif
  
extern void *sys_call_table;

#define X(name)	{ (void *) &name, "_" #name }

#ifdef CONFIG_FTAPE
extern char * ftape_big_buffer;
extern void (*do_floppy)(void);
#endif

extern int do_execve(char * filename, char ** argv, char ** envp,
		struct pt_regs * regs);
extern void flush_old_exec(struct linux_binprm * bprm);
extern int open_inode(struct inode * inode, int mode);
extern int read_exec(struct inode *inode, unsigned long offset,
	char * addr, unsigned long count);
extern int do_signal(unsigned long oldmask, struct pt_regs * regs);

extern void (* iABI_hook)(struct pt_regs * regs);

#ifdef CONFIG_INET
extern int register_netdev(struct device *);
extern void unregister_netdev(struct device *);
extern void ether_setup(struct device *);
extern struct sk_buff *alloc_skb(unsigned int,int);
extern void kfree_skb(struct sk_buff *, int);
extern void snarf_region(unsigned int, unsigned int);
extern void netif_rx(struct sk_buff *);
extern int dev_rint(unsigned char *, long, int, struct device *);
extern void dev_tint(struct device *);
extern struct device *irq2dev_map[];
#endif

struct {
	void *addr;
	const char *name;
} symbol_table[] = {
	/* system info variables */
	X(EISA_bus),
	X(wp_works_ok),

	/* process memory management */
	X(__verify_write),
	X(do_mmap),
	X(do_munmap),
	X(insert_vm_struct),
	X(zeromap_page_range),

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

	/* filesystem registration */
	X(register_filesystem),
	X(unregister_filesystem),

	/* executable format registration */
	X(register_binfmt),
	X(unregister_binfmt),

	/* interrupt handling */
	X(request_irq),
	X(free_irq),
	X(bh_active),
	X(bh_mask),

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
	X(snarf_region),
	X(netif_rx),
	X(dev_rint),
	X(dev_tint),
	X(irq2dev_map),
#endif
};

int symbol_table_size = sizeof (symbol_table) / sizeof (symbol_table[0]);
