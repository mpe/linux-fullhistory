/*
 * kallsyms.c: in-kernel printing of symbolic oopses and stack traces.
 *
 * Rewritten and vastly simplified by Rusty Russell for in-kernel
 * module loader:
 *   Copyright 2002 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 * Stem compression by Andi Kleen.
 */
#include <linux/kallsyms.h>
#include <linux/module.h>

static char kallsyms_dummy;

/* These will be re-linked against their real values during the second link stage */
extern unsigned long kallsyms_addresses[1] __attribute__((weak, alias("kallsyms_dummy")));
extern unsigned long kallsyms_num_syms __attribute__((weak, alias("kallsyms_dummy")));
extern char kallsyms_names[1] __attribute__((weak, alias("kallsyms_dummy")));

/* Defined by the linker script. */
extern char _stext[], _etext[];

/* Lookup an address.  modname is set to NULL if it's in the kernel. */
const char *kallsyms_lookup(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname, char *namebuf)
{
	unsigned long i, best = 0;

	/* This kernel should never had been booted. */
	if ((void *)kallsyms_addresses == &kallsyms_dummy)
		BUG();

	namebuf[127] = 0;
	namebuf[0] = 0;

	if (addr >= (unsigned long)_stext && addr <= (unsigned long)_etext) {
		unsigned long symbol_end;
		char *name = kallsyms_names;

		/* They're sorted, we could be clever here, but who cares? */
		for (i = 0; i < kallsyms_num_syms; i++) {
			if (kallsyms_addresses[i] > kallsyms_addresses[best] &&
			    kallsyms_addresses[i] <= addr)
				best = i;
		}

		/* Grab name */
		for (i = 0; i <= best; i++) { 
			unsigned prefix = *name++;
			strncpy(namebuf + prefix, name, 127 - prefix);
			name += strlen(name) + 1;
		}

		/* Base symbol size on next symbol. */
		if (best + 1 < kallsyms_num_syms)
			symbol_end = kallsyms_addresses[best + 1];
		else
			symbol_end = (unsigned long)_etext;

		*symbolsize = symbol_end - kallsyms_addresses[best];
		*modname = NULL;
		*offset = addr - kallsyms_addresses[best];
		return namebuf;
	}

	return module_address_lookup(addr, symbolsize, offset, modname);
}

/* Replace "%s" in format with address, or returns -errno. */
void __print_symbol(const char *fmt, unsigned long address)
{
	char *modname;
	const char *name;
	unsigned long offset, size;
	char namebuf[128];

	name = kallsyms_lookup(address, &size, &offset, &modname, namebuf);

	if (!name) {
		char addrstr[sizeof("0x%lx") + (BITS_PER_LONG*3/10)];

		sprintf(addrstr, "0x%lx", address);
		printk(fmt, addrstr);
		return;
	}

	if (modname) {
		/* This is pretty small. */
		char buffer[sizeof("%s+%#lx/%#lx [%s]")
			   + strlen(name) + 2*(BITS_PER_LONG*3/10)
			   + strlen(modname)];

		sprintf(buffer, "%s+%#lx/%#lx [%s]",
			name, offset, size, modname);
		printk(fmt, buffer);
	} else {
		char buffer[sizeof("%s+%#lx/%#lx")
			   + strlen(name) + 2*(BITS_PER_LONG*3/10)];

		sprintf(buffer, "%s+%#lx/%#lx", name, offset, size);
		printk(fmt, buffer);
	}
}

EXPORT_SYMBOL(kallsyms_lookup);
EXPORT_SYMBOL(__print_symbol);
