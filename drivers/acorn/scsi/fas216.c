/*
 * linux/arch/arm/drivers/scsi/fas216.c
 *
 * Copyright (C) 1997 Russell King
 *
 * Based on information in qlogicfas.c by Tom Zerucha, Michael Griffith, and
 * other sources, including:
 *   the AMD Am53CF94 data sheet
 *
 * This is a generic driver.  To use it, have a look at cumana_2.c.  You
 * should define your own structure that overlays FAS216_Info, eg:
 * struct my_host_data {
 *    FAS216_Info info;
 *    ... my host specific data ...
 * };
 *
 * Changelog:
 *  30-08-1997	RMK	Created
 *  14-09-1997	RMK	Started disconnect support
 *  08-02-1998	RMK	Corrected real DMA support
 *  15-02-1998	RMK	Started sync xfer support
 *  06-04-1998	RMK	Tightened conditions for printing incomplete
 *			transfers
 *  02-05-1998	RMK	Added extra checks in fas216_reset
 *  24-05-1998	RMK	Fixed synchronous transfers with period >= 200ns
 *  27-06-1998	RMK	Changed asm/delay.h to linux/delay.h
 *
 * Todo:
 *  - tighten up the MESSAGE_REJECT support.
 *  - allow individual devices to enable sync xfers.
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
#include <linux/delay.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ecard.h>

#define FAS216_C

#include "../../scsi/scsi.h"
#include "../../scsi/hosts.h"
#include "fas216.h"

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Generic FAS216/NCR53C9x driver");

#define VER_MAJOR	0
#define VER_MINOR	0
#define VER_PATCH	4

#define SCSI2_TAG

/* NOTE: SCSI2 Synchronous transfers *require* DMA according to
 *  the data sheet.  This restriction is crazy, especially when
 *  you only want to send 16 bytes!  What were the guys who
 *  designed this chip on at that time?  Did they read the SCSI2
 *  spec at all?  The following sections are taken from the SCSI2
 *  standard (s2r10) concerning this:
 *
 * > IMPLEMENTORS NOTES:
 * >   (1)  Re-negotiation at every selection is not recommended, since a
 * >   significant performance impact is likely.
 *
 * >  The implied synchronous agreement shall remain in effect until a BUS DEVICE
 * >  RESET message is received, until a hard reset condition occurs, or until one
 * >  of the two SCSI devices elects to modify the agreement.  The default data
 * >  transfer mode is asynchronous data transfer mode.  The default data transfer
 * >  mode is entered at power on, after a BUS DEVICE RESET message, or after a hard
 * >  reset condition.
 *
 *  In total, this means that once you have elected to use synchronous
 *  transfers, you must always use DMA.
 *
 *  I was thinking that this was a good chip until I found this restriction ;(
 */
#define SCSI2_SYNC

#undef DEBUG_CONNECT
#undef DEBUG_BUSSERVICE
#undef DEBUG_FUNCTIONDONE
#undef DEBUG_MESSAGES

#undef CHECK_STRUCTURE

static struct { int stat, ssr, isr, ph; } list[8];
static int ptr;

static void fas216_dumpstate(FAS216_Info *info)
{
	printk("FAS216: CTCL=%02X CTCM=%02X CMD=%02X STAT=%02X"
	       " INST=%02X IS=%02X CFIS=%02X",
		inb(REG_CTCL(info)), inb(REG_CTCM(info)),
		inb(REG_CMD(info)),  inb(REG_STAT(info)),
		inb(REG_INST(info)), inb(REG_IS(info)),
		inb(REG_CFIS(info)));
	printk(" CNTL1=%02X CNTL2=%02X CNTL3=%02X CTCH=%02X\n",
		inb(REG_CNTL1(info)), inb(REG_CNTL2(info)),
		inb(REG_CNTL3(info)), inb(REG_CTCH(info)));
}

static void fas216_dumpinfo(FAS216_Info *info)
{
	static int used = 0;
	int i;

	if (used++)
		return;

	printk("FAS216_Info=\n");
	printk("  { magic_start=%lX host=%p SCpnt=%p origSCpnt=%p\n",
		info->magic_start, info->host, info->SCpnt,
		info->origSCpnt);
	printk("    scsi={ io_port=%X io_shift=%X irq=%X cfg={ %X %X %X %X }\n",
		info->scsi.io_port, info->scsi.io_shift, info->scsi.irq,
		info->scsi.cfg[0], info->scsi.cfg[1], info->scsi.cfg[2],
		info->scsi.cfg[3]);
	printk("           type=%p phase=%X reconnected={ target=%d lun=%d tag=%d }\n",
		info->scsi.type, info->scsi.phase,
		info->scsi.reconnected.target,
		info->scsi.reconnected.lun, info->scsi.reconnected.tag);
	printk("           SCp={ ptr=%p this_residual=%X buffer=%p buffers_residual=%X }\n",
		info->scsi.SCp.ptr, info->scsi.SCp.this_residual,
		info->scsi.SCp.buffer, info->scsi.SCp.buffers_residual);
	printk("      msgs async_stp=%X last_message=%X disconnectable=%d aborting=%d }\n",
		info->scsi.async_stp, info->scsi.last_message,
		info->scsi.disconnectable, info->scsi.aborting);
	printk("    stats={ queues=%X removes=%X fins=%X reads=%X writes=%X miscs=%X\n"
	       "            disconnects=%X aborts=%X resets=%X }\n",
		info->stats.queues, info->stats.removes, info->stats.fins,
		info->stats.reads, info->stats.writes, info->stats.miscs,
		info->stats.disconnects, info->stats.aborts, info->stats.resets);
	printk("    ifcfg={ clockrate=%X select_timeout=%X asyncperiod=%X sync_max_depth=%X }\n",
		info->ifcfg.clockrate, info->ifcfg.select_timeout,
		info->ifcfg.asyncperiod, info->ifcfg.sync_max_depth);
	for (i = 0; i < 8; i++) {
		printk("    busyluns[%d]=%X dev[%d]={ disconnect_ok=%d stp=%X sof=%X negstate=%X }\n",
			i, info->busyluns[i], i,
			info->device[i].disconnect_ok, info->device[i].stp,
			info->device[i].sof, info->device[i].negstate);
	}
	printk("    dma={ transfer_type=%X setup=%p pseudo=%p stop=%p }\n",
		info->dma.transfer_type, info->dma.setup,
		info->dma.pseudo, info->dma.stop);
	printk("    internal_done=%X magic_end=%lX }\n",
		info->internal_done, info->magic_end);
}

#ifdef CHECK_STRUCTURE
static void fas216_checkmagic(FAS216_Info *info, const char *func)
{
	int corruption = 0;
	if (info->magic_start != MAGIC) {
		printk(KERN_CRIT "FAS216 Error: magic at start corrupted\n");
		corruption++;
	}
	if (info->magic_end != MAGIC) {
		printk(KERN_CRIT "FAS216 Error: magic at end corrupted\n");
		corruption++;
	}
	if (corruption) {
		fas216_dumpinfo(info);
		panic("scsi memory space corrupted in %s", func);
	}
}
#else
#define fas216_checkmagic(info,func)
#endif

static const char *fas216_bus_phase(int stat)
{
	static const char *phases[] = {
		"DATA OUT", "DATA IN",
		"COMMAND", "STATUS",
		"MISC OUT", "MISC IN",
		"MESG OUT", "MESG IN"
	};

	return phases[stat & STAT_BUSMASK];
}

static const char *fas216_drv_phase(FAS216_Info *info)
{
	switch (info->scsi.phase) {
	case PHASE_IDLE:	return "idle";
	case PHASE_SELECTION:	return "selection";
	case PHASE_MESSAGESENT:	return "message sent";
	case PHASE_RECONNECTED:	return "reconnected";
	case PHASE_DATAOUT:	return "data out";
	case PHASE_DATAIN:	return "data in";
	case PHASE_MSGOUT:	return "message out";
	case PHASE_MSGIN:	return "message in";
	case PHASE_AFTERMSGOUT:	return "after message out";
	case PHASE_STATUS:	return "status";
	case PHASE_DISCONNECT:	return "disconnect";
	case PHASE_DONE:	return "done";
	default:		return "???";
	}
}

static char fas216_target(FAS216_Info *info)
{
	if (info->SCpnt)
		return '0' + info->SCpnt->target;
	else
		return 'H';
}

static void add_debug_list(int stat, int ssr, int isr, int ph)
{
	list[ptr].stat = stat;
	list[ptr].ssr = ssr;
	list[ptr].isr = isr;
	list[ptr].ph = ph;

	ptr = (ptr + 1) & 7;
}

static void print_debug_list(void)
{
	int i;

	i = ptr;

	printk(KERN_ERR "SCSI IRQ trail: ");
	do {
		printk("%02X:%02X:%02X:%1X ",
			list[i].stat, list[i].ssr,
			list[i].isr, list[i].ph);
		i = (i + 1) & 7;
	} while (i != ptr);
	printk("\n");
}

static void fas216_done(FAS216_Info *info, unsigned int result);

/* Function: int fas216_clockrate(unsigned int clock)
 * Purpose : calculate correct value to be written into clock conversion
 *	     factor register.
 * Params  : clock - clock speed in MHz
 * Returns : CLKF_ value
 */
static int fas216_clockrate(int clock)
{
	if (clock <= 10 || clock > 40) {
		printk(KERN_CRIT
		       "fas216: invalid clock rate: check your driver!\n");
		clock = -1;
	} else
		clock = ((clock - 1) / 5 + 1) & 7;

	return clock;
}

/* Function: int fas216_syncperiod(FAS216_Info *info, int ns)
 * Purpose : Calculate value to be loaded into the STP register
 *           for a given period in ns
 * Params  : info - state structure for interface connected to device
 *         : ns   - period in ns (between subsequent bytes)
 * Returns : Value suitable for REG_STP
 */
static int
fas216_syncperiod(FAS216_Info *info, int ns)
{
	int value = (info->ifcfg.clockrate * ns) / 1000;

	fas216_checkmagic(info, "fas216_syncperiod");

	if (value < 4)
		value = 4;
	else if (value > 35)
		value = 35;

	return value & 31;
}

/* Function: void fas216_set_sync(FAS216_Info *info, int target)
 * Purpose : Correctly setup FAS216 chip for specified transfer period.
 * Params  : info   - state structure for interface
 *         : target - target
 * Notes   : we need to switch the chip out of FASTSCSI mode if we have
 *           a transfer period >= 200ns - otherwise the chip will violate
 *           the SCSI timings.
 */
static void
fas216_set_sync(FAS216_Info *info, int target)
{
	outb(info->device[target].sof, REG_SOF(info));
	outb(info->device[target].stp, REG_STP(info));
	if (info->device[target].period >= (200 / 4))
		outb(info->scsi.cfg[2] & ~CNTL3_FASTSCSI, REG_CNTL3(info));
	else
		outb(info->scsi.cfg[2], REG_CNTL3(info));
}

/* Function: void fas216_updateptrs(FAS216_Info *info, int bytes_transferred)
 * Purpose : update data pointers after transfer suspended/paused
 * Params  : info              - interface's local pointer to update
 *           bytes_transferred - number of bytes transferred
 */
static void
fas216_updateptrs(FAS216_Info *info, int bytes_transferred)
{
	unsigned char *ptr;
	unsigned int residual;

	fas216_checkmagic(info, "fas216_updateptrs");

	ptr = info->scsi.SCp.ptr;
	residual = info->scsi.SCp.this_residual;

	info->SCpnt->request_bufflen -= bytes_transferred;

	while (residual <= bytes_transferred && bytes_transferred) {
		/* We have used up this buffer */
		bytes_transferred -= residual;
		if (info->scsi.SCp.buffers_residual) {
			info->scsi.SCp.buffer++;
			info->scsi.SCp.buffers_residual--;
			ptr = (unsigned char *)info->scsi.SCp.buffer->address;
			residual = info->scsi.SCp.buffer->length;
		} else {
			ptr = NULL;
			residual = 0;
		}
	}

	residual -= bytes_transferred;
	ptr += bytes_transferred;

	info->scsi.SCp.ptr = ptr;
	info->scsi.SCp.this_residual = residual;
}

/* Function: void fas216_pio(FAS216_Info *info, fasdmadir_t direction)
 * Purpose : transfer data off of/on to card using programmed IO
 * Params  : info      - interface to transfer data to/from
 *           direction - direction to transfer data (DMA_OUT/DMA_IN)
 * Notes   : this is incredibly slow
 */
static void
fas216_pio(FAS216_Info *info, fasdmadir_t direction)
{
	unsigned int residual;
	char *ptr;
	int correction;

	fas216_checkmagic(info, "fas216_pio");

	residual = info->scsi.SCp.this_residual;
	ptr = info->scsi.SCp.ptr;

	if (direction == DMA_OUT) {
	    	while (residual > 0) {
			if ((inb(REG_CFIS(info)) & CFIS_CF) < 8) {
				outb(*ptr++, REG_FF(info));
				residual -= 1;
			} else if (inb(REG_STAT(info)) & STAT_INT)
				break;
		}
		correction = inb(REG_CFIS(info)) & CFIS_CF;
	} else {
	    	while (residual > 0) {
			if ((inb(REG_CFIS(info)) & CFIS_CF) != 0) {
				*ptr++ = inb(REG_FF(info));
				residual -= 1;
			} else if (inb(REG_STAT(info)) & STAT_INT)
				break;
		}
		correction = 0;
	}

	ptr -= correction;
	residual += correction;

	if (residual == 0) {
		if (info->scsi.SCp.buffers_residual) {
			info->scsi.SCp.buffer++;
			info->scsi.SCp.buffers_residual--;
			ptr = (unsigned char *)info->scsi.SCp.buffer->address;
			residual = info->scsi.SCp.buffer->length;
		} else {
			ptr = NULL;
			residual = 0;
		}
	}

	info->scsi.SCp.ptr = ptr;
	info->scsi.SCp.this_residual = residual;
}

/* Function: void fas216_starttransfer(FAS216_Info *info,
 *				       fasdmadir_t direction)
 * Purpose : Start a DMA/PIO transfer off of/on to card
 * Params  : info      - interface from which device disconnected from
 *           direction - transfer direction (DMA_OUT/DMA_IN)
 */
static void
fas216_starttransfer(FAS216_Info *info, fasdmadir_t direction, int flush_fifo)
{
	fasdmatype_t dmatype;

	fas216_checkmagic(info, "fas216_starttransfer");

	info->scsi.phase = (direction == DMA_OUT) ?
				PHASE_DATAOUT : PHASE_DATAIN;

	if (info->dma.transfer_type != fasdma_none &&
	    info->dma.transfer_type != fasdma_pio) {
		unsigned long total, residual;

		if (info->dma.transfer_type == fasdma_real_all)
			total = info->SCpnt->request_bufflen;
		else
			total = info->scsi.SCp.this_residual;

		residual = (inb(REG_CFIS(info)) & CFIS_CF) +
			    inb(REG_CTCL(info)) +
			    (inb(REG_CTCM(info)) << 8) +
			    (inb(REG_CTCH(info)) << 16);
		fas216_updateptrs(info, total - residual);
	}
	info->dma.transfer_type = fasdma_none;

	if (!info->scsi.SCp.ptr) {
		printk("scsi%d.%c: null buffer passed to "
			"fas216_starttransfer\n", info->host->host_no,
			fas216_target(info));
		return;
	}

	/* flush FIFO */
	if (flush_fifo)
		outb(CMD_FLUSHFIFO, REG_CMD(info));

	/*
	 * Default to PIO mode or DMA mode if we have a synchronous
	 * transfer agreement.
	 */
	if (info->device[info->SCpnt->target].sof && info->dma.setup)
		dmatype = fasdma_real_all;
	else
		dmatype = fasdma_pio;

	if (info->dma.setup)
		dmatype = info->dma.setup(info->host, &info->scsi.SCp,
					  direction, dmatype);
	info->dma.transfer_type = dmatype;

	switch (dmatype) {
	case fasdma_pio:
		outb(0, REG_SOF(info));
		outb(info->scsi.async_stp, REG_STP(info));
		outb(info->scsi.SCp.this_residual, REG_STCL(info));
		outb(info->scsi.SCp.this_residual >> 8, REG_STCM(info));
		outb(info->scsi.SCp.this_residual >> 16, REG_STCH(info));
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		fas216_pio(info, direction);
		break;

	case fasdma_pseudo:
		outb(info->scsi.SCp.this_residual, REG_STCL(info));
		outb(info->scsi.SCp.this_residual >> 8, REG_STCM(info));
		outb(info->scsi.SCp.this_residual >> 16, REG_STCH(info));
		outb(CMD_TRANSFERINFO | CMD_WITHDMA, REG_CMD(info));
		info->dma.pseudo(info->host, &info->scsi.SCp,
				 direction, info->SCpnt->transfersize);
		break;

	case fasdma_real_block:
		outb(info->scsi.SCp.this_residual, REG_STCL(info));
		outb(info->scsi.SCp.this_residual >> 8, REG_STCM(info));
		outb(info->scsi.SCp.this_residual >> 16, REG_STCH(info));
		outb(CMD_TRANSFERINFO | CMD_WITHDMA, REG_CMD(info));
		break;

	case fasdma_real_all:
		outb(info->SCpnt->request_bufflen, REG_STCL(info));
		outb(info->SCpnt->request_bufflen >> 8, REG_STCM(info));
		outb(info->SCpnt->request_bufflen >> 16, REG_STCH(info));
		outb(CMD_TRANSFERINFO | CMD_WITHDMA, REG_CMD(info));
		break;

	default:
		printk(KERN_ERR "scsi%d.%d: invalid FAS216 DMA type\n",
		       info->host->host_no, fas216_target(info));
		break;
	}
}

/* Function: void fas216_stoptransfer(FAS216_Info *info)
 * Purpose : Stop a DMA transfer onto / off of the card
 * Params  : info      - interface from which device disconnected from
 */
static void
fas216_stoptransfer(FAS216_Info *info)
{
	fas216_checkmagic(info, "fas216_stoptransfer");

	if (info->dma.transfer_type != fasdma_none &&
	    info->dma.transfer_type != fasdma_pio) {
		unsigned long total, residual;

		if ((info->dma.transfer_type == fasdma_real_all ||
		     info->dma.transfer_type == fasdma_real_block) &&
		    info->dma.stop)
			info->dma.stop(info->host, &info->scsi.SCp);

		if (info->dma.transfer_type == fasdma_real_all)
			total = info->SCpnt->request_bufflen;
		else
			total = info->scsi.SCp.this_residual;

		residual = (inb(REG_CFIS(info)) & CFIS_CF) +
			    inb(REG_CTCL(info)) +
			    (inb(REG_CTCM(info)) << 8) +
			    (inb(REG_CTCH(info)) << 16);
		fas216_updateptrs(info, total - residual);
		info->dma.transfer_type = fasdma_none;
	}
	if (info->scsi.phase == PHASE_DATAOUT)
		outb(CMD_FLUSHFIFO, REG_CMD(info));
}

/* Function: void fas216_disconnected_intr(FAS216_Info *info)
 * Purpose : handle device disconnection
 * Params  : info - interface from which device disconnected from
 */
static void
fas216_disconnect_intr(FAS216_Info *info)
{
	fas216_checkmagic(info, "fas216_disconnected_intr");

#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: disconnect phase=%02X\n", info->host->host_no,
		fas216_target(info), info->scsi.phase);
#endif
	msgqueue_flush(&info->scsi.msgs);

	switch (info->scsi.phase) {
	case PHASE_SELECTION:			/* while selecting - no target		*/
		fas216_done(info, DID_NO_CONNECT);
		break;

	case PHASE_DISCONNECT:			/* message in - disconnecting		*/
		outb(CMD_ENABLESEL, REG_CMD(info));
		info->scsi.disconnectable = 1;
		info->scsi.reconnected.tag = 0;
		info->scsi.phase = PHASE_IDLE;
		info->stats.disconnects += 1;
		break;

	case PHASE_DONE:			/* at end of command - complete		*/
		fas216_done(info, DID_OK);
		break;

	case PHASE_AFTERMSGOUT:			/* message out - possible ABORT message	*/
		if (info->scsi.last_message == ABORT) {
			info->scsi.aborting = 0;
			fas216_done(info, DID_ABORT);
			break;
		}

	default:				/* huh?					*/
		printk(KERN_ERR "scsi%d.%c: unexpected disconnect in phase %s\n",
			info->host->host_no, fas216_target(info), fas216_drv_phase(info));
		print_debug_list();
		fas216_stoptransfer(info);
		fas216_done(info, DID_ERROR);
		break;
	}
}

/* Function: void fas216_reselected_intr(FAS216_Info *info)
 * Purpose : Start reconnection of a device
 * Params  : info - interface which was reselected
 */
static void
fas216_reselected_intr(FAS216_Info *info)
{
	unsigned char target, identify_msg, ok;

	fas216_checkmagic(info, "fas216_reselected_intr");

	if (info->scsi.phase == PHASE_SELECTION && info->SCpnt) {
		Scsi_Cmnd *SCpnt = info->SCpnt;

		info->origSCpnt = SCpnt;
		info->SCpnt = NULL;

		if (info->device[SCpnt->target].negstate == syncneg_sent)
			info->device[SCpnt->target].negstate = syncneg_start;
	}

#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: reconnect phase=%02X\n", info->host->host_no,
		fas216_target(info), info->scsi.phase);
#endif

	msgqueue_flush(&info->scsi.msgs);

	if ((inb(REG_CFIS(info)) & CFIS_CF) != 2) {
		printk(KERN_ERR "scsi%d.H: incorrect number of bytes after reselect\n",
			info->host->host_no);
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_addmsg(&info->scsi.msgs, 1, MESSAGE_REJECT);
		info->scsi.phase = PHASE_MSGOUT;
		outb(CMD_MSGACCEPTED, REG_CMD(info));
		return;
	}

	target = inb(REG_FF(info));
	identify_msg = inb(REG_FF(info));

	ok = 1;
	if (!(target & (1 << info->host->this_id))) {
		printk(KERN_ERR "scsi%d.H: invalid host id on reselect\n", info->host->host_no);
		ok = 0;
	}

	if (!(identify_msg & 0x80)) {
		printk(KERN_ERR "scsi%d.H: no IDENTIFY message on reselect, got msg %02X\n",
			info->host->host_no, identify_msg);
		ok = 0;
	}

	if (!ok) {
		/*
		 * Something went wrong - abort the command on
		 * the target.  Should this be INITIATOR_ERROR ?
		 */
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_addmsg(&info->scsi.msgs, 1, ABORT);
		info->scsi.phase = PHASE_MSGOUT;
		outb(CMD_MSGACCEPTED, REG_CMD(info));
		return;
	}

	target &= ~(1 << info->host->this_id);
	switch (target) {
	case   1:  target = 0; break;
	case   2:  target = 1; break;
	case   4:  target = 2; break;
	case   8:  target = 3; break;
	case  16:  target = 4; break;
	case  32:  target = 5; break;
	case  64:  target = 6; break;
	case 128:  target = 7; break;
	default:   target = info->host->this_id; break;
	}

	identify_msg &= 7;
	info->scsi.reconnected.target = target;
	info->scsi.reconnected.lun    = identify_msg;
	info->scsi.reconnected.tag    = 0;

	ok = 0;
	if (info->scsi.disconnectable && info->SCpnt &&
	    info->SCpnt->target == target && info->SCpnt->lun == identify_msg)
		ok = 1;

	if (!ok && queue_probetgtlun(&info->queues.disconnected, target, identify_msg))
		ok = 1;

	if (ok) {
		info->scsi.phase = PHASE_RECONNECTED;
		outb(target, REG_SDID(info));
	} else {
		/*
		 * Our command structure not found - abort the command on the target
		 * Should this be INITIATOR_ERROR ?
		 */
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_addmsg(&info->scsi.msgs, 1, ABORT);
		info->scsi.phase = PHASE_MSGOUT;
	}
	outb(CMD_MSGACCEPTED, REG_CMD(info));
}

/* Function: void fas216_finish_reconnect(FAS216_Info *info)
 * Purpose : finish reconnection sequence for device
 * Params  : info - interface which caused function done interrupt
 */
static void
fas216_finish_reconnect(FAS216_Info *info)
{
	fas216_checkmagic(info, "fas216_reconnect");

#ifdef DEBUG_CONNECT
	printk("Connected: %1X %1X %02X, reconnected: %1X %1X %02X\n",
		info->SCpnt->target, info->SCpnt->lun, info->SCpnt->tag,
		info->scsi.reconnected.target, info->scsi.reconnected.lun,
		info->scsi.reconnected.tag);
#endif

	if (info->scsi.disconnectable && info->SCpnt) {
		info->scsi.disconnectable = 0;
		if (info->SCpnt->target == info->scsi.reconnected.target &&
		    info->SCpnt->lun    == info->scsi.reconnected.lun &&
		    info->SCpnt->tag    == info->scsi.reconnected.tag) {
#ifdef DEBUG_CONNECT
			printk("scsi%d.%c: reconnected",
				info->host->host_no, fas216_target(info));
#endif
		} else {
			queue_add_cmd_tail(&info->queues.disconnected, info->SCpnt);
#ifdef DEBUG_CONNECT
			printk("scsi%d.%c: had to move command to disconnected queue\n",
				info->host->host_no, fas216_target(info));
#endif
			info->SCpnt = NULL;
		}
	}
	if (!info->SCpnt) {
		info->SCpnt = queue_remove_tgtluntag(&info->queues.disconnected,
					info->scsi.reconnected.target,
					info->scsi.reconnected.lun,
					info->scsi.reconnected.tag);
#ifdef DEBUG_CONNECT
		printk("scsi%d.%c: had to get command",
			info->host->host_no, fas216_target(info));
#endif
	}
	if (!info->SCpnt) {
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_addmsg(&info->scsi.msgs, 1, ABORT);
		info->scsi.phase = PHASE_MSGOUT;
		info->scsi.aborting = 1;
	} else {
		/*
		 * Restore data pointer from SAVED data pointer
		 */
		info->scsi.SCp = info->SCpnt->SCp;
#ifdef DEBUG_CONNECT
		printk(", data pointers: [%p, %X]",
			info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
#endif
	}
#ifdef DEBUG_CONNECT
	printk("\n");
#endif
}

/* Function: void fas216_message(FAS216_Info *info)
 * Purpose : handle a function done interrupt from FAS216 chip
 * Params  : info - interface which caused function done interrupt
 */
static void fas216_message(FAS216_Info *info)
{
	unsigned char message[16];
	unsigned int msglen = 1;

	fas216_checkmagic(info, "fas216_message");

	message[0] = inb(REG_FF(info));

	if (message[0] == EXTENDED_MESSAGE) {
		int tout;
		outb(CMD_MSGACCEPTED, REG_CMD(info));
		for (tout = 1000000; tout; tout--)
			if (inb(REG_STAT(info)) & STAT_INT)
				break;
		inb(REG_INST(info));
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		for (tout = 1000000; tout; tout--)
			if (inb(REG_STAT(info)) & STAT_INT)
				break;
		inb(REG_INST(info));

		message[1] = inb(REG_FF(info));

		for (msglen = 2; msglen < message[1] + 2; msglen++) {
			outb(CMD_MSGACCEPTED, REG_CMD(info));
			for (tout = 1000000; tout; tout--)
				if (inb(REG_STAT(info)) & STAT_INT)
					break;
			inb(REG_INST(info));
			outb(CMD_TRANSFERINFO, REG_CMD(info));
			for (tout = 1000000; tout; tout--)
				if (inb(REG_STAT(info)) & STAT_INT)
					break;
			inb(REG_INST(info));

			message[msglen] = inb(REG_FF(info));
		}
	}

#ifdef DEBUG_MESSAGES
	{
		int i;

		printk("scsi%d.%c: message in: ",
			info->host->host_no, fas216_target(info));
		for (i = 0; i < msglen; i++)
			printk("%02X ", message[i]);
		printk("\n");
	}
#endif
	if (info->scsi.phase == PHASE_RECONNECTED) {
		if (message[0] == SIMPLE_QUEUE_TAG)
			info->scsi.reconnected.tag = message[1];
		fas216_finish_reconnect(info);
		info->scsi.phase = PHASE_MSGIN;
	}

	switch (message[0]) {
	case COMMAND_COMPLETE:
		printk("fas216: command complete with no status in MESSAGE_IN?\n");
		break;

	case SAVE_POINTERS:
		/*
		 * Save current data pointer to SAVED data pointer
		 */
		info->SCpnt->SCp = info->scsi.SCp;
#if defined (DEBUG_MESSAGES) || defined (DEBUG_CONNECT)
		printk("scsi%d.%c: save data pointers: [%p, %X]\n",
			info->host->host_no, fas216_target(info),
			info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
#endif
		break;

	case RESTORE_POINTERS:
		/*
		 * Restore current data pointer from SAVED data pointer
		 */
		info->scsi.SCp = info->SCpnt->SCp;
#if defined (DEBUG_MESSAGES) || defined (DEBUG_CONNECT)
		printk("scsi%d.%c: restore data pointers: [%p, %X]\n",
			info->host->host_no, fas216_target(info),
			info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
#endif
		break;

	case DISCONNECT:
		info->scsi.phase = PHASE_DISCONNECT;
		break;

	case MESSAGE_REJECT:
		printk("scsi%d.%c: reject, last message %04X\n",
			info->host->host_no, fas216_target(info),
			info->scsi.last_message);
		break;

	case SIMPLE_QUEUE_TAG:
		/* handled above */
		printk("scsi%d.%c: reconnect queue tag %02X\n",
			info->host->host_no, fas216_target(info),
			message[1]);
		break;

	case EXTENDED_MESSAGE:
		switch (message[2]) {
		case EXTENDED_SDTR:	/* Sync transfer negociation request/reply */
			switch (info->device[info->SCpnt->target].negstate) {
			case syncneg_invalid:
				msgqueue_flush(&info->scsi.msgs);
				outb(CMD_SETATN, REG_CMD(info));
				msgqueue_addmsg(&info->scsi.msgs, 1, MESSAGE_REJECT);
				info->scsi.phase = PHASE_MSGOUT;
				break;

			default:
				if (message[4] > info->ifcfg.sync_max_depth)
					message[4] = info->ifcfg.sync_max_depth;
				if (message[3] < 1000 / info->ifcfg.clockrate)
					message[3] = 1000 / info->ifcfg.clockrate;

				outb(CMD_SETATN, REG_CMD(info));
				msgqueue_addmsg(&info->scsi.msgs, 5,
						EXTENDED_MESSAGE, 3, EXTENDED_SDTR,
						message[3], message[4]);
				info->scsi.phase = PHASE_MSGOUT;
			case syncneg_sent:
				info->device[info->SCpnt->target].negstate = syncneg_complete;
				info->device[info->SCpnt->target].period = message[3];
				info->device[info->SCpnt->target].sof = message[4];
				info->device[info->SCpnt->target].stp =
					fas216_syncperiod(info, message[3] * 4);
				printk(KERN_NOTICE "scsi%d.%c: using synchronous transfer, offset %d, %d ns\n",
					info->host->host_no, fas216_target(info), message[4], message[3] * 4);
				fas216_set_sync(info, info->SCpnt->target);
				break;
			}
			break;

		case EXTENDED_WDTR:	/* Wide transfer negociation request/reply */
			/* We don't do wide transfers - reject message */
		default:
			printk("scsi%d.%c: unrecognised extended message %02X, rejecting\n",
				info->host->host_no, fas216_target(info),
				message[2]);
			msgqueue_flush(&info->scsi.msgs);
			outb(CMD_SETATN, REG_CMD(info));
			msgqueue_addmsg(&info->scsi.msgs, 1, MESSAGE_REJECT);
			info->scsi.phase = PHASE_MSGOUT;
			break;
		}
		break;

	default:
		printk("scsi%d.%c: unrecognised message %02X, rejecting\n",
			info->host->host_no, fas216_target(info),
			message[0]);
		msgqueue_flush(&info->scsi.msgs);
		outb(CMD_SETATN, REG_CMD(info));
		msgqueue_addmsg(&info->scsi.msgs, 1, MESSAGE_REJECT);
		info->scsi.phase = PHASE_MSGOUT;
		break;
	}
	outb(CMD_MSGACCEPTED, REG_CMD(info));
}

/* Function: void fas216_send_command(FAS216_Info *info)
 * Purpose : send a command to a target after all message bytes have been sent
 * Params  : info - interface which caused bus service
 */
static void fas216_send_command(FAS216_Info *info)
{
	int i;

	fas216_checkmagic(info, "fas216_send_command");

	outb(CMD_NOP|CMD_WITHDMA, REG_CMD(info));
	outb(CMD_FLUSHFIFO, REG_CMD(info));

	/* load command */
	for (i = 0; i < info->SCpnt->cmd_len; i++)
		outb(info->SCpnt->cmnd[i], REG_FF(info));

	outb(CMD_TRANSFERINFO, REG_CMD(info));
}

/* Function: int fas216_busservice_selection(FAS216_Info *info, unsigned int stat)
 * Purpose : handle bus service in selection phase
 * Params  : info - interface which caused bus service
 * Returns : 0 if unable to service this interrupt
 */
static int fas216_busservice_selection(FAS216_Info *info, unsigned int stat)
{
	fas216_checkmagic(info, "fas216_busservice_selection");

	switch (stat & STAT_BUSMASK) {
	case STAT_DATAOUT:	/* data out phase			*/
		fas216_starttransfer(info, DMA_OUT, 1);
		return 1;

	case STAT_DATAIN:		/* data in phase		*/
		fas216_starttransfer(info, DMA_IN, 0);
		return 1;

	case STAT_STATUS:		/* status phase			*/
		info->scsi.phase = PHASE_STATUS;
		outb(CMD_INITCMDCOMPLETE, REG_CMD(info));
		return 1;

	case STAT_MESGIN:		/* message in phase		*/
		info->scsi.phase = PHASE_MSGIN;
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		return 1;

	case STAT_MESGOUT:{		/* message out phase		*/
		char *msg;
		int start = 1, msglen;

		/* load message bytes, but don't forget to miss the first
		 * byte!
		 */
		while ((msg = msgqueue_getnextmsg(&info->scsi.msgs, &msglen)) != NULL) {
			int i;

			for (i = start; i < msglen; i++)
				outb(msg[i], REG_FF(info));
			start = 0;
		}
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		info->scsi.phase = PHASE_MESSAGESENT;
		return 1;
	}
	default:
		return 0;
	}
}

/* Function: int fas216_busservice_messagesent(FAS216_Info *info, unsigned int stat)
 * Purpose : handle bus service after the IDENTIFY message has been sent
 * Params  : info - interface which caused bus service
 * Returns : 0 if unable to service this interrupt
 */
static int fas216_busservice_messagesent(FAS216_Info *info, unsigned int stat)
{
	fas216_checkmagic(info, "fas216_busservice_messagesent");

	switch (stat & STAT_BUSMASK) {
	case STAT_MESGIN:		/* message in phase		*/
		info->scsi.phase = PHASE_MSGIN;
		outb(CMD_FLUSHFIFO, REG_CMD(info));
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		return 1;

	case STAT_COMMAND:		/* command phase		*/
		fas216_send_command(info);
		return 1;

	default:
		return 0;
	}
}

/* Function: int fas216_busservice_dataphase(FAS216_Info *info, unsigned int stat)
 * Purpose : handle bus service in a data in/out phase.
 * Params  : info - interface which caused bus service
 * Returns : 0 if unable to service this interrupt
 * Note    : We do not allow the device to change the data direction!
 */
static int fas216_busservice_dataphase(FAS216_Info *info, unsigned int stat)
{
	fas216_checkmagic(info, "fas216_busservice_dataphase");

	switch (stat & STAT_BUSMASK) {
	case STAT_DATAIN:		/* continue data in phase	*/
		if (info->scsi.phase == PHASE_DATAIN) {
			fas216_starttransfer(info, DMA_IN, 0);
			return 1;
		} else
			return 0;

	case STAT_DATAOUT:		/* continue data out phase	*/
		if (info->scsi.phase == PHASE_DATAOUT) {
			fas216_starttransfer(info, DMA_OUT, 0);
			return 1;
		} else
			return 0;

	case STAT_STATUS:		/* status in phase		*/
		fas216_stoptransfer(info);
		info->scsi.phase = PHASE_STATUS;
		outb(CMD_INITCMDCOMPLETE, REG_CMD(info));
		return 1;

	case STAT_MESGIN:		/* message in phase		*/
		fas216_stoptransfer(info);
		info->scsi.phase = PHASE_MSGIN;
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		return 1;

	default:
		return 0;
	}
}

/* Function: int fas216_busservice_reconnected(FAS216_Info *info, unsigned int stat)
 * Purpose : handle bus service in after a reconnection
 * Params  : info - interface which caused bus service
 * Returns : 0 if unable to service this interrupt
 * Note    : We do not allow the device to change the data direction!
 */
static int fas216_busservice_reconnected(FAS216_Info *info, unsigned int stat)
{
	fas216_checkmagic(info, "fas216_busservice_reconnected");

	switch (stat & STAT_BUSMASK) {
	case STAT_MESGIN:
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		return 1;

	case STAT_STATUS:
		fas216_finish_reconnect(info);
		info->scsi.phase = PHASE_STATUS;
		outb(CMD_INITCMDCOMPLETE, REG_CMD(info));
		return 1;

	case STAT_DATAOUT:		/* data out phase		*/
		fas216_finish_reconnect(info);
		fas216_starttransfer(info, DMA_OUT, 1);
		return 1;

	case STAT_DATAIN:		/* data in phase		*/
		fas216_finish_reconnect(info);
		fas216_starttransfer(info, DMA_IN, 0);
		return 1;

	default:
		return 0;
	}
}

/* Function: int fas216_busservice_messageout(FAS216_Info *info, unsigned int stat)
 * Purpose : handle bus service to send a message
 * Params  : info - interface which caused bus service
 * Returns : 0 if unable to service this interrupt
 * Note    : We do not allow the device to change the data direction!
 */
static int fas216_busservice_messageout(FAS216_Info *info, unsigned int stat)
{
	fas216_checkmagic(info, "fas216_busservice_messageout");

	if ((stat & STAT_BUSMASK) != STAT_MESGOUT) {
		printk("scsi%d.%c: didn't manage MESSAGE OUT phase\n",
		       info->host->host_no, fas216_target(info));
		return 0;
	} else {
		unsigned int msglen = msgqueue_msglength(&info->scsi.msgs);

		outb(CMD_FLUSHFIFO, REG_CMD(info));

		if (msglen == 0)
			outb(NOP, REG_FF(info));
		else {
			char *msg;

			while ((msg = msgqueue_getnextmsg(&info->scsi.msgs, &msglen)) != NULL) {
				int i;

				for (i = 0; i < msglen; i++)
					outb(msg[i], REG_FF(info));
			}
		}
		outb(CMD_TRANSFERINFO, REG_CMD(info));
		info->scsi.phase = PHASE_AFTERMSGOUT;
		return 1;
	}
}

/* Function: void fas216_busservice_intr(FAS216_Info *info, unsigned int stat, unsigned int ssr)
 * Purpose : handle a bus service interrupt from FAS216 chip
 * Params  : info - interface which caused bus service interrupt
 *           stat - Status register contents
 *           ssr  - SCSI Status register contents
 */
static void fas216_busservice_intr(FAS216_Info *info, unsigned int stat, unsigned int ssr)
{
	fas216_checkmagic(info, "fas216_busservice_intr");

#ifdef DEBUG_BUSSERVICE
	printk("scsi%d.%c: bus service: stat=%02X ssr=%02X phase=%02X\n",
		info->host->host_no, fas216_target(info), stat, ssr, info->scsi.phase);
#endif
	switch (ssr & IS_BITS) {
	case IS_MSGBYTESENT:		/* select with ATN and stop steps completed	*/
	case IS_COMPLETE:			/* last action completed		*/
		outb(CMD_NOP, REG_CMD(info));

		switch (info->scsi.phase) {
		case PHASE_SELECTION:		/* while selecting - selected target	*/
			if (!fas216_busservice_selection(info, stat))
				printk("scsi%d.%c: bus phase %s after connect?\n",
					info->host->host_no, fas216_target(info),
					fas216_bus_phase(stat));
			break;

		case PHASE_MESSAGESENT:
			if (!fas216_busservice_messagesent(info, stat))
				printk("scsi%d.%c: bus phase %s after message sent?\n",
					info->host->host_no, fas216_target(info),
					fas216_bus_phase(stat));
			break;

		case PHASE_DATAIN:		/* while transfering data in		*/
		case PHASE_DATAOUT:		/* while transfering data out		*/
			if (!fas216_busservice_dataphase(info, stat))
				printk("scsi%d.%c: bus phase %s after %s?\n",
					info->host->host_no, fas216_target(info),
					fas216_bus_phase(stat), fas216_drv_phase(info));
			break;

		case PHASE_RECONNECTED:		/* newly reconnected device		*/
			/*
			 * Command reconnected - if MESGIN, get message - it may be
			 * the tag.  If not, get command out of the disconnected queue
			 */
			if (!fas216_busservice_reconnected(info, stat))
				printk("scsi%d.%c: bus phase %s after reconnect?\n",
					info->host->host_no, fas216_target(info),
					fas216_bus_phase(stat));
			break;

		case PHASE_MSGIN:
		case PHASE_AFTERMSGOUT:
			switch (stat & STAT_BUSMASK) {
			case STAT_MESGIN:
				info->scsi.phase = PHASE_MSGIN;
				outb(CMD_TRANSFERINFO, REG_CMD(info));
				break;

			case STAT_COMMAND:	/* command phase			*/
				fas216_send_command(info);
				info->scsi.phase = PHASE_SELECTION;
				break;

			default:
				printk("scsi%d.%c: bus phase %s after %s?\n",
					info->host->host_no, fas216_target(info),
					fas216_bus_phase(stat),
					fas216_drv_phase(info));
			}
			break;

		case PHASE_MSGOUT:
			if (!fas216_busservice_messageout(info, stat))
				printk("scsi%d.%c: bus phase %s instead of message out?\n",
					info->host->host_no, fas216_target(info),
					fas216_bus_phase(stat));
			break;

		case PHASE_DISCONNECT:
			printk("scsi%d.%c: disconnect message received, but bus service %s?\n",
				info->host->host_no, fas216_target(info),
				fas216_bus_phase(stat));
			outb(CMD_SETATN, REG_CMD(info));
			msgqueue_addmsg(&info->scsi.msgs, 1, INITIATOR_ERROR);
			info->scsi.phase = PHASE_MSGOUT;
			info->scsi.aborting = 1;
			outb(CMD_TRANSFERINFO, REG_CMD(info));
			break;

		default:
			printk("scsi%d.%c: internal phase %s for bus service?"
				"  What do I do with this?\n",
				info->host->host_no, fas216_target(info),
				fas216_drv_phase(info));
		}
		break;

	default:
		printk("scsi%d.%c: bus service at step %d?\n",
			info->host->host_no, fas216_target(info),
			ssr & IS_BITS);
	}
}

/* Function: void fas216_funcdone_intr(FAS216_Info *info, unsigned int stat, unsigned int ssr)
 * Purpose : handle a function done interrupt from FAS216 chip
 * Params  : info - interface which caused function done interrupt
 *           stat - Status register contents
 *           ssr  - SCSI Status register contents
 */
static void fas216_funcdone_intr(FAS216_Info *info, unsigned int stat, unsigned int ssr)
{
	int status, message;

	fas216_checkmagic(info, "fas216_funcdone_intr");

#ifdef DEBUG_FUNCTIONDONE
	printk("scsi%d.%c: function done: stat=%X ssr=%X phase=%02X\n",
		info->host->host_no, fas216_target(info), stat, ssr, info->scsi.phase);
#endif
	switch (info->scsi.phase) {
	case PHASE_STATUS:			/* status phase - read status and msg	*/
		status = inb(REG_FF(info));
		message = inb(REG_FF(info));
		info->scsi.SCp.Message = message;
		info->scsi.SCp.Status = status;
		info->scsi.phase = PHASE_DONE;
		outb(CMD_MSGACCEPTED, REG_CMD(info));
		break;

	case PHASE_IDLE:			/* reselected?				*/
	case PHASE_MSGIN:			/* message in phase			*/
	case PHASE_RECONNECTED:			/* reconnected command			*/
		if ((stat & STAT_BUSMASK) == STAT_MESGIN) {
			fas216_message(info);
			break;
		}

	default:
		printk("scsi%d.%c: internal phase %s for function done?"
			"  What do I do with this?\n",
			info->host->host_no, fas216_target(info),
			fas216_drv_phase(info));
	}
}

/* Function: void fas216_intr(struct Scsi_Host *instance)
 * Purpose : handle interrupts from the interface to progress a command
 * Params  : instance - interface to service
 */
void fas216_intr(struct Scsi_Host *instance)
{
	FAS216_Info *info = (FAS216_Info *)instance->hostdata;
	unsigned char isr, ssr, stat;

	fas216_checkmagic(info, "fas216_intr");

	stat = inb(REG_STAT(info));
	ssr = inb(REG_IS(info));
	isr = inb(REG_INST(info));

	add_debug_list(stat, ssr, isr, info->scsi.phase);

	if (stat & STAT_INT) {
		if (isr & INST_BUSRESET)
			printk("scsi%d.H: fas216: bus reset detected\n", instance->host_no);
		else if (isr & INST_ILLEGALCMD)
			printk(KERN_CRIT "scsi%d.H: illegal command given\n", instance->host_no);
		else if (isr & INST_DISCONNECT)
			fas216_disconnect_intr(info);
		else if (isr & INST_RESELECTED)		/* reselected			*/
			fas216_reselected_intr(info);
		else if (isr & INST_BUSSERVICE)		/* bus service request		*/
			fas216_busservice_intr(info, stat, ssr);
		else if (isr & INST_FUNCDONE)		/* function done		*/
			fas216_funcdone_intr(info, stat, ssr);
		else
		    	printk("scsi%d.%c: unknown interrupt received:"
				" phase %s isr %02X ssr %02X stat %02X\n",
				instance->host_no, fas216_target(info),
				fas216_drv_phase(info), isr, ssr, stat);
	}
}

/* Function: void fas216_kick(FAS216_Info *info)
 * Purpose : kick a command to the interface - interface should be idle
 * Params  : info - our host interface to kick
 * Notes   : Interrupts are always disabled!
 */
static void fas216_kick(FAS216_Info *info)
{
	Scsi_Cmnd *SCpnt;
	int i, msglen, from_queue = 0;

	fas216_checkmagic(info, "fas216_kick");

	if (info->origSCpnt) {
		SCpnt = info->origSCpnt;
		info->origSCpnt = NULL;
	} else
		SCpnt = NULL;

	/* retrieve next command */
	if (!SCpnt) {
		SCpnt = queue_remove_exclude(&info->queues.issue, info->busyluns);
		from_queue = 1;
	}

	if (!SCpnt) /* no command pending - just exit */
		return;

	if (info->scsi.disconnectable && info->SCpnt) {
		queue_add_cmd_tail(&info->queues.disconnected, info->SCpnt);
		info->scsi.disconnectable = 0;
		info->SCpnt = NULL;
		printk("scsi%d.%c: moved command to disconnected queue\n",
			info->host->host_no, fas216_target(info));
	}

	/*
	 * tagged queuing - allocate a new tag to this command
	 */
	if (SCpnt->device->tagged_queue && SCpnt->cmnd[0] != REQUEST_SENSE) {
		SCpnt->device->current_tag += 1;
		if (SCpnt->device->current_tag == 0)
		    SCpnt->device->current_tag = 1;
		SCpnt->tag = SCpnt->device->current_tag;
	}

	/*
	 * claim host busy
	 */
	info->scsi.phase = PHASE_SELECTION;
	info->SCpnt = SCpnt;
	info->scsi.SCp = SCpnt->SCp;
	info->dma.transfer_type = fasdma_none;

#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: starting cmd %02X",
		info->host->host_no, '0' + SCpnt->target,
		SCpnt->cmnd[0]);
#endif

	if (from_queue) {
#ifdef SCSI2_TAG
		if (SCpnt->device->tagged_queue && SCpnt->cmnd[0] != REQUEST_SENSE) {
		    SCpnt->device->current_tag += 1;
			if (SCpnt->device->current_tag == 0)
			    SCpnt->device->current_tag = 1;
				SCpnt->tag = SCpnt->device->current_tag;
		} else
#endif
			set_bit(SCpnt->target * 8 + SCpnt->lun, info->busyluns);

		info->stats.removes += 1;
		switch (SCpnt->cmnd[0]) {
		case WRITE_6:
		case WRITE_10:
		case WRITE_12:
			info->stats.writes += 1;
			break;
		case READ_6:
		case READ_10:
		case READ_12:
			info->stats.reads += 1;
			break;
		default:
			info->stats.miscs += 1;
			break;
		}
	}

	/* build outgoing message bytes */
	msgqueue_flush(&info->scsi.msgs);
	if (info->device[SCpnt->target].disconnect_ok)
		msgqueue_addmsg(&info->scsi.msgs, 1, IDENTIFY(1, SCpnt->lun));
	else
		msgqueue_addmsg(&info->scsi.msgs, 1, IDENTIFY(0, SCpnt->lun));

	/* add tag message if required */
	if (SCpnt->tag)
		msgqueue_addmsg(&info->scsi.msgs, 2, SIMPLE_QUEUE_TAG, SCpnt->tag);

	/* add synchronous negociation */
	if (SCpnt->cmnd[0] == REQUEST_SENSE &&
	    info->device[SCpnt->target].negstate == syncneg_start) {
		info->device[SCpnt->target].negstate = syncneg_sent;
		msgqueue_addmsg(&info->scsi.msgs, 5,
				EXTENDED_MESSAGE, 3, EXTENDED_SDTR,
				1000 / info->ifcfg.clockrate,
				info->ifcfg.sync_max_depth);
	}

	/* following what the ESP driver says */
	outb(0, REG_STCL(info));
	outb(0, REG_STCM(info));
	outb(0, REG_STCH(info));
	outb(CMD_NOP | CMD_WITHDMA, REG_CMD(info));

	/* flush FIFO */
	outb(CMD_FLUSHFIFO, REG_CMD(info));

	/* load bus-id and timeout */
	outb(BUSID(SCpnt->target), REG_SDID(info));
	outb(info->ifcfg.select_timeout, REG_STIM(info));

	/* synchronous transfers */
	fas216_set_sync(info, SCpnt->target);

	msglen = msgqueue_msglength(&info->scsi.msgs);

	if (msglen == 1 || msglen == 3) {
		/*
		 * We have an easy message length to send...
		 */
		char *msg;

		/* load message bytes */
		while ((msg = msgqueue_getnextmsg(&info->scsi.msgs, &msglen)) != NULL) {
			for (i = 0; i < msglen; i++)
				outb(msg[i], REG_FF(info));
		}

		/* load command */
		for (i = 0; i < SCpnt->cmd_len; i++)
			outb(SCpnt->cmnd[i], REG_FF(info));

		if (msglen == 1)
			outb(CMD_SELECTATN, REG_CMD(info));
		else
			outb(CMD_SELECTATN3, REG_CMD(info));
	} else {
		/*
		 * We have an unusual number of message bytes to send.
		 *  Load first byte into fifo, and issue SELECT with ATN and
		 *  stop steps.
		 * Note: we only peek at t his message - we need the rest
		 * later on!
		 */
		int thismsg;
		char *msg = msgqueue_peeknextmsg(&info->scsi.msgs, &thismsg);

		if (!msg || thismsg < 1)
			printk(KERN_CRIT "scsi%d.%c: no message to send, but %d bytes\n",
				info->host->host_no, fas216_target(info), msglen);
		else
			outb(msg[0], REG_FF(info));

		outb(CMD_SELECTATNSTOP, REG_CMD(info));
	}

#ifdef DEBUG_CONNECT
	printk(", data pointers [%p, %X]\n",
		info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
#endif
	/* should now get either DISCONNECT or (FUNCTION DONE with BUS SERVICE) intr */
}

/* Function: void fas216_done(FAS216_Info *info, unsigned int result)
 * Purpose : complete processing for command
 * Params  : info   - interface that completed
 *	     result - driver byte of result
 */
static void fas216_done(FAS216_Info *info, unsigned int result)
{
	Scsi_Cmnd *SCpnt;

	fas216_checkmagic(info, "fas216_done");

	SCpnt = info->SCpnt;

	if (info->scsi.aborting) {
		printk("scsi%d.%c: uncaught abort - returning DID_ABORT\n",
			info->host->host_no, fas216_target(info));
		result = DID_ABORT;
		info->scsi.aborting = 0;
	}

	info->stats.fins += 1;

	if (SCpnt) {
	    	info->scsi.phase = PHASE_IDLE;
		info->SCpnt = NULL;

		SCpnt->result = result << 16 | info->scsi.SCp.Message << 8 |
				info->scsi.SCp.Status;

		/*
		 * In theory, this should not happen, but just in case it does.
		 */
		if (info->scsi.SCp.ptr && result == DID_OK) {
			switch (SCpnt->cmnd[0]) {
			case INQUIRY:
			case START_STOP:
			case READ_CAPACITY:
				break;

			default:
				switch (status_byte(SCpnt->result)) {
				case CHECK_CONDITION:
				case COMMAND_TERMINATED:
				case BUSY:
				case QUEUE_FULL:
				case RESERVATION_CONFLICT:
					break;

				default:
					printk(KERN_ERR "scsi%d.H: incomplete data transfer "
						"detected: res=%08X ptr=%p len=%X command=",
						info->host->host_no, SCpnt->result,
						info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
					print_command(SCpnt->cmnd);
				}
			}
		}
#ifdef DEBUG_CONNECT
		printk("scsi%d.%c: scsi command (%p) complete, result=%08X\n",
			info->host->host_no, fas216_target(info),
			SCpnt, SCpnt->result);
#endif

		if (!SCpnt->scsi_done)
			panic("scsi%d.H: null scsi_done function in "
				"fas216_done", info->host->host_no);

		clear_bit(SCpnt->target * 8 + SCpnt->lun, info->busyluns);

		SCpnt->scsi_done(SCpnt);
	} else
		panic("scsi%d.H: null command in fas216_done", info->host->host_no);

	if (info->scsi.irq != NO_IRQ)
		fas216_kick(info);
}

/* Function: int fas216_queue_command(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
 * Purpose : queue a command for adapter to process.
 * Params  : SCpnt - Command to queue
 *	     done  - done function to call once command is complete
 * Returns : 0 - success, else error
 */
int fas216_queue_command(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;

	fas216_checkmagic(info, "fas216_queue_command");

#ifdef DEBUG_CONNECT
	printk("scsi%d.%c: received queuable command (%p) %02X\n",
		SCpnt->host->host_no, '0' + SCpnt->target,
		SCpnt, SCpnt->cmnd[0]);
#endif

	SCpnt->scsi_done = done;
	SCpnt->host_scribble = NULL;
	SCpnt->result = 0;
	SCpnt->SCp.Message = 0;
	SCpnt->SCp.Status = 0;

	if (SCpnt->use_sg) {
		unsigned long len = 0;
		int buf;

		SCpnt->SCp.buffer = (struct scatterlist *) SCpnt->buffer;
		SCpnt->SCp.buffers_residual = SCpnt->use_sg - 1;
		SCpnt->SCp.ptr = (char *) SCpnt->SCp.buffer->address;
		SCpnt->SCp.this_residual = SCpnt->SCp.buffer->length;
		/*
		 * Calculate correct buffer length
		 */
		for (buf = 0; buf <= SCpnt->SCp.buffers_residual; buf++)
			len += SCpnt->SCp.buffer[buf].length;
		SCpnt->request_bufflen = len;
	} else {
		SCpnt->SCp.buffer = NULL;
		SCpnt->SCp.buffers_residual = 0;
		SCpnt->SCp.ptr = (unsigned char *)SCpnt->request_buffer;
		SCpnt->SCp.this_residual = SCpnt->request_bufflen;
	}

	info->stats.queues += 1;
	SCpnt->tag = 0;

	if (info->scsi.irq != NO_IRQ) {
		unsigned long flags;

		/* add command into execute queue and let it complete under
		 * the drivers interrupts.
		 */
		if (!queue_add_cmd_ordered(&info->queues.issue, SCpnt)) {
			SCpnt->result = DID_ERROR << 16;
			done(SCpnt);
		}
		save_flags_cli(flags);
		if (!info->SCpnt || info->scsi.disconnectable)
			fas216_kick(info);
		restore_flags(flags);
	} else {
		/* no interrupts to rely on - we'll have to handle the
		 * command ourselves.  For now, we give up.
		 */
		SCpnt->result = DID_ERROR << 16;
		done(SCpnt);
	}
	return 0;
}

/* Function: void fas216_internal_done(Scsi_Cmnd *SCpnt)
 * Purpose : trigger restart of a waiting thread in fas216_command
 * Params  : SCpnt - Command to wake
 */
static void fas216_internal_done(Scsi_Cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;

	fas216_checkmagic(info, "fas216_internal_done");

	info->internal_done = 1;
}

/* Function: int fas216_command(Scsi_Cmnd *SCpnt)
 * Purpose : queue a command for adapter to process.
 * Params  : SCpnt - Command to queue
 * Returns : scsi result code
 */
int fas216_command(Scsi_Cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;
	unsigned long flags;

	fas216_checkmagic(info, "fas216_command");

	info->internal_done = 0;
	fas216_queue_command(SCpnt, fas216_internal_done);

	/*
	 * This wastes time, since we can't return until the command is
	 * complete. We can't seep either since we may get re-entered!
	 * However, we must re-enable interrupts, or else we'll be
	 * waiting forever.
	 */
	save_flags(flags);
	sti();

	while (!info->internal_done)
		barrier();

	restore_flags(flags);

	return SCpnt->result;
}

/* Prototype: void fas216_reportstatus(Scsi_Cmnd **SCpntp1,
 *				       Scsi_Cmnd **SCpntp2, int result)
 * Purpose  : pass a result to *SCpntp1, and check if *SCpntp1 = *SCpntp2
 * Params   : SCpntp1 - pointer to command to return
 *	      SCpntp2 - pointer to command to check
 *	      result  - result to pass back to mid-level done function
 * Returns  : *SCpntp2 = NULL if *SCpntp1 is the same command
 *	      structure as *SCpntp2.
 */
static void fas216_reportstatus(Scsi_Cmnd **SCpntp1, Scsi_Cmnd **SCpntp2,
				int result)
{
	Scsi_Cmnd *SCpnt = *SCpntp1;

	if (SCpnt) {
		*SCpntp1 = NULL;

		SCpnt->result = result;
		SCpnt->scsi_done(SCpnt);
	}

	if (SCpnt == *SCpntp2)
		*SCpntp2 = NULL;
}

/* Function: int fas216_eh_abort(Scsi_Cmnd *SCpnt)
 * Purpose : abort this command
 * Params  : SCpnt - command to abort
 * Returns : FAILED if unable to abort
 */
int fas216_eh_abort(Scsi_Cmnd *SCpnt)
{
	return FAILED;
}

/* Function: int fas216_eh_device_reset(Scsi_Cmnd *SCpnt)
 * Purpose : Reset the device associated with this command
 * Params  : SCpnt - command specifing device to reset
 * Returns : FAILED if unable to reset
 */
int fas216_eh_device_reset(Scsi_Cmnd *SCpnt)
{
	return FAILED;
}

/* Function: int fas216_eh_bus_reset(Scsi_Cmnd *SCpnt)
 * Purpose : Reset the complete bus associated with this command
 * Params  : SCpnt - command specifing bus to reset
 * Returns : FAILED if unable to reset
 */
int fas216_eh_bus_reset(Scsi_Cmnd *SCpnt)
{
	return FAILED;
}

/* Function: int fas216_eh_host_reset(Scsi_Cmnd *SCpnt)
 * Purpose : Reset the host associated with this command
 * Params  : SCpnt - command specifing host to reset
 * Returns : FAILED if unable to reset
 */
int fas216_eh_host_reset(Scsi_Cmnd *SCpnt)
{
	return FAILED;
}

/* Function: int fas216_abort(Scsi_Cmnd *SCpnt)
 * Purpose : abort a command if something horrible happens.
 * Params  : SCpnt - Command that is believed to be causing a problem.
 * Returns : one of SCSI_ABORT_ macros.
 */
int fas216_abort(Scsi_Cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;
	int result = SCSI_ABORT_SNOOZE;

	fas216_checkmagic(info, "fas216_abort");

	info->stats.aborts += 1;

	print_debug_list();
	fas216_dumpstate(info);
	fas216_dumpinfo(info);
	printk(KERN_WARNING "scsi%d: fas216_abort: ", info->host->host_no);

	do {
		/* If command is waiting in the issue queue, then we can
		 * simply remove the command and return abort status
		 */
		if (queue_removecmd(&info->queues.issue, SCpnt)) {
			SCpnt->result = DID_ABORT << 16;
			SCpnt->scsi_done(SCpnt);
			printk("command on issue queue");
			result = SCSI_ABORT_SUCCESS;
			break;
		}

		/* If the command is on the disconencted queue, we need to
		 * reconnect to the device
		 */
		if (queue_cmdonqueue(&info->queues.disconnected, SCpnt))
			printk("command on disconnected queue");

		/* If the command is connected, we need to flag that the
		 * command needs to be aborted
		 */
		if (info->SCpnt == SCpnt)
			printk("command executing");

		/* If the command is pending for execution, then again
		 * this is simple - we remove it and report abort status
		 */
		if (info->origSCpnt == SCpnt) {
			info->origSCpnt = NULL;
			SCpnt->result = DID_ABORT << 16;
			SCpnt->scsi_done(SCpnt);
			printk("command waiting for execution");
			result = SCSI_ABORT_SUCCESS;
			break;
		}
	} while (0);

	printk("\n");

	return result;
}

/* Function: void fas216_reset_state(FAS216_Info *info)
 * Purpose : Initialise driver internal state
 * Params  : info - state to initialise
 */
static void fas216_reset_state(FAS216_Info *info)
{
	syncneg_t negstate;
	int i;

	fas216_checkmagic(info, "fas216_reset_state");

	/*
	 * Clear out all stale info in our state structure
	 */
	memset(info->busyluns, 0, sizeof(info->busyluns));
	msgqueue_flush(&info->scsi.msgs);
	info->scsi.reconnected.target = 0;
	info->scsi.reconnected.lun = 0;
	info->scsi.reconnected.tag = 0;
	info->scsi.disconnectable = 0;
	info->scsi.last_message = 0;
	info->scsi.aborting = 0;
	info->scsi.phase = PHASE_IDLE;
	info->scsi.async_stp = fas216_syncperiod(info, info->ifcfg.asyncperiod);

	if (info->host->dma_channel == NO_DMA || !info->dma.setup)
		negstate = syncneg_invalid;
	else
#ifdef SCSI2_SYNC
		negstate = syncneg_start;
#else
		negstate = syncneg_invalid;
#endif

	for (i = 0; i < 8; i++) {
		info->device[i].disconnect_ok = info->ifcfg.disconnect_ok;
		info->device[i].negstate = negstate;
		info->device[i].period = info->ifcfg.asyncperiod / 4;
		info->device[i].stp = info->scsi.async_stp;
		info->device[i].sof = 0;
	}
}

/* Function: void fas216_init_chip(FAS216_Info *info)
 * Purpose : Initialise FAS216 state after reset
 * Params  : info - state structure for interface
 */
static void fas216_init_chip(FAS216_Info *info)
{
	fas216_checkmagic(info, "fas216_init_chip");

	outb(fas216_clockrate(info->ifcfg.clockrate), REG_CLKF(info));
	outb(info->scsi.cfg[0], REG_CNTL1(info));
	outb(info->scsi.cfg[1], REG_CNTL2(info));
	outb(info->scsi.cfg[2], REG_CNTL3(info));
	outb(info->ifcfg.select_timeout, REG_STIM(info));
	outb(0, REG_SOF(info));
	outb(info->scsi.async_stp, REG_STP(info));
	outb(info->scsi.cfg[0], REG_CNTL1(info));
}

/* Function: int fas216_reset(Scsi_Cmnd *SCpnt, unsigned int reset_flags)
 * Purpose : resets the adapter if something horrible happens.
 * Params  : SCpnt - Command that is believed to be causing a problem.
 *	     reset_flags - flags indicating reset type that is believed
 *           to be required.
 * Returns : one of SCSI_RESET_ macros, or'd with the SCSI_RESET_*_RESET
 *           macros.
 */
int fas216_reset(Scsi_Cmnd *SCpnt, unsigned int reset_flags)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->host->hostdata;
	Scsi_Cmnd *SCptr;
	int result = 0;

	fas216_checkmagic(info, "fas216_reset");

	/*
	 * Validate that command is actually on one of our queues if we're doing
	 * an asynchronous reset
	 */
	if (reset_flags & SCSI_RESET_ASYNCHRONOUS &&
	    SCpnt &&
	    info->SCpnt != SCpnt &&
	    info->origSCpnt != SCpnt &&
	    !queue_cmdonqueue(&info->queues.disconnected, SCpnt) &&
	    !queue_cmdonqueue(&info->queues.issue, SCpnt)) {
		printk("scsi%d: fas216_reset: asynchronous reset for unknown command\n",
		       info->host->host_no);
		return SCSI_RESET_NOT_RUNNING;
	}

	info->stats.resets += 1;

	print_debug_list();
	printk(KERN_WARNING "scsi%d: fas216_reset: ", info->host->host_no);
	if (SCpnt)
		printk(" for target %d ", SCpnt->target);

	outb(info->scsi.cfg[3], REG_CNTL3(info));

	fas216_stoptransfer(info);

	switch (reset_flags & (SCSI_RESET_SUGGEST_BUS_RESET | SCSI_RESET_SUGGEST_HOST_RESET)) {
	case SCSI_RESET_SUGGEST_BUS_RESET:
		outb(CMD_RESETSCSI, REG_CMD(info));
		outb(CMD_NOP, REG_CMD(info));
		result |= SCSI_RESET_BUS_RESET;
		break;

	case SCSI_RESET_SUGGEST_HOST_RESET:
		outb(CMD_RESETCHIP, REG_CMD(info));
		outb(CMD_NOP, REG_CMD(info));
		result |= SCSI_RESET_HOST_RESET;
		break;

	default:
		outb(CMD_RESETCHIP, REG_CMD(info));
		outb(CMD_NOP, REG_CMD(info));
		outb(CMD_RESETSCSI, REG_CMD(info));
		result |= SCSI_RESET_HOST_RESET | SCSI_RESET_BUS_RESET;
		break;
	}

	udelay(300);
	fas216_reset_state(info);
	fas216_init_chip(info);

	/*
	 * Signal all commands in progress have been reset
	 */
	fas216_reportstatus(&info->SCpnt, &SCpnt, DID_RESET << 16);

	while ((SCptr = queue_remove(&info->queues.disconnected)) != NULL)
		fas216_reportstatus(&SCptr, &SCpnt, DID_RESET << 16);

	if (SCpnt) {
		/*
		 * Command not found on disconnected queue, nor currently
		 * executing command - check pending commands
		 */
		if (info->origSCpnt == SCpnt)
			info->origSCpnt = NULL;

		queue_removecmd(&info->queues.issue, SCpnt);

		SCpnt->result = DID_RESET << 16;
		SCpnt->scsi_done(SCpnt);
	}

	printk("\n");

	return result | SCSI_RESET_SUCCESS;
}

/* Function: int fas216_init(struct Scsi_Host *instance)
 * Purpose : initialise FAS/NCR/AMD SCSI ic.
 * Params  : instance - a driver-specific filled-out structure
 * Returns : 0 on success
 */
int fas216_init(struct Scsi_Host *instance)
{
	FAS216_Info *info = (FAS216_Info *)instance->hostdata;
	unsigned long flags;
	int target_jiffies;

	info->magic_start = MAGIC;
	info->magic_end = MAGIC;

	info->host = instance;
	info->scsi.cfg[0] = instance->this_id;
	info->scsi.cfg[1] = CNTL2_ENF | CNTL2_S2FE;
	info->scsi.cfg[2] = info->ifcfg.cntl3 | CNTL3_ADIDCHK | CNTL3_G2CB;
	info->scsi.type = "unknown";
	info->SCpnt = NULL;
	fas216_reset_state(info);

	memset(&info->stats, 0, sizeof(info->stats));

	msgqueue_initialise(&info->scsi.msgs);

	if (!queue_initialise(&info->queues.issue))
		return 1;

	if (!queue_initialise(&info->queues.disconnected)) {
		queue_free(&info->queues.issue);
		return 1;
	}

	outb(CMD_RESETCHIP, REG_CMD(info));

	outb(0, REG_CNTL3(info));
	outb(CNTL2_S2FE, REG_CNTL2(info));

	if ((inb(REG_CNTL2(info)) & (~0xe0)) != CNTL2_S2FE) {
		info->scsi.type = "NCR53C90";
	} else {
		outb(0, REG_CNTL2(info));
		outb(0, REG_CNTL3(info));
		outb(5, REG_CNTL3(info));
		if (inb(REG_CNTL3(info)) != 5) {
			info->scsi.type = "NCR53C90A";
		} else {
			outb(0, REG_CNTL3(info));
			info->scsi.type = "NCR53C9x";
		}
	}


	outb(CNTL3_ADIDCHK, REG_CNTL3(info));
	outb(0, REG_CNTL3(info));

	outb(CMD_RESETCHIP, REG_CMD(info));
	outb(CMD_WITHDMA | CMD_NOP, REG_CMD(info));
	outb(CNTL2_ENF, REG_CNTL2(info));
	outb(CMD_RESETCHIP, REG_CMD(info));
	switch (inb(REG1_ID(info))) {
	case 12:
		info->scsi.type = "Am53CF94";
		break;
	default:
		break;
	}

	udelay(300);
	/* now for the real initialisation */
	fas216_init_chip(info);

	outb(info->scsi.cfg[0] | CNTL1_DISR, REG_CNTL1(info));
	outb(CMD_RESETSCSI, REG_CMD(info));

	/* scsi standard says 250ms */
	target_jiffies = jiffies + (25 * HZ) / 100;
	save_flags(flags);
	sti();

	while (time_before(jiffies, target_jiffies)) barrier();

	restore_flags(flags);

	outb(info->scsi.cfg[0], REG_CNTL1(info));
	inb(REG_INST(info));

	/* now for the real initialisation */
	fas216_init_chip(info);

	fas216_checkmagic(info, "fas216_init");

	return 0;
}

/* Function: int fas216_release(struct Scsi_Host *instance)
 * Purpose : release all resources and put everything to bed for
 *           FAS/NCR/AMD SCSI ic.
 * Params  : instance - a driver-specific filled-out structure
 * Returns : 0 on success
 */
int fas216_release(struct Scsi_Host *instance)
{
	FAS216_Info *info = (FAS216_Info *)instance->hostdata;

	fas216_checkmagic(info, "fas216_release");

	outb(CMD_RESETCHIP, REG_CMD(info));
	queue_free(&info->queues.disconnected);
	queue_free(&info->queues.issue);

	return 0;
}

EXPORT_SYMBOL(fas216_init);
EXPORT_SYMBOL(fas216_abort);
EXPORT_SYMBOL(fas216_reset);
EXPORT_SYMBOL(fas216_queue_command);
EXPORT_SYMBOL(fas216_command);
EXPORT_SYMBOL(fas216_intr);
EXPORT_SYMBOL(fas216_release);
EXPORT_SYMBOL(fas216_eh_abort);
EXPORT_SYMBOL(fas216_eh_device_reset);
EXPORT_SYMBOL(fas216_eh_bus_reset);
EXPORT_SYMBOL(fas216_eh_host_reset);


#ifdef MODULE
int init_module(void)
{
	return 0;
}

void cleanup_module(void)
{
}
#endif
