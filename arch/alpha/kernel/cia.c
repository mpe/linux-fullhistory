/*
 * Code common to all CIA chips.
 *
 * Written by David A Rusling (david.rusling@reo.mts.dec.com).
 * December 1995.
 *
 */
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/hwrpb.h>
#include <asm/ptrace.h>
#include <asm/mmu_context.h>

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

extern struct hwrpb_struct *hwrpb;
extern asmlinkage void wrmces(unsigned long mces);

/*
 * Machine check reasons.  Defined according to PALcode sources
 * (osf.h and platform.h).
 */
#define MCHK_K_TPERR		0x0080
#define MCHK_K_TCPERR		0x0082
#define MCHK_K_HERR		0x0084
#define MCHK_K_ECC_C		0x0086
#define MCHK_K_ECC_NC		0x0088
#define MCHK_K_OS_BUGCHECK	0x008A
#define MCHK_K_PAL_BUGCHECK	0x0090

/*
 * BIOS32-style PCI interface:
 */

/* #define DEBUG_MCHECK */
/* #define DEBUG_CONFIG */
/* #define DEBUG_DUMP_REGS */

#ifdef DEBUG_MCHECK
# define DBGM(args)	printk args
#else
# define DBGM(args)
#endif
#ifdef DEBUG_CONFIG
# define DBGC(args)	printk args
#else
# define DBGC(args)
#endif

#define vuip	volatile unsigned int  *

static volatile unsigned int CIA_mcheck_expected = 0;
static volatile unsigned int CIA_mcheck_taken = 0;
static unsigned int CIA_jd;

#ifdef CONFIG_ALPHA_SRM_SETUP
unsigned int CIA_DMA_WIN_BASE = CIA_DMA_WIN_BASE_DEFAULT;
unsigned int CIA_DMA_WIN_SIZE = CIA_DMA_WIN_SIZE_DEFAULT;
unsigned long cia_sm_base_r1, cia_sm_base_r2, cia_sm_base_r3;
#endif /* SRM_SETUP */

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

	DBGC(("mk_conf_addr(bus=%d, device_fn=0x%x, where=0x%x, "
	      "pci_addr=0x%p, type1=0x%p)\n",
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


static unsigned int conf_read(unsigned long addr, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, value;
	unsigned int cia_cfg = 0; /* to keep gcc quiet */

	value = 0xffffffffU;
	mb();

	save_flags(flags);	/* avoid getting hit by machine check */
	cli();

	DBGC(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)CIA_IOC_CIA_ERR;
	*(vuip)CIA_IOC_CIA_ERR = stat0;
	mb();
	DBGC(("conf_read: CIA ERR was 0x%x\n", stat0));
	/* if Type1 access, must set CIA CFG */
	if (type1) {
		cia_cfg = *(vuip)CIA_IOC_CFG;
		*(vuip)CIA_IOC_CFG = cia_cfg | 1;
		mb();
		DBGC(("conf_read: TYPE1 access\n"));
	}

	mb();
	draina();
	CIA_mcheck_expected = 1;
	CIA_mcheck_taken = 0;
	mb();
	/* access configuration space: */
	value = *(vuip)addr;
	mb();
	mb();  /* magic */
	if (CIA_mcheck_taken) {
		CIA_mcheck_taken = 0;
		value = 0xffffffffU;
		mb();
	}
	CIA_mcheck_expected = 0;
	mb();

#if 0
	/*
	  this code might be necessary if machine checks aren't taken,
	  but I can't get it to work on CIA-2, so its disabled.
	  */
	draina();

	/* now look for any errors */
	stat0 = *(vuip)CIA_IOC_CIA_ERR;
	DBGC(("conf_read: CIA ERR after read 0x%x\n", stat0));
	if (stat0 & 0x8FEF0FFFU) { /* is any error bit set? */
		/* if not MAS_ABT, print status */
		if (!(stat0 & 0x0080)) {
			printk("CIA.c:conf_read: got stat0=%x\n", stat0);
		}

		/* reset error status: */
		*(vuip)CIA_IOC_CIA_ERR = stat0;
		mb();
		wrmces(0x7);			/* reset machine check */
		value = 0xffffffff;
	}
#endif

	/* if Type1 access, must reset IOC CFG so normal IO space ops work */
	if (type1) {
		*(vuip)CIA_IOC_CFG = cia_cfg & ~1;
		mb();
	}

	DBGC(("conf_read(): finished\n"));

	restore_flags(flags);
	return value;
}


static void conf_write(unsigned long addr, unsigned int value,
		       unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0;
	unsigned int cia_cfg = 0; /* to keep gcc quiet */

	save_flags(flags);	/* avoid getting hit by machine check */
	cli();

	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)CIA_IOC_CIA_ERR;
	*(vuip)CIA_IOC_CIA_ERR = stat0;
	mb();
	DBGC(("conf_write: CIA ERR was 0x%x\n", stat0));
	/* if Type1 access, must set CIA CFG */
	if (type1) {
		cia_cfg = *(vuip)CIA_IOC_CFG;
		*(vuip)CIA_IOC_CFG = cia_cfg | 1;
		mb();
		DBGC(("conf_write: TYPE1 access\n"));
	}

	draina();
	CIA_mcheck_expected = 1;
	mb();
	/* access configuration space: */
	*(vuip)addr = value;
	mb();
	mb();  /* magic */

	CIA_mcheck_expected = 0;
	mb();

#if 0
	/*
	 * This code might be necessary if machine checks aren't taken,
	 * but I can't get it to work on CIA-2, so its disabled.
	 */
	draina();

	/* Now look for any errors */
	stat0 = *(vuip)CIA_IOC_CIA_ERR;
	DBGC(("conf_write: CIA ERR after write 0x%x\n", stat0));
	if (stat0 & 0x8FEF0FFFU) { /* is any error bit set? */
		/* If not MAS_ABT, print status */
		if (!(stat0 & 0x0080)) {
			printk("CIA.c:conf_read: got stat0=%x\n", stat0);
		}

		/* reset error status: */
		*(vuip)CIA_IOC_CIA_ERR = stat0;
		mb();
		wrmces(0x7);			/* reset machine check */
		value = 0xffffffff;
	}
#endif

	/* if Type1 access, must reset IOC CFG so normal IO space ops work */
	if (type1) {
		*(vuip)CIA_IOC_CFG = cia_cfg & ~1;
		mb();
	}

	DBGC(("conf_write(): finished\n"));
	restore_flags(flags);
}


int pcibios_read_config_byte (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned char *value)
{
	unsigned long addr = CIA_CONF;
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
	unsigned long addr = CIA_CONF;
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
	unsigned long addr = CIA_CONF;
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
	unsigned long addr = CIA_CONF;
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
	unsigned long addr = CIA_CONF;
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
	unsigned long addr = CIA_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x18;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}


unsigned long cia_init(unsigned long mem_start, unsigned long mem_end)
{
        unsigned int cia_tmp;

#ifdef DEBUG_DUMP_REGS
	{
		unsigned int temp;
		temp = *(vuip)CIA_IOC_CIA_REV; mb();
		printk("CIA_init: CIA_REV was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_PCI_LAT; mb();
		printk("CIA_init: CIA_PCI_LAT was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_CIA_CTRL; mb();
		printk("CIA_init: CIA_CTRL was 0x%x\n", temp);
		temp = *(vuip)0xfffffc8740000140UL; mb();
		printk("CIA_init: CIA_CTRL1 was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_HAE_MEM; mb();
		printk("CIA_init: CIA_HAE_MEM was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_HAE_IO; mb();
		printk("CIA_init: CIA_HAE_IO was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_CFG; mb();
		printk("CIA_init: CIA_CFG was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_CACK_EN; mb();
		printk("CIA_init: CIA_CACK_EN was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_CFG; mb();
		printk("CIA_init: CIA_CFG was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_CIA_DIAG; mb();
		printk("CIA_init: CIA_DIAG was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_DIAG_CHECK; mb();
		printk("CIA_init: CIA_DIAG_CHECK was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_PERF_MONITOR; mb();
		printk("CIA_init: CIA_PERF_MONITOR was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_PERF_CONTROL; mb();
		printk("CIA_init: CIA_PERF_CONTROL was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_CIA_ERR; mb();
		printk("CIA_init: CIA_ERR was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_CIA_STAT; mb();
		printk("CIA_init: CIA_STAT was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_MCR; mb();
		printk("CIA_init: CIA_MCR was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_CIA_CTRL; mb();
		printk("CIA_init: CIA_CTRL was 0x%x\n", temp);
		temp = *(vuip)CIA_IOC_ERR_MASK; mb();
		printk("CIA_init: CIA_ERR_MASK was 0x%x\n", temp);
		temp = *((vuip)CIA_IOC_PCI_W0_BASE); mb();
		printk("CIA_init: W0_BASE was 0x%x\n", temp);
		temp = *((vuip)CIA_IOC_PCI_W1_BASE); mb();
		printk("CIA_init: W1_BASE was 0x%x\n", temp);
		temp = *((vuip)CIA_IOC_PCI_W2_BASE); mb();
		printk("CIA_init: W2_BASE was 0x%x\n", temp);
		temp = *((vuip)CIA_IOC_PCI_W3_BASE); mb();
		printk("CIA_init: W3_BASE was 0x%x\n", temp);
	}
#endif /* DEBUG_DUMP_REGS */

        /* 
	 * Set up error reporting.
	 */
	cia_tmp = *(vuip)CIA_IOC_CIA_ERR;
	cia_tmp |= 0x180;   /* master, target abort */
	*(vuip)CIA_IOC_CIA_ERR = cia_tmp;
	mb();

	cia_tmp = *(vuip)CIA_IOC_CIA_CTRL;
	cia_tmp |= 0x400;   /* turn on FILL_ERR to get mchecks */
	*(vuip)CIA_IOC_CIA_CTRL = cia_tmp;
	mb();

#ifdef CONFIG_ALPHA_SRM_SETUP
	/* check window 0 for enabled and mapped to 0 */
	if (((*(vuip)CIA_IOC_PCI_W0_BASE & 3) == 1) &&
	    (*(vuip)CIA_IOC_PCI_T0_BASE == 0))
	{
	  CIA_DMA_WIN_BASE = *(vuip)CIA_IOC_PCI_W0_BASE & 0xfff00000U;
	  CIA_DMA_WIN_SIZE = *(vuip)CIA_IOC_PCI_W0_MASK & 0xfff00000U;
	  CIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("cia_init: using Window 0 settings\n");
	  printk("cia_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)CIA_IOC_PCI_W0_BASE,
		 *(vuip)CIA_IOC_PCI_W0_MASK,
		 *(vuip)CIA_IOC_PCI_T0_BASE);
#endif
	}
	else  /* check window 1 for enabled and mapped to 0 */
	if (((*(vuip)CIA_IOC_PCI_W1_BASE & 3) == 1) &&
	    (*(vuip)CIA_IOC_PCI_T1_BASE == 0))
	{
	  CIA_DMA_WIN_BASE = *(vuip)CIA_IOC_PCI_W1_BASE & 0xfff00000U;
	  CIA_DMA_WIN_SIZE = *(vuip)CIA_IOC_PCI_W1_MASK & 0xfff00000U;
	  CIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("cia_init: using Window 1 settings\n");
	  printk("cia_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)CIA_IOC_PCI_W1_BASE,
		 *(vuip)CIA_IOC_PCI_W1_MASK,
		 *(vuip)CIA_IOC_PCI_T1_BASE);
#endif
	}
	else  /* check window 2 for enabled and mapped to 0 */
	if (((*(vuip)CIA_IOC_PCI_W2_BASE & 3) == 1) &&
	    (*(vuip)CIA_IOC_PCI_T2_BASE == 0))
	{
	  CIA_DMA_WIN_BASE = *(vuip)CIA_IOC_PCI_W2_BASE & 0xfff00000U;
	  CIA_DMA_WIN_SIZE = *(vuip)CIA_IOC_PCI_W2_MASK & 0xfff00000U;
	  CIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("cia_init: using Window 2 settings\n");
	  printk("cia_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)CIA_IOC_PCI_W2_BASE,
		 *(vuip)CIA_IOC_PCI_W2_MASK,
		 *(vuip)CIA_IOC_PCI_T2_BASE);
#endif
	}
	else  /* check window 3 for enabled and mapped to 0 */
	if (((*(vuip)CIA_IOC_PCI_W3_BASE & 3) == 1) &&
	    (*(vuip)CIA_IOC_PCI_T3_BASE == 0))
	{
	  CIA_DMA_WIN_BASE = *(vuip)CIA_IOC_PCI_W3_BASE & 0xfff00000U;
	  CIA_DMA_WIN_SIZE = *(vuip)CIA_IOC_PCI_W3_MASK & 0xfff00000U;
	  CIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("cia_init: using Window 3 settings\n");
	  printk("cia_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)CIA_IOC_PCI_W3_BASE,
		 *(vuip)CIA_IOC_PCI_W3_MASK,
		 *(vuip)CIA_IOC_PCI_T3_BASE);
#endif
	}
	else  /* we must use our defaults which were pre-initialized... */
#endif /* SRM_SETUP */
	{
	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, windows 1,2 and 3 are disabled.  In the future, we may
	 * want to use them to do scatter/gather DMA.  Window 0
	 * goes at 1 GB and is 1 GB large.
	 */

	*(vuip)CIA_IOC_PCI_W0_BASE = 1U | (CIA_DMA_WIN_BASE & 0xfff00000U);
 	*(vuip)CIA_IOC_PCI_W0_MASK = (CIA_DMA_WIN_SIZE - 1) & 0xfff00000U;
	*(vuip)CIA_IOC_PCI_T0_BASE = 0;

	*(vuip)CIA_IOC_PCI_W1_BASE = 0x0;
	*(vuip)CIA_IOC_PCI_W2_BASE = 0x0;
	*(vuip)CIA_IOC_PCI_W3_BASE = 0x0;
	}

	/*
	 * check ASN in HWRPB for validity, report if bad
	 */
	if (hwrpb->max_asn != MAX_ASN) {
		printk("CIA_init: max ASN from HWRPB is bad (0x%lx)\n",
		       hwrpb->max_asn);
		hwrpb->max_asn = MAX_ASN;
	}

        /*
         * Next, clear the CIA_CFG register, which gets used
         *  for PCI Config Space accesses. That is the way
         *  we want to use it, and we do not want to depend on
         *  what ARC or SRM might have left behind...
         */
        {
          unsigned int cia_cfg = *((vuip)CIA_IOC_CFG); mb();
          if (cia_cfg) {
	      printk("CIA_init: CFG was 0x%x\n", cia_cfg);
	      *((vuip)CIA_IOC_CFG) = 0; mb();
	  }
        }
 
	{
          unsigned int cia_hae_mem = *((vuip)CIA_IOC_HAE_MEM);
          unsigned int cia_hae_io = *((vuip)CIA_IOC_HAE_IO);
#if 0
	  printk("CIA_init: HAE_MEM was 0x%x\n", cia_hae_mem);
	  printk("CIA_init: HAE_IO was 0x%x\n", cia_hae_io);
#endif
#ifdef CONFIG_ALPHA_SRM_SETUP
	  /*
	    sigh... For the SRM setup, unless we know apriori what the HAE
	    contents will be, we need to setup the arbitrary region bases
	    so we can test against the range of addresses and tailor the
	    region chosen for the SPARSE memory access.

	    see include/asm-alpha/cia.h for the SPARSE mem read/write
	  */
	  cia_sm_base_r1 = (cia_hae_mem      ) & 0xe0000000UL; /* region 1 */
	  cia_sm_base_r2 = (cia_hae_mem << 16) & 0xf8000000UL; /* region 2 */
	  cia_sm_base_r3 = (cia_hae_mem << 24) & 0xfc000000UL; /* region 3 */

	  /*
	    Set the HAE cache, so that setup_arch() code
	    will use the SRM setting always. Our readb/writeb
	    code in cia.h expects never to have to change
	    the contents of the HAE.
	   */
	  hae.cache = cia_hae_mem;
#else /* SRM_SETUP */
	  *((vuip)CIA_IOC_HAE_MEM) = 0; mb();
	  cia_hae_mem = *((vuip)CIA_IOC_HAE_MEM);
	  *((vuip)CIA_IOC_HAE_IO) = 0; mb();
	  cia_hae_io = *((vuip)CIA_IOC_HAE_IO);
#endif /* SRM_SETUP */
        }
 
	return mem_start;
}

int cia_pci_clr_err(void)
{
	CIA_jd = *(vuip)CIA_IOC_CIA_ERR;
	DBGM(("CIA_pci_clr_err: CIA ERR after read 0x%x\n", CIA_jd));
	*(vuip)CIA_IOC_CIA_ERR = 0x0180;
	mb();
	return 0;
}

void cia_machine_check(unsigned long vector, unsigned long la_ptr,
		       struct pt_regs * regs)
{
	struct el_common *mchk_header;
	struct el_procdata *mchk_procdata;
	struct el_CIA_sysdata_mcheck *mchk_sysdata;
	unsigned long * ptr;
	const char * reason;
	char buf[128];
	long i;

	mchk_header = (struct el_common *)la_ptr;
	mchk_procdata = (struct el_procdata *)
		(la_ptr + mchk_header->proc_offset);
	mchk_sysdata = (struct el_CIA_sysdata_mcheck *)
		(la_ptr + mchk_header->sys_offset);

	DBGM(("cia_machine_check: vector=0x%lx la_ptr=0x%lx\n",
	      vector, la_ptr));
	DBGM(("                     pc=0x%lx size=0x%x procoffset=0x%x "
	      "sysoffset 0x%x\n", regs->pc, mchk_header->size,
	      mchk_header->proc_offset, mchk_header->sys_offset));
	DBGM(("cia_machine_check: expected %d DCSR 0x%lx PEAR 0x%lx\n",
	      CIA_mcheck_expected, mchk_sysdata->epic_dcsr,
	      mchk_sysdata->epic_pear));

#ifdef DEBUG_MCHECK
	{
		unsigned long *ptr;
		int i;

		ptr = (unsigned long *)la_ptr;
		for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
			printk(" +%lx %lx %lx\n", i*sizeof(long),
			       ptr[i], ptr[i+1]);
		}
	}
#endif

	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */
	mb();
	mb();  /* magic */
	if (CIA_mcheck_expected) {
		DBGM(("CIA machine check expected\n"));
		CIA_mcheck_expected = 0;
		CIA_mcheck_taken = 1;
		mb();
		mb();  /* magic */
		draina();
		cia_pci_clr_err();
		wrmces(0x7);
		mb();
		return;
	}

	switch ((unsigned int) mchk_header->code) {
	case MCHK_K_TPERR:	reason = "tag parity error"; break;
	case MCHK_K_TCPERR:	reason = "tag control parity error"; break;
	case MCHK_K_HERR:		reason = "generic hard error"; break;
	case MCHK_K_ECC_C:	reason = "correctable ECC error"; break;
	case MCHK_K_ECC_NC:	reason = "uncorrectable ECC error"; break;
	case MCHK_K_OS_BUGCHECK:	reason = "OS-specific PAL bugcheck"; break;
	case MCHK_K_PAL_BUGCHECK:	reason = "callsys in kernel mode"; break;
	case 0x96: reason = "i-cache read retryable error"; break;
	case 0x98: reason = "processor detected hard error"; break;

		/* system specific (these are for Alcor, at least): */
	case 0x203: reason = "system detected uncorrectable ECC error"; break;
	case 0x205: reason = "parity error detected by CIA"; break;
	case 0x207: reason = "non-existent memory error"; break;
	case 0x209: reason = "PCI SERR detected"; break;
	case 0x20b: reason = "PCI data parity error detected"; break;
	case 0x20d: reason = "PCI address parity error detected"; break;
	case 0x20f: reason = "PCI master abort error"; break;
	case 0x211: reason = "PCI target abort error"; break;
	case 0x213: reason = "scatter/gather PTE invalid error"; break;
	case 0x215: reason = "flash ROM write error"; break;
	case 0x217: reason = "IOA timeout detected"; break;
	case 0x219: reason = "IOCHK#, EISA add-in board parity or other catastrophic error"; break;
	case 0x21b: reason = "EISA fail-safe timer timeout"; break;
	case 0x21d: reason = "EISA bus time-out"; break;
	case 0x21f: reason = "EISA software generated NMI"; break;
	case 0x221: reason = "unexpected ev5 IRQ[3] interrupt"; break;
	default:
		sprintf(buf, "reason for machine-check unknown (0x%x)",
			(unsigned int) mchk_header->code);
		reason = buf;
		break;
	}
	wrmces(rdmces());	/* reset machine check pending flag */
	mb();

	printk(KERN_CRIT "  CIA machine check: %s%s\n",
	       reason, mchk_header->retry ? " (retryable)" : "");
	printk(KERN_CRIT "   vector=0x%lx la_ptr=0x%lx pc=0x%lx\n",
	       vector, la_ptr, regs->pc);

	/* dump the logout area to give all info: */

	ptr = (unsigned long *)la_ptr;
	for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
		printk(KERN_CRIT " +%8lx %016lx %016lx\n",
		       i*sizeof(long), ptr[i], ptr[i+1]);
	}
}
