/*
 * $Id: cx88-mpeg.c,v 1.14 2004/10/25 11:26:36 kraxel Exp $
 *
 *  Support for the mpeg transport stream transfers
 *  PCI function #2 of the cx2388x.
 *
 *    (c) 2004 Jelle Foks <jelle@foks.8m.com>
 *    (c) 2004 Chris Pascoe <c.pascoe@itee.uq.edu.au>
 *    (c) 2004 Gerd Knorr <kraxel@bytesex.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <asm/delay.h>

#include "cx88.h"

/* ------------------------------------------------------------------ */

MODULE_DESCRIPTION("mpeg driver for cx2388x based TV cards");
MODULE_AUTHOR("Jelle Foks <jelle@foks.8m.com>");
MODULE_AUTHOR("Chris Pascoe <c.pascoe@itee.uq.edu.au>");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int debug = 0;
module_param(debug,int,0644);
MODULE_PARM_DESC(debug,"enable debug messages [mpeg]");

#define dprintk(level,fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "%s/2: " fmt, dev->core->name , ## arg)

/* ------------------------------------------------------------------ */

static int cx8802_start_dma(struct cx8802_dev    *dev,
			    struct cx88_dmaqueue *q,
			    struct cx88_buffer   *buf)
{
	struct cx88_core *core = dev->core;

	dprintk(1, "cx8802_start_mpegport_dma %d\n", buf->vb.width);

	/* setup fifo + format */
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH28],
				dev->ts_packet_size, buf->risc.dma);

	/* write TS length to chip */
	cx_write(MO_TS_LNGTH, buf->vb.width);

#if 1
	/* FIXME: this needs a review.
	 * also: move to cx88-blackbird + cx88-dvb source files? */

	if (cx88_boards[core->board].dvb) {
		/* Setup TS portion of chip */
		cx_write(TS_GEN_CNTRL, 0x0c);
	}

	if (cx88_boards[core->board].blackbird) {
		cx_write(MO_PINMUX_IO, 0x88); /* enable MPEG parallel IO */

		// cx_write(TS_F2_CMD_STAT_MM, 0x2900106); /* F2_CMD_STAT_MM defaults + master + memory space */
		cx_write(TS_GEN_CNTRL, 0x46); /* punctured clock TS & posedge driven & software reset */
		udelay(100);

		cx_write(TS_HW_SOP_CNTRL, 0x408); /* mpeg start byte */
		//cx_write(TS_HW_SOP_CNTRL, 0x2F0BC0); /* mpeg start byte ts: 0x2F0BC0 ? */
		cx_write(TS_VALERR_CNTRL, 0x2000);

		cx_write(TS_GEN_CNTRL, 0x06); /* punctured clock TS & posedge driven */
		udelay(100);
	}
#endif

	/* reset counter */
	cx_write(MO_TS_GPCNTRL, GP_COUNT_CONTROL_RESET);
	q->count = 1;

	/* enable irqs */
	cx_set(MO_PCI_INTMSK, 0x00fc04);
	cx_write(MO_TS_INTMSK,  0x1f0011);

	/* start dma */
	cx_write(MO_DEV_CNTRL2, (1<<5)); /* FIXME: s/write/set/ ??? */
	cx_write(MO_TS_DMACNTRL, 0x11);
	return 0;
}

static int cx8802_stop_dma(struct cx8802_dev *dev)
{
	struct cx88_core *core = dev->core;

	/* stop dma */
	cx_clear(MO_TS_DMACNTRL, 0x11);

	/* disable irqs */
	cx_clear(MO_PCI_INTMSK, 0x000004);
	cx_clear(MO_TS_INTMSK, 0x1f0011);

	/* Reset the controller */
	cx_write(TS_GEN_CNTRL, 0xcd);
	return 0;
}

static int cx8802_restart_queue(struct cx8802_dev    *dev,
				struct cx88_dmaqueue *q)
{
	struct cx88_buffer *buf;
	struct list_head *item;

	if (list_empty(&q->active))
		return 0;

	buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
	dprintk(2,"restart_queue [%p/%d]: restart dma\n",
		buf, buf->vb.i);
	cx8802_start_dma(dev, q, buf);
	list_for_each(item,&q->active) {
		buf = list_entry(item, struct cx88_buffer, vb.queue);
		buf->count = q->count++;
	}
	mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

/* ------------------------------------------------------------------ */

int cx8802_buf_prepare(struct cx8802_dev *dev, struct cx88_buffer *buf)
{
	int size = dev->ts_packet_size * dev->ts_packet_count;
	int rc;

	dprintk(1, "%s: %p\n", __FUNCTION__, buf);
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < size)
		return -EINVAL;

	if (STATE_NEEDS_INIT == buf->vb.state) {
		buf->vb.width  = dev->ts_packet_size;
		buf->vb.height = dev->ts_packet_count;
		buf->vb.size   = size;
		buf->vb.field  = V4L2_FIELD_TOP;

		if (0 != (rc = videobuf_iolock(dev->pci,&buf->vb,NULL)))
			goto fail;
		cx88_risc_databuffer(dev->pci, &buf->risc,
				     buf->vb.dma.sglist,
				     buf->vb.width, buf->vb.height);
	}
	buf->vb.state = STATE_PREPARED;
	return 0;

 fail:
	cx88_free_buffer(dev->pci,buf);
	return rc;
}

void cx8802_buf_queue(struct cx8802_dev *dev, struct cx88_buffer *buf)
{
	struct cx88_buffer    *prev;
	struct cx88_dmaqueue  *q    = &dev->mpegq;

	/* add jump to stopper */
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(q->stopper.dma);

	if (list_empty(&q->active)) {
		list_add_tail(&buf->vb.queue,&q->active);
		cx8802_start_dma(dev, q, buf);
		buf->vb.state = STATE_ACTIVE;
		buf->count    = q->count++;
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
		dprintk(2,"[%p/%d] %s - first active\n",
			buf, buf->vb.i, __FUNCTION__);

	} else {
		prev = list_entry(q->active.prev, struct cx88_buffer, vb.queue);
		list_add_tail(&buf->vb.queue,&q->active);
		buf->vb.state = STATE_ACTIVE;
		buf->count    = q->count++;
		prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
		dprintk(2,"[%p/%d] %s - append to active\n",
			buf, buf->vb.i, __FUNCTION__);
	}
}

/* ----------------------------------------------------------- */

static void do_cancel_buffers(struct cx8802_dev *dev, char *reason, int restart)
{
	struct cx88_dmaqueue *q = &dev->mpegq;
	struct cx88_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&dev->slock,flags);
	while (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		list_del(&buf->vb.queue);
		buf->vb.state = STATE_ERROR;
		wake_up(&buf->vb.done);
		dprintk(1,"[%p/%d] %s - dma=0x%08lx\n",
			buf, buf->vb.i, reason, (unsigned long)buf->risc.dma);
	}
	if (restart)
		cx8802_restart_queue(dev,q);
	spin_unlock_irqrestore(&dev->slock,flags);
}

void cx8802_cancel_buffers(struct cx8802_dev *dev)
{
	struct cx88_dmaqueue *q = &dev->mpegq;

	del_timer_sync(&q->timeout);
	cx8802_stop_dma(dev);
	do_cancel_buffers(dev,"cancel",0);
}

static void cx8802_timeout(unsigned long data)
{
	struct cx8802_dev *dev = (struct cx8802_dev*)data;

	dprintk(1, "%s\n",__FUNCTION__);

	if (debug)
		cx88_sram_channel_dump(dev->core, &cx88_sram_channels[SRAM_CH28]);
	cx8802_stop_dma(dev);
	do_cancel_buffers(dev,"timeout",1);
}

static void cx8802_mpeg_irq(struct cx8802_dev *dev)
{
	struct cx88_core *core = dev->core;
	u32 status, mask, count;

	status = cx_read(MO_TS_INTSTAT);
	mask   = cx_read(MO_TS_INTMSK);
	if (0 == (status & mask))
		return;

	cx_write(MO_TS_INTSTAT, status);
	if (debug || (status & mask & ~0xff))
		cx88_print_irqbits(core->name, "irq mpeg ",
				   cx88_mpeg_irqs, status, mask);

	/* risc op code error */
	if (status & (1 << 16)) {
		printk(KERN_WARNING "%s: mpeg risc op code error\n",core->name);
		cx_clear(MO_TS_DMACNTRL, 0x11);
		cx88_sram_channel_dump(dev->core, &cx88_sram_channels[SRAM_CH28]);
	}

	/* risc1 y */
	if (status & 0x01) {
		spin_lock(&dev->slock);
		count = cx_read(MO_TS_GPCNT);
		cx88_wakeup(dev->core, &dev->mpegq, count);
		spin_unlock(&dev->slock);
	}

	/* risc2 y */
	if (status & 0x10) {
		spin_lock(&dev->slock);
		cx8802_restart_queue(dev,&dev->mpegq);
		spin_unlock(&dev->slock);
	}

        /* other general errors */
        if (status & 0x1f0100) {
                spin_lock(&dev->slock);
		cx8802_stop_dma(dev);
                cx8802_restart_queue(dev,&dev->mpegq);
                spin_unlock(&dev->slock);
        }
}

static irqreturn_t cx8802_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct cx8802_dev *dev = dev_id;
	struct cx88_core *core = dev->core;
	u32 status, mask;
	int loop, handled = 0;

	for (loop = 0; loop < 10; loop++) {
		status = cx_read(MO_PCI_INTSTAT) & (~0x1f | 0x04);
		mask   = cx_read(MO_PCI_INTMSK);
		if (0 == (status & mask))
			goto out;
		handled = 1;
		cx_write(MO_PCI_INTSTAT, status);

		if (status & mask & ~0x1f)
			cx88_irq(core,status,mask);
		if (status & 0x04)
			cx8802_mpeg_irq(dev);
	};
	if (10 == loop) {
		printk(KERN_WARNING "%s/0: irq loop -- clearing mask\n",
		       core->name);
		cx_write(MO_PCI_INTMSK,0);
	}

 out:
	return IRQ_RETVAL(handled);
}

/* ----------------------------------------------------------- */
/* exported stuff                                              */

int cx8802_init_common(struct cx8802_dev *dev)
{
	int err;

	/* pci init */
	if (pci_enable_device(dev->pci))
		return -EIO;
	pci_set_master(dev->pci);
	if (!pci_dma_supported(dev->pci,0xffffffff)) {
		printk("%s/2: Oops: no 32bit PCI DMA ???\n",dev->core->name);
		return -EIO;
	}

	pci_read_config_byte(dev->pci, PCI_CLASS_REVISION, &dev->pci_rev);
        pci_read_config_byte(dev->pci, PCI_LATENCY_TIMER,  &dev->pci_lat);
        printk(KERN_INFO "%s/2: found at %s, rev: %d, irq: %d, "
	       "latency: %d, mmio: 0x%lx\n", dev->core->name,
	       pci_name(dev->pci), dev->pci_rev, dev->pci->irq,
	       dev->pci_lat,pci_resource_start(dev->pci,0));

	/* initialize driver struct */
        init_MUTEX(&dev->lock);
	spin_lock_init(&dev->slock);

	/* init dma queue */
	INIT_LIST_HEAD(&dev->mpegq.active);
	INIT_LIST_HEAD(&dev->mpegq.queued);
	dev->mpegq.timeout.function = cx8802_timeout;
	dev->mpegq.timeout.data     = (unsigned long)dev;
	init_timer(&dev->mpegq.timeout);
	cx88_risc_stopper(dev->pci,&dev->mpegq.stopper,
			  MO_TS_DMACNTRL,0x11,0x00);

#if 0 /* FIXME */
	/* initialize hardware */
	cx8802_reset(dev);
#endif

	/* get irq */
	err = request_irq(dev->pci->irq, cx8802_irq,
			  SA_SHIRQ | SA_INTERRUPT, dev->core->name, dev);
	if (err < 0) {
		printk(KERN_ERR "%s: can't get IRQ %d\n",
		       dev->core->name, dev->pci->irq);
		return err;
	}

#if 0 /* FIXME */
	/* register i2c bus + load i2c helpers */
	cx88_card_setup(dev);
#endif

	/* everything worked */
	pci_set_drvdata(dev->pci,dev);
	return 0;
}

void cx8802_fini_common(struct cx8802_dev *dev)
{
	cx8802_stop_dma(dev);
	pci_disable_device(dev->pci);

	/* unregister stuff */
	free_irq(dev->pci->irq, dev);
	pci_set_drvdata(dev->pci, NULL);

	/* free memory */
	btcx_riscmem_free(dev->pci,&dev->mpegq.stopper);
}

/* ----------------------------------------------------------- */

int cx8802_suspend_common(struct pci_dev *pci_dev, u32 state)
{
        struct cx8802_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;

	/* stop mpeg dma */
	spin_lock(&dev->slock);
	if (!list_empty(&dev->mpegq.active)) {
		printk("%s: suspend mpeg\n", core->name);
		cx8802_stop_dma(dev);
		del_timer(&dev->mpegq.timeout);
	}
	spin_unlock(&dev->slock);

#if 1
	/* FIXME -- shutdown device */
	cx88_shutdown(dev->core);
#endif

	pci_save_state(pci_dev);
	if (0 != pci_set_power_state(pci_dev, state)) {
		pci_disable_device(pci_dev);
		dev->state.disabled = 1;
	}
	return 0;
}

int cx8802_resume_common(struct pci_dev *pci_dev)
{
        struct cx8802_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;

	if (dev->state.disabled) {
		pci_enable_device(pci_dev);
		dev->state.disabled = 0;
	}
	pci_set_power_state(pci_dev, 0);
	pci_restore_state(pci_dev);

#if 1
	/* FIXME: re-initialize hardware */
	cx88_reset(dev->core);
#endif

	/* restart video+vbi capture */
	spin_lock(&dev->slock);
	if (!list_empty(&dev->mpegq.active)) {
		printk("%s: resume mpeg\n", core->name);
		cx8802_restart_queue(dev,&dev->mpegq);
	}
	spin_unlock(&dev->slock);

	return 0;
}

/* ----------------------------------------------------------- */

EXPORT_SYMBOL(cx8802_buf_prepare);
EXPORT_SYMBOL(cx8802_buf_queue);
EXPORT_SYMBOL(cx8802_cancel_buffers);

EXPORT_SYMBOL(cx8802_init_common);
EXPORT_SYMBOL(cx8802_fini_common);

EXPORT_SYMBOL(cx8802_suspend_common);
EXPORT_SYMBOL(cx8802_resume_common);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
