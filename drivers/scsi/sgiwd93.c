/*
 * sgiwd93.c: SGI WD93 scsi driver.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * (In all truth, Jed Schimmel wrote all this code.)
 *
 * $Id: sgiwd93.c,v 1.1 1998/05/01 01:35:42 ralf Exp $
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blk.h>
#include <linux/version.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sgi.h>
#include <asm/sgialib.h>
#include <asm/sgimc.h>
#include <asm/sgihpc.h>
#include <asm/sgint23.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "scsi.h"
#include "hosts.h"
#include "wd33c93.h"
#include "sgiwd93.h"

#include <linux/stat.h>

struct hpc_chunk {
	struct hpc_dma_desc desc;
	unsigned long padding;
};

struct proc_dir_entry proc_scsi_sgiwd93 = {
	PROC_SCSI_SGIWD93, 5, "SGIWD93",
	S_IFDIR | S_IRUGO | S_IXUGO, 2
};

struct Scsi_Host *sgiwd93_host = NULL;

/* Wuff wuff, wuff, wd33c93.c, wuff wuff, object oriented, bow wow. */
static inline void write_wd33c93_count(wd33c93_regs *regp, unsigned long value)
{
	regp->SASR = WD_TRANSFER_COUNT_MSB;
	regp->SCMD = ((value >> 16) & 0xff);
	regp->SCMD = ((value >>  8) & 0xff);
	regp->SCMD = ((value >>  0) & 0xff);
}

static inline unsigned long read_wd33c93_count(wd33c93_regs *regp)
{
	unsigned long value;

	regp->SASR = WD_TRANSFER_COUNT_MSB;
	value =  ((regp->SCMD & 0xff) << 16);
	value |= ((regp->SCMD & 0xff) <<  8);
	value |= ((regp->SCMD & 0xff) <<  0);
	return value;
}

/* XXX woof! */
static void sgiwd93_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	wd33c93_intr(sgiwd93_host);
}

#undef DEBUG_DMA

static int dma_setup(Scsi_Cmnd *cmd, int datainp)
{
	struct WD33C93_hostdata *hdata = CMDHOSTDATA(cmd);
	wd33c93_regs *regp = hdata->regp;
	struct hpc3_scsiregs *hregs = (struct hpc3_scsiregs *) cmd->host->base;
	struct hpc_chunk *hcp = (struct hpc_chunk *) hdata->dma_bounce_buffer;

#ifdef DEBUG_DMA
	printk("dma_setup: datainp<%d> hcp<%p> ",
	       datainp, hcp);
#endif

	hdata->dma_dir = datainp;

	if(cmd->use_sg) {
		struct scatterlist *slp = cmd->SCp.buffer;
		int i, totlen = 0;

#ifdef DEBUG_DMA
		printk("SCLIST<");
#endif
		for(i = 0; i <= (cmd->use_sg - 1); i++, hcp++) {
#ifdef DEBUG_DMA
			printk("[%p,%d]", slp[i].address, slp[i].length);
#endif
			dma_cache_wback_inv((unsigned long)slp[i].address,
			                    PAGE_SIZE);
			hcp->desc.pbuf = PHYSADDR(slp[i].address);
			hcp->desc.cntinfo = (slp[i].length & HPCDMA_BCNT);
			totlen += slp[i].length;
		}
#ifdef DEBUG_DMA
		printk(">tlen<%d>", totlen);
#endif
		hdata->dma_bounce_len = totlen; /* a trick... */
		write_wd33c93_count(regp, totlen);
	} else {
		/* Non-scattered dma. */
#ifdef DEBUG_DMA
		printk("ONEBUF<%p,%d>", cmd->SCp.ptr, cmd->SCp.this_residual);
#endif
		dma_cache_wback_inv((unsigned long)cmd->SCp.ptr, PAGE_SIZE);
		hcp->desc.pbuf = PHYSADDR(cmd->SCp.ptr);
		hcp->desc.cntinfo = (cmd->SCp.this_residual & HPCDMA_BCNT);
		hcp++;
		write_wd33c93_count(regp, cmd->SCp.this_residual);
	}

	/* To make sure, if we trip an HPC bug, that we transfer
	 * every single byte, we tag on an extra zero length dma
	 * descriptor at the end of the chain.
	 */
	hcp->desc.pbuf = 0;
	hcp->desc.cntinfo = (HPCDMA_EOX);

#ifdef DEBUG_DMA
	printk(" HPCGO\n");
#endif

	/* Start up the HPC. */
	hregs->ndptr = PHYSADDR(hdata->dma_bounce_buffer);
	if(datainp)
		hregs->ctrl = (HPC3_SCTRL_ACTIVE);
	else
		hregs->ctrl = (HPC3_SCTRL_ACTIVE | HPC3_SCTRL_DIR);
	return 0;
}

static void dma_stop(struct Scsi_Host *instance, Scsi_Cmnd *SCpnt,
		     int status)
{
	struct WD33C93_hostdata *hdata = INSTHOSTDATA(instance);
	wd33c93_regs *regp = hdata->regp;
	struct hpc3_scsiregs *hregs = (struct hpc3_scsiregs *) SCpnt->host->base;

#ifdef DEBUG_DMA
	printk("dma_stop: status<%d> ", status);
#endif

	/* First stop the HPC and flush it's FIFO. */
	if(hdata->dma_dir) {
		hregs->ctrl |= HPC3_SCTRL_FLUSH;
		while(hregs->ctrl & HPC3_SCTRL_ACTIVE)
			barrier();
	}
	hregs->ctrl = 0;

	/* See how far we got and update scatterlist state if necessary. */
	if(SCpnt->use_sg) {
		struct scatterlist *slp = SCpnt->SCp.buffer;
		int totlen, wd93_residual, transferred, i;

		/* Yep, we were doing the scatterlist thang. */
		totlen = hdata->dma_bounce_len;
		wd93_residual = read_wd33c93_count(regp);
		transferred = totlen - wd93_residual;

#ifdef DEBUG_DMA
		printk("tlen<%d>resid<%d>transf<%d> ",
		       totlen, wd93_residual, transferred);
#endif

		/* Avoid long winded partial-transfer search for common case. */
		if(transferred != totlen) {
			/* This is the nut case. */
#ifdef DEBUG_DMA
			printk("Jed was here...");
#endif
			for(i = 0; i <= (SCpnt->use_sg - 1); i++) {
				if(slp[i].length >= transferred)
					break;
				transferred -= slp[i].length;
			}
		} else {
			/* This is the common case. */
#ifdef DEBUG_DMA
			printk("did it all...");
#endif
			i = (SCpnt->use_sg - 1);
		}
		SCpnt->SCp.buffer = &slp[i];
		SCpnt->SCp.buffers_residual = (SCpnt->use_sg - 1 - i);
		SCpnt->SCp.ptr = (char *) slp[i].address;
		SCpnt->SCp.this_residual = slp[i].length;
	}
#ifdef DEBUG_DMA
	printk("\n");
#endif
}

static inline void init_hpc_chain(uchar *buf)
{
	struct hpc_chunk *hcp = (struct hpc_chunk *) buf;
	unsigned long start, end;

	start = (unsigned long) buf;
	end = start + PAGE_SIZE;
	while(start < end) {
		hcp->desc.pnext = PHYSADDR((hcp + 1));
		hcp->desc.cntinfo = HPCDMA_EOX;
		hcp++;
		start += sizeof(struct hpc_chunk);
	};
	hcp--;
	hcp->desc.pnext = PHYSADDR(buf);
}

__initfunc(int sgiwd93_detect(Scsi_Host_Template *HPsUX))
{
	static unsigned char called = 0;
	struct hpc3_scsiregs *hregs = &hpc3c0->scsi_chan0;
	struct WD33C93_hostdata *hdata;
	uchar *buf;

	if(called)
		return 0; /* Should bitch on the console about this... */

	HPsUX->proc_dir = &proc_scsi_sgiwd93;

	sgiwd93_host = scsi_register(HPsUX, sizeof(struct WD33C93_hostdata));
	sgiwd93_host->base = (unsigned char *) hregs;

	buf = (uchar *) get_free_page(GFP_KERNEL);
	init_hpc_chain(buf);
	dma_cache_wback_inv((unsigned long) buf, PAGE_SIZE);

	wd33c93_init(sgiwd93_host, (wd33c93_regs *) 0xbfbc0003,
		     dma_setup, dma_stop, WD33C93_FS_16_20);

	hdata = INSTHOSTDATA(sgiwd93_host);
	hdata->no_sync = 0;
	hdata->dma_bounce_buffer = (uchar *) (KSEG1ADDR(buf));
	dma_cache_wback_inv((unsigned long) buf, PAGE_SIZE);

	request_irq(1, sgiwd93_intr, 0, "SGI WD93", (void *) sgiwd93_host);
	called = 1;

	return 1; /* Found one. */
}

#ifdef MODULE

#define HOSTS_C

#include "sgiwd93.h"

Scsi_Host_Template driver_template = SGIWD93_SCSI;

#include "scsi_module.c"

#endif

int sgiwd93_release(struct Scsi_Host *instance)
{
#ifdef MODULE
	free_irq(1, sgiwd93_intr);
	free_page(KSEG0ADDR(hdata->dma_bounce_buffer));
	wd33c93_release();
#endif
	return 1;
}
