/* 
 * Herein lies all the functions/variables that are "exported" for linkage
 * with dynamically loaded kernel modules.
 *			Jon.
 *
 * - Stacked module support and unified symbol table added (June 1994)
 * - External symbol table support added (December 1994)
 * - Versions on symbols added (December 1994)
 * by Bjorn Ekwall <bj0rn@blox.se>
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
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
#include <linux/termios.h>
#include <linux/tqueue.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/locks.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sem.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/random.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>
#include <linux/skbuff.h>
#include <linux/genhd.h>
#include <linux/swap.h>

extern unsigned char aux_device_present, kbd_read_mask;

#ifdef CONFIG_NET
#include <linux/in.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/firewall.h>
#include <linux/trdevice.h>

#ifdef CONFIG_AX25
#include <net/ax25.h>
#endif
#ifdef CONFIG_INET
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/icmp.h>
#include <net/route.h>
#include <linux/net_alias.h>
#endif
#ifdef CONFIG_NET_ALIAS
#include <linux/net_alias.h>
#endif
#endif
#ifdef CONFIG_PCI
#include <linux/bios32.h>
#include <linux/pci.h>
#endif
#if defined(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
#endif
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif
#include <asm/irq.h>
#ifdef __SMP__
#include <linux/smp.h>
#endif

extern char *get_options(char *str, int *ints);
extern void set_device_ro(int dev,int flag);
extern struct file_operations * get_blkfops(unsigned int);

extern void *sys_call_table;

#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)		
#include "../drivers/net/8390.h"
#endif

extern int sys_tz;
extern int request_dma(unsigned int dmanr, char * deviceID);
extern void free_dma(unsigned int dmanr);
extern int (*rarp_ioctl_hook)(int,void*);

extern void (* iABI_hook)(struct pt_regs * regs);

#ifdef CONFIG_BINFMT_ELF
#include <linux/elfcore.h>
extern int dump_fpu(elf_fpregset_t *);
#endif

extern void dump_thread(struct pt_regs *, struct user *);

struct symbol_table symbol_table = {
#include <linux/symtab_begin.h>
#ifdef MODVERSIONS
	{ (void *)1 /* Version version :-) */,
		SYMBOL_NAME_STR (Using_Versions) },
#endif

	/* stackable module support */
	X(rename_module_symbol),
	X(register_symtab),
#ifdef CONFIG_KERNELD
	X(kerneld_send),
#endif
	X(get_options),

	/* system info variables */
	/* These check that they aren't defines (0/1) */
#ifndef EISA_bus__is_a_macro
	X(EISA_bus),
#endif
#ifndef MCA_bus__is_a_macro
	X(MCA_bus),
#endif
#ifndef wp_works_ok__is_a_macro
	X(wp_works_ok),
#endif

#ifdef CONFIG_PCI
	/* PCI BIOS support */
	X(pcibios_present),
	X(pcibios_find_class),
	X(pcibios_find_device),
	X(pcibios_read_config_byte),
	X(pcibios_read_config_word),
	X(pcibios_read_config_dword),
    	X(pcibios_strerror),
	X(pcibios_write_config_byte),
	X(pcibios_write_config_word),
	X(pcibios_write_config_dword),
#endif

	/* process memory management */
	X(verify_area),
	X(do_mmap),
	X(do_munmap),
	X(insert_vm_struct),
	X(merge_segments),

	/* internal kernel memory management */
	X(__get_free_pages),
	X(free_pages),
	X(kmalloc),
	X(kfree),
	X(vmalloc),
	X(vremap),
	X(vfree),
 	X(mem_map),
 	X(remap_page_range),
	X(high_memory),
	X(update_vm_cache),

	/* filesystem internal functions */
	X(getname),
	X(putname),
	X(__iget),
	X(iput),
	X(namei),
	X(lnamei),
	X(open_namei),
	X(sys_close),
	X(close_fp),
	X(check_disk_change),
	X(invalidate_buffers),
	X(invalidate_inodes),
	X(fsync_dev),
	X(permission),
	X(inode_setattr),
	X(inode_change_ok),
	X(generic_mmap),
	X(set_blocksize),
	X(getblk),
	X(bread),
	X(breada),
	X(__brelse),
	X(__bforget),
	X(ll_rw_block),
	X(__wait_on_buffer),
	X(mark_buffer_uptodate),
	X(unlock_buffer),
	X(dcache_lookup),
	X(dcache_add),
	X(add_blkdev_randomness),
	X(generic_file_read),
	X(generic_readpage),
	X(mark_buffer_uptodate),

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
	X(hardsect_size),
	X(blk_size),
	X(blk_dev),
	X(is_read_only),
	X(set_device_ro),
	X(bmap),
	X(sync_dev),
	X(get_blkfops),
	
	/* Module creation of serial units */
	X(register_serial),
	X(unregister_serial),

	/* tty routines */
	X(tty_hangup),
	X(tty_wait_until_sent),
	X(tty_check_change),
	X(tty_hung_up_p),
	X(do_SAK),
	X(console_print),

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

	/* sysctl table registration */
	X(register_sysctl_table),
	X(unregister_sysctl_table),

	/* interrupt handling */
	X(request_irq),
	X(free_irq),
	X(enable_irq),
	X(disable_irq),
	X(probe_irq_on),
	X(probe_irq_off),
	X(bh_active),
	X(bh_mask),
	X(bh_base),
	X(add_timer),
	X(del_timer),
	X(tq_timer),
	X(tq_immediate),
	X(tq_scheduler),
	X(tq_last),
	X(timer_active),
	X(timer_table),
 	X(intr_count),

	/* autoirq from  drivers/net/auto_irq.c */
	X(autoirq_setup),
	X(autoirq_report),

	/* dma handling */
	X(request_dma),
	X(free_dma),
#ifdef HAVE_DISABLE_HLT
	X(disable_hlt),
	X(enable_hlt),
#endif

	/* IO port handling */
	X(check_region),
	X(request_region),
	X(release_region),

	/* process management */
	X(wake_up),
	X(wake_up_interruptible),
	X(sleep_on),
	X(interruptible_sleep_on),
	X(schedule),
	X(current_set),
	X(jiffies),
	X(xtime),
	X(do_gettimeofday),
	X(loops_per_sec),
	X(need_resched),
	X(kstat),
	X(kill_proc),
	X(kill_pg),
	X(kill_sl),

	/* misc */
	X(panic),
	X(printk),
	X(sprintf),
	X(vsprintf),
	X(kdevname),
	X(simple_strtoul),
	X(system_utsname),
	X(sys_call_table),

	/* Signal interfaces */
	X(send_sig),

	/* Program loader interfaces */
	X(setup_arg_pages),
	X(copy_strings),
	X(create_tables),
	X(do_execve),
	X(flush_old_exec),
	X(open_inode),
	X(read_exec),

	/* Miscellaneous access points */
	X(si_meminfo),
#ifdef CONFIG_NET
	/* Socket layer registration */
	X(sock_register),
	X(sock_unregister),
	/* Socket layer support routines */
	X(skb_recv_datagram),
	X(skb_free_datagram),
	X(skb_copy_datagram),
	X(skb_copy_datagram_iovec),
	X(datagram_select),
#ifdef CONFIG_FIREWALL
	/* Firewall registration */
	X(register_firewall),
	X(unregister_firewall),
#endif
#ifdef CONFIG_INET	
	/* Internet layer registration */
	X(inet_add_protocol),
	X(inet_del_protocol),
	X(rarp_ioctl_hook),
	X(init_etherdev),
	X(ip_rt_route),
	X(icmp_send),
	X(ip_options_compile),
	X(ip_rt_put),
	X(arp_send),
#ifdef CONFIG_IP_FORWARD
	X(ip_forward),
#endif
#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)
	/* If 8390 NIC support is built in, we will need these. */
	X(ei_open),
	X(ei_close),
	X(ei_debug),
	X(ei_interrupt),
	X(ethdev_init),
	X(NS8390_init),
#endif
#ifdef CONFIG_NET_ALIAS
#include <linux/net_alias.h>
#endif
#endif
	/* Device callback registration */
	X(register_netdevice_notifier),
	X(unregister_netdevice_notifier),
#ifdef CONFIG_NET_ALIAS
	X(register_net_alias_type),
	X(unregister_net_alias_type),
#endif
#endif

	/* support for loadable net drivers */
#ifdef CONFIG_AX25
	X(ax25_encapsulate),
	X(ax25_rebuild_header),	
#endif	
#ifdef CONFIG_INET
	X(register_netdev),
	X(unregister_netdev),
	X(ether_setup),
	X(eth_type_trans),
	X(eth_copy_and_sum),
	X(alloc_skb),
	X(kfree_skb),
	X(skb_clone),
	X(dev_alloc_skb),
	X(dev_kfree_skb),
	X(netif_rx),
	X(dev_tint),
	X(irq2dev_map),
	X(dev_add_pack),
	X(dev_remove_pack),
	X(dev_get),
	X(dev_ioctl),
	X(dev_queue_xmit),
	X(dev_base),
	X(dev_close),
	X(arp_find),
	X(n_tty_ioctl),
	X(tty_register_ldisc),
	X(kill_fasync),
#ifdef CONFIG_FIREWALL
	X(call_in_firewall),
#endif
#endif
#ifndef CONFIG_SCSI
	/*
	 * With no scsi configured, we still need to export a few
	 * symbols so that scsi can be loaded later via insmod.
	 * Don't remove this unless you are 100% sure of what you are
	 * doing.  If you want to remove this, you don't know what
	 * you are doing!
	 */
	X(gendisk_head),
	X(resetup_one_dev),
#endif
	/* Added to make file system as module */
	X(set_writetime),
	X(sys_tz),
	X(__wait_on_super),
	X(file_fsync),
	X(clear_inode),
	X(refile_buffer),
	X(nr_async_pages),
	X(___strtok),
	X(init_fifo),
	X(super_blocks),
	X(reuse_list),
	X(chrdev_inode_operations),
	X(blkdev_inode_operations),
	X(read_ahead),
	X(get_hash_table),
	X(get_empty_inode),
	X(insert_inode_hash),
	X(event),
	X(__down),
/* all busmice */
	X(add_mouse_randomness),
	X(fasync_helper),
/* psaux mouse */
	X(aux_device_present),
	X(kbd_read_mask),

#ifdef CONFIG_TR
	X(tr_setup),
	X(tr_type_trans),
#endif

#ifdef CONFIG_BINFMT_ELF
	X(dump_fpu),
#endif
	/* bimfm_aout */
	X(dump_thread),
	X(get_write_access),
	X(put_write_access),

	/********************************************************
	 * Do not add anything below this line,
	 * as the stacked modules depend on this!
	 */
#include <linux/symtab_end.h>
};

/*
int symbol_table_size = sizeof (symbol_table) / sizeof (symbol_table[0]);
*/
