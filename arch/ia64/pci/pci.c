/*
 * pci.c - Low-Level PCI Access in IA-64
 *
 * Derived from bios32.c of i386 tree.
 *
 * (c) Copyright 2002, 2005 Hewlett-Packard Development Company, L.P.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 * Copyright (C) 2004 Silicon Graphics, Inc.
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
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/hw_irq.h>


static int pci_routeirq;

/*
 * Low-level SAL-based PCI configuration access functions. Note that SAL
 * calls are already serialized (via sal_lock), so we don't need another
 * synchronization mechanism here.
 */

#define PCI_SAL_ADDRESS(seg, bus, devfn, reg)		\
	(((u64) seg << 24) | (bus << 16) | (devfn << 8) | (reg))

/* SAL 3.2 adds support for extended config space. */

#define PCI_SAL_EXT_ADDRESS(seg, bus, devfn, reg)	\
	(((u64) seg << 28) | (bus << 20) | (devfn << 12) | (reg))

static int
pci_sal_read (unsigned int seg, unsigned int bus, unsigned int devfn,
	      int reg, int len, u32 *value)
{
	u64 addr, data = 0;
	int mode, result;

	if (!value || (seg > 65535) || (bus > 255) || (devfn > 255) || (reg > 4095))
		return -EINVAL;

	if ((seg | reg) <= 255) {
		addr = PCI_SAL_ADDRESS(seg, bus, devfn, reg);
		mode = 0;
	} else {
		addr = PCI_SAL_EXT_ADDRESS(seg, bus, devfn, reg);
		mode = 1;
	}
	result = ia64_sal_pci_config_read(addr, mode, len, &data);
	if (result != 0)
		return -EINVAL;

	*value = (u32) data;
	return 0;
}

static int
pci_sal_write (unsigned int seg, unsigned int bus, unsigned int devfn,
	       int reg, int len, u32 value)
{
	u64 addr;
	int mode, result;

	if ((seg > 65535) || (bus > 255) || (devfn > 255) || (reg > 4095))
		return -EINVAL;

	if ((seg | reg) <= 255) {
		addr = PCI_SAL_ADDRESS(seg, bus, devfn, reg);
		mode = 0;
	} else {
		addr = PCI_SAL_EXT_ADDRESS(seg, bus, devfn, reg);
		mode = 1;
	}
	result = ia64_sal_pci_config_write(addr, mode, len, value);
	if (result != 0)
		return -EINVAL;
	return 0;
}

static struct pci_raw_ops pci_sal_ops = {
	.read = 	pci_sal_read,
	.write =	pci_sal_write
};

struct pci_raw_ops *raw_pci_ops = &pci_sal_ops;

static int
pci_read (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	return raw_pci_ops->read(pci_domain_nr(bus), bus->number,
				 devfn, where, size, value);
}

static int
pci_write (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	return raw_pci_ops->write(pci_domain_nr(bus), bus->number,
				  devfn, where, size, value);
}

struct pci_ops pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

#ifdef CONFIG_NUMA
extern acpi_status acpi_map_iosapic(acpi_handle, u32, void *, void **);
static void acpi_map_iosapics(void)
{
	acpi_get_devices(NULL, acpi_map_iosapic, NULL, NULL);
}
#else
static void acpi_map_iosapics(void)
{
	return;
}
#endif /* CONFIG_NUMA */

static int __init
pci_acpi_init (void)
{
	struct pci_dev *dev = NULL;

	printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");

	acpi_map_iosapics();

	if (pci_routeirq) {
		/*
		 * PCI IRQ routing is set up by pci_enable_device(), but we
		 * also do it here in case there are still broken drivers that
		 * don't use pci_enable_device().
		 */
		printk(KERN_INFO "PCI: Routing interrupts for all devices because \"pci=routeirq\" specified\n");
		for_each_pci_dev(dev)
			acpi_pci_irq_enable(dev);
	} else
		printk(KERN_INFO "PCI: If a device doesn't work, try \"pci=routeirq\".  If it helps, post a report\n");

	return 0;
}

subsys_initcall(pci_acpi_init);

/* Called by ACPI when it finds a new root bus.  */

static struct pci_controller * __devinit
alloc_pci_controller (int seg)
{
	struct pci_controller *controller;

	controller = kmalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return NULL;

	memset(controller, 0, sizeof(*controller));
	controller->segment = seg;
	return controller;
}

static u64 __devinit
add_io_space (struct acpi_resource_address64 *addr)
{
	u64 offset;
	int sparse = 0;
	int i;

	if (addr->address_translation_offset == 0)
		return IO_SPACE_BASE(0);	/* part of legacy IO space */

	if (addr->attribute.io.translation_attribute == ACPI_SPARSE_TRANSLATION)
		sparse = 1;

	offset = (u64) ioremap(addr->address_translation_offset, 0);
	for (i = 0; i < num_io_spaces; i++)
		if (io_space[i].mmio_base == offset &&
		    io_space[i].sparse == sparse)
			return IO_SPACE_BASE(i);

	if (num_io_spaces == MAX_IO_SPACES) {
		printk("Too many IO port spaces\n");
		return ~0;
	}

	i = num_io_spaces++;
	io_space[i].mmio_base = offset;
	io_space[i].sparse = sparse;

	return IO_SPACE_BASE(i);
}

static acpi_status __devinit
count_window (struct acpi_resource *resource, void *data)
{
	unsigned int *windows = (unsigned int *) data;
	struct acpi_resource_address64 addr;
	acpi_status status;

	status = acpi_resource_to_address64(resource, &addr);
	if (ACPI_SUCCESS(status))
		if (addr.resource_type == ACPI_MEMORY_RANGE ||
		    addr.resource_type == ACPI_IO_RANGE)
			(*windows)++;

	return AE_OK;
}

struct pci_root_info {
	struct pci_controller *controller;
	char *name;
};

static __devinit acpi_status add_window(struct acpi_resource *res, void *data)
{
	struct pci_root_info *info = data;
	struct pci_window *window;
	struct acpi_resource_address64 addr;
	acpi_status status;
	unsigned long flags, offset = 0;
	struct resource *root;

	status = acpi_resource_to_address64(res, &addr);
	if (!ACPI_SUCCESS(status))
		return AE_OK;

	if (!addr.address_length)
		return AE_OK;

	if (addr.resource_type == ACPI_MEMORY_RANGE) {
		flags = IORESOURCE_MEM;
		root = &iomem_resource;
		offset = addr.address_translation_offset;
	} else if (addr.resource_type == ACPI_IO_RANGE) {
		flags = IORESOURCE_IO;
		root = &ioport_resource;
		offset = add_io_space(&addr);
		if (offset == ~0)
			return AE_OK;
	} else
		return AE_OK;

	window = &info->controller->window[info->controller->windows++];
	window->resource.name = info->name;
	window->resource.flags = flags;
	window->resource.start = addr.min_address_range + offset;
	window->resource.end = addr.max_address_range + offset;
	window->resource.child = NULL;
	window->offset = offset;

	if (insert_resource(root, &window->resource)) {
		printk(KERN_ERR "alloc 0x%lx-0x%lx from %s for %s failed\n",
			window->resource.start, window->resource.end,
			root->name, info->name);
	}

	return AE_OK;
}

static void __devinit
pcibios_setup_root_windows(struct pci_bus *bus, struct pci_controller *ctrl)
{
	int i, j;

	j = 0;
	for (i = 0; i < ctrl->windows; i++) {
		struct resource *res = &ctrl->window[i].resource;
		/* HP's firmware has a hack to work around a Windows bug.
		 * Ignore these tiny memory ranges */
		if ((res->flags & IORESOURCE_MEM) &&
		    (res->end - res->start < 16))
			continue;
		if (j >= PCI_BUS_NUM_RESOURCES) {
			printk("Ignoring range [%lx-%lx] (%lx)\n", res->start,
					res->end, res->flags);
			continue;
		}
		bus->resource[j++] = res;
	}
}

struct pci_bus * __devinit
pci_acpi_scan_root(struct acpi_device *device, int domain, int bus)
{
	struct pci_root_info info;
	struct pci_controller *controller;
	unsigned int windows = 0;
	struct pci_bus *pbus;
	char *name;

	controller = alloc_pci_controller(domain);
	if (!controller)
		goto out1;

	controller->acpi_handle = device->handle;

	acpi_walk_resources(device->handle, METHOD_NAME__CRS, count_window,
			&windows);
	controller->window = kmalloc(sizeof(*controller->window) * windows,
			GFP_KERNEL);
	if (!controller->window)
		goto out2;

	name = kmalloc(16, GFP_KERNEL);
	if (!name)
		goto out3;

	sprintf(name, "PCI Bus %04x:%02x", domain, bus);
	info.controller = controller;
	info.name = name;
	acpi_walk_resources(device->handle, METHOD_NAME__CRS, add_window,
			&info);

	pbus = pci_scan_bus(bus, &pci_root_ops, controller);
	if (pbus)
		pcibios_setup_root_windows(pbus, controller);

	return pbus;

out3:
	kfree(controller->window);
out2:
	kfree(controller);
out1:
	return NULL;
}

void pcibios_resource_to_bus(struct pci_dev *dev,
		struct pci_bus_region *region, struct resource *res)
{
	struct pci_controller *controller = PCI_CONTROLLER(dev);
	unsigned long offset = 0;
	int i;

	for (i = 0; i < controller->windows; i++) {
		struct pci_window *window = &controller->window[i];
		if (!(window->resource.flags & res->flags))
			continue;
		if (window->resource.start > res->start)
			continue;
		if (window->resource.end < res->end)
			continue;
		offset = window->offset;
		break;
	}

	region->start = res->start - offset;
	region->end = res->end - offset;
}
EXPORT_SYMBOL(pcibios_resource_to_bus);

void pcibios_bus_to_resource(struct pci_dev *dev,
		struct resource *res, struct pci_bus_region *region)
{
	struct pci_controller *controller = PCI_CONTROLLER(dev);
	unsigned long offset = 0;
	int i;

	for (i = 0; i < controller->windows; i++) {
		struct pci_window *window = &controller->window[i];
		if (!(window->resource.flags & res->flags))
			continue;
		if (window->resource.start - window->offset > region->start)
			continue;
		if (window->resource.end - window->offset < region->end)
			continue;
		offset = window->offset;
		break;
	}

	res->start = region->start + offset;
	res->end = region->end + offset;
}

static void __devinit pcibios_fixup_device_resources(struct pci_dev *dev)
{
	struct pci_bus_region region;
	int i;
	int limit = (dev->hdr_type == PCI_HEADER_TYPE_NORMAL) ? \
		PCI_BRIDGE_RESOURCES : PCI_NUM_RESOURCES;

	for (i = 0; i < limit; i++) {
		if (!dev->resource[i].flags)
			continue;
		region.start = dev->resource[i].start;
		region.end = dev->resource[i].end;
		pcibios_bus_to_resource(dev, &dev->resource[i], &region);
		pci_claim_resource(dev, i);
	}
}

/*
 *  Called after each bus is probed, but before its children are examined.
 */
void __devinit
pcibios_fixup_bus (struct pci_bus *b)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &b->devices, bus_list)
		pcibios_fixup_device_resources(dev);

	return;
}

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
			       pci_name(dev));
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
		printk("PCI: Enabling device %s (%04x -> %04x)\n", pci_name(dev), old_cmd, cmd);
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

	return acpi_pci_irq_enable(dev);
}

#ifdef CONFIG_ACPI_DEALLOCATE_IRQ
void
pcibios_disable_device (struct pci_dev *dev)
{
	acpi_pci_irq_disable(dev);
}
#endif /* CONFIG_ACPI_DEALLOCATE_IRQ */

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
	if (!strcmp(str, "routeirq"))
		pci_routeirq = 1;
	return NULL;
}

int
pci_mmap_page_range (struct pci_dev *dev, struct vm_area_struct *vma,
		     enum pci_mmap_state mmap_state, int write_combine)
{
	/*
	 * I/O space cannot be accessed via normal processor loads and
	 * stores on this platform.
	 */
	if (mmap_state == pci_mmap_io)
		/*
		 * XXX we could relax this for I/O spaces for which ACPI
		 * indicates that the space is 1-to-1 mapped.  But at the
		 * moment, we don't support multiple PCI address spaces and
		 * the legacy I/O space is not 1-to-1 mapped, so this is moot.
		 */
		return -EINVAL;

	/*
	 * Leave vm_pgoff as-is, the PCI space address is the physical
	 * address on this platform.
	 */
	vma->vm_flags |= (VM_SHM | VM_RESERVED | VM_IO);

	if (write_combine && efi_range_is_wc(vma->vm_start,
					     vma->vm_end - vma->vm_start))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

/**
 * ia64_pci_get_legacy_mem - generic legacy mem routine
 * @bus: bus to get legacy memory base address for
 *
 * Find the base of legacy memory for @bus.  This is typically the first
 * megabyte of bus address space for @bus or is simply 0 on platforms whose
 * chipsets support legacy I/O and memory routing.  Returns the base address
 * or an error pointer if an error occurred.
 *
 * This is the ia64 generic version of this routine.  Other platforms
 * are free to override it with a machine vector.
 */
char *ia64_pci_get_legacy_mem(struct pci_bus *bus)
{
	return (char *)__IA64_UNCACHED_OFFSET;
}

/**
 * pci_mmap_legacy_page_range - map legacy memory space to userland
 * @bus: bus whose legacy space we're mapping
 * @vma: vma passed in by mmap
 *
 * Map legacy memory space for this device back to userspace using a machine
 * vector to get the base address.
 */
int
pci_mmap_legacy_page_range(struct pci_bus *bus, struct vm_area_struct *vma)
{
	char *addr;

	addr = pci_get_legacy_mem(bus);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	vma->vm_pgoff += (unsigned long)addr >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= (VM_SHM | VM_RESERVED | VM_IO);

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

/**
 * ia64_pci_legacy_read - read from legacy I/O space
 * @bus: bus to read
 * @port: legacy port value
 * @val: caller allocated storage for returned value
 * @size: number of bytes to read
 *
 * Simply reads @size bytes from @port and puts the result in @val.
 *
 * Again, this (and the write routine) are generic versions that can be
 * overridden by the platform.  This is necessary on platforms that don't
 * support legacy I/O routing or that hard fail on legacy I/O timeouts.
 */
int ia64_pci_legacy_read(struct pci_bus *bus, u16 port, u32 *val, u8 size)
{
	int ret = size;

	switch (size) {
	case 1:
		*val = inb(port);
		break;
	case 2:
		*val = inw(port);
		break;
	case 4:
		*val = inl(port);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * ia64_pci_legacy_write - perform a legacy I/O write
 * @bus: bus pointer
 * @port: port to write
 * @val: value to write
 * @size: number of bytes to write from @val
 *
 * Simply writes @size bytes of @val to @port.
 */
int ia64_pci_legacy_write(struct pci_dev *bus, u16 port, u32 val, u8 size)
{
	int ret = 0;

	switch (size) {
	case 1:
		outb(val, port);
		break;
	case 2:
		outw(val, port);
		break;
	case 4:
		outl(val, port);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
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
		       pci_name(dev), current_linesize);
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

int pci_vector_resources(int last, int nr_released)
{
	int count = nr_released;

 	count += (IA64_LAST_DEVICE_VECTOR - last);

	return count;
}
