/*
 *	$Id: pcisyms.c,v 1.8 1998/05/12 07:36:04 mj Exp $
 *
 *	PCI Bus Services -- Exported Symbols
 *
 *	Copyright 1998 Martin Mares
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/dma.h>	/* isa_dma_bridge_buggy */

EXPORT_SYMBOL(pcibios_present);
EXPORT_SYMBOL(pcibios_read_config_byte);
EXPORT_SYMBOL(pcibios_read_config_word);
EXPORT_SYMBOL(pcibios_read_config_dword);
EXPORT_SYMBOL(pcibios_write_config_byte);
EXPORT_SYMBOL(pcibios_write_config_word);
EXPORT_SYMBOL(pcibios_write_config_dword);
EXPORT_SYMBOL(pci_read_config_byte);
EXPORT_SYMBOL(pci_read_config_word);
EXPORT_SYMBOL(pci_read_config_dword);
EXPORT_SYMBOL(pci_write_config_byte);
EXPORT_SYMBOL(pci_write_config_word);
EXPORT_SYMBOL(pci_write_config_dword);
EXPORT_SYMBOL(pci_devices);
EXPORT_SYMBOL(pci_root);
EXPORT_SYMBOL(pci_find_class);
EXPORT_SYMBOL(pci_find_device);
EXPORT_SYMBOL(pci_find_slot);
EXPORT_SYMBOL(pci_set_master);
#ifdef	CONFIG_PROC_FS
EXPORT_SYMBOL(pci_proc_attach_device);
EXPORT_SYMBOL(pci_proc_detach_device);
#endif

/* Backward compatibility */

EXPORT_SYMBOL(pcibios_find_class);
EXPORT_SYMBOL(pcibios_find_device);

/* Quirk info */

#ifdef CONFIG_PCI_QUIRKS
EXPORT_SYMBOL(isa_dma_bridge_buggy);
#endif

