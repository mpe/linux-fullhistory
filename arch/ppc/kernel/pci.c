/*
 * PCI support
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/bios32.h>
#include <linux/pci.h>

/* #define PCI_DEBUG */

int PCI_conversions[2];

unsigned long pcibios_init(unsigned long mem_start,
			   unsigned long mem_end)
{
	printk("PPC init stub -- cort\n");

	return mem_start;
}

unsigned long pcibios_fixup(unsigned long mem_start, unsigned long mem_end)
{
  return mem_start;
}

unsigned long
_LE_to_BE_long(unsigned long val)
{
	unsigned char *p = (unsigned char *)&val;
	PCI_conversions[0]++;
	return ((p[3] << 24) | (p[2] << 16) | (p[1] << 8) | (p[0] << 0));
}

unsigned short
_LE_to_BE_short(unsigned long val)
{
	unsigned char *p = (unsigned char *)&val;
	PCI_conversions[1]++;
	return ((p[3] << 8) | (p[2] << 0));
}

int
pcibios_present (void)
{
#ifdef PCI_DEBUG	
	_printk("PCI [BIOS] present?\n");
#endif	
	return (1);
}

int
pcibios_read_config_dword (unsigned char bus,
    unsigned char dev, unsigned char offset, unsigned int *val)
{
	unsigned long _val;
	unsigned long *ptr;
	dev >>= 3;
#ifdef PCI_DEBUG	
	_printk("PCI Read config dword[%d.%d.%x] = ", bus, dev, offset);
#endif	
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned long *)(0x80800000 | (1<<dev) | offset);
#ifdef PCI_DEBUG	
		_printk("[%x] ", ptr);
#endif		
		_val = _LE_to_BE_long(*ptr);
	}
#ifdef PCI_DEBUG	
	_printk("%x\n", _val);
#endif	
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_read_config_word (unsigned char bus,
    unsigned char dev, unsigned char offset, unsigned short *val)
{
	unsigned short _val;
	unsigned short *ptr;
	dev >>= 3;
#ifdef PCI_DEBUG	
	_printk("PCI Read config word[%d.%d.%x] = ", bus, dev, offset);
#endif	
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		*val =(unsigned short) 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned short *)(0x80800000 | (1<<dev) | offset);
#ifdef PCI_DEBUG	
		_printk("[%x] ", ptr);
#endif		
		_val = _LE_to_BE_short(*ptr);
	}
#ifdef PCI_DEBUG	
	_printk("%x\n", _val);
#endif		
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_read_config_byte (unsigned char bus,
    unsigned char dev, unsigned char offset, unsigned char *val)
{
	unsigned char _val;
	unsigned char *ptr;
	dev >>= 3;
#ifdef PCI_DEBUG	
	_printk("PCI Read config byte[%d.%d.%x] = ", bus, dev, offset);
#endif		
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		*val = (unsigned char) 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned char *)(0x80800000 | (1<<dev) | offset ^ 1);
#ifdef PCI_DEBUG	
		_printk("[%x] ", ptr);
#endif		
		_val = *ptr;
	}
#ifdef PCI_DEBUG	
	_printk("%x\n", _val);
#endif		
	*val = _val;
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_write_config_dword (unsigned char bus,
    unsigned char dev, unsigned char offset, unsigned int val)
{
	unsigned long _val;
	unsigned long *ptr;
	dev >>= 3;
	_val = _LE_to_BE_long(val);
#ifdef PCI_DEBUG	
	_printk("PCI Write config dword[%d.%d.%x] = %x\n", bus, dev, offset, _val);
#endif		
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned long *)(0x80800000 | (1<<dev) | offset);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_write_config_word (unsigned char bus,
    unsigned char dev, unsigned char offset, unsigned short val)
{
	unsigned short _val;
	unsigned short *ptr;
	dev >>= 3;
	_val = _LE_to_BE_short(val);
#ifdef PCI_DEBUG	
	_printk("PCI Write config word[%d.%d.%x] = %x\n", bus, dev, offset, _val);
#endif		
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned short *)(0x80800000 | (1<<dev) | offset);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_write_config_byte (unsigned char bus,
    unsigned char dev, unsigned char offset, unsigned char val)
{
	unsigned char _val;
	unsigned char *ptr;
	dev >>= 3;
	_val = val;
#ifdef PCI_DEBUG	
	_printk("PCI Write config byte[%d.%d.%x] = %x\n", bus, dev, offset, _val);
#endif		
	if ((bus != 0) || (dev < 11) || (dev > 16))
	{
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else
	{
		ptr = (unsigned char *)(0x80800000 | (1<<dev) | offset ^ 1);
		*ptr = _val;
	}
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_find_device (unsigned short vendor, unsigned short device_id,
		     unsigned short index, unsigned char *bus,
		     unsigned char *dev)
{
	unsigned int w, desired = (device_id << 16) | vendor;
	int devnr;

	if (vendor == 0xffff) {
		return PCIBIOS_BAD_VENDOR_ID;
	}

	for (devnr = 11;  devnr < 16;  devnr++)
	{
		pcibios_read_config_dword(0, devnr<<3, PCI_VENDOR_ID, &w);
		if (w == desired) {
			if (index == 0) {
				*bus = 0;
				*dev = devnr<<3;
				return PCIBIOS_SUCCESSFUL;
			}
			--index;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

int
pcibios_find_class (unsigned int class_code, unsigned short index, 
    unsigned char *bus, unsigned char *dev)
{
	printk("pcibios_find_class\n");
	return PCIBIOS_FUNC_NOT_SUPPORTED;
}    

const char *pcibios_strerror(int error) { _panic("pcibios_strerror"); } 
/*int get_pci_list(char *buf) { _panic("get_pci_list"); } */
