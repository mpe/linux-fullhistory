#include <linux/module.h>

static struct symbol_table mach_amiga_symbol_table = {
#include <linux/symtab_begin.h>

  /*
   * Add things here when you find the need for it.
   */

  /* example
  X(something_you_need),
  */

#include <linux/symtab_end.h>
};

void mach_amiga_syms_export(void)
{
	register_symtab(&mach_amiga_symbol_table);
}
