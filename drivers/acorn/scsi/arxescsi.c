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
 *  30-08-1997	RMK	0.0.0	Created, READONLY version as cumana_2.c
 *  22-01-1998	RMK	0.0.1	Updated to 2.1.80
 *  15-04-1998	RMK	0.0.1	Only do PIO if FAS216 will allow it.
 *  11-06-1998 		0.0.2   Changed to support ARXE 16-bit SCSI card, enabled writing
 *  				by Stefan Hanske
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
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ecard.h>

#include "../../scsi/sd.h"
#include "../../scsi/hosts.h"
#include "arxescsi.h"
#include "fas216.h"

/* Hmm - this should go somewhere else */
#define BUS_ADDR(x) ((((unsigned long)(x)) << 2) + IO_BASE)

/* Configuration */
#define ARXESCSI_XTALFREQ		24
#define ARXESCSI_ASYNC_PERIOD		200
#define ARXESCSI_SYNC_DEPTH		0

/*
 * List of devices that the driver will recognise
 */
#define ARXESCSI_LIST		{ MANU_ARXE, PROD_ARXE_SCSI }

/*
 * Version
 */
#define VER_MAJOR	0
#define VER_MINOR	0
#define VER_PATCH	2

static struct expansion_card *ecs[MAX_ECARDS];

static struct proc_dir_entry proc_scsi_arxescsi = {
	PROC_SCSI_QLOGICFAS, 6, "arxescsi",
	S_IFDIR | S_IRUGO | S_IXUGO, 2
};

/*
 * Function: int arxescsi_dma_setup(host, SCpnt, direction, min_type)
 * Purpose : initialises DMA/PIO
 * Params  : host      - host
 *	     SCpnt     - command
 *	     direction - DMA on to/off of card
 *	     min_type  - minimum DMA support that we must have for this transfer
 * Returns : 0 if we should not set CMD_WITHDMA for transfer info command
 */
static fasdmatype_t
arxescsi_dma_setup(struct Scsi_Host *host, Scsi_Pointer *SCp,
		       fasdmadir_t direction, fasdmatype_t min_type)
{
	/*
	 * We don't do real DMA
	 */
	return fasdma_pseudo;
}



/* Faster transfer routines, written by SH to speed up the loops */

static __inline__ unsigned char getb(unsigned int address, unsigned int reg)
{
	unsigned char value;

	__asm__ __volatile__(
	"ldrb	%0, [%1, %2, lsl #5]"
	: "=r" (value)
	: "r" (address), "r" (reg) );
	return value;
}

static __inline__ unsigned int getw(unsigned int address, unsigned int reg)
{
	unsigned int value;
	
	__asm__ __volatile__(
	"ldr	%0, [%1, %2, lsl #5]\n\t"
	"mov	%0, %0, lsl #16\n\t"
	"mov	%0, %0, lsr #16"
	: "=r" (value)
	: "r" (address), "r" (reg) );
	return value;
}

static __inline__ void putw(unsigned int address, unsigned int reg, unsigned long value)
{
	__asm__ __volatile__(
	"mov	%0, %0, lsl #16\n\t"
	"str	%0, [%1, %2, lsl #5]"
	:
	: "r" (value), "r" (address), "r" (reg) );
}


/*
 * Function: int arxescsi_dma_pseudo(host, SCpnt, direction, transfer)
 * Purpose : handles pseudo DMA
 * Params  : host      - host
 *	     SCpnt     - command
 *	     direction - DMA on to/off of card
 *	     transfer  - minimum number of bytes we expect to transfer
 */
void arxescsi_dma_pseudo(struct Scsi_Host *host, Scsi_Pointer *SCp,
			fasdmadir_t direction, int transfer)
{
	ARXEScsi_Info *info = (ARXEScsi_Info *)host->hostdata;
	unsigned int length, io, error=0;
	unsigned char *addr;

	length = SCp->this_residual;
	addr = SCp->ptr;
	io = __ioaddr(host->io_port);

	if (direction == DMA_OUT) {
		while (length > 0) {
			unsigned long word;


			word = *addr | *(addr + 1) << 8;
			if (getb(io, 4) & STAT_INT)
				break;

			if (!(getb(io, 48) & CSTATUS_IRQ))
				continue;

			putw(io, 16, word);
			if (length > 1) {
				addr += 2;
				length -= 2;
			} else {
				addr += 1;
				length -= 1;
			}
		}
	}
	else {
		if (transfer && (transfer & 255)) {
			while (length >= 256) {
				if (getb(io, 4) & STAT_INT) {
					error=1;
					break;
				}
	    
				if (!(getb(io, 48) & CSTATUS_IRQ))
					continue;

				insw(info->dmaarea, addr, 256 >> 1);
				addr += 256;
				length -= 256;
			}
		}

		if (!(error))
			while (length > 0) {
				unsigned long word;

				if (getb(io, 4) & STAT_INT)
					break;

				if (!(getb(io, 48) & CSTATUS_IRQ))
					continue;

				word = getw(io, 16);
				*addr++ = word;
				if (--length > 0) {
					*addr++ = word >> 8;
					length --;
				}
			}
	}
}

/*
 * Function: int arxescsi_dma_stop(host, SCpnt)
 * Purpose : stops DMA/PIO
 * Params  : host  - host
 *	     SCpnt - command
 */
static void arxescsi_dma_stop(struct Scsi_Host *host, Scsi_Pointer *SCp)
{
	/*
	 * no DMA to stop
	 */
}

/*
 * Function: int arxescsi_detect(Scsi_Host_Template * tpnt)
 * Purpose : initialises ARXE SCSI driver
 * Params  : tpnt - template for this SCSI adapter
 * Returns : >0 if host found, 0 otherwise.
 */
int arxescsi_detect(Scsi_Host_Template *tpnt)
{
	static const card_ids arxescsi_cids[] = { ARXESCSI_LIST, { 0xffff, 0xffff} };
	int count = 0;
	struct Scsi_Host *host;
  
	tpnt->proc_dir = &proc_scsi_arxescsi;
	memset(ecs, 0, sizeof (ecs));

	ecard_startfind();

	while (1) {
	    	ARXEScsi_Info *info;

		ecs[count] = ecard_find(0, arxescsi_cids);
		if (!ecs[count])
			break;

		ecard_claim(ecs[count]);
		
		host = scsi_register(tpnt, sizeof (ARXEScsi_Info));
		if (!host) {
			ecard_release(ecs[count]);
			break;
		}

		host->io_port = ecard_address(ecs[count], ECARD_MEMC, 0) + 0x0800;
		host->irq = NO_IRQ;
		host->dma_channel = NO_DMA;
		host->can_queue = 0; /* no command queueing */
		info = (ARXEScsi_Info *)host->hostdata;

		info->info.scsi.io_port		= host->io_port;
		info->info.scsi.irq		= host->irq;
		info->info.scsi.io_shift	= 3;
		info->info.ifcfg.clockrate	= ARXESCSI_XTALFREQ;
		info->info.ifcfg.select_timeout = 255;
		info->info.ifcfg.asyncperiod	= ARXESCSI_ASYNC_PERIOD;
		info->info.ifcfg.sync_max_depth	= ARXESCSI_SYNC_DEPTH;
		info->info.ifcfg.cntl3		= CNTL3_FASTSCSI | CNTL3_FASTCLK;
		info->info.ifcfg.disconnect_ok	= 0;
		info->info.ifcfg.wide_max_size	= 0;
		info->info.dma.setup		= arxescsi_dma_setup;
		info->info.dma.pseudo		= arxescsi_dma_pseudo;
		info->info.dma.stop		= arxescsi_dma_stop;
		info->dmaarea			= host->io_port + 128;
		info->cstatus			= host->io_port + 384;
		
		ecs[count]->irqaddr = (unsigned char *)BUS_ADDR(host->io_port);
		ecs[count]->irqmask = CSTATUS_IRQ;

		request_region(host->io_port      , 120, "arxescsi-fas");
		request_region(host->io_port + 128, 384, "arxescsi-dma");

		printk("scsi%d: Has no interrupts - using polling mode\n",
		       host->host_no);

		fas216_init(host);
		++count;
	}
	return count;
}

/*
 * Function: int arxescsi_release(struct Scsi_Host * host)
 * Purpose : releases all resources used by this adapter
 * Params  : host - driver host structure to return info for.
 * Returns : nothing
 */
int arxescsi_release(struct Scsi_Host *host)
{
	int i;

	fas216_release(host);

	release_region(host->io_port, 120);
	release_region(host->io_port + 128, 384);

	for (i = 0; i < MAX_ECARDS; i++)
		if (ecs[i] && host->io_port == (ecard_address(ecs[i], ECARD_MEMC, 0) + 0x0800))
			ecard_release(ecs[i]);
	return 0;
}

/*
 * Function: const char *arxescsi_info(struct Scsi_Host * host)
 * Purpose : returns a descriptive string about this interface,
 * Params  : host - driver host structure to return info for.
 * Returns : pointer to a static buffer containing null terminated string.
 */
const char *arxescsi_info(struct Scsi_Host *host)
{
	ARXEScsi_Info *info = (ARXEScsi_Info *)host->hostdata;
	static char string[100], *p;

	p = string;
	p += sprintf(string, "%s at port %lX irq %d v%d.%d.%d scsi %s",
		     host->hostt->name, host->io_port, host->irq,
		     VER_MAJOR, VER_MINOR, VER_PATCH,
		     info->info.scsi.type);

	return string;
}

/*
 * Function: int arxescsi_proc_info(char *buffer, char **start, off_t offset,
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
int arxescsi_proc_info(char *buffer, char **start, off_t offset,
			    int length, int host_no, int inout)
{
	int pos, begin;
	struct Scsi_Host *host = scsi_hostlist;
	ARXEScsi_Info *info;
	Scsi_Device *scd;

	while (host) {
		if (host->host_no == host_no)
			break;
		host = host->next;
	}
	if (!host)
		return 0;

	info = (ARXEScsi_Info *)host->hostdata;
	if (inout == 1)
		return -EINVAL;

	begin = 0;
	pos = sprintf(buffer,
			"ARXE 16-bit SCSI driver version %d.%d.%d\n",
			VER_MAJOR, VER_MINOR, VER_PATCH);
	pos += sprintf(buffer + pos,
			"Address: %08lX          IRQ : %d\n"
			"FAS    : %s\n\n"
			"Statistics:\n",
			host->io_port, host->irq, info->info.scsi.type);

	pos += fas216_print_stats(&info->info, buffer + pos);

	pos += sprintf (buffer+pos, "\nAttached devices:\n");

	for (scd = host->host_queue; scd; scd = scd->next) {
		pos += fas216_print_device(&info->info, scd, buffer + pos);

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
Scsi_Host_Template driver_template = ARXEScsi;

#include "../../scsi/scsi_module.c"
#endif
