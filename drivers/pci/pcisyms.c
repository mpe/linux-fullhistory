/*
 *	$Id: pcisyms.c,v 1.8 1998/05/12 07:36:04 mj Exp $
 *
 *	PCI Bus Services -- Exported Symbols
 *
 *	Copyright 1998--2000 Martin Mares <mj@suse.cz>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/dma.h>	/* isa_dma_bridge_buggy */

EXPORT_SYMBOL(pci_read_config_byte);
EXPORT_SYMBOL(pci_read_config_word);
EXPORT_SYMBOL(pci_read_config_dword);
EXPORT_SYMBOL(pci_write_config_byte);
EXPORT_SYMBOL(pci_write_config_word);
EXPORT_SYMBOL(pci_write_config_dword);
EXPORT_SYMBOL(pci_devices);
EXPORT_SYMBOL(pci_root_buses);
EXPORT_SYMBOL(pci_enable_device);
EXPORT_SYMBOL(pci_find_class);
EXPORT_SYMBOL(pci_find_device);
EXPORT_SYMBOL(pci_find_slot);
EXPORT_SYMBOL(pci_set_master);
EXPORT_SYMBOL(pci_set_power_state);
EXPORT_SYMBOL(pci_assign_resource);
EXPORT_SYMBOL(pci_register_driver);
EXPORT_SYMBOL(pci_unregister_driver);
EXPORT_SYMBOL(pci_match_device);
EXPORT_SYMBOL(pci_find_parent_resource);

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pci_setup_device);
EXPORT_SYMBOL(pci_insert_device);
EXPORT_SYMBOL(pci_remove_device);
#endif

/* Obsolete functions */

EXPORT_SYMBOL(pcibios_present);
EXPORT_SYMBOL(pcibios_read_config_byte);
EXPORT_SYMBOL(pcibios_read_config_word);
EXPORT_SYMBOL(pcibios_read_config_dword);
EXPORT_SYMBOL(pcibios_write_config_byte);
EXPORT_SYMBOL(pcibios_write_config_word);
EXPORT_SYMBOL(pcibios_write_config_dword);
EXPORT_SYMBOL(pcibios_find_class);
EXPORT_SYMBOL(pcibios_find_device);

/* Quirk info */

EXPORT_SYMBOL(isa_dma_bridge_buggy);
EXPORT_SYMBOL(pci_pci_problems);

