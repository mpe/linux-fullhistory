/*  -*- linux-c -*-
 *  linux/drivers/block/promise.c	Version 0.07  Mar 26, 1996
 *
 *  Copyright (C) 1995-1996  Linus Torvalds & authors (see below)
 */

/*
 *  Principal Author/Maintainer:  peterd@pnd-pc.demon.co.uk
 *
 *  This file provides support for the second port and cache of Promise
 *  IDE interfaces, e.g. DC4030, DC5030.
 *
 *  Thanks are due to Mark Lord for advice and patiently answering stupid
 *  questions, and all those mugs^H^H^H^Hbrave souls who've tested this.
 *
 *  Version 0.01	Initial version, #include'd in ide.c rather than
 *                      compiled separately.
 *                      Reads use Promise commands, writes as before. Drives
 *                      on second channel are read-only.
 *  Version 0.02        Writes working on second channel, reads on both
 *                      channels. Writes fail under high load. Suspect
 *			transfers of >127 sectors don't work.
 *  Version 0.03        Brought into line with ide.c version 5.27.
 *                      Other minor changes.
 *  Version 0.04        Updated for ide.c version 5.30
 *                      Changed initialization strategy
 *  Version 0.05	Kernel integration.  -ml
 *  Version 0.06	Ooops. Add hwgroup to direct call of ide_intr() -ml
 *  Version 0.07	Added support for DC4030 variants
 *			Secondary interface autodetection
 */

/*
 * Once you've compiled it in, you'll have to also enable the interface
 * setup routine from the kernel command line, as in 
 *
 *	'linux ide0=dc4030'
 *
 * As before, it seems that somewhere around 3Megs when writing, bad things
 * start to happen [timeouts/retries -ml]. If anyone can give me more feedback,
 * I'd really appreciate it.  [email: peterd@pnd-pc.demon.co.uk]
 *
 */


#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <asm/io.h>
#include <asm/irq.h>
#include "ide.h"
#include "promise.h"

/* This is needed as the controller may not interrupt if the required data is
available in the cache. We have to simulate an interrupt. Ugh! */

extern void ide_intr(int, void *dev_id, struct pt_regs*);

/*
 * promise_selectproc() is invoked by ide.c
 * in preparation for access to the specified drive.
 */
static void promise_selectproc (ide_drive_t *drive)
{
	unsigned int number;

	OUT_BYTE(drive->select.all,IDE_SELECT_REG);
	udelay(1);	/* paranoia */
	number = ((HWIF(drive)->is_promise2)<<1) + drive->select.b.unit;
	OUT_BYTE(number,IDE_FEATURE_REG);
}

/*
 * promise_cmd handles the set of vendor specific commands that are initiated
 * by command F0. They all have the same success/failure notification.
 */
int promise_cmd(ide_drive_t *drive, byte cmd)
{
	unsigned long timeout, timer;
	byte status_val;

	promise_selectproc(drive);	/* redundant? */
	OUT_BYTE(0xF3,IDE_SECTOR_REG);
	OUT_BYTE(cmd,IDE_SELECT_REG);
	OUT_BYTE(PROMISE_EXTENDED_COMMAND,IDE_COMMAND_REG);
	timeout = HZ * 10;
	timeout += jiffies;
	do {
		if(jiffies > timeout) {
			return 2; /* device timed out */
		}
		/* This is out of delay_10ms() */
		/* Delays at least 10ms to give interface a chance */
		timer = jiffies + (HZ + 99)/100 + 1;
		while (timer > jiffies);
		status_val = IN_BYTE(IDE_SECTOR_REG);
	} while (status_val != 0x50 && status_val != 0x70);

	if(status_val == 0x50)
		return 0; /* device returned success */
	else
		return 1; /* device returned failure */
}

ide_hwif_t *hwif_required = NULL;

void setup_dc4030 (ide_hwif_t *hwif)
{
    hwif_required = hwif;
}

/*
init_dc4030: Test for presence of a Promise caching controller card.
Returns: 0 if no Promise card present at this io_base
	 1 if Promise card found
*/
int init_dc4030 (void)
{
	ide_hwif_t *hwif = hwif_required;
        ide_drive_t *drive;
	ide_hwif_t *second_hwif;
	struct dc_ident ident;
	int i;
	
	if (!hwif) return 0;

	drive = &hwif->drives[0];
	second_hwif = &ide_hwifs[hwif->index+1];
	if(hwif->is_promise2) /* we've already been found ! */
	    return 1;

	if(IN_BYTE(IDE_NSECTOR_REG) == 0xFF || IN_BYTE(IDE_SECTOR_REG) == 0xFF)
	{
	    return 0;
	}
	OUT_BYTE(0x08,IDE_CONTROL_REG);
	if(promise_cmd(drive,PROMISE_GET_CONFIG)) {
	    return 0;
	}
	if(ide_wait_stat(drive,DATA_READY,BAD_W_STAT,WAIT_DRQ)) {
	    printk("%s: Failed Promise read config!\n",hwif->name);
	    return 0;
	}
	ide_input_data(drive,&ident,SECTOR_WORDS);
	if(ident.id[1] != 'P' || ident.id[0] != 'T') {
            return 0;
	}
	printk("%s: Promise caching controller, ",hwif->name);
	switch(ident.type) {
            case 0x43:	printk("DC4030VL-2, "); break;
            case 0x41:	printk("DC4030VL-1, "); break;
	    case 0x40:	printk("DC4030VL, "); break;
            default:	printk("unknown - type 0x%02x - please report!\n"
			       ,ident.type);
			return 0;
	}
	printk("%dKB cache, ",(int)ident.cache_mem);
	switch(ident.irq) {
            case 0x00: hwif->irq = 14; break;
            case 0x01: hwif->irq = 12; break;
            default:   hwif->irq = 15; break;
	}
	printk("on IRQ %d\n",hwif->irq);
	hwif->chipset    = second_hwif->chipset    = ide_promise;
	hwif->selectproc = second_hwif->selectproc = &promise_selectproc;
/* Shift the remaining interfaces down by one */
	for (i=MAX_HWIFS-1 ; i > hwif->index+1 ; i--) {
		printk("Shifting i/f %d values to i/f %d\n",i-1,i);
		ide_hwifs[i].io_base = ide_hwifs[i-1].io_base;
		ide_hwifs[i].ctl_port = ide_hwifs[i-1].ctl_port;
		ide_hwifs[i].noprobe = ide_hwifs[i-1].noprobe;
	}
	second_hwif->is_promise2 = 1;
	second_hwif->io_base = hwif->io_base;
	second_hwif->ctl_port = hwif->ctl_port;	
	second_hwif->irq = hwif->irq;
	for (i=0; i<2 ; i++) {
            hwif->drives[i].io_32bit = 3;
	    second_hwif->drives[i].io_32bit = 3;
	    if(!ident.current_tm[i+2].cyl) second_hwif->drives[i].noprobe=1;
	}
        return 1;
}

/*
 * promise_read_intr() is the handler for disk read/multread interrupts
 */
static void promise_read_intr (ide_drive_t *drive)
{
	byte stat;
	int i;
	unsigned int sectors_left, sectors_avail, nsect;
	struct request *rq;

	if (!OK_STAT(stat=GET_STAT(),DATA_READY,BAD_R_STAT)) {
		ide_error(drive, "promise_read_intr", stat);
		return;
	}

read_again:
	do {
	    sectors_left = IN_BYTE(IDE_NSECTOR_REG);
	    IN_BYTE(IDE_SECTOR_REG);
	} while (IN_BYTE(IDE_NSECTOR_REG) != sectors_left);
	rq = HWGROUP(drive)->rq;
	sectors_avail = rq->nr_sectors - sectors_left;

read_next:
	rq = HWGROUP(drive)->rq;
	if ((nsect = rq->current_nr_sectors) > sectors_avail)
		nsect = sectors_avail;
	sectors_avail -= nsect;
	ide_input_data(drive, rq->buffer, nsect * SECTOR_WORDS);
#ifdef DEBUG
	printk("%s:  promise_read: sectors(%ld-%ld), buffer=0x%08lx, "
	       "remaining=%ld\n", drive->name, rq->sector, rq->sector+nsect-1, 
	       (unsigned long) rq->buffer+(nsect<<9), rq->nr_sectors-nsect);
#endif
	rq->sector += nsect;
	rq->buffer += nsect<<9;
	rq->errors = 0;
	i = (rq->nr_sectors -= nsect);
	if ((rq->current_nr_sectors -= nsect) <= 0)
		ide_end_request(1, HWGROUP(drive));
	if (i > 0) {
		if (sectors_avail)
		    goto read_next;
		stat = GET_STAT();
		if(stat & DRQ_STAT)
		    goto read_again;
		if(stat & BUSY_STAT) {
		    ide_set_handler (drive, &promise_read_intr, WAIT_CMD);
		    return;
		}
		printk("Ah! promise read intr: sectors left !DRQ !BUSY\n");
		ide_error(drive, "promise read intr", stat);
	}
}

/*
 * promise_write_pollfunc() is the handler for disk write completion polling.
 */
static void promise_write_pollfunc (ide_drive_t *drive)
{
	int i;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	struct request *rq;

        if (IN_BYTE(IDE_NSECTOR_REG) != 0) {
            if (jiffies < hwgroup->poll_timeout) {
                ide_set_handler (drive, &promise_write_pollfunc, 1);
                return; /* continue polling... */
            }
            printk("%s: write timed-out!\n",drive->name);
            ide_error (drive, "write timeout", GET_STAT());
            return;
        }
        
	ide_multwrite(drive, 4);
        rq = hwgroup->rq;
        for (i = rq->nr_sectors; i > 0;) {
            i -= rq->current_nr_sectors;
            ide_end_request(1, hwgroup);
        }
        return;
}

/*
 * promise_write() transfers a block of one or more sectors of data to a
 * drive as part of a disk write operation. All but 4 sectors are transfered
 * in the first attempt, then the interface is polled (nicely!) for completion
 * before the final 4 sectors are transfered. Don't ask me why, but this is
 * how it's done in the drivers for other O/Ses. There is no interrupt
 * generated on writes, which is why we have to do it like this.
 */
static void promise_write (ide_drive_t *drive)
{
    ide_hwgroup_t *hwgroup = HWGROUP(drive);
    struct request *rq = &hwgroup->wrq;
    int i;

    if (rq->nr_sectors > 4) {
        ide_multwrite(drive, rq->nr_sectors - 4);
        hwgroup->poll_timeout = jiffies + WAIT_WORSTCASE;
        ide_set_handler (drive, &promise_write_pollfunc, 1);
        return;
    } else {
        ide_multwrite(drive, rq->nr_sectors);
        rq = hwgroup->rq;
        for (i = rq->nr_sectors; i > 0;) {
            i -= rq->current_nr_sectors;
            ide_end_request(1, hwgroup);
        }
    }
}

/*
 * do_promise_io() is called from do_rw_disk, having had the block number
 * already set up. It issues a READ or WRITE command to the Promise
 * controller, assuming LBA has been used to set up the block number.
 */
void do_promise_io (ide_drive_t *drive, struct request *rq)
{
	unsigned long timeout;
	unsigned short io_base = HWIF(drive)->io_base;
	byte stat;

	if (rq->cmd == READ) {
	    ide_set_handler(drive, &promise_read_intr, WAIT_CMD);
	    OUT_BYTE(PROMISE_READ, io_base+IDE_COMMAND_OFFSET);
/* The card's behaviour is odd at this point. If the data is
   available, DRQ will be true, and no interrupt will be
   generated by the card. If this is the case, we need to simulate
   an interrupt. Ugh! Otherwise, if an interrupt will occur, bit0
   of the SELECT register will be high, so we can just return and
   be interrupted.*/
	    timeout = jiffies + HZ/20; /* 50ms wait */
	    do {
		stat=GET_STAT();
		if(stat & DRQ_STAT) {
/*                    unsigned long flags;
                    save_flags(flags);
                    cli();
                    disable_irq(HWIF(drive)->irq);
*/
		    ide_intr(HWIF(drive)->irq,HWGROUP(drive),NULL);
/*                    enable_irq(HWIF(drive)->irq);
                    restore_flags(flags);
*/
		    return;
		}
		if(IN_BYTE(io_base+IDE_SELECT_OFFSET) & 0x01)
		    return;
		udelay(1);
	    } while (jiffies < timeout);
	    printk("%s: reading: No DRQ and not waiting - Odd!\n",
		   drive->name);
	    return;
	}
	if (rq->cmd == WRITE) {
	    OUT_BYTE(PROMISE_WRITE, io_base+IDE_COMMAND_OFFSET);
	    if (ide_wait_stat(drive, DATA_READY, drive->bad_wstat, WAIT_DRQ)) {
		printk("%s: no DRQ after issuing PROMISE_WRITE\n", drive->name);
		return;
	    }
	    if (!drive->unmask)
		cli();
	    HWGROUP(drive)->wrq = *rq; /* scratchpad */
	    promise_write(drive);
	    return;
	}
	printk("%s: bad command: %d\n", drive->name, rq->cmd);
	ide_end_request(0, HWGROUP(drive));
}
