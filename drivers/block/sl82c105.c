#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/dma.h>

#include "ide.h"
#include "ide_modes.h"

unsigned int chrp_ide_irq = 0;
int chrp_ide_ports_known = 0;
ide_ioreg_t chrp_ide_regbase[MAX_HWIFS];
ide_ioreg_t chrp_idedma_regbase;

void ide_init_sl82c105(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	unsigned short t16;
	unsigned int t32;

	pci_read_config_word(dev, PCI_COMMAND, &t16);
	printk("SL82C105 command word: %x\n",t16);
        t16 |= PCI_COMMAND_IO;
        pci_write_config_word(dev, PCI_COMMAND, t16);
	/* IDE timing */
	pci_read_config_dword(dev, 0x44, &t32);
	printk("IDE timing: %08x, resetting to PIO0 timing\n",t32);
	pci_write_config_dword(dev, 0x44, 0x03e4);
	pci_read_config_dword(dev, 0x40, &t32);
	printk("IDE control/status register: %08x\n",t32);
	pci_write_config_dword(dev, 0x40, 0x10ff08a1);
}

#if 0	/* nobody ever calls these.. ?? */
void chrp_ide_probe(void) {

	struct pci_dev *pdev = pci_find_device(PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_82C105, NULL);

	chrp_ide_ports_known = 1;

        if(pdev) {
		chrp_ide_regbase[0]=pdev->base_address[0] &
			PCI_BASE_ADDRESS_IO_MASK;
		chrp_ide_regbase[1]=pdev->base_address[2] &
			PCI_BASE_ADDRESS_IO_MASK;
		chrp_idedma_regbase=pdev->base_address[4] &
			PCI_BASE_ADDRESS_IO_MASK;
		chrp_ide_irq=pdev->irq;
        }
}


void chrp_ide_init_hwif_ports (ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
        ide_ioreg_t port = base;
        int i = 8;

        while (i--)
                *p++ = port++;
        *p++ = port;
        if (irq != NULL)
                *irq = chrp_ide_irq;
}
#endif
