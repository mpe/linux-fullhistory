/*
 *	linux/arch/alpha/kernel/core_pyxis.c
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
 *
 * Code common to all PYXIS core logic chips.
 */

#include <linux/types.h>
#include <linux/kernel.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_pyxis.h>
#undef __EXTERN_INLINE

#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/ptrace.h>
#include <asm/system.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"


/* NOTE: Herein are back-to-back mb instructions.  They are magic.
   One plausible explanation is that the I/O controller does not properly
   handle the system transaction.  Another involves timing.  Ho hum.  */

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG_CNF(args)	printk args
#else
# define DBG_CNF(args)
#endif


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the PYXIS_HAXR2 register
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
	u8 bus = dev->bus->number;
	u8 device_fn = dev->devfn;

	*type1 = (bus == 0) ? 0 : 1;
	*pci_addr = (bus << 16) | (device_fn << 8) | (where);

	DBG_CNF(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
		 " returning address 0x%p\n"
		 bus, device_fn, where, *pci_addr));

	return 0;
}

static unsigned int
conf_read(unsigned long addr, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, value, temp;
	unsigned int pyxis_cfg = 0;

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = stat0; mb();
	temp = *(vuip)PYXIS_ERR;  /* re-read to force write */

	/* If Type1 access, must set PYXIS CFG.  */
	if (type1) {
		pyxis_cfg = *(vuip)PYXIS_CFG;
		*(vuip)PYXIS_CFG = (pyxis_cfg & ~3L) | 1; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
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
		*(vuip)PYXIS_CFG = pyxis_cfg & ~3L; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
	}

	__restore_flags(flags);

	DBG_CNF(("conf_read(addr=0x%lx, type1=%d) = %#x\n",
		 addr, type1, value));

	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, temp;
	unsigned int pyxis_cfg = 0;

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = stat0; mb();
	temp = *(vuip)PYXIS_ERR;  /* re-read to force write */

	/* If Type1 access, must set PYXIS CFG.  */
	if (type1) {
		pyxis_cfg = *(vuip)PYXIS_CFG;
		*(vuip)PYXIS_CFG = (pyxis_cfg & ~3L) | 1; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
	}

	mb();
	draina();
	mcheck_expected(0) = 1;
	mcheck_taken(0) = 0;
	mb();

	/* Access configuration space.  */
	*(vuip)addr = value;
	mb();
	temp = *(vuip)addr; /* read back to force the write */
	mcheck_expected(0) = 0;
	mb();

	/* If Type1 access, must reset IOC CFG so normal IO space ops work.  */
	if (type1) {
		*(vuip)PYXIS_CFG = pyxis_cfg & ~3L; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
	}

	__restore_flags(flags);

	DBG_CNF(("conf_write(addr=%#lx, value=%#x, type1=%d)\n",
		 addr, value, type1));
}

static int
pyxis_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x00 + PYXIS_CONF;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int
pyxis_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x08 + PYXIS_CONF;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int
pyxis_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x18 + PYXIS_CONF;
	*value = conf_read(addr, type1);
	return PCIBIOS_SUCCESSFUL;
}

static int
pyxis_write_config(struct pci_dev *dev, int where, u32 value, long mask)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + mask + PYXIS_CONF;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

static int 
pyxis_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	return pyxis_write_config(dev, where, value, 0x00);
}

static int 
pyxis_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	return pyxis_write_config(dev, where, value, 0x08);
}

static int 
pyxis_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	return pyxis_write_config(dev, where, value, 0x18);
}

struct pci_ops pyxis_pci_ops = 
{
	read_byte:	pyxis_read_config_byte,
	read_word:	pyxis_read_config_word,
	read_dword:	pyxis_read_config_dword,
	write_byte:	pyxis_write_config_byte,
	write_word:	pyxis_write_config_word,
	write_dword:	pyxis_write_config_dword
};

/* Note mask bit is true for ENABLED irqs.  */
static unsigned long cached_irq_mask;

static inline void
pyxis_update_irq_hw(unsigned long mask)
{
	*(vulp)PYXIS_INT_MASK = mask;
	mb();
	*(vulp)PYXIS_INT_MASK;
}

static inline void
pyxis_enable_irq(unsigned int irq)
{
	pyxis_update_irq_hw(cached_irq_mask |= 1UL << (irq - 16));
}

static void
pyxis_disable_irq(unsigned int irq)
{
	pyxis_update_irq_hw(cached_irq_mask &= ~(1UL << (irq - 16)));
}

static unsigned int
pyxis_startup_irq(unsigned int irq)
{
	pyxis_enable_irq(irq);
	return 0;
}

static void
pyxis_end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		pyxis_enable_irq(irq);
}

static void
pyxis_mask_and_ack_irq(unsigned int irq)
{
	unsigned long bit = 1UL << (irq - 16);
	unsigned long mask = cached_irq_mask &= ~bit;

	/* Disable the interrupt.  */
	*(vulp)PYXIS_INT_MASK = mask;
	wmb();
	/* Ack PYXIS PCI interrupt.  */
	*(vulp)PYXIS_INT_REQ = bit;
	mb();
	/* Re-read to force both writes.  */
	*(vulp)PYXIS_INT_MASK;
}

static struct hw_interrupt_type pyxis_irq_type = {
	typename:	"PYXIS",
	startup:	pyxis_startup_irq,
	shutdown:	pyxis_disable_irq,
	enable:		pyxis_enable_irq,
	disable:	pyxis_disable_irq,
	ack:		pyxis_mask_and_ack_irq,
	end:		pyxis_end_irq,
};

void 
pyxis_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary register of PYXIS */
	pld = *(vulp)PYXIS_INT_REQ;
	pld &= cached_irq_mask;

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i == 7)
			isa_device_interrupt(vector, regs);
		else
			handle_irq(16+i, regs);
	}
}

void __init
init_pyxis_irqs(unsigned long ignore_mask)
{
	long i;

	*(vulp)PYXIS_INT_MASK = 0;		/* disable all */
	*(vulp)PYXIS_INT_REQ  = -1;		/* flush all */
	mb();

	/* Send -INTA pulses to clear any pending interrupts ...*/
	*(vuip) PYXIS_IACK_SC;

	for (i = 16; i < 48; ++i) {
		if ((ignore_mask >> i) & 1)
			continue;
		irq_desc[i].status = IRQ_DISABLED | IRQ_LEVEL;
		irq_desc[i].handler = &pyxis_irq_type;
	}

	setup_irq(16+7, &isa_cascade_irqaction);
}

void
pyxis_pci_tbi(struct pci_controler *hose, dma_addr_t start, dma_addr_t end)
{
	wmb();
	*(vip)PYXIS_TBIA = 3;	/* Flush all locked and unlocked.  */
	mb();
}

/*
 * Pass 1 and 2 have a broken scatter-gather tlb -- it cannot be invalidated.
 * To work around this problem, we allocate mappings, and put the chip into
 * DMA loopback mode to read a garbage page.  This works by causing TLB
 * misses, causing old entries to be purged to make room for the new entries
 * coming in for the garbage page.
 * 
 * Thanks to NetBSD sources for pointing out this bug.  What a pain.
 */

static unsigned long broken_tbi_addr;

#define BROKEN_TBI_READS 12

static void
pyxis_broken_pci_tbi(struct pci_controler *hose,
		     dma_addr_t start, dma_addr_t end)
{
	unsigned long flags;
	unsigned long bus_addr;
	unsigned int ctrl;
	long i;

	__save_and_cli(flags);

	/* Put the chip into PCI loopback mode.  */
	mb();
	ctrl = *(vuip)PYXIS_CTRL;
	*(vuip)PYXIS_CTRL = ctrl | 4;
	mb();

	/* Read from PCI dense memory space at TBI_ADDR, skipping 64k
	   on each read.  This forces SG TLB misses.  It appears that
	   the TLB entries are "not quite LRU", meaning that we need
	   to read more times than there are actual tags.  */

	bus_addr = broken_tbi_addr;
	for (i = 0; i < BROKEN_TBI_READS; ++i, bus_addr += 64*1024)
		pyxis_readl(bus_addr);

	/* Restore normal PCI operation.  */
	mb();
	*(vuip)PYXIS_CTRL = ctrl;
	mb();

	__restore_flags(flags);
}

static void __init
pyxis_enable_broken_tbi(struct pci_iommu_arena *arena)
{
	void *page;
	unsigned long *ppte, ofs, pte;
	long i, npages;

	page = alloc_bootmem_pages(PAGE_SIZE);
	pte = (virt_to_phys(page) >> (PAGE_SHIFT - 1)) | 1;
	npages = (BROKEN_TBI_READS + 1) * 64*1024 / PAGE_SIZE;

	ofs = iommu_arena_alloc(arena, npages);
	ppte = arena->ptes + ofs;
	for (i = 0; i < npages; ++i)
		ppte[i] = pte;

	broken_tbi_addr = pyxis_ioremap(arena->dma_base + ofs*PAGE_SIZE);
	alpha_mv.mv_pci_tbi = pyxis_broken_pci_tbi;

	printk("PYXIS: Enabling broken tbia workaround.\n");
}

void __init
pyxis_init_arch(void)
{
	struct pci_controler *hose;
	unsigned int temp;

#if 0
	printk("pyxis_init: PYXIS_ERR_MASK 0x%x\n", *(vuip)PYXIS_ERR_MASK);
	printk("pyxis_init: PYXIS_ERR 0x%x\n", *(vuip)PYXIS_ERR);
	printk("pyxis_init: PYXIS_INT_REQ 0x%lx\n", *(vulp)PYXIS_INT_REQ);
	printk("pyxis_init: PYXIS_INT_MASK 0x%lx\n", *(vulp)PYXIS_INT_MASK);
	printk("pyxis_init: PYXIS_INT_ROUTE 0x%lx\n", *(vulp)PYXIS_INT_ROUTE);
	printk("pyxis_init: PYXIS_INT_HILO 0x%lx\n", *(vulp)PYXIS_INT_HILO);
	printk("pyxis_init: PYXIS_INT_CNFG 0x%x\n", *(vuip)PYXIS_INT_CNFG);
	printk("pyxis_init: PYXIS_RT_COUNT 0x%lx\n", *(vulp)PYXIS_RT_COUNT);
#endif

	/* 
	 * Set up error reporting. Make sure CPU_PE is OFF in the mask.
	 */
	temp = *(vuip)PYXIS_ERR_MASK;
	temp &= ~4;   
	*(vuip)PYXIS_ERR_MASK = temp;
	mb();
	*(vuip)PYXIS_ERR_MASK;	/* re-read to force write */

	temp = *(vuip)PYXIS_ERR;
	temp |= 0x180;		/* master/target abort */
	*(vuip)PYXIS_ERR = temp;
	mb();
	*(vuip)PYXIS_ERR;	/* re-read to force write */

 	/*
	 * Create our single hose.
	 */

	hose = alloc_pci_controler();
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->config_space = PYXIS_CONF;
	hose->index = 0;

	/*
	 * Set up the PCI to main memory translation windows.
	 *
	 * Window 0 is scatter-gather 8MB at 8MB (for isa)
	 * Window 1 is scatter-gather 128MB at 3GB
	 * Window 2 is direct access 1GB at 1GB
	 * Window 3 is direct access 1GB at 2GB
	 * ??? We ought to scale window 1 with memory.
	 *
	 * We must actually use 2 windows to direct-map the 2GB space,
	 * because of an idiot-syncrasy of the CYPRESS chip.  It may
	 * respond to a PCI bus address in the last 1MB of the 4GB
	 * address range.
	 */

	/* NetBSD hints that page tables must be aligned to 32K due
	   to a hardware bug.  No description of what models affected.  */
	hose->sg_isa = iommu_arena_new(0x00800000, 0x00800000, 32768);
	hose->sg_pci = iommu_arena_new(0xc0000000, 0x08000000, 32768);
	__direct_map_base = 0x40000000;
	__direct_map_size = 0x80000000;

	*(vuip)PYXIS_W0_BASE = hose->sg_isa->dma_base | 3;
	*(vuip)PYXIS_W0_MASK = (hose->sg_isa->size - 1) & 0xfff00000;
	*(vuip)PYXIS_T0_BASE = virt_to_phys(hose->sg_isa->ptes) >> 2;

	*(vuip)PYXIS_W1_BASE = hose->sg_pci->dma_base | 3;
	*(vuip)PYXIS_W1_MASK = (hose->sg_pci->size - 1) & 0xfff00000;
	*(vuip)PYXIS_T1_BASE = virt_to_phys(hose->sg_pci->ptes) >> 2;

	*(vuip)PYXIS_W2_BASE = 0x40000000 | 1;
	*(vuip)PYXIS_W2_MASK = (0x40000000 - 1) & 0xfff00000;
	*(vuip)PYXIS_T2_BASE = 0;

	*(vuip)PYXIS_W3_BASE = 0x80000000 | 1;
	*(vuip)PYXIS_W3_MASK = (0x40000000 - 1) & 0xfff00000;
	*(vuip)PYXIS_T3_BASE = 0;

	/* Pass 1 and 2 (ie revision <= 1) have a broken TBIA.  See the
	   complete description next to pyxis_broken_pci_tbi for details.  */
	if ((*(vuip)PYXIS_REV & 0xff) <= 1)
		pyxis_enable_broken_tbi(hose->sg_pci);

	alpha_mv.mv_pci_tbi(hose, 0, -1);
alpha_mv.mv_pci_tbi = 0;

	/*
	 * Next, clear the PYXIS_CFG register, which gets used
	 *  for PCI Config Space accesses. That is the way
	 *  we want to use it, and we do not want to depend on
	 *  what ARC or SRM might have left behind...
	 */
	temp = *(vuip)PYXIS_CFG;
	if (temp != 0) {
		*(vuip)PYXIS_CFG = 0;
		mb();
		*(vuip)PYXIS_CFG; /* re-read to force write */
	}
 
	/* Zero the HAE.  */
	*(vuip)PYXIS_HAE_MEM = 0U; mb();
	*(vuip)PYXIS_HAE_MEM;	/* re-read to force write */
	*(vuip)PYXIS_HAE_IO = 0; mb();
	*(vuip)PYXIS_HAE_IO;	/* re-read to force write */

	/*
	 * Finally, check that the PYXIS_CTRL1 has IOA_BEN set for
	 * enabling byte/word PCI bus space(s) access.
	 */
	temp = *(vuip) PYXIS_CTRL1;
	if (!(temp & 1)) {
		*(vuip)PYXIS_CTRL1 = temp | 1;
		mb();
		*(vuip)PYXIS_CTRL1; /* re-read */
	}
}

static inline void
pyxis_pci_clr_err(void)
{
	unsigned int tmp;

	tmp = *(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = tmp;
	mb();
	*(vuip)PYXIS_ERR;  /* re-read to force write */
}

void
pyxis_machine_check(unsigned long vector, unsigned long la_ptr,
		    struct pt_regs * regs)
{
	int expected;

	/* Clear the error before reporting anything.  */
	mb();
	mb();  /* magic */
	draina();
	pyxis_pci_clr_err();
	wrmces(0x7);
	mb();

	expected = mcheck_expected(0);
	if (!expected && vector == 0x660) {
		struct el_common *com;
		struct el_common_EV5_uncorrectable_mcheck *ev5;
		struct el_PYXIS_sysdata_mcheck *pyxis;

		com = (void *)la_ptr;
		ev5 = (void *)(la_ptr + com->proc_offset);
		pyxis = (void *)(la_ptr + com->sys_offset);

		if (com->code == 0x202) {
			printk(KERN_CRIT "PYXIS PCI machine check: err0=%08x "
			       "err1=%08x err2=%08x\n",
			       (int) pyxis->pci_err0, (int) pyxis->pci_err1,
			       (int) pyxis->pci_err2);
			expected = 1;
		}
	}
	process_mcheck_info(vector, la_ptr, regs, "PYXIS", expected);
}
