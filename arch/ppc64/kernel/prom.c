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

#if 0
#define DEBUG_YABOOT
#endif

#if 0
#define DEBUG_PROM
#endif

#include <stdarg.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/blk.h>

#ifdef DEBUG_YABOOT
#define call_yaboot(FUNC,...) \
	do { \
		if (FUNC) {					\
			struct prom_t *_prom = PTRRELOC(&prom);	\
			unsigned long prom_entry = _prom->entry;\
			_prom->entry = (unsigned long)(FUNC);	\
			enter_prom(__VA_ARGS__); 		\
			_prom->entry = prom_entry;		\
		}						\
	} while (0)
#else
#define call_yaboot(FUNC,...) do { ; } while (0)
#endif

#include <linux/types.h>
#include <linux/pci.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/lmb.h>
#include <asm/abs_addr.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/naca.h>
#include <asm/pci.h>
#include "open_pic.h"
#include <asm/bootinfo.h>
#include <asm/ppcdebug.h>

#ifdef CONFIG_FB
#include <asm/linux_logo.h>
#endif

extern char _end[];

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


#define PROM_BUG() do { \
        prom_print(RELOC("kernel BUG at ")); \
        prom_print(RELOC(__FILE__)); \
        prom_print(RELOC(":")); \
        prom_print_hex(__LINE__); \
        prom_print(RELOC("!\n")); \
        __asm__ __volatile__(".long " BUG_ILLEGAL_INSTR); \
} while (0)



struct pci_reg_property {
	struct pci_address addr;
	u32 size_hi;
	u32 size_lo;
};


struct isa_reg_property {
	u32 space;
	u32 address;
	u32 size;
};

struct pci_intr_map {
	struct pci_address addr;
	u32 dunno;
	phandle int_ctrler;
	u32 intr;
};


typedef unsigned long interpret_func(struct device_node *, unsigned long,
				     int, int);
#if 0
static interpret_func interpret_pci_props;
#endif
static unsigned long interpret_pci_props(struct device_node *, unsigned long,
					 int, int);

static interpret_func interpret_isa_props;
static interpret_func interpret_root_props;

#ifndef FB_MAX			/* avoid pulling in all of the fb stuff */
#define FB_MAX	8
#endif

static int ppc64_is_smp;

struct prom_t prom = {
	0,			/* entry */
	0,			/* chosen */
	0,			/* cpu */
	0,			/* stdout */
	0,			/* disp_node */
	{0,0,0,{0},NULL},	/* args */
	0,			/* version */
	32,			/* encode_phys_size */
	0			/* bi_rec pointer */
#ifdef DEBUG_YABOOT
	,NULL			/* yaboot */
#endif
};


char *prom_display_paths[FB_MAX] __initdata = { 0, };
unsigned int prom_num_displays = 0;
char *of_stdout_device = 0;

extern struct rtas_t rtas;
extern unsigned long klimit;
extern unsigned long embedded_sysmap_end;
extern struct lmb lmb;
#ifdef CONFIG_MSCHUNKS
extern struct msChunks msChunks;
#endif /* CONFIG_MSCHUNKS */

#define MAX_PHB 16 * 3  // 16 Towers * 3 PHBs/tower
struct _of_tce_table of_tce_table[MAX_PHB + 1] = {{0, 0, 0}};

char *bootpath = 0;
char *bootdevice = 0;

int boot_cpuid = 0;

struct device_node *allnodes = 0;

#define UNDEFINED_IRQ 0xffff
unsigned short real_irq_to_virt_map[NR_HW_IRQS];
unsigned short virt_irq_to_real_map[NR_IRQS];
int last_virt_irq = 2;	/* index of last virt_irq.  Skip through IPI */

static unsigned long call_prom(const char *service, int nargs, int nret, ...);
static void prom_exit(void);
static unsigned long copy_device_tree(unsigned long);
static unsigned long inspect_node(phandle, struct device_node *, unsigned long,
				  unsigned long, struct device_node ***);
static unsigned long finish_node(struct device_node *, unsigned long,
				 interpret_func *, int, int);
static unsigned long finish_node_interrupts(struct device_node *, unsigned long);
static unsigned long check_display(unsigned long);
static int prom_next_node(phandle *);
static struct bi_record * prom_bi_rec_verify(struct bi_record *);
static unsigned long prom_bi_rec_reserve(unsigned long);
static struct device_node *find_phandle(phandle);

#ifdef CONFIG_MSCHUNKS
static unsigned long prom_initialize_mschunks(unsigned long);
#ifdef DEBUG_PROM
void prom_dump_mschunks_mapping(void);
#endif /* DEBUG_PROM */
#endif /* CONFIG_MSCHUNKS */
#ifdef DEBUG_PROM
void prom_dump_lmb(void);
#endif

extern unsigned long reloc_offset(void);

extern void enter_prom(void *dummy,...);

void cacheable_memzero(void *, unsigned int);

extern char cmd_line[512];	/* XXX */
unsigned long dev_tree_size;

#ifdef CONFIG_HMT
struct {
	unsigned int pir;
	unsigned int threadid;
} hmt_thread_data[NR_CPUS] = {0};
#endif /* CONFIG_HMT */

char testString[] = "LINUX\n"; 


/* This is the one and *ONLY* place where we actually call open
 * firmware from, since we need to make sure we're running in 32b
 * mode when we do.  We switch back to 64b mode upon return.
 */

static unsigned long __init
call_prom(const char *service, int nargs, int nret, ...)
{
	int i;
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	va_list list;
        
	_prom->args.service = (u32)LONG_LSW(service);
	_prom->args.nargs = nargs;
	_prom->args.nret = nret;
        _prom->args.rets = (prom_arg_t *)&(_prom->args.args[nargs]);

        va_start(list, nret);
	for (i=0; i < nargs ;i++)
		_prom->args.args[i] = (prom_arg_t)LONG_LSW(va_arg(list, unsigned long));
        va_end(list);

	for (i=0; i < nret ;i++)
		_prom->args.rets[i] = 0;

	enter_prom(&_prom->args);

	return (unsigned long)((nret > 0) ? _prom->args.rets[0] : 0);
}


static void __init
prom_exit()
{
	unsigned long offset = reloc_offset();

	call_prom(RELOC("exit"), 0, 0);

	for (;;)			/* should never get here */
		;
}

void __init
prom_enter(void)
{
	unsigned long offset = reloc_offset();

	call_prom(RELOC("enter"), 0, 0);
}


void __init
prom_print(const char *msg)
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
			call_prom(RELOC("write"), 3, 1, _prom->stdout,
				  p, q - p);
		if (*q != 0) {
			++q;
			call_prom(RELOC("write"), 3, 1, _prom->stdout,
				  RELOC("\r\n"), 2);
		}
	}
}

void
prom_print_hex(unsigned long val)
{
        int i, nibbles = sizeof(val)*2;
        char buf[sizeof(val)*2+1];

        for (i = nibbles-1;  i >= 0;  i--) {
                buf[i] = (val & 0xf) + '0';
                if (buf[i] > '9')
                    buf[i] += ('a'-'0'-10);
                val >>= 4;
        }
        buf[nibbles] = '\0';
	prom_print(buf);
}

void
prom_print_nl(void)
{
	unsigned long offset = reloc_offset();
	prom_print(RELOC("\n"));
}


static unsigned long
prom_initialize_naca(unsigned long mem)
{
	phandle node;
	char type[64];
        unsigned long num_cpus = 0;
        unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
        struct naca_struct *_naca = RELOC(naca);

#ifdef DEBUG_PROM
	prom_print(RELOC("prom_initialize_naca: start...\n"));
#endif

	_naca->pftSize = 0;	/* ilog2 of htab size.  computed below. */

        for (node = 0; prom_next_node(&node); ) {
                type[0] = 0;
                call_prom(RELOC("getprop"), 4, 1, node, RELOC("device_type"),
                          type, sizeof(type));

                if (!strcmp(type, RELOC("cpu"))) {
			num_cpus += 1;

			/* We're assuming *all* of the CPUs have the same
			 * d-cache and i-cache sizes... -Peter
			 */
			if ( num_cpus == 1 ) {
				u32 size;

				call_prom(RELOC("getprop"), 4, 1, node,
					  RELOC("d-cache-line-size"),
					  &size, sizeof(size));

				_naca->dCacheL1LineSize     = size;
				_naca->dCacheL1LogLineSize  = __ilog2(size);
				_naca->dCacheL1LinesPerPage = PAGE_SIZE / size;

				call_prom(RELOC("getprop"), 4, 1, node,
					  RELOC("i-cache-line-size"),
					  &size, sizeof(size));

				_naca->iCacheL1LineSize     = size;
				_naca->iCacheL1LogLineSize  = __ilog2(size);
				_naca->iCacheL1LinesPerPage = PAGE_SIZE / size;

				if (_naca->platform == PLATFORM_PSERIES_LPAR) {
					u32 pft_size[2];
					call_prom(RELOC("getprop"), 4, 1, node, 
						  RELOC("ibm,pft-size"),
						  &pft_size, sizeof(pft_size));
				/* pft_size[0] is the NUMA CEC cookie */
					_naca->pftSize = pft_size[1];
				}
			}
                } else if (!strcmp(type, RELOC("serial"))) {
			phandle isa, pci;
			struct isa_reg_property reg;
			union pci_range ranges;

			type[0] = 0;
			call_prom(RELOC("getprop"), 4, 1, node,
				  RELOC("ibm,aix-loc"), type, sizeof(type));

			if (strcmp(type, RELOC("S1")))
				continue;

			call_prom(RELOC("getprop"), 4, 1, node, RELOC("reg"),
				  &reg, sizeof(reg));

			isa = call_prom(RELOC("parent"), 1, 1, node);
			if (!isa)
				PROM_BUG();
			pci = call_prom(RELOC("parent"), 1, 1, isa);
			if (!pci)
				PROM_BUG();

			call_prom(RELOC("getprop"), 4, 1, pci, RELOC("ranges"),
				  &ranges, sizeof(ranges));

			if ( _prom->encode_phys_size == 32 )
				_naca->serialPortAddr = ranges.pci32.phys+reg.address;
			else {
				_naca->serialPortAddr = 
					((((unsigned long)ranges.pci64.phys_hi) << 32) |
					 (ranges.pci64.phys_lo)) + reg.address;
			}
                }
	}

	_naca->interrupt_controller = IC_INVALID;
        for (node = 0; prom_next_node(&node); ) {
                type[0] = 0;
                call_prom(RELOC("getprop"), 4, 1, node, RELOC("name"),
                          type, sizeof(type));
                if (strcmp(type, RELOC("interrupt-controller"))) {
			continue;
		}
                call_prom(RELOC("getprop"), 4, 1, node, RELOC("compatible"),
                          type, sizeof(type));
                if (strstr(type, RELOC("open-pic"))) {
			_naca->interrupt_controller = IC_OPEN_PIC;
		} else if (strstr(type, RELOC("ppc-xicp"))) {
			_naca->interrupt_controller = IC_PPC_XIC;
		} else {
			prom_print(RELOC("prom: failed to recognize interrupt-controller\n"));
		}
		break;
	}

	if (_naca->interrupt_controller == IC_INVALID) {
		prom_print(RELOC("prom: failed to find interrupt-controller\n"));
		PROM_BUG();
	}

	/* We gotta have at least 1 cpu... */
        if (num_cpus < 1)
                PROM_BUG();

	if (num_cpus > 1)
		RELOC(ppc64_is_smp) = 1;

	_naca->physicalMemorySize = lmb_phys_mem_size();

	if (_naca->platform == PLATFORM_PSERIES) {
		unsigned long rnd_mem_size, pteg_count;

		/* round mem_size up to next power of 2 */
		rnd_mem_size = 1UL << __ilog2(_naca->physicalMemorySize);
		if (rnd_mem_size < _naca->physicalMemorySize)
			rnd_mem_size <<= 1;

		/* # pages / 2 */
		pteg_count = (rnd_mem_size >> (12 + 1));

		_naca->pftSize = __ilog2(pteg_count << 7);
	}

	if (_naca->pftSize == 0) {
		prom_print(RELOC("prom: failed to compute pftSize!\n"));
		PROM_BUG();
	}

	/* 
	 * Hardcode to GP size.  I am not sure where to get this info
	 * in general, as there does not appear to be a slb-size OF
	 * entry.  At least in Condor and earlier.  DRENG 
	 */
	_naca->slb_size = 64;

#ifdef DEBUG_PROM
        prom_print(RELOC("naca->physicalMemorySize   = 0x"));
        prom_print_hex(_naca->physicalMemorySize);
        prom_print_nl();

        prom_print(RELOC("naca->pftSize              = 0x"));
        prom_print_hex(_naca->pftSize);
        prom_print_nl();

        prom_print(RELOC("naca->dCacheL1LineSize     = 0x"));
        prom_print_hex(_naca->dCacheL1LineSize);
        prom_print_nl();

        prom_print(RELOC("naca->dCacheL1LogLineSize  = 0x"));
        prom_print_hex(_naca->dCacheL1LogLineSize);
        prom_print_nl();

        prom_print(RELOC("naca->dCacheL1LinesPerPage = 0x"));
        prom_print_hex(_naca->dCacheL1LinesPerPage);
        prom_print_nl();

        prom_print(RELOC("naca->iCacheL1LineSize     = 0x"));
        prom_print_hex(_naca->iCacheL1LineSize);
        prom_print_nl();

        prom_print(RELOC("naca->iCacheL1LogLineSize  = 0x"));
        prom_print_hex(_naca->iCacheL1LogLineSize);
        prom_print_nl();

        prom_print(RELOC("naca->iCacheL1LinesPerPage = 0x"));
        prom_print_hex(_naca->iCacheL1LinesPerPage);
        prom_print_nl();

        prom_print(RELOC("naca->serialPortAddr       = 0x"));
        prom_print_hex(_naca->serialPortAddr);
        prom_print_nl();

        prom_print(RELOC("naca->interrupt_controller = 0x"));
        prom_print_hex(_naca->interrupt_controller);
        prom_print_nl();

        prom_print(RELOC("naca->platform             = 0x"));
        prom_print_hex(_naca->platform);
        prom_print_nl();

	prom_print(RELOC("prom_initialize_naca: end...\n"));
#endif

	return mem;
}


static unsigned long __init
prom_initialize_lmb(unsigned long mem)
{
	phandle node;
	char type[64];
        unsigned long i, offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	union lmb_reg_property reg;
	unsigned long lmb_base, lmb_size;
	unsigned long num_regs, bytes_per_reg = (_prom->encode_phys_size*2)/8;

#ifdef CONFIG_MSCHUNKS
	unsigned long max_addr = 0;
#if 1
	/* Fix me: 630 3G-4G IO hack here... -Peter (PPPBBB) */
	unsigned long io_base = 3UL<<30;
	unsigned long io_size = 1UL<<30;
	unsigned long have_630 = 1;	/* assume we have a 630 */

#else
	unsigned long io_base = <real io base here>;
	unsigned long io_size = <real io size here>;
#endif
#endif /* CONFIG_MSCHUNKS */

	lmb_init();

        for (node = 0; prom_next_node(&node); ) {
                type[0] = 0;
                call_prom(RELOC("getprop"), 4, 1, node, RELOC("device_type"),
                          type, sizeof(type));

                if (strcmp(type, RELOC("memory")))
			continue;

		num_regs = call_prom(RELOC("getprop"), 4, 1, node, RELOC("reg"),
			&reg, sizeof(reg)) / bytes_per_reg;

		for (i=0; i < num_regs ;i++) {
			if (_prom->encode_phys_size == 32) {
				lmb_base = reg.addr32[i].address;
				lmb_size = reg.addr32[i].size;
			} else {
				lmb_base = reg.addr64[i].address;
				lmb_size = reg.addr64[i].size;
			}

#ifdef CONFIG_MSCHUNKS
			if ( lmb_addrs_overlap(lmb_base,lmb_size,
						io_base,io_size) ) {
				/* If we really have dram here, then we don't
			 	 * have a 630! -Peter
			 	 */
				have_630 = 0;
			}
#endif /* CONFIG_MSCHUNKS */
			if ( lmb_add(lmb_base, lmb_size) < 0 )
				prom_print(RELOC("Too many LMB's, discarding this one...\n"));
#ifdef CONFIG_MSCHUNKS
			else if ( max_addr < (lmb_base+lmb_size-1) )
				max_addr = lmb_base+lmb_size-1;
#endif /* CONFIG_MSCHUNKS */
		}

	}

#ifdef CONFIG_MSCHUNKS
	if ( have_630 && lmb_addrs_overlap(0,max_addr,io_base,io_size) )
		lmb_add_io(io_base, io_size);
#endif /* CONFIG_MSCHUNKS */

	lmb_analyze();
#ifdef DEBUG_PROM
	prom_dump_lmb();
#endif /* DEBUG_PROM */

#ifdef CONFIG_MSCHUNKS
	mem = prom_initialize_mschunks(mem);
#ifdef DEBUG_PROM
	prom_dump_mschunks_mapping();
#endif /* DEBUG_PROM */
#endif /* CONFIG_MSCHUNKS */

	return mem;
}


static void __init
prom_instantiate_rtas(void)
{
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	struct rtas_t *_rtas = PTRRELOC(&rtas);
	struct naca_struct *_naca = RELOC(naca);
	ihandle prom_rtas;
        u32 getprop_rval;

#ifdef DEBUG_PROM
	prom_print(RELOC("prom_instantiate_rtas: start...\n"));
#endif
	prom_rtas = (ihandle)call_prom(RELOC("finddevice"), 1, 1, RELOC("/rtas"));
	if (prom_rtas != (ihandle) -1) {
		char hypertas_funcs[1024];
		int  rc; 
		
		if ((rc = call_prom(RELOC("getprop"), 
				  4, 1, prom_rtas,
				  RELOC("ibm,hypertas-functions"), 
				  hypertas_funcs, 
				  sizeof(hypertas_funcs))) > 0) {
			_naca->platform = PLATFORM_PSERIES_LPAR;
		}

		call_prom(RELOC("getprop"), 
			  4, 1, prom_rtas,
			  RELOC("rtas-size"), 
			  &getprop_rval, 
			  sizeof(getprop_rval));
	        _rtas->size = getprop_rval;
		prom_print(RELOC("instantiating rtas"));
		if (_rtas->size != 0) {
			unsigned long rtas_region = RTAS_INSTANTIATE_MAX;

			/* Grab some space within the first RTAS_INSTANTIATE_MAX bytes
			 * of physical memory (or within the RMO region) because RTAS
			 * runs in 32-bit mode and relocate off.
			 */
			if ( _naca->platform == PLATFORM_PSERIES_LPAR ) {
				struct lmb *_lmb  = PTRRELOC(&lmb);
				rtas_region = min(_lmb->rmo_size, RTAS_INSTANTIATE_MAX);
			}
			_rtas->base = lmb_alloc_base(_rtas->size, PAGE_SIZE, rtas_region);

			prom_print(RELOC(" at 0x"));
			prom_print_hex(_rtas->base);

			prom_rtas = (ihandle)call_prom(RELOC("open"), 
					      	1, 1, RELOC("/rtas"));
			prom_print(RELOC("..."));

			if ((long)call_prom(RELOC("call-method"), 3, 2,
						      RELOC("instantiate-rtas"),
						      prom_rtas,
						      _rtas->base) >= 0) {
				_rtas->entry = (long)_prom->args.rets[1];
			}
		}

		if (_rtas->entry <= 0) {
			prom_print(RELOC(" failed\n"));
		} else {
			prom_print(RELOC(" done\n"));
		}

#ifdef DEBUG_PROM
        	prom_print(RELOC("rtas->base                 = 0x"));
        	prom_print_hex(_rtas->base);
        	prom_print_nl();
        	prom_print(RELOC("rtas->entry                = 0x"));
        	prom_print_hex(_rtas->entry);
        	prom_print_nl();
        	prom_print(RELOC("rtas->size                 = 0x"));
        	prom_print_hex(_rtas->size);
        	prom_print_nl();
#endif
	}
#ifdef DEBUG_PROM
	prom_print(RELOC("prom_instantiate_rtas: end...\n"));
#endif
}

unsigned long prom_strtoul(const char *cp)
{
	unsigned long result = 0,value;

	while (*cp) {
		value = *cp-'0';
		result = result*10 + value;
		cp++;
	} 

	return result;
}


#ifdef CONFIG_MSCHUNKS
static unsigned long
prom_initialize_mschunks(unsigned long mem)
{
        unsigned long offset = reloc_offset();
	struct lmb *_lmb  = PTRRELOC(&lmb);
	struct msChunks *_msChunks = PTRRELOC(&msChunks);
	unsigned long i, pchunk = 0;
	unsigned long addr_range = _lmb->memory.size + _lmb->memory.iosize;
	unsigned long chunk_size = _lmb->memory.lcd_size;


	mem = msChunks_alloc(mem, addr_range / chunk_size, chunk_size);

	/* First create phys -> abs mapping for memory/dram */
	for (i=0; i < _lmb->memory.cnt ;i++) {
		unsigned long base = _lmb->memory.region[i].base;
		unsigned long size = _lmb->memory.region[i].size;
		unsigned long achunk = addr_to_chunk(base);
		unsigned long end_achunk = addr_to_chunk(base+size);

		if(_lmb->memory.region[i].type != LMB_MEMORY_AREA)
			continue;

		_lmb->memory.region[i].physbase = chunk_to_addr(pchunk);
		for (; achunk < end_achunk ;) {
			PTRRELOC(_msChunks->abs)[pchunk++] = achunk++;
		}
	}

#ifdef CONFIG_MSCHUNKS
	/* Now create phys -> abs mapping for IO */
	for (i=0; i < _lmb->memory.cnt ;i++) {
		unsigned long base = _lmb->memory.region[i].base;
		unsigned long size = _lmb->memory.region[i].size;
		unsigned long achunk = addr_to_chunk(base);
		unsigned long end_achunk = addr_to_chunk(base+size);

		if(_lmb->memory.region[i].type != LMB_IO_AREA)
			continue;

		_lmb->memory.region[i].physbase = chunk_to_addr(pchunk);
		for (; achunk < end_achunk ;) {
			PTRRELOC(_msChunks->abs)[pchunk++] = achunk++;
		}
	}
#endif /* CONFIG_MSCHUNKS */

	return mem;
}

#ifdef DEBUG_PROM
void
prom_dump_mschunks_mapping(void)
{
        unsigned long offset = reloc_offset();
	struct msChunks *_msChunks = PTRRELOC(&msChunks);
	unsigned long chunk;

        prom_print(RELOC("\nprom_dump_mschunks_mapping:\n"));
        prom_print(RELOC("    msChunks.num_chunks         = 0x"));
        prom_print_hex(_msChunks->num_chunks);
	prom_print_nl();
        prom_print(RELOC("    msChunks.chunk_size         = 0x"));
        prom_print_hex(_msChunks->chunk_size);
	prom_print_nl();
        prom_print(RELOC("    msChunks.chunk_shift        = 0x"));
        prom_print_hex(_msChunks->chunk_shift);
	prom_print_nl();
        prom_print(RELOC("    msChunks.chunk_mask         = 0x"));
        prom_print_hex(_msChunks->chunk_mask);
	prom_print_nl();
        prom_print(RELOC("    msChunks.abs                = 0x"));
        prom_print_hex(_msChunks->abs);
	prom_print_nl();

        prom_print(RELOC("    msChunks mapping:\n"));
	for(chunk=0; chunk < _msChunks->num_chunks ;chunk++) {
        	prom_print(RELOC("        phys 0x"));
        	prom_print_hex(chunk);
        	prom_print(RELOC(" -> abs 0x"));
        	prom_print_hex(PTRRELOC(_msChunks->abs)[chunk]);
		prom_print_nl();
	}

}
#endif /* DEBUG_PROM */
#endif /* CONFIG_MSCHUNKS */

#ifdef DEBUG_PROM
void
prom_dump_lmb(void)
{
        unsigned long i;
        unsigned long offset = reloc_offset();
	struct lmb *_lmb  = PTRRELOC(&lmb);

        prom_print(RELOC("\nprom_dump_lmb:\n"));
        prom_print(RELOC("    memory.cnt                  = 0x"));
        prom_print_hex(_lmb->memory.cnt);
	prom_print_nl();
        prom_print(RELOC("    memory.size                 = 0x"));
        prom_print_hex(_lmb->memory.size);
	prom_print_nl();
        prom_print(RELOC("    memory.lcd_size             = 0x"));
        prom_print_hex(_lmb->memory.lcd_size);
	prom_print_nl();
        for (i=0; i < _lmb->memory.cnt ;i++) {
                prom_print(RELOC("    memory.region[0x"));
		prom_print_hex(i);
		prom_print(RELOC("].base       = 0x"));
                prom_print_hex(_lmb->memory.region[i].base);
		prom_print_nl();
                prom_print(RELOC("                      .physbase = 0x"));
                prom_print_hex(_lmb->memory.region[i].physbase);
		prom_print_nl();
                prom_print(RELOC("                      .size     = 0x"));
                prom_print_hex(_lmb->memory.region[i].size);
		prom_print_nl();
                prom_print(RELOC("                      .type     = 0x"));
                prom_print_hex(_lmb->memory.region[i].type);
		prom_print_nl();
        }

	prom_print_nl();
        prom_print(RELOC("    reserved.cnt                  = 0x"));
        prom_print_hex(_lmb->reserved.cnt);
	prom_print_nl();
        prom_print(RELOC("    reserved.size                 = 0x"));
        prom_print_hex(_lmb->reserved.size);
	prom_print_nl();
        prom_print(RELOC("    reserved.lcd_size             = 0x"));
        prom_print_hex(_lmb->reserved.lcd_size);
	prom_print_nl();
        for (i=0; i < _lmb->reserved.cnt ;i++) {
                prom_print(RELOC("    reserved.region[0x"));
		prom_print_hex(i);
		prom_print(RELOC("].base       = 0x"));
                prom_print_hex(_lmb->reserved.region[i].base);
		prom_print_nl();
                prom_print(RELOC("                      .physbase = 0x"));
                prom_print_hex(_lmb->reserved.region[i].physbase);
		prom_print_nl();
                prom_print(RELOC("                      .size     = 0x"));
                prom_print_hex(_lmb->reserved.region[i].size);
		prom_print_nl();
                prom_print(RELOC("                      .type     = 0x"));
                prom_print_hex(_lmb->reserved.region[i].type);
		prom_print_nl();
        }
}
#endif /* DEBUG_PROM */


void
prom_initialize_tce_table(void)
{
	phandle node;
	ihandle phb_node;
        unsigned long offset = reloc_offset();
	char compatible[64], path[64], type[64], model[64];
	unsigned long i, table = 0;
	unsigned long base, vbase, align;
	unsigned int minalign, minsize;
	struct _of_tce_table *prom_tce_table = RELOC(of_tce_table);
	unsigned long tce_entry, *tce_entryp;

#ifdef DEBUG_PROM
	prom_print(RELOC("starting prom_initialize_tce_table\n"));
#endif

	/* Search all nodes looking for PHBs. */
	for (node = 0; prom_next_node(&node); ) {
		compatible[0] = 0;
		type[0] = 0;
		model[0] = 0;
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("compatible"),
			  compatible, sizeof(compatible));
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("device_type"),
			  type, sizeof(type));
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("model"),
			  model, sizeof(model));

		/* Keep the old logic in tack to avoid regression. */
		if (compatible[0] != 0) {
			if((strstr(compatible, RELOC("python")) == NULL) &&
			   (strstr(compatible, RELOC("Speedwagon")) == NULL) &&
			   (strstr(compatible, RELOC("Winnipeg")) == NULL))
				continue;
		} else if (model[0] != 0) {
			if ((strstr(model, RELOC("ython")) == NULL) &&
			    (strstr(model, RELOC("peedwagon")) == NULL) &&
			    (strstr(model, RELOC("innipeg")) == NULL))
				continue;
		}

		if ((type[0] == 0) || (strstr(type, RELOC("pci")) == NULL)) {
			continue;
		}

		if (call_prom(RELOC("getprop"), 4, 1, node, 
			     RELOC("tce-table-minalign"), &minalign, 
			     sizeof(minalign)) < 0) {
			minalign = 0;
		}

		if (call_prom(RELOC("getprop"), 4, 1, node, 
			     RELOC("tce-table-minsize"), &minsize, 
			     sizeof(minsize)) < 0) {
			minsize = 4UL << 20;
		}

		/* Even though we read what OF wants, we just set the table
		 * size to 4 MB.  This is enough to map 2GB of PCI DMA space.
		 * By doing this, we avoid the pitfalls of trying to DMA to
		 * MMIO space and the DMA alias hole.
		 */
		minsize = 4UL << 20;

		/* Align to the greater of the align or size */
		align = (minalign < minsize) ? minsize : minalign;

		/* Carve out storage for the TCE table. */
		base = lmb_alloc(8UL << 20, 8UL << 20);

		if ( !base ) {
			prom_print(RELOC("ERROR, cannot find space for TCE table.\n"));
			prom_exit();
		}

		vbase = absolute_to_virt(base);

		/* Save away the TCE table attributes for later use. */
		prom_tce_table[table].node = node;
		prom_tce_table[table].base = vbase;
		prom_tce_table[table].size = minsize;

#ifdef DEBUG_PROM
		prom_print(RELOC("TCE table: 0x"));
		prom_print_hex(table);
		prom_print_nl();

		prom_print(RELOC("\tnode = 0x"));
		prom_print_hex(node);
		prom_print_nl();

		prom_print(RELOC("\tbase = 0x"));
		prom_print_hex(vbase);
		prom_print_nl();

		prom_print(RELOC("\tsize = 0x"));
		prom_print_hex(minsize);
		prom_print_nl();
#endif

		/* Initialize the table to have a one-to-one mapping
		 * over the allocated size.
		 */
		tce_entryp = (unsigned long *)base;
		for (i = 0; i < (minsize >> 3) ;tce_entryp++, i++) {
			tce_entry = (i << PAGE_SHIFT);
			tce_entry |= 0x3;
			*tce_entryp = tce_entry;
		}

		/* Call OF to setup the TCE hardware */
		if (call_prom(RELOC("package-to-path"), 3, 1, node,
                             path, 255) <= 0) {
                        prom_print(RELOC("package-to-path failed\n"));
                } else {
                        prom_print(RELOC("opened "));
                        prom_print(path);
                        prom_print_nl();
                }

                phb_node = (ihandle)call_prom(RELOC("open"), 1, 1, path);
                if ( (long)phb_node <= 0) {
                        prom_print(RELOC("open failed\n"));
                } else {
                        prom_print(RELOC("open success\n"));
                }
                call_prom(RELOC("call-method"), 6, 0,
                             RELOC("set-64-bit-addressing"),
			     phb_node,
			     -1,
                             minsize, 
                             base & 0xffffffff,
                             (base >> 32) & 0xffffffff);
                call_prom(RELOC("close"), 1, 0, phb_node);

		table++;
	}

	/* Flag the first invalid entry */
	prom_tce_table[table].node = 0;
#ifdef DEBUG_PROM
	prom_print(RELOC("ending prom_initialize_tce_table\n"));
#endif
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
static void
prom_hold_cpus(unsigned long mem)
{
	unsigned long i;
	unsigned int reg;
	phandle node;
	unsigned long offset = reloc_offset();
	char type[64], *path;
	extern void __secondary_hold(void);
        extern unsigned long __secondary_hold_spinloop;
        extern unsigned long __secondary_hold_acknowledge;
        unsigned long *spinloop     = __v2a(&__secondary_hold_spinloop);
        unsigned long *acknowledge  = __v2a(&__secondary_hold_acknowledge);
        unsigned long secondary_hold = (unsigned long)__v2a(*PTRRELOC((unsigned long *)__secondary_hold));
	struct paca_struct *_xPaca = PTRRELOC(&paca[0]);
	struct prom_t *_prom = PTRRELOC(&prom);

#ifdef DEBUG_PROM
	prom_print(RELOC("prom_hold_cpus: start...\n"));
	prom_print(RELOC("    1) spinloop       = 0x"));
	prom_print_hex(spinloop);
	prom_print_nl();
	prom_print(RELOC("    1) *spinloop      = 0x"));
	prom_print_hex(*spinloop);
	prom_print_nl();
	prom_print(RELOC("    1) acknowledge    = 0x"));
	prom_print_hex(acknowledge);
	prom_print_nl();
	prom_print(RELOC("    1) *acknowledge   = 0x"));
	prom_print_hex(*acknowledge);
	prom_print_nl();
	prom_print(RELOC("    1) secondary_hold = 0x"));
	prom_print_hex(secondary_hold);
	prom_print_nl();
#endif

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
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("device_type"),
			  type, sizeof(type));
		if (strcmp(type, RELOC("cpu")) != 0)
			continue;

		/* Skip non-configured cpus. */
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("status"),
			  type, sizeof(type));
		if (strcmp(type, RELOC("okay")) != 0)
			continue;

                reg = -1;
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("reg"),
			  &reg, sizeof(reg));

		/* Only need to start secondary procs, not ourself. */
		if ( reg == _prom->cpu )
			continue;

		path = (char *) mem;
		memset(path, 0, 256);
		if ((long) call_prom(RELOC("package-to-path"), 3, 1,
				     node, path, 255) < 0)
			continue;

#ifdef DEBUG_PROM
		prom_print_nl();
		prom_print(RELOC("cpu hw idx   = 0x"));
		prom_print_hex(reg);
		prom_print_nl();
#endif
		prom_print(RELOC("starting cpu "));
		prom_print(path);

		/* Init the acknowledge var which will be reset by
		 * the secondary cpu when it awakens from its OF
		 * spinloop.
		 */
		*acknowledge = (unsigned long)-1;

#ifdef DEBUG_PROM
		prom_print(RELOC("    3) spinloop       = 0x"));
		prom_print_hex(spinloop);
		prom_print_nl();
		prom_print(RELOC("    3) *spinloop      = 0x"));
		prom_print_hex(*spinloop);
		prom_print_nl();
		prom_print(RELOC("    3) acknowledge    = 0x"));
		prom_print_hex(acknowledge);
		prom_print_nl();
		prom_print(RELOC("    3) *acknowledge   = 0x"));
		prom_print_hex(*acknowledge);
		prom_print_nl();
		prom_print(RELOC("    3) secondary_hold = 0x"));
		prom_print_hex(secondary_hold);
		prom_print_nl();
		prom_print_nl();
#endif
		call_prom(RELOC("start-cpu"), 3, 0, node, secondary_hold, reg);
		prom_print(RELOC("..."));
		for ( i = 0 ; (i < 100000000) && 
			      (*acknowledge == ((unsigned long)-1)); i++ ) ;
#ifdef DEBUG_PROM
		{
			unsigned long *p = 0x0;
			prom_print(RELOC("    4) 0x0 = 0x"));
			prom_print_hex(*p);
			prom_print_nl();
		}
#endif
		if (*acknowledge == reg) {
			prom_print(RELOC("ok\n"));
			/* Set the number of active processors. */
			_xPaca[reg].active = 1;
		} else {
			prom_print(RELOC("failed: "));
			prom_print_hex(*acknowledge);
			prom_print_nl();
		}
	}
#ifdef CONFIG_HMT
	/* Only enable HMT on processors that provide support. */
	if (__is_processor(PV_PULSAR) || 
	    __is_processor(PV_ICESTAR) ||
	    __is_processor(PV_SSTAR)) {
		prom_print(RELOC("    starting secondary threads\n"));

		for (i = 0; i < NR_CPUS; i += 2) {
			if (!_xPaca[i].active)
				continue;

			if (i == boot_cpuid) {
				unsigned long pir = _get_PIR();
				if (__is_processor(PV_PULSAR)) {
					RELOC(hmt_thread_data)[i].pir = 
						pir & 0x1f;
				} else {
					RELOC(hmt_thread_data)[i].pir = 
						pir & 0x3ff;
				}
			}
			_xPaca[i+1].active = 1;
			RELOC(hmt_thread_data)[i].threadid = i+1;
		}
	} else {
		prom_print(RELOC("Processor is not HMT capable\n"));
	}
#endif
	
#ifdef DEBUG_PROM
	prom_print(RELOC("prom_hold_cpus: end...\n"));
#endif
}


/*
 * We enter here early on, when the Open Firmware prom is still
 * handling exceptions and the MMU hash table for us.
 */

unsigned long __init
prom_init(unsigned long r3, unsigned long r4, unsigned long pp,
	  unsigned long r6, unsigned long r7, yaboot_debug_t *yaboot)
{
	int chrp = 0;
	unsigned long mem;
	ihandle prom_mmu, prom_op, prom_root, prom_cpu;
	phandle cpu_pkg;
	unsigned long offset = reloc_offset();
	long l;
	char *p, *d;
 	unsigned long phys;
        u32 getprop_rval;
        struct naca_struct   *_naca = RELOC(naca);
	struct paca_struct *_xPaca = PTRRELOC(&paca[0]);
	struct prom_t *_prom = PTRRELOC(&prom);

	/* Default machine type. */
	_naca->platform = PLATFORM_PSERIES;
	/* Reset klimit to take into account the embedded system map */
	if (RELOC(embedded_sysmap_end))
		RELOC(klimit) = __va(PAGE_ALIGN(RELOC(embedded_sysmap_end)));

	/* Get a handle to the prom entry point before anything else */
	_prom->entry = pp;
	_prom->bi_recs = prom_bi_rec_verify((struct bi_record *)r6);
	if ( _prom->bi_recs != NULL ) {
		RELOC(klimit) = PTRUNRELOC((unsigned long)_prom->bi_recs + _prom->bi_recs->data[1]);
	}

#ifdef DEBUG_YABOOT
	call_yaboot(yaboot->dummy,offset>>32,offset&0xffffffff);
	call_yaboot(yaboot->printf, RELOC("offset = 0x%08x%08x\n"), LONG_MSW(offset), LONG_LSW(offset));
#endif

 	/* Default */
 	phys = KERNELBASE - offset;

#ifdef DEBUG_YABOOT
	call_yaboot(yaboot->printf, RELOC("phys = 0x%08x%08x\n"), LONG_MSW(phys), LONG_LSW(phys));
#endif


#ifdef DEBUG_YABOOT
	_prom->yaboot = yaboot;
	call_yaboot(yaboot->printf, RELOC("pp = 0x%08x%08x\n"), LONG_MSW(pp), LONG_LSW(pp));
	call_yaboot(yaboot->printf, RELOC("prom = 0x%08x%08x\n"), LONG_MSW(_prom->entry), LONG_LSW(_prom->entry));
#endif

	/* First get a handle for the stdout device */
	_prom->chosen = (ihandle)call_prom(RELOC("finddevice"), 1, 1,
				       RELOC("/chosen"));

#ifdef DEBUG_YABOOT
	call_yaboot(yaboot->printf, RELOC("prom->chosen = 0x%08x%08x\n"), LONG_MSW(_prom->chosen), LONG_LSW(_prom->chosen));
#endif

	if ((long)_prom->chosen <= 0)
		prom_exit();

        if ((long)call_prom(RELOC("getprop"), 4, 1, _prom->chosen,
			    RELOC("stdout"), &getprop_rval,
			    sizeof(getprop_rval)) <= 0)
                prom_exit();

        _prom->stdout = (ihandle)(unsigned long)getprop_rval;

#ifdef DEBUG_YABOOT
        if (_prom->stdout == 0) {
	    call_yaboot(yaboot->printf, RELOC("prom->stdout = 0x%08x%08x\n"), LONG_MSW(_prom->stdout), LONG_LSW(_prom->stdout));
        }

	call_yaboot(yaboot->printf, RELOC("prom->stdout = 0x%08x%08x\n"), LONG_MSW(_prom->stdout), LONG_LSW(_prom->stdout));
#endif

#ifdef DEBUG_YABOOT
	call_yaboot(yaboot->printf, RELOC("Location: 0x11\n"));
#endif

	mem = RELOC(klimit) - offset; 
#ifdef DEBUG_YABOOT
	call_yaboot(yaboot->printf, RELOC("Location: 0x11b\n"));
#endif

	/* Get the full OF pathname of the stdout device */
	p = (char *) mem;
	memset(p, 0, 256);
	call_prom(RELOC("instance-to-path"), 3, 1, _prom->stdout, p, 255);
	RELOC(of_stdout_device) = PTRUNRELOC(p);
	mem += strlen(p) + 1;

	getprop_rval = 1;
	prom_root = (ihandle)call_prom(RELOC("finddevice"), 1, 1, RELOC("/"));
	if (prom_root != (ihandle)-1) {
                call_prom(RELOC("getprop"), 4, 1,
                    prom_root, RELOC("#size-cells"),
		    &getprop_rval, sizeof(getprop_rval));
	}
	_prom->encode_phys_size = (getprop_rval==1) ? 32 : 64;

#ifdef DEBUG_PROM
	prom_print(RELOC("DRENG:    Detect OF version...\n"));
#endif
	/* Find the OF version */
	prom_op = (ihandle)call_prom(RELOC("finddevice"), 1, 1, RELOC("/openprom"));
	if (prom_op != (ihandle)-1) {
		char model[64];
		long sz;
		sz = (long)call_prom(RELOC("getprop"), 4, 1, prom_op,
				    RELOC("model"), model, 64);
		if (sz > 0) {
			char *c;
			/* hack to skip the ibm chrp firmware # */
			if ( strncmp(model,RELOC("IBM"),3) ) {
				for (c = model; *c; c++)
					if (*c >= '0' && *c <= '9') {
						_prom->version = *c - '0';
						break;
					}
			}
			else
				chrp = 1;
		}
	}
	if (_prom->version >= 3)
		prom_print(RELOC("OF Version 3 detected.\n"));


	/* Determine which cpu is actually running right _now_ */
        if ((long)call_prom(RELOC("getprop"), 4, 1, _prom->chosen,
			    RELOC("cpu"), &getprop_rval,
			    sizeof(getprop_rval)) <= 0)
                prom_exit();

	prom_cpu = (ihandle)(unsigned long)getprop_rval;
	cpu_pkg = call_prom(RELOC("instance-to-package"), 1, 1, prom_cpu);
	call_prom(RELOC("getprop"), 4, 1,
		cpu_pkg, RELOC("reg"),
		&getprop_rval, sizeof(getprop_rval));
	_prom->cpu = (int)(unsigned long)getprop_rval;
	_xPaca[_prom->cpu].active = 1;
#ifdef CONFIG_SMP
	RELOC(cpu_online_map) = 1 << _prom->cpu;
#endif
	RELOC(boot_cpuid) = _prom->cpu;

#ifdef DEBUG_PROM
  	prom_print(RELOC("Booting CPU hw index = 0x"));
  	prom_print_hex(_prom->cpu);
  	prom_print_nl();
#endif

	/* Get the boot device and translate it to a full OF pathname. */
	p = (char *) mem;
	l = (long) call_prom(RELOC("getprop"), 4, 1, _prom->chosen,
			    RELOC("bootpath"), p, 1<<20);
	if (l > 0) {
		p[l] = 0;	/* should already be null-terminated */
		RELOC(bootpath) = PTRUNRELOC(p);
		mem += l + 1;
		d = (char *) mem;
		*d = 0;
		call_prom(RELOC("canon"), 3, 1, p, d, 1<<20);
		RELOC(bootdevice) = PTRUNRELOC(d);
		mem = DOUBLEWORD_ALIGN(mem + strlen(d) + 1);
	}

	mem = prom_initialize_lmb(mem);

	mem = prom_bi_rec_reserve(mem);

	prom_instantiate_rtas();
        
        /* Initialize some system info into the Naca early... */
        mem = prom_initialize_naca(mem);

        /* If we are on an SMP machine, then we *MUST* do the
         * following, regardless of whether we have an SMP
         * kernel or not.
         */
        if (RELOC(ppc64_is_smp))
	        prom_hold_cpus(mem);

	mem = check_display(mem);

#ifdef DEBUG_PROM
	prom_print(RELOC("copying OF device tree...\n"));
#endif
	mem = copy_device_tree(mem);

	RELOC(klimit) = mem + offset;

	lmb_reserve(0, __pa(RELOC(klimit)));

	if (_naca->platform == PLATFORM_PSERIES)
		prom_initialize_tce_table();

 	if ((long) call_prom(RELOC("getprop"), 4, 1,
				_prom->chosen,
				RELOC("mmu"),
				&getprop_rval,
				sizeof(getprop_rval)) <= 0) {	
		prom_print(RELOC(" no MMU found\n"));
                prom_exit();
	}

	/* We assume the phys. address size is 3 cells */
	RELOC(prom_mmu) = (ihandle)(unsigned long)getprop_rval;

	if ((long)call_prom(RELOC("call-method"), 4, 4,
				RELOC("translate"),
				prom_mmu,
				(void *)(KERNELBASE - offset),
				(void *)1) != 0) {
		prom_print(RELOC(" (translate failed) "));
	} else {
		prom_print(RELOC(" (translate ok) "));
		phys = (unsigned long)_prom->args.rets[3];
	}

	/* If OpenFirmware version >= 3, then use quiesce call */
	if (_prom->version >= 3) {
		prom_print(RELOC("Calling quiesce ...\n"));
		call_prom(RELOC("quiesce"), 0, 0);
		phys = KERNELBASE - offset;
	}

	prom_print(RELOC("returning from prom_init\n"));
	return phys;
}


static int
prom_set_color(ihandle ih, int i, int r, int g, int b)
{
	unsigned long offset = reloc_offset();

	return (int)(long)call_prom(RELOC("call-method"), 6, 1,
		                    RELOC("color!"),
                                    ih,
                                    (void *)(long) i,
                                    (void *)(long) b,
                                    (void *)(long) g,
                                    (void *)(long) r );
}

/*
 * If we have a display that we don't know how to drive,
 * we will want to try to execute OF's open method for it
 * later.  However, OF will probably fall over if we do that
 * we've taken over the MMU.
 * So we check whether we will need to open the display,
 * and if so, open it now.
 */
static unsigned long __init
check_display(unsigned long mem)
{
	phandle node;
	ihandle ih;
	int i;
	unsigned long offset = reloc_offset();
        struct prom_t *_prom = PTRRELOC(&prom);
	char type[64], *path;
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

	_prom->disp_node = 0;

	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("device_type"),
			  type, sizeof(type));
		if (strcmp(type, RELOC("display")) != 0)
			continue;
		/* It seems OF doesn't null-terminate the path :-( */
		path = (char *) mem;
		memset(path, 0, 256);
		if ((long) call_prom(RELOC("package-to-path"), 3, 1,
				    node, path, 255) < 0)
			continue;
		prom_print(RELOC("opening display "));
		prom_print(path);
		ih = (ihandle)call_prom(RELOC("open"), 1, 1, path);
		if (ih == (ihandle)0 || ih == (ihandle)-1) {
			prom_print(RELOC("... failed\n"));
			continue;
		}
		prom_print(RELOC("... ok\n"));

		if (_prom->disp_node == 0)
			_prom->disp_node = (ihandle)(unsigned long)node;

		/* Setup a useable color table when the appropriate
		 * method is available. Should update this to set-colors */
		for (i = 0; i < 32; i++)
			if (prom_set_color(ih, i, RELOC(default_colors)[i*3],
					   RELOC(default_colors)[i*3+1],
					   RELOC(default_colors)[i*3+2]) != 0)
				break;

#ifdef CONFIG_FB
		for (i = 0; i < LINUX_LOGO_COLORS; i++)
			if (prom_set_color(ih, i + 32,
					   RELOC(linux_logo_red)[i],
					   RELOC(linux_logo_green)[i],
					   RELOC(linux_logo_blue)[i]) != 0)
				break;
#endif /* CONFIG_FB */

		/*
		 * If this display is the device that OF is using for stdout,
		 * move it to the front of the list.
		 */
		mem += strlen(path) + 1;
		i = RELOC(prom_num_displays)++;
		if (RELOC(of_stdout_device) != 0 && i > 0
		    && strcmp(PTRRELOC(RELOC(of_stdout_device)), path) == 0) {
			for (; i > 0; --i)
				RELOC(prom_display_paths[i]) = RELOC(prom_display_paths[i-1]);
		}
		RELOC(prom_display_paths[i]) = PTRUNRELOC(path);
		if (RELOC(prom_num_displays) >= FB_MAX)
			break;
	}
	return DOUBLEWORD_ALIGN(mem);
}

void
virt_irq_init(void)
{
	int i;
	for (i = 0; i < NR_IRQS; i++)
		virt_irq_to_real_map[i] = UNDEFINED_IRQ;
	for (i = 0; i < NR_HW_IRQS; i++)
		real_irq_to_virt_map[i] = UNDEFINED_IRQ;
}

/* Create a mapping for a real_irq if it doesn't already exist.
 * Return the virtual irq as a convenience.
 */
unsigned long
virt_irq_create_mapping(unsigned long real_irq)
{
	unsigned long virq;
	if (naca->interrupt_controller == IC_OPEN_PIC)
		return real_irq;	/* no mapping for openpic (for now) */
	virq = real_irq_to_virt(real_irq);
	if (virq == UNDEFINED_IRQ) {
		/* Assign a virtual IRQ number */
		if (real_irq < NR_IRQS && virt_irq_to_real(real_irq) == UNDEFINED_IRQ) {
			/* A 1-1 mapping will work. */
			virq = real_irq;
		} else {
			while (last_virt_irq < NR_IRQS &&
			       virt_irq_to_real(++last_virt_irq) != UNDEFINED_IRQ)
				/* skip irq's in use */;
			if (last_virt_irq >= NR_IRQS)
				panic("Too many IRQs are required on this system.  NR_IRQS=%d\n", NR_IRQS);
			virq = last_virt_irq;
		}
		virt_irq_to_real_map[virq] = real_irq;
		real_irq_to_virt_map[real_irq] = virq;
	}
	return virq;
}


static int __init
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
static unsigned long __init
copy_device_tree(unsigned long mem_start)
{
	phandle root;
	unsigned long new_start;
	struct device_node **allnextp;
	unsigned long offset = reloc_offset();
	unsigned long mem_end = mem_start + (8<<20);

	root = call_prom(RELOC("peer"), 1, 1, (phandle)0);
	if (root == (phandle)0) {
		prom_print(RELOC("couldn't get device tree root\n"));
		prom_exit();
	}
	allnextp = &RELOC(allnodes);
	mem_start = DOUBLEWORD_ALIGN(mem_start);
	new_start = inspect_node(root, 0, mem_start, mem_end, &allnextp);
	*allnextp = 0;
	return new_start;
}

__init
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
		if ((long) call_prom(RELOC("nextprop"), 3, 1, node, prev_name,
				    namep) <= 0)
			break;
		mem_start = DOUBLEWORD_ALIGN((unsigned long)namep + strlen(namep) + 1);
		prev_name = namep;
		valp = (unsigned char *) mem_start;
		pp->value = PTRUNRELOC(valp);
		pp->length = (int)(long)
			call_prom(RELOC("getprop"), 4, 1, node, namep,
				  valp, mem_end - mem_start);
		if (pp->length < 0)
			continue;
		mem_start = DOUBLEWORD_ALIGN(mem_start + pp->length);
		*prev_propp = PTRUNRELOC(pp);
		prev_propp = &pp->next;
	}
	*prev_propp = 0;

	/* get the node's full name */
	l = (long) call_prom(RELOC("package-to-path"), 3, 1, node,
			    (char *) mem_start, mem_end - mem_start);
	if (l >= 0) {
		np->full_name = PTRUNRELOC((char *) mem_start);
		*(char *)(mem_start + l) = 0;
		mem_start = DOUBLEWORD_ALIGN(mem_start + l + 1);
	}

	/* do all our children */
	child = call_prom(RELOC("child"), 1, 1, node);
	while (child != (phandle)0) {
		mem_start = inspect_node(child, np, mem_start, mem_end,
					 allnextpp);
		child = call_prom(RELOC("peer"), 1, 1, child);
	}

	return mem_start;
}

/*
 * finish_device_tree is called once things are running normally
 * (i.e. with text and data mapped to the address they were linked at).
 * It traverses the device tree and fills in the name, type,
 * {n_}addrs and {n_}intrs fields of each node.
 */
void __init
finish_device_tree(void)
{
	unsigned long mem = klimit;

	virt_irq_init();

	mem = finish_node(allnodes, mem, NULL, 0, 0);
	dev_tree_size = mem - (unsigned long) allnodes;

	mem = _ALIGN(mem, PAGE_SIZE);
	lmb_reserve(__pa(klimit), mem-klimit);

	klimit = mem;

	rtas.dev = find_devices("rtas");
}

static unsigned long __init
finish_node(struct device_node *np, unsigned long mem_start,
	    interpret_func *ifunc, int naddrc, int nsizec)
{
	struct device_node *child;
	int *ip;

	np->name = get_property(np, "name", 0);
	np->type = get_property(np, "device_type", 0);

	/* get the device addresses and interrupts */
	if (ifunc != NULL) {
	  mem_start = ifunc(np, mem_start, naddrc, nsizec);
	}
	mem_start = finish_node_interrupts(np, mem_start);

	/* Look for #address-cells and #size-cells properties. */
	ip = (int *) get_property(np, "#address-cells", 0);
	if (ip != NULL)
		naddrc = *ip;
	ip = (int *) get_property(np, "#size-cells", 0);
	if (ip != NULL)
		nsizec = *ip;

	/* the f50 sets the name to 'display' and 'compatible' to what we
	 * expect for the name -- Cort
	 */
	ifunc = NULL;
	if (!strcmp(np->name, "display"))
		np->name = get_property(np, "compatible", 0);

	if (!strcmp(np->name, "device-tree") || np->parent == NULL)
		ifunc = interpret_root_props;
	else if (np->type == 0)
		ifunc = NULL;
	else if (!strcmp(np->type, "pci") || !strcmp(np->type, "vci"))
		ifunc = interpret_pci_props;
	else if (!strcmp(np->type, "isa"))
		ifunc = interpret_isa_props;

	for (child = np->child; child != NULL; child = child->sibling)
		mem_start = finish_node(child, mem_start, ifunc,
					naddrc, nsizec);

	return mem_start;
}

/* This routine walks the interrupt tree for a given device node and gather 
 * all necessary informations according to the draft interrupt mapping
 * for CHRP. The current version was only tested on Apple "Core99" machines
 * and may not handle cascaded controllers correctly.
 */
__init
static unsigned long
finish_node_interrupts(struct device_node *np, unsigned long mem_start)
{
	/* Finish this node */
	unsigned int *isizep, *asizep, *interrupts, *map, *map_mask, *reg;
	phandle *parent, map_parent;
	struct device_node *node, *parent_node;
	int l, isize, ipsize, asize, map_size, regpsize;

	/* Currently, we don't look at all nodes with no "interrupts" property */

	interrupts = (unsigned int *)get_property(np, "interrupts", &l);
	if (interrupts == NULL)
		return mem_start;
	ipsize = l>>2;

	reg = (unsigned int *)get_property(np, "reg", &l);
	regpsize = l>>2;

	/* We assume default interrupt cell size is 1 (bugus ?) */
	isize = 1;
	node = np;
	
	do {
	    /* We adjust the cell size if the current parent contains an #interrupt-cells
	     * property */
	    isizep = (unsigned int *)get_property(node, "#interrupt-cells", &l);
	    if (isizep)
	    	isize = *isizep;

	    /* We don't do interrupt cascade (ISA) for now, we stop on the first 
	     * controller found
	     */
	    if (get_property(node, "interrupt-controller", &l)) {
	    	int i,j;

	    	np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = ipsize / isize;
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
		    np->intrs[i].line = openpic_to_irq(virt_irq_create_mapping(*interrupts++));
		    np->intrs[i].sense = 1;
		    if (isize > 1)
		        np->intrs[i].sense = *interrupts++;
		    for (j=2; j<isize; j++)
		    	interrupts++;
		}
		return mem_start;
	    }
	    /* We lookup for an interrupt-map. This code can only handle one interrupt
	     * per device in the map. We also don't handle #address-cells in the parent
	     * I skip the pci node itself here, may not be necessary but I don't like it's
	     * reg property.
	     */
	    if (np != node)
	        map = (unsigned int *)get_property(node, "interrupt-map", &l);
	     else
	     	map = NULL;
	    if (map && l) {
	    	int i, found, temp_isize, temp_asize;
	        map_size = l>>2;
	        map_mask = (unsigned int *)get_property(node, "interrupt-map-mask", &l);
	        asizep = (unsigned int *)get_property(node, "#address-cells", &l);
	        if (asizep && l == sizeof(unsigned int))
	            asize = *asizep;
	        else
	            asize = 0;
	        found = 0;
	        while (map_size>0 && !found) {
	            found = 1;
	            for (i=0; i<asize; i++) {
	            	unsigned int mask = map_mask ? map_mask[i] : 0xffffffff;
	            	if (!reg || (i>=regpsize) || ((mask & *map) != (mask & reg[i])))
	           	    found = 0;
	           	map++;
	           	map_size--;
	            }
	            for (i=0; i<isize; i++) {
	            	unsigned int mask = map_mask ? map_mask[i+asize] : 0xffffffff;
	            	if ((mask & *map) != (mask & interrupts[i]))
	            	    found = 0;
	            	map++;
	            	map_size--;
	            }
	            map_parent = *((phandle *)map);
	            map+=1; map_size-=1;
	            parent_node = find_phandle(map_parent);
	            temp_isize = isize;
		    temp_asize = 0;
	            if (parent_node) {
			isizep = (unsigned int *)get_property(parent_node, "#interrupt-cells", &l);
	    		if (isizep)
	    		    temp_isize = *isizep;
			asizep = (unsigned int *)get_property(parent_node, "#address-cells", &l);
			if (asizep && l == sizeof(unsigned int))
				temp_asize = *asizep;
	            }
	            if (!found) {
			map += temp_isize + temp_asize;
			map_size -= temp_isize + temp_asize;
	            }
	        }
	        if (found) {
		    /* Mapped to a new parent.  Use the reg and interrupts specified in
		     * the map as the new search parameters.  Then search from the parent.
		     */
	            node = parent_node;
		    reg = map;
		    regpsize = temp_asize;
		    interrupts = map + temp_asize;
		    ipsize = temp_isize;
		    continue;
	        }
	    }
	    /* We look for an explicit interrupt-parent.
	     */
	    parent = (phandle *)get_property(node, "interrupt-parent", &l);
	    if (parent && (l == sizeof(phandle)) &&
	    	(parent_node = find_phandle(*parent))) {
	    	node = parent_node;
	    	continue;
	    }
	    /* Default, get real parent */
	    node = node->parent;
	} while (node);

	return mem_start;
}

int
prom_n_addr_cells(struct device_node* np)
{
	int* ip;
	do {
		if (np->parent)
			np = np->parent;
		ip = (int *) get_property(np, "#address-cells", 0);
		if (ip != NULL)
			return *ip;
	} while (np->parent);
	/* No #address-cells property for the root node, default to 1 */
	return 1;
}

int
prom_n_size_cells(struct device_node* np)
{
	int* ip;
	do {
		if (np->parent)
			np = np->parent;
		ip = (int *) get_property(np, "#size-cells", 0);
		if (ip != NULL)
			return *ip;
	} while (np->parent);
	/* No #size-cells property for the root node, default to 1 */
	return 1;
}

static unsigned long __init
interpret_pci_props(struct device_node *np, unsigned long mem_start,
		    int naddrc, int nsizec)
{
	struct address_range *adr;
	struct pci_reg_property *pci_addrs;
	int i, l;

	pci_addrs = (struct pci_reg_property *)
		get_property(np, "assigned-addresses", &l);
	if (pci_addrs != 0 && l >= sizeof(struct pci_reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct pci_reg_property)) >= 0) {
			adr[i].space = pci_addrs[i].addr.a_hi;
			adr[i].address = pci_addrs[i].addr.a_lo;
			adr[i].size = pci_addrs[i].size_lo;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}
	return mem_start;
}

static unsigned long __init
interpret_isa_props(struct device_node *np, unsigned long mem_start,
		    int naddrc, int nsizec)
{
	struct isa_reg_property *rp;
	struct address_range *adr;
	int i, l;

	rp = (struct isa_reg_property *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct isa_reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct reg_property)) >= 0) {
			adr[i].space = rp[i].space;
			adr[i].address = rp[i].address
				+ (adr[i].space? 0: _ISA_MEM_BASE);
			adr[i].size = rp[i].size;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	return mem_start;
}

static unsigned long __init
interpret_root_props(struct device_node *np, unsigned long mem_start,
		     int naddrc, int nsizec)
{
	struct address_range *adr;
	int i, l;
	unsigned int *rp;
	int rpsize = (naddrc + nsizec) * sizeof(unsigned int);

	rp = (unsigned int *) get_property(np, "reg", &l);
	if (rp != 0 && l >= rpsize) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= rpsize) >= 0) {
			adr[i].space = 0;
			adr[i].address = rp[naddrc - 1];
			adr[i].size = rp[naddrc + nsizec - 1];
			++i;
			rp += naddrc + nsizec;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	return mem_start;
}

/*
 * Work out the sense (active-low level / active-high edge)
 * of each interrupt from the device tree.
 */
void __init
prom_get_irq_senses(unsigned char *senses, int off, int max)
{
	struct device_node *np;
	int i, j;

	/* default to level-triggered */
	memset(senses, 1, max - off);

	for (np = allnodes; np != 0; np = np->allnext) {
		for (j = 0; j < np->n_intrs; j++) {
			i = np->intrs[j].line;
			if (i >= off && i < max)
				senses[i-off] = np->intrs[j].sense;
		}
	}
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
 * Returns all nodes linked together
 */
struct device_node *
find_all_nodes(void)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		*prevp = np;
		prevp = &np->next;
	}
	*prevp = 0;
	return head;
}

/* Checks if the given "compat" string matches one of the strings in
 * the device's "compatible" property
 */
int
device_is_compatible(struct device_node *device, const char *compat)
{
	const char* cp;
	int cplen, l;

	cp = (char *) get_property(device, "compatible", &cplen);
	if (cp == NULL)
		return 0;
	while (cplen > 0) {
		if (strncasecmp(cp, compat, strlen(compat)) == 0)
			return 1;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
	}

	return 0;
}


/*
 * Indicates whether the root node has a given value in its
 * compatible property.
 */
int
machine_is_compatible(const char *compat)
{
	struct device_node *root;
	
	root = find_path_device("/");
	if (root == 0)
		return 0;
	return device_is_compatible(root, compat);
}

/*
 * Construct and return a list of the device_nodes with a given type
 * and compatible property.
 */
struct device_node *
find_compatible_devices(const char *type, const char *compat)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (type != NULL
		    && !(np->type != 0 && strcasecmp(np->type, type) == 0))
			continue;
		if (device_is_compatible(np, compat)) {
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
static struct device_node * __init
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

/*
 * Add a property to a node
 */
void
prom_add_property(struct device_node* np, struct property* prop)
{
	struct property **next = &np->properties;

	prop->next = NULL;	
	while (*next)
		next = &(*next)->next;
	*next = prop;
}

#if 0
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
#endif


void __init
abort()
{
#ifdef CONFIG_XMON
	xmon(NULL);
#endif
	for (;;)
		prom_exit();
}


/* Verify bi_recs are good */
static struct bi_record *
prom_bi_rec_verify(struct bi_record *bi_recs)
{
	struct bi_record *first, *last;

	if ( bi_recs == NULL || bi_recs->tag != BI_FIRST )
		return NULL;

	last = (struct bi_record *)bi_recs->data[0];
	if ( last == NULL || last->tag != BI_LAST )
		return NULL;

	first = (struct bi_record *)last->data[0];
	if ( first == NULL || first != bi_recs )
		return NULL;

	return bi_recs;
}

static unsigned long
prom_bi_rec_reserve(unsigned long mem)
{
	unsigned long offset = reloc_offset();
	struct prom_t *_prom = PTRRELOC(&prom);
	struct bi_record *rec;

	if ( _prom->bi_recs != NULL) {

		for ( rec=_prom->bi_recs;
		      rec->tag != BI_LAST;
		      rec=bi_rec_next(rec) ) {
			switch (rec->tag) {
#ifdef CONFIG_BLK_DEV_INITRD
			case BI_INITRD:
				lmb_reserve(rec->data[0], rec->data[1]);
				break;
#endif /* CONFIG_BLK_DEV_INITRD */
			}
		}
		/* The next use of this field will be after relocation
	 	 * is enabled, so convert this physical address into a
	 	 * virtual address.
	 	 */
		_prom->bi_recs = PTRUNRELOC(_prom->bi_recs);
	}

	return mem;
}

