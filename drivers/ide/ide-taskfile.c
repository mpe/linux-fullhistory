/*
 *  Copyright (C) 2000		Michael Cornwell <cornwell@acm.org>
 *  Copyright (C) 2000		Andre Hedrick <andre@linux-ide.org>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 */

#include <linux/config.h>
#define __NO_VERSION__
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
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

#define DEBUG_TASKFILE	0	/* unset when fixed */

#if DEBUG_TASKFILE
#define DTF(x...) printk(##x)
#else
#define DTF(x...)
#endif

/*
 * for now, taskfile requests are special :/
 */
static inline char *ide_map_rq(struct request *rq, unsigned long *flags)
{
	if (rq->bio)
		return bio_kmap_irq(rq->bio, flags) + ide_rq_offset(rq);
	else
		return rq->buffer + task_rq_offset(rq);
}

static inline void ide_unmap_rq(struct request *rq, char *to,
				unsigned long *flags)
{
	if (rq->bio)
	    bio_kunmap_irq(to, flags);
}

static void ata_bswap_data (void *buffer, int wcount)
{
	u16 *p = buffer;

	while (wcount--) {
		*p = *p << 8 | *p >> 8; p++;
		*p = *p << 8 | *p >> 8; p++;
	}
}

#if SUPPORT_VLB_SYNC
/*
 * Some localbus EIDE interfaces require a special access sequence
 * when using 32-bit I/O instructions to transfer data.  We call this
 * the "vlb_sync" sequence, which consists of three successive reads
 * of the sector count register location, with interrupts disabled
 * to ensure that the reads all happen together.
 */
static inline void task_vlb_sync(ide_ioreg_t port)
{
	IN_BYTE (port);
	IN_BYTE (port);
	IN_BYTE (port);
}
#endif

/*
 * This is used for most PIO data transfers *from* the IDE interface
 */
void ata_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	byte io_32bit;

	/*
	 * first check if this controller has defined a special function
	 * for handling polled ide transfers
	 */

	if (HWIF(drive)->ideproc) {
		HWIF(drive)->ideproc(ideproc_ide_input_data, drive, buffer, wcount);
		return;
	}

	io_32bit = drive->io_32bit;

	if (io_32bit) {
#if SUPPORT_VLB_SYNC
		if (io_32bit & 2) {
			unsigned long flags;
			__save_flags(flags);	/* local CPU only */
			__cli();		/* local CPU only */
			task_vlb_sync(IDE_NSECTOR_REG);
			insl(IDE_DATA_REG, buffer, wcount);
			__restore_flags(flags);	/* local CPU only */
		} else
#endif
			insl(IDE_DATA_REG, buffer, wcount);
	} else {
#if SUPPORT_SLOW_DATA_PORTS
		if (drive->slow) {
			unsigned short *ptr = (unsigned short *) buffer;
			while (wcount--) {
				*ptr++ = inw_p(IDE_DATA_REG);
				*ptr++ = inw_p(IDE_DATA_REG);
			}
		} else
#endif
			insw(IDE_DATA_REG, buffer, wcount<<1);
	}
}

/*
 * This is used for most PIO data transfers *to* the IDE interface
 */
void ata_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	byte io_32bit;

	if (HWIF(drive)->ideproc) {
		HWIF(drive)->ideproc(ideproc_ide_output_data, drive, buffer, wcount);
		return;
	}

	io_32bit = drive->io_32bit;

	if (io_32bit) {
#if SUPPORT_VLB_SYNC
		if (io_32bit & 2) {
			unsigned long flags;
			__save_flags(flags);	/* local CPU only */
			__cli();		/* local CPU only */
			task_vlb_sync(IDE_NSECTOR_REG);
			outsl(IDE_DATA_REG, buffer, wcount);
			__restore_flags(flags);	/* local CPU only */
		} else
#endif
			outsl(IDE_DATA_REG, buffer, wcount);
	} else {
#if SUPPORT_SLOW_DATA_PORTS
		if (drive->slow) {
			unsigned short *ptr = (unsigned short *) buffer;
			while (wcount--) {
				outw_p(*ptr++, IDE_DATA_REG);
				outw_p(*ptr++, IDE_DATA_REG);
			}
		} else
#endif
			outsw(IDE_DATA_REG, buffer, wcount<<1);
	}
}

/*
 * The following routines are mainly used by the ATAPI drivers.
 *
 * These routines will round up any request for an odd number of bytes,
 * so if an odd bytecount is specified, be sure that there's at least one
 * extra byte allocated for the buffer.
 */
void atapi_input_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount)
{
	if (HWIF(drive)->ideproc) {
		HWIF(drive)->ideproc(ideproc_atapi_input_bytes, drive, buffer, bytecount);
		return;
	}

	++bytecount;
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
	if (MACH_IS_ATARI || MACH_IS_Q40) {
		/* Atari has a byte-swapped IDE interface */
		insw_swapw(IDE_DATA_REG, buffer, bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI */
	ata_input_data (drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		insw (IDE_DATA_REG, ((byte *)buffer) + (bytecount & ~0x03), 1);
}

void atapi_output_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount)
{
	if (HWIF(drive)->ideproc) {
		HWIF(drive)->ideproc(ideproc_atapi_output_bytes, drive, buffer, bytecount);
		return;
	}

	++bytecount;
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
	if (MACH_IS_ATARI || MACH_IS_Q40) {
		/* Atari has a byte-swapped IDE interface */
		outsw_swapw(IDE_DATA_REG, buffer, bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI */
	ata_output_data (drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		outsw (IDE_DATA_REG, ((byte *)buffer) + (bytecount & ~0x03), 1);
}

void taskfile_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	ata_input_data(drive, buffer, wcount);
	if (drive->bswap)
		ata_bswap_data(buffer, wcount);
}

void taskfile_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	if (drive->bswap) {
		ata_bswap_data(buffer, wcount);
		ata_output_data(drive, buffer, wcount);
		ata_bswap_data(buffer, wcount);
	} else {
		ata_output_data(drive, buffer, wcount);
	}
}

/*
 * Needed for PCI irq sharing
 */
int drive_is_ready (ide_drive_t *drive)
{
	byte stat = 0;
	if (drive->waiting_for_dma)
		return HWIF(drive)->dmaproc(ide_dma_test_irq, drive);
#if 0
	/* need to guarantee 400ns since last command was issued */
	udelay(1);
#endif

#ifdef CONFIG_IDEPCI_SHARE_IRQ
	/*
	 * We do a passive status test under shared PCI interrupts on
	 * cards that truly share the ATA side interrupt, but may also share
	 * an interrupt with another pci card/device.  We make no assumptions
	 * about possible isa-pnp and pci-pnp issues yet.
	 */
	if (IDE_CONTROL_REG)
		stat = GET_ALTSTAT();
	else
#endif /* CONFIG_IDEPCI_SHARE_IRQ */
	stat = GET_STAT();	/* Note: this may clear a pending IRQ!! */

	if (stat & BUSY_STAT)
		return 0;	/* drive busy:  definitely not interrupting */
	return 1;		/* drive ready: *might* be interrupting */
}

/*
 * Polling wait until the drive is ready.
 *
 * Stuff the first sector(s) by implicitly calling the handler driectly
 * therafter.
 */
void ata_poll_drive_ready(ide_drive_t *drive)
{
	int i;


	if (drive_is_ready(drive))
		return;

	/* FIXME: Replace hard-coded 100, what about error handling?
	 */
	for (i = 0; i < 100; ++i) {
		if (drive_is_ready(drive))
			break;
	}
}
static ide_startstop_t bio_mulout_intr(ide_drive_t *drive);

/*
 * Handler for command write multiple
 * Called directly from execute_drive_cmd for the first bunch of sectors,
 * afterwards only by the ISR
 */
static ide_startstop_t task_mulout_intr (ide_drive_t *drive)
{
	unsigned int		msect, nsect;
	byte stat		= GET_STAT();
	byte io_32bit		= drive->io_32bit;
	struct request *rq	= HWGROUP(drive)->rq;
	ide_hwgroup_t *hwgroup	= HWGROUP(drive);
	char *pBuf		= NULL;
	unsigned long flags;

	/*
	 * (ks/hs): Handle last IRQ on multi-sector transfer,
	 * occurs after all data was sent in this chunk
	 */
	if (rq->current_nr_sectors == 0) {
		if (stat & (ERR_STAT|DRQ_STAT))
			return ide_error(drive, "task_mulout_intr", stat);

		/*
		 * there may be more, ide_do_request will restart it if
		 * necessary
		 */
		ide_end_request(drive, 1);
		return ide_stopped;
	}

	if (!OK_STAT(stat,DATA_READY,BAD_R_STAT)) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			return ide_error(drive, "task_mulout_intr", stat);
		}
		/* no data yet, so wait for another interrupt */
		if (hwgroup->handler == NULL)
			ide_set_handler(drive, &task_mulout_intr, WAIT_CMD, NULL);
		return ide_started;
	}

	/* (ks/hs): See task_mulin_intr */
	msect = drive->mult_count;
	nsect = rq->current_nr_sectors;
	if (nsect > msect)
		nsect = msect;

	pBuf = ide_map_rq(rq, &flags);
	DTF("Multiwrite: %p, nsect: %d , rq->current_nr_sectors: %ld\n",
		pBuf, nsect, rq->current_nr_sectors);
	drive->io_32bit = 0;
	taskfile_output_data(drive, pBuf, nsect * SECTOR_WORDS);
	ide_unmap_rq(rq, pBuf, &flags);
	drive->io_32bit = io_32bit;
	rq->errors = 0;
	rq->current_nr_sectors -= nsect;
	if (hwgroup->handler == NULL)
		ide_set_handler(drive, &task_mulout_intr, WAIT_CMD, NULL);
	return ide_started;
}

ide_startstop_t do_rw_taskfile (ide_drive_t *drive, ide_task_t *task)
{
	task_struct_t *taskfile = (task_struct_t *) task->tfRegister;
	hob_struct_t *hobfile = (hob_struct_t *) task->hobRegister;
	struct hd_driveid *id = drive->id;
	byte HIHI = (drive->addressing) ? 0xE0 : 0xEF;

	/* (ks/hs): Moved to start, do not use for multiple out commands */
	if (task->handler != task_mulout_intr && task->handler != bio_mulout_intr) {
		if (IDE_CONTROL_REG)
			OUT_BYTE(drive->ctl, IDE_CONTROL_REG);	/* clear nIEN */
		SELECT_MASK(HWIF(drive), drive, 0);
	}

	if ((id->command_set_2 & 0x0400) &&
	    (id->cfs_enable_2 & 0x0400) &&
	    (drive->addressing == 1)) {
		OUT_BYTE(hobfile->feature, IDE_FEATURE_REG);
		OUT_BYTE(hobfile->sector_count, IDE_NSECTOR_REG);
		OUT_BYTE(hobfile->sector_number, IDE_SECTOR_REG);
		OUT_BYTE(hobfile->low_cylinder, IDE_LCYL_REG);
		OUT_BYTE(hobfile->high_cylinder, IDE_HCYL_REG);
	}

	OUT_BYTE(taskfile->feature, IDE_FEATURE_REG);
	OUT_BYTE(taskfile->sector_count, IDE_NSECTOR_REG);
	/* refers to number of sectors to transfer */
	OUT_BYTE(taskfile->sector_number, IDE_SECTOR_REG);
	/* refers to sector offset or start sector */
	OUT_BYTE(taskfile->low_cylinder, IDE_LCYL_REG);
	OUT_BYTE(taskfile->high_cylinder, IDE_HCYL_REG);

	OUT_BYTE((taskfile->device_head & HIHI) | drive->select.all, IDE_SELECT_REG);
	if (task->handler != NULL) {
		ide_set_handler (drive, task->handler, WAIT_CMD, NULL);
		OUT_BYTE(taskfile->command, IDE_COMMAND_REG);
		/*
		 * Warning check for race between handler and prehandler for
		 * writing first block of data.  however since we are well
		 * inside the boundaries of the seek, we should be okay.
		 */
		if (task->prehandler != NULL) {
			return task->prehandler(drive, task->rq);
		}
	} else {
		/* for dma commands we down set the handler */
		if (drive->using_dma && !(HWIF(drive)->dmaproc(((taskfile->command == WIN_WRITEDMA) || (taskfile->command == WIN_WRITEDMA_EXT)) ? ide_dma_write : ide_dma_read, drive)));
	}

	return ide_started;
}

void do_taskfile (ide_drive_t *drive, struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile, ide_handler_t *handler)
{
	struct hd_driveid *id = drive->id;
	byte HIHI = (drive->addressing) ? 0xE0 : 0xEF;

	/* (ks/hs): Moved to start, do not use for multiple out commands */
	if (*handler != task_mulout_intr && handler != bio_mulout_intr) {
		if (IDE_CONTROL_REG)
			OUT_BYTE(drive->ctl, IDE_CONTROL_REG);  /* clear nIEN */
		SELECT_MASK(HWIF(drive), drive, 0);
	}

	if ((id->command_set_2 & 0x0400) &&
	    (id->cfs_enable_2 & 0x0400) &&
	    (drive->addressing == 1)) {
		OUT_BYTE(hobfile->feature, IDE_FEATURE_REG);
		OUT_BYTE(hobfile->sector_count, IDE_NSECTOR_REG);
		OUT_BYTE(hobfile->sector_number, IDE_SECTOR_REG);
		OUT_BYTE(hobfile->low_cylinder, IDE_LCYL_REG);
		OUT_BYTE(hobfile->high_cylinder, IDE_HCYL_REG);
	}

	OUT_BYTE(taskfile->feature, IDE_FEATURE_REG);
	OUT_BYTE(taskfile->sector_count, IDE_NSECTOR_REG);
	/* refers to number of sectors to transfer */
	OUT_BYTE(taskfile->sector_number, IDE_SECTOR_REG);
	/* refers to sector offset or start sector */
	OUT_BYTE(taskfile->low_cylinder, IDE_LCYL_REG);
	OUT_BYTE(taskfile->high_cylinder, IDE_HCYL_REG);

	OUT_BYTE((taskfile->device_head & HIHI) | drive->select.all, IDE_SELECT_REG);
	if (handler != NULL) {
		ide_set_handler (drive, handler, WAIT_CMD, NULL);
		OUT_BYTE(taskfile->command, IDE_COMMAND_REG);
	} else {
		/* for dma commands we down set the handler */
		if (drive->using_dma && !(HWIF(drive)->dmaproc(((taskfile->command == WIN_WRITEDMA) || (taskfile->command == WIN_WRITEDMA_EXT)) ? ide_dma_write : ide_dma_read, drive)));
	}
}

/*
 * Handler for special commands without a data phase from ide-disk
 */

/*
 * This is invoked on completion of a WIN_SETMULT cmd.
 */
ide_startstop_t set_multmode_intr (ide_drive_t *drive)
{
	byte stat;

	if (OK_STAT(stat=GET_STAT(),READY_STAT,BAD_STAT)) {
		drive->mult_count = drive->mult_req;
	} else {
		drive->mult_req = drive->mult_count = 0;
		drive->special.b.recalibrate = 1;
		ide_dump_status(drive, "set_multmode", stat);
	}
	return ide_stopped;
}

/*
 * This is invoked on completion of a WIN_SPECIFY cmd.
 */
static ide_startstop_t set_geometry_intr (ide_drive_t *drive)
{
	byte stat;

	if (OK_STAT(stat=GET_STAT(),READY_STAT,BAD_STAT))
		return ide_stopped;

	if (stat & (ERR_STAT|DRQ_STAT))
		return ide_error(drive, "set_geometry_intr", stat);

	ide_set_handler(drive, &set_geometry_intr, WAIT_CMD, NULL);
	return ide_started;
}

/*
 * This is invoked on completion of a WIN_RESTORE (recalibrate) cmd.
 */
static ide_startstop_t recal_intr (ide_drive_t *drive)
{
	byte stat = GET_STAT();

	if (!OK_STAT(stat,READY_STAT,BAD_STAT))
		return ide_error(drive, "recal_intr", stat);
	return ide_stopped;
}

/*
 * Handler for commands without a data phase
 */
ide_startstop_t task_no_data_intr (ide_drive_t *drive)
{
	ide_task_t *args	= HWGROUP(drive)->rq->special;
	byte stat		= GET_STAT();

	ide__sti();	/* local CPU only */

	if (!OK_STAT(stat, READY_STAT, BAD_STAT))
		return ide_error(drive, "task_no_data_intr", stat);
		/* calls ide_end_drive_cmd */
	if (args)
		ide_end_drive_cmd (drive, stat, GET_ERR());

	return ide_stopped;
}

/*
 * Handler for command with PIO data-in phase
 */
static ide_startstop_t task_in_intr (ide_drive_t *drive)
{
	byte stat		= GET_STAT();
	byte io_32bit		= drive->io_32bit;
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned long flags;

	if (!OK_STAT(stat,DATA_READY,BAD_R_STAT)) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			return ide_error(drive, "task_in_intr", stat);
		}
		if (!(stat & BUSY_STAT)) {
			DTF("task_in_intr to Soon wait for next interrupt\n");
			ide_set_handler(drive, &task_in_intr, WAIT_CMD, NULL);
			return ide_started;
		}
	}
	DTF("stat: %02x\n", stat);
	pBuf = ide_map_rq(rq, &flags);
	DTF("Read: %p, rq->current_nr_sectors: %d\n", pBuf, (int) rq->current_nr_sectors);

	drive->io_32bit = 0;
	taskfile_input_data(drive, pBuf, SECTOR_WORDS);
	ide_unmap_rq(rq, pBuf, &flags);
	drive->io_32bit = io_32bit;

	if (--rq->current_nr_sectors <= 0) {
		/* (hs): swapped next 2 lines */
		DTF("Request Ended stat: %02x\n", GET_STAT());
		if (ide_end_request(drive, 1)) {
			ide_set_handler(drive, &task_in_intr,  WAIT_CMD, NULL);
			return ide_started;
		}
	} else {
		ide_set_handler(drive, &task_in_intr,  WAIT_CMD, NULL);
		return ide_started;
	}
	return ide_stopped;
}

static ide_startstop_t pre_task_out_intr (ide_drive_t *drive, struct request *rq)
{
	ide_task_t *args = rq->special;
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, DATA_READY, drive->bad_wstat, WAIT_DRQ)) {
		printk(KERN_ERR "%s: no DRQ after issuing %s\n", drive->name, drive->mult_count ? "MULTWRITE" : "WRITE");
		return startstop;
	}

	/* (ks/hs): Fixed Multi Write */
	if ((args->tfRegister[IDE_COMMAND_OFFSET] != WIN_MULTWRITE) &&
	    (args->tfRegister[IDE_COMMAND_OFFSET] != WIN_MULTWRITE_EXT)) {
		unsigned long flags;
		char *buf = ide_map_rq(rq, &flags);
		/* For Write_sectors we need to stuff the first sector */
		taskfile_output_data(drive, buf, SECTOR_WORDS);
		rq->current_nr_sectors--;
		ide_unmap_rq(rq, buf, &flags);
	} else {
		ata_poll_drive_ready(drive);
		return args->handler(drive);
	}
	return ide_started;
}

/*
 * Handler for command with PIO data-out phase
 */
static ide_startstop_t task_out_intr(ide_drive_t *drive)
{
	byte stat		= GET_STAT();
	byte io_32bit		= drive->io_32bit;
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned long flags;

	if (!OK_STAT(stat,DRIVE_READY,drive->bad_wstat))
		return ide_error(drive, "task_out_intr", stat);

	if (!rq->current_nr_sectors)
		if (!ide_end_request(drive, 1))
			return ide_stopped;

	if ((rq->current_nr_sectors==1) ^ (stat & DRQ_STAT)) {
		rq = HWGROUP(drive)->rq;
		pBuf = ide_map_rq(rq, &flags);
		DTF("write: %p, rq->current_nr_sectors: %d\n", pBuf, (int) rq->current_nr_sectors);
		drive->io_32bit = 0;
		taskfile_output_data(drive, pBuf, SECTOR_WORDS);
		ide_unmap_rq(rq, pBuf, &flags);
		drive->io_32bit = io_32bit;
		rq->errors = 0;
		rq->current_nr_sectors--;
	}

	ide_set_handler(drive, task_out_intr, WAIT_CMD, NULL);
	return ide_started;
}

static ide_startstop_t pre_bio_out_intr(ide_drive_t *drive, struct request *rq)
{
	ide_task_t *args = rq->special;
	ide_startstop_t startstop;

	/*
	 * assign private copy for multi-write
	 */
	memcpy(&HWGROUP(drive)->wrq, rq, sizeof(struct request));

	if (ide_wait_stat(&startstop, drive, DATA_READY, drive->bad_wstat, WAIT_DRQ))
		return startstop;

	ata_poll_drive_ready(drive);
	return args->handler(drive);
}


static ide_startstop_t bio_mulout_intr (ide_drive_t *drive)
{
	byte stat		= GET_STAT();
	byte io_32bit		= drive->io_32bit;
	struct request *rq	= &HWGROUP(drive)->wrq;
	ide_hwgroup_t *hwgroup	= HWGROUP(drive);
	int mcount		= drive->mult_count;
	ide_startstop_t startstop;

	/*
	 * (ks/hs): Handle last IRQ on multi-sector transfer,
	 * occurs after all data was sent in this chunk
	 */
	if (!rq->nr_sectors) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			startstop = ide_error(drive, "bio_mulout_intr", stat);
			memcpy(rq, HWGROUP(drive)->rq, sizeof(struct request));
			return startstop;
		}

		__ide_end_request(drive, 1, rq->hard_nr_sectors);
		HWGROUP(drive)->wrq.bio = NULL;
		return ide_stopped;
	}

	if (!OK_STAT(stat, DATA_READY, BAD_R_STAT)) {
		if (stat & (ERR_STAT | DRQ_STAT)) {
			startstop = ide_error(drive, "bio_mulout_intr", stat);
			memcpy(rq, HWGROUP(drive)->rq, sizeof(struct request));
			return startstop;
		}

		/* no data yet, so wait for another interrupt */
		if (hwgroup->handler == NULL)
			ide_set_handler(drive, bio_mulout_intr, WAIT_CMD, NULL);

		return ide_started;
	}

	do {
		char *buffer;
		int nsect = rq->current_nr_sectors;
		unsigned long flags;

		if (nsect > mcount)
			nsect = mcount;
		mcount -= nsect;

		buffer = bio_kmap_irq(rq->bio, &flags) + ide_rq_offset(rq);
		rq->sector += nsect;
		rq->nr_sectors -= nsect;
		rq->current_nr_sectors -= nsect;

		/* Do we move to the next bio after this? */
		if (!rq->current_nr_sectors) {
			/* remember to fix this up /jens */
			struct bio *bio = rq->bio->bi_next;

			/* end early early we ran out of requests */
			if (!bio) {
				mcount = 0;
			} else {
				rq->bio = bio;
				rq->current_nr_sectors = bio_iovec(bio)->bv_len >> 9;
			}
		}

		/*
		 * Ok, we're all setup for the interrupt
		 * re-entering us on the last transfer.
		 */
		taskfile_output_data(drive, buffer, nsect * SECTOR_WORDS);
		bio_kunmap_irq(buffer, &flags);
	} while (mcount);

	drive->io_32bit = io_32bit;
	rq->errors = 0;
	if (hwgroup->handler == NULL)
		ide_set_handler(drive, bio_mulout_intr, WAIT_CMD, NULL);

	return ide_started;
}

/* Called by internal to feature out type of command being called */
ide_pre_handler_t * ide_pre_handler_parser (struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile)
{
	switch(taskfile->command) {
				/* IDE_DRIVE_TASK_RAW_WRITE */
				/* IDE_DRIVE_TASK_OUT */
		case WIN_WRITE:
		case WIN_WRITE_EXT:
		case WIN_WRITE_VERIFY:
		case WIN_WRITE_BUFFER:
		case CFA_WRITE_SECT_WO_ERASE:
		case WIN_DOWNLOAD_MICROCODE:
			return &pre_task_out_intr;
		case CFA_WRITE_MULTI_WO_ERASE:
		case WIN_MULTWRITE:
		case WIN_MULTWRITE_EXT:
			return &pre_bio_out_intr;
		case WIN_SMART:
			if (taskfile->feature == SMART_WRITE_LOG_SECTOR)
				return &pre_task_out_intr;
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_QUEUED:
		case WIN_WRITEDMA_EXT:
		case WIN_WRITEDMA_QUEUED_EXT:
				/* IDE_DRIVE_TASK_OUT */
		default:
			break;
	}
	return(NULL);
}

/*
 * Handler for command with Read Multiple
 */
static ide_startstop_t task_mulin_intr(ide_drive_t *drive)
{
	unsigned int		msect, nsect;
	byte stat		= GET_STAT();
	byte io_32bit		= drive->io_32bit;
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned long flags;

	if (!OK_STAT(stat,DATA_READY,BAD_R_STAT)) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			return ide_error(drive, "task_mulin_intr", stat);
		}
		/* no data yet, so wait for another interrupt */
		ide_set_handler(drive, task_mulin_intr, WAIT_CMD, NULL);
		return ide_started;
	}

	/* (ks/hs): Fixed Multi-Sector transfer */
	msect = drive->mult_count;

	do {
		nsect = rq->current_nr_sectors;
		if (nsect > msect)
			nsect = msect;

		pBuf = ide_map_rq(rq, &flags);

		DTF("Multiread: %p, nsect: %d , rq->current_nr_sectors: %ld\n",
			pBuf, nsect, rq->current_nr_sectors);
		drive->io_32bit = 0;
		taskfile_input_data(drive, pBuf, nsect * SECTOR_WORDS);
		ide_unmap_rq(rq, pBuf, &flags);
		drive->io_32bit = io_32bit;
		rq->errors = 0;
		rq->current_nr_sectors -= nsect;
		msect -= nsect;
		if (!rq->current_nr_sectors) {
			if (!ide_end_request(drive, 1))
				return ide_stopped;
		}
	} while (msect);


	/*
	 * more data left
	 */
	ide_set_handler(drive, task_mulin_intr, WAIT_CMD, NULL);
	return ide_started;
}

/* Called by internal to feature out type of command being called */
ide_handler_t * ide_handler_parser (struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile)
{
	switch(taskfile->command) {
		case WIN_IDENTIFY:
		case WIN_PIDENTIFY:
		case CFA_TRANSLATE_SECTOR:
		case WIN_READ_BUFFER:
		case WIN_READ:
		case WIN_READ_EXT:
			return &task_in_intr;
		case WIN_SECURITY_DISABLE:
		case WIN_SECURITY_ERASE_UNIT:
		case WIN_SECURITY_SET_PASS:
		case WIN_SECURITY_UNLOCK:
		case WIN_DOWNLOAD_MICROCODE:
		case CFA_WRITE_SECT_WO_ERASE:
		case WIN_WRITE_BUFFER:
		case WIN_WRITE_VERIFY:
		case WIN_WRITE:
		case WIN_WRITE_EXT:
			return &task_out_intr;
		case WIN_MULTREAD:
		case WIN_MULTREAD_EXT:
			return &task_mulin_intr;
		case CFA_WRITE_MULTI_WO_ERASE:
		case WIN_MULTWRITE:
		case WIN_MULTWRITE_EXT:
			return &bio_mulout_intr;
		case WIN_SMART:
			switch(taskfile->feature) {
				case SMART_READ_VALUES:
				case SMART_READ_THRESHOLDS:
				case SMART_READ_LOG_SECTOR:
					return &task_in_intr;
				case SMART_WRITE_LOG_SECTOR:
					return &task_out_intr;
				default:
					return &task_no_data_intr;
			}
		case CFA_REQ_EXT_ERROR_CODE:
		case CFA_ERASE_SECTORS:
		case WIN_VERIFY:
		case WIN_VERIFY_EXT:
		case WIN_SEEK:
			return &task_no_data_intr;
		case WIN_SPECIFY:
			return &set_geometry_intr;
		case WIN_RESTORE:
			return &recal_intr;
		case WIN_DIAGNOSE:
		case WIN_FLUSH_CACHE:
		case WIN_FLUSH_CACHE_EXT:
		case WIN_STANDBYNOW1:
		case WIN_STANDBYNOW2:
		case WIN_SLEEPNOW1:
		case WIN_SLEEPNOW2:
		case WIN_SETIDLE1:
		case WIN_CHECKPOWERMODE1:
		case WIN_CHECKPOWERMODE2:
		case WIN_GETMEDIASTATUS:
		case WIN_MEDIAEJECT:
			return &task_no_data_intr;
		case WIN_SETMULT:
			return &set_multmode_intr;
		case WIN_READ_NATIVE_MAX:
		case WIN_SET_MAX:
		case WIN_READ_NATIVE_MAX_EXT:
		case WIN_SET_MAX_EXT:
		case WIN_SECURITY_ERASE_PREPARE:
		case WIN_SECURITY_FREEZE_LOCK:
		case WIN_DOORLOCK:
		case WIN_DOORUNLOCK:
		case WIN_SETFEATURES:
			return &task_no_data_intr;
		case DISABLE_SEAGATE:
		case EXABYTE_ENABLE_NEST:
			return &task_no_data_intr;
#ifdef CONFIG_BLK_DEV_IDEDMA
		case WIN_READDMA:
		case WIN_IDENTIFY_DMA:
		case WIN_READDMA_QUEUED:
		case WIN_READDMA_EXT:
		case WIN_READDMA_QUEUED_EXT:
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_QUEUED:
		case WIN_WRITEDMA_EXT:
		case WIN_WRITEDMA_QUEUED_EXT:
#endif
		case WIN_FORMAT:
		case WIN_INIT:
		case WIN_DEVICE_RESET:
		case WIN_QUEUED_SERVICE:
		case WIN_PACKETCMD:
		default:
			return NULL;
	}
}

/* Called by ioctl to feature out type of command being called */
int ide_cmd_type_parser (ide_task_t *args)
{
	struct hd_drive_task_hdr *taskfile = (struct hd_drive_task_hdr *) args->tfRegister;
	struct hd_drive_hob_hdr *hobfile = (struct hd_drive_hob_hdr *) args->hobRegister;

	args->prehandler = ide_pre_handler_parser(taskfile, hobfile);
	args->handler = ide_handler_parser(taskfile, hobfile);

	switch(args->tfRegister[IDE_COMMAND_OFFSET]) {
		case WIN_IDENTIFY:
		case WIN_PIDENTIFY:
			return IDE_DRIVE_TASK_IN;
		case CFA_TRANSLATE_SECTOR:
		case WIN_READ:
		case WIN_READ_EXT:
		case WIN_READ_BUFFER:
			return IDE_DRIVE_TASK_IN;
		case WIN_WRITE:
		case WIN_WRITE_EXT:
		case WIN_WRITE_VERIFY:
		case WIN_WRITE_BUFFER:
		case CFA_WRITE_SECT_WO_ERASE:
		case WIN_DOWNLOAD_MICROCODE:
			return IDE_DRIVE_TASK_RAW_WRITE;
		case WIN_MULTREAD:
		case WIN_MULTREAD_EXT:
			return IDE_DRIVE_TASK_IN;
		case CFA_WRITE_MULTI_WO_ERASE:
		case WIN_MULTWRITE:
		case WIN_MULTWRITE_EXT:
			return IDE_DRIVE_TASK_RAW_WRITE;
		case WIN_SECURITY_DISABLE:
		case WIN_SECURITY_ERASE_UNIT:
		case WIN_SECURITY_SET_PASS:
		case WIN_SECURITY_UNLOCK:
			return IDE_DRIVE_TASK_OUT;
		case WIN_SMART:
			args->tfRegister[IDE_LCYL_OFFSET] = SMART_LCYL_PASS;
			args->tfRegister[IDE_HCYL_OFFSET] = SMART_HCYL_PASS;
			switch(args->tfRegister[IDE_FEATURE_OFFSET]) {
				case SMART_READ_VALUES:
				case SMART_READ_THRESHOLDS:
				case SMART_READ_LOG_SECTOR:
					return IDE_DRIVE_TASK_IN;
				case SMART_WRITE_LOG_SECTOR:
					return IDE_DRIVE_TASK_OUT;
				default:
					return IDE_DRIVE_TASK_NO_DATA;
			}
#ifdef CONFIG_BLK_DEV_IDEDMA
		case WIN_READDMA:
		case WIN_IDENTIFY_DMA:
		case WIN_READDMA_QUEUED:
		case WIN_READDMA_EXT:
		case WIN_READDMA_QUEUED_EXT:
			return IDE_DRIVE_TASK_IN;
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_QUEUED:
		case WIN_WRITEDMA_EXT:
		case WIN_WRITEDMA_QUEUED_EXT:
			return IDE_DRIVE_TASK_RAW_WRITE;
#endif
		case WIN_SETFEATURES:
			switch(args->tfRegister[IDE_FEATURE_OFFSET]) {
				case SETFEATURES_XFER:
					return IDE_DRIVE_TASK_SET_XFER;
				case SETFEATURES_DIS_DEFECT:
				case SETFEATURES_EN_APM:
				case SETFEATURES_DIS_MSN:
				case SETFEATURES_EN_RI:
				case SETFEATURES_EN_SI:
				case SETFEATURES_DIS_RPOD:
				case SETFEATURES_DIS_WCACHE:
				case SETFEATURES_EN_DEFECT:
				case SETFEATURES_DIS_APM:
				case SETFEATURES_EN_MSN:
				case SETFEATURES_EN_RLA:
				case SETFEATURES_PREFETCH:
				case SETFEATURES_EN_RPOD:
				case SETFEATURES_DIS_RI:
				case SETFEATURES_DIS_SI:
				default:
					return IDE_DRIVE_TASK_NO_DATA;
			}
		case WIN_NOP:
		case CFA_REQ_EXT_ERROR_CODE:
		case CFA_ERASE_SECTORS:
		case WIN_VERIFY:
		case WIN_VERIFY_EXT:
		case WIN_SEEK:
		case WIN_SPECIFY:
		case WIN_RESTORE:
		case WIN_DIAGNOSE:
		case WIN_FLUSH_CACHE:
		case WIN_FLUSH_CACHE_EXT:
		case WIN_STANDBYNOW1:
		case WIN_STANDBYNOW2:
		case WIN_SLEEPNOW1:
		case WIN_SLEEPNOW2:
		case WIN_SETIDLE1:
		case DISABLE_SEAGATE:
		case WIN_CHECKPOWERMODE1:
		case WIN_CHECKPOWERMODE2:
		case WIN_GETMEDIASTATUS:
		case WIN_MEDIAEJECT:
		case WIN_SETMULT:
		case WIN_READ_NATIVE_MAX:
		case WIN_SET_MAX:
		case WIN_READ_NATIVE_MAX_EXT:
		case WIN_SET_MAX_EXT:
		case WIN_SECURITY_ERASE_PREPARE:
		case WIN_SECURITY_FREEZE_LOCK:
		case EXABYTE_ENABLE_NEST:
		case WIN_DOORLOCK:
		case WIN_DOORUNLOCK:
			return IDE_DRIVE_TASK_NO_DATA;
		case WIN_FORMAT:
		case WIN_INIT:
		case WIN_DEVICE_RESET:
		case WIN_QUEUED_SERVICE:
		case WIN_PACKETCMD:
		default:
			return IDE_DRIVE_TASK_INVALID;
	}
}

/*
 * This function is intended to be used prior to invoking ide_do_drive_cmd().
 */
static void ide_init_drive_taskfile (struct request *rq)
{
	memset(rq, 0, sizeof(*rq));
	rq->flags = REQ_DRIVE_TASKFILE;
}

/*
 * This is kept for internal use only !!!
 * This is an internal call and nobody in user-space has a damn
 * reason to call this taskfile.
 *
 * ide_raw_taskfile is the one that user-space executes.
 */
int ide_wait_taskfile(ide_drive_t *drive, struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile, byte *buf)
{
	struct request rq;
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));

	args.tfRegister[IDE_DATA_OFFSET]         = taskfile->data;
	args.tfRegister[IDE_FEATURE_OFFSET]      = taskfile->feature;
	args.tfRegister[IDE_NSECTOR_OFFSET]      = taskfile->sector_count;
	args.tfRegister[IDE_SECTOR_OFFSET]       = taskfile->sector_number;
	args.tfRegister[IDE_LCYL_OFFSET]         = taskfile->low_cylinder;
	args.tfRegister[IDE_HCYL_OFFSET]         = taskfile->high_cylinder;
	args.tfRegister[IDE_SELECT_OFFSET]       = taskfile->device_head;
	args.tfRegister[IDE_COMMAND_OFFSET]      = taskfile->command;

	args.hobRegister[IDE_DATA_OFFSET_HOB]    = hobfile->data;
	args.hobRegister[IDE_FEATURE_OFFSET_HOB] = hobfile->feature;
	args.hobRegister[IDE_NSECTOR_OFFSET_HOB] = hobfile->sector_count;
	args.hobRegister[IDE_SECTOR_OFFSET_HOB]  = hobfile->sector_number;
	args.hobRegister[IDE_LCYL_OFFSET_HOB]    = hobfile->low_cylinder;
	args.hobRegister[IDE_HCYL_OFFSET_HOB]    = hobfile->high_cylinder;
	args.hobRegister[IDE_SELECT_OFFSET_HOB]  = hobfile->device_head;
	args.hobRegister[IDE_CONTROL_OFFSET_HOB] = hobfile->control;

	ide_init_drive_taskfile(&rq);
	/* This is kept for internal use only !!! */
	args.command_type = ide_cmd_type_parser (&args);
	if (args.command_type != IDE_DRIVE_TASK_NO_DATA)
		rq.current_nr_sectors = rq.nr_sectors = (hobfile->sector_count << 8) | taskfile->sector_count;

	rq.buffer = buf;
	rq.special = &args;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

int ide_raw_taskfile(ide_drive_t *drive, ide_task_t *args, byte *buf)
{
	struct request rq;
	ide_init_drive_taskfile(&rq);
	rq.buffer = buf;

	if (args->command_type != IDE_DRIVE_TASK_NO_DATA)
		rq.current_nr_sectors = rq.nr_sectors = (args->hobRegister[IDE_NSECTOR_OFFSET_HOB] << 8) | args->tfRegister[IDE_NSECTOR_OFFSET];

	rq.special = args;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

/*
 *  The taskfile glue table
 *
 *  reqtask.data_phase	reqtask.req_cmd
 *			args.command_type		args.handler
 *
 *  TASKFILE_P_OUT_DMAQ	??				??
 *  TASKFILE_P_IN_DMAQ	??				??
 *  TASKFILE_P_OUT_DMA	??				??
 *  TASKFILE_P_IN_DMA	??				??
 *  TASKFILE_P_OUT	??				??
 *  TASKFILE_P_IN	??				??
 *
 *  TASKFILE_OUT_DMAQ	IDE_DRIVE_TASK_RAW_WRITE	NULL
 *  TASKFILE_IN_DMAQ	IDE_DRIVE_TASK_IN		NULL
 *
 *  TASKFILE_OUT_DMA	IDE_DRIVE_TASK_RAW_WRITE	NULL
 *  TASKFILE_IN_DMA	IDE_DRIVE_TASK_IN		NULL
 *
 *  TASKFILE_IN_OUT	??				??
 *
 *  TASKFILE_MULTI_OUT	IDE_DRIVE_TASK_RAW_WRITE	task_mulout_intr
 *  TASKFILE_MULTI_IN	IDE_DRIVE_TASK_IN		task_mulin_intr
 *
 *  TASKFILE_OUT	IDE_DRIVE_TASK_RAW_WRITE	task_out_intr
 *  TASKFILE_OUT	IDE_DRIVE_TASK_OUT		task_out_intr
 *
 *  TASKFILE_IN		IDE_DRIVE_TASK_IN		task_in_intr
 *  TASKFILE_NO_DATA	IDE_DRIVE_TASK_NO_DATA		task_no_data_intr
 *
 *			IDE_DRIVE_TASK_SET_XFER		task_no_data_intr
 *			IDE_DRIVE_TASK_INVALID
 *
 */

/*
 * Issue ATA command and wait for completion. use for implementing commands in
 * kernel.
 *
 * The caller has to make sure buf is never NULL!
 */
static int ide_wait_cmd(ide_drive_t *drive, int cmd, int nsect, int feature, int sectors, byte *argbuf)
{
	struct request rq;

	/* FIXME: Do we really have to zero out the buffer?
	 */
	memset(argbuf, 0, 4 + SECTOR_WORDS * 4 * sectors);
	ide_init_drive_cmd(&rq);
	rq.buffer = argbuf;
	*argbuf++ = cmd;
	*argbuf++ = nsect;
	*argbuf++ = feature;
	*argbuf++ = sectors;

	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

int ide_cmd_ioctl(ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	byte args[4], *argbuf = args;
	byte xfer_rate = 0;
	int argsize = 4;
	ide_task_t tfargs;

	if (NULL == (void *) arg) {
		struct request rq;
		ide_init_drive_cmd(&rq);
		return ide_do_drive_cmd(drive, &rq, ide_wait);
	}
	if (copy_from_user(args, (void *)arg, 4))
		return -EFAULT;

	tfargs.tfRegister[IDE_FEATURE_OFFSET] = args[2];
	tfargs.tfRegister[IDE_NSECTOR_OFFSET] = args[3];
	tfargs.tfRegister[IDE_SECTOR_OFFSET]  = args[1];
	tfargs.tfRegister[IDE_LCYL_OFFSET]    = 0x00;
	tfargs.tfRegister[IDE_HCYL_OFFSET]    = 0x00;
	tfargs.tfRegister[IDE_SELECT_OFFSET]  = 0x00;
	tfargs.tfRegister[IDE_COMMAND_OFFSET] = args[0];

	if (args[3]) {
		argsize = 4 + (SECTOR_WORDS * 4 * args[3]);
		argbuf = kmalloc(argsize, GFP_KERNEL);
		if (argbuf == NULL)
			return -ENOMEM;
		memcpy(argbuf, args, 4);
	}
	if (set_transfer(drive, &tfargs)) {
		xfer_rate = args[1];
		if (ide_ata66_check(drive, &tfargs))
			goto abort;
	}

	err = ide_wait_cmd(drive, args[0], args[1], args[2], args[3], argbuf);

	if (!err && xfer_rate) {
		/* active-retuning-calls future */
		if ((HWIF(drive)->speedproc) != NULL)
			HWIF(drive)->speedproc(drive, xfer_rate);
		ide_driveid_update(drive);
	}
abort:
	if (copy_to_user((void *)arg, argbuf, argsize))
		err = -EFAULT;
	if (argsize > 4)
		kfree(argbuf);
	return err;
}

int ide_task_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	u8 args[7];
	u8 *argbuf;
	int argsize = 7;
	struct request rq;

	argbuf = args;

	if (copy_from_user(args, (void *)arg, 7))
		return -EFAULT;

	ide_init_drive_cmd(&rq);
	rq.flags = REQ_DRIVE_TASK;
	rq.buffer = argbuf;
	err = ide_do_drive_cmd(drive, &rq, ide_wait);
	if (copy_to_user((void *)arg, argbuf, argsize))
		err = -EFAULT;
	return err;
}

EXPORT_SYMBOL(drive_is_ready);
EXPORT_SYMBOL(ata_input_data);
EXPORT_SYMBOL(ata_output_data);
EXPORT_SYMBOL(atapi_input_bytes);
EXPORT_SYMBOL(atapi_output_bytes);
EXPORT_SYMBOL(taskfile_input_data);
EXPORT_SYMBOL(taskfile_output_data);
EXPORT_SYMBOL(do_rw_taskfile);
EXPORT_SYMBOL(do_taskfile);

EXPORT_SYMBOL(set_multmode_intr);

EXPORT_SYMBOL(task_no_data_intr);

EXPORT_SYMBOL(ide_wait_taskfile);
EXPORT_SYMBOL(ide_raw_taskfile);
EXPORT_SYMBOL(ide_pre_handler_parser);
EXPORT_SYMBOL(ide_handler_parser);
EXPORT_SYMBOL(ide_cmd_type_parser);
EXPORT_SYMBOL(ide_cmd_ioctl);
EXPORT_SYMBOL(ide_task_ioctl);
