/*
 *	linux/arch/alpha/kernel/core_t2.c
 *
 * Code common to all T2 core logic chips.
 *
 * Written by Jay A Estabrook (jestabro@amt.tay1.dec.com).
 * December 1996.
 *
 * based on CIA code by David A Rusling (david.rusling@reo.mts.dec.com)
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>

#define __EXTERN_INLINE
#include <asm/io.h>
#include <asm/core_t2.h>
#undef __EXTERN_INLINE

#include "proto.h"

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

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

static volatile unsigned int T2_mcheck_expected[NR_CPUS];
static volatile unsigned int T2_mcheck_taken[NR_CPUS];


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

	DBG(("mk_conf_addr(bus=%d, dfn=0x%x, where=0x%x,"
	     " addr=0x%lx, type1=0x%x)\n",
	     bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		int device = device_fn >> 3;

		/* Type 0 configuration cycle.  */

		if (device > 8) {
			DBG(("mk_conf_addr: device (%d)>20, returning -1\n",
			     device));
			return -1;
		}

		*type1 = 0;
		addr = (0x0800L << device) | ((device_fn & 7) << 8) | (where);
	} else {
		/* Type 1 configuration cycle.  */
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
	unsigned int value, cpu;
	unsigned long t2_cfg = 0;

	cpu = smp_processor_id();

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	DBG(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

#if 0
	{
	  unsigned long stat0;
	  /* Reset status register to avoid losing errors.  */
	  stat0 = *(vulp)T2_IOCSR;
	  *(vulp)T2_IOCSR = stat0;
	  mb();
	  DBG(("conf_read: T2 IOCSR was 0x%x\n", stat0));
	}
#endif

	/* If Type1 access, must set T2 CFG.  */
	if (type1) {
		t2_cfg = *(vulp)T2_HAE_3 & ~0xc0000000UL;
		*(vulp)T2_HAE_3 = 0x40000000UL | t2_cfg;
		mb();
		DBG(("conf_read: TYPE1 access\n"));
	}
	mb();
	draina();

	T2_mcheck_expected[cpu] = 1;
	T2_mcheck_taken[cpu] = 0;
	mb();

	/* Access configuration space. */
	value = *(vuip)addr;
	mb();
	mb();  /* magic */

	if (T2_mcheck_taken[cpu]) {
		T2_mcheck_taken[cpu] = 0;
		value = 0xffffffffU;
		mb();
	}
	T2_mcheck_expected[cpu] = 0;
	mb();

	/* If Type1 access, must reset T2 CFG so normal IO space ops work.  */
	if (type1) {
		*(vulp)T2_HAE_3 = t2_cfg;
		mb();
	}
	DBG(("conf_read(): finished\n"));

	__restore_flags(flags);
	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned long flags;
	unsigned int cpu;
	unsigned long t2_cfg = 0;

	cpu = smp_processor_id();

	__save_and_cli(flags);	/* avoid getting hit by machine check */

#if 0
	{
	  unsigned long stat0;
	  /* Reset status register to avoid losing errors.  */
	  stat0 = *(vulp)T2_IOCSR;
	  *(vulp)T2_IOCSR = stat0;
	  mb();
	  DBG(("conf_write: T2 ERR was 0x%x\n", stat0));
	}
#endif

	/* If Type1 access, must set T2 CFG.  */
	if (type1) {
		t2_cfg = *(vulp)T2_HAE_3 & ~0xc0000000UL;
		*(vulp)T2_HAE_3 = t2_cfg | 0x40000000UL;
		mb();
		DBG(("conf_write: TYPE1 access\n"));
	}
	mb();
	draina();

	T2_mcheck_expected[cpu] = 1;
	mb();

	/* Access configuration space.  */
	*(vuip)addr = value;
	mb();
	mb();  /* magic */

	T2_mcheck_expected[cpu] = 0;
	mb();

	/* If Type1 access, must reset T2 CFG so normal IO space ops work.  */
	if (type1) {
		*(vulp)T2_HAE_3 = t2_cfg;
		mb();
	}
	DBG(("conf_write(): finished\n"));
	__restore_flags(flags);
}

int
t2_hose_read_config_byte (u8 bus, u8 device_fn, u8 where, u8 *value,
			  struct linux_hose_info *hose)
{
	unsigned long addr = T2_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x00;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int 
t2_hose_read_config_word (u8 bus, u8 device_fn, u8 where, u16 *value,
			  struct linux_hose_info *hose)
{
	unsigned long addr = T2_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x08;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int 
t2_hose_read_config_dword (u8 bus, u8 device_fn, u8 where, u32 *value,
			   struct linux_hose_info *hose)
{
	unsigned long addr = T2_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x18;
	*value = conf_read(addr, type1);
	return PCIBIOS_SUCCESSFUL;
}

int 
t2_hose_write_config_byte (u8 bus, u8 device_fn, u8 where, u8 value,
			   struct linux_hose_info *hose)
{
	unsigned long addr = T2_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x00;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

int
t2_hose_write_config_word (u8 bus, u8 device_fn, u8 where, u16 value,
			   struct linux_hose_info *hose)
{
	unsigned long addr = T2_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x08;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

int 
t2_hose_write_config_dword (u8 bus, u8 device_fn, u8 where, u32 value,
			    struct linux_hose_info *hose)
{
	unsigned long addr = T2_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x18;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

void __init
t2_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	unsigned int i;

	for (i = 0; i < NR_CPUS; i++) {
		T2_mcheck_expected[i] = 0;
		T2_mcheck_taken[i] = 0;
	}

#if 0
	{
	  /* Set up error reporting.  */
	  unsigned long t2_err;

	  t2_err = *(vulp)T2_IOCSR;
	  t2_err |= (0x1 << 7);   /* master abort */
	  *(vulp)T2_IOCSR = t2_err;
	  mb();
	}
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

	switch (alpha_use_srm_setup) 
	{
	default:
#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
		/* Check window 1 for enabled and mapped to 0.  */
		if (((*(vulp)T2_WBASE1 & (3UL<<18)) == (2UL<<18))
		    && (*(vulp)T2_TBASE1 == 0)) {
			T2_DMA_WIN_BASE = *(vulp)T2_WBASE1 & 0xfff00000UL;
			T2_DMA_WIN_SIZE = *(vulp)T2_WMASK1 & 0xfff00000UL;
			T2_DMA_WIN_SIZE += 0x00100000UL;
			/* DISABLE window 2!! ?? */
#if 1
			printk("t2_init: using Window 1 settings\n");
			printk("t2_init: BASE 0x%lx MASK 0x%lx TRANS 0x%lx\n",
			       *(vulp)T2_WBASE1,
			       *(vulp)T2_WMASK1,
			       *(vulp)T2_TBASE1);
#endif
			break;
		}

		/* Check window 2 for enabled and mapped to 0.  */
		if (((*(vulp)T2_WBASE2 & (3UL<<18)) == (2UL<<18))
		    && (*(vulp)T2_TBASE2 == 0)) {
			T2_DMA_WIN_BASE = *(vulp)T2_WBASE2 & 0xfff00000UL;
			T2_DMA_WIN_SIZE = *(vulp)T2_WMASK2 & 0xfff00000UL;
			T2_DMA_WIN_SIZE += 0x00100000UL;
			/* DISABLE window 1!! ?? */
#if 1
			printk("t2_init: using Window 2 settings\n");
			printk("t2_init: BASE 0x%lx MASK 0x%lx TRANS 0x%lx\n",
			       *(vulp)T2_WBASE2,
			       *(vulp)T2_WMASK2,
			       *(vulp)T2_TBASE2);
#endif
			break;
		}

		/* Otherwise, we must use our defaults.  */
		T2_DMA_WIN_BASE = T2_DMA_WIN_BASE_DEFAULT;
		T2_DMA_WIN_SIZE = T2_DMA_WIN_SIZE_DEFAULT;
#endif
	case 0:
		/*
		 * Set up the PCI->physical memory translation windows.
		 * For now, window 2 is  disabled.  In the future, we may
		 * want to use it to do scatter/gather DMA. 
		 *
		 * Window 1 goes at 1 GB and is 1 GB large.
		 */

		/* WARNING!! must correspond to the DMA_WIN params!!! */
		*(vulp)T2_WBASE1 = 0x400807ffU;
		*(vulp)T2_WMASK1 = 0x3ff00000U;
		*(vulp)T2_TBASE1 = 0;

		*(vulp)T2_WBASE2 = 0x0;
		*(vulp)T2_HBASE = 0x0;
		break;
	}

	/*
	 * Sigh... For the SRM setup, unless we know apriori what the HAE
	 * contents will be, we need to setup the arbitrary region bases
	 * so we can test against the range of addresses and tailor the
	 * region chosen for the SPARSE memory access.
	 *
	 * See include/asm-alpha/t2.h for the SPARSE mem read/write.
	 */
	if (alpha_use_srm_setup) {
		unsigned long t2_hae_1 = *(vulp)T2_HAE_1;

		alpha_mv.sm_base_r1 = (t2_hae_1 << 27) & 0xf8000000UL;

		/*
		 * Set the HAE cache, so that setup_arch() code
		 * will use the SRM setting always. Our readb/writeb
		 * code in .h expects never to have to change
		 * the contents of the HAE.
		 */
		alpha_mv.hae_cache = t2_hae_1;

		alpha_mv.mv_readb = t2_srm_readb;
		alpha_mv.mv_readw = t2_srm_readw;
		alpha_mv.mv_writeb = t2_srm_writeb;
		alpha_mv.mv_writew = t2_srm_writew;
	} else {
		*(vulp)T2_HAE_1 = 0; mb();
		*(vulp)T2_HAE_2 = 0; mb();
		*(vulp)T2_HAE_3 = 0; mb();
#if 0
		*(vulp)T2_HAE_4 = 0; mb(); /* do not touch this */
#endif
	}
}

#define SIC_SEIC (1UL << 33)    /* System Event Clear */

static int
t2_clear_errors(void)
{
	unsigned int cpu = smp_processor_id();
	static struct sable_cpu_csr *cpu_regs = NULL;

	switch (cpu)
	{
	case 0: cpu_regs = (struct sable_cpu_csr *)T2_CPU0_BASE; break;
	case 1: cpu_regs = (struct sable_cpu_csr *)T2_CPU1_BASE; break;
	case 2: cpu_regs = (struct sable_cpu_csr *)T2_CPU2_BASE; break;
	case 3: cpu_regs = (struct sable_cpu_csr *)T2_CPU3_BASE; break;
	}

	DBGMC(("???????? t2_clear_errors\n"));

	cpu_regs->sic &= ~SIC_SEIC;

	/* 
	 * clear CPU errors
	 */
	cpu_regs->bcce |= cpu_regs->bcce;
	cpu_regs->cbe  |= cpu_regs->cbe;
	cpu_regs->bcue |= cpu_regs->bcue;
	cpu_regs->dter |= cpu_regs->dter;

	*(vulp)T2_CERR1 |= *(vulp)T2_CERR1;
	*(vulp)T2_PERR1 |= *(vulp)T2_PERR1;

	mb();
	mb();  /* magic */
	return 0;
}

void
t2_machine_check(unsigned long vector, unsigned long la_ptr,
		 struct pt_regs * regs)
{
	struct el_t2_logout_header *mchk_header;
	struct el_t2_procdata_mcheck *mchk_procdata;
	struct el_t2_sysdata_mcheck *mchk_sysdata;
	unsigned long * ptr;
	const char * reason;
	char buf[128];
	long i;
	unsigned int cpu = smp_processor_id();

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
	DBGMC(("t2_machine_check: expected %d\n", T2_mcheck_expected[cpu]));

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
	mb();  /* magic */
	if (T2_mcheck_expected[cpu]) {
		DBGMC(("T2 machine check expected\n"));
		T2_mcheck_taken[cpu] = 1;
		t2_clear_errors();
		T2_mcheck_expected[cpu] = 0;
		mb();
		mb();  /* magic */
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

	/* Dump the logout area to give all info.  */

	ptr = (unsigned long *)la_ptr;
	for (i = 0; i < mchk_header->elfl_size / sizeof(long); i += 2) {
		printk(KERN_CRIT " +%8lx %016lx %016lx\n",
		       i*sizeof(long), ptr[i], ptr[i+1]);
	}
}
