#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/mca.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>

extern void dump_thread(struct pt_regs *, struct user *);
extern int dump_fpu(elf_fpregset_t *);

/* platform dependent support */
EXPORT_SYMBOL(EISA_bus);
EXPORT_SYMBOL(MCA_bus);
EXPORT_SYMBOL(wp_works_ok);
EXPORT_SYMBOL(__verify_write);
EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(ioremap);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL_NOVERS(__down_failed);
EXPORT_SYMBOL_NOVERS(__up_wakeup);

#ifdef __SMP__
EXPORT_SYMBOL(apic_reg);	/* Needed internally for the I386 inlines */
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(syscall_count);
EXPORT_SYMBOL(kernel_flag);
EXPORT_SYMBOL(kernel_counter);
EXPORT_SYMBOL(active_kernel_processor);
EXPORT_SYMBOL(smp_invalidate_needed);
#endif

#ifdef CONFIG_MCA
/* Adapter probing and info methods. */
EXPORT_SYMBOL(mca_write_pos);
EXPORT_SYMBOL(mca_read_pos);
EXPORT_SYMBOL(mca_read_stored_pos);
EXPORT_SYMBOL(mca_set_adapter_name);
EXPORT_SYMBOL(mca_get_adapter_name);
EXPORT_SYMBOL(mca_set_adapter_procfn);
EXPORT_SYMBOL(mca_isenabled);
EXPORT_SYMBOL(mca_isadapter);
#endif
