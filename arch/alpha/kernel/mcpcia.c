/*
 * Code common to all MCbus-PCI adaptor chipsets
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
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
#include <asm/delay.h>

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

extern struct hwrpb_struct *hwrpb;
extern asmlinkage void wrmces(unsigned long mces);

/*
 * BIOS32-style PCI interface:
 */

#ifdef CONFIG_ALPHA_MCPCIA

#undef DEBUG_CFG

#ifdef DEBUG_CFG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif

#undef DEBUG_PCI

#ifdef DEBUG_PCI
# define DBG_PCI(args)	printk args
#else
# define DBG_PCI(args)
#endif

#define DEBUG_MCHECK

#ifdef DEBUG_MCHECK
# define DBG_MCK(args)	printk args
# define DEBUG_MCHECK_DUMP
#else
# define DBG_MCK(args)
#endif

#define vuip	volatile unsigned int  *
#define vulp	volatile unsigned long  *

static volatile unsigned int MCPCIA_mcheck_expected[NR_CPUS];
static volatile unsigned int MCPCIA_mcheck_taken[NR_CPUS];
static unsigned int MCPCIA_jd[NR_CPUS];

#define MCPCIA_MAX_HOSES 2
static int mcpcia_num_hoses = 0;

static int pci_probe_enabled = 0; /* disable to start */

static struct linux_hose_info *mcpcia_root = NULL, *mcpcia_last_hose;

struct linux_hose_info *bus2hose[256];

static inline unsigned long long_align(unsigned long addr)
{
	return ((addr + (sizeof(unsigned long) - 1)) &
		~(sizeof(unsigned long) - 1));
}

#ifdef CONFIG_ALPHA_SRM_SETUP
unsigned int MCPCIA_DMA_WIN_BASE = MCPCIA_DMA_WIN_BASE_DEFAULT;
unsigned int MCPCIA_DMA_WIN_SIZE = MCPCIA_DMA_WIN_SIZE_DEFAULT;
unsigned long mcpcia_sm_base_r1, mcpcia_sm_base_r2, mcpcia_sm_base_r3;
#endif /* SRM_SETUP */

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the MCPCIA_HAXR2 register
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

static unsigned int
conf_read(unsigned long addr, unsigned char type1,
	  struct linux_hose_info *hose)
{
	unsigned long flags;
	unsigned long hoseno = hose->pci_hose_index;
	unsigned int stat0, value, temp, cpu;

	cpu = smp_processor_id();

	save_and_cli(flags);

	DBG_CFG(("conf_read(addr=0x%lx, type1=%d, hose=%d)\n",
	     addr, type1, hoseno));

	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)MCPCIA_CAP_ERR(hoseno);
	*(vuip)MCPCIA_CAP_ERR(hoseno) = stat0; mb();
	temp = *(vuip)MCPCIA_CAP_ERR(hoseno);
	DBG_CFG(("conf_read: MCPCIA CAP_ERR(%d) was 0x%x\n", hoseno, stat0));

	mb();
	draina();
	MCPCIA_mcheck_expected[cpu] = 1;
	MCPCIA_mcheck_taken[cpu] = 0;
	mb();
	/* access configuration space: */
	value = *((vuip)addr);
	mb();
	mb();  /* magic */
	if (MCPCIA_mcheck_taken[cpu]) {
		MCPCIA_mcheck_taken[cpu] = 0;
		value = 0xffffffffU;
		mb();
	}
	MCPCIA_mcheck_expected[cpu] = 0;
	mb();

	DBG_CFG(("conf_read(): finished\n"));

	restore_flags(flags);
	return value;
}


static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1,
	   struct linux_hose_info *hose)
{
	unsigned long flags;
	unsigned long hoseno = hose->pci_hose_index;
	unsigned int stat0, temp, cpu;

	cpu = smp_processor_id();

	save_and_cli(flags);	/* avoid getting hit by machine check */

	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)MCPCIA_CAP_ERR(hoseno);
	*(vuip)MCPCIA_CAP_ERR(hoseno) = stat0; mb();
	temp = *(vuip)MCPCIA_CAP_ERR(hoseno);
	DBG_CFG(("conf_write: MCPCIA CAP_ERR(%d) was 0x%x\n", hoseno, stat0));

	draina();
	MCPCIA_mcheck_expected[cpu] = 1;
	mb();
	/* access configuration space: */
	*((vuip)addr) = value;
	mb();
	mb();  /* magic */
	temp = *(vuip)MCPCIA_CAP_ERR(hoseno); /* read to force the write */
	MCPCIA_mcheck_expected[cpu] = 0;
	mb();

	DBG_CFG(("conf_write(): finished\n"));
	restore_flags(flags);
}

static int mk_conf_addr(struct linux_hose_info *hose,
			unsigned char bus, unsigned char device_fn,
			unsigned char where, unsigned long *pci_addr,
			unsigned char *type1)
{
	unsigned long addr;

	if (!pci_probe_enabled) /* if doing standard pci_init(), ignore */
	    return -1;

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
	     " pci_addr=0x%p, type1=0x%p)\n",
	     bus, device_fn, where, pci_addr, type1));

	/* type 1 configuration cycle for *ALL* busses */
	*type1 = 1;

	if (hose->pci_first_busno == bus)
	    bus = 0;
	addr = (bus << 16) | (device_fn << 8) | (where);
	addr <<= 5; /* swizzle for SPARSE */
	addr |= hose->pci_config_space;

	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}


int hose_read_config_byte (struct linux_hose_info *hose,
			   unsigned char bus, unsigned char device_fn,
			   unsigned char where, unsigned char *value)
{
	unsigned long addr;
	unsigned char type1;

	*value = 0xff;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}

	addr |= 0x00; /* or in length */

	*value = conf_read(addr, type1, hose) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}


int hose_read_config_word (struct linux_hose_info *hose,
			   unsigned char bus, unsigned char device_fn,
			   unsigned char where, unsigned short *value)
{
	unsigned long addr;
	unsigned char type1;

	*value = 0xffff;

	if (where & 0x1) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1)) {
		return PCIBIOS_SUCCESSFUL;
	}

	addr |= 0x08; /* or in length */

	*value = conf_read(addr, type1, hose) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}


int hose_read_config_dword (struct linux_hose_info *hose,
			    unsigned char bus, unsigned char device_fn,
			    unsigned char where, unsigned int *value)
{
	unsigned long addr;
	unsigned char type1;

	*value = 0xffffffff;

	if (where & 0x3) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1)) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= 0x18; /* or in length */

	*value = conf_read(addr, type1, hose);
	return PCIBIOS_SUCCESSFUL;
}


int hose_write_config_byte (struct linux_hose_info *hose,
			    unsigned char bus, unsigned char device_fn,
			    unsigned char where, unsigned char value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}

	addr |= 0x00; /* or in length */

	conf_write(addr, value << ((where & 3) * 8), type1, hose);
	return PCIBIOS_SUCCESSFUL;
}


int hose_write_config_word (struct linux_hose_info *hose,
			    unsigned char bus, unsigned char device_fn,
			    unsigned char where, unsigned short value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}

	addr |= 0x08; /* or in length */

	conf_write(addr, value << ((where & 3) * 8), type1, hose);
	return PCIBIOS_SUCCESSFUL;
}


int hose_write_config_dword (struct linux_hose_info *hose,
			     unsigned char bus, unsigned char device_fn,
			     unsigned char where, unsigned int value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}

	addr |= 0x18; /* or in length */

	conf_write(addr, value << ((where & 3) * 8), type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_byte (unsigned char bus, unsigned char devfn,
			      unsigned char where, unsigned char *value)
{
	return hose_read_config_byte(bus2hose[bus], bus, devfn, where, value);
}

int pcibios_read_config_word (unsigned char bus, unsigned char devfn,
			      unsigned char where, unsigned short *value)
{
	return hose_read_config_word(bus2hose[bus], bus, devfn, where, value);
}

int pcibios_read_config_dword (unsigned char bus, unsigned char devfn,
			       unsigned char where, unsigned int *value)
{
	return hose_read_config_dword(bus2hose[bus], bus, devfn, where, value);
}

int pcibios_write_config_byte (unsigned char bus, unsigned char devfn,
			       unsigned char where, unsigned char value)
{
	return hose_write_config_byte(bus2hose[bus], bus, devfn, where, value);
}

int pcibios_write_config_word (unsigned char bus, unsigned char devfn,
			       unsigned char where, unsigned short value)
{
	return hose_write_config_word(bus2hose[bus], bus, devfn, where, value);
}

int pcibios_write_config_dword (unsigned char bus, unsigned char devfn,
			        unsigned char where, unsigned int value)
{
	return hose_write_config_dword(bus2hose[bus], bus, devfn, where, value);
}

unsigned long mcpcia_init(unsigned long mem_start, unsigned long mem_end)
{
    struct linux_hose_info *hose;
    unsigned int mcpcia_err;
    unsigned int pci_rev;
    int h;

    mem_start = long_align(mem_start);

    for (h = 0; h < NR_CPUS; h++) {
	MCPCIA_mcheck_expected[h] = 0;
	MCPCIA_mcheck_taken[h] = 0;
    }

    /* first, find how many hoses we have */
    for (h = 0; h < MCPCIA_MAX_HOSES; h++) {
	pci_rev = *(vuip)MCPCIA_REV(h);
#if 0
	printk("mcpcia_init: got 0x%x for PCI_REV for hose %d\n",
	       pci_rev, h);
#endif
	if ((pci_rev >> 16) == PCI_CLASS_BRIDGE_HOST) {
	    mcpcia_num_hoses++;

	    hose = (struct linux_hose_info *)mem_start;
	    mem_start = long_align(mem_start + sizeof(*hose));

	    memset(hose, 0, sizeof(*hose));

	    if (mcpcia_root)
	        mcpcia_last_hose->next = hose;
	    else
	        mcpcia_root = hose;
	    mcpcia_last_hose = hose;

	    hose->pci_io_space = MCPCIA_IO(h);
	    hose->pci_mem_space = MCPCIA_DENSE(h);
	    hose->pci_config_space = MCPCIA_CONF(h);
	    hose->pci_sparse_space = MCPCIA_SPARSE(h);
	    hose->pci_hose_index = h;
	    hose->pci_first_busno = 255;
	    hose->pci_last_busno = 0;
	}
    }

#if 1
    printk("mcpcia_init: found %d hoses\n", mcpcia_num_hoses);
#endif

    /* now do init for each hose */
    for (hose = mcpcia_root; hose; hose = hose->next) {
      h = hose->pci_hose_index;
#if 0
#define PRINTK printk
PRINTK("mcpcia_init: -------- hose %d --------\n",h);
PRINTK("mcpcia_init: MCPCIA_REV 0x%x\n", *(vuip)MCPCIA_REV(h));
PRINTK("mcpcia_init: MCPCIA_WHOAMI 0x%x\n", *(vuip)MCPCIA_WHOAMI(h));
PRINTK("mcpcia_init: MCPCIA_HAE_MEM 0x%x\n", *(vuip)MCPCIA_HAE_MEM(h));
PRINTK("mcpcia_init: MCPCIA_HAE_IO 0x%x\n", *(vuip)MCPCIA_HAE_IO(h));
PRINTK("mcpcia_init: MCPCIA_HAE_DENSE 0x%x\n", *(vuip)MCPCIA_HAE_DENSE(h));
PRINTK("mcpcia_init: MCPCIA_INT_CTL 0x%x\n", *(vuip)MCPCIA_INT_CTL(h));
PRINTK("mcpcia_init: MCPCIA_INT_REQ 0x%x\n", *(vuip)MCPCIA_INT_REQ(h));
PRINTK("mcpcia_init: MCPCIA_INT_TARG 0x%x\n", *(vuip)MCPCIA_INT_TARG(h));
PRINTK("mcpcia_init: MCPCIA_INT_ADR 0x%x\n", *(vuip)MCPCIA_INT_ADR(h));
PRINTK("mcpcia_init: MCPCIA_INT_ADR_EXT 0x%x\n", *(vuip)MCPCIA_INT_ADR_EXT(h));
PRINTK("mcpcia_init: MCPCIA_INT_MASK0 0x%x\n", *(vuip)MCPCIA_INT_MASK0(h));
PRINTK("mcpcia_init: MCPCIA_INT_MASK1 0x%x\n", *(vuip)MCPCIA_INT_MASK1(h));
PRINTK("mcpcia_init: MCPCIA_HBASE 0x%x\n", *(vuip)MCPCIA_HBASE(h));
#endif

        /* 
	 * Set up error reporting. Make sure CPU_PE is OFF in the mask.
	 */
#if 0
	mcpcia_err = *(vuip)MCPCIA_ERR_MASK(h);
	mcpcia_err &= ~4;   
	*(vuip)MCPCIA_ERR_MASK(h) = mcpcia_err;
	mb();
	mcpcia_err = *(vuip)MCPCIA_ERR_MASK;
#endif

	mcpcia_err = *(vuip)MCPCIA_CAP_ERR(h);
	mcpcia_err |= 0x0006;   /* master/target abort */
	*(vuip)MCPCIA_CAP_ERR(h) = mcpcia_err;
	mb() ;
	mcpcia_err = *(vuip)MCPCIA_CAP_ERR(h);

#ifdef CONFIG_ALPHA_SRM_SETUP
	/* check window 0 for enabled and mapped to 0 */
	if (((*(vuip)MCPCIA_W0_BASE(h) & 3) == 1) &&
	    (*(vuip)MCPCIA_T0_BASE(h) == 0) &&
	    ((*(vuip)MCPCIA_W0_MASK(h) & 0xfff00000U) > 0x0ff00000U))
	{
	  MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W0_BASE(h) & 0xfff00000U;
	  MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W0_MASK(h) & 0xfff00000U;
	  MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("mcpcia_init: using Window 0 settings\n");
	  printk("mcpcia_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)MCPCIA_W0_BASE(h),
		 *(vuip)MCPCIA_W0_MASK(h),
		 *(vuip)MCPCIA_T0_BASE(h));
#endif
	}
	else  /* check window 1 for enabled and mapped to 0 */
	if (((*(vuip)MCPCIA_W1_BASE(h) & 3) == 1) &&
	    (*(vuip)MCPCIA_T1_BASE(h) == 0) &&
	    ((*(vuip)MCPCIA_W1_MASK(h) & 0xfff00000U) > 0x0ff00000U))
{
	  MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W1_BASE(h) & 0xfff00000U;
	  MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W1_MASK(h) & 0xfff00000U;
	  MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("mcpcia_init: using Window 1 settings\n");
	  printk("mcpcia_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)MCPCIA_W1_BASE(h),
		 *(vuip)MCPCIA_W1_MASK(h),
		 *(vuip)MCPCIA_T1_BASE(h));
#endif
	}
	else  /* check window 2 for enabled and mapped to 0 */
	if (((*(vuip)MCPCIA_W2_BASE(h) & 3) == 1) &&
	    (*(vuip)MCPCIA_T2_BASE(h) == 0) &&
	    ((*(vuip)MCPCIA_W2_MASK(h) & 0xfff00000U) > 0x0ff00000U))
	{
	  MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W2_BASE(h) & 0xfff00000U;
	  MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W2_MASK(h) & 0xfff00000U;
	  MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("mcpcia_init: using Window 2 settings\n");
	  printk("mcpcia_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)MCPCIA_W2_BASE(h),
		 *(vuip)MCPCIA_W2_MASK(h),
		 *(vuip)MCPCIA_T2_BASE(h));
#endif
	}
	else  /* check window 3 for enabled and mapped to 0 */
	if (((*(vuip)MCPCIA_W3_BASE(h) & 3) == 1) &&
	    (*(vuip)MCPCIA_T3_BASE(h) == 0) &&
	    ((*(vuip)MCPCIA_W3_MASK(h) & 0xfff00000U) > 0x0ff00000U))
	{
	  MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W3_BASE(h) & 0xfff00000U;
	  MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W3_MASK(h) & 0xfff00000U;
	  MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
	  printk("mcpcia_init: using Window 3 settings\n");
	  printk("mcpcia_init: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
		 *(vuip)MCPCIA_W3_BASE(h),
		 *(vuip)MCPCIA_W3_MASK(h),
		 *(vuip)MCPCIA_T3_BASE(h));
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

	*(vuip)MCPCIA_W0_BASE(h) = 1U | (MCPCIA_DMA_WIN_BASE & 0xfff00000U);
 	*(vuip)MCPCIA_W0_MASK(h) = (MCPCIA_DMA_WIN_SIZE - 1) & 0xfff00000U;
	*(vuip)MCPCIA_T0_BASE(h) = 0;

	*(vuip)MCPCIA_W1_BASE(h) = 0x0 ;
	*(vuip)MCPCIA_W2_BASE(h) = 0x0 ;
	*(vuip)MCPCIA_W3_BASE(h) = 0x0 ;

	*(vuip)MCPCIA_HBASE(h) = 0x0 ;
	mb();
	}

	/*
	 * check ASN in HWRPB for validity, report if bad
	 */
	if (hwrpb->max_asn != MAX_ASN) {
		printk("mcpcia_init: max ASN from HWRPB is bad (0x%lx)\n",
			hwrpb->max_asn);
		hwrpb->max_asn = MAX_ASN;
	}

#if 0
        {
          unsigned int mcpcia_int_ctl = *((vuip)MCPCIA_INT_CTL(h));
	  printk("mcpcia_init: INT_CTL was 0x%x\n", mcpcia_int_ctl);
          *(vuip)MCPCIA_INT_CTL(h) = 1U; mb();
	  mcpcia_int_ctl = *(vuip)MCPCIA_INT_CTL(h);
        }
#endif

        {
          unsigned int mcpcia_hae_mem = *(vuip)MCPCIA_HAE_MEM(h);
          unsigned int mcpcia_hae_io = *(vuip)MCPCIA_HAE_IO(h);
#if 0
	  printk("mcpcia_init: HAE_MEM was 0x%x\n", mcpcia_hae_mem);
	  printk("mcpcia_init: HAE_IO was 0x%x\n", mcpcia_hae_io);
#endif
#ifdef CONFIG_ALPHA_SRM_SETUP
	  /*
	    sigh... For the SRM setup, unless we know apriori what the HAE
	    contents will be, we need to setup the arbitrary region bases
	    so we can test against the range of addresses and tailor the
	    region chosen for the SPARSE memory access.

	    see include/asm-alpha/mcpcia.h for the SPARSE mem read/write
	  */
	  mcpcia_sm_base_r1 = (mcpcia_hae_mem      ) & 0xe0000000UL;/* reg 1 */
	  mcpcia_sm_base_r2 = (mcpcia_hae_mem << 16) & 0xf8000000UL;/* reg 2 */
	  mcpcia_sm_base_r3 = (mcpcia_hae_mem << 24) & 0xfc000000UL;/* reg 3 */
	  /*
	    Set the HAE cache, so that setup_arch() code
	    will use the SRM setting always. Our readb/writeb
	    code in mcpcia.h expects never to have to change
	    the contents of the HAE.
	   */
	  hae.cache = mcpcia_hae_mem;
#else /* SRM_SETUP */
          *(vuip)MCPCIA_HAE_MEM(h) = 0U; mb();
	  mcpcia_hae_mem = *(vuip)MCPCIA_HAE_MEM(h);
          *(vuip)MCPCIA_HAE_IO(h) = 0; mb();
	  mcpcia_hae_io = *(vuip)MCPCIA_HAE_IO(h);
#endif /* SRM_SETUP */
        }
    } /* end for-loop on hoses */
    return mem_start;
}

int mcpcia_pci_clr_err(int h)
{
	unsigned int cpu = smp_processor_id();

	MCPCIA_jd[cpu] = *(vuip)MCPCIA_CAP_ERR(h);
#if 0
	DBG_MCK(("MCPCIA_pci_clr_err: MCPCIA CAP_ERR(%d) after read 0x%x\n",
	     h, MCPCIA_jd[cpu]));
#endif
	*(vuip)MCPCIA_CAP_ERR(h) = 0xffffffff; mb(); /* clear them all */
	MCPCIA_jd[cpu] = *(vuip)MCPCIA_CAP_ERR(h);
	return 0;
}

static void
mcpcia_print_uncorrectable(struct el_MCPCIA_uncorrected_frame_mcheck *logout)
{
  struct el_common_EV5_uncorrectable_mcheck *frame;
  int i;

  frame = &logout->procdata;

  /* Print PAL fields */
  for (i = 0; i < 24; i += 2) {
    printk("\tpal temp[%d-%d]\t\t= %16lx %16lx\n\r",
	   i, i+1, frame->paltemp[i], frame->paltemp[i+1]);
  }
  for (i = 0; i < 8; i += 2) {
    printk("\tshadow[%d-%d]\t\t= %16lx %16lx\n\r",
	   i, i+1, frame->shadow[i], 
	   frame->shadow[i+1]);
  }
  printk("\tAddr of excepting instruction\t= %16lx\n\r",
	 frame->exc_addr);
  printk("\tSummary of arithmetic traps\t= %16lx\n\r",
	 frame->exc_sum);
  printk("\tException mask\t\t\t= %16lx\n\r",
	 frame->exc_mask);
  printk("\tBase address for PALcode\t= %16lx\n\r",
	 frame->pal_base);
  printk("\tInterrupt Status Reg\t\t= %16lx\n\r",
	 frame->isr);
  printk("\tCURRENT SETUP OF EV5 IBOX\t= %16lx\n\r",
	 frame->icsr);
  printk("\tI-CACHE Reg %s parity error\t= %16lx\n\r",
	 (frame->ic_perr_stat & 0x800L) ? 
	 "Data" : "Tag", 
	 frame->ic_perr_stat); 
  printk("\tD-CACHE error Reg\t\t= %16lx\n\r",
	 frame->dc_perr_stat);
  if (frame->dc_perr_stat & 0x2) {
    switch (frame->dc_perr_stat & 0x03c) {
    case 8:
      printk("\t\tData error in bank 1\n\r");
      break;
    case 4:
      printk("\t\tData error in bank 0\n\r");
      break;
    case 20:
      printk("\t\tTag error in bank 1\n\r");
      break;
    case 10:
      printk("\t\tTag error in bank 0\n\r");
      break;
    }
  }
  printk("\tEffective VA\t\t\t= %16lx\n\r",
	 frame->va);
  printk("\tReason for D-stream\t\t= %16lx\n\r",
	 frame->mm_stat);
  printk("\tEV5 SCache address\t\t= %16lx\n\r",
	 frame->sc_addr);
  printk("\tEV5 SCache TAG/Data parity\t= %16lx\n\r",
	 frame->sc_stat);
  printk("\tEV5 BC_TAG_ADDR\t\t\t= %16lx\n\r",
	 frame->bc_tag_addr);
  printk("\tEV5 EI_ADDR: Phys addr of Xfer\t= %16lx\n\r",
	 frame->ei_addr);
  printk("\tFill Syndrome\t\t\t= %16lx\n\r",
	 frame->fill_syndrome);
  printk("\tEI_STAT reg\t\t\t= %16lx\n\r",
	 frame->ei_stat);
  printk("\tLD_LOCK\t\t\t\t= %16lx\n\r",
	 frame->ld_lock);
}

void mcpcia_machine_check(unsigned long type, unsigned long la_ptr,
			 struct pt_regs * regs)
{
#if 0
        printk("mcpcia machine check ignored\n") ;
#else
	struct el_common *mchk_header;
	struct el_MCPCIA_uncorrected_frame_mcheck *mchk_logout;
	unsigned int cpu = smp_processor_id();
	int h = 0;

	mchk_header = (struct el_common *)la_ptr;
	mchk_logout = (struct el_MCPCIA_uncorrected_frame_mcheck *)la_ptr;

#if 0
	DBG_MCK(("mcpcia_machine_check: type=0x%lx la_ptr=0x%lx\n",
	     type, la_ptr));
	DBG_MCK(("\t\t pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
	     regs->pc, mchk_header->size, mchk_header->proc_offset,
	     mchk_header->sys_offset));
#endif
	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */
	mb();
	mb();  /* magic */
	if (MCPCIA_mcheck_expected[cpu]) {
#if 0
		DBG_MCK(("MCPCIA machine check expected\n"));
#endif
		MCPCIA_mcheck_expected[cpu] = 0;
		MCPCIA_mcheck_taken[cpu] = 1;
		mb();
		mb();  /* magic */
		draina();
		mcpcia_pci_clr_err(h);
		wrmces(0x7);
		mb();
	}
#if 1
	else {
		printk("MCPCIA machine check NOT expected on CPU %d\n", cpu);
		DBG_MCK(("mcpcia_machine_check: type=0x%lx pc=0x%lx"
			 " code=0x%lx\n",
			 type, regs->pc, mchk_header->code));

		MCPCIA_mcheck_expected[cpu] = 0;
		MCPCIA_mcheck_taken[cpu] = 1;
		mb();
		mb();  /* magic */
		draina();
		mcpcia_pci_clr_err(h);
		wrmces(0x7);
		mb();
#ifdef DEBUG_MCHECK_DUMP
		if (type == 0x620)
		  printk("MCPCIA machine check: system CORRECTABLE!\n");
		else if (type == 0x630)
		  printk("MCPCIA machine check: processor CORRECTABLE!\n");
		else
		  mcpcia_print_uncorrectable(mchk_logout);
#endif /* DEBUG_MCHECK_DUMP */
	}
#endif
#endif
}

/*==========================================================================*/

#define PRIMARY(b) ((b)&0xff)
#define SECONDARY(b) (((b)>>8)&0xff)
#define SUBORDINATE(b) (((b)>>16)&0xff)

static int
hose_scan_bridges(struct linux_hose_info *hose, unsigned char bus)
{
	unsigned int devfn, l, class;
	unsigned char hdr_type = 0;
	unsigned int found = 0;

	for (devfn = 0; devfn < 0xff; ++devfn) {
		if (PCI_FUNC(devfn) == 0) {
			hose_read_config_byte(hose, bus, devfn,
					      PCI_HEADER_TYPE, &hdr_type);
		} else if (!(hdr_type & 0x80)) {
			/* not a multi-function device */
			continue;
		}

		/* Check if there is anything here. */
		hose_read_config_dword(hose, bus, devfn, PCI_VENDOR_ID, &l);
		if (l == 0xffffffff || l == 0x00000000) {
			hdr_type = 0;
			continue;
		}

		/* See if this is a bridge device. */
		hose_read_config_dword(hose, bus, devfn,
				       PCI_CLASS_REVISION, &class);

		if ((class >> 16) == PCI_CLASS_BRIDGE_PCI) {
			unsigned int busses;

			found++;

			hose_read_config_dword(hose, bus, devfn,
					       PCI_PRIMARY_BUS, &busses);

DBG_PCI(("hose_scan_bridges: hose %d bus %d slot %d busses 0x%x\n",
       hose->pci_hose_index, bus, PCI_SLOT(devfn), busses));
			/*
			 * do something with first_busno and last_busno
			 */
			if (hose->pci_first_busno > PRIMARY(busses)) {
				hose->pci_first_busno = PRIMARY(busses);
DBG_PCI(("hose_scan_bridges: hose %d bus %d slot %d change first to %d\n",
       hose->pci_hose_index, bus, PCI_SLOT(devfn), PRIMARY(busses)));
			}
			if (hose->pci_last_busno < SUBORDINATE(busses)) {
				hose->pci_last_busno = SUBORDINATE(busses);
DBG_PCI(("hose_scan_bridges: hose %d bus %d slot %d change last to %d\n",
       hose->pci_hose_index, bus, PCI_SLOT(devfn), SUBORDINATE(busses)));
			}
			/*
			 * Now scan everything underneath the bridge.
			 */
			hose_scan_bridges(hose, SECONDARY(busses));
		}
	}
	return found;
}

static void
hose_reconfigure_bridges(struct linux_hose_info *hose, unsigned char bus)
{
	unsigned int devfn, l, class;
	unsigned char hdr_type = 0;

	for (devfn = 0; devfn < 0xff; ++devfn) {
		if (PCI_FUNC(devfn) == 0) {
			hose_read_config_byte(hose, bus, devfn,
					      PCI_HEADER_TYPE, &hdr_type);
		} else if (!(hdr_type & 0x80)) {
			/* not a multi-function device */
			continue;
		}

		/* Check if there is anything here. */
		hose_read_config_dword(hose, bus, devfn, PCI_VENDOR_ID, &l);
		if (l == 0xffffffff || l == 0x00000000) {
			hdr_type = 0;
			continue;
		}

		/* See if this is a bridge device. */
		hose_read_config_dword(hose, bus, devfn,
				       PCI_CLASS_REVISION, &class);

		if ((class >> 16) == PCI_CLASS_BRIDGE_PCI) {
			unsigned int busses;

			hose_read_config_dword(hose, bus, devfn,
					       PCI_PRIMARY_BUS, &busses);

			/*
			 * First reconfigure everything underneath the bridge.
			 */
			hose_reconfigure_bridges(hose, (busses >> 8) & 0xff);

			/*
			 * Unconfigure this bridges bus numbers,
			 * pci_scan_bus() will fix this up properly.
			 */
			busses &= 0xff000000;
			hose_write_config_dword(hose, bus, devfn,
						PCI_PRIMARY_BUS, busses);
		}
	}
}

static void mcpcia_fixup_busno(struct linux_hose_info *hose, unsigned char bus)
{
	unsigned int nbus;

	/*
	 * First, scan for all bridge devices underneath this hose,
	 * to determine the first and last busnos.
	 */
	if (!hose_scan_bridges(hose, 0)) {
		/* none found, exit */
		hose->pci_first_busno = bus;
		hose->pci_last_busno = bus;
	} else {
		/*
		 * Reconfigure all bridge devices underneath this hose.
		 */
		hose_reconfigure_bridges(hose, hose->pci_first_busno);
	}

	/*
	 * Now reconfigure the hose to it's new bus number and set up
	 * our bus2hose mapping for this hose.
	 */
	nbus = hose->pci_last_busno - hose->pci_first_busno;

	hose->pci_first_busno = bus;

DBG_PCI(("mcpcia_fixup_busno: hose %d startbus %d nbus %d\n",
       hose->pci_hose_index, bus, nbus));

	do {
		bus2hose[bus++] = hose;
	} while (nbus-- > 0);
}

static void mcpcia_probe(struct linux_hose_info *hose,
			 unsigned long *mem_start)
{
	static struct pci_bus *pchain = NULL;
	struct pci_bus *pbus = &hose->pci_bus;
	static unsigned char busno = 0;

	/* Hoses include child PCI bridges in bus-range property,
	 * but we don't scan each of those ourselves, Linux generic PCI
	 * probing code will find child bridges and link them into this
	 * hose's root PCI device hierarchy.
	 */

	pbus->number = pbus->secondary = busno;
	pbus->sysdata = hose;

	mcpcia_fixup_busno(hose, busno);

	pbus->subordinate = pci_scan_bus(pbus, mem_start); /* the original! */

	/*
	 * Set the maximum subordinate bus of this hose.
	 */
	hose->pci_last_busno = pbus->subordinate;
#if 0
	hose_write_config_byte(hose, busno, 0, 0x41, hose->pci_last_busno);
#endif
	busno = pbus->subordinate + 1;

	/*
	 * Fixup the chain of primary PCI busses.
	 */
	if (pchain) {
		pchain->next = &hose->pci_bus;
		pchain = pchain->next;
	} else {
		pchain = &pci_root;
		memcpy(pchain, &hose->pci_bus, sizeof(pci_root));
	}
}

void mcpcia_fixup(void)
{
	struct linux_hose_info *hose;

	/* turn on Config space access finally! */
	pci_probe_enabled = 1;

	/* for each hose, probe and setup the devices on the hose */
	for (hose = mcpcia_root; hose; hose = hose->next) {
		mcpcia_probe(hose, &memory_start);
	}
}
#endif /* CONFIG_ALPHA_MCPCIA */
