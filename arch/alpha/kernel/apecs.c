/*
 * Code common to all APECS chips.
 *
 * Rewritten for Apecs from the lca.c from:
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

#include <asm/system.h>
#include <asm/io.h>
#include <asm/hwrpb.h>
#include <asm/ptrace.h>

extern struct hwrpb_struct *hwrpb;
extern asmlinkage void wrmces(unsigned long mces);
extern int alpha_sys_type;
/*
 * BIOS32-style PCI interface:
 */

#ifdef CONFIG_ALPHA_APECS

#ifdef DEBUG
# define DBG(args)	printk args
#else
# define DBG(args)
#endif

#define vulp	volatile unsigned long *
#define vuip	volatile unsigned int  *

static volatile unsigned int apecs_mcheck_expected = 0;
static volatile unsigned int apecs_mcheck_taken = 0;
static unsigned long apecs_jd, apecs_jd1, apecs_jd2;


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the APECS_HAXR2 register
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
			unsigned char where, unsigned long *pci_addr,
			unsigned char *type1)
{
	unsigned long addr;

	DBG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, pci_addr=0x%p, type1=0x%p)\n",
	     bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		int device = device_fn >> 3;

		/* type 0 configuration cycle: */

		if (device > 20) {
			DBG(("mk_conf_addr: device (%d) > 20, returning -1\n", device));
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
	DBG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}


static unsigned int conf_read(unsigned long addr, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0, value;
	unsigned int haxr2 = 0; /* to keep gcc quiet */

#ifdef CONFIG_ALPHA_AVANTI
	register long s0 asm ("9");
	register long s1 asm ("10");
	register long s2 asm ("11");
	register long s3 asm ("12");
	register long s4 asm ("13");
	register long s5 asm ("14");
	asm volatile ("# %0" : "r="(s0));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" : "r="(s1));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" : "r="(s2));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" : "r="(s3));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" : "r="(s4));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" : "r="(s5));/* SRM X4.2 on Avanti steps on this */
#endif

	save_flags(flags);	/* avoid getting hit by machine check */
	cli();

	DBG(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

	/* reset status register to avoid losing errors: */
	stat0 = *((volatile unsigned int *)APECS_IOC_DCSR);
	*((volatile unsigned int *)APECS_IOC_DCSR) = stat0;
	mb();
	DBG(("conf_read: APECS DCSR was 0x%x\n", stat0));
	/* if Type1 access, must set HAE #2 */
	if (type1) {
		haxr2 = *((unsigned int *)APECS_IOC_HAXR2);
		mb();
		*((unsigned int *)APECS_IOC_HAXR2) = haxr2 | 1;
		DBG(("conf_read: TYPE1 access\n"));
	}

	draina();
	apecs_mcheck_expected = 1;
	apecs_mcheck_taken = 0;
	mb();
	/* access configuration space: */
	value = *((volatile unsigned int *)addr);
	mb();
	mb();
	if (apecs_mcheck_taken) {
		apecs_mcheck_taken = 0;
		value = 0xffffffffU;
		mb();
	}
	apecs_mcheck_expected = 0;
	mb();
	/*
	 * david.rusling@reo.mts.dec.com.  This code is needed for the
	 * EB64+ as it does not generate a machine check (why I don't
	 * know).  When we build kernels for one particular platform
	 * then we can make this conditional on the type.
	 */
#if 1
	draina();

	/* now look for any errors */
	stat0 = *((unsigned int *)APECS_IOC_DCSR);
	DBG(("conf_read: APECS DCSR after read 0x%x\n", stat0));
	if (stat0 & 0xffe0U) { /* is any error bit set? */
		/* if not NDEV, print status */
		if (!(stat0 & 0x0800)) {
			printk("apecs.c:conf_read: got stat0=%x\n", stat0);
		}

		/* reset error status: */
		*((volatile unsigned long *)APECS_IOC_DCSR) = stat0;
		mb();
		wrmces(0x7);			/* reset machine check */
		value = 0xffffffff;
	}
#endif

	/* if Type1 access, must reset HAE #2 so normal IO space ops work */
	if (type1) {
		*((unsigned int *)APECS_IOC_HAXR2) = haxr2 & ~1;
		mb();
	}
	restore_flags(flags);
#ifdef CONFIG_ALPHA_AVANTI
	asm volatile ("# %0" :: "r"(s0));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" :: "r"(s1));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" :: "r"(s2));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" :: "r"(s3));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" :: "r"(s4));/* SRM X4.2 on Avanti steps on this */
	asm volatile ("# %0" :: "r"(s5));/* SRM X4.2 on Avanti steps on this */
#endif
	return value;
}


static void conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned long flags;
	unsigned int stat0;
	unsigned int haxr2 = 0; /* to keep gcc quiet */

	save_flags(flags);	/* avoid getting hit by machine check */
	cli();

	/* reset status register to avoid losing errors: */
	stat0 = *((volatile unsigned int *)APECS_IOC_DCSR);
	*((volatile unsigned int *)APECS_IOC_DCSR) = stat0;
	mb();

	/* if Type1 access, must set HAE #2 */
	if (type1) {
		haxr2 = *((unsigned int *)APECS_IOC_HAXR2);
		mb();
		*((unsigned int *)APECS_IOC_HAXR2) = haxr2 | 1;
	}

	draina();
	apecs_mcheck_expected = 1;
	mb();
	/* access configuration space: */
	*((volatile unsigned int *)addr) = value;
	mb();
	mb();
	apecs_mcheck_expected = 0;
	mb();
	/*
	 * david.rusling@reo.mts.dec.com.  This code is needed for the
	 * EB64+ as it does not generate a machine check (why I don't
	 * know).  When we build kernels for one particular platform
	 * then we can make this conditional on the type.
	 */
#if 1
	draina();

	/* now look for any errors */
	stat0 = *((unsigned int *)APECS_IOC_DCSR);
	if (stat0 & 0xffe0U) { /* is any error bit set? */
		/* if not NDEV, print status */
		if (!(stat0 & 0x0800)) {
			printk("apecs.c:conf_write: got stat0=%x\n", stat0);
		}

		/* reset error status: */
		*((volatile unsigned long *)APECS_IOC_DCSR) = stat0;
		mb();
		wrmces(0x7);			/* reset machine check */
	}
#endif

	/* if Type1 access, must reset HAE #2 so normal IO space ops work */
	if (type1) {
		*((unsigned int *)APECS_IOC_HAXR2) = haxr2 & ~1;
		mb();
	}
	restore_flags(flags);
}


int pcibios_read_config_byte (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned char *value)
{
	unsigned long addr = APECS_CONF;
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
	unsigned long addr = APECS_CONF;
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
	unsigned long addr = APECS_CONF;
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
	unsigned long addr = APECS_CONF;
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
	unsigned long addr = APECS_CONF;
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
	unsigned long addr = APECS_CONF;
	unsigned long pci_addr;
	unsigned char type1;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr, &type1) < 0) {
		return PCIBIOS_SUCCESSFUL;
	}
	addr |= (pci_addr << 5) + 0x18;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}


unsigned long apecs_init(unsigned long mem_start, unsigned long mem_end)
{
	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, window 1 is disabled.  In the future, we may
	 * want to use it to do scatter/gather DMA.  Window 0
	 * goes at 1 GB and is 1 GB large.
	 */
	*(vuip)APECS_IOC_PB2R  = 0U; /* disable window 2 */

	*(vuip)APECS_IOC_PB1R  = 1U<<19 | (APECS_DMA_WIN_BASE & 0xfff00000U);
	*(vuip)APECS_IOC_PM1R  = (APECS_DMA_WIN_SIZE - 1) & 0xfff00000U;
	*(vuip)APECS_IOC_TB1R  = 0;

#ifdef CONFIG_ALPHA_CABRIOLET
	/*
	 * JAE: HACK!!! for now, hardwire if configured...
	 * davidm: Older miniloader versions don't set the clockfrequency
	 * right, so hardcode it for now.
	 */
	if (hwrpb->sys_type == ST_DEC_EB64P) {
		hwrpb->sys_type = ST_DEC_EBPC64;
	}
	if (hwrpb->cycle_freq == 0) {
	    hwrpb->cycle_freq = 275000000;
	}

	/* update checksum: */
	{
	    unsigned long *l, sum;

	    sum = 0;
	    for (l = (unsigned long *) hwrpb; l < (unsigned long *) &hwrpb->chksum; ++l)
	      sum += *l;
	    hwrpb->chksum = sum;
	}
#endif /* CONFIG_ALPHA_CABRIOLET */
	return mem_start;
}

int apecs_pci_clr_err(void)
{
	apecs_jd = *((unsigned long *)APECS_IOC_DCSR);
	if (apecs_jd & 0xffe0L) {
		apecs_jd1 = *((unsigned long *)APECS_IOC_SEAR);
		*((unsigned long *)APECS_IOC_DCSR) = apecs_jd | 0xffe1L;
		apecs_jd = *((unsigned long *)APECS_IOC_DCSR);
		mb();
	}
	*((unsigned long *)APECS_IOC_TBIA) = APECS_IOC_TBIA;
	apecs_jd2 = *((unsigned long *)APECS_IOC_TBIA);
	mb();
	return 0;
}

void apecs_machine_check(unsigned long vector, unsigned long la_ptr,
			 struct pt_regs * regs)
{
	struct el_common *mchk_header;
	struct el_apecs_sysdata_mcheck *mchk_sysdata;

	mchk_header = (struct el_common *)la_ptr;

	mchk_sysdata = 
	  (struct el_apecs_sysdata_mcheck *)(la_ptr + mchk_header->sys_offset);

	DBG(("apecs_machine_check: vector=0x%lx la_ptr=0x%lx\n", vector, la_ptr));
	DBG(("                     pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
	     regs->pc, mchk_header->size, mchk_header->proc_offset, mchk_header->sys_offset));
	DBG(("apecs_machine_check: expected %d DCSR 0x%lx PEAR 0x%lx\n",
	     apecs_mcheck_expected, mchk_sysdata->epic_dcsr, mchk_sysdata->epic_pear));
#ifdef DEBUG
	{
	    unsigned long *ptr;
	    int i;

	    ptr = (unsigned long *)la_ptr;
	    for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
		printk(" +%lx %lx %lx\n", i*sizeof(long), ptr[i], ptr[i+1]);
	    }
	}
#endif /* DEBUG */

	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */
	if (apecs_mcheck_expected && (mchk_sysdata->epic_dcsr && 0x0c00UL)) {
		apecs_mcheck_expected = 0;
		apecs_mcheck_taken = 1;
		mb();
		mb();
		apecs_pci_clr_err();
		wrmces(0x7);
		mb();
		draina();
	}
}

#endif /* CONFIG_ALPHA_APECS */
