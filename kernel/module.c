/* Rewritten by Rusty Russell, on the backs of many others...
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
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , a...)
#endif

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];

/* Protects extables and symbol tables */
spinlock_t modlist_lock = SPIN_LOCK_UNLOCKED;

/* The exception and symbol tables: start with kernel only. */
LIST_HEAD(extables);
static LIST_HEAD(symbols);

static struct exception_table kernel_extable;
static struct kernel_symbol_group kernel_symbols;

/* List of modules, protected by module_mutex */
static DECLARE_MUTEX(module_mutex);
LIST_HEAD(modules); /* FIXME: Accessed w/o lock on oops by some archs */

/* Convenient structure for holding init and core sizes */
struct sizes
{
	unsigned long init_size;
	unsigned long core_size;
};

/* Find a symbol, return value and the symbol group */
static unsigned long __find_symbol(const char *name,
				   struct kernel_symbol_group **group)
{
	struct kernel_symbol_group *ks;
 
	list_for_each_entry(ks, &symbols, list) {
 		unsigned int i;

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
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_offset;

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
	set_cpus_allowed(current, 1 << (unsigned long)cpu);

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
	set_cpus_allowed(current, 1 << (unsigned long)cpu);

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

asmlinkage long
sys_delete_module(const char *name_user, unsigned int flags)
{
	struct module *mod;
	char name[MODULE_NAME_LEN];
	int ret;

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

	/* Already dying? */
	if (!mod->live) {
		DEBUGP("%s already dying\n", mod->name);
		ret = -EBUSY;
		goto out;
	}

	if (!mod->exit || mod->unsafe) {
		/* This module can't be removed */
		ret = -EBUSY;
		goto out;
	}
	if (!list_empty(&mod->modules_which_use_me)) {
		/* Other modules depend on us: get rid of them first. */
		ret = -EWOULDBLOCK;
		goto out;
	}

	/* Stop the machine so refcounts can't move: irqs disabled. */
	DEBUGP("Stopping refcounts...\n");
	ret = stop_refcounts();
	if (ret != 0)
		goto out;

	/* If it's not unused, quit unless we are told to block. */
	if ((flags & O_NONBLOCK) && module_refcount(mod) != 0)
		ret = -EWOULDBLOCK;
	else {
		mod->waiter = current;
		mod->live = 0;
	}
	restart_refcounts();

	if (ret != 0)
		goto out;

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

	/* Final destruction now noone is using it. */
	mod->exit();
	free_module(mod);
	ret = 0;

 out:
	up(&module_mutex);
	return ret;
}

static void print_unload_info(struct seq_file *m, struct module *mod)
{
	struct module_use *use;

	seq_printf(m, " %u", module_refcount(mod));

	list_for_each_entry(use, &mod->modules_which_use_me, list)
		seq_printf(m, " %s", use->module_which_uses->name);

	if (mod->unsafe)
		seq_printf(m, " [unsafe]");

	if (!mod->exit)
		seq_printf(m, " [permanent]");

	seq_printf(m, "\n");
}

void __symbol_put(const char *symbol)
{
	struct kernel_symbol_group *ksg;
	unsigned long flags;

	spin_lock_irqsave(&modlist_lock, flags);
	if (!__find_symbol(symbol, &ksg))
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
	seq_printf(m, "\n");
}

static inline void module_unload_free(struct module *mod)
{
}

static inline int use_module(struct module *a, struct module *b)
{
	return try_module_get(b);
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

/* Find an symbol for this module (ie. resolve internals first).
   It we find one, record usage.  Must be holding module_mutex. */
unsigned long find_symbol_internal(Elf_Shdr *sechdrs,
				   unsigned int symindex,
				   const char *strtab,
				   const char *name,
				   struct module *mod,
				   struct kernel_symbol_group **ksg)
{
	unsigned long ret;

	ret = find_local_symbol(sechdrs, symindex, strtab, name);
	if (ret) {
		*ksg = NULL;
		return ret;
	}
	/* Look in other modules... */
	spin_lock_irq(&modlist_lock);
	ret = __find_symbol(name, ksg);
	if (ret) {
		/* This can fail due to OOM, or module unloading */
		if (!use_module(mod, (*ksg)->owner))
			ret = 0;
	}
	spin_unlock_irq(&modlist_lock);
	return ret;
}

/* Free a module, remove from lists, etc (must hold module mutex). */
static void free_module(struct module *mod)
{
	/* Delete from various lists */
	list_del(&mod->list);
	spin_lock_irq(&modlist_lock);
	list_del(&mod->symbols.list);
	list_del(&mod->extable.list);
	spin_unlock_irq(&modlist_lock);

	/* These may be NULL, but that's OK */
	module_free(mod, mod->module_init);
	module_free(mod, mod->module_core);

	/* Module unload stuff */
	module_unload_free(mod);

	/* Finally, free the module structure */
	module_free(mod, mod);
}

void *__symbol_get(const char *symbol)
{
	struct kernel_symbol_group *ksg;
	unsigned long value, flags;

	spin_lock_irqsave(&modlist_lock, flags);
	value = __find_symbol(symbol, &ksg);
	if (value && !try_module_get(ksg->owner))
		value = 0;
	spin_unlock_irqrestore(&modlist_lock, flags);

	return (void *)value;
}
EXPORT_SYMBOL_GPL(__symbol_get);

/* Transfer one ELF section to the correct (init or core) area. */
static void *copy_section(const char *name,
			  void *base,
			  Elf_Shdr *sechdr,
			  struct module *mod,
			  struct sizes *used)
{
	void *dest;
	unsigned long *use;

	/* Only copy to init section if there is one */
	if (strstr(name, ".init") && mod->module_init) {
		dest = mod->module_init;
		use = &used->init_size;
	} else {
		dest = mod->module_core;
		use = &used->core_size;
	}

	/* Align up */
	*use = ALIGN(*use, sechdr->sh_addralign);
	dest += *use;
	*use += sechdr->sh_size;

	/* May not actually be in the file (eg. bss). */
	if (sechdr->sh_type != SHT_NOBITS)
		memcpy(dest, base + sechdr->sh_offset, sechdr->sh_size);

	return dest;
}

/* Look for the special symbols */
static int grab_private_symbols(Elf_Shdr *sechdrs,
				unsigned int symbolsec,
				const char *strtab,
				struct module *mod)
{
	Elf_Sym *sym = (void *)sechdrs[symbolsec].sh_offset;
	unsigned int i;

	for (i = 1; i < sechdrs[symbolsec].sh_size/sizeof(*sym); i++) {
		if (strcmp("__initfn", strtab + sym[i].st_name) == 0)
			mod->init = (void *)sym[i].st_value;
#ifdef CONFIG_MODULE_UNLOAD
		if (strcmp("__exitfn", strtab + sym[i].st_name) == 0)
			mod->exit = (void *)sym[i].st_value;
#endif
	}

	return 0;
}

/* Deal with the given section */
static int handle_section(const char *name,
			  Elf_Shdr *sechdrs,
			  unsigned int strindex,
			  unsigned int symindex,
			  unsigned int i,
			  struct module *mod)
{
	int ret;
	const char *strtab = (char *)sechdrs[strindex].sh_offset;

	switch (sechdrs[i].sh_type) {
	case SHT_REL:
		ret = apply_relocate(sechdrs, strtab, symindex, i, mod);
		break;
	case SHT_RELA:
		ret = apply_relocate_add(sechdrs, strtab, symindex, i, mod);
		break;
	case SHT_SYMTAB:
		ret = grab_private_symbols(sechdrs, i, strtab, mod);
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

/* Figure out total size desired for the common vars */
static unsigned long read_commons(void *start, Elf_Shdr *sechdr)
{
	unsigned long size, i, max_align;
	Elf_Sym *sym;
	
	size = max_align = 0;

	for (sym = start + sechdr->sh_offset, i = 0;
	     i < sechdr->sh_size / sizeof(Elf_Sym);
	     i++) {
		if (sym[i].st_shndx == SHN_COMMON) {
			/* Value encodes alignment. */
			if (sym[i].st_value > max_align)
				max_align = sym[i].st_value;
			/* Pad to required alignment */
			size = ALIGN(size, sym[i].st_value) + sym[i].st_size;
		}
	}

	/* Now, add in max alignment requirement (with align
	   attribute, this could be large), so we know we have space
	   whatever the start alignment is */
	return size + max_align;
}

/* Change all symbols so that sh_value encodes the pointer directly. */
static void simplify_symbols(Elf_Shdr *sechdrs,
			     unsigned int symindex,
			     unsigned int strindex,
			     void *common,
			     struct module *mod)
{
	unsigned int i;
	Elf_Sym *sym;

	/* First simplify defined symbols, so if they become the
           "answer" to undefined symbols, copying their st_value us
           correct. */
	for (sym = (void *)sechdrs[symindex].sh_offset, i = 0;
	     i < sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	     i++) {
		switch (sym[i].st_shndx) {
		case SHN_COMMON:
			/* Value encodes alignment. */
			common = (void *)ALIGN((unsigned long)common,
					       sym[i].st_value);
			/* Change it to encode pointer */
			sym[i].st_value = (unsigned long)common;
			common += sym[i].st_size;
			break;

		case SHN_ABS:
			/* Don't need to do anything */
			DEBUGP("Absolute symbol: 0x%08lx\n",
			       (long)sym[i].st_value);
			break;

		case SHN_UNDEF:
			break;

		default:
			sym[i].st_value 
				= (unsigned long)
				(sechdrs[sym[i].st_shndx].sh_offset
				 + sym[i].st_value);
		}
	}

	/* Now try to resolve undefined symbols */
	for (sym = (void *)sechdrs[symindex].sh_offset, i = 0;
	     i < sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	     i++) {
		if (sym[i].st_shndx == SHN_UNDEF) {
			/* Look for symbol */
			struct kernel_symbol_group *ksg = NULL;
			const char *strtab 
				= (char *)sechdrs[strindex].sh_offset;

			sym[i].st_value
				= find_symbol_internal(sechdrs,
						       symindex,
						       strtab,
						       strtab + sym[i].st_name,
						       mod,
						       &ksg);
			/* We fake up "__this_module" */
			if (strcmp(strtab+sym[i].st_name, "__this_module")==0)
				sym[i].st_value = (unsigned long)mod;
		}
	}
}

/* Get the total allocation size of the init and non-init sections */
static struct sizes get_sizes(const Elf_Ehdr *hdr,
			      const Elf_Shdr *sechdrs,
			      const char *secstrings)
{
	struct sizes ret = { 0, 0 };
	unsigned i;

	/* Everything marked ALLOC (this includes the exported
           symbols) */
	for (i = 1; i < hdr->e_shnum; i++) {
		unsigned long *add;

		/* If it's called *.init*, and we're init, we're interested */
		if (strstr(secstrings + sechdrs[i].sh_name, ".init") != 0)
			add = &ret.init_size;
		else
			add = &ret.core_size;

		if (sechdrs[i].sh_flags & SHF_ALLOC) {
			/* Pad up to required alignment */
			*add = ALIGN(*add, sechdrs[i].sh_addralign ?: 1);
			*add += sechdrs[i].sh_size;
		}
	}

	return ret;
}

/* Allocate and load the module */
static struct module *load_module(void *umod,
				  unsigned long len,
				  const char *uargs)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *sechdrs;
	char *secstrings;
	unsigned int i, symindex, exportindex, strindex, setupindex, exindex,
		modnameindex;
	long arglen;
	unsigned long common_length;
	struct sizes sizes, used;
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
	exportindex = setupindex = 0;

	/* And these should exist, but gcc whinges if we don't init them */
	symindex = strindex = exindex = modnameindex = 0;

	/* Find where important sections are */
	for (i = 1; i < hdr->e_shnum; i++) {
		if (sechdrs[i].sh_type == SHT_SYMTAB) {
			/* Internal symbols */
			DEBUGP("Symbol table in section %u\n", i);
			symindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name, ".modulename")
			   == 0) {
			/* This module's name */
			DEBUGP("Module name in section %u\n", i);
			modnameindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name, "__ksymtab")
			   == 0) {
			/* Exported symbols. */
			DEBUGP("EXPORT table in section %u\n", i);
			exportindex = i;
		} else if (strcmp(secstrings + sechdrs[i].sh_name, ".strtab")
			   == 0) {
			/* Strings */
			DEBUGP("String table found in section %u\n", i);
			strindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name, ".setup.init")
			   == 0) {
			/* Setup parameter info */
			DEBUGP("Setup table found in section %u\n", i);
			setupindex = i;
		} else if (strcmp(secstrings+sechdrs[i].sh_name, "__ex_table")
			   == 0) {
			/* Exception table */
			DEBUGP("Exception table found in section %u\n", i);
			exindex = i;
		}
#ifndef CONFIG_MODULE_UNLOAD
		/* Don't load .exit sections */
		if (strstr(secstrings+sechdrs[i].sh_name, ".exit"))
			sechdrs[i].sh_flags &= ~(unsigned long)SHF_ALLOC;
#endif
	}

	if (!modnameindex) {
		DEBUGP("Module has no name!\n");
		err = -ENOEXEC;
		goto free_hdr;
	}

	/* Now allocate space for the module proper, and copy name and args. */
	err = strlen_user(uargs);
	if (err < 0)
		goto free_hdr;
	arglen = err;

	mod = module_alloc(sizeof(*mod) + arglen+1);
	if (!mod) {
		err = -ENOMEM;
		goto free_hdr;
	}
	memset(mod, 0, sizeof(*mod) + arglen+1);
	if (copy_from_user(mod->args, uargs, arglen) != 0) {
		err = -EFAULT;
		goto free_mod;
	}
	strncpy(mod->name, (char *)hdr + sechdrs[modnameindex].sh_offset,
		sizeof(mod->name)-1);

	if (find_module(mod->name)) {
		err = -EEXIST;
		goto free_mod;
	}

	/* Initialize the lists, since they will be list_del'd if init fails */
	INIT_LIST_HEAD(&mod->extable.list);
	INIT_LIST_HEAD(&mod->list);
	INIT_LIST_HEAD(&mod->symbols.list);
	mod->symbols.owner = mod;
	mod->live = 0;
	module_unload_init(mod);

	/* How much space will we need?  (Common area in core) */
	sizes = get_sizes(hdr, sechdrs, secstrings);
	common_length = read_commons(hdr, &sechdrs[symindex]);
	sizes.core_size += common_length;

	/* Set these up, and allow archs to manipulate them. */
	mod->core_size = sizes.core_size;
	mod->init_size = sizes.init_size;

	/* Allow archs to add to them. */
	err = module_init_size(hdr, sechdrs, secstrings, mod);
	if (err < 0)
		goto free_mod;
	mod->init_size = err;

	err = module_core_size(hdr, sechdrs, secstrings, mod);
	if (err < 0)
		goto free_mod;
	mod->core_size = err;

	/* Do the allocs. */
	ptr = module_alloc(mod->core_size);
	if (!ptr) {
		err = -ENOMEM;
		goto free_mod;
	}
	memset(ptr, 0, mod->core_size);
	mod->module_core = ptr;

	ptr = module_alloc(mod->init_size);
	if (!ptr) {
		err = -ENOMEM;
		goto free_core;
	}
	memset(ptr, 0, mod->init_size);
	mod->module_init = ptr;

	/* Transfer each section which requires ALLOC, and set sh_offset
	   fields to absolute addresses. */
	used.core_size = common_length;
	used.init_size = 0;
	for (i = 1; i < hdr->e_shnum; i++) {
		if (sechdrs[i].sh_flags & SHF_ALLOC) {
			ptr = copy_section(secstrings + sechdrs[i].sh_name,
					   hdr, &sechdrs[i], mod, &used);
			if (IS_ERR(ptr))
				goto cleanup;
			sechdrs[i].sh_offset = (unsigned long)ptr;
		} else {
			sechdrs[i].sh_offset += (unsigned long)hdr;
		}
	}
	/* Don't use more than we allocated! */
	if (used.init_size > mod->init_size || used.core_size > mod->core_size)
		BUG();

	/* Fix up syms, so that st_value is a pointer to location. */
	simplify_symbols(sechdrs, symindex, strindex, mod->module_core, mod);

	/* Set up EXPORTed symbols */
	if (exportindex) {
		mod->symbols.num_syms = (sechdrs[exportindex].sh_size
					/ sizeof(*mod->symbols.syms));
		mod->symbols.syms = (void *)sechdrs[exportindex].sh_offset;
	}

	/* Set up exception table */
	if (exindex) {
		/* FIXME: Sort exception table. */
		mod->extable.num_entries = (sechdrs[exindex].sh_size
					    / sizeof(struct
						     exception_table_entry));
		mod->extable.entry = (void *)sechdrs[exindex].sh_offset;
	}

	/* Now handle each section. */
	for (i = 1; i < hdr->e_shnum; i++) {
		err = handle_section(secstrings + sechdrs[i].sh_name,
				     sechdrs, strindex, symindex, i, mod);
		if (err < 0)
			goto cleanup;
	}

	err = module_finalize(hdr, sechdrs, mod);
	if (err < 0)
		goto cleanup;

#if 0 /* Needs param support */
	/* Size of section 0 is 0, so this works well */
	err = parse_args(mod->args,
			 (struct kernel_param *)
			 sechdrs[setupindex].sh_offset,
			 sechdrs[setupindex].sh_size
			 / sizeof(struct kernel_param),
			 NULL);
	if (err < 0)
		goto cleanup;
#endif

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
	module_free(mod, mod);
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

	/* Now sew it into exception list (just in case...). */
	spin_lock_irq(&modlist_lock);
	list_add(&mod->extable.list, &extables);
	spin_unlock_irq(&modlist_lock);

	/* Start the module */
	ret = mod->init ? mod->init() : 0;
	if (ret < 0) {
		/* Init routine failed: abort.  Try to protect us from
                   buggy refcounters. */
		synchronize_kernel();
		if (mod->unsafe) {
			printk(KERN_ERR "%s: module is now stuck!\n",
			       mod->name);
			/* Mark it "live" so that they can force
			   deletion later, and we don't keep getting
			   woken on every decrement. */
			mod->live = 1;
		} else
			free_module(mod);
		up(&module_mutex);
		return ret;
	}

	/* Now it's a first class citizen! */
	spin_lock_irq(&modlist_lock);
	list_add(&mod->symbols.list, &kernel_symbols.list);
	spin_unlock_irq(&modlist_lock);
	list_add(&mod->list, &modules);

	module_free(mod, mod->module_init);
	mod->module_init = NULL;

	/* All ok! */
	mod->live = 1;
	up(&module_mutex);
	return 0;
}

/* Called by the /proc file system to return a current list of
   modules.  Al Viro came up with this interface as an "improvement".
   God save us from any more such interface improvements. */
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
	return 0;
}
struct seq_operations modules_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= m_show
};

static int __init init(void)
{
	/* Add kernel symbols to symbol table */
	kernel_symbols.num_syms = (__stop___ksymtab - __start___ksymtab);
	kernel_symbols.syms = __start___ksymtab;
	list_add(&kernel_symbols.list, &symbols);

	/* Add kernel exception table to exception tables */
	kernel_extable.num_entries = (__stop___ex_table -__start___ex_table);
	kernel_extable.entry = __start___ex_table;
	list_add(&kernel_extable.list, &extables);
	return 0;
}

/* Obsolete lvalue for broken code which asks about usage */
int module_dummy_usage = 1;

/* Call this at boot */
__initcall(init);
