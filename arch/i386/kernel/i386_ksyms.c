#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/mca.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/hardirq.h>

extern void dump_thread(struct pt_regs *, struct user *);
extern int dump_fpu(elf_fpregset_t *);
extern void __lock_kernel(void);

/* platform dependent support */
EXPORT_SYMBOL(EISA_bus);
EXPORT_SYMBOL(MCA_bus);
EXPORT_SYMBOL(wp_works_ok);
EXPORT_SYMBOL(__verify_write);
EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(local_irq_count);
EXPORT_SYMBOL_NOVERS(__down_failed);
EXPORT_SYMBOL_NOVERS(__down_failed_interruptible);
EXPORT_SYMBOL_NOVERS(__up_wakeup);
EXPORT_SYMBOL(__intel_bh_counter);
/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy);

#ifdef __SMP__
EXPORT_SYMBOL(apic_reg);	/* Needed internally for the I386 inlines */
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(kernel_flag);
EXPORT_SYMBOL(active_kernel_processor);
EXPORT_SYMBOL(smp_invalidate_needed);
EXPORT_SYMBOL_NOVERS(__lock_kernel);

/* Global SMP irq stuff */
EXPORT_SYMBOL(global_irq_holder);
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);
#endif

#ifdef CONFIG_MCA
/* Adapter probing and info methods. */
EXPORT_SYMBOL(mca_find_adapter);
EXPORT_SYMBOL(mca_write_pos);
EXPORT_SYMBOL(mca_read_pos);
EXPORT_SYMBOL(mca_read_stored_pos);
EXPORT_SYMBOL(mca_set_adapter_name);
EXPORT_SYMBOL(mca_get_adapter_name);
EXPORT_SYMBOL(mca_set_adapter_procfn);
EXPORT_SYMBOL(mca_isenabled);
EXPORT_SYMBOL(mca_isadapter);
#endif
