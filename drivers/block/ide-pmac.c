/*
 * Support for IDE interfaces on PowerMacs.
 * These IDE interfaces are memory-mapped and have a DBDMA channel
 * for doing DMA.
 *
 *  Copyright (C) 1998 Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * Some code taken from drivers/block/ide-dma.c:
 *
 *  Copyright (c) 1995-1998  Mark Lord
 *
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/dbdma.h>
#include <asm/ide.h>
#include <asm/mediabay.h>
#include <asm/feature.h>
#include "ide.h"

ide_ioreg_t pmac_ide_regbase[MAX_HWIFS];
int pmac_ide_irq[MAX_HWIFS];
int pmac_ide_count;
struct device_node *pmac_ide_node[MAX_HWIFS];

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
#define MAX_DCMDS	256	/* allow up to 256 DBDMA commands per xfer */

static void pmac_ide_setup_dma(struct device_node *np, ide_hwif_t *hwif);
static int pmac_ide_dmaproc(ide_dma_action_t func, ide_drive_t *drive);
static int pmac_ide_build_dmatable(ide_drive_t *drive, int wr);
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

/*
 * N.B. this can't be an initfunc, because the media-bay task can
 * call ide_[un]register at any time.
 */
void
pmac_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
	int i, r;

	*p = 0;
	if (base == 0)
		return;
	/* we check only for -EINVAL meaning that we have found a matching
	   bay but with the wrong device type */ 

	r = check_media_bay_by_base(base, MB_CD);
	if (r == -EINVAL)
		return;
		
	for (i = 0; i < 8; ++i)
		*p++ = base + i * 0x10;
	*p = base + 0x160;
	if (irq != NULL) {
		*irq = 0;
		for (i = 0; i < MAX_HWIFS; ++i) {
			if (base == pmac_ide_regbase[i]) {
				*irq = pmac_ide_irq[i];
				break;
			}
		}
	}
}

__initfunc(void
pmac_ide_probe(void))
{
	struct device_node *np;
	int i;
	struct device_node *atas;
	struct device_node *p, **pp, *removables, **rp;
	unsigned long base;
	int irq;
	ide_hwif_t *hwif;

	if (_machine != _MACH_Pmac)
		return;
	pp = &atas;
	rp = &removables;
	p = find_devices("ATA");
	if (p == NULL)
		p = find_devices("IDE");
	if (p == NULL)
		p = find_type_devices("ide");
	if (p == NULL)
		p = find_type_devices("ata");
	/* Move removable devices such as the media-bay CDROM
	   on the PB3400 to the end of the list. */
	for (; p != NULL; p = p->next) {
		if (p->parent && p->parent->type
		    && strcasecmp(p->parent->type, "media-bay") == 0) {
			*rp = p;
			rp = &p->next;
		} else {
			*pp = p;
			pp = &p->next;
		}
	}
	*rp = NULL;
	*pp = removables;

	for (i = 0, np = atas; i < MAX_HWIFS && np != NULL; np = np->next) {
		if (np->n_addrs == 0) {
			printk(KERN_WARNING "ide: no address for device %s\n",
			       np->full_name);
			continue;
		}
		
		base = (unsigned long) ioremap(np->addrs[0].address, 0x200);
		
		/* XXX This is bogus. Should be fixed in the registry by checking
		   the kind of host interrupt controller, a bit like gatwick
		   fixes in irq.c
		 */
		if (np->n_intrs == 0) {
			printk("ide: no intrs for device %s, using 13\n",
			       np->full_name);
			irq = 13;
		} else {
			irq = np->intrs[0].line;
		}
		pmac_ide_regbase[i] = base;
		pmac_ide_irq[i] = irq;
		pmac_ide_node[i] = np;

		if (np->parent && np->parent->name
		    && strcasecmp(np->parent->name, "media-bay") == 0) {
			media_bay_set_ide_infos(np->parent,base,irq,i);
		} else
			feature_set(np, FEATURE_IDE_enable);

		hwif = &ide_hwifs[i];
		pmac_ide_init_hwif_ports(hwif->io_ports, base, &hwif->irq);
		hwif->chipset = ide_generic;
		hwif->noprobe = !hwif->io_ports[IDE_DATA_OFFSET];

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
		if (np->n_addrs >= 2 && np->n_intrs >= 2) {
			/* has a DBDMA controller channel */
			pmac_ide_setup_dma(np, hwif);
		}
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

		++i;
	}
	pmac_ide_count = i;
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC

__initfunc(static void
pmac_ide_setup_dma(struct device_node *np, ide_hwif_t *hwif))
{
	hwif->dma_base = (unsigned long) ioremap(np->addrs[1].address, 0x200);

	/*
	 * Allocate space for the DBDMA commands.
	 * The +2 is +1 for the stop command and +1 to allow for
	 * aligning the start address to a multiple of 16 bytes.
	 */
	hwif->dmatable = (unsigned long *)
	       kmalloc((MAX_DCMDS + 2) * sizeof(struct dbdma_cmd), GFP_KERNEL);
	if (hwif->dmatable == 0) {
		printk(KERN_ERR "%s: unable to allocate DMA command list\n",
		       hwif->name);
		return;
	}

	hwif->dmaproc = &pmac_ide_dmaproc;
#ifdef CONFIG_PMAC_IDEDMA_AUTO
	hwif->autodma = 1;
#endif
}

/*
 * pmac_ide_build_dmatable builds the DBDMA command list
 * for a transfer and sets the DBDMA channel to point to it.
 */
static int
pmac_ide_build_dmatable(ide_drive_t *drive, int wr)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct dbdma_cmd *table, *tstart;
	int count = 0;
	struct request *rq = HWGROUP(drive)->rq;
	struct buffer_head *bh = rq->bh;
	unsigned int size, addr;
	volatile struct dbdma_regs *dma
		= (volatile struct dbdma_regs *) hwif->dma_base;

	table = tstart = (struct dbdma_cmd *) DBDMA_ALIGN(hwif->dmatable);
	out_le32(&dma->control, (RUN|PAUSE|FLUSH|WAKE|DEAD) << 16);

	do {
		/*
		 * Determine addr and size of next buffer area.  We assume that
		 * individual virtual buffers are always composed linearly in
		 * physical memory.  For example, we assume that any 8kB buffer
		 * is always composed of two adjacent physical 4kB pages rather
		 * than two possibly non-adjacent physical 4kB pages.
		 */
		if (bh == NULL) {  /* paging requests have (rq->bh == NULL) */
			addr = virt_to_bus(rq->buffer);
			size = rq->nr_sectors << 9;
		} else {
			/* group sequential buffers into one large buffer */
			addr = virt_to_bus(bh->b_data);
			size = bh->b_size;
			while ((bh = bh->b_reqnext) != NULL) {
				if ((addr + size) != virt_to_bus(bh->b_data))
					break;
				size += bh->b_size;
			}
		}

		/*
		 * Fill in the next DBDMA command block.
		 * Note that one DBDMA command can transfer
		 * at most 65535 bytes.
		 */
		while (size) {
			unsigned int tc = (size < 0xfe00)? size: 0xfe00;

			if (++count >= MAX_DCMDS) {
				printk("%s: DMA table too small\n",
				       drive->name);
				return 0; /* revert to PIO for this request */
			}
			st_le16(&table->command, wr? OUTPUT_MORE: INPUT_MORE);
			st_le16(&table->req_count, tc);
			st_le32(&table->phy_addr, addr);
			table->cmd_dep = 0;
			table->xfer_status = 0;
			table->res_count = 0;
			addr += tc;
			size -= tc;
			++table;
		}
	} while (bh != NULL);

	/* convert the last command to an input/output last command */
	if (count)
		st_le16(&table[-1].command, wr? OUTPUT_LAST: INPUT_LAST);
	else
		printk(KERN_DEBUG "%s: empty DMA table?\n", drive->name);

	/* add the stop command to the end of the list */
	memset(table, 0, sizeof(struct dbdma_cmd));
	out_le16(&table->command, DBDMA_STOP);

	out_le32(&dma->cmdptr, virt_to_bus(tstart));
	return 1;
}

int pmac_ide_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	volatile struct dbdma_regs *dma
		= (volatile struct dbdma_regs *) hwif->dma_base;
	int dstat;

	switch (func) {
	case ide_dma_on:
		/* ide-floppy DMA doesn't work yet... */
		drive->using_dma = drive->media != ide_floppy;
		break;
	case ide_dma_off:
		printk(KERN_INFO "%s: DMA disabled\n", drive->name);
	case ide_dma_off_quietly:
		drive->using_dma = 0;
		break;
	case ide_dma_check:
		/* ide-floppy DMA doesn't work yet... */
		drive->using_dma = hwif->autodma && drive->media != ide_floppy;
		break;
	case ide_dma_read:
	case ide_dma_write:
		if (!pmac_ide_build_dmatable(drive, func==ide_dma_write))
			return 1;
		drive->waiting_for_dma = 1;
		if (drive->media != ide_disk)
			return 0;
		ide_set_handler(drive, &ide_dma_intr, WAIT_CMD);
		OUT_BYTE(func==ide_dma_write? WIN_WRITEDMA: WIN_READDMA,
			 IDE_COMMAND_REG);
	case ide_dma_begin:
		out_le32(&dma->control, (RUN << 16) | RUN);
		break;
	case ide_dma_end:
		drive->waiting_for_dma = 0;
		dstat = in_le32(&dma->status);
		out_le32(&dma->control, ((RUN|WAKE|DEAD) << 16));
		/* verify good dma status */
		return (dstat & (RUN|DEAD|ACTIVE)) != RUN;
	case ide_dma_test_irq:
		return (in_le32(&dma->status) & (RUN|ACTIVE)) == RUN;
	default:
		printk(KERN_ERR "pmac_ide_dmaproc: bad func %d\n", func);
	}
	return 0;
}

#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */
