/*
 * Dynamic loading of modules into the kernel.
 *
 * Rewritten by Richard Henderson <rth@tamu.edu> Dec 1996
 */

#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#ifdef __GENKSYMS__
# undef MODVERSIONS
# define MODVERSIONS
#else /* ! __GENKSYMS__ */
# if defined(MODVERSIONS) && !defined(MODULE) && defined(EXPORT_SYMTAB)
#   include <linux/modversions.h>
# endif
#endif /* __GENKSYMS__ */

/* Don't need to bring in all of uaccess.h just for this decl.  */
struct exception_table_entry;

/* Used by get_kernel_syms, which is depreciated.  */
struct kernel_sym
{
	unsigned long value;
	char name[60];		/* should have been 64-sizeof(long); oh well */
};

struct module_symbol
{
	unsigned long value;
	const char *name;
};

struct module_ref
{
	struct module *dep;	/* "parent" pointer */
	struct module *ref;	/* "child" pointer */
	struct module_ref *next_ref;
};

struct module
{
	unsigned long size_of_struct;	/* == sizeof(module) */
	struct module *next;
	const char *name;
	unsigned long size;

	long usecount;
	unsigned long flags;		/* AUTOCLEAN et al */

	unsigned nsyms;
	unsigned ndeps;

	struct module_symbol *syms;
	struct module_ref *deps;
	struct module_ref *refs;
	int (*init)(void);
	void (*cleanup)(void);
	const struct exception_table_entry *ex_table_start;
	const struct exception_table_entry *ex_table_end;
#ifdef __alpha__
	unsigned long gp;
#endif
};

struct module_info
{
  unsigned long addr;
  unsigned long size;
  unsigned long flags;
};

/* Bits of module.flags.  */

#define MOD_UNINITIALIZED	0
#define MOD_RUNNING		1
#define MOD_DELETED		2
#define MOD_AUTOCLEAN		4
#define MOD_VISITED  		8

/* Values for query_module's which.  */

#define QM_MODULES	1
#define QM_DEPS		2
#define QM_REFS		3
#define QM_SYMBOLS	4
#define QM_INFO		5

/* Backwards compatibility definition.  */

#define GET_USE_COUNT(module)	((module)->usecount)

/* Indirect stringification.  */

#define __MODULE_STRING_1(x)	#x
#define __MODULE_STRING(x)	__MODULE_STRING_1(x)

#ifdef MODULE

/* Embedded module documentation macros.  */

/* For documentation purposes only.  */

#define MODULE_AUTHOR(name)						   \
const char __module_author[] __attribute__((section(".modinfo"))) = 	   \
"author=" name

/* Could potentially be used by kerneld...  */

#define MODULE_SUPPORTED_DEVICE(dev)					   \
const char __module_device[] __attribute__((section(".modinfo"))) = 	   \
"device=" dev

/* Used to verify parameters given to the module.  The TYPE arg should
   be a string in the following format:
   	[min[-max]]{b,h,i,l,s}
   The MIN and MAX specifiers delimit the length of the array.  If MAX
   is omitted, it defaults to MIN; if both are omitted, the default is 1.
   The final character is a type specifier:
	b	byte
	h	short
	i	int
	l	long
	s	string
*/

#define MODULE_PARM(var,type)			\
const char __module_parm_##var[]		\
__attribute__((section(".modinfo"))) =		\
"parm_" __MODULE_STRING(var) "=" type

/* The attributes of a section are set the first time the section is
   seen; we want .modinfo to not be allocated.  This header should be
   included before any functions or variables are defined.  */

__asm__(".section .modinfo\n\t.text");

/* Define the module variable, and usage macros.  */
extern struct module __this_module;

#define MOD_INC_USE_COUNT \
	(__this_module.usecount++, __this_module.flags |= MOD_VISITED)
#define MOD_DEC_USE_COUNT \
	(__this_module.usecount--, __this_module.flags |= MOD_VISITED)
#define MOD_IN_USE \
	(__this_module.usecount != 0)

#ifndef __NO_VERSION__
#include <linux/version.h>
const char __module_kernel_version[] __attribute__((section(".modinfo"))) =
"kernel_version=" UTS_RELEASE;
#if defined(MODVERSIONS) && !defined(__GENKSYMS__)
const char __module_using_checksums[] __attribute__((section(".modinfo"))) =
"using_checksums=1";
#endif
#endif

#else /* MODULE */

#define MODULE_AUTHOR(name)
#define MODULE_SUPPORTED_DEVICE(name)
#define MODULE_PARM(var,type)

#define MOD_INC_USE_COUNT	do { } while (0)
#define MOD_DEC_USE_COUNT	do { } while (0)
#define MOD_IN_USE		1

extern struct module *module_list;

#endif /* MODULE */

/* Export a symbol either from the kernel or a module.

   In the kernel, the symbol is added to the kernel's global symbol table.

   In a module, it controls which variables are exported.  If no
   variables are explicitly exported, the action is controled by the
   insmod -[xX] flags.  Otherwise, only the variables listed are exported.
   This obviates the need for the old register_symtab() function.  */

#if defined(__GENKSYMS__)

/* We want the EXPORT_SYMBOL tag left intact for recognition.  */

#elif !defined(CONFIG_MODULES)

#define EXPORT_SYMBOL(var)
#define EXPORT_SYMBOL_NOVERS(var)
#define EXPORT_NO_SYMBOLS

#else

#define EXPORT_SYMBOL(var)				\
const struct module_symbol __export_##var 		\
__attribute__((section("__ksymtab"))) = {		\
	(unsigned long)&var, __MODULE_STRING(var)	\
}							\

#define EXPORT_SYMBOL_NOVERS(var) EXPORT_SYMBOL(var)

#ifdef MODULE
/* Force a module to export no symbols.  */
#define EXPORT_NO_SYMBOLS				\
__asm__(".section __ksymtab\n.text")
#else
#define EXPORT_NO_SYMBOLS
#endif /* MODULE */

#endif

#endif /* _LINUX_MODULE_H */
