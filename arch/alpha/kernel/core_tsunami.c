/*
 *	linux/arch/alpha/kernel/core_tsunami.c
 *
 * Code common to all TSUNAMI core logic chips.
 *
 * Based on code written by David A. Rusling (david.rusling@reo.mts.dec.com).
 *
 */
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_tsunami.h>
#undef __EXTERN_INLINE

#include "proto.h"

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the I/O controller does not properly
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
mk_conf_addr(u8 bus, u8 device_fn, u8 where, unsigned long *pci_addr,
	     unsigned char *type1)
{
	unsigned long addr;

	DBG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, "
	     "pci_addr=0x%p, type1=0x%p)\n",
	     bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		*type1 = 0;
	} else {
		/* Type 1 configuration cycle.  */
		*type1 = 1;
	}
	addr = (bus << 16) | (device_fn << 8) | (where);
	*pci_addr = addr;
	DBG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

int 
tsunami_pcibios_read_config_byte (u8 bus, u8 device_fn, u8 where, u8 *value)
{
	unsigned long addr;
	unsigned char type1;

	*value = 0xff;
	if (mk_conf_addr(bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldbu(*(vucp)(addr+TSUNAMI_PCI0_CONF));
	return PCIBIOS_SUCCESSFUL;
}

int
tsunami_pcibios_read_config_word (u8 bus, u8 device_fn, u8 where, u16 *value)
{
	unsigned long addr;
	unsigned char type1;

	*value = 0xffff;
	if (where & 0x1)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (mk_conf_addr(bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = __kernel_ldwu(*(vusp)(addr+TSUNAMI_PCI0_CONF));
	return PCIBIOS_SUCCESSFUL;
}

int
tsunami_pcibios_read_config_dword (u8 bus, u8 device_fn, u8 where, u32 *value)
{
	unsigned long addr;
	unsigned char type1;

	*value = 0xffffffff;
	if (where & 0x3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (mk_conf_addr(bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = *(vuip)(addr+TSUNAMI_PCI0_CONF);
	return PCIBIOS_SUCCESSFUL;
}

int 
tsunami_pcibios_write_config_byte (u8 bus, u8 device_fn, u8 where, u8 value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	__kernel_stb(value, *(vucp)(addr+TSUNAMI_PCI0_CONF));
	return PCIBIOS_SUCCESSFUL;
}

int 
tsunami_pcibios_write_config_word (u8 bus, u8 device_fn, u8 where, u16 value)
{
	unsigned long addr;
	unsigned char type1;

	if (where & 0x1)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (mk_conf_addr(bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	__kernel_stw(value, *(vusp)(addr+TSUNAMI_PCI0_CONF));
	return PCIBIOS_SUCCESSFUL;
}

int
tsunami_pcibios_write_config_dword (u8 bus, u8 device_fn, u8 where, u32 value)
{
	unsigned long addr;
	unsigned char type1;

	if (where & 0x3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (mk_conf_addr(bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*(vuip)(addr+TSUNAMI_PCI0_CONF) = value;
	return PCIBIOS_SUCCESSFUL;
}

void __init
tsunami_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
        unsigned long tsunami_err;
	unsigned int i;

#if 0
	printk("tsunami_init: CChip registers:\n");
	printk("CSR_CSC 0x%lx\n", *(vulp)TSUNAMI_CSR_CSC);
	printk("CSR_MTR 0x%lx\n", *(vulp)TSUNAMI_CSR_MTR);
	printk("CSR_MISC 0x%lx\n", *(vulp)TSUNAMI_CSR_MISC);
	printk("CSR_DIM0 0x%lx\n", *(vulp)TSUNAMI_CSR_DIM0);
	printk("CSR_DIM1 0x%lx\n", *(vulp)TSUNAMI_CSR_DIM1);
	printk("CSR_DIR0 0x%lx\n", *(vulp)TSUNAMI_CSR_DIR0);
	printk("CSR_DIR1 0x%lx\n", *(vulp)TSUNAMI_CSR_DIR1);
	printk("CSR_DRIR 0x%lx\n", *(vulp)TSUNAMI_CSR_DRIR);

	printk("tsunami_init: DChip registers:\n");
	printk("CSR_DSC 0x%lx\n", *(vulp)TSUNAMI_CSR_DSC);
	printk("CSR_STR 0x%lx\n", *(vulp)TSUNAMI_CSR_STR);
	printk("CSR_DREV 0x%lx\n", *(vulp)TSUNAMI_CSR_DREV);

	printk("tsunami_init: PChip registers:\n");
	printk("PCHIP0_WSBA0 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_WSBA0);
	printk("PCHIP0_WSBA1 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_WSBA1);
	printk("PCHIP0_WSBA2 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_WSBA2);
	printk("PCHIP0_WSBA3 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_WSBA3);
	printk("PCHIP0_WSM0 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_WSM0);
	printk("PCHIP0_WSM1 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_WSM1);
	printk("PCHIP0_WSM2 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_WSM2);
	printk("PCHIP0_WSM3 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_WSM3);
	printk("PCHIP0_TBA0 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_TBA0);
	printk("PCHIP0_TBA1 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_TBA1);
	printk("PCHIP0_TBA2 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_TBA2);
	printk("PCHIP0_TBA3 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_TBA3);
	printk("PCHIP0_PCTL 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_PCTL);
	printk("PCHIP0_PLAT 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_PLAT);
	printk("PCHIP0_PERROR 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_PERROR);
	printk("PCHIP0_PERRMASK 0x%lx\n", *(vulp)TSUNAMI_PCHIP0_PERRMASK);
#endif

	for (i = 0; i < NR_CPUS; i++) {
		TSUNAMI_mcheck_expected[i] = 0;
		TSUNAMI_mcheck_taken[i] = 0;
	}

#ifdef NOT_YET
        /* 
	 * Set up error reporting. Make sure CPU_PE is OFF in the mask.
	 */
	tsunami_err = *(vulp)TSUNAMI_PCHIP0_PERRMASK;
	tsunami_err &= ~20;   
	*(vulp)TSUNAMI_PCHIP0_PERRMASK = tsunami_err;
	mb();
	tsunami_err = *(vulp)TSUNAMI_PCHIP0_PERRMASK;

	tsunami_err = *(vulp)TSUNAMI_PCHIP0_PERROR ;
	tsunami_err |= 0x40;   /* master/target abort */
	*(vulp)TSUNAMI_PCHIP0_PERROR = tsunami_err ;
	mb() ;
	tsunami_err = *(vulp)TSUNAMI_PCHIP0_PERROR ;
#endif /* NOT_YET */

	switch (alpha_use_srm_setup)
	{
	default:
#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
		/* Check window 0 for enabled and mapped to 0.  */
		if (((*(vulp)TSUNAMI_PCHIP0_WSBA0 & 3) == 1)
		    && (*(vulp)TSUNAMI_PCHIP0_TBA0 == 0)
		    && ((*(vulp)TSUNAMI_PCHIP0_WSM0 & 0xfff00000U) > 0x0ff00000U)) {
			TSUNAMI_DMA_WIN_BASE = *(vulp)TSUNAMI_PCHIP0_WSBA0 & 0xfff00000U;
			TSUNAMI_DMA_WIN_SIZE = *(vulp)TSUNAMI_PCHIP0_WSM0 & 0xfff00000U;
			TSUNAMI_DMA_WIN_SIZE += 0x00100000U;
#if 1
			printk("tsunami_init: using Window 0 settings\n");
			printk("tsunami_init: BASE 0x%lx MASK 0x%lx TRANS 0x%lx\n",
			       *(vulp)TSUNAMI_PCHIP0_WSBA0,
			       *(vulp)TSUNAMI_PCHIP0_WSM0,
			       *(vulp)TSUNAMI_PCHIP0_TBA0);
#endif
			break;
		}

		/* Check window 1 for enabled and mapped to 0.  */
		if (((*(vulp)TSUNAMI_PCHIP0_WSBA1 & 3) == 1)
		    && (*(vulp)TSUNAMI_PCHIP0_TBA1 == 0)
		    && ((*(vulp)TSUNAMI_PCHIP0_WSM1 & 0xfff00000U) > 0x0ff00000U)) {
			TSUNAMI_DMA_WIN_BASE = *(vulp)TSUNAMI_PCHIP0_WSBA1 & 0xfff00000U;
			TSUNAMI_DMA_WIN_SIZE = *(vulp)TSUNAMI_PCHIP0_WSM1 & 0xfff00000U;
			TSUNAMI_DMA_WIN_SIZE += 0x00100000U;
#if 1
			printk("tsunami_init: using Window 1 settings\n");
			printk("tsunami_init: BASE 0x%lx MASK 0x%lx TRANS 0x%lx\n",
			       *(vulp)TSUNAMI_PCHIP0_WSBA1,
			       *(vulp)TSUNAMI_PCHIP0_WSM1,
			       *(vulp)TSUNAMI_PCHIP0_TBA1);
#endif
			break;
		}

		/* Check window 2 for enabled and mapped to 0.  */
		if (((*(vulp)TSUNAMI_PCHIP0_WSBA2 & 3) == 1)
		    && (*(vulp)TSUNAMI_PCHIP0_TBA2 == 0)
		    && ((*(vulp)TSUNAMI_PCHIP0_WSM2 & 0xfff00000U) > 0x0ff00000U)) {
			TSUNAMI_DMA_WIN_BASE = *(vulp)TSUNAMI_PCHIP0_WSBA2 & 0xfff00000U;
			TSUNAMI_DMA_WIN_SIZE = *(vulp)TSUNAMI_PCHIP0_WSM2 & 0xfff00000U;
			TSUNAMI_DMA_WIN_SIZE += 0x00100000U;
#if 1
			printk("tsunami_init: using Window 2 settings\n");
			printk("tsunami_init: BASE 0x%lx MASK 0x%lx TRANS 0x%lx\n",
			       *(vulp)TSUNAMI_PCHIP0_WSBA2,
			       *(vulp)TSUNAMI_PCHIP0_WSM2,
			       *(vulp)TSUNAMI_PCHIP0_TBA2);
#endif
			break;
		}

		/* Check window 3 for enabled and mapped to 0.  */
		if (((*(vulp)TSUNAMI_PCHIP0_WSBA3 & 3) == 1)
		    && (*(vulp)TSUNAMI_PCHIP0_TBA3 == 0)
		    && ((*(vulp)TSUNAMI_PCHIP0_WSM3 & 0xfff00000U) > 0x0ff00000U)) {
			TSUNAMI_DMA_WIN_BASE = *(vulp)TSUNAMI_PCHIP0_WSBA3 & 0xfff00000U;
			TSUNAMI_DMA_WIN_SIZE = *(vulp)TSUNAMI_PCHIP0_WSM3 & 0xfff00000U;
			TSUNAMI_DMA_WIN_SIZE += 0x00100000U;
#if 1
			printk("tsunami_init: using Window 3 settings\n");
			printk("tsunami_init: BASE 0x%lx MASK 0x%lx TRANS 0x%lx\n",
			       *(vulp)TSUNAMI_PCHIP0_WSBA3,
			       *(vulp)TSUNAMI_PCHIP0_WSM3,
			       *(vulp)TSUNAMI_PCHIP0_TBA3);
#endif
			break;
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
		 * Window 0 goes at 1 GB and is 1 GB large.
		 */

		*(vulp)TSUNAMI_PCHIP0_WSBA0
			= 1L | (TSUNAMI_DMA_WIN_BASE_DEFAULT & 0xfff00000U);
		*(vulp)TSUNAMI_PCHIP0_WSM0
			= (TSUNAMI_DMA_WIN_SIZE_DEFAULT - 1) & 0xfff00000UL;
		*(vulp)TSUNAMI_PCHIP0_TBA0 = 0UL;

		*(vulp)TSUNAMI_PCHIP0_WSBA1 = 0UL;
		*(vulp)TSUNAMI_PCHIP0_WSBA2 = 0UL;
		*(vulp)TSUNAMI_PCHIP0_WSBA3 = 0UL;
		mb();
	}
}

static int
tsunami_pci_clr_err(void)
{
	unsigned int cpu = smp_processor_id();

	TSUNAMI_jd[cpu] = *((vulp)TSUNAMI_PCHIP0_PERROR);
	DBG(("TSUNAMI_pci_clr_err: PERROR after read 0x%x\n",TSUNAMI_jd[cpu]));
	*((vulp)TSUNAMI_PCHIP0_PERROR) = 0x040; mb();
	TSUNAMI_jd[cpu] = *((vulp)TSUNAMI_PCHIP0_PERROR);
	return 0;
}

void
tsunami_machine_check(unsigned long vector, unsigned long la_ptr,
		      struct pt_regs * regs)
{
#if 1
        printk("TSUNAMI machine check ignored\n") ;
#else
	struct el_common *mchk_header;
	struct el_TSUNAMI_sysdata_mcheck *mchk_sysdata;
	unsigned int cpu = smp_processor_id();

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
		DBG(("TSUNAMI machine check expected\n"));
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
