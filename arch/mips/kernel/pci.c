/* $Id: pci.c,v 1.6 1998/08/19 21:53:50 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * MIPS implementation of PCI BIOS services for PCI support.
 *
 * Copyright (C) 1997, 1998 Ralf Baechle
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/pci.h>

#ifdef CONFIG_PCI

struct pci_ops *pci_ops;

/*
 * BIOS32 replacement.
 */
__initfunc(void pcibios_init(void))
{
}

/*
 * Following the generic parts of the MIPS BIOS32 code.
 */

int pcibios_present (void)
{
	return pci_ops != NULL;
}

/*
 * The functions below are machine specific and must be reimplented for
 * each PCI chipset configuration.  We just run the hook to the machine
 * specific implementation.
 */
void pcibios_fixup (void)
{
	return pci_ops->pcibios_fixup();
}

int pcibios_read_config_byte (unsigned char bus, unsigned char dev_fn,
                              unsigned char where, unsigned char *val)
{
	return pci_ops->pcibios_read_config_byte(bus, dev_fn, where, val);
}

int pcibios_read_config_word (unsigned char bus, unsigned char dev_fn,
                              unsigned char where, unsigned short *val)
{
	return pci_ops->pcibios_read_config_word(bus, dev_fn, where, val);
}

int pcibios_read_config_dword (unsigned char bus, unsigned char dev_fn,
                               unsigned char where, unsigned int *val)
{
	return pci_ops->pcibios_read_config_dword(bus, dev_fn, where, val);
}

int pcibios_write_config_byte (unsigned char bus, unsigned char dev_fn,
                               unsigned char where, unsigned char val)
{
	return pci_ops->pcibios_write_config_byte(bus, dev_fn, where, val);
}

int pcibios_write_config_word (unsigned char bus, unsigned char dev_fn,
                               unsigned char where, unsigned short val)
{
	return pci_ops->pcibios_write_config_word(bus, dev_fn, where, val);
}

int pcibios_write_config_dword (unsigned char bus, unsigned char dev_fn,
                                unsigned char where, unsigned int val)
{
	return pci_ops->pcibios_write_config_dword(bus, dev_fn, where, val);
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}

__initfunc(void pcibios_fixup_bus(struct pci_bus *bus))
{
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}

#endif /* defined(CONFIG_PCI) */
