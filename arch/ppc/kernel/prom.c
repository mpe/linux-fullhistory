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
#include <linux/blk.h>
#include <asm/prom.h>
#include <asm/page.h>
#include <asm/processor.h>

#define getpromprop(node, name, buf, len)	\
	((int)call_prom("getprop", 4, 1, (node), (name), (buf), (len)))

ihandle prom_stdout;
ihandle prom_chosen;

char command_line[256];
int screen_initialized = 0;

char prom_display_path[128];

struct prom_args {
	const char *service;
	int nargs;
	int nret;
	void *args[10];
} prom_args;

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

void (*prom_entry)(void *);
extern int prom_trashed;

static int prom_callback(struct prom_args *);
static unsigned long inspect_node(phandle, struct device_node *, unsigned long,
				  unsigned long, unsigned long);
static void check_display(void);
static int prom_next_node(phandle *);

extern int pmac_display_supported(const char *);
extern void enter_prom(void *);

void
prom_exit()
{
	struct prom_args args;

	args.service = "exit";
	args.nargs = 0;
	args.nret = 0;
	enter_prom(&args);
	for (;;)			/* should never get here */
		;
}

void *
call_prom(const char *service, int nargs, int nret, ...)
{
	va_list list;
	int i;

	if (prom_trashed)
		panic("prom called after its memory was reclaimed");
	prom_args.service = service;
	prom_args.nargs = nargs;
	prom_args.nret = nret;
	va_start(list, nret);
	for (i = 0; i < nargs; ++i)
		prom_args.args[i] = va_arg(list, void *);
	va_end(list);
	for (i = 0; i < nret; ++i)
		prom_args.args[i + nargs] = 0;
	enter_prom(&prom_args);
	return prom_args.args[nargs];
}

void
prom_print(const char *msg)
{
	const char *p, *q;
	const char *crlf = "\r\n";

	if (screen_initialized)
		return;
	for (p = msg; *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n'; ++q)
			;
		if (q > p)
			call_prom("write", 3, 1, prom_stdout, p, q - p);
		if (*q != 0) {
			++q;
			call_prom("write", 3, 1, prom_stdout, crlf, 2);
		}
	}
}

/*
 * We enter here early on, when the Open Firmware prom is still
 * handling exceptions and the MMU hash table for us.
 */
void
prom_init(char *params, int unused, void (*pp)(void *))
{
	/* First get a handle for the stdout device */
	if ( ! have_of() )
		return;
	prom_entry = pp;
	prom_chosen = call_prom("finddevice", 1, 1, "/chosen");
	if (prom_chosen == (void *)-1)
		prom_exit();
	call_prom("getprop", 4, 1, prom_chosen, "stdout", &prom_stdout,
		  (void *) sizeof(prom_stdout));

	/*
	 * If we were booted via quik, params points to the physical address
	 * of the command-line parameters.
	 * If we were booted from an xcoff image (i.e. netbooted or
	 * booted from floppy), we get the command line from the bootargs
	 * property of the /chosen node.  If an initial ramdisk is present,
	 * params and unused are used for initrd_start and initrd_size,
	 * otherwise they contain 0xdeadbeef.  
	 */
	command_line[0] = 0;
	if ((unsigned long) params >= 0x4000
	    && (unsigned long) params < 0x800000
	    && unused == 0) {
		strncpy(command_line, params+KERNELBASE, sizeof(command_line));
	} else {
#ifdef CONFIG_BLK_DEV_INITRD
		if ((unsigned long) params - KERNELBASE < 0x800000
		    && unused != 0 && unused != 0xdeadbeef) {
			initrd_start = (unsigned long) params;
			initrd_end = initrd_start + unused;
			ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
		}
#endif
		call_prom("getprop", 4, 1, prom_chosen, "bootargs",
			  command_line, sizeof(command_line));
	}
	command_line[sizeof(command_line) - 1] = 0;

	check_display();
}

/*
 * If we have a display that we don't know how to drive,
 * we will want to try to execute OF's open method for it
 * later.  However, OF may fall over if we do that after
 * we've taken over the MMU and done set_prom_callback.
 * So we check whether we will need to open the display,
 * and if so, open it now.
 */
static void
check_display()
{
	phandle node;
	ihandle ih;
	char type[16], name[64], path[128];

	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		getpromprop(node, "device_type", type, sizeof(type));
		if (strcmp(type, "display") != 0)
			continue;
		name[0] = 0;
		getpromprop(node, "name", name, sizeof(name));
		if (pmac_display_supported(name))
			/* we have a supported display */
			return;
	}
	printk(KERN_INFO "No supported display found\n");
	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		getpromprop(node, "device_type", type, sizeof(type));
		if (strcmp(type, "display") != 0)
			continue;
		/* It seems OF doesn't null-terminate the path :-( */
		memset(path, 0, sizeof(path));
		if ((int) call_prom("package-to-path", 3, 1,
				    node, path, sizeof(path) - 1) < 0) {
			printk(KERN_WARNING "can't get path for display %p\n",
			       node);
			continue;
		}
		ih = call_prom("open", 1, 1, path);
		if (ih == 0 || ih == (ihandle) -1) {
			printk(KERN_WARNING "couldn't open display %s\n",
			       path);
			continue;
		}
		printk(KERN_INFO "Opened display device %s using "
		       "Open Firmware\n", path);
		strcpy(prom_display_path, path);
		break;
	}
}

static int
prom_next_node(phandle *nodep)
{
	phandle node;

	if ((node = *nodep) != 0
	    && (*nodep = call_prom("child", 1, 1, node)) != 0)
		return 1;
	if ((*nodep = call_prom("peer", 1, 1, node)) != 0)
		return 1;
	for (;;) {
		if ((node = call_prom("parent", 1, 1, node)) == 0)
			return 0;
		if ((*nodep = call_prom("peer", 1, 1, node)) != 0)
			return 1;
	}
}

/*
 * Callback routine for the PROM to call us.
 * No services are implemented yet :-)
 */
static int
prom_callback(struct prom_args *argv)
{
	printk("uh oh, prom callback '%s' (%d/%d)\n", argv->service,
	       argv->nargs, argv->nret);
	return -1;
}

/*
 * Register a callback with the Open Firmware PROM so it can ask
 * us to map/unmap memory, etc.
 */
void
set_prom_callback()
{
	call_prom("set-callback", 1, 1, prom_callback);
}

void
abort()
{
#ifdef CONFIG_XMON
	xmon(0);
#endif
	prom_exit();
}

/*
 * Make a copy of the device tree from the PROM.
 */

static struct device_node *allnodes;
static struct device_node **allnextp;

#define ALIGN(x) (((x) + sizeof(unsigned long)-1) & -sizeof(unsigned long))

unsigned long
copy_device_tree(unsigned long mem_start, unsigned long mem_end)
{
	phandle root;

	root = call_prom("peer", 1, 1, (phandle)0);
	if (root == (phandle)0)
		panic("couldn't get device tree root\n");
	allnextp = &allnodes;
	mem_start = inspect_node(root, 0, 0, mem_start, mem_end);
	*allnextp = 0;
	return mem_start;
}

static unsigned long
inspect_node(phandle node, struct device_node *dad, unsigned long base_address,
	     unsigned long mem_start, unsigned long mem_end)
{
	struct reg_property *reg, *rp;
	struct pci_reg_property *pci_addrs;
	int l, i;
	phandle child;
	struct device_node *np;
	struct property *pp, **prev_propp;
	char *prev_name;

	np = (struct device_node *) mem_start;
	mem_start += sizeof(struct device_node);
	memset(np, 0, sizeof(*np));
	np->node = node;
	*allnextp = np;
	allnextp = &np->allnext;
	np->parent = dad;
	if (dad != 0) {
		/* we temporarily use the `next' field as `last_child'. */
		if (dad->next == 0)
			dad->child = np;
		else
			dad->next->sibling = np;
		dad->next = np;
	}

	/* get and store all properties */
	prev_propp = &np->properties;
	prev_name = 0;
	for (;;) {
		pp = (struct property *) mem_start;
		pp->name = (char *) (pp + 1);
		if ((int) call_prom("nextprop", 3, 1, node, prev_name,
				    pp->name) <= 0)
			break;
		mem_start = ALIGN((unsigned long)pp->name
				  + strlen(pp->name) + 1);
		pp->value = (unsigned char *) mem_start;
		pp->length = (int)
			call_prom("getprop", 4, 1, node, pp->name, pp->value,
				  mem_end - mem_start);
		if (pp->length < 0)
			panic("hey, where did property %s go?", pp->name);
		mem_start = ALIGN(mem_start + pp->length);
		prev_name = pp->name;
		*prev_propp = pp;
		prev_propp = &pp->next;
	}
	*prev_propp = 0;

	np->name = get_property(np, "name", 0);
	np->type = get_property(np, "device_type", 0);

	/* get all the device addresses and interrupts */
	reg = (struct reg_property *) mem_start;
	pci_addrs = (struct pci_reg_property *)
		get_property(np, "assigned-addresses", &l);
	i = 0;
	if (pci_addrs != 0) {
		while ((l -= sizeof(struct pci_reg_property)) >= 0) {
			/* XXX assumes PCI addresses mapped 1-1 to physical */
			reg[i].address = pci_addrs[i].addr.a_lo;
			reg[i].size = pci_addrs[i].size_lo;
			++i;
		}
	} else {
		rp = (struct reg_property *) get_property(np, "reg", &l);
		if (rp != 0) {
			while ((l -= sizeof(struct reg_property)) >= 0) {
				reg[i].address = rp[i].address + base_address;
				reg[i].size = rp[i].size;
				++i;
			}
		}
	}
	if (i > 0) {
		np->addrs = reg;
		np->n_addrs = i;
		mem_start += i * sizeof(struct reg_property);
	}

	np->intrs = (int *) get_property(np, "AAPL,interrupts", &l);
	if (np->intrs != 0)
		np->n_intrs = l / sizeof(int);

	/* get the node's full name */
	l = (int) call_prom("package-to-path", 3, 1, node,
			    (char *) mem_start, mem_end - mem_start);
	if (l >= 0) {
		np->full_name = (char *) mem_start;
		np->full_name[l] = 0;
		mem_start = ALIGN(mem_start + l + 1);
	}

	if (np->type != 0 && strcmp(np->type, "dbdma") == 0 && np->n_addrs > 0)
		base_address = np->addrs[0].address;

	child = call_prom("child", 1, 1, node);
	while (child != (void *)0) {
		mem_start = inspect_node(child, np, base_address,
					 mem_start, mem_end);
		child = call_prom("peer", 1, 1, child);
	}

	return mem_start;
}

/*
 * Construct a return a list of the device_nodes with a given name.
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
 * Construct a return a list of the device_nodes with a given type.
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
