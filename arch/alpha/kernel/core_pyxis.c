/*
 *	linux/arch/alpha/kernel/core_pyxis.c
 *
 * Code common to all PYXIS core logic chips.
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
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

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_pyxis.h>
#undef __EXTERN_INLINE

#include "proto.h"

/* NOTE: Herein are back-to-back mb instructions.  They are magic.
   One plausible explanation is that the I/O controller does not properly
   handle the system transaction.  Another involves timing.  Ho hum.  */

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CONFIG 0
#define DEBUG_MCHECK 0

#if DEBUG_CONFIG
# define DBG_CNF(args)	printk args
#else
# define DBG_CNF(args)
#endif

#if DEBUG_MCHECK
# define DBG_MCK(args)	printk args
# define DEBUG_MCHECK_DUMP
#else
# define DBG_MCK(args)
#endif


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
	*type1 = (bus == 0) ? 0 : 1;
	*pci_addr = (bus << 16) | (device_fn << 8) | (where);

	DBG_CNF(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
		 " returning address 0x%p\n"
		 bus, device_fn, where, *pci_addr));

	return 0;
}

static unsigned int
conf_read(unsigned long addr, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, value, temp;
	unsigned int pyxis_cfg = 0;

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = stat0; mb();
	temp = *(vuip)PYXIS_ERR;  /* re-read to force write */

	/* If Type1 access, must set PYXIS CFG.  */
	if (type1) {
		pyxis_cfg = *(vuip)PYXIS_CFG;
		*(vuip)PYXIS_CFG = (pyxis_cfg & ~3L) | 1; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
	}

	mb();
	draina();
	PYXIS_mcheck_expected = 1;
	PYXIS_mcheck_taken = 0;
	mb();

	/* Access configuration space.  */
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

	/* If Type1 access, must reset IOC CFG so normal IO space ops work.  */
	if (type1) {
		*(vuip)PYXIS_CFG = pyxis_cfg & ~3L; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
	}

	__restore_flags(flags);

	DBG_CNF(("conf_read(addr=0x%lx, type1=%d) = %#x\n",
		 addr, type1, value));

	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, temp;
	unsigned int pyxis_cfg = 0;

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)PYXIS_ERR;
	*(vuip)PYXIS_ERR = stat0; mb();
	temp = *(vuip)PYXIS_ERR;  /* re-read to force write */

	/* If Type1 access, must set PYXIS CFG.  */
	if (type1) {
		pyxis_cfg = *(vuip)PYXIS_CFG;
		*(vuip)PYXIS_CFG = (pyxis_cfg & ~3L) | 1; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
	}

	mb();
	draina();
	PYXIS_mcheck_expected = 1;
	PYXIS_mcheck_taken = 0;
	mb();

	/* Access configuration space.  */
	*(vuip)addr = value;
	mb();
	temp = *(vuip)addr; /* read back to force the write */
	PYXIS_mcheck_expected = 0;
	mb();

	/* If Type1 access, must reset IOC CFG so normal IO space ops work.  */
	if (type1) {
		*(vuip)PYXIS_CFG = pyxis_cfg & ~3L; mb();
		temp = *(vuip)PYXIS_CFG;  /* re-read to force write */
	}

	__restore_flags(flags);

	DBG_CNF(("conf_write(addr=%#lx, value=%#x, type1=%d)\n",
		 addr, value, type1));
}

int
pyxis_hose_read_config_byte (u8 bus, u8 device_fn, u8 where, u8 *value,
			     struct linux_hose_info *hose)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x00;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int
pyxis_hose_read_config_word (u8 bus, u8 device_fn, u8 where, u16 *value,
			     struct linux_hose_info *hose)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x08;
	*value = conf_read(addr, type1) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int
pyxis_hose_read_config_dword (u8 bus, u8 device_fn, u8 where, u32 *value,
			      struct linux_hose_info *hose)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x18;
	*value = conf_read(addr, type1);
	return PCIBIOS_SUCCESSFUL;
}

int 
pyxis_hose_write_config_byte (u8 bus, u8 device_fn, u8 where, u8 value,
			      struct linux_hose_info *hose)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x00;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

int 
pyxis_hose_write_config_word (u8 bus, u8 device_fn, u8 where, u16 value,
			      struct linux_hose_info *hose)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x08;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

int 
pyxis_hose_write_config_dword (u8 bus, u8 device_fn, u8 where, u32 value,
			       struct linux_hose_info *hose)
{
	unsigned long addr = PYXIS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x18;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

void __init
pyxis_enable_errors (void)
{
	unsigned int pyxis_err;

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
	pyxis_err = *(vuip)PYXIS_ERR_MASK; /* re-read to force write */

	pyxis_err = *(vuip)PYXIS_ERR ;
	pyxis_err |= 0x180;   /* master/target abort */
	*(vuip)PYXIS_ERR = pyxis_err; mb();
	pyxis_err = *(vuip)PYXIS_ERR; /* re-read to force write */
}

int __init
pyxis_srm_window_setup (void)
{
	switch (alpha_use_srm_setup)
	{
	default:
#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
		/* Check window 0 for enabled and mapped to 0.  */
		if (((*(vuip)PYXIS_W0_BASE & 3) == 1)
		    && (*(vuip)PYXIS_T0_BASE == 0)
		    && ((*(vuip)PYXIS_W0_MASK & 0xfff00000U) > 0x0ff00000U)) {
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
			break;
		}

		/* Check window 1 for enabled and mapped to 0.  */
		if (((*(vuip)PYXIS_W1_BASE & 3) == 1)
		    && (*(vuip)PYXIS_T1_BASE == 0)
		    && ((*(vuip)PYXIS_W1_MASK & 0xfff00000U) > 0x0ff00000U)) {
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
			break;
		}

		/* Check window 2 for enabled and mapped to 0.  */
		if (((*(vuip)PYXIS_W2_BASE & 3) == 1)
		    && (*(vuip)PYXIS_T2_BASE == 0)
		    && ((*(vuip)PYXIS_W2_MASK & 0xfff00000U) > 0x0ff00000U)) {
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
			break;
		}

		/* Check window 3 for enabled and mapped to 0.  */
		if (((*(vuip)PYXIS_W3_BASE & 3) == 1)
		    && (*(vuip)PYXIS_T3_BASE == 0)
		    && ((*(vuip)PYXIS_W3_MASK & 0xfff00000U) > 0x0ff00000U)) {
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
			break;
		}

		/* Otherwise, we must use our defaults.  */
		PYXIS_DMA_WIN_BASE = PYXIS_DMA_WIN_BASE_DEFAULT;
		PYXIS_DMA_WIN_SIZE = PYXIS_DMA_WIN_SIZE_DEFAULT;
#endif
	case 0:
		return 0;
	}
	return 1;
}

void __init
pyxis_native_window_setup(void)
{
	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, windows 1,2 and 3 are disabled.  In the future, we may
	 * want to use them to do scatter/gather DMA. 
	 *
	 * Window 0 goes at 1 GB and is 1 GB large.
	 */

	*(vuip)PYXIS_W0_BASE = 1U | (PYXIS_DMA_WIN_BASE_DEFAULT & 0xfff00000U);
	*(vuip)PYXIS_W0_MASK = (PYXIS_DMA_WIN_SIZE_DEFAULT - 1) & 0xfff00000U;
	*(vuip)PYXIS_T0_BASE = 0;

	*(vuip)PYXIS_W1_BASE = 0x0 ;
	*(vuip)PYXIS_W2_BASE = 0x0 ;
	*(vuip)PYXIS_W3_BASE = 0x0 ;
	mb();
}

void __init
pyxis_finish_init_arch(void)
{
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
 
	/*
	 * Sigh... For the SRM setup, unless we know apriori what the HAE
	 * contents will be, we need to setup the arbitrary region bases
	 * so we can test against the range of addresses and tailor the
	 * region chosen for the SPARSE memory access.
	 *
	 * See include/asm-alpha/pyxis.h for the SPARSE mem read/write.
	 */
	if (alpha_use_srm_setup) {
		unsigned int pyxis_hae_mem = *(vuip)PYXIS_HAE_MEM;

		alpha_mv.sm_base_r1 = (pyxis_hae_mem      ) & 0xe0000000UL;
		alpha_mv.sm_base_r2 = (pyxis_hae_mem << 16) & 0xf8000000UL;
		alpha_mv.sm_base_r3 = (pyxis_hae_mem << 24) & 0xfc000000UL;

		/*
		 * Set the HAE cache, so that setup_arch() code
		 * will use the SRM setting always. Our readb/writeb
		 * code in pyxis.h expects never to have to change
		 * the contents of the HAE.
		 */
		alpha_mv.hae_cache = pyxis_hae_mem;

#ifndef CONFIG_ALPHA_GENERIC
		/* In a generic kernel, we can always use BWIO.  */
		alpha_mv.mv_readb = pyxis_srm_readb;
		alpha_mv.mv_readw = pyxis_srm_readw;
		alpha_mv.mv_writeb = pyxis_srm_writeb;
		alpha_mv.mv_writew = pyxis_srm_writew;
#endif
	} else {
		*(vuip)PYXIS_HAE_MEM = 0U; mb();
		*(vuip)PYXIS_HAE_MEM; /* re-read to force write */
		*(vuip)PYXIS_HAE_IO = 0; mb();
		*(vuip)PYXIS_HAE_IO;  /* re-read to force write */
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
			ctrl1 = *(vuip)PYXIS_CTRL1;  /* re-read */
		}
	}
}

void __init
pyxis_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	pyxis_enable_errors();
	if (!pyxis_srm_window_setup())
		pyxis_native_window_setup();
	pyxis_finish_init_arch();
}

static int
pyxis_pci_clr_err(void)
{
	PYXIS_jd = *(vuip)PYXIS_ERR;
	DBG_MCK(("PYXIS_pci_clr_err: PYXIS ERR after read 0x%x\n", PYXIS_jd));
	*(vuip)PYXIS_ERR = 0x0180; mb();
	PYXIS_jd = *(vuip)PYXIS_ERR;  /* re-read to force write */
	return 0;
}

void
pyxis_machine_check(unsigned long vector, unsigned long la_ptr,
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

	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */
	mb();
	mb();  /* magic */
	if (PYXIS_mcheck_expected) {
		DBG_MCK(("PYXIS machine check expected\n"));
		PYXIS_mcheck_expected = 0;
		PYXIS_mcheck_taken = 1;
		mb();
		mb();  /* magic */
		draina();
		pyxis_pci_clr_err();
		wrmces(0x7);
		mb();
	}
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

#ifdef DEBUG_MCHECK_DUMP
		{
			unsigned long *ptr = (unsigned long *)la_ptr;;
			long n = mchk_header->size / (2*sizeof(long));

			do
				printk(" +%lx %lx %lx\n", i*sizeof(long),
				       ptr[i], ptr[i+1]);
			while (--i);
		}
#endif
	}
}
