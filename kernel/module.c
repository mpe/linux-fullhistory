#include <linux/errno.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <linux/mm.h>		/* defines GFP_KERNEL */
#include <linux/string.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/malloc.h>
/*
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
asmlinkage int
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
	return (int) addr;
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

		if ((error = verify_area(VERIFY_READ, symtab, sizeof(int))))
			return error;
		memcpy_fromfs((char *)(&(size)), symtab, sizeof(int));

		if ((newtab = (struct symbol_table*) kmalloc(size, GFP_KERNEL)) == NULL) {
			return -ENOMEM;
		}

		if ((error = verify_area(VERIFY_READ, symtab, size)))
			return error;
		memcpy_fromfs((char *)(newtab), symtab, size);

		/* relocate name pointers, index referred from start of table */
		for (sym = &(newtab->symbol[0]), i = 0;
			i < newtab->n_symbols; ++sym, ++i) {
			sym->name += (long)newtab;
		}
		mp->symtab = newtab;

		/* Update module references.
		 * On entry, from "insmod", ref->module points to
		 * the referenced module!
		 * Also, "sym" from above, points to the first ref entry!!!
		 */
		for (ref = (struct module_ref *)sym, i = 0;
			i < newtab->n_refs; ++ref, ++i) {
			ref->next = ref->module->ref;
			ref->module->ref = ref;
			ref->module = mp;
		}
	}

	if ((*rt.init)() != 0)
		return -EBUSY;
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
	}

	if (table != NULL) {
		to = table;

		if ((i = verify_area(VERIFY_WRITE, to, nmodsyms * sizeof(*table))))
			return i;

		/* copy all module symbols first (always LIFO order) */
		for (mp = module_list; mp; mp = mp->next) {
			if ((mp->state == MOD_RUNNING) &&
				(mp->symtab != NULL) && (mp->symtab->n_symbols > 0)) {
				/* magic: write module info as a pseudo symbol */
				isym.value = (unsigned long)mp;
				sprintf(isym.name, "#%s", mp->name);
				memcpy_tofs(to, &isym, sizeof isym);
				++to;

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
