/*
 * Dynamic loading of modules into the kernel.
 *
 * Modified by Bjorn Ekwall <bj0rn@blox.se>
 */

#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#ifdef CONFIG_MODVERSIONS
# ifndef __GENKSYMS__
#  ifdef MODULE
#   define _set_ver(sym,vers) sym ## _R ## vers
#   include <linux/modversions.h>
#  else /* MODULE */
#   ifdef EXPORT_SYMTAB
#    define _set_ver(sym,vers) sym
#    include <linux/modversions.h>
#   endif /* EXPORT_SYMTAB */
#  endif /* MODULE */
# else /* __GENKSYMS__ */
#  define _set_ver(sym,vers) sym
#  endif /* __GENKSYMS__ */
#endif /* CONFIG_MODVERSIONS */

/* values of module.state */
#define MOD_UNINITIALIZED 0
#define MOD_RUNNING 1
#define MOD_DELETED 2

/* maximum length of module name */
#define MOD_MAX_NAME 64

/* maximum length of symbol name */
#define SYM_MAX_NAME 60

struct kernel_sym { /* sent to "insmod" */
	unsigned long value;		/* value of symbol */
	char name[SYM_MAX_NAME];	/* name of symbol */
};

struct module_ref {
	struct module *module;
	struct module_ref *next;
};

struct internal_symbol {
	void *addr;
	char *name;
	};

struct symbol_table { /* received from "insmod" */
	int size; /* total, including string table!!! */
	int n_symbols;
	int n_refs;
	struct internal_symbol symbol[0]; /* actual size defined by n_symbols */
	struct module_ref ref[0]; /* actual size defined by n_refs */
};
/*
 * Note: The string table follows immediately after the symbol table in memory!
 */

struct module {
	struct module *next;
	struct module_ref *ref;	/* the list of modules that refer to me */
	struct symbol_table *symtab;
	char *name;
	int size;			/* size of module in pages */
	void* addr;			/* address of module */
	int state;
	void (*cleanup)(void);		/* cleanup routine */
};

struct mod_routines {
	int (*init)(void);		/* initialization routine */
	void (*cleanup)(void);		/* cleanup routine */
};

/* rename_module_symbol(old_name, new_name)  WOW! */
extern int rename_module_symbol(char *, char *);

/* insert new symbol table */
extern int register_symtab(struct symbol_table *);

/*
 * The first word of the module contains the use count.
 */
#define GET_USE_COUNT(module)	(* (int *) (module)->addr)
/*
 * define the count variable, and usage macros.
 */

extern int mod_use_count_;
#if defined(CONFIG_MODVERSIONS) && defined(MODULE) && !defined(__GENKSYMS__)
int Using_Versions; /* gcc will handle this global (used as a flag) correctly */
#endif

#define MOD_INC_USE_COUNT      mod_use_count_++
#define MOD_DEC_USE_COUNT      mod_use_count_--
#define MOD_IN_USE	       (mod_use_count_ != 0)

#endif
