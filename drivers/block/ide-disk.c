/*
 *  linux/drivers/block/ide-disk.c	Version 1.01  Nov  25, 1996
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors (see below)
 */

/*
 *  Maintained by Mark Lord  <mlord@pobox.com>
 *            and Gadi Oxman <gadio@netvision.net.il>
 *
 * This is the IDE/ATA disk driver, as evolved from hd.c and ide.c.
 *
 *  From hd.c:
 *  |
 *  | It traverses the request-list, using interrupts to jump between functions.
 *  | As nearly all functions can be called within interrupts, we may not sleep.
 *  | Special care is recommended.  Have Fun!
 *  |
 *  | modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *  |
 *  | Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  | in the early extended-partition checks and added DM partitions.
 *  |
 *  | Early work on error handling by Mika Liljeberg (liljeber@cs.Helsinki.FI).
 *  |
 *  | IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  | and general streamlining by Mark Lord (mlord@pobox.com).
 *
 *  October, 1994 -- Complete line-by-line overhaul for linux 1.1.x, by:
 *
 *	Mark Lord	(mlord@pobox.com)		(IDE Perf.Pkg)
 *	Delman Lee	(delman@mipg.upenn.edu)		("Mr. atdisk2")
 *	Scott Snyder	(snyder@fnald0.fnal.gov)	(ATAPI IDE cd-rom)
 *
 *  This was a rewrite of just about everything from hd.c, though some original
 *  code is still sprinkled about.  Think of it as a major evolution, with
 *  inspiration from lots of linux users, esp.  hamish@zot.apana.org.au
 *
 * Version 1.00		move disk only code from ide.c to ide-disk.c
 *			support optional byte-swapping of all data
 * Version 1.01		fix previous byte-swapping code
 */

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/malloc.h>
#include <linux/delay.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "ide.h"

static void idedisk_bswap_data (void *buffer, int wcount)
{
	u16 *p = buffer;

	while (wcount--) {
		*p++ = *p << 8 | *p >> 8;
		*p++ = *p << 8 | *p >> 8;
	}
}

static inline void idedisk_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	ide_input_data(drive, buffer, wcount);
	if (drive->bswap)
		idedisk_bswap_data(buffer, wcount);
}

static inline void idedisk_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	if (drive->bswap) {
		idedisk_bswap_data(buffer, wcount);
		ide_output_data(drive, buffer, wcount);
		idedisk_bswap_data(buffer, wcount);
	} else
		ide_output_data(drive, buffer, wcount);
}

/*
 * lba_capacity_is_ok() performs a sanity check on the claimed "lba_capacity"
 * value for this drive (from its reported identification information).
 *
 * Returns:	1 if lba_capacity looks sensible
 *		0 otherwise
 */
static int lba_capacity_is_ok (struct hd_driveid *id)
{
	unsigned long lba_sects   = id->lba_capacity;
	unsigned long chs_sects   = id->cyls * id->heads * id->sectors;
	unsigned long _10_percent = chs_sects / 10;

	/* perform a rough sanity check on lba_sects:  within 10% is "okay" */
	if ((lba_sects - chs_sects) < _10_percent)
		return 1;	/* lba_capacity is good */

	/* some drives have the word order reversed */
	lba_sects = (lba_sects << 16) | (lba_sects >> 16);
	if ((lba_sects - chs_sects) < _10_percent) {
		id->lba_capacity = lba_sects;	/* fix it */
		return 1;	/* lba_capacity is (now) good */
	}
	return 0;	/* lba_capacity value is bad */
}

/*
 * read_intr() is the handler for disk read/multread interrupts
 */
static void read_intr (ide_drive_t *drive)
{
	byte stat;
	int i;
	unsigned int msect, nsect;
	struct request *rq;

	if (!OK_STAT(stat=GET_STAT(),DATA_READY,BAD_R_STAT)) {
		ide_error(drive, "read_intr", stat);
		return;
	}
	msect = drive->mult_count;
read_next:
	rq = HWGROUP(drive)->rq;
	if (msect) {
		if ((nsect = rq->current_nr_sectors) > msect)
			nsect = msect;
		msect -= nsect;
	} else
		nsect = 1;
	idedisk_input_data(drive, rq->buffer, nsect * SECTOR_WORDS);
#ifdef DEBUG
	printk("%s:  read: sectors(%ld-%ld), buffer=0x%08lx, remaining=%ld\n",
		drive->name, rq->sector, rq->sector+nsect-1,
		(unsigned long) rq->buffer+(nsect<<9), rq->nr_sectors-nsect);
#endif
	rq->sector += nsect;
	rq->buffer += nsect<<9;
	rq->errors = 0;
	i = (rq->nr_sectors -= nsect);
	if ((rq->current_nr_sectors -= nsect) <= 0)
		ide_end_request(1, HWGROUP(drive));
	if (i > 0) {
		if (msect)
			goto read_next;
		ide_set_handler (drive, &read_intr, WAIT_CMD);
	}
}

/*
 * write_intr() is the handler for disk write interrupts
 */
static void write_intr (ide_drive_t *drive)
{
	byte stat;
	int i;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	struct request *rq = hwgroup->rq;

	if (OK_STAT(stat=GET_STAT(),DRIVE_READY,drive->bad_wstat)) {
#ifdef DEBUG
		printk("%s: write: sector %ld, buffer=0x%08lx, remaining=%ld\n",
			drive->name, rq->sector, (unsigned long) rq->buffer,
			rq->nr_sectors-1);
#endif
		if ((rq->nr_sectors == 1) ^ ((stat & DRQ_STAT) != 0)) {
			rq->sector++;
			rq->buffer += 512;
			rq->errors = 0;
			i = --rq->nr_sectors;
			--rq->current_nr_sectors;
			if (rq->current_nr_sectors <= 0)
				ide_end_request(1, hwgroup);
			if (i > 0) {
				idedisk_output_data (drive, rq->buffer, SECTOR_WORDS);
				ide_set_handler (drive, &write_intr, WAIT_CMD);
			}
			return;
		}
	}
	ide_error(drive, "write_intr", stat);
}

/*
 * ide_multwrite() transfers a block of up to mcount sectors of data
 * to a drive as part of a disk multiple-sector write operation.
 */
void ide_multwrite (ide_drive_t *drive, unsigned int mcount)
{
	struct request *rq = &HWGROUP(drive)->wrq;

	do {
		unsigned int nsect = rq->current_nr_sectors;
		if (nsect > mcount)
			nsect = mcount;
		mcount -= nsect;

		idedisk_output_data(drive, rq->buffer, nsect<<7);
#ifdef DEBUG
		printk("%s: multwrite: sector %ld, buffer=0x%08lx, count=%d, remaining=%ld\n",
			drive->name, rq->sector, (unsigned long) rq->buffer,
			nsect, rq->nr_sectors - nsect);
#endif
		if ((rq->nr_sectors -= nsect) <= 0)
			break;
		if ((rq->current_nr_sectors -= nsect) == 0) {
			if ((rq->bh = rq->bh->b_reqnext) != NULL) {
				rq->current_nr_sectors = rq->bh->b_size>>9;
				rq->buffer             = rq->bh->b_data;
			} else {
				panic("%s: buffer list corrupted\n", drive->name);
				break;
			}
		} else {
			rq->buffer += nsect << 9;
		}
	} while (mcount);
}

/*
 * multwrite_intr() is the handler for disk multwrite interrupts
 */
static void multwrite_intr (ide_drive_t *drive)
{
	byte stat;
	int i;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	struct request *rq = &hwgroup->wrq;

	if (OK_STAT(stat=GET_STAT(),DRIVE_READY,drive->bad_wstat)) {
		if (stat & DRQ_STAT) {
			if (rq->nr_sectors) {
				ide_multwrite(drive, drive->mult_count);
				ide_set_handler (drive, &multwrite_intr, WAIT_CMD);
				return;
			}
		} else {
			if (!rq->nr_sectors) {	/* all done? */
				rq = hwgroup->rq;
				for (i = rq->nr_sectors; i > 0;){
					i -= rq->current_nr_sectors;
					ide_end_request(1, hwgroup);
				}
				return;
			}
		}
	}
	ide_error(drive, "multwrite_intr", stat);
}

/*
 * set_multmode_intr() is invoked on completion of a WIN_SETMULT cmd.
 */
static void set_multmode_intr (ide_drive_t *drive)
{
	byte stat = GET_STAT();

	if (OK_STAT(stat,READY_STAT,BAD_STAT)) {
		drive->mult_count = drive->mult_req;
	} else {
		drive->mult_req = drive->mult_count = 0;
		drive->special.b.recalibrate = 1;
		(void) ide_dump_status(drive, "set_multmode", stat);
	}
}

/*
 * set_geometry_intr() is invoked on completion of a WIN_SPECIFY cmd.
 */
static void set_geometry_intr (ide_drive_t *drive)
{
	byte stat = GET_STAT();

	if (!OK_STAT(stat,READY_STAT,BAD_STAT))
		ide_error(drive, "set_geometry_intr", stat);
}

/*
 * recal_intr() is invoked on completion of a WIN_RESTORE (recalibrate) cmd.
 */
static void recal_intr (ide_drive_t *drive)
{
	byte stat = GET_STAT();

	if (!OK_STAT(stat,READY_STAT,BAD_STAT))
		ide_error(drive, "recal_intr", stat);
}

/*
 * do_rw_disk() issues READ and WRITE commands to a disk,
 * using LBA if supported, or CHS otherwise, to address sectors.
 * It also takes care of issuing special DRIVE_CMDs.
 */
static void do_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long block)
{
#ifdef CONFIG_BLK_DEV_PROMISE
	ide_hwif_t *hwif = HWIF(drive);
	int use_promise_io = 0;
#endif /* CONFIG_BLK_DEV_PROMISE */

	OUT_BYTE(drive->ctl,IDE_CONTROL_REG);
	OUT_BYTE(rq->nr_sectors,IDE_NSECTOR_REG);
#ifdef CONFIG_BLK_DEV_PROMISE
	if (IS_PROMISE_DRIVE) {
		if (hwif->is_promise2 || rq->cmd == READ) {
			use_promise_io = 1;
		}
	}
	if (drive->select.b.lba || use_promise_io) {
#else /* !CONFIG_BLK_DEV_PROMISE */
	if (drive->select.b.lba) {
#endif /* CONFIG_BLK_DEV_PROMISE */
#ifdef DEBUG
		printk("%s: %sing: LBAsect=%ld, sectors=%ld, buffer=0x%08lx\n",
			drive->name, (rq->cmd==READ)?"read":"writ",
			block, rq->nr_sectors, (unsigned long) rq->buffer);
#endif
		OUT_BYTE(block,IDE_SECTOR_REG);
		OUT_BYTE(block>>=8,IDE_LCYL_REG);
		OUT_BYTE(block>>=8,IDE_HCYL_REG);
		OUT_BYTE(((block>>8)&0x0f)|drive->select.all,IDE_SELECT_REG);
	} else {
		unsigned int sect,head,cyl,track;
		track = block / drive->sect;
		sect  = block % drive->sect + 1;
		OUT_BYTE(sect,IDE_SECTOR_REG);
		head  = track % drive->head;
		cyl   = track / drive->head;
		OUT_BYTE(cyl,IDE_LCYL_REG);
		OUT_BYTE(cyl>>8,IDE_HCYL_REG);
		OUT_BYTE(head|drive->select.all,IDE_SELECT_REG);
#ifdef DEBUG
		printk("%s: %sing: CHS=%d/%d/%d, sectors=%ld, buffer=0x%08lx\n",
			drive->name, (rq->cmd==READ)?"read":"writ", cyl,
			head, sect, rq->nr_sectors, (unsigned long) rq->buffer);
#endif
	}
#ifdef CONFIG_BLK_DEV_PROMISE
	if (use_promise_io) {
		do_promise_io (drive, rq);
		return;
	}
#endif /* CONFIG_BLK_DEV_PROMISE */
	if (rq->cmd == READ) {
#ifdef CONFIG_BLK_DEV_TRITON
		if (drive->using_dma && !(HWIF(drive)->dmaproc(ide_dma_read, drive)))
			return;
#endif /* CONFIG_BLK_DEV_TRITON */
		ide_set_handler(drive, &read_intr, WAIT_CMD);
		OUT_BYTE(drive->mult_count ? WIN_MULTREAD : WIN_READ, IDE_COMMAND_REG);
		return;
	}
	if (rq->cmd == WRITE) {
#ifdef CONFIG_BLK_DEV_TRITON
		if (drive->using_dma && !(HWIF(drive)->dmaproc(ide_dma_write, drive)))
			return;
#endif /* CONFIG_BLK_DEV_TRITON */
		OUT_BYTE(drive->mult_count ? WIN_MULTWRITE : WIN_WRITE, IDE_COMMAND_REG);
		if (ide_wait_stat(drive, DATA_READY, drive->bad_wstat, WAIT_DRQ)) {
			printk(KERN_ERR "%s: no DRQ after issuing %s\n", drive->name,
				drive->mult_count ? "MULTWRITE" : "WRITE");
			return;
		}
		if (!drive->unmask)
			cli();
		if (drive->mult_count) {
			HWGROUP(drive)->wrq = *rq; /* scratchpad */
			ide_set_handler (drive, &multwrite_intr, WAIT_CMD);
			ide_multwrite(drive, drive->mult_count);
		} else {
			ide_set_handler (drive, &write_intr, WAIT_CMD);
			idedisk_output_data(drive, rq->buffer, SECTOR_WORDS);
		}
		return;
	}
	printk(KERN_ERR "%s: bad command: %d\n", drive->name, rq->cmd);
	ide_end_request(0, HWGROUP(drive));
}

static int idedisk_open (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	MOD_INC_USE_COUNT;
	if (drive->removable && drive->usage == 1) {
		byte door_lock[] = {WIN_DOORLOCK,0,0,0};
		struct request rq;
		check_disk_change(inode->i_rdev);
		ide_init_drive_cmd (&rq);
		rq.buffer = door_lock;
		/*
		 * Ignore the return code from door_lock,
		 * since the open() has already succeeded,
		 * and the door_lock is irrelevant at this point.
		 */
		(void) ide_do_drive_cmd(drive, &rq, ide_wait);
	}
	return 0;
}

static void idedisk_release (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	if (drive->removable && !drive->usage) {
		byte door_unlock[] = {WIN_DOORUNLOCK,0,0,0};
		struct request rq;
		invalidate_buffers(inode->i_rdev);
		ide_init_drive_cmd (&rq);
		rq.buffer = door_unlock;
		(void) ide_do_drive_cmd(drive, &rq, ide_wait);
	}
	MOD_DEC_USE_COUNT;
}

static int idedisk_media_change (ide_drive_t *drive)
{
	return drive->removable;	/* if removable, always assume it was changed */
}

/*
 * current_capacity() returns the capacity (in sectors) of a drive
 * according to its current geometry/LBA settings.
 */
static unsigned long idedisk_capacity (ide_drive_t  *drive)
{
	struct hd_driveid *id = drive->id;
	unsigned long capacity = drive->cyl * drive->head * drive->sect;

	drive->select.b.lba = 0;
	/* Determine capacity, and use LBA if the drive properly supports it */
	if (id != NULL && (id->capability & 2) && lba_capacity_is_ok(id)) {
		if (id->lba_capacity >= capacity) {
			capacity = id->lba_capacity;
			drive->select.b.lba = 1;
		}
	}
	return (capacity - drive->sect0);
}

static void idedisk_special (ide_drive_t *drive)
{
	special_t *s = &drive->special;

	if (s->b.set_geometry) {
		s->b.set_geometry = 0;
		OUT_BYTE(drive->sect,IDE_SECTOR_REG);
		OUT_BYTE(drive->cyl,IDE_LCYL_REG);
		OUT_BYTE(drive->cyl>>8,IDE_HCYL_REG);
		OUT_BYTE(((drive->head-1)|drive->select.all)&0xBF,IDE_SELECT_REG);
		if (!IS_PROMISE_DRIVE)
			ide_cmd(drive, WIN_SPECIFY, drive->sect, &set_geometry_intr);
	} else if (s->b.recalibrate) {
		s->b.recalibrate = 0;
		if (!IS_PROMISE_DRIVE)
			ide_cmd(drive, WIN_RESTORE, drive->sect, &recal_intr);
	} else if (s->b.set_multmode) {
		s->b.set_multmode = 0;
		if (drive->id && drive->mult_req > drive->id->max_multsect)
			drive->mult_req = drive->id->max_multsect;
		if (!IS_PROMISE_DRIVE)
			ide_cmd(drive, WIN_SETMULT, drive->mult_req, &set_multmode_intr);
	} else if (s->all) {
		int special = s->all;
		s->all = 0;
		printk(KERN_ERR "%s: bad special flag: 0x%02x\n", drive->name, special);
	}
}

static void idedisk_pre_reset (ide_drive_t *drive)
{
	drive->special.all = 0;
	drive->special.b.set_geometry = 1;
	drive->special.b.recalibrate  = 1;
	if (OK_TO_RESET_CONTROLLER)
		drive->mult_count = 0;
	if (!drive->keep_settings)
		drive->mult_req = 0;
	if (drive->mult_req != drive->mult_count)
		drive->special.b.set_multmode = 1;
}

int idedisk_init (void);
static ide_module_t idedisk_module = {
	IDE_DRIVER_MODULE,
	idedisk_init,
	NULL
};

/*
 *	IDE subdriver functions, registered with ide.c
 */
static ide_driver_t idedisk_driver = {
	ide_disk,		/* media */
	0,			/* busy */
	1,			/* supports_dma */
	0,			/* supports_dsc_overlap */
	NULL,			/* cleanup */
	do_rw_disk,		/* do_request */
	NULL,			/* end_request */
	NULL,			/* ioctl */
	idedisk_open,		/* open */
	idedisk_release,	/* release */
	idedisk_media_change,	/* media_change */
	idedisk_pre_reset,	/* pre_reset */
	idedisk_capacity,	/* capacity */
	idedisk_special		/* special */
};

static int idedisk_cleanup (ide_drive_t *drive)
{
	return ide_unregister_subdriver(drive);
}

static int idedisk_identify_device (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	
	if (id == NULL)
		return 0;

	/* SunDisk drives: force one unit */
	if (id->model[0] == 'S' && id->model[1] == 'u' && (drive->select.all & (1<<4)))
		return 1;

	return 0;
}

static void idedisk_setup (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	unsigned long capacity, check;
	
	if (id == NULL)
		return;

	/* check for removable disks (eg. SYQUEST), ignore 'WD' drives */
	if (id->config & (1<<7)) {	/* removable disk ? */
		if (id->model[0] != 'W' || id->model[1] != 'D')
			drive->removable = 1;
	}

	/* SunDisk drives: treat as non-removable */
	if (id->model[0] == 'S' && id->model[1] == 'u')
		drive->removable = 0;

	/* Extract geometry if we did not already have one for the drive */
	if (!drive->cyl || !drive->head || !drive->sect) {
		drive->cyl     = drive->bios_cyl  = id->cyls;
		drive->head    = drive->bios_head = id->heads;
		drive->sect    = drive->bios_sect = id->sectors;
	}
	/* Handle logical geometry translation by the drive */
	if ((id->field_valid & 1) && id->cur_cyls && id->cur_heads
	 && (id->cur_heads <= 16) && id->cur_sectors)
	{
		/*
		 * Extract the physical drive geometry for our use.
		 * Note that we purposely do *not* update the bios info.
		 * This way, programs that use it (like fdisk) will
		 * still have the same logical view as the BIOS does,
		 * which keeps the partition table from being screwed.
		 *
		 * An exception to this is the cylinder count,
		 * which we reexamine later on to correct for 1024 limitations.
		 */
		drive->cyl  = id->cur_cyls;
		drive->head = id->cur_heads;
		drive->sect = id->cur_sectors;

		/* check for word-swapped "capacity" field in id information */
		capacity = drive->cyl * drive->head * drive->sect;
		check = (id->cur_capacity0 << 16) | id->cur_capacity1;
		if (check == capacity) {	/* was it swapped? */
			/* yes, bring it into little-endian order: */
			id->cur_capacity0 = (capacity >>  0) & 0xffff;
			id->cur_capacity1 = (capacity >> 16) & 0xffff;
		}
	}
	/* Use physical geometry if what we have still makes no sense */
	if ((!drive->head || drive->head > 16) && id->heads && id->heads <= 16) {
		drive->cyl  = id->cyls;
		drive->head = id->heads;
		drive->sect = id->sectors;
	}
	/* Correct the number of cyls if the bios value is too small */
	if (drive->sect == drive->bios_sect && drive->head == drive->bios_head) {
		if (drive->cyl > drive->bios_cyl)
			drive->bios_cyl = drive->cyl;
	}

	(void) idedisk_capacity (drive); /* initialize LBA selection */

	printk (KERN_INFO "%s: %.40s, %ldMB w/%dkB Cache, %sCHS=%d/%d/%d%s\n",
	 drive->name, id->model, idedisk_capacity(drive)/2048L, id->buf_size/2,
	 drive->select.b.lba ? "LBA, " : "",
	 drive->bios_cyl, drive->bios_head, drive->bios_sect,
	 drive->using_dma ? ", DMA" : "");

	drive->mult_count = 0;
	if (id->max_multsect) {
		drive->mult_req = INITIAL_MULT_COUNT;
		if (drive->mult_req > id->max_multsect)
			drive->mult_req = id->max_multsect;
		if (drive->mult_req || ((id->multsect_valid & 1) && id->multsect))
			drive->special.b.set_multmode = 1;
	}
}

int idedisk_init (void)
{
	ide_drive_t *drive;
	int failed = 0;
	
	MOD_INC_USE_COUNT;
	while ((drive = ide_scan_devices (ide_disk, NULL, failed++)) != NULL) {
		if (idedisk_identify_device (drive))
			continue;
		if (ide_register_subdriver (drive, &idedisk_driver, IDE_SUBDRIVER_VERSION)) {
			printk (KERN_ERR "ide-disk: %s: Failed to register the driver with ide.c\n", drive->name);
			continue;
		}
		idedisk_setup(drive);
		if ((!drive->head || drive->head > 16) && !drive->select.b.lba) {
			printk(KERN_ERR "%s: INVALID GEOMETRY: %d PHYSICAL HEADS?\n", drive->name, drive->head);
			(void) idedisk_cleanup(drive);
			continue;
		}
		failed--;
	}
	ide_register_module(&idedisk_module);
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE
int init_module (void)
{
	return idedisk_init();
}

void cleanup_module (void)
{
	ide_drive_t *drive;
	int failed = 0;

	while ((drive = ide_scan_devices (ide_disk, &idedisk_driver, failed)) != NULL)
		if (idedisk_cleanup (drive)) {
			printk (KERN_ERR "%s: cleanup_module() called while still busy\n", drive->name);
			failed++;
		}
	ide_unregister_module(&idedisk_module);
}
#endif /* MODULE */
