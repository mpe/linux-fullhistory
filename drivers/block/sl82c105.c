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
