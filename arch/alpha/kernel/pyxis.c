/*
 * Code common to all PYXIS chips.
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/hwrpb.h>
#include <asm/ptrace.h>
#include <asm/mmu_context.h>

extern struct hwrpb_struct *hwrpb;
extern asmlinkage void wrmces(unsigned long mces);
extern int alpha_sys_type;

/*
 * BIOS32-style PCI interface:
 */

#ifdef DEBUG 
# define DBG(args)	printk args
#else
# define DBG(args)
#endif

#define DEBUG_MCHECK
#ifdef DEBUG_MCHECK
# define DBG_MCK(args)	printk args
#else
# define DBG_MCK(args)
#endif

#define vulp	volatile unsigned long *
#define vuip	volatile unsigned int  *

static volatile unsigned int PYXIS_mcheck_expected = 0;
static volatile unsigned int PYXIS_mcheck_taken = 0;
static unsigned int PYXIS_jd;


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
 *	(e.g., scsi and ethernet).
 * 
 *	The register selects a DWORD (32 bit) register offset.  Hence it
 *	doesn't get shifted by 2 bits as we want to "drop" the bottom two
 *	bits.
 */
static int mk_conf_addr(unsigned char bus, unsigned char device_fn,
			unsigned char where, unsigned long *pci_addr,
			unsigned char *type1)
{
	unsigned long addr;

	DBG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
	     " pci_addr=0x%p, type1=0x%p)\n",
	     bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		int device;

		device = device_fn >> 3;
		/* type 0 configuration cycle: */
#if NOT_NOW
		if (device > 20) {
			DBG(("mk_conf_addr: device (%d) > 20, returning -1\n",
			     device));
			return -1;
		}
#endif
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


static unsigned int conf_read(unsigned long addr, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, value;
	unsigned int pyxis_cfg = 0; /* to keep gcc quiet */

	save_flags(flags);	/* avoid getting hit by machine check */
	cli();

	DBG(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = stat0;
	mb();
	DBG(("conf_read: PYXIS ERR was 0x%x\n", stat0));
	/* if Type1 access, must set PYXIS CFG */
	if (type1) {
		pyxis_cfg = *(vuip)PYXIS_CFG;
		*(vuip)PYXIS_CFG = pyxis_cfg | 1;
		mb();
		DBG(("conf_read: TYPE1 access\n"));
	}

	mb();
	draina();
	PYXIS_mcheck_expected = 1;
	PYXIS_mcheck_taken = 0;
	mb();
	/* access configuration space: */
	value = *(vuip)addr;
	mb();
	if (PYXIS_mcheck_taken) {
		PYXIS_mcheck_taken = 0;
		value = 0xffffffffU;
		mb();
	}
	PYXIS_mcheck_expected = 0;
	mb();
	/*
	 * david.rusling@reo.mts.dec.com.  This code is needed for the
	 * EB64+ as it does not generate a machine check (why I don't
	 * know).  When we build kernels for one particular platform
	 * then we can make this conditional on the type.
	 */
#if 0
	draina();

	/* now look for any errors */
	stat0 = *(vuip)PYXIS_IOC_PYXIS_ERR;
	DBG(("conf_read: PYXIS ERR after read 0x%x\n", stat0));
	if (stat0 & 0x8280U) { /* is any error bit set? */
		/* if not NDEV, print status */
		if (!(stat0 & 0x0080)) {
			printk("PYXIS.c:conf_read: got stat0=%x\n", stat0);
		}

		/* reset error status: */
		*(vulp)PYXIS_IOC_PYXIS_ERR = stat0;
		mb();
		wrmces(0x7);			/* reset machine check */
		value = 0xffffffff;
	}
#endif

	/* if Type1 access, must reset IOC CFG so normal IO space ops work */
	if (type1) {
		*(vuip)PYXIS_CFG = pyxis_cfg & ~1;
		mb();
	}

	DBG(("conf_read(): finished\n"));

	restore_flags(flags);
	return value;
}


static void conf_write(unsigned long addr, unsigned int value,
		       unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0;
	unsigned int pyxis_cfg = 0; /* to keep gcc quiet */

	save_flags(flags);	/* avoid getting hit by machine check */
	cli();

	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = stat0;
	mb();
	DBG(("conf_write: PYXIS ERR was 0x%x\n", stat0));
	/* if Type1 access, must set PYXIS CFG */
	if (type1) {
		pyxis_cfg = *(vuip)PYXIS_CFG;
		*(vuip)PYXIS_CFG = pyxis_cfg | 1;
		mb();
		DBG(("conf_read: TYPE1 access\n"));
	}

	draina();
	PYXIS_mcheck_expected = 1;
	mb();
	/* access configuration space: */
	*(vuip)addr = value;
	mb();
	PYXIS_mcheck_expected = 0;
	mb();

	/* if Type1 access, must reset IOC CFG so normal IO space ops work */
	if (type1) {
		*(vuip)PYXIS_CFG = pyxis_cfg & ~1;
		mb();
	}

	DBG(("conf_write(): finished\n"));
	restore_flags(flags);
}


int pcibios_read_config_byte (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned char *value)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	*value = 0xff;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}

	addr |= (pci_addr << 5) + 0x00;

	*value = conf_read(addr, type1) >> ((where & 3) * 8);

	return PCIBIOS_SUCCESSFUL;
}


int pcibios_read_config_word (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned short *value)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	*value = 0xffff;

	if (where & 0x1) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1)) {
		return PCIBIOS_SUCCESSFUL;
	}

	addr |= (pci_addr << 5) + 0x08;

	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}


int pcibios_read_config_dword (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned int *value)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	*value = 0xffffffff;
	if (where & 0x3) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1)) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x18;
	*value = conf_read(addr, type1);
	return PCIBIOS_SUCCESSFUL;
}


int pcibios_write_config_byte (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned char value)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x00;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}


int pcibios_write_config_word (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned short value)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x08;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}


int pcibios_write_config_dword (unsigned char bus, unsigned char device_fn,
				unsigned char where, unsigned int value)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x18;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}


unsigned long pyxis_init(unsigned long mem_start, unsigned long mem_end)
{
	unsigned int pyxis_err ;

	/* 
	 * Set up error reporting.
	 */
	pyxis_err = *(vuip)PYXIS_ERR ;
	pyxis_err |= 0x180;   /* master/target abort */
	*(vuip)PYXIS_ERR = pyxis_err ;
	mb() ;
	pyxis_err = *(vuip)PYXIS_ERR ;

	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, windows 1,2 and 3 are disabled.  In the future, we may
	 * want to use them to do scatter/gather DMA.  Window 0
	 * goes at 1 GB and is 1 GB large.
	 */

	*(vuip)PYXIS_W0_BASE = 1U | (PYXIS_DMA_WIN_BASE & 0xfff00000U);
	*(vuip)PYXIS_W0_MASK = (PYXIS_DMA_WIN_SIZE - 1) & 0xfff00000U;
	*(vuip)PYXIS_T0_BASE = 0;

	*(vuip)PYXIS_W1_BASE = 0x0 ;
	*(vuip)PYXIS_W2_BASE = 0x0 ;
	*(vuip)PYXIS_W3_BASE = 0x0 ;
	mb();

	/*
	 * check ASN in HWRPB for validity, report if bad
	 */
	if (hwrpb->max_asn != MAX_ASN) {
		printk("PYXIS_init: max ASN from HWRPB is bad (0x%lx)\n",
		       hwrpb->max_asn);
		hwrpb->max_asn = MAX_ASN;
	}

	/*
	 * Finally, clear the PYXIS_CFG register, which gets used
	 *  for PCI Config Space accesses. That is the way
	 *  we want to use it, and we do not want to depend on
	 *  what ARC or SRM might have left behind...
	 */
	{
		unsigned int pyxis_cfg;
		pyxis_cfg = *(vuip)PYXIS_CFG; mb();
#if 0
		printk("PYXIS_init: CFG was 0x%x\n", pyxis_cfg);
#endif
		*(vuip)PYXIS_CFG = 0; mb();
	}
 
	{
		unsigned int pyxis_hae_mem = *(vuip)PYXIS_HAE_MEM;
		unsigned int pyxis_hae_io = *(vuip)PYXIS_HAE_IO;
#if 0
		printk("PYXIS_init: HAE_MEM was 0x%x\n", pyxis_hae_mem);
		printk("PYXIS_init: HAE_IO was 0x%x\n", pyxis_hae_io);
#endif
		*(vuip)PYXIS_HAE_MEM = 0; mb();
		pyxis_hae_mem = *(vuip)PYXIS_HAE_MEM;
		*(vuip)PYXIS_HAE_IO = 0; mb();
		pyxis_hae_io = *(vuip)PYXIS_HAE_IO;
	}

	return mem_start;
}

int pyxis_pci_clr_err(void)
{
	PYXIS_jd = *(vuip)PYXIS_ERR;
	DBG(("PYXIS_pci_clr_err: PYXIS ERR after read 0x%x\n", PYXIS_jd));
	*(vuip)PYXIS_ERR = 0x0180;
	mb();
	PYXIS_jd = *(vuip)PYXIS_ERR;
	return 0;
}

void pyxis_machine_check(unsigned long vector, unsigned long la_ptr,
			 struct pt_regs * regs)
{
	struct el_common *mchk_header;
	struct el_PYXIS_sysdata_mcheck *mchk_sysdata;

	mchk_header = (struct el_common *)la_ptr;

	mchk_sysdata = (struct el_PYXIS_sysdata_mcheck *)
		(la_ptr + mchk_header->sys_offset);

#if 0
	DBG_MCK(("pyxis_machine_check: vector=0x%lx la_ptr=0x%lx\n",
		 vector, la_ptr));
	DBG_MCK(("\t\t pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
		 regs->pc, mchk_header->size, mchk_header->proc_offset,
		 mchk_header->sys_offset));
	DBG_MCK(("pyxis_machine_check: expected %d DCSR 0x%lx PEAR 0x%lx\n",
		 PYXIS_mcheck_expected, mchk_sysdata->epic_dcsr,
		 mchk_sysdata->epic_pear));
#endif
#ifdef DEBUG_MCHECK_DUMP
	{
		unsigned long *ptr;
		int i;

		ptr = (unsigned long *)la_ptr;
		for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
			printk(" +%lx %lx %lx\n", i*sizeof(long), ptr[i], ptr[i+1]);
		}
	}
#endif /* DEBUG_MCHECK_DUMP */

	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */
	mb();
	if (PYXIS_mcheck_expected/* && (mchk_sysdata->epic_dcsr && 0x0c00UL)*/) {
		DBG(("PYXIS machine check expected\n"));
		PYXIS_mcheck_expected = 0;
		PYXIS_mcheck_taken = 1;
		mb();
		draina();
		pyxis_pci_clr_err();
		wrmces(0x7);
		mb();
	}
#if 1
	else {
		printk("PYXIS machine check NOT expected\n") ;
		DBG_MCK(("pyxis_machine_check: vector=0x%lx la_ptr=0x%lx\n",
			 vector, la_ptr));
		DBG_MCK(("\t\t pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
			 regs->pc, mchk_header->size, mchk_header->proc_offset,
			 mchk_header->sys_offset));
		PYXIS_mcheck_expected = 0;
		PYXIS_mcheck_taken = 1;
		mb();
		draina();
		pyxis_pci_clr_err();
		wrmces(0x7);
		mb();
	}
#endif
}
