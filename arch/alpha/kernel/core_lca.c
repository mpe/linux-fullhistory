/*
 *	linux/arch/alpha/kernel/core_lca.c
 *
 * Code common to all LCA core logic chips.
 *
 * Written by David Mosberger (davidm@cs.arizona.edu) with some code
 * taken from Dave Rusling's (david.rusling@reo.mts.dec.com) 32-bit
 * bios code.
 */
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/tty.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/smp.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_lca.h>
#undef __EXTERN_INLINE

#include "proto.h"

/*
 * BIOS32-style PCI interface:
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
#define MCHK_K_UNKNOWN		0x008A
#define MCHK_K_CACKSOFT		0x008C
#define MCHK_K_BUGCHECK		0x008E
#define MCHK_K_OS_BUGCHECK	0x0090
#define MCHK_K_DCPERR		0x0092
#define MCHK_K_ICPERR		0x0094


/*
 * Platform-specific machine-check reasons:
 */
#define MCHK_K_SIO_SERR		0x204	/* all platforms so far */
#define MCHK_K_SIO_IOCHK	0x206	/* all platforms so far */
#define MCHK_K_DCSR		0x208	/* all but Noname */


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the LCA_IOC_CONF register
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | | | | | | | | | | | | | | |F|F|F|R|R|R|R|R|R|0|0|
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
mk_conf_addr(u8 bus, u8 device_fn, u8 where, unsigned long *pci_addr)
{
	unsigned long addr;

	if (bus == 0) {
		int device = device_fn >> 3;
		int func = device_fn & 0x7;

		/* Type 0 configuration cycle.  */

		if (device > 12) {
			return -1;
		}

		*(vulp)LCA_IOC_CONF = 0;
		addr = (1 << (11 + device)) | (func << 8) | where;
	} else {
		/* Type 1 configuration cycle.  */
		*(vulp)LCA_IOC_CONF = 1;
		addr = (bus << 16) | (device_fn << 8) | where;
	}
	*pci_addr = addr;
	return 0;
}

static unsigned int
conf_read(unsigned long addr)
{
	unsigned long flags, code, stat0;
	unsigned int value;

	__save_and_cli(flags);

	/* Reset status register to avoid loosing errors.  */
	stat0 = *(vulp)LCA_IOC_STAT0;
	*(vulp)LCA_IOC_STAT0 = stat0;
	mb();

	/* Access configuration space.  */
	value = *(vuip)addr;
	draina();

	stat0 = *(vulp)LCA_IOC_STAT0;
	if (stat0 & LCA_IOC_STAT0_ERR) {
		code = ((stat0 >> LCA_IOC_STAT0_CODE_SHIFT)
			& LCA_IOC_STAT0_CODE_MASK);
		if (code != 1) {
			printk("lca.c:conf_read: got stat0=%lx\n", stat0);
		}

		/* Reset error status.  */
		*(vulp)LCA_IOC_STAT0 = stat0;
		mb();

		/* Reset machine check.  */
		wrmces(0x7);

		value = 0xffffffff;
	}
	__restore_flags(flags);
	return value;
}

static void
conf_write(unsigned long addr, unsigned int value)
{
	unsigned long flags, code, stat0;

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid loosing errors.  */
	stat0 = *(vulp)LCA_IOC_STAT0;
	*(vulp)LCA_IOC_STAT0 = stat0;
	mb();

	/* Access configuration space.  */
	*(vuip)addr = value;
	draina();

	stat0 = *(vulp)LCA_IOC_STAT0;
	if (stat0 & LCA_IOC_STAT0_ERR) {
		code = ((stat0 >> LCA_IOC_STAT0_CODE_SHIFT)
			& LCA_IOC_STAT0_CODE_MASK);
		if (code != 1) {
			printk("lca.c:conf_write: got stat0=%lx\n", stat0);
		}

		/* Reset error status.  */
		*(vulp)LCA_IOC_STAT0 = stat0;
		mb();

		/* Reset machine check. */
		wrmces(0x7);
	}
	__restore_flags(flags);
}

int
lca_hose_read_config_byte (u8 bus, u8 device_fn, u8 where, u8 *value,
			   struct linux_hose_info *hose)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x00;
	*value = conf_read(addr) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int 
lca_hose_read_config_word (u8 bus, u8 device_fn, u8 where, u16 *value,
			   struct linux_hose_info *hose)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x08;
	*value = conf_read(addr) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int
lca_hose_read_config_dword (u8 bus, u8 device_fn, u8 where, u32 *value,
			    struct linux_hose_info *hose)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x18;
	*value = conf_read(addr);
	return PCIBIOS_SUCCESSFUL;
}

int 
lca_hose_write_config_byte (u8 bus, u8 device_fn, u8 where, u8 value,
			    struct linux_hose_info *hose)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x00;
	conf_write(addr, value << ((where & 3) * 8));
	return PCIBIOS_SUCCESSFUL;
}

int
lca_hose_write_config_word (u8 bus, u8 device_fn, u8 where, u16 value,
			    struct linux_hose_info *hose)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x08;
	conf_write(addr, value << ((where & 3) * 8));
	return PCIBIOS_SUCCESSFUL;
}

int 
lca_hose_write_config_dword (u8 bus, u8 device_fn, u8 where, u32 value,
			     struct linux_hose_info *hose)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (pci_addr << 5) + 0x18;
	conf_write(addr, value << ((where & 3) * 8));
	return PCIBIOS_SUCCESSFUL;
}

void __init
lca_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	switch (alpha_use_srm_setup)
	{
	default:
#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
		/* Check window 0 for enabled and mapped to 0.  */
		if ((*(vulp)LCA_IOC_W_BASE0 & (1UL<<33))
		    && (*(vulp)LCA_IOC_T_BASE0 == 0)) {
			LCA_DMA_WIN_BASE = *(vulp)LCA_IOC_W_BASE0 & 0xffffffffUL;
			LCA_DMA_WIN_SIZE = *(vulp)LCA_IOC_W_MASK0 & 0xffffffffUL;
			LCA_DMA_WIN_SIZE += 1;
#if 0
			printk("lca_init: using Window 0 settings\n");
			printk("lca_init: BASE 0x%lx MASK 0x%lx TRANS 0x%lx\n",
			       *(vulp)LCA_IOC_W_BASE0,
			       *(vulp)LCA_IOC_W_MASK0,
			       *(vulp)LCA_IOC_T_BASE0);
#endif
			break;
		}

		/* Check window 2 for enabled and mapped to 0.  */
		if ((*(vulp)LCA_IOC_W_BASE1 & (1UL<<33))
		    && (*(vulp)LCA_IOC_T_BASE1 == 0)) {
			LCA_DMA_WIN_BASE = *(vulp)LCA_IOC_W_BASE1 & 0xffffffffUL;
			LCA_DMA_WIN_SIZE = *(vulp)LCA_IOC_W_MASK1 & 0xffffffffUL;
			LCA_DMA_WIN_SIZE += 1;
#if 1
			printk("lca_init: using Window 1 settings\n");
			printk("lca_init: BASE 0x%lx MASK 0x%lx TRANS 0x%lx\n",
			       *(vulp)LCA_IOC_W_BASE1,
			       *(vulp)LCA_IOC_W_MASK1,
			       *(vulp)LCA_IOC_T_BASE1);
#endif
			break;
		}

		/* Otherwise, we must use our defaults.  */
		LCA_DMA_WIN_BASE = LCA_DMA_WIN_BASE_DEFAULT;
		LCA_DMA_WIN_SIZE = LCA_DMA_WIN_SIZE_DEFAULT;
#endif
	case 0:
		/*
		 * Set up the PCI->physical memory translation windows.
		 * For now, window 1 is disabled.  In the future, we may
		 * want to use it to do scatter/gather DMA. 
		 *
		 * Window 0 goes at 1 GB and is 1 GB large.
		 */
		*(vulp)LCA_IOC_W_BASE1 = 0UL<<33;

		*(vulp)LCA_IOC_W_BASE0 = 1UL<<33 | LCA_DMA_WIN_BASE_DEFAULT;
		*(vulp)LCA_IOC_W_MASK0 = LCA_DMA_WIN_SIZE_DEFAULT - 1;
		*(vulp)LCA_IOC_T_BASE0 = 0;
		break;
	}

	/*
	 * Disable PCI parity for now.  The NCR53c810 chip has
	 * troubles meeting the PCI spec which results in
	 * data parity errors.
	 */
	*(vulp)LCA_IOC_PAR_DIS = 1UL<<5;
}

/*
 * Constants used during machine-check handling.  I suppose these
 * could be moved into lca.h but I don't see much reason why anybody
 * else would want to use them.
 */

#define ESR_EAV		(1UL<< 0)	/* error address valid */
#define ESR_CEE		(1UL<< 1)	/* correctable error */
#define ESR_UEE		(1UL<< 2)	/* uncorrectable error */
#define ESR_WRE		(1UL<< 3)	/* write-error */
#define ESR_SOR		(1UL<< 4)	/* error source */
#define ESR_CTE		(1UL<< 7)	/* cache-tag error */
#define ESR_MSE		(1UL<< 9)	/* multiple soft errors */
#define ESR_MHE		(1UL<<10)	/* multiple hard errors */
#define ESR_NXM		(1UL<<12)	/* non-existent memory */

#define IOC_ERR		(  1<<4)	/* ioc logs an error */
#define IOC_CMD_SHIFT	0
#define IOC_CMD		(0xf<<IOC_CMD_SHIFT)
#define IOC_CODE_SHIFT	8
#define IOC_CODE	(0xf<<IOC_CODE_SHIFT)
#define IOC_LOST	(  1<<5)
#define IOC_P_NBR	((__u32) ~((1<<13) - 1))

static void
mem_error (unsigned long esr, unsigned long ear)
{
	printk("    %s %s error to %s occurred at address %x\n",
	       ((esr & ESR_CEE) ? "Correctable" :
		(esr & ESR_UEE) ? "Uncorrectable" : "A"),
	       (esr & ESR_WRE) ? "write" : "read",
	       (esr & ESR_SOR) ? "memory" : "b-cache",
	       (unsigned) (ear & 0x1ffffff8));
	if (esr & ESR_CTE) {
		printk("    A b-cache tag parity error was detected.\n");
	}
	if (esr & ESR_MSE) {
		printk("    Several other correctable errors occurred.\n");
	}
	if (esr & ESR_MHE) {
		printk("    Several other uncorrectable errors occurred.\n");
	}
	if (esr & ESR_NXM) {
		printk("    Attempted to access non-existent memory.\n");
	}
}

static void
ioc_error (__u32 stat0, __u32 stat1)
{
	static const char * const pci_cmd[] = {
		"Interrupt Acknowledge", "Special", "I/O Read", "I/O Write",
		"Rsvd 1", "Rsvd 2", "Memory Read", "Memory Write", "Rsvd3",
		"Rsvd4", "Configuration Read", "Configuration Write",
		"Memory Read Multiple", "Dual Address", "Memory Read Line",
		"Memory Write and Invalidate"
	};
	static const char * const err_name[] = {
		"exceeded retry limit", "no device", "bad data parity",
		"target abort", "bad address parity", "page table read error",
		"invalid page", "data error"
	};
	unsigned code = (stat0 & IOC_CODE) >> IOC_CODE_SHIFT;
	unsigned cmd  = (stat0 & IOC_CMD)  >> IOC_CMD_SHIFT;

	printk("    %s initiated PCI %s cycle to address %x"
	       " failed due to %s.\n",
	       code > 3 ? "PCI" : "CPU", pci_cmd[cmd], stat1, err_name[code]);

	if (code == 5 || code == 6) {
		printk("    (Error occurred at PCI memory address %x.)\n",
		       (stat0 & ~IOC_P_NBR));
	}
	if (stat0 & IOC_LOST) {
		printk("    Other PCI errors occurred simultaneously.\n");
	}
}

void
lca_machine_check (unsigned long vector, unsigned long la,
		   struct pt_regs *regs)
{
	unsigned long * ptr;
	const char * reason;
	union el_lca el;
	char buf[128];
	long i;

	printk(KERN_CRIT "lca: machine check (la=0x%lx,pc=0x%lx)\n",
	       la, regs->pc);
	el.c = (struct el_common *) la;

	/*
	 * The first quadword after the common header always seems to
	 * be the machine check reason---don't know why this isn't
	 * part of the common header instead.  In the case of a long
	 * logout frame, the upper 32 bits is the machine check
	 * revision level, which we ignore for now.
	 */
	switch (el.c->code & 0xffffffff) {
	case MCHK_K_TPERR:	reason = "tag parity error"; break;
	case MCHK_K_TCPERR:	reason = "tag control parity error"; break;
	case MCHK_K_HERR:	reason = "access to non-existent memory"; break;
	case MCHK_K_ECC_C:	reason = "correctable ECC error"; break;
	case MCHK_K_ECC_NC:	reason = "non-correctable ECC error"; break;
	case MCHK_K_CACKSOFT:	reason = "MCHK_K_CACKSOFT"; break; /* what's this? */
	case MCHK_K_BUGCHECK:	reason = "illegal exception in PAL mode"; break;
	case MCHK_K_OS_BUGCHECK: reason = "callsys in kernel mode"; break;
	case MCHK_K_DCPERR:	reason = "d-cache parity error"; break;
	case MCHK_K_ICPERR:	reason = "i-cache parity error"; break;
	case MCHK_K_SIO_SERR:	reason = "SIO SERR occurred on PCI bus"; break;
	case MCHK_K_SIO_IOCHK:	reason = "SIO IOCHK occurred on ISA bus"; break;
	case MCHK_K_DCSR:	reason = "MCHK_K_DCSR"; break;
	case MCHK_K_UNKNOWN:
	default:
		sprintf(buf, "reason for machine-check unknown (0x%lx)",
			el.c->code & 0xffffffff);
		reason = buf;
		break;
	}

	wrmces(rdmces());	/* reset machine check pending flag */

	switch (el.c->size) {
	case sizeof(struct el_lca_mcheck_short):
		printk(KERN_CRIT
		       "  Reason: %s (short frame%s, dc_stat=%lx):\n",
		       reason, el.c->retry ? ", retryable" : "",
		       el.s->dc_stat);
		if (el.s->esr & ESR_EAV) {
			mem_error(el.s->esr, el.s->ear);
		}
		if (el.s->ioc_stat0 & IOC_ERR) {
			ioc_error(el.s->ioc_stat0, el.s->ioc_stat1);
		}
		break;

	case sizeof(struct el_lca_mcheck_long):
		printk(KERN_CRIT "  Reason: %s (long frame%s):\n",
		       reason, el.c->retry ? ", retryable" : "");
		printk(KERN_CRIT
		       "    reason: %lx  exc_addr: %lx  dc_stat: %lx\n", 
		       el.l->pt[0], el.l->exc_addr, el.l->dc_stat);
		printk(KERN_CRIT "    car: %lx\n", el.l->car);
		if (el.l->esr & ESR_EAV) {
			mem_error(el.l->esr, el.l->ear);
		}
		if (el.l->ioc_stat0 & IOC_ERR) {
			ioc_error(el.l->ioc_stat0, el.l->ioc_stat1);
		}
		break;

	default:
		printk(KERN_CRIT "  Unknown errorlog size %d\n", el.c->size);
	}

	/* Dump the logout area to give all info.  */

	ptr = (unsigned long *) la;
	for (i = 0; i < el.c->size / sizeof(long); i += 2) {
		printk(KERN_CRIT " +%8lx %016lx %016lx\n",
		       i*sizeof(long), ptr[i], ptr[i+1]);
	}
}

/*
 * The following routines are needed to support the SPEED changing
 * necessary to successfully manage the thermal problem on the AlphaBook1.
 */

void
lca_clock_print(void)
{
        long    pmr_reg;

        pmr_reg = LCA_READ_PMR;

        printk("Status of clock control:\n");
        printk("\tPrimary clock divisor\t0x%lx\n", LCA_GET_PRIMARY(pmr_reg));
        printk("\tOverride clock divisor\t0x%lx\n", LCA_GET_OVERRIDE(pmr_reg));
        printk("\tInterrupt override is %s\n",
	       (pmr_reg & LCA_PMR_INTO) ? "on" : "off"); 
        printk("\tDMA override is %s\n",
	       (pmr_reg & LCA_PMR_DMAO) ? "on" : "off"); 

}

int
lca_get_clock(void)
{
        long    pmr_reg;

        pmr_reg = LCA_READ_PMR;
        return(LCA_GET_PRIMARY(pmr_reg));

}

void
lca_clock_fiddle(int divisor)
{
        long    pmr_reg;

        pmr_reg = LCA_READ_PMR;
        LCA_SET_PRIMARY_CLOCK(pmr_reg, divisor);
	/* lca_norm_clock = divisor; */
        LCA_WRITE_PMR(pmr_reg);
        mb();
}
