/*
 * Driver for the NetChip 2280 USB device controller.
 * Specs and errata are available from <http://www.netchip.com>.
 *
 * NetChip Technology Inc. supported the development of this driver.
 *
 *
 * CODE STATUS HIGHLIGHTS
 *
 * Used with a gadget driver like "zero.c" this enumerates fine to Windows
 * or Linux hosts; handles disconnect, reconnect, and reset, for full or
 * high speed operation; and passes USB-IF "chapter 9" tests.
 *
 * Handles standard stress loads from the Linux "usbtest" driver, with
 * either DMA (default) or PIO (use_dma=n) used for ep-{a,b,c,d}.  Testing
 * with "ttcp" (and the "ether.c" driver) behaves nicely too.
 *
 * DMA is enabled by default.  Drivers using transfer queues might use
 * DMA chaining to remove IRQ latencies between transfers.  (Except when
 * short OUT transfers happen.)  Drivers can use the req->no_interrupt
 * hint to completely eliminate some IRQs, if a later IRQ is guaranteed
 * and DMA chaining is enabled.
 *
 * Note that almost all the errata workarounds here are only needed for
 * rev1 chips.  Rev1a silicon (0110) fixes almost all of them.
 */

#define USE_DMA_CHAINING


/*
 * Copyright (C) 2003 David Brownell
 * Copyright (C) 2003 NetChip Technologies
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define DEBUG	1
// #define	VERBOSE		/* extra debug messages (success too) */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>


#define	DRIVER_DESC		"NetChip 2280 USB Peripheral Controller"
#define	DRIVER_VERSION		"Bastille Day 2003"

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)
#define	EP_DONTUSE		13	/* nonzero */

#define USE_RDK_LEDS		/* GPIO pins control three LEDs */
#define USE_SYSFS_DEBUG_FILES


static const char driver_name [] = "net2280";
static const char driver_desc [] = DRIVER_DESC;

static const char ep0name [] = "ep0";
static const char *ep_name [] = {
	ep0name,
	"ep-a", "ep-b", "ep-c", "ep-d",
	"ep-e", "ep-f",
};

static int use_dma = 1;

/* "modprobe net2280 use_dma=n" etc */
module_param (use_dma, bool, S_IRUGO|S_IWUSR);

/* mode 0 == ep-{a,b,c,d} 1K fifo each
 * mode 1 == ep-{a,b} 2K fifo each, ep-{c,d} unavailable
 * mode 2 == ep-a 2K fifo, ep-{b,c} 1K each, ep-d unavailable
 */
static ushort fifo_mode = 0;

/* "modprobe net2280 fifo_mode=1" etc */
module_param (fifo_mode, ushort, 0644);

#define	DIR_STRING(bAddress) (((bAddress) & USB_DIR_IN) ? "in" : "out")

#if defined(USE_SYSFS_DEBUG_FILES) || defined (DEBUG)
static char *type_string (u8 bmAttributes)
{
	switch ((bmAttributes) & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:	return "bulk";
	case USB_ENDPOINT_XFER_ISOC:	return "iso";
	case USB_ENDPOINT_XFER_INT:	return "intr";
	};
	return "control";
}
#endif

#include "net2280.h"

#define valid_bit	__constant_cpu_to_le32 (1 << VALID_BIT)
#define dma_done_ie	__constant_cpu_to_le32 (1 << DMA_DONE_INTERRUPT_ENABLE)

/*-------------------------------------------------------------------------*/

static int
net2280_enable (struct usb_ep *_ep, const struct usb_endpoint_descriptor *desc)
{
	struct net2280		*dev;
	struct net2280_ep	*ep;
	u32			max, tmp;
	unsigned long		flags;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || !desc || ep->desc || _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;
	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	/* erratum 0119 workaround ties up an endpoint number */
	if ((desc->bEndpointAddress & 0x0f) == EP_DONTUSE)
		return -EDOM;

	/* sanity check ep-e/ep-f since their fifos are small */
	max = le16_to_cpu (desc->wMaxPacketSize) & 0x1fff;
	if (ep->num > 4 && max > 64)
		return -ERANGE;

	spin_lock_irqsave (&dev->lock, flags);
	_ep->maxpacket = max & 0x7ff;
	ep->desc = desc;

	/* ep_reset() has already been called */
	ep->stopped = 0;

	/* set speed-dependent max packet; may kick in high bandwidth */
	set_idx_reg (dev->regs, REG_EP_MAXPKT (dev, ep->num), max);

	/* FIFO lines can't go to different packets.  PIO is ok, so
	 * use it instead of troublesome (non-bulk) multi-packet DMA.
	 */
	if (ep->is_in && ep->dma && (max % 4) != 0) {
		DEBUG (ep->dev, "%s, no IN dma for maxpacket %d\n",
			ep->ep.name, ep->ep.maxpacket);
		ep->dma = 0;
	}

	/* set type, direction, address; reset fifo counters */
	writel ((1 << FIFO_FLUSH), &ep->regs->ep_stat);
	tmp = (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);
	if (tmp == USB_ENDPOINT_XFER_INT) {
		/* not just because of erratum 0105; avoid ever
		 * kicking in the "toggle-irrelevant" mode.
		 */
		tmp = USB_ENDPOINT_XFER_BULK;
	} else if (tmp == USB_ENDPOINT_XFER_BULK) {
		/* catch some particularly blatant driver bugs */
		if ((dev->gadget.speed == USB_SPEED_HIGH
					&& max != 512)
				|| (dev->gadget.speed == USB_SPEED_FULL
					&& max > 64))
			return -ERANGE;
	}
	ep->is_iso = (tmp == USB_ENDPOINT_XFER_ISOC) ? 1 : 0;
	tmp <<= ENDPOINT_TYPE;
	tmp |= desc->bEndpointAddress;
	tmp |= (4 << ENDPOINT_BYTE_COUNT);	/* default full fifo lines */
	tmp |= 1 << ENDPOINT_ENABLE;
	wmb ();

	/* for OUT transfers, block the rx fifo until a read is posted */
	ep->is_in = (tmp & USB_DIR_IN) != 0;
	if (!ep->is_in)
		writel ((1 << SET_NAK_OUT_PACKETS), &ep->regs->ep_rsp);

	writel (tmp, &ep->regs->ep_cfg);

#ifdef NET2280_DMA_OUT_WORKAROUND
	if (!ep->is_in)
		ep->dma = 0;
#endif

	/* enable irqs */
	if (!ep->dma) {				/* pio, per-packet */
		tmp = (1 << ep->num) | readl (&dev->regs->pciirqenb0);
		writel (tmp, &dev->regs->pciirqenb0);

		tmp = (1 << DATA_PACKET_RECEIVED_INTERRUPT_ENABLE)
			| (1 << DATA_PACKET_TRANSMITTED_INTERRUPT_ENABLE)
			| readl (&ep->regs->ep_irqenb);
		writel (tmp, &ep->regs->ep_irqenb);
	} else {				/* dma, per-request */
		tmp = (1 << (8 + ep->num));	/* completion */
		tmp |= readl (&dev->regs->pciirqenb1);
		writel (tmp, &dev->regs->pciirqenb1);

		/* for short OUT transfers, dma completions can't
		 * advance the queue; do it pio-style, by hand.
		 * NOTE erratum 0112 workaround #2
		 */
		if ((desc->bEndpointAddress & USB_DIR_IN) == 0) {
			tmp = (1 << SHORT_PACKET_TRANSFERRED_INTERRUPT_ENABLE);
			writel (tmp, &ep->regs->ep_irqenb);

			tmp = (1 << ep->num) | readl (&dev->regs->pciirqenb0);
			writel (tmp, &dev->regs->pciirqenb0);
		}
	}

	tmp = desc->bEndpointAddress;
	DEBUG (dev, "enabled %s (ep%d%s-%s) %s max %04x\n",
		_ep->name, tmp & 0x0f, DIR_STRING (tmp),
		type_string (desc->bmAttributes),
		ep->dma ? "dma" : "pio", max);

	/* pci writes may still be posted */
	spin_unlock_irqrestore (&dev->lock, flags);
	return 0;
}

static int handshake (u32 *ptr, u32 mask, u32 done, int usec)
{
	u32	result;

	do {
		result = readl (ptr);
		if (result == ~(u32)0)		/* "device unplugged" */
			return -ENODEV;
		result &= mask;
		if (result == done)
			return 0;
		udelay (1);
		usec--;
	} while (usec > 0);
	return -ETIMEDOUT;
}

static struct usb_ep_ops net2280_ep_ops;

static void ep_reset (struct net2280_regs *regs, struct net2280_ep *ep)
{
	u32		tmp;

	ep->desc = 0;
	INIT_LIST_HEAD (&ep->queue);

	ep->ep.maxpacket = ~0;
	ep->ep.ops = &net2280_ep_ops;

	/* disable the dma, irqs, endpoint... */
	if (ep->dma) {
		writel (0, &ep->dma->dmactl);
		writel (  (1 << DMA_SCATTER_GATHER_DONE_INTERRUPT)
			| (1 << DMA_TRANSACTION_DONE_INTERRUPT)
			| (1 << DMA_ABORT)
			, &ep->dma->dmastat);

		tmp = readl (&regs->pciirqenb0);
		tmp &= ~(1 << ep->num);
		writel (tmp, &regs->pciirqenb0);
	} else {
		tmp = readl (&regs->pciirqenb1);
		tmp &= ~(1 << (8 + ep->num));	/* completion */
		writel (tmp, &regs->pciirqenb1);
	}
	writel (0, &ep->regs->ep_irqenb);

	/* init to our chosen defaults, notably so that we NAK OUT
	 * packets until the driver queues a read (+note erratum 0112)
	 */
	writel (  (1 << SET_NAK_OUT_PACKETS_MODE)
		| (1 << SET_NAK_OUT_PACKETS)
		| (1 << CLEAR_EP_HIDE_STATUS_PHASE)
		| (1 << CLEAR_INTERRUPT_MODE)
		| (1 << CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE)
		| (1 << CLEAR_ENDPOINT_TOGGLE)
		| (1 << CLEAR_ENDPOINT_HALT)
		, &ep->regs->ep_rsp);

	/* scrub most status bits, and flush any fifo state */
	writel (  (1 << TIMEOUT)
		| (1 << USB_STALL_SENT)
		| (1 << USB_IN_NAK_SENT)
		| (1 << USB_IN_ACK_RCVD)
		| (1 << USB_OUT_PING_NAK_SENT)
		| (1 << USB_OUT_ACK_SENT)
		| (1 << FIFO_OVERFLOW)
		| (1 << FIFO_UNDERFLOW)
		| (1 << FIFO_FLUSH)
		| (1 << SHORT_PACKET_OUT_DONE_INTERRUPT)
		| (1 << SHORT_PACKET_TRANSFERRED_INTERRUPT)
		| (1 << DATA_PACKET_RECEIVED_INTERRUPT)
		| (1 << DATA_PACKET_TRANSMITTED_INTERRUPT)
		| (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
		| (1 << DATA_IN_TOKEN_INTERRUPT)
		, &ep->regs->ep_stat);

	/* fifo size is handled separately */
}

static void nuke (struct net2280_ep *);

static int net2280_disable (struct usb_ep *_ep)
{
	struct net2280_ep	*ep;
	unsigned long		flags;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || !ep->desc || _ep->name == ep0name)
		return -EINVAL;

	spin_lock_irqsave (&ep->dev->lock, flags);
	nuke (ep);
	ep_reset (ep->dev->regs, ep);

	VDEBUG (ep->dev, "disabled %s %s\n",
			ep->dma ? "dma" : "pio", _ep->name);

	/* synch memory views with the device */
	(void) readl (&ep->regs->ep_cfg);

	if (use_dma && !ep->dma && ep->num >= 1 && ep->num <= 4)
		ep->dma = &ep->dev->dma [ep->num - 1];

	spin_unlock_irqrestore (&ep->dev->lock, flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct usb_request *
net2280_alloc_request (struct usb_ep *_ep, int gfp_flags)
{
	struct net2280_ep	*ep;
	struct net2280_request	*req;

	if (!_ep)
		return 0;
	ep = container_of (_ep, struct net2280_ep, ep);

	req = kmalloc (sizeof *req, gfp_flags);
	if (!req)
		return 0;

	memset (req, 0, sizeof *req);
	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD (&req->queue);

	/* this dma descriptor may be swapped with the previous dummy */
	if (ep->dma) {
		struct net2280_dma	*td;

		td = pci_pool_alloc (ep->dev->requests, gfp_flags,
				&req->td_dma);
		if (!td) {
			kfree (req);
			return 0;
		}
		td->dmacount = 0;	/* not VALID */
		td->dmaaddr = __constant_cpu_to_le32 (DMA_ADDR_INVALID);
		req->td = td;
	}
	return &req->req;
}

static void
net2280_free_request (struct usb_ep *_ep, struct usb_request *_req)
{
	struct net2280_ep	*ep;
	struct net2280_request	*req;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || !_req)
		return;

	req = container_of (_req, struct net2280_request, req);
	WARN_ON (!list_empty (&req->queue));
	if (req->td)
		pci_pool_free (ep->dev->requests, req->td, req->td_dma);
	kfree (req);
}

/*-------------------------------------------------------------------------*/

#undef USE_KMALLOC

/* many common platforms have dma-coherent caches, which means that it's
 * safe to use kmalloc() memory for all i/o buffers without using any
 * cache flushing calls.  (unless you're trying to share cache lines
 * between dma and non-dma activities, which is a slow idea in any case.)
 *
 * other platforms need more care, with 2.5 having a moderately general
 * solution (which falls down for allocations smaller than one page)
 * that improves significantly on the 2.4 PCI allocators by removing
 * the restriction that memory never be freed in_interrupt().
 */
#if	defined(CONFIG_X86)
#define USE_KMALLOC

#elif	defined(CONFIG_PPC) && !defined(CONFIG_NOT_COHERENT_CACHE)
#define USE_KMALLOC

#elif	defined(CONFIG_MIPS) && !defined(CONFIG_NONCOHERENT_IO)
#define USE_KMALLOC

/* FIXME there are other cases, including an x86-64 one ...  */
#endif

/* allocating buffers this way eliminates dma mapping overhead, which
 * on some platforms will mean eliminating a per-io buffer copy.  with
 * some kinds of system caches, further tweaks may still be needed.
 */
static void *
net2280_alloc_buffer (
	struct usb_ep		*_ep,
	unsigned		bytes,
	dma_addr_t		*dma,
	int			gfp_flags
)
{
	void			*retval;
	struct net2280_ep	*ep;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep)
		return 0;
	*dma = DMA_ADDR_INVALID;

#if	defined(USE_KMALLOC)
	retval = kmalloc(bytes, gfp_flags);
	if (retval)
		*dma = virt_to_phys(retval);
#else
	if (ep->dma) {
		/* the main problem with this call is that it wastes memory
		 * on typical 1/N page allocations: it allocates 1-N pages.
		 */
#warning Using dma_alloc_coherent even with buffers smaller than a page.
		retval = dma_alloc_coherent(&ep->dev->pdev->dev,
				bytes, dma, gfp_flags);
	} else
		retval = kmalloc(bytes, gfp_flags);
#endif
	return retval;
}

static void
net2280_free_buffer (
	struct usb_ep *_ep,
	void *buf,
	dma_addr_t dma,
	unsigned bytes
) {
	/* free memory into the right allocator */
#ifndef	USE_KMALLOC
	if (dma != DMA_ADDR_INVALID) {
		struct net2280_ep	*ep;

		ep = container_of(_ep, struct net2280_ep, ep);
		if (!_ep)
			return;
		dma_free_coherent(&ep->dev->pdev->dev, bytes, buf, dma);
	} else
#endif
		kfree (buf);
}

/*-------------------------------------------------------------------------*/

/* load a packet into the fifo we use for usb IN transfers.
 * works for all endpoints.
 *
 * NOTE: pio with ep-a..ep-d could stuff multiple packets into the fifo
 * at a time, but this code is simpler because it knows it only writes
 * one packet.  ep-a..ep-d should use dma instead.
 */
static void
write_fifo (struct net2280_ep *ep, struct usb_request *req)
{
	struct net2280_ep_regs	*regs = ep->regs;
	u8			*buf;
	u32			tmp;
	unsigned		count, total;

	/* INVARIANT:  fifo is currently empty. (testable) */

	if (req) {
		buf = req->buf + req->actual;
		prefetch (buf);
		total = req->length - req->actual;
	} else {
		total = 0;
		buf = 0;
	}

	/* write just one packet at a time */
	count = min (ep->ep.maxpacket, total);
	VDEBUG (ep->dev, "write %s fifo (IN) %d bytes%s req %p\n",
			ep->ep.name, count,
			(count != ep->ep.maxpacket) ? " (short)" : "",
			req);
	while (count >= 4) {
		/* NOTE be careful if you try to align these. fifo lines
		 * should normally be full (4 bytes) and successive partial
		 * lines are ok only in certain cases.
		 */
		tmp = get_unaligned ((u32 *)buf);
		cpu_to_le32s (&tmp);
		writel (tmp, &regs->ep_data);
		buf += 4;
		count -= 4;
	}

	/* last fifo entry is "short" unless we wrote a full packet */
	if (total < ep->ep.maxpacket) {
		tmp = count ? get_unaligned ((u32 *)buf) : count;
		cpu_to_le32s (&tmp);
		set_fifo_bytecount (ep, count & 0x03);
		writel (tmp, &regs->ep_data);
	}

	/* pci writes may still be posted */
}

/* work around erratum 0106: PCI and USB race over the OUT fifo.
 * caller guarantees chiprev 0100, out endpoint is NAKing, and
 * there's no real data in the fifo.
 */
static void out_flush (struct net2280_ep *ep)
{
	u32	*statp, tmp;

	ASSERT_OUT_NAKING (ep);

	statp = &ep->regs->ep_stat;
	writel (  (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
		| (1 << DATA_PACKET_RECEIVED_INTERRUPT)
		, statp);
	writel ((1 << FIFO_FLUSH), statp);
	mb ();
	tmp = readl (statp);
	if (tmp & (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
			/* high speed did bulk NYET; fifo isn't filling */
			&& ep->dev->gadget.speed == USB_SPEED_FULL) {
		unsigned	usec;

		usec = 50;		/* 64 byte bulk/interrupt */
		handshake (statp, (1 << USB_OUT_PING_NAK_SENT),
				(1 << USB_OUT_PING_NAK_SENT), usec);
		/* NAK done; now CLEAR_NAK_OUT_PACKETS is safe */
	}
}

/* unload packet(s) from the fifo we use for usb OUT transfers.
 * returns true iff the request completed, because of short packet
 * or the request buffer having filled with full packets.
 *
 * for ep-a..ep-d this will read multiple packets out when they
 * have been accepted.
 */
static int
read_fifo (struct net2280_ep *ep, struct net2280_request *req)
{
	struct net2280_ep_regs	*regs = ep->regs;
	u8			*buf = req->req.buf + req->req.actual;
	unsigned		count, tmp, is_short;
	unsigned		cleanup = 0, prevent = 0;

	/* erratum 0106 ... packets coming in during fifo reads might
	 * be incompletely rejected.  not all cases have workarounds.
	 */
	if (ep->dev->chiprev == 0x0100) {
		tmp = readl (&ep->regs->ep_stat);
		if ((tmp & (1 << NAK_OUT_PACKETS)))
			/* cleanup = 1 */;
		else if ((tmp & (1 << FIFO_FULL))
				/* don't break hs PING protocol ... */
				|| ep->dev->gadget.speed == USB_SPEED_FULL) {
			start_out_naking (ep);
			prevent = 1;
		}
		/* else: hope we don't see the problem */
	}

	/* never overflow the rx buffer. the fifo reads packets until
	 * it sees a short one; we might not be ready for them all.
	 */
	prefetchw (buf);
	count = readl (&regs->ep_avail);
	tmp = req->req.length - req->req.actual;
	if (count > tmp) {
		/* as with DMA, data overflow gets flushed */
		if ((tmp % ep->ep.maxpacket) != 0) {
			ERROR (ep->dev,
				"%s out fifo %d bytes, expected %d\n",
				ep->ep.name, count, tmp);
			req->req.status = -EOVERFLOW;
			cleanup = 1;
		}
		count = tmp;
	}
	req->req.actual += count;

	is_short = (count == 0) || ((count % ep->ep.maxpacket) != 0);

	VDEBUG (ep->dev, "read %s fifo (OUT) %d bytes%s%s%s req %p %d/%d\n",
			ep->ep.name, count, is_short ? " (short)" : "",
			cleanup ? " flush" : "", prevent ? " nak" : "",
			req, req->req.actual, req->req.length);

	while (count >= 4) {
		tmp = readl (&regs->ep_data);
		cpu_to_le32s (&tmp);
		put_unaligned (tmp, (u32 *)buf);
		buf += 4;
		count -= 4;
	}
	if (count) {
		tmp = readl (&regs->ep_data);
		cpu_to_le32s (&tmp);
		do {
			*buf++ = (u8) tmp;
			tmp >>= 8;
		} while (--count);
	}
	if (cleanup)
		out_flush (ep);
	if (prevent) {
		writel ((1 << CLEAR_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
		(void) readl (&ep->regs->ep_rsp);
	}

	return is_short || ((req->req.actual == req->req.length)
				&& !req->req.zero);
}

/* fill out dma descriptor to match a given request */
static inline void
fill_dma_desc (struct net2280_ep *ep, struct net2280_request *req, int valid)
{
	struct net2280_dma	*td = req->td;
	u32			dmacount = req->req.length;

	/* don't let DMA continue after a short OUT packet,
	 * so overruns can't affect the next transfer.
	 * in case of overruns on max-size packets, we can't
	 * stop the fifo from filling but we can flush it.
	 */
	if (ep->is_in)
		dmacount |= (1 << DMA_DIRECTION);
	else
		dmacount |= (1 << END_OF_CHAIN);

	req->valid = valid;
	if (valid)
		dmacount |= (1 << VALID_BIT);
#ifdef USE_DMA_CHAINING
	if (!req->req.no_interrupt)
#endif
		dmacount |= (1 << DMA_DONE_INTERRUPT_ENABLE);

	/* td->dmadesc = previously set by caller */
	td->dmaaddr = cpu_to_le32p (&req->req.dma);

	/* 2280 may be polling VALID_BIT through ep->dma->dmadesc */
	wmb ();
	td->dmacount = cpu_to_le32p (&dmacount);
}

static const u32 dmactl_default =
		  (1 << DMA_CLEAR_COUNT_ENABLE)
		/* erratum 0116 workaround part 1 (use POLLING) */
		| (POLL_100_USEC << DESCRIPTOR_POLLING_RATE)
		| (1 << DMA_VALID_BIT_POLLING_ENABLE)
		| (1 << DMA_VALID_BIT_ENABLE)
		| (1 << DMA_SCATTER_GATHER_ENABLE)
		/* erratum 0116 workaround part 2 (no AUTOSTART) */
		| (1 << DMA_ENABLE);

static inline void spin_stop_dma (struct net2280_dma_regs *dma)
{
	handshake (&dma->dmactl, (1 << DMA_ENABLE), 0, 50);
}

static inline void stop_dma (struct net2280_dma_regs *dma)
{
	writel (dmactl_default & ~(1 << DMA_ENABLE), &dma->dmactl);
	spin_stop_dma (dma);
}

static void start_dma (struct net2280_ep *ep, struct net2280_request *req)
{
	u32			tmp;
	int			clear_nak = 0;
	struct net2280_dma_regs	*dma = ep->dma;

	/* FIXME can't use DMA for ZLPs */

	/* previous OUT packet might have been short */
	if (!ep->is_in && ((tmp = readl (&ep->regs->ep_stat))
		 		& (1 << NAK_OUT_PACKETS)) != 0) {
		writel ((1 << SHORT_PACKET_TRANSFERRED_INTERRUPT),
			&ep->regs->ep_stat);

		tmp = readl (&ep->regs->ep_avail);
		if (tmp == 0)
			clear_nak = 1;
		else {
			/* transfer all/some fifo data */
			writel (req->req.dma, &dma->dmaaddr);
			tmp = min (tmp, req->req.length);

			/* dma irq, faking scatterlist status */
			req->td->dmacount = cpu_to_le32 (req->req.length - tmp);
			writel ((1 << DMA_DONE_INTERRUPT_ENABLE)
				| tmp, &dma->dmacount);

			writel ((1 << DMA_ENABLE), &dma->dmactl);
			writel ((1 << DMA_START), &dma->dmastat);
			return;
		}
	}

	/* on this path we know there's no dma queue (yet) */
	WARN_ON (readl (&dma->dmactl) & (1 << DMA_ENABLE));
	tmp = dmactl_default;

	/* force packet boundaries between dma requests, but prevent the
	 * controller from automagically writing a last "short" packet
	 * (zero length) unless the driver explicitly said to do that.
	 */
	if (ep->is_in) {
		if (likely ((req->req.length % ep->ep.maxpacket) != 0
				|| req->req.zero)) {
			tmp |= (1 << DMA_FIFO_VALIDATE);
			ep->in_fifo_validate = 1;
		} else
			ep->in_fifo_validate = 0;
	}

	/* init req->td, pointing to the current dummy */
	req->td->dmadesc = cpu_to_le32 (ep->td_dma);
	fill_dma_desc (ep, req, 1);

#ifdef USE_DMA_CHAINING
	writel (  (1 << VALID_BIT)
		| (ep->is_in << DMA_DIRECTION)
		| 0, &dma->dmacount);
#else
	req->td->dmacount |= __constant_cpu_to_le32 (1 << END_OF_CHAIN);
#endif

	writel (req->td_dma, &dma->dmadesc);
	writel (tmp, &dma->dmactl);

	/* erratum 0116 workaround part 3:  pci arbiter away from net2280 */
	(void) readl (&ep->dev->pci->pcimstctl);

	writel ((1 << DMA_START), &dma->dmastat);

	/* recover from previous short read; erratum 0112 workaround #1 */
	if (clear_nak)
		writel ((1 << CLEAR_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
}

static inline void
queue_dma (struct net2280_ep *ep, struct net2280_request *req, int valid)
{
	struct net2280_dma	*end;
	dma_addr_t		tmp;

	/* swap new dummy for old, link; fill and maybe activate */
	end = ep->dummy;
	ep->dummy = req->td;
	req->td = end;

	tmp = ep->td_dma;
	ep->td_dma = req->td_dma;
	req->td_dma = tmp;

	end->dmadesc = cpu_to_le32 (ep->td_dma);

	fill_dma_desc (ep, req, valid);
}

static void
done (struct net2280_ep *ep, struct net2280_request *req, int status)
{
	struct net2280		*dev;
	unsigned		stopped = ep->stopped;

	list_del_init (&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	dev = ep->dev;
	if (req->mapped) {
		pci_unmap_single (dev->pdev, req->req.dma, req->req.length,
			ep->is_in ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
	}

	if (status && status != -ESHUTDOWN)
		VDEBUG (dev, "complete %s req %p stat %d len %u/%u\n",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	spin_unlock (&dev->lock);
	req->req.complete (&ep->ep, &req->req);
	spin_lock (&dev->lock);
	ep->stopped = stopped;
}

/*-------------------------------------------------------------------------*/

static int
net2280_queue (struct usb_ep *_ep, struct usb_request *_req, int gfp_flags)
{
	struct net2280_request	*req;
	struct net2280_ep	*ep;
	struct net2280		*dev;
	unsigned long		flags;

	/* we always require a cpu-view buffer, so that we can
	 * always use pio (as fallback or whatever).
	 */
	req = container_of (_req, struct net2280_request, req);
	if (!_req || !_req->complete || !_req->buf
			|| !list_empty (&req->queue))
		return -EINVAL;
	if (_req->length > (~0 & DMA_BYTE_COUNT_MASK))
		return -EDOM;
	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0))
		return -EINVAL;
	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	/* FIXME implement PIO fallback for ZLPs with DMA */
	if (ep->dma && _req->length == 0)
		return -EOPNOTSUPP;

	/* set up dma mapping in case the caller didn't */
	if (ep->dma && _req->dma == DMA_ADDR_INVALID) {
		_req->dma = pci_map_single (dev->pdev, _req->buf, _req->length,
			ep->is_in ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
		req->mapped = 1;
	}

#if 0
	VDEBUG (dev, "%s queue req %p, len %d buf %p\n",
			_ep->name, _req, _req->length, _req->buf);
#endif

	spin_lock_irqsave (&dev->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;
	req->dma_done = 0;

	/* kickstart this i/o queue? */
	if (list_empty (&ep->queue) && !ep->stopped) {
		/* use DMA if the endpoint supports it, else pio */
		if (ep->dma)
			start_dma (ep, req);
		else {
			/* maybe there's no control data, just status ack */
			if (ep->num == 0 && _req->length == 0) {
				allow_status (ep);
				done (ep, req, 0);
				VDEBUG (dev, "%s status ack\n", ep->ep.name);
				goto done;
			}

			/* PIO ... stuff the fifo, or unblock it.  */
			if (ep->is_in)
				write_fifo (ep, _req);
			else if (list_empty (&ep->queue)) {
				u32	s;

				/* OUT FIFO might have packet(s) buffered */
				s = readl (&ep->regs->ep_stat);
				if ((s & (1 << FIFO_EMPTY)) == 0) {
					/* note:  _req->short_not_ok is
					 * ignored here since PIO _always_
					 * stops queue advance here, and
					 * _req->status doesn't change for
					 * short reads (only _req->actual)
					 */
					if (read_fifo (ep, req)) {
						done (ep, req, 0);
						if (ep->num == 0)
							allow_status (ep);
						/* don't queue it */
						req = 0;
					} else
						s = readl (&ep->regs->ep_stat);
				}

				/* don't NAK, let the fifo fill */
				if (req && (s & (1 << NAK_OUT_PACKETS)))
					writel ((1 << CLEAR_NAK_OUT_PACKETS),
							&ep->regs->ep_rsp);
			}
		}

	} else if (ep->dma) {
		int	valid = 1;

		if (ep->is_in) {
			int	expect;

			/* preventing magic zlps is per-engine state, not
			 * per-transfer; irq logic must recover hiccups.
			 */
			expect = likely (req->req.zero
				|| (req->req.length % ep->ep.maxpacket) != 0);
			if (expect != ep->in_fifo_validate)
				valid = 0;
		}
		queue_dma (ep, req, valid);

	} /* else the irq handler advances the queue. */

	if (req)
		list_add_tail (&req->queue, &ep->queue);
done:
	spin_unlock_irqrestore (&dev->lock, flags);

	/* pci writes may still be posted */
	return 0;
}

static inline void
dma_done (
	struct net2280_ep *ep,
	struct net2280_request *req,
	u32 dmacount,
	int status
)
{
	req->req.actual = req->req.length - (DMA_BYTE_COUNT_MASK & dmacount);
	rmb ();
	done (ep, req, status);
}

static void scan_dma_completions (struct net2280_ep *ep)
{
	/* only look at descriptors that were "naturally" retired,
	 * so fifo and list head state won't matter
	 */
	while (!list_empty (&ep->queue)) {
		struct net2280_request	*req;
		u32			tmp;

		req = list_entry (ep->queue.next,
				struct net2280_request, queue);
		if (!req->valid)
			break;
		rmb ();
		tmp = le32_to_cpup (&req->td->dmacount);
		if ((tmp & (1 << VALID_BIT)) != 0)
			break;

		/* SHORT_PACKET_TRANSFERRED_INTERRUPT handles "usb-short"
		 * packets, including overruns, even when the transfer was
		 * exactly the length requested (dmacount now zero).
		 * FIXME there's an overrun case here too, where we expect
		 * a short packet but receive a max length one (won't NAK).
		 */
		if (!ep->is_in && (req->req.length % ep->ep.maxpacket) != 0) {
			req->dma_done = 1;
			break;
		}
		dma_done (ep, req, tmp, 0);
	}
}

static void restart_dma (struct net2280_ep *ep)
{
	struct net2280_request	*req;

	if (ep->stopped)
		return;
	req = list_entry (ep->queue.next, struct net2280_request, queue);

#ifdef USE_DMA_CHAINING
	/* the 2280 will be processing the queue unless queue hiccups after
	 * the previous transfer:
	 *  IN:   wanted automagic zlp, head doesn't (or vice versa)
	 *  OUT:  was "usb-short", we must restart.
	 */
	if (!req->valid) {
		struct net2280_request	*entry, *prev = 0;
		int			qmode, reqmode, done = 0;

		DEBUG (ep->dev, "%s dma hiccup td %p\n", ep->ep.name, req->td);
		qmode = likely (req->req.zero
			|| (req->req.length % ep->ep.maxpacket) != 0);
		list_for_each_entry (entry, &ep->queue, queue) {
			u32		dmacount;

			if (entry != req)
				continue;
			dmacount = entry->td->dmacount;
			if (!done) {
				reqmode = likely (entry->req.zero
					|| (entry->req.length
						% ep->ep.maxpacket) != 0);
				if (reqmode == qmode) {
					entry->valid = 1;
					dmacount |= valid_bit;
					entry->td->dmacount = dmacount;
					prev = entry;
					continue;
				} else {
					prev->td->dmacount |= dma_done_ie;
					done = 1;
				}
			}

			/* walk the rest of the queue so unlinks behave */
			entry->valid = 0;
			dmacount &= ~valid_bit;
			entry->td->dmacount = dmacount;
			prev = entry;
		}
		start_dma (ep, req);
	} else if (!ep->is_in
			&& (readl (&ep->regs->ep_stat)
				& (1 << NAK_OUT_PACKETS)) != 0)
		start_dma (ep, req);
#else
	start_dma (ep, req);
#endif
}

static inline void abort_dma (struct net2280_ep *ep)
{
	/* abort the current transfer */
	writel ((1 << DMA_ABORT), &ep->dma->dmastat);

	/* collect completed transfers (except the current one) */
	scan_dma_completions (ep);
}

/* dequeue ALL requests */
static void nuke (struct net2280_ep *ep)
{
	struct net2280_request	*req;

	/* called with spinlock held */
	ep->stopped = 1;
	if (ep->dma)
		abort_dma (ep);
	while (!list_empty (&ep->queue)) {
		req = list_entry (ep->queue.next,
				struct net2280_request,
				queue);
		done (ep, req, -ESHUTDOWN);
	}
}

/* dequeue JUST ONE request */
static int net2280_dequeue (struct usb_ep *_ep, struct usb_request *_req)
{
	struct net2280_ep	*ep;
	struct net2280_request	*req;
	unsigned long		flags;
	u32			dmactl;
	int			stopped;

	ep = container_of (_ep, struct net2280_ep, ep);
	req = container_of (_req, struct net2280_request, req);
	if (!_ep || (!ep->desc && ep->num != 0) || !_req)
		return -EINVAL;

	spin_lock_irqsave (&ep->dev->lock, flags);
	stopped = ep->stopped;

	/* pause dma while we scan the queue */
	dmactl = 0;
	ep->stopped = 1;
	if (ep->dma) {
		dmactl = readl (&ep->dma->dmactl);
		writel (dmactl & ~(1 << DMA_ENABLE), &ep->dma->dmactl);
		/* force synch, clean any completed requests */
		spin_stop_dma (ep->dma);
		scan_dma_completions (ep);
	}

	/* queue head may be partially complete. */
	if (ep->queue.next == &req->queue) {
		if (ep->dma) {
			DEBUG (ep->dev, "unlink (%s) dma\n", _ep->name);
			_req->status = -ECONNRESET;
			abort_dma (ep);
			if (likely (ep->queue.next == &req->queue))
				dma_done (ep, req,
					le32_to_cpup (&req->td->dmacount),
					-ECONNRESET);
		} else {
			DEBUG (ep->dev, "unlink (%s) pio\n", _ep->name);
			done (ep, req, -ECONNRESET);
		}
		req = 0;

#ifdef USE_DMA_CHAINING
	/* patch up hardware chaining data */
	} else if (ep->dma) {
		if (req->queue.prev == ep->queue.next) {
			writel (le32_to_cpu (req->td->dmadesc),
				&ep->dma->dmadesc);
			if (req->td->dmacount & dma_done_ie)
				writel (readl (&ep->dma->dmacount)
						| dma_done_ie,
					&ep->dma->dmacount);
		} else {
			struct net2280_request	*prev;

			prev = list_entry (req->queue.prev,
				struct net2280_request, queue);
			prev->td->dmadesc = req->td->dmadesc;
			if (req->td->dmacount & dma_done_ie)
				prev->td->dmacount |= dma_done_ie;
		}
#endif
	}

	if (req)
		done (ep, req, -ECONNRESET);
	ep->stopped = stopped;

	if (ep->dma) {
		/* turn off dma on inactive queues */
		if (list_empty (&ep->queue))
			stop_dma (ep->dma);
		else if (!ep->stopped) {
			/* resume current request, or start new one */
			if (req)
				writel (dmactl, &ep->dma->dmactl);
			else
				start_dma (ep, list_entry (ep->queue.next,
					struct net2280_request, queue));
		}
	}

	spin_unlock_irqrestore (&ep->dev->lock, flags);
	return req ? 0 : -EOPNOTSUPP;
}

/*-------------------------------------------------------------------------*/

static int
net2280_set_halt (struct usb_ep *_ep, int value)
{
	struct net2280_ep	*ep;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0))
		return -EINVAL;
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;
	if (ep->desc /* not ep0 */ && (ep->desc->bmAttributes & 0x03)
						== USB_ENDPOINT_XFER_ISOC)
		return -EINVAL;

	VDEBUG (ep->dev, "%s %s halt\n", _ep->name, value ? "set" : "clear");

	/* set/clear, then synch memory views with the device */
	if (value) {
		if (ep->num == 0)
			ep->dev->protocol_stall = 1;
		else
			set_halt (ep);
	} else
		clear_halt (ep);
	(void) readl (&ep->regs->ep_rsp);

	return 0;
}

static int
net2280_fifo_status (struct usb_ep *_ep)
{
	struct net2280_ep	*ep;
	u32			avail;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0))
		return -ENODEV;
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	avail = readl (&ep->regs->ep_avail) & ((1 << 12) - 1);
	if (avail > ep->fifo_size)
		return -EOVERFLOW;
	if (ep->is_in)
		avail = ep->fifo_size - avail;
	return avail;
}

static void
net2280_fifo_flush (struct usb_ep *_ep)
{
	struct net2280_ep	*ep;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0))
		return;
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return;

	writel ((1 << FIFO_FLUSH), &ep->regs->ep_stat);
	(void) readl (&ep->regs->ep_rsp);
}

static struct usb_ep_ops net2280_ep_ops = {
	.enable		= net2280_enable,
	.disable	= net2280_disable,

	.alloc_request	= net2280_alloc_request,
	.free_request	= net2280_free_request,

	.alloc_buffer	= net2280_alloc_buffer,
	.free_buffer	= net2280_free_buffer,

	.queue		= net2280_queue,
	.dequeue	= net2280_dequeue,

	.set_halt	= net2280_set_halt,
	.fifo_status	= net2280_fifo_status,
	.fifo_flush	= net2280_fifo_flush,
};

/*-------------------------------------------------------------------------*/

static int net2280_get_frame (struct usb_gadget *_gadget)
{
	struct net2280		*dev;
	unsigned long		flags;
	u16			retval;

	if (!_gadget)
		return -ENODEV;
	dev = container_of (_gadget, struct net2280, gadget);
	spin_lock_irqsave (&dev->lock, flags);
	retval = get_idx_reg (dev->regs, REG_FRAME) & 0x03ff;
	spin_unlock_irqrestore (&dev->lock, flags);
	return retval;
}

static int net2280_wakeup (struct usb_gadget *_gadget)
{
	struct net2280		*dev;

	if (!_gadget)
		return 0;
	dev = container_of (_gadget, struct net2280, gadget);
	writel (1 << GENERATE_RESUME, &dev->usb->usbstat);

	/* pci writes may still be posted */
	return 0;
}

static const struct usb_gadget_ops net2280_ops = {
	.get_frame	= net2280_get_frame,
	.wakeup		= net2280_wakeup,

	// .set_selfpowered = net2280_set_selfpowered,
};

/*-------------------------------------------------------------------------*/

#ifdef	USE_SYSFS_DEBUG_FILES

/* "function" sysfs attribute */
static ssize_t
show_function (struct device *_dev, char *buf)
{
	struct net2280	*dev = dev_get_drvdata (_dev);

	if (!dev->driver
			|| !dev->driver->function
			|| strlen (dev->driver->function) > PAGE_SIZE)
		return 0;
	return snprintf (buf, PAGE_SIZE, "%s\n", dev->driver->function);
}
static DEVICE_ATTR (function, S_IRUGO, show_function, NULL);

static ssize_t
show_registers (struct device *_dev, char *buf)
{
	struct net2280		*dev;
	char			*next;
	unsigned		size, t;
	unsigned long		flags;
	int			i;
	u32			t1, t2;
	char			*s;

	dev = dev_get_drvdata (_dev);
	next = buf;
	size = PAGE_SIZE;
	spin_lock_irqsave (&dev->lock, flags);

	if (dev->driver)
		s = dev->driver->driver.name;
	else
		s = "(none)";

	/* Main Control Registers */
	t = snprintf (next, size, "%s version " DRIVER_VERSION
			", chiprev %04x\n"
			"devinit %03x fifoctl %08x gadget '%s'\n"
			"pci irqenb0 %02x irqenb1 %08x "
			"irqstat0 %04x irqstat1 %08x\n",
			driver_name, dev->chiprev,
			readl (&dev->regs->devinit),
			readl (&dev->regs->fifoctl),
			s,
			readl (&dev->regs->pciirqenb0),
			readl (&dev->regs->pciirqenb1),
			readl (&dev->regs->irqstat0),
			readl (&dev->regs->irqstat1));
	size -= t;
	next += t;

	/* USB Control Registers */
	t1 = readl (&dev->usb->usbctl);
	t2 = readl (&dev->usb->usbstat);
	if (t1 & (1 << VBUS_PIN)) {
		if (t2 & (1 << HIGH_SPEED))
			s = "high speed";
		else if (dev->gadget.speed == USB_SPEED_UNKNOWN)
			s = "powered";
		else
			s = "full speed";
		/* full speed bit (6) not working?? */
	} else
			s = "not attached";
	t = snprintf (next, size,
			"stdrsp %08x usbctl %08x usbstat %08x "
				"addr 0x%02x (%s)\n",
			readl (&dev->usb->stdrsp), t1, t2,
			readl (&dev->usb->ouraddr), s);
	size -= t;
	next += t;

	/* PCI Master Control Registers */

	/* DMA Control Registers */

	/* Configurable EP Control Registers */
	for (i = 0; i < 7; i++) {
		struct net2280_ep	*ep;

		ep = &dev->ep [i];
		if (i && !ep->desc)
			continue;

		t1 = readl (&ep->regs->ep_cfg);
		t2 = readl (&ep->regs->ep_rsp) & 0xff;
		t = snprintf (next, size,
				"%s\tcfg %05x rsp (%02x) %s%s%s%s%s%s%s%s"
					"irqenb %02x\n",
				ep->ep.name, t1, t2,
				(t2 & (1 << CLEAR_NAK_OUT_PACKETS))
					? "NAK " : "",
				(t2 & (1 << CLEAR_EP_HIDE_STATUS_PHASE))
					? "hide " : "",
				(t2 & (1 << CLEAR_EP_FORCE_CRC_ERROR))
					? "CRC " : "",
				(t2 & (1 << CLEAR_INTERRUPT_MODE))
					? "interrupt " : "",
				(t2 & (1<<CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE))
					? "status " : "",
				(t2 & (1 << CLEAR_NAK_OUT_PACKETS_MODE))
					? "NAKmode " : "",
				(t2 & (1 << CLEAR_ENDPOINT_TOGGLE))
					? "DATA1 " : "DATA0 ",
				(t2 & (1 << CLEAR_ENDPOINT_HALT))
					? "HALT " : "",
				readl (&ep->regs->ep_irqenb));
		size -= t;
		next += t;

		t = snprintf (next, size,
				"\tstat %08x avail %04x "
				"(ep%d%s-%s)%s\n",
				readl (&ep->regs->ep_stat),
				readl (&ep->regs->ep_avail),
				t1 & 0x0f, DIR_STRING (t1),
				type_string (t1 >> 8),
				ep->stopped ? "*" : "");
		size -= t;
		next += t;

		if (!ep->dma)
			continue;

		t = snprintf (next, size,
				"  dma\tctl %08x stat %08x count %08x\n"
				"\taddr %08x desc %08x\n",
				readl (&ep->dma->dmactl),
				readl (&ep->dma->dmastat),
				readl (&ep->dma->dmacount),
				readl (&ep->dma->dmaaddr),
				readl (&ep->dma->dmadesc));
		size -= t;
		next += t;

	}

	/* Indexed Registers */
		// none yet 

	/* Statistics */
	t = snprintf (next, size, "irqs:  ");
	size -= t;
	next += t;
	for (i = 0; i < 7; i++) {
		struct net2280_ep	*ep;

		ep = &dev->ep [i];
		if (i && !ep->irqs)
			continue;
		t = snprintf (next, size, " %s/%ld", ep->ep.name, ep->irqs);
		size -= t;
		next += t;

	}
	t = snprintf (next, size, "\n");
	size -= t;
	next += t;

	spin_unlock_irqrestore (&dev->lock, flags);

	return PAGE_SIZE - size;
}
static DEVICE_ATTR (registers, S_IRUGO, show_registers, NULL);

static ssize_t
show_queues (struct device *_dev, char *buf)
{
	struct net2280		*dev;
	char			*next;
	unsigned		size;
	unsigned long		flags;
	int			i;

	dev = dev_get_drvdata (_dev);
	next = buf;
	size = PAGE_SIZE;
	spin_lock_irqsave (&dev->lock, flags);

	for (i = 0; i < 7; i++) {
		struct net2280_ep		*ep = &dev->ep [i];
		struct net2280_request		*req;
		int				t;

		if (i != 0) {
			const struct usb_endpoint_descriptor	*d;

			d = ep->desc;
			if (!d)
				continue;
			t = d->bEndpointAddress;
			t = snprintf (next, size,
				"%s (ep%d%s-%s) max %04x %s\n",
				ep->ep.name, t & USB_ENDPOINT_NUMBER_MASK,
				(t & USB_DIR_IN) ? "in" : "out",
				({ char *val;
				 switch (d->bmAttributes & 0x03) {
				 case USB_ENDPOINT_XFER_BULK:
				 	val = "bulk"; break;
				 case USB_ENDPOINT_XFER_INT:
				 	val = "intr"; break;
				 default:
				 	val = "iso"; break;
				 }; val; }),
				le16_to_cpu (d->wMaxPacketSize) & 0x1fff,
				ep->dma ? "dma" : "pio"
				);
		} else /* ep0 should only have one transfer queued */
			t = snprintf (next, size, "ep0 max 64 pio %s\n",
					ep->is_in ? "in" : "out");
		if (t <= 0 || t > size)
			goto done;
		size -= t;
		next += t;

		if (list_empty (&ep->queue)) {
			t = snprintf (next, size, "\t(nothing queued)\n");
			if (t <= 0 || t > size)
				goto done;
			size -= t;
			next += t;
			continue;
		}
		list_for_each_entry (req, &ep->queue, queue) {
			if (ep->dma && req->td_dma == readl (&ep->dma->dmadesc))
				t = snprintf (next, size,
					"\treq %p len %d/%d "
					"buf %p (dmacount %08x)\n",
					&req->req, req->req.actual,
					req->req.length, req->req.buf,
					readl (&ep->dma->dmacount));
			else
				t = snprintf (next, size,
					"\treq %p len %d/%d buf %p\n",
					&req->req, req->req.actual,
					req->req.length, req->req.buf);
			if (t <= 0 || t > size)
				goto done;
			size -= t;
			next += t;
		}
	}

done:
	spin_unlock_irqrestore (&dev->lock, flags);
	return PAGE_SIZE - size;
}
static DEVICE_ATTR (queues, S_IRUGO, show_queues, NULL);


#else

#define device_create_file(a,b)	do {} while (0)
#define device_remove_file	device_create_file

#endif

/*-------------------------------------------------------------------------*/

/* another driver-specific mode might be a request type doing dma
 * to/from another device fifo instead of to/from memory.
 */

static void set_fifo_mode (struct net2280 *dev, int mode)
{
	/* keeping high bits preserves BAR2 */
	writel ((0xffff << PCI_BASE2_RANGE) | mode, &dev->regs->fifoctl);

	/* always ep-{a,b,e,f} ... maybe not ep-c or ep-d */
	INIT_LIST_HEAD (&dev->gadget.ep_list);
	list_add_tail (&dev->ep [1].ep.ep_list, &dev->gadget.ep_list);
	list_add_tail (&dev->ep [2].ep.ep_list, &dev->gadget.ep_list);
	switch (mode) {
	case 0:
		list_add_tail (&dev->ep [3].ep.ep_list, &dev->gadget.ep_list);
		list_add_tail (&dev->ep [4].ep.ep_list, &dev->gadget.ep_list);
		dev->ep [1].fifo_size = dev->ep [2].fifo_size = 1024;
		break;
	case 1:
		dev->ep [1].fifo_size = dev->ep [2].fifo_size = 2048;
		break;
	case 2:
		list_add_tail (&dev->ep [3].ep.ep_list, &dev->gadget.ep_list);
		dev->ep [1].fifo_size = 2048;
		dev->ep [2].fifo_size = 1024;
		break;
	}
	/* fifo sizes for ep0, ep-c, ep-d, ep-e, and ep-f never change */
	list_add_tail (&dev->ep [5].ep.ep_list, &dev->gadget.ep_list);
	list_add_tail (&dev->ep [6].ep.ep_list, &dev->gadget.ep_list);
}

/**
 * net2280_set_fifo_mode - change allocation of fifo buffers
 * @gadget: access to the net2280 device that will be updated
 * @mode: 0 for default, four 1kB buffers (ep-a through ep-d);
 * 	1 for two 2kB buffers (ep-a and ep-b only);
 * 	2 for one 2kB buffer (ep-a) and two 1kB ones (ep-b, ep-c).
 *
 * returns zero on success, else negative errno.  when this succeeds,
 * the contents of gadget->ep_list may have changed.
 *
 * you may only call this function when endpoints a-d are all disabled.
 * use it whenever extra hardware buffering can help performance, such
 * as before enabling "high bandwidth" interrupt endpoints that use
 * maxpacket bigger than 512 (when double buffering would otherwise
 * be unavailable).
 */
int net2280_set_fifo_mode (struct usb_gadget *gadget, int mode)
{
	int			i;
	struct net2280		*dev;
	int			status = 0;
	unsigned long		flags;

	if (!gadget)
		return -ENODEV;
	dev = container_of (gadget, struct net2280, gadget);

	spin_lock_irqsave (&dev->lock, flags);

	for (i = 1; i <= 4; i++)
		if (dev->ep [i].desc) {
			status = -EINVAL;
			break;
		}
	if (mode < 0 || mode > 2)
		status = -EINVAL;
	if (status == 0)
		set_fifo_mode (dev, mode);
	spin_unlock_irqrestore (&dev->lock, flags);

	if (status == 0) {
		if (mode == 1)
			DEBUG (dev, "fifo:  ep-a 2K, ep-b 2K\n");
		else if (mode == 2)
			DEBUG (dev, "fifo:  ep-a 2K, ep-b 1K, ep-c 1K\n");
		/* else all are 1K */
	}
	return status;
}
EXPORT_SYMBOL (net2280_set_fifo_mode);

/*-------------------------------------------------------------------------*/

/* keeping it simple:
 * - one bus driver, initted first;
 * - one function driver, initted second
 *
 * most of the work to support multiple net2280 controllers would
 * be to associate this gadget driver (yes?) with all of them, or
 * perhaps to bind specific drivers to specific devices.
 */

static struct net2280	*the_controller;

static void usb_reset (struct net2280 *dev)
{
	u32	tmp;

	/* force immediate bus disconnect, and synch through pci */
	writel (0, &dev->usb->usbctl);
	dev->gadget.speed = USB_SPEED_UNKNOWN;
	(void) readl (&dev->usb->usbctl);

	net2280_led_init (dev);

	/* disable automatic responses, and irqs */
	writel (0, &dev->usb->stdrsp);
	writel (0, &dev->regs->pciirqenb0);
	writel (0, &dev->regs->pciirqenb1);

	/* clear old dma and irq state */
	for (tmp = 0; tmp < 4; tmp++) {
		writel ((1 << DMA_ABORT), &dev->dma [tmp].dmastat);
		stop_dma (&dev->dma [tmp]);
	}
	writel (~0, &dev->regs->irqstat0),
	writel (~(1 << SUSPEND_REQUEST_INTERRUPT), &dev->regs->irqstat1),

	/* reset, and enable pci */
	tmp = readl (&dev->regs->devinit)
		| (1 << PCI_ENABLE)
		| (1 << FIFO_SOFT_RESET)
		| (1 << USB_SOFT_RESET)
		| (1 << M8051_RESET);
	writel (tmp, &dev->regs->devinit);

	/* standard fifo and endpoint allocations */
	set_fifo_mode (dev, (fifo_mode <= 2) ? fifo_mode : 0);
}

static void usb_reinit (struct net2280 *dev)
{
	u32	tmp;
	int	init_dma;

	/* use_dma changes are ignored till next device re-init */
	init_dma = use_dma;

	/* basic endpoint init */
	for (tmp = 0; tmp < 7; tmp++) {
		struct net2280_ep	*ep = &dev->ep [tmp];

		ep->ep.name = ep_name [tmp];
		ep->dev = dev;
		ep->num = tmp;

		if (tmp > 0 && tmp <= 4) {
			ep->fifo_size = 1024;
			if (init_dma)
				ep->dma = &dev->dma [tmp - 1];
		} else
			ep->fifo_size = 64;
		ep->regs = &dev->epregs [tmp];
		ep_reset (dev->regs, ep);
	}
	dev->ep [0].ep.maxpacket = 64;
	dev->ep [5].ep.maxpacket = 64;
	dev->ep [6].ep.maxpacket = 64;

	dev->gadget.ep0 = &dev->ep [0].ep;
	dev->ep [0].stopped = 0;
	INIT_LIST_HEAD (&dev->gadget.ep0->ep_list);

	/* we want to prevent lowlevel/insecure access from the USB host,
	 * but erratum 0119 means this enable bit is ignored
	 */
	for (tmp = 0; tmp < 5; tmp++)
		writel (EP_DONTUSE, &dev->dep [tmp].dep_cfg);
}

static void ep0_start (struct net2280 *dev)
{
	writel (  (1 << CLEAR_EP_HIDE_STATUS_PHASE)
		| (1 << CLEAR_NAK_OUT_PACKETS)
		| (1 << CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE)
		, &dev->epregs [0].ep_rsp);

	/*
	 * hardware optionally handles a bunch of standard requests
	 * that the API hides from drivers anyway.  have it do so.
	 * endpoint status/features are handled in software, to
	 * help pass tests for some dubious behavior.
	 */
	writel (  (1 << SET_TEST_MODE)
		| (1 << SET_ADDRESS)
		| (1 << DEVICE_SET_CLEAR_DEVICE_REMOTE_WAKEUP)
		| (1 << GET_DEVICE_STATUS)
		| (1 << GET_INTERFACE_STATUS)
		, &dev->usb->stdrsp);
	writel (  (1 << USB_ROOT_PORT_WAKEUP_ENABLE)
		| (1 << SELF_POWERED_USB_DEVICE)
		| (1 << REMOTE_WAKEUP_SUPPORT)
		| (1 << USB_DETECT_ENABLE)
		| (1 << DEVICE_REMOTE_WAKEUP_ENABLE)
		, &dev->usb->usbctl);

	/* enable irqs so we can see ep0 and general operation  */
	writel (  (1 << SETUP_PACKET_INTERRUPT_ENABLE)
		| (1 << ENDPOINT_0_INTERRUPT_ENABLE)
		, &dev->regs->pciirqenb0);
	writel (  (1 << PCI_INTERRUPT_ENABLE)
		| (1 << PCI_MASTER_ABORT_RECEIVED_INTERRUPT_ENABLE)
		| (1 << PCI_TARGET_ABORT_RECEIVED_INTERRUPT_ENABLE)
		| (1 << PCI_RETRY_ABORT_INTERRUPT_ENABLE)
		| (1 << VBUS_INTERRUPT_ENABLE)
		| (1 << ROOT_PORT_RESET_INTERRUPT_ENABLE)
		, &dev->regs->pciirqenb1);

	/* don't leave any writes posted */
	(void) readl (&dev->usb->usbctl);
}

/* when a driver is successfully registered, it will receive
 * control requests including set_configuration(), which enables
 * non-control requests.  then usb traffic follows until a
 * disconnect is reported.  then a host may connect again, or
 * the driver might get unbound.
 */
int usb_gadget_register_driver (struct usb_gadget_driver *driver)
{
	struct net2280		*dev = the_controller;
	int			retval;
	unsigned		i;

	/* insist on high speed support from the driver, since
	 * (dev->usb->xcvrdiag & FORCE_FULL_SPEED_MODE)
	 * "must not be used in normal operation"
	 */
	if (!driver
			|| driver->speed != USB_SPEED_HIGH
			|| !driver->bind
			|| !driver->unbind
			|| !driver->setup)
		return -EINVAL;
	if (!dev)
		return -ENODEV;
	if (dev->driver)
		return -EBUSY;

	for (i = 0; i < 7; i++)
		dev->ep [i].irqs = 0;

	/* hook up the driver ... */
	driver->driver.bus = 0;
	dev->driver = driver;
	dev->gadget.dev.driver = &driver->driver;
	retval = driver->bind (&dev->gadget);
	if (retval) {
		DEBUG (dev, "bind to driver %s --> %d\n",
				driver->driver.name, retval);
		dev->driver = 0;
		dev->gadget.dev.driver = 0;
		return retval;
	}

	device_create_file (&dev->pdev->dev, &dev_attr_function);
	device_create_file (&dev->pdev->dev, &dev_attr_queues);

	/* ... then enable host detection and ep0; and we're ready
	 * for set_configuration as well as eventual disconnect.
	 */
	net2280_led_active (dev, 1);
	ep0_start (dev);

	DEBUG (dev, "%s ready, usbctl %08x stdrsp %08x\n",
			driver->driver.name,
			readl (&dev->usb->usbctl),
			readl (&dev->usb->stdrsp));

	/* pci writes may still be posted */
	return 0;
}
EXPORT_SYMBOL (usb_gadget_register_driver);

static void
stop_activity (struct net2280 *dev, struct usb_gadget_driver *driver)
{
	int			i;

	/* don't disconnect if it's not connected */
	if (dev->gadget.speed == USB_SPEED_UNKNOWN)
		driver = 0;

	/* stop hardware; prevent new request submissions;
	 * and kill any outstanding requests.
	 */
	usb_reset (dev);
	for (i = 0; i < 7; i++)
		nuke (&dev->ep [i]);

	/* report disconnect; the driver is already quiesced */
	if (driver) {
		spin_unlock (&dev->lock);
		driver->disconnect (&dev->gadget);
		spin_lock (&dev->lock);
	}

	usb_reinit (dev);
}

int usb_gadget_unregister_driver (struct usb_gadget_driver *driver)
{
	struct net2280	*dev = the_controller;
	unsigned long	flags;

	if (!dev)
		return -ENODEV;
	if (!driver || driver != dev->driver)
		return -EINVAL;

	spin_lock_irqsave (&dev->lock, flags);
	stop_activity (dev, driver);
	spin_unlock_irqrestore (&dev->lock, flags);

	driver->unbind (&dev->gadget);
	dev->driver = 0;

	net2280_led_active (dev, 0);
	device_remove_file (&dev->pdev->dev, &dev_attr_function);
	device_remove_file (&dev->pdev->dev, &dev_attr_queues);

	DEBUG (dev, "unregistered driver '%s'\n", driver->driver.name);
	return 0;
}
EXPORT_SYMBOL (usb_gadget_unregister_driver);


/*-------------------------------------------------------------------------*/

/* handle ep0, ep-e, ep-f with 64 byte packets: packet per irq.
 * also works for dma-capable endpoints, in pio mode or just
 * to manually advance the queue after short OUT transfers.
 */
static void handle_ep_small (struct net2280_ep *ep)
{
	struct net2280_request	*req;
	u32			t;
	/* 0 error, 1 mid-data, 2 done */
	int			mode = 1;

	if (!list_empty (&ep->queue))
		req = list_entry (ep->queue.next,
			struct net2280_request, queue);
	else
		req = 0;

	/* ack all, and handle what we care about */
	t = readl (&ep->regs->ep_stat);
	ep->irqs++;
#if 0
	VDEBUG (ep->dev, "%s ack ep_stat %08x, req %p\n",
			ep->ep.name, t, req ? &req->req : 0);
#endif
	writel (t & ~(1 << NAK_OUT_PACKETS), &ep->regs->ep_stat);

	/* for ep0, monitor token irqs to catch data stage length errors
	 * and to synchronize on status.
	 *
	 * also, to defer reporting of protocol stalls ... here's where
	 * data or status first appears, handling stalls here should never
	 * cause trouble on the host side..
	 *
	 * control requests could be slightly faster without token synch for
	 * status, but status can jam up that way.
	 */
	if (unlikely (ep->num == 0)) {
		if (ep->is_in) {
			/* status; stop NAKing */
			if (t & (1 << DATA_OUT_PING_TOKEN_INTERRUPT)) {
				if (ep->dev->protocol_stall) {
					ep->stopped = 1;
					set_halt (ep);
				}
				mode = 2;
			/* reply to extra IN data tokens with a zlp */
			} else if (t & (1 << DATA_IN_TOKEN_INTERRUPT)) {
				if (ep->dev->protocol_stall) {
					ep->stopped = 1;
					set_halt (ep);
					mode = 2;
				} else if (!req && ep->stopped)
					write_fifo (ep, 0);
			}
		} else {
			/* status; stop NAKing */
			if (t & (1 << DATA_IN_TOKEN_INTERRUPT)) {
				if (ep->dev->protocol_stall) {
					ep->stopped = 1;
					set_halt (ep);
				}
				mode = 2;
			/* an extra OUT token is an error */
			} else if (((t & (1 << DATA_OUT_PING_TOKEN_INTERRUPT))
					&& req
					&& req->req.actual == req->req.length)
					|| !req) {
				ep->dev->protocol_stall = 1;
				set_halt (ep);
				ep->stopped = 1;
				if (req)
					done (ep, req, -EOVERFLOW);
				req = 0;
			}
		}
	}

	if (unlikely (!req))
		return;

	/* manual DMA queue advance after short OUT */
	if (likely (ep->dma != 0)) {
		if (t & (1 << SHORT_PACKET_TRANSFERRED_INTERRUPT)) {
			u32	count;

			/* TRANSFERRED works around OUT_DONE erratum 0112.
			 * we expect (N <= maxpacket) bytes; host wrote M.
			 * iff (M < N) we won't ever see a DMA interrupt.
			 */
			count = readl (&ep->dma->dmacount);
			count &= DMA_BYTE_COUNT_MASK;
			if (!req->dma_done) {
				/* dma can finish with the FIFO non-empty,
				 * on (M > N) errors.
				 */
				while (count && (t & (1 << FIFO_EMPTY)) == 0) {
					cpu_relax ();
					t = readl (&ep->regs->ep_stat);
					count = readl (&ep->dma->dmacount);
					count &= DMA_BYTE_COUNT_MASK;
				}
			}

			/* stop DMA, leave ep NAKing */
			writel ((1 << DMA_ABORT), &ep->dma->dmastat);
			spin_stop_dma (ep->dma);

			/* buffer might have been too small */
			t = readl (&ep->regs->ep_avail);
			if (t != 0)
				DEBUG (ep->dev, "%s dma, discard %d len %d\n",
						ep->ep.name, t, count);
			dma_done (ep, req, count, t ? -EOVERFLOW : 0);

			/* also flush to prevent erratum 0106 trouble */
			if (t || ep->dev->chiprev == 0x0100)
				out_flush (ep);

			/* restart dma (still NAKing OUT!) if needed */
			if (!list_empty (&ep->queue))
				restart_dma (ep);
		} else
			DEBUG (ep->dev, "%s dma ep_stat %08x ??\n",
					ep->ep.name, t);
		return;

	/* data packet(s) received (in the fifo, OUT) */
	} else if (t & (1 << DATA_PACKET_RECEIVED_INTERRUPT)) {
		if (read_fifo (ep, req) && ep->num != 0)
			mode = 2;

	/* data packet(s) transmitted (IN) */
	} else if (t & (1 << DATA_PACKET_TRANSMITTED_INTERRUPT)) {
		unsigned	len;

		len = req->req.length - req->req.actual;
		len = min (ep->ep.maxpacket, len);
		req->req.actual += len;

		/* if we wrote it all, we're usually done */
		if (req->req.actual == req->req.length) {
			if (ep->num == 0) {
				/* wait for control status */
				if (mode != 2)
					req = 0;
			} else if (!req->req.zero || len != ep->ep.maxpacket)
				mode = 2;
		}

	/* there was nothing to do ...  */
	} else if (mode == 1)
		return;

	/* done */
	if (mode == 2) {
		/* stream endpoints often resubmit/unlink in completion */
		done (ep, req, 0);

		/* maybe advance queue to next request */
		if (ep->num == 0) {
			/* NOTE:  net2280 could let gadget driver start the
			 * status stage later. since not all controllers let
			 * them control that, the api doesn't (yet) allow it.
			 */
			if (!ep->stopped)
				allow_status (ep);
			req = 0;
		} else {
			if (!list_empty (&ep->queue) && !ep->stopped)
				req = list_entry (ep->queue.next,
					struct net2280_request, queue);
			else
				req = 0;
			if (req && !ep->is_in)
				stop_out_naking (ep);
		}
	}

	/* is there a buffer for the next packet?
	 * for best streaming performance, make sure there is one.
	 */
	if (req && !ep->stopped) {

		/* load IN fifo with next packet (may be zlp) */
		if (t & (1 << DATA_PACKET_TRANSMITTED_INTERRUPT))
			write_fifo (ep, &req->req);
	}
}

static struct net2280_ep *
get_ep_by_addr (struct net2280 *dev, u16 wIndex)
{
	struct net2280_ep	*ep;

	if ((wIndex & USB_ENDPOINT_NUMBER_MASK) == 0)
		return &dev->ep [0];
	list_for_each_entry (ep, &dev->gadget.ep_list, ep.ep_list) {
		u8	bEndpointAddress;

		if (!ep->desc)
			continue;
		bEndpointAddress = ep->desc->bEndpointAddress;
		if ((wIndex ^ bEndpointAddress) & USB_DIR_IN)
			continue;
		if ((wIndex & 0x0f) == (bEndpointAddress & 0x0f))
			return ep;
	}
	return 0;
}

static void handle_stat0_irqs (struct net2280 *dev, u32 stat)
{
	struct net2280_ep	*ep;
	u32			num, scratch;

	/* most of these don't need individual acks */
	stat &= ~(1 << INTA_ASSERTED);
	if (!stat)
		return;
	// DEBUG (dev, "irqstat0 %04x\n", stat);

	/* starting a control request? */
	if (unlikely (stat & (1 << SETUP_PACKET_INTERRUPT))) {
		union {
			u32			raw [2];
			struct usb_ctrlrequest	r;
		} u;
		int				tmp = 0;
		struct net2280_request		*req;

		if (dev->gadget.speed == USB_SPEED_UNKNOWN) {
			if (readl (&dev->usb->usbstat) & (1 << HIGH_SPEED))
				dev->gadget.speed = USB_SPEED_HIGH;
			else
				dev->gadget.speed = USB_SPEED_FULL;
			net2280_led_speed (dev, dev->gadget.speed);
			DEBUG (dev, "%s speed\n",
				(dev->gadget.speed == USB_SPEED_HIGH)
					? "high" : "full");
		}

		ep = &dev->ep [0];
		ep->irqs++;

		/* make sure any leftover request state is cleared */
		stat &= ~(1 << ENDPOINT_0_INTERRUPT);
		while (!list_empty (&ep->queue)) {
			req = list_entry (ep->queue.next,
					struct net2280_request, queue);
			done (ep, req, (req->req.actual == req->req.length)
						? 0 : -EPROTO);
		}
		ep->stopped = 0;
		dev->protocol_stall = 0;
		writel (  (1 << TIMEOUT)
			| (1 << USB_STALL_SENT)
			| (1 << USB_IN_NAK_SENT)
			| (1 << USB_IN_ACK_RCVD)
			| (1 << USB_OUT_PING_NAK_SENT)
			| (1 << USB_OUT_ACK_SENT)
			| (1 << FIFO_OVERFLOW)
			| (1 << FIFO_UNDERFLOW)
			| (1 << SHORT_PACKET_OUT_DONE_INTERRUPT)
			| (1 << SHORT_PACKET_TRANSFERRED_INTERRUPT)
			| (1 << DATA_PACKET_RECEIVED_INTERRUPT)
			| (1 << DATA_PACKET_TRANSMITTED_INTERRUPT)
			| (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
			| (1 << DATA_IN_TOKEN_INTERRUPT)
			, &ep->regs->ep_stat);
		u.raw [0] = readl (&dev->usb->setup0123);
		u.raw [1] = readl (&dev->usb->setup4567);
		
		cpu_to_le32s (&u.raw [0]);
		cpu_to_le32s (&u.raw [1]);

		le16_to_cpus (&u.r.wValue);
		le16_to_cpus (&u.r.wIndex);
		le16_to_cpus (&u.r.wLength);

		/* ack the irq */
		writel (1 << SETUP_PACKET_INTERRUPT, &dev->regs->irqstat0);
		stat ^= (1 << SETUP_PACKET_INTERRUPT);

		/* watch control traffic at the token level, and force
		 * synchronization before letting the status stage happen.
		 * FIXME ignore tokens we'll NAK, until driver responds.
		 * that'll mean a lot less irqs for some drivers.
		 */
		ep->is_in = (u.r.bRequestType & USB_DIR_IN) != 0;
		if (ep->is_in)
			scratch = (1 << DATA_PACKET_TRANSMITTED_INTERRUPT)
				| (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
				| (1 << DATA_IN_TOKEN_INTERRUPT);
		else
			scratch = (1 << DATA_PACKET_RECEIVED_INTERRUPT)
				| (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
				| (1 << DATA_IN_TOKEN_INTERRUPT);
		writel (scratch, &dev->epregs [0].ep_irqenb);

		/* we made the hardware handle most lowlevel requests;
		 * everything else goes uplevel to the gadget code.
		 */
		switch (u.r.bRequest) {
		case USB_REQ_GET_STATUS: {
			struct net2280_ep	*e;
			u16			status;

			/* hw handles device and interface status */
			if (u.r.bRequestType != (USB_DIR_IN|USB_RECIP_ENDPOINT))
				goto delegate;
			if ((e = get_ep_by_addr (dev, u.r.wIndex)) == 0
					|| u.r.wLength > 2)
				goto do_stall;

			if (readl (&e->regs->ep_rsp)
					& (1 << SET_ENDPOINT_HALT))
				status = __constant_cpu_to_le16 (1);
			else
				status = __constant_cpu_to_le16 (0);

			/* don't bother with a request object! */
			writel (0, &dev->epregs [0].ep_irqenb);
			set_fifo_bytecount (ep, u.r.wLength);
			writel (status, &dev->epregs [0].ep_data);
			allow_status (ep);
			VDEBUG (dev, "%s stat %02x\n", ep->ep.name, status);
			goto next_endpoints;
			}
			break;
		case USB_REQ_CLEAR_FEATURE: {
			struct net2280_ep	*e;

			/* hw handles device features */
			if (u.r.bRequestType != USB_RECIP_ENDPOINT)
				goto delegate;
			if (u.r.wValue != 0 /* HALT feature */
					|| u.r.wLength != 0)
				goto do_stall;
			if ((e = get_ep_by_addr (dev, u.r.wIndex)) == 0)
				goto do_stall;
			clear_halt (e);
			allow_status (ep);
			VDEBUG (dev, "%s clear halt\n", ep->ep.name);
			goto next_endpoints;
			}
			break;
		case USB_REQ_SET_FEATURE: {
			struct net2280_ep	*e;

			/* hw handles device features */
			if (u.r.bRequestType != USB_RECIP_ENDPOINT)
				goto delegate;
			if (u.r.wValue != 0 /* HALT feature */
					|| u.r.wLength != 0)
				goto do_stall;
			if ((e = get_ep_by_addr (dev, u.r.wIndex)) == 0)
				goto do_stall;
			set_halt (e);
			allow_status (ep);
			VDEBUG (dev, "%s set halt\n", ep->ep.name);
			goto next_endpoints;
			}
			break;
		default:
delegate:
			VDEBUG (dev, "setup %02x.%02x v%04x i%04x "
				"ep_cfg %08x\n",
				u.r.bRequestType, u.r.bRequest,
				u.r.wValue, u.r.wIndex,
				readl (&ep->regs->ep_cfg));
			spin_unlock (&dev->lock);
			tmp = dev->driver->setup (&dev->gadget, &u.r);
			spin_lock (&dev->lock);
		}

		/* stall ep0 on error */
		if (tmp < 0) {
do_stall:
			VDEBUG (dev, "req %02x.%02x protocol STALL; stat %d\n",
					u.r.bRequestType, u.r.bRequest, tmp);
			dev->protocol_stall = 1;
		}

		/* some in/out token irq should follow; maybe stall then.
		 * driver must queue a request (even zlp) or halt ep0
		 * before the host times out.
		 */
	}

next_endpoints:
	/* endpoint data irq ? */
	scratch = stat & 0x7f;
	stat &= ~0x7f;
	for (num = 0; scratch; num++) {
		u32		t;

		/* do this endpoint's FIFO and queue need tending? */
		t = 1 << num;
		if ((scratch & t) == 0)
			continue;
		scratch ^= t;

		ep = &dev->ep [num];
		handle_ep_small (ep);
	}

	if (stat)
		DEBUG (dev, "unhandled irqstat0 %08x\n", stat);
}

#define DMA_INTERRUPTS ( \
		  (1 << DMA_D_INTERRUPT) \
		| (1 << DMA_C_INTERRUPT) \
		| (1 << DMA_B_INTERRUPT) \
		| (1 << DMA_A_INTERRUPT))
#define	PCI_ERROR_INTERRUPTS ( \
		  (1 << PCI_MASTER_ABORT_RECEIVED_INTERRUPT) \
		| (1 << PCI_TARGET_ABORT_RECEIVED_INTERRUPT) \
		| (1 << PCI_RETRY_ABORT_INTERRUPT))

static void handle_stat1_irqs (struct net2280 *dev, u32 stat)
{
	struct net2280_ep	*ep;
	u32			tmp, num, scratch;

	/* after disconnect there's nothing else to do! */
	tmp = (1 << VBUS_INTERRUPT) | (1 << ROOT_PORT_RESET_INTERRUPT);
	if (stat & tmp) {
		writel (tmp, &dev->regs->irqstat1);
		if (((stat & (1 << ROOT_PORT_RESET_INTERRUPT)) != 0
			|| (readl (&dev->usb->usbctl) & (1 << VBUS_PIN)) == 0
			) && dev->gadget.speed != USB_SPEED_UNKNOWN) {
			DEBUG (dev, "disconnect %s\n",
					dev->driver->driver.name);
			stop_activity (dev, dev->driver);
			ep0_start (dev);
			return;
		}
		stat &= ~tmp;

		/* vBUS can bounce ... one of many reasons to ignore the
		 * notion of hotplug events on bus connect/disconnect!
		 */
		if (!stat)
			return;
	}

	/* NOTE: we don't actually suspend the hardware; that starts to
	 * interact with PCI power management, and needs something like a
	 * controller->suspend() call to clear SUSPEND_REQUEST_INTERRUPT.
	 * we shouldn't see resume interrupts.
	 * for rev 0100, this also avoids erratum 0102.
	 */
	tmp = (1 << SUSPEND_REQUEST_CHANGE_INTERRUPT);
	if (stat & tmp) {
		if (dev->driver->suspend)
			dev->driver->suspend (&dev->gadget);
		stat &= ~tmp;
	}
	stat &= ~(1 << SUSPEND_REQUEST_INTERRUPT);

	/* clear any other status/irqs */
	if (stat)
		writel (stat, &dev->regs->irqstat1);

	/* some status we can just ignore */
	stat &= ~((1 << CONTROL_STATUS_INTERRUPT)
			| (1 << RESUME_INTERRUPT)
			| (1 << SOF_INTERRUPT));
	if (!stat)
		return;
	// DEBUG (dev, "irqstat1 %08x\n", stat);

	/* DMA status, for ep-{a,b,c,d} */
	scratch = stat & DMA_INTERRUPTS;
	stat &= ~DMA_INTERRUPTS;
	scratch >>= 9;
	for (num = 0; scratch; num++) {
		struct net2280_dma_regs	*dma;

		tmp = 1 << num;
		if ((tmp & scratch) == 0)
			continue;
		scratch ^= tmp;

		ep = &dev->ep [num + 1];
		dma = ep->dma;

		if (!dma)
			continue;

		/* clear ep's dma status */
		tmp = readl (&dma->dmastat);
		writel (tmp, &dma->dmastat);

#ifdef USE_DMA_CHAINING
		/* chaining should stop only on error (which?)
		 * or (stat0 codepath) short OUT transfer.
		 */
#else
		if ((tmp & (1 << DMA_TRANSACTION_DONE_INTERRUPT)) == 0) {
			DEBUG (ep->dev, "%s no xact done? %08x\n",
				ep->ep.name, tmp);
			continue;
		}
		stop_dma (ep->dma);
#endif

		/* OUT transfers terminate when the data from the
		 * host is in our memory.  Process whatever's done.
		 * On this path, we know transfer's last packet wasn't
		 * less than req->length. NAK_OUT_PACKETS may be set,
		 * or the FIFO may already be holding new packets.
		 *
		 * IN transfers can linger in the FIFO for a very
		 * long time ... we ignore that for now, accounting
		 * precisely (like PIO does) needs per-packet irqs
		 */
		scan_dma_completions (ep);

		/* disable dma on inactive queues; else maybe restart */
		if (list_empty (&ep->queue)) {
#ifdef USE_DMA_CHAINING
			stop_dma (ep->dma);
#endif
		} else {
			tmp = readl (&dma->dmactl);
			if ((tmp & (1 << DMA_SCATTER_GATHER_ENABLE)) == 0
					|| (tmp & (1 << DMA_ENABLE)) == 0)
				restart_dma (ep);
#ifdef USE_DMA_CHAINING
			else if (ep->desc->bEndpointAddress & USB_DIR_IN) {
				struct net2280_request	*req;
				u32			dmacount;

				/* the descriptor at the head of the chain
				 * may still have VALID_BIT clear; that's
				 * used to trigger changing DMA_FIFO_VALIDATE
				 * (affects automagic zlp writes).
				 */
				req = list_entry (ep->queue.next,
						struct net2280_request, queue);
				dmacount = req->td->dmacount;
				dmacount &= __constant_cpu_to_le32 (
						(1 << VALID_BIT)
						| DMA_BYTE_COUNT_MASK);
				if (dmacount && (dmacount & valid_bit) == 0) {
					stop_dma (ep->dma);
					restart_dma (ep);
				}
			}
#endif
		}
		ep->irqs++;
	}

	/* NOTE:  there are other PCI errors we might usefully notice.
	 * if they appear very often, here's where to try recovering.
	 */
	if (stat & PCI_ERROR_INTERRUPTS) {
		ERROR (dev, "pci dma error; stat %08x\n", stat);
		stat &= ~PCI_ERROR_INTERRUPTS;
		/* these are fatal errors, but "maybe" they won't
		 * happen again ...
		 */
		stop_activity (dev, dev->driver);
		ep0_start (dev);
		stat = 0;
	}

	if (stat)
		DEBUG (dev, "unhandled irqstat1 %08x\n", stat);
}

static irqreturn_t net2280_irq (int irq, void *_dev, struct pt_regs * r)
{
	struct net2280		*dev = _dev;

	spin_lock (&dev->lock);

	/* handle disconnect, dma, and more */
	handle_stat1_irqs (dev, readl (&dev->regs->irqstat1));

	/* control requests and PIO */
	handle_stat0_irqs (dev, readl (&dev->regs->irqstat0));

	spin_unlock (&dev->lock);

	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static void gadget_release (struct device *_dev)
{
	struct net2280	*dev = dev_get_drvdata (_dev);

	kfree (dev);
}

/* tear down the binding between this driver and the pci device */

static void net2280_remove (struct pci_dev *pdev)
{
	struct net2280		*dev = pci_get_drvdata (pdev);

	/* start with the driver above us */
	if (dev->driver) {
		/* should have been done already by driver model core */
		WARN (dev, "pci remove, driver '%s' is still registered\n",
				dev->driver->driver.name);
		usb_gadget_unregister_driver (dev->driver);
	}

	/* then clean up the resources we allocated during probe() */
	net2280_led_shutdown (dev);
	if (dev->requests) {
		int		i;
		for (i = 1; i < 5; i++) {
			if (!dev->ep [i].dummy)
				continue;
			pci_pool_free (dev->requests, dev->ep [i].dummy,
					dev->ep [i].td_dma);
		}
		pci_pool_destroy (dev->requests);
	}
	if (dev->got_irq)
		free_irq (pdev->irq, dev);
	if (dev->regs)
		iounmap (dev->regs);
	if (dev->region)
		release_mem_region (pci_resource_start (pdev, 0),
				pci_resource_len (pdev, 0));
	if (dev->enabled)
		pci_disable_device (pdev);
	device_unregister (&dev->gadget.dev);
	device_remove_file (&pdev->dev, &dev_attr_registers);
	pci_set_drvdata (pdev, 0);

	INFO (dev, "unbind\n");

	the_controller = 0;
}

/* wrap this driver around the specified device, but
 * don't respond over USB until a gadget driver binds to us.
 */

static int net2280_probe (struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net2280		*dev;
	unsigned long		resource, len;
	void			*base = 0;
	int			retval, i;
	char			buf [8], *bufp;

	/* if you want to support more than one controller in a system,
	 * usb_gadget_driver_{register,unregister}() must change.
	 */
	if (the_controller) {
		dev_warn (&pdev->dev, "ignoring\n");
		return -EBUSY;
	}

	/* alloc, and start init */
	dev = kmalloc (sizeof *dev, SLAB_KERNEL);
	if (dev == NULL){
		retval = -ENOMEM;
		goto done;
	}

	memset (dev, 0, sizeof *dev);
	spin_lock_init (&dev->lock);
	dev->pdev = pdev;
	dev->gadget.ops = &net2280_ops;

	/* the "gadget" abstracts/virtualizes the controller */
	strcpy (dev->gadget.dev.bus_id, "gadget");
	dev->gadget.dev.parent = &pdev->dev;
	dev->gadget.dev.dma_mask = pdev->dev.dma_mask;
	dev->gadget.dev.release = gadget_release;
	dev->gadget.name = driver_name;

	/* now all the pci goodies ... */
	if (pci_enable_device (pdev) < 0) {
   	        retval = -ENODEV;
		goto done;
	}
	dev->enabled = 1;

	/* BAR 0 holds all the registers
	 * BAR 1 is 8051 memory; unused here (note erratum 0103)
	 * BAR 2 is fifo memory; unused here
	 */
	resource = pci_resource_start (pdev, 0);
	len = pci_resource_len (pdev, 0);
	if (!request_mem_region (resource, len, driver_name)) {
		DEBUG (dev, "controller already in use\n");
		retval = -EBUSY;
		goto done;
	}
	dev->region = 1;

	base = ioremap_nocache (resource, len);
	if (base == NULL) {
		DEBUG (dev, "can't map memory\n");
		retval = -EFAULT;
		goto done;
	}
	dev->regs = (struct net2280_regs *) base;
	dev->usb = (struct net2280_usb_regs *) (base + 0x0080);
	dev->pci = (struct net2280_pci_regs *) (base + 0x0100);
	dev->dma = (struct net2280_dma_regs *) (base + 0x0180);
	dev->dep = (struct net2280_dep_regs *) (base + 0x0200);
	dev->epregs = (struct net2280_ep_regs *) (base + 0x0300);

	/* put into initial config, link up all endpoints */
	usb_reset (dev);
	usb_reinit (dev);

	/* irq setup after old hardware is cleaned up */
	if (!pdev->irq) {
		ERROR (dev, "No IRQ.  Check PCI setup!\n");
		retval = -ENODEV;
		goto done;
	}
#ifndef __sparc__
	snprintf (buf, sizeof buf, "%d", pdev->irq);
	bufp = buf;
#else
	bufp = __irq_itoa(pdev->irq);
#endif
	if (request_irq (pdev->irq, net2280_irq, SA_SHIRQ, driver_name, dev)
			!= 0) {
		ERROR (dev, "request interrupt %s failed\n", bufp);
		retval = -EBUSY;
		goto done;
	}
	dev->got_irq = 1;

	/* DMA setup */
	dev->requests = pci_pool_create ("requests", pdev,
		sizeof (struct net2280_dma),
		0 /* no alignment requirements */,
		0 /* or page-crossing issues */);
	if (!dev->requests) {
		DEBUG (dev, "can't get request pool\n");
		retval = -ENOMEM;
		goto done;
	}
	for (i = 1; i < 5; i++) {
		struct net2280_dma	*td;

		td = pci_pool_alloc (dev->requests, GFP_KERNEL,
				&dev->ep [i].td_dma);
		if (!td) {
			DEBUG (dev, "can't get dummy %d\n", i);
			retval = -ENOMEM;
			goto done;
		}
		td->dmacount = 0;	/* not VALID */
		td->dmaaddr = __constant_cpu_to_le32 (DMA_ADDR_INVALID);
		dev->ep [i].dummy = td;
	}

	/* enable lower-overhead pci memory bursts during DMA */
	writel ((1 << PCI_RETRY_ABORT_ENABLE)
			| (1 << DMA_MEMORY_WRITE_AND_INVALIDATE_ENABLE)
			| (1 << DMA_READ_MULTIPLE_ENABLE)
			| (1 << DMA_READ_LINE_ENABLE)
			, &dev->pci->pcimstctl);
	/* erratum 0115 shouldn't appear: Linux inits PCI_LATENCY_TIMER */
	pci_set_master (pdev);
	pci_set_mwi (pdev);

	/* ... also flushes any posted pci writes */
	dev->chiprev = get_idx_reg (dev->regs, REG_CHIPREV) & 0xffff;

	/* done */
	pci_set_drvdata (pdev, dev);
	INFO (dev, "%s\n", driver_desc);
	INFO (dev, "irq %s, pci mem %p, chip rev %04x\n",
			bufp, base, dev->chiprev);
	bufp = DRIVER_VERSION
#ifndef USE_DMA_CHAINING
		" (no dma chain)"
#endif
#ifdef NET2280_DMA_OUT_WORKAROUND
		" (no dma out)"
#endif
		;
	INFO (dev, "version: %s\n", bufp);
	the_controller = dev;

	device_register (&dev->gadget.dev);
	device_create_file (&pdev->dev, &dev_attr_registers);

	return 0;

done:
	if (dev)
		net2280_remove (pdev);
	return retval;
}


/*-------------------------------------------------------------------------*/

static struct pci_device_id pci_ids [] = { {
	.class = 	((PCI_CLASS_SERIAL_USB << 8) | 0xfe),
	.class_mask = 	~0,
	.vendor =	0x17cc,
	.device =	0x2280,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,

}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver net2280_pci_driver = {
	.name =		(char *) driver_name,
	.id_table =	pci_ids,

	.probe =	net2280_probe,
	.remove =	net2280_remove,

	/* FIXME add power management support */
};

MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_AUTHOR ("David Brownell");
MODULE_LICENSE ("GPL");

static int __init init (void)
{
	return pci_module_init (&net2280_pci_driver);
}
module_init (init);

static void __exit cleanup (void)
{
	pci_unregister_driver (&net2280_pci_driver);
}
module_exit (cleanup);
