/*
 * Driver for USB Scanners (linux-2.4.0test1-ac7)
 *
 * Copyright (C) 1999, 2000 David E. Nelson
 *
 * David E. Nelson (dnelson@jump.net)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */ 

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>

// #define DEBUG

#include <linux/usb.h>

static __s32 vendor=-1, product=-1;

MODULE_AUTHOR("David E. Nelson, dnelson@jump.net, http://www.jump.net/~dnelson");
MODULE_DESCRIPTION("USB Scanner Driver");

MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");


/* Enable to activate the ioctl interface.  This is mainly meant for */
/* development purposes until an ioctl number is officially registered */
// #define SCN_IOCTL

/* WARNING: These DATA_DUMP's can produce a lot of data. Caveat Emptor. */
// #define RD_DATA_DUMP /* Enable to dump data - limited to 24 bytes */
// #define WR_DATA_DUMP /* DEBUG does not have to be defined. */

#define IS_EP_BULK(ep)  ((ep).bmAttributes == USB_ENDPOINT_XFER_BULK ? 1 : 0)
#define IS_EP_BULK_IN(ep) (IS_EP_BULK(ep) && ((ep).bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
#define IS_EP_BULK_OUT(ep) (IS_EP_BULK(ep) && ((ep).bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT)
#define IS_EP_INTR(ep) ((ep).bmAttributes == USB_ENDPOINT_XFER_INT ? 1 : 0)

#define USB_SCN_MINOR(X) MINOR((X)->i_rdev) - SCN_BASE_MNR

#ifdef DEBUG
#define SCN_DEBUG(X) X
#else
#define SCN_DEBUG(X)
#endif

#define IBUF_SIZE 32768
#define OBUF_SIZE 4096

/* read_scanner timeouts -- RD_NAK_TIMEOUT * RD_EXPIRE = Number of seconds */
#define RD_NAK_TIMEOUT (10*HZ)	/* Number of X seconds to wait */
#define RD_EXPIRE 12		/* Number of attempts to wait X seconds */


/* FIXME: These are NOT registered ioctls()'s */
#define PV8630_IOCTL_INREQUEST 69
#define PV8630_IOCTL_OUTREQUEST 70

#define SCN_MAX_MNR 16		/* We're allocated 16 minors */
#define SCN_BASE_MNR 48		/* USB Scanners start at minor 48 */

struct scn_usb_data {
	struct usb_device *scn_dev;
	struct urb scn_irq;
	unsigned int ifnum;	/* Interface number of the USB device */
	kdev_t scn_minor;	/* Scanner minor - used in disconnect() */
	unsigned char button;	/* Front panel buffer */
        char isopen;		/* Not zero if the device is open */
	char present;		/* Not zero if device is present */
	char *obuf, *ibuf;	/* transfer buffers */
	char bulk_in_ep, bulk_out_ep, intr_ep; /* Endpoint assignments */
	wait_queue_head_t rd_wait_q; /* read timeouts */
	struct semaphore gen_lock; /* lock to prevent concurrent reads or writes */
};

static struct scn_usb_data *p_scn_table[SCN_MAX_MNR] = { NULL, /* ... */};

/* table of scanners that may work with this driver */
static const struct scanner_device {
	__u16		idVendor;
	__u16		idProduct;
} scanner_device_ids [] = {
	/* Acer */
		{ 0x04a5, 0x2060 },	/* Prisa Acerscan 620U & 640U (!) */
		{ 0x04a5, 0x2040 },	/* Prisa AcerScan 620U (!) */
		{ 0x04a5, 0x2022 },	/* Vuego Scan Brisa 340U */
	/* Agfa */
		{ 0x06bd, 0x0001 },	/* SnapScan 1212U */
		{ 0x06bd, 0x2061 },	/* Another SnapScan 1212U (?) */
		{ 0x06bd, 0x0100 },	/* SnapScan Touch */
	/* Colorado -- See Primax/Colorado below */
	/* Epson -- See Seiko/Epson below */
	/* Genius */
		{ 0x0458, 0x2001 },	/* ColorPage-Vivid Pro */
	/* Hewlett Packard */
		{ 0x03f0, 0x0205 },	/* 3300C */
		{ 0x03f0, 0x0101 },	/* 4100C */
		{ 0x03f0, 0x0105 },	/* 4200C */
		{ 0x03f0, 0x0102 },	/* PhotoSmart S20 */
		{ 0x03f0, 0x0401 },	/* 5200C */
		{ 0x03f0, 0x0701 },	/* 5300C */
		{ 0x03f0, 0x0201 },	/* 6200C */
		{ 0x03f0, 0x0601 },	/* 6300C */
	/* iVina */
		{ 0x0638, 0x0268 },     /* 1200U */
	/* Microtek */
		{ 0x05da, 0x0099 },	/* ScanMaker X6 - X6U */
		{ 0x05da, 0x0094 },	/* Phantom 336CX - C3 */
		{ 0x05da, 0x00a0 },	/* Phantom 336CX - C3 #2 */
		{ 0x05da, 0x009a },	/* Phantom C6 */
		{ 0x05da, 0x00a3 },	/* ScanMaker V6USL */
		{ 0x05da, 0x80a3 },	/* ScanMaker V6USL #2 */
		{ 0x05da, 0x80ac },	/* ScanMaker V6UL - SpicyU */
	/* Mustek */
		{ 0x055f, 0x0001 },	/* 1200 CU */
		{ 0x0400, 0x1000 },	/* BearPaw 1200 */
		{ 0x055f, 0x0002 },	/* 600 CU */
		{ 0x055f, 0x0003 },	/* 1200 USB */
		{ 0x055f, 0x0006 },	/* 1200 UB */
	/* Primax/Colorado */
		{ 0x0461, 0x0300 },	/* G2-300 #1 */
		{ 0x0461, 0x0380 },	/* G2-600 #1 */
		{ 0x0461, 0x0301 },	/* G2E-300 #1 */
		{ 0x0461, 0x0381 },	/* ReadyScan 636i */
		{ 0x0461, 0x0302 },	/* G2-300 #2 */
		{ 0x0461, 0x0382 },	/* G2-600 #2 */
		{ 0x0461, 0x0303 },	/* G2E-300 #2 */
		{ 0x0461, 0x0383 },	/* G2E-600 */
		{ 0x0461, 0x0340 },	/* Colorado USB 9600 */
		{ 0x0461, 0x0360 },	/* Colorado USB 19200 */
		{ 0x0461, 0x0341 },	/* Colorado 600u */
		{ 0x0461, 0x0361 },	/* Colorado 1200u */
	/* Seiko/Epson Corp. */
		{ 0x04b8, 0x0101 },	/* Perfection 636U and 636Photo */
		{ 0x04b8, 0x0103 },	/* Perfection 610 */
		{ 0x04b8, 0x0104 },	/* Perfection 1200U and 1200Photo */
		{ 0x04b8, 0x0107 },     /* Expression 1600 */
	/* Umax */
		{ 0x1606, 0x0010 },	/* Astra 1220U */
		{ 0x1606, 0x0002 },	/* Astra 1236U */
		{ 0x1606, 0x0030 },	/* Astra 2000U */
		{ 0x1606, 0x0230 },	/* Astra 2200U */
	/* Visioneer */
		{ 0x04a7, 0x0221 },	/* OneTouch 5300 USB */
		{ 0x04a7, 0x0211 },	/* OneTouch 7600 USB */
		{ 0x04a7, 0x0231 },	/* 6100 USB */
		{ 0x04a7, 0x0311 },	/* 6200 EPP/USB */
		{ 0x04a7, 0x0321 },	/* OneTouch 8100 EPP/USB */
		{ 0x04a7, 0x0331 }, 	/* OneTouch 8600 EPP/USB */
};

/* Forward declarations */
static struct usb_driver scanner_driver;
