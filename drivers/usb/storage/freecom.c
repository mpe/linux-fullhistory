/* Driver for Freecom USB/IDE adaptor
 *
 * $Id: freecom.c,v 1.11 2000/09/15 23:06:40 mdharm Exp $
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
#include "linux/hdreg.h"

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
} __attribute__ ((packed));

struct freecom_ide_out {
        __u8    Type;                   /* Type + IDE register. */
        __u8    Pad;
        __u16   Value;                  /* Value to write. */
        __u8    Pad2[60];
};

struct freecom_ide_in {
        __u8    Type;                   /* Type | IDE register. */
        __u8    Pad[63];
};

struct freecom_status {
        __u8    Status;
        __u8    Reason;
        __u16   Count;
        __u8    Pad[60];
};

/* Freecom stuffs the interrupt status in the INDEX_STAT bit of the ide
 * register. */
#define FCM_INT_STATUS   INDEX_STAT

/* These are the packet types.  The low bit indicates that this command
 * should wait for an interrupt. */
#define FCM_PACKET_ATAPI  0x21

/* Receive data from the IDE interface.  The ATAPI packet has already
 * waited, so the data should be immediately available. */
#define FCM_PACKET_INPUT  0x81

/* Send data to the IDE interface. */
#define FCM_PACKET_OUTPUT 0x01

/* Write a value to an ide register.  Or the ide register to write after
 * munging the addres a bit. */
#define FCM_PACKET_IDE_WRITE    0x40
#define FCM_PACKET_IDE_READ     0xC0

/* All packets (except for status) are 64 bytes long. */
#define FCM_PACKET_LENGTH 64

/*
 * Transfer an entire SCSI command's worth of data payload over the bulk
 * pipe.
 *
 * Note that this uses us_transfer_partial to achieve it's goals -- this
 * function simply determines if we're going to use scatter-gather or not,
 * and acts appropriately.  For now, it also re-interprets the error codes.
 */
static void us_transfer_freecom(Scsi_Cmnd *srb, struct us_data* us, int transfer_amount)
{
	int i;
	int result = -1;
	struct scatterlist *sg;
	unsigned int total_transferred = 0;

	/* was someone foolish enough to request more data than available
	 * buffer space? */
	if (transfer_amount > srb->request_bufflen)
		transfer_amount = srb->request_bufflen;

	/* are we scatter-gathering? */
	if (srb->use_sg) {

		/* loop over all the scatter gather structures and 
		 * make the appropriate requests for each, until done
		 */
		sg = (struct scatterlist *) srb->request_buffer;
		for (i = 0; i < srb->use_sg; i++) {

			/* transfer the lesser of the next buffer or the
			 * remaining data */
			if (transfer_amount - total_transferred >= 
					sg[i].length) {
				result = us_transfer_partial(us, sg[i].address, 
						sg[i].length);
				total_transferred += sg[i].length;
			} else
				result = us_transfer_partial(us, sg[i].address,
						transfer_amount - total_transferred);

			/* if we get an error, end the loop here */
			if (result)
				break;
		}
	}
	else
		/* no scatter-gather, just make the request */
		result = us_transfer_partial(us, srb->request_buffer, 
					     transfer_amount);

	/* return the result in the data structure itself */
	srb->result = result;
}


/* Write a value to an ide register. */
static int
freecom_ide_write (struct us_data *us, int reg, int value)
{
        freecom_udata_t extra = (freecom_udata_t) us->extra;
        struct freecom_ide_out *ideout =
                (struct freecom_ide_out *) extra->buffer;
        int opipe;
        int result, partial;

        printk (KERN_DEBUG "IDE out 0x%02x <- 0x%02x\n", reg, value);

        /* Get handles for both transports. */
        opipe = usb_sndbulkpipe (us->pusb_dev, us->ep_out);

        if (reg < 0 || reg > 8)
                return USB_STOR_TRANSPORT_ERROR;
        if (reg < 8)
                reg |= 0x20;
        else
                reg = 0x0e;

        ideout->Type = FCM_PACKET_IDE_WRITE | reg;
        ideout->Pad = 0;
        ideout->Value = cpu_to_le16 (value);
        memset (ideout->Pad2, 0, sizeof (ideout->Pad2));

        result = usb_stor_bulk_msg (us, ideout, opipe,
                        FCM_PACKET_LENGTH, &partial);
        if (result != 0) {
                if (result == -ENOENT)
                        return US_BULK_TRANSFER_ABORTED;
                else
                        return USB_STOR_TRANSPORT_ERROR;
        }

        return USB_STOR_TRANSPORT_GOOD;
}

/* Read a value from an ide register. */
static int
freecom_ide_read (struct us_data *us, int reg, int *value)
{
        freecom_udata_t extra = (freecom_udata_t) us->extra;
        struct freecom_ide_in *idein =
                (struct freecom_ide_in *) extra->buffer;
        __u8 *buffer = extra->buffer;
        int ipipe, opipe;
        int result, partial;
        int desired_length;

        /* Get handles for both transports. */
        opipe = usb_sndbulkpipe (us->pusb_dev, us->ep_out);
        ipipe = usb_rcvbulkpipe (us->pusb_dev, us->ep_in);

        if (reg < 0 || reg > 8)
                return USB_STOR_TRANSPORT_ERROR;
        if (reg < 8)
                reg |= 0x10;
        else
                reg = 0x0e;

        idein->Type = FCM_PACKET_IDE_READ | reg;
        memset (idein->Pad, 0, sizeof (idein->Pad));

        result = usb_stor_bulk_msg (us, idein, opipe,
                        FCM_PACKET_LENGTH, &partial);
        if (result != 0) {
                if (result == -ENOENT)
                        return US_BULK_TRANSFER_ABORTED;
                else
                        return USB_STOR_TRANSPORT_ERROR;
        }

        desired_length = 1;
        if (reg == 0x10)
                desired_length = 2;

        result = usb_stor_bulk_msg (us, buffer, ipipe,
                        desired_length, &partial);
        if (result != 0) {
                if (result == -ENOENT)
                        return US_BULK_TRANSFER_ABORTED;
                else
                        return USB_STOR_TRANSPORT_ERROR;
        }

        if (desired_length == 1)
                *value = buffer[0];
        else
                *value = le16_to_cpu (*(__u16 *) buffer);

        printk (KERN_DEBUG "IDE in  0x%02x -> 0x%02x\n", reg, *value);

        return USB_STOR_TRANSPORT_GOOD;
}

static int
freecom_readdata (Scsi_Cmnd *srb, struct us_data *us,
                int ipipe, int opipe, int count)
{
        freecom_udata_t extra = (freecom_udata_t) us->extra;
        struct freecom_xfer_wrap *fxfr =
                (struct freecom_xfer_wrap *) extra->buffer;
        int result, partial;
        int offset;
        int this_read;
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
	printk (KERN_DEBUG "Start of read\n");
	us_transfer_freecom(srb, us, count);
#if 0
        if (srb->use_sg) {
                US_DEBUGP ("Need to implement scatter-gather\n");
                return USB_STOR_TRANSPORT_ERROR;
        } else {
                offset = 0;

                while (offset < count) {
#if 0
                        this_read = count - offset;
                        if (this_read > 64)
                                this_read = 64;
#else
                        this_read = 64;
#endif

                        printk (KERN_DEBUG "Start of read\n");
                        /* Use the given buffer directly, but only if there
                         * is space for an entire packet. */

                        if (offset + 64 <= srb->request_bufflen) {
                                result = usb_stor_bulk_msg (
                                                us, srb->request_buffer+offset,
                                                ipipe, this_read, &partial);
                                printk (KERN_DEBUG "Read111 = %d, %d\n",
                                                result, partial);
                                pdump (srb->request_buffer+offset,
                                                partial);
                        } else {
                                result = usb_stor_bulk_msg (
                                                us, buffer,
                                                ipipe, this_read, &partial);
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

                        offset += this_read;
                }
        }
#endif

        printk (KERN_DEBUG "freecom_readdata done!\n");
        return USB_STOR_TRANSPORT_GOOD;
}

static int
freecom_writedata (Scsi_Cmnd *srb, struct us_data *us,
                int ipipe, int opipe, int count)
{
        freecom_udata_t extra = (freecom_udata_t) us->extra;
        struct freecom_xfer_wrap *fxfr =
                (struct freecom_xfer_wrap *) extra->buffer;
        int result, partial;
        int offset;
        int this_write;
        __u8 *buffer = extra->buffer;

        fxfr->Type = FCM_PACKET_OUTPUT | 0x00;
        fxfr->Timeout = 0;    /* Short timeout for debugging. */
        fxfr->Count = cpu_to_le32 (count);
        memset (fxfr->Pad, 0, sizeof (fxfr->Pad));

        printk (KERN_DEBUG "Write data Freecom! (c=%d)\n", count);

        /* Issue the transfer command. */
        result = usb_stor_bulk_msg (us, fxfr, opipe,
                        FCM_PACKET_LENGTH, &partial);
        if (result != 0) {
                US_DEBUGP ("Freecom writedata xpot failure: r=%d, p=%d\n",
                                result, partial);

		/* -ENOENT -- we canceled this transfer */
		if (result == -ENOENT) {
			US_DEBUGP("us_transfer_partial(): transfer aborted\n");
			return US_BULK_TRANSFER_ABORTED;
		}

                return USB_STOR_TRANSPORT_ERROR;
        }
        printk (KERN_DEBUG "Done issuing write request: %d %d\n",
                        result, partial);

        /* Now transfer all of our blocks. */
	printk (KERN_DEBUG "Start of write\n");
	us_transfer_freecom(srb, us, count);
#if 0
        if (srb->use_sg) {
                US_DEBUGP ("Need to implement scatter-gather\n");
                return USB_STOR_TRANSPORT_ERROR;
        } else {
                offset = 0;

                while (offset < count) {
#if 1
                        this_write = count - offset;
                        if (this_write > 64)
                                this_write = 64;
#else
                        this_write = 64;
#endif

                        printk (KERN_DEBUG "Start of write\n");
                        /* Use the given buffer directly, but only if there
                         * is space for an entire packet. */

                        if (offset + 64 <= srb->request_bufflen) {
                                result = usb_stor_bulk_msg (
                                                us, srb->request_buffer+offset,
                                                opipe, this_write, &partial);
                                printk (KERN_DEBUG "Write111 = %d, %d\n",
                                                result, partial);
                                pdump (srb->request_buffer+offset,
                                                partial);
                        } else {
                                result = usb_stor_bulk_msg (
                                                us, buffer,
                                                opipe, this_write, &partial);
                                printk (KERN_DEBUG "Write112 = %d, %d\n",
                                                result, partial);
                                memcpy (buffer,
                                                srb->request_buffer+offset,
                                                srb->request_bufflen - offset);
                                pdump (srb->request_buffer+offset,
						srb->request_bufflen - offset);
                        }

                        if (result != 0) {
                                US_DEBUGP ("Freecom writeblock r=%d, p=%d\n",
                                                result, partial);

				/* -ENOENT -- we canceled this transfer */
				if (result == -ENOENT) {
					US_DEBUGP("us_transfer_partial(): transfer aborted\n");
					return US_BULK_TRANSFER_ABORTED;
				}

                                return USB_STOR_TRANSPORT_ERROR;
                        }

                        offset += this_write;
                }
        }
#endif

        printk (KERN_DEBUG "freecom_writedata done!\n");
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
        if ((fst->Status & 1) != 0) {
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
                /* Make sure that the status indicates that the device
                 * wants data as well. */
                if ((fst->Status & DRQ_STAT) == 0 || (fst->Reason & 3) != 2) {
                        printk (KERN_DEBUG "SCSI wants data, drive doesn't have any\n");
                        return USB_STOR_TRANSPORT_FAILED;
                }
                result = freecom_readdata (srb, us, ipipe, opipe, length);
                if (result != USB_STOR_TRANSPORT_GOOD)
                        return result;

                printk (KERN_DEBUG "FCM: Waiting for status\n");
                result = usb_stor_bulk_msg (us, fst, ipipe,
                                FCM_PACKET_LENGTH, &partial);
		pdump ((void *) fst, partial);
                if (result == -ENOENT) {
                        US_DEBUGP ("freecom_transport: transfer aborted\n");
                        return US_BULK_TRANSFER_ABORTED;
                }
                if (partial != 4 || result != 0)
                        return USB_STOR_TRANSPORT_ERROR;
                if ((fst->Status & ERR_STAT) != 0) {
                        printk (KERN_DEBUG "operation failed\n");
                        return USB_STOR_TRANSPORT_FAILED;
                }
                if ((fst->Reason & 3) != 3) {
                        printk (KERN_DEBUG "Drive seems still hungry\n");
                        return USB_STOR_TRANSPORT_FAILED;
                }
                printk (KERN_DEBUG "Transfer happy\n");
                break;

        case SCSI_DATA_WRITE:
                /* Make sure the status indicates that the device wants to
                 * send us data. */
                /* !!IMPLEMENT!! */
                result = freecom_writedata (srb, us, ipipe, opipe, length);
                if (result != USB_STOR_TRANSPORT_GOOD)
                        return result;

#if 1
                printk (KERN_DEBUG "FCM: Waiting for status\n");
                result = usb_stor_bulk_msg (us, fst, ipipe,
                                FCM_PACKET_LENGTH, &partial);
                if (result == -ENOENT) {
                        US_DEBUGP ("freecom_transport: transfer aborted\n");
                        return US_BULK_TRANSFER_ABORTED;
                }
                if (partial != 4 || result != 0)
                        return USB_STOR_TRANSPORT_ERROR;
                if ((fst->Status & ERR_STAT) != 0) {
                        printk (KERN_DEBUG "operation failed\n");
                        return USB_STOR_TRANSPORT_FAILED;
                }
                if ((fst->Reason & 3) != 3) {
                        printk (KERN_DEBUG "Drive seems still hungry\n");
                        return USB_STOR_TRANSPORT_FAILED;
                }
#endif
                printk (KERN_DEBUG "Transfer happy\n");
                break;


        case SCSI_DATA_NONE:
                /* Easy, do nothing. */
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

int
freecom_init (struct us_data *us)
{
        int result, value;
        int counter;

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

        result = freecom_ide_write (us, 0x06, 0xA0);
        if (result != USB_STOR_TRANSPORT_GOOD)
                return result;
        result = freecom_ide_write (us, 0x01, 0x00);
        if (result != USB_STOR_TRANSPORT_GOOD)
                return result;

        counter = 50;
        do {
                result = freecom_ide_read (us, 0x07, &value);
                if (result != USB_STOR_TRANSPORT_GOOD)
                        return result;
                if (counter-- < 0) {
                        printk (KERN_WARNING USB_STORAGE "Timeout in freecom");
                        return USB_STOR_TRANSPORT_ERROR;
                }
        } while ((value & 0x80) != 0);

        result = freecom_ide_write (us, 0x07, 0x08);
        if (result != USB_STOR_TRANSPORT_GOOD)
                return result;

        counter = 50;
        do {
                result = freecom_ide_read (us, 0x07, &value);
                if (result != USB_STOR_TRANSPORT_GOOD)
                        return result;
                if (counter-- < 0) {
                        printk (KERN_WARNING USB_STORAGE "Timeout in freecom");
                        return USB_STOR_TRANSPORT_ERROR;
                }
        } while ((value & 0x80) != 0);

        result = freecom_ide_write (us, 0x08, 0x08);
        if (result != USB_STOR_TRANSPORT_GOOD)
                return result;

        return USB_STOR_TRANSPORT_GOOD;
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

