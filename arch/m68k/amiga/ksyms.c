#include <linux/module.h>
#include <asm/zorro.h>
#include <asm/amigatypes.h>
#include <asm/amigahw.h>

static struct symbol_table mach_amiga_symbol_table = {
#include <linux/symtab_begin.h>

  /*
   * Add things here when you find the need for it.
   */
  X(amiga_colorclock),
  X(amiga_chip_alloc),
  X(amiga_chip_free),
  X(amiga_chip_avail),

  X(zorro_find),
  X(zorro_get_board),
  X(zorro_config_board),
  X(zorro_unconfig_board),
  X(zorro_unused_z2ram),

  /* example
  X(something_you_need),
  */


#include <linux/symtab_end.h>
};

void mach_amiga_syms_export(void)
{
	register_symtab(&mach_amiga_symbol_table);
}
