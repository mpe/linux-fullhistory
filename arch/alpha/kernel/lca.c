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


void lca_machine_check (unsigned long vector, unsigned long la, struct pt_regs *regs)
{
	unsigned long mces;

	mces = rdmces();
	wrmces(mces);		/* reset machine check asap */
	printk("Machine check (la=0x%lx,mces=0x%lx)\n", la, mces);
	printk("esr=%lx, ear=%lx, ioc_stat0=%lx, ioc_stat1=%lx\n",
	       *(unsigned long*)LCA_MEM_ESR, *(unsigned long*)LCA_MEM_EAR,
	       *(unsigned long*)LCA_IOC_STAT0, *(unsigned long*)LCA_IOC_STAT1);
}

#endif /* CONFIG_ALPHA_LCA */
