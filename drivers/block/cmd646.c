/* $Id: cmd646.c,v 1.1 1998/03/15 13:29:10 ecd Exp $
 * cmd646.c: Enable interrupts at initialization time on Ultra/PCI machines
 *
 * Copyright (C) 1998       Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/pci.h>
#include "ide.h"

__initfunc(void ide_init_cmd646 (ide_hwif_t *hwif))
{
#ifdef __sparc_v9__
	struct pci_dev *dev = hwif->pci_dev;
	unsigned char mrdmode;

	(void) pci_read_config_byte(dev, 0x71, &mrdmode);
	mrdmode &= ~(0x30);
	(void) pci_write_config_byte(dev, 0x71, mrdmode);
#endif
}
