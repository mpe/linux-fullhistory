/*
 * File:	portdrv_core.c
 * Purpose:	PCI Express Port Bus Driver's Core Functions
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/pcieport_if.h>

#include "portdrv.h"

extern int pcie_mch_quirk;	/* MSI-quirk Indicator */

static int pcie_port_probe_service(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;
	int status = -ENODEV;

	if (!dev || !dev->driver)
		return status;

 	driver = to_service_driver(dev->driver);
	if (!driver || !driver->probe)
		return status;

	pciedev = to_pcie_device(dev);
	status = driver->probe(pciedev, driver->id_table);
	if (!status) {
		printk(KERN_DEBUG "Load service driver %s on pcie device %s\n",
			driver->name, dev->bus_id);
		get_device(dev);
	}
	return status;
}

static int pcie_port_remove_service(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
 	driver = to_service_driver(dev->driver);
	if (driver && driver->remove) { 
		printk(KERN_DEBUG "Unload service driver %s on pcie device %s\n",
			driver->name, dev->bus_id);
		driver->remove(pciedev);
		put_device(dev);
	}
	return 0;
}

static void pcie_port_shutdown_service(struct device *dev) {}

static int pcie_port_suspend_service(struct device *dev, u32 state, u32 level)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
 	driver = to_service_driver(dev->driver);
	if (driver && driver->suspend)
		driver->suspend(pciedev, state);
	return 0;
}

static int pcie_port_resume_service(struct device *dev, u32 state)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
 	driver = to_service_driver(dev->driver);

	if (driver && driver->resume)
		driver->resume(pciedev);
	return 0;
}

/*
 * release_pcie_device
 *	
 *	Being invoked automatically when device is being removed 
 *	in response to device_unregister(dev) call.
 *	Release all resources being claimed.
 */
static void release_pcie_device(struct device *dev)
{
	printk(KERN_DEBUG "Free Port Service[%s]\n", dev->bus_id);
	kfree(to_pcie_device(dev));			
}

static int is_msi_quirked(struct pci_dev *dev)
{
	int port_type, quirk = 0;
	u16 reg16;

	pci_read_config_word(dev, 
		pci_find_capability(dev, PCI_CAP_ID_EXP) + 
		PCIE_CAPABILITIES_REG, &reg16);
	port_type = (reg16 >> 4) & PORT_TYPE_MASK;
	switch(port_type) {
	case PCIE_RC_PORT:
		if (pcie_mch_quirk == 1)
			quirk = 1;
		break;
	case PCIE_SW_UPSTREAM_PORT:
	case PCIE_SW_DOWNSTREAM_PORT:
	default:
		break;	
	}
	return quirk;
}
	
static int assign_interrupt_mode(struct pci_dev *dev, int *vectors, int mask)
{
	int i, pos, nvec, status = -EINVAL;
	int interrupt_mode = PCIE_PORT_INTx_MODE;

	/* Set INTx as default */
	for (i = 0, nvec = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
		if (mask & (1 << i)) 
			nvec++;
		vectors[i] = dev->irq;
	}
	
	/* Check MSI quirk */
	if (is_msi_quirked(dev))
		return interrupt_mode;

	/* Select MSI-X over MSI if supported */		
	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos) {
		struct msix_entry msix_entries[PCIE_PORT_DEVICE_MAXSERVICES] = 
			{{0, 0}, {0, 1}, {0, 2}, {0, 3}};
		printk("%s Found MSIX capability\n", __FUNCTION__);
		status = pci_enable_msix(dev, msix_entries, nvec);
		if (!status) {
			int j = 0;

			interrupt_mode = PCIE_PORT_MSIX_MODE;
			for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
				if (mask & (1 << i)) 
					vectors[i] = msix_entries[j++].vector;
			}
		}
	} 
	if (status) {
		pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
		if (pos) {
			printk("%s Found MSI capability\n", __FUNCTION__);
			status = pci_enable_msi(dev);
			if (!status) {
				interrupt_mode = PCIE_PORT_MSI_MODE;
				for (i = 0;i < PCIE_PORT_DEVICE_MAXSERVICES;i++)
					vectors[i] = dev->irq;
			}
		}
	} 
	return interrupt_mode;
}

static int get_port_device_capability(struct pci_dev *dev)
{
	int services = 0, pos;
	u16 reg16;
	u32 reg32;

	pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
	pci_read_config_word(dev, pos + PCIE_CAPABILITIES_REG, &reg16);
	/* Hot-Plug Capable */
	if (reg16 & PORT_TO_SLOT_MASK) {
		pci_read_config_dword(dev, 
			pos + PCIE_SLOT_CAPABILITIES_REG, &reg32);
		if (reg32 & SLOT_HP_CAPABLE_MASK)
			services |= PCIE_PORT_SERVICE_HP;
	} 
	/* PME Capable */
	pos = pci_find_capability(dev, PCI_CAP_ID_PME);
	if (pos) 
		services |= PCIE_PORT_SERVICE_PME;
	
	pos = PCI_CFG_SPACE_SIZE;
	while (pos) {
		pci_read_config_dword(dev, pos, &reg32);
		switch (reg32 & 0xffff) {
		case PCI_EXT_CAP_ID_ERR:
			services |= PCIE_PORT_SERVICE_AER;
			pos = reg32 >> 20;
			break;
		case PCI_EXT_CAP_ID_VC:
			services |= PCIE_PORT_SERVICE_VC;
			pos = reg32 >> 20;
			break;
		default:
			pos = 0;
			break;
		}
	}

	return services;
}

static void pcie_device_init(struct pci_dev *parent, struct pcie_device *dev, 
	int port_type, int service_type, int irq, int irq_mode)
{
	struct device *device;

	dev->port = parent;
	dev->interrupt_mode = irq_mode;
	dev->irq = irq;
	dev->id.vendor = parent->vendor;
	dev->id.device = parent->device;
	dev->id.port_type = port_type;
	dev->id.service_type = (1 << service_type);

	/* Initialize generic device interface */
	device = &dev->device;
	memset(device, 0, sizeof(struct device));
	INIT_LIST_HEAD(&device->node);
	INIT_LIST_HEAD(&device->children);
	INIT_LIST_HEAD(&device->bus_list);
	device->bus = &pcie_port_bus_type;
	device->driver = NULL;
	device->driver_data = NULL; 
	device->release = release_pcie_device;	/* callback to free pcie dev */
	sprintf(&device->bus_id[0], "pcie%02x", 
		get_descriptor_id(port_type, service_type));
	device->parent = &parent->dev;
}

static struct pcie_device* alloc_pcie_device(struct pci_dev *parent, 
	int port_type, int service_type, int irq, int irq_mode)
{
	struct pcie_device *device;

	device = kmalloc(sizeof(struct pcie_device), GFP_KERNEL);
	if (!device)
		return NULL;

	memset(device, 0, sizeof(struct pcie_device));
	pcie_device_init(parent, device, port_type, service_type, irq,irq_mode);
	printk(KERN_DEBUG "Allocate Port Service[%s]\n", device->device.bus_id);
	return device;
}

int pcie_port_device_probe(struct pci_dev *dev)
{
	int pos, type;
	u16 reg;

	if (!(pos = pci_find_capability(dev, PCI_CAP_ID_EXP)))
		return -ENODEV;

	pci_read_config_word(dev, pos + PCIE_CAPABILITIES_REG, &reg);
	type = (reg >> 4) & PORT_TYPE_MASK;
	if (	type == PCIE_RC_PORT || type == PCIE_SW_UPSTREAM_PORT ||
		type == PCIE_SW_DOWNSTREAM_PORT )  
		return 0;
 
	return -ENODEV;
}

int pcie_port_device_register(struct pci_dev *dev)
{
	int status, type, capabilities, irq_mode, i;
	int vectors[PCIE_PORT_DEVICE_MAXSERVICES];
	u16 reg16;

	/* Get port type */
	pci_read_config_word(dev, 
		pci_find_capability(dev, PCI_CAP_ID_EXP) + 
		PCIE_CAPABILITIES_REG, &reg16);
	type = (reg16 >> 4) & PORT_TYPE_MASK;

	/* Now get port services */
	capabilities = get_port_device_capability(dev);
	irq_mode = assign_interrupt_mode(dev, vectors, capabilities);

	/* Allocate child services if any */
	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
		struct pcie_device *child;

		if (capabilities & (1 << i)) {
			child = alloc_pcie_device(
				dev, 		/* parent */
				type,		/* port type */ 
				i,		/* service type */
				vectors[i],	/* irq */
				irq_mode	/* interrupt mode */);
			if (child) { 
				status = device_register(&child->device);
				if (status) {
					kfree(child);
					continue;
				}
				get_device(&child->device);
			}
		}
	}
	return 0;
}

#ifdef CONFIG_PM
int pcie_port_device_suspend(struct pci_dev *dev, u32 state)
{
	struct list_head 		*head, *tmp;
	struct device 			*parent, *child;
	struct device_driver 		*driver;
	struct pcie_port_service_driver *service_driver;

	parent = &dev->dev;
	head = &parent->children;
	tmp = head->next;
	while (head != tmp) {
		child = container_of(tmp, struct device, node);
		tmp = tmp->next;
		if (child->bus != &pcie_port_bus_type)
			continue;
		driver = child->driver;
		if (!driver)
			continue;
		service_driver = to_service_driver(driver);
		if (service_driver->suspend)  
			service_driver->suspend(to_pcie_device(child), state);
	}
	return 0; 
}

int pcie_port_device_resume(struct pci_dev *dev) 
{ 
	struct list_head 		*head, *tmp;
	struct device 			*parent, *child;
	struct device_driver 		*driver;
	struct pcie_port_service_driver *service_driver;

	parent = &dev->dev;
	head = &parent->children;
	tmp = head->next;
	while (head != tmp) {
		child = container_of(tmp, struct device, node);
		tmp = tmp->next;
		if (child->bus != &pcie_port_bus_type)
			continue;
		driver = child->driver;
		if (!driver)
			continue;
		service_driver = to_service_driver(driver);
		if (service_driver->resume)  
			service_driver->resume(to_pcie_device(child));
	}
	return 0; 

}
#endif

void pcie_port_device_remove(struct pci_dev *dev)
{
	struct list_head 		*head, *tmp;
	struct device 			*parent, *child;
	struct device_driver 		*driver;
	struct pcie_port_service_driver *service_driver;
	int interrupt_mode = PCIE_PORT_INTx_MODE;

	parent = &dev->dev;
	head = &parent->children;
	tmp = head->next;
	while (head != tmp) {
		child = container_of(tmp, struct device, node);
		tmp = tmp->next;
		if (child->bus != &pcie_port_bus_type)
			continue;
		driver = child->driver;
		if (driver) { 
			service_driver = to_service_driver(driver);
			if (service_driver->remove)  
				service_driver->remove(to_pcie_device(child));
		}
		interrupt_mode = (to_pcie_device(child))->interrupt_mode;
		put_device(child);
		device_unregister(child);
	}
	/* Switch to INTx by default if MSI enabled */
	if (interrupt_mode == PCIE_PORT_MSIX_MODE)
		pci_disable_msix(dev);
	else if (interrupt_mode == PCIE_PORT_MSI_MODE)
		pci_disable_msi(dev);
}

void pcie_port_bus_register(void)
{
	bus_register(&pcie_port_bus_type);
}

void pcie_port_bus_unregister(void)
{
	bus_unregister(&pcie_port_bus_type);
}

int pcie_port_service_register(struct pcie_port_service_driver *new)
{
	new->driver.name = (char *)new->name;
	new->driver.bus = &pcie_port_bus_type;
	new->driver.probe = pcie_port_probe_service;
	new->driver.remove = pcie_port_remove_service;
	new->driver.shutdown = pcie_port_shutdown_service;
	new->driver.suspend = pcie_port_suspend_service;
	new->driver.resume = pcie_port_resume_service;

	return driver_register(&new->driver);
} 

void pcie_port_service_unregister(struct pcie_port_service_driver *new)
{
	driver_unregister(&new->driver);
}

EXPORT_SYMBOL(pcie_port_service_register);
EXPORT_SYMBOL(pcie_port_service_unregister);
