/*
 * $Id: pci.c,v 1.43 1998/12/29 18:55:11 cort Exp $
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/openpic.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/gg2.h>

unsigned long isa_io_base;
unsigned long isa_mem_base;
unsigned long pci_dram_offset;

unsigned int * pci_config_address;
unsigned char * pci_config_data;

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

#define decl_config_access_method(name) \
extern int name##_pcibios_read_config_byte(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned char *val); \
extern int name##_pcibios_read_config_word(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned short *val); \
extern int name##_pcibios_read_config_dword(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned int *val); \
extern int name##_pcibios_write_config_byte(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned char val); \
extern int name##_pcibios_write_config_word(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned short val); \
extern int name##_pcibios_write_config_dword(unsigned char bus, \
	unsigned char dev_fn, unsigned char offset, unsigned int val)

#define set_config_access_method(name) \
	ptr_pcibios_read_config_byte = name##_pcibios_read_config_byte; \
	ptr_pcibios_read_config_word = name##_pcibios_read_config_word; \
	ptr_pcibios_read_config_dword = name##_pcibios_read_config_dword; \
	ptr_pcibios_write_config_byte = name##_pcibios_write_config_byte; \
	ptr_pcibios_write_config_word = name##_pcibios_write_config_word; \
	ptr_pcibios_write_config_dword = name##_pcibios_write_config_dword

decl_config_access_method(pmac);
decl_config_access_method(grackle);
decl_config_access_method(gg2);
decl_config_access_method(raven);
decl_config_access_method(prep);
decl_config_access_method(mbx);

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

__initfunc(void pcibios_init(void))
{
}

__initfunc(void
	   setup_pci_ptrs(void))
{
#ifndef CONFIG_MBX  
	PPC_DEVICE *hostbridge;
	switch (_machine) {
	case _MACH_prep:
	  	hostbridge=residual_find_device(PROCESSORDEVICE, NULL,
						BridgeController, PCIBridge,
						-1, 0);
		if (hostbridge && 
		    hostbridge->DeviceId.Interface == PCIBridgeIndirect) {
			PnP_TAG_PACKET * pkt;	
			set_config_access_method(raven);
			pkt=PnP_find_large_vendor_packet(
			       res->DevicePnPHeap+hostbridge->AllocatedOffset, 
			       3, 0);
			if(pkt) { 
#define p pkt->L4_Pack.L4_Data.L4_PPCPack
				pci_config_address= (unsigned *)
				  ld_le32((unsigned *) p.PPCData);
				pci_config_data= (unsigned char *)
				  ld_le32((unsigned *) (p.PPCData+8));
			} else {/* default values */
				pci_config_address= (unsigned *) 0x80000cf8;
				pci_config_data= (unsigned char *) 0x80000cfc; 
			}  
		} else {
			set_config_access_method(prep);
		}
		break;
	case _MACH_Pmac:
		if (find_devices("pci") != 0) {
			/* looks like a G3 powermac */
			set_config_access_method(grackle);
		} else {
			set_config_access_method(pmac);
		}
		break;
	case _MACH_chrp:
		if ( !strncmp("MOT",
			      get_property(find_path_device("/"), "model", NULL),3) )
		{
			isa_io_base = 0xfe000000;
			set_config_access_method(grackle);
		}
		else
		{
			isa_io_base = GG2_ISA_IO_BASE;
			set_config_access_method(gg2);
		}
		break;
	default:
		printk("setup_pci_ptrs(): unknown machine type!\n");
	}
#else  /* CONFIG_MBX */
	set_config_access_method(mbx);
#endif /* CONFIG_MBX */	
#undef set_config_access_method
}

__initfunc(void pcibios_fixup(void))
{
	extern unsigned long route_pci_interrupts(void);
	struct pci_dev *dev;
	extern struct bridge_data **bridges;
	extern unsigned char *Motherboard_map;
	extern unsigned char *Motherboard_routes;
	unsigned char i;
#ifndef CONFIG_MBX
	switch (_machine )
	{
	case _MACH_prep:
		route_pci_interrupts();
		for(dev=pci_devices; dev; dev=dev->next)
		{
			/*
			 * Use our old hard-coded kludge to figure out what
			 * irq this device uses.  This is necessary on things
			 * without residual data. -- Cort
			 */
			unsigned char d = PCI_SLOT(dev->devfn);
			dev->irq = Motherboard_routes[Motherboard_map[d]];
			for ( i = 0 ; i <= 5 ; i++ )
			{
				if ( dev->base_address[i] > 0x10000000 )
				{
					printk("Relocating PCI address %x -> %x\n",
					       dev->base_address[i],
					       (dev->base_address[i] & 0x00FFFFFF)
				               | 0x01000000);
					dev->base_address[i] =
					  (dev->base_address[i] & 0x00FFFFFF) | 0x01000000;
					pci_write_config_dword(dev,
						PCI_BASE_ADDRESS_0+(i*0x4),
						dev->base_address[i] );
				}
			}
#if 0			
			/*
			 * If we have residual data and if it knows about this
			 * device ask it what the irq is.
			 *  -- Cort
			 */
			ppcd = residual_find_device_id( ~0L, dev->device,
							-1,-1,-1, 0);
#endif			
		}
		break;
	case _MACH_chrp:
		/* PCI interrupts are controlled by the OpenPIC */
		for(dev=pci_devices; dev; dev=dev->next)
			if (dev->irq)
				dev->irq = openpic_to_irq(dev->irq);
		break;
	case _MACH_Pmac:
		for(dev=pci_devices; dev; dev=dev->next)
		{
			/*
			 * Open Firmware often doesn't initialize the,
			 * PCI_INTERRUPT_LINE config register properly, so we
			 * should find the device node and se if it has an
			 * AAPL,interrupts property.
			 */
			struct bridge_data *bp = bridges[dev->bus->number];
			struct device_node *node;
			unsigned int *reg;
			unsigned char pin;
			
			if (pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin) ||
			    !pin)
				continue;	/* No interrupt generated -> no fixup */
			for (node = bp->node->child; node != 0;
			     node = node->sibling) {
				reg = (unsigned int *) get_property(node, "reg", 0);
				if (reg == 0 || ((reg[0] >> 8) & 0xff) != dev->devfn)
					continue;
				/* this is the node, see if it has interrupts */
				if (node->n_intrs > 0)
					dev->irq = node->intrs[0].line;
				break;
			}
		}
		break;
	}
#else /* CONFIG_MBX */
	for(dev=pci_devices; dev; dev=dev->next)
	{
	}
#endif /* CONFIG_MBX */
}

__initfunc(void pcibios_fixup_bus(struct pci_bus *bus))
{
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}
