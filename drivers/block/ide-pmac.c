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
#include <linux/delay.h>
#include <linux/ide.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/dbdma.h>
#include <asm/ide.h>
#include <asm/mediabay.h>
#include <asm/feature.h>
#ifdef CONFIG_PMAC_PBOOK
#include <linux/adb.h>
#include <linux/pmu.h>
#include <asm/irq.h>
#endif
#include "ide_modes.h"

int pmac_ide_ports_known;
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

#ifdef CONFIG_PMAC_PBOOK
static int idepmac_notify(struct pmu_sleep_notifier *self, int when);
struct pmu_sleep_notifier idepmac_sleep_notifier = {
	idepmac_notify, SLEEP_LEVEL_BLOCK,
};
#endif /* CONFIG_PMAC_PBOOK */

/*
 * N.B. this can't be an __init, because the media-bay task can
 * call ide_[un]register at any time.
 */
void pmac_ide_init_hwif_ports(hw_regs_t *hw,
			      ide_ioreg_t data_port, ide_ioreg_t ctrl_port,
			      int *irq)
{
	int i, ix;

	if (data_port == 0)
		return;

	for (ix = 0; ix < MAX_HWIFS; ++ix)
		if (data_port == pmac_ide_regbase[ix])
			break;

	if (ix >= MAX_HWIFS) {
		/* Probably a PCI interface... */
		for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; ++i)
			hw->io_ports[i] = data_port + i - IDE_DATA_OFFSET;
		/* XXX is this right? */
		hw->io_ports[IDE_CONTROL_OFFSET] = 0;
		if (irq != 0)
			*irq = 0;
		return;
	}

	/* we check only for -EINVAL meaning that we have found a matching
	   bay but with the wrong device type */ 
	i = check_media_bay_by_base(data_port, MB_CD);
	if (i == -EINVAL) {
		hw->io_ports[IDE_DATA_OFFSET] = 0;
		return;
	}

	for (i = 0; i < 8; ++i)
		hw->io_ports[i] = data_port + i * 0x10;
	hw->io_ports[8] = data_port + 0x160;

	if (irq != NULL)
		*irq = pmac_ide_irq[ix];
}

void pmac_ide_tuneproc(ide_drive_t *drive, byte pio)
{
	ide_pio_data_t d;

	if (_machine != _MACH_Pmac)
		return;
	pio = ide_get_best_pio_mode(drive, pio, 4, &d);
	switch (pio) {
	case 4:
		out_le32((unsigned *)(IDE_DATA_REG + 0x200 + _IO_BASE), 0x211025);
		break;
	default:
		out_le32((unsigned *)(IDE_DATA_REG + 0x200 + _IO_BASE), 0x2f8526);
		break;
	}
}

void __init pmac_ide_probe(void)
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
		struct device_node *tp;

		/*
		 * If this node is not under a mac-io or dbdma node,
		 * leave it to the generic PCI driver.
		 */
		for (tp = np->parent; tp != 0; tp = tp->parent)
			if (tp->type && (strcmp(tp->type, "mac-io") == 0
					 || strcmp(tp->type, "dbdma") == 0))
				break;
		if (tp == 0)
			continue;

		if (np->n_addrs == 0) {
			printk(KERN_WARNING "ide: no address for device %s\n",
			       np->full_name);
			continue;
		}

		/*
		 * If this slot is taken (e.g. by ide-pci.c) try the next one.
		 */
		while (i < MAX_HWIFS
		       && ide_hwifs[i].io_ports[IDE_DATA_OFFSET] != 0)
			++i;
		if (i >= MAX_HWIFS)
			break;

		base = (unsigned long) ioremap(np->addrs[0].address, 0x200) - _IO_BASE;

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
		pmac_ide_init_hwif_ports(&hwif->hw, base, 0, &hwif->irq);
		memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
		hwif->chipset = ide_generic;
		hwif->noprobe = !hwif->io_ports[IDE_DATA_OFFSET];
		hwif->tuneproc = pmac_ide_tuneproc;

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
		if (np->n_addrs >= 2) {
			/* has a DBDMA controller channel */
			pmac_ide_setup_dma(np, hwif);
		}
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

		++i;
	}
	pmac_ide_count = i;

#ifdef CONFIG_PMAC_PBOOK
	pmu_register_sleep_notifier(&idepmac_sleep_notifier);
#endif /* CONFIG_PMAC_PBOOK */
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC

static void __init 
pmac_ide_setup_dma(struct device_node *np, ide_hwif_t *hwif)
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
#ifdef CONFIG_IDEDMA_PMAC_AUTO
	hwif->autodma = 1;
#endif /* CONFIG_IDEDMA_PMAC_AUTO */
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
		drive->timeout = WAIT_CMD;
		ide_set_handler(drive, &ide_dma_intr);
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

#ifdef CONFIG_PMAC_PBOOK
static void idepmac_sleep_disk(int i, unsigned long base)
{
	int j;

	/* Reset to PIO 0 */
	out_le32((unsigned *)(base + 0x200 + _IO_BASE), 0x2f8526);

	/* FIXME: We only handle the master IDE */
	if (ide_hwifs[i].drives[0].media == ide_disk) {
		/* Spin down the drive */
		outb(0xa0, base+0x60);
		outb(0x0, base+0x30);
		outb(0x0, base+0x20);
		outb(0x0, base+0x40);
		outb(0x0, base+0x50);
		outb(0xe0, base+0x70);
		outb(0x2, base+0x160);   
		for (j = 0; j < 10; j++) {
			int status;
			mdelay(100);
			status = inb(base+0x70);
			if (!(status & BUSY_STAT) && (status & DRQ_STAT))
				break;
		}
	}
}

static void idepmac_wake_disk(int i, unsigned long base)
{
	int j;

	/* Revive IDE disk and controller */
	feature_set(pmac_ide_node[i], FEATURE_IDE_enable);
	mdelay(1);
	feature_set(pmac_ide_node[i], FEATURE_IDE_DiskPower);
	mdelay(100);
	feature_set(pmac_ide_node[i], FEATURE_IDE_Reset);
	mdelay(1);
	/* Make sure we are still PIO0 */
	out_le32((unsigned *)(base + 0x200 + _IO_BASE), 0x2f8526);
	mdelay(100);

	/* Wait up to 10 seconds (enough for recent drives) */
	for (j = 0; j < 100; j++) {
		int status;
		mdelay(100);
		status = inb(base + 0x70);
		if (!(status & BUSY_STAT))
			break;
	}
}

/* Here we handle media bay devices */
static void
idepmac_wake_bay(int i, unsigned long base)
{
	int timeout;

	timeout = 5000;
	while ((inb(base + 0x70) & BUSY_STAT) && timeout) {
		mdelay(1);
		--timeout;
	}
}

static int idepmac_notify(struct pmu_sleep_notifier *self, int when)
{
	int i, ret;
	unsigned long base;

	switch (when) {
	case PBOOK_SLEEP_REQUEST:
		break;
	case PBOOK_SLEEP_REJECT:
		break;
	case PBOOK_SLEEP_NOW:
		for (i = 0; i < pmac_ide_count; ++i) {
			if ((base = pmac_ide_regbase[i]) == 0)
				continue;
			/* Disable irq during sleep */
			disable_irq(pmac_ide_irq[i]);
			ret = check_media_bay_by_base(base, MB_CD);
			if (ret == -ENODEV)
				/* not media bay - put the disk to sleep */
				idepmac_sleep_disk(i, base);
		}
		break;
	case PBOOK_WAKE:
		for (i = 0; i < pmac_ide_count; ++i) {
			if ((base = pmac_ide_regbase[i]) == 0)
				continue;
		        /* We don't handle media bay devices this way */
			ret = check_media_bay_by_base(base, MB_CD);
			if (ret == -ENODEV)
				idepmac_wake_disk(i, base);
			else if (ret == 0)
				idepmac_wake_bay(i, base);
			enable_irq(pmac_ide_irq[i]);
		}
		break;
	}
	return PBOOK_SLEEP_OK;
}
#endif /* CONFIG_PMAC_PBOOK */
