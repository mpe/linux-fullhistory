/*
 *	linux/arch/alpha/kernel/core_tsunami.c
 *
 * Code common to all TSUNAMI core logic chips.
 *
 * Based on code written by David A. Rusling (david.rusling@reo.mts.dec.com).
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
#include <asm/pci.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_tsunami.h>
#undef __EXTERN_INLINE

#include "proto.h"
#include "bios32.h"

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the I/O controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

/*
 * BIOS32-style PCI interface:
 */

#ifdef DEBUG_CONFIG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif

#define DEBUG_MCHECK
#ifdef DEBUG_MCHECK
# define DBG_MCK(args)	printk args
#define DEBUG_MCHECK_DUMP
#else
# define DBG_MCK(args)
#endif

static volatile unsigned int TSUNAMI_mcheck_expected[NR_CPUS];
static volatile unsigned int TSUNAMI_mcheck_taken[NR_CPUS];
static unsigned int TSUNAMI_jd[NR_CPUS];
int TSUNAMI_bootcpu;

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Note that all config space accesses use Type 1 address format.
 *
 * Note also that type 1 is determined by non-zero bus number.
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
mk_conf_addr(u8 bus, u8 device_fn, u8 where, struct linux_hose_info *hose,
	     unsigned long *pci_addr, unsigned char *type1)
{
	unsigned long addr;

	if (!pci_probe_enabled)
		return -1;

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, "
		 "pci_addr=0x%p, type1=0x%p)\n",
		 bus, device_fn, where, pci_addr, type1));

        *type1 = (bus != 0);

        if (hose->pci_first_busno == bus)
		bus = 0;

        addr = (bus << 16) | (device_fn << 8) | where;
	addr |= hose->pci_config_space;
		
	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

int 
tsunami_hose_read_config_byte (u8 bus, u8 device_fn, u8 where, u8 *value,
			       struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldbu(*(vucp)addr);
	return PCIBIOS_SUCCESSFUL;
}

int
tsunami_hose_read_config_word (u8 bus, u8 device_fn, u8 where, u16 *value,
			       struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldwu(*(vusp)addr);
	return PCIBIOS_SUCCESSFUL;
}

int
tsunami_hose_read_config_dword (u8 bus, u8 device_fn, u8 where, u32 *value,
				struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = *(vuip)addr;
	return PCIBIOS_SUCCESSFUL;
}

int 
tsunami_hose_write_config_byte (u8 bus, u8 device_fn, u8 where, u8 value,
				struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	__kernel_stb(value, *(vucp)addr);
	return PCIBIOS_SUCCESSFUL;
}

int 
tsunami_hose_write_config_word (u8 bus, u8 device_fn, u8 where, u16 value,
				struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	__kernel_stw(value, *(vusp)addr);
	return PCIBIOS_SUCCESSFUL;
}

int
tsunami_hose_write_config_dword (u8 bus, u8 device_fn, u8 where, u32 value,
				 struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*(vuip)addr = value;
	return PCIBIOS_SUCCESSFUL;
}

#ifdef NXM_MACHINE_CHECKS_ON_TSUNAMI
static long
tsunami_probe_read(volatile unsigned long *vaddr)
{
	long dont_care, probe_result;
	int cpu = smp_processor_id();
	int s = swpipl(6);	/* Block everything but machine checks. */

	TSUNAMI_mcheck_taken[cpu] = 0;
	TSUNAMI_mcheck_expected[cpu] = 1;
	dont_care = *vaddr;
	draina();
	TSUNAMI_mcheck_expected[cpu] = 0;
	probe_result = !TSUNAMI_mcheck_taken[cpu];
	TSUNAMI_mcheck_taken[cpu] = 0;
	setipl(s);

	printk("dont_care == 0x%lx\n", dont_care);

	return probe_result;
}

static long
tsunami_probe_write(volatile unsigned long *vaddr)
{
	long true_contents, probe_result = 1;

	TSUNAMI_cchip->misc.csr |= (1L << 28); /* clear NXM... */
	true_contents = *vaddr;
	*vaddr = 0;
	draina();
	if (TSUNAMI_cchip->misc.csr & (1L << 28)) {
		int source = (TSUNAMI_cchip->misc.csr >> 29) & 7;
		TSUNAMI_cchip->misc.csr |= (1L << 28); /* ...and unlock NXS. */
		probe_result = 0;
		printk("tsunami_probe_write: unit %d at 0x%016lx\n", source,
		       (unsigned long)vaddr);
	}
	if (probe_result)
		*vaddr = true_contents;
	return probe_result;
}
#else
#define tsunami_probe_read(ADDR) 1
#endif /* NXM_MACHINE_CHECKS_ON_TSUNAMI */

#define FN __FUNCTION__

static void __init
tsunami_init_one_pchip(tsunami_pchip *pchip, int index,
		       unsigned long *mem_start)
{
	struct linux_hose_info *hose;
	int i;

	if (tsunami_probe_read(&pchip->pctl.csr) == 0)
		return;

	hose = (struct linux_hose_info *)*mem_start;
	*mem_start = (unsigned long)(hose + 1);
	memset(hose, 0, sizeof(*hose));

	*hose_tail = hose;
	hose_tail = &hose->next;

	hose->pci_io_space = TSUNAMI_IO(index);
	hose->pci_mem_space = TSUNAMI_MEM(index);
	hose->pci_config_space = TSUNAMI_CONF(index);
	hose->pci_sparse_space = 0;
	hose->pci_hose_index = index;

	switch (alpha_use_srm_setup)
	{
	default:
#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
		for (i = 0; i < 4; ++i) {
			if ((pchip->wsba[i].csr & 3) == 1
			    && pchip->tba[i].csr == 0
			    && (pchip->wsm[i].csr & 0xfff00000) > 0x0ff00000) {
				TSUNAMI_DMA_WIN_BASE = pchip->wsba[i].csr & 0xfff00000;
				TSUNAMI_DMA_WIN_SIZE = pchip->wsm[i].csr & 0xfff00000;
				TSUNAMI_DMA_WIN_SIZE += 0x00100000;
#if 1
				printk("%s: using Window %d settings\n", FN, i);
				printk("%s: BASE 0x%lx MASK 0x%lx TRANS 0x%lx\n",
				       FN, pchip->wsba[i].csr, pchip->wsm[i].csr,
				       pchip->tba[i].csr);
#endif
				goto found;
			}
		}

		/* Otherwise, we must use our defaults.  */
		TSUNAMI_DMA_WIN_BASE = TSUNAMI_DMA_WIN_BASE_DEFAULT;
		TSUNAMI_DMA_WIN_SIZE = TSUNAMI_DMA_WIN_SIZE_DEFAULT;
#endif
	case 0:
		/*
		 * Set up the PCI->physical memory translation windows.
		 * For now, windows 1,2 and 3 are disabled.  In the future,
		 * we may want to use them to do scatter/gather DMA. 
		 *
		 * Window 0 goes at 1 GB and is 1 GB large, mapping to 0.
		 */

		pchip->wsba[0].csr = 1L | (TSUNAMI_DMA_WIN_BASE_DEFAULT & 0xfff00000U);
		pchip->wsm[0].csr = (TSUNAMI_DMA_WIN_SIZE_DEFAULT - 1) & 0xfff00000UL;
		pchip->tba[0].csr = 0;

#if 0
		pchip->wsba[1].csr = 0;
#else
		/* make the second window at 2Gb for 1Gb mapping to 1Gb */
		pchip->wsba[1].csr = 1L | ((0x80000000U) & 0xfff00000U);
		pchip->wsm[1].csr = (0x40000000UL - 1) & 0xfff00000UL;
		pchip->tba[1].csr = 0x40000000;
#endif

		pchip->wsba[2].csr = 0;
		pchip->wsba[3].csr = 0;
		mb();
	}
found:;
}

void __init
tsunami_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
#ifdef NXM_MACHINE_CHECKS_ON_TSUNAMI
	extern asmlinkage void entInt(void);
        unsigned long tmp;
	
	/* Ho hum.. init_arch is called before init_IRQ, but we need to be
	   able to handle machine checks.  So install the handler now.  */
	wrent(entInt, 0);

	/* NXMs just don't matter to Tsunami--unless they make it
	   choke completely. */
	tmp = (unsigned long)(TSUNAMI_cchip - 1);
	printk("%s: probing bogus address:  0x%016lx\n", FN, bogus_addr);
	printk("\tprobe %s\n",
	       tsunami_probe_write((unsigned long *)bogus_addr)
	       ? "succeeded" : "failed");
#endif /* NXM_MACHINE_CHECKS_ON_TSUNAMI */

#if 0
	printk("%s: CChip registers:\n", FN);
	printk("%s: CSR_CSC 0x%lx\n", FN, TSUNAMI_cchip->csc.csr);
	printk("%s: CSR_MTR 0x%lx\n", FN, TSUNAMI_cchip.mtr.csr);
	printk("%s: CSR_MISC 0x%lx\n", FN, TSUNAMI_cchip->misc.csr);
	printk("%s: CSR_DIM0 0x%lx\n", FN, TSUNAMI_cchip->dim0.csr);
	printk("%s: CSR_DIM1 0x%lx\n", FN, TSUNAMI_cchip->dim1.csr);
	printk("%s: CSR_DIR0 0x%lx\n", FN, TSUNAMI_cchip->dir0.csr);
	printk("%s: CSR_DIR1 0x%lx\n", FN, TSUNAMI_cchip->dir1.csr);
	printk("%s: CSR_DRIR 0x%lx\n", FN, TSUNAMI_cchip->drir.csr);

	printk("%s: DChip registers:\n");
	printk("%s: CSR_DSC 0x%lx\n", FN, TSUNAMI_dchip->dsc.csr);
	printk("%s: CSR_STR 0x%lx\n", FN, TSUNAMI_dchip->str.csr);
	printk("%s: CSR_DREV 0x%lx\n", FN, TSUNAMI_dchip->drev.csr);
#endif

	/* Align memory to cache line; we'll be allocating from it.  */
	*mem_start = (*mem_start | 31) + 1;

	/* Find how many hoses we have, and initialize them.  */
	tsunami_init_one_pchip(TSUNAMI_pchip0, 0, mem_start);
	/* must change this for TYPHOON which may have 4 */
	if (TSUNAMI_cchip->csc.csr & 1L<<14)
	    tsunami_init_one_pchip(TSUNAMI_pchip1, 1, mem_start);
}

static inline void
tsunami_pci_clr_err_1(tsunami_pchip *pchip, int cpu)
{
	TSUNAMI_jd[cpu] = pchip->perror.csr;
	DBG_MCK(("TSUNAMI_pci_clr_err: PERROR after read 0x%x\n",
		 TSUNAMI_jd[cpu]));
	pchip->perror.csr = 0x040;
	mb();
	TSUNAMI_jd[cpu] = pchip->perror.csr;
}

static int
tsunami_pci_clr_err(void)
{
	int cpu = smp_processor_id();
	tsunami_pci_clr_err_1(TSUNAMI_pchip0, cpu);
	/* must change this for TYPHOON which may have 4 */
	if (TSUNAMI_cchip->csc.csr & 1L<<14)
	    tsunami_pci_clr_err_1(TSUNAMI_pchip1, cpu);
	return 0;
}

void
tsunami_machine_check(unsigned long vector, unsigned long la_ptr,
		      struct pt_regs * regs)
{
#if 0
        printk("TSUNAMI machine check ignored\n") ;
#else
	struct el_common *mchk_header;
	struct el_TSUNAMI_sysdata_mcheck *mchk_sysdata;
	unsigned int cpu = smp_processor_id();

	mb();
	mchk_header = (struct el_common *)la_ptr;

	mchk_sysdata = (struct el_TSUNAMI_sysdata_mcheck *)
		(la_ptr + mchk_header->sys_offset);

#if 0
	DBG_MCK(("tsunami_machine_check: vector=0x%lx la_ptr=0x%lx\n",
		 vector, la_ptr));
	DBG_MCK(("\t\t pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
		 regs->pc, mchk_header->size, mchk_header->proc_offset,
		 mchk_header->sys_offset));
	DBG_MCK(("tsunami_machine_check: expected %d DCSR 0x%lx PEAR 0x%lx\n",
		 TSUNAMI_mcheck_expected[cpu], mchk_sysdata->epic_dcsr,
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
	if (TSUNAMI_mcheck_expected[cpu]) {
		DBG_MCK(("TSUNAMI machine check expected\n"));
		TSUNAMI_mcheck_expected[cpu] = 0;
		TSUNAMI_mcheck_taken[cpu] = 1;
		mb();
		mb();  /* magic */
		draina();
		tsunami_pci_clr_err();
		wrmces(0x7);
		mb();
	}
#if 1
	else {
		printk("TSUNAMI machine check NOT expected\n") ;
		DBG_MCK(("tsunami_machine_check: vector=0x%lx la_ptr=0x%lx\n",
			 vector, la_ptr));
		DBG_MCK(("\t\t pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
			 regs->pc, mchk_header->size, mchk_header->proc_offset,
			 mchk_header->sys_offset));
		TSUNAMI_mcheck_expected[cpu] = 0;
		TSUNAMI_mcheck_taken[cpu] = 1;
		mb();
		mb();  /* magic */
		draina();
		tsunami_pci_clr_err();
		wrmces(0x7);
		mb();
	}
#endif
#endif
}
