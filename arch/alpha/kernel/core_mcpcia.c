/*
 *	linux/arch/alpha/kernel/core_mcpcia.c
 *
 * Code common to all MCbus-PCI Adaptor core logic chipsets
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
#include <linux/delay.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pci.h>
#include <asm/hwrpb.h>
#include <asm/mmu_context.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_mcpcia.h>
#undef __EXTERN_INLINE

#include "proto.h"
#include "bios32.h"

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

/*
 * BIOS32-style PCI interface:
 */

#undef DEBUG_CFG

#ifdef DEBUG_CFG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif


#define DEBUG_MCHECK

#ifdef DEBUG_MCHECK
# define DBG_MCK(args)	printk args
#else
# define DBG_MCK(args)
#endif

static volatile unsigned int MCPCIA_mcheck_expected[NR_CPUS];
static volatile unsigned int MCPCIA_mcheck_taken[NR_CPUS];
static unsigned int MCPCIA_jd[NR_CPUS];

#define MCPCIA_MAX_HOSES 2


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

	__save_and_cli(flags);

	DBG_CFG(("conf_read(addr=0x%lx, type1=%d, hose=%d)\n",
		 addr, type1, hoseno));

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)MCPCIA_CAP_ERR(hoseno);
	*(vuip)MCPCIA_CAP_ERR(hoseno) = stat0; mb();
	temp = *(vuip)MCPCIA_CAP_ERR(hoseno);
	DBG_CFG(("conf_read: MCPCIA CAP_ERR(%d) was 0x%x\n", hoseno, stat0));

	mb();
	draina();
	MCPCIA_mcheck_expected[cpu] = 1;
	MCPCIA_mcheck_taken[cpu] = 0;
	mb();

	/* Access configuration space.  */
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

	__restore_flags(flags);
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

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)MCPCIA_CAP_ERR(hoseno);
	*(vuip)MCPCIA_CAP_ERR(hoseno) = stat0; mb();
	temp = *(vuip)MCPCIA_CAP_ERR(hoseno);
	DBG_CFG(("conf_write: MCPCIA CAP_ERR(%d) was 0x%x\n", hoseno, stat0));

	draina();
	MCPCIA_mcheck_expected[cpu] = 1;
	mb();

	/* Access configuration space.  */
	*((vuip)addr) = value;
	mb();
	mb();  /* magic */
	temp = *(vuip)MCPCIA_CAP_ERR(hoseno); /* read to force the write */
	MCPCIA_mcheck_expected[cpu] = 0;
	mb();

	DBG_CFG(("conf_write(): finished\n"));
	__restore_flags(flags);
}

static int
mk_conf_addr(struct linux_hose_info *hose,
	     u8 bus, u8 device_fn, u8 where,
	     unsigned long *pci_addr, unsigned char *type1)
{
	unsigned long addr;

	if (!pci_probe_enabled)
		return -1;

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
		 " pci_addr=0x%p, type1=0x%p)\n",
		 bus, device_fn, where, pci_addr, type1));

	/* Type 1 configuration cycle for *ALL* busses.  */
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

int
mcpcia_hose_read_config_byte (u8 bus, u8 device_fn, u8 where, u8 *value,
			      struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x00;
	*value = conf_read(addr, type1, hose) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_read_config_word (u8 bus, u8 device_fn, u8 where, u16 *value,
			      struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x08;
	*value = conf_read(addr, type1, hose) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_read_config_dword (u8 bus, u8 device_fn, u8 where, u32 *value,
			       struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x18;
	*value = conf_read(addr, type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_write_config_byte (u8 bus, u8 device_fn, u8 where, u8 value,
			       struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x00;
	conf_write(addr, value << ((where & 3) * 8), type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_write_config_word (u8 bus, u8 device_fn, u8 where, u16 value,
			       struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x08;
	conf_write(addr, value << ((where & 3) * 8), type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_write_config_dword (u8 bus, u8 device_fn, u8 where, u32 value,
				struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x18;
	conf_write(addr, value << ((where & 3) * 8), type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

void __init
mcpcia_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	extern asmlinkage void entInt(void);
	struct linux_hose_info *hose;
	unsigned int mcpcia_err;
	unsigned int pci_rev;
	int h, cpu;

	/* Ho hum.. init_arch is called before init_IRQ, but we need to be
	   able to handle machine checks.  So install the handler now.  */
	wrent(entInt, 0);

	/* Align memory to cache line; we'll be allocating from it.  */
	*mem_start = (*mem_start | 31) + 1;

	cpu = smp_processor_id();

	/* First, find how many hoses we have.  */
	for (h = 0; h < MCPCIA_MAX_HOSES; h++) {

		/* Gotta be REAL careful.  If hose is absent, we get a
		   machine check.  */

		mb();
		mb();
		draina();
		MCPCIA_mcheck_expected[cpu] = 1;
		MCPCIA_mcheck_taken[cpu]    = 0;
		mb();

		/* Access the bus revision word. */
		pci_rev = *(vuip)MCPCIA_REV(h);

		mb();
		mb();  /* magic */
		if (MCPCIA_mcheck_taken[cpu]) {
			MCPCIA_mcheck_taken[cpu] = 0;
			pci_rev = 0xffffffff;
			mb();
		}
		MCPCIA_mcheck_expected[cpu] = 0;
		mb();

#if 0
		printk("mcpcia_init_arch: got 0x%x for PCI_REV for hose %d\n",
		       pci_rev, h);
#endif
		if ((pci_rev >> 16) == PCI_CLASS_BRIDGE_HOST) {
			hose_count++;

			hose = (struct linux_hose_info *)*mem_start;
			*mem_start = (unsigned long)(hose + 1);

			memset(hose, 0, sizeof(*hose));

			*hose_tail = hose;
			hose_tail = &hose->next;

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
	printk("mcpcia_init_arch: found %d hoses\n", hose_count);
#endif

	/* Now do init for each hose.  */
	for (hose = hose_head; hose; hose = hose->next) {
		h = hose->pci_hose_index;
#if 0
		printk("mcpcia_init_arch: -------- hose %d --------\n",h);
		printk("MCPCIA_REV 0x%x\n", *(vuip)MCPCIA_REV(h));
		printk("MCPCIA_WHOAMI 0x%x\n", *(vuip)MCPCIA_WHOAMI(h));
		printk("MCPCIA_HAE_MEM 0x%x\n", *(vuip)MCPCIA_HAE_MEM(h));
		printk("MCPCIA_HAE_IO 0x%x\n", *(vuip)MCPCIA_HAE_IO(h));
		printk("MCPCIA_HAE_DENSE 0x%x\n", *(vuip)MCPCIA_HAE_DENSE(h));
		printk("MCPCIA_INT_CTL 0x%x\n", *(vuip)MCPCIA_INT_CTL(h));
		printk("MCPCIA_INT_REQ 0x%x\n", *(vuip)MCPCIA_INT_REQ(h));
		printk("MCPCIA_INT_TARG 0x%x\n", *(vuip)MCPCIA_INT_TARG(h));
		printk("MCPCIA_INT_ADR 0x%x\n", *(vuip)MCPCIA_INT_ADR(h));
		printk("MCPCIA_INT_ADR_EXT 0x%x\n", *(vuip)MCPCIA_INT_ADR_EXT(h));
		printk("MCPCIA_INT_MASK0 0x%x\n", *(vuip)MCPCIA_INT_MASK0(h));
		printk("MCPCIA_INT_MASK1 0x%x\n", *(vuip)MCPCIA_INT_MASK1(h));
		printk("MCPCIA_HBASE 0x%x\n", *(vuip)MCPCIA_HBASE(h));
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

		switch (alpha_use_srm_setup)
		{
		default:
#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
			/* Check window 0 for enabled and mapped to 0. */
			if (((*(vuip)MCPCIA_W0_BASE(h) & 3) == 1)
			    && (*(vuip)MCPCIA_T0_BASE(h) == 0)
			    && ((*(vuip)MCPCIA_W0_MASK(h) & 0xfff00000U) > 0x0ff00000U)) {
				MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W0_BASE(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W0_MASK(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
				printk("mcpcia_init_arch: using Window 0 settings\n");
				printk("mcpcia_init_arch: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
				       *(vuip)MCPCIA_W0_BASE(h),
				       *(vuip)MCPCIA_W0_MASK(h),
				       *(vuip)MCPCIA_T0_BASE(h));
#endif
				break;
			}

			/* Check window 1 for enabled and mapped to 0.  */
			if (((*(vuip)MCPCIA_W1_BASE(h) & 3) == 1)
			    && (*(vuip)MCPCIA_T1_BASE(h) == 0)
			    && ((*(vuip)MCPCIA_W1_MASK(h) & 0xfff00000U) > 0x0ff00000U)) {
				MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W1_BASE(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W1_MASK(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
				printk("mcpcia_init_arch: using Window 1 settings\n");
				printk("mcpcia_init_arch: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
				       *(vuip)MCPCIA_W1_BASE(h),
				       *(vuip)MCPCIA_W1_MASK(h),
				       *(vuip)MCPCIA_T1_BASE(h));
#endif
				break;
			}

			/* Check window 2 for enabled and mapped to 0.  */
			if (((*(vuip)MCPCIA_W2_BASE(h) & 3) == 1)
			    && (*(vuip)MCPCIA_T2_BASE(h) == 0)
			    && ((*(vuip)MCPCIA_W2_MASK(h) & 0xfff00000U) > 0x0ff00000U)) {
				MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W2_BASE(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W2_MASK(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
				printk("mcpcia_init_arch: using Window 2 settings\n");
				printk("mcpcia_init_arch: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
				       *(vuip)MCPCIA_W2_BASE(h),
				       *(vuip)MCPCIA_W2_MASK(h),
				       *(vuip)MCPCIA_T2_BASE(h));
#endif
				break;
			}

			/* Check window 3 for enabled and mapped to 0.  */
			if (((*(vuip)MCPCIA_W3_BASE(h) & 3) == 1)
			    && (*(vuip)MCPCIA_T3_BASE(h) == 0)
			    && ((*(vuip)MCPCIA_W3_MASK(h) & 0xfff00000U) > 0x0ff00000U)) {
				MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W3_BASE(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W3_MASK(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
				printk("mcpcia_init_arch: using Window 3 settings\n");
				printk("mcpcia_init_arch: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
				       *(vuip)MCPCIA_W3_BASE(h),
				       *(vuip)MCPCIA_W3_MASK(h),
				       *(vuip)MCPCIA_T3_BASE(h));
#endif
				break;
			}

			/* Otherwise, we must use our defaults.  */
			MCPCIA_DMA_WIN_BASE = MCPCIA_DMA_WIN_BASE_DEFAULT;
			MCPCIA_DMA_WIN_SIZE = MCPCIA_DMA_WIN_SIZE_DEFAULT;
#endif
		case 0:
			/*
			 * Set up the PCI->physical memory translation windows.
			 * For now, windows 1,2 and 3 are disabled.  In the
			 * future, we may want to use them to do scatter/
			 * gather DMA.
			 *
			 * Window 0 goes at 2 GB and is 2 GB large.
			 */

			*(vuip)MCPCIA_W0_BASE(h) = 1U | (MCPCIA_DMA_WIN_BASE_DEFAULT & 0xfff00000U);
			*(vuip)MCPCIA_W0_MASK(h) = (MCPCIA_DMA_WIN_SIZE_DEFAULT - 1) & 0xfff00000U;
			*(vuip)MCPCIA_T0_BASE(h) = 0;

			*(vuip)MCPCIA_W1_BASE(h) = 0x0 ;
			*(vuip)MCPCIA_W2_BASE(h) = 0x0 ;
			*(vuip)MCPCIA_W3_BASE(h) = 0x0 ;

			*(vuip)MCPCIA_HBASE(h) = 0x0 ;
			mb();
			break;
		}
#if 0
		{
			unsigned int mcpcia_int_ctl = *((vuip)MCPCIA_INT_CTL(h));
			printk("mcpcia_init_arch: INT_CTL was 0x%x\n", mcpcia_int_ctl);
			*(vuip)MCPCIA_INT_CTL(h) = 1U; mb();
			mcpcia_int_ctl = *(vuip)MCPCIA_INT_CTL(h);
		}
#endif

		/*
		 * Sigh... For the SRM setup, unless we know apriori what the HAE
		 * contents will be, we need to setup the arbitrary region bases
		 * so we can test against the range of addresses and tailor the
		 * region chosen for the SPARSE memory access.
		 *
		 * See include/asm-alpha/mcpcia.h for the SPARSE mem read/write.
		 */
		if (alpha_use_srm_setup) {
			unsigned int mcpcia_hae_mem = *(vuip)MCPCIA_HAE_MEM(h);

			alpha_mv.sm_base_r1 = (mcpcia_hae_mem      ) & 0xe0000000UL;
			alpha_mv.sm_base_r2 = (mcpcia_hae_mem << 16) & 0xf8000000UL;
			alpha_mv.sm_base_r3 = (mcpcia_hae_mem << 24) & 0xfc000000UL;

			/*
			 * Set the HAE cache, so that setup_arch() code
			 * will use the SRM setting always. Our readb/writeb
			 * code in mcpcia.h expects never to have to change
			 * the contents of the HAE.
			 */
			alpha_mv.hae_cache = mcpcia_hae_mem;

			alpha_mv.mv_readb = mcpcia_srm_readb;
			alpha_mv.mv_readw = mcpcia_srm_readw;
			alpha_mv.mv_writeb = mcpcia_srm_writeb;
			alpha_mv.mv_writew = mcpcia_srm_writew;
		} else {
			*(vuip)MCPCIA_HAE_MEM(h) = 0U; mb();
			*(vuip)MCPCIA_HAE_MEM(h); /* read it back. */
			*(vuip)MCPCIA_HAE_IO(h) = 0; mb();
			*(vuip)MCPCIA_HAE_IO(h);  /* read it back. */
		}
	}
}

static int
mcpcia_pci_clr_err(int h)
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

void
mcpcia_machine_check(unsigned long type, unsigned long la_ptr,
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
#endif /* DEBUG_MCHECK_DUMP */
			mcpcia_print_uncorrectable(mchk_logout);
	}
#endif
#endif
}
