/*
 * Code common to all LCA chips.
 *
 * Written by David Mosberger (davidm@cs.arizona.edu) with some code
 * taken from Dave Rusling's (david.rusling@reo.mts.dec.com) 32-bit
 * bios code.
 */
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/bios32.h>
#include <linux/pci.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/io.h>

/*
 * BIOS32-style PCI interface:
 */

#ifdef CONFIG_ALPHA_LCA

#define vulp	volatile unsigned long *

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
 *	(e.g., scsi and ethernet).
 * 
 *	The register selects a DWORD (32 bit) register offset.  Hence it
 *	doesn't get shifted by 2 bits as we want to "drop" the bottom two
 *	bits.
 */
static int mk_conf_addr(unsigned char bus, unsigned char device_fn,
			unsigned char where, unsigned long *pci_addr)
{
	unsigned long addr;

	if (bus == 0) {
		int device = device_fn >> 3;
		int func = device_fn & 0x7;

		/* type 0 configuration cycle: */

		if (device > 12) {
			return -1;
		}

		*((volatile unsigned long*) LCA_IOC_CONF) = 0;
		addr = (1 << (11 + device)) | (func << 8) | where;
	} else {
		/* type 1 configuration cycle: */
		*((volatile unsigned long*) LCA_IOC_CONF) = 1;
		addr = (bus << 16) | (device_fn << 8) | where;
	}
	*pci_addr = addr;
	return 0;
}


static unsigned int conf_read(unsigned long addr)
{
	unsigned long flags, code, stat0;
	unsigned int value;

	save_flags(flags);
	cli();

	/* reset status register to avoid loosing errors: */
	stat0 = *((volatile unsigned long*)LCA_IOC_STAT0);
	*((volatile unsigned long*)LCA_IOC_STAT0) = stat0;
	mb();

	/* access configuration space: */

	value = *((volatile unsigned int*)addr);
	draina();

	stat0 = *((unsigned long*)LCA_IOC_STAT0);
	if (stat0 & LCA_IOC_STAT0_ERR) {
		code = ((stat0 >> LCA_IOC_STAT0_CODE_SHIFT)
			& LCA_IOC_STAT0_CODE_MASK);
		if (code != 1) {
			printk("lca.c:conf_read: got stat0=%lx\n", stat0);
		}

		/* reset error status: */
		*((volatile unsigned long*)LCA_IOC_STAT0) = stat0;
		mb();
		wrmces(0x7);			/* reset machine check */

		value = 0xffffffff;
	}
	restore_flags(flags);
	return value;
}


static void conf_write(unsigned long addr, unsigned int value)
{
	unsigned long flags, code, stat0;

	save_flags(flags);	/* avoid getting hit by machine check */
	cli();

	/* reset status register to avoid loosing errors: */
	stat0 = *((volatile unsigned long*)LCA_IOC_STAT0);
	*((volatile unsigned long*)LCA_IOC_STAT0) = stat0;
	mb();

	/* access configuration space: */

	*((volatile unsigned int*)addr) = value;
	draina();

	stat0 = *((unsigned long*)LCA_IOC_STAT0);
	if (stat0 & LCA_IOC_STAT0_ERR) {
		code = ((stat0 >> LCA_IOC_STAT0_CODE_SHIFT)
			& LCA_IOC_STAT0_CODE_MASK);
		if (code != 1) {
			printk("lca.c:conf_write: got stat0=%lx\n", stat0);
		}

		/* reset error status: */
		*((volatile unsigned long*)LCA_IOC_STAT0) = stat0;
		mb();
		wrmces(0x7);			/* reset machine check */
	}
	restore_flags(flags);
}


int pcibios_read_config_byte (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned char *value)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	*value = 0xff;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x00;
	*value = conf_read(addr) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}


int pcibios_read_config_word (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned short *value)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	*value = 0xffff;

	if (where & 0x1) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	if (mk_conf_addr(bus, device_fn, where, &pci_addr)) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x08;
	*value = conf_read(addr) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}


int pcibios_read_config_dword (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned int *value)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	*value = 0xffffffff;
	if (where & 0x3) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	if (mk_conf_addr(bus, device_fn, where, &pci_addr)) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x18;
	*value = conf_read(addr);
	return PCIBIOS_SUCCESSFUL;
}


int pcibios_write_config_byte (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned char value)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x00;
	conf_write(addr, value << ((where & 3) * 8));
	return PCIBIOS_SUCCESSFUL;
}


int pcibios_write_config_word (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned short value)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x08;
	conf_write(addr, value << ((where & 3) * 8));
	return PCIBIOS_SUCCESSFUL;
}


int pcibios_write_config_dword (unsigned char bus, unsigned char device_fn,
				unsigned char where, unsigned int value)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x18;
	conf_write(addr, value << ((where & 3) * 8));
	return PCIBIOS_SUCCESSFUL;
}


unsigned long lca_init(unsigned long mem_start, unsigned long mem_end)
{
	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, window 1 is disabled.  In the future, we may
	 * want to use it to do scatter/gather DMA.  Window 0
	 * goes at 1 GB and is 1 GB large.
	 */
	*(vulp)LCA_IOC_W_BASE1 = 0UL<<33;
	*(vulp)LCA_IOC_W_BASE0 = 1UL<<33 | LCA_DMA_WIN_BASE;
	*(vulp)LCA_IOC_W_MASK0 = LCA_DMA_WIN_SIZE - 1;
	*(vulp)LCA_IOC_T_BASE0 = 0;
	return mem_start;
}




/*
 * Constants used during machine-check handling.  I suppose these
 * could be moved into lca.h but I don't see much reason why anybody
 * else would want to use them.
 */
#define ESR_EAV	(1UL<< 0)	/* error address valid */
#define ESR_CEE	(1UL<< 1)	/* correctable error */
#define ESR_UEE (1UL<< 2)	/* uncorrectable error */
#define ESR_NXM (1UL<<12)	/* non-existent memory */


void lca_machine_check (unsigned long vector, unsigned long la, struct pt_regs *regs)
{
	const char * reason;
	union el_lca el;
	char buf[128];

	printk("lca: machine check (la=0x%lx)\n", la);
	el.c = (struct el_common *) la;
	/*
	 * The first quadword after the common header always seems to
	 * be the machine check reason---don't know why this isn't
	 * part of the common header instead.
	 */
	switch (el.s->reason) {
	      case MCHK_K_TPERR:	reason = "tag parity error"; break;
	      case MCHK_K_TCPERR:	reason = "tag something parity error"; break;
	      case MCHK_K_HERR:		reason = "access to non-existent memory"; break;
	      case MCHK_K_ECC_C:	reason = "correctable ECC error"; break;
	      case MCHK_K_ECC_NC:	reason = "non-correctable ECC error"; break;
	      case MCHK_K_CACKSOFT:	reason = "MCHK_K_CACKSOFT"; break; /* what's this? */
	      case MCHK_K_BUGCHECK:	reason = "illegal exception in PAL mode"; break;
	      case MCHK_K_OS_BUGCHECK:	reason = "callsys in kernel mode"; break;
	      case MCHK_K_DCPERR:	reason = "d-cache parity error"; break;
	      case MCHK_K_ICPERR:	reason = "i-cache parity error"; break;
	      case MCHK_K_SIO_SERR:	reason = "SIO SERR occurred on on PCI bus"; break;
	      case MCHK_K_SIO_IOCHK:	reason = "SIO IOCHK occurred on ISA bus"; break;
	      case MCHK_K_DCSR:		reason = "MCHK_K_DCSR"; break;
	      case MCHK_K_UNKNOWN:
	      default:
		sprintf(buf, "reason for machine-check unknown (0x%lx)", el.s->reason);
		reason = buf;
		break;
	}

	wrmces(rdmces());	/* reset machine check pending flag */

	switch (el.c->size) {
	      case sizeof(struct el_lca_mcheck_short):
		printk("  Reason: %s (short frame%s):\n",
		       reason, el.c->retry ? ", retryable" : "");
		printk("    esr: %lx  ear: %lx\n", el.s->esr, el.s->ear);
		printk("    dc_stat: %lx  ioc_stat0: %lx  ioc_stat1: %lx\n",
		       el.s->dc_stat, el.s->ioc_stat0, el.s->ioc_stat1);
		break;

	      case sizeof(struct el_lca_mcheck_long):
		printk("  Reason: %s (long frame%s):\n",
		       reason, el.c->retry ? ", retryable" : "");
		printk("    reason: %lx  exc_addr: %lx  dc_stat: %lx\n", 
		       el.l->pt[0], el.l->exc_addr, el.l->dc_stat);
		printk("    esr: %lx  ear: %lx  car: %lx\n", el.l->esr, el.l->ear, el.l->car);
		printk("    ioc_stat0: %lx  ioc_stat1: %lx\n", el.l->ioc_stat0, el.l->ioc_stat1);
		break;

	      default:
		printk("  Unknown errorlog size %d\n", el.c->size);
	}
}

#endif /* CONFIG_ALPHA_LCA */
