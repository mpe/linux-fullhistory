/*
 * $Id: residual.c,v 1.2 1997/08/25 06:54:56 cort Exp $
 *
 * Code to deal with the PReP residual data.
 *
 * Written by: Cort Dougan (cort@cs.nmt.edu)
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/blk.h>
#include <linux/ioport.h>
#include <linux/pci.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>
#include <asm/pnp.h>


/*
 * Spit out some info about residual data
 */
void print_residual_device_info(void)
{
	int i;
	union _PnP_TAG_PACKET *pkt;
	PPC_DEVICE *dev;
#define did dev->DeviceId
	
	/* make sure we have residual data first */
	if ( res.ResidualLength == 0 )
		return;
	
	printk("Residual: %ld devices\n", res.ActualNumDevices);
	for ( i = 0;
	      i < res.ActualNumDevices ;
	      i++)
	{
		dev = &res.Devices[i];
		/*
		 * pci devices
		 */
		if ( did.BusId & PCIDEVICE )
		{
			printk("PCI Device:");
			/* unknown vendor */
			if ( !strncmp( "Unknown", pci_strvendor(did.DevId>>16), 7) )
				printk(" id %08lx types %d/%d", did.DevId,
				       did.BaseType, did.SubType);
			/* known vendor */
			else
				printk(" %s %s",
				       pci_strvendor(did.DevId>>16),
				       pci_strdev(did.DevId>>16,
						  did.DevId&0xffff)
					);
			
			if ( did.BusId & PNPISADEVICE )
			{
				printk(" pnp:");
				/* get pnp info on the device */
				pkt = (union _PnP_TAG_PACKET *)
					&res.DevicePnPHeap[dev->AllocatedOffset];
				for (; pkt->S1_Pack.Tag != DF_END_TAG;
				     pkt++ )
				{
					if ( (pkt->S1_Pack.Tag == S4_Packet) ||
					     (pkt->S1_Pack.Tag == S4_Packet_flags) )
						printk(" irq %02x%02x",
						       pkt->S4_Pack.IRQMask[0],
						       pkt->S4_Pack.IRQMask[1]);
				}
			}
			printk("\n");
			continue;
		}
		/*
		 * isa devices
		 */
		if ( did.BusId & ISADEVICE )
		{
			printk("ISA Device: basetype: %d subtype: %d",
			       did.BaseType, did.SubType);
			printk("\n");
			continue;
		}		
		/*
		 * eisa devices
		 */
		if ( did.BusId & EISADEVICE )
		{
			printk("EISA Device: basetype: %d subtype: %d",
			       did.BaseType, did.SubType);
			printk("\n");
			continue;
		}		
		/*
		 * proc bus devices
		 */
		if ( did.BusId & PROCESSORDEVICE )
		{
			printk("ProcBus Device: basetype: %d subtype: %d",
			       did.BaseType, did.SubType);
			printk("\n");
			continue;
		}
		/*
		 * pcmcia devices
		 */
		if ( did.BusId & PCMCIADEVICE )
		{
			printk("PCMCIA Device: basetype: %d subtype: %d",
			       did.BaseType, did.SubType);
			printk("\n");
			continue;
		}		
		printk("Unknown bus access device: busid %lx\n",
		       did.BusId);
	}
}






