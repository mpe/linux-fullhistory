#include <linux/pci.h>
#include <linux/module.h>
#include <linux/kmod.h>		/* for hotplug_path */

extern int pci_announce_device(struct pci_driver *drv, struct pci_dev *dev);

#ifndef FALSE
#define FALSE	(0)
#define TRUE	(!FALSE)
#endif

static void
run_sbin_hotplug(struct pci_dev *pdev, int insert)
{
	int i;
	char *argv[3], *envp[8];
	char id[20], sub_id[24], bus_id[24], class_id[20];

	if (!hotplug_path[0])
		return;

	sprintf(class_id, "PCI_CLASS=%04X", pdev->class);
	sprintf(id, "PCI_ID=%04X:%04X", pdev->vendor, pdev->device);
	sprintf(sub_id, "PCI_SUBSYS_ID=%04X:%04X", pdev->subsystem_vendor, pdev->subsystem_device);
	sprintf(bus_id, "PCI_SLOT_NAME=%s", pdev->slot_name);

	i = 0;
	argv[i++] = hotplug_path;
	argv[i++] = "pci";
	argv[i] = 0;

	i = 0;
	/* minimal command environment */
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	
	/* other stuff we want to pass to /sbin/hotplug */
	envp[i++] = class_id;
	envp[i++] = id;
	envp[i++] = sub_id;
	envp[i++] = bus_id;
	if (insert)
		envp[i++] = "ACTION=add";
	else
		envp[i++] = "ACTION=remove";
	envp[i] = 0;

	call_usermodehelper (argv [0], argv, envp);
}

/**
 * pci_announce_device_to_drivers - tell the drivers a new device has appeared
 * @dev: the device that has shown up
 *
 * Notifys the drivers that a new device has appeared, and also notifys
 * userspace through /sbin/hotplug.
 */
void
pci_announce_device_to_drivers(struct pci_dev *dev)
{
	struct list_head *ln;

	for(ln=pci_bus_type.drivers.next; ln != &pci_bus_type.drivers; ln=ln->next) {
		struct pci_driver *drv = list_entry(ln, struct pci_driver, node);
		if (drv->remove && pci_announce_device(drv, dev))
			break;
	}

	/* notify userspace of new hotplug device */
	run_sbin_hotplug(dev, TRUE);
}

/**
 * pci_insert_device - insert a hotplug device
 * @dev: the device to insert
 * @bus: where to insert it
 *
 * Add a new device to the device lists and notify userspace (/sbin/hotplug).
 */
void
pci_insert_device(struct pci_dev *dev, struct pci_bus *bus)
{
	list_add_tail(&dev->bus_list, &bus->devices);
	list_add_tail(&dev->global_list, &pci_devices);
#ifdef CONFIG_PROC_FS
	pci_proc_attach_device(dev);
#endif
	pci_announce_device_to_drivers(dev);
}

static void
pci_free_resources(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = dev->resource + i;
		if (res->parent)
			release_resource(res);
	}
}

/**
 * pci_remove_device - remove a hotplug device
 * @dev: the device to remove
 *
 * Delete the device structure from the device lists and 
 * notify userspace (/sbin/hotplug).
 */
void
pci_remove_device(struct pci_dev *dev)
{
	if (dev->driver) {
		if (dev->driver->remove)
			dev->driver->remove(dev);
		dev->driver = NULL;
	}
	list_del(&dev->bus_list);
	list_del(&dev->global_list);
	pci_free_resources(dev);
#ifdef CONFIG_PROC_FS
	pci_proc_detach_device(dev);
#endif

	/* notify userspace of hotplug device removal */
	run_sbin_hotplug(dev, FALSE);
}

EXPORT_SYMBOL(pci_insert_device);
EXPORT_SYMBOL(pci_remove_device);
EXPORT_SYMBOL(pci_announce_device_to_drivers);
