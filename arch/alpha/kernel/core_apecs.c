/*
 *	linux/arch/alpha/kernel/core_apecs.c
 *
 * Code common to all APECS core logic chips.
 *
 * Rewritten for Apecs from the lca.c from:
 *
 * Written by David Mosberger (davidm@cs.arizona.edu) with some code
 * taken from Dave Rusling's (david.rusling@reo.mts.dec.com) 32-bit
 * bios code.
 */
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/ptrace.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_apecs.h>
#undef __EXTERN_INLINE

#include "proto.h"

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

/*
 * BIOS32-style PCI interface:
 */

#ifdef DEBUG
# define DBG(args)	printk args
#else
# define DBG(args)
#endif

#define vuip	volatile unsigned int  *

volatile unsigned int apecs_mcheck_expected = 0;
volatile unsigned int apecs_mcheck_taken = 0;
static unsigned int apecs_jd, apecs_jd1, apecs_jd2;


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
mk_conf_addr(u8 bus, u8 device_fn, u8 where, unsigned long *pci_addr,
	     unsigned char *type1)
{
	unsigned long addr;

	DBG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
	     " pci_addr=0x%p, type1=0x%p)\n",
	     bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		int device = device_fn >> 3;

		/* type 0 configuration cycle: */

		if (device > 20) {
			DBG(("mk_conf_addr: device (%d) > 20, returning -1\n",
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
	DBG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static unsigned int
conf_read(unsigned long addr, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, value;
	unsigned int haxr2 = 0;

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	DBG(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)APECS_IOC_DCSR;
	*(vuip)APECS_IOC_DCSR = stat0;
	mb();
	DBG(("conf_read: APECS DCSR was 0x%x\n", stat0));

	/* If Type1 access, must set HAE #2. */
	if (type1) {
		haxr2 = *(vuip)APECS_IOC_HAXR2;
		mb();
		*(vuip)APECS_IOC_HAXR2 = haxr2 | 1;
		DBG(("conf_read: TYPE1 access\n"));
	}

	draina();
	apecs_mcheck_expected = 1;
	apecs_mcheck_taken = 0;
	mb();

	/* Access configuration space.  */

	/* Some SRMs step on these registers during a machine check.  */
	asm volatile("ldl %0,%1; mb; mb" : "=r"(value) : "m"(*(vuip)addr)
		     : "$9", "$10", "$11", "$12", "$13", "$14", "memory");

	if (apecs_mcheck_taken) {
		apecs_mcheck_taken = 0;
		value = 0xffffffffU;
		mb();
	}
	apecs_mcheck_expected = 0;
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
	DBG(("conf_read: APECS DCSR after read 0x%x\n", stat0));

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
	apecs_mcheck_expected = 1;
	mb();

	/* Access configuration space.  */
	*(vuip)addr = value;
	mb();
	mb();  /* magic */
	apecs_mcheck_expected = 0;
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

int
apecs_hose_read_config_byte (u8 bus, u8 device_fn, u8 where, u8 *value,
			     struct linux_hose_info *hose)
{
	unsigned long addr = APECS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x00;

	*value = conf_read(addr, type1) >> ((where & 3) * 8);

	return PCIBIOS_SUCCESSFUL;
}

int 
apecs_hose_read_config_word (u8 bus, u8 device_fn, u8 where, u16 *value,
			     struct linux_hose_info *hose)
{
	unsigned long addr = APECS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x08;

	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int
apecs_hose_read_config_dword (u8 bus, u8 device_fn, u8 where, u32 *value,
			      struct linux_hose_info *hose)
{
	unsigned long addr = APECS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x18;
	*value = conf_read(addr, type1);
	return PCIBIOS_SUCCESSFUL;
}

int
apecs_hose_write_config_byte (u8 bus, u8 device_fn, u8 where, u8 value,
			      struct linux_hose_info *hose)
{
	unsigned long addr = APECS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x00;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

int 
apecs_hose_write_config_word (u8 bus, u8 device_fn, u8 where, u16 value,
			      struct linux_hose_info *hose)
{
	unsigned long addr = APECS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x08;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

int 
apecs_hose_write_config_dword (u8 bus, u8 device_fn, u8 where, u32 value,
			       struct linux_hose_info *hose)
{
	unsigned long addr = APECS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x18;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

void __init
apecs_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	switch (alpha_use_srm_setup)
	{
	default:
#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
		/* Check window 1 for enabled and mapped to 0. */
		if ((*(vuip)APECS_IOC_PB1R & (1U<<19))
		    && (*(vuip)APECS_IOC_TB1R == 0)) {
			APECS_DMA_WIN_BASE = *(vuip)APECS_IOC_PB1R & 0xfff00000U;
			APECS_DMA_WIN_SIZE = *(vuip)APECS_IOC_PM1R & 0xfff00000U;
			APECS_DMA_WIN_SIZE += 0x00100000U;
#if 1
			printk("apecs_init: using Window 1 settings\n");
			printk("apecs_init: PB1R 0x%x PM1R 0x%x TB1R 0x%x\n",
			       *(vuip)APECS_IOC_PB1R,
			       *(vuip)APECS_IOC_PM1R,
			       *(vuip)APECS_IOC_TB1R);
#endif
			break;
		}

		/* Check window 2 for enabled and mapped to 0.  */
		if ((*(vuip)APECS_IOC_PB2R & (1U<<19))
		    && (*(vuip)APECS_IOC_TB2R == 0)) {
			APECS_DMA_WIN_BASE = *(vuip)APECS_IOC_PB2R & 0xfff00000U;
			APECS_DMA_WIN_SIZE = *(vuip)APECS_IOC_PM2R & 0xfff00000U;
			APECS_DMA_WIN_SIZE += 0x00100000U;
#if 1
			printk("apecs_init: using Window 2 settings\n");
			printk("apecs_init: PB2R 0x%x PM2R 0x%x TB2R 0x%x\n",
			       *(vuip)APECS_IOC_PB2R,
			       *(vuip)APECS_IOC_PM2R,
			       *(vuip)APECS_IOC_TB2R);
#endif
			break;
		}
		
		/* Otherwise, we must use our defaults.  */
		APECS_DMA_WIN_BASE = APECS_DMA_WIN_BASE_DEFAULT;
		APECS_DMA_WIN_SIZE = APECS_DMA_WIN_SIZE_DEFAULT;
#endif
	case 0:
		/*
		 * Set up the PCI->physical memory translation windows.
		 * For now, window 2 is disabled.  In the future, we may
		 * want to use it to do scatter/gather DMA.  Window 1
		 * goes at 1 GB and is 1 GB large.
		 */
		*(vuip)APECS_IOC_PB2R  = 0U; /* disable window 2 */

		*(vuip)APECS_IOC_PB1R  = 1U<<19 | (APECS_DMA_WIN_BASE_DEFAULT & 0xfff00000U);
		*(vuip)APECS_IOC_PM1R  = (APECS_DMA_WIN_SIZE_DEFAULT - 1) & 0xfff00000U;
		*(vuip)APECS_IOC_TB1R  = 0;
		break;
	}

	/*
	 * Finally, clear the HAXR2 register, which gets used
	 * for PCI Config Space accesses. That is the way
	 * we want to use it, and we do not want to depend on
	 * what ARC or SRM might have left behind...
	 */
	*(vuip)APECS_IOC_HAXR2 = 0; mb();
}

int
apecs_pci_clr_err(void)
{
	apecs_jd = *(vuip)APECS_IOC_DCSR;
	if (apecs_jd & 0xffe0L) {
		apecs_jd1 = *(vuip)APECS_IOC_SEAR;
		*(vuip)APECS_IOC_DCSR = apecs_jd | 0xffe1L;
		apecs_jd = *(vuip)APECS_IOC_DCSR;
		mb();
	}
	*(vuip)APECS_IOC_TBIA = (unsigned int)APECS_IOC_TBIA;
	apecs_jd2 = *(vuip)APECS_IOC_TBIA;
	mb();
	return 0;
}

void
apecs_machine_check(unsigned long vector, unsigned long la_ptr,
		    struct pt_regs * regs)
{
	struct el_common *mchk_header;
	struct el_apecs_procdata *mchk_procdata;
	struct el_apecs_sysdata_mcheck *mchk_sysdata;
	unsigned long *ptr;
	int i;

	mchk_header = (struct el_common *)la_ptr;

	mchk_procdata = (struct el_apecs_procdata *)
		(la_ptr + mchk_header->proc_offset
		 - sizeof(mchk_procdata->paltemp));

	mchk_sysdata = (struct el_apecs_sysdata_mcheck *)
		(la_ptr + mchk_header->sys_offset);

#ifdef DEBUG
	printk("apecs_machine_check: vector=0x%lx la_ptr=0x%lx\n",
	       vector, la_ptr);
	printk("        pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
	       regs->pc, mchk_header->size, mchk_header->proc_offset,
	       mchk_header->sys_offset);
	printk("apecs_machine_check: expected %d DCSR 0x%lx PEAR 0x%lx\n",
	       apecs_mcheck_expected, mchk_sysdata->epic_dcsr,
	       mchk_sysdata->epic_pear);
	ptr = (unsigned long *)la_ptr;
	for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
		printk(" +%lx %lx %lx\n", i*sizeof(long), ptr[i], ptr[i+1]);
	}
#endif

	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */

	if (apecs_mcheck_expected
	    && (mchk_sysdata->epic_dcsr & 0x0c00UL)) {
		apecs_mcheck_expected = 0;
		apecs_mcheck_taken = 1;
		mb();
		mb(); /* magic */
		apecs_pci_clr_err();
		wrmces(0x7);
		mb();
		draina();
		DBG(("apecs_machine_check: EXPECTED\n"));
	}
	else if (vector == 0x620 || vector == 0x630) {
		/* Disable correctable from now on.  */
		wrmces(0x1f);
		mb();
		draina();
		printk("apecs_machine_check: HW correctable (0x%lx)\n",
		       vector);
	}
	else {
		printk(KERN_CRIT "APECS machine check:\n");
		printk(KERN_CRIT "  vector=0x%lx la_ptr=0x%lx\n",
		       vector, la_ptr);
		printk(KERN_CRIT
		       "  pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
		       regs->pc, mchk_header->size, mchk_header->proc_offset,
		       mchk_header->sys_offset);
		printk(KERN_CRIT "  expected %d DCSR 0x%lx PEAR 0x%lx\n",
		       apecs_mcheck_expected, mchk_sysdata->epic_dcsr,
		       mchk_sysdata->epic_pear);

		ptr = (unsigned long *)la_ptr;
		for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
			printk(KERN_CRIT " +%lx %lx %lx\n",
			       i*sizeof(long), ptr[i], ptr[i+1]);
		}
#if 0
		/* doesn't work with MILO */
		show_regs(regs);
#endif
	}
}
