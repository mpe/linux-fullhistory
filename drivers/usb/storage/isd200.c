/* Transport & Protocol Driver for In-System Design, Inc. ISD200 ASIC
 *
 * First release
 *
 * Current development and maintenance by:
 *   (c) 2000 In-System Design, Inc. (support@in-system.com)
 *
 * The ISD200 ASIC does not natively support ATA devices.  The chip
 * does implement an interface, the ATA Command Block (ATACB) which provides
 * a means of passing ATA commands and ATA register accesses to a device.
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
 * History:
 *
 *  2001-02-24: Removed lots of duplicate code and simplified the structure.
 *              (bjorn@haxx.se)
 */


/* Include files */

#include "transport.h"
#include "protocol.h"
#include "usb.h"
#include "debug.h"
#include "scsiglue.h"
#include "isd200.h"

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

/*
 * Inquiry defines. Used to interpret data returned from target as result
 * of inquiry command.
 *
 * DeviceType field
 */

#define DIRECT_ACCESS_DEVICE            0x00    /* disks */

/* Timeout defines (in Seconds) */

#define ISD200_ENUM_BSY_TIMEOUT         35
#define ISD200_ENUM_DETECT_TIMEOUT      30
#define ISD200_DEFAULT_TIMEOUT          30

/* device flags */
#define DF_ATA_DEVICE               0x0001
#define DF_MEDIA_STATUS_ENABLED     0x0002
#define DF_REMOVABLE_MEDIA          0x0004

/* capability bit definitions */
#define CAPABILITY_DMA		0x01
#define CAPABILITY_LBA		0x02

/* command_setX bit definitions */
#define COMMANDSET_REMOVABLE	0x02
#define COMMANDSET_MEDIA_STATUS 0x10

/* ATA Vendor Specific defines */
#define ATA_ADDRESS_DEVHEAD_STD      0xa0
#define ATA_ADDRESS_DEVHEAD_LBA_MODE 0x40    
#define ATA_ADDRESS_DEVHEAD_SLAVE    0x10

/* Action Select bits */
#define ACTION_SELECT_0             0x01
#define ACTION_SELECT_1             0x02
#define ACTION_SELECT_2             0x04
#define ACTION_SELECT_3             0x08
#define ACTION_SELECT_4             0x10
#define ACTION_SELECT_5             0x20
#define ACTION_SELECT_6             0x40
#define ACTION_SELECT_7             0x80

/* ATA error definitions not in <linux/hdreg.h> */
#define ATA_ERROR_MEDIA_CHANGE       0x20

/* ATA command definitions not in <linux/hdreg.h> */
#define ATA_COMMAND_GET_MEDIA_STATUS        0xDA
#define ATA_COMMAND_MEDIA_EJECT             0xED

/* ATA drive control definitions */
#define ATA_DC_DISABLE_INTERRUPTS    0x02
#define ATA_DC_RESET_CONTROLLER      0x04
#define ATA_DC_REENABLE_CONTROLLER   0x00

/*
 *  General purpose return codes
 */ 

#define ISD200_ERROR                -1
#define ISD200_GOOD                 0

/*
 * Transport return codes
 */

#define ISD200_TRANSPORT_GOOD       0   /* Transport good, command good     */
#define ISD200_TRANSPORT_FAILED     1   /* Transport good, command failed   */
#define ISD200_TRANSPORT_ERROR      2   /* Transport bad (i.e. device dead) */
#define ISD200_TRANSPORT_ABORTED    3   /* Transport aborted                */
#define ISD200_TRANSPORT_SHORT      4   /* Transport short                  */

/* driver action codes */
#define	ACTION_READ_STATUS	0
#define	ACTION_RESET		1
#define	ACTION_REENABLE		2
#define	ACTION_SOFT_RESET	3
#define	ACTION_ENUM		4
#define	ACTION_IDENTIFY		5


/*
 * ata_cdb struct
 */


union ata_cdb {
	struct {
		unsigned char SignatureByte0;
		unsigned char SignatureByte1;
		unsigned char ActionSelect;
		unsigned char RegisterSelect;
		unsigned char TransferBlockSize;
		unsigned char WriteData3F6;
		unsigned char WriteData1F1;
		unsigned char WriteData1F2;
		unsigned char WriteData1F3;
		unsigned char WriteData1F4;
		unsigned char WriteData1F5;
		unsigned char WriteData1F6;
		unsigned char WriteData1F7;
		unsigned char Reserved[3];
	} generic;
        
	struct {
		unsigned char SignatureByte0;
		unsigned char SignatureByte1;
		unsigned char ReadRegisterAccessBit : 1;
		unsigned char NoDeviceSelectionBit : 1;
		unsigned char NoBSYPollBit : 1;
		unsigned char IgnorePhaseErrorBit : 1;
		unsigned char IgnoreDeviceErrorBit : 1;
		unsigned char Reserved0Bit : 3;
		unsigned char SelectAlternateStatus : 1;
		unsigned char SelectError : 1;
		unsigned char SelectSectorCount : 1;
		unsigned char SelectSectorNumber : 1;
		unsigned char SelectCylinderLow : 1;
		unsigned char SelectCylinderHigh : 1;
		unsigned char SelectDeviceHead : 1;
		unsigned char SelectStatus : 1;
		unsigned char TransferBlockSize;
		unsigned char AlternateStatusByte;
		unsigned char ErrorByte;
		unsigned char SectorCountByte;
		unsigned char SectorNumberByte;
		unsigned char CylinderLowByte;
		unsigned char CylinderHighByte;
		unsigned char DeviceHeadByte;
		unsigned char StatusByte;
		unsigned char Reserved[3];
	} read;

        struct {
		unsigned char SignatureByte0;
		unsigned char SignatureByte1;
		unsigned char ReadRegisterAccessBit : 1;
		unsigned char NoDeviceSelectionBit : 1;
		unsigned char NoBSYPollBit : 1;
		unsigned char IgnorePhaseErrorBit : 1;
		unsigned char IgnoreDeviceErrorBit : 1;
		unsigned char Reserved0Bit : 3;
		unsigned char SelectDeviceControl : 1;
		unsigned char SelectFeatures : 1;
		unsigned char SelectSectorCount : 1;
		unsigned char SelectSectorNumber : 1;
		unsigned char SelectCylinderLow : 1;
		unsigned char SelectCylinderHigh : 1;
		unsigned char SelectDeviceHead : 1;
		unsigned char SelectCommand : 1;
		unsigned char TransferBlockSize;
		unsigned char DeviceControlByte;
		unsigned char FeaturesByte;
		unsigned char SectorCountByte;
		unsigned char SectorNumberByte;
		unsigned char CylinderLowByte;
		unsigned char CylinderHighByte;
		unsigned char DeviceHeadByte;
		unsigned char CommandByte;
		unsigned char Reserved[3];
	} write;
};


/*
 * Inquiry data structure. This is the data returned from the target
 * after it receives an inquiry.
 *
 * This structure may be extended by the number of bytes specified
 * in the field AdditionalLength. The defined size constant only
 * includes fields through ProductRevisionLevel.
 */

struct inquiry_data {
	unsigned char DeviceType : 5;
	unsigned char DeviceTypeQualifier : 3;
	unsigned char DeviceTypeModifier : 7;
	unsigned char RemovableMedia : 1;
	unsigned char Versions;
	unsigned char ResponseDataFormat : 4;
	unsigned char HiSupport : 1;
	unsigned char NormACA : 1;
	unsigned char ReservedBit : 1;
	unsigned char AERC : 1;
	unsigned char AdditionalLength;
	unsigned char Reserved[2];
	unsigned char SoftReset : 1;
	unsigned char CommandQueue : 1;
	unsigned char Reserved2 : 1;
	unsigned char LinkedCommands : 1;
	unsigned char Synchronous : 1;
	unsigned char Wide16Bit : 1;
	unsigned char Wide32Bit : 1;
	unsigned char RelativeAddressing : 1;
	unsigned char VendorId[8];
	unsigned char ProductId[16];
	unsigned char ProductRevisionLevel[4];
	unsigned char VendorSpecific[20];
	unsigned char Reserved3[40];
} __attribute__ ((packed));

/*
 * INQUIRY data buffer size
 */

#define INQUIRYDATABUFFERSIZE 36


/*
 * ISD200 CONFIG data struct
 */

struct isd200_config {
        unsigned char EventNotification;
        unsigned char ExternalClock;
        unsigned char ATAInitTimeout;
        unsigned char ATATiming : 4;
        unsigned char ATAPIReset : 1;
        unsigned char MasterSlaveSelection : 1;
        unsigned char ATAPICommandBlockSize : 2;
        unsigned char ATAMajorCommand;
        unsigned char ATAMinorCommand;
        unsigned char LastLUNIdentifier : 3;
        unsigned char DescriptOverride : 1;
        unsigned char ATA3StateSuspend : 1;
        unsigned char SkipDeviceBoot : 1;
        unsigned char ConfigDescriptor2 : 1;
        unsigned char InitStatus : 1;
        unsigned char SRSTEnable : 1;
        unsigned char Reserved0 : 7;
};


/*
 * ISD200 driver information struct
 */

struct isd200_info {
	struct inquiry_data InquiryData;
	struct hd_driveid drive;
	struct isd200_config ConfigData;
	unsigned char ATARegs[8];
	unsigned char DeviceHead;
	unsigned char DeviceFlags;

	/* maximum number of LUNs supported */
	unsigned char MaxLUNs;
};


/*
 * Read Capacity Data - returned in Big Endian format
 */

struct read_capacity_data {
	unsigned long LogicalBlockAddress;
	unsigned long BytesPerBlock;
};

/*
 * Read Block Limits Data - returned in Big Endian format
 * This structure returns the maximum and minimum block
 * size for a TAPE device.
 */

struct read_block_limits {
	unsigned char Reserved;
	unsigned char BlockMaximumSize[3];
	unsigned char BlockMinimumSize[2];
};


/*
 * Sense Data Format
 */

struct sense_data {
        unsigned char ErrorCode:7;
        unsigned char Valid:1;
        unsigned char SegmentNumber;
        unsigned char SenseKey:4;
        unsigned char Reserved:1;
        unsigned char IncorrectLength:1;
        unsigned char EndOfMedia:1;
        unsigned char FileMark:1;
        unsigned char Information[4];
        unsigned char AdditionalSenseLength;
        unsigned char CommandSpecificInformation[4];
        unsigned char AdditionalSenseCode;
        unsigned char AdditionalSenseCodeQualifier;
        unsigned char FieldReplaceableUnitCode;
        unsigned char SenseKeySpecific[3];
} __attribute__ ((packed));

/*
 * Default request sense buffer size
 */

#define SENSE_BUFFER_SIZE 18

/***********************************************************************
 * Helper routines
 ***********************************************************************/


/**************************************************************************
 * isd200_build_sense
 *                                                                         
 *  Builds an artificial sense buffer to report the results of a 
 *  failed command.
 *                                                                       
 * RETURNS:
 *    void
 */                                                                        
void isd200_build_sense(struct us_data *us, Scsi_Cmnd *srb)
{
        struct isd200_info *info = (struct isd200_info *)us->extra;
        struct sense_data *buf = (struct sense_data *) &srb->sense_buffer[0];
        unsigned char error = info->ATARegs[IDE_ERROR_OFFSET];

	if(error & ATA_ERROR_MEDIA_CHANGE) {
		buf->ErrorCode = 0x70;
		buf->Valid     = 1;
		buf->AdditionalSenseLength = 0xb;
		buf->SenseKey =  UNIT_ATTENTION;
		buf->AdditionalSenseCode = 0;
		buf->AdditionalSenseCodeQualifier = 0;
        } else if(error & MCR_ERR) {
		buf->ErrorCode = 0x70;
		buf->Valid     = 1;
		buf->AdditionalSenseLength = 0xb;
		buf->SenseKey =  UNIT_ATTENTION;
		buf->AdditionalSenseCode = 0;
		buf->AdditionalSenseCodeQualifier = 0;
        } else if(error & TRK0_ERR) {
		buf->ErrorCode = 0x70;
		buf->Valid     = 1;
		buf->AdditionalSenseLength = 0xb;
		buf->SenseKey =  NOT_READY;
		buf->AdditionalSenseCode = 0;
		buf->AdditionalSenseCodeQualifier = 0;
        } else if(error & ECC_ERR) {
		buf->ErrorCode = 0x70;
		buf->Valid     = 1;
		buf->AdditionalSenseLength = 0xb;
		buf->SenseKey =  DATA_PROTECT;
		buf->AdditionalSenseCode = 0;
		buf->AdditionalSenseCodeQualifier = 0;
        } else {
		buf->ErrorCode = 0;
		buf->Valid     = 0;
		buf->AdditionalSenseLength = 0;
		buf->SenseKey =  0;
		buf->AdditionalSenseCode = 0;
		buf->AdditionalSenseCodeQualifier = 0;
        }
}

/***********************************************************************
 * Data transfer routines
 ***********************************************************************/


/**************************************************************************
 * Transfer one SCSI scatter-gather buffer via bulk transfer
 *
 * Note that this function is necessary because we want the ability to
 * use scatter-gather memory.  Good performance is achieved by a combination
 * of scatter-gather and clustering (which makes each chunk bigger).
 *
 * Note that the lower layer will always retry when a NAK occurs, up to the
 * timeout limit.  Thus we don't have to worry about it for individual
 * packets.
 */
static int isd200_transfer_partial( struct us_data *us, 
				    unsigned char dataDirection,
				    char *buf, int length )
{
        int result;
        int partial;
        int pipe;

        /* calculate the appropriate pipe information */
	if (dataDirection == SCSI_DATA_READ)
                pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
        else
                pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);

        /* transfer the data */
        US_DEBUGP("isd200_transfer_partial(): xfer %d bytes\n", length);
        result = usb_stor_bulk_msg(us, buf, pipe, length, &partial);
        US_DEBUGP("usb_stor_bulk_msg() returned %d xferred %d/%d\n",
                  result, partial, length);

        /* if we stall, we need to clear it before we go on */
        if (result == -EPIPE) {
                US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
                usb_stor_clear_halt(us->pusb_dev, pipe);
        }
    
        /* did we send all the data? */
        if (partial == length) {
                US_DEBUGP("isd200_transfer_partial(): transfer complete\n");
                return ISD200_TRANSPORT_GOOD;
        }

        /* uh oh... we have an error code, so something went wrong. */
        if (result) {
                /* NAK - that means we've retried a few times already */
                if (result == -ETIMEDOUT) {
                        US_DEBUGP("isd200_transfer_partial(): device NAKed\n");
                        return ISD200_TRANSPORT_FAILED;
                }

                /* -ENOENT -- we canceled this transfer */
                if (result == -ENOENT) {
                        US_DEBUGP("isd200_transfer_partial(): transfer aborted\n");
                        return ISD200_TRANSPORT_ABORTED;
                }

                /* the catch-all case */
                US_DEBUGP("isd200_transfer_partial(): unknown error\n");
                return ISD200_TRANSPORT_FAILED;
        }

        /* no error code, so we must have transferred some data, 
         * just not all of it */
        return ISD200_TRANSPORT_SHORT;
}


/**************************************************************************
 * Transfer an entire SCSI command's worth of data payload over the bulk
 * pipe.
 *
 * Note that this uses us_transfer_partial to achieve it's goals -- this
 * function simply determines if we're going to use scatter-gather or not,
 * and acts appropriately.  For now, it also re-interprets the error codes.
 */
static void isd200_transfer( struct us_data *us, Scsi_Cmnd *srb )
{
        int i;
        int result = -1;
        struct scatterlist *sg;
        unsigned int total_transferred = 0;
        unsigned int transfer_amount;

        /* calculate how much we want to transfer */
	int dir = srb->sc_data_direction;
	srb->sc_data_direction = SCSI_DATA_WRITE;
        transfer_amount = usb_stor_transfer_length(srb);
	srb->sc_data_direction = dir;

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
                                result = isd200_transfer_partial(us, 
                                                                 srb->sc_data_direction,
                                                                 sg[i].address, 
                                                                 sg[i].length);
                                total_transferred += sg[i].length;
                        } else
                                result = isd200_transfer_partial(us, 
                                                                 srb->sc_data_direction,                            
                                                                 sg[i].address,
                                                                 transfer_amount - total_transferred);

                        /* if we get an error, end the loop here */
                        if (result)
                                break;
                }
        }
        else
                /* no scatter-gather, just make the request */
                result = isd200_transfer_partial(us, 
                                                 srb->sc_data_direction,
                                                 srb->request_buffer, 
                                                 transfer_amount);

        /* return the result in the data structure itself */
        srb->result = result;
}


/***********************************************************************
 * Transport routines
 ***********************************************************************/


/**************************************************************************
 *  ISD200 Bulk Transport
 *
 * Note:  This routine was copied from the usb_stor_Bulk_transport routine
 * located in the transport.c source file.  The scsi command is limited to
 * only 12 bytes while the CDB for the ISD200 must be 16 bytes.
 */
int isd200_Bulk_transport( struct us_data *us, Scsi_Cmnd *srb, 
                           union ata_cdb *AtaCdb, unsigned char AtaCdbLength )
{
        struct bulk_cb_wrap bcb;
        struct bulk_cs_wrap bcs;
        int result;
        int pipe;
        int partial;
        unsigned int transfer_amount;

	int dir = srb->sc_data_direction;
	srb->sc_data_direction = SCSI_DATA_WRITE;
        transfer_amount = usb_stor_transfer_length(srb);
	srb->sc_data_direction = dir;
    
        /* set up the command wrapper */
        bcb.Signature = cpu_to_le32(US_BULK_CB_SIGN);
        bcb.DataTransferLength = cpu_to_le32(transfer_amount);
        bcb.Flags = srb->sc_data_direction == SCSI_DATA_READ ? 1 << 7 : 0;
        bcb.Tag = srb->serial_number;
        bcb.Lun = srb->cmnd[1] >> 5;
        if (us->flags & US_FL_SCM_MULT_TARG)
                bcb.Lun |= srb->target << 4;

        bcb.Length = AtaCdbLength;
    
        /* construct the pipe handle */
        pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);
    
        /* copy the command payload */
        memset(bcb.CDB, 0, sizeof(bcb.CDB));
        memcpy(bcb.CDB, AtaCdb, bcb.Length);
    
        /* send it to out endpoint */
        US_DEBUGP("Bulk command S 0x%x T 0x%x Trg %d LUN %d L %d F %d CL %d\n",
                  le32_to_cpu(bcb.Signature), bcb.Tag,
                  (bcb.Lun >> 4), (bcb.Lun & 0xFF), 
                  bcb.DataTransferLength, bcb.Flags, bcb.Length);
        result = usb_stor_bulk_msg(us, &bcb, pipe, US_BULK_CB_WRAP_LEN, 
				   &partial);
        US_DEBUGP("Bulk command transfer result=%d\n", result);
    
	if (result == -ENOENT)
		return ISD200_TRANSPORT_ABORTED;
	else if (result == -EPIPE) {
		/* if we stall, we need to clear it before we go on */
                US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
                usb_stor_clear_halt(us->pusb_dev, pipe);
	} else if (result)  
                return ISD200_TRANSPORT_ERROR;
    
        /* if the command transfered well, then we go to the data stage */
        if (!result && bcb.DataTransferLength) {
		isd200_transfer(us, srb);
		US_DEBUGP("Bulk data transfer result 0x%x\n", srb->result);
		
		if (srb->result == ISD200_TRANSPORT_ABORTED)
			return ISD200_TRANSPORT_ABORTED;
        }
    
        /* See flow chart on pg 15 of the Bulk Only Transport spec for
         * an explanation of how this code works.
         */
    
        /* construct the pipe handle */
        pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
    
        /* get CSW for device status */
        US_DEBUGP("Attempting to get CSW...\n");
        result = usb_stor_bulk_msg(us, &bcs, pipe, US_BULK_CS_WRAP_LEN, 
				   &partial);
        if (result == -ENOENT)
                return ISD200_TRANSPORT_ABORTED;

        /* did the attempt to read the CSW fail? */
        if (result == -EPIPE) {
                US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
                usb_stor_clear_halt(us->pusb_dev, pipe);
           
                /* get the status again */
                US_DEBUGP("Attempting to get CSW (2nd try)...\n");
                result = usb_stor_bulk_msg(us, &bcs, pipe,
                                           US_BULK_CS_WRAP_LEN, &partial);

                /* if the command was aborted, indicate that */
                if (result == -ENOENT)
                        return ISD200_TRANSPORT_ABORTED;
        
                /* if it fails again, we need a reset and return an error*/
                if (result == -EPIPE) {
                        US_DEBUGP("clearing halt for pipe 0x%x\n", pipe);
                        usb_stor_clear_halt(us->pusb_dev, pipe);
                        return ISD200_TRANSPORT_ERROR;
                }
        }
    
        /* if we still have a failure at this point, we're in trouble */
        US_DEBUGP("Bulk status result = %d\n", result);
        if (result)
                return ISD200_TRANSPORT_ERROR;
    
        /* check bulk status */
        US_DEBUGP("Bulk status Sig 0x%x T 0x%x R %d Stat 0x%x\n",
                  le32_to_cpu(bcs.Signature), bcs.Tag, 
                  bcs.Residue, bcs.Status);
        if (bcs.Signature != cpu_to_le32(US_BULK_CS_SIGN) || 
            bcs.Tag != bcb.Tag || 
            bcs.Status > US_BULK_STAT_PHASE || partial != 13) {
                US_DEBUGP("Bulk logical error\n");
                return ISD200_TRANSPORT_ERROR;
        }
    
        /* based on the status code, we report good or bad */
        switch (bcs.Status) {
        case US_BULK_STAT_OK:
                /* command good -- note that we could be short on data */
                return ISD200_TRANSPORT_GOOD;

        case US_BULK_STAT_FAIL:
                /* command failed */
                return ISD200_TRANSPORT_FAILED;
        
        case US_BULK_STAT_PHASE:
                /* phase error */
                usb_stor_Bulk_reset(us);
                return ISD200_TRANSPORT_ERROR;
        }
    
        /* we should never get here, but if we do, we're in trouble */
        return ISD200_TRANSPORT_ERROR;
}


/**************************************************************************
 *  isd200_action
 *
 * Routine for sending commands to the isd200
 *
 * RETURNS:
 *    ISD status code
 */
static int isd200_action( struct us_data *us, int action, 
			  void* pointer, int value )
{
	union ata_cdb ata;
	struct scsi_cmnd srb;
	struct isd200_info *info = (struct isd200_info *)us->extra;
	int status;

	memset(&ata, 0, sizeof(ata));
	memset(&srb, 0, sizeof(srb));

	ata.generic.SignatureByte0 = info->ConfigData.ATAMajorCommand;
	ata.generic.SignatureByte1 = info->ConfigData.ATAMinorCommand;
	ata.generic.TransferBlockSize = 1;

	switch ( action ) {
	case ACTION_READ_STATUS:
		US_DEBUGP("   isd200_action(READ_STATUS)\n");
		ata.generic.ActionSelect = ACTION_SELECT_0|ACTION_SELECT_2;
		ata.read.SelectStatus = 1;
		ata.read.SelectError = 1;
		ata.read.SelectCylinderHigh = 1;
		ata.read.SelectCylinderLow = 1;
		srb.sc_data_direction = SCSI_DATA_READ;
		srb.request_buffer = pointer;
		srb.request_bufflen = value;
		break;

	case ACTION_ENUM:
		US_DEBUGP("   isd200_action(ENUM,0x%02x)\n",value);
		ata.generic.ActionSelect = ACTION_SELECT_1|ACTION_SELECT_2|
			                   ACTION_SELECT_3|ACTION_SELECT_4|
		                           ACTION_SELECT_5;
		ata.write.SelectDeviceHead = 1;
		ata.write.DeviceHeadByte = value;
		srb.sc_data_direction = SCSI_DATA_NONE;
		break;

	case ACTION_RESET:
		US_DEBUGP("   isd200_action(RESET)\n");
		ata.generic.ActionSelect = ACTION_SELECT_1|ACTION_SELECT_2|
			                   ACTION_SELECT_3|ACTION_SELECT_4;
		ata.write.SelectDeviceControl = 1;
		ata.write.DeviceControlByte = ATA_DC_RESET_CONTROLLER;
		srb.sc_data_direction = SCSI_DATA_NONE;
		break;

	case ACTION_REENABLE:
		US_DEBUGP("   isd200_action(REENABLE)\n");
		ata.generic.ActionSelect = ACTION_SELECT_1|ACTION_SELECT_2|
			                   ACTION_SELECT_3|ACTION_SELECT_4;
		ata.write.SelectDeviceControl = 1;
		ata.write.DeviceControlByte = ATA_DC_REENABLE_CONTROLLER;
		srb.sc_data_direction = SCSI_DATA_NONE;
		break;

	case ACTION_SOFT_RESET:
		US_DEBUGP("   isd200_action(SOFT_RESET)\n");
		ata.generic.ActionSelect = ACTION_SELECT_1|ACTION_SELECT_5;
		ata.write.SelectDeviceHead = 1;
		ata.write.DeviceHeadByte = info->DeviceHead;
		ata.write.SelectCommand = 1;
		ata.write.CommandByte = WIN_SRST;
		srb.sc_data_direction = SCSI_DATA_NONE;
		break;

	case ACTION_IDENTIFY:
		US_DEBUGP("   isd200_action(IDENTIFY)\n");
		ata.write.SelectCommand = 1;
		ata.write.CommandByte = WIN_IDENTIFY;
		srb.sc_data_direction = SCSI_DATA_READ;
		srb.request_buffer = (void *)&info->drive;
		srb.request_bufflen = sizeof(struct hd_driveid);
		break;

	default:
		US_DEBUGP("Error: Undefined action %d\n",action);
		break;
	}

	status = isd200_Bulk_transport(us, &srb, &ata, sizeof(ata.generic));
	if (status != ISD200_TRANSPORT_GOOD) {
		US_DEBUGP("   isd200_action(0x%02x) error: %d\n",action,status);
		status = ISD200_ERROR;
		/* need to reset device here */
	}

	return status;
}

/**************************************************************************
 * isd200_read_regs
 *                                                                         
 * Read ATA Registers
 *
 * RETURNS:
 *    ISD status code
 */                                                                        
int isd200_read_regs( struct us_data *us )
{
	struct isd200_info *info = (struct isd200_info *)us->extra;
	int retStatus = ISD200_GOOD;
	int transferStatus;

	US_DEBUGP("Entering isd200_IssueATAReadRegs\n");

	transferStatus = isd200_action( us, ACTION_READ_STATUS,
				    info->ATARegs, sizeof(info->ATARegs) );
	if (transferStatus != ISD200_TRANSPORT_GOOD) {
		US_DEBUGP("   Error reading ATA registers\n");
		retStatus = ISD200_ERROR;
        } else {
		US_DEBUGP("   Got ATA Register[IDE_ERROR_OFFSET] = 0x%x\n", 
			  info->ATARegs[IDE_ERROR_OFFSET]);
        }

	return retStatus;
}


/**************************************************************************
 * Invoke the transport and basic error-handling/recovery methods
 *
 * This is used by the protocol layers to actually send the message to
 * the device and recieve the response.
 */
void isd200_invoke_transport( struct us_data *us, 
			      Scsi_Cmnd *srb, 
			      union ata_cdb *ataCdb )
{
	int need_auto_sense = 0;
	int transferStatus;

	/* send the command to the transport layer */
	transferStatus = isd200_Bulk_transport(us, srb, ataCdb,
					       sizeof(ataCdb->generic));
	switch (transferStatus) {

	case ISD200_TRANSPORT_GOOD:
		/* Indicate a good result */
		srb->result = GOOD;
		break;

	case ISD200_TRANSPORT_ABORTED:
		/* if the command gets aborted by the higher layers, we need to
		 * short-circuit all other processing
		 */
		US_DEBUGP("-- transport indicates command was aborted\n");
		srb->result = DID_ABORT << 16;
		break;

	case ISD200_TRANSPORT_FAILED:
		US_DEBUGP("-- transport indicates command failure\n");
		need_auto_sense = 1;
		break;

	case ISD200_TRANSPORT_ERROR:
		US_DEBUGP("-- transport indicates transport failure\n");
		srb->result = DID_ERROR << 16;
		break;

	case ISD200_TRANSPORT_SHORT:
		if (!((srb->cmnd[0] == REQUEST_SENSE) ||
		      (srb->cmnd[0] == INQUIRY) ||
		      (srb->cmnd[0] == MODE_SENSE) ||
		      (srb->cmnd[0] == LOG_SENSE) ||
		      (srb->cmnd[0] == MODE_SENSE_10))) {
			US_DEBUGP("-- unexpectedly short transfer\n");
			need_auto_sense = 1;
		}
		break;
    
	default:
		US_DEBUGP("-- transport indicates unknown failure\n");   
		srb->result = DID_ERROR << 16;
       
	}

	if (need_auto_sense)
		if (isd200_read_regs(us) == ISD200_GOOD)
			isd200_build_sense(us, srb);

	/* Regardless of auto-sense, if we _know_ we have an error
	 * condition, show that in the result code
	 */
	if (transferStatus == ISD200_TRANSPORT_FAILED)
		srb->result = CHECK_CONDITION;
}


/**************************************************************************
 * isd200_write_config
 *                                                                         
 * Write the ISD200 Configuraton data
 *
 * RETURNS:
 *    ISD status code
 */                                                                        
int isd200_write_config( struct us_data *us ) 
{
	struct isd200_info *info = (struct isd200_info *)us->extra;
	int retStatus = ISD200_GOOD;
	int result;


	US_DEBUGP("Entering isd200_write_config\n");

	US_DEBUGP("   Writing the following ISD200 Config Data:\n");
	US_DEBUGP("      Event Notification: 0x%x\n", info->ConfigData.EventNotification);
	US_DEBUGP("      External Clock: 0x%x\n", info->ConfigData.ExternalClock);
	US_DEBUGP("      ATA Init Timeout: 0x%x\n", info->ConfigData.ATAInitTimeout);
	US_DEBUGP("      ATAPI Command Block Size: 0x%x\n", info->ConfigData.ATAPICommandBlockSize);
	US_DEBUGP("      Master/Slave Selection: 0x%x\n", info->ConfigData.MasterSlaveSelection);
	US_DEBUGP("      ATAPI Reset: 0x%x\n", info->ConfigData.ATAPIReset);
	US_DEBUGP("      ATA Timing: 0x%x\n", info->ConfigData.ATATiming);
	US_DEBUGP("      ATA Major Command: 0x%x\n", info->ConfigData.ATAMajorCommand);
	US_DEBUGP("      ATA Minor Command: 0x%x\n", info->ConfigData.ATAMinorCommand);
	US_DEBUGP("      Init Status: 0x%x\n", info->ConfigData.InitStatus);
	US_DEBUGP("      Config Descriptor 2: 0x%x\n", info->ConfigData.ConfigDescriptor2);
	US_DEBUGP("      Skip Device Boot: 0x%x\n", info->ConfigData.SkipDeviceBoot);
	US_DEBUGP("      ATA 3 State Supsend: 0x%x\n", info->ConfigData.ATA3StateSuspend);
	US_DEBUGP("      Descriptor Override: 0x%x\n", info->ConfigData.DescriptOverride);
	US_DEBUGP("      Last LUN Identifier: 0x%x\n", info->ConfigData.LastLUNIdentifier);
	US_DEBUGP("      SRST Enable: 0x%x\n", info->ConfigData.SRSTEnable);

	/* let's send the command via the control pipe */
	result = usb_stor_control_msg(
                us, 
                usb_sndctrlpipe(us->pusb_dev,0),
                0x01, 
                USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
                0x0000, 
                0x0002, 
                (void *) &info->ConfigData, 
                sizeof(info->ConfigData));

	if (result >= 0) {
		US_DEBUGP("   ISD200 Config Data was written successfully\n");
        } else {
		US_DEBUGP("   Request to write ISD200 Config Data failed!\n");

		/* STALL must be cleared when they are detected */
		if (result == -EPIPE) {
			US_DEBUGP("-- Stall on control pipe. Clearing\n");
			result = usb_stor_clear_halt(us->pusb_dev,
						     usb_sndctrlpipe(us->pusb_dev, 0));
			US_DEBUGP("-- usb_stor_clear_halt() returns %d\n", result);

		}
		retStatus = ISD200_ERROR;
        }

	US_DEBUGP("Leaving isd200_write_config %08X\n", retStatus);
	return retStatus;
}


/**************************************************************************
 * isd200_read_config
 *                                                                         
 * Reads the ISD200 Configuraton data
 *
 * RETURNS:
 *    ISD status code
 */                                                                        
int isd200_read_config( struct us_data *us ) 
{
	struct isd200_info *info = (struct isd200_info *)us->extra;
	int retStatus = ISD200_GOOD;
	int result;

	US_DEBUGP("Entering isd200_read_config\n");

	/* read the configuration information from ISD200.  Use this to */
	/* determine what the special ATA CDB bytes are.                */

	result = usb_stor_control_msg(
                us, 
                usb_rcvctrlpipe(us->pusb_dev,0),
                0x02, 
                USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
                0x0000, 
                0x0002, 
                (void *) &info->ConfigData, 
                sizeof(info->ConfigData));


	if (result >= 0) {
		US_DEBUGP("   Retrieved the following ISD200 Config Data:\n");
		US_DEBUGP("      Event Notification: 0x%x\n", info->ConfigData.EventNotification);
		US_DEBUGP("      External Clock: 0x%x\n", info->ConfigData.ExternalClock);
		US_DEBUGP("      ATA Init Timeout: 0x%x\n", info->ConfigData.ATAInitTimeout);
		US_DEBUGP("      ATAPI Command Block Size: 0x%x\n", info->ConfigData.ATAPICommandBlockSize);
		US_DEBUGP("      Master/Slave Selection: 0x%x\n", info->ConfigData.MasterSlaveSelection);
		US_DEBUGP("      ATAPI Reset: 0x%x\n", info->ConfigData.ATAPIReset);
		US_DEBUGP("      ATA Timing: 0x%x\n", info->ConfigData.ATATiming);
		US_DEBUGP("      ATA Major Command: 0x%x\n", info->ConfigData.ATAMajorCommand);
		US_DEBUGP("      ATA Minor Command: 0x%x\n", info->ConfigData.ATAMinorCommand);
		US_DEBUGP("      Init Status: 0x%x\n", info->ConfigData.InitStatus);
		US_DEBUGP("      Config Descriptor 2: 0x%x\n", info->ConfigData.ConfigDescriptor2);
		US_DEBUGP("      Skip Device Boot: 0x%x\n", info->ConfigData.SkipDeviceBoot);
		US_DEBUGP("      ATA 3 State Supsend: 0x%x\n", info->ConfigData.ATA3StateSuspend);
		US_DEBUGP("      Descriptor Override: 0x%x\n", info->ConfigData.DescriptOverride);
		US_DEBUGP("      Last LUN Identifier: 0x%x\n", info->ConfigData.LastLUNIdentifier);
		US_DEBUGP("      SRST Enable: 0x%x\n", info->ConfigData.SRSTEnable);
        } else {
		US_DEBUGP("   Request to get ISD200 Config Data failed!\n");

		/* STALL must be cleared when they are detected */
		if (result == -EPIPE) {
			US_DEBUGP("-- Stall on control pipe. Clearing\n");
			result = usb_stor_clear_halt(us->pusb_dev,   
						     usb_sndctrlpipe(us->pusb_dev, 0));
			US_DEBUGP("-- usb_stor_clear_halt() returns %d\n", result);

		}
		retStatus = ISD200_ERROR;
        }

	US_DEBUGP("Leaving isd200_read_config %08X\n", retStatus);
	return retStatus;
}


/**************************************************************************
 * isd200_atapi_soft_reset
 *                                                                         
 * Perform an Atapi Soft Reset on the device
 *
 * RETURNS:
 *    NT status code
 */                                                                        
int isd200_atapi_soft_reset( struct us_data *us ) 
{
	int retStatus = ISD200_GOOD;
	int transferStatus;

	US_DEBUGP("Entering isd200_atapi_soft_reset\n");

	transferStatus = isd200_action( us, ACTION_SOFT_RESET, NULL, 0 );
	if (transferStatus != ISD200_TRANSPORT_GOOD) {
		US_DEBUGP("   Error issuing Atapi Soft Reset\n");
		retStatus = ISD200_ERROR;
        }

	US_DEBUGP("Leaving isd200_atapi_soft_reset %08X\n", retStatus);
	return retStatus;
}


/**************************************************************************
 * isd200_srst
 *                                                                         
 * Perform an SRST on the device
 *
 * RETURNS:
 *    ISD status code
 */                                                                        
int isd200_srst( struct us_data *us ) 
{
	int retStatus = ISD200_GOOD;
	int transferStatus;

	US_DEBUGP("Entering isd200_SRST\n");

	transferStatus = isd200_action( us, ACTION_RESET, NULL, 0 );

	/* check to see if this request failed */
	if (transferStatus != ISD200_TRANSPORT_GOOD) {
		US_DEBUGP("   Error issuing SRST\n");
		retStatus = ISD200_ERROR;
        } else {
		/* delay 10ms to give the drive a chance to see it */
		wait_ms(10);

		transferStatus = isd200_action( us, ACTION_REENABLE, NULL, 0 );
		if (transferStatus != ISD200_TRANSPORT_GOOD) {
			US_DEBUGP("   Error taking drive out of reset\n");
			retStatus = ISD200_ERROR;
		} else {
			/* delay 50ms to give the drive a chance to recover after SRST */
			wait_ms(50);
		}
        }

	US_DEBUGP("Leaving isd200_srst %08X\n", retStatus);
	return retStatus;
}


/**************************************************************************
 * isd200_try_enum
 *                                                                         
 * Helper function for isd200_manual_enum(). Does ENUM and READ_STATUS
 * and tries to analyze the status registers
 *
 * RETURNS:
 *    ISD status code
 */                                                                        
static int isd200_try_enum(struct us_data *us, unsigned char master_slave,
			   int detect )
{
	int status = ISD200_GOOD;
	unsigned char regs[8];
	unsigned long endTime;
	struct isd200_info *info = (struct isd200_info *)us->extra;
	int recheckAsMaster = FALSE;

	if ( detect )
		endTime = jiffies + ISD200_ENUM_DETECT_TIMEOUT * HZ;
	else
		endTime = jiffies + ISD200_ENUM_BSY_TIMEOUT * HZ;

	/* loop until we detect !BSY or timeout */
	while(TRUE) {
#ifdef CONFIG_USB_STORAGE_DEBUG
		char* mstr = master_slave == ATA_ADDRESS_DEVHEAD_STD ?
			"Master" : "Slave";
#endif

		status = isd200_action( us, ACTION_ENUM, NULL, master_slave );
		if ( status != ISD200_GOOD )
			break;

		status = isd200_action( us, ACTION_READ_STATUS, 
					regs, sizeof(regs) );
		if ( status != ISD200_GOOD )
			break;

		if (!detect) {
			if (regs[IDE_STATUS_OFFSET] & BUSY_STAT ) {
				US_DEBUGP("   %s status is still BSY, try again...\n",mstr);
			} else {
				US_DEBUGP("   %s status !BSY, continue with next operation\n",mstr);
				break;
			}
		}
		/* check for BUSY_STAT and */
		/* WRERR_STAT (workaround ATA Zip drive) and */ 
		/* ERR_STAT (workaround for Archos CD-ROM) */
		else if (regs[IDE_STATUS_OFFSET] & 
			 (BUSY_STAT | WRERR_STAT | ERR_STAT )) {
			US_DEBUGP("   Status indicates it is not ready, try again...\n");
		}
		/* check for DRDY, ATA devices set DRDY after SRST */
		else if (regs[IDE_STATUS_OFFSET] & READY_STAT) {
			US_DEBUGP("   Identified ATA device\n");
			info->DeviceFlags |= DF_ATA_DEVICE;
			info->DeviceHead = master_slave;
			break;
		} 
		/* check Cylinder High/Low to
		   determine if it is an ATAPI device
		*/
		else if ((regs[IDE_HCYL_OFFSET] == 0xEB) &&
			 (regs[IDE_LCYL_OFFSET] == 0x14)) {
			/* It seems that the RICOH 
			   MP6200A CD/RW drive will 
			   report itself okay as a
			   slave when it is really a
			   master. So this check again
			   as a master device just to
			   make sure it doesn't report
			   itself okay as a master also
			*/
			if ((master_slave & ATA_ADDRESS_DEVHEAD_SLAVE) &&
			    (recheckAsMaster == FALSE)) {
				US_DEBUGP("   Identified ATAPI device as slave.  Rechecking again as master\n");
				recheckAsMaster = TRUE;
				master_slave = ATA_ADDRESS_DEVHEAD_STD;
			} else {
				US_DEBUGP("   Identified ATAPI device\n");
				info->DeviceHead = master_slave;
			      
				status = isd200_atapi_soft_reset(us);
				break;
			}
		} else {
			US_DEBUGP("   Not ATA, not ATAPI. Weird.\n");
		}

		/* check for timeout on this request */
		if (jiffies >= endTime) {
			if (!detect)
				US_DEBUGP("   BSY check timeout, just continue with next operation...\n");
			else
				US_DEBUGP("   Device detect timeout!\n");
			break;
		}
	}

	return status;
}

/**************************************************************************
 * isd200_manual_enum
 *                                                                         
 * Determines if the drive attached is an ATA or ATAPI and if it is a
 * master or slave.
 *
 * RETURNS:
 *    ISD status code
 */                                                                        
int isd200_manual_enum(struct us_data *us)
{
	struct isd200_info *info = (struct isd200_info *)us->extra;
	int retStatus = ISD200_GOOD;

	US_DEBUGP("Entering isd200_manual_enum\n");

	retStatus = isd200_read_config(us);
	if (retStatus == ISD200_GOOD) {
		int isslave;
		/* master or slave? */
		retStatus = isd200_try_enum( us, ATA_ADDRESS_DEVHEAD_STD, FALSE );
		if (retStatus == ISD200_GOOD)
			retStatus = isd200_try_enum( us, ATA_ADDRESS_DEVHEAD_SLAVE, FALSE );

		if (retStatus == ISD200_GOOD) {
			retStatus = isd200_srst(us);
			if (retStatus == ISD200_GOOD)
				/* ata or atapi? */
				retStatus = isd200_try_enum( us, ATA_ADDRESS_DEVHEAD_STD, TRUE );
		}

		isslave = (info->DeviceHead & ATA_ADDRESS_DEVHEAD_SLAVE) ? 1 : 0;
		if (info->ConfigData.MasterSlaveSelection != isslave) {
			US_DEBUGP("   Setting Master/Slave selection to %d\n", isslave);
			info->ConfigData.MasterSlaveSelection = isslave;
			retStatus = isd200_write_config(us);
		}
	}

	US_DEBUGP("Leaving isd200_manual_enum %08X\n", retStatus);
	return(retStatus);
}


/**************************************************************************
 * isd200_get_inquiry_data
 *
 * Get inquiry data
 *
 * RETURNS:
 *    ISD status code
 */
int isd200_get_inquiry_data( struct us_data *us )
{
	struct isd200_info *info = (struct isd200_info *)us->extra;
	int retStatus = ISD200_GOOD;

	US_DEBUGP("Entering isd200_get_inquiry_data\n");

	/* set default to Master */
	info->DeviceHead = ATA_ADDRESS_DEVHEAD_STD;

	/* attempt to manually enumerate this device */
	retStatus = isd200_manual_enum(us);
	if (retStatus == ISD200_GOOD) {
		int transferStatus;

		/* check for an ATA device */
		if (info->DeviceFlags & DF_ATA_DEVICE) {
			/* this must be an ATA device */
			/* perform an ATA Commmand Identify */
			transferStatus = isd200_action( us, ACTION_IDENTIFY,
							&info->drive, 
							sizeof(struct hd_driveid) );
			if (transferStatus != ISD200_TRANSPORT_GOOD) {
				/* Error issuing ATA Command Identify */
				US_DEBUGP("   Error issuing ATA Command Identify\n");
				retStatus = ISD200_ERROR;
			} else {
				/* ATA Command Identify successful */
				int i;

				US_DEBUGP("   Identify Data Structure:\n");
				US_DEBUGP("      config = 0x%x\n", info->drive.config);
				US_DEBUGP("      cyls = 0x%x\n", info->drive.cyls);
				US_DEBUGP("      heads = 0x%x\n", info->drive.heads);
				US_DEBUGP("      track_bytes = 0x%x\n", info->drive.track_bytes);
				US_DEBUGP("      sector_bytes = 0x%x\n", info->drive.sector_bytes);
				US_DEBUGP("      sectors = 0x%x\n", info->drive.sectors);
				US_DEBUGP("      serial_no[0] = 0x%x\n", info->drive.serial_no[0]);
				US_DEBUGP("      buf_type = 0x%x\n", info->drive.buf_type);
				US_DEBUGP("      buf_size = 0x%x\n", info->drive.buf_size);
				US_DEBUGP("      ecc_bytes = 0x%x\n", info->drive.ecc_bytes);
				US_DEBUGP("      fw_rev[0] = 0x%x\n", info->drive.fw_rev[0]);
				US_DEBUGP("      model[0] = 0x%x\n", info->drive.model[0]);
				US_DEBUGP("      max_multsect = 0x%x\n", info->drive.max_multsect);
				US_DEBUGP("      dword_io = 0x%x\n", info->drive.dword_io);
				US_DEBUGP("      capability = 0x%x\n", info->drive.capability);
				US_DEBUGP("      tPIO = 0x%x\n", info->drive.tPIO);
				US_DEBUGP("      tDMA = 0x%x\n", info->drive.tDMA);
				US_DEBUGP("      field_valid = 0x%x\n", info->drive.field_valid);
				US_DEBUGP("      cur_cyls = 0x%x\n", info->drive.cur_cyls);
				US_DEBUGP("      cur_heads = 0x%x\n", info->drive.cur_heads);
				US_DEBUGP("      cur_sectors = 0x%x\n", info->drive.cur_sectors);
				US_DEBUGP("      cur_capacity = 0x%x\n", (info->drive.cur_capacity1 << 16) + info->drive.cur_capacity0 );
				US_DEBUGP("      multsect = 0x%x\n", info->drive.multsect);
				US_DEBUGP("      lba_capacity = 0x%x\n", info->drive.lba_capacity);
				US_DEBUGP("      command_set_1 = 0x%x\n", info->drive.command_set_1);
				US_DEBUGP("      command_set_2 = 0x%x\n", info->drive.command_set_2);

				memset(&info->InquiryData, 0, sizeof(info->InquiryData));

				/* Standard IDE interface only supports disks */
				info->InquiryData.DeviceType = DIRECT_ACCESS_DEVICE;

				/* Fix-up the return data from an INQUIRY command to show 
				 * ANSI SCSI rev 2 so we don't confuse the SCSI layers above us
				 * in Linux.
				 */
				info->InquiryData.Versions = 0x2;

				/* The length must be at least 36 (5 + 31) */
				info->InquiryData.AdditionalLength = 0x1F;

				if (info->drive.command_set_1 & COMMANDSET_MEDIA_STATUS) {
					/* set the removable bit */
					info->InquiryData.RemovableMedia = 1;
					info->DeviceFlags |= DF_REMOVABLE_MEDIA;
				}

				/* Fill in vendor identification fields */
				for (i = 0; i < 20; i += 2) {
					info->InquiryData.VendorId[i] = 
						info->drive.model[i + 1];
					info->InquiryData.VendorId[i+1] = 
						info->drive.model[i];
				}

				/* Initialize unused portion of product id */
				for (i = 0; i < 4; i++) {
					info->InquiryData.ProductId[12+i] = ' ';
				}

				/* Move firmware revision from IDENTIFY data to */
				/* product revision in INQUIRY data             */
				for (i = 0; i < 4; i += 2) {
					info->InquiryData.ProductRevisionLevel[i] =
						info->drive.fw_rev[i+1];
					info->InquiryData.ProductRevisionLevel[i+1] =
						info->drive.fw_rev[i];
				}

				/* determine if it supports Media Status Notification */
				if (info->drive.command_set_2 & COMMANDSET_MEDIA_STATUS) {
					US_DEBUGP("   Device supports Media Status Notification\n");

					/* Indicate that it is enabled, even though it is not
					 * This allows the lock/unlock of the media to work
					 * correctly.
					 */
					info->DeviceFlags |= DF_MEDIA_STATUS_ENABLED;
				}
				else
					info->DeviceFlags &= ~DF_MEDIA_STATUS_ENABLED;

			}
		} else {
			/* 
			 * this must be an ATAPI device 
			 * use an ATAPI protocol (Transparent SCSI)
			 */
			us->protocol_name = "Transparent SCSI";
			us->proto_handler = usb_stor_transparent_scsi_command;

			US_DEBUGP("Protocol changed to: %s\n", us->protocol_name);
            
			/* Free driver structure */            
			if (us->extra != NULL) {
				kfree(us->extra);
				us->extra = NULL;
				us->extra_destructor = NULL;
			}
		}
        }

	US_DEBUGP("Leaving isd200_get_inquiry_data %08X\n", retStatus);

	return(retStatus);
}


/**************************************************************************
 * isd200_data_copy
 *                                                                         
 * Copy data into the srb request buffer.  Use scatter gather if required.
 *
 * RETURNS:
 *    void
 */                                                                        
void isd200_data_copy(Scsi_Cmnd *srb, char * src, int length)
{
	unsigned int len = length;
	struct scatterlist *sg;

	if (srb->use_sg) {
		int i;
		unsigned int total = 0;

		/* Add up the sizes of all the sg segments */
		sg = (struct scatterlist *) srb->request_buffer;
		for (i = 0; i < srb->use_sg; i++)
			total += sg[i].length;

		if (length > total)
			len = total;

		total = 0;

		/* Copy data into sg buffer(s) */
		for (i = 0; i < srb->use_sg; i++) {
			if ((len > total) && (len > 0)) {
				/* transfer the lesser of the next buffer or the
				 * remaining data */
				if (len - total >= sg[i].length) {
					memcpy(sg[i].address, src + total, sg[i].length);
					total += sg[i].length;
				} else {
					memcpy(sg[i].address, src + total, len - total);
					total = len;
				}
			} 
			else
				break;
		}
	} else	{
		/* Make sure length does not exceed buffer length */
		if (length > srb->request_bufflen)
			len = srb->request_bufflen;

		if (len > 0)
			memcpy(srb->request_buffer, src, len);
	}
}


/**************************************************************************
 * isd200_scsi_to_ata
 *                                                                         
 * Translate SCSI commands to ATA commands.
 *
 * RETURNS:
 *    TRUE if the command needs to be sent to the transport layer
 *    FALSE otherwise
 */                                                                        
int isd200_scsi_to_ata(Scsi_Cmnd *srb, struct us_data *us, 
		       union ata_cdb * ataCdb)
{
	struct isd200_info *info = (struct isd200_info *)us->extra;
	int sendToTransport = TRUE;
	unsigned char sectnum, head;
	unsigned short cylinder;
	unsigned long lba;
	unsigned long blockCount;
	unsigned char senseData[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	memset(ataCdb, 0, sizeof(union ata_cdb));

	/* SCSI Command */
	switch (srb->cmnd[0]) {
	case INQUIRY:
		US_DEBUGP("   ATA OUT - INQUIRY\n");

		if (srb->request_bufflen > sizeof(struct inquiry_data))
			srb->request_bufflen = sizeof(struct inquiry_data);

		/* copy InquiryData */
		isd200_data_copy(srb, (char *) &info->InquiryData, srb->request_bufflen);
		srb->result = GOOD;
		sendToTransport = FALSE;
		break;

	case MODE_SENSE:
		US_DEBUGP("   ATA OUT - SCSIOP_MODE_SENSE\n");

		/* Initialize the return buffer */
		isd200_data_copy(srb, (char *) &senseData, 8);

		if (info->DeviceFlags & DF_MEDIA_STATUS_ENABLED)
		{
			ataCdb->generic.SignatureByte0 = info->ConfigData.ATAMajorCommand;
			ataCdb->generic.SignatureByte1 = info->ConfigData.ATAMinorCommand;
			ataCdb->generic.TransferBlockSize = 1;
			ataCdb->write.SelectCommand = 1;
			ataCdb->write.CommandByte = ATA_COMMAND_GET_MEDIA_STATUS;
			srb->request_bufflen = 0;
		} else {
			US_DEBUGP("   Media Status not supported, just report okay\n");
			srb->result = GOOD;
			sendToTransport = FALSE;
		}
		break;

	case TEST_UNIT_READY:
		US_DEBUGP("   ATA OUT - SCSIOP_TEST_UNIT_READY\n");

		/* Initialize the return buffer */
		isd200_data_copy(srb, (char *) &senseData, 8);

		if (info->DeviceFlags & DF_MEDIA_STATUS_ENABLED)
		{
			ataCdb->generic.SignatureByte0 = info->ConfigData.ATAMajorCommand;
			ataCdb->generic.SignatureByte1 = info->ConfigData.ATAMinorCommand;
			ataCdb->generic.TransferBlockSize = 1;
			ataCdb->write.SelectCommand = 1;
			ataCdb->write.CommandByte = ATA_COMMAND_GET_MEDIA_STATUS;
			srb->request_bufflen = 0;
		} else {
			US_DEBUGP("   Media Status not supported, just report okay\n");
			srb->result = GOOD;
			sendToTransport = FALSE;
		}
		break;

	case READ_CAPACITY:
	{
		unsigned long capacity;
		struct read_capacity_data readCapacityData;

		US_DEBUGP("   ATA OUT - SCSIOP_READ_CAPACITY\n");

		if (info->drive.capability & CAPABILITY_LBA ) {
			capacity = info->drive.lba_capacity - 1;
		} else {
			capacity = (info->drive.heads *
				    info->drive.cyls *
				    info->drive.sectors) - 1;
		}
		readCapacityData.LogicalBlockAddress = cpu_to_be32(capacity);
		readCapacityData.BytesPerBlock = cpu_to_be32(0x200);

		if (srb->request_bufflen > sizeof(struct read_capacity_data))
			srb->request_bufflen = sizeof(struct read_capacity_data);

		isd200_data_copy(srb, (char *) &readCapacityData, srb->request_bufflen);
		srb->result = GOOD;
		sendToTransport = FALSE;
	}
	break;

	case READ_10:
		US_DEBUGP("   ATA OUT - SCSIOP_READ\n");

		lba = *(unsigned long *)&srb->cmnd[2]; 
		lba = cpu_to_be32(lba);
		blockCount = (unsigned long)srb->cmnd[7]<<8 | (unsigned long)srb->cmnd[8];

		if (info->drive.capability & CAPABILITY_LBA) {
			sectnum = (unsigned char)(lba);
			cylinder = (unsigned short)(lba>>8);
			head = ATA_ADDRESS_DEVHEAD_LBA_MODE | (unsigned char)(lba>>24 & 0x0F);
		} else {
			sectnum = (unsigned char)((lba % info->drive.sectors) + 1);
			cylinder = (unsigned short)(lba / (info->drive.sectors *
							   info->drive.heads));
			head = (unsigned char)((lba / info->drive.sectors) %
					       info->drive.heads);
		}
		ataCdb->generic.SignatureByte0 = info->ConfigData.ATAMajorCommand;
		ataCdb->generic.SignatureByte1 = info->ConfigData.ATAMinorCommand;
		ataCdb->generic.TransferBlockSize = 1;
		ataCdb->write.SelectSectorCount = 1;
		ataCdb->write.SectorCountByte = (unsigned char)blockCount;
		ataCdb->write.SelectSectorNumber = 1;
		ataCdb->write.SectorNumberByte = sectnum;
		ataCdb->write.SelectCylinderHigh = 1;
		ataCdb->write.CylinderHighByte = (unsigned char)(cylinder>>8);
		ataCdb->write.SelectCylinderLow = 1;
		ataCdb->write.CylinderLowByte = (unsigned char)cylinder;
		ataCdb->write.SelectDeviceHead = 1;
		ataCdb->write.DeviceHeadByte = (head | ATA_ADDRESS_DEVHEAD_STD);
		ataCdb->write.SelectCommand = 1;
		ataCdb->write.CommandByte = WIN_READ;
		break;

	case WRITE_10:
		US_DEBUGP("   ATA OUT - SCSIOP_WRITE\n");

		lba = *(unsigned long *)&srb->cmnd[2]; 
		lba = cpu_to_be32(lba);
		blockCount = (unsigned long)srb->cmnd[7]<<8 | (unsigned long)srb->cmnd[8];

		if (info->drive.capability & CAPABILITY_LBA) {
			sectnum = (unsigned char)(lba);
			cylinder = (unsigned short)(lba>>8);
			head = ATA_ADDRESS_DEVHEAD_LBA_MODE | (unsigned char)(lba>>24 & 0x0F);
		} else {
			sectnum = (unsigned char)((lba % info->drive.sectors) + 1);
			cylinder = (unsigned short)(lba / (info->drive.sectors * info->drive.heads));
			head = (unsigned char)((lba / info->drive.sectors) % info->drive.heads);
		}
		ataCdb->generic.SignatureByte0 = info->ConfigData.ATAMajorCommand;
		ataCdb->generic.SignatureByte1 = info->ConfigData.ATAMinorCommand;
		ataCdb->generic.TransferBlockSize = 1;
		ataCdb->write.SelectSectorCount = 1;
		ataCdb->write.SectorCountByte = (unsigned char)blockCount;
		ataCdb->write.SelectSectorNumber = 1;
		ataCdb->write.SectorNumberByte = sectnum;
		ataCdb->write.SelectCylinderHigh = 1;
		ataCdb->write.CylinderHighByte = (unsigned char)(cylinder>>8);
		ataCdb->write.SelectCylinderLow = 1;
		ataCdb->write.CylinderLowByte = (unsigned char)cylinder;
		ataCdb->write.SelectDeviceHead = 1;
		ataCdb->write.DeviceHeadByte = (head | ATA_ADDRESS_DEVHEAD_STD);
		ataCdb->write.SelectCommand = 1;
		ataCdb->write.CommandByte = WIN_WRITE;
		break;

	case ALLOW_MEDIUM_REMOVAL:
		US_DEBUGP("   ATA OUT - SCSIOP_MEDIUM_REMOVAL\n");

		if (info->DeviceFlags & DF_REMOVABLE_MEDIA) {
			US_DEBUGP("   srb->cmnd[4] = 0x%X\n", srb->cmnd[4]);
            
			ataCdb->generic.SignatureByte0 = info->ConfigData.ATAMajorCommand;
			ataCdb->generic.SignatureByte1 = info->ConfigData.ATAMinorCommand;
			ataCdb->generic.TransferBlockSize = 1;
			ataCdb->write.SelectCommand = 1;
			ataCdb->write.CommandByte = (srb->cmnd[4] & 0x1) ?
				WIN_DOORLOCK : WIN_DOORUNLOCK;
			srb->request_bufflen = 0;
		} else {
			US_DEBUGP("   Not removeable media, just report okay\n");
			srb->result = GOOD;
			sendToTransport = FALSE;
		}
		break;

	case START_STOP:    
		US_DEBUGP("   ATA OUT - SCSIOP_START_STOP_UNIT\n");
		US_DEBUGP("   srb->cmnd[4] = 0x%X\n", srb->cmnd[4]);

		/* Initialize the return buffer */
		isd200_data_copy(srb, (char *) &senseData, 8);

		if ((srb->cmnd[4] & 0x3) == 0x2) {
			US_DEBUGP("   Media Eject\n");
			ataCdb->generic.SignatureByte0 = info->ConfigData.ATAMajorCommand;
			ataCdb->generic.SignatureByte1 = info->ConfigData.ATAMinorCommand;
			ataCdb->generic.TransferBlockSize = 0;
			ataCdb->write.SelectCommand = 1;
			ataCdb->write.CommandByte = ATA_COMMAND_MEDIA_EJECT;
		} else if ((srb->cmnd[4] & 0x3) == 0x1) {
			US_DEBUGP("   Get Media Status\n");
			ataCdb->generic.SignatureByte0 = info->ConfigData.ATAMajorCommand;
			ataCdb->generic.SignatureByte1 = info->ConfigData.ATAMinorCommand;
			ataCdb->generic.TransferBlockSize = 1;
			ataCdb->write.SelectCommand = 1;
			ataCdb->write.CommandByte = ATA_COMMAND_GET_MEDIA_STATUS;
			srb->request_bufflen = 0;
		} else {
			US_DEBUGP("   Nothing to do, just report okay\n");
			srb->result = GOOD;
			sendToTransport = FALSE;
		}
		break;

	default:
		US_DEBUGP("Unsupported SCSI command - 0x%X\n", srb->cmnd[0]);
		srb->result = DID_ERROR << 16;
		sendToTransport = FALSE;
		break;
	}

	return(sendToTransport);
}


/**************************************************************************
 * isd200_init_info
 *                                                                         
 * Allocates (if necessary) and initializes the driver structure.
 *
 * RETURNS:
 *    ISD status code
 */                                                                        
int isd200_init_info(struct us_data *us)
{
	int retStatus = ISD200_GOOD;

	if (!us->extra) {
		us->extra = (void *) kmalloc(sizeof(struct isd200_info), GFP_KERNEL);
		if (!us->extra) {
			US_DEBUGP("ERROR - kmalloc failure\n");
			retStatus = ISD200_ERROR;
		}
        }

	if (retStatus == ISD200_GOOD) {
		memset(us->extra, 0, sizeof(struct isd200_info));
        }

	return(retStatus);
}

/**************************************************************************
 * Initialization for the ISD200 
 */

int isd200_Initialization(struct us_data *us)
{
	US_DEBUGP("ISD200 Initialization...\n");

	/* Initialize ISD200 info struct */

	if (isd200_init_info(us) == ISD200_ERROR) {
		US_DEBUGP("ERROR Initializing ISD200 Info struct\n");
        } else {
		/* Get device specific data */

		if (isd200_get_inquiry_data(us) != ISD200_GOOD)
			US_DEBUGP("ISD200 Initialization Failure\n");
		else
			US_DEBUGP("ISD200 Initialization complete\n");
        }

	return 0;
}


/**************************************************************************
 * Protocol and Transport for the ISD200 ASIC
 *
 * This protocol and transport are for ATA devices connected to an ISD200
 * ASIC.  An ATAPI device that is conected as a slave device will be
 * detected in the driver initialization function and the protocol will
 * be changed to an ATAPI protocol (Transparent SCSI).
 *
 */

void isd200_ata_command(Scsi_Cmnd *srb, struct us_data *us)
{
	int sendToTransport = TRUE;
	union ata_cdb ataCdb;

	/* Make sure driver was initialized */

	if (us->extra == NULL)
		US_DEBUGP("ERROR Driver not initialized\n");

	/* Convert command */
	sendToTransport = isd200_scsi_to_ata(srb, us, &ataCdb);

	/* send the command to the transport layer */
	if (sendToTransport)
		isd200_invoke_transport(us, srb, &ataCdb);
}
