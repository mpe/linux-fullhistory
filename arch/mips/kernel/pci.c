/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * MIPS implementation of PCI BIOS services for PCI support.
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
__initfunc(unsigned long pcibios_init(unsigned long memory_start,
                                      unsigned long memory_end))
{
	return memory_start;
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
unsigned long pcibios_fixup (unsigned long memory_start,
                             unsigned long memory_end)
{
	return pci_ops->pcibios_fixup(memory_start, memory_end);
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

#endif /* defined(CONFIG_PCI) */
