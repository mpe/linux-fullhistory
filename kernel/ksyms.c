/*
 * Herein lies all the functions/variables that are "exported" for linkage
 * with dynamically loaded kernel modules.
 *			Jon.
 *
 * - Stacked module support and unified symbol table added (June 1994)
 * - External symbol table support added (December 1994)
 * - Versions on symbols added (December 1994)
 *   by Bjorn Ekwall <bj0rn@blox.se>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <linux/ucdrom.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
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
#include <linux/reboot.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>
#include <linux/hdreg.h>
#include <linux/skbuff.h>
#include <linux/genhd.h>
#include <linux/swap.h>
#include <linux/ctype.h>
#include <linux/file.h>

extern unsigned char aux_device_present, kbd_read_mask;

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
extern int blkdev_release(struct inode * inode);
#if !defined(CONFIG_NFSD) && defined(CONFIG_NFSD_MODULE)
extern int (*do_nfsservctl)(int, void *, void *);
#endif

extern void *sys_call_table;

extern int sys_tz;
extern int request_dma(unsigned int dmanr, char * deviceID);
extern void free_dma(unsigned int dmanr);

#ifdef MODVERSIONS
const struct module_symbol __export_Using_Versions
__attribute__((section("__ksymtab"))) = {
	1 /* Version version */, "Using_Versions"
};
#endif

#ifdef CONFIG_KERNELD
EXPORT_SYMBOL(kerneld_send);
#endif
EXPORT_SYMBOL(get_options);

#ifdef CONFIG_PCI
/* PCI BIOS support */
EXPORT_SYMBOL(pcibios_present);
EXPORT_SYMBOL(pcibios_find_class);
EXPORT_SYMBOL(pcibios_find_device);
EXPORT_SYMBOL(pcibios_read_config_byte);
EXPORT_SYMBOL(pcibios_read_config_word);
EXPORT_SYMBOL(pcibios_read_config_dword);
EXPORT_SYMBOL(pcibios_write_config_byte);
EXPORT_SYMBOL(pcibios_write_config_word);
EXPORT_SYMBOL(pcibios_write_config_dword);
EXPORT_SYMBOL(pcibios_strerror);
EXPORT_SYMBOL(pci_strvendor);
EXPORT_SYMBOL(pci_strdev);
#endif

/* process memory management */
EXPORT_SYMBOL(do_mmap);
EXPORT_SYMBOL(do_munmap);
EXPORT_SYMBOL(exit_mm);
EXPORT_SYMBOL(exit_files);

/* internal kernel memory management */
EXPORT_SYMBOL(__get_free_pages);
EXPORT_SYMBOL(free_pages);
EXPORT_SYMBOL(kmem_find_general_cachep);
EXPORT_SYMBOL(kmem_cache_create);
EXPORT_SYMBOL(kmem_cache_shrink);
EXPORT_SYMBOL(kmem_cache_alloc);
EXPORT_SYMBOL(kmem_cache_free);
EXPORT_SYMBOL(kmalloc);
EXPORT_SYMBOL(kfree);
EXPORT_SYMBOL(kfree_s);
EXPORT_SYMBOL(vmalloc);
EXPORT_SYMBOL(vfree);
EXPORT_SYMBOL(mem_map);
EXPORT_SYMBOL(remap_page_range);
EXPORT_SYMBOL(max_mapnr);
EXPORT_SYMBOL(num_physpages);
EXPORT_SYMBOL(high_memory);
EXPORT_SYMBOL(update_vm_cache);
EXPORT_SYMBOL(vmtruncate);

/* filesystem internal functions */
EXPORT_SYMBOL(getname);
EXPORT_SYMBOL(putname);
EXPORT_SYMBOL(__fput);
EXPORT_SYMBOL(iget);
EXPORT_SYMBOL(iput);
EXPORT_SYMBOL(__namei);
EXPORT_SYMBOL(lookup_dentry);
EXPORT_SYMBOL(open_namei);
EXPORT_SYMBOL(sys_close);
EXPORT_SYMBOL(close_fp);
EXPORT_SYMBOL(d_alloc_root);
EXPORT_SYMBOL(d_delete);
EXPORT_SYMBOL(d_validate);
EXPORT_SYMBOL(d_add);
EXPORT_SYMBOL(d_move);
EXPORT_SYMBOL(d_instantiate);
EXPORT_SYMBOL(d_alloc);
EXPORT_SYMBOL(d_lookup);
EXPORT_SYMBOL(__mark_inode_dirty);
EXPORT_SYMBOL(init_private_file);
EXPORT_SYMBOL(insert_file_free);
EXPORT_SYMBOL(check_disk_change);
EXPORT_SYMBOL(invalidate_buffers);
EXPORT_SYMBOL(invalidate_inodes);
EXPORT_SYMBOL(invalidate_inode_pages);
EXPORT_SYMBOL(fsync_dev);
EXPORT_SYMBOL(permission);
EXPORT_SYMBOL(inode_setattr);
EXPORT_SYMBOL(inode_change_ok);
EXPORT_SYMBOL(write_inode_now);
EXPORT_SYMBOL(notify_change);
EXPORT_SYMBOL(get_hardblocksize);
EXPORT_SYMBOL(set_blocksize);
EXPORT_SYMBOL(getblk);
EXPORT_SYMBOL(bread);
EXPORT_SYMBOL(breada);
EXPORT_SYMBOL(__brelse);
EXPORT_SYMBOL(__bforget);
EXPORT_SYMBOL(ll_rw_block);
EXPORT_SYMBOL(__wait_on_buffer);
EXPORT_SYMBOL(mark_buffer_uptodate);
EXPORT_SYMBOL(unlock_buffer);
EXPORT_SYMBOL(add_blkdev_randomness);
EXPORT_SYMBOL(generic_file_read);
EXPORT_SYMBOL(generic_file_write);
EXPORT_SYMBOL(generic_file_mmap);
EXPORT_SYMBOL(generic_readpage);
EXPORT_SYMBOL(file_lock_table);
EXPORT_SYMBOL(posix_lock_file);
EXPORT_SYMBOL(posix_test_lock);
EXPORT_SYMBOL(posix_block_lock);
EXPORT_SYMBOL(posix_unblock_lock);
EXPORT_SYMBOL(dput);

#if !defined(CONFIG_NFSD) && defined(CONFIG_NFSD_MODULE)
EXPORT_SYMBOL(do_nfsservctl);
#endif

/* device registration */
EXPORT_SYMBOL(register_chrdev);
EXPORT_SYMBOL(unregister_chrdev);
EXPORT_SYMBOL(register_blkdev);
EXPORT_SYMBOL(unregister_blkdev);
EXPORT_SYMBOL(tty_register_driver);
EXPORT_SYMBOL(tty_unregister_driver);
EXPORT_SYMBOL(tty_std_termios);

/* block device driver support */
EXPORT_SYMBOL(block_read);
EXPORT_SYMBOL(block_write);
EXPORT_SYMBOL(block_fsync);
EXPORT_SYMBOL(wait_for_request);
EXPORT_SYMBOL(blksize_size);
EXPORT_SYMBOL(hardsect_size);
EXPORT_SYMBOL(blk_size);
EXPORT_SYMBOL(blk_dev);
EXPORT_SYMBOL(is_read_only);
EXPORT_SYMBOL(set_device_ro);
EXPORT_SYMBOL(bmap);
EXPORT_SYMBOL(sync_dev);
EXPORT_SYMBOL(get_blkfops);
EXPORT_SYMBOL(blkdev_open);
EXPORT_SYMBOL(blkdev_release);
EXPORT_SYMBOL(gendisk_head);
EXPORT_SYMBOL(resetup_one_dev);
EXPORT_SYMBOL(unplug_device);

/* tty routines */
EXPORT_SYMBOL(tty_hangup);
EXPORT_SYMBOL(tty_wait_until_sent);
EXPORT_SYMBOL(tty_check_change);
EXPORT_SYMBOL(tty_hung_up_p);
EXPORT_SYMBOL(do_SAK);
EXPORT_SYMBOL(console_print);

/* filesystem registration */
EXPORT_SYMBOL(register_filesystem);
EXPORT_SYMBOL(unregister_filesystem);

/* executable format registration */
EXPORT_SYMBOL(register_binfmt);
EXPORT_SYMBOL(unregister_binfmt);
EXPORT_SYMBOL(search_binary_handler);
EXPORT_SYMBOL(prepare_binprm);
EXPORT_SYMBOL(remove_arg_zero);

/* execution environment registration */
EXPORT_SYMBOL(lookup_exec_domain);
EXPORT_SYMBOL(register_exec_domain);
EXPORT_SYMBOL(unregister_exec_domain);

/* sysctl table registration */
EXPORT_SYMBOL(register_sysctl_table);
EXPORT_SYMBOL(unregister_sysctl_table);
EXPORT_SYMBOL(sysctl_string);
EXPORT_SYMBOL(sysctl_intvec);
EXPORT_SYMBOL(proc_dostring);
EXPORT_SYMBOL(proc_dointvec);
EXPORT_SYMBOL(proc_dointvec_jiffies);
EXPORT_SYMBOL(proc_dointvec_minmax);

/* interrupt handling */
EXPORT_SYMBOL(request_irq);
EXPORT_SYMBOL(free_irq);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(probe_irq_on);
EXPORT_SYMBOL(probe_irq_off);
EXPORT_SYMBOL(bh_active);
EXPORT_SYMBOL(bh_mask);
EXPORT_SYMBOL(bh_mask_count);
EXPORT_SYMBOL(bh_base);
EXPORT_SYMBOL(add_timer);
EXPORT_SYMBOL(del_timer);
EXPORT_SYMBOL(tq_timer);
EXPORT_SYMBOL(tq_immediate);
EXPORT_SYMBOL(tq_scheduler);
EXPORT_SYMBOL(timer_active);
EXPORT_SYMBOL(timer_table);

#ifdef __SMP__
/* Various random spinlocks we want to export */
EXPORT_SYMBOL(tqueue_lock);
EXPORT_SYMBOL(waitqueue_lock);
#endif

/* autoirq from  drivers/net/auto_irq.c */
EXPORT_SYMBOL(autoirq_setup);
EXPORT_SYMBOL(autoirq_report);

/* dma handling */
EXPORT_SYMBOL(request_dma);
EXPORT_SYMBOL(free_dma);
#ifdef HAVE_DISABLE_HLT
EXPORT_SYMBOL(disable_hlt);
EXPORT_SYMBOL(enable_hlt);
#endif

/* IO port handling */
EXPORT_SYMBOL(check_region);
EXPORT_SYMBOL(request_region);
EXPORT_SYMBOL(release_region);

/* process management */
EXPORT_SYMBOL(wake_up);
EXPORT_SYMBOL(wake_up_interruptible);
EXPORT_SYMBOL(sleep_on);
EXPORT_SYMBOL(interruptible_sleep_on);
EXPORT_SYMBOL(schedule);
EXPORT_SYMBOL(jiffies);
EXPORT_SYMBOL(xtime);
EXPORT_SYMBOL(do_gettimeofday);
EXPORT_SYMBOL(loops_per_sec);
EXPORT_SYMBOL(need_resched);
EXPORT_SYMBOL(kstat);
EXPORT_SYMBOL(kill_proc);
EXPORT_SYMBOL(kill_pg);
EXPORT_SYMBOL(kill_sl);

/* misc */
EXPORT_SYMBOL(panic);
EXPORT_SYMBOL(printk);
EXPORT_SYMBOL(sprintf);
EXPORT_SYMBOL(vsprintf);
EXPORT_SYMBOL(kdevname);
EXPORT_SYMBOL(simple_strtoul);
EXPORT_SYMBOL(system_utsname);
EXPORT_SYMBOL(sys_call_table);
EXPORT_SYMBOL(machine_restart);
EXPORT_SYMBOL(machine_halt);
EXPORT_SYMBOL(machine_power_off);
EXPORT_SYMBOL(register_reboot_notifier);
EXPORT_SYMBOL(unregister_reboot_notifier);
EXPORT_SYMBOL(_ctype);
EXPORT_SYMBOL(secure_tcp_sequence_number);
EXPORT_SYMBOL(get_random_bytes);

/* Signal interfaces */
EXPORT_SYMBOL(send_sig);

/* Program loader interfaces */
EXPORT_SYMBOL(setup_arg_pages);
EXPORT_SYMBOL(copy_strings);
EXPORT_SYMBOL(do_execve);
EXPORT_SYMBOL(flush_old_exec);
EXPORT_SYMBOL(open_dentry);
EXPORT_SYMBOL(read_exec);

/* Miscellaneous access points */
EXPORT_SYMBOL(si_meminfo);

/* Added to make file system as module */
EXPORT_SYMBOL(get_super);
EXPORT_SYMBOL(set_writetime);
EXPORT_SYMBOL(sys_tz);
EXPORT_SYMBOL(__wait_on_super);
EXPORT_SYMBOL(file_fsync);
EXPORT_SYMBOL(clear_inode);
EXPORT_SYMBOL(refile_buffer);
EXPORT_SYMBOL(nr_async_pages);
EXPORT_SYMBOL(___strtok);
EXPORT_SYMBOL(init_fifo);
EXPORT_SYMBOL(super_blocks);
EXPORT_SYMBOL(fifo_inode_operations);
EXPORT_SYMBOL(chrdev_inode_operations);
EXPORT_SYMBOL(blkdev_inode_operations);
EXPORT_SYMBOL(read_ahead);
EXPORT_SYMBOL(get_hash_table);
EXPORT_SYMBOL(get_empty_inode);
EXPORT_SYMBOL(insert_inode_hash);
EXPORT_SYMBOL(event);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(securelevel);

/* all busmice */
EXPORT_SYMBOL(add_mouse_randomness);
EXPORT_SYMBOL(fasync_helper);

/* psaux mouse */
EXPORT_SYMBOL(aux_device_present);
#ifdef CONFIG_VT
EXPORT_SYMBOL(kbd_read_mask);
#endif

#ifdef CONFIG_BLK_DEV_MD
EXPORT_SYMBOL(disk_name);	/* for md.c */
#endif
 	
/* binfmt_aout */
EXPORT_SYMBOL(get_write_access);
EXPORT_SYMBOL(put_write_access);
