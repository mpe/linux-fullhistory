#include <linux/module.h>
#include <linux/smp.h>


static struct symbol_table arch_symbol_table = {
#include <linux/symtab_begin.h>
	/* platform dependent support */
#ifdef __SMP__
	X(apic_reg),		/* Needed internally for the I386 inlines */
	X(cpu_data),
#endif
#include <linux/symtab_end.h>
};

void arch_syms_export(void)
{
	register_symtab(&arch_symbol_table);
}
