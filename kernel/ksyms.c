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
#include <linux/malloc.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <linux/kernel_stat.h>
#include <linux/vmalloc.h>
#include <linux/sys.h>
#include <linux/utsname.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/serial.h>
#include <linux/locks.h>
#include <linux/delay.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>
#include <linux/hdreg.h>
#include <linux/skbuff.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/swap.h>
#include <linux/ctype.h>
#include <linux/file.h>
#include <linux/iobuf.h>
#include <linux/console.h>
#include <linux/poll.h>
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/capability.h>
#include <linux/highuid.h>

#if defined(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
#endif
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

extern int console_loglevel;
extern void set_device_ro(kdev_t dev,int flag);
#if !defined(CONFIG_NFSD) && defined(CONFIG_NFSD_MODULE)
extern int (*do_nfsservctl)(int, void *, void *);
#endif

extern void *sys_call_table;

extern int sys_tz;
extern int request_dma(unsigned int dmanr, char * deviceID);
extern void free_dma(unsigned int dmanr);
extern spinlock_t dma_spin_lock;

#ifdef CONFIG_MODVERSIONS
const struct module_symbol __export_Using_Versions
__attribute__((section("__ksymtab"))) = {
	1 /* Version version */, "Using_Versions"
};
#endif


#ifdef CONFIG_KMOD
EXPORT_SYMBOL(request_module);
#endif

#ifdef CONFIG_MODULES
EXPORT_SYMBOL(get_module_symbol);
#endif
EXPORT_SYMBOL(get_option);
EXPORT_SYMBOL(get_options);

/* process memory management */
EXPORT_SYMBOL(do_mmap_pgoff);
EXPORT_SYMBOL(do_munmap);
EXPORT_SYMBOL(do_brk);
EXPORT_SYMBOL(exit_mm);
EXPORT_SYMBOL(exit_files);
EXPORT_SYMBOL(exit_fs);
EXPORT_SYMBOL(exit_sighand);

/* internal kernel memory management */
EXPORT_SYMBOL(__alloc_pages);
EXPORT_SYMBOL(alloc_pages_node);
EXPORT_SYMBOL(__free_pages_ok);
#ifndef CONFIG_DISCONTIGMEM
EXPORT_SYMBOL(contig_page_data);
#endif
EXPORT_SYMBOL(num_physpages);
EXPORT_SYMBOL(kmem_find_general_cachep);
EXPORT_SYMBOL(kmem_cache_create);
EXPORT_SYMBOL(kmem_cache_destroy);
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
EXPORT_SYMBOL(high_memory);
EXPORT_SYMBOL(vmtruncate);
EXPORT_SYMBOL(find_vma);
EXPORT_SYMBOL(get_unmapped_area);
EXPORT_SYMBOL(init_mm);
#ifdef CONFIG_HIGHMEM
EXPORT_SYMBOL(kmap_high);
EXPORT_SYMBOL(kunmap_high);
EXPORT_SYMBOL(highmem_start_page);
#endif

/* filesystem internal functions */
EXPORT_SYMBOL(in_group_p);
EXPORT_SYMBOL(update_atime);
EXPORT_SYMBOL(get_super);
EXPORT_SYMBOL(get_fs_type);
EXPORT_SYMBOL(get_empty_super);
EXPORT_SYMBOL(remove_vfsmnt);
EXPORT_SYMBOL(getname);
EXPORT_SYMBOL(_fput);
EXPORT_SYMBOL(igrab);
EXPORT_SYMBOL(iunique);
EXPORT_SYMBOL(iget4);
EXPORT_SYMBOL(iput);
EXPORT_SYMBOL(__namei);
EXPORT_SYMBOL(lookup_dentry);
EXPORT_SYMBOL(open_namei);
EXPORT_SYMBOL(sys_close);
EXPORT_SYMBOL(d_alloc_root);
EXPORT_SYMBOL(d_delete);
EXPORT_SYMBOL(d_validate);
EXPORT_SYMBOL(d_rehash);
EXPORT_SYMBOL(d_invalidate);	/* May be it will be better in dcache.h? */
EXPORT_SYMBOL(d_move);
EXPORT_SYMBOL(d_instantiate);
EXPORT_SYMBOL(d_alloc);
EXPORT_SYMBOL(d_lookup);
EXPORT_SYMBOL(d_path);
EXPORT_SYMBOL(__mark_buffer_dirty);
EXPORT_SYMBOL(__mark_inode_dirty);
EXPORT_SYMBOL(free_kiovec);
EXPORT_SYMBOL(brw_kiovec);
EXPORT_SYMBOL(alloc_kiovec);
EXPORT_SYMBOL(get_empty_filp);
EXPORT_SYMBOL(init_private_file);
EXPORT_SYMBOL(filp_open);
EXPORT_SYMBOL(filp_close);
EXPORT_SYMBOL(put_filp);
EXPORT_SYMBOL(files_lock);
EXPORT_SYMBOL(check_disk_change);
EXPORT_SYMBOL(invalidate_buffers);
EXPORT_SYMBOL(invalidate_inodes);
EXPORT_SYMBOL(invalidate_inode_pages);
EXPORT_SYMBOL(truncate_inode_pages);
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
EXPORT_SYMBOL(___wait_on_page);
EXPORT_SYMBOL(block_read_full_page);
EXPORT_SYMBOL(block_write_full_page);
EXPORT_SYMBOL(block_write_partial_page);
EXPORT_SYMBOL(block_write_cont_page);
EXPORT_SYMBOL(block_write_zero_range);
EXPORT_SYMBOL(generic_file_read);
EXPORT_SYMBOL(do_generic_file_read);
EXPORT_SYMBOL(generic_file_write);
EXPORT_SYMBOL(generic_file_mmap);
EXPORT_SYMBOL(generic_buffer_fdatasync);
EXPORT_SYMBOL(page_hash_bits);
EXPORT_SYMBOL(page_hash_table);
EXPORT_SYMBOL(file_lock_table);
EXPORT_SYMBOL(posix_lock_file);
EXPORT_SYMBOL(posix_test_lock);
EXPORT_SYMBOL(posix_block_lock);
EXPORT_SYMBOL(posix_unblock_lock);
EXPORT_SYMBOL(locks_mandatory_area);
EXPORT_SYMBOL(dput);
EXPORT_SYMBOL(is_root_busy);
EXPORT_SYMBOL(prune_dcache);
EXPORT_SYMBOL(shrink_dcache_sb);
EXPORT_SYMBOL(shrink_dcache_parent);
EXPORT_SYMBOL(find_inode_number);
EXPORT_SYMBOL(is_subdir);
EXPORT_SYMBOL(get_unused_fd);
EXPORT_SYMBOL(vfs_rmdir);
EXPORT_SYMBOL(vfs_unlink);
EXPORT_SYMBOL(vfs_rename);
EXPORT_SYMBOL(__pollwait);
EXPORT_SYMBOL(ROOT_DEV);
EXPORT_SYMBOL(__find_get_page);
EXPORT_SYMBOL(__find_lock_page);
EXPORT_SYMBOL(grab_cache_page);
EXPORT_SYMBOL(read_cache_page);
EXPORT_SYMBOL(vfs_readlink);
EXPORT_SYMBOL(vfs_follow_link);
EXPORT_SYMBOL(page_readlink);
EXPORT_SYMBOL(page_follow_link);
EXPORT_SYMBOL(block_symlink);

/* for stackable file systems (lofs, wrapfs, etc.) */
EXPORT_SYMBOL(add_to_page_cache);
EXPORT_SYMBOL(filemap_nopage);
EXPORT_SYMBOL(filemap_swapout);
EXPORT_SYMBOL(filemap_sync);
EXPORT_SYMBOL(remove_inode_page);

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
EXPORT_SYMBOL(wait_for_request);
EXPORT_SYMBOL(blksize_size);
EXPORT_SYMBOL(hardsect_size);
EXPORT_SYMBOL(blk_size);
EXPORT_SYMBOL(blk_dev);
EXPORT_SYMBOL(is_read_only);
EXPORT_SYMBOL(set_device_ro);
EXPORT_SYMBOL(bmap);
EXPORT_SYMBOL(sync_dev);
EXPORT_SYMBOL(blkdev_open);
EXPORT_SYMBOL(blkdev_get);
EXPORT_SYMBOL(blkdev_put);
EXPORT_SYMBOL(ioctl_by_bdev);
EXPORT_SYMBOL(gendisk_head);
EXPORT_SYMBOL(grok_partitions);
EXPORT_SYMBOL(register_disk);
EXPORT_SYMBOL(unplug_device);
EXPORT_SYMBOL(make_request);
EXPORT_SYMBOL(tq_disk);
EXPORT_SYMBOL(init_buffer);
EXPORT_SYMBOL(refile_buffer);
EXPORT_SYMBOL(max_sectors);
EXPORT_SYMBOL(max_readahead);
EXPORT_SYMBOL(file_moveto);

/* tty routines */
EXPORT_SYMBOL(tty_hangup);
EXPORT_SYMBOL(tty_wait_until_sent);
EXPORT_SYMBOL(tty_check_change);
EXPORT_SYMBOL(tty_hung_up_p);
EXPORT_SYMBOL(tty_flip_buffer_push);
EXPORT_SYMBOL(tty_get_baud_rate);
EXPORT_SYMBOL(do_SAK);
EXPORT_SYMBOL(console_print);
EXPORT_SYMBOL(console_loglevel);

/* filesystem registration */
EXPORT_SYMBOL(register_filesystem);
EXPORT_SYMBOL(unregister_filesystem);

/* executable format registration */
EXPORT_SYMBOL(register_binfmt);
EXPORT_SYMBOL(unregister_binfmt);
EXPORT_SYMBOL(search_binary_handler);
EXPORT_SYMBOL(prepare_binprm);
EXPORT_SYMBOL(compute_creds);
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
EXPORT_SYMBOL(proc_doulongvec_ms_jiffies_minmax);
EXPORT_SYMBOL(proc_doulongvec_minmax);

/* interrupt handling */
EXPORT_SYMBOL(request_irq);
EXPORT_SYMBOL(free_irq);
EXPORT_SYMBOL(probe_irq_on);
EXPORT_SYMBOL(probe_irq_off);
EXPORT_SYMBOL(bh_active);
EXPORT_SYMBOL(bh_mask);
EXPORT_SYMBOL(bh_mask_count);
EXPORT_SYMBOL(bh_base);
EXPORT_SYMBOL(add_timer);
EXPORT_SYMBOL(del_timer);
EXPORT_SYMBOL(mod_timer);
EXPORT_SYMBOL(tq_timer);
EXPORT_SYMBOL(tq_immediate);
EXPORT_SYMBOL(tq_scheduler);
EXPORT_SYMBOL(timer_active);
EXPORT_SYMBOL(timer_table);

#ifdef __SMP__
/* Various random spinlocks we want to export */
EXPORT_SYMBOL(tqueue_lock);
#endif

/* autoirq from  drivers/net/auto_irq.c */
EXPORT_SYMBOL(autoirq_setup);
EXPORT_SYMBOL(autoirq_report);

/* dma handling */
EXPORT_SYMBOL(request_dma);
EXPORT_SYMBOL(free_dma);
EXPORT_SYMBOL(dma_spin_lock);
#ifdef HAVE_DISABLE_HLT
EXPORT_SYMBOL(disable_hlt);
EXPORT_SYMBOL(enable_hlt);
#endif

/* resource handling */
EXPORT_SYMBOL(request_resource);
EXPORT_SYMBOL(release_resource);
EXPORT_SYMBOL(allocate_resource);
EXPORT_SYMBOL(__request_region);
EXPORT_SYMBOL(__check_region);
EXPORT_SYMBOL(__release_region);
EXPORT_SYMBOL(ioport_resource);
EXPORT_SYMBOL(iomem_resource);

/* process management */
EXPORT_SYMBOL(__wake_up);
EXPORT_SYMBOL(sleep_on);
EXPORT_SYMBOL(sleep_on_timeout);
EXPORT_SYMBOL(interruptible_sleep_on);
EXPORT_SYMBOL(interruptible_sleep_on_timeout);
EXPORT_SYMBOL(schedule);
EXPORT_SYMBOL(schedule_timeout);
EXPORT_SYMBOL(jiffies);
EXPORT_SYMBOL(xtime);
EXPORT_SYMBOL(do_gettimeofday);
EXPORT_SYMBOL(loops_per_sec);
EXPORT_SYMBOL(kstat);

/* misc */
EXPORT_SYMBOL(panic);
EXPORT_SYMBOL(printk);
EXPORT_SYMBOL(sprintf);
EXPORT_SYMBOL(vsprintf);
EXPORT_SYMBOL(kdevname);
EXPORT_SYMBOL(bdevname);
EXPORT_SYMBOL(cdevname);
EXPORT_SYMBOL(partition_name);		/* md.c only */
EXPORT_SYMBOL(simple_strtoul);
EXPORT_SYMBOL(system_utsname);	/* UTS data */
EXPORT_SYMBOL(uts_sem);		/* UTS semaphore */
EXPORT_SYMBOL(sys_call_table);
EXPORT_SYMBOL(machine_restart);
EXPORT_SYMBOL(machine_halt);
EXPORT_SYMBOL(machine_power_off);
EXPORT_SYMBOL(register_reboot_notifier);
EXPORT_SYMBOL(unregister_reboot_notifier);
EXPORT_SYMBOL(_ctype);
EXPORT_SYMBOL(secure_tcp_sequence_number);
EXPORT_SYMBOL(get_random_bytes);
EXPORT_SYMBOL(securebits);
EXPORT_SYMBOL(cap_bset);
EXPORT_SYMBOL(daemonize);

/* Program loader interfaces */
EXPORT_SYMBOL(setup_arg_pages);
EXPORT_SYMBOL(copy_strings_kernel);
EXPORT_SYMBOL(do_execve);
EXPORT_SYMBOL(flush_old_exec);
EXPORT_SYMBOL(open_dentry);
EXPORT_SYMBOL(read_exec);

/* Miscellaneous access points */
EXPORT_SYMBOL(si_meminfo);

/* Added to make file system as module */
EXPORT_SYMBOL(sys_tz);
EXPORT_SYMBOL(__wait_on_super);
EXPORT_SYMBOL(file_fsync);
EXPORT_SYMBOL(clear_inode);
EXPORT_SYMBOL(nr_async_pages);
EXPORT_SYMBOL(___strtok);
EXPORT_SYMBOL(init_special_inode);
EXPORT_SYMBOL(read_ahead);
EXPORT_SYMBOL(get_hash_table);
EXPORT_SYMBOL(get_empty_inode);
EXPORT_SYMBOL(insert_inode_hash);
EXPORT_SYMBOL(remove_inode_hash);
EXPORT_SYMBOL(make_bad_inode);
EXPORT_SYMBOL(is_bad_inode);
EXPORT_SYMBOL(event);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_interruptible);
EXPORT_SYMBOL(__down_trylock);
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(brw_page);

#ifdef CONFIG_UID16
EXPORT_SYMBOL(overflowuid);
EXPORT_SYMBOL(overflowgid);
#endif
EXPORT_SYMBOL(fs_overflowuid);
EXPORT_SYMBOL(fs_overflowgid);

/* all busmice */
EXPORT_SYMBOL(fasync_helper);

#ifdef CONFIG_BLK_DEV_MD
EXPORT_SYMBOL(disk_name);	/* for md.c */
#endif

/* binfmt_aout */
EXPORT_SYMBOL(get_write_access);
EXPORT_SYMBOL(put_write_access);

/* dynamic registering of consoles */
EXPORT_SYMBOL(register_console);
EXPORT_SYMBOL(unregister_console);

/* time */
EXPORT_SYMBOL(get_fast_time);

/* library functions */
EXPORT_SYMBOL(strnicmp);

/* init task, for moving kthread roots - ought to export a function ?? */
EXPORT_SYMBOL(init_task_union);
