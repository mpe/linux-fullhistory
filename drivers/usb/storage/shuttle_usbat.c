/* Driver for SCM Microsystems USB-ATAPI cable
 *
 * $Id: shuttle_usbat.c,v 1.2 2000/08/03 00:03:39 groovyjava Exp $
 *
 * SCM driver v0.2:
 *
 * Removed any reference to maxlen for bulk transfers.
 * Changed scm_bulk_transport to allow for transfers without commands.
 * Changed hp8200e transport to use the request_bufflen field in the
 *   SCSI command for the length of the transfer, rather than calculating
 *   it ourselves based on the command.
 *
 * SCM driver v0.1:
 *
 * First release - hp8200e.
 *
 * Current development and maintainance by:
 *   (c) 2000 Robert Baruch (autophile@dol.net)
 *
 * Many originally ATAPI devices were slightly modified to meet the USB
 * market by using some kind of translation from ATAPI to USB on the host,
 * and the peripheral would translate from USB back to ATAPI.
 *
 * SCM Microsystems (www.scmmicro.com) makes a device, sold to OEM's only, 
 * which does the USB-to-ATAPI conversion.  By obtaining the data sheet on
 * their device under nondisclosure agreement, I have been able to write
 * this driver for Linux.
 *
 * The chip used in the device can also be used for EPP and ISA translation
 * as well. This driver is only guaranteed to work with the ATAPI
 * translation.
 *
 * The only peripherals that I know of (as of 14 Jul 2000) that uses this
 * device is the Hewlett-Packard 8200e CD-Writer Plus.
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
#include "shuttle_usbat.h"

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

static int usbat_send_control(struct us_data *us,
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

static int usbat_raw_bulk(struct us_data *us, 
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

static int usbat_bulk_transport(struct us_data *us,
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
	int pipe;

/*
	if (command_len != 0) {

		// Fix up the command's data length

		command[6] = len&0xFF;
		command[7] = (len>>8)&0xFF;

		

		result = usbat_send_control(us, 
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
		result = usbat_raw_bulk(us, direction, data, len);
	else {
		sg = (struct scatterlist *)data;
		for (i=0; i<use_sg && transferred<len; i++) {
			result = usbat_raw_bulk(us, direction,
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

int usbat_read(struct us_data *us,
	     unsigned char access,
	     unsigned char reg, 
	     unsigned char *content) {

	int result;
	unsigned char command[8] = {
		0xC0, access, reg, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	result = usbat_send_control(us,
		usb_rcvctrlpipe(us->pusb_dev,0),
		access,
		0xC0,
		(u16)reg,
		0,
		content,
		1);
		
	// result =  usbat_send_control(us, command, content, 1);

	return result;
}

int usbat_write(struct us_data *us,
	     unsigned char access,
	     unsigned char reg, 
	     unsigned char content) {

	int result;
	unsigned char command[8] = {
		0x40, access|0x01, reg, content, 0x00, 0x00, 0x00, 0x00
	};

	result = usbat_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		access|0x01,
		0x40,
		short_pack(reg, content),
		0,
		NULL,
		0);
		
	// result =  usbat_send_control(us, command, NULL, 0);

	return result;
}

int usbat_set_shuttle_features(struct us_data *us,
	     unsigned char external_trigger,
	     unsigned char epp_control, 
	     unsigned char mask_byte, 
	     unsigned char test_pattern, 
	     unsigned char subcountH, 
	     unsigned char subcountL) {

	int result;
	unsigned char command[8] = {
		0x40, 0x81, epp_control, external_trigger,
		test_pattern, mask_byte, subcountL, subcountH
	};

	result = usbat_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0x80,
		0x40,
		0,
		0,
		command,
		8);
		
	// result =  usbat_bulk_transport(us, command, 8, 0, NULL, 0, 0);

	return result;
}

int usbat_read_block(struct us_data *us,
	     unsigned char access,
	     unsigned char reg, 
	     unsigned char *content,
	     unsigned short len,
	     int use_sg) {

	int result;
	unsigned char command[8] = {
		0xC0, access|0x02, reg, 0x00, 0x00, 0x00, 
		LSB_of(len), MSB_of(len)
	};

	result = usbat_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0x80,
		0x40,
		0,
		0,
		command,
		8);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = usbat_bulk_transport(us,
		NULL, 0, SCSI_DATA_READ, content, len, use_sg);
		
	// result =  usbat_bulk_transport(us,
	//	command, 8, 0, content, len, use_sg);

	return result;
}

/*
 * Block, waiting for an ATA device to become not busy or to report
 * an error condition.
 */

int usbat_wait_not_busy(struct us_data *us) {

	int i;
	int result;
	unsigned char status;

	/* Synchronizing cache on a CDR could take a heck of a long time,
	   but probably not more than 15 minutes or so */

	for (i=0; i<500; i++) {
 		result = usbat_read(us, USBAT_ATA, 0x17, &status);
		US_DEBUGP("SCM: Write ATA data status is %02X\n", status);
		if (result!=USB_STOR_TRANSPORT_GOOD)
			return result;
		if (status&0x01) // check condition
			return USB_STOR_TRANSPORT_FAILED;
		if (status&0x20) // device fault
			return USB_STOR_TRANSPORT_FAILED;
		if ((status&0x80)!=0x80) // not busy
			break;
		if (i<5)
			wait_ms(100);
		else if (i<20)
			wait_ms(500);
		else if (i<49)
			wait_ms(1000);
		else if (i<499)
			wait_ms(2000);
	}

	if (i==500)
		return USB_STOR_TRANSPORT_FAILED;

	return USB_STOR_TRANSPORT_GOOD;
}

int usbat_write_block(struct us_data *us,
	     unsigned char access,
	     unsigned char reg, 
	     unsigned char *content,
	     unsigned short len,
	     int use_sg) {

	int result;
	unsigned char command[8] = {
		0x40, access|0x03, reg, 0x00, 0x00, 0x00, 
		LSB_of(len), MSB_of(len)
	};

	result = usbat_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0x80,
		0x40,
		0,
		0,
		command,
		8);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = usbat_bulk_transport(us,
		NULL, 0, SCSI_DATA_WRITE, content, len, use_sg);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	// result =  usbat_bulk_transport(us,
	//	command, 8, 0, content, len, use_sg);

	return usbat_wait_not_busy(us);
}

int usbat_write_block_test(struct us_data *us,
	     unsigned char access,
	     unsigned char *registers,
	     unsigned char *data_out,
	     unsigned short num_registers,
	     unsigned char data_reg, 
	     unsigned char status_reg, 
	     unsigned char qualifier, 
	     unsigned char timeout, 
	     unsigned char *content,
	     unsigned short len,
	     int use_sg) {

	int result;

	// Not really sure the 0x07, 0x17, 0xfc, 0xe7 is necessary here,
	// but that's what came out of the trace.

	unsigned char command[16] = {
		0x40, access|0x07, 0x07, 0x17, 0xfc, 0xe7,
		LSB_of(num_registers*2), MSB_of(num_registers*2),
		0x40, access|0x05, data_reg, status_reg,
		qualifier, timeout, LSB_of(len), MSB_of(len)
	};
	int i;
	unsigned char data[num_registers*2];
	int transferred;
	struct scatterlist *sg;
	char string[64];

	for (i=0; i<num_registers; i++) {
		data[i<<1] = registers[i];
		data[1+(i<<1)] = data_out[i];
	}

	result = usbat_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0x80,
		0x40,
		0,
		0,
		command,
		16);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = usbat_bulk_transport(us,
		NULL, 0, SCSI_DATA_WRITE, data, num_registers*2, 0);

	// result =  usbat_bulk_transport(us,
	//	command, 16, 0, data, num_registers*2, 0);

	if (result!=USB_STOR_TRANSPORT_GOOD)
		return result;

	// transferred = 0;

	US_DEBUGP("Transfer out %d bytes, sg buffers %d\n",
		len, use_sg);

	result = usbat_bulk_transport(us,
		NULL, 0, SCSI_DATA_WRITE, content, len, use_sg);

/*
	if (!use_sg) {

		// Debug-print the first 48 bytes of the transfer

		string[0] = 0;
		for (i=0; i<len && i<48; i++) {
			sprintf(string+strlen(string), "%02X ",
				content[i]);
			if ((i%16)==15) {
				US_DEBUGP("%s\n", string);
				string[0] = 0;
			}
		}
		if (string[0]!=0)
			US_DEBUGP("%s\n", string);

		result = usbat_raw_bulk(us, SCSI_DATA_WRITE, content, len);

	} else {

		sg = (struct scatterlist *)content;
		for (i=0; i<use_sg && transferred<len; i++) {
			result = usbat_raw_bulk(us, SCSI_DATA_WRITE,
				sg[i].address, 
				len-transferred > sg[i].length ?
					sg[i].length : len-transferred);
			if (result!=US_BULK_TRANSFER_GOOD)
				break;
			transferred += sg[i].length;
		}
	}
*/
	if (result!=USB_STOR_TRANSPORT_GOOD)
		return result;

	return usbat_wait_not_busy(us);
}

int usbat_multiple_write(struct us_data *us, 
			unsigned char access,
			unsigned char *registers,
			unsigned char *data_out,
			unsigned short num_registers) {

	int result;
	unsigned char data[num_registers*2];
	int i;
	unsigned char command[8] = {
		0x40, access|0x07, 0x00, 0x00, 0x00, 0x00,
		LSB_of(num_registers*2), MSB_of(num_registers*2)
	};

	for (i=0; i<num_registers; i++) {
		data[i<<1] = registers[i];
		data[1+(i<<1)] = data_out[i];
	}

	result = usbat_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0x80,
		0x40,
		0,
		0,
		command,
		8);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	result = usbat_bulk_transport(us,
		NULL, 0, SCSI_DATA_WRITE, data, num_registers*2, 0);

	// result = usbat_bulk_transport(us, cmd, 8, 0, 
	//	data, num_registers*2, 0);

	if (result!=USB_STOR_TRANSPORT_GOOD)
		return result;

	return usbat_wait_not_busy(us);
}

int usbat_read_user_io(struct us_data *us,
		unsigned char *data_flags) {

	unsigned char command[8] = {
		0xC0, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	int result;

	result = usbat_send_control(us,
		usb_rcvctrlpipe(us->pusb_dev,0),
		0x82,
		0xC0,
		0,
		0,
		data_flags,
		1);
		
	// result = usbat_send_control(us, command, data_flags, 1);

	return result;
}

int usbat_write_user_io(struct us_data *us,
		unsigned char enable_flags,
		unsigned char data_flags) {

	unsigned char command[8] = {
		0x40, 0x82, enable_flags, data_flags, 0x00, 0x00, 0x00, 0x00
	};
	int result;

	result = usbat_send_control(us,
		usb_sndctrlpipe(us->pusb_dev,0),
		0x82,
		0x40,
		short_pack(enable_flags, data_flags),
		0,
		NULL,
		0);
		
	// result = usbat_send_control(us, command, NULL, 0);

	return result;
}

static int hp_8200e_select_and_test_registers(struct us_data *us) {

	int result;
	int selector;
	unsigned char status;

	// try device = master, then device = slave.

	for (selector = 0xA0; selector <= 0xB0; selector += 0x10) {

		if ( (result = usbat_write(us, USBAT_ATA, 0x16, selector)) != 
				USB_STOR_TRANSPORT_GOOD)
			return result;

		if ( (result = usbat_read(us, USBAT_ATA, 0x17, &status)) != 
				USB_STOR_TRANSPORT_GOOD)
			return result;

		if ( (result = usbat_read(us, USBAT_ATA, 0x16, &status)) != 
				USB_STOR_TRANSPORT_GOOD)
			return result;

		if ( (result = usbat_read(us, USBAT_ATA, 0x14, &status)) != 
				USB_STOR_TRANSPORT_GOOD)
			return result;

		if ( (result = usbat_read(us, USBAT_ATA, 0x15, &status)) != 
				USB_STOR_TRANSPORT_GOOD)
			return result;

		if ( (result = usbat_write(us, USBAT_ATA, 0x14, 0x55)) != 
				USB_STOR_TRANSPORT_GOOD)
			return result;

		if ( (result = usbat_write(us, USBAT_ATA, 0x15, 0xAA)) != 
				USB_STOR_TRANSPORT_GOOD)
			return result;

		if ( (result = usbat_read(us, USBAT_ATA, 0x14, &status)) != 
				USB_STOR_TRANSPORT_GOOD)
			return result;

		if ( (result = usbat_read(us, USBAT_ATA, 0x15, &status)) != 
				USB_STOR_TRANSPORT_GOOD)
			return result;
	}

	return result;
}

static int init_8200e(struct us_data *us) {

	int result;
	unsigned char status;

	// Enable peripheral control signals

	if ( (result = usbat_write_user_io(us,
	  USBAT_UIO_OE1 | USBAT_UIO_OE0,
	  USBAT_UIO_EPAD | USBAT_UIO_1)) != USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 1\n");

	wait_ms(2000);

	if ( (result = usbat_read_user_io(us, &status)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 2\n");

	if ( (result = usbat_read_user_io(us, &status)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 3\n");

	// Reset peripheral, enable periph control signals
	// (bring reset signal up)

	if ( (result = usbat_write_user_io(us,
	  USBAT_UIO_DRVRST | USBAT_UIO_OE1 | USBAT_UIO_OE0,
	  USBAT_UIO_EPAD | USBAT_UIO_1)) != USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 4\n");

	// Enable periph control signals
	// (bring reset signal down)

	if ( (result = usbat_write_user_io(us,
	  USBAT_UIO_OE1 | USBAT_UIO_OE0,
	  USBAT_UIO_EPAD | USBAT_UIO_1)) != USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 5\n");

	wait_ms(250);

	// Write 0x80 to ISA port 0x3F

	if ( (result = usbat_write(us, USBAT_ISA, 0x3F, 0x80)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 6\n");

	// Read ISA port 0x27

	if ( (result = usbat_read(us, USBAT_ISA, 0x27, &status)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 7\n");

	if ( (result = usbat_read_user_io(us, &status)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 8\n");

	if ( (result = hp_8200e_select_and_test_registers(us)) !=
			 USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 9\n");

	if ( (result = usbat_read_user_io(us, &status)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 10\n");

	// Enable periph control signals and card detect

	if ( (result = usbat_write_user_io(us,
	  USBAT_UIO_ACKD |USBAT_UIO_OE1 | USBAT_UIO_OE0,
	  USBAT_UIO_EPAD | USBAT_UIO_1)) != USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 11\n");

	if ( (result = usbat_read_user_io(us, &status)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 12\n");

	wait_ms(1400);

	if ( (result = usbat_read_user_io(us, &status)) !=
			USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 13\n");

	if ( (result = hp_8200e_select_and_test_registers(us)) !=
			 USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 14\n");

	if ( (result = usbat_set_shuttle_features(us, 
			0x83, 0x00, 0x88, 0x08, 0x15, 0x14)) !=
			 USB_STOR_TRANSPORT_GOOD)
		return result;

	US_DEBUGP("INIT 15\n");

	return result;
}

/*
 * Transport for the HP 8200e
 */
int hp8200e_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int result;
	unsigned char status;
	unsigned char registers[32];
	unsigned char data[32];
	unsigned int len;
	int i;
	char string[64];

	/* This table tells us:
	   X = command not supported
	   L = return length in cmnd[4] (8 bits).
	   H = return length in cmnd[7] and cmnd[8] (16 bits).
	   D = return length in cmnd[6] to cmnd[9] (32 bits).
	   B = return length/blocksize in cmnd[6] to cmnd[8].
	   T = return length in cmnd[6] to cmnd[8] (24 bits).
	   0-9 = fixed return length
	   W = 24 bytes
	   h = return length/2048 in cmnd[7-8].
	*/

	static char *lengths =

	/* 0123456789ABCDEF   0123456789ABCDEF */

	  "0XXL0XXXXXXXXXXX" "XXLXXXXXXXX0XX0X"  /* 00-1F */
	  "XXXXX8XXhXH0XXX0" "XXXXX0XXXXXXXXXX"  /* 20-3F */
	  "XXHHL0X0XXH0XX0X" "XHH00HXX0TH0H0XX"  /* 40-5F */
	  "XXXXXXXXXXXXXXXX" "XXXXXXXXXXXXXXXX"  /* 60-7F */
	  "XXXXXXXXXXXXXXXX" "XXXXXXXXXXXXXXXX"  /* 80-9F */
	  "X0XXX0XXDXDXXXXX" "XXXXXXXXX000XHBX"  /* A0-BF */
	  "XXXXXXXXXXXXXXXX" "XXXXXXXXXXXXXXXX"  /* C0-DF */
	  "XDXXXXXXXXXXXXXX" "XXW00HXXXXXXXXXX"; /* E0-FF */

	if (us->flags & US_FL_NEED_INIT) {
		US_DEBUGP("8200e: initializing\n");
		init_8200e(us);
		us->flags &= ~US_FL_NEED_INIT;
	}

	len = srb->request_bufflen;

/*	if (srb->sc_data_direction == SCSI_DATA_WRITE)
		len = srb->request_bufflen;
	else {

		switch (lengths[srb->cmnd[0]]) {

		case 'L':
			len = srb->cmnd[4];
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			len = lengths[srb->cmnd[0]]-'0';
			break;
		case 'H':
			len = (((unsigned int)srb->cmnd[7])<<8) | srb->cmnd[8];
			break;
		case 'h':
			len = (((unsigned int)srb->cmnd[7])<<8) | srb->cmnd[8];
			len <<= 11; // *2048
			break;
		case 'T':
			len = (((unsigned int)srb->cmnd[6])<<16) |
			      (((unsigned int)srb->cmnd[7])<<8) |
			      srb->cmnd[8];
			break;
		case 'D':
			len = (((unsigned int)srb->cmnd[6])<<24) |
			      (((unsigned int)srb->cmnd[7])<<16) |
			      (((unsigned int)srb->cmnd[8])<<8) |
			      srb->cmnd[9];
			break;
		case 'W':
			len = 24;
			break;
		case 'B':
			// Let's try using the command structure's
			//   request_bufflen here 
			len = srb->request_bufflen;
			break;
		default:
			US_DEBUGP("Error: UNSUPPORTED COMMAND %02X\n",
				srb->cmnd[0]);
			return USB_STOR_TRANSPORT_ERROR;
		}
	} */

	if (len > 0xFFFF) {
		US_DEBUGP("Error: len = %08X... what do I do now?\n",
			len);
		return USB_STOR_TRANSPORT_ERROR;
	}

	// US_DEBUGP("XXXXXXXXXXXXXXXX req_bufflen %d, len %d, bufflen %d\n", 
 	//	srb->request_bufflen, len, srb->bufflen);

	/* Send A0 (ATA PACKET COMMAND).
	   Note: I guess we're never going to get any of the ATA
	   commands... just ATA Packet Commands.
 	 */

	registers[0] = 0x11;
	registers[1] = 0x12;
	registers[2] = 0x13;
	registers[3] = 0x14;
	registers[4] = 0x15;
	registers[5] = 0x16;
	registers[6] = 0x17;
	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = len&0xFF; 		// (cylL) = expected length (L)
	data[4] = (len>>8)&0xFF; 	// (cylH) = expected length (H)
	data[5] = 0xB0; 		// (device sel) = slave
	data[6] = 0xA0; 		// (command) = ATA PACKET COMMAND

	if (srb->sc_data_direction == SCSI_DATA_WRITE) {

		for (i=7; i<19; i++) {
			registers[i] = 0x10;
			data[i] = (i-7 >= srb->cmd_len) ? 0 : srb->cmnd[i-7];
		}

		result = usbat_write_block_test(us, USBAT_ATA, 
			registers, data, 19,
			0x10, 0x17, 0xFD, 0x30,
			srb->request_buffer, 
			len, srb->use_sg);

		return result;
	}

	if ( (result = usbat_multiple_write(us, 
			USBAT_ATA,
			registers, data, 7)) != USB_STOR_TRANSPORT_GOOD) {
		return result;
	}

	// Write the 12-byte command header.

	if ( (result = usbat_write_block(us, 
			USBAT_ATA, 0x10, srb->cmnd, 12, 0)) !=
				USB_STOR_TRANSPORT_GOOD) {
		return result;
	}

	// If there is response data to be read in 
	// then do it here.

	if (len != 0 && (srb->sc_data_direction == SCSI_DATA_READ)) {

		// How many bytes to read in? Check cylL register

		if ( (result = usbat_read(us, USBAT_ATA, 0x14, &status)) != 
		    USB_STOR_TRANSPORT_GOOD) {
			return result;
		}

		if (len>0xFF) { // need to read cylH also
			len = status;
			if ( (result = usbat_read(us, USBAT_ATA, 0x15,
				&status)) !=
				    USB_STOR_TRANSPORT_GOOD) {
				return result;
			}
			len += ((unsigned int)status)<<8;
		}
		else
			len = status;
			

		result = usbat_read_block(us, USBAT_ATA, 0x10, 
			srb->request_buffer, len, srb->use_sg);

		/* Debug-print the first 32 bytes of the transfer */

		if (!srb->use_sg) {
			string[0] = 0;
			for (i=0; i<len && i<32; i++) {
				sprintf(string+strlen(string), "%02X ",
				  ((unsigned char *)srb->request_buffer)[i]);
				if ((i%16)==15) {
					US_DEBUGP("%s\n", string);
					string[0] = 0;
				}
			}
			if (string[0]!=0)
				US_DEBUGP("%s\n", string);
		}
	}

	// US_DEBUGP("Command result %d\n", result);

	return result;
}


