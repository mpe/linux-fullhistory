#include <linux/config.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/smp_lock.h>
#include <asm/pgtable.h>
#include <linux/init.h>

/*
 * Originally by Anonymous (as far as I know...)
 * Linux version by Bas Laarhoven <bas@vimec.nl>
 * 0.99.14 version by Jon Tombs <jon@gtex02.us.es>,
 * Heavily modified by Bjorn Ekwall <bj0rn@blox.se> May 1994 (C)
 * Rewritten by Richard Henderson <rth@tamu.edu> Dec 1996
 *
 * This source is covered by the GNU GPL, the same as all kernel sources.
 */

#ifdef CONFIG_MODULES		/* a *big* #ifdef block... */

extern struct module_symbol __start___ksymtab[];
extern struct module_symbol __stop___ksymtab[];

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

static struct module kernel_module =
{
	sizeof(struct module),	/* size_of_struct */
	NULL,			/* next */
	"",			/* name */
	0,			/* size */
	{ATOMIC_INIT(1)},	/* usecount */
	MOD_RUNNING,		/* flags */
	0,			/* nsyms -- to filled in in init_modules */
	0,			/* ndeps */
	__start___ksymtab,	/* syms */
	NULL,			/* deps */
	NULL,			/* refs */
	NULL,			/* init */
	NULL,			/* cleanup */
	__start___ex_table,	/* ex_table_start */
	__stop___ex_table,	/* ex_table_end */
	/* Rest are NULL */
};

struct module *module_list = &kernel_module;

static long get_mod_name(const char *user_name, char **buf);
static void put_mod_name(char *buf);
static struct module *find_module(const char *name);
static void free_module(struct module *, int tag_freed);


/*
 * Called at boot time
 */

__initfunc(void init_modules(void))
{
	kernel_module.nsyms = __stop___ksymtab - __start___ksymtab;

#ifdef __alpha__
	__asm__("stq $29,%0" : "=m"(kernel_module.gp));
#endif
}

/*
 * Copy the name of a module from user space.
 */

static inline long
get_mod_name(const char *user_name, char **buf)
{
	unsigned long page;
	long retval;

	if ((unsigned long)user_name >= TASK_SIZE
	    && !segment_eq(get_fs (), KERNEL_DS))
		return -EFAULT;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	retval = strncpy_from_user((char *)page, user_name, PAGE_SIZE);
	if (retval > 0) {
		if (retval < PAGE_SIZE) {
			*buf = (char *)page;
			return retval;
		}
		retval = -ENAMETOOLONG;
	} else if (!retval)
		retval = -EINVAL;

	free_page(page);
	return retval;
}

static inline void
put_mod_name(char *buf)
{
	free_page((unsigned long)buf);
}

/*
 * Allocate space for a module.
 */

asmlinkage unsigned long
sys_create_module(const char *name_user, size_t size)
{
	char *name;
	long namelen, error;
	struct module *mod;

	lock_kernel();
	if (!capable(CAP_SYS_MODULE)) {
		error = -EPERM;
		goto err0;
	}
	if ((namelen = get_mod_name(name_user, &name)) < 0) {
		error = namelen;
		goto err0;
	}
	if (size < sizeof(struct module)+namelen) {
		error = -EINVAL;
		goto err1;
	}
	if (find_module(name) != NULL) {
		error = -EEXIST;
		goto err1;
	}
	if ((mod = (struct module *)module_map(size)) == NULL) {
		error = -ENOMEM;
		goto err1;
	}

	memset(mod, 0, sizeof(*mod));
	mod->size_of_struct = sizeof(*mod);
	mod->next = module_list;
	mod->name = (char *)(mod + 1);
	mod->size = size;
	memcpy((char*)(mod+1), name, namelen+1);

	put_mod_name(name);

	module_list = mod;	/* link it in */

	error = (long) mod;
	goto err0;
err1:
	put_mod_name(name);
err0:
	unlock_kernel();
	return error;
}

/*
 * Initialize a module.
 */

asmlinkage int
sys_init_module(const char *name_user, struct module *mod_user)
{
	struct module mod_tmp, *mod;
	char *name, *n_name;
	long namelen, n_namelen, i, error = -EPERM;
	unsigned long mod_user_size;
	struct module_ref *dep;

	lock_kernel();
	if (!capable(CAP_SYS_MODULE))
		goto err0;
	if ((namelen = get_mod_name(name_user, &name)) < 0) {
		error = namelen;
		goto err0;
	}
	if ((mod = find_module(name)) == NULL) {
		error = -ENOENT;
		goto err1;
	}

	/* Check module header size.  We allow a bit of slop over the 
	   size we are familiar with to cope with a version of insmod
	   for a newer kernel.  But don't over do it. */
	if ((error = get_user(mod_user_size, &mod_user->size_of_struct)) != 0)
		goto err1;
	if (mod_user_size < (unsigned long)&((struct module *)0L)->persist_start
	    || mod_user_size > sizeof(struct module) + 16*sizeof(void*)) {
		printk(KERN_ERR "init_module: Invalid module header size.\n"
		       KERN_ERR "A new version of the modutils is likely "
				"needed.\n");
		error = -EINVAL;
		goto err1;
	}

	/* Hold the current contents while we play with the user's idea
	   of righteousness.  */
	mod_tmp = *mod;

	error = copy_from_user(mod, mod_user, sizeof(struct module));
	if (error) {
		error = -EFAULT;
		goto err2;
	}

	/* Sanity check the size of the module.  */
	error = -EINVAL;

	if (mod->size > mod_tmp.size) {
		printk(KERN_ERR "init_module: Size of initialized module "
				"exceeds size of created module.\n");
		goto err2;
	}

	/* Make sure all interesting pointers are sane.  */

#define bound(p, n, m)  ((unsigned long)(p) >= (unsigned long)(m+1) &&  \
	         (unsigned long)((p)+(n)) <= (unsigned long)(m) + (m)->size)

	if (!bound(mod->name, namelen, mod)) {
		printk(KERN_ERR "init_module: mod->name out of bounds.\n");
		goto err2;
	}
	if (mod->nsyms && !bound(mod->syms, mod->nsyms, mod)) {
		printk(KERN_ERR "init_module: mod->syms out of bounds.\n");
		goto err2;
	}
	if (mod->ndeps && !bound(mod->deps, mod->ndeps, mod)) {
		printk(KERN_ERR "init_module: mod->deps out of bounds.\n");
		goto err2;
	}
	if (mod->init && !bound(mod->init, 0, mod)) {
		printk(KERN_ERR "init_module: mod->init out of bounds.\n");
		goto err2;
	}
	if (mod->cleanup && !bound(mod->cleanup, 0, mod)) {
		printk(KERN_ERR "init_module: mod->cleanup out of bounds.\n");
		goto err2;
	}
	if (mod->ex_table_start > mod->ex_table_end
	    || (mod->ex_table_start &&
		!((unsigned long)mod->ex_table_start >= (unsigned long)(mod+1)
		  && ((unsigned long)mod->ex_table_end
		      < (unsigned long)mod + mod->size)))
	    || (((unsigned long)mod->ex_table_start
		 - (unsigned long)mod->ex_table_end)
		% sizeof(struct exception_table_entry))) {
		printk(KERN_ERR "init_module: mod->ex_table_* invalid.\n");
		goto err2;
	}
	if (mod->flags & ~MOD_AUTOCLEAN) {
		printk(KERN_ERR "init_module: mod->flags invalid.\n");
		goto err2;
	}
#ifdef __alpha__
	if (!bound(mod->gp - 0x8000, 0, mod)) {
		printk(KERN_ERR "init_module: mod->gp out of bounds.\n");
		goto err2;
	}
#endif
	if (mod_member_present(mod, can_unload)
	    && mod->can_unload && !bound(mod->can_unload, 0, mod)) {
		printk(KERN_ERR "init_module: mod->can_unload out of bounds.\n");
		goto err2;
	}

#undef bound

	/* Check that the user isn't doing something silly with the name.  */

	if ((n_namelen = get_mod_name(mod->name - (unsigned long)mod
				      + (unsigned long)mod_user,
				      &n_name)) < 0) {
		error = n_namelen;
		goto err2;
	}
	if (namelen != n_namelen || strcmp(n_name, mod_tmp.name) != 0) {
		printk(KERN_ERR "init_module: changed module name to "
				"`%s' from `%s'\n",
		       n_name, mod_tmp.name);
		goto err3;
	}

	/* Ok, that's about all the sanity we can stomach; copy the rest.  */

	if (copy_from_user(mod+1, mod_user+1, mod->size-sizeof(*mod))) {
		error = -EFAULT;
		goto err3;
	}

	/* On some machines it is necessary to do something here
	   to make the I and D caches consistent.  */
	flush_icache_range((unsigned long)mod, (unsigned long)mod + mod->size);

	/* Update module references.  */
	mod->next = mod_tmp.next;
	mod->refs = NULL;
	for (i = 0, dep = mod->deps; i < mod->ndeps; ++i, ++dep) {
		struct module *o, *d = dep->dep;

		/* Make sure the indicated dependencies are really modules.  */
		if (d == mod) {
			printk(KERN_ERR "init_module: self-referential "
					"dependency in mod->deps.\n");
			goto err3;
		}

		for (o = module_list; o != &kernel_module; o = o->next)
			if (o == d) goto found_dep;

		printk(KERN_ERR "init_module: found dependency that is "
				"(no longer?) a module.\n");
		goto err3;
		
	found_dep:
		dep->ref = mod;
		dep->next_ref = d->refs;
		d->refs = dep;
		/* Being referenced by a dependent module counts as a 
		   use as far as kmod is concerned.  */
		d->flags |= MOD_USED_ONCE;
	}

	/* Free our temporary memory.  */
	put_mod_name(n_name);
	put_mod_name(name);

	/* Initialize the module.  */
	atomic_set(&mod->uc.usecount,1);
	if (mod->init && mod->init() != 0) {
		atomic_set(&mod->uc.usecount,0);
		error = -EBUSY;
		goto err0;
	}
	atomic_dec(&mod->uc.usecount);

	/* And set it running.  */
	mod->flags |= MOD_RUNNING;
	error = 0;
	goto err0;

err3:
	put_mod_name(n_name);
err2:
	*mod = mod_tmp;
err1:
	put_mod_name(name);
err0:
	unlock_kernel();
	return error;
}

asmlinkage int
sys_delete_module(const char *name_user)
{
	struct module *mod, *next;
	char *name;
	long error = -EPERM;
	int something_changed;

	lock_kernel();
	if (!capable(CAP_SYS_MODULE))
		goto out;

	if (name_user) {
		if ((error = get_mod_name(name_user, &name)) < 0)
			goto out;
		if (error == 0) {
			error = -EINVAL;
			put_mod_name(name);
			goto out;
		}
		error = -ENOENT;
		if ((mod = find_module(name)) == NULL) {
			put_mod_name(name);
			goto out;
		}
		put_mod_name(name);
		error = -EBUSY;
 		if (mod->refs != NULL || __MOD_IN_USE(mod))
			goto out;

		free_module(mod, 0);
		error = 0;
		goto out;
	}

	/* Do automatic reaping */
restart:
	something_changed = 0;
	for (mod = module_list; mod != &kernel_module; mod = next) {
		next = mod->next;
		if (mod->refs == NULL
		    && (mod->flags & MOD_AUTOCLEAN)
		    && (mod->flags & MOD_RUNNING)
		    && !(mod->flags & MOD_DELETED)
		    && (mod->flags & MOD_USED_ONCE)
		    && !__MOD_IN_USE(mod)) {
			if ((mod->flags & MOD_VISITED)
			    && !(mod->flags & MOD_JUST_FREED)) {
				mod->flags &= ~MOD_VISITED;
			} else {
				free_module(mod, 1);
				something_changed = 1;
			}
		}
	}
	if (something_changed)
		goto restart;
	for (mod = module_list; mod != &kernel_module; mod = mod->next)
		mod->flags &= ~MOD_JUST_FREED;
	error = 0;
out:
	unlock_kernel();
	return error;
}

/* Query various bits about modules.  */

static int
qm_modules(char *buf, size_t bufsize, size_t *ret)
{
	struct module *mod;
	size_t nmod, space, len;

	nmod = space = 0;

	for (mod=module_list; mod != &kernel_module; mod=mod->next, ++nmod) {
		len = strlen(mod->name)+1;
		if (len > bufsize)
			goto calc_space_needed;
		if (copy_to_user(buf, mod->name, len))
			return -EFAULT;
		buf += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(nmod, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	space += len;
	while ((mod = mod->next) != &kernel_module)
		space += strlen(mod->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static int
qm_deps(struct module *mod, char *buf, size_t bufsize, size_t *ret)
{
	size_t i, space, len;

	if (mod == &kernel_module)
		return -EINVAL;
	if ((mod->flags & (MOD_RUNNING | MOD_DELETED)) != MOD_RUNNING)
		if (put_user(0, ret))
			return -EFAULT;
		else
			return 0;

	space = 0;
	for (i = 0; i < mod->ndeps; ++i) {
		const char *dep_name = mod->deps[i].dep->name;

		len = strlen(dep_name)+1;
		if (len > bufsize)
			goto calc_space_needed;
		if (copy_to_user(buf, dep_name, len))
			return -EFAULT;
		buf += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(i, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	space += len;
	while (++i < mod->ndeps)
		space += strlen(mod->deps[i].dep->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static int
qm_refs(struct module *mod, char *buf, size_t bufsize, size_t *ret)
{
	size_t nrefs, space, len;
	struct module_ref *ref;

	if (mod == &kernel_module)
		return -EINVAL;
	if ((mod->flags & (MOD_RUNNING | MOD_DELETED)) != MOD_RUNNING)
		if (put_user(0, ret))
			return -EFAULT;
		else
			return 0;

	space = 0;
	for (nrefs = 0, ref = mod->refs; ref ; ++nrefs, ref = ref->next_ref) {
		const char *ref_name = ref->ref->name;

		len = strlen(ref_name)+1;
		if (len > bufsize)
			goto calc_space_needed;
		if (copy_to_user(buf, ref_name, len))
			return -EFAULT;
		buf += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(nrefs, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	space += len;
	while ((ref = ref->next_ref) != NULL)
		space += strlen(ref->ref->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static int
qm_symbols(struct module *mod, char *buf, size_t bufsize, size_t *ret)
{
	size_t i, space, len;
	struct module_symbol *s;
	char *strings;
	unsigned long *vals;

	if ((mod->flags & (MOD_RUNNING | MOD_DELETED)) != MOD_RUNNING)
		if (put_user(0, ret))
			return -EFAULT;
		else
			return 0;

	space = mod->nsyms * 2*sizeof(void *);

	i = len = 0;
	s = mod->syms;

	if (space > bufsize)
		goto calc_space_needed;

	if (!access_ok(VERIFY_WRITE, buf, space))
		return -EFAULT;

	bufsize -= space;
	vals = (unsigned long *)buf;
	strings = buf+space;

	for (; i < mod->nsyms ; ++i, ++s, vals += 2) {
		len = strlen(s->name)+1;
		if (len > bufsize)
			goto calc_space_needed;

		if (copy_to_user(strings, s->name, len)
		    || __put_user(s->value, vals+0)
		    || __put_user(space, vals+1))
			return -EFAULT;

		strings += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(i, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	for (; i < mod->nsyms; ++i, ++s)
		space += strlen(s->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static int
qm_info(struct module *mod, char *buf, size_t bufsize, size_t *ret)
{
	int error = 0;

	if (mod == &kernel_module)
		return -EINVAL;

	if (sizeof(struct module_info) <= bufsize) {
		struct module_info info;
		info.addr = (unsigned long)mod;
		info.size = mod->size;
		info.flags = mod->flags;
		info.usecount = (mod_member_present(mod, can_unload)
				 && mod->can_unload ? -1 : atomic_read(&mod->uc.usecount));

		if (copy_to_user(buf, &info, sizeof(struct module_info)))
			return -EFAULT;
	} else
		error = -ENOSPC;

	if (put_user(sizeof(struct module_info), ret))
		return -EFAULT;

	return error;
}

asmlinkage int
sys_query_module(const char *name_user, int which, char *buf, size_t bufsize,
		 size_t *ret)
{
	struct module *mod;
	int err;

	lock_kernel();
	if (name_user == NULL)
		mod = &kernel_module;
	else {
		long namelen;
		char *name;

		if ((namelen = get_mod_name(name_user, &name)) < 0) {
			err = namelen;
			goto out;
		}
		err = -ENOENT;
		if (namelen == 0)
			mod = &kernel_module;
		else if ((mod = find_module(name)) == NULL) {
			put_mod_name(name);
			goto out;
		}
		put_mod_name(name);
	}

	switch (which)
	{
	case 0:
		err = 0;
		break;
	case QM_MODULES:
		err = qm_modules(buf, bufsize, ret);
		break;
	case QM_DEPS:
		err = qm_deps(mod, buf, bufsize, ret);
		break;
	case QM_REFS:
		err = qm_refs(mod, buf, bufsize, ret);
		break;
	case QM_SYMBOLS:
		err = qm_symbols(mod, buf, bufsize, ret);
		break;
	case QM_INFO:
		err = qm_info(mod, buf, bufsize, ret);
		break;
	default:
		err = -EINVAL;
		break;
	}
out:
	unlock_kernel();
	return err;
}

/*
 * Copy the kernel symbol table to user space.  If the argument is
 * NULL, just return the size of the table.
 *
 * This call is obsolete.  New programs should use query_module+QM_SYMBOLS
 * which does not arbitrarily limit the length of symbols.
 */

asmlinkage int
sys_get_kernel_syms(struct kernel_sym *table)
{
	struct module *mod;
	int i;
	struct kernel_sym ksym;

	lock_kernel();
	for (mod = module_list, i = 0; mod; mod = mod->next) {
		/* include the count for the module name! */
		i += mod->nsyms + 1;
	}

	if (table == NULL)
		goto out;

	/* So that we don't give the user our stack content */
	memset (&ksym, 0, sizeof (ksym));

	for (mod = module_list, i = 0; mod; mod = mod->next) {
		struct module_symbol *msym;
		unsigned int j;

		if ((mod->flags & (MOD_RUNNING|MOD_DELETED)) != MOD_RUNNING)
			continue;

		/* magic: write module info as a pseudo symbol */
		ksym.value = (unsigned long)mod;
		ksym.name[0] = '#';
		strncpy(ksym.name+1, mod->name, sizeof(ksym.name)-1);
		ksym.name[sizeof(ksym.name)-1] = '\0';

		if (copy_to_user(table, &ksym, sizeof(ksym)) != 0)
			goto out;
		++i, ++table;

		if (mod->nsyms == 0)
			continue;

		for (j = 0, msym = mod->syms; j < mod->nsyms; ++j, ++msym) {
			ksym.value = msym->value;
			strncpy(ksym.name, msym->name, sizeof(ksym.name));
			ksym.name[sizeof(ksym.name)-1] = '\0';

			if (copy_to_user(table, &ksym, sizeof(ksym)) != 0)
				goto out;
			++i, ++table;
		}
	}
out:
	unlock_kernel();
	return i;
}

/*
 * Look for a module by name, ignoring modules marked for deletion.
 */

static struct module *
find_module(const char *name)
{
	struct module *mod;

	for (mod = module_list; mod ; mod = mod->next) {
		if (mod->flags & MOD_DELETED)
			continue;
		if (!strcmp(mod->name, name))
			break;
	}

	return mod;
}

/*
 * Free the given module.
 */

static void
free_module(struct module *mod, int tag_freed)
{
	struct module_ref *dep;
	unsigned i;

	/* Let the module clean up.  */

	mod->flags |= MOD_DELETED;
	if (mod->flags & MOD_RUNNING) 
	{
		if(mod->cleanup)
			mod->cleanup();
		mod->flags &= ~MOD_RUNNING;
	}

	/* Remove the module from the dependency lists.  */

	for (i = 0, dep = mod->deps; i < mod->ndeps; ++i, ++dep) {
		struct module_ref **pp;
		for (pp = &dep->dep->refs; *pp != dep; pp = &(*pp)->next_ref)
			continue;
		*pp = dep->next_ref;
		if (tag_freed && dep->dep->refs == NULL)
			dep->dep->flags |= MOD_JUST_FREED;
	}

	/* And from the main module list.  */

	if (mod == module_list) {
		module_list = mod->next;
	} else {
		struct module *p;
		for (p = module_list; p->next != mod; p = p->next)
			continue;
		p->next = mod->next;
	}

	/* And free the memory.  */

	module_unmap(mod);
}

/*
 * Called by the /proc file system to return a current list of modules.
 */

int get_module_list(char *p)
{
	size_t left = PAGE_SIZE;
	struct module *mod;
	char tmpstr[64];
	struct module_ref *ref;

	for (mod = module_list; mod != &kernel_module; mod = mod->next) {
		long len;
		const char *q;

#define safe_copy_str(str, len)						\
		do {							\
			if (left < len)					\
				goto fini;				\
			memcpy(p, str, len); p += len, left -= len;	\
		} while (0)
#define safe_copy_cstr(str)	safe_copy_str(str, sizeof(str)-1)

        	len = strlen(mod->name);
		safe_copy_str(mod->name, len);

		if ((len = 20 - len) > 0) {
			if (left < len)
				goto fini;
			memset(p, ' ', len);
			p += len;
			left -= len;
		}

		len = sprintf(tmpstr, "%8lu", mod->size);
		safe_copy_str(tmpstr, len);

		if (mod->flags & MOD_RUNNING) {
			len = sprintf(tmpstr, "%4ld",
				      (mod_member_present(mod, can_unload)
				       && mod->can_unload
				       ? -1L : (long)atomic_read(&mod->uc.usecount)));
			safe_copy_str(tmpstr, len);
		}

		if (mod->flags & MOD_DELETED)
			safe_copy_cstr(" (deleted)");
		else if (mod->flags & MOD_RUNNING) {
			if (mod->flags & MOD_AUTOCLEAN)
				safe_copy_cstr(" (autoclean)");
			if (!(mod->flags & MOD_USED_ONCE))
				safe_copy_cstr(" (unused)");
		} else
			safe_copy_cstr(" (uninitialized)");

		if ((ref = mod->refs) != NULL) {
			safe_copy_cstr(" [");
			while (1) {
				q = ref->ref->name;
				len = strlen(q);
				safe_copy_str(q, len);

				if ((ref = ref->next_ref) != NULL)
					safe_copy_cstr(" ");
				else
					break;
			}
			safe_copy_cstr("]");
		}
		safe_copy_cstr("\n");

#undef safe_copy_str
#undef safe_copy_cstr
	}

fini:
	return PAGE_SIZE - left;
}

/*
 * Called by the /proc file system to return a current list of ksyms.
 */

int
get_ksyms_list(char *buf, char **start, off_t offset, int length)
{
	struct module *mod;
	char *p = buf;
	int len     = 0;	/* code from  net/ipv4/proc.c */
	off_t pos   = 0;
	off_t begin = 0;

	for (mod = module_list; mod; mod = mod->next) {
		unsigned i;
		struct module_symbol *sym;

		if (!(mod->flags & MOD_RUNNING) || (mod->flags & MOD_DELETED))
			continue;

		for (i = mod->nsyms, sym = mod->syms; i > 0; --i, ++sym) {
			p = buf + len;
			if (*mod->name) {
				len += sprintf(p, "%0*lx %s\t[%s]\n",
					       (int)(2*sizeof(void*)),
					       sym->value, sym->name,
					       mod->name);
			} else {
				len += sprintf(p, "%0*lx %s\n",
					       (int)(2*sizeof(void*)),
					       sym->value, sym->name);
			}
			pos = begin + len;
			if (pos < offset) {
				len = 0;
				begin = pos;
			}
			pos = begin + len;
			if (pos > offset+length)
				goto leave_the_loop;
		}
	}
leave_the_loop:
	*start = buf + (offset - begin);
	len -= (offset - begin);
	if (len > length)
		len = length;
	return len;
}

/*
 * Gets the address for a symbol in the given module.  If modname is
 * NULL, it looks for the name in any registered symbol table.  If the
 * modname is an empty string, it looks for the symbol in kernel exported
 * symbol tables.
 */
unsigned long
get_module_symbol(char *modname, char *symname)
{
	struct module *mp;
	struct module_symbol *sym;
	int i;

	for (mp = module_list; mp; mp = mp->next) {
		if (((modname == NULL) || (strcmp(mp->name, modname) == 0)) &&
			(mp->flags & (MOD_RUNNING | MOD_DELETED)) == MOD_RUNNING &&
			(mp->nsyms > 0)) {
			for (i = mp->nsyms, sym = mp->syms;
				i > 0; --i, ++sym) {

				if (strcmp(sym->name, symname) == 0) {
					return sym->value;
				}
			}
		}
	}
	return 0;
}

#else		/* CONFIG_MODULES */

/* Dummy syscalls for people who don't want modules */

asmlinkage unsigned long
sys_create_module(const char *name_user, size_t size)
{
	return -ENOSYS;
}

asmlinkage int
sys_init_module(const char *name_user, struct module *mod_user)
{
	return -ENOSYS;
}

asmlinkage int
sys_delete_module(const char *name_user)
{
	return -ENOSYS;
}

asmlinkage int
sys_query_module(const char *name_user, int which, char *buf, size_t bufsize,
		 size_t *ret)
{
	/* Let the program know about the new interface.  Not that
	   it'll do them much good.  */
	if (which == 0)
		return 0;

	return -ENOSYS;
}

asmlinkage int
sys_get_kernel_syms(struct kernel_sym *table)
{
	return -ENOSYS;
}

#endif	/* CONFIG_MODULES */
