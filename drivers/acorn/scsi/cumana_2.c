/*
 * linux/arch/arm/drivers/scsi/cumana_2.c
 *
 * Copyright (C) 1997,1998 Russell King
 *
 * This driver is based on experimentation.  Hence, it may have made
 * assumptions about the particular card that I have available, and
 * may not be reliable!
 *
 * Changelog:
 *  30-08-1997	RMK	0.0.0	Created, READONLY version
 *  22-01-1998	RMK	0.0.1	Updated to 2.1.80
 */

#include <linux/module.h>
#include <linux/blk.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/unistd.h>
#include <linux/stat.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ecard.h>

#include "../../scsi/sd.h"
#include "../../scsi/hosts.h"
#include "cumana_2.h"
#include "fas216.h"

/* Hmm - this should go somewhere else */
#define BUS_ADDR(x) ((((unsigned long)(x)) << 2) + IO_BASE)

/* Configuration */
#define XTALFREQ		40
#define INT_POLARITY		CTRL_INT_HIGH

/*
 * List of devices that the driver will recognise
 */
#define CUMANASCSI2_LIST	{ MANU_CUMANA, PROD_CUMANA_SCSI_2 }

/*
 * Version
 */
#define VER_MAJOR	0
#define VER_MINOR	0
#define VER_PATCH	1

static struct expansion_card *ecs[MAX_ECARDS];

static struct proc_dir_entry proc_scsi_cumanascsi_2 = {
	PROC_SCSI_QLOGICFAS, 6, "cumanascs2",
	S_IFDIR | S_IRUGO | S_IXUGO, 2
};

/*
 * Function: void cumanascsi_2_intr (int irq, void *dev_id, struct pt_regs *regs)
 * Purpose : handle interrupts from Cumana SCSI 2 card
 * Params  : irq - interrupt number
 *	     dev_id - user-defined (Scsi_Host structure)
 *	     regs - processor registers at interrupt
 */
static void cumanascsi_2_intr (int irq, void *dev_id, struct pt_regs *regs)
{
    struct Scsi_Host *instance = (struct Scsi_Host *)dev_id;

    fas216_intr (instance);
}

/*
 * Function: int cumanascsi_2_dma_setup (instance, SCpnt, direction)
 * Purpose : initialises DMA/PIO
 * Params  : instance  - host
 *	     SCpnt     - command
 *	     direction - DMA on to/off of card
 * Returns : 0 if we should not set CMD_WITHDMA for transfer info command
 */
static fasdmatype_t cumanascsi_2_dma_setup (struct Scsi_Host *instance, Scsi_Pointer *SCp, fasdmadir_t direction)
{
    /*
     * We don't do DMA
     */
    return fasdma_pseudo;
}

/*
 * Function: int cumanascsi_2_dma_pseudo (instance, SCpnt, direction, transfer)
 * Purpose : handles pseudo DMA
 * Params  : instance  - host
 *	     SCpnt     - command
 *	     direction - DMA on to/off of card
 *	     transfer  - minimum number of bytes we expect to transfer
 * Returns : bytes transfered
 */
static int
cumanascsi_2_dma_pseudo (struct Scsi_Host *instance, Scsi_Pointer *SCp,
			 fasdmadir_t direction, int transfer)
{
    CumanaScsi2_Info *info = (CumanaScsi2_Info *)instance->hostdata;
    unsigned int length;
    unsigned char *addr;

    length = SCp->this_residual;
    addr = SCp->ptr;

    if (direction == DMA_OUT)
#if 0
	while (length > 1) {
	    unsigned long word;


	    if (inb (REG0_STATUS(&info->info)) & STATUS_INT)
		goto end;

	    if (!(inb (info->cstatus) & CSTATUS_DRQ))
		continue;

	    word = *addr | (*addr + 1) << 8;
	    outw (info->dmaarea);
	    addr += 2;
	    length -= 2;
	}
#else
	printk ("PSEUDO_OUT???\n");
#endif
    else {
	if (transfer && (transfer & 255)) {
	    while (length >= 256) {
		if (inb (REG0_STATUS(&info->info)) & STATUS_INT)
		    goto end;
	    
		if (!(inb (info->cstatus) & CSTATUS_DRQ))
		    continue;

		insw (info->dmaarea, addr, 256 >> 1);
		addr += 256;
		length -= 256;
	    }
	}

	while (length > 0) {
	    unsigned long word;

	    if (inb (REG0_STATUS(&info->info)) & STATUS_INT)
		goto end;
	    
	    if (!(inb (info->cstatus) & CSTATUS_DRQ))
		continue;

	    word = inw (info->dmaarea);
	    *addr++ = word;
	    if (--length > 0) {
		*addr++ = word >> 8;
		length --;
	    }
	}
    }

end:
    return SCp->this_residual - length;
}

/*
 * Function: int cumanascsi_2_dma_stop (instance, SCpnt)
 * Purpose : stops DMA/PIO
 * Params  : instance  - host
 *	     SCpnt     - command
 */
static void cumanascsi_2_dma_stop (struct Scsi_Host *instance, Scsi_Pointer *SCp)
{
    /*
     * no DMA to stop
     */
}

/*
 * Function: int cumanascsi_2_detect (Scsi_Host_Template * tpnt)
 * Purpose : initialises Cumana SCSI 2 driver
 * Params  : tpnt - template for this SCSI adapter
 * Returns : >0 if host found, 0 otherwise.
 */
int cumanascsi_2_detect (Scsi_Host_Template *tpnt)
{
    static const card_ids cumanascsi_2_cids[] = { CUMANASCSI2_LIST, { 0xffff, 0xffff} };
    int count = 0;
    struct Scsi_Host *instance;
  
    tpnt->proc_dir = &proc_scsi_cumanascsi_2;
    memset (ecs, 0, sizeof (ecs));

    ecard_startfind ();

    while (1) {
    	CumanaScsi2_Info *info;

	ecs[count] = ecard_find (0, cumanascsi_2_cids);
	if (!ecs[count])
	    break;

	ecard_claim (ecs[count]);

	instance = scsi_register (tpnt, sizeof (CumanaScsi2_Info));
	if (!instance) {
	    ecard_release (ecs[count]);
	    break;
	}

	instance->io_port = ecard_address (ecs[count], ECARD_MEMC, 0);
	instance->irq = ecs[count]->irq;

	ecs[count]->irqaddr = (unsigned char *)BUS_ADDR(instance->io_port);
	ecs[count]->irqmask = CSTATUS_IRQ;

	request_region (instance->io_port      ,  1, "cumanascsi2-stat");
	request_region (instance->io_port + 128, 64, "cumanascsi2-dma");
	request_region (instance->io_port + 192, 16, "cumanascsi2-fas");
	if (request_irq (instance->irq, cumanascsi_2_intr, SA_INTERRUPT, "cumanascsi2", instance)) {
	    printk ("scsi%d: IRQ%d not free, interrupts disabled\n",
		      instance->host_no, instance->irq);
	}

	info = (CumanaScsi2_Info *)instance->hostdata;
	info->info.scsi.io_port	= instance->io_port + 192;
	info->info.scsi.irq	= instance->irq;
	info->info.ifcfg.clockrate = XTALFREQ;
	info->info.ifcfg.select_timeout = 255;
	info->info.dma.setup	= cumanascsi_2_dma_setup;
	info->info.dma.pseudo	= cumanascsi_2_dma_pseudo;
	info->info.dma.stop	= cumanascsi_2_dma_stop;
	info->dmaarea		= instance->io_port + 128;
	info->cstatus		= instance->io_port;

	fas216_init (instance);
	++count;
    }
    return count;
}

/*
 * Function: int cumanascsi_2_release (struct Scsi_Host * host)
 * Purpose : releases all resources used by this adapter
 * Params  : host - driver host structure to return info for.
 * Returns : nothing
 */
int cumanascsi_2_release (struct Scsi_Host *instance)
{
    int i;

    fas216_release (instance);

    if (instance->irq != 255)
	free_irq (instance->irq, instance);
    release_region (instance->io_port, 1);
    release_region (instance->io_port + 128, 32);
    release_region (instance->io_port + 192, 16);

    for (i = 0; i < MAX_ECARDS; i++)
	if (ecs[i] && instance->io_port == ecard_address (ecs[i], ECARD_MEMC, 0))
		ecard_release (ecs[i]);
    return 0;
}

/*
 * Function: const char *cumanascsi_2_info (struct Scsi_Host * host)
 * Purpose : returns a descriptive string about this interface,
 * Params  : host - driver host structure to return info for.
 * Returns : pointer to a static buffer containing null terminated string.
 */
const char *cumanascsi_2_info (struct Scsi_Host *host)
{
    CumanaScsi2_Info *info = (CumanaScsi2_Info *)host->hostdata;
    static char string[100], *p;

    p = string;
    p += sprintf (string, "%s at port %X irq %d v%d.%d.%d scsi %s",
		  host->hostt->name, host->io_port, host->irq,
		  VER_MAJOR, VER_MINOR, VER_PATCH,
		  info->info.scsi.type);

    return string;
}

/*
 * Function: int cumanascsi_2_proc_info (char *buffer, char **start, off_t offset,
 *					 int length, int host_no, int inout)
 * Purpose : Return information about the driver to a user process accessing
 *	     the /proc filesystem.
 * Params  : buffer - a buffer to write information to
 *	     start  - a pointer into this buffer set by this routine to the start
 *		      of the required information.
 *	     offset - offset into information that we have read upto.
 *	     length - length of buffer
 *	     host_no - host number to return information for
 *	     inout  - 0 for reading, 1 for writing.
 * Returns : length of data written to buffer.
 */
int cumanascsi_2_proc_info (char *buffer, char **start, off_t offset,
			    int length, int host_no, int inout)
{
    int pos, begin;
    struct Scsi_Host *host = scsi_hostlist;
    CumanaScsi2_Info *info;
    Scsi_Device *scd;

    while (host) {
	if (host->host_no == host_no)
	    break;
	host = host->next;
    }
    if (!host)
	return 0;

    info = (CumanaScsi2_Info *)host->hostdata;
    if (inout == 1)
      return -EINVAL;

    begin = 0;
    pos = sprintf (buffer,
			"Cumana SCSI II driver version %d.%d.%d\n",
			VER_MAJOR, VER_MINOR, VER_PATCH);
    pos += sprintf (buffer + pos,
			"Address: %08X          IRQ : %d\n"
			"FAS    : %s\n\n"
			"Statistics:\n",
			host->io_port, host->irq, info->info.scsi.type);

    pos += sprintf (buffer+pos,
			"Queued commands: %-10ld   Issued commands: %-10ld\n"
			"Done commands  : %-10ld   Reads          : %-10ld\n"
			"Writes         : %-10ld   Others         : %-10ld\n"
			"Disconnects    : %-10ld   Aborts         : %-10ld\n"
			"Resets         : %-10ld\n",
			 info->info.stats.queues,      info->info.stats.removes,
			 info->info.stats.fins,        info->info.stats.reads,
			 info->info.stats.writes,      info->info.stats.miscs,
			 info->info.stats.disconnects, info->info.stats.aborts,
			 info->info.stats.resets);

    pos += sprintf (buffer+pos, "\nAttached devices:%s\n", host->host_queue ? "" : " none");

    for (scd = host->host_queue; scd; scd = scd->next) {
	int len;

	proc_print_scsidevice (scd, buffer, &len, pos);
	pos += len;
	pos += sprintf (buffer+pos, "Extensions: ");
	if (scd->tagged_supported)
	    pos += sprintf (buffer+pos, "TAG %sabled [%d] ",
			    scd->tagged_queue ? "en" : "dis",
			    scd->current_tag);
	pos += sprintf (buffer+pos, "\n");

	if (pos + begin < offset) {
	    begin += pos;
	    pos = 0;
	}
	if (pos + begin > offset + length)
	    break;
    }

    *start = buffer + (offset - begin);
    pos -= offset - begin;
    if (pos > length)
	pos = length;

    return pos;
}

#ifdef MODULE
Scsi_Host_Template driver_template = CUMANASCSI_2;

#include "../../scsi/scsi_module.c"
#endif
