/*
 * 
 *
 * Procedures for interfacing to Open Firmware.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 * 
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com 
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG_PROM

#include <stdarg.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/stringify.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/bitops.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/abs_addr.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/pci.h>
#include <asm/iommu.h>
#include <asm/bootinfo.h>
#include <asm/ppcdebug.h>
#include <asm/btext.h>
#include <asm/sections.h>
#include <asm/machdep.h>

#ifdef CONFIG_LOGO_LINUX_CLUT224
#include <linux/linux_logo.h>
extern const struct linux_logo logo_linux_clut224;
#endif

/*
 * Properties whose value is longer than this get excluded from our
 * copy of the device tree. This value does need to be big enough to
 * ensure that we don't lose things like the interrupt-map property
 * on a PCI-PCI bridge.
 */
#define MAX_PROPERTY_LENGTH	(1UL * 1024 * 1024)

/*
 * Eventually bump that one up
 */
#define DEVTREE_CHUNK_SIZE	0x100000

/*
 * This is the size of the local memory reserve map that gets copied
 * into the boot params passed to the kernel. That size is totally
 * flexible as the kernel just reads the list until it encounters an
 * entry with size 0, so it can be changed without breaking binary
 * compatibility
 */
#define MEM_RESERVE_MAP_SIZE	8

/*
 * prom_init() is called very early on, before the kernel text
 * and data have been mapped to KERNELBASE.  At this point the code
 * is running at whatever address it has been loaded at, so
 * references to extern and static variables must be relocated
 * explicitly.  The procedure reloc_offset() returns the address
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


#define PROM_BUG() do {						\
        prom_printf("kernel BUG at %s line 0x%x!\n",		\
		    RELOC(__FILE__), __LINE__);			\
        __asm__ __volatile__(".long " BUG_ILLEGAL_INSTR);	\
} while (0)

#ifdef DEBUG_PROM
#define prom_debug(x...)	prom_printf(x)
#else
#define prom_debug(x...)
#endif


typedef u32 prom_arg_t;

struct prom_args {
        u32 service;
        u32 nargs;
        u32 nret;
        prom_arg_t args[10];
        prom_arg_t *rets;     /* Pointer to return values in args[16]. */
};

struct prom_t {
	unsigned long entry;
	ihandle root;
	ihandle chosen;
	int cpu;
	ihandle stdout;
	ihandle disp_node;
	struct prom_args args;
	unsigned long version;
	unsigned long root_size_cells;
	unsigned long root_addr_cells;
};

struct pci_reg_property {
	struct pci_address addr;
	u32 size_hi;
	u32 size_lo;
};

struct mem_map_entry {
	u64	base;
	u64	size;
};

typedef u32 cell_t;

extern void __start(unsigned long r3, unsigned long r4, unsigned long r5);

extern unsigned long reloc_offset(void);
extern void enter_prom(struct prom_args *args, unsigned long entry);
extern void copy_and_flush(unsigned long dest, unsigned long src,
			   unsigned long size, unsigned long offset);

extern unsigned long klimit;

/* prom structure */
static struct prom_t __initdata prom;

#define PROM_SCRATCH_SIZE 256

static char __initdata of_stdout_device[256];
static char __initdata prom_scratch[PROM_SCRATCH_SIZE];

static unsigned long __initdata dt_header_start;
static unsigned long __initdata dt_struct_start, dt_struct_end;
static unsigned long __initdata dt_string_start, dt_string_end;

static unsigned long __initdata prom_initrd_start, prom_initrd_end;

static int __initdata iommu_force_on;
static int __initdata ppc64_iommu_off;
static int __initdata of_platform;

static char __initdata prom_cmd_line[COMMAND_LINE_SIZE];

static unsigned long __initdata alloc_top;
static unsigned long __initdata alloc_top_high;
static unsigned long __initdata alloc_bottom;
static unsigned long __initdata rmo_top;
static unsigned long __initdata ram_top;

static struct mem_map_entry __initdata mem_reserve_map[MEM_RESERVE_MAP_SIZE];
static int __initdata mem_reserve_cnt;

static cell_t __initdata regbuf[1024];


#define MAX_CPU_THREADS 2

/* TO GO */
#ifdef CONFIG_HMT
struct {
	unsigned int pir;
	unsigned int threadid;
} hmt_thread_data[NR_CPUS];
#endif /* CONFIG_HMT */

/*
 * This are used in calls to call_prom.  The 4th and following
 * arguments to call_prom should be 32-bit values.  64 bit values
 * are truncated to 32 bits (and fortunately don't get interpreted
 * as two arguments).
 */
#define ADDR(x)		(u32) ((unsigned long)(x) - offset)

/* This is the one and *ONLY* place where we actually call open
 * firmware from, since we need to make sure we're running in 32b
 * mode when we do.  We switch back to 64b mode upon return.
 */

#define PROM_ERROR	(-1)

static int __init call_prom(const char *service, int nargs, int nret, ...)
{
	int i;
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	va_list list;

	_prom->args.service = ADDR(service);
	_prom->args.nargs = nargs;
	_prom->args.nret = nret;
	_prom->args.rets = (prom_arg_t *)&(_prom->args.args[nargs]);

	va_start(list, nret);
	for (i=0; i < nargs; i++)
		_prom->args.args[i] = va_arg(list, prom_arg_t);
	va_end(list);

	for (i=0; i < nret ;i++)
		_prom->args.rets[i] = 0;

	enter_prom(&_prom->args, _prom->entry);

	return (nret > 0) ? _prom->args.rets[0] : 0;
}


static unsigned int __init prom_claim(unsigned long virt, unsigned long size,
				unsigned long align)
{
	return (unsigned int)call_prom("claim", 3, 1,
				       (prom_arg_t)virt, (prom_arg_t)size,
				       (prom_arg_t)align);
}

static void __init prom_print(const char *msg)
{
	const char *p, *q;
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);

	if (_prom->stdout == 0)
		return;

	for (p = msg; *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n'; ++q)
			;
		if (q > p)
			call_prom("write", 3, 1, _prom->stdout, p, q - p);
		if (*q == 0)
			break;
		++q;
		call_prom("write", 3, 1, _prom->stdout, ADDR("\r\n"), 2);
	}
}


static void __init prom_print_hex(unsigned long val)
{
	unsigned long offset = reloc_offset();
	int i, nibbles = sizeof(val)*2;
	char buf[sizeof(val)*2+1];
	struct prom_t *_prom = PTRRELOC(&prom);

	for (i = nibbles-1;  i >= 0;  i--) {
		buf[i] = (val & 0xf) + '0';
		if (buf[i] > '9')
			buf[i] += ('a'-'0'-10);
		val >>= 4;
	}
	buf[nibbles] = '\0';
	call_prom("write", 3, 1, _prom->stdout, buf, nibbles);
}


static void __init prom_printf(const char *format, ...)
{
	unsigned long offset = reloc_offset();
	const char *p, *q, *s;
	va_list args;
	unsigned long v;
	struct prom_t *_prom = PTRRELOC(&prom);

	va_start(args, format);
	for (p = PTRRELOC(format); *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n' && *q != '%'; ++q)
			;
		if (q > p)
			call_prom("write", 3, 1, _prom->stdout, p, q - p);
		if (*q == 0)
			break;
		if (*q == '\n') {
			++q;
			call_prom("write", 3, 1, _prom->stdout,
				  ADDR("\r\n"), 2);
			continue;
		}
		++q;
		if (*q == 0)
			break;
		switch (*q) {
		case 's':
			++q;
			s = va_arg(args, const char *);
			prom_print(s);
			break;
		case 'x':
			++q;
			v = va_arg(args, unsigned long);
			prom_print_hex(v);
			break;
		}
	}
}


static void __init __attribute__((noreturn)) prom_panic(const char *reason)
{
	unsigned long offset = reloc_offset();

	prom_print(PTRRELOC(reason));
	/* ToDo: should put up an SRC here */
	call_prom("exit", 0, 0);

	for (;;)			/* should never get here */
		;
}


static int __init prom_next_node(phandle *nodep)
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

static int __init prom_getprop(phandle node, const char *pname,
			       void *value, size_t valuelen)
{
	unsigned long offset = reloc_offset();

	return call_prom("getprop", 4, 1, node, ADDR(pname),
			 (u32)(unsigned long) value, (u32) valuelen);
}

static int __init prom_getproplen(phandle node, const char *pname)
{
	unsigned long offset = reloc_offset();

	return call_prom("getproplen", 2, 1, node, ADDR(pname));
}

static int __init prom_setprop(phandle node, const char *pname,
			       void *value, size_t valuelen)
{
	unsigned long offset = reloc_offset();

	return call_prom("setprop", 4, 1, node, ADDR(pname),
			 (u32)(unsigned long) value, (u32) valuelen);
}


/*
 * Early parsing of the command line passed to the kernel, used for
 * the options that affect the iommu
 */
static void __init early_cmdline_parse(void)
{
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	char *opt, *p;
	int l = 0;

	RELOC(prom_cmd_line[0]) = 0;
	p = RELOC(prom_cmd_line);
	if ((long)_prom->chosen > 0)
		l = prom_getprop(_prom->chosen, "bootargs", p, COMMAND_LINE_SIZE-1);
#ifdef CONFIG_CMDLINE
	if (l == 0) /* dbl check */
		strlcpy(RELOC(prom_cmd_line),
			RELOC(CONFIG_CMDLINE), sizeof(prom_cmd_line));
#endif /* CONFIG_CMDLINE */
	prom_printf("command line: %s\n", RELOC(prom_cmd_line));

	opt = strstr(RELOC(prom_cmd_line), RELOC("iommu="));
	if (opt) {
		prom_printf("iommu opt is: %s\n", opt);
		opt += 6;
		while (*opt && *opt == ' ')
			opt++;
		if (!strncmp(opt, RELOC("off"), 3))
			RELOC(ppc64_iommu_off) = 1;
		else if (!strncmp(opt, RELOC("force"), 5))
			RELOC(iommu_force_on) = 1;
	}
}

/*
 * Memory allocation strategy... our layout is normally:
 *
 *  at 14Mb or more we vmlinux, then a gap and initrd. In some rare cases, initrd
 *  might end up beeing before the kernel though. We assume this won't override
 *  the final kernel at 0, we have no provision to handle that in this version,
 *  but it should hopefully never happen.
 *
 *  alloc_top is set to the top of RMO, eventually shrink down if the TCEs overlap
 *  alloc_bottom is set to the top of kernel/initrd
 *
 *  from there, allocations are done that way : rtas is allocated topmost, and
 *  the device-tree is allocated from the bottom. We try to grow the device-tree
 *  allocation as we progress. If we can't, then we fail, we don't currently have
 *  a facility to restart elsewhere, but that shouldn't be necessary neither
 *
 *  Note that calls to reserve_mem have to be done explicitely, memory allocated
 *  with either alloc_up or alloc_down isn't automatically reserved.
 */


/*
 * Allocates memory in the RMO upward from the kernel/initrd
 *
 * When align is 0, this is a special case, it means to allocate in place
 * at the current location of alloc_bottom or fail (that is basically
 * extending the previous allocation). Used for the device-tree flattening
 */
static unsigned long __init alloc_up(unsigned long size, unsigned long align)
{
	unsigned long offset = reloc_offset();
	unsigned long base = _ALIGN_UP(RELOC(alloc_bottom), align);
	unsigned long addr = 0;

	prom_debug("alloc_up(%x, %x)\n", size, align);
	if (RELOC(ram_top) == 0)
		prom_panic("alloc_up() called with mem not initialized\n");

	if (align)
		base = _ALIGN_UP(RELOC(alloc_bottom), align);
	else
		base = RELOC(alloc_bottom);

	for(; (base + size) <= RELOC(alloc_top); 
	    base = _ALIGN_UP(base + 0x100000, align)) {
		prom_debug("    trying: 0x%x\n\r", base);
		addr = (unsigned long)prom_claim(base, size, 0);
		if ((int)addr != PROM_ERROR)
			break;
		addr = 0;
		if (align == 0)
			break;
	}
	if (addr == 0)
		return 0;
	RELOC(alloc_bottom) = addr;

	prom_debug(" -> %x\n", addr);
	prom_debug("  alloc_bottom : %x\n", RELOC(alloc_bottom));
	prom_debug("  alloc_top    : %x\n", RELOC(alloc_top));
	prom_debug("  alloc_top_hi : %x\n", RELOC(alloc_top_high));
	prom_debug("  rmo_top      : %x\n", RELOC(rmo_top));
	prom_debug("  ram_top      : %x\n", RELOC(ram_top));

	return addr;
}

/*
 * Allocates memory downard, either from top of RMO, or if highmem
 * is set, from the top of RAM. Note that this one doesn't handle
 * failures. In does claim memory if highmem is not set.
 */
static unsigned long __init alloc_down(unsigned long size, unsigned long align,
				       int highmem)
{
	unsigned long offset = reloc_offset();
	unsigned long base, addr = 0;

	prom_debug("alloc_down(%x, %x, %s)\n", size, align,
		   highmem ? RELOC("(high)") : RELOC("(low)"));
	if (RELOC(ram_top) == 0)
		prom_panic("alloc_down() called with mem not initialized\n");

	if (highmem) {
		/* Carve out storage for the TCE table. */
		addr = _ALIGN_DOWN(RELOC(alloc_top_high) - size, align);
		if (addr <= RELOC(alloc_bottom))
			return 0;
		else {
			/* Will we bump into the RMO ? If yes, check out that we
			 * didn't overlap existing allocations there, if we did,
			 * we are dead, we must be the first in town !
			 */
			if (addr < RELOC(rmo_top)) {
				/* Good, we are first */
				if (RELOC(alloc_top) == RELOC(rmo_top))
					RELOC(alloc_top) = RELOC(rmo_top) = addr;
				else
					return 0;
			}
			RELOC(alloc_top_high) = addr;
		}
		goto bail;
	}

	base = _ALIGN_DOWN(RELOC(alloc_top) - size, align);
	for(; base > RELOC(alloc_bottom); base = _ALIGN_DOWN(base - 0x100000, align))  {
		prom_debug("    trying: 0x%x\n\r", base);
		addr = (unsigned long)prom_claim(base, size, 0);
		if ((int)addr != PROM_ERROR)
			break;
		addr = 0;
	}
	if (addr == 0)
		return 0;
	RELOC(alloc_top) = addr;

 bail:
	prom_debug(" -> %x\n", addr);
	prom_debug("  alloc_bottom : %x\n", RELOC(alloc_bottom));
	prom_debug("  alloc_top    : %x\n", RELOC(alloc_top));
	prom_debug("  alloc_top_hi : %x\n", RELOC(alloc_top_high));
	prom_debug("  rmo_top      : %x\n", RELOC(rmo_top));
	prom_debug("  ram_top      : %x\n", RELOC(ram_top));

	return addr;
}

/*
 * Parse a "reg" cell
 */
static unsigned long __init prom_next_cell(int s, cell_t **cellp)
{
	cell_t *p = *cellp;
	unsigned long r = 0;

	/* Ignore more than 2 cells */
	while (s > 2) {
		p++;
		s--;
	}
	while (s) {
		r <<= 32;
		r |= *(p++);
		s--;
	}

	*cellp = p;
	return r;
}

/*
 * Very dumb function for adding to the memory reserve list, but
 * we don't need anything smarter at this point
 *
 * XXX Eventually check for collisions. They should NEVER happen
 * if problems seem to show up, it would be a good start to track
 * them down.
 */
static void reserve_mem(unsigned long base, unsigned long size)
{
	unsigned long offset = reloc_offset();
	unsigned long top = base + size;
	unsigned long cnt = RELOC(mem_reserve_cnt);

	if (size == 0)
		return;

	/* We need to always keep one empty entry so that we
	 * have our terminator with "size" set to 0 since we are
	 * dumb and just copy this entire array to the boot params
	 */
	base = _ALIGN_DOWN(base, PAGE_SIZE);
	top = _ALIGN_UP(top, PAGE_SIZE);
	size = top - base;

	if (cnt >= (MEM_RESERVE_MAP_SIZE - 1))
		prom_panic("Memory reserve map exhausted !\n");
	RELOC(mem_reserve_map)[cnt].base = base;
	RELOC(mem_reserve_map)[cnt].size = size;
	RELOC(mem_reserve_cnt) = cnt + 1;
}

/*
 * Initialize memory allocation mecanism, parse "memory" nodes and
 * obtain that way the top of memory and RMO to setup out local allocator
 */
static void __init prom_init_mem(void)
{
	phandle node;
	char *path, type[64];
	unsigned int plen;
	cell_t *p, *endp;
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);

	/*
	 * We iterate the memory nodes to find
	 * 1) top of RMO (first node)
	 * 2) top of memory
	 */
	prom_debug("root_addr_cells: %x\n", (long)_prom->root_addr_cells);
	prom_debug("root_size_cells: %x\n", (long)_prom->root_size_cells);

	prom_debug("scanning memory:\n");
	path = RELOC(prom_scratch);

	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		prom_getprop(node, "device_type", type, sizeof(type));

		if (strcmp(type, RELOC("memory")))
			continue;
	
		plen = prom_getprop(node, "reg", RELOC(regbuf), sizeof(regbuf));
		if (plen > sizeof(regbuf)) {
			prom_printf("memory node too large for buffer !\n");
			plen = sizeof(regbuf);
		}
		p = RELOC(regbuf);
		endp = p + (plen / sizeof(cell_t));

#ifdef DEBUG_PROM
		memset(path, 0, PROM_SCRATCH_SIZE);
		call_prom("package-to-path", 3, 1, node, path, PROM_SCRATCH_SIZE-1);
		prom_debug("  node %s :\n", path);
#endif /* DEBUG_PROM */

		while ((endp - p) >= (_prom->root_addr_cells + _prom->root_size_cells)) {
			unsigned long base, size;

			base = prom_next_cell(_prom->root_addr_cells, &p);
			size = prom_next_cell(_prom->root_size_cells, &p);

			if (size == 0)
				continue;
			prom_debug("    %x %x\n", base, size);
			if (base == 0)
				RELOC(rmo_top) = size;
			if ((base + size) > RELOC(ram_top))
				RELOC(ram_top) = base + size;
		}
	}

	/* Setup our top/bottom alloc points, that is top of RMO or top of
	 * segment 0 when running non-LPAR
	 */
	if ( RELOC(of_platform) == PLATFORM_PSERIES_LPAR )
		RELOC(alloc_top) = RELOC(rmo_top);
	else
		RELOC(alloc_top) = RELOC(rmo_top) = min(0x40000000ul, RELOC(ram_top));
	RELOC(alloc_bottom) = PAGE_ALIGN(RELOC(klimit) - offset + 0x4000);
	RELOC(alloc_top_high) = RELOC(ram_top);

	/* Check if we have an initrd after the kernel, if we do move our bottom
	 * point to after it
	 */
	if (RELOC(prom_initrd_start)) {
		if ((RELOC(prom_initrd_start) + RELOC(prom_initrd_end))
		    > RELOC(alloc_bottom))
			RELOC(alloc_bottom) = PAGE_ALIGN(RELOC(prom_initrd_end));
	}

	prom_printf("memory layout at init:\n");
	prom_printf("  alloc_bottom : %x\n", RELOC(alloc_bottom));
	prom_printf("  alloc_top    : %x\n", RELOC(alloc_top));
	prom_printf("  alloc_top_hi : %x\n", RELOC(alloc_top_high));
	prom_printf("  rmo_top      : %x\n", RELOC(rmo_top));
	prom_printf("  ram_top      : %x\n", RELOC(ram_top));
}


/*
 * Allocate room for and instanciate RTAS
 */
static void __init prom_instantiate_rtas(void)
{
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	phandle prom_rtas, rtas_node;
	u32 base, entry = 0;
	u32 size = 0;

	prom_debug("prom_instantiate_rtas: start...\n");

	prom_rtas = call_prom("finddevice", 1, 1, ADDR("/rtas"));
	prom_debug("prom_rtas: %x\n", prom_rtas);
	if (prom_rtas == (phandle) -1)
		return;

	prom_getprop(prom_rtas, "rtas-size", &size, sizeof(size));
	if (size == 0)
		return;

	base = alloc_down(size, PAGE_SIZE, 0);
	if (base == 0) {
		prom_printf("RTAS allocation failed !\n");
		return;
	}
	prom_printf("instantiating rtas at 0x%x", base);

	rtas_node = call_prom("open", 1, 1, ADDR("/rtas"));
	prom_printf("...");

	if (call_prom("call-method", 3, 2,
		      ADDR("instantiate-rtas"),
		      rtas_node, base) != PROM_ERROR) {
		entry = (long)_prom->args.rets[1];
	}
	if (entry == 0) {
		prom_printf(" failed\n");
		return;
	}
	prom_printf(" done\n");

	reserve_mem(base, size);

	prom_setprop(prom_rtas, "linux,rtas-base", &base, sizeof(base));
	prom_setprop(prom_rtas, "linux,rtas-entry", &entry, sizeof(entry));

	prom_debug("rtas base     = 0x%x\n", base);
	prom_debug("rtas entry    = 0x%x\n", entry);
	prom_debug("rtas size     = 0x%x\n", (long)size);

	prom_debug("prom_instantiate_rtas: end...\n");
}


/*
 * Allocate room for and initialize TCE tables
 */
static void __init prom_initialize_tce_table(void)
{
	phandle node;
	ihandle phb_node;
	unsigned long offset = reloc_offset();
	char compatible[64], type[64], model[64];
	char *path = RELOC(prom_scratch);
	u64 base, align;
	u32 minalign, minsize;
	u64 tce_entry, *tce_entryp;
	u64 local_alloc_top, local_alloc_bottom;
	u64 i;

	if (RELOC(ppc64_iommu_off))
		return;

	prom_debug("starting prom_initialize_tce_table\n");

	/* Cache current top of allocs so we reserve a single block */
	local_alloc_top = RELOC(alloc_top_high);
	local_alloc_bottom = local_alloc_top;

	/* Search all nodes looking for PHBs. */
	for (node = 0; prom_next_node(&node); ) {
		compatible[0] = 0;
		type[0] = 0;
		model[0] = 0;
		prom_getprop(node, "compatible",
			     compatible, sizeof(compatible));
		prom_getprop(node, "device_type", type, sizeof(type));
		prom_getprop(node, "model", model, sizeof(model));

		if ((type[0] == 0) || (strstr(type, RELOC("pci")) == NULL))
			continue;

		/* Keep the old logic in tack to avoid regression. */
		if (compatible[0] != 0) {
			if ((strstr(compatible, RELOC("python")) == NULL) &&
			    (strstr(compatible, RELOC("Speedwagon")) == NULL) &&
			    (strstr(compatible, RELOC("Winnipeg")) == NULL))
				continue;
		} else if (model[0] != 0) {
			if ((strstr(model, RELOC("ython")) == NULL) &&
			    (strstr(model, RELOC("peedwagon")) == NULL) &&
			    (strstr(model, RELOC("innipeg")) == NULL))
				continue;
		}

		if (prom_getprop(node, "tce-table-minalign", &minalign,
				 sizeof(minalign)) == PROM_ERROR)
			minalign = 0;
		if (prom_getprop(node, "tce-table-minsize", &minsize,
				 sizeof(minsize)) == PROM_ERROR)
			minsize = 4UL << 20;

		/*
		 * Even though we read what OF wants, we just set the table
		 * size to 4 MB.  This is enough to map 2GB of PCI DMA space.
		 * By doing this, we avoid the pitfalls of trying to DMA to
		 * MMIO space and the DMA alias hole.
		 *
		 * On POWER4, firmware sets the TCE region by assuming
		 * each TCE table is 8MB. Using this memory for anything
		 * else will impact performance, so we always allocate 8MB.
		 * Anton
		 */
		if (__is_processor(PV_POWER4) || __is_processor(PV_POWER4p))
			minsize = 8UL << 20;
		else
			minsize = 4UL << 20;

		/* Align to the greater of the align or size */
		align = max(minalign, minsize);
		base = alloc_down(minsize, align, 1);
		if (base == 0)
			prom_panic("ERROR, cannot find space for TCE table.\n");
		if (base < local_alloc_bottom)
			local_alloc_bottom = base;

		/* Save away the TCE table attributes for later use. */
		prom_setprop(node, "linux,tce-base", &base, sizeof(base));
		prom_setprop(node, "linux,tce-size", &minsize, sizeof(minsize));

		/* It seems OF doesn't null-terminate the path :-( */
		memset(path, 0, sizeof(path));
		/* Call OF to setup the TCE hardware */
		if (call_prom("package-to-path", 3, 1, node,
			      path, PROM_SCRATCH_SIZE-1) == PROM_ERROR) {
			prom_printf("package-to-path failed\n");
		}

		prom_debug("TCE table: %s\n", path);
		prom_debug("\tnode = 0x%x\n", node);
		prom_debug("\tbase = 0x%x\n", base);
		prom_debug("\tsize = 0x%x\n", minsize);

		/* Initialize the table to have a one-to-one mapping
		 * over the allocated size.
		 */
		tce_entryp = (unsigned long *)base;
		for (i = 0; i < (minsize >> 3) ;tce_entryp++, i++) {
			tce_entry = (i << PAGE_SHIFT);
			tce_entry |= 0x3;
			*tce_entryp = tce_entry;
		}

		prom_printf("opening PHB %s", path);
		phb_node = call_prom("open", 1, 1, path);
		if ( (long)phb_node <= 0)
			prom_printf("... failed\n");
		else
			prom_printf("... done\n");

		call_prom("call-method", 6, 0, ADDR("set-64-bit-addressing"),
			  phb_node, -1, minsize,
			  (u32) base, (u32) (base >> 32));
		call_prom("close", 1, 0, phb_node);
	}

	reserve_mem(local_alloc_bottom, local_alloc_top - local_alloc_bottom);

	/* Flag the first invalid entry */
	prom_debug("ending prom_initialize_tce_table\n");
}

/*
 * With CHRP SMP we need to use the OF to start the other
 * processors so we can't wait until smp_boot_cpus (the OF is
 * trashed by then) so we have to put the processors into
 * a holding pattern controlled by the kernel (not OF) before
 * we destroy the OF.
 *
 * This uses a chunk of low memory, puts some holding pattern
 * code there and sends the other processors off to there until
 * smp_boot_cpus tells them to do something.  The holding pattern
 * checks that address until its cpu # is there, when it is that
 * cpu jumps to __secondary_start().  smp_boot_cpus() takes care
 * of setting those values.
 *
 * We also use physical address 0x4 here to tell when a cpu
 * is in its holding pattern code.
 *
 * Fixup comment... DRENG / PPPBBB - Peter
 *
 * -- Cort
 */
static void __init prom_hold_cpus(void)
{
	unsigned long i;
	unsigned int reg;
	phandle node;
	unsigned long offset = reloc_offset();
	char type[64];
	int cpuid = 0;
	unsigned int interrupt_server[MAX_CPU_THREADS];
	unsigned int cpu_threads, hw_cpu_num;
	int propsize;
	extern void __secondary_hold(void);
	extern unsigned long __secondary_hold_spinloop;
	extern unsigned long __secondary_hold_acknowledge;
	unsigned long *spinloop
		= (void *)virt_to_abs(&__secondary_hold_spinloop);
	unsigned long *acknowledge
		= (void *)virt_to_abs(&__secondary_hold_acknowledge);
	unsigned long secondary_hold
		= virt_to_abs(*PTRRELOC((unsigned long *)__secondary_hold));
	struct prom_t *_prom = PTRRELOC(&prom);

	prom_debug("prom_hold_cpus: start...\n");
	prom_debug("    1) spinloop       = 0x%x\n", (unsigned long)spinloop);
	prom_debug("    1) *spinloop      = 0x%x\n", *spinloop);
	prom_debug("    1) acknowledge    = 0x%x\n",
		   (unsigned long)acknowledge);
	prom_debug("    1) *acknowledge   = 0x%x\n", *acknowledge);
	prom_debug("    1) secondary_hold = 0x%x\n", secondary_hold);

	/* Set the common spinloop variable, so all of the secondary cpus
	 * will block when they are awakened from their OF spinloop.
	 * This must occur for both SMP and non SMP kernels, since OF will
	 * be trashed when we move the kernel.
	 */
	*spinloop = 0;

#ifdef CONFIG_HMT
	for (i=0; i < NR_CPUS; i++) {
		RELOC(hmt_thread_data)[i].pir = 0xdeadbeef;
	}
#endif
	/* look for cpus */
	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		prom_getprop(node, "device_type", type, sizeof(type));
		if (strcmp(type, RELOC("cpu")) != 0)
			continue;

		/* Skip non-configured cpus. */
		if (prom_getprop(node, "status", type, sizeof(type)) > 0)
			if (strcmp(type, RELOC("okay")) != 0)
				continue;

		reg = -1;
		prom_getprop(node, "reg", &reg, sizeof(reg));

		prom_debug("\ncpuid        = 0x%x\n", cpuid);
		prom_debug("cpu hw idx   = 0x%x\n", reg);

		/* Init the acknowledge var which will be reset by
		 * the secondary cpu when it awakens from its OF
		 * spinloop.
		 */
		*acknowledge = (unsigned long)-1;

		propsize = prom_getprop(node, "ibm,ppc-interrupt-server#s",
					&interrupt_server,
					sizeof(interrupt_server));
		if (propsize < 0) {
			/* no property.  old hardware has no SMT */
			cpu_threads = 1;
			interrupt_server[0] = reg; /* fake it with phys id */
		} else {
			/* We have a threaded processor */
			cpu_threads = propsize / sizeof(u32);
			if (cpu_threads > MAX_CPU_THREADS) {
				prom_printf("SMT: too many threads!\n"
					    "SMT: found %x, max is %x\n",
					    cpu_threads, MAX_CPU_THREADS);
				cpu_threads = 1; /* ToDo: panic? */
			}
		}

		hw_cpu_num = interrupt_server[0];
		if (hw_cpu_num != _prom->cpu) {
			/* Primary Thread of non-boot cpu */
			prom_printf("%x : starting cpu hw idx %x... ", cpuid, reg);
			call_prom("start-cpu", 3, 0, node,
				  secondary_hold, reg);

			for ( i = 0 ; (i < 100000000) && 
			      (*acknowledge == ((unsigned long)-1)); i++ )
				mb();

			if (*acknowledge == reg) {
				prom_printf("done\n");
				/* We have to get every CPU out of OF,
				 * even if we never start it. */
				if (cpuid >= NR_CPUS)
					goto next;
			} else {
				prom_printf("failed: %x\n", *acknowledge);
			}
		}
#ifdef CONFIG_SMP
		else
			prom_printf("%x : boot cpu     %x\n", cpuid, reg);
#endif
next:
#ifdef CONFIG_SMP
		/* Init paca for secondary threads.   They start later. */
		for (i=1; i < cpu_threads; i++) {
			cpuid++;
			if (cpuid >= NR_CPUS)
				continue;
		}
#endif /* CONFIG_SMP */
		cpuid++;
	}
#ifdef CONFIG_HMT
	/* Only enable HMT on processors that provide support. */
	if (__is_processor(PV_PULSAR) || 
	    __is_processor(PV_ICESTAR) ||
	    __is_processor(PV_SSTAR)) {
		prom_printf("    starting secondary threads\n");

		for (i = 0; i < NR_CPUS; i += 2) {
			if (!cpu_online(i))
				continue;

			if (i == 0) {
				unsigned long pir = mfspr(SPRN_PIR);
				if (__is_processor(PV_PULSAR)) {
					RELOC(hmt_thread_data)[i].pir = 
						pir & 0x1f;
				} else {
					RELOC(hmt_thread_data)[i].pir = 
						pir & 0x3ff;
				}
			}
		}
	} else {
		prom_printf("Processor is not HMT capable\n");
	}
#endif

	if (cpuid > NR_CPUS)
		prom_printf("WARNING: maximum CPUs (" __stringify(NR_CPUS)
			    ") exceeded: ignoring extras\n");

	prom_debug("prom_hold_cpus: end...\n");
}


static void __init prom_init_client_services(unsigned long pp)
{
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);

	/* Get a handle to the prom entry point before anything else */
	_prom->entry = pp;

	/* Init default value for phys size */
	_prom->root_size_cells = 1;
	_prom->root_addr_cells = 2;

	/* get a handle for the stdout device */
	_prom->chosen = call_prom("finddevice", 1, 1, ADDR("/chosen"));
	if ((long)_prom->chosen <= 0)
		prom_panic("cannot find chosen"); /* msg won't be printed :( */

	/* get device tree root */
	_prom->root = call_prom("finddevice", 1, 1, ADDR("/"));
	if ((long)_prom->root <= 0)
		prom_panic("cannot find device tree root"); /* msg won't be printed :( */
}

static void __init prom_init_stdout(void)
{
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	char *path = RELOC(of_stdout_device);
	char type[16];
	u32 val;

	if (prom_getprop(_prom->chosen, "stdout", &val, sizeof(val)) <= 0)
		prom_panic("cannot find stdout");

	_prom->stdout = val;

	/* Get the full OF pathname of the stdout device */
	memset(path, 0, 256);
	call_prom("instance-to-path", 3, 1, _prom->stdout, path, 255);
	val = call_prom("instance-to-package", 1, 1, _prom->stdout);
	prom_setprop(_prom->chosen, "linux,stdout-package", &val, sizeof(val));
	prom_printf("OF stdout device is: %s\n", RELOC(of_stdout_device));
	prom_setprop(_prom->chosen, "linux,stdout-path",
		     RELOC(of_stdout_device), strlen(RELOC(of_stdout_device))+1);

	/* If it's a display, note it */
	memset(type, 0, sizeof(type));
	prom_getprop(val, "device_type", type, sizeof(type));
	if (strcmp(type, RELOC("display")) == 0) {
		_prom->disp_node = val;
		prom_setprop(val, "linux,boot-display", NULL, 0);
	}
}

static void __init prom_close_stdin(void)
{
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	ihandle val;

	if (prom_getprop(_prom->chosen, "stdin", &val, sizeof(val)) > 0)
		call_prom("close", 1, 0, val);
}

static int __init prom_find_machine_type(void)
{
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	char compat[256];
	int len, i = 0;
	phandle rtas;

	len = prom_getprop(_prom->root, "compatible",
			   compat, sizeof(compat)-1);
	if (len > 0) {
		compat[len] = 0;
		while (i < len) {
			char *p = &compat[i];
			int sl = strlen(p);
			if (sl == 0)
				break;
			if (strstr(p, RELOC("Power Macintosh")) ||
			    strstr(p, RELOC("MacRISC4")))
				return PLATFORM_POWERMAC;
			if (strstr(p, RELOC("Momentum,Maple")))
				return PLATFORM_MAPLE;
			i += sl + 1;
		}
	}
	/* Default to pSeries. We need to know if we are running LPAR */
	rtas = call_prom("finddevice", 1, 1, ADDR("/rtas"));
	if (rtas != (phandle) -1) {
		unsigned long x;
		x = prom_getproplen(rtas, "ibm,hypertas-functions");
		if (x != PROM_ERROR) {
			prom_printf("Hypertas detected, assuming LPAR !\n");
			return PLATFORM_PSERIES_LPAR;
		}
	}
	return PLATFORM_PSERIES;
}

static int __init prom_set_color(ihandle ih, int i, int r, int g, int b)
{
	unsigned long offset = reloc_offset();

	return call_prom("call-method", 6, 1, ADDR("color!"), ih, i, b, g, r);
}

/*
 * If we have a display that we don't know how to drive,
 * we will want to try to execute OF's open method for it
 * later.  However, OF will probably fall over if we do that
 * we've taken over the MMU.
 * So we check whether we will need to open the display,
 * and if so, open it now.
 */
static void __init prom_check_displays(void)
{
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	char type[16], *path;
	phandle node;
	ihandle ih;
	int i;

	static unsigned char default_colors[] = {
		0x00, 0x00, 0x00,
		0x00, 0x00, 0xaa,
		0x00, 0xaa, 0x00,
		0x00, 0xaa, 0xaa,
		0xaa, 0x00, 0x00,
		0xaa, 0x00, 0xaa,
		0xaa, 0xaa, 0x00,
		0xaa, 0xaa, 0xaa,
		0x55, 0x55, 0x55,
		0x55, 0x55, 0xff,
		0x55, 0xff, 0x55,
		0x55, 0xff, 0xff,
		0xff, 0x55, 0x55,
		0xff, 0x55, 0xff,
		0xff, 0xff, 0x55,
		0xff, 0xff, 0xff
	};
	const unsigned char *clut;

	prom_printf("Looking for displays\n");
	for (node = 0; prom_next_node(&node); ) {
		memset(type, 0, sizeof(type));
		prom_getprop(node, "device_type", type, sizeof(type));
		if (strcmp(type, RELOC("display")) != 0)
			continue;

		/* It seems OF doesn't null-terminate the path :-( */
		path = RELOC(prom_scratch);
		memset(path, 0, PROM_SCRATCH_SIZE);

		/*
		 * leave some room at the end of the path for appending extra
		 * arguments
		 */
		if (call_prom("package-to-path", 3, 1, node, path, PROM_SCRATCH_SIZE-10) < 0)
			continue;
		prom_printf("found display   : %s, opening ... ", path);
		
		ih = call_prom("open", 1, 1, path);
		if (ih == (ihandle)0 || ih == (ihandle)-1) {
			prom_printf("failed\n");
			continue;
		}

		/* Success */
		prom_printf("done\n");
		prom_setprop(node, "linux,opened", NULL, 0);

		/*
		 * stdout wasn't a display node, pick the first we can find
		 * for btext
		 */
		if (_prom->disp_node == 0)
			_prom->disp_node = node;

		/* Setup a useable color table when the appropriate
		 * method is available. Should update this to set-colors */
		clut = RELOC(default_colors);
		for (i = 0; i < 32; i++, clut += 3)
			if (prom_set_color(ih, i, clut[0], clut[1],
					   clut[2]) != 0)
				break;

#ifdef CONFIG_LOGO_LINUX_CLUT224
		clut = PTRRELOC(RELOC(logo_linux_clut224.clut));
		for (i = 0; i < RELOC(logo_linux_clut224.clutsize); i++, clut += 3)
			if (prom_set_color(ih, i + 32, clut[0], clut[1],
					   clut[2]) != 0)
				break;
#endif /* CONFIG_LOGO_LINUX_CLUT224 */
	}
}


/* Return (relocated) pointer to this much memory: moves initrd if reqd. */
static void __init *make_room(unsigned long *mem_start, unsigned long *mem_end,
			      unsigned long needed, unsigned long align)
{
	unsigned long offset = reloc_offset();
	void *ret;

	*mem_start = _ALIGN(*mem_start, align);
	while ((*mem_start + needed) > *mem_end) {
		unsigned long room, chunk;

		prom_debug("Chunk exhausted, claiming more at %x...\n",
			   RELOC(alloc_bottom));
		room = RELOC(alloc_top) - RELOC(alloc_bottom);
		if (room > DEVTREE_CHUNK_SIZE)
			room = DEVTREE_CHUNK_SIZE;
		if (room < PAGE_SIZE)
			prom_panic("No memory for flatten_device_tree (no room)");
		chunk = alloc_up(room, 0);
		if (chunk == 0)
			prom_panic("No memory for flatten_device_tree (claim failed)");
		*mem_end = RELOC(alloc_top);
	}

	ret = (void *)*mem_start;
	*mem_start += needed;

	return ret;
}

#define dt_push_token(token, mem_start, mem_end) \
	do { *((u32 *)make_room(mem_start, mem_end, 4, 4)) = token; } while(0)

static unsigned long __init dt_find_string(char *str)
{
	unsigned long offset = reloc_offset();
	char *s, *os;

	s = os = (char *)RELOC(dt_string_start);
	s += 4;
	while (s <  (char *)RELOC(dt_string_end)) {
		if (strcmp(s, str) == 0)
			return s - os;
		s += strlen(s) + 1;
	}
	return 0;
}

static void __init scan_dt_build_strings(phandle node, unsigned long *mem_start,
					 unsigned long *mem_end)
{
	unsigned long offset = reloc_offset();
	char *prev_name, *namep, *sstart;
	unsigned long soff;
	phandle child;

	sstart =  (char *)RELOC(dt_string_start);

	/* get and store all property names */
	prev_name = RELOC("");
	for (;;) {
		
		/* 32 is max len of name including nul. */
		namep = make_room(mem_start, mem_end, 32, 1);
		if (call_prom("nextprop", 3, 1, node, prev_name, namep) <= 0) {
			/* No more nodes: unwind alloc */
			*mem_start = (unsigned long)namep;
			break;
		}
		soff = dt_find_string(namep);
		if (soff != 0) {
			*mem_start = (unsigned long)namep;
			namep = sstart + soff;
		} else {
			/* Trim off some if we can */
			*mem_start = (unsigned long)namep + strlen(namep) + 1;
			RELOC(dt_string_end) = *mem_start;
		}
		prev_name = namep;
	}

	/* do all our children */
	child = call_prom("child", 1, 1, node);
	while (child != (phandle)0) {
		scan_dt_build_strings(child, mem_start, mem_end);
		child = call_prom("peer", 1, 1, child);
	}
}

static void __init scan_dt_build_struct(phandle node, unsigned long *mem_start,
					unsigned long *mem_end)
{
	int l, align;
	phandle child;
	char *namep, *prev_name, *sstart;
	unsigned long soff;
	unsigned char *valp;
	unsigned long offset = reloc_offset();
	char pname[32];
	char *path;

	path = RELOC(prom_scratch);

	dt_push_token(OF_DT_BEGIN_NODE, mem_start, mem_end);

	/* get the node's full name */
	namep = (char *)*mem_start;
	l = call_prom("package-to-path", 3, 1, node,
		      namep, *mem_end - *mem_start);
	if (l >= 0) {
		/* Didn't fit?  Get more room. */
		if (l+1 > *mem_end - *mem_start) {
			namep = make_room(mem_start, mem_end, l+1, 1);
			call_prom("package-to-path", 3, 1, node, namep, l);
		}
		namep[l] = '\0';
		*mem_start = _ALIGN(((unsigned long) namep) + strlen(namep) + 1, 4);
	}

	/* get it again for debugging */
	memset(path, 0, PROM_SCRATCH_SIZE);
	call_prom("package-to-path", 3, 1, node, path, PROM_SCRATCH_SIZE-1);

	/* get and store all properties */
	prev_name = RELOC("");
	sstart = (char *)RELOC(dt_string_start);
	for (;;) {
		if (call_prom("nextprop", 3, 1, node, prev_name, pname) <= 0)
			break;

		/* find string offset */
		soff = dt_find_string(pname);
		if (soff == 0) {
			prom_printf("WARNING: Can't find string index for <%s>, node %s\n",
				    pname, path);
			break;
		}
		prev_name = sstart + soff;

		/* get length */
		l = call_prom("getproplen", 2, 1, node, pname);

		/* sanity checks */
		if (l < 0)
			continue;
		if (l > MAX_PROPERTY_LENGTH) {
			prom_printf("WARNING: ignoring large property ");
			/* It seems OF doesn't null-terminate the path :-( */
			prom_printf("[%s] ", path);
			prom_printf("%s length 0x%x\n", pname, l);
			continue;
		}

		/* push property head */
		dt_push_token(OF_DT_PROP, mem_start, mem_end);
		dt_push_token(l, mem_start, mem_end);
		dt_push_token(soff, mem_start, mem_end);

		/* push property content */
		align = (l >= 8) ? 8 : 4;
		valp = make_room(mem_start, mem_end, l, align);
		call_prom("getprop", 4, 1, node, pname, valp, l);
		*mem_start = _ALIGN(*mem_start, 4);
	}

	/* Add a "linux,phandle" property. */
	soff = dt_find_string(RELOC("linux,phandle"));
	if (soff == 0)
		prom_printf("WARNING: Can't find string index for <linux-phandle>"
			    " node %s\n", path);
	else {
		dt_push_token(OF_DT_PROP, mem_start, mem_end);
		dt_push_token(4, mem_start, mem_end);
		dt_push_token(soff, mem_start, mem_end);
		valp = make_room(mem_start, mem_end, 4, 4);
		*(u32 *)valp = node;
	}

	/* do all our children */
	child = call_prom("child", 1, 1, node);
	while (child != (phandle)0) {
		scan_dt_build_struct(child, mem_start, mem_end);
		child = call_prom("peer", 1, 1, child);
	}

	dt_push_token(OF_DT_END_NODE, mem_start, mem_end);
}

static void __init flatten_device_tree(void)
{
	phandle root;
	unsigned long offset = reloc_offset();
	unsigned long mem_start, mem_end, room;
	struct boot_param_header *hdr;
	char *namep;
	u64 *rsvmap;

	/*
	 * Check how much room we have between alloc top & bottom (+/- a
	 * few pages), crop to 4Mb, as this is our "chuck" size
	 */
	room = RELOC(alloc_top) - RELOC(alloc_bottom) - 0x4000;
	if (room > DEVTREE_CHUNK_SIZE)
		room = DEVTREE_CHUNK_SIZE;
	prom_debug("starting device tree allocs at %x\n", RELOC(alloc_bottom));

	/* Now try to claim that */
	mem_start = (unsigned long)alloc_up(room, PAGE_SIZE);
	if (mem_start == 0)
		prom_panic("Can't allocate initial device-tree chunk\n");
	mem_end = RELOC(alloc_top);

	/* Get root of tree */
	root = call_prom("peer", 1, 1, (phandle)0);
	if (root == (phandle)0)
		prom_panic ("couldn't get device tree root\n");

	/* Build header and make room for mem rsv map */ 
	mem_start = _ALIGN(mem_start, 4);
	hdr = make_room(&mem_start, &mem_end, sizeof(struct boot_param_header), 4);
	RELOC(dt_header_start) = (unsigned long)hdr;
	rsvmap = make_room(&mem_start, &mem_end, sizeof(mem_reserve_map), 8);

	/* Start of strings */
	mem_start = PAGE_ALIGN(mem_start);
	RELOC(dt_string_start) = mem_start;
	mem_start += 4; /* hole */

	/* Add "linux,phandle" in there, we'll need it */
	namep = make_room(&mem_start, &mem_end, 16, 1);
	strcpy(namep, RELOC("linux,phandle"));
	mem_start = (unsigned long)namep + strlen(namep) + 1;
	RELOC(dt_string_end) = mem_start;

	/* Build string array */
	prom_printf("Building dt strings...\n"); 
	scan_dt_build_strings(root, &mem_start, &mem_end);

	/* Build structure */
	mem_start = PAGE_ALIGN(mem_start);
	RELOC(dt_struct_start) = mem_start;
	prom_printf("Building dt structure...\n"); 
	scan_dt_build_struct(root, &mem_start, &mem_end);
	dt_push_token(OF_DT_END, &mem_start, &mem_end);
	RELOC(dt_struct_end) = PAGE_ALIGN(mem_start);

	/* Finish header */
	hdr->magic = OF_DT_HEADER;
	hdr->totalsize = RELOC(dt_struct_end) - RELOC(dt_header_start);
	hdr->off_dt_struct = RELOC(dt_struct_start) - RELOC(dt_header_start);
	hdr->off_dt_strings = RELOC(dt_string_start) - RELOC(dt_header_start);
	hdr->off_mem_rsvmap = ((unsigned long)rsvmap) - RELOC(dt_header_start);
	hdr->version = OF_DT_VERSION;
	hdr->last_comp_version = 1;

	/* Reserve the whole thing and copy the reserve map in, we
	 * also bump mem_reserve_cnt to cause further reservations to
	 * fail since it's too late.
	 */
	reserve_mem(RELOC(dt_header_start), hdr->totalsize);
	memcpy(rsvmap, RELOC(mem_reserve_map), sizeof(mem_reserve_map));

#ifdef DEBUG_PROM
	{
		int i;
		prom_printf("reserved memory map:\n");
		for (i = 0; i < RELOC(mem_reserve_cnt); i++)
			prom_printf("  %x - %x\n", RELOC(mem_reserve_map)[i].base,
				    RELOC(mem_reserve_map)[i].size);
	}
#endif
	RELOC(mem_reserve_cnt) = MEM_RESERVE_MAP_SIZE;

	prom_printf("Device tree strings 0x%x -> 0x%x\n",
		    RELOC(dt_string_start), RELOC(dt_string_end)); 
	prom_printf("Device tree struct  0x%x -> 0x%x\n",
		    RELOC(dt_struct_start), RELOC(dt_struct_end));

 }

static void __init prom_find_boot_cpu(void)
{
	unsigned long offset = reloc_offset();
       	struct prom_t *_prom = PTRRELOC(&prom);
	u32 getprop_rval;
	ihandle prom_cpu;
	phandle cpu_pkg;

	if (prom_getprop(_prom->chosen, "cpu", &prom_cpu, sizeof(prom_cpu)) <= 0)
		prom_panic("cannot find boot cpu");

	cpu_pkg = call_prom("instance-to-package", 1, 1, prom_cpu);

	prom_setprop(cpu_pkg, "linux,boot-cpu", NULL, 0);
	prom_getprop(cpu_pkg, "reg", &getprop_rval, sizeof(getprop_rval));
	_prom->cpu = getprop_rval;

	prom_debug("Booting CPU hw index = 0x%x\n", _prom->cpu);
}

static void __init prom_check_initrd(unsigned long r3, unsigned long r4)
{
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long offset = reloc_offset();
       	struct prom_t *_prom = PTRRELOC(&prom);

	if ( r3 && r4 && r4 != 0xdeadbeef) {
		u64 val;

		RELOC(prom_initrd_start) = (r3 >= KERNELBASE) ? __pa(r3) : r3;
		RELOC(prom_initrd_end) = RELOC(prom_initrd_start) + r4;

		val = (u64)RELOC(prom_initrd_start);
		prom_setprop(_prom->chosen, "linux,initrd-start", &val, sizeof(val));
		val = (u64)RELOC(prom_initrd_end);
		prom_setprop(_prom->chosen, "linux,initrd-end", &val, sizeof(val));

		reserve_mem(RELOC(prom_initrd_start),
			    RELOC(prom_initrd_end) - RELOC(prom_initrd_start));

		prom_debug("initrd_start=0x%x\n", RELOC(prom_initrd_start));
		prom_debug("initrd_end=0x%x\n", RELOC(prom_initrd_end));
	}
#endif /* CONFIG_BLK_DEV_INITRD */
}

/*
 * We enter here early on, when the Open Firmware prom is still
 * handling exceptions and the MMU hash table for us.
 */

unsigned long __init prom_init(unsigned long r3, unsigned long r4, unsigned long pp,
			       unsigned long r6, unsigned long r7)
{	
	unsigned long offset = reloc_offset();
       	struct prom_t *_prom = PTRRELOC(&prom);
	unsigned long phys = KERNELBASE - offset;
	u32 getprop_rval;
	
	/*
	 * First zero the BSS
	 */
	memset(PTRRELOC(&__bss_start), 0, __bss_stop - __bss_start);

	/*
	 * Init interface to Open Firmware, get some node references,
	 * like /chosen
	 */
	prom_init_client_services(pp);

	/*
	 * Init prom stdout device
	 */
	prom_init_stdout();
	prom_debug("klimit=0x%x\n", RELOC(klimit));
	prom_debug("offset=0x%x\n", offset);

	/*
	 * Check for an initrd
	 */
	prom_check_initrd(r3, r4);

	/*
	 * Get default machine type. At this point, we do not differenciate
	 * between pSeries SMP and pSeries LPAR
	 */
	RELOC(of_platform) = prom_find_machine_type();
	getprop_rval = RELOC(of_platform);
	prom_setprop(_prom->chosen, "linux,platform",
		     &getprop_rval, sizeof(getprop_rval));

	/*
	 * On pSeries, copy the CPU hold code
	 */
       	if (RELOC(of_platform) & PLATFORM_PSERIES)
       		copy_and_flush(0, KERNELBASE - offset, 0x100, 0);

	/*
	 * Get memory cells format
	 */
	getprop_rval = 1;
	prom_getprop(_prom->root, "#size-cells",
		     &getprop_rval, sizeof(getprop_rval));
	_prom->root_size_cells = getprop_rval;
	getprop_rval = 2;
	prom_getprop(_prom->root, "#address-cells",
		     &getprop_rval, sizeof(getprop_rval));
	_prom->root_addr_cells = getprop_rval;

	/*
	 * Do early parsing of command line
	 */
	early_cmdline_parse();

	/*
	 * Initialize memory management within prom_init
	 */
	prom_init_mem();

	/*
	 * Determine which cpu is actually running right _now_
	 */
	prom_find_boot_cpu();

	/* 
	 * Initialize display devices
	 */
	prom_check_displays();

	/*
	 * Initialize IOMMU (TCE tables) on pSeries. Do that before anything else
	 * that uses the allocator, we need to make sure we get the top of memory
	 * available for us here...
	 */
	if (RELOC(of_platform) == PLATFORM_PSERIES)
		prom_initialize_tce_table();

	/*
	 * On non-powermacs, try to instantiate RTAS and puts all CPUs
	 * in spin-loops. PowerMacs don't have a working RTAS and use
	 * a different way to spin CPUs
	 */
	if (RELOC(of_platform) != PLATFORM_POWERMAC) {
		prom_instantiate_rtas();
		prom_hold_cpus();
	}

	/*
	 * Fill in some infos for use by the kernel later on
	 */
	if (RELOC(ppc64_iommu_off))
		prom_setprop(_prom->chosen, "linux,iommu-off", NULL, 0);
	if (RELOC(iommu_force_on))
		prom_setprop(_prom->chosen, "linux,iommu-force-on", NULL, 0);

	/*
	 * Now finally create the flattened device-tree
	 */
       	prom_printf("copying OF device tree ...\n");
       	flatten_device_tree();

	/* in case stdin is USB and still active on IBM machines... */
	prom_close_stdin();

	/*
	 * Call OF "quiesce" method to shut down pending DMA's from
	 * devices etc...
	 */
	prom_printf("Calling quiesce ...\n");
	call_prom("quiesce", 0, 0);

	/*
	 * And finally, call the kernel passing it the flattened device
	 * tree and NULL as r5, thus triggering the new entry point which
	 * is common to us and kexec
	 */
	prom_printf("returning from prom_init\n");
	prom_debug("->dt_header_start=0x%x\n", RELOC(dt_header_start));
	prom_debug("->phys=0x%x\n", phys);

	__start(RELOC(dt_header_start), phys, 0);

	return 0;
}

