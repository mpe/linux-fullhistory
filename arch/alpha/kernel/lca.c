/*
 * Code common to all LCA chips.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/bios32.h>
#include <linux/pci.h>

#include <asm/system.h>
#include <asm/lca.h>

/*
 * BIOS32-style PCI interface:
 */

/*
 * PCI BIOS32 interface:
 */
#define MAJOR_REV	0
#define MINOR_REV	0

#ifdef CONFIG_PCI

#define mtpr_mces(v) \
({ \
    register unsigned long v0 asm ("0"); \
    register unsigned long a0 asm ("16"); \
    a0 = (v); \
    asm volatile ("call_pal %1 # %0 %2" : "r="(v0) \
		  : "i"(PAL_mtpr_mces), "r"(a0) \
		  : "memory", "0", "1", "16", "22", "23", "24", "25"); \
    v0; \
})

#define draina()	asm volatile ("call_pal %0" :: "i"(PAL_draina))


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the LCA_IOC_CONF register
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 */
static int
mk_conf_addr(unsigned char bus, unsigned char device_fn,
	    unsigned char where, unsigned long *pci_addr)
{
	unsigned long addr;

	if (bus == 0) {
		int device = device_fn >> 3;
		int func = device_fn & 0x7;

		/* type 0 configuration cycle: */

		if (device > 12) {
			return -1;
		} /* if */

		*((volatile unsigned long*) LCA_IOC_CONF) = 0;
		addr = (1 << (11 + device)) | (func << 8) | where;
	} else {
		/* type 1 configuration cycle: */
		*((volatile unsigned long*) LCA_IOC_CONF) = 1;
		addr = (bus << 16) | (device_fn << 8) | where;
	} /* if */
	*pci_addr = addr;

	return 0;
}


static unsigned int
conf_read(unsigned long addr)
{
	unsigned long old_ipl, code, stat0;
	unsigned int value;

	old_ipl = swpipl(7);	/* avoid getting hit by machine check */

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
		mtpr_mces(0x7);			/* reset machine check */

		value = 0xffffffff;
	}
	swpipl(old_ipl);

	return value;
}


static void
conf_write(unsigned long addr, unsigned int value)
{
}


int
pcibios_present (void)
{
	return 1;		/* present if configured */
}


int
pcibios_find_class (unsigned long class_code, unsigned short index,
		    unsigned char *bus, unsigned char *device_fn)
{
	pci_resource_t *dev;
	unsigned long w;

	for (dev = pci_device_list; dev; dev = dev->next) {
		pcibios_read_config_dword(dev->bus, dev->dev_fn,
					  PCI_CLASS_REVISION, &w);
		if ((w >> 8) == class_code) {
			if (index == 0) {
				*bus = dev->bus;
				*device_fn = dev->dev_fn;
				return PCIBIOS_SUCCESSFUL;
			}
			--index;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}


int
pcibios_find_device (unsigned short vendor, unsigned short device_id,
		     unsigned short index, unsigned char *bus,
		     unsigned char *device_fn)
{
	unsigned long w, desired = (device_id << 16) | vendor;
	pci_resource_t *dev;

	if (vendor == 0xffff) {
		return PCIBIOS_BAD_VENDOR_ID;
	}

	for (dev = pci_device_list; dev; dev = dev->next) {
		pcibios_read_config_dword(dev->bus, dev->dev_fn,
					  PCI_VENDOR_ID, &w);
		if (w == desired) {
			if (index == 0) {
				*bus = dev->bus;
				*device_fn = dev->dev_fn;
				return PCIBIOS_SUCCESSFUL;
			}
			--index;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}


int
pcibios_read_config_byte (unsigned char bus, unsigned char device_fn,
			  unsigned char where, unsigned char *value)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	*value = 0xff;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr) < 0) {
		return PCIBIOS_SUCCESSFUL;
	} /* if */

	addr |= (pci_addr << 5) + 0x00;

	*value = conf_read(addr) >> ((where & 3) * 8);

	return PCIBIOS_SUCCESSFUL;
}


int
pcibios_read_config_word (unsigned char bus, unsigned char device_fn,
			  unsigned char where, unsigned short *value)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	*value = 0xffff;

	if (where & 0x1) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	} /* if */

	if (mk_conf_addr(bus, device_fn, where, &pci_addr)) {
		return PCIBIOS_SUCCESSFUL;
	} /* if */

	addr |= (pci_addr << 5) + 0x08;

	*value = conf_read(addr) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}


int
pcibios_read_config_dword (unsigned char bus, unsigned char device_fn,
			   unsigned char where, unsigned long *value)
{
	unsigned long addr = LCA_CONF;
	unsigned long pci_addr;

	*value = 0xffffffff;

	if (where & 0x3) {
		return PCIBIOS_BAD_REGISTER_NUMBER;
	} /* if */

	if (mk_conf_addr(bus, device_fn, where, &pci_addr)) {
		return PCIBIOS_SUCCESSFUL;
	} /* if */

	addr |= (pci_addr << 5) + 0x18;

	*value = conf_read(addr);

	return PCIBIOS_SUCCESSFUL;
}


int
pcibios_write_config_byte (unsigned char bus, unsigned char device_fn,
			   unsigned char where, unsigned char value)
{
	panic("pcibios_write_config_byte");
}

int
pcibios_write_config_word (unsigned char bus, unsigned char device_fn,
			   unsigned char where, unsigned short value)
{
	panic("pcibios_write_config_word");
}

int
pcibios_write_config_dword (unsigned char bus, unsigned char device_fn,
			    unsigned char where, unsigned long value)
{
	panic("pcibios_write_config_dword");
}

#endif /* CONFIG_PCI */


unsigned long
bios32_init(unsigned long memory_start, unsigned long memory_end)
{
#ifdef CONFIG_PCI
	printk("LCA PCI BIOS32 revision %x.%02x\n", MAJOR_REV, MINOR_REV);

	probe_pci();

#if 0
	{
		char buf[4096];

		get_pci_list(buf);
		printk("%s", buf);
	}
#endif

#if 0
	{
		extern void NCR53c810_test(void);
		NCR53c810_test();
	}
#endif
#endif /* CONFIG_PCI */

    return memory_start;
} /* bios32_init */

			/*** end of lca.c ***/
