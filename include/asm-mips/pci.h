/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Declarations for the MIPS specific implementation of the PCI BIOS32 services.
 */
#ifndef __ASM_MIPS_PCI_H
#define __ASM_MIPS_PCI_H

extern unsigned long (*_pcibios_init)(unsigned long memory_start, unsigned long memory_end);
extern unsigned long (*_pcibios_fixup) (unsigned long memory_start,
                                        unsigned long memory_end);
extern int (*_pcibios_read_config_byte) (unsigned char bus,
                                         unsigned char dev_fn,
                                         unsigned char where,
                                         unsigned char *val);
extern int (*_pcibios_read_config_word) (unsigned char bus,
                                         unsigned char dev_fn,
                                         unsigned char where,
                                         unsigned short *val);
extern int (*_pcibios_read_config_dword) (unsigned char bus,
                                          unsigned char dev_fn,
                                          unsigned char where,
                                          unsigned int *val);
extern int (*_pcibios_write_config_byte) (unsigned char bus,
                                          unsigned char dev_fn,
                                          unsigned char where,
                                          unsigned char val);
extern int (*_pcibios_write_config_word) (unsigned char bus,
                                          unsigned char dev_fn,
                                          unsigned char where,
                                          unsigned short val);
extern int (*_pcibios_write_config_dword) (unsigned char bus,
                                           unsigned char dev_fn,
                                           unsigned char where,
                                           unsigned int val);

#endif /* __ASM_MIPS_PCI_H */
