/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  Copyright (C) 1994-1998,2002  Linus Torvalds and authors:
 *
 *	Mark Lord	<mlord@pobox.com>
 *      Gadi Oxman	<gadio@netvision.net.il>
 *      Andre Hedrick	<andre@linux-ide.org>
 *	Jens Axboe	<axboe@suse.de>
 *      Marcin Dalecki	<martin@dalecki.de>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 * This is the basic common code of the ATA interface drivers.
 *
 * It supports up to MAX_HWIFS IDE interfaces, on one or more IRQs (usually 14
 * & 15).  There can be up to two drives per interface, as per the ATA-7 spec.
 *
 * Primary:    ide0, port 0x1f0; major=3;  hda is minor=0; hdb is minor=64
 * Secondary:  ide1, port 0x170; major=22; hdc is minor=0; hdd is minor=64
 * Tertiary:   ide2, port 0x???; major=33; hde is minor=0; hdf is minor=64
 * Quaternary: ide3, port 0x???; major=34; hdg is minor=0; hdh is minor=64
 * ...
 *
 *  Contributors:
 *
 *	Drew Eckhardt
 *	Branko Lankester	<lankeste@fwi.uva.nl>
 *	Mika Liljeberg
 *	Delman Lee		<delman@ieee.org>
 *	Scott Snyder		<snyder@fnald0.fnal.gov>
 *
 *  Some additional driver compile-time options are in <linux/ide.h>
 */

#include <linux/config.h>
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
#ifndef MODULE
# include <linux/init.h>
#endif
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/reboot.h>
#include <linux/cdrom.h>
#include <linux/device.h>
#include <linux/kmod.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

#include "ata-timing.h"
#include "pcihost.h"
#include "ioctl.h"

/*
 * CompactFlash cards and their relatives pretend to be removable hard disks, except:
 *	(1) they never have a slave unit, and
 *	(2) they don't have a door lock mechanisms.
 * This test catches them, and is invoked elsewhere when setting appropriate config bits.
 *
 * FIXME FIXME: Yes this is for certain applicable for all of them as time has shown.
 *
 * FIXME: This treatment is probably applicable for *all* PCMCIA (PC CARD) devices,
 * so in linux 2.3.x we should change this to just treat all PCMCIA drives this way,
 * and get rid of the model-name tests below (too big of an interface change for 2.2.x).
 * At that time, we might also consider parameterizing the timeouts and retries,
 * since these are MUCH faster than mechanical drives.	-M.Lord
 */
int drive_is_flashcard(struct ata_device *drive)
{
	struct hd_driveid *id = drive->id;
	int i;

	char *flashcards[] = {
		"KODAK ATA_FLASH",
		"Hitachi CV",
		"SunDisk SDCFB",
		"HAGIWARA HPC",
		"LEXAR ATA_FLASH",
		"ATA_FLASH"		/* Simple Tech */
	};

	if (drive->removable && id != NULL) {
		if (id->config == 0x848a)
			return 1;	/* CompactFlash */
		for (i = 0; i < ARRAY_SIZE(flashcards); i++)
			if (!strncmp(id->model, flashcards[i],
				     strlen(flashcards[i])))
				return 1;
	}
	return 0;
}

/*
 * Not locking variabt of the end_request method.
 *
 * Channel lock should be held.
 */
int __ata_end_request(struct ata_device *drive, struct request *rq, int uptodate, unsigned int nr_secs)
{
	int ret = 1;

	BUG_ON(!(rq->flags & REQ_STARTED));

	/* FIXME: Make this "small" hack to eliminate locking from
	 * ata_end_request to grab the first segment number of sectors go away.
	 */
	if (!nr_secs)
		nr_secs = rq->hard_cur_sectors;

	/*
	 * Decide whether to reenable DMA -- 3 is a random magic for now,
	 * if we DMA timeout more than 3 times, just stay in PIO.
	 */
	if (drive->state == DMA_PIO_RETRY && drive->retry_pio <= 3) {
		drive->state = 0;
		udma_enable(drive, 1, 1);
	}

	if (!end_that_request_first(rq, uptodate, nr_secs)) {
		add_blkdev_randomness(major(rq->rq_dev));
		if (!blk_rq_tagged(rq))
			blkdev_dequeue_request(rq);
		else
			blk_queue_end_tag(&drive->queue, rq);
		drive->rq = NULL;
		end_that_request_last(rq);
		ret = 0;
	}


	return ret;
}

/*
 * This is the default end request function as well
 */
int ata_end_request(struct ata_device *drive, struct request *rq, int uptodate)
{
	unsigned long flags;
	struct ata_channel *ch = drive->channel;
	int ret;

	spin_lock_irqsave(ch->lock, flags);
	ret = __ata_end_request(drive, rq, uptodate, 0);
	spin_unlock_irqrestore(drive->channel->lock, flags);

	return ret;
}

/*
 * This should get invoked any time we exit the driver to
 * wait for an interrupt response from a drive.  handler() points
 * at the appropriate code to handle the next interrupt, and a
 * timer is started to prevent us from waiting forever in case
 * something goes wrong (see the ide_timer_expiry() handler later on).
 *
 * Channel lock should be held.
 */
void ata_set_handler(struct ata_device *drive, ata_handler_t handler,
		      unsigned long timeout, ata_expiry_t expiry)
{
	struct ata_channel *ch = drive->channel;

	if (ch->handler)
		printk("%s: %s: handler not null; old=%p, new=%p, from %p\n",
			drive->name, __FUNCTION__, ch->handler, handler, __builtin_return_address(0));

	ch->handler = handler;

	ch->expiry = expiry;
	ch->timer.expires = jiffies + timeout;

	add_timer(&ch->timer);

}

static void check_crc_errors(struct ata_device *drive)
{
	if (!drive->using_dma)
	    return;

	/* check the DMA crc count */
	if (drive->crc_count) {
		udma_enable(drive, 0, 0);
		if (drive->channel->speedproc) {
			u8 pio = XFER_PIO_4;
			drive->crc_count = 0;

			switch (drive->current_speed) {
			case XFER_UDMA_7: pio = XFER_UDMA_6;
				break;
			case XFER_UDMA_6: pio = XFER_UDMA_5;
				break;
			case XFER_UDMA_5: pio = XFER_UDMA_4;
				break;
			case XFER_UDMA_4: pio = XFER_UDMA_3;
				break;
			case XFER_UDMA_3: pio = XFER_UDMA_2;
				break;
			case XFER_UDMA_2: pio = XFER_UDMA_1;
				break;
			case XFER_UDMA_1: pio = XFER_UDMA_0;
				break;
			/*
			 * OOPS we do not goto non Ultra DMA modes
			 * without iCRC's available we force
			 * the system to PIO and make the user
			 * invoke the ATA-1 ATA-2 DMA modes.
			 */
			case XFER_UDMA_0:
			default:
				pio = XFER_PIO_4;
			}
		        drive->channel->speedproc(drive, pio);
		}
		if (drive->current_speed >= XFER_SW_DMA_0)
			udma_enable(drive, 1, 1);
	} else
		udma_enable(drive, 0, 1);
}

/*
 * The capacity of a drive according to its current geometry/LBA settings in
 * sectors.
 */
sector_t ata_capacity(struct ata_device *drive)
{
	if (!drive->present || !drive->driver)
		return 0;

	if (ata_ops(drive) && ata_ops(drive)->capacity)
		return ata_ops(drive)->capacity(drive);

	/* This used to be 0x7fffffff, but since now we use the maximal drive
	 * capacity value used by other kernel subsystems as well.
	 */

	return ~0UL;
}

extern struct block_device_operations ide_fops[];

static ide_startstop_t do_reset1(struct ata_device *, int); /* needed below */

/*
 * Poll the interface for completion every 50ms during an ATAPI drive reset
 * operation. If the drive has not yet responded, and we have not yet hit our
 * maximum waiting time, then the timer is restarted for another 50ms.
 */
static ide_startstop_t atapi_reset_pollfunc(struct ata_device *drive, struct request *__rq)
{
	unsigned long flags;
	struct ata_channel *ch = drive->channel;
	int ret = ide_stopped;

	spin_lock_irqsave(ch->lock, flags);
	ata_select(drive, 10);
	if (!ata_status(drive, 0, BUSY_STAT)) {
		if (time_before(jiffies, ch->poll_timeout)) {
			ata_set_handler(drive, atapi_reset_pollfunc, HZ/20, NULL);
			ret = ide_started;	/* continue polling */
		} else {
			ch->poll_timeout = 0;	/* end of polling */
			printk("%s: ATAPI reset timed out, status=0x%02x\n", drive->name, drive->status);

			ret = do_reset1(drive, 0);	/* do it the old fashioned way */
		}
	} else {
		printk("%s: ATAPI reset complete\n", drive->name);
		ch->poll_timeout = 0;	/* done polling */

		ret = ide_stopped;
	}
	spin_unlock_irqrestore(ch->lock, flags);

	return ret;
}

/*
 * Poll the interface for completion every 50ms during an ata reset operation.
 * If the drives have not yet responded, and we have not yet hit our maximum
 * waiting time, then the timer is restarted for another 50ms.
 */
static ide_startstop_t reset_pollfunc(struct ata_device *drive, struct request *__rq)
{
	unsigned long flags;
	struct ata_channel *ch = drive->channel;
	int ret;

	spin_lock_irqsave(ch->lock, flags);
	if (!ata_status(drive, 0, BUSY_STAT)) {
		if (time_before(jiffies, ch->poll_timeout)) {
			ata_set_handler(drive, reset_pollfunc, HZ/20, NULL);
			ret = ide_started;	/* continue polling */
		} else {
			printk("%s: reset timed out, status=0x%02x\n", ch->name, drive->status);
			++drive->failures;
			ret = ide_stopped;
		}
	} else  {
		u8 stat;

		printk("%s: reset: ", ch->name);
		if ((stat = GET_ERR()) == 1) {
			printk("success\n");
			drive->failures = 0;
		} else {
			const char *msg = "";

#if FANCY_STATUS_DUMPS
			u8 val;
			static const char *messages[5] = {
				" passed",
				" formatter device",
				" sector buffer",
				" ECC circuitry",
				" controlling MPU error"
			};

			printk("master:");
			val = stat & 0x7f;
			if (val >= 1 && val <= 5)
				msg = messages[val -1];
			if (stat & 0x80)
				printk("; slave:");
#endif
			printk(KERN_ERR "%s error [%02x]\n", msg, stat);
			++drive->failures;
		}

		ret = ide_stopped;
	}
	ch->poll_timeout = 0;	/* done polling */
	spin_unlock_irqrestore(ch->lock, flags);

	return ide_stopped;
}

/*
 * Attempt to recover a confused drive by resetting it.  Unfortunately,
 * resetting a disk drive actually resets all devices on the same interface, so
 * it can really be thought of as resetting the interface rather than resetting
 * the drive.
 *
 * ATAPI devices have their own reset mechanism which allows them to be
 * individually reset without clobbering other devices on the same interface.
 *
 * Unfortunately, the IDE interface does not generate an interrupt to let us
 * know when the reset operation has finished, so we must poll for this.
 * Equally poor, though, is the fact that this may a very long time to
 * complete, (up to 30 seconds worst case).  So, instead of busy-waiting here
 * for it, we set a timer to poll at 50ms intervals.
 *
 * Channel lock should be held.
 */

static ide_startstop_t do_reset1(struct ata_device *drive, int try_atapi)
{
	unsigned int unit;
	unsigned long flags;
	struct ata_channel *ch = drive->channel;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	/* For an ATAPI device, first try an ATAPI SRST. */
	if (try_atapi) {
		if (drive->type != ATA_DISK) {
			check_crc_errors(drive);
			ata_select(drive, 20);
			OUT_BYTE(WIN_SRST, IDE_COMMAND_REG);
			ch->poll_timeout = jiffies + WAIT_WORSTCASE;
			ata_set_handler(drive, atapi_reset_pollfunc, HZ/20, NULL);
			__restore_flags(flags);	/* local CPU only */

			return ide_started;
		}
	}

	/*
	 * First, reset any device state data we were maintaining
	 * for any of the drives on this interface.
	 */
	for (unit = 0; unit < MAX_DRIVES; ++unit)
		check_crc_errors(&ch->drives[unit]);

	__restore_flags(flags);	/* local CPU only */

	return ide_started;
}

static inline u32 read_24(struct ata_device *drive)
{
	return  (IN_BYTE(IDE_HCYL_REG) << 16) |
		(IN_BYTE(IDE_LCYL_REG) << 8) |
		 IN_BYTE(IDE_SECTOR_REG);
}

#if FANCY_STATUS_DUMPS
struct ata_bit_messages {
	u8 mask;
	u8 match;
	const char *msg;
};

static struct ata_bit_messages ata_status_msgs[] = {
	{ BUSY_STAT,  BUSY_STAT,  "busy"            },
	{ READY_STAT, READY_STAT, "drive ready"     },
	{ WRERR_STAT, WRERR_STAT, "device fault"    },
	{ SEEK_STAT,  SEEK_STAT,  "seek complete"   },
	{ DRQ_STAT,   DRQ_STAT,   "data request"    },
	{ ECC_STAT,   ECC_STAT,   "corrected error" },
	{ INDEX_STAT, INDEX_STAT, "index"           },
	{ ERR_STAT,   ERR_STAT,   "error"           }
};

static struct ata_bit_messages ata_error_msgs[] = {
	{ ICRC_ERR|ABRT_ERR,	ABRT_ERR,		"drive status error"	},
	{ ICRC_ERR|ABRT_ERR,	ICRC_ERR,		"bad sectorr"		},
	{ ICRC_ERR|ABRT_ERR,	ICRC_ERR|ABRT_ERR,	"invalid checksum"	},
	{ ECC_ERR,		ECC_ERR,		"uncorrectable error"	},
	{ ID_ERR,		ID_ERR,			"sector id not found"   },
	{ TRK0_ERR,		TRK0_ERR,		"track zero not found"	},
	{ MARK_ERR,		MARK_ERR,		"addr mark not found"   }
};

static void dump_bits(struct ata_bit_messages *msgs, int nr, byte bits)
{
	int i;

	printk(" [ ");

	for (i = 0; i < nr; i++, msgs++)
		if ((bits & msgs->mask) == msgs->match)
			printk("%s ", msgs->msg);

	printk("] ");
}
#else
# define dump_bits(msgs,nr,bits) do { } while (0)
#endif

/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
u8 ata_dump(struct ata_device *drive, struct request * rq, const char *msg)
{
	unsigned long flags;
	u8 err = 0;

	__save_flags (flags);	/* local CPU only */
	ide__sti();		/* local CPU only */

	printk("%s: %s: status=0x%02x", drive->name, msg, drive->status);
	dump_bits(ata_status_msgs, ARRAY_SIZE(ata_status_msgs), drive->status);
	printk("\n");

	if ((drive->status & (BUSY_STAT|ERR_STAT)) == ERR_STAT) {
		err = GET_ERR();
		printk("%s: %s: error=0x%02x", drive->name, msg, err);
#if FANCY_STATUS_DUMPS
		if (drive->type == ATA_DISK) {
			dump_bits(ata_error_msgs, ARRAY_SIZE(ata_error_msgs), err);

			if ((err & (BBD_ERR | ABRT_ERR)) == BBD_ERR || (err & (ECC_ERR|ID_ERR|MARK_ERR))) {
				if ((drive->id->command_set_2 & 0x0400) &&
				    (drive->id->cfs_enable_2 & 0x0400) &&
				    (drive->addressing == 1)) {
					__u64 sectors = 0;
					u32 low = 0, high = 0;
					low = read_24(drive);
					OUT_BYTE(0x80, drive->channel->io_ports[IDE_CONTROL_OFFSET]);
					high = read_24(drive);

					sectors = ((__u64)high << 24) | low;
					printk(", LBAsect=%lld, high=%d, low=%d", (long long) sectors, high, low);
				} else {
					u8 cur = IN_BYTE(IDE_SELECT_REG);
					if (cur & 0x40) {	/* using LBA? */
						printk(", LBAsect=%ld", (unsigned long)
						 ((cur&0xf)<<24)
						 |(IN_BYTE(IDE_HCYL_REG)<<16)
						 |(IN_BYTE(IDE_LCYL_REG)<<8)
						 | IN_BYTE(IDE_SECTOR_REG));
					} else {
						printk(", CHS=%d/%d/%d",
						 (IN_BYTE(IDE_HCYL_REG)<<8) +
						  IN_BYTE(IDE_LCYL_REG),
						  cur & 0xf,
						  IN_BYTE(IDE_SECTOR_REG));
					}
				}
				if (rq)
					printk(", sector=%ld", rq->sector);
			}
		}
#endif
		printk("\n");
	}
	__restore_flags (flags);	/* local CPU only */
	return err;
}

/*
 * This gets invoked in response to a drive unexpectedly having its DRQ_STAT
 * bit set.  As an alternative to resetting the drive, it tries to clear the
 * condition by reading a sector's worth of data from the drive.  Of course,
 * this may not help if the drive is *waiting* for data from *us*.
 */
static void try_to_flush_leftover_data(struct ata_device *drive)
{
	int i;

	if (drive->type != ATA_DISK)
		return;

	for (i = (drive->mult_count ? drive->mult_count : 1); i > 0; --i) {
		u32 buffer[SECTOR_WORDS];

		ata_read(drive, buffer, SECTOR_WORDS);
	}
}

#ifdef CONFIG_BLK_DEV_PDC4030
# define IS_PDC4030_DRIVE (drive->channel->chipset == ide_pdc4030)
#else
# define IS_PDC4030_DRIVE (0)	/* auto-NULLs out pdc4030 code */
#endif

/*
 * We are still on the old request path here so issuing the recalibrate command
 * directly should just work.
 */
static int do_recalibrate(struct ata_device *drive)
{

	if (drive->type != ATA_DISK)
		return ide_stopped;

	if (!IS_PDC4030_DRIVE) {
		struct ata_taskfile args;

		printk(KERN_INFO "%s: recalibrating...\n", drive->name);
		memset(&args, 0, sizeof(args));
		args.taskfile.sector_count = drive->sect;
		args.cmd = WIN_RESTORE;
		ide_raw_taskfile(drive, &args);
		printk(KERN_INFO "%s: done!\n", drive->name);
	}

	return IS_PDC4030_DRIVE ? ide_stopped : ide_started;
}

/*
 * Take action based on the error returned by the drive.
 *
 * FIXME: Channel lock should be held.
 */
ide_startstop_t ata_error(struct ata_device *drive, struct request *rq,	const char *msg)
{
	u8 err;
	u8 stat = drive->status;

	err = ata_dump(drive, rq, msg);
	if (!drive || !rq)
		return ide_stopped;

	/* retry only "normal" I/O: */
	if (!(rq->flags & REQ_CMD)) {
		rq->errors = 1;

		return ide_stopped;
	}

	/* other bits are useless when BUSY */
	if (stat & BUSY_STAT || ((stat & WRERR_STAT) && !drive->nowerr))
		rq->errors |= ERROR_RESET; /* FIXME: What's that?! */
	else {
		if (drive->type == ATA_DISK && (stat & ERR_STAT)) {
			/* err has different meaning on cdrom and tape */
			if (err == ABRT_ERR) {
				if (drive->select.b.lba && IN_BYTE(IDE_COMMAND_REG) == WIN_SPECIFY)
					return ide_stopped; /* some newer drives don't support WIN_SPECIFY */
			} else if ((err & (ABRT_ERR | ICRC_ERR)) == (ABRT_ERR | ICRC_ERR))
				drive->crc_count++; /* UDMA crc error -- just retry the operation */
			else if (err & (BBD_ERR | ECC_ERR))	/* retries won't help these */
				rq->errors = ERROR_MAX;
			else if (err & TRK0_ERR)	/* help it find track zero */
				rq->errors |= ERROR_RECAL;
		}
		/* pre bio (rq->cmd != WRITE) */
		if ((stat & DRQ_STAT) && rq_data_dir(rq) == READ)
			try_to_flush_leftover_data(drive);
	}

	if (!ata_status(drive, 0, BUSY_STAT | DRQ_STAT))
		OUT_BYTE(WIN_IDLEIMMEDIATE, IDE_COMMAND_REG);	/* force an abort */

	if (rq->errors >= ERROR_MAX) {
		printk(KERN_ERR "%s: max number of retries exceeded!\n", drive->name);
		/* FIXME: make sure all end_request implementations are lock free */
		if (ata_ops(drive) && ata_ops(drive)->end_request)
			ata_ops(drive)->end_request(drive, rq, 0);
		else
			__ata_end_request(drive, rq, 0, 0);
	} else {
		++rq->errors;
		if ((rq->errors & ERROR_RESET) == ERROR_RESET)
			return do_reset1(drive, 1);
		if ((rq->errors & ERROR_RECAL) == ERROR_RECAL)
			return do_recalibrate(drive);
	}

	return ide_stopped;
}

/*
 * This initiates handling of a new I/O request.
 */
static ide_startstop_t start_request(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	sector_t block;
	unsigned int minor = minor(rq->rq_dev);
	unsigned int unit = minor >> PARTN_BITS;
	ide_startstop_t ret;

	BUG_ON(!(rq->flags & REQ_STARTED));

#ifdef DEBUG
	printk("%s: %s: current=0x%08lx\n", ch->name, __FUNCTION__, (unsigned long) rq);
#endif

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures))
		goto kill_rq;

	if (unit >= MAX_DRIVES) {
		printk(KERN_ERR "%s: bad device number: %s\n", ch->name, kdevname(rq->rq_dev));
		goto kill_rq;
	}

	block = rq->sector;

	/* Strange disk manager remap.
	 */
	if (rq->flags & REQ_CMD)
		if (drive->type == ATA_DISK || drive->type == ATA_FLOPPY)
			block += drive->sect0;

	/* Yecch - this will shift the entire interval, possibly killing some
	 * innocent following sector.
	 */
	if (block == 0 && drive->remap_0_to_1 == 1)
		block = 1;  /* redirect MBR access to EZ-Drive partn table */

	ata_select(drive, 0);
	spin_unlock_irq(ch->lock);
	if (ata_status_poll(drive, drive->ready_stat, BUSY_STAT | DRQ_STAT,
				WAIT_READY, rq, &ret)) {
		printk(KERN_WARNING "%s: drive not ready for command\n", drive->name);
		spin_lock_irq(ch->lock);

		return ret;
	}
	spin_lock_irq(ch->lock);

	/* This issues a special drive command.
	 *
	 * FIXME: move this down to idedisk_do_request().
	 */
	if (rq->flags & REQ_SPECIAL)
		if (drive->type == ATA_DISK) {
			spin_unlock_irq(ch->lock);
			ret = ata_taskfile(drive, rq->special, NULL);
			spin_lock_irq(ch->lock);

			return ret;
		}

	if (!ata_ops(drive)) {
		printk(KERN_WARNING "%s: device type %d not supported\n",
				drive->name, drive->type);
		goto kill_rq;
	}

	/* The normal way of execution is to pass and execute the request
	 * handler down to the device type driver.
	 */

	if (ata_ops(drive)->XXX_do_request) {
		ret = ata_ops(drive)->XXX_do_request(drive, rq, block);
	} else {
		__ata_end_request(drive, rq, 0, 0);
		ret = ide_stopped;
	}
	return ret;

kill_rq:
	if (ata_ops(drive)) {
		if (ata_ops(drive)->end_request) {
			spin_unlock_irq(ch->lock);
			ata_ops(drive)->end_request(drive, rq, 0);
			spin_lock_irq(ch->lock);
		}
	} else
		__ata_end_request(drive, rq, 0, 0);

	return ide_stopped;
}

ide_startstop_t restart_request(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(ch->lock, flags);

	ch->handler = NULL;
	del_timer(&ch->timer);
	ret = start_request(drive, drive->rq);
	spin_unlock_irqrestore(ch->lock, flags);

	return ret;
}

/*
 * This is used by a drive to give excess bandwidth back by sleeping for
 * timeout jiffies.
 */
void ide_stall_queue(struct ata_device *drive, unsigned long timeout)
{
	if (timeout > WAIT_WORSTCASE)
		timeout = WAIT_WORSTCASE;
	drive->sleep = timeout + jiffies;
}


/*
 * Determine the longest sleep time for the devices at this channel.
 */
static unsigned long longest_sleep(struct ata_channel *channel)
{
	unsigned long sleep = 0;
	int unit;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device *drive = &channel->drives[unit];

		if (!drive->present)
			continue;

		/* This device is sleeping and waiting to be serviced
		 * later than any other device we checked thus far.
		 */
		if (drive->sleep && (!sleep || time_after(sleep, drive->sleep)))
			sleep = drive->sleep;
	}

	return sleep;
}

/*
 * Select the next device which will be serviced.  This selects only between
 * devices on the same channel, since everything else will be scheduled on the
 * queue level.
 */
static struct ata_device *choose_urgent_device(struct ata_channel *channel)
{
	struct ata_device *choice = NULL;
	unsigned long sleep = 0;
	int unit;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device *drive = &channel->drives[unit];

		if (!drive->present)
			continue;

		/* There are no request pending for this device.
		 */
		if (list_empty(&drive->queue.queue_head))
			continue;

		/* This device still wants to remain idle.
		 */
		if (drive->sleep && time_after(drive->sleep, jiffies))
			continue;

		/* Take this device, if there is no device choosen thus far or
		 * it's more urgent.
		 */
		if (!choice || (drive->sleep && (!choice->sleep || time_after(choice->sleep, drive->sleep)))) {
			if (!blk_queue_plugged(&drive->queue))
				choice = drive;
		}
	}

	if (choice)
		return choice;

	sleep = longest_sleep(channel);

	if (sleep) {

		/*
		 * Take a short snooze, and then wake up again.  Just in case
		 * there are big differences in relative throughputs.. don't
		 * want to hog the cpu too much.
		 */

		if (time_after(jiffies, sleep - WAIT_MIN_SLEEP))
			sleep = jiffies + WAIT_MIN_SLEEP;
#if 1
		if (timer_pending(&channel->timer))
			printk(KERN_ERR "%s: timer already active\n", __FUNCTION__);
#endif
		set_bit(IDE_SLEEP, channel->active);
		mod_timer(&channel->timer, sleep);
		/* we purposely leave hwgroup busy while sleeping */
	} else {
		/* FIXME: use queue plugging instead of active to
		 * block upper layers from stomping on us */
		/* Ugly, but how can we sleep for the lock otherwise? */
		ide_release_lock(&ide_irq_lock);/* for atari only */
		clear_bit(IDE_BUSY, channel->active);
	}

	return NULL;
}


/*
 * Feed commands to a drive until it barfs.  Called with queue lock held and
 * busy channel.
 */
static void queue_commands(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	ide_startstop_t startstop = -1;

	for (;;) {
		struct request *rq = NULL;

		if (!test_bit(IDE_BUSY, ch->active))
			printk(KERN_ERR "%s: error: not busy while queueing!\n", drive->name);

		/* Abort early if we can't queue another command. for non
		 * tcq, ata_can_queue is always 1 since we never get here
		 * unless the drive is idle.
		 */
		if (!ata_can_queue(drive)) {
			if (!ata_pending_commands(drive))
				clear_bit(IDE_BUSY, ch->active);
			break;
		}

		drive->sleep = 0;

		if (test_bit(IDE_DMA, ch->active)) {
			printk(KERN_ERR "%s: error: DMA in progress...\n", drive->name);
			break;
		}

		/* There's a small window between where the queue could be
		 * replugged while we are in here when using tcq (in which
		 * case the queue is probably empty anyways...), so check
		 * and leave if appropriate. When not using tcq, this is
		 * still a severe BUG!
		 */
		if (blk_queue_plugged(&drive->queue)) {
			BUG_ON(!drive->using_tcq);
			break;
		}

		if (!(rq = elv_next_request(&drive->queue))) {
			if (!ata_pending_commands(drive))
				clear_bit(IDE_BUSY, ch->active);
			drive->rq = NULL;
			break;
		}

		/* If there are queued commands, we can't start a non-fs
		 * request (really, a non-queuable command) until the
		 * queue is empty.
		 */
		if (!(rq->flags & REQ_CMD) && ata_pending_commands(drive))
			break;

		drive->rq = rq;

		ide__sti();	/* allow other IRQs while we start this request */
		startstop = start_request(drive, rq);

		/* command started, we are busy */
		if (startstop == ide_started)
			break;

		/* start_request() can return either ide_stopped (no command
		 * was started), ide_started (command started, don't queue
		 * more), or ide_released (command started, try and queue
		 * more).
		 */
#if 0
		if (startstop == ide_stopped)
			set_bit(IDE_BUSY, &hwgroup->flags);
#endif

	}
}

/*
 * Issue a new request.
 * Caller must have already done spin_lock_irqsave(channel->lock, ...)
 */
static void do_request(struct ata_channel *channel)
{
	ide_get_lock(&ide_irq_lock, ata_irq_request, channel);/* for atari only: POSSIBLY BROKEN HERE(?) */
//	__cli();	/* necessary paranoia: ensure IRQs are masked on local CPU */

	while (!test_and_set_bit(IDE_BUSY, channel->active)) {
		struct ata_channel *ch;
		struct ata_device *drive;

		/* this will clear IDE_BUSY, if appropriate */
		drive = choose_urgent_device(channel);

		if (!drive)
			break;

		ch = drive->channel;

		/* Disable intrerrupts from the drive on the previous channel.
		 *
		 * FIXME: This should be only done if we are indeed sharing the same
		 * interrupt line with it.
		 *
		 * FIXME: check this! It appears to act on the current channel!
		 */
		if (ch != channel && channel->sharing_irq && ch->irq == channel->irq)
			ata_irq_enable(drive, 0);

		/* Remember the last drive we where acting on.
		 */
		ch->drive = drive;

		queue_commands(drive);
	}

}

void do_ide_request(request_queue_t *q)
{
	do_request(q->queuedata);
}

/*
 * This is our timeout function for all drive operations.  But note that it can
 * also be invoked as a result of a "sleep" operation triggered by the
 * mod_timer() call in do_request.
 *
 * FIXME: this should take a drive context instead of a channel.
 */
void ide_timer_expiry(unsigned long data)
{
	unsigned long flags;
	struct ata_channel *ch = (struct ata_channel *) data;

	spin_lock_irqsave(ch->lock, flags);
	del_timer(&ch->timer);

	if (!ch->drive) {
		printk(KERN_ERR "%s: IRQ handler was NULL\n", __FUNCTION__);
		ch->handler = NULL;
	} else if (!ch->handler) {

		/*
		 * Either a marginal timeout occurred (got the interrupt just
		 * as timer expired), or we were "sleeping" to give other
		 * devices a chance.  Either way, we don't really want to
		 * complain about anything.
		 */

		if (test_and_clear_bit(IDE_SLEEP, ch->active))
			clear_bit(IDE_BUSY, ch->active);
	} else {
		struct ata_device *drive = ch->drive;
		ide_startstop_t ret;
		ata_handler_t *handler;

		/* paranoia */
		if (!test_and_set_bit(IDE_BUSY, ch->active))
			printk(KERN_ERR "%s: %s: IRQ handler was not busy?!\n",
					drive->name, __FUNCTION__);

		if (ch->expiry) {
			unsigned long wait;

			/* continue */
			if ((wait = ch->expiry(drive, drive->rq)) != 0) {
				/* reengage timer */
				ch->timer.expires  = jiffies + wait;
				add_timer(&ch->timer);

				spin_unlock_irqrestore(ch->lock, flags);

				return;
			}
		}

		/*
		 * We need to simulate a real interrupt when invoking the
		 * handler() function, which means we need to globally mask the
		 * specific IRQ:
		 */

		handler = ch->handler;
		ch->handler = NULL;
		spin_unlock(ch->lock);

		ch = drive->channel;
#if DISABLE_IRQ_NOSYNC
		disable_irq_nosync(ch->irq);
#else
		disable_irq(ch->irq);	/* disable_irq_nosync ?? */
#endif
		__cli();	/* local CPU only, as if we were handling an interrupt */
		if (ch->poll_timeout) {
			ret = handler(drive, drive->rq);
		} else if (drive_is_ready(drive)) {
			if (drive->waiting_for_dma)
				udma_irq_lost(drive);
			(void) ide_ack_intr(ch);
			printk("%s: lost interrupt\n", drive->name);
			ret = handler(drive, drive->rq);
		} else if (drive->waiting_for_dma) {
			struct request *rq = drive->rq;

			/*
			 * Un-busy the hwgroup etc, and clear any pending DMA
			 * status. we want to retry the current request in PIO
			 * mode instead of risking tossing it all away.
			 */

			udma_stop(drive);
			udma_timeout(drive);

			/* Disable dma for now, but remember that we did so
			 * because of a timeout -- we'll reenable after we
			 * finish this next request (or rather the first chunk
			 * of it) in pio.
			 */

			drive->retry_pio++;
			drive->state = DMA_PIO_RETRY;
			udma_enable(drive, 0, 0);

			/* Un-busy drive etc (hwgroup->busy is cleared on
			 * return) and make sure request is sane.
			 */

			drive->rq = NULL;

			rq->errors = 0;
			if (rq->bio) {
				rq->sector = rq->bio->bi_sector;
				rq->current_nr_sectors = bio_iovec(rq->bio)->bv_len >> 9;
				rq->buffer = NULL;
			}
			ret = ide_stopped;
		} else
			ret = ata_error(drive, drive->rq, "irq timeout");

		enable_irq(ch->irq);

		spin_lock_irq(ch->lock);

		if (ret == ide_stopped)
			clear_bit(IDE_BUSY, ch->active);


		/* Reenter the request handling engine */
		do_request(ch);
	}
	spin_unlock_irqrestore(ch->lock, flags);
}

/*
 * There's nothing really useful we can do with an unexpected interrupt,
 * other than reading the status register (to clear it), and logging it.
 * There should be no way that an irq can happen before we're ready for it,
 * so we needn't worry much about losing an "important" interrupt here.
 *
 * On laptops (and "green" PCs), an unexpected interrupt occurs whenever the
 * drive enters "idle", "standby", or "sleep" mode, so if the status looks
 * "good", we just ignore the interrupt completely.
 *
 * This routine assumes __cli() is in effect when called.
 *
 * If an unexpected interrupt happens on irq15 while we are handling irq14
 * and if the two interfaces are "serialized" (CMD640), then it looks like
 * we could screw up by interfering with a new request being set up for irq15.
 *
 * In reality, this is a non-issue.  The new command is not sent unless the
 * drive is ready to accept one, in which case we know the drive is not
 * trying to interrupt us.  And ata_set_handler() is always invoked before
 * completing the issuance of any new drive command, so we will not be
 * accidentally invoked as a result of any valid command completion interrupt.
 *
 */
static void unexpected_irq(int irq)
{
	int i;

	for (i = 0; i < MAX_HWIFS; ++i) {
		struct ata_channel *ch = &ide_hwifs[i];
		struct ata_device *drive;

		if (!ch->present)
			continue;

		if (ch->irq != irq)
			continue;

		/* FIXME: this is a bit weak */
		drive = &ch->drives[0];

		if (!ata_status(drive, READY_STAT, BAD_STAT)) {
			/* Try to not flood the console with msgs */
			static unsigned long last_msgtime;
			static int count;

			++count;
			if (time_after(jiffies, last_msgtime + HZ)) {
				last_msgtime = jiffies;
				printk("%s: unexpected interrupt, status=0x%02x, count=%d\n",
						ch->name, drive->status, count);
			}
		}
	}
}

/*
 * Entry point for all interrupts, caller does __cli() for us.
 */
void ata_irq_request(int irq, void *data, struct pt_regs *regs)
{
	struct ata_channel *ch = data;
	unsigned long flags;
	struct ata_device *drive;
	ata_handler_t *handler = ch->handler;
	ide_startstop_t startstop;

	spin_lock_irqsave(ch->lock, flags);

	if (!ide_ack_intr(ch))
		goto out_lock;

	if (handler == NULL || ch->poll_timeout != 0) {
#if 0
		printk(KERN_INFO "ide: unexpected interrupt %d %d\n", ch->unit, irq);
#endif
		/*
		 * Not expecting an interrupt from this drive.
		 * That means this could be:
		 *	(1) an interrupt from another PCI device
		 *	sharing the same PCI INT# as us.
		 * or	(2) a drive just entered sleep or standby mode,
		 *	and is interrupting to let us know.
		 * or	(3) a spurious interrupt of unknown origin.
		 *
		 * For PCI, we cannot tell the difference,
		 * so in that case we just ignore it and hope it goes away.
		 */
#ifdef CONFIG_PCI
		if (ch->pci_dev && !ch->pci_dev->vendor)
#endif
		{
			/* Probably not a shared PCI interrupt, so we can
			 * safely try to do something about it:
			 */
			unexpected_irq(irq);
#ifdef CONFIG_PCI
		} else {
			/*
			 * Whack the status register, just in case we have a leftover pending IRQ.
			 */
			IN_BYTE(ch->io_ports[IDE_STATUS_OFFSET]);
#endif
		}
		goto out_lock;
	}
	drive = ch->drive;
	if (!drive_is_ready(drive)) {
		/*
		 * This happens regularly when we share a PCI IRQ with another device.
		 * Unfortunately, it can also happen with some buggy drives that trigger
		 * the IRQ before their status register is up to date.  Hopefully we have
		 * enough advance overhead that the latter isn't a problem.
		 */
		goto out_lock;
	}
	/* paranoia */
	if (!test_and_set_bit(IDE_BUSY, ch->active))
		printk(KERN_ERR "%s: %s: hwgroup was not busy!?\n", drive->name, __FUNCTION__);
	ch->handler = NULL;
	del_timer(&ch->timer);

	spin_unlock(ch->lock);

	if (ch->unmask)
		ide__sti();	/* local CPU only */

	/* service this interrupt, may set handler for next interrupt */
	startstop = handler(drive, drive->rq);
	spin_lock_irq(ch->lock);

	/*
	 * Note that handler() may have set things up for another
	 * interrupt to occur soon, but it cannot happen until
	 * we exit from this routine, because it will be the
	 * same irq as is currently being serviced here, and Linux
	 * won't allow another of the same (on any CPU) until we return.
	 */
	if (startstop == ide_stopped) {
		if (!ch->handler) {	/* paranoia */
			clear_bit(IDE_BUSY, ch->active);
			do_request(ch);
		} else {
			printk("%s: %s: huh? expected NULL handler on exit\n", drive->name, __FUNCTION__);
		}
	} else if (startstop == ide_released)
		queue_commands(drive);

out_lock:
	spin_unlock_irqrestore(ch->lock, flags);
}

static int ide_open(struct inode * inode, struct file * filp)
{
	struct ata_device *drive;

	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENXIO;

	/* Request a particular device type module.
	 *
	 * FIXME: The function which should rather requests the drivers is
	 * ide_driver_module(), since it seems illogical and even a bit
	 * dangerous to postpone this until open time!
	 */

#ifdef CONFIG_KMOD
	if (drive->driver == NULL) {
		char *module = NULL;

		switch (drive->type) {
			case ATA_DISK:
				module = "ide-disk";
				break;
			case ATA_ROM:
				module = "ide-cd";
				break;
			case ATA_TAPE:
				module = "ide-tape";
				break;
			case ATA_FLOPPY:
				module = "ide-floppy";
				break;
			case ATA_SCSI:
				module = "ide-scsi";
				break;
			default:
				/* nothing we can do about it */ ;
		}
		if (module)
			request_module(module);
	}
#endif

	if (drive->driver == NULL)
		ide_driver_module();

	while (drive->busy)
		sleep_on(&drive->wqueue);

	++drive->usage;
	if (ata_ops(drive) && ata_ops(drive)->open)
		return ata_ops(drive)->open(inode, filp, drive);
	else {
		--drive->usage;
		return -ENODEV;
	}

	printk(KERN_INFO "%s: driver not present\n", drive->name);
	--drive->usage;

	return -ENXIO;
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */
static int ide_release(struct inode * inode, struct file * file)
{
	struct ata_device *drive;

	if (!(drive = get_info_ptr(inode->i_rdev)))
		return 0;

	drive->usage--;
	if (ata_ops(drive) && ata_ops(drive)->release)
		ata_ops(drive)->release(inode, file, drive);
	return 0;
}

int ide_spin_wait_hwgroup(struct ata_device *drive)
{
	/* FIXME: Wait on a proper timer. Instead of playing games on the
	 * spin_lock().
	 */

	unsigned long timeout = jiffies + (10 * HZ);

	spin_lock_irq(drive->channel->lock);

	while (test_bit(IDE_BUSY, drive->channel->active)) {

		spin_unlock_irq(drive->channel->lock);

		if (time_after(jiffies, timeout)) {
			printk("%s: channel busy\n", drive->name);
			return -EBUSY;
		}

		spin_lock_irq(drive->channel->lock);
	}

	return 0;
}

static int ide_check_media_change(kdev_t i_rdev)
{
	struct ata_device *drive;
	int res = 0; /* not changed */

	drive = get_info_ptr(i_rdev);
	if (!drive)
		return -ENODEV;

	if (ata_ops(drive)) {
		ata_get(ata_ops(drive));
		if (ata_ops(drive)->check_media_change)
			res = ata_ops(drive)->check_media_change(drive);
		else
			res = 1; /* assume it was changed */
		ata_put(ata_ops(drive));
	}

	return res;
}

struct block_device_operations ide_fops[] = {{
	owner:			THIS_MODULE,
	open:			ide_open,
	release:		ide_release,
	ioctl:			ata_ioctl,
	check_media_change:	ide_check_media_change,
	revalidate:		ata_revalidate
}};

EXPORT_SYMBOL(ide_fops);
EXPORT_SYMBOL(ide_spin_wait_hwgroup);

EXPORT_SYMBOL(drive_is_flashcard);
EXPORT_SYMBOL(ide_timer_expiry);
EXPORT_SYMBOL(do_ide_request);

EXPORT_SYMBOL(ata_set_handler);
EXPORT_SYMBOL(ata_dump);
EXPORT_SYMBOL(ata_error);

/* FIXME: this is a trully bad name */
EXPORT_SYMBOL(restart_request);
EXPORT_SYMBOL(ata_end_request);
EXPORT_SYMBOL(__ata_end_request);
EXPORT_SYMBOL(ide_stall_queue);

EXPORT_SYMBOL(ide_setup_ports);
