/*
 * linux/drivers/block/ide-tape.c	Version 1.0 - ALPHA	Dec  3, 1995
 *
 * Copyright (C) 1995 Gadi Oxman <tgud@tochnapc2.technion.ac.il>
 *
 * This driver was constructed as a student project in the software laboratory
 * of the faculty of electrical engineering in the Technion - Israel's
 * Institute Of Technology, with the guide of Avner Lottem and Dr. Ilana David.
 *
 * It is hereby placed under the terms of the GNU general public license.
 * (See linux/COPYING).
 */
 
/*
 * IDE ATAPI streaming tape driver.
 *
 * This driver is a part of the Linux ide driver and works in co-operation
 * with linux/drivers/block/ide.c.
 *
 * This driver provides both a block device and a character device interface to
 * the tape. The driver, in co-operation with ide.c, basically traverses the
 * request-list for the block device interface. The character device interface,
 * on the other hand, creates new requests, adds them to the request-list
 * of the block device, and waits for their completion.
 *
 * The block device major and minor numbers are determined from the
 * tape relative position in the ide interfaces, as explained in ide.c.
 *
 * The character device interface consists of two devices:
 *
 * ht0		major=37,minor=0	first IDE tape, rewind on close.
 * nht0		major=37,minor=128	first IDE tape, no rewind on close.
 *
 * Run /usr/src/linux/drivers/block/MAKEDEV.ide to create the above entries.
 * We currently support only one ide tape drive.
 *
 * Although we do support requests which originate from the buffer cache to
 * some extent, it is recommended to use the character device interface when
 * performing a long read or write operation (relative to the amount of free
 * memory in your system). Otherwise, free memory will be used to cache tape
 * blocks, those cached blocks won't be used, Linux's responsiveness will
 * suffer as we start to swap.
 *
 * The general magnetic tape commands compatible interface, as defined by
 * include/linux/mtio.h, is accessible through the character device.
 * Our own ide-tape ioctl's can can be issued to either the block device or
 * the character device.
 *
 * Opening the block device interface will be refused by default.
 *
 * Testing was done with a 2 GB CONNER CTMA 4000 IDE ATAPI Streaming Tape Drive.
 *
 * Ver 0.1   Nov  1 95   Pre-working code :-)
 * Ver 0.2   Nov 23 95   A short backup (few megabytes) and restore procedure
 *                        was successful ! (Using tar cvf ... on the block
 *                        device interface).
 *                       A longer backup resulted in major swapping, bad
 *                        overall Linux performance and eventually failed as
 *                        we received non serial read-ahead requests from the
 *                        buffer cache.
 * Ver 0.3   Nov 28 95   Long backups are now possible, thanks to the
 *                        character device interface. Linux's responsiveness
 *                        and performance doesn't seem to be much affected
 *                        from the background backup procedure.
 *                       Some general mtio.h magnetic tape operations are
 *                        now supported by our character device. As a result,
 *                        popular tape utilities are starting to work with
 *                        ide tapes :-)
 *                       The following configurations were tested:
 *                       	1. An IDE ATAPI TAPE shares the same interface
 *                       	   and irq with an IDE ATAPI CDROM.
 *                        	2. An IDE ATAPI TAPE shares the same interface
 *                          	   and irq with a normal IDE disk.
 *                        Both configurations seemed to work just fine !
 *                        However, to be on the safe side, it is meanwhile
 *                        recommended to give the IDE TAPE its own interface
 *                        and irq.
 *                       The one thing which needs to be done here is to
 *                        add a "request postpone" feature to ide.c,
 *                        so that we won't have to wait for the tape to finish
 *                        performing a long media access (DSC) request (such
 *                        as a rewind) before we can access the other device
 *                        on the same interface. This effect doesn't disturb
 *                        normal operation most of the time because read/write
 *                        requests are relatively fast, and once we are
 *                        performing one tape r/w request, a lot of requests
 *                        from the other device can be queued and ide.c will
 *			  service all of them after this single tape request.
 * Ver 1.0   ???         Integrated into Linux 1.3.??? development tree.
 *                       On each read / write request, we now ask the drive
 *                        if we can transfer a constant number of bytes
 *                        (a parameter of the drive) only to its buffers,
 *                        without causing actual media access. If we can't,
 *                        we just wait until we can by polling the DSC bit.
 *                        This ensures that while we are not transferring
 *                        more bytes than the constant reffered to above, the
 *                        interrupt latency will not become too high and
 *                        we won't cause an interrupt timeout, as happened
 *                        occasionally in the previous version.
 *                       While polling for DSC, the current request is
 *                        postponed and ide.c is free to handle requests from
 *                        the other device. This is handled transparently to
 *                        ide.c. The hwgroup locking method which was used
 *                        in the previous version was removed.
 *                       Use of new general features which are provided by
 *                        ide.c for use with atapi devices.
 *                        (Programming done by Mark Lord)
 *                       Few potential bug fixes (Again, suggested by Mark)
 *                       Single character device data transfers are now
 *                        not limited in size, as they were before.
 *                       We are asking the tape about its recommended
 *                        transfer unit and send a larger data transfer
 *                        as several transfers of the above size.
 *                        For best results, use an integral number of this
 *                        basic unit (which is shown during driver
 *                        initialization). I will soon add an ioctl to get
 *                        this important parameter.
 *                       Our data transfer buffer is allocated on startup,
 *                        rather than before each data transfer. This should
 *                        ensure that we will indeed have a data buffer.
 *
 * We are currently in an *alpha* stage. The driver is not complete and not
 * much tested. I would strongly suggest to:
 *
 *	1. Connect the tape to a separate interface and irq.
 *	2. Be truly prepared for a kernel crash and the resulting data loss.
 *	3. Don't rely too much on the resulting backups.
 *
 * Other than that, enjoy !
 *
 * Here are some words from the first releases of hd.c, which are quoted
 * in ide.c and apply here as well:
 *
 * | Special care is recommended.  Have Fun!
 *
 */

#include <linux/hdreg.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/malloc.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/segment.h>
#include <asm/io.h>

#define _IDE_TAPE_C			/* For ide_end_request in blk.h */

/*
 *	Main Linux ide driver include file
 *
 *	Automatically includes our first include file - ide-tape1.h.
 */
 
#include "ide.h"		

/*
 *	Supported ATAPI tape drives packet commands
 */

#define	IDETAPE_TEST_UNIT_READY_CMD	0x00
#define	IDETAPE_REWIND_CMD		0x01
#define	IDETAPE_REQUEST_SENSE_CMD	0x03
#define	IDETAPE_READ_CMD		0x08
#define	IDETAPE_WRITE_CMD		0x0a
#define	IDETAPE_WRITE_FILEMARK_CMD	0x10
#define	IDETAPE_SPACE_CMD		0x11
#define	IDETAPE_INQUIRY_CMD		0x12
#define	IDETAPE_ERASE_CMD		0x19
#define	IDETAPE_MODE_SENSE_CMD		0x1a
#define	IDETAPE_LOCATE_CMD		0x2b
#define	IDETAPE_READ_POSITION_CMD	0x34

/*
 *	Some defines for the SPACE command
 *
 *	(The code field in the SPACE packet command).
 */
 
#define	IDETAPE_SPACE_OVER_FILEMARK	1
#define	IDETAPE_SPACE_TO_EOD		3

/*
 *	Our ioctls - We will use 0x034n and 0x035n
 *
 *	Nothing special meanwhile.
 *	mtio.h MTIOCTOP compatible commands are supported on the character
 *	device interface.
 */

#define IDETAPE_INQUIRY_IOCTL		0x0341
#define	IDETAPE_LOCATE_IOCTL		0x0342

#define IDETAPE_RESET_IOCTL		0x0350

/*
 *	Special requests for our block device strategy routine.
 *
 *	In order to service a character device command, we add special
 *	requests to the tail of our block device request queue and wait
 *	for their completion.
 *
 */

/*
 * 	IDETAPE_PACKET_COMMAND_REQUEST_TYPE1 is used to queue a packet command
 *	in the request queue. We will wait for DSC before issuing the command
 *	if it is still not set. In that case, we will temporary replace the
 *	cmd field to type 2 and restore it back to type 1 when we receive DSC
 *	and can start with sending the command.
 */
 
#define	IDETAPE_PACKET_COMMAND_REQUEST_TYPE1	90
#define	IDETAPE_PACKET_COMMAND_REQUEST_TYPE2	91

/*
 *	IDETAPE_READ_REQUEST and IDETAPE_WRITE_REQUEST are used by our
 *	character device interface to request read/write operations from
 *	our block device interface.
 *
 *	In case a read or write request was requested by the buffer cache
 *	and not by our character device interface, the cmd field in the
 *	request will contain READ and WRITE instead.
 *
 *	We handle both cases in a similar way. The main difference is that
 *	in our own requests, buffer head is NULL and idetape_end_request
 *	will update the errors field if the request was not completed.
 */

#define	IDETAPE_READ_REQUEST			92
#define	IDETAPE_WRITE_REQUEST			93

/*
 *	We are now able to postpone an idetape request in the stage
 *	where it is polling for DSC and service requests from the other
 *	ide device meanwhile.
 */

#define	IDETAPE_RQ_POSTPONED		0x1234

/*
 *	ATAPI Task File Registers (Re-definition of the ATA Task File
 *	Registers for an ATAPI packet command).
 * 	From Table 3-2 of QIC-157C.
 */

/* Read Access */

#define	IDETAPE_DATA_OFFSET		(0)
#define IDETAPE_ERROR_OFFSET		(1)
#define	IDETAPE_IREASON_OFFSET		(2)
#define IDETAPE_RESERVED3_OFFSET	(3)
#define IDETAPE_BCOUNTL_OFFSET		(4)
#define	IDETAPE_BCOUNTH_OFFSET		(5)
#define IDETAPE_DRIVESEL_OFFSET		(6)
#define	IDETAPE_STATUS_OFFSET		(7)

#define	IDETAPE_DATA_REG		(HWIF(drive)->io_base+IDETAPE_DATA_OFFSET)
#define IDETAPE_ERROR_REG		(HWIF(drive)->io_base+IDETAPE_ERROR_OFFSET)
#define	IDETAPE_IREASON_REG		(HWIF(drive)->io_base+IDETAPE_IREASON_OFFSET)
#define IDETAPE_RESERVED3_REG		(HWIF(drive)->io_base+IDETAPE_RESERVED3_OFFSET)
#define IDETAPE_BCOUNTL_REG		(HWIF(drive)->io_base+IDETAPE_BCOUNTL_OFFSET)
#define	IDETAPE_BCOUNTH_REG		(HWIF(drive)->io_base+IDETAPE_BCOUNTH_OFFSET)
#define IDETAPE_DRIVESEL_REG		(HWIF(drive)->io_base+IDETAPE_DRIVESEL_OFFSET)
#define	IDETAPE_STATUS_REG		(HWIF(drive)->io_base+IDETAPE_STATUS_OFFSET)

/* Write Access */

#define	IDETAPE_FEATURES_OFFSET		(1)
#define IDETAPE_ATACOMMAND_OFFSET	(7)

#define IDETAPE_FEATURES_REG		(HWIF(drive)->io_base+IDETAPE_FEATURES_OFFSET)
#define IDETAPE_ATACOMMAND_REG		(HWIF(drive)->io_base+IDETAPE_ATACOMMAND_OFFSET)
#define IDETAPE_CONTROL_REG		(HWIF(drive)->ctl_port)


/*
 *	Structure of the various task file registers
 */

/*
 *	The ATAPI Status Register.
 */
 
typedef union {
	unsigned all			:8;
	struct {
		unsigned check		:1;	/* Error occured */
		unsigned idx		:1;	/* Reserved */
		unsigned corr		:1;	/* Correctable error occured */
		unsigned drq		:1;	/* Data is request by the device */
		unsigned dsc		:1;	/* Set when a media access command is finished */
						/* Reads / Writes are NOT media access commands */
		unsigned reserved5	:1;	/* Reserved */
		unsigned drdy		:1;	/* Ignored for ATAPI commands */
						/* (The device is ready to accept ATA command) */
		unsigned bsy		:1;	/* The device has access to the command block */
	} b;
} idetape_status_reg_t;

/*
 *	The ATAPI error register.
 */
 
typedef union {
	unsigned all			:8;
	struct {
		unsigned ili		:1;	/* Illegal Length Indication */
		unsigned eom		:1;	/* End Of Media Detected */
		unsigned abrt		:1;	/* Aborted command - As defined by ATA */
		unsigned mcr		:1;	/* Media Change Requested - As defined by ATA */
		unsigned sense_key	:4;	/* Sense key of the last failed packet command */
	} b;
} idetape_error_reg_t;

/*
 *	ATAPI Feature Register
 */
 
typedef union {
	unsigned all			:8;
	struct {
		unsigned dma		:1;	/* Using DMA of PIO */
		unsigned reserved321	:3;	/* Reserved */
		unsigned reserved654	:3;	/* Reserved (Tag Type) */
		unsigned reserved7	:1;	/* Reserved */
	} b;
} idetape_feature_reg_t;

/*
 *	ATAPI Byte Count Register.
 */
 
typedef union {
	unsigned all			:16;
	struct {
		unsigned low		:8;	/* LSB */
		unsigned high		:8;	/* MSB */
	} b;
} idetape_bcount_reg_t;

/*
 *	ATAPI Interrupt Reason Register.
 */
 
typedef union {
	unsigned all			:8;
	struct {
		unsigned cod		:1;	/* Information transferred is command (1) or data (0) */
		unsigned io		:1;	/* The device requests us to read (1) or write (0) */
		unsigned reserved	:6;	/* Reserved */
	} b;
} idetape_ireason_reg_t;

/*
 *	ATAPI Drive Select Register
 */
 
typedef union {	
	unsigned all			:8;
	struct {
		unsigned sam_lun	:4;	/* Should be zero with ATAPI (not used) */
		unsigned drv		:1;	/* The responding drive will be drive 0 (0) or drive 1 (1) */
		unsigned one5		:1;	/* Should be set to 1 */
		unsigned reserved6	:1;	/* Reserved */
		unsigned one7		:1;	/* Should be set to 1 */
	} b;
} idetape_drivesel_reg_t;

/*
 *	ATAPI Device Control Register
 */
 
typedef union {			
	unsigned all			:8;
	struct {
		unsigned zero0		:1;	/* Should be set to zero */
		unsigned nien		:1;	/* Device interrupt is disabled (1) or enabled (0) */
		unsigned srst		:1;	/* ATA software reset. ATAPI devices should use the new ATAPI srst. */
		unsigned one3		:1;	/* Should be set to 1 */
		unsigned reserved4567	:4;	/* Reserved */
	} b;
} idetape_control_reg_t;

/*
 *	idetape_chrdev_t provides the link between out character device
 *	interface and our block device interface and the corresponding
 *	ide_drive_t structure.
 *
 *	We currently support only one tape drive.
 * 
 */
 
typedef struct {
	ide_drive_t *drive;
	int major,minor;
	char name[4];
} idetape_chrdev_t;

/*
 *	The following is used to format the general configuration word of
 *	the ATAPI IDENTIFY DEVICE command.
 */

struct idetape_id_gcw {	

	unsigned packet_size	:2;	/* Packet Size */
	unsigned reserved2	:1;	/* Reserved */
	unsigned reserved3	:1;	/* Reserved */
	unsigned reserved4	:1;	/* Reserved */
	unsigned drq_type	:2;	/* Command packet DRQ type */
	unsigned removable	:1;	/* Removable media */
	unsigned device_type	:5;	/* Device type */
	unsigned reserved13	:1;	/* Reserved */
	unsigned protocol	:2;	/* Protocol type */
};

/*
 *	INQUIRY packet command - Data Format (From Table 6-8 of QIC-157C)
 */
 
typedef struct {
	unsigned device_type	:5;	/* Peripheral Device Type */
	unsigned reserved0_765	:3;	/* Peripheral Qualifier - Reserved */
	unsigned reserved1_6t0	:7;	/* Reserved */
	unsigned rmb		:1;	/* Removable Medium Bit */
	unsigned ansi_version	:3;	/* ANSI Version */
	unsigned ecma_version	:3;	/* ECMA Version */
	unsigned iso_version	:2;	/* ISO Version */
	unsigned response_format :4;	/* Response Data Format */
	unsigned reserved3_45	:2;	/* Reserved */
	unsigned reserved3_6	:1;	/* TrmIOP - Reserved */
	unsigned reserved3_7	:1;	/* AENC - Reserved */
	byte additional_length;		/* Additional Length (total_length-4) */
	byte reserved_5;		/* Reserved */
	byte reserved_6;		/* Reserved */
	unsigned reserved7_0	:1;	/* SftRe - Reserved */
	unsigned reserved7_1	:1;	/* CmdQue - Reserved */
	unsigned reserved7_2	:1;	/* Reserved */
	unsigned reserved7_3	:1;	/* Linked - Reserved */
	unsigned reserved7_4	:1;	/* Sync - Reserved */
	unsigned reserved7_5	:1;	/* WBus16 - Reserved */
	unsigned reserved7_6	:1;	/* WBus32 - Reserved */
	unsigned reserved7_7	:1;	/* RelAdr - Reserved */
	byte vendor_id [8];		/* Vendor Identification */
	byte product_id [16];		/* Product Identification */
	byte revision_level [4];	/* Revision Level */
	byte vendor_specific [20];	/* Vendor Specific - Optional */
	byte reserved56t95 [40];	/* Reserved - Optional */
	
					/* Additional information may be returned */
} idetape_inquiry_result_t;

/*
 *	READ POSITION packet command - Data Format (From Table 6-57)
 */
 
typedef struct {
	unsigned reserved0_10	:2;	/* Reserved */
	unsigned bpu		:1;	/* Block Position Unknown */	
	unsigned reserved0_543	:3;	/* Reserved */
	unsigned eop		:1;	/* End Of Partition */
	unsigned bop		:1;	/* Begining Of Partition */
	byte partition_num;		/* Partition Number */
	byte reserved_2;		/* Reserved */
	byte reserved_3;		/* Reserved */
	unsigned long first_block;	/* First Block Location */
	unsigned long last_block;	/* Last Block Location (Optional) */
	byte reserved_12;		/* Reserved */
	byte blocks_in_buffer_2;	/* Blocks In Buffer - MSB (Optional) */
	byte blocks_in_buffer_1;
	byte blocks_in_buffer_0;	/* Blocks In Buffer - LSB (Optional) */
	unsigned long bytes_in_buffer;	/* Bytes In Buffer (Optional) */
} idetape_read_position_result_t;

/*
 *	REQUEST SENSE packet command result - Data Format.
 */

typedef struct {
	unsigned error_code	:7;	/* Current of deferred errors */
	unsigned valid		:1;	/* The information field conforms to QIC-157C */
	byte reserved_1;		/* Segment Number - Reserved */
	unsigned sense_key	:4;	/* Sense Key */
	unsigned reserved2_4	:1;	/* Reserved */
	unsigned ili		:1;	/* Incorrect Length Indicator */
	unsigned eom		:1;	/* End Of Medium */
	unsigned filemark	:1;	/* Filemark */
	unsigned long information;	/* Information - Command specific */
	byte asl;			/* Additional sense length (n-7) */
	unsigned long command_specific; /* Additional command specific information */
	byte asc;			/* Additional Sense Code */
	byte ascq;			/* Additional Sense Code Qualifier */
	byte replaceable_unit_code;	/* Field Replaceable Unit Code */
	unsigned sk_specific1 	:7;	/* Sense Key Specific */
	unsigned sksv		:1;	/* Sense Key Specific informatio is valid */
	byte sk_specific2;		/* Sense Key Specific */
	byte sk_specific3;		/* Sense Key Specific */
} idetape_request_sense_result_t;

/*
 *	Follows structures which are realted to the SELECT SENSE / MODE SENSE
 *	packet commands. Those packet commands are still not supported
 *	by ide-tape.
 */

#define	IDETAPE_CAPABILITIES_PAGE	0x2a

/*
 *	Mode Parameter Header for the MODE SENSE packet command
 */

typedef struct {
	byte mode_data_length;		/* The length of the following data that is */
					/* available to be transferred */
	byte medium_type;		/* Medium Type */
	byte dsp;			/* Device Specific Parameter */
	byte bdl;			/* Block Descriptor Length */
} idetape_mode_parameter_header_t;

/*
 *	Mode Parameter Block Descriptor the MODE SENSE packet command
 *
 *	Support for block descriptors is optional.
 */

typedef struct {
	byte density_code;		/* Medium density code */
	byte blocks1;			/* Number of blocks - MSB */
	byte blocks2;			/* Number of blocks - Middle byte */
	byte blocks3;			/* Number of blocks - LSB */
	byte reserved4;			/* Reserved */
	byte length1;			/* Block Length - MSB */
	byte length2;			/* Block Length - Middle byte */
	byte length3;			/* Block Length - LSB */
} idetape_parameter_block_descriptor_t;

/*
 *	The Data Compression Page, as returned by the MODE SENSE packet command.
 */
 
typedef struct {
	unsigned page_code	:6;	/* Page Code - Should be 0xf */
	unsigned reserved	:1;	/* Reserved */
	unsigned ps		:1;
	byte page_length;		/* Page Length - Should be 14 */
	unsigned reserved2	:6;	/* Reserved */
	unsigned dcc		:1;	/* Data Compression Capable */
	unsigned dce		:1;	/* Data Compression Enable */
	unsigned reserved3	:5;	/* Reserved */
	unsigned red		:2;	/* Report Exception on Decompression */
	unsigned dde		:1;	/* Data Decompression Enable */
	unsigned long ca;		/* Compression Algorithm */
	unsigned long da;		/* Decompression Algorithm */
	byte reserved_12;		/* Reserved */
	byte reserved_13;		/* Reserved */
	byte reserved_14;		/* Reserved */
	byte reserved_15;		/* Reserved */
} idetape_data_compression_page_t;

/*
 *	The Medium Partition Page, as returned by the MODE SENSE packet command.
 */

typedef struct {
	unsigned page_code	:6;	/* Page Code - Should be 0x11 */
	unsigned reserved1_6	:1;	/* Reserved */
	unsigned ps		:1;
	byte page_length;		/* Page Length - Should be 6 */
	byte map;			/* Maximum Additional Partitions - Should be 0 */
	byte apd;			/* Additional Partitions Defined - Should be 0 */
	unsigned reserved4_012	:3;	/* Reserved */
	unsigned psum		:2;	/* Should be 0 */
	unsigned idp		:1;	/* Should be 0 */
	unsigned sdp		:1;	/* Should be 0 */
	unsigned fdp		:1;	/* Fixed Data Partitions */
	byte mfr;			/* Medium Format Recognition */
	byte reserved6;			/* Reserved */
	byte reserved7;			/* Reserved */
} idetape_medium_partition_page_t;

/*
 *	Prototypes of various functions in ide-tape.c
 *
 *	The following functions are called from ide.c, and their prototypes
 *	are available in ide.h:
 *
 *		idetape_identify_device
 *		idetape_setup
 *		idetape_blkdev_ioctl
 *		idetape_do_request
 *		idetape_blkdev_open
 *		idetape_blkdev_release
 *		idetape_register_chrdev (void);
 */

/*
 *	The following functions are used to transfer data from / to the
 *	tape's data register.
 */
 
void idetape_input_data (ide_drive_t *drive,void *buffer, unsigned long bcount);
void idetape_output_data (ide_drive_t *drive,void *buffer, unsigned long bcount);
void idetape_discard_data (ide_drive_t *drive, unsigned long bcount);


/*
 *	Packet command related functions.
 */
 
void idetape_issue_packet_command  (ide_drive_t *drive,idetape_packet_command_t *pc,ide_handler_t *handler);
void idetape_pc_intr (ide_drive_t *drive);

/*
 *	DSC handling functions.
 */
 
void idetape_postpone_request (ide_drive_t *drive);
void idetape_poll_for_dsc (unsigned long data);
void idetape_put_back_postponed_request (ide_drive_t *drive);
void idetape_media_access_finished (ide_drive_t *drive);

/*
 *	Some more packet command related functions.
 */
 
void idetape_pc_callback (ide_drive_t *drive);
void idetape_retry_pc (ide_drive_t *drive);
void idetape_zero_packet_command (idetape_packet_command_t *pc);
void idetape_queue_pc_head (ide_drive_t *drive,idetape_packet_command_t *pc,struct request *rq);

idetape_packet_command_t *idetape_next_pc_storage (ide_drive_t *drive);
struct request *idetape_next_rq_storage (ide_drive_t *drive);

void idetape_end_request (byte uptodate, ide_hwgroup_t *hwgroup);

/*
 *	Various packet commands
 */
 
void idetape_create_inquiry_cmd (idetape_packet_command_t *pc);
void idetape_inquiry_callback (ide_drive_t *drive);
void idetape_create_locate_cmd (idetape_packet_command_t *pc,unsigned long block,byte partition);
void idetape_create_rewind_cmd (idetape_packet_command_t *pc);
void idetape_create_write_filemark_cmd (idetape_packet_command_t *pc,int write_filemark);
void idetape_create_space_cmd (idetape_packet_command_t *pc,long count,byte cmd);
void idetape_create_erase_cmd (idetape_packet_command_t *pc);
void idetape_create_test_unit_ready_cmd (idetape_packet_command_t *pc);
void idetape_create_read_position_cmd (idetape_packet_command_t *pc);
void idetape_read_position_callback (ide_drive_t *drive);
void idetape_create_read_cmd (idetape_packet_command_t *pc,unsigned long length);
void idetape_read_callback (ide_drive_t *drive);
void idetape_create_write_cmd (idetape_packet_command_t *pc,unsigned long length);
void idetape_write_callback (ide_drive_t *drive);
void idetape_create_request_sense_cmd (idetape_packet_command_t *pc);
void idetape_create_mode_sense_cmd (idetape_packet_command_t *pc,byte page_code);
void idetape_request_sense_callback (ide_drive_t *drive);

void idetape_display_inquiry_result (byte *buffer);
void idetape_analyze_error (ide_drive_t *drive,idetape_request_sense_result_t *result);

/*
 *	Character device callback functions.
 *
 *	We currently support:
 *
 *		OPEN, RELEASE, READ, WRITE and IOCTL.
 */

int idetape_chrdev_read (struct inode *inode, struct file *file, char *buf, int count);
int idetape_chrdev_read_remainder (struct inode *inode, struct file *file, char *buf, int count);
int idetape_chrdev_write (struct inode *inode, struct file *file, const char *buf, int count);
int idetape_chrdev_write_remainder (struct inode *inode, struct file *file, const char *buf, int count);
int idetape_chrdev_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
int idetape_chrdev_open (struct inode *inode, struct file *file);
void idetape_chrdev_release (struct inode *inode,struct file *file);

/*
 *	idetape_mtioctop implements general magnetic tape io control
 *	commands, as defined in include/linux/mtio.h. Those commands are
 *	accessed through the character device interface, using the MTIOCTOP
 *	ioctl.
 */
 
int idetape_mtioctop (ide_drive_t *drive,short mt_op,int mt_count);
int idetape_queue_rw_tail (ide_drive_t *drive,int cmd,int blocks,char *buffer);
int idetape_queue_pc_tail (ide_drive_t *drive,idetape_packet_command_t *pc);

void idetape_fake_read (ide_drive_t *drive);
int idetape_position_tape (ide_drive_t *drive,unsigned long block);
int idetape_rewind_tape (ide_drive_t *drive);

/*
 *	Used to get device information
 */

void idetape_get_mode_sense_results (ide_drive_t *drive);

/*
 *	General utility functions
 */
 
void idetape_fixstring (byte *s, const int bytecount, const int byteswap);
unsigned long idetape_swap_long (unsigned long temp);
unsigned short idetape_swap_short (unsigned short temp);

/*
 *	For general magnetic tape device compatibility.
 */
 
#include <linux/mtio.h>

/*
 *	Global variables
 *
 *	The variables below are used for the character device interface.
 *
 *	Additional state variables are defined in our ide_drive_t structure.
 */
 
idetape_chrdev_t idetape_chrdev;		/* Character device interface information */
byte idetape_drive_already_found=0;		/* 1 when the above data structure is initialized */

/*
 *	Our character device supporting functions, passed to register_chrdev.
 */
 
static struct file_operations idetape_fops = {
	NULL,			/* lseek - default */
	idetape_chrdev_read,	/* read  */
	idetape_chrdev_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	idetape_chrdev_ioctl,	/* ioctl */
	NULL,			/* mmap */
	idetape_chrdev_open,	/* open */
	idetape_chrdev_release,	/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};


/*
 *	idetape_identify_device is called by do_identify in ide.c during
 *	the device probing stage to check the contents of the ATAPI IDENTIFY
 *	command results, in case the device type is tape. We return:
 *
 *	1	If the tape can be supported by us, based on the information
 *		we have so far.
 *
 *	0 	If this tape driver is not currently supported by us.
 *
 *	In case we decide to support the tape, we store the current drive
 *	pointer in our character device global variables, so that we can
 *	pass between both interfaces.
 */
 
int idetape_identify_device (ide_drive_t *drive,struct hd_driveid *id)

{
	struct idetape_id_gcw gcw;
	unsigned short *ptr;
	int support=1;
#if IDETAPE_DEBUG
	unsigned short mask,i;
#endif /* IDETAPE_DEBUG */
		
	ptr=(unsigned short *) &gcw;
	*ptr=id->config;

#if IDETAPE_DEBUG
	printk ("Dumping ATAPI Identify Device tape parameters\n");
	
	printk ("Protocol Type: ");
	switch (gcw.protocol) {
		case 0: case 1: printk ("ATA\n");break;
		case 2:	printk ("ATAPI\n");break;
		case 3: printk ("Reserved (Unknown to ide-tape)\n");break;
	}
	
	printk ("Device Type: %x - ",gcw.device_type);	
	switch (gcw.device_type) {
		case 0: printk ("Direct-access Device\n");break;
		case 1: printk ("Streaming Tape Device\n");break;
		case 2: case 3: case 4: printk ("Reserved\n");break;
		case 5: printk ("CD-ROM Device\n");break;
		case 6: printk ("Reserved\n");
		case 7: printk ("Optical memory Device\n");break;
		case 0x1f: printk ("Unknown or no Device type\n");break;
		default: printk ("Reserved\n");
	}
	printk ("Removable: %s",gcw.removable ? "Yes\n":"No\n");	
		
	printk ("Command Packet DRQ Type: ");
	switch (gcw.drq_type) {
		case 0: printk ("Microprocessor DRQ\n");break;
		case 1: printk ("Interrupt DRQ\n");break;
		case 2: printk ("Accelerated DRQ\n");break;
		case 3: printk ("Reserved\n");break;
	}
	
	printk ("Command Packet Size: ");
	switch (gcw.packet_size) {
		case 0: printk ("12 bytes\n");break;
		case 1: printk ("16 bytes\n");break;
		default: printk ("Reserved\n");break;
	}
	printk ("Model: %s\n",id->model);
	printk ("Firmware Revision: %s\n",id->fw_rev);
	printk ("Serial Number: %s\n",id->serial_no);
	printk ("Write buffer size: %d bytes\n",id->buf_size*512);
	printk ("DMA: %s",id->capability & 0x01 ? "Yes\n":"No\n");
	printk ("LBA: %s",id->capability & 0x02 ? "Yes\n":"No\n");
	printk ("IORDY can be disabled: %s",id->capability & 0x04 ? "Yes\n":"No\n");
	printk ("IORDY supported: %s",id->capability & 0x08 ? "Yes\n":"Unknown\n");
	printk ("PIO Cycle Timing Category: %d\n",id->tPIO);
	printk ("DMA Cycle Timing Category: %d\n",id->tDMA);
	printk ("Single Word DMA supported modes: ");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_1word & mask)
			printk ("%d ",i);
		if (id->dma_1word & (mask << 8))
			printk ("(active) ");
	}
	printk ("\n");

	printk ("Multi Word DMA supported modes: ");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_mword & mask)
			printk ("%d ",i);
		if (id->dma_mword & (mask << 8))
			printk ("(active) ");
	}
	printk ("\n");

	if (id->field_valid & 0x0002) {
		printk ("Enhanced PIO Modes: %s\n",id->eide_pio_modes & 1 ? "Mode 3":"None");
		printk ("Minimum Multi-word DMA cycle per word: ");
		if (id->eide_dma_min == 0)
			printk ("Not supported\n");
		else
			printk ("%d ns\n",id->eide_dma_min);

		printk ("Manafactuer\'s Recommended Multi-word cycle: ");
		if (id->eide_dma_time == 0)
			printk ("Not supported\n");
		else
			printk ("%d ns\n",id->eide_dma_time);

		printk ("Minimum PIO cycle without IORDY: ");
		if (id->eide_pio == 0)
			printk ("Not supported\n");
		else
			printk ("%d ns\n",id->eide_pio);

		printk ("Minimum PIO cycle with IORDY: ");
		if (id->eide_pio_iordy == 0)
			printk ("Not supported\n");
		else
			printk ("%d ns\n",id->eide_pio_iordy);
		
	}

	else {
		printk ("According to the device, fields 64-70 are not valid.\n");
	}
#endif /* IDETAPE_DEBUG */

	/* Check that we can support this device */

	if (gcw.protocol !=2 ) {
		printk ("ide-tape: Protocol is not ATAPI\n");support=0;
	}

	if (gcw.device_type != 1) {
		printk ("ide-tape: Device type is not set to tape\n");support=0;
	}

	if (!gcw.removable) {
		printk ("ide-tape: The removable flag is not set\n");support=0;
	}

	if (gcw.drq_type != 2) {
		printk ("ide-tape: Sorry, DRQ types other than Accelerated DRQ\n");
		printk ("ide-tape: are still not supproted by the driver\n");support=0;
	}

	if (gcw.packet_size != 0) {
		printk ("ide-tape: Packet size is not 12 bytes long\n");
		if (gcw.packet_size == 1)
			printk ("ide-tape: Sorry, padding to 16 bytes is still not supported\n");
		support=0;			
	}

	if (idetape_drive_already_found) {
		printk ("ide-tape: Sorry, only one ide tape drive is supported by the driver\n");
		support=0;
	}
	else {
		idetape_drive_already_found=1;
		idetape_chrdev.drive=drive;
		idetape_chrdev.major=IDETAPE_MAJOR;
		idetape_chrdev.minor=0;
		idetape_chrdev.name[0]='h';
		idetape_chrdev.name[1]='t';
		idetape_chrdev.name[2]='0';
		idetape_chrdev.name[3]=0;
	}

	return (support);		/* In case support=0, we will not install the driver */
}

/*
 *	idetape_register_chrdev calls register_chrdev to register our character
 *	device interface. The connection to the ide_drive_t structure, which
 *	is used by the entire ide driver is provided by our global variable
 *	idetape_chrdev.drive, which was initialized earlier, during the device
 *	probing stage.
 */
 
void idetape_register_chrdev (void)

{
	int major,minor;
	ide_drive_t *drive;

	if (!idetape_drive_already_found)
		return;

	drive=idetape_chrdev.drive;
	major=idetape_chrdev.major;
	minor=idetape_chrdev.minor;
	
	if (register_chrdev (major,idetape_chrdev.name,&idetape_fops)) {
		printk ("Unable to register character device interface !\n");
		/* ??? */
	}
	else {
		printk ("ide-tape: %s <-> %s : Character device interface on major = %d\n",
			drive->name,idetape_chrdev.name,major);
	}
}

/*
 *	idetape_setup is called from the ide driver in the partition table
 *	identification stage, to:
 *
 *		1.	Initialize our various state variables.
 *		2.	Ask the tape for its capabilities.
 *		3.	Allocate a buffer which will be used for data
 *			transfer. The buffer size is chosen based on
 *			the recommendation which we received in step (2).
 *
 *	Note that at this point ide.c already assigned us an irq, so that
 *	we can queue requests here and wait for their completion.
 */
 
void idetape_setup (ide_drive_t *drive)

{
	int buffer_size;
	idetape_tape_t *tape=&(drive->tape);

#if IDETAPE_DEBUG
	printk ("ide-tape: Reached idetape_setup\n");
#endif /* IDETAPE_DEBUG */	
	
	drive->ready_stat = 0;			/* With an ATAPI device, we can issue packet commands */
						/* regardless of the state of DRDY */
	tape->block_address=0;			
	tape->block_address_valid=0;
	tape->locate_to=0;
	tape->locate_retries=0;
	tape->pc_stack_index=0;
	tape->failed_pc=NULL;
	tape->postponed_rq=NULL;
	tape->last_written_valid=0;
	tape->busy=0;
	
	idetape_get_mode_sense_results (drive);

	buffer_size=tape->capabilities.ctl*tape->tape_block_size;
	tape->data_buffer=kmalloc (buffer_size,GFP_KERNEL);
	if (tape->data_buffer == NULL) {
		printk ("ide-tape: FATAL - Can not allocate %d bytes for data transfer buffer\n",buffer_size);
		printk ("ide-tape: Aborting character device installation\n");
		idetape_drive_already_found=0;
		unregister_chrdev (idetape_chrdev.major,idetape_chrdev.name);
		return;
	}
	printk ("ide-tape: Speed - %d KBps. Recommended transfer unit - %d bytes.\n",tape->capabilities.speed,buffer_size);
	return;
}

/*
 *	idetape_get_mode_sense_results asks the tape about its various
 *	parameters. In particular, we will adjust our data transfer buffer
 *	size to the recommended value as returned by the tape.
 */

void idetape_get_mode_sense_results (ide_drive_t *drive)

{
	int retval;
	idetape_tape_t *tape=&(drive->tape);
	idetape_mode_parameter_header_t *header;
	idetape_capabilities_page_t *capabilities;
	idetape_packet_command_t pc;
	
	idetape_create_mode_sense_cmd (&pc,IDETAPE_CAPABILITIES_PAGE);
	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;
	retval=idetape_queue_pc_tail (drive,&pc);

	header=(idetape_mode_parameter_header_t *) pc.buffer;	
	capabilities=(idetape_capabilities_page_t *) (pc.buffer+sizeof (idetape_mode_parameter_header_t));

	capabilities->max_speed=idetape_swap_short (capabilities->max_speed);
	capabilities->ctl=idetape_swap_short (capabilities->ctl);
	capabilities->speed=idetape_swap_short (capabilities->speed);
	capabilities->buffer_size=idetape_swap_short (capabilities->buffer_size);

	tape->capabilities=*capabilities;		/* Save us a copy */
	tape->tape_block_size=capabilities->blk512 ? 512:1024;

	if (retval) {
		printk ("ide-tape: Can't get tape parameters\n");
		printk ("ide-tape: Assuming some default parameters\n");
		tape->tape_block_size=512;
		tape->capabilities.ctl=26*1024;
		return;
	}

#if IDETAPE_DEBUG
	printk ("Dumping the results of the MODE SENSE packet command\n");
	printk ("Mode Parameter Header:\n");
	printk ("Mode Data Length - %d\n",header->mode_data_length);
	printk ("Medium Type - %d\n",header->medium_type);
	printk ("Device Specific Parameter - %d\n",header->dsp);
	printk ("Block Descriptor Length - %d\n",header->bdl);
	
	printk ("Capabilities and Mechanical Status Page:\n");
	printk ("Page code - %d\n",capabilities->page_code);
	printk ("Page length - %d\n",capabilities->page_length);
	printk ("Read only - %s\n",capabilities->ro ? "Yes":"No");
	printk ("Supports reverse space - %s\n",capabilities->sprev ? "Yes":"No");
	printk ("Supports erase initiated formatting - %s\n",capabilities->efmt ? "Yes":"No");
	printk ("Supports QFA two Partition format - %s\n",capabilities->qfa ? "Yes":"No");
	printk ("Supports locking the medium - %s\n",capabilities->lock ? "Yes":"No");
	printk ("The volume is currently locked - %s\n",capabilities->locked ? "Yes":"No");
	printk ("The device defaults in the prevent state - %s\n",capabilities->prevent ? "Yes":"No");
	printk ("Supports ejecting the medium - %s\n",capabilities->eject ? "Yes":"No");
	printk ("Supports error correction - %s\n",capabilities->ecc ? "Yes":"No");
	printk ("Supports data compression - %s\n",capabilities->cmprs ? "Yes":"No");
	printk ("Supports 512 bytes block size - %s\n",capabilities->blk512 ? "Yes":"No");
	printk ("Supports 1024 bytes block size - %s\n",capabilities->blk1024 ? "Yes":"No");
	printk ("Restricted byte count for PIO transfers - %s\n",capabilities->slowb ? "Yes":"No");
	printk ("Maximum supported speed in KBps - %d\n",capabilities->max_speed);
	printk ("Continuous transfer limits in blocks - %d\n",capabilities->ctl);
	printk ("Current speed in KBps - %d\n",capabilities->speed);	
	printk ("Buffer size - %d\n",capabilities->buffer_size*512);
#endif /* IDETAPE_DEBUG */
}

/*
 *	Packet Command Interface
 *
 *	The current Packet Command is available in tape->pc, and will not
 *	change until we finish handling it. Each packet command is associated
 *	with a callback function that will be called when the command is
 *	finished.
 *
 *	The handling will be done in three stages:
 *
 *	1.	idetape_issue_packet_command will send the packet command to the
 *		drive, and will set the interrupt handler to idetape_pc_intr.
 *
 *	2.	On each interrupt, idetape_pc_intr will be called. This step
 *		will be repeated until the device signals us that no more
 *		interrupts will be issued.
 *
 *	3.	ATAPI Tape media access commands have immediate status with a
 *		delayed process. In case of a successfull initiation of a
 *		media access packet command, the DSC bit will be set when the
 *		actual execution of the command is finished. 
 *		Since the tape drive will not issue an interrupt, we have to
 *		poll for this event. In this case, we define the request as
 *		"low priority request" by setting rq_status to
 *		IDETAPE_RQ_POSTPONED, 	set a timer to poll for DSC and exit
 *		the driver.
 *
 *		ide.c will then give higher priority to requests which
 *		originate from the other device, until will change rq_status
 *		to RQ_ACTIVE.
 *
 *	4.	When the packet command is finished, it will be checked for errors.
 *
 *	5.	In case an error was found, we queue a request sense packet command
 *		in front of the request queue and retry the operation up to
 *		IDETAPE_MAX_PC_RETRIES times.
 *
 *	6.	In case no error was found, or we decided to give up and not
 *		to retry again, the callback function will be called and then
 *		we will handle the next request.
 *
 */

void idetape_issue_packet_command  (ide_drive_t *drive,idetape_packet_command_t *pc,ide_handler_t *handler)

{
	idetape_tape_t *tape;
	idetape_bcount_reg_t bcount;
        idetape_ireason_reg_t ireason;

	tape=&(drive->tape);
	        
#ifdef IDETAPE_DEBUG
	if (tape->pc->c[0] == IDETAPE_REQUEST_SENSE_CMD && pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
		printk ("ide-tape: ide-tape.c bug - Two request sense in serial were issued\n");
		/* ??? Need to rethink about that */		
	}
#endif /* IDETAPE_DEBUG */

	if (tape->failed_pc == NULL && pc->c[0] != IDETAPE_REQUEST_SENSE_CMD)
		tape->failed_pc=pc;
	tape->pc=pc;							/* Set the current packet command */

	if (pc->retries > IDETAPE_MAX_PC_RETRIES) {
		printk ("ide-tape: %s: I/O error, ",drive->name);
		printk ("pc = %x, key = %x, asc = %x, ascq = %x\n",pc->c[0],tape->sense_key,tape->asc,tape->ascq);
		printk ("ide-tape: Maximum retries reached - Giving up\n");
		pc->error=1;					/* Giving up */
		pc->active=0;
		tape->failed_pc=NULL;
#if IDETAPE_DEBUG
		if (pc->callback==NULL)
			printk ("ide-tape: ide-tape bug - Callback function not set !\n");
		else
#endif /* IDETAPE_DEBUG */
			(*pc->callback)(drive);
		return;
	}

#if IDETAPE_DEBUG
	printk ("Retry number - %d\n",pc->retries);
#endif /* IDETAPE_DEBUG */

	pc->retries++;

/*
 *	We no longer call ide_wait_stat to wait for the drive to be ready,
 *	as ide.c already does this for us in do_request.
 */
 
	pc->actually_transferred=0;					/* We haven't transferred any data yet */
	pc->active=1;							/* Packet command started */
	bcount.all=pc->request_transfer;				/* Request to transfer the entire buffer at once */

									/* Initialize the task file registers */
	OUT_BYTE (0,IDETAPE_FEATURES_REG);				/* Use PIO data transger, No DMA */
	OUT_BYTE (bcount.b.high,IDETAPE_BCOUNTH_REG);
	OUT_BYTE (bcount.b.low,IDETAPE_BCOUNTL_REG);
	OUT_BYTE (drive->select.all,IDETAPE_DRIVESEL_REG);
	
	ide_set_handler (drive,handler,WAIT_CMD);			/* Set the interrupt routine */
	OUT_BYTE (WIN_PACKETCMD,IDETAPE_ATACOMMAND_REG);		/* Issue the packet command */
	if (ide_wait_stat (drive,DRQ_STAT,BUSY_STAT,WAIT_READY)) { 	/* Wait for DRQ to be ready - Assuming Accelerated DRQ */	
		/*
		 *	We currently only support tape drives which report
		 *	accelerated DRQ assertion. For this case, specs
		 *	allow up to 50us. We really shouldn't get here.
		 *
		 *	??? Still needs to think what to do if we reach
		 *	here anyway.
		 */
		 
		 printk ("ide-tape: Strange, packet command initiated yet DRQ isn't asserted\n");
		 return;
	}
	
	ireason.all=IN_BYTE (IDETAPE_IREASON_REG);
	if (!ireason.b.cod || ireason.b.io) {
		printk ("ide-tape: (IO,CoD) != (0,1) while issuing a packet command\n");
		/* ??? */
	}
		
	ide_output_data (drive,pc->c,12/4);			/* Send the actual packet */
}

/*
 *	idetape_pc_intr is the usual interrupt handler which will be called
 *	during a packet command. We will transfer some of the data (as
 *	requested by the drive) and will re-point interrupt handler to us.
 *	When data transfer is finished, we will act according to the
 *	algorithm described before idetape_issue_packet_command.
 *
 */
 

void idetape_pc_intr (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	idetape_status_reg_t status;
	idetape_bcount_reg_t bcount;
        idetape_ireason_reg_t ireason;
	idetape_packet_command_t *pc;
	
	unsigned long temp;

	tape=&(drive->tape);
	
	status.all=IN_BYTE (IDETAPE_STATUS_REG);		/* Clear the interrupt */

#if IDETAPE_DEBUG
	printk ("ide-tape: Reached idetape_pc_intr interrupt handler\n");
#endif /* IDETAPE_DEBUG */	

	pc=tape->pc;						/* Current packet command */
		
	if (!status.b.drq) {					/* No more interrupts */
#if IDETAPE_DEBUG
		printk ("Packet command completed\n");
		printk ("Total bytes transferred: %lu\n",pc->actually_transferred);
#endif /* IDETAPE_DEBUG */
		if (status.b.check) {					/* Error detected */
#if IDETAPE_DEBUG
	/*
	 *	Without debugging, we only log an error if we decided to
	 *	give up retrying.
	 */
			printk ("ide-tape: %s: I/O error, ",drive->name);
#endif /* IDETAPE_DEBUG */
			idetape_retry_pc (drive);			/* Retry operation */
			return;
		}
		pc->error=0;
		if (pc->wait_for_dsc && !status.b.dsc) {				/* Media access command */
			tape->dsc_polling_frequency=IDETAPE_DSC_FAST_MEDIA_ACCESS_FREQUENCY;
			idetape_postpone_request (drive);		/* Allow ide.c to handle other requests */
			return;
		}
		pc->active=0;
		if (tape->failed_pc == pc)
			tape->failed_pc=NULL;
#if IDETAPE_DEBUG
		if (pc->callback==NULL)			
			printk ("ide-tape: ide-tape bug - Callback function not set !\n");
		else
#endif IDETAPE_DEBUG
			(*pc->callback)(drive);			/* Command finished - Call the callback function */
		return;
	}

	bcount.b.high=IN_BYTE (IDETAPE_BCOUNTH_REG);			/* Get the number of bytes to transfer */
	bcount.b.low=IN_BYTE (IDETAPE_BCOUNTL_REG);			/* on this interrupt */
	ireason.all=IN_BYTE (IDETAPE_IREASON_REG);			/* Read the interrupt reason register */

	if (ireason.b.cod) {
		printk ("ide-tape: CoD != 0 in idetape_pc_intr\n");
		/* ??? */
	}
	if (ireason.b.io != !(pc->writing)) {			/* Hopefully, we will never get here */
		printk ("ide-tape: We wanted to %s, ",pc->writing ? "Write":"Read");
		printk ("but the tape wants us to %s !\n",ireason.b.io ? "Read":"Write");
		/* ??? */		
	}
	
	if (!pc->writing) {					/* Reading - Check that we have enough space */
		temp=(unsigned long) pc->actually_transferred + bcount.all;
		if ( temp > pc->request_transfer) {
			printk ("ide-tape: The tape wants to send us more data than requested - ");
			if (temp > pc->buffer_size) {
				printk ("Discarding data\n");
				idetape_discard_data (drive,bcount.all);
				ide_set_handler (drive,&idetape_pc_intr,WAIT_CMD);
				return;
			}
			else
				printk ("Allowing transfer\n");
		}
	}
#if IDETAPE_DEBUG	
	if (bcount.all && !pc->buffer) {	
		printk ("ide-tape: ide-tape.c bug - Buffer not set in idetape_pc_intr. Discarding data.\n");
		
		if (!pc->writing) {
			printk ("ide-tape: Discarding data\n");
			idetape_discard_data (drive,bcount.all);
			ide_set_handler (drive,&idetape_pc_intr,WAIT_CMD);
			return;
		}
		else {	/* ??? */
		}
	}
#endif /* IDETAPE_DEBUG */
	if (pc->writing)
		idetape_output_data (drive,pc->current_position,bcount.all);	/* Write the current buffer */
	else
		idetape_input_data (drive,pc->current_position,bcount.all);	/* Read the current buffer */
#if IDETAPE_DEBUG
	printk ("ide-tape: %s %d bytes\n",pc->writing ? "Wrote":"Received",bcount.all);
#endif /* IDETAPE_DEBUG */
	pc->actually_transferred+=bcount.all;					/* Update the current position */
	pc->current_position+=bcount.all;

	ide_set_handler (drive,&idetape_pc_intr,WAIT_CMD);		/* And set the interrupt handler again */
}

/*
 *	idetape_postpone_request postpones the current request so that
 *	ide.c will be able to service requests from another device on
 *	the same hwgroup while we are polling for DSC.
 */

void idetape_postpone_request (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	unsigned long flags;
	struct request *rq;
		
	tape=&(drive->tape);

#if IDETAPE_DEBUG
	printk ("Reached idetape_postpone_request\n");
	if (tape->postponed_rq != NULL)
		printk ("ide-tape.c bug - postponed_rq not NULL in idetape_postpone_request\n");
#endif /* IDETAPE_DEBUG */

	tape->dsc_count=0;
	tape->dsc_timer.expires=jiffies + tape->dsc_polling_frequency;	/* Set timer to poll for */
	tape->dsc_timeout=jiffies+IDETAPE_DSC_TIMEOUT;			/* actual completion */
	tape->dsc_timer.data=(unsigned long) drive;
	tape->dsc_timer.function=&idetape_poll_for_dsc;
	init_timer (&(tape->dsc_timer));

	/*
	 * Remove current request from the request queue:
	 */
	save_flags(flags);						/* Let ide.c handle another request */
	cli();
	tape->postponed_rq = rq = HWGROUP(drive)->rq;
	rq->rq_status = IDETAPE_RQ_POSTPONED;	
	blk_dev[MAJOR(rq->rq_dev)].current_request = rq->next;
	HWGROUP(drive)->rq = NULL;
	restore_flags(flags);

	tape->dsc_polling_start=jiffies;	
	add_timer(&(tape->dsc_timer));		/* Activate the polling timer */
}


/*
 *	idetape_poll_for_dsc gets invoked by a timer (which was set
 *	by idetape_postpone_request) to poll for the DSC bit
 *	in the status register. When the DSC bit is set, or a timeout is
 *	reached, we put back the postponed request in front of the request
 *	queue.
 */
 
void idetape_poll_for_dsc (unsigned long data)

{
	ide_drive_t *drive;
	idetape_tape_t *tape;
	
	idetape_status_reg_t status;
	idetape_packet_command_t *pc;

	drive=(ide_drive_t *) data;
	tape=&(drive->tape);
	pc=tape->pc;
	
#if IDETAPE_DEBUG
	printk ("%s: idetape_poll_for_dsc called\n",drive->name);
#endif /* IDETAPE_DEBUG */	

	status.all=IN_BYTE (IDETAPE_STATUS_REG);

	if (status.b.dsc)
		tape->dsc_count++;
	else {
		if (tape->dsc_count)
			printk ("ide-tape: DSC fluctuation detected - Restarting DSC count\n");
		tape->dsc_count=0;
	}

	if (tape->dsc_count == IDETAPE_DSC_COUNT) {		/* DSC received */
		tape->dsc_received=1;
		del_timer (&(tape->dsc_timer));			/* Stop polling and put back the postponed */
		idetape_put_back_postponed_request (drive);	/* request in the request queue */
		return;
	}
		
	if (jiffies > tape->dsc_timeout) 	{ /* Timeout */
		tape->dsc_received=0;
		del_timer (&(tape->dsc_timer));
		/* ??? */
		idetape_put_back_postponed_request (drive);
		return;
	}
	
	/* Poll again */

	if (jiffies - tape->dsc_polling_start > IDETAPE_FAST_SLOW_THRESHOLD)
		tape->dsc_timer.expires = jiffies + IDETAPE_DSC_SLOW_MEDIA_ACCESS_FREQUENCY;
	else
		tape->dsc_timer.expires = jiffies + tape->dsc_polling_frequency;
/*	init_timer (&(tape->dsc_timer)); */
	add_timer(&(tape->dsc_timer));
	return;
}

/*
 *	idetape_put_back_postponed_request gets called by
 *	idetape_poll_for_dsc when we decided to stop polling - Either
 *	becase we received DSC or because we decided to give up and
 *	stop waiting.
 */

void idetape_put_back_postponed_request (ide_drive_t *drive)

{
	idetape_tape_t *tape = &(drive->tape);

#if IDETAPE_DEBUG
	printk ("ide-tape: Putting back postponed request\n");
#endif /* IDETAPE_DEBUG */

	(void) ide_do_drive_cmd (drive, tape->postponed_rq, ide_next);

	/*
	 * 	Note that the procedure done here is differnet than the method
	 *	we are using in idetape_queue_pc_head - There we are putting
	 *	request(s) before our currently called request.
	 *
	 *	Here, on the other hand, HWGROUP(drive)->rq is not our
	 *	request but rather a request to another device. Therefore,
	 *	we will let it finish and only then service our postponed
	 *	request --> We don't touch HWGROUP(drive)->rq.
	 */
}

void idetape_media_access_finished (ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);
	idetape_status_reg_t status;
	idetape_packet_command_t *pc;

	pc=tape->pc;
	
	status.all=IN_BYTE (IDETAPE_STATUS_REG);

	if (tape->dsc_received) {
#if IDETAPE_DEBUG
		printk ("DSC received\n");
#endif /* IDETAPE_DEBUG */
		pc->active=0;
		if (status.b.check) {					/* Error detected */
			printk ("ide-tape: %s: I/O error, ",drive->name);
			idetape_retry_pc (drive);			/* Retry operation */
			return;
		}
		pc->error=0;
		if (tape->failed_pc == pc)
			tape->failed_pc=NULL;
#if IDETAPE_DEBUG
		if (pc->callback==NULL)
			printk ("ide-tape: ide-tape bug - Callback function not set !\n");
		else
#endif /* IDETAPE_DEBUG */
			(*pc->callback)(drive);

		return;
	}
	else {
		pc->active=0;
		printk ("ide-tape: %s: DSC timeout.\n",drive->name);
		/* ??? */
		pc->error=1;
		tape->failed_pc=NULL;
#if IDETAPE_DEBUG
		if (pc->callback==NULL)
			printk ("ide-tape: ide-tape bug - Callback function not set !\n");
		else
#endif /* IDETAPE_DEBUG */
			(*pc->callback)(drive);
		return;
	}
}


/*
 *	idetape_retry_pc is called when an error was detected during the
 *	last packet command. We queue a request sense packet command in
 *	the head of the request list.
 */
 
void idetape_retry_pc (ide_drive_t *drive)

{
	idetape_packet_command_t *pc;
	struct request *new_rq;

	idetape_error_reg_t error;
	error.all=IN_BYTE (IDETAPE_ERROR_REG);
	pc=idetape_next_pc_storage (drive);
	new_rq=idetape_next_rq_storage (drive);
	idetape_create_request_sense_cmd (pc); 
	pc->buffer=pc->temp_buffer;
	pc->buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc->current_position=pc->temp_buffer;
	idetape_queue_pc_head (drive,pc,new_rq);
}

/*
 *	General packet command callback function.
 */
 
void idetape_pc_callback (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	struct request *rq;
	
	tape=&(drive->tape);
	rq=HWGROUP(drive)->rq;
	
#if IDETAPE_DEBUG
	printk ("ide-tape: Reached idetape_pc_callback\n");
#endif /* IDETAPE_DEBUG */
	if (!tape->pc->error) {
#if IDETAPE_DEBUG
		printk ("Request completed\n");
#endif /* IDETAPE_DEBUG */
		idetape_end_request (1,HWGROUP (drive));
	}
	else {
		printk ("Aborting request\n");
		idetape_end_request (0,HWGROUP (drive));
	}
	return;
}


void idetape_read_callback (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	struct request *rq;

	tape=&(drive->tape);	
	rq=HWGROUP(drive)->rq;
#if IDETAPE_DEBUG	
	printk ("ide-tape: Reached idetape_read_callback\n");
#endif /* IDETAPE_DEBUG */
	tape->block_address+=tape->pc->actually_transferred/512;
	if (!tape->pc->error) {
#if IDETAPE_DEBUG
		printk ("Request completed\n");
#endif /* IDETAPE_DEBUG */
		rq->sector+=rq->current_nr_sectors;
		rq->nr_sectors-=rq->current_nr_sectors;
		rq->current_nr_sectors=0;
		idetape_end_request (1,HWGROUP (drive));
	}
	else {
		printk ("Aborting request\n");
		idetape_end_request (0,HWGROUP (drive));
	}
	return;
}

void idetape_fake_read (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	struct request *rq;
	unsigned long i;

	tape=&(drive->tape);	
	rq=HWGROUP(drive)->rq;
#if IDETAPE_DEBUG	
	printk ("ide-tape: Reached idetape_fake_read\n");
#endif /* IDETAPE_DEBUG */

#if IDETAPE_DEBUG
	printk ("Request completed\n");
#endif /* IDETAPE_DEBUG */
	
	for (i=0;i<rq->current_nr_sectors*512;i++)
		rq->buffer [i]=0;

	tape->block_address+=rq->current_nr_sectors;
	rq->sector+=rq->current_nr_sectors;
	rq->nr_sectors-=rq->current_nr_sectors;
	rq->current_nr_sectors=0;
	idetape_end_request (1,HWGROUP (drive));
}

void idetape_write_callback (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	struct request *rq;
	
	tape=&(drive->tape);
	rq=HWGROUP(drive)->rq;
#if IDETAPE_DEBUG	
	printk ("ide-tape: Reached idetape_write_callback\n");
#endif /* IDETAPE_DEBUG */
	tape->block_address+=tape->pc->actually_transferred/512;
	if (!tape->pc->error) {
#if IDETAPE_DEBUG
		printk ("Request completed\n");
#endif /* IDETAPE_DEBUG */
		rq->sector+=rq->current_nr_sectors;
		rq->nr_sectors-=rq->current_nr_sectors;
		rq->current_nr_sectors=0;
		idetape_end_request (1,HWGROUP (drive));
	}
	else {
		printk ("Aborting request\n");
		idetape_end_request (0,HWGROUP (drive));
	}
	return;
}

void idetape_inquiry_callback (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	
	tape=&(drive->tape);
	
	idetape_display_inquiry_result (tape->pc->buffer);
	idetape_pc_callback (drive);
	return;
}

/*
 *	idetape_input_data is called to read data from the tape's data
 *	register. We basically let ide_input_data do the job, but we also
 *	take care about the remaining bytes which can not be transferred
 *	in 32-bit data transfers.
 */
 
void idetape_input_data (ide_drive_t *drive,void *buffer, unsigned long bcount)

{
	unsigned long wcount;
	
	wcount=bcount >> 2;
	bcount -= 4*wcount;
	
	if (wcount)
		ide_input_data (drive,buffer,wcount);
	
	if (bcount) {
		((byte *)buffer) += 4*wcount;
		insb (IDETAPE_DATA_REG,buffer,bcount);
	}
}

/*
 *	idetape_output_data is used to write data to the tape.
 */
 
void idetape_output_data (ide_drive_t *drive,void *buffer, unsigned long bcount)

{
	unsigned long wcount;
	
	wcount=bcount >> 2;
	bcount -= 4*wcount;
	
	if (wcount)
		ide_output_data (drive,buffer,wcount);
	
	if (bcount) {
		((byte *)buffer) += 4*wcount;
		outsb (IDETAPE_DATA_REG,buffer,bcount);
	}
}

/*
 *	Too bad. The drive wants to send us data which we are not ready to accept.
 *	Just throw it away.
 */
 
void idetape_discard_data (ide_drive_t *drive, unsigned long bcount)

{
	unsigned long i;
	
	for (i=0;i<bcount;i++)
		IN_BYTE (IDETAPE_DATA_REG);
}

/*
 *	Issue an INQUIRY packet command.
 */
 
void idetape_create_inquiry_cmd (idetape_packet_command_t *pc)

{
#if IDETAPE_DEBUG
	printk ("ide-tape: Creating INQUIRY packet command\n");
#endif /* IDETAPE_DEBUG */	
	pc->request_transfer=36;
	pc->callback=&idetape_inquiry_callback;
	pc->writing=0;
	
	idetape_zero_packet_command (pc);		
	pc->c[0]=IDETAPE_INQUIRY_CMD;
	pc->c[4]=255;
}

/*
 *	Format the INQUIRY command results.
 */
 
void idetape_display_inquiry_result (byte *buffer)

{
	idetape_inquiry_result_t *result;

	result=(idetape_inquiry_result_t *) buffer;
	idetape_fixstring (result->vendor_id,8,0);
	idetape_fixstring (result->product_id,16,0);
	idetape_fixstring (result->revision_level,4,0);

	if (result->response_format != 2) {
		printk ("The INQUIRY Data Format is unknown to us !\n");
		printk ("Assuming QIC-157C format.\n");
	}

#if IDETAPE_DEBUG
	printk ("Dumping INQUIRY command results:\n");
	printk ("Response Data Format: %d - ",result->response_format);
	switch (result->response_format) {
		case 2:
			printk ("As specified in QIC-157 Revision C\n");
			break;
		default:
			printk ("Unknown\n");
			break;
	}
	
	printk ("Device Type: %x - ",result->device_type);	
	switch (result->device_type) {
		case 0: printk ("Direct-access Device\n");break;
		case 1: printk ("Streaming Tape Device\n");break;
		case 2: case 3: case 4: printk ("Reserved\n");break;
		case 5: printk ("CD-ROM Device\n");break;
		case 6: printk ("Reserved\n");
		case 7: printk ("Optical memory Device\n");break;
		case 0x1f: printk ("Unknown or no Device type\n");break;
		default: printk ("Reserved\n");
	}
	
	printk ("Removable Medium: %s",result->rmb ? "Yes\n":"No\n");

	printk ("ANSI Version: %d - ",result->ansi_version);
	switch (result->ansi_version) {
		case 2:
			printk ("QIC-157 Revision C\n");
			break;
		default:
			printk ("Unknown\n");
			break;
	}

	printk ("ECMA Version: ");
	if (result->ecma_version)
		printk ("%d\n",result->ecma_version);
	else
		printk ("Not supported\n");

	printk ("ISO Version: ");
	if (result->iso_version)
		printk ("%d\n",result->iso_version);
	else
		printk ("Not supported\n");

	printk ("Additional Length: %d\n",result->additional_length);
	printk ("Vendor Identification: %s\n",result->vendor_id);
	printk ("Product Identification: %s\n",result->product_id);
	printk ("Product Revision Level: %s\n",result->revision_level);
#endif /* IDETAPE_DEBUG */

	if (result->device_type != 1)
		printk ("Device type is not set to tape\n");

	if (!result->rmb)
		printk ("The removable flag is not set\n");

	if (result->ansi_version != 2) {
		printk ("The Ansi Version is unknown to us !\n");
		printk ("Assuming compliance with QIC-157C specification.\n");
	}
}

void idetape_create_request_sense_cmd (idetape_packet_command_t *pc)

{
#if IDETAPE_DEBUG
	printk ("ide-tape: Creating REQUEST SENSE packet command\n");
#endif /* IDETAPE_DEBUG */	
	pc->request_transfer=18;
	pc->callback=&idetape_request_sense_callback;
	pc->writing=0;
	
	idetape_zero_packet_command (pc);	
	pc->c[0]=IDETAPE_REQUEST_SENSE_CMD;
	pc->c[4]=255;
}

void idetape_request_sense_callback (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	struct request *rq;

	tape=&(drive->tape);	
	rq=HWGROUP(drive)->rq;
	
#if IDETAPE_DEBUG
	printk ("ide-tape: Reached idetape_request_sense_callback\n");
#endif /* IDETAPE_DEBUG */
	if (!tape->pc->error) {
#if IDETAPE_DEBUG
		printk ("Request completed\n");
#endif /* IDETAPE_DEBUG */
		idetape_analyze_error (drive,(idetape_request_sense_result_t *) tape->pc->buffer);
		idetape_end_request (1,HWGROUP (drive));
	}
	else {
		printk ("Error in REQUEST SENSE itself - Aborting request!\n");
		idetape_end_request (0,HWGROUP (drive));
	}
	return;
}

/*
 *	idetape_analyze_error is called on each failed packet command retry
 *	to analyze the request sense. We currently do not utilize this
 *	information.
 */
 
void idetape_analyze_error (ide_drive_t *drive,idetape_request_sense_result_t *result)

{
	idetape_tape_t *tape;
	
	tape=&(drive->tape);
	tape->sense_key=result->sense_key;
	tape->asc=result->asc;
	tape->ascq=result->ascq;
#if IDETAPE_DEBUG	
	/*
	 *	Without debugging, we only log an error if we decided to
	 *	give up retrying.
	 */
	printk ("ide-tape: sense key = %x, asc = %x, ascq = %x\n",result->sense_key,result->asc,result->ascq);
#endif /* IDETAPE_DEBUG */	
	return;	
}

void idetape_create_test_unit_ready_cmd (idetape_packet_command_t *pc)

{
#if IDETAPE_DEBUG
	printk ("ide-tape: Creating TEST UNIT READY packet command\n");
#endif /* IDETAPE_DEBUG */	
	pc->request_transfer=0;
	pc->buffer=NULL;
	pc->current_position=NULL;
	pc->callback=&idetape_pc_callback;
	pc->writing=0;
	
	idetape_zero_packet_command (pc);	
	pc->c[0]=IDETAPE_TEST_UNIT_READY_CMD;
}

void idetape_create_locate_cmd (idetape_packet_command_t *pc,unsigned long block,byte partition)

{
	unsigned long *ptr;

#if IDETAPE_DEBUG
	printk ("ide-tape: Creating LOCATE packet command\n");
#endif /* IDETAPE_DEBUG */
	pc->request_transfer=0;
	pc->buffer=NULL;
	pc->current_position=NULL;
	pc->buffer_size=0;
	pc->wait_for_dsc=1;
	pc->callback=&idetape_pc_callback;
	pc->writing=0;
		
	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_LOCATE_CMD;
	pc->c [1]=2;
	ptr=(unsigned long *) &(pc->c[3]);
	*ptr=idetape_swap_long (block);
	pc->c[8]=partition;
}

void idetape_create_rewind_cmd (idetape_packet_command_t *pc)

{
#if IDETAPE_DEBUG
	printk ("ide-tape: Creating REWIND packet command\n");
#endif /* IDETAPE_DEBUG */
	pc->request_transfer=0;
	pc->buffer=NULL;
	pc->current_position=NULL;
	pc->buffer_size=0;
	pc->wait_for_dsc=1;
	pc->callback=&idetape_pc_callback;
	pc->writing=0;
		
	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_REWIND_CMD;
}

/*
 *	A mode sense command is used to "sense" tape parameters.
 */

void idetape_create_mode_sense_cmd (idetape_packet_command_t *pc,byte page_code)

{
#if IDETAPE_DEBUG
		printk ("ide-tape: Creating MODE SENSE packet command - Page %d\n",page_code);
#endif /* IDETAPE_DEBUG */

	pc->wait_for_dsc=0;
	pc->callback=&idetape_pc_callback;
	pc->writing=0;

	switch (page_code) {
		case IDETAPE_CAPABILITIES_PAGE:
			pc->request_transfer=24;
	}
		
	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_MODE_SENSE_CMD;
	pc->c [1]=8;				/* DBD = 1 - Don't return block descriptors for now */
	pc->c [2]=page_code;
	pc->c [3]=255;				/* Don't limit the returned information */
	pc->c [4]=255;				/* (We will just discard data in that case) */
}

/*
 *	idetape_create_write_filemark_cmd will:
 *
 *		1.	Write a filemark if write_filemark=1.
 *		2.	Flush the device buffers without writing a filemark
 *			if write_filemark=0.
 *
 */
 
void idetape_create_write_filemark_cmd (idetape_packet_command_t *pc,int write_filemark)

{
#if IDETAPE_DEBUG
	printk ("Creating WRITE FILEMARK packet command\n");
	if (!write_filemark)
		printk ("which will only flush buffered data\n");
#endif /* IDETAPE_DEBUG */
	pc->request_transfer=0;
	pc->buffer=NULL;
	pc->current_position=NULL;
	pc->buffer_size=0;
	pc->wait_for_dsc=1;
	pc->callback=&idetape_pc_callback;
	pc->writing=0;
		
	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_WRITE_FILEMARK_CMD;
	if (write_filemark)
		pc->c [4]=1;
}

void idetape_create_erase_cmd (idetape_packet_command_t *pc)

{

#if IDETAPE_DEBUG
	printk ("Creating ERASE command\n");
#endif /* IDETAPE_DEBUG */

	pc->request_transfer=0;
	pc->buffer=NULL;
	pc->current_position=NULL;
	pc->buffer_size=0;
	pc->wait_for_dsc=1;
	pc->callback=&idetape_pc_callback;
	pc->writing=0;
		
	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_ERASE_CMD;
	pc->c [1]=1;
}

void idetape_create_read_cmd (idetape_packet_command_t *pc,unsigned long length)

{
	union convert {
		unsigned all	:32;
		struct {
			unsigned b1	:8;
			unsigned b2	:8;
			unsigned b3	:8;
			unsigned b4	:8;
		} b;
	} original;
	
#if IDETAPE_DEBUG
	printk ("ide-tape: Creating READ packet command\n");
#endif /* IDETAPE_DEBUG */

	original.all=length;

	pc->wait_for_dsc=0;
	pc->callback=&idetape_read_callback;
	pc->writing=0;

	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_READ_CMD;
	pc->c [1]=1;
	pc->c [4]=original.b.b1;
	pc->c [3]=original.b.b2;
	pc->c [2]=original.b.b3;

	return;
}

void idetape_create_space_cmd (idetape_packet_command_t *pc,long count,byte cmd)

{
	union convert {
		unsigned all	:32;
		struct {
			unsigned b1	:8;
			unsigned b2	:8;
			unsigned b3	:8;
			unsigned b4	:8;
		} b;
	} original;
	
#if IDETAPE_DEBUG
	printk ("ide-tape: Creating SPACE packet command\n");
#endif /* IDETAPE_DEBUG */

	original.all=count;

	pc->request_transfer=0;
	pc->buffer=NULL;
	pc->current_position=NULL;
	pc->buffer_size=0;
	pc->wait_for_dsc=1;
	pc->callback=&idetape_pc_callback;
	pc->writing=0;

	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_SPACE_CMD;
	pc->c [1]=cmd;
	pc->c [4]=original.b.b1;
	pc->c [3]=original.b.b2;
	pc->c [2]=original.b.b3;

	return;
}

void idetape_create_write_cmd (idetape_packet_command_t *pc,unsigned long length)

{
	union convert {
		unsigned all	:32;
		struct {
			unsigned b1	:8;
			unsigned b2	:8;
			unsigned b3	:8;
			unsigned b4	:8;
		} b;
	} original;
	
#if IDETAPE_DEBUG
	printk ("ide-tape: Creating WRITE packet command\n");
#endif /* IDETAPE_DEBUG */

	original.all=length;

	pc->wait_for_dsc=0;
	pc->callback=&idetape_write_callback;
	pc->writing=1;

	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_WRITE_CMD;
	pc->c [1]=1;
	pc->c [4]=original.b.b1;
	pc->c [3]=original.b.b2;
	pc->c [2]=original.b.b3;

	return;
}

void idetape_create_read_position_cmd (idetape_packet_command_t *pc)

{
#if IDETAPE_DEBUG
	printk ("ide-tape: Creating READ POSITION packet command\n");
#endif /* IDETAPE_DEBUG */

	pc->request_transfer=20;
	pc->wait_for_dsc=0;
	pc->callback=&idetape_read_position_callback;
	pc->writing=0;

	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_READ_POSITION_CMD;
	pc->c [1]=0;
}

void idetape_read_position_callback (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	struct request *rq;
	idetape_read_position_result_t *result;
	
	tape=&(drive->tape);
	
#if IDETAPE_DEBUG
	printk ("ide-tape: Reached idetape_read_position_callback\n");
#endif /* IDETAPE_DEBUG */

	rq=HWGROUP(drive)->rq;
	
	if (!tape->pc->error) {
		result=(idetape_read_position_result_t *) tape->pc->buffer;
#if IDETAPE_DEBUG
		printk ("Request completed\n");
		printk ("Dumping the results of the READ POSITION command\n");
		printk ("BOP - %s\n",result->bop ? "Yes":"No");
		printk ("EOP - %s\n",result->eop ? "Yes":"No");
#endif /* IDETAPE_DEBUG */
		if (result->bpu) {
			printk ("ide-tape: Block location is unknown to the tape\n");
			printk ("Aborting request\n");
			tape->block_address_valid=0;
			idetape_end_request (0,HWGROUP (drive));
		}
		else {
#if IDETAPE_DEBUG
			printk ("Block Location - %lu\n",idetape_swap_long (result->first_block));
#endif /* IDETAPE_DEBUG */
			tape->block_address=idetape_swap_long (result->first_block);
			tape->block_address_valid=1;
			idetape_end_request (1,HWGROUP (drive));
		}
	}
	else {
		printk ("Aborting request\n");
		idetape_end_request (0,HWGROUP (drive));
	}
	return;
}

/*
 *	Our special ide-tape ioctl's.
 *
 *	Currently there aren't any significant ioctl's.
 *	mtio.h compatible commands should be issued to the character device
 *	interface.
 */
 
int idetape_blkdev_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	idetape_packet_command_t pc;
	int retval;
	
	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;

#if IDETAPE_DEBUG	
	printk ("ide-tape: Reached idetape_blkdev_ioctl\n");
#endif /* IDETAPE_DEBUG */
	switch (cmd) {
		case IDETAPE_INQUIRY_IOCTL:
#if IDETAPE_DEBUG
			printk ("Adding INQUIRY packet command to the tail of the request queue\n");
#endif /* IDETAPE_DEBUG */
			idetape_create_inquiry_cmd (&pc);
			pc.buffer=pc.temp_buffer;
			pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
			pc.current_position=pc.temp_buffer;
			return (idetape_queue_pc_tail (drive,&pc));
		case IDETAPE_LOCATE_IOCTL:
#if IDETAPE_DEBUG
			printk ("Adding LOCATE packet command to the tail of the request queue\n");
#endif /* IDETAPE_DEBUG */
			idetape_create_locate_cmd (&pc,arg,0);
			retval=idetape_queue_pc_tail (drive,&pc);
			if (retval!=0) return (retval);
			
			idetape_create_read_position_cmd (&pc);
			pc.buffer=pc.temp_buffer;
			pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
			pc.current_position=pc.temp_buffer;
			return (idetape_queue_pc_tail (drive,&pc));
/*
		case IDETAPE_RESET_IOCTL:
			printk ("Resetting drive\n");
			return (!ide_do_reset (drive));
*/
		default:
			return -EIO;
	}
}

/*
 *	Functions which handle requests.
 */

/*
 *	idetape_end_request is used to end a request.
 *
 *	It is very similiar to ide_end_request, with a major difference - If
 *	we are handling our own requests rather than requests which originate
 *	in the buffer cache, we set rq->errors to 1 if the request failed.
 */

void idetape_end_request (byte uptodate, ide_hwgroup_t *hwgroup)

{
	ide_drive_t *drive = hwgroup->drive;
	struct request *rq = hwgroup->rq;

	if (rq->cmd == READ || rq->cmd == WRITE) {	/* Buffer cache originated request */
		ide_end_request (uptodate,hwgroup);	/* Let the common code handle it */
		return;
	}
							/* Our own originated request */
	rq->errors=!uptodate;				/* rq->errors will tell us if the request was successfull */
	ide_end_drive_cmd (drive, 0, 0);

	/* The "up(rq->sem);" does the necessary "wake_up()" for us,
	 * providing we started sleeping with a "down()" call.
	 * This may not be the case if the driver converts a READ or WRITE
	 * request into a special internal rq->cmd type.   -ml
	 */
	 
	 /*
	  * As Mark explained, we do not need a "wake_up()" call here,
	  * since we are always sleeping with a "down()" call.
	  */

}

/*
 *	idetape_do_request is our request handling function.	
 */

void idetape_do_request (ide_drive_t *drive, struct request *rq, unsigned long block)

{
	idetape_tape_t *tape;
	idetape_packet_command_t *pc;
	struct request *new_rq;
	idetape_status_reg_t status;

	tape=&(drive->tape);
		
#if IDETAPE_DEBUG
	printk ("Current request:\n");
	printk ("rq_status: %d, rq_dev: %u, cmd: %d, errors: %d\n",rq->rq_status,(unsigned int) rq->rq_dev,rq->cmd,rq->errors);
	printk ("sector: %ld, nr_sectors: %ld, current_nr_sectors: %ld\n",rq->sector,rq->nr_sectors,rq->current_nr_sectors);
#endif /* IDETAPE_DEBUG */

	/* Retry a failed packet command */

	if (tape->failed_pc != NULL && tape->pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
		idetape_issue_packet_command (drive,tape->failed_pc,&idetape_pc_intr);
		return;
	}

	/* Check if we have a postponed request */
	
	if (tape->postponed_rq != NULL) {
/* #if IDETAPE_DEBUG */
		if (tape->postponed_rq->rq_status != RQ_ACTIVE || rq != tape->postponed_rq) {
			printk ("ide-tape: ide-tape.c bug - Two DSC requests were queued\n");
			idetape_end_request (0,HWGROUP (drive));
			return;
		}
/* #endif */ /* IDETAPE_DEBUG */
		if (rq->cmd == IDETAPE_PACKET_COMMAND_REQUEST_TYPE1) {
	
			/* Media access command */
			
			tape->postponed_rq = NULL;
			idetape_media_access_finished (drive);
			return;
		}
		
		/*
		 * Read / Write command - DSC polling was done before the
		 * actual command - Continue normally so that the command
		 * will be performed below.
		 */
		 
		 tape->postponed_rq = NULL;
	}	
	

	if (rq->cmd == READ || rq->cmd == IDETAPE_READ_REQUEST || rq->cmd == WRITE || rq->cmd == IDETAPE_WRITE_REQUEST) {

		if (!tape->block_address_valid || tape->block_address!=rq->sector) {		/* Re-position the tape */
		
			if (tape->locate_to == rq->sector && tape->locate_retries > IDETAPE_LOCATE_RETRIES) {
				printk ("ide-tape: Can not reach block %lu - Aborting request\n",rq->sector);
				tape->locate_retries=0;
				idetape_end_request (0,HWGROUP (drive));
				return;
			}
						
			if (tape->locate_to == rq->sector)
				tape->locate_retries++;
			else {
				tape->locate_to=rq->sector;
				tape->locate_retries=1;
			}
#if IDETAPE_DEBUG
			printk ("ide-tape: We are not at the requested block\n");
			printk ("ide-tape: Re-positioning tape\n");
			printk ("ide-tape: Adding READ POSITION command to the head of the queue\n");
#endif /* IDETAPE_DEBUG */
			pc=idetape_next_pc_storage (drive);
			new_rq=idetape_next_rq_storage (drive);
			idetape_create_read_position_cmd (pc); 
			pc->buffer=pc->temp_buffer;
			pc->buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
			pc->current_position=pc->temp_buffer;
			idetape_queue_pc_head (drive,pc,new_rq);
#if IDETAPE_DEBUG			
			printk ("ide-tape: Adding LOCATE %lu command to the head of the queue\n",rq->sector);
#endif /* IDETAPE_DEBUG */
			pc=idetape_next_pc_storage (drive);
			new_rq=idetape_next_rq_storage (drive);
			idetape_create_locate_cmd (pc,rq->sector,0);
			idetape_queue_pc_head (drive,pc,new_rq);

			if (!tape->block_address_valid) {		/* The tape doesn't know the position - help it */
									/* by rewinding the tape */
#if IDETAPE_DEBUG			
				printk ("ide-tape: Adding LOCATE 0 command to the head of the queue\n");
#endif /* IDETAPE_DEBUG */
				pc=idetape_next_pc_storage (drive);
				new_rq=idetape_next_rq_storage (drive);
				idetape_create_locate_cmd (pc,0,0);
				idetape_queue_pc_head (drive,pc,new_rq);
			}
			
			return;
		}
		else
			tape->locate_retries=0;
	}
	
	switch (rq->cmd) {
		case READ:
		case IDETAPE_READ_REQUEST:
#if IDETAPE_DEBUG
			if (rq->cmd == READ)
				printk ("ide-tape: Handling buffer cache READ request\n");
			else
				printk ("ide-tape: Handling our own (not buffer cache originated) READ request\n");
#endif /* IDETAPE_DEBUG */			
			status.all=IN_BYTE (IDETAPE_STATUS_REG);
			if (!status.b.dsc) {				/* Tape buffer not ready to accept r/w command */
#if IDETAPE_DEBUG
				printk ("ide-tape: DSC != 1 - Postponing read request\n");
#endif /* IDETAPE_DEBUG */	
				tape->dsc_polling_frequency=IDETAPE_DSC_READ_WRITE_FREQUENCY;
				idetape_postpone_request (drive);	/* Allow ide.c to process requests from */
				return;
			}			

			tape->last_written_valid=0;
			
			pc=idetape_next_pc_storage (drive);

			idetape_create_read_cmd (pc,rq->current_nr_sectors);
			
			pc->buffer=rq->buffer;
			pc->buffer_size=rq->current_nr_sectors*512;
			pc->current_position=rq->buffer;
			pc->request_transfer=rq->current_nr_sectors*512;

			idetape_issue_packet_command (drive,pc,&idetape_pc_intr);
			return;
		
		case WRITE:
		case IDETAPE_WRITE_REQUEST:
#if IDETAPE_DEBUG
			if (rq->cmd == WRITE)
				printk ("ide-tape: Handling buffer cache WRITE request\n");
			else
				printk ("ide-tape: Handling our own (not buffer cache originated) WRITE request\n");
#endif /* IDETAPE_DEBUG */			

			status.all=IN_BYTE (IDETAPE_STATUS_REG);
			if (!status.b.dsc) {				/* Tape buffer not ready to accept r/w command */
#if IDETAPE_DEBUG
				printk ("ide-tape: DSC != 1 - Postponing write request\n");
#endif /* IDETAPE_DEBUG */	
				tape->dsc_polling_frequency=IDETAPE_DSC_READ_WRITE_FREQUENCY;
				idetape_postpone_request (drive);	/* Allow ide.c to process requests from */
				return;
			}			

			tape->last_written_valid=1;
			tape->last_written_block=rq->sector;
			
			pc=idetape_next_pc_storage (drive);

			idetape_create_write_cmd (pc,rq->current_nr_sectors);
			
			pc->buffer=rq->buffer;
			pc->buffer_size=rq->current_nr_sectors*512;
			pc->current_position=rq->buffer;
			pc->request_transfer=rq->current_nr_sectors*512;

			idetape_issue_packet_command (drive,pc,&idetape_pc_intr);
			return;
					
		case IDETAPE_PACKET_COMMAND_REQUEST_TYPE1:
		case IDETAPE_PACKET_COMMAND_REQUEST_TYPE2:
/*
 *	This should be unnecessary (postponing of a general packet command),
 *	but I have occasionally missed DSC on a media access command otherwise.
 *	??? Still have to figure it out ...
 */
			status.all=IN_BYTE (IDETAPE_STATUS_REG);
			if (!status.b.dsc) {				/* Tape buffers are still not ready */
#if IDETAPE_DEBUG
				printk ("ide-tape: DSC != 1 - Postponing packet command request\n");
#endif IDETAPE_DEBUG
				rq->cmd=IDETAPE_PACKET_COMMAND_REQUEST_TYPE2;	/* Note that we are waiting for DSC *before* we */
										/* even issued the command */
				tape->dsc_polling_frequency=IDETAPE_DSC_READ_WRITE_FREQUENCY;
				idetape_postpone_request (drive);	/* Allow ide.c to process requests from */
				return;
			}
			rq->cmd=IDETAPE_PACKET_COMMAND_REQUEST_TYPE1;
			pc=(idetape_packet_command_t *) rq->buffer;
			idetape_issue_packet_command (drive,pc,&idetape_pc_intr);
			return;

		default:
			printk ("ide-tape: Unknown command in request - Aborting request\n");
			idetape_end_request (0,HWGROUP (drive));
	}	
}

/*
 *	idetape_queue_pc_tail is based on the following functions:
 *
 *	ide_do_drive_cmd from ide.c
 *	cdrom_queue_request and cdrom_queue_packet_command from ide-cd.c
 *
 *	We add a special packet command request to the tail of the request queue,
 *	and wait for it to be serviced.
 *
 *	This is not to be called from within the request handling part
 *	of the driver ! We allocate here data in the stack, and it is valid
 *	until the request is finished. This is not the case for the bottom
 *	part of the driver, where we are always leaving the functions to wait
 *	for an interrupt or a timer event.
 *
 *	From the bottom part of the driver, we should allocate safe memory
 *	using idetape_next_pc_storage and idetape_next_rq_storage, and add
 *	the request to the request list without waiting for it to be serviced !
 *	In that case, we usually use idetape_queue_pc_head.
 */

int idetape_queue_pc_tail (ide_drive_t *drive,idetape_packet_command_t *pc)
{
	struct request rq;

	ide_init_drive_cmd (&rq);
	rq.buffer = (char *) pc;
	rq.cmd = IDETAPE_PACKET_COMMAND_REQUEST_TYPE1;
	return ide_do_drive_cmd (drive, &rq, ide_wait);
}

/*
 *	idetape_queue_pc_head generates a new packet command request in front
 *	of the request queue, before the current request, so that it will be
 *	processed immediately, on the next pass through the driver.
 *
 *	idetape_queue_pc_head is called from the request handling part of
 *	the driver (the "bottom" part). Safe storage for the request should
 *	be allocated with idetape_next_pc_storage and idetape_next_rq_storage
 *	before calling idetape_queue_pc_head.
 *
 *	Memory for those requests is pre-allocated at initialization time, and
 *	is limited to IDETAPE_PC_STACK requests. We assume that we have enough
 *	space for the maximum possible number of inter-dependent packet commands.
 *
 *	The higher level of the driver - The ioctl handler and the character
 *	device handling functions should queue request to the lower level part
 *	and wait for their completion using idetape_queue_pc_tail or
 *	idetape_queue_rw_tail.
 */
 
void idetape_queue_pc_head (ide_drive_t *drive,idetape_packet_command_t *pc,struct request *rq)

{
	ide_init_drive_cmd (rq);
	rq->buffer = (char *) pc;
	rq->cmd = IDETAPE_PACKET_COMMAND_REQUEST_TYPE1;
	(void) ide_do_drive_cmd (drive, rq, ide_preempt);
}

/*
 *	idetape_queue_rw_tail is typically called from the character device
 *	interface to generate a read/write request for the block device interface
 *	and wait for it to be serviced. Note that cmd will be different than
 *	a buffer cache originated read/write request. This will be used
 *	in idetape_end_request.
 *
 *	Returns 0 on success or -EIO if an error occured.
 */

int idetape_queue_rw_tail (ide_drive_t *drive,int cmd,int blocks,char *buffer)

{
	idetape_tape_t *tape = &(drive->tape);
	struct request rq;

#if IDETAPE_DEBUG
	printk ("idetape_queue_rw_tail: cmd=%d\n",cmd);
#endif /* IDETAPE_DEBUG */
	/* build up a special read request, and add it to the queue */
	
	ide_init_drive_cmd (&rq);
	rq.buffer = buffer;
	rq.cmd = cmd;
	rq.sector = tape->block_address;
	rq.nr_sectors = blocks;
	rq.current_nr_sectors = blocks;
	return ide_do_drive_cmd (drive, &rq, ide_wait);
}

/*
 *	Copied from ide.c (declared static there)
 */

void idetape_fixstring (byte *s, const int bytecount, const int byteswap)
{
	byte *p = s, *end = &s[bytecount & ~1]; /* bytecount must be even */

	if (byteswap) {
		/* convert from big-endian to host byte order */
		for (p = end ; p != s;) {
			unsigned short *pp = (unsigned short *) (p -= 2);
			*pp = ntohs(*pp);
		}
	}

	/* strip leading blanks */
	while (s != end && *s == ' ')
		++s;

	/* compress internal blanks and strip trailing blanks */
	while (s != end && *s) {
		if (*s++ != ' ' || (s != end && *s && *s != ' '))
			*p++ = *(s-1);
	}

	/* wipe out trailing garbage */
	while (p != end)
		*p++ = '\0';
}

/*
 *	idetape_zero_packet_command just zeros a packet command and
 *	sets the number of retries to 0, as we haven't retried it yet.
 */
 
void idetape_zero_packet_command (idetape_packet_command_t *pc)

{
	int i;
	
	for (i=0;i<12;i++)
		pc->c[i]=0;
	pc->retries=0;
}

/*
 *	idetape_swap_shorts converts a 16 bit number from little endian
 *	to big endian format.
 */
 
unsigned short idetape_swap_short (unsigned short temp)

{
	union convert {
		unsigned all	:16;
		struct {
			unsigned b1	:8;
			unsigned b2	:8;
		} b;
	} original,converted;
	
	original.all=temp;
	converted.b.b1=original.b.b2;
	converted.b.b2=original.b.b1;
	return (converted.all);
}

/*
 *	idetape_swap_long converts from little endian to big endian format.
 */
 
unsigned long idetape_swap_long (unsigned long temp)

{
	union convert {
		unsigned all	:32;
		struct {
			unsigned b1	:8;
			unsigned b2	:8;
			unsigned b3	:8;
			unsigned b4	:8;
		} b;
	} original,converted;
	
	original.all=temp;
	converted.b.b1=original.b.b4;
	converted.b.b2=original.b.b3;
	converted.b.b3=original.b.b2;
	converted.b.b4=original.b.b1;
	return (converted.all);
}


/*
 *	idetape_next_pc_storage returns a pointer to a place in which we can
 *	safely store a packet command, even though we intend to leave the
 *	driver. A storage space for a maximum of IDETAPE_PC_STACK packet
 *	commands is allocated at initialization time.
 */
 
idetape_packet_command_t *idetape_next_pc_storage (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	
	tape=&(drive->tape);
#if IDETAPE_DEBUG
	printk ("ide-tape: pc_stack_index=%d\n",tape->pc_stack_index);
#endif /* IDETAPE_DEBUG */
	if (tape->pc_stack_index==IDETAPE_PC_STACK)
		tape->pc_stack_index=0;
	return (&(tape->pc_stack [tape->pc_stack_index++]));
}

/*
 *	idetape_next_rq_storage is used along with idetape_next_pc_storage.
 *	Since we queue packet commands in the request queue, we need to
 *	allocate a request, along with the allocation of a packet command.
 */
 
/**************************************************************
 *                                                            *
 *  This should get fixed to use kmalloc(GFP_ATOMIC, ..)      *
 *  followed later on by kfree().   -ml                       *
 *                                                            *
 **************************************************************/
 
struct request *idetape_next_rq_storage (ide_drive_t *drive)

{
	idetape_tape_t *tape;
	
	tape=&(drive->tape);

#if IDETAPE_DEBUG
	printk ("ide-tape: rq_stack_index=%d\n",tape->rq_stack_index);
#endif /* IDETAPE_DEBUG */
	if (tape->rq_stack_index==IDETAPE_PC_STACK)
		tape->rq_stack_index=0;
	return (&(tape->rq_stack [tape->rq_stack_index++]));
}

/*
 *	Block device interface functions
 *
 *	The default action is not to allow direct access to the block device
 *	interface (-EBUSY will be returned on open).
 */

int idetape_blkdev_open (struct inode *inode, struct file *filp, ide_drive_t *drive)

{
#if IDETAPE_ALLOW_OPENING_BLOCK_DEVICE
	return (0);
#else
	printk ("ide-tape: The block device interface should not be used.\n");
	printk ("ide-tape: Use the character device interfaces\n");
	printk ("ide-tape: /dev/ht0 and /dev/nht0 instead.\n");
	printk ("ide-tape: (Run linux/drivers/block/MAKEDEV.ide to create them)\n");
	printk ("ide-tape: Refusing open request.\n");
	return (-EBUSY);
#endif /* IDETAPE_ALLOW_OPENING_BLOCK_DEVICE */
}

void idetape_blkdev_release (struct inode *inode, struct file *filp, ide_drive_t *drive)

{
	return;
}

/*
 *	Character device interface functions
 */

/*
 *	lseek is currently not installed.
 */
 
int idetape_chrdev_lseek (struct inode *inode, struct file *file, off_t offset, int origin)

{
	ide_drive_t *drive;
	
#if IDETAPE_DEBUG
	printk ("Reached idetape_chrdev_lseek\n");
#endif /* IDETAPE_DEBUG */

	drive=idetape_chrdev.drive;
	if (idetape_position_tape (drive,offset) != 0) {
		printk ("ide-tape: Rewinding tape failed\n");
		return (-1);
	}
	
	return (0);
}

/*
 *	Our character device read / write functions.
 *
 *	The tape is optimized to maximize throughpot when it is transfering
 *	an integral number of the "continous transfer limit", which is
 *	a parameter of the specific tape (26 KB on my particular tape). The
 *	resulting increase in performance should be dramatical. In the
 *	character device read/write functions, we split the current
 *	request to units of the above size, and handle the remaining bytes
 *	in some other sub-functions.
 *
 *	In case the count number is not even an integral number of the tape
 *	block size (usually 512 or 1024 bytes), we will pad the transfer with
 *	zeroes (write) or read the entire block and return only the requested
 *	bytes (but the tape will be in the "wrong" position). Do not supply
 *	such a count value unless you are going to close the device right
 *	after this request.
 *
 *	Again, for best results use an integral number of the tape's parameter
 *	(which is displayed in the driver installation stage). I will soon
 *	add an ioctl to get this important parameter.
 */

/*
 *	Our character device read function.
 */

int idetape_chrdev_read (struct inode *inode, struct file *file, char *buf, int count)

{
	ide_drive_t *drive;
	idetape_tape_t *tape;
	int blocks,remainder,retval,ctl_bytes;
	char *buf_ptr;
	unsigned long previous_block_address,actually_read;

#if IDETAPE_DEBUG
	printk ("Reached idetape_chrdev_read\n");
#endif /* IDETAPE_DEBUG */

	drive=idetape_chrdev.drive;
	tape=&(drive->tape);
	tape->last_dt_was_write=0;

	if (count==0)
		return (0);

	actually_read=0;
	buf_ptr=buf;
	ctl_bytes=tape->capabilities.ctl*tape->tape_block_size;
	blocks=count/ctl_bytes;
	remainder=count%ctl_bytes;
	
	while (blocks) {
#if IDETAPE_DEBUG
		printk ("Adding a READ request to the block device request queue\n");
#endif /* IDETAPE_DEBUG */
		previous_block_address=tape->block_address;
		retval=idetape_queue_rw_tail (drive,IDETAPE_READ_REQUEST,tape->capabilities.ctl,tape->data_buffer);
		actually_read+=tape->tape_block_size*(tape->block_address-previous_block_address);

		if (retval) {
			printk ("ide-tape: Error occured while reading\n");
			return (actually_read);
		}
#if IDETAPE_DEBUG
	printk ("Copying %d bytes to the user space memory\n",ctl_bytes);
#endif /* IDETAPE_DEBUG */

		memcpy_tofs (buf_ptr,tape->data_buffer,ctl_bytes);
		buf_ptr+=ctl_bytes;
		blocks--;
	}
	if (remainder)
		return (actually_read+idetape_chrdev_read_remainder (inode,file,buf_ptr,remainder));
	else
		return (actually_read);		
}
 
int idetape_chrdev_read_remainder (struct inode *inode, struct file *file, char *buf, int count)

{
	ide_drive_t *drive;
	idetape_tape_t *tape;
	int blocks,remainder,retval;
	unsigned long previous_block_address,actually_read;

#if IDETAPE_DEBUG
	printk ("Reached idetape_chrdev_read_remainder\n");
#endif /* IDETAPE_DEBUG */

	drive=idetape_chrdev.drive;
	tape=&(drive->tape);

	tape->last_dt_was_write=0;

	if (count==0)
		return (0);


	blocks=count/512;
	remainder=count%512;
	if (remainder) {
#if IDETAPE_DEBUG
	printk ("ide-tape: Padding read to block boundary\n");
#endif /* IDETAPE_DEBUG */
		blocks++;
	}
#if IDETAPE_DEBUG
	printk ("Adding a READ request to the block device request queue\n");
#endif /* IDETAPE_DEBUG */
	previous_block_address=tape->block_address;
	retval=idetape_queue_rw_tail (drive,IDETAPE_READ_REQUEST,blocks,tape->data_buffer);
	if (retval) {
		printk ("ide-tape: Error occured while reading\n");
		actually_read=512*(tape->block_address-previous_block_address);
		if (actually_read > count)
			actually_read=count;
		if (actually_read != 0)
			memcpy_tofs (buf,tape->data_buffer,actually_read);
		return (actually_read);
	}
#if IDETAPE_DEBUG
	printk ("Copying %d bytes to the user space memory\n",count);
#endif /* IDETAPE_DEBUG */
	memcpy_tofs (buf,tape->data_buffer,count);
	return (count);
}

int idetape_chrdev_write (struct inode *inode, struct file *file, const char *buf, int count)

{
	ide_drive_t *drive;
	idetape_tape_t *tape;
	int blocks,remainder,retval,ctl_bytes;
	const char *buf_ptr;
	unsigned long previous_block_address,actually_written;

#if IDETAPE_DEBUG
	printk ("Reached idetape_chrdev_write\n");
#endif /* IDETAPE_DEBUG */

	drive=idetape_chrdev.drive;
	tape=&(drive->tape);
	tape->last_dt_was_write=1;

	if (count==0)
		return (0);

	actually_written=0;
	buf_ptr=buf;
	ctl_bytes=tape->capabilities.ctl*tape->tape_block_size;
	blocks=count/ctl_bytes;
	remainder=count%ctl_bytes;

	while (blocks) {
#if IDETAPE_DEBUG
		printk ("Copying %d bytes from the user space memory\n",ctl_bytes);
#endif /* IDETAPE_DEBUG */
		memcpy_fromfs (tape->data_buffer,buf_ptr,ctl_bytes);
		buf_ptr+=ctl_bytes;
#if IDETAPE_DEBUG
		printk ("Adding a WRITE request to the block device request queue\n");
#endif /* IDETAPE_DEBUG */
		previous_block_address=tape->block_address;
		retval=idetape_queue_rw_tail (drive,IDETAPE_WRITE_REQUEST,tape->capabilities.ctl,tape->data_buffer);
		actually_written+=tape->tape_block_size*(tape->block_address-previous_block_address);

		if (retval) {
			printk ("ide-tape: Error occured while writing\n");
			return (actually_written);
		}
		blocks--;
	}
	if (remainder)
		return (actually_written+idetape_chrdev_write_remainder (inode,file,buf_ptr,remainder));
	else
		return (actually_written);		
}

int idetape_chrdev_write_remainder (struct inode *inode, struct file *file, const char *buf, int count)

{
	ide_drive_t *drive;
	idetape_tape_t *tape;
	int blocks,remainder,retval;
	char *ptr;
	unsigned long previous_block_address,actually_written;

#if IDETAPE_DEBUG
	printk ("Reached idetape_chrdev_write_remainder\n");
#endif /* IDETAPE_DEBUG */
		
	drive=idetape_chrdev.drive;
	tape=&(drive->tape);

	blocks=count/512;
	remainder=count%512;
	if (remainder)
		blocks++;
#if IDETAPE_DEBUG
	printk ("Copying %d bytes from the user space memory\n",count);
#endif /* IDETAPE_DEBUG */

	memcpy_fromfs (tape->data_buffer,buf,count);
	if (remainder) {
#if IDETAPE_DEBUG
	printk ("ide-tape: Padding written data to block boundary\n");
#endif /* IDETAPE_DEBUG */
		ptr=tape->data_buffer+(blocks-1)*512;
		memset (ptr,0,remainder);
	}
#if IDETAPE_DEBUG
	printk ("Adding a WRITE request to the block device request queue\n");
#endif /* IDETAPE_DEBUG */

	previous_block_address=tape->block_address;
	retval=idetape_queue_rw_tail (drive,IDETAPE_WRITE_REQUEST,blocks,tape->data_buffer);
	if (retval) {
		printk ("ide-tape: Error occured while writing\n");
		actually_written=512*(tape->block_address-previous_block_address);
		if (actually_written > count)
			actually_written=count;
		return (actually_written);
	}
	return (count);
}

/*
 *	Our character device ioctls.
 *
 *	General mtio.h magnetic io commands are supported here, and not in
 *	the correspoding block interface.
 *
 *	Our own ide-tape ioctls are supported on both interfaces.
 */

int idetape_chrdev_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)

{
	struct mtop mtop;
	ide_drive_t *drive;
	int retval;

#if IDETAPE_DEBUG
	printk ("Reached idetape_chrdev_ioctl, cmd=%u\n",cmd);
#endif

	drive=idetape_chrdev.drive;

	switch (cmd) {
		case MTIOCTOP:
			retval=verify_area (VERIFY_READ,(char *) arg,sizeof (struct mtop));
			if (retval) return (retval);
			memcpy_fromfs ((char *) &mtop, (char *) arg, sizeof (struct mtop));
			return (idetape_mtioctop (drive,mtop.mt_op,mtop.mt_count));
		default:
			return (idetape_blkdev_ioctl (drive,inode,file,cmd,arg));
	}
}

/*
 *	idetape_mtioctop is called from idetape_chrdev_ioctl when
 *	the general mtio MTIOCTOP ioctl is requested.
 *
 *	We currently support the following mtio.h operations:
 *
 *	MTFSF	-	Space over mt_count filemarks in the positive direction.
 *			The tape is positioned after the last spaced filemark.
 *
 *	MTFSFM	-	Same as MTFSF, but the tape is positioned before the
 *			last filemark.
 *
 *	MTBSF	-	Steps background over mt_count filemarks, tape is
 *			positioned before the last filemark.
 *
 *	MTBSFM	-	Like MTBSF, only tape is positioned after the last filemark.
 *
 *	MTWEOF	-	Writes mt_count filemarks. Tape is positioned after
 *			the last written filemark.
 *
 *	MTREW	-	Rewindes tape.
 *
 *	MTNOP	-	Flushes tape buffers.
 *
 *	MTEOM	-	Moves to the end of recorded data.
 *
 *	MTERASE	-	Erases tape.
 *
 *	The following commands are currently not supported:
 *
 *	MTFSR, MTBSR, MTFSS, MTBSS, MTWSM, MTOFFL, MTRETEN, MTSEEK, MTSETBLK,
 *	MTSETDENSITY, MTSETDRVBUFFER, MT_ST_BOOLEANS, MT_ST_WRITE_THRESHOLD.
 */
 
int idetape_mtioctop (ide_drive_t *drive,short mt_op,int mt_count)

{
	int i,retval;
	
	idetape_packet_command_t pc;

	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;

	idetape_create_write_filemark_cmd (&pc,0);	/* Flush buffers */
	retval=idetape_queue_pc_tail (drive,&pc);
	if (retval) return (retval);

	switch (mt_op) {
		case MTFSF:
#if IDETAPE_DEBUG
			printk ("Handling MTFSF command\n");
#endif /* IDETAPE_DEBUG */
			idetape_create_space_cmd (&pc,mt_count,IDETAPE_SPACE_OVER_FILEMARK);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTFSFM:
#if IDETAPE_DEBUG
			printk ("Handling MTFSFM command\n");
#endif /* IDETAPE_DEBUG */
			retval=idetape_mtioctop (drive,MTFSF,mt_count);
			if (retval) return (retval);
			return (idetape_mtioctop (drive,MTBSF,1));
		case MTBSF:
#if IDETAPE_DEBUG
			printk ("Handling MTBSF command\n");
#endif /* IDETAPE_DEBUG */
			idetape_create_space_cmd (&pc,-mt_count,IDETAPE_SPACE_OVER_FILEMARK);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTBSFM:
#if IDETAPE_DEBUG
			printk ("Handling MTBSFM command\n");
#endif /* IDETAPE_DEBUG */
			retval=idetape_mtioctop (drive,MTBSF,mt_count);
			if (retval) return (retval);
			return (idetape_mtioctop (drive,MTFSF,1));
		case MTWEOF:
#if IDETAPE_DEBUG
			printk ("Handling MTWEOF command\n");
#endif /* IDETAPE_DEBUG */
		
			for (i=0;i<mt_count;i++) {
				idetape_create_write_filemark_cmd (&pc,1);
				retval=idetape_queue_pc_tail (drive,&pc);
				if (retval) return (retval);
			}
			return (0);
		case MTREW:
#if IDETAPE_DEBUG
			printk ("Handling MTREW command\n");
#endif /* IDETAPE_DEBUG */
			return (idetape_rewind_tape (drive));
		case MTNOP:
#if IDETAPE_DEBUG
			printk ("Handling MTNOP command\n");
#endif /* IDETAPE_DEBUG */
			idetape_create_write_filemark_cmd (&pc,0);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTEOM:
#if IDETAPE_DEBUG
			printk ("Handling MTEOM command\n");
#endif /* IDETAPE_DEBUG */
		
			idetape_create_space_cmd (&pc,0,IDETAPE_SPACE_TO_EOD);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTERASE:
#if IDETAPE_DEBUG
			printk ("Handling MTERASE command\n");
#endif /* IDETAPE_DEBUG */
			retval=idetape_position_tape (drive,0);
			if (retval) return (retval);
			idetape_create_erase_cmd (&pc);
			return (idetape_queue_pc_tail (drive,&pc));
		default:
			printk ("ide-tape: MTIO operation %d not supported\n",mt_op);
			return (-EIO);
	}
}

/*
 *	Our character device open function.
 */

int idetape_chrdev_open (struct inode *inode, struct file *filp)

{
	ide_drive_t *drive;
	idetape_tape_t *tape;
	unsigned long flags;
	unsigned int minor;
		
	save_flags (flags);
	cli();

#if IDETAPE_DEBUG
	printk ("Reached idetape_chrdev_open\n");
#endif /* IDETAPE_DEBUG */


	drive=idetape_chrdev.drive;
	tape=&(drive->tape);
	minor=MINOR (inode->i_rdev);

	if (minor!=0 && minor!=128) {		/* Currently supporting only one */
		restore_flags (flags);		/* tape drive */
		return (-ENXIO);
	}

	if (tape->busy) {
		restore_flags (flags);		/* Allowing access only through one */
		return (-EBUSY);		/* one file descriptor */
	}

	tape->busy=1;
	restore_flags (flags);

	if (!tape->block_address_valid) {
		if (idetape_rewind_tape (drive)) {
			printk ("ide-tape: Rewinding tape failed\n");
			tape->busy=0;
			return (-EIO);
		}
	}

	tape->last_dt_was_write=0;
	
	return (0);
}

/*
 *	Our character device release function.
 */

void idetape_chrdev_release (struct inode *inode, struct file *filp)

{
	ide_drive_t *drive;
	idetape_tape_t *tape;
	
	unsigned int minor;
	idetape_packet_command_t pc;
	unsigned long flags;
			
#if IDETAPE_DEBUG
	printk ("Reached idetape_chrdev_release\n");
#endif /* IDETAPE_DEBUG */

	drive=idetape_chrdev.drive;
	tape=&(drive->tape);
	minor=MINOR (inode->i_rdev);

	if (tape->last_dt_was_write) {
		idetape_create_write_filemark_cmd (&pc,1);	/* Write a filemark */
		if (idetape_queue_pc_tail (drive,&pc)) {
			printk ("ide-tape: Couldn't write a filemark\n");
			/* ??? */
		}
	}
	else {
		idetape_create_write_filemark_cmd (&pc,0);	/* Flush buffers */
		if (idetape_queue_pc_tail (drive,&pc)) {
			printk ("ide-tape: Couldn't flush buffers\n");
			/* ??? */
		}
	}

	if (minor < 128) {
		if (idetape_rewind_tape (drive)) {
			printk ("ide-tape: Rewinding tape failed\n");
			/* ??? */
		}
	}

	save_flags (flags);
	cli();
	tape->busy=0;
	restore_flags (flags);
		
	return;
}

/*
 *	idetape_position_tape positions the tape to the requested block
 *	using the LOCATE packet command. A READ POSITION command is then
 *	issued to check where we are positioned.
 *
 *	Like all higher level operations, we queue the commands at the tail
 *	of the request queue and wait for their completion.
 *	
 */
 
int idetape_position_tape (ide_drive_t *drive,unsigned long block)

{
	int retval;
	idetape_packet_command_t pc;

	idetape_create_locate_cmd (&pc,block,0);
	retval=idetape_queue_pc_tail (drive,&pc);
	if (retval!=0) return (retval);
			
	idetape_create_read_position_cmd (&pc);
	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;
	return (idetape_queue_pc_tail (drive,&pc));
}

/*
 *	Rewinds the tape to the Begining Of the current Partition (BOP).
 *
 *	We currently support only one partition.
 */ 

int idetape_rewind_tape (ide_drive_t *drive)

{
	int retval;
	idetape_packet_command_t pc;
#if IDETAPE_DEBUG
	printk ("Reached idetape_rewind_tape\n");
#endif /* IDETAPE_DEBUG */	

	idetape_create_write_filemark_cmd (&pc,0);	/* Flush buffers */
	retval=idetape_queue_pc_tail (drive,&pc);
	if (retval) return (retval);
	
	idetape_create_rewind_cmd (&pc);
	retval=idetape_queue_pc_tail (drive,&pc);
	if (retval) return (retval);
			
	idetape_create_read_position_cmd (&pc);
	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;
	return (idetape_queue_pc_tail (drive,&pc));
}
