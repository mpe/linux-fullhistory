/*
 * linux/drivers/block/ide-tape.c	Version 1.5 - ALPHA	Apr  12, 1996
 *
 * Copyright (C) 1995, 1996 Gadi Oxman <gadio@netvision.net.il>
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
 * The driver, in co-operation with ide.c, basically traverses the 
 * request-list for the block device interface. The character device
 * interface, on the other hand, creates new requests, adds them
 * to the request-list of the block device, and waits for their completion.
 *
 * Pipelined operation mode is now supported on both reads and writes.
 *
 * The block device major and minor numbers are determined from the
 * tape's relative position in the ide interfaces, as explained in ide.c.
 *
 * The character device interface consists of two devices:
 *
 * ht0		major=37,minor=0	first IDE tape, rewind on close.
 * nht0		major=37,minor=128	first IDE tape, no rewind on close.
 *
 * Run /usr/src/linux/drivers/block/MAKEDEV.ide to create the above entries.
 * We currently support only one ide tape drive.
 *
 * The general magnetic tape commands compatible interface, as defined by
 * include/linux/mtio.h, is accessible through the character device.
 *
 * General ide driver configuration options, such as the interrupt-unmask
 * flag, can be configured by issuing an ioctl to the block device interface,
 * as any other ide device.
 *
 * Our own ide-tape ioctl's can can be issued to either the block device or
 * the character device interface.
 *
 * Maximal throughput with minimal bus load will usually be achieved in the
 * following scenario:
 *
 *	1.	ide-tape is operating in the pipelined operation mode.
 *	2.	All character device read/write requests consist of an
 *		integral number of the tape's recommended data transfer unit
 *		(which is shown on initialization and can be received with
 *		 an ioctl).
 *		As of version 1.3 of the driver, this is no longer as critical
 *		as it used to be.
 *	3.	No buffering is performed by the user backup program.
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
 * Ver 1.0   Dec 11 95   Integrated into Linux 1.3.46 development tree.
 *                       On each read / write request, we now ask the drive
 *                        if we can transfer a constant number of bytes
 *                        (a parameter of the drive) only to its buffers,
 *                        without causing actual media access. If we can't,
 *                        we just wait until we can by polling the DSC bit.
 *                        This ensures that while we are not transferring
 *                        more bytes than the constant referred to above, the
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
 * Ver 1.1   Dec 14 95   Fixed random problems which occurred when the tape
 *                        shared an interface with another device.
 *                        (poll_for_dsc was a complete mess).
 *                       Removed some old (non-active) code which had
 *                        to do with supporting buffer cache originated
 *                        requests.
 *                       The block device interface can now be opened, so
 *                        that general ide driver features like the unmask
 *                        interrupts flag can be selected with an ioctl.
 *                        This is the only use of the block device interface.
 *                       New fast pipelined operation mode (currently only on
 *                        writes). When using the pipelined mode, the
 *                        throughput can potentially reach the maximum
 *                        tape supported throughput, regardless of the
 *                        user backup program. On my tape drive, it sometimes
 *                        boosted performance by a factor of 2. Pipelined
 *                        mode is enabled by default, but since it has a few
 *                        downfalls as well, you may want to disable it.
 *                        A short explanation of the pipelined operation mode
 *                        is available below.
 * Ver 1.2   Jan  1 96   Eliminated pipelined mode race condition.
 *                       Added pipeline read mode. As a result, restores
 *                        are now as fast as backups.
 *                       Optimized shared interface behavior. The new behavior
 *                        typically results in better IDE bus efficiency and
 *                        higher tape throughput.
 *                       Pre-calculation of the expected read/write request
 *                        service time, based on the tape's parameters. In
 *                        the pipelined operation mode, this allows us to
 *                        adjust our polling frequency to a much lower value,
 *                        and thus to dramatically reduce our load on Linux,
 *                        without any decrease in performance.
 *                       Implemented additional mtio.h operations.
 *                       The recommended user block size is returned by
 *                        the MTIOCGET ioctl.
 *                       Additional minor changes.
 * Ver 1.3   Feb  9 96   Fixed pipelined read mode bug which prevented the
 *                        use of some block sizes during a restore procedure.
 *                       The character device interface will now present a
 *                        continuous view of the media - any mix of block sizes
 *                        during a backup/restore procedure is supported. The
 *                        driver will buffer the requests internally and
 *                        convert them to the tape's recommended transfer
 *                        unit, making performance almost independent of the
 *                        chosen user block size.
 *                       Some improvements in error recovery.
 *                       By cooperating with triton.c, bus mastering DMA can
 *                        now sometimes be used with IDE tape drives as well.
 *                        Bus mastering DMA has the potential to dramatically
 *                        reduce the CPU's overhead when accessing the device,
 *                        and can be enabled by using hdparm -d1 on the tape's
 *                        block device interface. For more info, read the
 *                        comments in triton.c.
 * Ver 1.4   Mar 13 96   Fixed serialize support.
 * Ver 1.5   Apr 12 96   Fixed shared interface operation, broken in 1.3.85.
 *                       Fixed pipelined read mode inefficiency.
 *                       Fixed nasty null dereferencing bug.
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

/*
 * An overview of the pipelined operation mode.
 *
 * In the pipelined write mode, we will usually just add requests to our
 * pipeline and return immediately, before we even start to service them. The
 * user program will then have enough time to prepare the next request while
 * we are still busy servicing previous requests. In the pipelined read mode,
 * the situation is similar - we add read-ahead requests into the pipeline,
 * before the user even requested them.
 *
 * The pipeline can be viewed as a "safety net" which will be activated when
 * the system load is high and prevents the user backup program from keeping up
 * with the current tape speed. At this point, the pipeline will get
 * shorter and shorter but the tape will still be streaming at the same speed.
 * Assuming we have enough pipeline stages, the system load will hopefully
 * decrease before the pipeline is completely empty, and the backup program
 * will be able to "catch up" and refill the pipeline again.
 * 
 * When using the pipelined mode, it would be best to disable any type of
 * buffering done by the user program, as ide-tape already provides all the
 * benefits in the kernel, where it can be done in a more efficient way.
 * As we will usually not block the user program on a request, the most
 * efficient user code will then be a simple read-write-read-... cycle.
 * Any additional logic will usually just slow down the backup process.
 *
 * Using the pipelined mode, I get a constant over 400 KBps throughput,
 * which seems to be the maximum throughput supported by my tape.
 *
 * However, there are some downfalls:
 *
 *	1.	We use memory (for data buffers) in proportional to the number
 *		of pipeline stages (each stage is about 26 KB with my tape).
 *	2.	In the pipelined write mode, we cheat and postpone error codes
 *		to the user task. In read mode, the actual tape position
 *		will be a bit further than the last requested block.
 *
 * Concerning (1):
 *
 *	1.	We allocate stages dynamically only when we need them. When
 *		we don't need them, we don't consume additional memory. In
 *		case we can't allocate stages, we just manage without them
 *		(at the expense of decreased throughput) so when Linux is
 *		tight in memory, we will not pose additional difficulties.
 *
 *	2.	The maximum number of stages (which is, in fact, the maximum
 *		amount of memory) which we allocate is limited by the compile
 *		time parameter IDETAPE_MAX_PIPELINE_STAGES.
 *
 *	3.	The maximum number of stages is a controlled parameter - We
 *		don't start from the user defined maximum number of stages
 *		but from the lower IDETAPE_MIN_PIPELINE_STAGES (again, we
 *		will not even allocate this amount of stages if the user
 *		program can't handle the speed). We then implement a feedback
 *		loop which checks if the pipeline is empty, and if it is, we
 *		increase the maximum number of stages as necessary until we
 *		reach the optimum value which just manages to keep the tape
 *		busy with with minimum allocated memory or until we reach
 *		IDETAPE_MAX_PIPELINE_STAGES.
 *
 * Concerning (2):
 *
 *	In pipelined write mode, ide-tape can not return accurate error codes
 *	to the user program since we usually just add the request to the
 *      pipeline without waiting for it to be serviced. In case an error
 *      occurs, I will report it on the next user request.
 *
 *	In the pipelined read mode, subsequent read requests or forward
 *	filemark spacing will perform correctly, as we preserve all blocks
 *	and filemarks which we encountered during our excess read-ahead.
 * 
 *	For accurate tape positioning and error reporting, disabling
 *	pipelined mode might be the best option.
 *
 * You can enable/disable/tune the pipelined operation mode by adjusting
 * the compile time parameters in ide-tape.h.
 */

/*
 *	Possible improvements.
 *
 *	1.	Support for the ATAPI overlap protocol.
 *
 *		In order to maximize bus throughput, we currently use the DSC
 *		overlap method which enables ide.c to service requests from the
 *		other device while the tape is busy executing a command. The
 *		DSC overlap method involves polling the tape's status register
 *		for the DSC bit, and servicing the other device while the tape
 *		isn't ready.
 *
 *		In the current QIC development standard (December 1995),
 *		it is recommended that new tape drives will *in addition* 
 *		implement the ATAPI overlap protocol, which is used for the
 *		same purpose - efficient use of the IDE bus, but is interrupt
 *		driven and thus has much less CPU overhead.
 *
 *		ATAPI overlap is likely to be supported in most new ATAPI
 *		devices, including new ATAPI cdroms, and thus provides us
 *		a method by which we can achieve higher throughput when
 *		sharing a (fast) ATA-2 disk with any (slow) new ATAPI device.
 */

#include <linux/config.h>
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

/*
 *	Main Linux ide driver include file
 *
 *	Automatically includes our include file - ide-tape.h.
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
#define	IDETAPE_LOAD_UNLOAD_CMD		0x1b
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
 *	Some defines for the LOAD UNLOAD command
 */
 
#define	IDETAPE_LU_LOAD_MASK		1
#define	IDETAPE_LU_RETENSION_MASK	2
#define	IDETAPE_LU_EOT_MASK		4

/*
 *	Our ioctls - We will use 0x034n and 0x035n
 *
 *	Nothing special meanwhile.
 *	mtio.h MTIOCTOP compatible commands are supported on the character
 *	device interface.
 */

/*
 *	Special requests for our block device strategy routine.
 *
 *	In order to service a character device command, we add special
 *	requests to the tail of our block device request queue and wait
 *	for their completion.
 *
 */

#define	IDETAPE_FIRST_REQUEST			90

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

#define IDETAPE_LAST_REQUEST			93

/*
 *	A macro which can be used to check if a we support a given
 *	request command.
 */

#define IDETAPE_REQUEST_CMD(cmd) 	((cmd >= IDETAPE_FIRST_REQUEST) && (cmd <= IDETAPE_LAST_REQUEST))

/*
 *	We are now able to postpone an idetape request in the stage
 *	where it is polling for DSC and service requests from the other
 *	ide device meanwhile.
 */

#define	IDETAPE_RQ_POSTPONED		0x1234

/*
 *	Error codes which are returned in rq->errors to the higher part
 *	of the driver.
 */

#define	IDETAPE_RQ_ERROR_GENERAL	1 
#define	IDETAPE_RQ_ERROR_FILEMARK	2
#define	IDETAPE_RQ_ERROR_EOD		3

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
		unsigned check		:1;	/* Error occurred */
		unsigned idx		:1;	/* Reserved */
		unsigned corr		:1;	/* Correctable error occurred */
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
	unsigned bop		:1;	/* Beginning Of Partition */
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
	unsigned reserved_1	:8;	/* Segment Number - Reserved */
	unsigned sense_key	:4;	/* Sense Key */
	unsigned reserved2_4	:1;	/* Reserved */
	unsigned ili		:1;	/* Incorrect Length Indicator */
	unsigned eom		:1;	/* End Of Medium */
	unsigned filemark 	:1;	/* Filemark */

	/*
	 *	We can't use a 32 bit variable, since it will be re-aligned
	 *	by GCC, as we are not on a 32 bit boundary.
	 */

	byte information1;		/* MSB - Information - Command specific */
	byte information2;
	byte information3;
	byte information4;		/* LSB */
	byte asl;			/* Additional sense length (n-7) */
	unsigned long command_specific; /* Additional command specific information */
	byte asc;			/* Additional Sense Code */
	byte ascq;			/* Additional Sense Code Qualifier */
	byte replaceable_unit_code;	/* Field Replaceable Unit Code */
	unsigned sk_specific1 	:7;	/* Sense Key Specific */
	unsigned sksv		:1;	/* Sense Key Specific information is valid */
	byte sk_specific2;		/* Sense Key Specific */
	byte sk_specific3;		/* Sense Key Specific */
	byte pad [2];			/* Padding to 20 bytes */
} idetape_request_sense_result_t;

/*
 *	Follows structures which are related to the SELECT SENSE / MODE SENSE
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
void idetape_poll_for_dsc_direct (unsigned long data);
void idetape_put_back_postponed_request (ide_drive_t *drive);
void idetape_media_access_finished (ide_drive_t *drive);

/*
 *	Some more packet command related functions.
 */
 
void idetape_pc_callback (ide_drive_t *drive);
void idetape_retry_pc (ide_drive_t *drive);
void idetape_zero_packet_command (idetape_packet_command_t *pc);
void idetape_queue_pc_head (ide_drive_t *drive,idetape_packet_command_t *pc,struct request *rq);
void idetape_analyze_error (ide_drive_t *drive,idetape_request_sense_result_t *result);

idetape_packet_command_t *idetape_next_pc_storage (ide_drive_t *drive);
struct request *idetape_next_rq_storage (ide_drive_t *drive);

/*
 *	Various packet commands
 */
 
void idetape_create_inquiry_cmd (idetape_packet_command_t *pc);
void idetape_inquiry_callback (ide_drive_t *drive);
void idetape_create_locate_cmd (idetape_packet_command_t *pc,unsigned long block,byte partition);
void idetape_create_rewind_cmd (idetape_packet_command_t *pc);
void idetape_create_write_filemark_cmd (idetape_packet_command_t *pc,int write_filemark);
void idetape_create_load_unload_cmd (idetape_packet_command_t *pc,int cmd);
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

/*
 *	Character device callback functions.
 *
 *	We currently support:
 *
 *		OPEN, RELEASE, READ, WRITE and IOCTL.
 */

int idetape_chrdev_read (struct inode *inode, struct file *file, char *buf, int count);
int idetape_chrdev_write (struct inode *inode, struct file *file, const char *buf, int count);
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

/*
 *	idetape_space_over_filemarks handles the MTFSF, MTFSFM, ... mtio.h
 *	commands.
 */
 
int idetape_space_over_filemarks (ide_drive_t *drive,short mt_op,int mt_count);

/*
 *	idetape_add_chrdev_read_request is called from idetape_chrdev_read
 *	to service a character device read request and add read-ahead
 *	requests to our pipeline.
 */
 
int idetape_add_chrdev_read_request (ide_drive_t *drive,int blocks,char *buffer);

/*
 *	idetape_add_chrdev_write_request adds a character device write
 *	request to the pipeline.
 */
 
int idetape_add_chrdev_write_request (ide_drive_t *drive,int blocks,char *buffer);

/*
 *	idetape_queue_rw_tail will add a command to the tail of the device
 *	request queue and wait for it to finish. This is used when we
 *	can not allocate pipeline stages (or in non-pipelined mode).
 */
 
int idetape_queue_rw_tail (ide_drive_t *drive,int cmd,int blocks,char *buffer);

/*
 *	Adds a packet command request to the tail of the device request
 *	queue and waits for it to be serviced.
 */
 
int idetape_queue_pc_tail (ide_drive_t *drive,idetape_packet_command_t *pc);

int idetape_position_tape (ide_drive_t *drive,unsigned long block);
int idetape_rewind_tape (ide_drive_t *drive);
int idetape_flush_tape_buffers (ide_drive_t *drive);

/*
 *	Used to get device information
 */

void idetape_get_mode_sense_results (ide_drive_t *drive);

/*
 *	General utility functions
 */
 
unsigned long idetape_swap_long (unsigned long temp);
unsigned short idetape_swap_short (unsigned short temp);

#define IDETAPE_MIN(a,b)	((a)<(b) ? (a):(b))

/*
 *	Pipeline related functions
 */

idetape_pipeline_stage_t *idetape_kmalloc_stage (ide_drive_t *drive);
void idetape_kfree_stage (idetape_pipeline_stage_t *stage);
void idetape_copy_buffer_from_stage (idetape_pipeline_stage_t *stage,char *buffer);
void idetape_copy_buffer_to_stage (idetape_pipeline_stage_t *stage,char *buffer);
void idetape_increase_max_pipeline_stages (ide_drive_t *drive);
void idetape_add_stage_tail (ide_drive_t *drive,idetape_pipeline_stage_t *stage);
void idetape_remove_stage_head (ide_drive_t *drive);
void idetape_active_next_stage (ide_drive_t *drive);
void idetape_wait_for_pipeline (ide_drive_t *drive);
void idetape_discard_read_pipeline (ide_drive_t *drive);
void idetape_empty_write_pipeline (ide_drive_t *drive);
void idetape_insert_pipeline_into_queue (ide_drive_t *drive);

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
#if IDETAPE_DEBUG_LOG
	unsigned short mask,i;
#endif /* IDETAPE_DEBUG_LOG */
		
	ptr=(unsigned short *) &gcw;
	*ptr=id->config;

#if IDETAPE_DEBUG_LOG
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

		printk ("Manufacturer\'s Recommended Multi-word cycle: ");
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
#endif /* IDETAPE_DEBUG_LOG */

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
		printk ("ide-tape: are still not supported by the driver\n");support=0;
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
	idetape_tape_t *tape=&(drive->tape);
	unsigned int allocation_length;
	double service_time,nr_units;
		
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Reached idetape_setup\n");
#endif /* IDETAPE_DEBUG_LOG */	
	
	drive->ready_stat = 0;			/* With an ATAPI device, we can issue packet commands */
						/* regardless of the state of DRDY */
	HWIF(drive)->tape_drive=drive;

	tape->block_address=0;			
	tape->block_address_valid=0;
	tape->pc_stack_index=0;
	tape->failed_pc=NULL;
	tape->postponed_rq=NULL;
	tape->busy=0;
	tape->active_data_request=NULL;
	tape->current_number_of_stages=0;
	tape->first_stage=tape->next_stage=tape->last_stage=NULL;
	tape->error_in_pipeline_stage=0;
	tape->request_status=0;
	tape->chrdev_direction=idetape_direction_none;
	tape->reset_issued=0;
	tape->pc=&(tape->pc_stack [0]);
	
#if IDETAPE_PIPELINE
	tape->max_number_of_stages=IDETAPE_MIN_PIPELINE_STAGES;
	printk ("ide-tape: Operating in pipelined (fast and tricky) operation mode.\n");
#else
	tape->max_number_of_stages=0;
	printk ("ide-tape: Operating in non-pipelined (slow and safe) operation mode.\n");
#endif /* IDETAPE_PIPELINE */

	idetape_get_mode_sense_results (drive);

	tape->data_buffer_size=tape->capabilities.ctl*tape->tape_block_size;

	allocation_length=tape->data_buffer_size;
	if (tape->data_buffer_size % IDETAPE_ALLOCATION_BLOCK)
		allocation_length+=IDETAPE_ALLOCATION_BLOCK;
	
#if IDETAPE_MINIMIZE_IDLE_MEMORY_USAGE
	tape->data_buffer=tape->merge_buffer=NULL;
#else
	tape->data_buffer=kmalloc (allocation_length,GFP_KERNEL);
	tape->merge_buffer=kmalloc (allocation_length,GFP_KERNEL);
	if (tape->data_buffer == NULL || tape->merge_buffer == NULL) {
		printk ("ide-tape: FATAL - Can not allocate 2 buffers of %d bytes each\n",allocation_length);
		printk ("ide-tape: Aborting character device installation\n");
		idetape_drive_already_found=0;
		unregister_chrdev (idetape_chrdev.major,idetape_chrdev.name);
		return;
	}
#endif /* IDETAPE_MINIMIZE_IDLE_MEMORY_USAGE */

	tape->merge_buffer_size=tape->merge_buffer_offset=0;
	
#if IDETAPE_ANTICIPATE_READ_WRITE_DSC

	/*
	 *	Cleverly select the DSC read/write polling frequency, based
	 *	on the tape's speed, its recommended transfer unit, its
	 *	internal buffer size and our operation mode.
	 *
	 *	In the pipelined operation mode we aim for "catching" the
	 *	tape when its internal buffer is about 50% full. This will
	 *	dramatically reduce our polling frequency and will also
	 *	leave enough time for the ongoing request of the other device
	 *	to complete before the buffer is completely empty. We will
	 *	then completely refill the buffer with requests from our
	 *	internal pipeline.
	 *
	 *	When operating in the non-pipelined operation mode, we
	 *	can't allow ourself this luxury. Instead, we will try to take
	 *	full advantage of the internal tape buffer by waiting only
	 *	for one request to complete. This will increase our load
	 *	on linux but will usually still fail to keep the tape
	 *	constantly streaming.
	 */

	service_time=((double) tape->data_buffer_size/1024.0)/((double) tape->capabilities.speed*(1000.0/1024.0));
	nr_units=(double) tape->capabilities.buffer_size*512.0/(double) tape->data_buffer_size;

	if (tape->max_number_of_stages)	
		tape->best_dsc_rw_frequency=(unsigned long) (0.5*nr_units*service_time*HZ);
	else		
		tape->best_dsc_rw_frequency=(unsigned long) (service_time*HZ);
	
	/*
	 *	Ensure that the number we got makes sense.
	 */

	if (tape->best_dsc_rw_frequency > IDETAPE_DSC_READ_WRITE_LOWEST_FREQUENCY) {
		printk ("ide-tape: Although the recommended polling period is %lu jiffies, \n",tape->best_dsc_rw_frequency);
		printk ("ide-tape: we will use %u jiffies\n",IDETAPE_DSC_READ_WRITE_LOWEST_FREQUENCY);
		printk ("ide-tape: (It may well be that we are wrong here)\n");
		tape->best_dsc_rw_frequency = IDETAPE_DSC_READ_WRITE_LOWEST_FREQUENCY;
	}

	if (tape->best_dsc_rw_frequency < IDETAPE_DSC_READ_WRITE_FALLBACK_FREQUENCY) {
		printk ("ide-tape: Although the recommended polling period is %lu jiffies, \n",tape->best_dsc_rw_frequency);
		printk ("ide-tape: we will use %u jiffies\n",IDETAPE_DSC_READ_WRITE_FALLBACK_FREQUENCY);
		tape->best_dsc_rw_frequency = IDETAPE_DSC_READ_WRITE_FALLBACK_FREQUENCY;
	}

#else
	tape->best_dsc_rw_frequency=IDETAPE_DSC_READ_WRITE_FALLBACK_FREQUENCY;
#endif /* IDETAPE_ANTICIPATE_READ_WRITE_DSC */

	printk ("ide-tape: Tape speed - %d KBps. Recommended transfer unit - %d bytes.\n",tape->capabilities.speed,tape->data_buffer_size);

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
		tape->capabilities.ctl=52;
		tape->capabilities.speed=450;
		tape->capabilities.buffer_size=6*52;
		return;
	}

#if IDETAPE_DEBUG_LOG
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
#endif /* IDETAPE_DEBUG_LOG */
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
 *		delayed process. In case of a successful initiation of a
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
	int dma_ok=0;

	tape=&(drive->tape);
	        
#if IDETAPE_DEBUG_BUGS
	if (tape->pc->c[0] == IDETAPE_REQUEST_SENSE_CMD && pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
		printk ("ide-tape: possible ide-tape.c bug - Two request sense in serial were issued\n");
	}
#endif /* IDETAPE_DEBUG_BUGS */

	if (tape->failed_pc == NULL && pc->c[0] != IDETAPE_REQUEST_SENSE_CMD)
		tape->failed_pc=pc;
	tape->pc=pc;							/* Set the current packet command */

	if (pc->retries > IDETAPE_MAX_PC_RETRIES || pc->abort) {

		/*
		 *	We will "abort" retrying a packet command in case
		 *	a legitimate error code was received (crossing a
		 *	filemark, or DMA error in the end of media, for
		 *	example).
		 */

		if (!pc->abort) {
			printk ("ide-tape: %s: I/O error, ",drive->name);
			printk ("pc = %x, key = %x, asc = %x, ascq = %x\n",pc->c[0],tape->sense_key,tape->asc,tape->ascq);
			printk ("ide-tape: Maximum retries reached - Giving up\n");
			pc->error=1;					/* Giving up */
		}
		tape->failed_pc=NULL;
#if IDETAPE_DEBUG_BUGS
		if (pc->callback==NULL)
			printk ("ide-tape: ide-tape bug - Callback function not set !\n");
		else
#endif /* IDETAPE_DEBUG_BUGS */
			(*pc->callback)(drive);
		return;
	}

#if IDETAPE_DEBUG_LOG
	printk ("Retry number - %d\n",pc->retries);
#endif /* IDETAPE_DEBUG_LOG */

	pc->retries++;

/*
 *	We no longer call ide_wait_stat to wait for the drive to be ready,
 *	as ide.c already does this for us in do_request.
 */
 
	pc->actually_transferred=0;					/* We haven't transferred any data yet */
	pc->current_position=pc->buffer;	
	bcount.all=pc->request_transfer;				/* Request to transfer the entire buffer at once */

#ifdef CONFIG_BLK_DEV_TRITON
	if (pc->dma_error) {
		printk ("ide-tape: DMA disabled, reverting to PIO\n");
		drive->using_dma=0;
		pc->dma_error=0;
	}
	if (pc->request_transfer && pc->dma_recommended && drive->using_dma) {
		dma_ok=!(HWIF(drive)->dmaproc(pc->writing ? ide_dma_write : ide_dma_read, drive));
	}		
#endif /* CONFIG_BLK_DEV_TRITON */

	OUT_BYTE (drive->ctl,IDETAPE_CONTROL_REG);
	OUT_BYTE (dma_ok ? 1:0,IDETAPE_FEATURES_REG);			/* Use PIO/DMA */
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
		ide_do_reset (drive);
		return;		
	}
		
	ide_output_data (drive,pc->c,12/4);			/* Send the actual packet */
#ifdef CONFIG_BLK_DEV_TRITON
	if ((pc->dma_in_progress=dma_ok)) {			/* Begin DMA, if necessary */
		pc->dma_error=0;
		(void) (HWIF(drive)->dmaproc(ide_dma_begin, drive));
	}
#endif /* CONFIG_BLK_DEV_TRITON */
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
	idetape_tape_t *tape=&(drive->tape);
	idetape_status_reg_t status;
	idetape_bcount_reg_t bcount;
	idetape_ireason_reg_t ireason;
	idetape_packet_command_t *pc=tape->pc;
	unsigned long temp;

#ifdef CONFIG_BLK_DEV_TRITON
	if (pc->dma_in_progress) {
		if ((pc->dma_error=HWIF(drive)->dmaproc(ide_dma_status_bad, drive)))
			/*
			 *	We will currently correct the following in
			 *	idetape_analyze_error.
			 */
			pc->actually_transferred=HWIF(drive)->dmaproc(ide_dma_transferred, drive);
		else
			pc->actually_transferred=pc->request_transfer;
		(void) (HWIF(drive)->dmaproc(ide_dma_abort, drive));	/* End DMA */
#if IDETAPE_DEBUG_LOG
		printk ("ide-tape: DMA finished\n");
#endif /* IDETAPE_DEBUG_LOG */
	}
#endif /* CONFIG_BLK_DEV_TRITON */

	status.all=IN_BYTE (IDETAPE_STATUS_REG);		/* Clear the interrupt */

#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Reached idetape_pc_intr interrupt handler\n");
#endif /* IDETAPE_DEBUG_LOG */	

	if (!status.b.drq) {					/* No more interrupts */
#if IDETAPE_DEBUG_LOG
		printk ("Packet command completed\n");
		printk ("Total bytes transferred: %lu\n",pc->actually_transferred);
#endif /* IDETAPE_DEBUG_LOG */
		pc->dma_in_progress=0;
						
		sti ();

		if (status.b.check || pc->dma_error) {			/* Error detected */
#if IDETAPE_DEBUG_LOG
	/*
	 *	Without debugging, we only log an error if we decided to
	 *	give up retrying.
	 */
			printk ("ide-tape: %s: I/O error, ",drive->name);
#endif /* IDETAPE_DEBUG_LOG */
			if (pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
				printk ("ide-tape: I/O error in request sense command\n");
				ide_do_reset (drive);
				return;
			}			
						
			idetape_retry_pc (drive);			/* Retry operation */
			return;
		}
		pc->error=0;
		if (pc->wait_for_dsc && !status.b.dsc) {				/* Media access command */
			tape->dsc_polling_frequency=IDETAPE_DSC_FAST_MEDIA_ACCESS_FREQUENCY;
			idetape_postpone_request (drive);		/* Allow ide.c to handle other requests */
			return;
		}
		if (tape->failed_pc == pc)
			tape->failed_pc=NULL;
#if IDETAPE_DEBUG_BUGS
		if (pc->callback==NULL)			
			printk ("ide-tape: ide-tape bug - Callback function not set !\n");
		else
#endif /* IDETAPE_DEBUG_BUGS */
			(*pc->callback)(drive);			/* Command finished - Call the callback function */
		return;
	}
#ifdef CONFIG_BLK_DEV_TRITON
	if (pc->dma_in_progress) {
		pc->dma_in_progress=0;
		printk ("ide-tape: The tape wants to issue more interrupts in DMA mode\n");
		printk ("ide-tape: DMA disabled, reverting to PIO\n");
		drive->using_dma=0;
		ide_do_reset (drive);
		return;
	}
#endif /* CONFIG_BLK_DEV_TRITON */
	bcount.b.high=IN_BYTE (IDETAPE_BCOUNTH_REG);			/* Get the number of bytes to transfer */
	bcount.b.low=IN_BYTE (IDETAPE_BCOUNTL_REG);			/* on this interrupt */
	ireason.all=IN_BYTE (IDETAPE_IREASON_REG);			/* Read the interrupt reason register */

	if (ireason.b.cod) {
		printk ("ide-tape: CoD != 0 in idetape_pc_intr\n");
		ide_do_reset (drive);
		return;
	}
	if (ireason.b.io != !(pc->writing)) {			/* Hopefully, we will never get here */
		printk ("ide-tape: We wanted to %s, ",pc->writing ? "Write":"Read");
		printk ("but the tape wants us to %s !\n",ireason.b.io ? "Read":"Write");
		ide_do_reset (drive);
		return;
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
#if IDETAPE_DEBUG_BUGS	
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
#endif /* IDETAPE_DEBUG_BUGS */
	if (pc->writing)
		idetape_output_data (drive,pc->current_position,bcount.all);	/* Write the current buffer */
	else
		idetape_input_data (drive,pc->current_position,bcount.all);	/* Read the current buffer */
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: %s %d bytes\n",pc->writing ? "Wrote":"Received",bcount.all);
#endif /* IDETAPE_DEBUG_LOG */
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
	idetape_tape_t *tape=&(drive->tape);
	struct request *rq;
	idetape_status_reg_t status;
	
#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_postpone_request\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (tape->postponed_rq != NULL)
		printk ("ide-tape.c bug - postponed_rq not NULL in idetape_postpone_request\n");
#endif /* IDETAPE_DEBUG_BUGS */

	tape->dsc_timer.expires=jiffies + tape->dsc_polling_frequency;	/* Set timer to poll for */
	tape->dsc_timeout=jiffies+IDETAPE_DSC_TIMEOUT;			/* actual completion */
	tape->dsc_timer.data=(unsigned long) drive;
	tape->dsc_timer.function=&idetape_poll_for_dsc;
	init_timer (&(tape->dsc_timer));

	/*
	 * Remove current request from the request queue:
	 */

	tape->postponed_rq = rq = HWGROUP(drive)->rq;
	rq->rq_status = IDETAPE_RQ_POSTPONED;	
	blk_dev[MAJOR(rq->rq_dev)].current_request = rq->next;
	HWGROUP(drive)->rq = NULL;

	/*
	 *	Check the status again - Maybe we can save one polling period.
	 */
	 
	status.all=IN_BYTE (IDETAPE_STATUS_REG);
	tape->last_status=status.all;
	tape->request_status=1;	
	
	tape->dsc_polling_start=jiffies;
	add_timer(&(tape->dsc_timer));		/* Activate the polling timer */
}

/*
 *	idetape_poll_for_dsc_direct is called from idetape_poll_for_dsc
 *	to handle the case in which we can safely communicate with the tape
 *	(since no other request for this hwgroup is active).
 */
 
void idetape_poll_for_dsc_direct (unsigned long data)

{
	ide_drive_t *drive=(ide_drive_t *) data;
	idetape_tape_t *tape=&(drive->tape);
	idetape_status_reg_t status;

#if IDETAPE_DEBUG_LOG
	printk ("%s: idetape_poll_for_dsc_direct called\n",drive->name);
#endif /* IDETAPE_DEBUG_LOG */	

	OUT_BYTE(drive->select.all,IDE_SELECT_REG);
	status.all=IN_BYTE (IDETAPE_STATUS_REG);
	
	if (status.b.dsc) {					/* DSC received */
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
	add_timer(&(tape->dsc_timer));
	return;
}

/*
 *	idetape_poll_for_dsc gets invoked by a timer (which was set
 *	by idetape_postpone_request) to poll for the DSC bit
 *	in the status register.
 *
 *	We take care not to perform any tape access if the driver is
 *	accessing the other device. We will instead ask ide.c to sample
 *	the tape status register on our behalf in the next call to do_request,
 *	at the point in which the other device is idle, or assume that
 *	DSC was received even though we won't verify it (but when we assume
 *	that, it will usually have a solid basis).
 *
 *	The use of cli () below is a must, as we inspect and change
 *	the device request list while another request is active.
 */
 
void idetape_poll_for_dsc (unsigned long data)

{
	ide_drive_t *drive=(ide_drive_t *) data;
	unsigned int major = HWIF(drive)->major;
	idetape_tape_t *tape=&(drive->tape);
	struct blk_dev_struct *bdev = &blk_dev[major];
	struct request *next_rq;
	unsigned long flags;
	idetape_status_reg_t status;

#if IDETAPE_DEBUG_LOG
	printk ("%s: idetape_poll_for_dsc called\n",drive->name);
#endif /* IDETAPE_DEBUG_LOG */	

	save_flags (flags);cli ();

	/*
	 *	Check if the other device is idle. If there are no requests,
	 *	we can safely access the tape.
	 */

	if (HWGROUP (drive)->rq == NULL) {
		sti ();
		idetape_poll_for_dsc_direct (data);
		return;
	}

	/*
	 *	If DSC was received, re-insert our postponed request into
	 *	the request queue (using ide_next).
	 */

	status.all=tape->last_status;

	if (status.b.dsc) {					/* DSC received */
		tape->dsc_received=1;
		idetape_put_back_postponed_request (drive);
		del_timer (&(tape->dsc_timer));
		restore_flags (flags);
		return;
	}

	/*
	 *	At this point, DSC may have been received, but we can't
	 *	check it. We now have two options:
	 *
	 *		1.	The "simple" method - We can continue polling
	 *			until we know the value of DSC.
	 *
	 *	but we also have a more clever option :-)
	 *
	 *		2.	We can sometimes more or less anticipate in
	 *			advance how much time it will take for
	 *			the tape to perform the request. This is the
	 *			place to take advantage of this !
	 *
	 *			We can assume that DSC was received, put
	 *			back our request, and hope that we will have
	 *			a "cache hit". This will only work when
	 *			we haven't initiated the packet command yet,
	 *			but this is the common read/write case. As
	 *			for the slower media access commands, fallback
	 *			to method 1 above.
	 *
	 *	When using method 2, we can also take advantage of the
	 *	knowledge of the tape's internal buffer size - We can
	 *	precalculate the time it will take for the tape to complete
	 *	servicing not only one request, but rather, say, 50% of its
	 *	internal buffer. The polling period will then be much larger,
	 *	decreasing our load on Linux, and we will also call
	 *	idetape_postpone_request less often, as there will usually
	 *	be more room in the internal tape buffer while we are in
	 *	idetape_do_request.
	 *
	 *	For this method to work well, the ongoing request of the
	 *	other device should be serviced by the time the tape is
	 *	still working on its remaining 50% internal buffer. This
	 *	will usually happen when the other device is much faster
	 *	than the tape.
	 */

#if IDETAPE_ANTICIPATE_READ_WRITE_DSC

	/*
	 *	Method 2.
	 *
	 *	There is a high chance that DSC was received, even though
	 *	we couldn't verify it. Let's hope that it's a "cache hit"
	 *	rather than a "cache miss". Someday I will probably add a
	 *	feedback loop around the number of "cache hits" which will
	 *	fine-tune the polling period.
	 */
	 
	if (tape->postponed_rq->cmd != IDETAPE_PACKET_COMMAND_REQUEST_TYPE1) {

		/*
		 *	We can use this method only when the packet command
		 *	was still not initiated.
		 */
		 
		idetape_put_back_postponed_request (drive);
		del_timer (&(tape->dsc_timer));
		restore_flags (flags);
		return;
	}
#endif /* IDETAPE_ANTICIPATE_READ_WRITE_DSC */

	/*
	 *	Fallback to method 1.
	 */

	next_rq=bdev->current_request;
	if (next_rq == HWGROUP (drive)->rq)
		next_rq=next_rq->next;

	if (next_rq == NULL) {

		/*
		 *	There will not be another request after the currently
		 *	ongoing request, so ide.c won't be able to sample
		 *	the status register on our behalf in do_request.
		 *
		 *	In case we are waiting for DSC before the packet
		 *	command was initiated, we will put back our postponed
		 *	request and have another look at the status register
		 *	in idetape_do_request, as done in method 2 above.
		 *
		 *	In case we already initiated the command, we can't
		 *	put it back, but it is anyway a slow media access
		 *	command. We will just give up and poll again until
		 *	we are lucky.
		 */

		if (tape->postponed_rq->cmd == IDETAPE_PACKET_COMMAND_REQUEST_TYPE1) {

			/*
			 *	Media access command - Poll again.
			 *
			 *	We set tape->request_status to 1, just in case
			 *	other requests are added while we are waiting.
			 */
			 
			tape->request_status=1;
			restore_flags (flags);
			tape->dsc_timer.expires = jiffies + tape->dsc_polling_frequency;
			add_timer(&(tape->dsc_timer));
			return;
		}
		
		/*
		 *	The packet command hasn't been sent to the tape yet -
		 *	We can safely put back the request and have another
		 *	look at the status register in idetape_do_request.
		 */

		idetape_put_back_postponed_request (drive);
		del_timer (&(tape->dsc_timer));
		restore_flags (flags);
		return;
	}

	/*
	 *	There will be another request after the current request.
	 *
	 *	Request ide.c to sample for us the tape's status register
	 *	before the next request.
	 */

	tape->request_status=1;
	restore_flags (flags);

	if (jiffies > tape->dsc_timeout) 	{ 		/* Timeout */
		tape->dsc_received=0;
		/* ??? */
		idetape_put_back_postponed_request (drive);
		del_timer (&(tape->dsc_timer));
		restore_flags (flags);
		return;
	}

	/* Poll again */
	
	if (jiffies - tape->dsc_polling_start > IDETAPE_FAST_SLOW_THRESHOLD)
		tape->dsc_timer.expires = jiffies + IDETAPE_DSC_SLOW_MEDIA_ACCESS_FREQUENCY;
	else
		tape->dsc_timer.expires = jiffies + tape->dsc_polling_frequency;
	add_timer(&(tape->dsc_timer));
	return;
}

/*
 *	idetape_put_back_postponed_request gets called when we decided to
 *	stop polling for DSC and continue servicing our postponed request.
 */

void idetape_put_back_postponed_request (ide_drive_t *drive)

{
	idetape_tape_t *tape = &(drive->tape);

#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Putting back postponed request\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (tape->postponed_rq == NULL) {
		printk ("tape->postponed_rq is NULL in put_back_postponed_request\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	(void) ide_do_drive_cmd (drive, tape->postponed_rq, ide_next);

	/*
	 * 	Note that the procedure done here is different than the method
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
#if IDETAPE_DEBUG_LOG
		printk ("DSC received\n");
#endif /* IDETAPE_DEBUG_LOG */
		if (status.b.check) {					/* Error detected */
			printk ("ide-tape: %s: I/O error, ",drive->name);
			idetape_retry_pc (drive);			/* Retry operation */
			return;
		}
		pc->error=0;
		if (tape->failed_pc == pc)
			tape->failed_pc=NULL;
#if IDETAPE_DEBUG_BUGS
		if (pc->callback==NULL)
			printk ("ide-tape: ide-tape bug - Callback function not set !\n");
		else
#endif /* IDETAPE_DEBUG_BUGS */
			(*pc->callback)(drive);

		return;
	}
	else {
		printk ("ide-tape: %s: DSC timeout.\n",drive->name);
		/* ??? */
		pc->error=1;
		tape->failed_pc=NULL;
#if IDETAPE_DEBUG_BUGS
		if (pc->callback==NULL)
			printk ("ide-tape: ide-tape bug - Callback function not set !\n");
		else
#endif /* IDETAPE_DEBUG_BUGS */
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
	
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Reached idetape_pc_callback\n");
#endif /* IDETAPE_DEBUG_LOG */
	if (!tape->pc->error) {
#if IDETAPE_DEBUG_LOG
		printk ("Request completed\n");
#endif /* IDETAPE_DEBUG_LOG */
		idetape_end_request (1,HWGROUP (drive));
	}
	else {
		idetape_end_request (0,HWGROUP (drive));
	}
	return;
}


void idetape_read_callback (ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);
	struct request *rq=HWGROUP(drive)->rq;
	int blocks_read=tape->pc->actually_transferred/tape->tape_block_size;

#if IDETAPE_DEBUG_LOG	
	printk ("ide-tape: Reached idetape_read_callback\n");
#endif /* IDETAPE_DEBUG_LOG */

	tape->block_address+=blocks_read;
	rq->current_nr_sectors-=blocks_read;	

	if (!tape->pc->error)
		idetape_end_request (1,HWGROUP (drive));
	else {
		rq->errors=tape->pc->error;
		switch (rq->errors) {
			case IDETAPE_RQ_ERROR_FILEMARK:
			case IDETAPE_RQ_ERROR_EOD:
				break;
		}
		idetape_end_request (0,HWGROUP (drive));
	}
	return;
}

void idetape_write_callback (ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);
	struct request *rq=HWGROUP(drive)->rq;
	int blocks_written=tape->pc->actually_transferred/tape->tape_block_size;
		
#if IDETAPE_DEBUG_LOG	
	printk ("ide-tape: Reached idetape_write_callback\n");
#endif /* IDETAPE_DEBUG_LOG */

	tape->block_address+=blocks_written;
	rq->current_nr_sectors-=blocks_written;

	if (!tape->pc->error)
		idetape_end_request (1,HWGROUP (drive));
	else {
		rq->errors=tape->pc->error;
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
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating INQUIRY packet command\n");
#endif /* IDETAPE_DEBUG_LOG */	
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
	ide_fixstring (result->vendor_id,8,0);
	ide_fixstring (result->product_id,16,0);
	ide_fixstring (result->revision_level,4,0);

	if (result->response_format != 2) {
		printk ("The INQUIRY Data Format is unknown to us !\n");
		printk ("Assuming QIC-157C format.\n");
	}

#if IDETAPE_DEBUG_LOG
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
#endif /* IDETAPE_DEBUG_LOG */

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
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating REQUEST SENSE packet command\n");
#endif /* IDETAPE_DEBUG_LOG */	
	pc->request_transfer=18;
	pc->callback=&idetape_request_sense_callback;
	pc->writing=0;
	
	idetape_zero_packet_command (pc);	
	pc->c[0]=IDETAPE_REQUEST_SENSE_CMD;
	pc->c[4]=255;
}

void idetape_request_sense_callback (ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);

#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Reached idetape_request_sense_callback\n");
#endif /* IDETAPE_DEBUG_LOG */
	if (!tape->pc->error) {
#if IDETAPE_DEBUG_LOG
		printk ("Request completed\n");
#endif /* IDETAPE_DEBUG_LOG */
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
	idetape_tape_t *tape=&(drive->tape);
	idetape_packet_command_t *pc=tape->failed_pc;
		
	tape->sense_key=result->sense_key;
	tape->asc=result->asc;
	tape->ascq=result->ascq;
	
#if IDETAPE_DEBUG_LOG	
	/*
	 *	Without debugging, we only log an error if we decided to
	 *	give up retrying.
	 */
	printk ("ide-tape: pc = %x, sense key = %x, asc = %x, ascq = %x\n",pc->c[0],result->sense_key,result->asc,result->ascq);
#endif /* IDETAPE_DEBUG_LOG */

	if (pc->c[0] == IDETAPE_READ_CMD) {
		if (result->filemark) {
			pc->error=IDETAPE_RQ_ERROR_FILEMARK;
			pc->abort=1;
		}
	}

	if (pc->c[0] == IDETAPE_READ_CMD || pc->c[0] == IDETAPE_WRITE_CMD) {
		if (result->sense_key == 8) {
			pc->error=IDETAPE_RQ_ERROR_EOD;
			pc->abort=1;
		}
	}

#if 1
#ifdef CONFIG_BLK_DEV_TRITON

	/*
	 *	Correct pc->actually_transferred by asking the tape.
	 */

	if (pc->dma_error && pc->abort) {
		unsigned long *long_ptr=(unsigned long *) &(result->information1);
		pc->actually_transferred=pc->request_transfer-tape->tape_block_size*idetape_swap_long (*long_ptr);
	}		
#endif /* CONFIG_BLK_DEV_TRITON */
#endif
}

void idetape_create_test_unit_ready_cmd (idetape_packet_command_t *pc)

{
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating TEST UNIT READY packet command\n");
#endif /* IDETAPE_DEBUG_LOG */	
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

#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating LOCATE packet command\n");
#endif /* IDETAPE_DEBUG_LOG */
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
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating REWIND packet command\n");
#endif /* IDETAPE_DEBUG_LOG */
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
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating MODE SENSE packet command - Page %d\n",page_code);
#endif /* IDETAPE_DEBUG_LOG */

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
#if IDETAPE_DEBUG_LOG
	printk ("Creating WRITE FILEMARK packet command\n");
	if (!write_filemark)
		printk ("which will only flush buffered data\n");
#endif /* IDETAPE_DEBUG_LOG */
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

void idetape_create_load_unload_cmd (idetape_packet_command_t *pc,int cmd)

{
#if IDETAPE_DEBUG_LOG
	printk ("Creating LOAD UNLOAD packet command, cmd=%d\n",cmd);
#endif /* IDETAPE_DEBUG_LOG */
	pc->request_transfer=0;
	pc->buffer=NULL;
	pc->current_position=NULL;
	pc->buffer_size=0;
	pc->wait_for_dsc=1;
	pc->callback=&idetape_pc_callback;
	pc->writing=0;
		
	idetape_zero_packet_command (pc);
	pc->c [0]=IDETAPE_LOAD_UNLOAD_CMD;
	pc->c [4]=cmd;
}

void idetape_create_erase_cmd (idetape_packet_command_t *pc)

{

#if IDETAPE_DEBUG_LOG
	printk ("Creating ERASE command\n");
#endif /* IDETAPE_DEBUG_LOG */

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
	
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating READ packet command\n");
#endif /* IDETAPE_DEBUG_LOG */

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

	if (length)
		pc->dma_recommended=1;

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
	
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating SPACE packet command\n");
#endif /* IDETAPE_DEBUG_LOG */

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
	
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating WRITE packet command\n");
#endif /* IDETAPE_DEBUG_LOG */

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

	if (length)
		pc->dma_recommended=1;

	return;
}

void idetape_create_read_position_cmd (idetape_packet_command_t *pc)

{
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Creating READ POSITION packet command\n");
#endif /* IDETAPE_DEBUG_LOG */

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
	
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: Reached idetape_read_position_callback\n");
#endif /* IDETAPE_DEBUG_LOG */

	rq=HWGROUP(drive)->rq;
	
	if (!tape->pc->error) {
		result=(idetape_read_position_result_t *) tape->pc->buffer;
#if IDETAPE_DEBUG_LOG
		printk ("Request completed\n");
		printk ("Dumping the results of the READ POSITION command\n");
		printk ("BOP - %s\n",result->bop ? "Yes":"No");
		printk ("EOP - %s\n",result->eop ? "Yes":"No");
#endif /* IDETAPE_DEBUG_LOG */
		if (result->bpu) {
			printk ("ide-tape: Block location is unknown to the tape\n");
			printk ("Aborting request\n");
			tape->block_address_valid=0;
			idetape_end_request (0,HWGROUP (drive));
		}
		else {
#if IDETAPE_DEBUG_LOG
			printk ("Block Location - %lu\n",idetape_swap_long (result->first_block));
#endif /* IDETAPE_DEBUG_LOG */
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
	
	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;

#if IDETAPE_DEBUG_LOG	
	printk ("ide-tape: Reached idetape_blkdev_ioctl\n");
#endif /* IDETAPE_DEBUG_LOG */
	switch (cmd) {
		default:
			return -EIO;
	}
}

/*
 *	Functions which handle requests.
 */

/*
 *	idetape_end_request is used to end a request.
 */

void idetape_end_request (byte uptodate, ide_hwgroup_t *hwgroup)

{
	ide_drive_t *drive = hwgroup->drive;
	struct request *rq = hwgroup->rq;
	idetape_tape_t *tape = &(drive->tape);
	unsigned int major = HWIF(drive)->major;
	struct blk_dev_struct *bdev = &blk_dev[major];
	int error;

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_end_request\n");
#endif /* IDETAPE_DEBUG_LOG */

	bdev->current_request=rq;			/* Since we may have taken it out */

	if (!rq->errors)				/* In case rq->errors is already set, */
		rq->errors=!uptodate;			/* we won't change it. */
	error=rq->errors;
		
	if (tape->active_data_request == rq) {		/* The request was a pipelined data transfer request */

		if (rq->cmd == IDETAPE_READ_REQUEST) {
#if IDETAPE_DEBUG_BUGS
			if (tape->active_stage == NULL)
				printk ("ide-tape: bug: active_stage is NULL in idetape_end_request\n");
			else				
#endif /* IDETAPE_DEBUG_BUGS */
			idetape_copy_buffer_to_stage (tape->active_stage,tape->data_buffer);
		}

		tape->active_stage=NULL;
		tape->active_data_request=NULL;

		if (rq->cmd == IDETAPE_WRITE_REQUEST) {
			if (rq->errors)
				tape->error_in_pipeline_stage=rq->errors;
			idetape_remove_stage_head (drive);
		}
		
		if (tape->next_stage == NULL) {
			if (!error)
				idetape_increase_max_pipeline_stages (drive);
			ide_end_drive_cmd (drive, 0, 0);
			return;
		}

		idetape_active_next_stage (drive);

		/*
		 *	Insert the next request into the request queue.
		 *
		 *	The choice of using ide_next or ide_end is now left
		 *	to the user.
		 */
		 
#if IDETAPE_LOW_TAPE_PRIORITY
		(void) ide_do_drive_cmd (drive,tape->active_data_request,ide_end);
#else
		(void) ide_do_drive_cmd (drive,tape->active_data_request,ide_next);
#endif /* IDETAPE_LOW_TAPE_PRIORITY */
	}
	ide_end_drive_cmd (drive, 0, 0);
}

/*
 *	idetape_do_request is our request handling function.	
 */

void idetape_do_request (ide_drive_t *drive, struct request *rq, unsigned long block)

{
	idetape_tape_t *tape=&(drive->tape);
	idetape_packet_command_t *pc;
	unsigned int major = HWIF(drive)->major;
	struct blk_dev_struct *bdev = &blk_dev[major];
	idetape_status_reg_t status;

#if IDETAPE_DEBUG_LOG
	printk ("Current request:\n");
	printk ("rq_status: %d, rq_dev: %u, cmd: %d, errors: %d\n",rq->rq_status,(unsigned int) rq->rq_dev,rq->cmd,rq->errors);
	printk ("sector: %ld, nr_sectors: %ld, current_nr_sectors: %ld\n",rq->sector,rq->nr_sectors,rq->current_nr_sectors);
#endif /* IDETAPE_DEBUG_LOG */

	if (!IDETAPE_REQUEST_CMD (rq->cmd)) {

		/*
		 *	We do not support buffer cache originated requests.
		 */

		printk ("ide-tape: Unsupported command in request queue\n");
		printk ("ide-tape: The block device interface should not be used for data transfers.\n");
		printk ("ide-tape: Use the character device interfaces\n");
		printk ("ide-tape: /dev/ht0 and /dev/nht0 instead.\n");
		printk ("ide-tape: (Run linux/drivers/block/MAKEDEV.ide to create them)\n");
		printk ("ide-tape: Aborting request.\n");

		ide_end_request (0,HWGROUP (drive));			/* Let the common code handle it */
		return;
	}

	/*
	 *	This is an important point. We will try to remove our request
	 *	from the block device request queue while we service the
	 *	request. Note that the request must be returned to
	 *	bdev->current_request before the next call to
	 *	ide_end_drive_cmd or ide_do_drive_cmd to conform with the
	 *	normal behavior of the IDE driver, which leaves the active
	 *	request in bdev->current_request during I/O.
	 *
	 *	This will eliminate fragmentation of disk/cdrom requests
	 *	around a tape request, now that we are using ide_next to
	 *	insert pending pipeline requests, since we have only one
	 *	ide-tape.c data request in the device request queue, and
	 *	thus once removed, ll_rw_blk.c will only see requests from
	 *	the other device.
	 *
	 *	The potential fragmentation inefficiency was pointed to me
	 *	by Mark Lord.
	 */
	 
	if (rq->next != NULL && rq->rq_dev != rq->next->rq_dev)
		bdev->current_request=rq->next;

	/* Retry a failed packet command */

	if (tape->failed_pc != NULL && tape->pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
		idetape_issue_packet_command (drive,tape->failed_pc,&idetape_pc_intr);
		return;
	}

	/* Check if we have a postponed request */
	
	if (tape->postponed_rq != NULL) {
#if IDETAPE_DEBUG_BUGS
		if (tape->postponed_rq->rq_status != RQ_ACTIVE || rq != tape->postponed_rq) {
			printk ("ide-tape: ide-tape.c bug - Two DSC requests were queued\n");
			idetape_end_request (0,HWGROUP (drive));
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
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

	status.all=IN_BYTE (IDETAPE_STATUS_REG);

	/*
	 *	After a software reset, the status register is locked. We
	 *	will ignore the DSC value for our very first packet command,
	 *	which will restore DSC operation.
	 */

	if (tape->reset_issued) {
		status.b.dsc=1;
		tape->reset_issued=0;
	}
	
	switch (rq->cmd) {
		case IDETAPE_READ_REQUEST:
			if (!status.b.dsc) {				/* Tape buffer not ready to accept r/w command */
#if IDETAPE_DEBUG_LOG
				printk ("ide-tape: DSC != 1 - Postponing read request\n");
#endif /* IDETAPE_DEBUG_LOG */	
				tape->dsc_polling_frequency=tape->best_dsc_rw_frequency;
				idetape_postpone_request (drive);	/* Allow ide.c to process requests from */
				return;
			}			

			pc=idetape_next_pc_storage (drive);

			idetape_create_read_cmd (pc,rq->current_nr_sectors);
			
			pc->buffer=rq->buffer;
			pc->buffer_size=rq->current_nr_sectors*tape->tape_block_size;
			pc->current_position=rq->buffer;
			pc->request_transfer=rq->current_nr_sectors*tape->tape_block_size;

			idetape_issue_packet_command (drive,pc,&idetape_pc_intr);
			return;
		
		case IDETAPE_WRITE_REQUEST:
			if (!status.b.dsc) {				/* Tape buffer not ready to accept r/w command */
#if IDETAPE_DEBUG_LOG
				printk ("ide-tape: DSC != 1 - Postponing write request\n");
#endif /* IDETAPE_DEBUG_LOG */	
				tape->dsc_polling_frequency=tape->best_dsc_rw_frequency;
				idetape_postpone_request (drive);	/* Allow ide.c to process requests from */
				return;
			}			

			pc=idetape_next_pc_storage (drive);

			idetape_create_write_cmd (pc,rq->current_nr_sectors);
			
			pc->buffer=rq->buffer;
			pc->buffer_size=rq->current_nr_sectors*tape->tape_block_size;
			pc->current_position=rq->buffer;
			pc->request_transfer=rq->current_nr_sectors*tape->tape_block_size;

			idetape_issue_packet_command (drive,pc,&idetape_pc_intr);
			return;
					
		case IDETAPE_PACKET_COMMAND_REQUEST_TYPE1:
		case IDETAPE_PACKET_COMMAND_REQUEST_TYPE2:
/*
 *	This should be unnecessary (postponing of a general packet command),
 *	but I have occasionally missed DSC on a media access command otherwise.
 *	??? Still have to figure it out ...
 */
			if (!status.b.dsc) {				/* Tape buffers are still not ready */
#if IDETAPE_DEBUG_LOG
				printk ("ide-tape: DSC != 1 - Postponing packet command request\n");
#endif /* IDETAPE_DEBUG_LOG */
				rq->cmd=IDETAPE_PACKET_COMMAND_REQUEST_TYPE2;	/* Note that we are waiting for DSC *before* we */
										/* even issued the command */
				tape->dsc_polling_frequency=IDETAPE_DSC_READ_WRITE_FALLBACK_FREQUENCY;
				idetape_postpone_request (drive);	/* Allow ide.c to process requests from */
				return;
			}
			rq->cmd=IDETAPE_PACKET_COMMAND_REQUEST_TYPE1;
			pc=(idetape_packet_command_t *) rq->buffer;
			idetape_issue_packet_command (drive,pc,&idetape_pc_intr);
			return;
#if IDETAPE_DEBUG_BUGS
		default:
			printk ("ide-tape: bug in IDETAPE_REQUEST_CMD macro\n");
			idetape_end_request (0,HWGROUP (drive));
#endif /* IDETAPE_DEBUG_BUGS */
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
	unsigned int major = HWIF(drive)->major;
	struct blk_dev_struct *bdev = &blk_dev[major];

	bdev->current_request=HWGROUP (drive)->rq;		/* Since we may have taken it out */

	ide_init_drive_cmd (rq);
	rq->buffer = (char *) pc;
	rq->cmd = IDETAPE_PACKET_COMMAND_REQUEST_TYPE1;
	(void) ide_do_drive_cmd (drive, rq, ide_preempt);
}

/*
 *	idetape_wait_for_request installs a semaphore in a pending request
 *	and sleeps until it is serviced.
 *
 *	The caller should ensure that the request will not be serviced
 *	before we install the semaphore (usually by disabling interrupts).
 */
 
void idetape_wait_for_request (struct request *rq)

{
	struct semaphore sem = MUTEX_LOCKED;

#if IDETAPE_DEBUG_BUGS
	if (rq == NULL || !IDETAPE_REQUEST_CMD (rq->cmd)) {
		printk ("ide-tape: bug: Trying to sleep on non-valid request\n");
		return;		
	}
#endif /* IDETAPE_DEBUG_BUGS */

	rq->sem=&sem;
	down (&sem);
}

/*
 *	idetape_queue_rw_tail generates a read/write request for the block
 *	device interface and wait for it to be serviced.
 */

int idetape_queue_rw_tail (ide_drive_t *drive,int cmd,int blocks,char *buffer)

{
	idetape_tape_t *tape = &(drive->tape);
	struct request rq;

#if IDETAPE_DEBUG_LOG
	printk ("idetape_queue_rw_tail: cmd=%d\n",cmd);
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (tape->active_data_request != NULL) {
		printk ("ide-tape: bug: the pipeline is active in idetape_queue_rw_tail\n");
		return (0);
	}
#endif /* IDETAPE_DEBUG_BUGS */	

	ide_init_drive_cmd (&rq);
	rq.buffer = buffer;
	rq.cmd = cmd;
	rq.sector = tape->block_address;
	rq.nr_sectors = rq.current_nr_sectors = blocks;
	(void) ide_do_drive_cmd (drive, &rq, ide_wait);

	return (tape->tape_block_size*(blocks-rq.current_nr_sectors));
}

/*
 *	idetape_add_chrdev_read_request handles character device read requests
 *	when operating in the pipelined operation mode.
 */
 
int idetape_add_chrdev_read_request (ide_drive_t *drive,int blocks,char *buffer)

{
	idetape_tape_t *tape = &(drive->tape);
	idetape_pipeline_stage_t *new_stage;
	unsigned long flags;
	struct request rq,*rq_ptr;
	int bytes_read;
	
#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_add_chrdev_read_request\n");
#endif /* IDETAPE_DEBUG_LOG */

	ide_init_drive_cmd (&rq);
	rq.cmd = IDETAPE_READ_REQUEST;
	rq.sector = tape->block_address;
	rq.nr_sectors = rq.current_nr_sectors = blocks;

	if (tape->active_data_request != NULL || tape->current_number_of_stages <= 0.25*tape->max_number_of_stages) {
		new_stage=idetape_kmalloc_stage (drive);
		while (new_stage != NULL) {
			new_stage->rq=rq;
			save_flags (flags);cli ();
			idetape_add_stage_tail (drive,new_stage);
			restore_flags (flags);
			new_stage=idetape_kmalloc_stage (drive);
		}
		if (tape->active_data_request == NULL)
			idetape_insert_pipeline_into_queue (drive);
	}

	if (tape->first_stage == NULL) {

		/*
		 *	Linux is short on memory. Revert to non-pipelined
		 *	operation mode for this request.
		 */
		 
		return (idetape_queue_rw_tail (drive,IDETAPE_READ_REQUEST,blocks,buffer));
	}		
	
	save_flags (flags);cli ();
	if (tape->active_data_request == &(tape->first_stage->rq))
		idetape_wait_for_request (tape->active_data_request);
	restore_flags (flags);

	rq_ptr=&(tape->first_stage->rq);
	bytes_read=tape->tape_block_size*(rq_ptr->nr_sectors-rq_ptr->current_nr_sectors);
	rq_ptr->nr_sectors=rq_ptr->current_nr_sectors=0;
	idetape_copy_buffer_from_stage (tape->first_stage,buffer);
	if (rq_ptr->errors != IDETAPE_RQ_ERROR_FILEMARK)
		idetape_remove_stage_head (drive);
#if IDETAPE_DEBUG_BUGS
	if (bytes_read > blocks*tape->tape_block_size) {
		printk ("ide-tape: bug: trying to return more bytes than requested\n");
		bytes_read=blocks*tape->tape_block_size;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	return (bytes_read);
}

/*
 *	idetape_add_chrdev_write_request tries to add a character device
 *	originated write request to our pipeline. In case we don't succeed,
 *	we revert to non-pipelined operation mode for this request.
 *
 *	1.	Try to allocate a new pipeline stage.
 *	2.	If we can't, wait for more and more requests to be serviced
 *		and try again each time.
 *	3.	If we still can't allocate a stage, fallback to
 *		non-pipelined operation mode for this request.
 */

int idetape_add_chrdev_write_request (ide_drive_t *drive,int blocks,char *buffer)

{
	idetape_tape_t *tape = &(drive->tape);
	idetape_pipeline_stage_t *new_stage;
	unsigned long flags;
	struct request *rq;

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_add_chrdev_write_request\n");
#endif /* IDETAPE_DEBUG_LOG */
	
	
	new_stage=idetape_kmalloc_stage (drive);

	/*
	 *	If we don't have a new stage, wait for more and more requests
	 *	to finish, and try to allocate after each one.
	 *
	 *	Pay special attention to possible race conditions.
	 */

	while (new_stage == NULL) {
		save_flags (flags);cli ();
		if (tape->active_data_request != NULL) {
			idetape_wait_for_request (tape->active_data_request);
			restore_flags (flags);
			new_stage=idetape_kmalloc_stage (drive);
		}
		else {
			/*
			 *	Linux is short on memory. Fallback to
			 *	non-pipelined operation mode for this request.
			 */
	 	 	
			restore_flags (flags);
			return (idetape_queue_rw_tail (drive,IDETAPE_WRITE_REQUEST,blocks,buffer));
		}
	}

	rq=&(new_stage->rq);

	ide_init_drive_cmd (rq);
	rq->cmd = IDETAPE_WRITE_REQUEST;
	rq->sector = tape->block_address;	/* Doesn't actually matter - We always assume sequential access */
	rq->nr_sectors = blocks;
	rq->current_nr_sectors = blocks;

	idetape_copy_buffer_to_stage (new_stage,buffer);

	save_flags (flags);cli ();
	idetape_add_stage_tail (drive,new_stage);
	restore_flags (flags);

	/*
	 *	Check if we are currently servicing requests in the bottom
	 *	part of the driver.
	 *
	 *	If not, wait for the pipeline to be full enough (75%) before
	 *	starting to service requests, so that we will be able to
	 *	keep up with the higher speeds of the tape.
	 */

	if (tape->active_data_request == NULL && tape->current_number_of_stages >= 0.75*tape->max_number_of_stages)
		idetape_insert_pipeline_into_queue (drive);		

	if (tape->error_in_pipeline_stage) {		/* Return a deferred error */
		tape->error_in_pipeline_stage=0;
		return (-EIO);
	}
	
	return (blocks);
}

void idetape_discard_read_pipeline (ide_drive_t *drive)

{
	idetape_tape_t *tape = &(drive->tape);
	unsigned long flags;

#if IDETAPE_DEBUG_BUGS
	if (tape->chrdev_direction != idetape_direction_read) {
		printk ("ide-tape: bug: Trying to discard read pipeline, but we are not reading.\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */

	tape->merge_buffer_size=tape->merge_buffer_offset=0;
	tape->chrdev_direction=idetape_direction_none;
	
	if (tape->first_stage == NULL)
		return;
		
	save_flags (flags);cli ();
	tape->next_stage=NULL;
	if (tape->active_data_request != NULL)
		idetape_wait_for_request (tape->active_data_request);
	restore_flags (flags);

	while (tape->first_stage != NULL)
		idetape_remove_stage_head (drive);

#if IDETAPE_PIPELINE
	tape->max_number_of_stages=IDETAPE_MIN_PIPELINE_STAGES;
#else
	tape->max_number_of_stages=0;
#endif /* IDETAPE_PIPELINE */
}

/*
 *	idetape_wait_for_pipeline will wait until all pending pipeline
 *	requests are serviced. Typically called on device close.
 */
 
void idetape_wait_for_pipeline (ide_drive_t *drive)

{
	idetape_tape_t *tape = &(drive->tape);
	unsigned long flags;

	if (tape->active_data_request == NULL)
		idetape_insert_pipeline_into_queue (drive);		

	save_flags (flags);cli ();
	if (tape->active_data_request == NULL) {
		restore_flags (flags);
		return;
	}
	
	if (tape->last_stage != NULL)
		idetape_wait_for_request (&(tape->last_stage->rq));

	else if (tape->active_data_request != NULL)
		idetape_wait_for_request (tape->active_data_request);
	restore_flags (flags);
}

void idetape_empty_write_pipeline (ide_drive_t *drive)

{
	idetape_tape_t *tape = &(drive->tape);
	int blocks;
	
#if IDETAPE_DEBUG_BUGS
	if (tape->chrdev_direction != idetape_direction_write) {
		printk ("ide-tape: bug: Trying to empty write pipeline, but we are not writing.\n");
		return;
	}
	if (tape->merge_buffer_size > tape->data_buffer_size) {
		printk ("ide-tape: bug: merge_buffer too big\n");
		tape->merge_buffer_size = tape->data_buffer_size;
	}
#endif /* IDETAPE_DEBUG_BUGS */

	if (tape->merge_buffer_size) {
		blocks=tape->merge_buffer_size/tape->tape_block_size;
		if (tape->merge_buffer_size % tape->tape_block_size) {
			blocks++;
			memset (tape->merge_buffer+tape->merge_buffer_size,0,tape->data_buffer_size-tape->merge_buffer_size);
		}
		(void) idetape_add_chrdev_write_request (drive,blocks,tape->merge_buffer);
		tape->merge_buffer_size=0;
	}
	
	idetape_wait_for_pipeline (drive);

	tape->error_in_pipeline_stage=0;
	tape->chrdev_direction=idetape_direction_none;

	/*
	 *	On the next backup, perform the feedback loop again.
	 *	(I don't want to keep sense information between backups,
	 *	 as some systems are constantly on, and the system load
	 *	 can be totally different on the next backup).
	 */

#if IDETAPE_PIPELINE
	tape->max_number_of_stages=IDETAPE_MIN_PIPELINE_STAGES;
#else
	tape->max_number_of_stages=0;
#endif /* IDETAPE_PIPELINE */
#if IDETAPE_DEBUG_BUGS
	if (tape->first_stage != NULL || tape->next_stage != NULL || tape->last_stage != NULL || tape->current_number_of_stages != 0) {
		printk ("ide-tape: ide-tape pipeline bug\n");		
	}
#endif /* IDETAPE_DEBUG_BUGS */
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
	pc->abort=0;
	pc->dma_recommended=0;
	pc->dma_error=0;
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
#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: pc_stack_index=%d\n",tape->pc_stack_index);
#endif /* IDETAPE_DEBUG_LOG */
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

#if IDETAPE_DEBUG_LOG
	printk ("ide-tape: rq_stack_index=%d\n",tape->rq_stack_index);
#endif /* IDETAPE_DEBUG_LOG */
	if (tape->rq_stack_index==IDETAPE_PC_STACK)
		tape->rq_stack_index=0;
	return (&(tape->rq_stack [tape->rq_stack_index++]));
}

/*
 *	Block device interface functions
 *
 *	The block device interface should not be used for data transfers.
 *	However, we still allow opening it so that we can issue general
 *	ide driver configuration ioctl's, such as the interrupt unmask feature.
 */

int idetape_blkdev_open (struct inode *inode, struct file *filp, ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);
	unsigned long flags;
			
	save_flags (flags);cli ();

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_blkdev_open\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->busy) {
		restore_flags (flags);		/* Allowing access only through one */
		return (-EBUSY);		/* one file descriptor */
	}

	tape->busy=1;
	restore_flags (flags);

	return (0);
}

void idetape_blkdev_release (struct inode *inode, struct file *filp, ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);
	unsigned long flags;
			
#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_blkdev_release\n");
#endif /* IDETAPE_DEBUG_LOG */

	save_flags (flags);cli ();
	tape->busy=0;
	restore_flags (flags);

	return;
}

/*
 *	Character device interface functions
 */

/*
 *	Our character device read / write functions.
 *
 *	The tape is optimized to maximize throughput when it is transferring
 *	an integral number of the "continuous transfer limit", which is
 *	a parameter of the specific tape (26 KB on my particular tape).
 *
 *	For best results use an integral number of the tape's parameter
 *	(which is displayed in the driver installation stage and is returned
 *	 by the MTIOCGET ioctl).
 *
 *	As of version 1.3 of the driver, the character device provides an
 *	abstract continuous view of the media - any mix of block sizes (even 1
 *	byte) on the same backup/restore procedure is supported. The driver
 *	will internally convert the requests to the recommended transfer unit,
 *	so that an unmatch between the user's block size to the recommended
 *	size will only result in a (slightly) increased driver overhead, but
 *	will no longer hit performance.
 */

int idetape_chrdev_read (struct inode *inode, struct file *file, char *buf, int count)

{
	ide_drive_t *drive=idetape_chrdev.drive;
	idetape_tape_t *tape=&(drive->tape);
	char *buf_ptr=buf;
	int bytes_read,temp,actually_read=0;

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_chrdev_read\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->chrdev_direction != idetape_direction_read) {		/* Initialize read operation */
		if (tape->chrdev_direction == idetape_direction_write) {
			idetape_empty_write_pipeline (drive);
			idetape_flush_tape_buffers (drive);
		}
		
		/*
		 *	Issue a read 0 command to ensure that DSC handshake
		 *	is switched from completion mode to buffer available
		 *	mode.
		 */
		 
		bytes_read=idetape_queue_rw_tail (drive,IDETAPE_READ_REQUEST,0,tape->merge_buffer);
		if (bytes_read < 0)
			return (bytes_read);

		tape->chrdev_direction=idetape_direction_read;
	}
	
	if (count==0)
		return (0);

	if (tape->merge_buffer_size) {
#if IDETAPE_DEBUG_BUGS
		if (tape->merge_buffer_offset+tape->merge_buffer_size > tape->data_buffer_size) {
			printk ("ide-tape: bug: merge buffer too big\n");
			tape->merge_buffer_offset=0;tape->merge_buffer_size=tape->data_buffer_size-1;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		actually_read=IDETAPE_MIN (tape->merge_buffer_size,count);
		memcpy_tofs (buf_ptr,tape->merge_buffer+tape->merge_buffer_offset,actually_read);
		buf_ptr+=actually_read;tape->merge_buffer_size-=actually_read;
		count-=actually_read;tape->merge_buffer_offset+=actually_read;
	}

	while (count >= tape->data_buffer_size) {
		bytes_read=idetape_add_chrdev_read_request (drive,tape->capabilities.ctl,tape->merge_buffer);
		if (bytes_read <= 0)
			return (actually_read);
		memcpy_tofs (buf_ptr,tape->merge_buffer,bytes_read);
		buf_ptr+=bytes_read;count-=bytes_read;actually_read+=bytes_read;
	}

	if (count) {
		bytes_read=idetape_add_chrdev_read_request (drive,tape->capabilities.ctl,tape->merge_buffer);
		if (bytes_read <= 0)
			return (actually_read);
		temp=IDETAPE_MIN (count,bytes_read);
		memcpy_tofs (buf_ptr,tape->merge_buffer,temp);
		actually_read+=temp;
		tape->merge_buffer_offset=temp;
		tape->merge_buffer_size=bytes_read-temp;
	}
	return (actually_read);
}
 
int idetape_chrdev_write (struct inode *inode, struct file *file, const char *buf, int count)

{
	ide_drive_t *drive=idetape_chrdev.drive;
	idetape_tape_t *tape=&(drive->tape);
	const char *buf_ptr=buf;
	int retval,actually_written=0;

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_chrdev_write\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->chrdev_direction != idetape_direction_write) {	/* Initialize write operation */
		if (tape->chrdev_direction == idetape_direction_read)
			idetape_discard_read_pipeline (drive);

		/*
		 *	Issue a write 0 command to ensure that DSC handshake
		 *	is switched from completion mode to buffer available
		 *	mode.
		 */

		retval=idetape_queue_rw_tail (drive,IDETAPE_WRITE_REQUEST,0,tape->merge_buffer);
		if (retval < 0)
			return (retval);		

		tape->chrdev_direction=idetape_direction_write;
	}

	if (count==0)
		return (0);

	if (tape->merge_buffer_size) {
#if IDETAPE_DEBUG_BUGS
		if (tape->merge_buffer_size >= tape->data_buffer_size) {
			printk ("ide-tape: bug: merge buffer too big\n");
			tape->merge_buffer_size=0;
		}
#endif /* IDETAPE_DEBUG_BUGS */

		actually_written=IDETAPE_MIN (tape->data_buffer_size-tape->merge_buffer_size,count);
		memcpy_fromfs (tape->merge_buffer+tape->merge_buffer_size,buf_ptr,actually_written);
		buf_ptr+=actually_written;tape->merge_buffer_size+=actually_written;count-=actually_written;

		if (tape->merge_buffer_size == tape->data_buffer_size) {
			tape->merge_buffer_size=0;
			retval=idetape_add_chrdev_write_request (drive,tape->capabilities.ctl,tape->merge_buffer);
			if (retval <= 0)
				return (retval);
		}
	}

	while (count >= tape->data_buffer_size) {
		memcpy_fromfs (tape->merge_buffer,buf_ptr,tape->data_buffer_size);
		buf_ptr+=tape->data_buffer_size;count-=tape->data_buffer_size;
		retval=idetape_add_chrdev_write_request (drive,tape->capabilities.ctl,tape->merge_buffer);
		actually_written+=tape->data_buffer_size;
		if (retval <= 0)
			return (retval);
	}

	if (count) {
		actually_written+=count;
		memcpy_fromfs (tape->merge_buffer,buf_ptr,count);
		tape->merge_buffer_size+=count;
	}
	return (actually_written);
}

/*
 *	Our character device ioctls.
 *
 *	General mtio.h magnetic io commands are supported here, and not in
 *	the corresponding block interface.
 *
 *	The following ioctls are supported:
 *
 *	MTIOCTOP -	Refer to idetape_mtioctop for detailed description.
 *
 *	MTIOCGET - 	The mt_dsreg field in the returned mtget structure
 *			will be set to (recommended block size <<
 *			MT_ST_BLKSIZE_SHIFT) & MT_ST_BLKSIZE_MASK, which
 *			is currently equal to the size itself.
 *			The other mtget fields are not supported.
 *
 *			Note that we do not actually return the tape's
 *			block size. Rather, we provide the recommended
 *			number of bytes which should be used as a "user
 *			block size" with the character device read/write
 *			functions to maximize throughput.
 *
 *	MTIOCPOS -	The current tape "position" is returned.
 *			(A unique number which can be used with the MTSEEK
 *			 operation to return to this position in some
 *			 future time, provided this place was not overwritten
 *			 meanwhile).
 *
 *	Our own ide-tape ioctls are supported on both interfaces.
 */

int idetape_chrdev_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)

{
	ide_drive_t *drive=idetape_chrdev.drive;
	idetape_tape_t *tape=&(drive->tape);
	idetape_packet_command_t pc;
	struct mtop mtop;
	struct mtget mtget;
	struct mtpos mtpos;
	int retval;

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_chrdev_ioctl, cmd=%u\n",cmd);
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->chrdev_direction == idetape_direction_write) {
		idetape_empty_write_pipeline (drive);
		idetape_flush_tape_buffers (drive);
	}

	if (tape->chrdev_direction == idetape_direction_read && cmd != MTIOCTOP)
		idetape_discard_read_pipeline (drive);
	
	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;

	switch (cmd) {
		case MTIOCTOP:
			retval=verify_area (VERIFY_READ,(char *) arg,sizeof (struct mtop));
			if (retval) return (retval);
			memcpy_fromfs ((char *) &mtop, (char *) arg, sizeof (struct mtop));
			return (idetape_mtioctop (drive,mtop.mt_op,mtop.mt_count));
		case MTIOCGET:
			mtget.mt_dsreg=(tape->data_buffer_size << MT_ST_BLKSIZE_SHIFT) & MT_ST_BLKSIZE_MASK;
			retval=verify_area (VERIFY_WRITE,(char *) arg,sizeof (struct mtget));
			if (retval) return (retval);
			memcpy_tofs ((char *) arg,(char *) &mtget, sizeof (struct mtget));
			return (0);
		case MTIOCPOS:
			idetape_create_read_position_cmd (&pc);
			retval=idetape_queue_pc_tail (drive,&pc);
			if (retval) return (retval);
			mtpos.mt_blkno=tape->block_address;
			retval=verify_area (VERIFY_WRITE,(char *) arg,sizeof (struct mtpos));
			if (retval) return (retval);
			memcpy_tofs ((char *) arg,(char *) &mtpos, sizeof (struct mtpos));
			return (0);
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
 *
 *	Note:
 *
 *		MTBSF and MTBSFM are not supported when the tape doesn't
 *		supports spacing over filemarks in the reverse direction.
 *		In this case, MTFSFM is also usually not supported (it is
 *		supported in the rare case in which we crossed the filemark
 *		during our read-ahead pipelined operation mode).
 *		
 *	MTWEOF	-	Writes mt_count filemarks. Tape is positioned after
 *			the last written filemark.
 *
 *	MTREW	-	Rewinds tape.
 *
 *	MTOFFL	-	Puts the tape drive "Offline": Rewinds the tape and
 *			prevents further access until the media is replaced.
 *
 *	MTNOP	-	Flushes tape buffers.
 *
 *	MTRETEN	-	Retension media. This typically consists of one end
 *			to end pass on the media.
 *
 *	MTEOM	-	Moves to the end of recorded data.
 *
 *	MTERASE	-	Erases tape.
 *
 *	MTSEEK	-	Positions the tape in a specific block number, which
 *			was previously received using the MTIOCPOS ioctl,
 *			assuming this place was not overwritten meanwhile.
 *
 *	The following commands are currently not supported:
 *
 *	MTFSR, MTBSR, MTFSS, MTBSS, MTWSM, MTSETBLK, MTSETDENSITY,
 *	MTSETDRVBUFFER, MT_ST_BOOLEANS, MT_ST_WRITE_THRESHOLD.
 */
 
int idetape_mtioctop (ide_drive_t *drive,short mt_op,int mt_count)

{
	idetape_tape_t *tape=&(drive->tape);
	idetape_packet_command_t pc;
	int i,retval;

	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;

#if IDETAPE_DEBUG_LOG
	printk ("Handling MTIOCTOP ioctl: mt_op=%d, mt_count=%d\n",mt_op,mt_count);
#endif /* IDETAPE_DEBUG_LOG */

	/*
	 *	Commands which need our pipelined read-ahead stages.
	 */

	switch (mt_op) {
		case MTFSF:
		case MTFSFM:
		case MTBSF:
		case MTBSFM:
			if (!mt_count)
				return (0);
			return (idetape_space_over_filemarks (drive,mt_op,mt_count));
		default:
			break;
	}

	/*
	 *	Empty the pipeline.
	 */

	if (tape->chrdev_direction == idetape_direction_read)
		idetape_discard_read_pipeline (drive);

	switch (mt_op) {
		case MTWEOF:
			for (i=0;i<mt_count;i++) {
				idetape_create_write_filemark_cmd (&pc,1);
				retval=idetape_queue_pc_tail (drive,&pc);
				if (retval) return (retval);
			}
			return (0);
		case MTREW:
			return (idetape_rewind_tape (drive));
		case MTOFFL:
			idetape_create_load_unload_cmd (&pc,!IDETAPE_LU_LOAD_MASK);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTNOP:
			return (idetape_flush_tape_buffers (drive));
		case MTRETEN:
			idetape_create_load_unload_cmd (&pc,IDETAPE_LU_RETENSION_MASK | IDETAPE_LU_LOAD_MASK);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTEOM:
			idetape_create_space_cmd (&pc,0,IDETAPE_SPACE_TO_EOD);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTERASE:
			retval=idetape_rewind_tape (drive);
			if (retval) return (retval);
			idetape_create_erase_cmd (&pc);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTSEEK:
			return (idetape_position_tape (drive,mt_count));
		default:
			printk ("ide-tape: MTIO operation %d not supported\n",mt_op);
			return (-EIO);
	}
}

/*
 *	idetape_space_over_filemarks is now a bit more complicated than just
 *	passing the command to the tape since we may have crossed some
 *	filemarks during our pipelined read-ahead mode.
 *
 *	As a minor side effect, the pipeline enables us to support MTFSFM when
 *	the filemark is in our internal pipeline even if the tape doesn't
 *	support spacing over filemarks in the reverse direction.
 */
 
int idetape_space_over_filemarks (ide_drive_t *drive,short mt_op,int mt_count)

{
	idetape_tape_t *tape=&(drive->tape);
	idetape_packet_command_t pc;
	unsigned long flags;
	int retval,count=0,errors;

	if (tape->chrdev_direction == idetape_direction_read) {

		/*
		 *	We have a read-ahead buffer. Scan it for crossed
		 *	filemarks.
		 */

		tape->merge_buffer_size=tape->merge_buffer_offset=0;
		while (tape->first_stage != NULL) {
			
			/*
			 *	Wait until the first read-ahead request
			 *	is serviced.
			 */
		
			save_flags (flags);cli ();
			if (tape->active_data_request == &(tape->first_stage->rq))
				idetape_wait_for_request (tape->active_data_request);
			restore_flags (flags);

			errors=tape->first_stage->rq.errors;
			if (errors == IDETAPE_RQ_ERROR_FILEMARK)
				count++;

			if (count == mt_count) {
				switch (mt_op) {
					case MTFSF:
						idetape_remove_stage_head (drive);
					case MTFSFM:
						return (0);
				}
			}
			idetape_remove_stage_head (drive);
		}
		idetape_discard_read_pipeline (drive);
	}

	/*
	 *	The filemark was not found in our internal pipeline.
	 *	Now we can issue the space command.
	 */

	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;

	switch (mt_op) {
		case MTFSF:
			idetape_create_space_cmd (&pc,mt_count-count,IDETAPE_SPACE_OVER_FILEMARK);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTFSFM:
			if (!tape->capabilities.sprev)
				return (-EIO);
			retval=idetape_mtioctop (drive,MTFSF,mt_count-count);
			if (retval) return (retval);
			return (idetape_mtioctop (drive,MTBSF,1));
		case MTBSF:
			if (!tape->capabilities.sprev)
				return (-EIO);
			idetape_create_space_cmd (&pc,-(mt_count+count),IDETAPE_SPACE_OVER_FILEMARK);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTBSFM:
			if (!tape->capabilities.sprev)
				return (-EIO);
			retval=idetape_mtioctop (drive,MTBSF,mt_count+count);
			if (retval) return (retval);
			return (idetape_mtioctop (drive,MTFSF,1));
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
	ide_drive_t *drive=idetape_chrdev.drive;
	idetape_tape_t *tape=&(drive->tape);
	unsigned long flags;
	unsigned int minor=MINOR (inode->i_rdev),allocation_length;
			
	save_flags (flags);cli ();

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_chrdev_open\n");
#endif /* IDETAPE_DEBUG_LOG */

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

	allocation_length=tape->data_buffer_size;
	if (tape->data_buffer_size % IDETAPE_ALLOCATION_BLOCK)
		allocation_length+=IDETAPE_ALLOCATION_BLOCK;

#if IDETAPE_MINIMIZE_IDLE_MEMORY_USAGE
	if (tape->data_buffer == NULL)
		tape->data_buffer=kmalloc (allocation_length,GFP_KERNEL);
	if (tape->data_buffer == NULL)
		goto sorry;
	if (tape->merge_buffer == NULL)
		tape->merge_buffer=kmalloc (allocation_length,GFP_KERNEL);
	if (tape->merge_buffer == NULL) {
		kfree (tape->data_buffer);
	sorry:
		printk ("ide-tape: FATAL - Can not allocate continuous buffer of %d bytes\n",allocation_length);
		tape->busy=0;
		return (-EIO);
	}
#endif /* IDETAPE_MINIMIZE_IDLE_MEMORY_USAGE */

	if (!tape->block_address_valid) {
		if (idetape_rewind_tape (drive)) {
			printk ("ide-tape: Rewinding tape failed\n");
			tape->busy=0;
			return (-EIO);
		}
	}

	return (0);
}

/*
 *	Our character device release function.
 */

void idetape_chrdev_release (struct inode *inode, struct file *filp)

{
	ide_drive_t *drive=idetape_chrdev.drive;
	idetape_tape_t *tape=&(drive->tape);
	unsigned int minor=MINOR (inode->i_rdev);
	idetape_packet_command_t pc;
	unsigned long flags;
			
#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_chrdev_release\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->chrdev_direction == idetape_direction_write) {
		idetape_empty_write_pipeline (drive);
		idetape_create_write_filemark_cmd (&pc,1);	/* Write a filemark */
		if (idetape_queue_pc_tail (drive,&pc))
			printk ("ide-tape: Couldn't write a filemark\n");
	}
	
	if (tape->chrdev_direction == idetape_direction_read) {
		if (minor < 128)
			idetape_discard_read_pipeline (drive);
		else
			idetape_wait_for_pipeline (drive);
	}
	
	if (minor < 128)
		if (idetape_rewind_tape (drive))
			printk ("ide-tape: Rewinding tape failed\n");

#if IDETAPE_MINIMIZE_IDLE_MEMORY_USAGE
	kfree (tape->data_buffer);
	tape->data_buffer=NULL;
	if (!tape->merge_buffer_size) {
		kfree (tape->merge_buffer);
		tape->merge_buffer=NULL;
	}
#endif /* IDETAPE_MINIMIZE_IDLE_MEMORY_USAGE */

	save_flags (flags);cli ();
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
 *	Rewinds the tape to the Beginning Of the current Partition (BOP).
 *
 *	We currently support only one partition.
 */ 

int idetape_rewind_tape (ide_drive_t *drive)

{
	int retval;
	idetape_packet_command_t pc;
#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_rewind_tape\n");
#endif /* IDETAPE_DEBUG_LOG */	
	
	idetape_create_rewind_cmd (&pc);
	retval=idetape_queue_pc_tail (drive,&pc);
	if (retval) return (retval);
			
	idetape_create_read_position_cmd (&pc);
	pc.buffer=pc.temp_buffer;
	pc.buffer_size=IDETAPE_TEMP_BUFFER_SIZE;
	pc.current_position=pc.temp_buffer;
	return (idetape_queue_pc_tail (drive,&pc));
}

int idetape_flush_tape_buffers (ide_drive_t *drive)

{
	idetape_packet_command_t pc;

	idetape_create_write_filemark_cmd (&pc,0);
	return (idetape_queue_pc_tail (drive,&pc));
}

/*
 *	Pipeline related functions
 */

/*
 *	idetape_kmalloc_stage uses kmalloc to allocate a pipeline stage,
 *	along with all the necessary small buffers which together make
 *	a buffer of size tape->data_buffer_size or a bit more, in case
 *	it is not a multiply of IDETAPE_ALLOCATION_BLOCK (it isn't ...).
 *
 *	Returns a pointer to the new allocated stage, or NULL if we
 *	can't (or don't want to, in case we already have too many stages)
 *	allocate a stage.
 *
 *	Pipeline stages are optional and are used to increase performance.
 *	If we can't allocate them, we'll manage without them.
 */
 
idetape_pipeline_stage_t *idetape_kmalloc_stage (ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);
	idetape_pipeline_stage_t *new_stage;
	idetape_buffer_head_t *prev_bh,*bh;
	int buffers_num,i;
	
#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_kmalloc_stage\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->current_number_of_stages>=tape->max_number_of_stages) {
		return (NULL);
	}
		
	new_stage=(idetape_pipeline_stage_t *) kmalloc (sizeof (idetape_pipeline_stage_t),GFP_KERNEL);
	if (new_stage==NULL)
		return (NULL);
		
	new_stage->next=new_stage->prev=NULL;

	buffers_num=tape->data_buffer_size / IDETAPE_ALLOCATION_BLOCK;
	if (tape->data_buffer_size % IDETAPE_ALLOCATION_BLOCK)
		buffers_num++;

	prev_bh=new_stage->bh=(idetape_buffer_head_t *) kmalloc (sizeof (idetape_buffer_head_t),GFP_KERNEL);
	if (new_stage->bh==NULL) {
		idetape_kfree_stage (new_stage);
		return (NULL);
	}
	new_stage->bh->next=NULL;

	new_stage->bh->data=kmalloc (IDETAPE_ALLOCATION_BLOCK,GFP_KERNEL);
	if (new_stage->bh->data==NULL) {
		idetape_kfree_stage (new_stage);
		return (NULL);
	}
	
	for (i=1;i<buffers_num;i++) {
		bh=(idetape_buffer_head_t *) kmalloc (sizeof (idetape_buffer_head_t),GFP_KERNEL);
		if (bh==NULL) {
			idetape_kfree_stage (new_stage);
			return (NULL);
		}
		bh->next=NULL;
		prev_bh->next=bh;
		bh->data=kmalloc (IDETAPE_ALLOCATION_BLOCK,GFP_KERNEL);
		if (bh->data == NULL) {
			idetape_kfree_stage (new_stage);
			return (NULL);
		}
		prev_bh=bh;
	}
	return (new_stage);
}

/*
 *	idetape_kfree_stage calls kfree to completely free a stage, along with
 *	its related buffers.
 */
 
void idetape_kfree_stage (idetape_pipeline_stage_t *stage)

{
	idetape_buffer_head_t *prev_bh,*bh;
	
	if (stage == NULL)
		return;

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_kfree_stage\n");
#endif /* IDETAPE_DEBUG_LOG */
	
	bh=stage->bh;
	
	while (bh != NULL) {
		prev_bh=bh;
		if (bh->data != NULL)
			kfree (bh->data);
		bh=bh->next;
		kfree (prev_bh);
	}
	
	kfree (stage);
	return;
}

/*
 *	idetape_copy_buffer_from_stage and idetape_copy_buffer_to_stage
 *	copy data from/to the small buffers into/from a continuous buffer.
 */
  
void idetape_copy_buffer_from_stage (idetape_pipeline_stage_t *stage,char *buffer)

{
	idetape_buffer_head_t *bh;
	char *ptr;

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_copy_buffer_from_stage\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (buffer == NULL) {
		printk ("ide-tape: bug: buffer is null in copy_buffer_from_stage\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	
	ptr=buffer;
	bh=stage->bh;
	
	while (bh != NULL) {
#if IDETAPE_DEBUG_BUGS
		if (bh->data == NULL) {
			printk ("ide-tape: bug: bh->data is null\n");
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		memcpy (ptr,bh->data,IDETAPE_ALLOCATION_BLOCK);
		bh=bh->next;
		ptr=ptr+IDETAPE_ALLOCATION_BLOCK;
	}
	return;
}

/*
 *	Here we copy a continuous data buffer to the various small buffers
 *	in the pipeline stage.
 */
 
void idetape_copy_buffer_to_stage (idetape_pipeline_stage_t *stage,char *buffer)

{
	idetape_buffer_head_t *bh;
	char *ptr;

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_copy_buffer_to_stage\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (buffer == NULL) {
		printk ("ide-tape: bug: buffer is null in copy_buffer_to_stage\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */

	ptr=buffer;
	bh=stage->bh;
	
	while (bh != NULL) {
#if IDETAPE_DEBUG_BUGS
		if (bh->data == NULL) {
			printk ("ide-tape: bug: bh->data is null\n");
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		memcpy (bh->data,ptr,IDETAPE_ALLOCATION_BLOCK);
		bh=bh->next;
		ptr=ptr+IDETAPE_ALLOCATION_BLOCK;
	}
	return;
}

/*
 *	idetape_increase_max_pipeline_stages is a part of the feedback
 *	loop which tries to find the optimum number of stages. In the
 *	feedback loop, we are starting from a minimum maximum number of
 *	stages, and if we sense that the pipeline is empty, we try to
 *	increase it, until we reach the user compile time memory limit.
 */

void idetape_increase_max_pipeline_stages (ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);
	
#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_increase_max_pipeline_stages\n");
#endif /* IDETAPE_DEBUG_LOG */

	tape->max_number_of_stages+=IDETAPE_INCREASE_STAGES_RATE*
					(IDETAPE_MAX_PIPELINE_STAGES-IDETAPE_MIN_PIPELINE_STAGES);

	if (tape->max_number_of_stages >= IDETAPE_MAX_PIPELINE_STAGES)
		tape->max_number_of_stages = IDETAPE_MAX_PIPELINE_STAGES;

#if IDETAPE_DEBUG_LOG
	printk ("Maximum number of stages: %d\n",tape->max_number_of_stages);
#endif /* IDETAPE_DEBUG_LOG */

	return;
}

/*
 *	idetape_add_stage_tail adds a new stage at the end of the pipeline.
 *
 *	Caller should disable interrupts, if necessary.
 */
 
void idetape_add_stage_tail (ide_drive_t *drive,idetape_pipeline_stage_t *stage)

{
	idetape_tape_t *tape=&(drive->tape);
	
#if IDETAPE_DEBUG_LOG
		printk ("Reached idetape_add_stage_tail\n");
#endif /* IDETAPE_DEBUG_LOG */

	stage->next=NULL;
	stage->prev=tape->last_stage;
	if (tape->last_stage != NULL)
		tape->last_stage->next=stage;
	else
		tape->first_stage=tape->next_stage=stage;
	tape->last_stage=stage;
	if (tape->next_stage == NULL)
		tape->next_stage=tape->last_stage;
	tape->current_number_of_stages++;
}

/*
 *	idetape_remove_stage_head removes tape->first_stage from the pipeline.
 *
 *	Again, caller should avoid race conditions.
 */
 
void idetape_remove_stage_head (ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);
	idetape_pipeline_stage_t *stage;
	
#if IDETAPE_DEBUG_LOG
		printk ("Reached idetape_remove_stage_head\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (tape->first_stage == NULL) {
		printk ("ide-tape: bug: tape->first_stage is NULL\n");
		return;		
	}
	if (tape->active_stage == tape->first_stage) {
		printk ("ide-tape: bug: Trying to free our active pipeline stage\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	stage=tape->first_stage;
	tape->first_stage=stage->next;
	idetape_kfree_stage (stage);
	tape->current_number_of_stages--;
	if (tape->first_stage == NULL) {
		tape->last_stage=NULL;
#if IDETAPE_DEBUG_BUGS
		if (tape->next_stage != NULL)
			printk ("ide-tape: bug: tape->next_stage != NULL\n");
		if (tape->current_number_of_stages)
			printk ("ide-tape: bug: current_number_of_stages should be 0 now\n");
#endif /* IDETAPE_DEBUG_BUGS */
	}
}

/*
 *	idetape_insert_pipeline_into_queue is used to start servicing the
 *	pipeline stages, starting from tape->next_stage.
 */
 
void idetape_insert_pipeline_into_queue (ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);

	if (tape->next_stage == NULL)
		return;

	if (tape->active_data_request == NULL) {
		idetape_active_next_stage (drive);
		(void) (ide_do_drive_cmd (drive,tape->active_data_request,ide_end));
		return;
	}
}

/*
 *	idetape_active_next_stage will declare the next stage as "active".
 */
 
void idetape_active_next_stage (ide_drive_t *drive)

{
	idetape_tape_t *tape=&(drive->tape);
	idetape_pipeline_stage_t *stage=tape->next_stage;
	struct request *rq=&(stage->rq);

#if IDETAPE_DEBUG_LOG
	printk ("Reached idetape_active_next_stage\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (stage == NULL) {
		printk ("ide-tape: bug: Trying to activate a non existing stage\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */	
	if (rq->cmd == IDETAPE_WRITE_REQUEST)
		idetape_copy_buffer_from_stage (stage,tape->data_buffer);
	
	rq->buffer=tape->data_buffer;
	tape->active_data_request=rq;
	tape->active_stage=stage;
	tape->next_stage=stage->next;
}
