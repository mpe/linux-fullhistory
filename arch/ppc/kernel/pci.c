/*
 * $Id: pci.c,v 1.18 1997/10/29 03:35:07 cort Exp $
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/pci.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

#if !defined(CONFIG_MACH_SPECIFIC)
unsigned long isa_io_base;
unsigned long isa_mem_base;
unsigned long pci_dram_offset;
#endif /* CONFIG_MACH_SPECIFIC */

/*
 * It would be nice if we could create a include/asm/pci.h and have just
 * function ptrs for all these in there, but that isn't the case.
 * We have a function, pcibios_*() which calls the function ptr ptr_pcibios_*()
 * which has been setup by pcibios_init().  This is all to avoid a check
 * for pmac/prep every time we call one of these.  It should also make the move
 * to a include/asm/pcibios.h easier, we can drop the ptr_ on these functions
 * and create pci.h
 *   -- Cort
 */
int (*ptr_pcibios_read_config_byte)(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned char *val);
int (*ptr_pcibios_read_config_word)(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned short *val);
int (*ptr_pcibios_read_config_dword)(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned int *val);
int (*ptr_pcibios_write_config_byte)(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned char val);
int (*ptr_pcibios_write_config_word)(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned short val);
int (*ptr_pcibios_write_config_dword)(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned int val);

extern int pmac_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned char *val);
extern int pmac_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned short *val);
extern int pmac_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned int *val);
extern int pmac_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned char val);
extern int pmac_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned short val);
extern int pmac_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
					   unsigned char offset, unsigned int val);

extern int chrp_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned char *val);
extern int chrp_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned short *val);
extern int chrp_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned int *val);
extern int chrp_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned char val);
extern int chrp_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned short val);
extern int chrp_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
					   unsigned char offset, unsigned int val);

extern int prep_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned char *val);
extern int prep_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
					 unsigned char offset, unsigned short *val);
extern int prep_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned int *val);
extern int prep_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned char val);
extern int prep_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
					  unsigned char offset, unsigned short val);
extern int prep_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
					   unsigned char offset, unsigned int val);

int pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned char *val)
{
	return ptr_pcibios_read_config_byte(bus,dev_fn,offset,val);
}
int pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned short *val)
{
	return ptr_pcibios_read_config_word(bus,dev_fn,offset,val);
}
int pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned int *val)
{
	return ptr_pcibios_read_config_dword(bus,dev_fn,offset,val);
}
int pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned char val)
{
	return ptr_pcibios_write_config_byte(bus,dev_fn,offset,val);
}
int pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned short val)
{
	return ptr_pcibios_write_config_word(bus,dev_fn,offset,val);
}
int pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
			       unsigned char offset, unsigned int val)
{
	return ptr_pcibios_write_config_dword(bus,dev_fn,offset,val);
}

int pcibios_present(void)
{
	return 1;
}

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


__initfunc(unsigned long
	   pcibios_init(unsigned long mem_start,unsigned long mem_end))
{
	return mem_start;
}

__initfunc(void
	   setup_pci_ptrs(void))
{
	switch (_machine) {
	case _MACH_prep:
		ptr_pcibios_read_config_byte = prep_pcibios_read_config_byte;
		ptr_pcibios_read_config_word = prep_pcibios_read_config_word;
		ptr_pcibios_read_config_dword = prep_pcibios_read_config_dword;
		ptr_pcibios_write_config_byte = prep_pcibios_write_config_byte;
		ptr_pcibios_write_config_word = prep_pcibios_write_config_word;
		ptr_pcibios_write_config_dword = prep_pcibios_write_config_dword;
		break;
	case _MACH_Pmac:
		ptr_pcibios_read_config_byte = pmac_pcibios_read_config_byte;
		ptr_pcibios_read_config_word = pmac_pcibios_read_config_word;
		ptr_pcibios_read_config_dword = pmac_pcibios_read_config_dword;
		ptr_pcibios_write_config_byte = pmac_pcibios_write_config_byte;
		ptr_pcibios_write_config_word = pmac_pcibios_write_config_word;
		ptr_pcibios_write_config_dword = pmac_pcibios_write_config_dword;
		break;
	case _MACH_chrp:
		ptr_pcibios_read_config_byte = chrp_pcibios_read_config_byte;
		ptr_pcibios_read_config_word = chrp_pcibios_read_config_word;
		ptr_pcibios_read_config_dword = chrp_pcibios_read_config_dword;
		ptr_pcibios_write_config_byte = chrp_pcibios_write_config_byte;
		ptr_pcibios_write_config_word = chrp_pcibios_write_config_word;
		ptr_pcibios_write_config_dword = chrp_pcibios_write_config_dword;
		break;
	}
}

__initfunc(unsigned long
	   pcibios_fixup(unsigned long mem_start, unsigned long mem_end))
{
	return mem_start;
}
