/*
 * Linux driver attachment glue for PCI based U320 controllers.
 *
 * Copyright (c) 2000-2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic79xx_osm_pci.c#25 $
 */

#include "aic79xx_osm.h"
#include "aic79xx_inline.h"
#include "aic79xx_pci.h"

static int	ahd_linux_pci_dev_probe(struct pci_dev *pdev,
					const struct pci_device_id *ent);
static int	ahd_linux_pci_reserve_io_regions(struct ahd_softc *ahd,
						 u_long *base, u_long *base2);
static int	ahd_linux_pci_reserve_mem_region(struct ahd_softc *ahd,
						 u_long *bus_addr,
						 uint8_t __iomem **maddr);
static void	ahd_linux_pci_dev_remove(struct pci_dev *pdev);

/* Define the macro locally since it's different for different class of chips.
 */
#define ID(x)            \
	ID2C(x),         \
	ID2C(IDIROC(x))

static struct pci_device_id ahd_linux_pci_id_table[] = {
	/* aic7901 based controllers */
	ID(ID_AHA_29320A),
	ID(ID_AHA_29320ALP),
	/* aic7902 based controllers */
	ID(ID_AHA_29320),
	ID(ID_AHA_29320B),
	ID(ID_AHA_29320LP),
	ID(ID_AHA_39320),
	ID(ID_AHA_39320_B),
	ID(ID_AHA_39320A),
	ID(ID_AHA_39320D),
	ID(ID_AHA_39320D_HP),
	ID(ID_AHA_39320D_B),
	ID(ID_AHA_39320D_B_HP),
	/* Generic chip probes for devices we don't know exactly. */
	ID16(ID_AIC7901 & ID_9005_GENERIC_MASK),
	ID(ID_AIC7901A & ID_DEV_VENDOR_MASK),
	ID16(ID_AIC7902 & ID_9005_GENERIC_MASK),
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, ahd_linux_pci_id_table);

struct pci_driver aic79xx_pci_driver = {
	.name		= "aic79xx",
	.probe		= ahd_linux_pci_dev_probe,
	.remove		= ahd_linux_pci_dev_remove,
	.id_table	= ahd_linux_pci_id_table
};

static void
ahd_linux_pci_dev_remove(struct pci_dev *pdev)
{
	struct ahd_softc *ahd;
	u_long l;

	/*
	 * We should be able to just perform
	 * the free directly, but check our
	 * list for extra sanity.
	 */
	ahd_list_lock(&l);
	ahd = ahd_find_softc((struct ahd_softc *)pci_get_drvdata(pdev));
	if (ahd != NULL) {
		u_long s;

		TAILQ_REMOVE(&ahd_tailq, ahd, links);
		ahd_list_unlock(&l);
		ahd_lock(ahd, &s);
		ahd_intr_enable(ahd, FALSE);
		ahd_unlock(ahd, &s);
		ahd_free(ahd);
	} else
		ahd_list_unlock(&l);
}

static int
ahd_linux_pci_dev_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	char		 buf[80];
	struct		 ahd_softc *ahd;
	ahd_dev_softc_t	 pci;
	struct		 ahd_pci_identity *entry;
	char		*name;
	int		 error;

	/*
	 * Some BIOSen report the same device multiple times.
	 */
	TAILQ_FOREACH(ahd, &ahd_tailq, links) {
		struct pci_dev *probed_pdev;

		probed_pdev = ahd->dev_softc;
		if (probed_pdev->bus->number == pdev->bus->number
		 && probed_pdev->devfn == pdev->devfn)
			break;
	}
	if (ahd != NULL) {
		/* Skip duplicate. */
		return (-ENODEV);
	}

	pci = pdev;
	entry = ahd_find_pci_device(pci);
	if (entry == NULL)
		return (-ENODEV);

	/*
	 * Allocate a softc for this card and
	 * set it up for attachment by our
	 * common detect routine.
	 */
	sprintf(buf, "ahd_pci:%d:%d:%d",
		ahd_get_pci_bus(pci),
		ahd_get_pci_slot(pci),
		ahd_get_pci_function(pci));
	name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		return (-ENOMEM);
	strcpy(name, buf);
	ahd = ahd_alloc(NULL, name);
	if (ahd == NULL)
		return (-ENOMEM);
	if (pci_enable_device(pdev)) {
		ahd_free(ahd);
		return (-ENODEV);
	}
	pci_set_master(pdev);

	if (sizeof(dma_addr_t) > 4) {
		uint64_t   memsize;
		const uint64_t mask_39bit = 0x7FFFFFFFFFULL;

		memsize = ahd_linux_get_memsize();

		if (memsize >= 0x8000000000ULL
	 	 && pci_set_dma_mask(pdev, DMA_64BIT_MASK) == 0) {
			ahd->flags |= AHD_64BIT_ADDRESSING;
			ahd->platform_data->hw_dma_mask = DMA_64BIT_MASK;
		} else if (memsize > 0x80000000
			&& pci_set_dma_mask(pdev, mask_39bit) == 0) {
			ahd->flags |= AHD_39BIT_ADDRESSING;
			ahd->platform_data->hw_dma_mask = mask_39bit;
		}
	} else {
		pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		ahd->platform_data->hw_dma_mask = DMA_32BIT_MASK;
	}
	ahd->dev_softc = pci;
	error = ahd_pci_config(ahd, entry);
	if (error != 0) {
		ahd_free(ahd);
		return (-error);
	}
	pci_set_drvdata(pdev, ahd);
	if (aic79xx_detect_complete) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
		ahd_linux_register_host(ahd, &aic79xx_driver_template);
#else
		printf("aic79xx: ignoring PCI device found after "
		       "initialization\n");
		return (-ENODEV);
#endif
	}
	return (0);
}

int
ahd_linux_pci_init(void)
{
	return (pci_module_init(&aic79xx_pci_driver));
}

void
ahd_linux_pci_exit(void)
{
	pci_unregister_driver(&aic79xx_pci_driver);
}

static int
ahd_linux_pci_reserve_io_regions(struct ahd_softc *ahd, u_long *base,
				 u_long *base2)
{
	*base = pci_resource_start(ahd->dev_softc, 0);
	/*
	 * This is really the 3rd bar and should be at index 2,
	 * but the Linux PCI code doesn't know how to "count" 64bit
	 * bars.
	 */
	*base2 = pci_resource_start(ahd->dev_softc, 3);
	if (*base == 0 || *base2 == 0)
		return (ENOMEM);
	if (request_region(*base, 256, "aic79xx") == 0)
		return (ENOMEM);
	if (request_region(*base2, 256, "aic79xx") == 0) {
		release_region(*base2, 256);
		return (ENOMEM);
	}
	return (0);
}

static int
ahd_linux_pci_reserve_mem_region(struct ahd_softc *ahd,
				 u_long *bus_addr,
				 uint8_t __iomem **maddr)
{
	u_long	start;
	u_long	base_page;
	u_long	base_offset;
	int	error;

	if (aic79xx_allow_memio == 0)
		return (ENOMEM);

	if ((ahd->bugs & AHD_PCIX_MMAPIO_BUG) != 0)
		return (ENOMEM);

	error = 0;
	start = pci_resource_start(ahd->dev_softc, 1);
	base_page = start & PAGE_MASK;
	base_offset = start - base_page;
	if (start != 0) {
		*bus_addr = start;
		if (request_mem_region(start, 0x1000, "aic79xx") == 0)
			error = ENOMEM;
		if (error == 0) {
			*maddr = ioremap_nocache(base_page, base_offset + 256);
			if (*maddr == NULL) {
				error = ENOMEM;
				release_mem_region(start, 0x1000);
			} else
				*maddr += base_offset;
		}
	} else
		error = ENOMEM;
	return (error);
}

int
ahd_pci_map_registers(struct ahd_softc *ahd)
{
	uint32_t command;
	u_long	 base;
	uint8_t	__iomem *maddr;
	int	 error;

	/*
	 * If its allowed, we prefer memory mapped access.
	 */
	command = ahd_pci_read_config(ahd->dev_softc, PCIR_COMMAND, 4);
	command &= ~(PCIM_CMD_PORTEN|PCIM_CMD_MEMEN);
	base = 0;
	maddr = NULL;
	error = ahd_linux_pci_reserve_mem_region(ahd, &base, &maddr);
	if (error == 0) {
		ahd->platform_data->mem_busaddr = base;
		ahd->tags[0] = BUS_SPACE_MEMIO;
		ahd->bshs[0].maddr = maddr;
		ahd->tags[1] = BUS_SPACE_MEMIO;
		ahd->bshs[1].maddr = maddr + 0x100;
		ahd_pci_write_config(ahd->dev_softc, PCIR_COMMAND,
				     command | PCIM_CMD_MEMEN, 4);

		if (ahd_pci_test_register_access(ahd) != 0) {

			printf("aic79xx: PCI Device %d:%d:%d "
			       "failed memory mapped test.  Using PIO.\n",
			       ahd_get_pci_bus(ahd->dev_softc),
			       ahd_get_pci_slot(ahd->dev_softc),
			       ahd_get_pci_function(ahd->dev_softc));
			iounmap(maddr);
			release_mem_region(ahd->platform_data->mem_busaddr,
					   0x1000);
			ahd->bshs[0].maddr = NULL;
			maddr = NULL;
		} else
			command |= PCIM_CMD_MEMEN;
	} else if (bootverbose) {
		printf("aic79xx: PCI%d:%d:%d MEM region 0x%lx "
		       "unavailable. Cannot memory map device.\n",
		       ahd_get_pci_bus(ahd->dev_softc),
		       ahd_get_pci_slot(ahd->dev_softc),
		       ahd_get_pci_function(ahd->dev_softc),
		       base);
	}

	if (maddr == NULL) {
		u_long	 base2;

		error = ahd_linux_pci_reserve_io_regions(ahd, &base, &base2);
		if (error == 0) {
			ahd->tags[0] = BUS_SPACE_PIO;
			ahd->tags[1] = BUS_SPACE_PIO;
			ahd->bshs[0].ioport = base;
			ahd->bshs[1].ioport = base2;
			command |= PCIM_CMD_PORTEN;
		} else {
			printf("aic79xx: PCI%d:%d:%d IO regions 0x%lx and 0x%lx"
			       "unavailable. Cannot map device.\n",
			       ahd_get_pci_bus(ahd->dev_softc),
			       ahd_get_pci_slot(ahd->dev_softc),
			       ahd_get_pci_function(ahd->dev_softc),
			       base, base2);
		}
	}
	ahd_pci_write_config(ahd->dev_softc, PCIR_COMMAND, command, 4);
	return (error);
}

int
ahd_pci_map_int(struct ahd_softc *ahd)
{
	int error;

	error = request_irq(ahd->dev_softc->irq, ahd_linux_isr,
			    SA_SHIRQ, "aic79xx", ahd);
	if (error == 0)
		ahd->platform_data->irq = ahd->dev_softc->irq;
	
	return (-error);
}

void
ahd_power_state_change(struct ahd_softc *ahd, ahd_power_state new_state)
{
	pci_set_power_state(ahd->dev_softc, new_state);
}
