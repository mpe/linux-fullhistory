/*
 * $Id: cx88-video.c,v 1.46 2004/11/07 14:44:59 kraxel Exp $
 *
 * device driver for Conexant 2388x based TV cards
 * video4linux video interface
 *
 * (c) 2003-04 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
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

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <asm/div64.h>

#include "cx88.h"

MODULE_DESCRIPTION("v4l2 driver module for cx2388x based TV cards");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------------ */

static unsigned int video_nr[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
static unsigned int vbi_nr[]   = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
static unsigned int radio_nr[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };

module_param_array(video_nr, int, NULL, 0444);
module_param_array(vbi_nr,   int, NULL, 0444);
module_param_array(radio_nr, int, NULL, 0444);

MODULE_PARM_DESC(video_nr,"video device numbers");
MODULE_PARM_DESC(vbi_nr,"vbi device numbers");
MODULE_PARM_DESC(radio_nr,"radio device numbers");

static unsigned int video_debug = 0;
module_param(video_debug,int,0644);
MODULE_PARM_DESC(video_debug,"enable debug messages [video]");

static unsigned int irq_debug = 0;
module_param(irq_debug,int,0644);
MODULE_PARM_DESC(irq_debug,"enable debug messages [IRQ handler]");

static unsigned int vid_limit = 16;
module_param(vid_limit,int,0644);
MODULE_PARM_DESC(vid_limit,"capture memory limit in megabytes");

#define dprintk(level,fmt, arg...)	if (video_debug >= level) \
	printk(KERN_DEBUG "%s/0: " fmt, dev->core->name , ## arg)

/* ------------------------------------------------------------------ */

static LIST_HEAD(cx8800_devlist);

/* ------------------------------------------------------------------- */
/* static data                                                         */

static struct cx88_tvnorm tvnorms[] = {
	{
		.name      = "NTSC-M",
		.id        = V4L2_STD_NTSC_M,
		.cxiformat = VideoFormatNTSC,
		.cxoformat = 0x181f0008,
	},{
		.name      = "NTSC-JP",
		.id        = V4L2_STD_NTSC_M_JP,
		.cxiformat = VideoFormatNTSCJapan,
		.cxoformat = 0x181f0008,
#if 0
	},{
		.name      = "NTSC-4.43",
		.id        = FIXME,
		.cxiformat = VideoFormatNTSC443,
		.cxoformat = 0x181f0008,
#endif
	},{
		.name      = "PAL-BG",
		.id        = V4L2_STD_PAL_BG,
		.cxiformat = VideoFormatPAL,
		.cxoformat = 0x181f0008,
	},{
		.name      = "PAL-DK",
		.id        = V4L2_STD_PAL_DK,
		.cxiformat = VideoFormatPAL,
		.cxoformat = 0x181f0008,
	},{
		.name      = "PAL-I",
		.id        = V4L2_STD_PAL_I,
		.cxiformat = VideoFormatPAL,
		.cxoformat = 0x181f0008,
        },{
		.name      = "PAL-M",
		.id        = V4L2_STD_PAL_M,
		.cxiformat = VideoFormatPALM,
		.cxoformat = 0x1c1f0008,
	},{
		.name      = "PAL-N",
		.id        = V4L2_STD_PAL_N,
		.cxiformat = VideoFormatPALN,
		.cxoformat = 0x1c1f0008,
	},{
		.name      = "PAL-Nc",
		.id        = V4L2_STD_PAL_Nc,
		.cxiformat = VideoFormatPALNC,
		.cxoformat = 0x1c1f0008,
	},{
		.name      = "PAL-60",
		.id        = V4L2_STD_PAL_60,
		.cxiformat = VideoFormatPAL60,
		.cxoformat = 0x181f0008,
	},{
		.name      = "SECAM-L",
		.id        = V4L2_STD_SECAM_L,
		.cxiformat = VideoFormatSECAM,
		.cxoformat = 0x181f0008,
	},{
		.name      = "SECAM-DK",
		.id        = V4L2_STD_SECAM_DK,
		.cxiformat = VideoFormatSECAM,
		.cxoformat = 0x181f0008,
	}
};

static struct cx8800_fmt formats[] = {
	{
		.name     = "8 bpp, gray",
		.fourcc   = V4L2_PIX_FMT_GREY,
		.cxformat = ColorFormatY8,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "15 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB555,
		.cxformat = ColorFormatRGB15,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "15 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB555X,
		.cxformat = ColorFormatRGB15 | ColorFormatBSWAP,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "16 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB565,
		.cxformat = ColorFormatRGB16,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "16 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB565X,
		.cxformat = ColorFormatRGB16 | ColorFormatBSWAP,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "24 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR24,
		.cxformat = ColorFormatRGB24,
		.depth    = 24,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "32 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR32,
		.cxformat = ColorFormatRGB32,
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "32 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB32,
		.cxformat = ColorFormatRGB32 | ColorFormatBSWAP | ColorFormatWSWAP,
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, packed, YUYV",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.cxformat = ColorFormatYUY2,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.cxformat = ColorFormatYUY2 | ColorFormatBSWAP,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},
};

static struct cx8800_fmt* format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].fourcc == fourcc)
			return formats+i;
	return NULL;
}

/* ------------------------------------------------------------------- */

static const struct v4l2_queryctrl no_ctl = {
	.name  = "42",
	.flags = V4L2_CTRL_FLAG_DISABLED,
};

static struct cx88_ctrl cx8800_ctls[] = {
	/* --- video --- */
	{
		.v = {
			.id            = V4L2_CID_BRIGHTNESS,
			.name          = "Brightness",
			.minimum       = 0x00,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 128,
		.reg                   = MO_CONTR_BRIGHT,
		.mask                  = 0x00ff,
		.shift                 = 0,
	},{
		.v = {
			.id            = V4L2_CID_CONTRAST,
			.name          = "Contrast",
			.minimum       = 0,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.reg                   = MO_CONTR_BRIGHT,
		.mask                  = 0xff00,
		.shift                 = 8,
	},{
		.v = {
			.id            = V4L2_CID_HUE,
			.name          = "Hue",
			.minimum       = 0,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 0,
		.reg                   = MO_HUE,
		.mask                  = 0x00ff,
		.shift                 = 0,
	},{
		/* strictly, this only describes only U saturation.
		 * V saturation is handled specially through code.
		 */
		.v = {
			.id            = V4L2_CID_SATURATION,
			.name          = "Saturation",
			.minimum       = 0,
			.maximum       = 0xff,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.off                   = 0,
		.reg                   = MO_UV_SATURATION,
		.mask                  = 0x00ff,
		.shift                 = 0,
	},{
	/* --- audio --- */
		.v = {
			.id            = V4L2_CID_AUDIO_MUTE,
			.name          = "Mute",
			.minimum       = 0,
			.maximum       = 1,
			.type          = V4L2_CTRL_TYPE_BOOLEAN,
		},
		.reg                   = AUD_VOL_CTL,
		.sreg                  = SHADOW_AUD_VOL_CTL,
		.mask                  = (1 << 6),
		.shift                 = 6,
	},{
		.v = {
			.id            = V4L2_CID_AUDIO_VOLUME,
			.name          = "Volume",
			.minimum       = 0,
			.maximum       = 0x3f,
			.step          = 1,
			.default_value = 0,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.reg                   = AUD_VOL_CTL,
		.sreg                  = SHADOW_AUD_VOL_CTL,
		.mask                  = 0x3f,
		.shift                 = 0,
	},{
		.v = {
			.id            = V4L2_CID_AUDIO_BALANCE,
			.name          = "Balance",
			.minimum       = 0,
			.maximum       = 0x7f,
			.step          = 1,
			.default_value = 0x40,
			.type          = V4L2_CTRL_TYPE_INTEGER,
		},
		.reg                   = AUD_BAL_CTL,
		.sreg                  = SHADOW_AUD_BAL_CTL,
		.mask                  = 0x7f,
		.shift                 = 0,
	}
};
const int CX8800_CTLS = ARRAY_SIZE(cx8800_ctls);

/* ------------------------------------------------------------------- */
/* resource management                                                 */

static int res_get(struct cx8800_dev *dev, struct cx8800_fh *fh, unsigned int bit)
{
	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	down(&dev->lock);
	if (dev->resources & bit) {
		/* no, someone else uses it */
		up(&dev->lock);
		return 0;
	}
	/* it's free, grab it */
	fh->resources  |= bit;
	dev->resources |= bit;
	dprintk(1,"res: get %d\n",bit);
	up(&dev->lock);
	return 1;
}

static
int res_check(struct cx8800_fh *fh, unsigned int bit)
{
	return (fh->resources & bit);
}

static
int res_locked(struct cx8800_dev *dev, unsigned int bit)
{
	return (dev->resources & bit);
}

static
void res_free(struct cx8800_dev *dev, struct cx8800_fh *fh, unsigned int bits)
{
	if ((fh->resources & bits) != bits)
		BUG();

	down(&dev->lock);
	fh->resources  &= ~bits;
	dev->resources &= ~bits;
	dprintk(1,"res: put %d\n",bits);
	up(&dev->lock);
}

/* ------------------------------------------------------------------ */

static int video_mux(struct cx8800_dev *dev, unsigned int input)
{
	struct cx88_core *core = dev->core;

	dprintk(1,"video_mux: %d [vmux=%d,gpio=0x%x,0x%x,0x%x,0x%x]\n",
		input, INPUT(input)->vmux,
		INPUT(input)->gpio0,INPUT(input)->gpio1,
		INPUT(input)->gpio2,INPUT(input)->gpio3);
	dev->core->input = input;
	cx_andor(MO_INPUT_FORMAT, 0x03 << 14, INPUT(input)->vmux << 14);
	cx_write(MO_GP3_IO, INPUT(input)->gpio3);
	cx_write(MO_GP0_IO, INPUT(input)->gpio0);
	cx_write(MO_GP1_IO, INPUT(input)->gpio1);
	cx_write(MO_GP2_IO, INPUT(input)->gpio2);

	switch (INPUT(input)->type) {
	case CX88_VMUX_SVIDEO:
		cx_set(MO_AFECFG_IO,    0x00000001);
		cx_set(MO_INPUT_FORMAT, 0x00010010);
		cx_set(MO_FILTER_EVEN,  0x00002020);
		cx_set(MO_FILTER_ODD,   0x00002020);
		break;
	default:
		cx_clear(MO_AFECFG_IO,    0x00000001);
		cx_clear(MO_INPUT_FORMAT, 0x00010010);
		cx_clear(MO_FILTER_EVEN,  0x00002020);
		cx_clear(MO_FILTER_ODD,   0x00002020);
		break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */

static int start_video_dma(struct cx8800_dev    *dev,
			   struct cx88_dmaqueue *q,
			   struct cx88_buffer   *buf)
{
	struct cx88_core *core = dev->core;

	/* setup fifo + format */
	cx88_sram_channel_setup(dev->core, &cx88_sram_channels[SRAM_CH21],
				buf->bpl, buf->risc.dma);
	cx88_set_scale(dev->core, buf->vb.width, buf->vb.height, buf->vb.field);
	cx_write(MO_COLOR_CTRL, buf->fmt->cxformat | ColorFormatGamma);

	/* reset counter */
	cx_write(MO_VIDY_GPCNTRL,GP_COUNT_CONTROL_RESET);
	q->count = 1;

	/* enable irqs */
	cx_set(MO_PCI_INTMSK, 0x00fc01);
	cx_set(MO_VID_INTMSK, 0x0f0011);

	/* enable capture */
	cx_set(VID_CAPTURE_CONTROL,0x06);

	/* start dma */
	cx_set(MO_DEV_CNTRL2, (1<<5));
	cx_set(MO_VID_DMACNTRL, 0x11);

	return 0;
}

static int stop_video_dma(struct cx8800_dev    *dev)
{
	struct cx88_core *core = dev->core;

	/* stop dma */
	cx_clear(MO_VID_DMACNTRL, 0x11);

	/* disable capture */
	cx_clear(VID_CAPTURE_CONTROL,0x06);

	/* disable irqs */
	cx_clear(MO_PCI_INTMSK, 0x000001);
	cx_clear(MO_VID_INTMSK, 0x0f0011);
	return 0;
}

static int restart_video_queue(struct cx8800_dev    *dev,
			       struct cx88_dmaqueue *q)
{
	struct cx88_buffer *buf, *prev;
	struct list_head *item;

	if (!list_empty(&q->active)) {
	        buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		dprintk(2,"restart_queue [%p/%d]: restart dma\n",
			buf, buf->vb.i);
		start_video_dma(dev, q, buf);
		list_for_each(item,&q->active) {
			buf = list_entry(item, struct cx88_buffer, vb.queue);
			buf->count    = q->count++;
		}
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
		return 0;
	}

	prev = NULL;
	for (;;) {
		if (list_empty(&q->queued))
			return 0;
	        buf = list_entry(q->queued.next, struct cx88_buffer, vb.queue);
		if (NULL == prev) {
			list_del(&buf->vb.queue);
			list_add_tail(&buf->vb.queue,&q->active);
			start_video_dma(dev, q, buf);
			buf->vb.state = STATE_ACTIVE;
			buf->count    = q->count++;
			mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
			dprintk(2,"[%p/%d] restart_queue - first active\n",
				buf,buf->vb.i);

		} else if (prev->vb.width  == buf->vb.width  &&
			   prev->vb.height == buf->vb.height &&
			   prev->fmt       == buf->fmt) {
			list_del(&buf->vb.queue);
			list_add_tail(&buf->vb.queue,&q->active);
			buf->vb.state = STATE_ACTIVE;
			buf->count    = q->count++;
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			dprintk(2,"[%p/%d] restart_queue - move to active\n",
				buf,buf->vb.i);
		} else {
			return 0;
		}
		prev = buf;
	}
}

/* ------------------------------------------------------------------ */

static int
buffer_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	struct cx8800_fh *fh = q->priv_data;

	*size = fh->fmt->depth*fh->width*fh->height >> 3;
	if (0 == *count)
		*count = 32;
	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;
	return 0;
}

static int
buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
	       enum v4l2_field field)
{
	struct cx8800_fh   *fh  = q->priv_data;
	struct cx8800_dev  *dev = fh->dev;
	struct cx88_buffer *buf = container_of(vb,struct cx88_buffer,vb);
	int rc, init_buffer = 0;

	BUG_ON(NULL == fh->fmt);
	if (fh->width  < 48 || fh->width  > norm_maxw(dev->core->tvnorm) ||
	    fh->height < 32 || fh->height > norm_maxh(dev->core->tvnorm))
		return -EINVAL;
	buf->vb.size = (fh->width * fh->height * fh->fmt->depth) >> 3;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->fmt       != fh->fmt    ||
	    buf->vb.width  != fh->width  ||
	    buf->vb.height != fh->height ||
	    buf->vb.field  != field) {
		buf->fmt       = fh->fmt;
		buf->vb.width  = fh->width;
		buf->vb.height = fh->height;
		buf->vb.field  = field;
		init_buffer = 1;
	}

	if (STATE_NEEDS_INIT == buf->vb.state) {
		init_buffer = 1;
		if (0 != (rc = videobuf_iolock(dev->pci,&buf->vb,NULL)))
			goto fail;
	}

	if (init_buffer) {
		buf->bpl = buf->vb.width * buf->fmt->depth >> 3;
		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist, 0, UNSET,
					 buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_BOTTOM:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist, UNSET, 0,
					 buf->bpl, 0, buf->vb.height);
			break;
		case V4L2_FIELD_INTERLACED:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist, 0, buf->bpl,
					 buf->bpl, buf->bpl,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_TB:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist,
					 0, buf->bpl * (buf->vb.height >> 1),
					 buf->bpl, 0,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_BT:
			cx88_risc_buffer(dev->pci, &buf->risc,
					 buf->vb.dma.sglist,
					 buf->bpl * (buf->vb.height >> 1), 0,
					 buf->bpl, 0,
					 buf->vb.height >> 1);
			break;
		default:
			BUG();
		}
	}
	dprintk(2,"[%p/%d] buffer_prepare - %dx%d %dbpp \"%s\" - dma=0x%08lx\n",
		buf, buf->vb.i,
		fh->width, fh->height, fh->fmt->depth, fh->fmt->name,
		(unsigned long)buf->risc.dma);

	buf->vb.state = STATE_PREPARED;
	return 0;

 fail:
	cx88_free_buffer(dev->pci,buf);
	return rc;
}

static void
buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct cx88_buffer    *buf = container_of(vb,struct cx88_buffer,vb);
	struct cx88_buffer    *prev;
	struct cx8800_fh      *fh   = vq->priv_data;
	struct cx8800_dev     *dev  = fh->dev;
	struct cx88_dmaqueue  *q    = &dev->vidq;

	/* add jump to stopper */
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(q->stopper.dma);

	if (!list_empty(&q->queued)) {
		list_add_tail(&buf->vb.queue,&q->queued);
		buf->vb.state = STATE_QUEUED;
		dprintk(2,"[%p/%d] buffer_queue - append to queued\n",
			buf, buf->vb.i);

	} else if (list_empty(&q->active)) {
		list_add_tail(&buf->vb.queue,&q->active);
		start_video_dma(dev, q, buf);
		buf->vb.state = STATE_ACTIVE;
		buf->count    = q->count++;
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
		dprintk(2,"[%p/%d] buffer_queue - first active\n",
			buf, buf->vb.i);

	} else {
		prev = list_entry(q->active.prev, struct cx88_buffer, vb.queue);
		if (prev->vb.width  == buf->vb.width  &&
		    prev->vb.height == buf->vb.height &&
		    prev->fmt       == buf->fmt) {
			list_add_tail(&buf->vb.queue,&q->active);
			buf->vb.state = STATE_ACTIVE;
			buf->count    = q->count++;
			prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
			dprintk(2,"[%p/%d] buffer_queue - append to active\n",
				buf, buf->vb.i);

		} else {
			list_add_tail(&buf->vb.queue,&q->queued);
			buf->vb.state = STATE_QUEUED;
			dprintk(2,"[%p/%d] buffer_queue - first queued\n",
				buf, buf->vb.i);
		}
	}
}

static void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct cx88_buffer *buf = container_of(vb,struct cx88_buffer,vb);
	struct cx8800_fh   *fh  = q->priv_data;

	cx88_free_buffer(fh->dev->pci,buf);
}

struct videobuf_queue_ops cx8800_video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/* ------------------------------------------------------------------ */

#if 0 /* overlay support not finished yet */
static u32* ov_risc_field(struct cx8800_dev *dev, struct cx8800_fh *fh,
			  u32 *rp, struct btcx_skiplist *skips,
			  u32 sync_line, int skip_even, int skip_odd)
{
	int line,maxy,start,end,skip,nskips;
	u32 ri,ra;
	u32 addr;

	/* sync instruction */
	*(rp++) = cpu_to_le32(RISC_RESYNC | sync_line);

	addr  = (unsigned long)dev->fbuf.base;
	addr += dev->fbuf.fmt.bytesperline * fh->win.w.top;
	addr += (fh->fmt->depth >> 3)      * fh->win.w.left;

	/* scan lines */
	for (maxy = -1, line = 0; line < fh->win.w.height;
	     line++, addr += dev->fbuf.fmt.bytesperline) {
		if ((line%2) == 0  &&  skip_even)
			continue;
		if ((line%2) == 1  &&  skip_odd)
			continue;

		/* calculate clipping */
		if (line > maxy)
			btcx_calc_skips(line, fh->win.w.width, &maxy,
					skips, &nskips, fh->clips, fh->nclips);

		/* write out risc code */
		for (start = 0, skip = 0; start < fh->win.w.width; start = end) {
			if (skip >= nskips) {
				ri  = RISC_WRITE;
				end = fh->win.w.width;
			} else if (start < skips[skip].start) {
				ri  = RISC_WRITE;
				end = skips[skip].start;
			} else {
				ri  = RISC_SKIP;
				end = skips[skip].end;
				skip++;
			}
			if (RISC_WRITE == ri)
				ra = addr + (fh->fmt->depth>>3)*start;
			else
				ra = 0;

			if (0 == start)
				ri |= RISC_SOL;
			if (fh->win.w.width == end)
				ri |= RISC_EOL;
			ri |= (fh->fmt->depth>>3) * (end-start);

			*(rp++)=cpu_to_le32(ri);
			if (0 != ra)
				*(rp++)=cpu_to_le32(ra);
		}
	}
	kfree(skips);
	return rp;
}

static int ov_risc_frame(struct cx8800_dev *dev, struct cx8800_fh *fh,
			 struct cx88_buffer *buf)
{
	struct btcx_skiplist *skips;
	u32 instructions,fields;
	u32 *rp;
	int rc;

	/* skip list for window clipping */
	if (NULL == (skips = kmalloc(sizeof(*skips) * fh->nclips,GFP_KERNEL)))
		return -ENOMEM;

	fields = 0;
	if (V4L2_FIELD_HAS_TOP(fh->win.field))
		fields++;
	if (V4L2_FIELD_HAS_BOTTOM(fh->win.field))
		fields++;

        /* estimate risc mem: worst case is (clip+1) * lines instructions
           + syncs + jump (all 2 dwords) */
	instructions  = (fh->nclips+1) * fh->win.w.height;
	instructions += 3 + 4;
	if ((rc = btcx_riscmem_alloc(dev->pci,&buf->risc,instructions*8)) < 0) {
		kfree(skips);
		return rc;
	}

	/* write risc instructions */
	rp = buf->risc.cpu;
	switch (fh->win.field) {
	case V4L2_FIELD_TOP:
		rp = ov_risc_field(dev, fh, rp, skips, 0,     0, 0);
		break;
	case V4L2_FIELD_BOTTOM:
		rp = ov_risc_field(dev, fh, rp, skips, 0x200, 0, 0);
		break;
	case V4L2_FIELD_INTERLACED:
		rp = ov_risc_field(dev, fh, rp, skips, 0,     0, 1);
		rp = ov_risc_field(dev, fh, rp, skips, 0x200, 1, 0);
		break;
	default:
		BUG();
	}

	/* save pointer to jmp instruction address */
	buf->risc.jmp = rp;
	kfree(skips);
	return 0;
}

static int verify_window(struct cx8800_dev *dev, struct v4l2_window *win)
{
	enum v4l2_field field;
	int maxw, maxh;

	if (NULL == dev->fbuf.base)
		return -EINVAL;
	if (win->w.width < 48 || win->w.height <  32)
		return -EINVAL;
	if (win->clipcount > 2048)
		return -EINVAL;

	field = win->field;
	maxw  = norm_maxw(core->tvnorm);
	maxh  = norm_maxh(core->tvnorm);

	if (V4L2_FIELD_ANY == field) {
                field = (win->w.height > maxh/2)
                        ? V4L2_FIELD_INTERLACED
                        : V4L2_FIELD_TOP;
        }
        switch (field) {
        case V4L2_FIELD_TOP:
        case V4L2_FIELD_BOTTOM:
                maxh = maxh / 2;
                break;
        case V4L2_FIELD_INTERLACED:
                break;
        default:
                return -EINVAL;
        }

	win->field = field;
	if (win->w.width > maxw)
		win->w.width = maxw;
	if (win->w.height > maxh)
		win->w.height = maxh;
	return 0;
}

static int setup_window(struct cx8800_dev *dev, struct cx8800_fh *fh,
			struct v4l2_window *win)
{
	struct v4l2_clip *clips = NULL;
	int n,size,retval = 0;

	if (NULL == fh->fmt)
		return -EINVAL;
	retval = verify_window(dev,win);
	if (0 != retval)
		return retval;

	/* copy clips  --  luckily v4l1 + v4l2 are binary
	   compatible here ...*/
	n = win->clipcount;
	size = sizeof(*clips)*(n+4);
	clips = kmalloc(size,GFP_KERNEL);
	if (NULL == clips)
		return -ENOMEM;
	if (n > 0) {
		if (copy_from_user(clips,win->clips,sizeof(struct v4l2_clip)*n)) {
			kfree(clips);
			return -EFAULT;
		}
	}

	/* clip against screen */
	if (NULL != dev->fbuf.base)
		n = btcx_screen_clips(dev->fbuf.fmt.width, dev->fbuf.fmt.height,
				      &win->w, clips, n);
	btcx_sort_clips(clips,n);

	/* 4-byte alignments */
	switch (fh->fmt->depth) {
	case 8:
	case 24:
		btcx_align(&win->w, clips, n, 3);
		break;
	case 16:
		btcx_align(&win->w, clips, n, 1);
		break;
	case 32:
		/* no alignment fixups needed */
		break;
	default:
		BUG();
	}

	down(&fh->vidq.lock);
	if (fh->clips)
		kfree(fh->clips);
	fh->clips    = clips;
	fh->nclips   = n;
	fh->win      = *win;
#if 0
	fh->ov.setup_ok = 1;
#endif

	/* update overlay if needed */
	retval = 0;
#if 0
	if (check_btres(fh, RESOURCE_OVERLAY)) {
		struct bttv_buffer *new;

		new = videobuf_alloc(sizeof(*new));
		bttv_overlay_risc(btv, &fh->ov, fh->ovfmt, new);
		retval = bttv_switch_overlay(btv,fh,new);
	}
#endif
	up(&fh->vidq.lock);
	return retval;
}
#endif

/* ------------------------------------------------------------------ */

static struct videobuf_queue* get_queue(struct cx8800_fh *fh)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &fh->vidq;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return &fh->vbiq;
	default:
		BUG();
		return NULL;
	}
}

static int get_ressource(struct cx8800_fh *fh)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return RESOURCE_VIDEO;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return RESOURCE_VBI;
	default:
		BUG();
		return 0;
	}
}

static int video_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cx8800_dev *h,*dev = NULL;
	struct cx8800_fh *fh;
	struct list_head *list;
	enum v4l2_buf_type type = 0;
	int radio = 0;

	list_for_each(list,&cx8800_devlist) {
		h = list_entry(list, struct cx8800_dev, devlist);
		if (h->video_dev->minor == minor) {
			dev  = h;
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		}
		if (h->vbi_dev->minor == minor) {
			dev  = h;
			type = V4L2_BUF_TYPE_VBI_CAPTURE;
		}
		if (h->radio_dev &&
		    h->radio_dev->minor == minor) {
			radio = 1;
			dev   = h;
		}
	}
	if (NULL == dev)
		return -ENODEV;

	dprintk(1,"open minor=%d radio=%d type=%s\n",
		minor,radio,v4l2_type_names[type]);

	/* allocate + initialize per filehandle data */
	fh = kmalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;
	memset(fh,0,sizeof(*fh));
	file->private_data = fh;
	fh->dev      = dev;
	fh->radio    = radio;
	fh->type     = type;
	fh->width    = 320;
	fh->height   = 240;
	fh->fmt      = format_by_fourcc(V4L2_PIX_FMT_BGR24);

	videobuf_queue_init(&fh->vidq, &cx8800_video_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct cx88_buffer),
			    fh);
	videobuf_queue_init(&fh->vbiq, &cx8800_vbi_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VBI_CAPTURE,
			    V4L2_FIELD_SEQ_TB,
			    sizeof(struct cx88_buffer),
			    fh);

	if (fh->radio) {
		struct cx88_core *core = dev->core;
		int board = core->board;
		dprintk(1,"video_open: setting radio device\n");
		cx_write(MO_GP0_IO, cx88_boards[board].radio.gpio0);
		cx_write(MO_GP1_IO, cx88_boards[board].radio.gpio1);
		cx_write(MO_GP2_IO, cx88_boards[board].radio.gpio2);
		cx_write(MO_GP3_IO, cx88_boards[board].radio.gpio3);
		dev->core->tvaudio = WW_FM;
		cx88_set_tvaudio(core);
		cx88_set_stereo(core,V4L2_TUNER_MODE_STEREO);
		cx88_call_i2c_clients(dev->core,AUDC_SET_RADIO,NULL);
	}

        return 0;
}

static ssize_t
video_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct cx8800_fh *fh = file->private_data;

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (res_locked(fh->dev,RESOURCE_VIDEO))
			return -EBUSY;
		return videobuf_read_one(&fh->vidq, data, count, ppos,
					 file->f_flags & O_NONBLOCK);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (!res_get(fh->dev,fh,RESOURCE_VBI))
			return -EBUSY;
		return videobuf_read_stream(&fh->vbiq, data, count, ppos, 1,
					    file->f_flags & O_NONBLOCK);
	default:
		BUG();
		return 0;
	}
}

static unsigned int
video_poll(struct file *file, struct poll_table_struct *wait)
{
	struct cx8800_fh *fh = file->private_data;
	struct cx88_buffer *buf;

	if (V4L2_BUF_TYPE_VBI_CAPTURE == fh->type) {
		if (!res_get(fh->dev,fh,RESOURCE_VBI))
			return POLLERR;
		return videobuf_poll_stream(file, &fh->vbiq, wait);
	}

	if (res_check(fh,RESOURCE_VIDEO)) {
		/* streaming capture */
		if (list_empty(&fh->vidq.stream))
			return POLLERR;
		buf = list_entry(fh->vidq.stream.next,struct cx88_buffer,vb.stream);
	} else {
		/* read() capture */
		buf = (struct cx88_buffer*)fh->vidq.read_buf;
		if (NULL == buf)
			return POLLERR;
	}
	poll_wait(file, &buf->vb.done, wait);
	if (buf->vb.state == STATE_DONE ||
	    buf->vb.state == STATE_ERROR)
		return POLLIN|POLLRDNORM;
	return 0;
}

static int video_release(struct inode *inode, struct file *file)
{
	struct cx8800_fh  *fh  = file->private_data;
	struct cx8800_dev *dev = fh->dev;

	/* turn off overlay */
	if (res_check(fh, RESOURCE_OVERLAY)) {
		/* FIXME */
		res_free(dev,fh,RESOURCE_OVERLAY);
	}

	/* stop video capture */
	if (res_check(fh, RESOURCE_VIDEO)) {
		videobuf_queue_cancel(&fh->vidq);
		res_free(dev,fh,RESOURCE_VIDEO);
	}
	if (fh->vidq.read_buf) {
		buffer_release(&fh->vidq,fh->vidq.read_buf);
		kfree(fh->vidq.read_buf);
	}

	/* stop vbi capture */
	if (res_check(fh, RESOURCE_VBI)) {
		if (fh->vbiq.streaming)
			videobuf_streamoff(&fh->vbiq);
		if (fh->vbiq.reading)
			videobuf_read_stop(&fh->vbiq);
		res_free(dev,fh,RESOURCE_VBI);
	}

	file->private_data = NULL;
	kfree(fh);
	return 0;
}

static int
video_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct cx8800_fh *fh = file->private_data;

	return videobuf_mmap_mapper(get_queue(fh), vma);
}

/* ------------------------------------------------------------------ */

static int get_control(struct cx8800_dev *dev, struct v4l2_control *ctl)
{
	struct cx88_core *core = dev->core;
	struct cx88_ctrl *c = NULL;
	u32 value;
	int i;

	for (i = 0; i < CX8800_CTLS; i++)
		if (cx8800_ctls[i].v.id == ctl->id)
			c = &cx8800_ctls[i];
	if (NULL == c)
		return -EINVAL;

	value = c->sreg ? cx_sread(c->sreg) : cx_read(c->reg);
	switch (ctl->id) {
	case V4L2_CID_AUDIO_BALANCE:
		ctl->value = (value & 0x40) ? (value & 0x3f) : (0x40 - (value & 0x3f));
		break;
	case V4L2_CID_AUDIO_VOLUME:
		ctl->value = 0x3f - (value & 0x3f);
		break;
	default:
		ctl->value = ((value + (c->off << c->shift)) & c->mask) >> c->shift;
		break;
	}
	return 0;
}

static int set_control(struct cx8800_dev *dev, struct v4l2_control *ctl)
{
	struct cx88_core *core = dev->core;
	struct cx88_ctrl *c = NULL;
        u32 v_sat_value;
	u32 value;
	int i;

	for (i = 0; i < CX8800_CTLS; i++)
		if (cx8800_ctls[i].v.id == ctl->id)
			c = &cx8800_ctls[i];
	if (NULL == c)
		return -EINVAL;

	if (ctl->value < c->v.minimum)
		return -ERANGE;
	if (ctl->value > c->v.maximum)
		return -ERANGE;
	switch (ctl->id) {
	case V4L2_CID_AUDIO_BALANCE:
		value = (ctl->value < 0x40) ? (0x40 - ctl->value) : ctl->value;
		break;
	case V4L2_CID_AUDIO_VOLUME:
		value = 0x3f - (ctl->value & 0x3f);
		break;
	case V4L2_CID_SATURATION:
		/* special v_sat handling */
		v_sat_value = ctl->value - (0x7f - 0x5a);
		if (v_sat_value > 0xff)
			v_sat_value = 0xff;
		if (v_sat_value < 0x00)
			v_sat_value = 0x00;
		cx_andor(MO_UV_SATURATION, 0xff00, v_sat_value << 8);
		/* fall through to default route for u_sat */
	default:
		value = ((ctl->value - c->off) << c->shift) & c->mask;
		break;
	}
	dprintk(1,"set_control id=0x%X reg=0x%x val=0x%x%s\n",
		ctl->id, c->reg, value, c->sreg ? " [shadowed]" : "");
	if (c->sreg) {
		cx_sandor(c->sreg, c->reg, c->mask, value);
	} else {
		cx_andor(c->reg, c->mask, value);
	}
	return 0;
}

static void init_controls(struct cx8800_dev *dev)
{
	static struct v4l2_control mute = {
		.id    = V4L2_CID_AUDIO_MUTE,
		.value = 1,
	};
	static struct v4l2_control volume = {
		.id    = V4L2_CID_AUDIO_VOLUME,
		.value = 0x3f,
	};

	set_control(dev,&mute);
	set_control(dev,&volume);
}

/* ------------------------------------------------------------------ */

static int cx8800_g_fmt(struct cx8800_dev *dev, struct cx8800_fh *fh,
			struct v4l2_format *f)
{
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(&f->fmt.pix,0,sizeof(f->fmt.pix));
		f->fmt.pix.width        = fh->width;
		f->fmt.pix.height       = fh->height;
		f->fmt.pix.field        = fh->vidq.field;
		f->fmt.pix.pixelformat  = fh->fmt->fourcc;
		f->fmt.pix.bytesperline =
			(f->fmt.pix.width * fh->fmt->depth) >> 3;
		f->fmt.pix.sizeimage =
			f->fmt.pix.height * f->fmt.pix.bytesperline;
		return 0;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		cx8800_vbi_fmt(dev, f);
		return 0;
	default:
		return -EINVAL;
	}
}

static int cx8800_try_fmt(struct cx8800_dev *dev, struct cx8800_fh *fh,
			  struct v4l2_format *f)
{
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	{
		struct cx8800_fmt *fmt;
		enum v4l2_field field;
		unsigned int maxw, maxh;

		fmt = format_by_fourcc(f->fmt.pix.pixelformat);
		if (NULL == fmt)
			return -EINVAL;

		field = f->fmt.pix.field;
		maxw  = norm_maxw(dev->core->tvnorm);
		maxh  = norm_maxh(dev->core->tvnorm);

		if (V4L2_FIELD_ANY == field) {
			field = (f->fmt.pix.height > maxh/2)
				? V4L2_FIELD_INTERLACED
				: V4L2_FIELD_BOTTOM;
		}

		switch (field) {
		case V4L2_FIELD_TOP:
		case V4L2_FIELD_BOTTOM:
			maxh = maxh / 2;
			break;
		case V4L2_FIELD_INTERLACED:
			break;
		default:
			return -EINVAL;
		}

		f->fmt.pix.field = field;
		if (f->fmt.pix.height < 32)
			f->fmt.pix.height = 32;
		if (f->fmt.pix.height > maxh)
			f->fmt.pix.height = maxh;
		if (f->fmt.pix.width < 48)
			f->fmt.pix.width = 48;
		if (f->fmt.pix.width > maxw)
			f->fmt.pix.width = maxw;
		f->fmt.pix.width &= ~0x03;
		f->fmt.pix.bytesperline =
			(f->fmt.pix.width * fmt->depth) >> 3;
		f->fmt.pix.sizeimage =
			f->fmt.pix.height * f->fmt.pix.bytesperline;

		return 0;
	}
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		cx8800_vbi_fmt(dev, f);
		return 0;
	default:
		return -EINVAL;
	}
}

static int cx8800_s_fmt(struct cx8800_dev *dev, struct cx8800_fh *fh,
			struct v4l2_format *f)
{
	int err;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		err = cx8800_try_fmt(dev,fh,f);
		if (0 != err)
			return err;

		fh->fmt        = format_by_fourcc(f->fmt.pix.pixelformat);
		fh->width      = f->fmt.pix.width;
		fh->height     = f->fmt.pix.height;
		fh->vidq.field = f->fmt.pix.field;
		return 0;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		cx8800_vbi_fmt(dev, f);
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * This function is _not_ called directly, but from
 * video_generic_ioctl (and maybe others).  userspace
 * copying is done already, arg is a kernel pointer.
 */
static int video_do_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, void *arg)
{
	struct cx8800_fh  *fh   = file->private_data;
	struct cx8800_dev *dev  = fh->dev;
	struct cx88_core  *core = dev->core;
#if 0
	unsigned long flags;
#endif
	int err;

	if (video_debug > 1)
		cx88_print_ioctl(core->name,cmd);
	switch (cmd) {
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
                strcpy(cap->driver, "cx8800");
		strlcpy(cap->card, cx88_boards[core->board].name,
			sizeof(cap->card));
		sprintf(cap->bus_info,"PCI:%s",pci_name(dev->pci));
		cap->version = CX88_VERSION_CODE;
		cap->capabilities =
			V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_READWRITE     |
			V4L2_CAP_STREAMING     |
			V4L2_CAP_VBI_CAPTURE   |
#if 0
			V4L2_CAP_VIDEO_OVERLAY |
#endif
			0;
		if (UNSET != core->tuner_type)
			cap->capabilities |= V4L2_CAP_TUNER;

		return 0;
	}

	/* ---------- tv norms ---------- */
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *e = arg;
		unsigned int i;

		i = e->index;
		if (i >= ARRAY_SIZE(tvnorms))
			return -EINVAL;
		err = v4l2_video_std_construct(e, tvnorms[e->index].id,
					       tvnorms[e->index].name);
		e->index = i;
		if (err < 0)
			return err;
		return 0;
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = arg;

		*id = core->tvnorm->id;
		return 0;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;
		unsigned int i;

		for(i = 0; i < ARRAY_SIZE(tvnorms); i++)
			if (*id & tvnorms[i].id)
				break;
		if (i == ARRAY_SIZE(tvnorms))
			return -EINVAL;

		down(&dev->lock);
		cx88_set_tvnorm(dev->core,&tvnorms[i]);
		up(&dev->lock);
		return 0;
	}

	/* ------ input switching ---------- */
	case VIDIOC_ENUMINPUT:
	{
		static const char *iname[] = {
			[ CX88_VMUX_COMPOSITE1 ] = "Composite1",
			[ CX88_VMUX_COMPOSITE2 ] = "Composite2",
			[ CX88_VMUX_COMPOSITE3 ] = "Composite3",
			[ CX88_VMUX_COMPOSITE4 ] = "Composite4",
			[ CX88_VMUX_SVIDEO     ] = "S-Video",
			[ CX88_VMUX_TELEVISION ] = "Television",
			[ CX88_VMUX_CABLE      ] = "Cable TV",
			[ CX88_VMUX_DVB        ] = "DVB",
			[ CX88_VMUX_DEBUG      ] = "for debug only",
		};
		struct v4l2_input *i = arg;
		unsigned int n;

		n = i->index;
		if (n >= 4)
			return -EINVAL;
		if (0 == INPUT(n)->type)
			return -EINVAL;
		memset(i,0,sizeof(*i));
		i->index = n;
		i->type  = V4L2_INPUT_TYPE_CAMERA;
		strcpy(i->name,iname[INPUT(n)->type]);
		if ((CX88_VMUX_TELEVISION == INPUT(n)->type) ||
		    (CX88_VMUX_CABLE      == INPUT(n)->type))
			i->type = V4L2_INPUT_TYPE_TUNER;
		for (n = 0; n < ARRAY_SIZE(tvnorms); n++)
			i->std |= tvnorms[n].id;
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		unsigned int *i = arg;

		*i = dev->core->input;
		return 0;
	}
	case VIDIOC_S_INPUT:
	{
		unsigned int *i = arg;

		if (*i >= 4)
			return -EINVAL;
		down(&dev->lock);
		video_mux(dev,*i);
		up(&dev->lock);
		return 0;
	}


#if 0
	/* needs review */
	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *a = arg;
		unsigned int n = a->index;

		memset(a,0,sizeof(*a));
		a->index = n;
		switch (n) {
		case 0:
			if ((CX88_VMUX_TELEVISION == INPUT(n)->type)
			    || (CX88_VMUX_CABLE == INPUT(n)->type)) {
				strcpy(a->name,"Television");
				// FIXME figure out if stereo received and set V4L2_AUDCAP_STEREO.
				return 0;
			}
			break;
		case 1:
			if (CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD == core->board) {
				strcpy(a->name,"Line In");
				a->capability = V4L2_AUDCAP_STEREO;
				return 0;
			}
			break;
		}
		// Audio input not available.
		return -EINVAL;
	}
#endif

	/* --- capture ioctls ---------------------------------------- */
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		enum v4l2_buf_type type;
		unsigned int index;

		index = f->index;
		type  = f->type;
		switch (type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			if (index >= ARRAY_SIZE(formats))
				return -EINVAL;
			memset(f,0,sizeof(*f));
			f->index = index;
			f->type  = type;
			strlcpy(f->description,formats[index].name,sizeof(f->description));
			f->pixelformat = formats[index].fourcc;
			break;
		default:
			return -EINVAL;
		}
		return 0;
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;
		return cx8800_g_fmt(dev,fh,f);
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;
		return cx8800_s_fmt(dev,fh,f);
	}
	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *f = arg;
		return cx8800_try_fmt(dev,fh,f);
	}

	/* --- controls ---------------------------------------------- */
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *c = arg;
		int i;

		if (c->id <  V4L2_CID_BASE ||
		    c->id >= V4L2_CID_LASTP1)
			return -EINVAL;
		for (i = 0; i < CX8800_CTLS; i++)
			if (cx8800_ctls[i].v.id == c->id)
				break;
		if (i == CX8800_CTLS) {
			*c = no_ctl;
			return 0;
		}
		*c = cx8800_ctls[i].v;
		return 0;
	}
	case VIDIOC_G_CTRL:
		return get_control(dev,arg);
	case VIDIOC_S_CTRL:
		return set_control(dev,arg);

	/* --- tuner ioctls ------------------------------------------ */
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *t = arg;
		u32 reg;

		if (UNSET == core->tuner_type)
			return -EINVAL;
		if (0 != t->index)
			return -EINVAL;

		memset(t,0,sizeof(*t));
		strcpy(t->name, "Television");
		t->type       = V4L2_TUNER_ANALOG_TV;
		t->capability = V4L2_TUNER_CAP_NORM;
		t->rangehigh  = 0xffffffffUL;

		cx88_get_stereo(core ,t);
		reg = cx_read(MO_DEVICE_STATUS);
                t->signal = (reg & (1<<5)) ? 0xffff : 0x0000;
		return 0;
	}
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *t = arg;

		if (UNSET == core->tuner_type)
			return -EINVAL;
		if (0 != t->index)
			return -EINVAL;
		cx88_set_stereo(core, t->audmode);
		return 0;
	}
	case VIDIOC_G_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		if (UNSET == core->tuner_type)
			return -EINVAL;
		if (f->tuner != 0)
			return -EINVAL;
		memset(f,0,sizeof(*f));
		f->type = fh->radio ? V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
		f->frequency = dev->freq;
		return 0;
	}
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		if (UNSET == core->tuner_type)
			return -EINVAL;
		if (f->tuner != 0)
			return -EINVAL;
		if (0 == fh->radio && f->type != V4L2_TUNER_ANALOG_TV)
			return -EINVAL;
		if (1 == fh->radio && f->type != V4L2_TUNER_RADIO)
			return -EINVAL;
		down(&dev->lock);
		dev->freq = f->frequency;
#ifdef V4L2_I2C_CLIENTS
		cx88_call_i2c_clients(dev->core,VIDIOC_S_FREQUENCY,f);
#else
		cx88_call_i2c_clients(dev->core,VIDIOCSFREQ,&dev->freq);
#endif
		up(&dev->lock);
		return 0;
	}

	/* --- streaming capture ------------------------------------- */
	case VIDIOCGMBUF:
	{
		struct video_mbuf *mbuf = arg;
		struct videobuf_queue *q;
		struct v4l2_requestbuffers req;
		unsigned int i;

		q = get_queue(fh);
		memset(&req,0,sizeof(req));
		req.type   = q->type;
		req.count  = 8;
		req.memory = V4L2_MEMORY_MMAP;
		err = videobuf_reqbufs(q,&req);
		if (err < 0)
			return err;
		memset(mbuf,0,sizeof(*mbuf));
		mbuf->frames = req.count;
		mbuf->size   = 0;
		for (i = 0; i < mbuf->frames; i++) {
			mbuf->offsets[i]  = q->bufs[i]->boff;
			mbuf->size       += q->bufs[i]->bsize;
		}
		return 0;
	}
	case VIDIOC_REQBUFS:
		return videobuf_reqbufs(get_queue(fh), arg);

	case VIDIOC_QUERYBUF:
		return videobuf_querybuf(get_queue(fh), arg);

	case VIDIOC_QBUF:
		return videobuf_qbuf(get_queue(fh), arg);

	case VIDIOC_DQBUF:
		return videobuf_dqbuf(get_queue(fh), arg,
				      file->f_flags & O_NONBLOCK);

	case VIDIOC_STREAMON:
	{
		int res = get_ressource(fh);

                if (!res_get(dev,fh,res))
			return -EBUSY;
		return videobuf_streamon(get_queue(fh));
	}
	case VIDIOC_STREAMOFF:
	{
		int res = get_ressource(fh);

		err = videobuf_streamoff(get_queue(fh));
		if (err < 0)
			return err;
		res_free(dev,fh,res);
		return 0;
	}

	default:
		return v4l_compat_translate_ioctl(inode,file,cmd,arg,
						  video_do_ioctl);
	}
	return 0;
}

static int video_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, video_do_ioctl);
}

/* ----------------------------------------------------------- */

static int radio_do_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, void *arg)
{
	struct cx8800_fh *fh    = file->private_data;
	struct cx8800_dev *dev  = fh->dev;
	struct cx88_core  *core = dev->core;

	if (video_debug > 1)
		cx88_print_ioctl(core->name,cmd);

	switch (cmd) {
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
                strcpy(cap->driver, "cx8800");
		strlcpy(cap->card, cx88_boards[core->board].name,
			sizeof(cap->card));
		sprintf(cap->bus_info,"PCI:%s", pci_name(dev->pci));
		cap->version = CX88_VERSION_CODE;
		cap->capabilities = V4L2_CAP_TUNER;
		return 0;
	}
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *t = arg;

		if (t->index > 0)
			return -EINVAL;

		memset(t,0,sizeof(*t));
		strcpy(t->name, "Radio");
                t->rangelow  = (int)(65*16);
                t->rangehigh = (int)(108*16);

#ifdef V4L2_I2C_CLIENTS
		cx88_call_i2c_clients(dev->core,VIDIOC_G_TUNER,t);
#else
		{
			struct video_tuner vt;
			memset(&vt,0,sizeof(vt));
			cx88_call_i2c_clients(dev,VIDIOCGTUNER,&vt);
			t->signal = vt.signal;
		}
#endif
		return 0;
	}
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;

		if (i->index != 0)
			return -EINVAL;
		strcpy(i->name,"Radio");
		i->type = V4L2_INPUT_TYPE_TUNER;
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *i = arg;
		*i = 0;
		return 0;
	}
	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *a = arg;

		memset(a,0,sizeof(*a));
		strcpy(a->name,"Radio");
		return 0;
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = arg;
		*id = 0;
		return 0;
	}
	case VIDIOC_S_AUDIO:
	case VIDIOC_S_TUNER:
	case VIDIOC_S_INPUT:
	case VIDIOC_S_STD:
		return 0;

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *c = arg;
		int i;

		if (c->id <  V4L2_CID_BASE ||
		    c->id >= V4L2_CID_LASTP1)
			return -EINVAL;
		if (c->id == V4L2_CID_AUDIO_MUTE) {
			for (i = 0; i < CX8800_CTLS; i++)
				if (cx8800_ctls[i].v.id == c->id)
					break;
			*c = cx8800_ctls[i].v;
		} else
			*c = no_ctl;
		return 0;
	}


	case VIDIOC_G_CTRL:
	case VIDIOC_S_CTRL:
	case VIDIOC_G_FREQUENCY:
	case VIDIOC_S_FREQUENCY:
		return video_do_ioctl(inode,file,cmd,arg);

	default:
		return v4l_compat_translate_ioctl(inode,file,cmd,arg,
						  radio_do_ioctl);
	}
	return 0;
};

static int radio_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, radio_do_ioctl);
};

/* ----------------------------------------------------------- */

static void cx8800_vid_timeout(unsigned long data)
{
	struct cx8800_dev *dev = (struct cx8800_dev*)data;
	struct cx88_core *core = dev->core;
	struct cx88_dmaqueue *q = &dev->vidq;
	struct cx88_buffer *buf;
	unsigned long flags;

	cx88_sram_channel_dump(dev->core, &cx88_sram_channels[SRAM_CH21]);

	cx_clear(MO_VID_DMACNTRL, 0x11);
	cx_clear(VID_CAPTURE_CONTROL, 0x06);

	spin_lock_irqsave(&dev->slock,flags);
	while (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		list_del(&buf->vb.queue);
		buf->vb.state = STATE_ERROR;
		wake_up(&buf->vb.done);
		printk("%s/0: [%p/%d] timeout - dma=0x%08lx\n", core->name,
		       buf, buf->vb.i, (unsigned long)buf->risc.dma);
	}
	restart_video_queue(dev,q);
	spin_unlock_irqrestore(&dev->slock,flags);
}

static void cx8800_vid_irq(struct cx8800_dev *dev)
{
	struct cx88_core *core = dev->core;
	u32 status, mask, count;

	status = cx_read(MO_VID_INTSTAT);
	mask   = cx_read(MO_VID_INTMSK);
	if (0 == (status & mask))
		return;
	cx_write(MO_VID_INTSTAT, status);
	if (irq_debug  ||  (status & mask & ~0xff))
		cx88_print_irqbits(core->name, "irq vid",
				   cx88_vid_irqs, status, mask);

	/* risc op code error */
	if (status & (1 << 16)) {
		printk(KERN_WARNING "%s/0: video risc op code error\n",core->name);
		cx_clear(MO_VID_DMACNTRL, 0x11);
		cx_clear(VID_CAPTURE_CONTROL, 0x06);
		cx88_sram_channel_dump(dev->core, &cx88_sram_channels[SRAM_CH21]);
	}

	/* risc1 y */
	if (status & 0x01) {
		spin_lock(&dev->slock);
		count = cx_read(MO_VIDY_GPCNT);
		cx88_wakeup(dev->core, &dev->vidq, count);
		spin_unlock(&dev->slock);
	}

	/* risc1 vbi */
	if (status & 0x08) {
		spin_lock(&dev->slock);
		count = cx_read(MO_VBI_GPCNT);
		cx88_wakeup(dev->core, &dev->vbiq, count);
		spin_unlock(&dev->slock);
	}

	/* risc2 y */
	if (status & 0x10) {
		dprintk(2,"stopper video\n");
		spin_lock(&dev->slock);
		restart_video_queue(dev,&dev->vidq);
		spin_unlock(&dev->slock);
	}

	/* risc2 vbi */
	if (status & 0x80) {
		dprintk(2,"stopper vbi\n");
		spin_lock(&dev->slock);
		cx8800_restart_vbi_queue(dev,&dev->vbiq);
		spin_unlock(&dev->slock);
	}
}

static irqreturn_t cx8800_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct cx8800_dev *dev = dev_id;
	struct cx88_core *core = dev->core;
	u32 status, mask;
	int loop, handled = 0;

	for (loop = 0; loop < 10; loop++) {
		status = cx_read(MO_PCI_INTSTAT) & (~0x1f | 0x01);
		mask   = cx_read(MO_PCI_INTMSK);
		if (0 == (status & mask))
			goto out;
		cx_write(MO_PCI_INTSTAT, status);
		handled = 1;

		if (status & mask & ~0x1f)
			cx88_irq(core,status,mask);
		if (status & 0x01)
			cx8800_vid_irq(dev);
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

static struct file_operations video_fops =
{
	.owner	       = THIS_MODULE,
	.open	       = video_open,
	.release       = video_release,
	.read	       = video_read,
	.poll          = video_poll,
	.mmap	       = video_mmap,
	.ioctl	       = video_ioctl,
	.llseek        = no_llseek,
};

struct video_device cx8800_video_template =
{
	.name          = "cx8800-video",
	.type          = VID_TYPE_CAPTURE|VID_TYPE_TUNER|VID_TYPE_SCALES,
	.hardware      = 0,
	.fops          = &video_fops,
	.minor         = -1,
};

struct video_device cx8800_vbi_template =
{
	.name          = "cx8800-vbi",
	.type          = VID_TYPE_TELETEXT|VID_TYPE_TUNER,
	.hardware      = 0,
	.fops          = &video_fops,
	.minor         = -1,
};

static struct file_operations radio_fops =
{
	.owner         = THIS_MODULE,
	.open          = video_open,
	.release       = video_release,
	.ioctl         = radio_ioctl,
	.llseek        = no_llseek,
};

struct video_device cx8800_radio_template =
{
	.name          = "cx8800-radio",
	.type          = VID_TYPE_TUNER,
	.hardware      = 0,
	.fops          = &radio_fops,
	.minor         = -1,
};

/* ----------------------------------------------------------- */

static void cx8800_unregister_video(struct cx8800_dev *dev)
{
	if (dev->radio_dev) {
		if (-1 != dev->radio_dev->minor)
			video_unregister_device(dev->radio_dev);
		else
			video_device_release(dev->radio_dev);
		dev->radio_dev = NULL;
	}
	if (dev->vbi_dev) {
		if (-1 != dev->vbi_dev->minor)
			video_unregister_device(dev->vbi_dev);
		else
			video_device_release(dev->vbi_dev);
		dev->vbi_dev = NULL;
	}
	if (dev->video_dev) {
		if (-1 != dev->video_dev->minor)
			video_unregister_device(dev->video_dev);
		else
			video_device_release(dev->video_dev);
		dev->video_dev = NULL;
	}
}

static int __devinit cx8800_initdev(struct pci_dev *pci_dev,
				    const struct pci_device_id *pci_id)
{
	struct cx8800_dev *dev;
	struct cx88_core *core;
	int err;

	dev = kmalloc(sizeof(*dev),GFP_KERNEL);
	if (NULL == dev)
		return -ENOMEM;
	memset(dev,0,sizeof(*dev));

	/* pci init */
	dev->pci = pci_dev;
	if (pci_enable_device(pci_dev)) {
		err = -EIO;
		goto fail_free;
	}
	core = cx88_core_get(dev->pci);
	if (NULL == core) {
		err = -EINVAL;
		goto fail_free;
	}
	dev->core = core;

	/* print pci info */
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &dev->pci_rev);
        pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER,  &dev->pci_lat);
        printk(KERN_INFO "%s/0: found at %s, rev: %d, irq: %d, "
	       "latency: %d, mmio: 0x%lx\n", core->name,
	       pci_name(pci_dev), dev->pci_rev, pci_dev->irq,
	       dev->pci_lat,pci_resource_start(pci_dev,0));

	pci_set_master(pci_dev);
	if (!pci_dma_supported(pci_dev,0xffffffff)) {
		printk("%s/0: Oops: no 32bit PCI DMA ???\n",core->name);
		err = -EIO;
		goto fail_core;
	}

	/* initialize driver struct */
        init_MUTEX(&dev->lock);
	spin_lock_init(&dev->slock);
	core->tvnorm = tvnorms;

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vidq.queued);
	dev->vidq.timeout.function = cx8800_vid_timeout;
	dev->vidq.timeout.data     = (unsigned long)dev;
	init_timer(&dev->vidq.timeout);
	cx88_risc_stopper(dev->pci,&dev->vidq.stopper,
			  MO_VID_DMACNTRL,0x11,0x00);

	/* init vbi dma queues */
	INIT_LIST_HEAD(&dev->vbiq.active);
	INIT_LIST_HEAD(&dev->vbiq.queued);
	dev->vbiq.timeout.function = cx8800_vbi_timeout;
	dev->vbiq.timeout.data     = (unsigned long)dev;
	init_timer(&dev->vbiq.timeout);
	cx88_risc_stopper(dev->pci,&dev->vbiq.stopper,
			  MO_VID_DMACNTRL,0x88,0x00);

	/* get irq */
	err = request_irq(pci_dev->irq, cx8800_irq,
			  SA_SHIRQ | SA_INTERRUPT, core->name, dev);
	if (err < 0) {
		printk(KERN_ERR "%s: can't get IRQ %d\n",
		       core->name,pci_dev->irq);
		goto fail_core;
	}

	/* load and configure helper modules */
	if (TUNER_ABSENT != core->tuner_type)
		request_module("tuner");
	if (core->tda9887_conf)
		request_module("tda9887");
	if (core->tuner_type != UNSET)
		cx88_call_i2c_clients(dev->core,TUNER_SET_TYPE,&core->tuner_type);
	if (core->tda9887_conf)
		cx88_call_i2c_clients(dev->core,TDA9887_SET_CONFIG,&core->tda9887_conf);

	/* register v4l devices */
	dev->video_dev = cx88_vdev_init(core,dev->pci,
					&cx8800_video_template,"video");
	err = video_register_device(dev->video_dev,VFL_TYPE_GRABBER,
				    video_nr[core->nr]);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register video device\n",
		       core->name);
		goto fail_unreg;
	}
	printk(KERN_INFO "%s/0: registered device video%d [v4l2]\n",
	       core->name,dev->video_dev->minor & 0x1f);

	dev->vbi_dev = cx88_vdev_init(core,dev->pci,&cx8800_vbi_template,"vbi");
	err = video_register_device(dev->vbi_dev,VFL_TYPE_VBI,
				    vbi_nr[core->nr]);
	if (err < 0) {
		printk(KERN_INFO "%s/0: can't register vbi device\n",
		       core->name);
		goto fail_unreg;
	}
	printk(KERN_INFO "%s/0: registered device vbi%d\n",
	       core->name,dev->vbi_dev->minor & 0x1f);

	if (core->has_radio) {
		dev->radio_dev = cx88_vdev_init(core,dev->pci,
						&cx8800_radio_template,"radio");
		err = video_register_device(dev->radio_dev,VFL_TYPE_RADIO,
					    radio_nr[core->nr]);
		if (err < 0) {
			printk(KERN_INFO "%s/0: can't register radio device\n",
			       core->name);
			goto fail_unreg;
		}
		printk(KERN_INFO "%s/0: registered device radio%d\n",
		       core->name,dev->radio_dev->minor & 0x1f);
	}

	/* everything worked */
	list_add_tail(&dev->devlist,&cx8800_devlist);
	pci_set_drvdata(pci_dev,dev);

	/* initial device configuration */
	down(&dev->lock);
	init_controls(dev);
	cx88_set_tvnorm(dev->core,tvnorms);
	video_mux(dev,0);
	up(&dev->lock);

	/* start tvaudio thread */
	if (core->tuner_type != TUNER_ABSENT)
		core->kthread = kthread_run(cx88_audio_thread, core, "cx88 tvaudio");
	return 0;

fail_unreg:
	cx8800_unregister_video(dev);
	free_irq(pci_dev->irq, dev);
fail_core:
	cx88_core_put(core,dev->pci);
fail_free:
	kfree(dev);
	return err;
}

static void __devexit cx8800_finidev(struct pci_dev *pci_dev)
{
        struct cx8800_dev *dev = pci_get_drvdata(pci_dev);

	/* stop thread */
	if (dev->core->kthread) {
		kthread_stop(dev->core->kthread);
		dev->core->kthread = NULL;
	}

	cx88_shutdown(dev->core); /* FIXME */
	pci_disable_device(pci_dev);

	/* unregister stuff */

	free_irq(pci_dev->irq, dev);
	cx8800_unregister_video(dev);
	pci_set_drvdata(pci_dev, NULL);

	/* free memory */
	btcx_riscmem_free(dev->pci,&dev->vidq.stopper);
	list_del(&dev->devlist);
	cx88_core_put(dev->core,dev->pci);
	kfree(dev);
}

static int cx8800_suspend(struct pci_dev *pci_dev, u32 state)
{
        struct cx8800_dev *dev = pci_get_drvdata(pci_dev);
	struct cx88_core *core = dev->core;

	/* stop video+vbi capture */
	spin_lock(&dev->slock);
	if (!list_empty(&dev->vidq.active)) {
		printk("%s: suspend video\n", core->name);
		stop_video_dma(dev);
		del_timer(&dev->vidq.timeout);
	}
	if (!list_empty(&dev->vbiq.active)) {
		printk("%s: suspend vbi\n", core->name);
		cx8800_stop_vbi_dma(dev);
		del_timer(&dev->vbiq.timeout);
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

static int cx8800_resume(struct pci_dev *pci_dev)
{
        struct cx8800_dev *dev = pci_get_drvdata(pci_dev);
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
	if (!list_empty(&dev->vidq.active)) {
		printk("%s: resume video\n", core->name);
		restart_video_queue(dev,&dev->vidq);
	}
	if (!list_empty(&dev->vbiq.active)) {
		printk("%s: resume vbi\n", core->name);
		cx8800_restart_vbi_queue(dev,&dev->vbiq);
	}
	spin_unlock(&dev->slock);

	return 0;
}

/* ----------------------------------------------------------- */

struct pci_device_id cx8800_pci_tbl[] = {
	{
		.vendor       = 0x14f1,
		.device       = 0x8800,
                .subvendor    = PCI_ANY_ID,
                .subdevice    = PCI_ANY_ID,
	},{
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, cx8800_pci_tbl);

static struct pci_driver cx8800_pci_driver = {
        .name     = "cx8800",
        .id_table = cx8800_pci_tbl,
        .probe    = cx8800_initdev,
        .remove   = __devexit_p(cx8800_finidev),

	.suspend  = cx8800_suspend,
	.resume   = cx8800_resume,
};

static int cx8800_init(void)
{
	printk(KERN_INFO "cx2388x v4l2 driver version %d.%d.%d loaded\n",
	       (CX88_VERSION_CODE >> 16) & 0xff,
	       (CX88_VERSION_CODE >>  8) & 0xff,
	       CX88_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO "cx2388x: snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	return pci_module_init(&cx8800_pci_driver);
}

static void cx8800_fini(void)
{
	pci_unregister_driver(&cx8800_pci_driver);
}

module_init(cx8800_init);
module_exit(cx8800_fini);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
