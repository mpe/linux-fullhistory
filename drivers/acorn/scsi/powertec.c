/*
 * linux/arch/arm/drivers/scsi/powertec.c
 *
 * Copyright (C) 1997 Russell King
 *
 * This driver is based on experimentation.  Hence, it may have made
 * assumptions about the particular card that I have available, and
 * may not be reliable!
 *
 * Changelog:
 *  01-10-1997	RMK	Created, READONLY version
 *  15-02-1998	RMK	Added DMA support and hardware definitions
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
#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/pgtable.h>

#include "../../scsi/sd.h"
#include "../../scsi/hosts.h"
#include "powertec.h"

/* Configuration */
#define POWERTEC_XTALFREQ	40
#define POWERTEC_ASYNC_PERIOD	200
#define POWERTEC_SYNC_DEPTH	16

/*
 * List of devices that the driver will recognise
 */
#define POWERTECSCSI_LIST	{ MANU_ALSYSTEMS, PROD_ALSYS_SCSIATAPI }

#define POWERTEC_FAS216_OFFSET	0xc00
#define POWERTEC_FAS216_SHIFT	4
#define POWERTEC_INTR_STATUS	0x800
#define POWERTEC_INTR_BIT	0x80
#define POWERTEC_INTR_CONTROL	0x407
#define POWERTEC_INTR_ENABLE	1
#define POWERTEC_INTR_DISABLE	0

/*
 * Version
 */
#define VER_MAJOR	0
#define VER_MINOR	0
#define VER_PATCH	1

static struct expansion_card *ecs[MAX_ECARDS];

static struct proc_dir_entry proc_scsi_powertec = {
	PROC_SCSI_QLOGICISP, 8, "powertec",
	S_IFDIR | S_IRUGO | S_IXUGO, 2
};

/* Function: void powertecscsi_irqenable(ec, irqnr)
 * Purpose : Enable interrupts on powertec SCSI card
 * Params  : ec    - expansion card structure
 *         : irqnr - interrupt number
 */
static void
powertecscsi_irqenable(struct expansion_card *ec, int irqnr)
{
	unsigned int port = (unsigned int)ec->irq_data;
	outb(POWERTEC_INTR_ENABLE, port);
}

/* Function: void powertecscsi_irqdisable(ec, irqnr)
 * Purpose : Disable interrupts on powertec SCSI card
 * Params  : ec    - expansion card structure
 *         : irqnr - interrupt number
 */
static void
powertecscsi_irqdisable(struct expansion_card *ec, int irqnr)
{
	unsigned int port = (unsigned int)ec->irq_data;
	outb(POWERTEC_INTR_DISABLE, port);
}

static const expansioncard_ops_t powertecscsi_ops = {
	powertecscsi_irqenable,
	powertecscsi_irqdisable,
	NULL,
	NULL
};

/* Function: void powertecscsi_intr(int irq, void *dev_id,
 *				    struct pt_regs *regs)
 * Purpose : handle interrupts from Powertec SCSI card
 * Params  : irq - interrupt number
 *	     dev_id - user-defined (Scsi_Host structure)
 *	     regs - processor registers at interrupt
 */
static void
powertecscsi_intr(int irq, void *dev_id, struct pt_regs *regs)
{
    struct Scsi_Host *instance = (struct Scsi_Host *)dev_id;

    fas216_intr(instance);
}

static void
powertecscsi_invalidate(char *addr, long len, fasdmadir_t direction)
{
	unsigned int page;

	if (direction == DMA_OUT) {
		for (page = (unsigned int) addr; len > 0;
		     page += PAGE_SIZE, len -= PAGE_SIZE)
			flush_page_to_ram(page);
	} else
		flush_cache_range(current->mm, (unsigned long)addr,
				  (unsigned long)addr + len);
}

/* Function: fasdmatype_t powertecscsi_dma_setup(instance, SCpnt, direction)
 * Purpose : initialises DMA/PIO
 * Params  : instance  - host
 *	     SCpnt     - command
 *	     direction - DMA on to/off of card
 * Returns : type of transfer to be performed
 */
static fasdmatype_t
powertecscsi_dma_setup(struct Scsi_Host *instance, Scsi_Pointer *SCp,
		       fasdmadir_t direction)
{
	if (instance->dma_channel != NO_DMA && SCp->this_residual >= 512) {
		int buf;
static		dmasg_t dmasg[256];

		for (buf = 1; buf <= SCp->buffers_residual; buf++) {
			dmasg[buf].address = __virt_to_bus(
				(unsigned long)SCp->buffer[buf].address);
			dmasg[buf].length = SCp->buffer[buf].length;

			powertecscsi_invalidate(SCp->buffer[buf].address,
						SCp->buffer[buf].length,
						direction);
		}

		dmasg[0].address = __virt_to_phys((unsigned long)SCp->ptr);
		dmasg[0].length = SCp->this_residual;
		powertecscsi_invalidate(SCp->ptr,
					SCp->this_residual, direction);

		disable_dma(instance->dma_channel);
		set_dma_sg(instance->dma_channel, dmasg, buf);
		set_dma_mode(instance->dma_channel,
			     direction == DMA_OUT ? DMA_MODE_WRITE :
						    DMA_MODE_READ);
		enable_dma(instance->dma_channel);
		return fasdma_real_all;
	}
	/*
	 * We don't do DMA, we only do slow PIO
	 */
	return fasdma_none;
}

/* Function: int powertecscsi_dma_stop(instance, SCpnt)
 * Purpose : stops DMA/PIO
 * Params  : instance  - host
 *	     SCpnt     - command
 */
static void
powertecscsi_dma_stop(struct Scsi_Host *instance, Scsi_Pointer *SCp)
{
	if (instance->dma_channel != NO_DMA)
		disable_dma(instance->dma_channel);
}

/* Function: int powertecscsi_detect(Scsi_Host_Template * tpnt)
 * Purpose : initialises PowerTec SCSI driver
 * Params  : tpnt - template for this SCSI adapter
 * Returns : >0 if host found, 0 otherwise.
 */
int
powertecscsi_detect(Scsi_Host_Template *tpnt)
{
	static const card_ids powertecscsi_cids[] =
			{ POWERTECSCSI_LIST, { 0xffff, 0xffff} };
	int count = 0;
	struct Scsi_Host *instance;
  
	tpnt->proc_dir = &proc_scsi_powertec;
	memset(ecs, 0, sizeof (ecs));

	ecard_startfind();

	while(1) {
	    	PowerTecScsi_Info *info;

		ecs[count] = ecard_find(0, powertecscsi_cids);
		if (!ecs[count])
			break;

		ecard_claim(ecs[count]);

		instance = scsi_register(tpnt, sizeof (PowerTecScsi_Info));
		if (!instance) {
			ecard_release(ecs[count]);
			break;
		}

		instance->io_port = ecard_address(ecs[count], ECARD_IOC, 0);
		instance->irq = ecs[count]->irq;

		ecs[count]->irqaddr = (unsigned char *)
			    ioaddr(instance->io_port + POWERTEC_INTR_STATUS);
		ecs[count]->irqmask = POWERTEC_INTR_BIT;
		ecs[count]->irq_data = (void *)
			    (instance->io_port + POWERTEC_INTR_CONTROL);
		ecs[count]->ops = (expansioncard_ops_t *)&powertecscsi_ops;

		request_region(instance->io_port + POWERTEC_FAS216_OFFSET,
				16 << POWERTEC_FAS216_SHIFT, "powertec2-fas");

		if (request_irq(instance->irq, powertecscsi_intr,
				SA_INTERRUPT, "powertec", instance)) {
			printk("scsi%d: IRQ%d not free, interrupts disabled\n",
			       instance->host_no, instance->irq);
			instance->irq = NO_IRQ;
		}

		info = (PowerTecScsi_Info *)instance->hostdata;

		instance->dma_channel = 3; /* slot 1 */
		if (request_dma(instance->dma_channel, "powertec")) {
			printk("scsi%d: DMA%d not free, DMA disabled\n",
			       instance->host_no, instance->dma_channel);
			instance->dma_channel = NO_DMA;
		}

		info->info.scsi.io_port	=
				instance->io_port + POWERTEC_FAS216_OFFSET;
		info->info.scsi.io_shift= POWERTEC_FAS216_SHIFT;
		info->info.scsi.irq	= instance->irq;
		info->info.ifcfg.clockrate = POWERTEC_XTALFREQ;
		info->info.ifcfg.select_timeout = 255;
		info->info.ifcfg.asyncperiod = POWERTEC_ASYNC_PERIOD;
		info->info.ifcfg.sync_max_depth = POWERTEC_SYNC_DEPTH;
		info->info.dma.setup	= powertecscsi_dma_setup;
		info->info.dma.pseudo	= NULL;
		info->info.dma.stop	= powertecscsi_dma_stop;

		fas216_init(instance);
		++count;
	}
	return count;
}

/* Function: int powertecscsi_release(struct Scsi_Host * host)
 * Purpose : releases all resources used by this adapter
 * Params  : host - driver host structure to return info for.
 * Returns : nothing
 */
int powertecscsi_release(struct Scsi_Host *instance)
{
	int i;

	fas216_release(instance);

	if (instance->irq != NO_IRQ)
		free_irq(instance->irq, instance);
	if (instance->dma_channel != NO_DMA)
		free_dma(instance->dma_channel);
	release_region(instance->io_port + POWERTEC_FAS216_OFFSET,
		       16 << POWERTEC_FAS216_SHIFT);

	for (i = 0; i < MAX_ECARDS; i++)
		if (ecs[i] &&
		    instance->io_port == ecard_address(ecs[i], ECARD_IOC, 0))
			ecard_release(ecs[i]);
	return 0;
}

/* Function: const char *powertecscsi_info(struct Scsi_Host * host)
 * Purpose : returns a descriptive string about this interface,
 * Params  : host - driver host structure to return info for.
 * Returns : pointer to a static buffer containing null terminated string.
 */
const char *powertecscsi_info(struct Scsi_Host *host)
{
	PowerTecScsi_Info *info = (PowerTecScsi_Info *)host->hostdata;
	static char string[100], *p;

	p = string;
	p += sprintf(string, "%s at port %X ",
		     host->hostt->name, host->io_port);

	if (host->irq != NO_IRQ)
		p += sprintf(p, "irq %d ", host->irq);
	else
		p += sprintf(p, "NO IRQ ");

	if (host->dma_channel != NO_DMA)
		p += sprintf(p, "dma %d ", host->dma_channel);
	else
		p += sprintf(p, "NO DMA ");

	p += sprintf(p, "v%d.%d.%d scsi %s",
		     VER_MAJOR, VER_MINOR, VER_PATCH,
		     info->info.scsi.type);

	return string;
}

/* Function: int powertecscsi_proc_info(char *buffer, char **start, off_t offset,
 *					int length, int host_no, int inout)
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
int powertecscsi_proc_info(char *buffer, char **start, off_t offset,
			    int length, int host_no, int inout)
{
	int pos, begin;
	struct Scsi_Host *host = scsi_hostlist;
	PowerTecScsi_Info *info;
	Scsi_Device *scd;

	while (host) {
		if (host->host_no == host_no)
			break;
		host = host->next;
	}
	if (!host)
		return 0;

	info = (PowerTecScsi_Info *)host->hostdata;
	if (inout == 1)
		return -EINVAL;

	begin = 0;
	pos = sprintf(buffer,
			"PowerTec SCSI driver version %d.%d.%d\n",
			VER_MAJOR, VER_MINOR, VER_PATCH);
	pos += sprintf(buffer + pos,
			"Address: %08X    IRQ : %d     DMA : %d\n"
			"FAS    : %s\n\n"
			"Statistics:\n",
			host->io_port, host->irq, host->dma_channel,
			info->info.scsi.type);

	pos += sprintf(buffer+pos,
			"Queued commands: %-10d   Issued commands: %-10d\n"
			"Done commands  : %-10d   Reads          : %-10d\n"
			"Writes         : %-10d   Others         : %-10d\n"
			"Disconnects    : %-10d   Aborts         : %-10d\n"
			"Resets         : %-10d\n",
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
Scsi_Host_Template driver_template = POWERTECSCSI;

#include "../../scsi/scsi_module.c"
#endif
