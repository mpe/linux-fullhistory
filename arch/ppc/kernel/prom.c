/*
 * Procedures for interfacing to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * In particular, we are interested in the device tree
 * and in using some of its services (exit, write to stdout).
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <stdarg.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/prom.h>
#include <asm/page.h>
#include <asm/processor.h>

/*
 * Properties whose value is longer than this get excluded from our
 * copy of the device tree.  This way we don't waste space storing
 * things like "driver,AAPL,MacOS,PowerPC" properties.
 */
#define MAX_PROPERTY_LENGTH	1024

struct prom_args {
	const char *service;
	int nargs;
	int nret;
	void *args[10];
};

struct pci_address {
	unsigned a_hi;
	unsigned a_mid;
	unsigned a_lo;
};

struct pci_reg_property {
	struct pci_address addr;
	unsigned size_hi;
	unsigned size_lo;
};

struct pci_range {
	struct pci_address addr;
	unsigned phys;
	unsigned size_hi;
	unsigned size_lo;
};

char *prom_display_paths[FB_MAX] __initdata = { 0, };
unsigned int prom_num_displays = 0;

prom_entry prom = 0;
ihandle prom_chosen = 0, prom_stdout = 0;

extern char *klimit;
char *bootpath = 0;
char *bootdevice = 0;

unsigned int rtas_data = 0;
unsigned int rtas_entry = 0;

static struct device_node *allnodes = 0;

static void *call_prom(const char *service, int nargs, int nret, ...);
static void prom_print(const char *msg);
static void prom_exit(void);
static unsigned long copy_device_tree(unsigned long, unsigned long);
static unsigned long inspect_node(phandle, struct device_node *, unsigned long,
				  unsigned long, struct device_node ***);
static unsigned long finish_node(struct device_node *, unsigned long,
				 unsigned long);
static unsigned long check_display(unsigned long);
static int prom_next_node(phandle *);

extern void enter_rtas(void *);
extern unsigned long reloc_offset(void);

/*
 * prom_init() is called very early on, before the kernel text
 * and data have been mapped to KERNELBASE.  At this point the code
 * is running at whatever address it has been loaded at, so
 * references to extern and static variables must be relocated
 * explicitly.  The procedure reloc_offset() returns the the address
 * we're currently running at minus the address we were linked at.
 * (Note that strings count as static variables.)
 *
 * Because OF may have mapped I/O devices into the area starting at
 * KERNELBASE, particularly on CHRP machines, we can't safely call
 * OF once the kernel has been mapped to KERNELBASE.  Therefore all
 * OF calls should be done within prom_init(), and prom_init()
 * and all routines called within it must be careful to relocate
 * references as necessary.
 *
 * Note that the bss is cleared *after* prom_init runs, so we have
 * to make sure that any static or extern variables it accesses
 * are put in the data segment.
 */
#define PTRRELOC(x)	((typeof(x))((unsigned long)(x) + offset))
#define PTRUNRELOC(x)	((typeof(x))((unsigned long)(x) - offset))
#define RELOC(x)	(*PTRRELOC(&(x)))

#define ALIGN(x) (((x) + sizeof(unsigned long)-1) & -sizeof(unsigned long))

static void
prom_exit()
{
	struct prom_args args;
	unsigned long offset = reloc_offset();

	args.service = "exit";
	args.nargs = 0;
	args.nret = 0;
	RELOC(prom)(&args);
	for (;;)			/* should never get here */
		;
}

static void *
call_prom(const char *service, int nargs, int nret, ...)
{
	va_list list;
	int i;
	unsigned long offset = reloc_offset();
	struct prom_args prom_args;

	prom_args.service = service;
	prom_args.nargs = nargs;
	prom_args.nret = nret;
	va_start(list, nret);
	for (i = 0; i < nargs; ++i)
		prom_args.args[i] = va_arg(list, void *);
	va_end(list);
	for (i = 0; i < nret; ++i)
		prom_args.args[i + nargs] = 0;
	RELOC(prom)(&prom_args);
	return prom_args.args[nargs];
}

static void
prom_print(const char *msg)
{
	const char *p, *q;
	unsigned long offset = reloc_offset();

	for (p = msg; *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n'; ++q)
			;
		if (q > p)
			call_prom(RELOC("write"), 3, 1, RELOC(prom_stdout),
				  p, q - p);
		if (*q != 0) {
			++q;
			call_prom(RELOC("write"), 3, 1, RELOC(prom_stdout),
				  RELOC("\r\n"), 2);
		}
	}
}

/*
 * We enter here early on, when the Open Firmware prom is still
 * handling exceptions and the MMU hash table for us.
 */
void
prom_init(int r3, int r4, prom_entry pp)
{
	unsigned long mem;
	ihandle prom_rtas;
	unsigned int rtas_size;
	unsigned long offset = reloc_offset();
	int l;
	char *p, *d;

	/* First get a handle for the stdout device */
	RELOC(prom) = pp;
	RELOC(prom_chosen) = call_prom(RELOC("finddevice"), 1, 1,
				       RELOC("/chosen"));
	if (RELOC(prom_chosen) == (void *)-1)
		prom_exit();
	if ((int) call_prom(RELOC("getprop"), 4, 1, RELOC(prom_chosen),
			    RELOC("stdout"), &RELOC(prom_stdout),
			    sizeof(prom_stdout)) <= 0)
		prom_exit();

	/* Get the boot device and translate it to a full OF pathname. */
	mem = (unsigned long) RELOC(klimit) + offset;
	p = (char *) mem;
	l = (int) call_prom(RELOC("getprop"), 4, 1, RELOC(prom_chosen),
			    RELOC("bootpath"), p, 1<<20);
	if (l > 0) {
		p[l] = 0;	/* should already be null-terminated */
		RELOC(bootpath) = PTRUNRELOC(p);
		mem += l + 1;
		d = (char *) mem;
		*d = 0;
		call_prom(RELOC("canon"), 3, 1, p, d, 1<<20);
		RELOC(bootdevice) = PTRUNRELOC(d);
		mem = ALIGN(mem + strlen(d) + 1);
	}

	mem = check_display(mem);

	prom_print(RELOC("copying OF device tree..."));
	mem = copy_device_tree(mem, mem + (1<<20));
	prom_print(RELOC("done\n"));

	prom_rtas = call_prom(RELOC("finddevice"), 1, 1, RELOC("/rtas"));
	if (prom_rtas != (void *) -1) {
		rtas_size = 0;
		call_prom(RELOC("getprop"), 4, 1, prom_rtas,
			  RELOC("rtas-size"), &rtas_size, sizeof(rtas_size));
		prom_print(RELOC("instantiating rtas..."));
		if (rtas_size == 0) {
			RELOC(rtas_data) = 0;
		} else {
			mem = (mem + 4095) & -4096; /* round to page bdry */
			RELOC(rtas_data) = mem - KERNELBASE;
			mem += rtas_size;
		}
		RELOC(rtas_entry) = (unsigned int)
			call_prom(RELOC("instantiate-rtas"), 1, 1,
				  RELOC(rtas_data));
		if (RELOC(rtas_entry) == -1)
			prom_print(RELOC(" failed\n"));
		else
			prom_print(RELOC(" done\n"));
	}

	RELOC(klimit) = (char *) (mem - offset);
}

/*
 * If we have a display that we don't know how to drive,
 * we will want to try to execute OF's open method for it
 * later.  However, OF will probably fall over if we do that
 * we've taken over the MMU.
 * So we check whether we will need to open the display,
 * and if so, open it now.
 */
static unsigned long
check_display(unsigned long mem)
{
	phandle node;
	ihandle ih;
	unsigned long offset = reloc_offset();
	char type[16], *path;

	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("device_type"),
			  type, sizeof(type));
		if (strcmp(type, RELOC("display")) != 0)
			continue;
		/* It seems OF doesn't null-terminate the path :-( */
		path = (char *) mem;
		memset(path, 0, 256);
		if ((int) call_prom(RELOC("package-to-path"), 3, 1,
				    node, path, 255) < 0)
			continue;
		prom_print(RELOC("opening display "));
		prom_print(path);
		ih = call_prom(RELOC("open"), 1, 1, path);
		if (ih == 0 || ih == (ihandle) -1) {
			prom_print(RELOC("... failed\n"));
			continue;
		}
		prom_print(RELOC("... ok\n"));
		mem += strlen(path) + 1;
		RELOC(prom_display_paths[RELOC(prom_num_displays)++])
			= PTRUNRELOC(path);
		if (RELOC(prom_num_displays) >= FB_MAX)
			break;
	}
	return ALIGN(mem);
}

static int
prom_next_node(phandle *nodep)
{
	phandle node;
	unsigned long offset = reloc_offset();

	if ((node = *nodep) != 0
	    && (*nodep = call_prom(RELOC("child"), 1, 1, node)) != 0)
		return 1;
	if ((*nodep = call_prom(RELOC("peer"), 1, 1, node)) != 0)
		return 1;
	for (;;) {
		if ((node = call_prom(RELOC("parent"), 1, 1, node)) == 0)
			return 0;
		if ((*nodep = call_prom(RELOC("peer"), 1, 1, node)) != 0)
			return 1;
	}
}

/*
 * Make a copy of the device tree from the PROM.
 */
static unsigned long
copy_device_tree(unsigned long mem_start, unsigned long mem_end)
{
	phandle root;
	unsigned long new_start;
	struct device_node **allnextp;
	unsigned long offset = reloc_offset();

	root = call_prom(RELOC("peer"), 1, 1, (phandle)0);
	if (root == (phandle)0) {
		prom_print(RELOC("couldn't get device tree root\n"));
		prom_exit();
	}
	allnextp = &RELOC(allnodes);
	mem_start = ALIGN(mem_start);
	new_start = inspect_node(root, 0, mem_start, mem_end, &allnextp);
	*allnextp = 0;
	return new_start;
}

static unsigned long
inspect_node(phandle node, struct device_node *dad,
	     unsigned long mem_start, unsigned long mem_end,
	     struct device_node ***allnextpp)
{
	int l;
	phandle child;
	struct device_node *np;
	struct property *pp, **prev_propp;
	char *prev_name, *namep;
	unsigned char *valp;
	unsigned long offset = reloc_offset();

	np = (struct device_node *) mem_start;
	mem_start += sizeof(struct device_node);
	memset(np, 0, sizeof(*np));
	np->node = node;
	**allnextpp = PTRUNRELOC(np);
	*allnextpp = &np->allnext;
	if (dad != 0) {
		np->parent = PTRUNRELOC(dad);
		/* we temporarily use the `next' field as `last_child'. */
		if (dad->next == 0)
			dad->child = PTRUNRELOC(np);
		else
			dad->next->sibling = PTRUNRELOC(np);
		dad->next = np;
	}

	/* get and store all properties */
	prev_propp = &np->properties;
	prev_name = RELOC("");
	for (;;) {
		pp = (struct property *) mem_start;
		namep = (char *) (pp + 1);
		pp->name = PTRUNRELOC(namep);
		if ((int) call_prom(RELOC("nextprop"), 3, 1, node, prev_name,
				    namep) <= 0)
			break;
		mem_start = ALIGN((unsigned long)namep + strlen(namep) + 1);
		prev_name = namep;
		valp = (unsigned char *) mem_start;
		pp->value = PTRUNRELOC(valp);
		pp->length = (int)
			call_prom(RELOC("getprop"), 4, 1, node, namep,
				  valp, mem_end - mem_start);
		if (pp->length < 0)
			continue;
#ifdef MAX_PROPERTY_LENGTH
		if (pp->length > MAX_PROPERTY_LENGTH)
			continue; /* ignore this property */
#endif
		mem_start = ALIGN(mem_start + pp->length);
		*prev_propp = PTRUNRELOC(pp);
		prev_propp = &pp->next;
	}
	*prev_propp = 0;

	/* get the node's full name */
	l = (int) call_prom(RELOC("package-to-path"), 3, 1, node,
			    (char *) mem_start, mem_end - mem_start);
	if (l >= 0) {
		np->full_name = PTRUNRELOC((char *) mem_start);
		*(char *)(mem_start + l) = 0;
		mem_start = ALIGN(mem_start + l + 1);
	}

	/* do all our children */
	child = call_prom(RELOC("child"), 1, 1, node);
	while (child != (void *)0) {
		mem_start = inspect_node(child, np, mem_start, mem_end,
					 allnextpp);
		child = call_prom(RELOC("peer"), 1, 1, child);
	}

	return mem_start;
}

void
finish_device_tree(void)
{
	unsigned long mem = (unsigned long) klimit;

	mem = finish_node(allnodes, mem, 0UL);
	printk(KERN_INFO "device tree used %lu bytes\n",
	       mem - (unsigned long) allnodes);
	klimit = (char *) mem;
}

static unsigned long
finish_node(struct device_node *np, unsigned long mem_start,
	    unsigned long base_address)
{
	struct reg_property *rp;
	struct pci_reg_property *pci_addrs;
	struct address_range *adr;
	struct device_node *child;
	int i, l;

	np->name = get_property(np, "name", 0);
	np->type = get_property(np, "device_type", 0);

	/* get all the device addresses and interrupts */
	adr = (struct address_range *) mem_start;
	pci_addrs = (struct pci_reg_property *)
		get_property(np, "assigned-addresses", &l);
	i = 0;
	if (pci_addrs != 0) {
		while ((l -= sizeof(struct pci_reg_property)) >= 0) {
			/* XXX assumes PCI addresses mapped 1-1 to physical */
			adr[i].space = pci_addrs[i].addr.a_hi;
			adr[i].address = pci_addrs[i].addr.a_lo;
			adr[i].size = pci_addrs[i].size_lo;
			++i;
		}
	} else {
		rp = (struct reg_property *) get_property(np, "reg", &l);
		if (rp != 0) {
			while ((l -= sizeof(struct reg_property)) >= 0) {
				adr[i].space = 0;
				adr[i].address = rp[i].address + base_address;
				adr[i].size = rp[i].size;
				++i;
			}
		}
	}
	if (i > 0) {
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	np->intrs = (int *) get_property(np, "AAPL,interrupts", &l);
	if (np->intrs == 0)
		np->intrs = (int *) get_property(np, "interrupts", &l);
	if (np->intrs != 0)
		np->n_intrs = l / sizeof(int);

	if (np->type != 0 && np->n_addrs > 0
	    && (strcmp(np->type, "dbdma") == 0
		|| strcmp(np->type, "mac-io") == 0))
		base_address = np->addrs[0].address;

	for (child = np->child; child != NULL; child = child->sibling)
		mem_start = finish_node(child, mem_start, base_address);

	return mem_start;
}

/*
 * Construct and return a list of the device_nodes with a given name.
 */
struct device_node *
find_devices(const char *name)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (np->name != 0 && strcasecmp(np->name, name) == 0) {
			*prevp = np;
			prevp = &np->next;
		}
	}
	*prevp = 0;
	return head;
}

/*
 * Construct and return a list of the device_nodes with a given type.
 */
struct device_node *
find_type_devices(const char *type)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (np->type != 0 && strcasecmp(np->type, type) == 0) {
			*prevp = np;
			prevp = &np->next;
		}
	}
	*prevp = 0;
	return head;
}

/*
 * Construct and return a list of the device_nodes with a given type
 * and compatible property.
 */
struct device_node *
find_compatible_devices(const char *type, const char *compat)
{
	struct device_node *head, **prevp, *np;
	const char *cp;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (type != NULL
		    && !(np->type != 0 && strcasecmp(np->type, type) == 0))
			continue;
		cp = (char *) get_property(np, "compatible", NULL);
		if (cp != NULL && strcasecmp(cp, compat) == 0) {
			*prevp = np;
			prevp = &np->next;
		}
	}
	*prevp = 0;
	return head;
}

/*
 * Find the device_node with a given full_name.
 */
struct device_node *
find_path_device(const char *path)
{
	struct device_node *np;

	for (np = allnodes; np != 0; np = np->allnext)
		if (np->full_name != 0 && strcasecmp(np->full_name, path) == 0)
			return np;
	return NULL;
}

/*
 * Find the device_node with a given phandle.
 */
struct device_node *
find_phandle(phandle ph)
{
	struct device_node *np;

	for (np = allnodes; np != 0; np = np->allnext)
		if (np->node == ph)
			return np;
	return NULL;
}

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
unsigned char *
get_property(struct device_node *np, const char *name, int *lenp)
{
	struct property *pp;

	for (pp = np->properties; pp != 0; pp = pp->next)
		if (strcmp(pp->name, name) == 0) {
			if (lenp != 0)
				*lenp = pp->length;
			return pp->value;
		}
	return 0;
}

void
print_properties(struct device_node *np)
{
	struct property *pp;
	char *cp;
	int i, n;

	for (pp = np->properties; pp != 0; pp = pp->next) {
		printk(KERN_INFO "%s", pp->name);
		for (i = strlen(pp->name); i < 16; ++i)
			printk(" ");
		cp = (char *) pp->value;
		for (i = pp->length; i > 0; --i, ++cp)
			if ((i > 1 && (*cp < 0x20 || *cp > 0x7e))
			    || (i == 1 && *cp != 0))
				break;
		if (i == 0 && pp->length > 1) {
			/* looks like a string */
			printk(" %s\n", (char *) pp->value);
		} else {
			/* dump it in hex */
			n = pp->length;
			if (n > 64)
				n = 64;
			if (pp->length % 4 == 0) {
				unsigned int *p = (unsigned int *) pp->value;

				n /= 4;
				for (i = 0; i < n; ++i) {
					if (i != 0 && (i % 4) == 0)
						printk("\n                ");
					printk(" %08x", *p++);
				}
			} else {
				unsigned char *bp = pp->value;

				for (i = 0; i < n; ++i) {
					if (i != 0 && (i % 16) == 0)
						printk("\n                ");
					printk(" %02x", *bp++);
				}
			}
			printk("\n");
			if (pp->length > 64)
				printk("                 ... (length = %d)\n",
				       pp->length);
		}
	}
}

int
call_rtas(const char *service, int nargs, int nret,
	  unsigned long *outputs, ...)
{
	va_list list;
	int i;
	struct device_node *rtas;
	int *tokp;
	union {
		unsigned long words[16];
		double align;
	} u;

	rtas = find_devices("rtas");
	if (rtas == NULL)
		return -1;
	tokp = (int *) get_property(rtas, service, NULL);
	if (tokp == NULL) {
		printk(KERN_ERR "No RTAS service called %s\n", service);
		return -1;
	}
	u.words[0] = *tokp;
	u.words[1] = nargs;
	u.words[2] = nret;
	va_start(list, outputs);
	for (i = 0; i < nargs; ++i)
		u.words[i+3] = va_arg(list, unsigned long);
	va_end(list);
	enter_rtas(&u);
	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = u.words[i+nargs+4];
	return u.words[nargs+3];
}

void
abort()
{
#ifdef CONFIG_XMON
	extern void xmon(void *);
	xmon(0);
#endif
	prom_exit();
}
