/*
 * Code common to all PYXIS chips.
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
 *
 */
#include <linux/config.h> /* CONFIG_ALPHA_RUFFIAN. */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/hwrpb.h>
#include <asm/ptrace.h>
#include <asm/mmu_context.h>

/* NOTE: Herein are back-to-back mb instructions.  They are magic.
   One plausible explanation is that the I/O controller does not properly
   handle the system transaction.  Another involves timing.  Ho hum.  */

extern struct hwrpb_struct *hwrpb;
extern asmlinkage void wrmces(unsigned long mces);

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
#define DEBUG_MCHECK_DUMP
#else
# define DBG_MCK(args)
#endif

#define vulp	volatile unsigned long *
#define vuip	volatile unsigned int  *

static volatile unsigned int PYXIS_mcheck_expected = 0;
static volatile unsigned int PYXIS_mcheck_taken = 0;
static unsigned int PYXIS_jd;

#ifdef CONFIG_ALPHA_SRM_SETUP
unsigned int PYXIS_DMA_WIN_BASE = PYXIS_DMA_WIN_BASE_DEFAULT;
unsigned int PYXIS_DMA_WIN_SIZE = PYXIS_DMA_WIN_SIZE_DEFAULT;
unsigned long pyxis_sm_base_r1, pyxis_sm_base_r2, pyxis_sm_base_r3;
#endif /* SRM_SETUP */

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
	unsigned int stat0, value, temp;
	unsigned int pyxis_cfg = 0; /* to keep gcc quiet */

	save_and_cli(flags);	/* avoid getting hit by machine check */

	DBG(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = stat0; mb();
	temp = *(vuip)PYXIS_ERR;  /* re-read to force write */
	DBG(("conf_read: PYXIS ERR was 0x%x\n", stat0));
	/* if Type1 access, must set PYXIS CFG */
	if (type1) {
		pyxis_cfg = *(vuip)PYXIS_CFG;
		*(vuip)PYXIS_CFG = pyxis_cfg | 1; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
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
	mb();  /* magic */
	if (PYXIS_mcheck_taken) {
		PYXIS_mcheck_taken = 0;
		value = 0xffffffffU;
		mb();
	}
	PYXIS_mcheck_expected = 0;
	mb();

	/* if Type1 access, must reset IOC CFG so normal IO space ops work */
	if (type1) {
		*(vuip)PYXIS_CFG = pyxis_cfg & ~1; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
	}

	DBG(("conf_read(): finished\n"));

	restore_flags(flags);
	return value;
}


static void conf_write(unsigned long addr, unsigned int value,
		       unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, temp;
	unsigned int pyxis_cfg = 0; /* to keep gcc quiet */

	save_and_cli(flags);	/* avoid getting hit by machine check */

	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = stat0; mb();
	temp = *(vuip)PYXIS_ERR;  /* re-read to force write */
	DBG(("conf_write: PYXIS ERR was 0x%x\n", stat0));
	/* if Type1 access, must set PYXIS CFG */
	if (type1) {
		pyxis_cfg = *(vuip)PYXIS_CFG;
		*(vuip)PYXIS_CFG = pyxis_cfg | 1; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
		DBG(("conf_read: TYPE1 access\n"));
	}

	draina();
	PYXIS_mcheck_expected = 1;
	mb();
	/* access configuration space: */
	*(vuip)addr = value;
	mb();
	mb();  /* magic */
	temp = *(vuip)PYXIS_ERR; /* do a PYXIS read to force the write */
	PYXIS_mcheck_expected = 0;
	mb();

	/* if Type1 access, must reset IOC CFG so normal IO space ops work */
	if (type1) {
		*(vuip)PYXIS_CFG = pyxis_cfg & ~1; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
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
	pyxis_err = *(vuip)PYXIS_ERR_MASK;
	pyxis_err &= ~4;   
	*(vuip)PYXIS_ERR_MASK = pyxis_err; mb();
	pyxis_err = *(vuip)PYXIS_ERR_MASK;  /* re-read to force write */

	pyxis_err = *(vuip)PYXIS_ERR ;
	pyxis_err |= 0x180;   /* master/target abort */
	*(vuip)PYXIS_ERR = pyxis_err; mb();
	pyxis_err = *(vuip)PYXIS_ERR;  /* re-read to force write */

#ifdef CONFIG_ALPHA_SRM_SETUP
	/* check window 0 for enabled and mapped to 0 */
	if (((*(vuip)PYXIS_W0_BASE & 3) == 1) &&
	    (*(vuip)PYXIS_T0_BASE == 0) &&
	    ((*(vuip)PYXIS_W0_MASK & 0xfff00000U) > 0x0ff00000U))
	{
	  PYXIS_DMA_WIN_BASE = *(vuip)PYXIS_W0_BASE & 0xfff00000U;
	  PYXIS_DMA_WIN_SIZE = *(vuip)PYXIS_W0_MASK & 0xfff00000U;
	  PYXIS_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("pyxis_init: using Window 0 settings\n");
	  printk("pyxis_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)PYXIS_W0_BASE,
		 *(vuip)PYXIS_W0_MASK,
		 *(vuip)PYXIS_T0_BASE);
#endif
	}
	else  /* check window 1 for enabled and mapped to 0 */
	if (((*(vuip)PYXIS_W1_BASE & 3) == 1) &&
	    (*(vuip)PYXIS_T1_BASE == 0) &&
	    ((*(vuip)PYXIS_W1_MASK & 0xfff00000U) > 0x0ff00000U))
{
	  PYXIS_DMA_WIN_BASE = *(vuip)PYXIS_W1_BASE & 0xfff00000U;
	  PYXIS_DMA_WIN_SIZE = *(vuip)PYXIS_W1_MASK & 0xfff00000U;
	  PYXIS_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("pyxis_init: using Window 1 settings\n");
	  printk("pyxis_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)PYXIS_W1_BASE,
		 *(vuip)PYXIS_W1_MASK,
		 *(vuip)PYXIS_T1_BASE);
#endif
	}
	else  /* check window 2 for enabled and mapped to 0 */
	if (((*(vuip)PYXIS_W2_BASE & 3) == 1) &&
	    (*(vuip)PYXIS_T2_BASE == 0) &&
	    ((*(vuip)PYXIS_W2_MASK & 0xfff00000U) > 0x0ff00000U))
	{
	  PYXIS_DMA_WIN_BASE = *(vuip)PYXIS_W2_BASE & 0xfff00000U;
	  PYXIS_DMA_WIN_SIZE = *(vuip)PYXIS_W2_MASK & 0xfff00000U;
	  PYXIS_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("pyxis_init: using Window 2 settings\n");
	  printk("pyxis_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)PYXIS_W2_BASE,
		 *(vuip)PYXIS_W2_MASK,
		 *(vuip)PYXIS_T2_BASE);
#endif
	}
	else  /* check window 3 for enabled and mapped to 0 */
	if (((*(vuip)PYXIS_W3_BASE & 3) == 1) &&
	    (*(vuip)PYXIS_T3_BASE == 0) &&
	    ((*(vuip)PYXIS_W3_MASK & 0xfff00000U) > 0x0ff00000U))
	{
	  PYXIS_DMA_WIN_BASE = *(vuip)PYXIS_W3_BASE & 0xfff00000U;
	  PYXIS_DMA_WIN_SIZE = *(vuip)PYXIS_W3_MASK & 0xfff00000U;
	  PYXIS_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("pyxis_init: using Window 3 settings\n");
	  printk("pyxis_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)PYXIS_W3_BASE,
		 *(vuip)PYXIS_W3_MASK,
		 *(vuip)PYXIS_T3_BASE);
#endif
	}
	else  /* we must use our defaults which were pre-initialized... */
#endif /* SRM_SETUP */
	{
#if defined(CONFIG_ALPHA_RUFFIAN)
#if 1
	printk("pyxis_init: skipping window register rewrites... "
	       " trust DeskStation firmware!\n");
#endif
#else /* RUFFIAN */
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
#endif /* RUFFIAN */
	}

	/*
	 * check ASN in HWRPB for validity, report if bad
	 */
	if (hwrpb->max_asn != MAX_ASN) {
		printk("PYXIS_init: max ASN from HWRPB is bad (0x%lx)\n",
		       hwrpb->max_asn);
		hwrpb->max_asn = MAX_ASN;
	}

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
 
	{
		unsigned int pyxis_hae_mem = *(vuip)PYXIS_HAE_MEM;
		unsigned int pyxis_hae_io = *(vuip)PYXIS_HAE_IO;
#if 0
		printk("PYXIS_init: HAE_MEM was 0x%x\n", pyxis_hae_mem);
		printk("PYXIS_init: HAE_IO was 0x%x\n", pyxis_hae_io);
#endif
#ifdef CONFIG_ALPHA_SRM_SETUP
	  /*
	   * sigh... For the SRM setup, unless we know apriori what the HAE
	   * contents will be, we need to setup the arbitrary region bases
	   * so we can test against the range of addresses and tailor the
	   * region chosen for the SPARSE memory access.
	   * 
	   * see include/asm-alpha/pyxis.h for the SPARSE mem read/write
	  */
	  pyxis_sm_base_r1 = (pyxis_hae_mem      ) & 0xe0000000UL;/* region 1 */
	  pyxis_sm_base_r2 = (pyxis_hae_mem << 16) & 0xf8000000UL;/* region 2 */
	  pyxis_sm_base_r3 = (pyxis_hae_mem << 24) & 0xfc000000UL;/* region 3 */

	  /*
	    Set the HAE cache, so that setup_arch() code
	    will use the SRM setting always. Our readb/writeb
	    code in pyxis.h expects never to have to change
	    the contents of the HAE.
	   */
	  hae.cache = pyxis_hae_mem;
#else /* SRM_SETUP */
          *(vuip)PYXIS_HAE_MEM = 0U; mb();
	  pyxis_hae_mem = *(vuip)PYXIS_HAE_MEM;  /* re-read to force write */
		*(vuip)PYXIS_HAE_IO = 0; mb();
	  pyxis_hae_io = *(vuip)PYXIS_HAE_IO;  /* re-read to force write */
#endif /* SRM_SETUP */
        }

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
	    ctrl1 = *(vuip)PYXIS_CTRL1;  /* re-read to force write */
	  }
	}

	return mem_start;
}

int pyxis_pci_clr_err(void)
{
	PYXIS_jd = *(vuip)PYXIS_ERR;
	DBG(("PYXIS_pci_clr_err: PYXIS ERR after read 0x%x\n", PYXIS_jd));
	*(vuip)PYXIS_ERR = 0x0180; mb();
	PYXIS_jd = *(vuip)PYXIS_ERR;  /* re-read to force write */
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
	mb();  /* magic */
	if (PYXIS_mcheck_expected) {
		DBG(("PYXIS machine check expected\n"));
		PYXIS_mcheck_expected = 0;
		PYXIS_mcheck_taken = 1;
		mb();
		mb();  /* magic */
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
		DBG_MCK(("\t\t pc=0x%lx size=0x%x procoffset=0x%x"
			 " sysoffset 0x%x\n",
			 regs->pc, mchk_header->size, mchk_header->proc_offset,
			 mchk_header->sys_offset));
		PYXIS_mcheck_expected = 0;
		PYXIS_mcheck_taken = 1;
		mb();
		mb();  /* magic */
		draina();
		pyxis_pci_clr_err();
		wrmces(0x7);
		mb();
	}
#endif
}

#if defined(CONFIG_ALPHA_RUFFIAN)
/* Note: This is only used by MILO, AFAIK... */
/*
 * The DeskStation Ruffian motherboard firmware does not place
 * the memory size in the PALimpure area.  Therefore, we use
 * the Bank Configuration Registers in PYXIS to obtain the size.
 */
unsigned long pyxis_get_bank_size(unsigned long offset)
{
	unsigned long bank_addr, bank, ret = 0;
  
	/* Valid offsets are: 0x800, 0x840 and 0x880
	   since Ruffian only uses three banks.  */
	bank_addr = (unsigned long)PYXIS_MCR + offset;
	bank = *(vulp)bank_addr;
    
	/* Check BANK_ENABLE */
	if (bank & 0x01) {
		static unsigned long size[] = {
			0x40000000UL, /* 0x00,   1G */ 
			0x20000000UL, /* 0x02, 512M */
			0x10000000UL, /* 0x04, 256M */
			0x08000000UL, /* 0x06, 128M */
			0x04000000UL, /* 0x08,  64M */
			0x02000000UL, /* 0x0a,  32M */
			0x01000000UL, /* 0x0c,  16M */
			0x00800000UL, /* 0x0e,   8M */
			0x80000000UL, /* 0x10,   2G */
		};

		bank = (bank & 0x1e) >> 1;
		if (bank < sizeof(size)/sizeof(*size))
			ret = size[bank];
	}

	return ret;
}
#endif /* CONFIG_ALPHA_RUFFIAN */
