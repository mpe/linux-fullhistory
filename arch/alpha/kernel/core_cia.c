/*
 *	linux/arch/alpha/kernel/core_cia.c
 *
 * Written by David A Rusling (david.rusling@reo.mts.dec.com).
 * December 1995.
 *
 *	Copyright (C) 1995  David A Rusling
 *	Copyright (C) 1997, 1998  Jay Estabrook
 *	Copyright (C) 1998, 1999  Richard Henderson
 *
 * Code common to all CIA core logic chips.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/ptrace.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_cia.h>
#undef __EXTERN_INLINE

#include "proto.h"
#include "pci_impl.h"


/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CONFIG 0
#define DEBUG_DUMP_REGS 0

#if DEBUG_CONFIG
# define DBGC(args)	printk args
#else
# define DBGC(args)
#endif

#define vuip	volatile unsigned int  *

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the CIA_HAXR2 register
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | |D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|0|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:11	Device select bit.
 * 	10:8	Function number
 * 	 7:2	Register number
 *
 * Type 1:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | |B|B|B|B|B|B|B|B|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|1|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:24	reserved
 *	23:16	bus number (8 bits = 128 possible buses)
 *	15:11	Device number (5 bits)
 *	10:8	function number
 *	 7:2	register number
 *  
 * Notes:
 *	The function number selects which function of a multi-function device 
 *	(e.g., SCSI and Ethernet).
 * 
 *	The register selects a DWORD (32 bit) register offset.  Hence it
 *	doesn't get shifted by 2 bits as we want to "drop" the bottom two
 *	bits.
 */

static int
mk_conf_addr(struct pci_dev *dev, int where, unsigned long *pci_addr,
	     unsigned char *type1)
{
	unsigned long addr;
	u8 bus = dev->bus->number;
	u8 device_fn = dev->devfn;

	DBGC(("mk_conf_addr(bus=%d, device_fn=0x%x, where=0x%x, "
	      "pci_addr=0x%p, type1=0x%p)\n",
	      bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		int device = device_fn >> 3;

		/* Type 0 configuration cycle.  */

		if (device > 20) {
			DBGC(("mk_conf_addr: device (%d) > 20, returning -1\n",
			      device));
			return -1;
		}

		*type1 = 0;
		addr = (device_fn << 8) | (where);
	} else {
		/* Type 1 configuration cycle.  */
		*type1 = 1;
		addr = (bus << 16) | (device_fn << 8) | (where);
	}
	*pci_addr = addr;
	DBGC(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static unsigned int
conf_read(unsigned long addr, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, value;
	unsigned int cia_cfg = 0;

	value = 0xffffffffU;
	mb();

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	DBGC(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)CIA_IOC_CIA_ERR;
	*(vuip)CIA_IOC_CIA_ERR = stat0;
	mb();
	DBGC(("conf_read: CIA ERR was 0x%x\n", stat0));

	/* If Type1 access, must set CIA CFG. */
	if (type1) {
		cia_cfg = *(vuip)CIA_IOC_CFG;
		*(vuip)CIA_IOC_CFG = cia_cfg | 1;
		mb();
		DBGC(("conf_read: TYPE1 access\n"));
	}

	mb();
	draina();
	mcheck_expected(0) = 1;
	mcheck_taken(0) = 0;
	mb();

	/* Access configuration space.  */
	value = *(vuip)addr;
	mb();
	mb();  /* magic */
	if (mcheck_taken(0)) {
		mcheck_taken(0) = 0;
		value = 0xffffffffU;
		mb();
	}
	mcheck_expected(0) = 0;
	mb();

	/* If Type1 access, must reset IOC CFG so normal IO space ops work.  */
	if (type1) {
		*(vuip)CIA_IOC_CFG = cia_cfg & ~1;
		mb();
	}

	DBGC(("conf_read(): finished\n"));

	__restore_flags(flags);
	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0;
	unsigned int cia_cfg = 0;

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)CIA_IOC_CIA_ERR;
	*(vuip)CIA_IOC_CIA_ERR = stat0;
	mb();
	DBGC(("conf_write: CIA ERR was 0x%x\n", stat0));

	/* If Type1 access, must set CIA CFG.  */
	if (type1) {
		cia_cfg = *(vuip)CIA_IOC_CFG;
		*(vuip)CIA_IOC_CFG = cia_cfg | 1;
		mb();
		DBGC(("conf_write: TYPE1 access\n"));
	}

	draina();
	mcheck_expected(0) = 1;
	mb();

	/* Access configuration space.  */
	*(vuip)addr = value;
	mb();
	mb();  /* magic */

	mcheck_expected(0) = 0;
	mb();

	/* If Type1 access, must reset IOC CFG so normal IO space ops work.  */
	if (type1) {
		*(vuip)CIA_IOC_CFG = cia_cfg & ~1;
		mb();
	}

	DBGC(("conf_write(): finished\n"));
	__restore_flags(flags);
}

static int
cia_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x00 + CIA_CONF;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int 
cia_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x08 + CIA_CONF;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int 
cia_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x18 + CIA_CONF;
	*value = conf_read(addr, type1);
	return PCIBIOS_SUCCESSFUL;
}

static int 
cia_write_config(struct pci_dev *dev, int where, u32 value, long mask)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + mask + CIA_CONF;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

static int
cia_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	return cia_write_config(dev, where, value, 0x00);
}

static int 
cia_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	return cia_write_config(dev, where, value, 0x08);
}

static int 
cia_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	return cia_write_config(dev, where, value, 0x18);
}

struct pci_ops cia_pci_ops = 
{
	read_byte:	cia_read_config_byte,
	read_word:	cia_read_config_word,
	read_dword:	cia_read_config_dword,
	write_byte:	cia_write_config_byte,
	write_word:	cia_write_config_word,
	write_dword:	cia_write_config_dword
};

void
cia_pci_tbi(struct pci_controler *hose, dma_addr_t start, dma_addr_t end)
{
	wmb();
	*(vip)CIA_IOC_PCI_TBIA = 3;	/* Flush all locked and unlocked.  */
	mb();
}

void __init
cia_init_arch(void)
{
	struct pci_controler *hose;
	struct resource *hae_mem;
	unsigned int temp;

#if DEBUG_DUMP_REGS
	temp = *(vuip)CIA_IOC_CIA_REV; mb();
	printk("cia_init: CIA_REV was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_PCI_LAT; mb();
	printk("cia_init: CIA_PCI_LAT was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_CIA_CTRL; mb();
	printk("cia_init: CIA_CTRL was 0x%x\n", temp);
	temp = *(vuip)0xfffffc8740000140UL; mb();
	printk("cia_init: CIA_CTRL1 was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_HAE_MEM; mb();
	printk("cia_init: CIA_HAE_MEM was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_HAE_IO; mb();
	printk("cia_init: CIA_HAE_IO was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_CFG; mb();
	printk("cia_init: CIA_CFG was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_CACK_EN; mb();
	printk("cia_init: CIA_CACK_EN was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_CFG; mb();
	printk("cia_init: CIA_CFG was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_CIA_DIAG; mb();
	printk("cia_init: CIA_DIAG was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_DIAG_CHECK; mb();
	printk("cia_init: CIA_DIAG_CHECK was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_PERF_MONITOR; mb();
	printk("cia_init: CIA_PERF_MONITOR was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_PERF_CONTROL; mb();
	printk("cia_init: CIA_PERF_CONTROL was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_CIA_ERR; mb();
	printk("cia_init: CIA_ERR was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_CIA_STAT; mb();
	printk("cia_init: CIA_STAT was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_MCR; mb();
	printk("cia_init: CIA_MCR was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_CIA_CTRL; mb();
	printk("cia_init: CIA_CTRL was 0x%x\n", temp);
	temp = *(vuip)CIA_IOC_ERR_MASK; mb();
	printk("cia_init: CIA_ERR_MASK was 0x%x\n", temp);
	temp = *((vuip)CIA_IOC_PCI_W0_BASE); mb();
	printk("cia_init: W0_BASE was 0x%x\n", temp);
	temp = *((vuip)CIA_IOC_PCI_W1_BASE); mb();
	printk("cia_init: W1_BASE was 0x%x\n", temp);
	temp = *((vuip)CIA_IOC_PCI_W2_BASE); mb();
	printk("cia_init: W2_BASE was 0x%x\n", temp);
	temp = *((vuip)CIA_IOC_PCI_W3_BASE); mb();
	printk("cia_init: W3_BASE was 0x%x\n", temp);
#endif /* DEBUG_DUMP_REGS */

	/*
	 * Create our single hose.
	 */

	pci_isa_hose = hose = alloc_pci_controler();
	hae_mem = alloc_resource();

	hose->io_space = &ioport_resource;
	hose->mem_space = hae_mem;
	hose->config_space = CIA_CONF;
	hose->index = 0;

	hae_mem->start = 0;
	hae_mem->end = CIA_MEM_R1_MASK;
	hae_mem->name = pci_hae0_name;
	hae_mem->flags = IORESOURCE_MEM;

	if (request_resource(&iomem_resource, hae_mem) < 0)
		printk(KERN_ERR "Failed to request HAE_MEM\n");

	/*
	 * Set up the PCI to main memory translation windows.
	 *
	 * Window 0 is scatter-gather 8MB at 8MB (for isa)
	 * Window 1 is scatter-gather 128MB at 1GB
	 * Window 2 is direct access 2GB at 2GB
	 * ??? We ought to scale window 1 with memory.
	 */

	/* NetBSD hints that page tables must be aligned to 32K due
	   to a hardware bug.  No description of what models affected.  */
	hose->sg_isa = iommu_arena_new(0x00800000, 0x00800000, 32768);
	hose->sg_pci = iommu_arena_new(0x40000000, 0x08000000, 32768);
	__direct_map_base = 0x80000000;
	__direct_map_size = 0x80000000;

	*(vuip)CIA_IOC_PCI_W0_BASE = hose->sg_isa->dma_base | 3;
	*(vuip)CIA_IOC_PCI_W0_MASK = (hose->sg_isa->size - 1) & 0xfff00000;
	*(vuip)CIA_IOC_PCI_T0_BASE = virt_to_phys(hose->sg_isa->ptes) >> 2;

	*(vuip)CIA_IOC_PCI_W1_BASE = hose->sg_pci->dma_base | 3;
	*(vuip)CIA_IOC_PCI_W1_MASK = (hose->sg_pci->size - 1) & 0xfff00000;
	*(vuip)CIA_IOC_PCI_T1_BASE = virt_to_phys(hose->sg_pci->ptes) >> 2;

	*(vuip)CIA_IOC_PCI_W2_BASE = __direct_map_base | 1;
	*(vuip)CIA_IOC_PCI_W2_MASK = (__direct_map_size - 1) & 0xfff00000;
	*(vuip)CIA_IOC_PCI_T2_BASE = 0;

	*(vuip)CIA_IOC_PCI_W3_BASE = 0;

	cia_pci_tbi(hose, 0, -1);

	/* 
	 * Set up error reporting.
	 */
	temp = *(vuip)CIA_IOC_CIA_ERR;
	temp |= 0x180;   /* master, target abort */
	*(vuip)CIA_IOC_CIA_ERR = temp;

	temp = *(vuip)CIA_IOC_CIA_CTRL;
	temp |= 0x400;	/* turn on FILL_ERR to get mchecks */
	*(vuip)CIA_IOC_CIA_CTRL = temp;

	/*
	 * Next, clear the CIA_CFG register, which gets used
	 * for PCI Config Space accesses. That is the way
	 * we want to use it, and we do not want to depend on
	 * what ARC or SRM might have left behind...
	 */
	*(vuip)CIA_IOC_CFG = 0;
 
	/*
	 * Zero the HAEs. 
	 */
	*(vuip)CIA_IOC_HAE_MEM = 0;
	*(vuip)CIA_IOC_HAE_IO = 0;
	mb();
}

static inline void
cia_pci_clr_err(void)
{
	unsigned int jd;

	jd = *(vuip)CIA_IOC_CIA_ERR;
	*(vuip)CIA_IOC_CIA_ERR = jd;
	mb();
	*(vuip)CIA_IOC_CIA_ERR;		/* re-read to force write.  */
}

void
cia_machine_check(unsigned long vector, unsigned long la_ptr,
		  struct pt_regs * regs)
{
	int expected;

	/* Clear the error before any reporting.  */
	mb();
	mb();  /* magic */
	draina();
	cia_pci_clr_err();
	wrmces(rdmces());	/* reset machine check pending flag.  */
	mb();

	expected = mcheck_expected(0);
	if (!expected && vector == 0x660) {
		struct el_common *com;
		struct el_common_EV5_uncorrectable_mcheck *ev5;
		struct el_CIA_sysdata_mcheck *cia;

		com = (void *)la_ptr;
		ev5 = (void *)(la_ptr + com->proc_offset);
		cia = (void *)(la_ptr + com->sys_offset);

		if (com->code == 0x202) {
			printk(KERN_CRIT "CIA PCI machine check: err0=%08x "
			       "err1=%08x err2=%08x\n",
			       (int) cia->pci_err0, (int) cia->pci_err1,
			       (int) cia->pci_err2);
			expected = 1;
		}
	}
	process_mcheck_info(vector, la_ptr, regs, "CIA", expected);
}
