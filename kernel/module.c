/* Rewritten by Rusty Russell, on the backs of many others...
   Copyright (C) 2002 Richard Henderson
   Copyright (C) 2001 Rusty Russell, 2002 Rusty Russell IBM.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <linux/seq_file.h>
#include <linux/fcntl.h>
#include <linux/rcupdate.h>
#include <linux/cpu.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , a...)
#endif

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/* If this is set, the section belongs in the init part of the module */
#define INIT_OFFSET_MASK (1UL << (BITS_PER_LONG-1))

#define symbol_is(literal, string)				\
	(strcmp(MODULE_SYMBOL_PREFIX literal, (string)) == 0)

/* Protects extables and symbols lists */
static spinlock_t modlist_lock = SPIN_LOCK_UNLOCKED;

/* List of modules, protected by module_mutex AND modlist_lock */
static DECLARE_MUTEX(module_mutex);
static LIST_HEAD(modules);
static LIST_HEAD(symbols);
static LIST_HEAD(extables);

/* We require a truly strong try_module_get() */
static inline int strong_try_module_get(struct module *mod)
{
	if (mod && mod->state == MODULE_STATE_COMING)
		return 0;
	return try_module_get(mod);
}

/* Stub function for modules which don't have an initfn */
int init_module(void)
{
	return 0;
}
EXPORT_SYMBOL(init_module);

/* Find a symbol, return value and the symbol group */
static unsigned long __find_symbol(const char *name,
				   struct kernel_symbol_group **group,
				   int gplok)
{
	struct kernel_symbol_group *ks;
 
	list_for_each_entry(ks, &symbols, list) {
 		unsigned int i;

		if (ks->gplonly && !gplok)
			continue;
		for (i = 0; i < ks->num_syms; i++) {
			if (strcmp(ks->syms[i].name, name) == 0) {
				*group = ks;
				return ks->syms[i].value;
			}
		}
	}
	DEBUGP("Failed to find symbol %s\n", name);
 	return 0;
}

/* Find a symbol in this elf symbol table */
static unsigned long find_local_symbol(Elf_Shdr *sechdrs,
				       unsigned int symindex,
				       const char *strtab,
				       const char *name)
{
	unsigned int i;
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_addr;

	/* Search (defined) internal symbols first. */
	for (i = 1; i < sechdrs[symindex].sh_size/sizeof(*sym); i++) {
		if (sym[i].st_shndx != SHN_UNDEF
		    && strcmp(name, strtab + sym[i].st_name) == 0)
			return sym[i].st_value;
	}
	return 0;
}

/* Search for module by name: must hold module_mutex. */
static struct module *find_module(const char *name)
{
	struct module *mod;

	list_for_each_entry(mod, &modules, list) {
		if (strcmp(mod->name, name) == 0)
			return mod;
	}
	return NULL;
}

#ifdef CONFIG_MODULE_UNLOAD
/* Init the unload section of the module. */
static void module_unload_init(struct module *mod)
{
	unsigned int i;

	INIT_LIST_HEAD(&mod->modules_which_use_me);
	for (i = 0; i < NR_CPUS; i++)
		atomic_set(&mod->ref[i].count, 0);
	/* Backwards compatibility macros put refcount during init. */
	mod->waiter = current;
}

/* modules using other modules */
struct module_use
{
	struct list_head list;
	struct module *module_which_uses;
};

/* Does a already use b? */
static int already_uses(struct module *a, struct module *b)
{
	struct module_use *use;

	list_for_each_entry(use, &b->modules_which_use_me, list) {
		if (use->module_which_uses == a) {
			DEBUGP("%s uses %s!\n", a->name, b->name);
			return 1;
		}
	}
	DEBUGP("%s does not use %s!\n", a->name, b->name);
	return 0;
}

/* Module a uses b */
static int use_module(struct module *a, struct module *b)
{
	struct module_use *use;
	if (b == NULL || already_uses(a, b)) return 1;

	DEBUGP("Allocating new usage for %s.\n", a->name);
	use = kmalloc(sizeof(*use), GFP_ATOMIC);
	if (!use) {
		printk("%s: out of memory loading\n", a->name);
		return 0;
	}

	use->module_which_uses = a;
	list_add(&use->list, &b->modules_which_use_me);
	try_module_get(b); /* Can't fail */
	return 1;
}

/* Clear the unload stuff of the module. */
static void module_unload_free(struct module *mod)
{
	struct module *i;

	list_for_each_entry(i, &modules, list) {
		struct module_use *use;

		list_for_each_entry(use, &i->modules_which_use_me, list) {
			if (use->module_which_uses == mod) {
				DEBUGP("%s unusing %s\n", mod->name, i->name);
				module_put(i);
				list_del(&use->list);
				kfree(use);
				/* There can be at most one match. */
				break;
			}
		}
	}
}

#ifdef CONFIG_SMP
/* Thread to stop each CPU in user context. */
enum stopref_state {
	STOPREF_WAIT,
	STOPREF_PREPARE,
	STOPREF_DISABLE_IRQ,
	STOPREF_EXIT,
};

static enum stopref_state stopref_state;
static unsigned int stopref_num_threads;
static atomic_t stopref_thread_ack;

static int stopref(void *cpu)
{
	int irqs_disabled = 0;
	int prepared = 0;

	sprintf(current->comm, "kmodule%lu\n", (unsigned long)cpu);

	/* Highest priority we can manage, and move to right CPU. */
#if 0 /* FIXME */
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };
	setscheduler(current->pid, SCHED_FIFO, &param);
#endif
	set_cpus_allowed(current, 1UL << (unsigned long)cpu);

	/* Ack: we are alive */
	atomic_inc(&stopref_thread_ack);

	/* Simple state machine */
	while (stopref_state != STOPREF_EXIT) {
		if (stopref_state == STOPREF_DISABLE_IRQ && !irqs_disabled) {
			local_irq_disable();
			irqs_disabled = 1;
			/* Ack: irqs disabled. */
			atomic_inc(&stopref_thread_ack);
		} else if (stopref_state == STOPREF_PREPARE && !prepared) {
			/* Everyone is in place, hold CPU. */
			preempt_disable();
			prepared = 1;
			atomic_inc(&stopref_thread_ack);
		}
		if (irqs_disabled || prepared)
			cpu_relax();
		else
			yield();
	}

	/* Ack: we are exiting. */
	atomic_inc(&stopref_thread_ack);

	if (irqs_disabled)
		local_irq_enable();
	if (prepared)
		preempt_enable();

	return 0;
}

/* Change the thread state */
static void stopref_set_state(enum stopref_state state, int sleep)
{
	atomic_set(&stopref_thread_ack, 0);
	wmb();
	stopref_state = state;
	while (atomic_read(&stopref_thread_ack) != stopref_num_threads) {
		if (sleep)
			yield();
		else
			cpu_relax();
	}
}

/* Stop the machine.  Disables irqs. */
static int stop_refcounts(void)
{
	unsigned int i, cpu;
	unsigned long old_allowed;
	int ret = 0;

	/* One thread per cpu.  We'll do our own. */
	cpu = smp_processor_id();

	/* FIXME: racy with set_cpus_allowed. */
	old_allowed = current->cpus_allowed;
	set_cpus_allowed(current, 1UL << (unsigned long)cpu);

	atomic_set(&stopref_thread_ack, 0);
	stopref_num_threads = 0;
	stopref_state = STOPREF_WAIT;

	/* No CPUs can come up or down during this. */
	down(&cpucontrol);

	for (i = 0; i < NR_CPUS; i++) {
		if (i == cpu || !cpu_online(i))
			continue;
		ret = kernel_thread(stopref, (void *)(long)i, CLONE_KERNEL);
		if (ret < 0)
			break;
		stopref_num_threads++;
	}

	/* Wait for them all to come to life. */
	while (atomic_read(&stopref_thread_ack) != stopref_num_threads)
		yield();

	/* If some failed, kill them all. */
	if (ret < 0) {
		stopref_set_state(STOPREF_EXIT, 1);
		up(&cpucontrol);
		return ret;
	}

	/* Don't schedule us away at this point, please. */
	preempt_disable();

	/* Now they are all scheduled, make them hold the CPUs, ready. */
	stopref_set_state(STOPREF_PREPARE, 0);

	/* Make them disable irqs. */
	stopref_set_state(STOPREF_DISABLE_IRQ, 0);

	local_irq_disable();
	return 0;
}

/* Restart the machine.  Re-enables irqs. */
static void restart_refcounts(void)
{
	stopref_set_state(STOPREF_EXIT, 0);
	local_irq_enable();
	preempt_enable();
	up(&cpucontrol);
}
#else /* ...!SMP */
static inline int stop_refcounts(void)
{
	local_irq_disable();
	return 0;
}
static inline void restart_refcounts(void)
{
	local_irq_enable();
}
#endif

static unsigned int module_refcount(struct module *mod)
{
	unsigned int i, total = 0;

	for (i = 0; i < NR_CPUS; i++)
		total += atomic_read(&mod->ref[i].count);
	return total;
}

/* This exists whether we can unload or not */
static void free_module(struct module *mod);

#ifdef CONFIG_MODULE_FORCE_UNLOAD
static inline int try_force(unsigned int flags)
{
	return (flags & O_TRUNC);
}
#else
static inline int try_force(unsigned int flags)
{
	return 0;
}
#endif /* CONFIG_MODULE_FORCE_UNLOAD */

/* Stub function for modules which don't have an exitfn */
void cleanup_module(void)
{
}
EXPORT_SYMBOL(cleanup_module);

asmlinkage long
sys_delete_module(const char *name_user, unsigned int flags)
{
	struct module *mod;
	char name[MODULE_NAME_LEN];
	int ret, forced = 0;

	if (!capable(CAP_SYS_MODULE))
		return -EPERM;

	if (strncpy_from_user(name, name_user, MODULE_NAME_LEN-1) < 0)
		return -EFAULT;
	name[MODULE_NAME_LEN-1] = '\0';

	if (down_interruptible(&module_mutex) != 0)
		return -EINTR;

	mod = find_module(name);
	if (!mod) {
		ret = -ENOENT;
		goto out;
	}

	if (!list_empty(&mod->modules_which_use_me)) {
		/* Other modules depend on us: get rid of them first. */
		ret = -EWOULDBLOCK;
		goto out;
	}

	/* Already dying? */
	if (mod->state == MODULE_STATE_GOING) {
		/* FIXME: if (force), slam module count and wake up
                   waiter --RR */
		DEBUGP("%s already dying\n", mod->name);
		ret = -EBUSY;
		goto out;
	}

	/* Coming up?  Allow force on stuck modules. */
	if (mod->state == MODULE_STATE_COMING) {
		forced = try_force(flags);
		if (!forced) {
			/* This module can't be removed */
			ret = -EBUSY;
			goto out;
		}
	}

	/* If it has an init func, it must have an exit func to unload */
	if ((mod->init != init_module && mod->exit == cleanup_module)
	    || mod->unsafe) {
		forced = try_force(flags);
		if (!forced) {
			/* This module can't be removed */
			ret = -EBUSY;
			goto out;
		}
	}
	/* Stop the machine so refcounts can't move: irqs disabled. */
	DEBUGP("Stopping refcounts...\n");
	ret = stop_refcounts();
	if (ret != 0)
		goto out;

	/* If it's not unused, quit unless we are told to block. */
	if ((flags & O_NONBLOCK) && module_refcount(mod) != 0) {
		forced = try_force(flags);
		if (!forced)
			ret = -EWOULDBLOCK;
	} else {
		mod->waiter = current;
		mod->state = MODULE_STATE_GOING;
	}
	restart_refcounts();

	if (ret != 0)
		goto out;

	if (forced)
		goto destroy;

	/* Since we might sleep for some time, drop the semaphore first */
	up(&module_mutex);
	for (;;) {
		DEBUGP("Looking at refcount...\n");
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (module_refcount(mod) == 0)
			break;
		schedule();
	}
	current->state = TASK_RUNNING;

	DEBUGP("Regrabbing mutex...\n");
	down(&module_mutex);

 destroy:
	/* Final destruction now noone is using it. */
	mod->exit();
	free_module(mod);

 out:
	up(&module_mutex);
	return ret;
}

static void print_unload_info(struct seq_file *m, struct module *mod)
{
	struct module_use *use;
	int printed_something = 0;

	seq_printf(m, " %u ", module_refcount(mod));

	/* Always include a trailing , so userspace can differentiate
           between this and the old multi-field proc format. */
	list_for_each_entry(use, &mod->modules_which_use_me, list) {
		printed_something = 1;
		seq_printf(m, "%s,", use->module_which_uses->name);
	}

	if (mod->unsafe) {
		printed_something = 1;
		seq_printf(m, "[unsafe],");
	}

	if (mod->init != init_module && mod->exit == cleanup_module) {
		printed_something = 1;
		seq_printf(m, "[permanent],");
	}

	if (!printed_something)
		seq_printf(m, "-");
}

void __symbol_put(const char *symbol)
{
	struct kernel_symbol_group *ksg;
	unsigned long flags;

	spin_lock_irqsave(&modlist_lock, flags);
	if (!__find_symbol(symbol, &ksg, 1))
		BUG();
	module_put(ksg->owner);
	spin_unlock_irqrestore(&modlist_lock, flags);
}
EXPORT_SYMBOL(__symbol_put);

void symbol_put_addr(void *addr)
{
	struct kernel_symbol_group *ks;
	unsigned long flags;

	spin_lock_irqsave(&modlist_lock, flags);
	list_for_each_entry(ks, &symbols, list) {
 		unsigned int i;

		for (i = 0; i < ks->num_syms; i++) {
			if (ks->syms[i].value == (unsigned long)addr) {
				module_put(ks->owner);
				spin_unlock_irqrestore(&modlist_lock, flags);
				return;
			}
		}
	}
	spin_unlock_irqrestore(&modlist_lock, flags);
	BUG();
}
EXPORT_SYMBOL_GPL(symbol_put_addr);

#else /* !CONFIG_MODULE_UNLOAD */
static void print_unload_info(struct seq_file *m, struct module *mod)
{
	/* We don't know the usage count, or what modules are using. */
	seq_printf(m, " - -");
}

static inline void module_unload_free(struct module *mod)
{
}

static inline int use_module(struct module *a, struct module *b)
{
	return strong_try_module_get(b);
}

static inline void module_unload_init(struct module *mod)
{
}

asmlinkage long
sys_delete_module(const char *name_user, unsigned int flags)
{
	return -ENOSYS;
}

#endif /* CONFIG_MODULE_UNLOAD */

#ifdef CONFIG_OBSOLETE_MODPARM
static int param_set_byte(const char *val, struct kernel_param *kp)  
{
	char *endp;
	long l;

	if (!val) return -EINVAL;
	l = simple_strtol(val, &endp, 0);
	if (endp == val || *endp || ((char)l != l))
		return -EINVAL;
	*((char *)kp->arg) = l;
	return 0;
}

/* Bounds checking done below */
static int obsparm_copy_string(const char *val, struct kernel_param *kp)
{
	strcpy(kp->arg, val);
	return 0;
}

extern int set_obsolete(const char *val, struct kernel_param *kp)
{
	unsigned int min, max;
	unsigned int size, maxsize;
	char *endp;
	const char *p;
	struct obsolete_modparm *obsparm = kp->arg;

	if (!val) {
		printk(KERN_ERR "Parameter %s needs an argument\n", kp->name);
		return -EINVAL;
	}

	/* type is: [min[-max]]{b,h,i,l,s} */
	p = obsparm->type;
	min = simple_strtol(p, &endp, 10);
	if (endp == obsparm->type)
		min = max = 1;
	else if (*endp == '-') {
		p = endp+1;
		max = simple_strtol(p, &endp, 10);
	} else
		max = min;
	switch (*endp) {
	case 'b':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   1, param_set_byte);
	case 'h':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   sizeof(short), param_set_short);
	case 'i':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   sizeof(int), param_set_int);
	case 'l':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   sizeof(long), param_set_long);
	case 's':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   sizeof(char *), param_set_charp);

	case 'c':
		/* Undocumented: 1-5c50 means 1-5 strings of up to 49 chars,
		   and the decl is "char xxx[5][50];" */
		p = endp+1;
		maxsize = simple_strtol(p, &endp, 10);
		/* We check lengths here (yes, this is a hack). */
		p = val;
		while (p[size = strcspn(p, ",")]) {
			if (size >= maxsize) 
				goto oversize;
			p += size+1;
		}
		if (size >= maxsize) 
			goto oversize;
		return param_array(kp->name, val, min, max, obsparm->addr,
				   maxsize, obsparm_copy_string);
	}
	printk(KERN_ERR "Unknown obsolete parameter type %s\n", obsparm->type);
	return -EINVAL;
 oversize:
	printk(KERN_ERR
	       "Parameter %s doesn't fit in %u chars.\n", kp->name, maxsize);
	return -EINVAL;
}

static int obsolete_params(const char *name,
			   char *args,
			   struct obsolete_modparm obsparm[],
			   unsigned int num,
			   Elf_Shdr *sechdrs,
			   unsigned int symindex,
			   const char *strtab)
{
	struct kernel_param *kp;
	unsigned int i;
	int ret;

	kp = kmalloc(sizeof(kp[0]) * num, GFP_KERNEL);
	if (!kp)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		char sym_name[128 + sizeof(MODULE_SYMBOL_PREFIX)];

		snprintf(sym_name, sizeof(sym_name), "%s%s",
			 MODULE_SYMBOL_PREFIX, obsparm[i].name);

		kp[i].name = obsparm[i].name;
		kp[i].perm = 000;
		kp[i].set = set_obsolete;
		kp[i].get = NULL;
		obsparm[i].addr
			= (void *)find_local_symbol(sechdrs, symindex, strtab,
						    sym_name);
		if (!obsparm[i].addr) {
			printk("%s: falsely claims to have parameter %s\n",
			       name, obsparm[i].name);
			ret = -EINVAL;
			goto out;
		}
		kp[i].arg = &obsparm[i];
	}

	ret = parse_args(name, args, kp, num, NULL);
 out:
	kfree(kp);
	return ret;
}
#else
static int obsolete_params(const char *name,
			   char *args,
			   struct obsolete_modparm obsparm[],
			   unsigned int num,
			   Elf_Shdr *sechdrs,
			   unsigned int symindex,
			   const char *strtab)
{
	if (num != 0)
		printk(KERN_WARNING "%s: Ignoring obsolete parameters\n",
		       name);
	return 0;
}
#endif /* CONFIG_OBSOLETE_MODPARM */

/* Resolve a symbol for this module.  I.e. if we find one, record usage.
   Must be holding module_mutex. */
static unsigned long resolve_symbol(Elf_Shdr *sechdrs,
				    unsigned int symindex,
				    const char *strtab,
				    const char *name,
				    struct module *mod)
{
	struct kernel_symbol_group *ksg;
	unsigned long ret;

	spin_lock_irq(&modlist_lock);
	ret = __find_symbol(name, &ksg, mod->license_gplok);
	if (ret) {
		/* This can fail due to OOM, or module unloading */
		if (!use_module(mod, ksg->owner))
			ret = 0;
	}
	spin_unlock_irq(&modlist_lock);
	return ret;
}

/* Free a module, remove from lists, etc (must hold module mutex). */
static void free_module(struct module *mod)
{
	/* Delete from various lists */
	spin_lock_irq(&modlist_lock);
	list_del(&mod->list);
	list_del(&mod->symbols.list);
	list_del(&mod->gpl_symbols.list);
	list_del(&mod->extable.list);
	spin_unlock_irq(&modlist_lock);

	/* Module unload stuff */
	module_unload_free(mod);

	/* This may be NULL, but that's OK */
	module_free(mod, mod->module_init);
	kfree(mod->args);

	/* Finally, free the core (containing the module structure) */
	module_free(mod, mod->module_core);
}

void *__symbol_get(const char *symbol)
{
	struct kernel_symbol_group *ksg;
	unsigned long value, flags;

	spin_lock_irqsave(&modlist_lock, flags);
	value = __find_symbol(symbol, &ksg, 1);
	if (value && !strong_try_module_get(ksg->owner))
		value = 0;
	spin_unlock_irqrestore(&modlist_lock, flags);

	return (void *)value;
}
EXPORT_SYMBOL_GPL(__symbol_get);

/* Deal with the given section */
static int handle_section(const char *name,
			  Elf_Shdr *sechdrs,
			  unsigned int strindex,
			  unsigned int symindex,
			  unsigned int i,
			  struct module *mod)
{
	int ret;
	const char *strtab = (char *)sechdrs[strindex].sh_addr;

	switch (sechdrs[i].sh_type) {
	case SHT_REL:
		ret = apply_relocate(sechdrs, strtab, symindex, i, mod);
		break;
	case SHT_RELA:
		ret = apply_relocate_add(sechdrs, strtab, symindex, i, mod);
		break;
	default:
		DEBUGP("Ignoring section %u: %s\n", i,
		       sechdrs[i].sh_type==SHT_NULL ? "NULL":
		       sechdrs[i].sh_type==SHT_PROGBITS ? "PROGBITS":
		       sechdrs[i].sh_type==SHT_SYMTAB ? "SYMTAB":
		       sechdrs[i].sh_type==SHT_STRTAB ? "STRTAB":
		       sechdrs[i].sh_type==SHT_RELA ? "RELA":
		       sechdrs[i].sh_type==SHT_HASH ? "HASH":
		       sechdrs[i].sh_type==SHT_DYNAMIC ? "DYNAMIC":
		       sechdrs[i].sh_type==SHT_NOTE ? "NOTE":
		       sechdrs[i].sh_type==SHT_NOBITS ? "NOBITS":
		       sechdrs[i].sh_type==SHT_REL ? "REL":
		       sechdrs[i].sh_type==SHT_SHLIB ? "SHLIB":
		       sechdrs[i].sh_type==SHT_DYNSYM ? "DYNSYM":
		       sechdrs[i].sh_type==SHT_NUM ? "NUM":
		       "UNKNOWN");
		ret = 0;
	}
	return ret;
}

/* Change all symbols so that sh_value encodes the pointer directly. */
static int simplify_symbols(Elf_Shdr *sechdrs,
			    unsigned int symindex,
			    unsigned int strindex,
			    struct module *mod)
{
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_addr;
	const char *strtab = (char *)sechdrs[strindex].sh_addr;
	unsigned int i, n = sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	int ret = 0;

	for (i = 1; i < n; i++) {
		switch (sym[i].st_shndx) {
		case SHN_COMMON:
			/* We compiled with -fno-common.  These are not
			   supposed to happen.  */
			DEBUGP("Common symbol: %s\n", strtab + sym[i].st_name);
			ret = -ENOEXEC;
			break;

		case SHN_ABS:
			/* Don't need to do anything */
			DEBUGP("Absolute symbol: 0x%08lx\n",
			       (long)sym[i].st_value);
			break;

		case SHN_UNDEF:
			sym[i].st_value
			  = resolve_symbol(sechdrs, symindex, strtab,
					   strtab + sym[i].st_name, mod);

			/* Ok if resolved.  */
			if (sym[i].st_value != 0)
				break;
			/* Ok if weak.  */
			if (ELF_ST_BIND(sym[i].st_info) == STB_WEAK)
				break;

			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       mod->name, strtab + sym[i].st_name);
			ret = -ENOENT;
			break;

		default:
			sym[i].st_value 
				= (unsigned long)
				(sechdrs[sym[i].st_shndx].sh_addr
				 + sym[i].st_value);
			break;
		}
	}

	return ret;
}

/* Update size with this section: return offset. */
static long get_offset(unsigned long *size, Elf_Shdr *sechdr)
{
	long ret;

	ret = ALIGN(*size, sechdr->sh_addralign ?: 1);
	*size = ret + sechdr->sh_size;
	return ret;
}

/* Lay out the SHF_ALLOC sections in a way not dissimilar to how ld
   might -- code, read-only data, read-write data, small data.  Tally
   sizes, and place the offsets into sh_entsize fields: high bit means it
   belongs in init. */
static void layout_sections(struct module *mod,
			    const Elf_Ehdr *hdr,
			    Elf_Shdr *sechdrs,
			    const char *secstrings)
{
	static unsigned long const masks[][2] = {
		{ SHF_EXECINSTR | SHF_ALLOC, ARCH_SHF_SMALL },
		{ SHF_ALLOC, SHF_WRITE | ARCH_SHF_SMALL },
		{ SHF_WRITE | SHF_ALLOC, ARCH_SHF_SMALL },
		{ ARCH_SHF_SMALL | SHF_ALLOC, 0 }
	};
	unsigned int m, i;

	for (i = 0; i < hdr->e_shnum; i++)
		sechdrs[i].sh_entsize = ~0UL;

	DEBUGP("Core section allocation order:\n");
	for (m = 0; m < ARRAY_SIZE(masks); ++m) {
		for (i = 0; i < hdr->e_shnum; ++i) {
			Elf_Shdr *s = &sechdrs[i];

			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL
			    || strstr(secstrings + s->sh_name, ".init"))
				continue;
			s->sh_entsize = get_offset(&mod->core_size, s);
			DEBUGP("\t%s\n", name);
		}
	}

	DEBUGP("Init section allocation order:\n");
	for (m = 0; m < ARRAY_SIZE(masks); ++m) {
		for (i = 0; i < hdr->e_shnum; ++i) {
			Elf_Shdr *s = &sechdrs[i];

			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL
			    || !strstr(secstrings + s->sh_name, ".init"))
				continue;
			s->sh_entsize = (get_offset(&mod->init_size, s)
					 | INIT_OFFSET_MASK);
			DEBUGP("\t%s\n", name);
		}
	}
}

static inline int license_is_gpl_compatible(const char *license)
{
	return (strcmp(license, "GPL") == 0
		|| strcmp(license, "GPL v2") == 0
		|| strcmp(license, "GPL and additional rights") == 0
		|| strcmp(license, "Dual BSD/GPL") == 0
		|| strcmp(license, "Dual MPL/GPL") == 0);
}

static void set_license(struct module *mod, Elf_Shdr *sechdrs, int licenseidx)
{
	char *license;

	if (licenseidx) 
		license = (char *)sechdrs[licenseidx].sh_addr;
	else
		license = "unspecified";

	mod->license_gplok = license_is_gpl_compatible(license);
	if (!mod->license_gplok) {
		printk(KERN_WARNING "%s: module license '%s' taints kernel.\n",
		       mod->name, license);
		tainted |= TAINT_PROPRIETARY_MODULE;
	}
}

/* From init/vermagic.o */
extern char vermagic[];

/* Allocate and load the module: note that size of section 0 is always
   zero, and we rely on this for optional sections. */
static struct module *load_module(void *umod,
				  unsigned long len,
				  const char *uargs)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *sechdrs;
	char *secstrings, *args;
	unsigned int i, symindex, exportindex, strindex, setupindex, exindex,
		modindex, obsparmindex, licenseindex, gplindex, vmagindex;
	long arglen;
	struct module *mod;
	long err = 0;
	void *ptr = NULL; /* Stops spurious gcc uninitialized warning */

	DEBUGP("load_module: umod=%p, len=%lu, uargs=%p\n",
	       umod, len, uargs);
	if (len < sizeof(*hdr))
		return ERR_PTR(-ENOEXEC);

	/* Suck in entire file: we'll want most of it. */
	/* vmalloc barfs on "unusual" numbers.  Check here */
	if (len > 64 * 1024 * 1024 || (hdr = vmalloc(len)) == NULL)
		return ERR_PTR(-ENOMEM);
	if (copy_from_user(hdr, umod, len) != 0) {
		err = -EFAULT;
		goto free_hdr;
	}

	/* Sanity checks against insmoding binaries or wrong arch,
           weird elf version */
	if (memcmp(hdr->e_ident, ELFMAG, 4) != 0
	    || hdr->e_type != ET_REL
	    || !elf_check_arch(hdr)
	    || hdr->e_shentsize != sizeof(*sechdrs)) {
		err = -ENOEXEC;
		goto free_hdr;
	}

	/* Convenience variables */
	sechdrs = (void *)hdr + hdr->e_shoff;
	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

	/* May not export symbols, or have setup params, so these may
           not exist */
	exportindex = setupindex = obsparmindex = gplindex = licenseindex = 0;

	/* And these should exist, but gcc whinges if we don't init them */
	symindex = strindex = exindex = modindex = vmagindex = 0;

	/* Find where important sections are */
	for (i = 1; i < hdr->e_shnum; i++) {
		/* Mark all sections sh_addr with their address in the
		   temporary image. */
		sechdrs[i].sh_addr = (size_t)hdr + sechdrs[i].sh_offset;

		if (sechdrs[i].sh_type == SHT_SYMTAB) {
			/* Internal symbols */
			DEBUGP("Symbol table in section %u\n", i);
			symindex = i;
			/* Strings */
			strindex = sechdrs[i].sh_link;
			DEBUGP("String table found in section %u\n", strindex);
		} else if (strcmp(secstrings+sechdrs[i].sh_name,
				  ".gnu.linkonce.this_module") == 0) {
			/* The module struct */
			DEBUGP("Module in section %u\n", i);
			modindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name, "__ksymtab")
			   == 0) {
			/* Exported symbols. */
			DEBUGP("EXPORT table in section %u\n", i);
			exportindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name, "__param")
			   == 0) {
			/* Setup parameter info */
			DEBUGP("Setup table found in section %u\n", i);
			setupindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name, "__ex_table")
			   == 0) {
			/* Exception table */
			DEBUGP("Exception table found in section %u\n", i);
			exindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name, "__obsparm")
			   == 0) {
			/* Obsolete MODULE_PARM() table */
			DEBUGP("Obsolete param found in section %u\n", i);
			obsparmindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name,".init.license")
			   == 0) {
			/* MODULE_LICENSE() */
			DEBUGP("Licence found in section %u\n", i);
			licenseindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name,
				  "__gpl_ksymtab") == 0) {
			/* EXPORT_SYMBOL_GPL() */
			DEBUGP("GPL symbols found in section %u\n", i);
			gplindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name,
				  "__vermagic") == 0) {
			/* Version magic. */
			DEBUGP("Version magic found in section %u\n", i);
			vmagindex = i;
		}
#ifdef CONFIG_KALLSYMS
		/* symbol and string tables for decoding later. */
		if (sechdrs[i].sh_type == SHT_SYMTAB || i == strindex)
			sechdrs[i].sh_flags |= SHF_ALLOC;
#endif
#ifndef CONFIG_MODULE_UNLOAD
		/* Don't load .exit sections */
		if (strstr(secstrings+sechdrs[i].sh_name, ".exit"))
			sechdrs[i].sh_flags &= ~(unsigned long)SHF_ALLOC;
#endif
	}

	if (!modindex) {
		printk(KERN_WARNING "No module found in object\n");
		err = -ENOEXEC;
		goto free_hdr;
	}
	mod = (void *)sechdrs[modindex].sh_addr;

	/* This is allowed: modprobe --force will strip it. */
	if (!vmagindex) {
		tainted |= TAINT_FORCED_MODULE;
		printk(KERN_WARNING "%s: no version magic, tainting kernel.\n",
		       mod->name);
	} else if (strcmp((char *)sechdrs[vmagindex].sh_addr, vermagic) != 0) {
		printk(KERN_ERR "%s: version magic '%s' should be '%s'\n",
		       mod->name, (char*)sechdrs[vmagindex].sh_addr, vermagic);
		err = -ENOEXEC;
		goto free_hdr;
	}

	/* Now copy in args */
	arglen = strlen_user(uargs);
	if (!arglen) {
		err = -EFAULT;
		goto free_hdr;
	}
	args = kmalloc(arglen, GFP_KERNEL);
	if (!args) {
		err = -ENOMEM;
		goto free_hdr;
	}
	if (copy_from_user(args, uargs, arglen) != 0) {
		err = -EFAULT;
		goto free_mod;
	}

	if (find_module(mod->name)) {
		err = -EEXIST;
		goto free_mod;
	}

	mod->state = MODULE_STATE_COMING;

	/* Allow arches to frob section contents and sizes.  */
	err = module_frob_arch_sections(hdr, sechdrs, secstrings, mod);
	if (err < 0)
		goto free_mod;

	/* Determine total sizes, and put offsets in sh_entsize.  For now
	   this is done generically; there doesn't appear to be any
	   special cases for the architectures. */
	layout_sections(mod, hdr, sechdrs, secstrings);

	/* Do the allocs. */
	ptr = module_alloc(mod->core_size);
	if (!ptr) {
		err = -ENOMEM;
		goto free_mod;
	}
	memset(ptr, 0, mod->core_size);
	mod->module_core = ptr;

	ptr = module_alloc(mod->init_size);
	if (!ptr && mod->init_size) {
		err = -ENOMEM;
		goto free_core;
	}
	memset(ptr, 0, mod->init_size);
	mod->module_init = ptr;

	/* Transfer each section which specifies SHF_ALLOC */
	for (i = 0; i < hdr->e_shnum; i++) {
		void *dest;

		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		if (sechdrs[i].sh_entsize & INIT_OFFSET_MASK)
			dest = mod->module_init
				+ (sechdrs[i].sh_entsize & ~INIT_OFFSET_MASK);
		else
			dest = mod->module_core + sechdrs[i].sh_entsize;

		if (sechdrs[i].sh_type != SHT_NOBITS)
			memcpy(dest, (void *)sechdrs[i].sh_addr,
			       sechdrs[i].sh_size);
		/* Update sh_addr to point to copy in image. */
		sechdrs[i].sh_addr = (unsigned long)dest;
	}
	/* Module has been moved. */
	mod = (void *)sechdrs[modindex].sh_addr;

	/* Now we've moved module, initialize linked lists, etc. */
	module_unload_init(mod);

	/* Set up license info based on contents of section */
	set_license(mod, sechdrs, licenseindex);

	/* Fix up syms, so that st_value is a pointer to location. */
	err = simplify_symbols(sechdrs, symindex, strindex, mod);
	if (err < 0)
		goto cleanup;

	/* Set up EXPORTed & EXPORT_GPLed symbols (section 0 is 0 length) */
	mod->symbols.num_syms = (sechdrs[exportindex].sh_size
				 / sizeof(*mod->symbols.syms));
	mod->symbols.syms = (void *)sechdrs[exportindex].sh_addr;
	mod->gpl_symbols.num_syms = (sechdrs[gplindex].sh_size
				 / sizeof(*mod->symbols.syms));
	mod->gpl_symbols.syms = (void *)sechdrs[gplindex].sh_addr;

	/* Set up exception table */
	if (exindex) {
		/* FIXME: Sort exception table. */
		mod->extable.num_entries = (sechdrs[exindex].sh_size
					    / sizeof(struct
						     exception_table_entry));
		mod->extable.entry = (void *)sechdrs[exindex].sh_addr;
	}

	/* Now handle each section. */
	for (i = 1; i < hdr->e_shnum; i++) {
		err = handle_section(secstrings + sechdrs[i].sh_name,
				     sechdrs, strindex, symindex, i, mod);
		if (err < 0)
			goto cleanup;
	}

#ifdef CONFIG_KALLSYMS
	mod->symtab = (void *)sechdrs[symindex].sh_addr;
	mod->num_syms = sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	mod->strtab = (void *)sechdrs[strindex].sh_addr;
#endif
	err = module_finalize(hdr, sechdrs, mod);
	if (err < 0)
		goto cleanup;

	mod->args = args;
	if (obsparmindex) {
		err = obsolete_params(mod->name, mod->args,
				      (struct obsolete_modparm *)
				      sechdrs[obsparmindex].sh_addr,
				      sechdrs[obsparmindex].sh_size
				      / sizeof(struct obsolete_modparm),
				      sechdrs, symindex,
				      (char *)sechdrs[strindex].sh_addr);
	} else {
		/* Size of section 0 is 0, so this works well if no params */
		err = parse_args(mod->name, mod->args,
				 (struct kernel_param *)
				 sechdrs[setupindex].sh_addr,
				 sechdrs[setupindex].sh_size
				 / sizeof(struct kernel_param),
				 NULL);
	}
	if (err < 0)
		goto cleanup;

	/* Get rid of temporary copy */
	vfree(hdr);

	/* Done! */
	return mod;

 cleanup:
	module_unload_free(mod);
	module_free(mod, mod->module_init);
 free_core:
	module_free(mod, mod->module_core);
 free_mod:
	kfree(args);
 free_hdr:
	vfree(hdr);
	if (err < 0) return ERR_PTR(err);
	else return ptr;
}

/* This is where the real work happens */
asmlinkage long
sys_init_module(void *umod,
		unsigned long len,
		const char *uargs)
{
	struct module *mod;
	int ret;

	/* Must have permission */
	if (!capable(CAP_SYS_MODULE))
		return -EPERM;

	/* Only one module load at a time, please */
	if (down_interruptible(&module_mutex) != 0)
		return -EINTR;

	/* Do all the hard work */
	mod = load_module(umod, len, uargs);
	if (IS_ERR(mod)) {
		up(&module_mutex);
		return PTR_ERR(mod);
	}

	/* Flush the instruction cache, since we've played with text */
	if (mod->module_init)
		flush_icache_range((unsigned long)mod->module_init,
				   (unsigned long)mod->module_init
				   + mod->init_size);
	flush_icache_range((unsigned long)mod->module_core,
			   (unsigned long)mod->module_core + mod->core_size);

	/* Now sew it into the lists.  They won't access us, since
           strong_try_module_get() will fail. */
	spin_lock_irq(&modlist_lock);
	list_add(&mod->extable.list, &extables);
	list_add_tail(&mod->symbols.list, &symbols);
	list_add_tail(&mod->gpl_symbols.list, &symbols);
	list_add(&mod->list, &modules);
	spin_unlock_irq(&modlist_lock);

	/* Drop lock so they can recurse */
	up(&module_mutex);

	/* Start the module */
	ret = mod->init();
	if (ret < 0) {
		/* Init routine failed: abort.  Try to protect us from
                   buggy refcounters. */
		mod->state = MODULE_STATE_GOING;
		synchronize_kernel();
		if (mod->unsafe)
			printk(KERN_ERR "%s: module is now stuck!\n",
			       mod->name);
		else {
			down(&module_mutex);
			free_module(mod);
			up(&module_mutex);
		}
		return ret;
	}

	/* Now it's a first class citizen! */
	mod->state = MODULE_STATE_LIVE;
	module_free(mod, mod->module_init);
	mod->module_init = NULL;
	mod->init_size = 0;

	return 0;
}

static inline int within(unsigned long addr, void *start, unsigned long size)
{
	return ((void *)addr >= start && (void *)addr < start + size);
}

#ifdef CONFIG_KALLSYMS
static const char *get_ksymbol(struct module *mod,
			       unsigned long addr,
			       unsigned long *size,
			       unsigned long *offset)
{
	unsigned int i, best = 0;
	unsigned long nextval;

	/* At worse, next value is at end of module */
	if (within(addr, mod->module_init, mod->init_size))
		nextval = (unsigned long)mod->module_core+mod->core_size;
	else 
		nextval = (unsigned long)mod->module_init+mod->init_size;

	/* Scan for closest preceeding symbol, and next symbol. (ELF
           starts real symbols at 1). */
	for (i = 1; i < mod->num_syms; i++) {
		if (mod->symtab[i].st_shndx == SHN_UNDEF)
			continue;

		if (mod->symtab[i].st_value <= addr
		    && mod->symtab[i].st_value > mod->symtab[best].st_value)
			best = i;
		if (mod->symtab[i].st_value > addr
		    && mod->symtab[i].st_value < nextval)
			nextval = mod->symtab[i].st_value;
	}

	if (!best)
		return NULL;

	*size = nextval - mod->symtab[best].st_value;
	*offset = addr - mod->symtab[best].st_value;
	return mod->strtab + mod->symtab[best].st_name;
}

/* For kallsyms to ask for address resolution.  NULL means not found.
   We don't lock, as this is used for oops resolution and races are a
   lesser concern. */
const char *module_address_lookup(unsigned long addr,
				  unsigned long *size,
				  unsigned long *offset,
				  char **modname)
{
	struct module *mod;

	list_for_each_entry(mod, &modules, list) {
		if (within(addr, mod->module_init, mod->init_size)
		    || within(addr, mod->module_core, mod->core_size)) {
			*modname = mod->name;
			return get_ksymbol(mod, addr, size, offset);
		}
	}
	return NULL;
}
#endif /* CONFIG_KALLSYMS */

/* Called by the /proc file system to return a list of modules. */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct list_head *i;
	loff_t n = 0;

	down(&module_mutex);
	list_for_each(i, &modules) {
		if (n++ == *pos)
			break;
	}
	if (i == &modules)
		return NULL;
	return i;
}

static void *m_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct list_head *i = p;
	(*pos)++;
	if (i->next == &modules)
		return NULL;
	return i->next;
}

static void m_stop(struct seq_file *m, void *p)
{
	up(&module_mutex);
}

static int m_show(struct seq_file *m, void *p)
{
	struct module *mod = list_entry(p, struct module, list);
	seq_printf(m, "%s %lu",
		   mod->name, mod->init_size + mod->core_size);
	print_unload_info(m, mod);

	/* Informative for users. */
	seq_printf(m, " %s",
		   mod->state == MODULE_STATE_GOING ? "Unloading":
		   mod->state == MODULE_STATE_COMING ? "Loading":
		   "Live");
	/* Used by oprofile and other similar tools. */
	seq_printf(m, " 0x%p", mod->module_core);

	seq_printf(m, "\n");
	return 0;
}

/* Format: modulename size refcount deps

   Where refcount is a number or -, and deps is a comma-separated list
   of depends or -.
*/
struct seq_operations modules_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= m_show
};

/* Given an address, look for it in the module exception tables. */
const struct exception_table_entry *search_module_extables(unsigned long addr)
{
	unsigned long flags;
	const struct exception_table_entry *e = NULL;
	struct exception_table *i;

	spin_lock_irqsave(&modlist_lock, flags);
	list_for_each_entry(i, &extables, list) {
		if (i->num_entries == 0)
			continue;
				
		e = search_extable(i->entry, i->entry+i->num_entries-1, addr);
		if (e)
			break;
	}
	spin_unlock_irqrestore(&modlist_lock, flags);

	/* Now, if we found one, we are running inside it now, hence
           we cannot unload the module, hence no refcnt needed. */
	return e;
}

/* Is this a valid kernel address?  We don't grab the lock: we are oopsing. */
int module_text_address(unsigned long addr)
{
	struct module *mod;

	list_for_each_entry(mod, &modules, list)
		if (within(addr, mod->module_init, mod->init_size)
		    || within(addr, mod->module_core, mod->core_size))
			return 1;
	return 0;
}

/* Provided by the linker */
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];
extern const struct kernel_symbol __start___gpl_ksymtab[];
extern const struct kernel_symbol __stop___gpl_ksymtab[];

static struct kernel_symbol_group kernel_symbols, kernel_gpl_symbols;

static int __init symbols_init(void)
{
	/* Add kernel symbols to symbol table */
	kernel_symbols.num_syms = (__stop___ksymtab - __start___ksymtab);
	kernel_symbols.syms = __start___ksymtab;
	kernel_symbols.gplonly = 0;
	list_add(&kernel_symbols.list, &symbols);
	kernel_gpl_symbols.num_syms = (__stop___gpl_ksymtab
				       - __start___gpl_ksymtab);
	kernel_gpl_symbols.syms = __start___gpl_ksymtab;
	kernel_gpl_symbols.gplonly = 1;
	list_add(&kernel_gpl_symbols.list, &symbols);

	return 0;
}

__initcall(symbols_init);
