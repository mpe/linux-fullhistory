/*
 * $Id: pci.c,v 1.11 1997/08/13 03:06:14 cort Exp $
 * Common pmac/prep pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
/*#include <linux/bios32.h>*/
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

unsigned long io_base;

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
int (*ptr_pcibios_find_device)(unsigned short vendor, unsigned short dev_id,
			unsigned short index, unsigned char *bus_ptr,
			unsigned char *dev_fn_ptr);
int (*ptr_pcibios_find_class)(unsigned int class_code, unsigned short index,
		       unsigned char *bus_ptr, unsigned char *dev_fn_ptr);

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
extern int pmac_pcibios_find_device(unsigned short vendor, unsigned short dev_id,
			unsigned short index, unsigned char *bus_ptr,
			unsigned char *dev_fn_ptr);
extern int pmac_pcibios_find_class(unsigned int class_code, unsigned short index,
		       unsigned char *bus_ptr, unsigned char *dev_fn_ptr);

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
extern int prep_pcibios_find_device(unsigned short vendor, unsigned short dev_id,
			unsigned short index, unsigned char *bus_ptr,
			unsigned char *dev_fn_ptr);
extern int prep_pcibios_find_class(unsigned int class_code, unsigned short index,
		       unsigned char *bus_ptr, unsigned char *dev_fn_ptr);


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
int pcibios_find_device(unsigned short vendor, unsigned short dev_id,
			unsigned short index, unsigned char *bus_ptr,
			unsigned char *dev_fn_ptr)
{
	return ptr_pcibios_find_device(vendor,dev_id,index,bus_ptr,dev_fn_ptr);
}
int pcibios_find_class(unsigned int class_code, unsigned short index,
		       unsigned char *bus_ptr, unsigned char *dev_fn_ptr)
{
	return ptr_pcibios_find_class(class_code,index,bus_ptr,dev_fn_ptr);
}

int pcibios_present(void)
{
	return 1;
}

__initfunc(unsigned long
pcibios_init(unsigned long mem_start,unsigned long mem_end))
{
	if ( _machine == _MACH_Pmac )
	{
		ptr_pcibios_read_config_byte = pmac_pcibios_read_config_byte;
		ptr_pcibios_read_config_word = pmac_pcibios_read_config_word;
		ptr_pcibios_read_config_dword = pmac_pcibios_read_config_dword;
		ptr_pcibios_write_config_byte = pmac_pcibios_write_config_byte;
		ptr_pcibios_write_config_word = pmac_pcibios_write_config_word;
		ptr_pcibios_write_config_dword = pmac_pcibios_write_config_dword;
		ptr_pcibios_find_device = pmac_pcibios_find_device;
		ptr_pcibios_find_class = pmac_pcibios_find_class;
	}
	else /* prep */
	{
		ptr_pcibios_read_config_byte = prep_pcibios_read_config_byte;
		ptr_pcibios_read_config_word = prep_pcibios_read_config_word;
		ptr_pcibios_read_config_dword = prep_pcibios_read_config_dword;
		ptr_pcibios_write_config_byte = prep_pcibios_write_config_byte;
		ptr_pcibios_write_config_word = prep_pcibios_write_config_word;
		ptr_pcibios_write_config_dword = prep_pcibios_write_config_dword;
		ptr_pcibios_find_device = prep_pcibios_find_device;
		ptr_pcibios_find_class = prep_pcibios_find_class;
	}
	return mem_start;
}

__initfunc(unsigned long
pcibios_fixup(unsigned long mem_start, unsigned long mem_end))
{
	return mem_start;
}
