/*
 * linux/arch/alpha/kernel/ksyms.c
 *
 * Export the alpha-specific functions that are needed for loadable
 * modules.
 */

#include <linux/string.h>
#include <linux/module.h>
#include <linux/string.h>
#include <asm/io.h>
#include <asm/hwrpb.h>

extern void bcopy (const char *src, char *dst, int len);
extern struct hwrpb_struct *hwrpb;

/* these are C runtime functions with special calling conventions: */
extern void __divl (void);
extern void __reml (void);
extern void __divq (void);
extern void __remq (void);
extern void __divlu (void);
extern void __remlu (void);
extern void __divqu (void);
extern void __remqu (void);

static struct symbol_table arch_symbol_table = {
#include <linux/symtab_begin.h>
	/* platform dependent support */

	X(_inb),
	X(_inw),
	X(_inl),
	X(_outb),
	X(_outw),
	X(_outl),
	X(_readb),
	X(_readw),
	X(_readl),
	X(_writeb),
	X(_writew),
	X(_writel),
	X(__divl),
	X(__reml),
	X(__divq),
	X(__remq),
	X(__divlu),
	X(__remlu),
	X(__divqu),
	X(__remqu),
	X(insl),
	X(insw),
	X(outsl),
	X(outsw),
	X(strcat),
	X(strcmp),
	X(strcpy),
	X(strlen),
	X(strncmp),
	X(strncpy),
	X(strnlen),
	X(strstr),
	X(strtok),
	X(strchr),
	X(hwrpb),
	X(memcmp),
	X(memmove),
	X(__memcpy),
	X(__constant_c_memset),
	/*
	 * The following are special because they're not called
	 * explicitly (the C compiler or assembler generates them in
	 * response to division operations).  Fortunately, their
	 * interface isn't gonna change any time soon now, so it's OK
	 * to leave it out of version control.
	 */
# undef bcopy
# undef memcpy
# undef memset
	XNOVERS(__divl),
	XNOVERS(__divlu),
	XNOVERS(__divq),
	XNOVERS(__divqu),
	XNOVERS(__reml),
	XNOVERS(__remlu),
	XNOVERS(__remq),
	XNOVERS(__remqu),
	XNOVERS(memcpy),
	XNOVERS(memset),
	/* these shouldn't be necessary---they should be versioned: */
	XNOVERS(__memcpy),
	XNOVERS(__memset),
#include <linux/symtab_end.h>
};

void arch_syms_export(void)
{
	register_symtab(&arch_symbol_table);
}
