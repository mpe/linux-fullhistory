/*
 *	linux/arch/alpha/kernel/core_pyxis.c
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
 *
 * Code common to all PYXIS core logic chips.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pci.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_pyxis.h>
#undef __EXTERN_INLINE

#include "proto.h"
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

void __init
pyxis_init_arch(unsigned long *mem_start, unsigned long *mem_end)
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
	*(vuip)PYXIS_ERR_MASK = temp; mb();
	temp = *(vuip)PYXIS_ERR_MASK; /* re-read to force write */

	temp = *(vuip)PYXIS_ERR ;
	temp |= 0x180;   /* master/target abort */
	*(vuip)PYXIS_ERR = temp; mb();
	temp = *(vuip)PYXIS_ERR; /* re-read to force write */

	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, windows 2 and 3 are disabled.  In the future, we may
	 * want to use them to do scatter/gather DMA. 
	 *
	 * Window 0 goes at 2 GB and is 1 GB large.
	 * Window 1 goes at 3 GB and is 1 GB large.
	 */

	*(vuip)PYXIS_W0_BASE = PYXIS_DMA_WIN0_BASE_DEFAULT | 1U;
	*(vuip)PYXIS_W0_MASK = (PYXIS_DMA_WIN0_SIZE_DEFAULT - 1) & 0xfff00000U;
	*(vuip)PYXIS_T0_BASE = PYXIS_DMA_WIN0_TRAN_DEFAULT >> 2;

	*(vuip)PYXIS_W1_BASE = PYXIS_DMA_WIN1_BASE_DEFAULT | 1U;
	*(vuip)PYXIS_W1_MASK = (PYXIS_DMA_WIN1_SIZE_DEFAULT - 1) & 0xfff00000U;
	*(vuip)PYXIS_T1_BASE = PYXIS_DMA_WIN1_TRAN_DEFAULT >> 2;

	*(vuip)PYXIS_W2_BASE = 0x0;
	*(vuip)PYXIS_W3_BASE = 0x0;
	mb();

	/*
         * Next, clear the PYXIS_CFG register, which gets used
	 *  for PCI Config Space accesses. That is the way
	 *  we want to use it, and we do not want to depend on
	 *  what ARC or SRM might have left behind...
	 */
	{
		unsigned int pyxis_cfg, temp;
		pyxis_cfg = *(vuip)PYXIS_CFG; mb();
		if (pyxis_cfg != 0) {
#if 1
			printk("PYXIS_init: CFG was 0x%x\n", pyxis_cfg);
#endif
			*(vuip)PYXIS_CFG = 0; mb();
			temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
		}
	}
 
	/* Zero the HAE.  */
	*(vuip)PYXIS_HAE_MEM = 0U; mb();
	*(vuip)PYXIS_HAE_MEM; /* re-read to force write */
	*(vuip)PYXIS_HAE_IO = 0; mb();
	*(vuip)PYXIS_HAE_IO;  /* re-read to force write */

	/*
	 * Finally, check that the PYXIS_CTRL1 has IOA_BEN set for
	 * enabling byte/word PCI bus space(s) access.
	 */
	{
		unsigned int ctrl1;
		ctrl1 = *(vuip) PYXIS_CTRL1;
		if (!(ctrl1 & 1)) {
#if 1
			printk("PYXIS_init: enabling byte/word PCI space\n");
#endif
			*(vuip) PYXIS_CTRL1 = ctrl1 | 1; mb();
			ctrl1 = *(vuip)PYXIS_CTRL1;  /* re-read */
		}
	}

	/*
	 * Create our single hose.
	 */

	hose = alloc_pci_controler(mem_start);
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->config_space = PYXIS_CONF;
	hose->index = 0;
}

static inline void
pyxis_pci_clr_err(void)
{
	*(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = 0x0180;
	mb();
	*(vuip)PYXIS_ERR;  /* re-read to force write */
}

void
pyxis_machine_check(unsigned long vector, unsigned long la_ptr,
		    struct pt_regs * regs)
{
	/* Clear the error before reporting anything.  */
	mb();
	mb();  /* magic */
	draina();
	pyxis_pci_clr_err();
	wrmces(0x7);
	mb();

	process_mcheck_info(vector, la_ptr, regs, "PYXIS", mcheck_expected(0));
}
