/*
 * $Id: totalmp.c,v 1.5 1998/08/26 13:58:50 cort Exp $
 *
 * Support for Total Impact's TotalMP PowerPC accelerator board.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/openpic.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/mm.h>

#include <asm/io.h>

extern void totalmp_init(void);

extern inline void openpic_writefield(volatile u_int *addr, u_int mask,
				      u_int field);
__initfunc(void totalmp_init(void))
{
	struct pci_dev *dev;
	u32 val;
	unsigned long ctl_area, ctl_area_phys;

	/* it's a pci card */
	if ( !pci_present() ) return;

	/* search for a MPIC.  For now, we assume
	 * only one TotalMP card installed. -- Cort
	 */
	for(dev=pci_devices; dev; dev=dev->next)
	{
		if ( (dev->vendor == PCI_VENDOR_ID_IBM)
		     && ((dev->device == PCI_DEVICE_ID_IBM_MPIC)
			 || (dev->device==PCI_DEVICE_ID_IBM_MPIC_2)) )
		{
			break;
		}
	}

	if ( !dev ) return;

	OpenPIC = (struct OpenPIC *)bus_to_virt(dev->base_address[0]);
#if 0	
	if ( (ulong)OpenPIC > 0x10000000 )
	{
		printk("TotalMP: relocating base %lx -> %lx\n",
		       (ulong)OpenPIC, ((ulong)OpenPIC & 0x00FFFFFF) | 0x01000000);
		OpenPIC = (struct OpenPIC *)(((ulong)OpenPIC & 0x00FFFFFF) | 0x01000000);
		pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, (ulong)OpenPIC);
	}*/
#endif	
	OpenPIC = (struct OpenPIC *)((ulong)OpenPIC + _IO_BASE);

	openpic_init(0);

	/* put openpic in 8259-cascade mode */
	openpic_writefield(&OpenPIC->Global.Global_Configuration0, 0, 0x20000000);
	/* set ipi to highest priority */
	openpic_writefield(&OpenPIC->Global._IPI_Vector_Priority[0].Reg, 0, 0x000f0000);

	/* allocate and remap the control area to be no-cache */
	ctl_area = __get_free_pages(GFP_ATOMIC, 3);
	ctl_area_phys = (unsigned long) virt_to_phys((void *)ctl_area);
	ctl_area = (unsigned long)ioremap(ctl_area, 0x8000);
	
	/* soft reset cpu 0 */
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &val);
	openpic_writefield(&OpenPIC->Global._Processor_Initialization.Reg, 0, 0x1);

	/* wait for base address reg to change, signaling that cpu 0 is done */
#define wait_for(where) { 					\
	udelay(100);						\
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &val); 	\
	if ( val != 0x77700000 ) 				\
	{ 							\
		printk("TotalMP: CPU0 did not respond: val %x %d\n", val, where); \
		/*free_pages((ulong)phys_to_virt(ctl_area_phys),1);*/ \
		return;						\
	} }
	
	/* tell cpu0 where the control area is */
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,(~val) >> 16);
	wait_for(0);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
			       ((ulong)ctl_area & 0xff000000)>>20);
	wait_for(1);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
			       ((ulong)ctl_area & 0x00ff0000)>>12);
	wait_for(2);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
			       ((ulong)ctl_area & 0x0000ff00)>>4);
	wait_for(3);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
			       ((ulong)ctl_area & 0x000000ff)<<4);
	wait_for(4);
#undef wait_for
	/* wait for cpu0 to "sign-on" */
}

