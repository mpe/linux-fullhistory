/* $Id: pci.h,v 1.4 1998/05/08 01:44:30 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Declarations for the MIPS specific implementation of the PCI BIOS32 services.
 */
#ifndef __ASM_MIPS_PCI_H
#define __ASM_MIPS_PCI_H

struct pci_ops {
	unsigned long (*pcibios_fixup) (void);
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
