/*
 * FILE NAME: ppc405_pci.c
 *
 * BRIEF MODULE DESCRIPTION: 
 * Based on arch/ppc/kernel/indirect.c, Copyright (C) 1998 Gabriel Paubert.
 *
 * Author: MontaVista Software, Inc.  <source@mvista.com>
 *         Frank Rowand <frank_rowand@mvista.com>
 *         Debbie Chu   <debbie_chu@mvista.com>
 *
 * Copyright 2000 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/pci.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/machdep.h>
#include <linux/init.h>
#include <asm/ibm4xx.h>
#include <asm/pci-bridge.h>
#include <platforms/ibm_ocp.h>

#ifdef  CONFIG_DEBUG_BRINGUP
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

extern void bios_fixup(struct pci_controller *, void *);
extern int ppc405_map_irq(struct pci_dev *dev, unsigned char idsel,
			  unsigned char pin);
extern struct pcil0_regs *PCIL_ADDR[];

void
ppc405_pcibios_fixup_resources(struct pci_dev *dev)
{
	int i;
	unsigned long max_host_addr;
	unsigned long min_host_addr;
	struct resource *res;

	/*
	 * openbios puts some graphics cards in the same range as the host
	 * controller uses to map to SDRAM.  Fix it.
	 */

	min_host_addr = 0;
	max_host_addr = PPC405_PCI_MEM_BASE - 1;

	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		res = dev->resource + i;
		if (!res->start)
			continue;
		if ((res->flags & IORESOURCE_MEM) &&
		    (((res->start >= min_host_addr)
		      && (res->start <= max_host_addr))
		     || ((res->end >= min_host_addr)
			 && (res->end <= max_host_addr))
		     || ((res->start < min_host_addr)
			 && (res->end > max_host_addr))
		    )
		    ) {

			DBG(KERN_ERR "PCI: 0x%lx <= resource[%d] <= 0x%lx"
			    ", bus 0x%x dev 0x%2.2x.%1.1x,\n"
			    KERN_ERR "  %s\n"
			    KERN_ERR "  fixup will be attempted later\n",
			    min_host_addr, i, max_host_addr,
			    dev->bus->number, PCI_SLOT(dev->devfn),
			    PCI_FUNC(dev->devfn), dev->name);

			/* force pcibios_assign_resources() to assign a new address */
			res->end -= res->start;
			res->start = 0;
		}
	}
}

static int
ppc4xx_exclude_device(unsigned char bus, unsigned char devfn)
{
	/* We prevent us from seeing ourselves to avoid having
	 * the kernel try to remap our BAR #1 and fuck up bus
	 * master from external PCI devices
	 */
	return (bus == 0 && devfn == 0);
}

void
ppc4xx_find_bridges(void)
{
	struct pci_controller *hose_a;
	struct pcil0_regs *pcip;
	unsigned int tmp_addr;
	unsigned int tmp_size;
	unsigned int reg_index;
	unsigned int new_pmm_max;
	unsigned int new_pmm_min;

	isa_io_base = 0;
	isa_mem_base = 0;
	pci_dram_offset = 0;
	
	/* Check if running in slave mode */
	if ((mfdcr(DCRN_CHPSR) & PSR_PCI_ARBIT_EN) == 0) {
		printk("Running as PCI slave, kernel PCI disabled !\n");
		return;
	}
	/* Setup PCI32 hose */
	hose_a = pcibios_alloc_controller();
	if (!hose_a)
		return;
	setup_indirect_pci(hose_a, PPC405_PCI_CONFIG_ADDR,
			   PPC405_PCI_CONFIG_DATA);
	pcip = ioremap((unsigned long) PCIL_ADDR[0], PAGE_SIZE);
	if (pcip != NULL) {

#if defined(CONFIG_BIOS_FIXUP)
		bios_fixup(hose_a, pcip);
#endif
		new_pmm_min = 0xffffffff;
		for (reg_index = 0; reg_index < 3; reg_index++) {
			tmp_size = in_le32((void *) &(pcip->pmm[reg_index].ma));	// *_PMM0MA
			if (tmp_size & 0x1) {
				tmp_addr = in_le32((void *) &(pcip->pmm[reg_index].pcila));	// *_PMM0PCILA
				if (tmp_addr < PPC405_PCI_PHY_MEM_BASE) {
					printk(KERN_DEBUG
					       "Disabling mapping to PCI mem addr 0x%8.8x\n",
					       tmp_addr);
					out_le32((void *) &(pcip->pmm[reg_index].ma), tmp_size & ~1);	// *_PMMOMA
				} else {
					tmp_addr = in_le32((void *) &(pcip->pmm[reg_index].la));	// *_PMMOLA
					if (tmp_addr < new_pmm_min)
						new_pmm_min = tmp_addr;
					tmp_addr =
					    tmp_addr + (0xffffffff -
							(tmp_size &
							 0xffffc000));
					if (tmp_addr > PPC405_PCI_UPPER_MEM) {
						new_pmm_max = tmp_addr;	// PPC405_PCI_UPPER_MEM 
					} else {
						new_pmm_max =
						    PPC405_PCI_UPPER_MEM;
					}
				}
			}

		}		// for

		iounmap(pcip);
	}
	hose_a->first_busno      = 0;
	hose_a->last_busno       = 0xff;
	hose_a->pci_mem_offset   = 0;

	/* Setup bridge memory/IO ranges & resources
	 * TODO: Handle firmwares setting up a legacy ISA mem base
	 */
	hose_a->io_space.start		= PPC405_PCI_LOWER_IO;
	hose_a->io_space.end		= PPC405_PCI_UPPER_IO;
	hose_a->mem_space.start		= new_pmm_min;
	hose_a->mem_space.end		= new_pmm_max;
	hose_a->io_base_phys		= PPC405_PCI_PHY_IO_BASE;
	hose_a->io_base_virt		= ioremap(hose_a->io_base_phys, 0x10000);
	hose_a->io_resource.start	= 0;
	hose_a->io_resource.end		= PPC405_PCI_UPPER_IO-PPC405_PCI_LOWER_IO;
	hose_a->io_resource.flags	= IORESOURCE_IO;
	hose_a->io_resource.name	= "PCI I/O";
	hose_a->mem_resources[0].start	= new_pmm_min;
	hose_a->mem_resources[0].end	= new_pmm_max;
	hose_a->mem_resources[0].flags	= IORESOURCE_MEM;
	hose_a->mem_resources[0].name	= "PCI Memory";
	isa_io_base			= (int)hose_a->io_base_virt;
	isa_mem_base			= 0;	      /*     ISA not implemented */
	ISA_DMA_THRESHOLD		= 0x00ffffff; /* ??? ISA not implemented */

	/* Scan busses & initial setup by pci_auto */
	hose_a->last_busno = pciauto_bus_scan(hose_a, hose_a->first_busno);
	hose_a->last_busno = 0;

	/* Setup ppc_md */
	ppc_md.pcibios_fixup     	= NULL;
	ppc_md.pci_exclude_device	= ppc4xx_exclude_device;
	ppc_md.pcibios_fixup_resources	= ppc405_pcibios_fixup_resources;
	ppc_md.pci_swizzle		= common_swizzle;
	ppc_md.pci_map_irq		= ppc405_map_irq;
}
