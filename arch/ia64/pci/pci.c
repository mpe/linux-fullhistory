/*
 * pci.c - Low-Level PCI Access in IA-64
 *
 * Derived from bios32.c of i386 tree.
 *
 * Copyright (C) 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Note: Above list of copyright holders is incomplete...
 */
#include <linux/config.h>

#include <linux/acpi.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

#include <asm/sal.h>


#ifdef CONFIG_SMP
# include <asm/smp.h>
#endif
#include <asm/irq.h>


#undef DEBUG
#define DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

struct pci_fixup pcibios_fixups[1];

/*
 * Low-level SAL-based PCI configuration access functions. Note that SAL
 * calls are already serialized (via sal_lock), so we don't need another
 * synchronization mechanism here.  Not using segment number (yet).
 */

#define PCI_SAL_ADDRESS(bus, dev, fn, reg) \
	((u64)(bus << 16) | (u64)(dev << 11) | (u64)(fn << 8) | (u64)(reg))

static int
__pci_sal_read (int seg, int bus, int dev, int fn, int reg, int len, u32 *value)
{
	int result = 0;
	u64 data = 0;

	if (!value || (bus > 255) || (dev > 31) || (fn > 7) || (reg > 255))
		return -EINVAL;

	result = ia64_sal_pci_config_read(PCI_SAL_ADDRESS(bus, dev, fn, reg), len, &data);

	*value = (u32) data;

	return result;
}

static int
__pci_sal_write (int seg, int bus, int dev, int fn, int reg, int len, u32 value)
{
	if ((bus > 255) || (dev > 31) || (fn > 7) || (reg > 255))
		return -EINVAL;

	return ia64_sal_pci_config_write(PCI_SAL_ADDRESS(bus, dev, fn, reg), len, value);
}


static int
pci_sal_read (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	return __pci_sal_read(0, bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn),
			      where, size, value);
}

static int
pci_sal_write (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	return __pci_sal_write(0, bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn),
			       where, size, value);
}

struct pci_ops pci_sal_ops = {
	.read = 	pci_sal_read,
	.write =	pci_sal_write
};

struct pci_ops *pci_root_ops = &pci_sal_ops;	/* default to SAL */

static int __init
pci_acpi_init (void)
{
	if (!acpi_pci_irq_init())
		printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");
	else
		printk(KERN_WARNING "PCI: Invalid ACPI-PCI IRQ routing table\n");
	return 0;
}

subsys_initcall(pci_acpi_init);

/* Called by ACPI when it finds a new root bus.  */
struct pci_bus *
pcibios_scan_root (int bus)
{
	struct list_head *list;
	struct pci_bus *pci_bus;

	list_for_each(list, &pci_root_buses) {
		pci_bus = pci_bus_b(list);
		if (pci_bus->number == bus) {
			/* Already scanned */
			printk("PCI: Bus (%02x) already probed\n", bus);
			return pci_bus;
		}
	}

	printk("PCI: Probing PCI hardware on bus (%02x)\n", bus);
	return pci_scan_bus(bus, pci_root_ops, NULL);
}

/*
 *  Called after each bus is probed, but before its children are examined.
 */
void __devinit
pcibios_fixup_bus (struct pci_bus *b)
{
	return;
}

#warning pcibios_update_resource() is now a generic implementation - please check

void __devinit
pcibios_update_irq (struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);

	/* ??? FIXME -- record old value for shutdown.  */
}

static inline int
pcibios_enable_resources (struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	if (!dev)
		return -EINVAL;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx=0; idx<6; idx++) {
		/* Only set up the desired resources.  */
		if (!(mask & (1 << idx)))
			continue;

		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR
			       "PCI: Device %s not available because of resource collisions\n",
			       dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (dev->resource[PCI_ROM_RESOURCE].start)
		cmd |= PCI_COMMAND_MEMORY;
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n", dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

int
pcibios_enable_device (struct pci_dev *dev, int mask)
{
	int ret;

	ret = pcibios_enable_resources(dev, mask);
	if (ret < 0)
		return ret;

	printk(KERN_INFO "PCI: Found IRQ %d for device %s\n", dev->irq, dev->slot_name);
	return acpi_pci_irq_enable(dev);
}

void
pcibios_align_resource (void *data, struct resource *res,
		        unsigned long size, unsigned long align)
{
}

/*
 * PCI BIOS setup, always defaults to SAL interface
 */
char * __init
pcibios_setup (char *str)
{
	return NULL;
}

int
pci_mmap_page_range (struct pci_dev *dev, struct vm_area_struct *vma,
		     enum pci_mmap_state mmap_state, int write_combine)
{
	/*
	 * I/O space cannot be accessed via normal processor loads and stores on this
	 * platform.
	 */
	if (mmap_state == pci_mmap_io)
		/*
		 * XXX we could relax this for I/O spaces for which ACPI indicates that
		 * the space is 1-to-1 mapped.  But at the moment, we don't support
		 * multiple PCI address spaces and the legacy I/O space is not 1-to-1
		 * mapped, so this is moot.
		 */
		return -EINVAL;

	/*
	 * Leave vm_pgoff as-is, the PCI space address is the physical address on this
	 * platform.
	 */
	vma->vm_flags |= (VM_SHM | VM_LOCKED | VM_IO);

	if (write_combine)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_page_range(vma, vma->vm_start, vma->vm_pgoff << PAGE_SHIFT,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

/**
 * pci_cacheline_size - determine cacheline size for PCI devices
 * @dev: void
 *
 * We want to use the line-size of the outer-most cache.  We assume
 * that this line-size is the same for all CPUs.
 *
 * Code mostly taken from arch/ia64/kernel/palinfo.c:cache_info().
 *
 * RETURNS: An appropriate -ERRNO error value on eror, or zero for success.
 */
static unsigned long
pci_cacheline_size (void)
{
	u64 levels, unique_caches;
	s64 status;
	pal_cache_config_info_t cci;
	static u8 cacheline_size;

	if (cacheline_size)
		return cacheline_size;

	status = ia64_pal_cache_summary(&levels, &unique_caches);
	if (status != 0) {
		printk(KERN_ERR "%s: ia64_pal_cache_summary() failed (status=%ld)\n",
		       __FUNCTION__, status);
		return SMP_CACHE_BYTES;
	}

	status = ia64_pal_cache_config_info(levels - 1, /* cache_type (data_or_unified)= */ 2,
					    &cci);
	if (status != 0) {
		printk(KERN_ERR "%s: ia64_pal_cache_config_info() failed (status=%ld)\n",
		       __FUNCTION__, status);
		return SMP_CACHE_BYTES;
	}
	cacheline_size = 1 << cci.pcci_line_size;
	return cacheline_size;
}

/**
 * pcibios_prep_mwi - helper function for drivers/pci/pci.c:pci_set_mwi()
 * @dev: the PCI device for which MWI is enabled
 *
 * For ia64, we can get the cacheline sizes from PAL.
 *
 * RETURNS: An appropriate -ERRNO error value on eror, or zero for success.
 */
int
pcibios_prep_mwi (struct pci_dev *dev)
{
	unsigned long desired_linesize, current_linesize;
	int rc = 0;
	u8 pci_linesize;

	desired_linesize = pci_cacheline_size();

	pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &pci_linesize);
	current_linesize = 4 * pci_linesize;
	if (desired_linesize != current_linesize) {
		printk(KERN_WARNING "PCI: slot %s has incorrect PCI cache line size of %lu bytes,",
		       dev->slot_name, current_linesize);
		if (current_linesize > desired_linesize) {
			printk(" expected %lu bytes instead\n", desired_linesize);
			rc = -EINVAL;
		} else {
			printk(" correcting to %lu\n", desired_linesize);
			pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, desired_linesize / 4);
		}
	}
	return rc;
}
