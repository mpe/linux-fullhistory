/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * MIPS implementation of PCI BIOS services for PCI support.
 */
#include <linux/bios32.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/pci.h>

#ifndef CONFIG_PCI

/*
 * BIOS32 replacement.
 */
__initfunc(unsigned long pcibios_init(unsigned long memory_start,
                                      unsigned long memory_end))
{
	return memory_start;
}

#else /* defined(CONFIG_PCI) */

/*
 * Following the generic parts of the MIPS BIOS32 code.
 */

int pcibios_present (void)
{
	return _pcibios_init != NULL;
}

/*
 * Given the vendor and device ids, find the n'th instance of that device
 * in the system.  
 */
int pcibios_find_device (unsigned short vendor, unsigned short device_id,
			 unsigned short index, unsigned char *bus,
			 unsigned char *devfn)
{
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->vendor == vendor && dev->device == device_id) {
			if (curr == index) {
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++curr;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

/*
 * Given the class, find the n'th instance of that device
 * in the system.
 */
int pcibios_find_class (unsigned int class_code, unsigned short index,
			unsigned char *bus, unsigned char *devfn)
{
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->class == class_code) {
			if (curr == index) {
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++curr;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

const char *pcibios_strerror (int error)
{
	static char buf[80];

	switch (error) {
	case PCIBIOS_SUCCESSFUL:
		return "SUCCESSFUL";

	case PCIBIOS_FUNC_NOT_SUPPORTED:
		return "FUNC_NOT_SUPPORTED";

	case PCIBIOS_BAD_VENDOR_ID:
		return "SUCCESSFUL";

	case PCIBIOS_DEVICE_NOT_FOUND:
		return "DEVICE_NOT_FOUND";

	case PCIBIOS_BAD_REGISTER_NUMBER:
		return "BAD_REGISTER_NUMBER";

	default:
		sprintf (buf, "UNKNOWN RETURN 0x%x", error);
		return buf;
	}
}

/*
 * The functions below are machine specific and must be reimplented for
 * each PCI chipset configuration.  We just run the hook to the machine
 * specific implementation.
 */
unsigned long (*_pcibios_init)(unsigned long memory_start, unsigned long memory_end);
__initfunc(unsigned long pcibios_init(unsigned long memory_start,
                                      unsigned long memory_end))
{
	return _pcibios_init ? _pcibios_init(memory_start, memory_end)
	                     : memory_start;
}

unsigned long (*_pcibios_fixup) (unsigned long memory_start,
                                 unsigned long memory_end);
unsigned long pcibios_fixup (unsigned long memory_start,
                             unsigned long memory_end)
{
	return _pcibios_fixup(memory_start, memory_end);
}

int (*_pcibios_read_config_byte) (unsigned char bus, unsigned char dev_fn,
                                  unsigned char where, unsigned char *val);
int pcibios_read_config_byte (unsigned char bus, unsigned char dev_fn,
                              unsigned char where, unsigned char *val)
{
	return _pcibios_read_config_byte(bus, dev_fn, where, val);
}

int (*_pcibios_read_config_word) (unsigned char bus, unsigned char dev_fn,
                                  unsigned char where, unsigned short *val);
int pcibios_read_config_word (unsigned char bus, unsigned char dev_fn,
                              unsigned char where, unsigned short *val)
{
	return _pcibios_read_config_word(bus, dev_fn, where, val);
}

int (*_pcibios_read_config_dword) (unsigned char bus, unsigned char dev_fn,
                                   unsigned char where, unsigned int *val);
int pcibios_read_config_dword (unsigned char bus, unsigned char dev_fn,
                               unsigned char where, unsigned int *val)
{
	return _pcibios_read_config_dword(bus, dev_fn, where, val);
}

int (*_pcibios_write_config_byte) (unsigned char bus, unsigned char dev_fn,
                                   unsigned char where, unsigned char val);
int pcibios_write_config_byte (unsigned char bus, unsigned char dev_fn,
                               unsigned char where, unsigned char val)
{
	return _pcibios_write_config_byte(bus, dev_fn, where, val);
}

int (*_pcibios_write_config_word) (unsigned char bus, unsigned char dev_fn,
                                   unsigned char where, unsigned short val);
int pcibios_write_config_word (unsigned char bus, unsigned char dev_fn,
                               unsigned char where, unsigned short val)
{
	return _pcibios_write_config_word(bus, dev_fn, where, val);
}

int (*_pcibios_write_config_dword) (unsigned char bus, unsigned char dev_fn,
                                    unsigned char where, unsigned int val);
int pcibios_write_config_dword (unsigned char bus, unsigned char dev_fn,
                                unsigned char where, unsigned int val)
{
	return _pcibios_write_config_dword(bus, dev_fn, where, val);
}

#endif /* defined(CONFIG_PCI) */
