/*
 * linux/drivers/block/ide-features.c
 *
 *  Copyright (C) 1999  Linus Torvalds & authors (see below)
 *  
 *  Andre Hedrick <andre@suse.com>
 *
 *  Extracts if ide.c to address the evolving transfer rate code for
 *  the SETFEATURES_XFER callouts.  Below are original authors of some or
 *  various parts of any given function below.
 *
 *  Mark Lord     <mlord@pobox.com>
 *  Gadi Oxman    <gadio@netvision.net.il>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/malloc.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

/*
 *
 */
char *ide_xfer_verbose (byte xfer_rate)
{
	switch(xfer_rate) {
		case XFER_UDMA_4:	return("UDMA 4");
		case XFER_UDMA_3:	return("UDMA 3");
		case XFER_UDMA_2:	return("UDMA 2");
		case XFER_UDMA_1:	return("UDMA 1");
		case XFER_UDMA_0:	return("UDMA 0");
		case XFER_MW_DMA_2:	return("MW DMA 2");
		case XFER_MW_DMA_1:	return("MW DMA 1");
		case XFER_MW_DMA_0:	return("MW DMA 0");
		case XFER_SW_DMA_2:	return("SW DMA 2");
		case XFER_SW_DMA_1:	return("SW DMA 1");
		case XFER_SW_DMA_0:	return("SW DMA 0");
		case XFER_PIO_4:	return("PIO 4");
		case XFER_PIO_3:	return("PIO 3");
		case XFER_PIO_2:	return("PIO 2");
		case XFER_PIO_1:	return("PIO 1");
		case XFER_PIO_0:	return("PIO 0");
		case XFER_PIO_SLOW:	return("PIO SLOW");
		default:		return("XFER ERROR");
	}
}

/*
 *
 */
char *ide_media_verbose (ide_drive_t *drive)
{
	switch (drive->media) {
		case ide_disk:		return("disk  ");
		case ide_cdrom:		return("cdrom ");
		case ide_tape:		return("tape  ");
		case ide_floppy:	return("floppy");
		default:		return("??????");
	}
}

int ide_driveid_update (ide_drive_t *drive)
{
	/*
	 * Re-read drive->id for possible DMA mode
	 * change (copied from ide-probe.c)
	 */
	struct hd_driveid *id;
	unsigned long timeout, irqs, flags;

	probe_irq_off(probe_irq_on());
	irqs = probe_irq_on();
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl,IDE_CONTROL_REG);
	ide_delay_50ms();
	OUT_BYTE(WIN_IDENTIFY, IDE_COMMAND_REG);
	timeout = jiffies + WAIT_WORSTCASE;
	do {
		if (0 < (signed long)(jiffies - timeout)) {
			if (irqs)
				(void) probe_irq_off(irqs);
			return 0;	/* drive timed-out */
		}
		ide_delay_50ms();	/* give drive a breather */
	} while (IN_BYTE(IDE_ALTSTATUS_REG) & BUSY_STAT);
	ide_delay_50ms();	/* wait for IRQ and DRQ_STAT */
	if (!OK_STAT(GET_STAT(),DRQ_STAT,BAD_R_STAT))
		return 0;
	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only; some systems need this */
	id = kmalloc(SECTOR_WORDS*4, GFP_ATOMIC);
	ide_input_data(drive, id, SECTOR_WORDS);
	(void) GET_STAT();	/* clear drive IRQ */
	ide__sti();		/* local CPU only */
	__restore_flags(flags);	/* local CPU only */
	ide_fix_driveid(id);
	if (id && id->cyls) {
		drive->id->dma_ultra = id->dma_ultra;
		drive->id->dma_mword = id->dma_mword;
		drive->id->dma_1word = id->dma_1word;
		/* anything more ? */
#ifdef DEBUG
		printk("%s: dma_ultra=%04X, dma_mword=%04X, dma_1word=%04X\n",
			drive->name, id->dma_ultra, id->dma_mword, id->dma_1word);
#endif
		kfree(id);
	}
	return 1;
}

/*
 * Similar to ide_wait_stat(), except it never calls ide_error internally.
 * This is a kludge to handle the new ide_config_drive_speed() function,
 * and should not otherwise be used anywhere.  Eventually, the tuneproc's
 * should be updated to return ide_startstop_t, in which case we can get
 * rid of this abomination again.  :)   -ml
 */
int ide_wait_noerr (ide_drive_t *drive, byte good, byte bad, unsigned long timeout)
{
	byte stat;
	int i;
	unsigned long flags;

	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */
	if ((stat = GET_STAT()) & BUSY_STAT) {
		__save_flags(flags);	/* local CPU only */
		ide__sti();	/* local CPU only */
		timeout += jiffies;
		while ((stat = GET_STAT()) & BUSY_STAT) {
			if (0 < (signed long)(jiffies - timeout)) {
				__restore_flags(flags);	/* local CPU only */
				(void)ide_dump_status(drive, "ide_wait_noerr", stat);
				return 1;
			}
		}
		__restore_flags(flags); /* local CPU only */
	}
	/*
	 * Allow status to settle, then read it again.
	 * A few rare drives vastly violate the 400ns spec here,
	 * so we'll wait up to 10usec for a "good" status
	 * rather than expensively fail things immediately.
	 * This fix courtesy of Matthew Faupel & Niccolo Rigacci.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		if (OK_STAT((stat = GET_STAT()), good, bad))
			return 0;
	}
	(void)ide_dump_status(drive, "ide_wait_noerr", stat);
	return 1;
}


/*
 * Verify that we are doing an approved SETFEATURES_XFER with respect
 * to the hardware being able to support request.  Since some hardware
 * can improperly report capabilties, we check to see if the host adapter
 * in combination with the device (usually a disk) properly detect
 * and acknowledge each end of the ribbon.
 */
int ide_ata66_check (ide_drive_t *drive, int cmd, int nsect, int feature)
{
	if ((cmd == WIN_SETFEATURES) &&
	    (nsect > XFER_UDMA_2) &&
	    (feature == SETFEATURES_XFER)) {
		if (!HWIF(drive)->udma_four) {
			printk("%s: Speed warnings UDMA 3/4 is not functional.\n", HWIF(drive)->name);
			return 1;
		}
		if ((drive->id->word93 & 0x2000) == 0) {
			printk("%s: Speed warnings UDMA 3/4 is not functional.\n", drive->name);
			return 1;
		}
	}
	return 0;
}

/*
 * Backside of HDIO_DRIVE_CMD call of SETFEATURES_XFER.
 * 1 : Safe to update drive->id DMA registers.
 * 0 : OOPs not allowed.
 */
int set_transfer (ide_drive_t *drive, int cmd, int nsect, int feature)
{
	struct hd_driveid *id = drive->id;

	if ((cmd == WIN_SETFEATURES) &&
	    (nsect >= XFER_SW_DMA_0) &&
	    (feature == SETFEATURES_XFER) &&
	    (id->dma_ultra || id->dma_mword || id->dma_1word))
		return 1;
	return 0;
}

int ide_config_drive_speed (ide_drive_t *drive, byte speed)
{
	unsigned long flags;
	int err;
	byte stat;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	/*
	 * Don't use ide_wait_cmd here - it will
	 * attempt to set_geometry and recalibrate,
	 * but for some reason these don't work at
	 * this point (lost interrupt).
	 */
	SELECT_DRIVE(HWIF(drive), drive);
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl | 2, IDE_CONTROL_REG);
	OUT_BYTE(speed, IDE_NSECTOR_REG);
	OUT_BYTE(SETFEATURES_XFER, IDE_FEATURE_REG);
	OUT_BYTE(WIN_SETFEATURES, IDE_COMMAND_REG);

	err = ide_wait_noerr(drive, DRIVE_READY, BUSY_STAT|DRQ_STAT|ERR_STAT, WAIT_CMD);

#if 0
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl, IDE_CONTROL_REG);
#endif

	__restore_flags(flags);	/* local CPU only */

	stat = GET_STAT();
	if (stat != DRIVE_READY)
		(void) ide_dump_status(drive, "set_drive_speed_status", stat);

	drive->id->dma_ultra &= ~0xFF00;
	drive->id->dma_mword &= ~0x0F00;
	drive->id->dma_1word &= ~0x0F00;

	switch(speed) {
		case XFER_UDMA_4:   drive->id->dma_ultra |= 0x1010; break;
		case XFER_UDMA_3:   drive->id->dma_ultra |= 0x0808; break;
		case XFER_UDMA_2:   drive->id->dma_ultra |= 0x0404; break;
		case XFER_UDMA_1:   drive->id->dma_ultra |= 0x0202; break;
		case XFER_UDMA_0:   drive->id->dma_ultra |= 0x0101; break;
		case XFER_MW_DMA_2: drive->id->dma_mword |= 0x0404; break;
		case XFER_MW_DMA_1: drive->id->dma_mword |= 0x0202; break;
		case XFER_MW_DMA_0: drive->id->dma_mword |= 0x0101; break;
		case XFER_SW_DMA_2: drive->id->dma_1word |= 0x0404; break;
		case XFER_SW_DMA_1: drive->id->dma_1word |= 0x0202; break;
		case XFER_SW_DMA_0: drive->id->dma_1word |= 0x0101; break;
		default: break;
	}
	return(err);
}

EXPORT_SYMBOL(ide_driveid_update);
EXPORT_SYMBOL(ide_wait_noerr);
EXPORT_SYMBOL(ide_ata66_check);
EXPORT_SYMBOL(set_transfer);
EXPORT_SYMBOL(ide_config_drive_speed);

