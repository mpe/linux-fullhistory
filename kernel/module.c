#include <linux/errno.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <linux/mm.h>		/* defines GFP_KERNEL */
#include <linux/string.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/malloc.h>
/*
 * Originally by Anonymous (as far as I know...)
 * Linux version by Bas Laarhoven <bas@vimec.nl>
 * 0.99.14 version by Jon Tombs <jon@gtex02.us.es>,
 *
 * Heavily modified by Bjorn Ekwall <bj0rn@blox.se> May 1994 (C)
 * This source is covered by the GNU GPL, the same as all kernel sources.
 *
 * Features:
 *	- Supports stacked modules (removable only of there are no dependents).
 *	- Supports table of symbols defined by the modules.
 *	- Supports /proc/ksyms, showing value, name and owner of all
 *	  the symbols defined by all modules (in stack order).
 *	- Added module dependencies information into /proc/modules
 *	- Supports redefines of all symbols, for streams-like behaviour.
 *	- Compatible with older versions of insmod.
 *
 * New addition in December 1994: (Bjorn Ekwall, idea from Jacques Gelinas)
 *	- Externally callable function:
 *
 *		"int register_symtab(struct symbol_table *)"
 *
 *	  This function can be called from within the kernel,
 *	  and ALSO from loadable modules.
 *	  The goal is to assist in modularizing the kernel even more,
 *	  and finally: reducing the number of entries in ksyms.c
 *	  since every subsystem should now be able to decide and
 *	  control exactly what symbols it wants to export, locally!
 */

#ifdef DEBUG_MODULE
#define PRINTK(a) printk a
#else
#define PRINTK(a) /* */
#endif

static struct module kernel_module;
static struct module *module_list = &kernel_module;

static int freeing_modules; /* true if some modules are marked for deletion */

static struct module *find_module( const char *name);
static int get_mod_name( char *user_name, char *buf);
static int free_modules( void);

static int module_init_flag = 0; /* Hmm... */

/*
 * Called at boot time
 */
void init_modules(void) {
	extern struct symbol_table symbol_table; /* in kernel/ksyms.c */
	struct internal_symbol *sym;
	int i;

	for (i = 0, sym = symbol_table.symbol; sym->name; ++sym, ++i)
		;
	symbol_table.n_symbols = i;

	kernel_module.symtab = &symbol_table;
	kernel_module.state = MOD_RUNNING; /* Hah! */
	kernel_module.name = "";
}

int
rename_module_symbol(char *old_name, char *new_name)
{
	struct internal_symbol *sym;
	int i = 0; /* keep gcc silent */

	if (module_list->symtab) {
		sym = module_list->symtab->symbol;
		for (i = module_list->symtab->n_symbols; i > 0; ++sym, --i) {
			if (strcmp(sym->name, old_name) == 0) { /* found it! */
				sym->name = new_name; /* done! */
				PRINTK(("renamed %s to %s\n", old_name, new_name));
				return 1; /* it worked! */
			}
		}
	}
	printk("rename %s to %s failed!\n", old_name, new_name);
	return 0; /* not there... */

	/*
	 * This one will change the name of the first matching symbol!
	 *
	 * With this function, you can replace the name of a symbol defined
	 * in the current module with a new name, e.g. when you want to insert
	 * your own function instead of a previously defined function
	 * with the same name.
	 *
	 * "Normal" usage:
	 *
	 * bogus_function(int params)
	 * {
	 *	do something "smart";
	 *	return real_function(params);
	 * }
	 *
	 * ...
	 *
	 * init_module()
	 * {
	 *	if (rename_module_symbol("_bogus_function", "_real_function"))
	 *		printk("yep!\n");
	 *	else
	 *		printk("no way!\n");
	 * ...
	 * }
	 *
	 * When loading this module, real_function will be resolved
	 * to the real function address.
	 * All later loaded modules that refer to "real_function()" will
	 * then really call "bogus_function()" instead!!!
	 *
	 * This feature will give you ample opportunities to get to know
	 * the taste of your foot when you stuff it into your mouth!!!
	 */
}

/*
 * Allocate space for a module.
 */
asmlinkage unsigned long
sys_create_module(char *module_name, unsigned long size)
{
	struct module *mp;
	void* addr;
	int error;
	int npages;
	int sspace = sizeof(struct module) + MOD_MAX_NAME;
	char name[MOD_MAX_NAME];

	if (!suser())
		return -EPERM;
	if (module_name == NULL || size == 0)
		return -EINVAL;
	if ((error = get_mod_name(module_name, name)) != 0)
		return error;
	if (find_module(name) != NULL) {
		return -EEXIST;
	}

	if ((mp = (struct module*) kmalloc(sspace, GFP_KERNEL)) == NULL) {
		return -ENOMEM;
	}
	strcpy((char *)(mp + 1), name); /* why not? */

	npages = (size + sizeof (int) + 4095) / 4096;
	if ((addr = vmalloc(npages * 4096)) == 0) {
		kfree_s(mp, sspace);
		return -ENOMEM;
	}

	mp->next = module_list;
	mp->ref = NULL;
	mp->symtab = NULL;
	mp->name = (char *)(mp + 1);
	mp->size = npages;
	mp->addr = addr;
	mp->state = MOD_UNINITIALIZED;
	mp->cleanup = NULL;

	* (int *) addr = 0;	/* set use count to zero */
	module_list = mp;	/* link it in */

	PRINTK(("module `%s' (%lu pages @ 0x%08lx) created\n",
		mp->name, (unsigned long) mp->size, (unsigned long) mp->addr));
	return (unsigned long) addr;
}

/*
 * Initialize a module.
 */
asmlinkage int
sys_init_module(char *module_name, char *code, unsigned codesize,
		struct mod_routines *routines,
		struct symbol_table *symtab)
{
	struct module *mp;
	struct symbol_table *newtab;
	char name[MOD_MAX_NAME];
	int error;
	struct mod_routines rt;

	if (!suser())
		return -EPERM;

	/* A little bit of protection... we "know" where the user stack is... */
	if (symtab && ((unsigned long)symtab > 0xb0000000)) {
		printk("warning: you are using an old insmod, no symbols will be inserted!\n");
		symtab = NULL;
	}

	/*
	 * First reclaim any memory from dead modules that where not
	 * freed when deleted. Should I think be done by timers when
	 * the module was deleted - Jon.
	 */
	free_modules();

	if ((error = get_mod_name(module_name, name)) != 0)
		return error;
	PRINTK(("initializing module `%s', %d (0x%x) bytes\n",
		name, codesize, codesize));
	memcpy_fromfs(&rt, routines, sizeof rt);
	if ((mp = find_module(name)) == NULL)
		return -ENOENT;
	if ((codesize + sizeof (int) + 4095) / 4096 > mp->size)
		return -EINVAL;
	memcpy_fromfs((char *)mp->addr + sizeof (int), code, codesize);
	memset((char *)mp->addr + sizeof (int) + codesize, 0,
		mp->size * 4096 - (codesize + sizeof (int)));
	PRINTK(( "module init entry = 0x%08lx, cleanup entry = 0x%08lx\n",
		(unsigned long) rt.init, (unsigned long) rt.cleanup));
	mp->cleanup = rt.cleanup;

	/* update kernel symbol table */
	if (symtab) { /* symtab == NULL means no new entries to handle */
		struct internal_symbol *sym;
		struct module_ref *ref;
		int size;
		int i;
		int legal_start;

		if ((error = verify_area(VERIFY_READ, symtab, sizeof(int))))
			return error;
		memcpy_fromfs((char *)(&(size)), symtab, sizeof(int));

		if ((newtab = (struct symbol_table*) kmalloc(size, GFP_KERNEL)) == NULL) {
			return -ENOMEM;
		}

		if ((error = verify_area(VERIFY_READ, symtab, size))) {
			kfree_s(newtab, size);
			return error;
		}
		memcpy_fromfs((char *)(newtab), symtab, size);

		/* sanity check */
		legal_start = sizeof(struct symbol_table) +
			newtab->n_symbols * sizeof(struct internal_symbol) +
			newtab->n_refs * sizeof(struct module_ref);

		if ((newtab->n_symbols < 0) || (newtab->n_refs < 0) ||
			(legal_start > size)) {
			printk("Illegal symbol table! Rejected!\n");
			kfree_s(newtab, size);
			return -EINVAL;
		}

		/* relocate name pointers, index referred from start of table */
		for (sym = &(newtab->symbol[0]), i = 0;
			i < newtab->n_symbols; ++sym, ++i) {
			if ((unsigned long)sym->name < legal_start || size <= (unsigned long)sym->name) {
				printk("Illegal symbol table! Rejected!\n");
				kfree_s(newtab, size);
				return -EINVAL;
			}
			/* else */
			sym->name += (long)newtab;
		}
		mp->symtab = newtab;

		/* Update module references.
		 * On entry, from "insmod", ref->module points to
		 * the referenced module!
		 * Now it will point to the current module instead!
		 * The ref structure becomes the first link in the linked
		 * list of references to the referenced module.
		 * Also, "sym" from above, points to the first ref entry!!!
		 */
		for (ref = (struct module_ref *)sym, i = 0;
			i < newtab->n_refs; ++ref, ++i) {

			/* Check for valid reference */
			struct module *link = module_list;
			while (link && (ref->module != link))
				link = link->next;

			if (link == (struct module *)0) {
				printk("Non-module reference! Rejected!\n");
				return -EINVAL;
			}

			ref->next = ref->module->ref;
			ref->module->ref = ref;
			ref->module = mp;
		}
	}

	module_init_flag = 1; /* Hmm... */
	if ((*rt.init)() != 0) {
		module_init_flag = 0; /* Hmm... */
		return -EBUSY;
	}
	module_init_flag = 0; /* Hmm... */
	mp->state = MOD_RUNNING;

	return 0;
}

asmlinkage int
sys_delete_module(char *module_name)
{
	struct module *mp;
	char name[MOD_MAX_NAME];
	int error;

	if (!suser())
		return -EPERM;
	/* else */
	if (module_name != NULL) {
		if ((error = get_mod_name(module_name, name)) != 0)
			return error;
		if ((mp = find_module(name)) == NULL)
			return -ENOENT;
		if ((mp->ref != NULL) || (GET_USE_COUNT(mp) != 0))
			return -EBUSY;
		if (mp->state == MOD_RUNNING)
			(*mp->cleanup)();
		mp->state = MOD_DELETED;
	}
	free_modules();
	return 0;
}


/*
 * Copy the kernel symbol table to user space.  If the argument is null,
 * just return the size of the table.
 *
 * Note that the transient module symbols are copied _first_,
 * in lifo order!!!
 *
 * The symbols to "insmod" are according to the "old" format: struct kernel_sym,
 * which is actually quite handy for this purpose.
 * Note that insmod inserts a struct symbol_table later on...
 * (as that format is quite handy for the kernel...)
 *
 * For every module, the first (pseudo)symbol copied is the module name
 * and the address of the module struct.
 * This lets "insmod" keep track of references, and build the array of
 * struct module_refs in the symbol table.
 * The format of the module name is "#module", so that "insmod" can easily
 * notice when a module name comes along. Also, this will make it possible
 * to use old versions of "insmod", albeit with reduced functionality...
 * The "kernel" module has an empty name.
 */
asmlinkage int
sys_get_kernel_syms(struct kernel_sym *table)
{
	struct internal_symbol *from;
	struct kernel_sym isym;
	struct kernel_sym *to;
	struct module *mp = module_list;
	int i;
	int nmodsyms = 0;

	for (mp = module_list; mp; mp = mp->next) {
		if (mp->symtab && mp->symtab->n_symbols) {
			/* include the count for the module name! */
			nmodsyms += mp->symtab->n_symbols + 1;
		}
		else
			/* include the count for the module name! */
			nmodsyms += 1; /* return modules without symbols too */
	}

	if (table != NULL) {
		to = table;

		if ((i = verify_area(VERIFY_WRITE, to, nmodsyms * sizeof(*table))))
			return i;

		/* copy all module symbols first (always LIFO order) */
		for (mp = module_list; mp; mp = mp->next) {
			if (mp->state == MOD_RUNNING) {
				/* magic: write module info as a pseudo symbol */
				isym.value = (unsigned long)mp;
				sprintf(isym.name, "#%s", mp->name);
				memcpy_tofs(to, &isym, sizeof isym);
				++to;

				if (mp->symtab != NULL) {
					for (i = mp->symtab->n_symbols,
						from = mp->symtab->symbol;
						i > 0; --i, ++from, ++to) {

						isym.value = (unsigned long)from->addr;
						strncpy(isym.name, from->name, sizeof isym.name);
						memcpy_tofs(to, &isym, sizeof isym);
					}
				}
			}
		}
	}

	return nmodsyms;
}


/*
 * Copy the name of a module from user space.
 */
int
get_mod_name(char *user_name, char *buf)
{
	int i;

	i = 0;
	for (i = 0 ; (buf[i] = get_fs_byte(user_name + i)) != '\0' ; ) {
		if (++i >= MOD_MAX_NAME)
			return -E2BIG;
	}
	return 0;
}


/*
 * Look for a module by name, ignoring modules marked for deletion.
 */
struct module *
find_module( const char *name)
{
	struct module *mp;

	for (mp = module_list ; mp ; mp = mp->next) {
		if (mp->state == MOD_DELETED)
			continue;
		if (!strcmp(mp->name, name))
			break;
	}
	return mp;
}

static void
drop_refs(struct module *mp)
{
	struct module *step;
	struct module_ref *prev;
	struct module_ref *ref;

	for (step = module_list; step; step = step->next) {
		for (prev = ref = step->ref; ref; ref = prev->next) {
			if (ref->module == mp) {
				if (ref == step->ref)
					step->ref = ref->next;
				else
					prev->next = ref->next;
				break; /* every module only references once! */
			}
			else
				prev = ref;
		}
	}
}

/*
 * Try to free modules which have been marked for deletion.  Returns nonzero
 * if a module was actually freed.
 */
int
free_modules( void)
{
	struct module *mp;
	struct module **mpp;
	int did_deletion;

	did_deletion = 0;
	freeing_modules = 0;
	mpp = &module_list;
	while ((mp = *mpp) != NULL) {
		if (mp->state != MOD_DELETED) {
			mpp = &mp->next;
		} else {
			if (GET_USE_COUNT(mp) != 0) {
				freeing_modules = 1;
				mpp = &mp->next;
			} else {	/* delete it */
				*mpp = mp->next;
				if (mp->symtab) {
					if (mp->symtab->n_refs)
						drop_refs(mp);
					if (mp->symtab->size)
						kfree_s(mp->symtab, mp->symtab->size);
				}
				vfree(mp->addr);
				kfree_s(mp, sizeof(struct module) + MOD_MAX_NAME);
				did_deletion = 1;
			}
		}
	}
	return did_deletion;
}


/*
 * Called by the /proc file system to return a current list of modules.
 */
int get_module_list(char *buf)
{
	char *p;
	char *q;
	int i;
	struct module *mp;
	struct module_ref *ref;
	char size[32];

	p = buf;
	/* Do not show the kernel pseudo module */
	for (mp = module_list ; mp && mp->next; mp = mp->next) {
		if (p - buf > 4096 - 100)
			break;			/* avoid overflowing buffer */
		q = mp->name;
		i = 20;
		while (*q) {
			*p++ = *q++;
			i--;
		}
		sprintf(size, "%d", mp->size);
		i -= strlen(size);
		if (i <= 0)
			i = 1;
		while (--i >= 0)
			*p++ = ' ';
		q = size;
		while (*q)
			*p++ = *q++;
		if (mp->state == MOD_UNINITIALIZED)
			q = "  (uninitialized)";
		else if (mp->state == MOD_RUNNING)
			q = "";
		else if (mp->state == MOD_DELETED)
			q = "  (deleted)";
		else
			q = "  (bad state)";
		while (*q)
			*p++ = *q++;

		if ((ref = mp->ref) != NULL) {
			*p++ = '\t';
			*p++ = '[';
			for (; ref; ref = ref->next) {
				q = ref->module->name;
				while (*q)
					*p++ = *q++;
				if (ref->next)
					*p++ = ' ';
			}
			*p++ = ']';
		}
		*p++ = '\n';
	}
	return p - buf;
}


/*
 * Called by the /proc file system to return a current list of ksyms.
 */
int get_ksyms_list(char *buf)
{
	struct module *mp;
	struct internal_symbol *sym;
	int i;
	char *p = buf;

	for (mp = module_list; mp; mp = mp->next) {
		if ((mp->state == MOD_RUNNING) &&
			(mp->symtab != NULL) && (mp->symtab->n_symbols > 0)) {
			for (i = mp->symtab->n_symbols,
				sym = mp->symtab->symbol;
				i > 0; --i, ++sym) {

				if (p - buf > 4096 - 100) {
					strcat(p, "...\n");
					p += strlen(p);
					return p - buf; /* avoid overflowing buffer */
				}

				if (mp->name[0]) {
					sprintf(p, "%08lx %s\t[%s]\n",
						(long)sym->addr, sym->name, mp->name);
				}
				else {
					sprintf(p, "%08lx %s\n",
						(long)sym->addr, sym->name);
				}
				p += strlen(p);
			}
		}
	}

	return p - buf;
}

/*
 * Rules:
 * - The new symbol table should be statically allocated, or else you _have_
 *   to set the "size" field of the struct to the number of bytes allocated.
 *
 * - The strings that name the symbols will not be copied, maybe the pointers
 *
 * - For a loadable module, the function should only be called in the
 *   context of init_module
 *
 * Those are the only restrictions! (apart from not being reenterable...)
 *
 * If you want to remove a symbol table for a loadable module,
 * the call looks like: "register_symtab(0)".
 *
 * The look of the code is mostly dictated by the format of
 * the frozen struct symbol_table, due to compatibility demands.
 */
#define INTSIZ sizeof(struct internal_symbol)
#define REFSIZ sizeof(struct module_ref)
#define SYMSIZ sizeof(struct symbol_table)
#define MODSIZ sizeof(struct module)
static struct symbol_table nulltab;

int
register_symtab(struct symbol_table *intab)
{
	struct module *mp;
	struct module *link;
	struct symbol_table *oldtab;
	struct symbol_table *newtab;
	struct module_ref *newref;
	int size;

	if (intab && (intab->n_symbols == 0)) {
		struct internal_symbol *sym;
		/* How many symbols, really? */

		for (sym = intab->symbol; sym->name; ++sym)
			intab->n_symbols +=1;
	}

#if 1
	if (module_init_flag == 0) { /* Hmm... */
#else
	if (module_list == &kernel_module) {
#endif
		/* Aha! Called from an "internal" module */
		if (!intab)
			return 0; /* or -ESILLY_PROGRAMMER :-) */

		/* create a pseudo module! */
		if (!(mp = (struct module*) kmalloc(MODSIZ, GFP_KERNEL))) {
			/* panic time! */
			printk("Out of memory for new symbol table!\n");
			return -ENOMEM;
		}
		/* else  OK */
		memset(mp, 0, MODSIZ);
		mp->state = MOD_RUNNING; /* Since it is resident... */
		mp->name = ""; /* This is still the "kernel" symbol table! */
		mp->symtab = intab;

		/* link it in _after_ the resident symbol table */
		mp->next = kernel_module.next;
		kernel_module.next = mp;

		return 0;
	}

	/* else ******** Called from a loadable module **********/

	/*
	 * This call should _only_ be done in the context of the
	 * call to  init_module  i.e. when loading the module!!
	 * Or else...
	 */
	mp = module_list; /* true when doing init_module! */

	/* Any table there before? */
	if ((oldtab = mp->symtab) == (struct symbol_table*)0) {
		/* No, just insert it! */
		mp->symtab = intab;
		return 0;
	}

	/* else  ****** we have to replace the module symbol table ******/
#if 0
	if (oldtab->n_symbols > 0) {
		/* Oh dear, I have to drop the old ones... */
		printk("Warning, dropping old symbols\n");
	}
#endif

	if (oldtab->n_refs == 0) { /* no problems! */
		mp->symtab = intab;
		/* if the old table was kmalloc-ed, drop it */
		if (oldtab->size > 0)
			kfree_s(oldtab, oldtab->size);

		return 0;
	}

	/* else */
	/***** The module references other modules... insmod said so! *****/
	/* We have to allocate a new symbol table, or we lose them! */
	if (intab == (struct symbol_table*)0)
		intab = &nulltab; /* easier code with zeroes in place */

	/* the input symbol table space does not include the string table */
	/* (it does for symbol tables that insmod creates) */

	if (!(newtab = (struct symbol_table*)kmalloc(
		size = SYMSIZ + intab->n_symbols * INTSIZ +
			oldtab->n_refs * REFSIZ,
		GFP_KERNEL))) {
		/* panic time! */
		printk("Out of memory for new symbol table!\n");
		return -ENOMEM;
	}

	/* copy up to, and including, the new symbols */
	memcpy(newtab, intab, SYMSIZ + intab->n_symbols * INTSIZ);

	newtab->size = size;
	newtab->n_refs = oldtab->n_refs;

	/* copy references */
	memcpy( ((char *)newtab) + SYMSIZ + intab->n_symbols * INTSIZ,
		((char *)oldtab) + SYMSIZ + oldtab->n_symbols * INTSIZ,
		oldtab->n_refs * REFSIZ);

	/* relink references from the old table to the new one */

	/* pointer to the first reference entry in newtab! Really! */
	newref = (struct module_ref*) &(newtab->symbol[newtab->n_symbols]);

	/* check for reference links from previous modules */
	for (	link = module_list;
		link && (link != &kernel_module);
		link = link->next) {

		if (link->ref && (link->ref->module == mp))
			link->ref = newref++;
	}

	mp->symtab = newtab;

	/* all references (if any) have been handled */

	/* if the old table was kmalloc-ed, drop it */
	if (oldtab->size > 0)
		kfree_s(oldtab, oldtab->size);

	return 0;
}
