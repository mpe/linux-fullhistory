/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Declarations for the MIPS specific implementation of the PCI BIOS32 services.
 *
 * $Id: pci.h,v 1.2 1997/09/20 21:16:37 ralf Exp $
 */
#ifndef __ASM_MIPS_PCI_H
#define __ASM_MIPS_PCI_H

struct pci_ops {
	unsigned long (*pcibios_fixup) (unsigned long memory_start,
	                                unsigned long memory_end);
	int (*pcibios_read_config_byte) (unsigned char bus,
	                                 unsigned char dev_fn,
	                                 unsigned char where,
	                                 unsigned char *val);
	int (*pcibios_read_config_word) (unsigned char bus,
	                                 unsigned char dev_fn,
	                                 unsigned char where,
	                                 unsigned short *val);
	int (*pcibios_read_config_dword) (unsigned char bus,
	                                  unsigned char dev_fn,
	                                  unsigned char where,
	                                  unsigned int *val);
	int (*pcibios_write_config_byte) (unsigned char bus,
	                                  unsigned char dev_fn,
	                                  unsigned char where,
	                                  unsigned char val);
	int (*pcibios_write_config_word) (unsigned char bus,
	                                  unsigned char dev_fn,
	                                  unsigned char where,
	                                  unsigned short val);
	int (*pcibios_write_config_dword) (unsigned char bus,
	                                   unsigned char dev_fn,
	                                   unsigned char where,
	                                   unsigned int val);
};

extern struct pci_ops *pci_ops;

#endif /* __ASM_MIPS_PCI_H */
