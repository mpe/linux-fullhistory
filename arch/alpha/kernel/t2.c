/*
 * Code common to all T2 chips.
 *
 * Written by Jay A Estabrook (jestabro@amt.tay1.dec.com).
 * December 1996.
 *
 * based on CIA code by David A Rusling (david.rusling@reo.mts.dec.com)
 *
 */
#include <linux/kernel.h>
#include <linux/config.h>
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
extern asmlinkage unsigned long whami(void);
extern int alpha_sys_type;

#define CPUID whami()


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

#ifdef DEBUG_CONF
# define DBG(args)	printk args
#else
# define DBG(args)
#endif

#ifdef DEBUG_MCHECK
# define DBGMC(args)	printk args
#else
# define DBGMC(args)
#endif

#define vulp	volatile unsigned long *
#define vuip	volatile unsigned int  *

static volatile unsigned int T2_mcheck_expected = 0;
static volatile unsigned int T2_mcheck_taken = 0;
static unsigned long T2_jd;


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the T2_HAXR2 register
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

	DBG(("mk_conf_addr(bus=%d, dfn=0x%x, where=0x%x,"
	     " addr=0x%lx, type1=0x%x)\n",
	     bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		int device = device_fn >> 3;

		/* type 0 configuration cycle: */

		if (device > 8) {
			DBG(("mk_conf_addr: device (%d)>20, returning -1\n",
			     device));
			return -1;
		}

		*type1 = 0;
		addr = (0x0800L << device) | ((device_fn & 7) << 8) | (where);
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
	unsigned int t2_cfg = 0; /* to keep gcc quiet */

	save_flags(flags);	/* avoid getting hit by machine check */
	cli();

	DBG(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

#if 0
	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)T2_IOCSR;
	*(vuip)T2_IOCSR = stat0;
	mb();
	DBG(("conf_read: T2 IOCSR was 0x%x\n", stat0));
	/* if Type1 access, must set T2 CFG */
	if (type1) {
		t2_cfg = *(vuip)T2_IOC_CFG;
		mb();
		*(vuip)T2_IOC_CFG = t2_cfg | 1;
		DBG(("conf_read: TYPE1 access\n"));
	}
	mb();
	draina();
#endif

	T2_mcheck_expected = 1;
	T2_mcheck_taken = 0;
	mb();
	/* access configuration space: */
	value = *(vuip)addr;
	mb();
	if (T2_mcheck_taken) {
		T2_mcheck_taken = 0;
		value = 0xffffffffU;
		mb();
	}
	T2_mcheck_expected = 0;
	mb();

#if 0
	/* if Type1 access, must reset IOC CFG so normal IO space ops work */
	if (type1) {
		*(vuip)T2_IOC_CFG = t2_cfg & ~1;
		mb();
	}
#endif
	DBG(("conf_read(): finished\n"));

	restore_flags(flags);
	return value;
}


static void conf_write(unsigned long addr, unsigned int value,
		       unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0;
	unsigned int t2_cfg = 0; /* to keep gcc quiet */

	save_flags(flags);	/* avoid getting hit by machine check */
	cli();

#if 0
	/* reset status register to avoid losing errors: */
	stat0 = *(vuip)T2_IOCSR;
	*(vuip)T2_IOCSR = stat0;
	mb();
	DBG(("conf_write: T2 ERR was 0x%x\n", stat0));
	/* if Type1 access, must set T2 CFG */
	if (type1) {
		t2_cfg = *(vuip)T2_IOC_CFG;
		mb();
		*(vuip)T2_IOC_CFG = t2_cfg | 1;
		DBG(("conf_write: TYPE1 access\n"));
	}
	draina();
#endif

	T2_mcheck_expected = 1;
	mb();
	/* access configuration space: */
	*(vuip)addr = value;
	mb();
	T2_mcheck_expected = 0;
	mb();

#if 0
	/* if Type1 access, must reset IOC CFG so normal IO space ops work */
	if (type1) {
		*(vuip)T2_IOC_CFG = t2_cfg & ~1;
		mb();
	}
#endif
	DBG(("conf_write(): finished\n"));
	restore_flags(flags);
}


int pcibios_read_config_byte (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned char *value)
{
	unsigned long addr = T2_CONF;
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
	unsigned long addr = T2_CONF;
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
	unsigned long addr = T2_CONF;
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
	unsigned long addr = T2_CONF;
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
	unsigned long addr = T2_CONF;
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
	unsigned long addr = T2_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x18;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}


unsigned long t2_init(unsigned long mem_start, unsigned long mem_end)
{
	unsigned int t2_err;
	struct percpu_struct *cpu;
	int i;

#if 0
	/* 
	 * Set up error reporting.
	 */
	t2_err = *(vuip)T2_IOCSR ;
	t2_err |= (0x1 << 7) ;   /* master abort */
	*(vuip)T2_IOC_T2_ERR = t2_err ;
	mb() ;
#endif

	printk("t2_init: HBASE was 0x%lx\n", *(vulp)T2_HBASE);
#if 0
	printk("t2_init: WBASE1=0x%lx WMASK1=0x%lx TBASE1=0x%lx\n",
	       *(vulp)T2_WBASE1,
	       *(vulp)T2_WMASK1,
	       *(vulp)T2_TBASE1);
	printk("t2_init: WBASE2=0x%lx WMASK2=0x%lx TBASE2=0x%lx\n",
	       *(vulp)T2_WBASE2,
	       *(vulp)T2_WMASK2,
	       *(vulp)T2_TBASE2);
#endif

	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, window 2 is  disabled.  In the future, we may
	 * want to use it to do scatter/gather DMA.  Window 1
	 * goes at 1 GB and is 1 GB large.
	 */

	/* WARNING!! must correspond to the DMA_WIN params!!! */
	*(vuip)T2_WBASE1 = 0x400807ffU;
	*(vuip)T2_WMASK1 = 0x3ff00000U;
	*(vuip)T2_TBASE1 = 0;

	*(vuip)T2_WBASE2 = 0x0;

	*(vuip)T2_HBASE = 0x0;

	/*
	 * check ASN in HWRPB for validity, report if bad
	 */
	if (hwrpb->max_asn != MAX_ASN) {
		printk("T2_init: max ASN from HWRPB is bad (0x%lx)\n",
		       hwrpb->max_asn);
		hwrpb->max_asn = MAX_ASN;
	}

	/*
	 * Finally, clear the T2_HAE_3 register, which gets used
	 *  for PCI Config Space accesses. That is the way
	 *  we want to use it, and we do not want to depend on
	 *  what ARC or SRM might have left behind...
	 */
	{
#if 0
		printk("T2_init: HAE1 was 0x%lx\n", *(vulp)T2_HAE_1);
		printk("T2_init: HAE2 was 0x%lx\n", *(vulp)T2_HAE_2);
		printk("T2_init: HAE3 was 0x%lx\n", *(vulp)T2_HAE_3);
		printk("T2_init: HAE4 was 0x%lx\n", *(vulp)T2_HAE_4);
#endif
#if 0
		*(vuip)T2_HAE_1 = 0; mb();
		*(vuip)T2_HAE_2 = 0; mb();
		*(vuip)T2_HAE_3 = 0; mb();
		*(vuip)T2_HAE_4 = 0; mb();
#endif
	}
 
#if 1
	if (hwrpb->nr_processors > 1) {
		printk("T2_init: nr_processors 0x%lx\n",
		       hwrpb->nr_processors);
		printk("T2_init: processor_size 0x%lx\n",
		       hwrpb->processor_size);
		printk("T2_init: processor_offset 0x%lx\n",
		       hwrpb->processor_offset);

		cpu = (struct percpu_struct *)
			((char*)hwrpb + hwrpb->processor_offset);

		for (i = 0; i < hwrpb->nr_processors; i++ ) {
			printk("T2_init: CPU 0x%x: flags 0x%lx type 0x%lx\n",
			       i, cpu->flags, cpu->type);
			cpu = (struct percpu_struct *)
				((char *)cpu + hwrpb->processor_size);
		}
	}
#endif

	return mem_start;
}

#define SIC_SEIC (1UL << 33)    /* System Event Clear */

static struct sable_cpu_csr *sable_cpu_regs[4] = {
	(struct sable_cpu_csr *)CPU0_BASE,
	(struct sable_cpu_csr *)CPU1_BASE,
	(struct sable_cpu_csr *)CPU2_BASE,
	(struct sable_cpu_csr *)CPU3_BASE,
};

int t2_clear_errors(void)
{
	DBGMC(("???????? t2_clear_errors\n"));

	sable_cpu_regs[CPUID]->sic &= ~SIC_SEIC;

	/* 
	 * clear cpu errors
	 */
	sable_cpu_regs[CPUID]->bcce |= sable_cpu_regs[CPUID]->bcce;
	sable_cpu_regs[CPUID]->cbe  |= sable_cpu_regs[CPUID]->cbe;
	sable_cpu_regs[CPUID]->bcue |= sable_cpu_regs[CPUID]->bcue;
	sable_cpu_regs[CPUID]->dter |= sable_cpu_regs[CPUID]->dter;

	*(vulp)T2_CERR1 |= *(vulp)T2_CERR1;
	*(vulp)T2_PERR1 |= *(vulp)T2_PERR1;

	mb();
	return 0;
}

void t2_machine_check(unsigned long vector, unsigned long la_ptr,
		      struct pt_regs * regs)
{
	struct el_t2_logout_header *mchk_header;
	struct el_t2_procdata_mcheck *mchk_procdata;
	struct el_t2_sysdata_mcheck *mchk_sysdata;
	unsigned long * ptr;
	const char * reason;
	char buf[128];
	long i;

	DBGMC(("t2_machine_check: vector=0x%lx la_ptr=0x%lx\n",
	       vector, la_ptr));

	mchk_header = (struct el_t2_logout_header *)la_ptr;

	DBGMC(("t2_machine_check: susoffset=0x%lx procoffset=0x%lx\n",
	       mchk_header->elfl_sysoffset, mchk_header->elfl_procoffset));

	mchk_sysdata = (struct el_t2_sysdata_mcheck *)
	  (la_ptr + mchk_header->elfl_sysoffset);
	mchk_procdata = (struct el_t2_procdata_mcheck *)
	  (la_ptr + mchk_header->elfl_procoffset - sizeof(unsigned long)*32);

	DBGMC(("         pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
	       regs->pc, mchk_header->elfl_size, mchk_header->elfl_procoffset,
	       mchk_header->elfl_sysoffset));
	DBGMC(("t2_machine_check: expected %d\n", T2_mcheck_expected));

#ifdef DEBUG_DUMP
	{
		unsigned long *ptr;
		int i;

		ptr = (unsigned long *)la_ptr;
		for (i = 0; i < mchk_header->elfl_size/sizeof(long); i += 2) {
			printk(" +%lx %lx %lx\n", i*sizeof(long),
			       ptr[i], ptr[i+1]);
		}
	}
#endif /* DEBUG_DUMP */

	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */
	mb();
	if (T2_mcheck_expected/* && (mchk_sysdata->epic_dcsr && 0x0c00UL)*/) {
		DBGMC(("T2 machine check expected\n"));
		T2_mcheck_taken = 1;
		t2_clear_errors();
		T2_mcheck_expected = 0;
		mb();
		wrmces(rdmces()|1);/* ??? */
		draina();
		return;
	}

	switch ((unsigned int) mchk_header->elfl_error_type) {
	case MCHK_K_TPERR:	reason = "tag parity error"; break;
	case MCHK_K_TCPERR:	reason = "tag control parity error"; break;
	case MCHK_K_HERR:	reason = "generic hard error"; break;
	case MCHK_K_ECC_C:	reason = "correctable ECC error"; break;
	case MCHK_K_ECC_NC:	reason = "uncorrectable ECC error"; break;
	case MCHK_K_OS_BUGCHECK: reason = "OS-specific PAL bugcheck"; break;
	case MCHK_K_PAL_BUGCHECK: reason = "callsys in kernel mode"; break;
	case 0x96: reason = "i-cache read retryable error"; break;
	case 0x98: reason = "processor detected hard error"; break;

	/* System specific (these are for Alcor, at least): */
	case 0x203: reason = "system detected uncorrectable ECC error"; break;
	case 0x205: reason = "parity error detected by T2"; break;
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
			(unsigned int) mchk_header->elfl_error_type);
		reason = buf;
		break;
	}
	wrmces(rdmces()|1);	/* reset machine check pending flag */
	mb();

	printk(KERN_CRIT "  T2 machine check: %s%s\n",
	       reason, mchk_header->elfl_retry ? " (retryable)" : "");

	/* dump the the logout area to give all info: */

	ptr = (unsigned long *)la_ptr;
	for (i = 0; i < mchk_header->elfl_size / sizeof(long); i += 2) {
		printk(KERN_CRIT " +%8lx %016lx %016lx\n",
		       i*sizeof(long), ptr[i], ptr[i+1]);
	}
}
