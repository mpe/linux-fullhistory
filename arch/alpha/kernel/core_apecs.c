/*
 *	linux/arch/alpha/kernel/core_apecs.c
 *
 * Rewritten for Apecs from the lca.c from:
 *
 * Written by David Mosberger (davidm@cs.arizona.edu) with some code
 * taken from Dave Rusling's (david.rusling@reo.mts.dec.com) 32-bit
 * bios code.
 *
 * Code common to all APECS core logic chips.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/smp.h>
#include <asm/pci.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_apecs.h>
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

#if DEBUG_CONFIG
# define DBGC(args)	printk args
#else
# define DBGC(args)
#endif

#define vuip	volatile unsigned int  *

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the APECS_HAXR2 register
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | | | | | | | | | | | | | | |F|F|F|R|R|R|R|R|R|0|0|
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

	DBGC(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
	      " pci_addr=0x%p, type1=0x%p)\n",
	      bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		int device = device_fn >> 3;

		/* type 0 configuration cycle: */

		if (device > 20) {
			DBGC(("mk_conf_addr: device (%d) > 20, returning -1\n",
			      device));
			return -1;
		}

		*type1 = 0;
		addr = (device_fn << 8) | (where);
	} else {
		/* type 1 configuration cycle: */
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
	unsigned int haxr2 = 0;

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	DBGC(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)APECS_IOC_DCSR;
	*(vuip)APECS_IOC_DCSR = stat0;
	mb();
	DBGC(("conf_read: APECS DCSR was 0x%x\n", stat0));

	/* If Type1 access, must set HAE #2. */
	if (type1) {
		haxr2 = *(vuip)APECS_IOC_HAXR2;
		mb();
		*(vuip)APECS_IOC_HAXR2 = haxr2 | 1;
		DBGC(("conf_read: TYPE1 access\n"));
	}

	draina();
	mcheck_expected(0) = 1;
	mcheck_taken(0) = 0;
	mb();

	/* Access configuration space.  */

	/* Some SRMs step on these registers during a machine check.  */
	asm volatile("ldl %0,%1; mb; mb" : "=r"(value) : "m"(*(vuip)addr)
		     : "$9", "$10", "$11", "$12", "$13", "$14", "memory");

	if (mcheck_taken(0)) {
		mcheck_taken(0) = 0;
		value = 0xffffffffU;
		mb();
	}
	mcheck_expected(0) = 0;
	mb();

#if 1
	/*
	 * david.rusling@reo.mts.dec.com.  This code is needed for the
	 * EB64+ as it does not generate a machine check (why I don't
	 * know).  When we build kernels for one particular platform
	 * then we can make this conditional on the type.
	 */
	draina();

	/* Now look for any errors.  */
	stat0 = *(vuip)APECS_IOC_DCSR;
	DBGC(("conf_read: APECS DCSR after read 0x%x\n", stat0));

	/* Is any error bit set? */
	if (stat0 & 0xffe0U) {
		/* If not NDEV, print status.  */
		if (!(stat0 & 0x0800)) {
			printk("apecs.c:conf_read: got stat0=%x\n", stat0);
		}

		/* Reset error status.  */
		*(vuip)APECS_IOC_DCSR = stat0;
		mb();
		wrmces(0x7);			/* reset machine check */
		value = 0xffffffff;
	}
#endif

	/* If Type1 access, must reset HAE #2 so normal IO space ops work.  */
	if (type1) {
		*(vuip)APECS_IOC_HAXR2 = haxr2 & ~1;
		mb();
	}
	__restore_flags(flags);

	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0;
	unsigned int haxr2 = 0;

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)APECS_IOC_DCSR;
	*(vuip)APECS_IOC_DCSR = stat0;
	mb();

	/* If Type1 access, must set HAE #2. */
	if (type1) {
		haxr2 = *(vuip)APECS_IOC_HAXR2;
		mb();
		*(vuip)APECS_IOC_HAXR2 = haxr2 | 1;
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

#if 1
	/*
	 * david.rusling@reo.mts.dec.com.  This code is needed for the
	 * EB64+ as it does not generate a machine check (why I don't
	 * know).  When we build kernels for one particular platform
	 * then we can make this conditional on the type.
	 */
	draina();

	/* Now look for any errors.  */
	stat0 = *(vuip)APECS_IOC_DCSR;

	/* Is any error bit set? */
	if (stat0 & 0xffe0U) {
		/* If not NDEV, print status.  */
		if (!(stat0 & 0x0800)) {
			printk("apecs.c:conf_write: got stat0=%x\n", stat0);
		}

		/* Reset error status.  */
		*(vuip)APECS_IOC_DCSR = stat0;
		mb();
		wrmces(0x7);			/* reset machine check */
	}
#endif

	/* If Type1 access, must reset HAE #2 so normal IO space ops work.  */
	if (type1) {
		*(vuip)APECS_IOC_HAXR2 = haxr2 & ~1;
		mb();
	}
	__restore_flags(flags);
}

static int
apecs_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x00 + APECS_CONF;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int 
apecs_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x08 + APECS_CONF;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

static int
apecs_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + 0x18 + APECS_CONF;
	*value = conf_read(addr, type1);
	return PCIBIOS_SUCCESSFUL;
}

static int
apecs_write_config(struct pci_dev *dev, int where, u32 value, long mask)
{
	unsigned long addr, pci_addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = (pci_addr << 5) + mask + APECS_CONF;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

static int
apecs_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	return apecs_write_config(dev, where, value, 0x00);
}

static int
apecs_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	return apecs_write_config(dev, where, value, 0x08);
}

static int
apecs_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	return apecs_write_config(dev, where, value, 0x18);
}

struct pci_ops apecs_pci_ops = 
{
	read_byte:	apecs_read_config_byte,
	read_word:	apecs_read_config_word,
	read_dword:	apecs_read_config_dword,
	write_byte:	apecs_write_config_byte,
	write_word:	apecs_write_config_word,
	write_dword:	apecs_write_config_dword
};

void __init
apecs_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	struct pci_controler *hose;

	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, window 2 is disabled.  In the future, we may
	 * want to use it to do scatter/gather DMA.  Window 1
	 * goes at 1 GB and is 1 GB large.
	 */
	*(vuip)APECS_IOC_PB1R = 1UL << 19 | (APECS_DMA_WIN_BASE & 0xfff00000U);
	*(vuip)APECS_IOC_PM1R = (APECS_DMA_WIN_SIZE - 1) & 0xfff00000U;
	*(vuip)APECS_IOC_TB1R = 0;

	*(vuip)APECS_IOC_PB2R = 0U;	/* disable window 2 */

	/*
	 * Finally, clear the HAXR2 register, which gets used
	 * for PCI Config Space accesses. That is the way
	 * we want to use it, and we do not want to depend on
	 * what ARC or SRM might have left behind...
	 */
	*(vuip)APECS_IOC_HAXR2 = 0;
	mb();

	/*
	 * Create our single hose.
	 */

	hose = alloc_pci_controler(mem_start);
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->config_space = APECS_CONF;
	hose->index = 0;
}

void
apecs_pci_clr_err(void)
{
	unsigned int jd;

	jd = *(vuip)APECS_IOC_DCSR;
	if (jd & 0xffe0L) {
		*(vuip)APECS_IOC_SEAR;
		*(vuip)APECS_IOC_DCSR = jd | 0xffe1L;
		mb();
		*(vuip)APECS_IOC_DCSR;
	}
	*(vuip)APECS_IOC_TBIA = (unsigned int)APECS_IOC_TBIA;
	mb();
	*(vuip)APECS_IOC_TBIA;
}

void
apecs_machine_check(unsigned long vector, unsigned long la_ptr,
		    struct pt_regs * regs)
{
	struct el_common *mchk_header;
	struct el_apecs_procdata *mchk_procdata;
	struct el_apecs_sysdata_mcheck *mchk_sysdata;

	mchk_header = (struct el_common *)la_ptr;

	mchk_procdata = (struct el_apecs_procdata *)
		(la_ptr + mchk_header->proc_offset
		 - sizeof(mchk_procdata->paltemp));

	mchk_sysdata = (struct el_apecs_sysdata_mcheck *)
		(la_ptr + mchk_header->sys_offset);


	/* Clear the error before any reporting.  */
	mb();
	mb(); /* magic */
	draina();
	apecs_pci_clr_err();
	wrmces(0x7);		/* reset machine check pending flag */
	mb();

	process_mcheck_info(vector, la_ptr, regs, "APECS",
			    (mcheck_expected(0)
			     && (mchk_sysdata->epic_dcsr & 0x0c00UL)));
}
