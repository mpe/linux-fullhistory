#ifndef CS5530_H
#define CS5530_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define DISPLAY_CS5530_TIMINGS

static unsigned int init_chipset_cs5530(struct pci_dev *, const char *);
static void init_hwif_cs5530(ide_hwif_t *);

static ide_pci_device_t cs5530_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "CS5530",
		.init_chipset	= init_chipset_cs5530,
		.init_hwif	= init_hwif_cs5530,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= ON_BOARD,
		.flags		= IDEPCI_FLAG_FORCE_MASTER,
	}
};

#endif /* CS5530_H */
