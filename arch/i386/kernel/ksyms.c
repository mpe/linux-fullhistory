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

static struct symbol_table arch_symbol_table = {
#include <linux/symtab_begin.h>
	/* platform dependent support */
	X(EISA_bus),
	X(wp_works_ok),
	X(__verify_write),
	X(dump_thread),
	X(dump_fpu),
	X(ioremap),
	X(iounmap),
	XNOVERS(__down_failed),
	XNOVERS(__up_wakeup),
#ifdef __SMP__
	X(apic_reg),		/* Needed internally for the I386 inlines */
	X(cpu_data),
	X(syscall_count),
	X(kernel_flag),
	X(kernel_counter),
	X(active_kernel_processor),
	X(smp_invalidate_needed),
#endif
#ifdef CONFIG_MCA
	/* Adapter probing and info methods. */
	X(mca_write_pos),
	X(mca_read_pos),
	X(mca_read_stored_pos),
	X(mca_set_adapter_name),
	X(mca_get_adapter_name),
	X(mca_set_adapter_procfn),
	X(mca_isenabled),
	X(mca_isadapter),
#endif
#include <linux/symtab_end.h>
};

void arch_syms_export(void)
{
	register_symtab(&arch_symbol_table);
}
