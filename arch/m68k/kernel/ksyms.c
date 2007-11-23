#include <linux/config.h>
#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/string.h>
#include <linux/mm.h>

#include <asm/bootinfo.h>
#include <asm/pgtable.h>

asmlinkage long long __ashrdi3 (long long, int);
extern char m68k_debug_device[];

#ifdef CONFIG_ATARI
extern void mach_atari_syms_export (void);
#endif
#ifdef CONFIG_AMIGA
extern void mach_amiga_syms_export (void);
#endif
#ifdef CONFIG_MAC
extern void mach_mac_syms_export (void);
#endif

static struct symbol_table arch_symbol_table = {
#include <linux/symtab_begin.h>
	/* platform dependent support */

	X(memcmp),
	X(boot_info),
	X(m68k_is040or060),
	X(cache_push),
	X(cache_clear),
	X(mm_vtop),
	X(mm_ptov),
	X(m68k_debug_device),

	/* The following are special because they're not called
	   explicitly (the C compiler generates them).  Fortunately,
	   their interface isn't gonna change any time soon now, so
	   it's OK to leave it out of version control.  */
	XNOVERS(__ashrdi3),
	XNOVERS(memcpy),
	
#include <linux/symtab_end.h>
};

void arch_syms_export(void)
{
	register_symtab(&arch_symbol_table);

	switch (boot_info.machtype) {
#ifdef CONFIG_ATARI
	case MACH_ATARI:
		mach_atari_syms_export();
		break;
#endif
#ifdef CONFIG_AMIGA
	case MACH_AMIGA:
		mach_amiga_syms_export();
		break;
#endif
#ifdef CONFIG_MAC
	case MACH_MAC:
		mach_mac_syms_export();
		break;
#endif
	default:
		break;
	}
}
