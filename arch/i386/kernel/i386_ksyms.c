#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/mca.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/acpi.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/hardirq.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/mmx.h>

extern void dump_thread(struct pt_regs *, struct user *);
extern int dump_fpu(elf_fpregset_t *);

#ifdef CONFIG_SMP
extern void FASTCALL( __write_lock_failed(rwlock_t *rw));
extern void FASTCALL( __read_lock_failed(rwlock_t *rw));
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_HD) || defined(CONFIG_BLK_DEV_IDE_MODULE) || defined(CONFIG_BLK_DEV_HD_MODULE)
extern struct drive_info_struct drive_info;
EXPORT_SYMBOL(drive_info);
#endif

/* platform dependent support */
EXPORT_SYMBOL(boot_cpu_data);
EXPORT_SYMBOL(EISA_bus);
EXPORT_SYMBOL(MCA_bus);
EXPORT_SYMBOL(__verify_write);
EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(__io_virt_debug);
EXPORT_SYMBOL(local_bh_count);
EXPORT_SYMBOL(local_irq_count);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(disable_irq_nosync);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(acpi_idle);
EXPORT_SYMBOL(acpi_power_off);

EXPORT_SYMBOL_NOVERS(__down_failed);
EXPORT_SYMBOL_NOVERS(__down_failed_interruptible);
EXPORT_SYMBOL_NOVERS(__down_failed_trylock);
EXPORT_SYMBOL_NOVERS(__up_wakeup);
EXPORT_SYMBOL_NOVERS(__down_write_failed);
EXPORT_SYMBOL_NOVERS(__down_read_failed);
EXPORT_SYMBOL_NOVERS(__rwsem_wake);
/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy);
EXPORT_SYMBOL(csum_partial_copy_generic);
/* Delay loops */
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(__const_udelay);

EXPORT_SYMBOL_NOVERS(__get_user_1);
EXPORT_SYMBOL_NOVERS(__get_user_2);
EXPORT_SYMBOL_NOVERS(__get_user_4);
EXPORT_SYMBOL_NOVERS(__put_user_1);
EXPORT_SYMBOL_NOVERS(__put_user_2);
EXPORT_SYMBOL_NOVERS(__put_user_4);

EXPORT_SYMBOL(strtok);
EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strstr);

EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(clear_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__generic_copy_from_user);
EXPORT_SYMBOL(__generic_copy_to_user);
EXPORT_SYMBOL(strnlen_user);

#ifdef CONFIG_X86_USE_3DNOW
EXPORT_SYMBOL(_mmx_memcpy);
EXPORT_SYMBOL(mmx_clear_page);
EXPORT_SYMBOL(mmx_copy_page);
#endif

#ifdef CONFIG_SMP
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(kernel_flag);
EXPORT_SYMBOL(smp_num_cpus);
EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL_NOVERS(__write_lock_failed);
EXPORT_SYMBOL_NOVERS(__read_lock_failed);

/* Global SMP irq stuff */
EXPORT_SYMBOL(synchronize_irq);
EXPORT_SYMBOL(synchronize_bh);
EXPORT_SYMBOL(global_bh_count);
EXPORT_SYMBOL(global_bh_lock);
EXPORT_SYMBOL(global_irq_holder);
EXPORT_SYMBOL(i386_bh_lock);
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);
EXPORT_SYMBOL(smp_call_function);
#endif

#ifdef CONFIG_MCA
/* Adapter probing and info methods. */
EXPORT_SYMBOL(machine_id);
EXPORT_SYMBOL(mca_find_adapter);
EXPORT_SYMBOL(mca_write_pos);
EXPORT_SYMBOL(mca_read_pos);
EXPORT_SYMBOL(mca_read_stored_pos);
EXPORT_SYMBOL(mca_set_adapter_name);
EXPORT_SYMBOL(mca_get_adapter_name);
EXPORT_SYMBOL(mca_set_adapter_procfn);
EXPORT_SYMBOL(mca_isenabled);
EXPORT_SYMBOL(mca_isadapter);
EXPORT_SYMBOL(mca_mark_as_used);
EXPORT_SYMBOL(mca_mark_as_unused);
EXPORT_SYMBOL(mca_find_unused_adapter);
EXPORT_SYMBOL(mca_is_adapter_used);
#endif

#ifdef CONFIG_VT
EXPORT_SYMBOL(screen_info);
#endif

EXPORT_SYMBOL(get_wchan);
