/*
 * CHRP pci routines.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/openpic.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hydra.h>
#include <asm/prom.h>
#include <asm/gg2.h>
#include <asm/ide.h>
#include <asm/machdep.h>

#include "pci.h"

/* LongTrail */
#define pci_config_addr(bus, dev, offset) \
(GG2_PCI_CONFIG_BASE | ((bus)<<16) | ((dev)<<8) | (offset))

volatile struct Hydra *Hydra = NULL;

/*
 * The VLSI Golden Gate II has only 512K of PCI configuration space, so we
 * limit the bus number to 3 bits
 */

int gg2_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				 unsigned char offset, unsigned char *val)
{
	if (bus > 7) {
		*val = 0xff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	*val = in_8((unsigned char *)pci_config_addr(bus, dev_fn, offset));
	return PCIBIOS_SUCCESSFUL;
}

int gg2_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				 unsigned char offset, unsigned short *val)
{
	if (bus > 7) {
		*val = 0xffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	*val = in_le16((unsigned short *)pci_config_addr(bus, dev_fn, offset));
	return PCIBIOS_SUCCESSFUL;
}


int gg2_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned int *val)
{
	if (bus > 7) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	*val = in_le32((unsigned int *)pci_config_addr(bus, dev_fn, offset));
	return PCIBIOS_SUCCESSFUL;
}

int gg2_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned char val)
{
	if (bus > 7)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_8((unsigned char *)pci_config_addr(bus, dev_fn, offset), val);
	return PCIBIOS_SUCCESSFUL;
}

int gg2_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned short val)
{
	if (bus > 7)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_le16((unsigned short *)pci_config_addr(bus, dev_fn, offset), val);
	return PCIBIOS_SUCCESSFUL;
}

int gg2_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned int val)
{
	if (bus > 7)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_le32((unsigned int *)pci_config_addr(bus, dev_fn, offset), val);
	return PCIBIOS_SUCCESSFUL;
}

#define python_config_address(bus) (unsigned *)((0xfef00000+0xf8000)-(bus*0x100000))
#define python_config_data(bus) ((0xfef00000+0xf8010)-(bus*0x100000))
#define PYTHON_CFA(b, d, o)	(0x80 | ((b<<6) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))
unsigned int python_busnr = 1;

int python_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned char *val)
{
	if (bus > python_busnr) {
		*val = 0xff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	*val = in_8((unsigned char *)python_config_data(bus) + (offset&3));
	return PCIBIOS_SUCCESSFUL;
}

int python_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned short *val)
{
	if (bus > python_busnr) {
		*val = 0xffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	*val = in_le16((unsigned short *)(python_config_data(bus) + (offset&3)));
	return PCIBIOS_SUCCESSFUL;
}


int python_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned int *val)
{
	if (bus > python_busnr) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	*val = in_le32((unsigned *)python_config_data(bus));
	return PCIBIOS_SUCCESSFUL;
}

int python_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned char val)
{
	if (bus > python_busnr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	out_8((volatile unsigned char *)python_config_data(bus) + (offset&3), val);
	return PCIBIOS_SUCCESSFUL;
}

int python_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned short val)
{
	if (bus > python_busnr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	out_le16((volatile unsigned short *)python_config_data(bus) + (offset&3),
		 val);
	return PCIBIOS_SUCCESSFUL;
}

int python_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned int val)
{
	if (bus > python_busnr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	out_le32((unsigned *)python_config_data(bus) + (offset&3), val);
	return PCIBIOS_SUCCESSFUL;
}

    /*
     *  Temporary fixes for PCI devices. These should be replaced by OF query
     *  code -- Geert
     */

static u_char hydra_openpic_initsenses[] __initdata = {
    1,	/* HYDRA_INT_SIO */
    0,	/* HYDRA_INT_SCSI_DMA */
    0,	/* HYDRA_INT_SCCA_TX_DMA */
    0,	/* HYDRA_INT_SCCA_RX_DMA */
    0,	/* HYDRA_INT_SCCB_TX_DMA */
    0,	/* HYDRA_INT_SCCB_RX_DMA */
    1,	/* HYDRA_INT_SCSI */
    1,	/* HYDRA_INT_SCCA */
    1,	/* HYDRA_INT_SCCB */
    1,	/* HYDRA_INT_VIA */
    1,	/* HYDRA_INT_ADB */
    0,	/* HYDRA_INT_ADB_NMI */
    	/* all others are 1 (= default) */
};

int __init
hydra_init(void)
{
	struct device_node *np;

	np = find_devices("mac-io");
	if (np == NULL || np->n_addrs == 0) {
		printk(KERN_WARNING "Warning: no mac-io found\n");
		return 0;
	}
	Hydra = ioremap(np->addrs[0].address, np->addrs[0].size);
	printk("Hydra Mac I/O at %x\n", np->addrs[0].address);
	out_le32(&Hydra->Feature_Control, (HYDRA_FC_SCC_CELL_EN |
					   HYDRA_FC_SCSI_CELL_EN |
					   HYDRA_FC_SCCA_ENABLE |
					   HYDRA_FC_SCCB_ENABLE |
					   HYDRA_FC_ARB_BYPASS |
					   HYDRA_FC_MPIC_ENABLE |
					   HYDRA_FC_SLOW_SCC_PCLK |
					   HYDRA_FC_MPIC_IS_MASTER));
	OpenPIC = (volatile struct OpenPIC *)&Hydra->OpenPIC;
	OpenPIC_InitSenses = hydra_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(hydra_openpic_initsenses);
	return 1;
}

void __init
chrp_pcibios_fixup(void)
{
	struct pci_dev *dev;
	
	/* some of IBM chrps have > 1 bus */
	if ( !strncmp("IBM", get_property(find_path_device("/"),
					 "name", NULL),3) )
	{
		pci_scan_peer_bridge(1);
		pci_scan_peer_bridge(2);
	}
	
	/* PCI interrupts are controlled by the OpenPIC */
	for( dev=pci_devices ; dev; dev=dev->next )
	{
		if ( dev->irq )
			dev->irq = openpic_to_irq( dev->irq );
		/* adjust the io_port for the NCR cards for busses other than 0 -- Cort */
		if ( (dev->bus->number > 0) && (dev->vendor == PCI_VENDOR_ID_NCR) )
			dev->base_address[0] += (dev->bus->number*0x08000000);
		/* these need to be absolute addrs for OF and Matrox FB -- Cort */
		if ( dev->vendor == PCI_VENDOR_ID_MATROX )
		{
			if ( dev->base_address[0] < isa_mem_base )
				dev->base_address[0] += isa_mem_base;
			if ( dev->base_address[1] < isa_mem_base )
				dev->base_address[1] += isa_mem_base;
		}
		/* the F50 identifies the amd as a trident */
		if ( (dev->vendor == PCI_VENDOR_ID_TRIDENT) &&
		      (dev->class == PCI_CLASS_NETWORK_ETHERNET) )
		{
			dev->vendor = PCI_VENDOR_ID_AMD;
			pcibios_write_config_word(dev->bus->number, dev->devfn,
						   PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
		}
	}
}

decl_config_access_method(grackle);
decl_config_access_method(indirect);

void __init
chrp_setup_pci_ptrs(void)
{
	struct device_node *py;
	
        if ( !strncmp("MOT",
                      get_property(find_path_device("/"), "model", NULL),3) )
        {
                pci_dram_offset = 0;
                isa_mem_base = 0xf7000000;
                isa_io_base = 0xfe000000;
                set_config_access_method(grackle);
        }
        else
        {
		if ( (py = find_compatible_devices( "pci", "IBM,python" )) )
		{
			/* find out how many pythons */
			while ( (py = py->next) ) python_busnr++;
                        set_config_access_method(python);
			/*
			 * We base these values on the machine type but should
			 * try to read them from the python controller itself.
			 * -- Cort
			 */
			if ( !strncmp("IBM,7025-F50", get_property(find_path_device("/"), "name", NULL),12) )
			{
				pci_dram_offset = 0x80000000;
				isa_mem_base = 0xa0000000;
				isa_io_base = 0x88000000;
			} else if ( !strncmp("IBM,7043-260",
			   get_property(find_path_device("/"), "name", NULL),12) )
			{
				pci_dram_offset = 0x80000000;
				isa_mem_base = 0xc0000000;
				isa_io_base = 0xf8000000;
			}
                }
                else
                {
			pci_dram_offset = 0;
			isa_mem_base = 0xf7000000;
			isa_io_base = 0xf8000000;
			set_config_access_method(gg2);
                }
        }
	
	ppc_md.pcibios_fixup = chrp_pcibios_fixup;
}
