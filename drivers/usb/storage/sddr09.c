/* Driver for SanDisk SDDR-09 SmartMedia reader
 *
 * SDDR09 driver v0.1:
 *
 * First release
 *
 * Current development and maintainance by:
 *   (c) 2000 Robert Baruch (autophile@dol.net)
 *
 * The SanDisk SDDR-09 SmartMedia reader uses the Shuttle EUSB-01 chip.
 * This chip is a programmable USB controller. In the SDDR-09, it has
 * been programmed to obey a certain limited set of SCSI commands. This
 * driver translates the "real" SCSI commands to the SDDR-09 SCSI
 * commands.
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
 */

#include "transport.h"
#include "protocol.h"
#include "usb.h"
#include "debug.h"
#include "sddr09.h"

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/malloc.h>

extern int usb_stor_control_msg(struct us_data *us, unsigned int pipe,
	u8 request, u8 requesttype, u16 value, u16 index,
	void *data, u16 size);
extern int usb_stor_bulk_msg(struct us_data *us, void *data, int pipe,
	unsigned int len, unsigned int *act_len);

#define short_pack(b1,b2) ( ((u16)(b1)) | ( ((u16)(b2))<<8 ) )
#define LSB_of(s) ((s)&0xFF)
#define MSB_of(s) ((s)>>8)

/*
 * Send a control message and wait for the response.
 *
 * us - the pointer to the us_data structure for the device to use
 *
 * request - the URB Setup Packet's first 6 bytes. The first byte always
 *  corresponds to the request type, and the second byte always corresponds
 *  to the request.  The other 4 bytes do not correspond to value and index,
 *  since they are used in a custom way by the SCM protocol.
 *
 * xfer_data - a buffer from which to get, or to which to store, any data
 *  that gets send or received, respectively, with the URB. Even though
 *  it looks like we allocate a buffer in this code for the data, xfer_data
 *  must contain enough allocated space.
 *
 * xfer_len - the number of bytes to send or receive with the URB.
 *
 */

static int sddr09_send_control(struct us_data *us,
		int pipe,
		unsigned char request,
		unsigned char requesttype,
		unsigned short value,
		unsigned short index,
		unsigned char *xfer_data,
		unsigned int xfer_len) {

	int result;

	// If data is going to be sent or received with the URB,
	// then allocate a buffer for it. If data is to be sent,
	// copy the data into the buffer.
/*
	if (xfer_len > 0) {
		buffer = kmalloc(xfer_len, GFP_KERNEL);
		if (!(command[0] & USB_DIR_IN))
			memcpy(buffer, xfer_data, xfer_len);
	}
*/
	// Send the URB to the device and wait for a response.

	/* Why are request and request type reversed in this call? */

	result = usb_stor_control_msg(us, pipe,
			request, requesttype, value, index,
			xfer_data, xfer_len);


	// If data was sent or received with the URB, free the buffer we
	// allocated earlier, but not before reading the data out of the
	// buffer if we wanted to receive data.
/*
	if (xfer_len > 0) {
		if (command[0] & USB_DIR_IN)
			memcpy(xfer_data, buffer, xfer_len);
		kfree(buffer);
	}
*/
	// Check the return code for the command.

	if (result < 0) {
		/* if the command was aborted, indicate that */
		if (result == -ENOENT)
			return USB_STOR_TRANSPORT_ABORTED;

		/* a stall is a fatal condition from the device */
		if (result == -EPIPE) {
			US_DEBUGP("-- Stall on control pipe. Clearing\n");
			result = usb_clear_halt(us->pusb_dev, pipe);
			US_DEBUGP("-- usb_clear_halt() returns %d\n", result);
			return USB_STOR_TRANSPORT_FAILED;
		}

		/* Uh oh... serious problem here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

static int sddr09_raw_bulk(struct us_data *us, 
		int direction,
		unsigned char *data,
		unsigned short len) {

	int result;
	int act_len;
	int pipe;

	if (direction == SCSI_DATA_READ)
		pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
	else
		pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);

	result = usb_stor_bulk_msg(us, data, pipe, len, &act_len);

        /* if we stall, we need to clear it before we go on */
        if (result == -EPIPE) {
       	        US_DEBUGP("EPIPE: clearing endpoint halt for"
			" pipe 0x%x, stalled at %d bytes\n",
			pipe, act_len);
               	usb_clear_halt(us->pusb_dev, pipe);
        }

	if (result) {

                /* NAK - that means we've retried a few times already */
       	        if (result == -ETIMEDOUT) {
                        US_DEBUGP("usbat_raw_bulk():"
				" device NAKed\n");
                        return US_BULK_TRANSFER_FAILED;
                }

                /* -ENOENT -- we canceled this transfer */
                if (result == -ENOENT) {
                        US_DEBUGP("usbat_raw_bulk():"
				" transfer aborted\n");
                        return US_BULK_TRANSFER_ABORTED;
                }

		if (result == -EPIPE) {
			US_DEBUGP("usbat_raw_bulk():"
				" output pipe stalled\n");
			return USB_STOR_TRANSPORT_FAILED;
		}

                /* the catch-all case */
                US_DEBUGP("us_transfer_partial(): unknown error\n");
                return US_BULK_TRANSFER_FAILED;
        }
	
	if (act_len != len) {
		US_DEBUGP("Warning: Transferred only %d bytes\n",
			act_len);
		return US_BULK_TRANSFER_SHORT;
	}

	US_DEBUGP("Transfered %d of %d bytes\n", act_len, len);

	return US_BULK_TRANSFER_GOOD;
}

/*
 * Note: direction must be set if command_len == 0.
 */

static int sddr09_bulk_transport(struct us_data *us,
			  unsigned char *command,
			  unsigned short command_len,
			  int direction,
			  unsigned char *data,
			  unsigned short len,
			  int use_sg) {

	int result = USB_STOR_TRANSPORT_GOOD;
	int transferred = 0;
	unsigned char execute[8] = {
		0x40, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	int i;
	struct scatterlist *sg;
	char string[64];
/*
	if (command_len != 0) {

		// Fix up the command's data length

		command[6] = len&0xFF;
		command[7] = (len>>8)&0xFF;

		result = sddr09_send_control(us, 
					  execute,
					  command,
					  command_len);

		if (result != USB_STOR_TRANSPORT_GOOD)
			return result;
	}
*/
	if (len==0)
		return USB_STOR_TRANSPORT_GOOD;


	/* transfer the data payload for the command, if there is any */


	if (command_len != 0)
		direction = (command[0]&0x80) ? SCSI_DATA_READ :
			SCSI_DATA_WRITE;

	if (direction == SCSI_DATA_WRITE) {

		/* Debug-print the first 48 bytes of the write transfer */

		if (!use_sg) {
			string[0] = 0;
			for (i=0; i<len && i<48; i++) {
				sprintf(string+strlen(string), "%02X ",
				  data[i]);
				if ((i%16)==15) {
					US_DEBUGP("%s\n", string);
					string[0] = 0;
				}
			}
			if (string[0]!=0)
				US_DEBUGP("%s\n", string);
		}
	}


	US_DEBUGP("SCM data %s transfer %d sg buffers %d\n",
		  ( direction==SCSI_DATA_READ ? "in" : "out"),
		  len, use_sg);

	if (!use_sg)
		result = sddr09_raw_bulk(us, direction, data, len);
	else {
		sg = (struct scatterlist *)data;
		for (i=0; i<use_sg && transferred<len; i++) {
			result = sddr09_raw_bulk(us, direction,
				sg[i].address, 
				len-transferred > sg[i].length ?
					sg[i].length : len-transferred);
			if (result!=US_BULK_TRANSFER_GOOD)
				break;
			transferred += sg[i].length;
		}
	}

	return result;
}

int sddr09_read_data(struct us_data *us,
		unsigned long address,
		unsigned short sectors,
		unsigned char *content,
		int use_sg) {

	int result;
	unsigned char command[12] = {
		0xe8, 0x20, MSB_of(address>>16),
		LSB_of(address>>16), MSB_of(address&0xFFFF),
		LSB_of(address&0xFFFF), 0, 0, 0, 0,
		MSB_of(sectors), LSB_of(sectors)
	};

	result = sddr09_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0,
		0x41,
		0,
		0,
		command,
		12);
		
	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = sddr09_bulk_transport(us,
		NULL, 0, SCSI_DATA_READ, content,
		sectors*512, use_sg);

	return result;
}

int sddr09_read_control(struct us_data *us,
		unsigned long address,
		unsigned short sectors,
		unsigned char *content,
		int use_sg) {

	int result;
	unsigned char command[12] = {
		0xe8, 0x21, MSB_of(address>>16),
		LSB_of(address>>16), MSB_of(address&0xFFFF),
		LSB_of(address&0xFFFF), 0, 0, 0, 0,
		MSB_of(sectors), LSB_of(sectors)
	};

	result = sddr09_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0,
		0x41,
		0,
		0,
		command,
		12);
		
	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = sddr09_bulk_transport(us,
		NULL, 0, SCSI_DATA_READ, content,
		sectors*64, use_sg);

	return result;
}

int sddr09_read_deviceID(struct us_data *us,
		unsigned char *manufacturerID,
		unsigned char *deviceID) {

	int result;
	unsigned char command[12] = {
		0xed, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	unsigned char content[64];

	result = sddr09_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0,
		0x41,
		0,
		0,
		command,
		12);

	US_DEBUGP("Result of send_control for device ID is %d\n",
		result);
		
	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = sddr09_bulk_transport(us,
		NULL, 0, SCSI_DATA_READ, content,
		64, 0);

	*manufacturerID = content[0];
	*deviceID = content[1];

	return result;
}

int sddr09_read_status(struct us_data *us,
		unsigned char *status) {

	int result;
	unsigned char command[12] = {
		0xec, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	unsigned char content[2];

	result = sddr09_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0,
		0x41,
		0,
		0,
		command,
		12);
		
	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = sddr09_bulk_transport(us,
		NULL, 0, SCSI_DATA_READ, status,
		1, 0);

	return result;
}

int sddr09_reset(struct us_data *us) {

	int result;
	unsigned char command[12] = {
		0xeb, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};

	result = sddr09_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0,
		0x41,
		0,
		0,
		command,
		12);
		
	return result;
}

/*
static int init_sddr09(struct us_data *us) {

	int result;
	unsigned char data[14];
	unsigned char command[8] = {
		0xc1, 0x01, 0, 0, 0, 0, 0, 0
	};
	unsigned char command2[8] = {
		0x41, 0, 0, 0, 0, 0, 0, 0
	};
	unsigned char tur[12] = {
		0x03, 0x20, 0, 0, 0x0e, 0, 0, 0, 0, 0, 0, 0
	};

	if ( (result = sddr09_send_control(us, command, data, 2)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("SDDR09: %02X %02X\n", data[0], data[1]);

	command[1] = 0x08;

	if ( (result = sddr09_send_control(us, command, data, 2)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("SDDR09: %02X %02X\n", data[0], data[1]);

	if ( (result = sddr09_send_control(us, command2, tur, 12)) !=
			USB_STOR_TRANSPORT_GOOD) {
		US_DEBUGP("SDDR09: request sense failed\n");
		return result;
	}

	if ( (result = sddr09_raw_bulk(
		us, SCSI_DATA_READ, data, 14)) !=
			USB_STOR_TRANSPORT_GOOD) {
		US_DEBUGP("SDDR09: request sense bulk in failed\n");
		return result;
	}

	US_DEBUGP("SDDR09: request sense worked\n");

	return result;
}
*/

/*
 * Transport for the Sandisk SDDR-09
 */
int sddr09_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int result;
	unsigned char send_scsi_command[8] = {
		0x41, 0, 0, 0, 0, 0, 0, 0
	};
	int i;
	char string[64];
	unsigned char inquiry_response[36] = {
		0x00, 0x80, 0x00, 0x02, 0x1F, 0x00, 0x00, 0x00,
		'S', 'a', 'n', 'D', 'i', 's', 'k', ' ',
		'I', 'm', 'a', 'g', 'e', 'M', 'a', 't',
		'e', ' ', 'S', 'D', 'D', 'R', '0', '9',
		' ', ' ', ' ', ' '
	};
	unsigned char deviceID;
	unsigned char manufacturerID;
	unsigned char *ptr;

/*
	if (us->flags & US_FL_NEED_INIT) {
		US_DEBUGP("SDDR-09: initializing\n");
		init_sddr09(us);
		us->flags &= ~US_FL_NEED_INIT;
	}
*/

	ptr = (unsigned char *)srb->request_buffer;

	/* Dummy up a response for INQUIRY since SDDR09 doesn't
	   respond to INQUIRY commands */

	if (srb->cmnd[0] == INQUIRY) {
		memcpy(srb->request_buffer, inquiry_response, 36);
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == READ_CAPACITY) {

		US_DEBUGP("Reading capacity...\n");

		result = sddr09_read_deviceID(us,
			&manufacturerID,
			&deviceID);

		US_DEBUGP("Result of read_deviceID is %d\n",
			result);

		if (result != USB_STOR_TRANSPORT_GOOD)
			return result;

		US_DEBUGP("Device ID = %02X\n", deviceID);
		US_DEBUGP("Manuf  ID = %02X\n", manufacturerID);

		ptr[0] = 0;
		ptr[1] = 0;
		ptr[2] = 0;
		ptr[3] = 0;

		switch (deviceID) {

		case 0x6e: // 1MB
		case 0xe8:
		case 0xec:
			ptr[4] = 0;
			ptr[5] = 0x10;
			break;

		case 0xea: // 2MB
		case 0x64:
		case 0x5d:
			ptr[4] = 0;
			ptr[5] = 0x20;
			break;

		case 0xe3: // 4MB
		case 0xe5:
		case 0x6b:
		case 0xd5:
			ptr[4] = 0;
			ptr[5] = 0x40;
			break;

		case 0xe6: // 8MB
		case 0xd6:
			ptr[4] = 0;
			ptr[5] = 0x80;
			break;

		case 0x75: // 32MB
			ptr[4] = 0x02;
			ptr[5] = 0;
			break;

		default: // unknown
			ptr[4] = 0;
			ptr[5] = 0;

		}

		ptr[6] = 0;
		ptr[7] = 0;
		
		return USB_STOR_TRANSPORT_GOOD;
	}

	for (; srb->cmd_len<12; srb->cmd_len++)
		srb->cmnd[srb->cmd_len] = 0;

	srb->cmnd[1] = 0x20;

	string[0] = 0;
	for (i=0; i<12; i++)
	  sprintf(string+strlen(string), "%02X ", srb->cmnd[i]);

	US_DEBUGP("SDDR09: Send control for command %s\n",
		string);

	if ( (result = sddr09_send_control(us,
			usb_sndctrlpipe(us->pusb_dev,0),
			0,
			0x41,
			0,
			0,
			srb->cmnd,
			12)) != USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("SDDR09: Control for command OK\n");

	if (srb->request_bufflen == 0)
		return USB_STOR_TRANSPORT_GOOD;
	
	if (srb->sc_data_direction == SCSI_DATA_WRITE ||
	    srb->sc_data_direction == SCSI_DATA_READ) {

		US_DEBUGP("SDDR09: %s %d bytes\n",
			srb->sc_data_direction==SCSI_DATA_WRITE ?
			  "sending" : "receiving",
			srb->request_bufflen);

		result = sddr09_bulk_transport(us,
			NULL, 0, srb->sc_data_direction,
			srb->request_buffer, 
			srb->request_bufflen, srb->use_sg);

		return result;

	} 

	return USB_STOR_TRANSPORT_GOOD;
}

