/* Driver for Freecom USB/IDE adaptor
 *
 * $Id: freecom.c,v 1.7 2000/08/25 00:13:51 mdharm Exp $
 *
 * Freecom v0.1:
 *
 * First release
 *
 * Current development and maintenance by:
 *   (C) 2000 David Brown <usb-storage@davidb.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * This driver was developed with information provided in FREECOM's USB
 * Programmers Reference Guide.  For further information contact Freecom
 * (http://www.freecom.de/)
 */

#include <linux/config.h>
#include "transport.h"
#include "protocol.h"
#include "usb.h"
#include "debug.h"
#include "freecom.h"

static void pdump (void *, int);

struct freecom_udata {
        __u8    buffer[64];             /* Common command block. */
};
typedef struct freecom_udata *freecom_udata_t;

/* All of the outgoing packets are 64 bytes long. */
struct freecom_cb_wrap {
        __u8    Type;                   /* Command type. */
        __u8    Timeout;                /* Timeout in seconds. */
        __u8    Atapi[12];              /* An ATAPI packet. */
        __u8    Filler[50];             /* Padding Data. */
};

struct freecom_xfer_wrap {
        __u8    Type;                   /* Command type. */
        __u8    Timeout;                /* Timeout in seconds. */
        __u32   Count;                  /* Number of bytes to transfer. */
        __u8    Pad[58];
};

struct freecom_status {
        __u8    Status;
        __u8    Reason;
        __u16   Count;
        __u8    Pad[60];
};

/* These are the packet types.  The low bit indicates that this command
 * should wait for an interrupt. */
#define FCM_PACKET_ATAPI  0x21

/* Receive data from the IDE interface.  The ATAPI packet has already
 * waited, so the data should be immediately available. */
#define FCM_PACKET_INPUT  0x90

/* All packets (except for status) are 64 bytes long. */
#define FCM_PACKET_LENGTH 64

static int
freecom_readdata (Scsi_Cmnd *srb, struct us_data *us,
                int ipipe, int opipe, int count)
{
        freecom_udata_t extra = (freecom_udata_t) us->extra;
        struct freecom_xfer_wrap *fxfr =
                (struct freecom_xfer_wrap *) extra->buffer;
        int result, partial;
        int offset;
        __u8 *buffer = extra->buffer;

        fxfr->Type = FCM_PACKET_INPUT | 0x00;
        fxfr->Timeout = 0;    /* Short timeout for debugging. */
        fxfr->Count = cpu_to_le32 (count);
        memset (fxfr->Pad, 0, sizeof (fxfr->Pad));

        printk (KERN_DEBUG "Read data Freecom! (c=%d)\n", count);

        /* Issue the transfer command. */
        result = usb_stor_bulk_msg (us, fxfr, opipe,
                        FCM_PACKET_LENGTH, &partial);
        if (result != 0) {
                US_DEBUGP ("Freecom readdata xpot failure: r=%d, p=%d\n",
                                result, partial);

		/* -ENOENT -- we canceled this transfer */
		if (result == -ENOENT) {
			US_DEBUGP("us_transfer_partial(): transfer aborted\n");
			return US_BULK_TRANSFER_ABORTED;
		}

                return USB_STOR_TRANSPORT_ERROR;
        }
        printk (KERN_DEBUG "Done issuing read request: %d %d\n",
                        result, partial);

        /* Now transfer all of our blocks. */
        if (srb->use_sg) {
                US_DEBUGP ("Need to implement scatter-gather\n");
                return USB_STOR_TRANSPORT_ERROR;
        } else {
                offset = 0;

                while (offset < count) {
                        printk (KERN_DEBUG "Start of read\n");
                        /* Use the given buffer directly, but only if there
                         * is space for an entire packet. */

                        if (offset + 64 <= srb->request_bufflen) {
                                result = usb_stor_bulk_msg (
                                                us, srb->request_buffer+offset,
                                                ipipe, 64, &partial);
                                printk (KERN_DEBUG "Read111 = %d, %d\n",
                                                result, partial);
                                pdump (srb->request_buffer+offset,
                                                partial);
                        } else {
                                result = usb_stor_bulk_msg (
                                                us, buffer,
                                                ipipe, 64, &partial);
                                printk (KERN_DEBUG "Read112 = %d, %d\n",
                                                result, partial);
                                memcpy (srb->request_buffer+offset,
                                                buffer,
                                                srb->request_bufflen - offset);
                                pdump (srb->request_buffer+offset,
						srb->request_bufflen - offset);
                        }

                        if (result != 0) {
                                US_DEBUGP ("Freecom readblock r=%d, p=%d\n",
                                                result, partial);

				/* -ENOENT -- we canceled this transfer */
				if (result == -ENOENT) {
					US_DEBUGP("us_transfer_partial(): transfer aborted\n");
					return US_BULK_TRANSFER_ABORTED;
				}

                                return USB_STOR_TRANSPORT_ERROR;
                        }

                        offset += 64;
                }
        }

        printk (KERN_DEBUG "freecom_readdata done!\n");
        return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Transport for the Freecom USB/IDE adaptor.
 *
 */
int freecom_transport(Scsi_Cmnd *srb, struct us_data *us)
{
        struct freecom_cb_wrap *fcb;
        struct freecom_status  *fst;
        int ipipe, opipe;             /* We need both pipes. */
        int result;
        int partial;
        int length;
        freecom_udata_t extra;

        /* Allocate a buffer for us.  The upper usb transport code will
         * free this for us when cleaning up. */
        if (us->extra == NULL) {
                us->extra = kmalloc (sizeof (struct freecom_udata),
                                GFP_KERNEL);
                if (us->extra == NULL) {
                        printk (KERN_WARNING USB_STORAGE "Out of memory\n");
                        return USB_STOR_TRANSPORT_ERROR;
                }
        }

        extra = (freecom_udata_t) us->extra;

        fcb = (struct freecom_cb_wrap *) extra->buffer;
        fst = (struct freecom_status *) extra->buffer;

        printk (KERN_DEBUG "Freecom TRANSPORT STARTED\n");

        /* Get handles for both transports. */
        opipe = usb_sndbulkpipe (us->pusb_dev, us->ep_out);
        ipipe = usb_rcvbulkpipe (us->pusb_dev, us->ep_in);

#if 0
        /* Yuck, let's see if this helps us.  Artificially increase the
         * length on this. */
        if (srb->cmnd[0] == 0x03 && srb->cmnd[4] == 0x12)
                srb->cmnd[4] = 0x0E;
#endif

        /* The ATAPI Command always goes out first. */
        fcb->Type = FCM_PACKET_ATAPI;
        fcb->Timeout = 0;
        memcpy (fcb->Atapi, srb->cmnd, 12);
        memset (fcb->Filler, 0, sizeof (fcb->Filler));

        pdump (srb->cmnd, 12);

        /* Send it out. */
        result = usb_stor_bulk_msg (us, fcb, opipe,
                        FCM_PACKET_LENGTH, &partial);

        /* The Freecom device will only fail if there is something wrong in
         * USB land.  It returns the status in its own registers, which
         * come back in the bulk pipe. */
        if (result != 0) {
                US_DEBUGP ("freecom xport failure: r=%d, p=%d\n",
                                result, partial);

		/* -ENOENT -- we canceled this transfer */
		if (result == -ENOENT) {
			US_DEBUGP("us_transfer_partial(): transfer aborted\n");
			return US_BULK_TRANSFER_ABORTED;
		}

                return USB_STOR_TRANSPORT_ERROR;
        }

        /* There are times we can optimize out this status read, but it
         * doesn't hurt us to always do it now. */
        result = usb_stor_bulk_msg (us, fst, ipipe,
                        FCM_PACKET_LENGTH, &partial);
        printk (KERN_DEBUG "foo Status result %d %d\n", result, partial);
	/* -ENOENT -- we canceled this transfer */
	if (result == -ENOENT) {
		US_DEBUGP("us_transfer_partial(): transfer aborted\n");
		return US_BULK_TRANSFER_ABORTED;
	}

        pdump ((void *) fst, partial);
        if (partial != 4 || result != 0) {
                return USB_STOR_TRANSPORT_ERROR;
        }
        if ((fst->Reason & 1) != 0) {
                printk (KERN_DEBUG "operation failed\n");
                return USB_STOR_TRANSPORT_FAILED;
        }

        /* The device might not have as much data available as we
         * requested.  If you ask for more than the device has, this reads
         * and such will hang. */
        printk (KERN_DEBUG "Device indicates that it has %d bytes available\n",
                        le16_to_cpu (fst->Count));

        /* Find the length we desire to read.  It is the lesser of the SCSI
         * layer's requested length, and the length the device claims to
         * have available. */
        length = us_transfer_length (srb);
        printk (KERN_DEBUG "SCSI requested %d\n", length);
        if (length > le16_to_cpu (fst->Count))
                length = le16_to_cpu (fst->Count);

        /* What we do now depends on what direction the data is supposed to
         * move in. */

        switch (us->srb->sc_data_direction) {
        case SCSI_DATA_READ:
                result = freecom_readdata (srb, us, ipipe, opipe, length);
                if (result != USB_STOR_TRANSPORT_GOOD)
                        return result;
                break;

        default:
                US_DEBUGP ("freecom unimplemented direction: %d\n",
                                us->srb->sc_data_direction);
                // Return fail, SCSI seems to handle this better.
                return USB_STOR_TRANSPORT_FAILED;
                break;
        }

#if 0
        /* After the transfer, we can read our status register. */
        printk (KERN_DEBUG "Going to read status register\n");
        result = usb_stor_bulk_msg (us, &fst, ipipe,
                        FCM_PACKET_LENGTH, &partial);
        printk (KERN_DEBUG "Result from read %d %d\n", result, partial);
        if (result != 0) {
                return USB_STOR_TRANSPORT_ERROR;
        }
        if ((fst.Reason & 1) != 0) {
                return USB_STOR_TRANSPORT_FAILED;
        }
#endif

        return USB_STOR_TRANSPORT_GOOD;

        printk (KERN_DEBUG "Freecom: transfer_length = %d\n",
                        us_transfer_length (srb));

        printk (KERN_DEBUG "Freecom: direction = %d\n",
                        srb->sc_data_direction);

        return USB_STOR_TRANSPORT_ERROR;
}

int usb_stor_freecom_reset(struct us_data *us)
{
        printk (KERN_DEBUG "freecom reset called\n");

        /* We don't really have this feature. */
        return USB_STOR_TRANSPORT_ERROR;
}

static void pdump (void *ibuffer, int length)
{
	static char line[80];
	int offset = 0;
	unsigned char *buffer = (unsigned char *) ibuffer;
	int i, j;
	int from, base;
	
	offset = 0;
	for (i = 0; i < length; i++) {
		if ((i & 15) == 0) {
			if (i > 0) {
				offset += sprintf (line+offset, " - ");
				for (j = i - 16; j < i; j++) {
					if (buffer[j] >= 32 && buffer[j] <= 126)
						line[offset++] = buffer[j];
					else
						line[offset++] = '.';
				}
				line[offset] = 0;
				printk (KERN_DEBUG "%s\n", line);
				offset = 0;
			}
			offset += sprintf (line+offset, "%08x:", i);
		}
		else if ((i & 7) == 0) {
			offset += sprintf (line+offset, " -");
		}
		offset += sprintf (line+offset, " %02x", buffer[i] & 0xff);
	}
	
	/* Add the last "chunk" of data. */
	from = (length - 1) % 16;
	base = ((length - 1) / 16) * 16;
	
	for (i = from + 1; i < 16; i++)
		offset += sprintf (line+offset, "   ");
	if (from < 8)
		offset += sprintf (line+offset, "  ");
	offset += sprintf (line+offset, " - ");
	
	for (i = 0; i <= from; i++) {
		if (buffer[base+i] >= 32 && buffer[base+i] <= 126)
			line[offset++] = buffer[base+i];
		else
			line[offset++] = '.';
	}
	line[offset] = 0;
	printk (KERN_DEBUG "%s\n", line);
	offset = 0;
}

