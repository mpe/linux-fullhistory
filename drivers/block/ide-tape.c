/*
 * linux/drivers/block/ide-tape.c	Version 1.14		Dec  30, 1998
 *
 * Copyright (C) 1995 - 1998 Gadi Oxman <gadio@netvision.net.il>
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
 * The character device interface consists of the following devices:
 *
 * ht0		major 37, minor 0	first  IDE tape, rewind on close.
 * ht1		major 37, minor 1	second IDE tape, rewind on close.
 * ...
 * nht0		major 37, minor 128	first  IDE tape, no rewind on close.
 * nht1		major 37, minor 129	second IDE tape, no rewind on close.
 * ...
 *
 * Run linux/scripts/MAKEDEV.ide to create the above entries.
 *
 * The general magnetic tape commands compatible interface, as defined by
 * include/linux/mtio.h, is accessible through the character device.
 *
 * General ide driver configuration options, such as the interrupt-unmask
 * flag, can be configured by issuing an ioctl to the block device interface,
 * as any other ide device.
 *
 * Our own ide-tape ioctl's can be issued to either the block device or
 * the character device interface.
 *
 * Maximal throughput with minimal bus load will usually be achieved in the
 * following scenario:
 *
 *	1.	ide-tape is operating in the pipelined operation mode.
 *	2.	No buffering is performed by the user backup program.
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
 *                       By cooperating with ide-dma.c, bus mastering DMA can
 *                        now sometimes be used with IDE tape drives as well.
 *                        Bus mastering DMA has the potential to dramatically
 *                        reduce the CPU's overhead when accessing the device,
 *                        and can be enabled by using hdparm -d1 on the tape's
 *                        block device interface. For more info, read the
 *                        comments in ide-dma.c.
 * Ver 1.4   Mar 13 96   Fixed serialize support.
 * Ver 1.5   Apr 12 96   Fixed shared interface operation, broken in 1.3.85.
 *                       Fixed pipelined read mode inefficiency.
 *                       Fixed nasty null dereferencing bug.
 * Ver 1.6   Aug 16 96   Fixed FPU usage in the driver.
 *                       Fixed end of media bug.
 * Ver 1.7   Sep 10 96   Minor changes for the CONNER CTT8000-A model.
 * Ver 1.8   Sep 26 96   Attempt to find a better balance between good
 *                        interactive response and high system throughput.
 * Ver 1.9   Nov  5 96   Automatically cross encountered filemarks rather
 *                        than requiring an explicit FSF command.
 *                       Abort pending requests at end of media.
 *                       MTTELL was sometimes returning incorrect results.
 *                       Return the real block size in the MTIOCGET ioctl.
 *                       Some error recovery bug fixes.
 * Ver 1.10  Nov  5 96   Major reorganization.
 *                       Reduced CPU overhead a bit by eliminating internal
 *                        bounce buffers.
 *                       Added module support.
 *                       Added multiple tape drives support.
 *                       Added partition support.
 *                       Rewrote DSC handling.
 *                       Some portability fixes.
 *                       Removed ide-tape.h.
 *                       Additional minor changes.
 * Ver 1.11  Dec  2 96   Bug fix in previous DSC timeout handling.
 *                       Use ide_stall_queue() for DSC overlap.
 *                       Use the maximum speed rather than the current speed
 *                        to compute the request service time.
 * Ver 1.12  Dec  7 97   Fix random memory overwriting and/or last block data
 *                        corruption, which could occur if the total number
 *                        of bytes written to the tape was not an integral
 *                        number of tape blocks.
 *                       Add support for INTERRUPT DRQ devices.
 * Ver 1.13  Jan  2 98   Add "speed == 0" work-around for HP COLORADO 5GB
 * Ver 1.14  Dec 30 98   Partial fixes for the Sony/AIWA tape drives.
 *                       Replace cli()/sti() with hwgroup spinlocks.
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
 *		busy with minimum allocated memory or until we reach
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
 * the compile time parameters below.
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

#define IDETAPE_VERSION "1.13"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/malloc.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <asm/bitops.h>

/*
 *	Main Linux ide driver include file
 */
#include "ide.h"

/*
 *	For general magnetic tape device compatibility.
 */
#include <linux/mtio.h>

/**************************** Tunable parameters *****************************/

/*
 *	Pipelined mode parameters.
 *
 *	We try to use the minimum number of stages which is enough to
 *	keep the tape constantly streaming. To accomplish that, we implement
 *	a feedback loop around the maximum number of stages:
 *
 *	We start from MIN maximum stages (we will not even use MIN stages
 *      if we don't need them), increment it by RATE*(MAX-MIN)
 *	whenever we sense that the pipeline is empty, until we reach
 *	the optimum value or until we reach MAX.
 *
 *	Setting the following parameter to 0 will disable the pipelined mode.
 */
#define IDETAPE_MIN_PIPELINE_STAGES	100
#define IDETAPE_MAX_PIPELINE_STAGES	200
#define IDETAPE_INCREASE_STAGES_RATE	 20

/*
 *	Assuming the tape shares an interface with another device, the default
 *	behavior is to service our pending pipeline requests as soon as
 *	possible, but to gracefully postpone them in favor of the other device
 *	when the tape is busy. This has the potential to maximize our
 *	throughput and in the same time, to make efficient use of the IDE bus.
 *
 *	Note that when we transfer data to / from the tape, we co-operate with
 *	the relatively fast tape buffers and the tape will perform the
 *	actual media access in the background, without blocking the IDE
 *	bus. This means that as long as the maximum IDE bus throughput is much
 *	higher than the sum of our maximum throughput and the maximum
 *	throughput of the other device, we should probably leave the default
 *	behavior.
 *
 *	However, if it is still desired to give the other device a share even
 *	in our own (small) bus bandwidth, you can set IDETAPE_LOW_TAPE_PRIORITY
 *	to 1. This will let the other device finish *all* its pending requests
 *	before we even check if we can service our next pending request.
 */
#define IDETAPE_LOW_TAPE_PRIORITY	0

/*
 *	The following are used to debug the driver:
 *
 *	Setting IDETAPE_DEBUG_LOG to 1 will log driver flow control.
 *	Setting IDETAPE_DEBUG_BUGS to 1 will enable self-sanity checks in
 *	some places.
 *
 *	Setting them to 0 will restore normal operation mode:
 *
 *		1.	Disable logging normal successful operations.
 *		2.	Disable self-sanity checks.
 *		3.	Errors will still be logged, of course.
 *
 *	All the #if DEBUG code will be removed some day, when the driver
 *	is verified to be stable enough. This will make it much more
 *	esthetic.
 */
#define IDETAPE_DEBUG_LOG		0
#define IDETAPE_DEBUG_BUGS		1

/*
 *	After each failed packet command we issue a request sense command
 *	and retry the packet command IDETAPE_MAX_PC_RETRIES times.
 *
 *	Setting IDETAPE_MAX_PC_RETRIES to 0 will disable retries.
 */
#define IDETAPE_MAX_PC_RETRIES		3

/*
 *	With each packet command, we allocate a buffer of
 *	IDETAPE_PC_BUFFER_SIZE bytes. This is used for several packet
 *	commands (Not for READ/WRITE commands).
 */
#define IDETAPE_PC_BUFFER_SIZE		256

/*
 *	In various places in the driver, we need to allocate storage
 *	for packet commands and requests, which will remain valid while
 *	we leave the driver to wait for an interrupt or a timeout event.
 */
#define IDETAPE_PC_STACK		(10 + IDETAPE_MAX_PC_RETRIES)

/*
 *	DSC polling parameters.
 *
 *	Polling for DSC (a single bit in the status register) is a very
 *	important function in ide-tape. There are two cases in which we
 *	poll for DSC:
 *
 *	1.	Before a read/write packet command, to ensure that we
 *		can transfer data from/to the tape's data buffers, without
 *		causing an actual media access. In case the tape is not
 *		ready yet, we take out our request from the device
 *		request queue, so that ide.c will service requests from
 *		the other device on the same interface meanwhile.
 *
 *	2.	After the successful initialization of a "media access
 *		packet command", which is a command which can take a long
 *		time to complete (it can be several seconds or even an hour).
 *
 *		Again, we postpone our request in the middle to free the bus
 *		for the other device. The polling frequency here should be
 *		lower than the read/write frequency since those media access
 *		commands are slow. We start from a "fast" frequency -
 *		IDETAPE_DSC_MA_FAST (one second), and if we don't receive DSC
 *		after IDETAPE_DSC_MA_THRESHOLD (5 minutes), we switch it to a
 *		lower frequency - IDETAPE_DSC_MA_SLOW (1 minute).
 *
 *	We also set a timeout for the timer, in case something goes wrong.
 *	The timeout should be longer then the maximum execution time of a
 *	tape operation.
 */
 
/*
 *	The following parameter is used to select the point in the internal
 *	tape fifo in which we will start to refill the buffer. Decreasing
 *	the following parameter will improve the system's latency and
 *	interactive response, while using a high value might improve sytem
 *	throughput.
 */
#define IDETAPE_FIFO_THRESHOLD 		2

/*
 *	Some tape drives require a long irq timeout
 */
#define IDETAPE_WAIT_CMD		(60*HZ)

/*
 *	DSC timings.
 */
#define IDETAPE_DSC_RW_MIN		5*HZ/100	/* 50 msec */
#define IDETAPE_DSC_RW_MAX		40*HZ/100	/* 400 msec */
#define IDETAPE_DSC_RW_TIMEOUT		2*60*HZ		/* 2 minutes */
#define IDETAPE_DSC_MA_FAST		2*HZ		/* 2 seconds */
#define IDETAPE_DSC_MA_THRESHOLD	5*60*HZ		/* 5 minutes */
#define IDETAPE_DSC_MA_SLOW		30*HZ		/* 30 seconds */
#define IDETAPE_DSC_MA_TIMEOUT		2*60*60*HZ	/* 2 hours */

/*************************** End of tunable parameters ***********************/

typedef enum {
	idetape_direction_none,
	idetape_direction_read,
	idetape_direction_write
} idetape_chrdev_direction_t;

/*
 *	Our view of a packet command.
 */
typedef struct idetape_packet_command_s {
	u8 c[12];				/* Actual packet bytes */
	int retries;				/* On each retry, we increment retries */
	int error;				/* Error code */
	int request_transfer;			/* Bytes to transfer */
	int actually_transferred;		/* Bytes actually transferred */
	int buffer_size;			/* Size of our data buffer */
	struct buffer_head *bh;
	char *b_data;
	int b_count;
	byte *buffer;				/* Data buffer */
	byte *current_position;			/* Pointer into the above buffer */
	void (*callback) (ide_drive_t *);	/* Called when this packet command is completed */
	byte pc_buffer[IDETAPE_PC_BUFFER_SIZE];	/* Temporary buffer */
	unsigned int flags;			/* Status/Action bit flags */
} idetape_pc_t;

/*
 *	Packet command flag bits.
 */
#define	PC_ABORT			0	/* Set when an error is considered normal - We won't retry */
#define PC_WAIT_FOR_DSC			1	/* 1 When polling for DSC on a media access command */
#define PC_DMA_RECOMMENDED		2	/* 1 when we prefer to use DMA if possible */
#define	PC_DMA_IN_PROGRESS		3	/* 1 while DMA in progress */
#define	PC_DMA_ERROR			4	/* 1 when encountered problem during DMA */
#define	PC_WRITING			5	/* Data direction */

/*
 *	Capabilities and Mechanical Status Page
 */
typedef struct {
	unsigned	page_code	:6;	/* Page code - Should be 0x2a */
	unsigned	reserved1_67	:2;
	u8		page_length;		/* Page Length - Should be 0x12 */
	u8		reserved2, reserved3;
	unsigned	ro		:1;	/* Read Only Mode */
	unsigned	reserved4_1234	:4;
	unsigned	sprev		:1;	/* Supports SPACE in the reverse direction */
	unsigned	reserved4_67	:2;
	unsigned	reserved5_012	:3;
	unsigned	efmt		:1;	/* Supports ERASE command initiated formatting */
	unsigned	reserved5_4	:1;
	unsigned	qfa		:1;	/* Supports the QFA two partition formats */
	unsigned	reserved5_67	:2;
	unsigned	lock		:1;	/* Supports locking the volume */
	unsigned	locked		:1;	/* The volume is locked */
	unsigned	prevent		:1;	/* The device defaults in the prevent state after power up */	
	unsigned	eject		:1;	/* The device can eject the volume */
	unsigned	reserved6_45	:2;	/* Reserved */	
	unsigned	ecc		:1;	/* Supports error correction */
	unsigned	cmprs		:1;	/* Supports data compression */
	unsigned	reserved7_0	:1;
	unsigned	blk512		:1;	/* Supports 512 bytes block size */
	unsigned	blk1024		:1;	/* Supports 1024 bytes block size */
	unsigned	reserved7_3_6	:4;
	unsigned	slowb		:1;	/* The device restricts the byte count for PIO */
						/* transfers for slow buffer memory ??? */
	u16		max_speed;		/* Maximum speed supported in KBps */
	u8		reserved10, reserved11;
	u16		ctl;			/* Continuous Transfer Limit in blocks */
	u16		speed;			/* Current Speed, in KBps */
	u16		buffer_size;		/* Buffer Size, in 512 bytes */
	u8		reserved18, reserved19;
} idetape_capabilities_page_t;

/*
 *	A pipeline stage.
 */
typedef struct idetape_stage_s {
	struct request rq;			/* The corresponding request */
	struct buffer_head *bh;			/* The data buffers */
	struct idetape_stage_s *next;		/* Pointer to the next stage */
} idetape_stage_t;

/*
 *	Most of our global data which we need to save even as we leave the
 *	driver due to an interrupt or a timer event is stored in a variable
 *	of type idetape_tape_t, defined below.
 */
typedef struct {
	ide_drive_t *drive;

	/*
	 *	Since a typical character device operation requires more
	 *	than one packet command, we provide here enough memory
	 *	for the maximum of interconnected packet commands.
	 *	The packet commands are stored in the circular array pc_stack.
	 *	pc_stack_index points to the last used entry, and warps around
	 *	to the start when we get to the last array entry.
	 *
	 *	pc points to the current processed packet command.
	 *
	 *	failed_pc points to the last failed packet command, or contains
	 *	NULL if we do not need to retry any packet command. This is
	 *	required since an additional packet command is needed before the
	 *	retry, to get detailed information on what went wrong.
      	 */
	idetape_pc_t *pc;			/* Current packet command */
	idetape_pc_t *failed_pc; 		/* Last failed packet command */
	idetape_pc_t pc_stack[IDETAPE_PC_STACK];/* Packet command stack */
	int pc_stack_index;			/* Next free packet command storage space */
	struct request rq_stack[IDETAPE_PC_STACK];
	int rq_stack_index;			/* We implement a circular array */

	/*
	 *	DSC polling variables.
	 *
	 *	While polling for DSC we use postponed_rq to postpone the
	 *	current request so that ide.c will be able to service
	 *	pending requests on the other device. Note that at most
	 *	we will have only one DSC (usually data transfer) request
	 *	in the device request queue. Additional requests can be
	 *	queued in our internal pipeline, but they will be visible
	 *	to ide.c only one at a time.
	 */
	struct request *postponed_rq;
	unsigned long dsc_polling_start;	/* The time in which we started polling for DSC */
	struct timer_list dsc_timer;		/* Timer used to poll for dsc */
	unsigned long best_dsc_rw_frequency;	/* Read/Write dsc polling frequency */
	unsigned long dsc_polling_frequency;	/* The current polling frequency */
	unsigned long dsc_timeout;		/* Maximum waiting time */

	/*
	 *	Position information
	 */
	byte partition;
	unsigned int block_address;		/* Current block */

	/*
	 *	Last error information
	 */
	byte sense_key, asc, ascq;

	/*
	 *	Character device operation
	 */
	unsigned int minor;
	char name[4];					/* device name */
	idetape_chrdev_direction_t chrdev_direction;	/* Current character device data transfer direction */

	/*
	 *	Device information
	 */
	unsigned short tape_block_size;			/* Usually 512 or 1024 bytes */
	int user_bs_factor;
	idetape_capabilities_page_t capabilities;	/* Copy of the tape's Capabilities and Mechanical Page */

	/*
	 *	Active data transfer request parameters.
	 *
	 *	At most, there is only one ide-tape originated data transfer
	 *	request in the device request queue. This allows ide.c to
	 *	easily service requests from the other device when we
	 *	postpone our active request. In the pipelined operation
	 *	mode, we use our internal pipeline structure to hold
	 *	more data requests.
	 *
	 *	The data buffer size is chosen based on the tape's
	 *	recommendation.
	 */
	struct request *active_data_request;	/* Pointer to the request which is waiting in the device request queue */
	int stage_size;				/* Data buffer size (chosen based on the tape's recommendation */
	idetape_stage_t *merge_stage;
	int merge_stage_size;
	struct buffer_head *bh;
	char *b_data;
	int b_count;
	
	/*
	 *	Pipeline parameters.
	 *
	 *	To accomplish non-pipelined mode, we simply set the following
	 *	variables to zero (or NULL, where appropriate).
	 */
	int nr_stages;				/* Number of currently used stages */
	int nr_pending_stages;			/* Number of pending stages */
	int max_stages, min_pipeline, max_pipeline; /* We will not allocate more than this number of stages */
	idetape_stage_t *first_stage;		/* The first stage which will be removed from the pipeline */
	idetape_stage_t *active_stage;		/* The currently active stage */
	idetape_stage_t *next_stage;		/* Will be serviced after the currently active request */
	idetape_stage_t *last_stage;		/* New requests will be added to the pipeline here */
	idetape_stage_t *cache_stage;		/* Optional free stage which we can use */
	int pages_per_stage;
	int excess_bh_size;			/* Wasted space in each stage */

	unsigned int flags;			/* Status/Action flags */
} idetape_tape_t;

/*
 *	Tape flag bits values.
 */
#define IDETAPE_IGNORE_DSC		0
#define IDETAPE_ADDRESS_VALID		1	/* 0 When the tape position is unknown */
#define IDETAPE_BUSY			2	/* Device already opened */
#define IDETAPE_PIPELINE_ERROR		3	/* Error detected in a pipeline stage */
#define IDETAPE_DETECT_BS		4	/* Attempt to auto-detect the current user block size */
#define IDETAPE_FILEMARK		5	/* Currently on a filemark */
#define IDETAPE_DRQ_INTERRUPT		6	/* DRQ interrupt device */

/*
 *	Supported ATAPI tape drives packet commands
 */
#define IDETAPE_TEST_UNIT_READY_CMD	0x00
#define IDETAPE_REWIND_CMD		0x01
#define IDETAPE_REQUEST_SENSE_CMD	0x03
#define IDETAPE_READ_CMD		0x08
#define IDETAPE_WRITE_CMD		0x0a
#define IDETAPE_WRITE_FILEMARK_CMD	0x10
#define IDETAPE_SPACE_CMD		0x11
#define IDETAPE_INQUIRY_CMD		0x12
#define IDETAPE_ERASE_CMD		0x19
#define IDETAPE_MODE_SENSE_CMD		0x1a
#define IDETAPE_LOAD_UNLOAD_CMD		0x1b
#define IDETAPE_LOCATE_CMD		0x2b
#define IDETAPE_READ_POSITION_CMD	0x34

/*
 *	Some defines for the SPACE command
 */
#define IDETAPE_SPACE_OVER_FILEMARK	1
#define IDETAPE_SPACE_TO_EOD		3

/*
 *	Some defines for the LOAD UNLOAD command
 */
#define IDETAPE_LU_LOAD_MASK		1
#define IDETAPE_LU_RETENSION_MASK	2
#define IDETAPE_LU_EOT_MASK		4

/*
 *	Special requests for our block device strategy routine.
 *
 *	In order to service a character device command, we add special
 *	requests to the tail of our block device request queue and wait
 *	for their completion.
 *
 */
#define IDETAPE_FIRST_RQ		90

/*
 * 	IDETAPE_PC_RQ is used to queue a packet command in the request queue.
 */
#define IDETAPE_PC_RQ1			90
#define IDETAPE_PC_RQ2			91

/*
 *	IDETAPE_READ_RQ and IDETAPE_WRITE_RQ are used by our
 *	character device interface to request read/write operations from
 *	our block device interface.
 */
#define IDETAPE_READ_RQ			92
#define IDETAPE_WRITE_RQ		93
#define IDETAPE_ABORTED_WRITE_RQ	94

#define IDETAPE_LAST_RQ			94

/*
 *	A macro which can be used to check if a we support a given
 *	request command.
 */
#define IDETAPE_RQ_CMD(cmd) 		((cmd >= IDETAPE_FIRST_RQ) && (cmd <= IDETAPE_LAST_RQ))

/*
 *	Error codes which are returned in rq->errors to the higher part
 *	of the driver.
 */
#define	IDETAPE_ERROR_GENERAL		101
#define	IDETAPE_ERROR_FILEMARK		102
#define	IDETAPE_ERROR_EOD		103

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
		unsigned dsc		:1;	/* Buffer availability / Media access command finished */
		unsigned reserved5	:1;	/* Reserved */
		unsigned drdy		:1;	/* Ignored for ATAPI commands (ready to accept ATA command) */
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
 */
typedef struct {
	ide_drive_t *drive;
} idetape_chrdev_t;

/*
 *	The following is used to format the general configuration word of
 *	the ATAPI IDENTIFY DEVICE command.
 */
struct idetape_id_gcw {	
	unsigned packet_size		:2;	/* Packet Size */
	unsigned reserved234		:3;	/* Reserved */
	unsigned drq_type		:2;	/* Command packet DRQ type */
	unsigned removable		:1;	/* Removable media */
	unsigned device_type		:5;	/* Device type */
	unsigned reserved13		:1;	/* Reserved */
	unsigned protocol		:2;	/* Protocol type */
};

/*
 *	INQUIRY packet command - Data Format (From Table 6-8 of QIC-157C)
 */
typedef struct {
	unsigned	device_type	:5;	/* Peripheral Device Type */
	unsigned	reserved0_765	:3;	/* Peripheral Qualifier - Reserved */
	unsigned	reserved1_6t0	:7;	/* Reserved */
	unsigned	rmb		:1;	/* Removable Medium Bit */
	unsigned	ansi_version	:3;	/* ANSI Version */
	unsigned	ecma_version	:3;	/* ECMA Version */
	unsigned	iso_version	:2;	/* ISO Version */
	unsigned	response_format :4;	/* Response Data Format */
	unsigned	reserved3_45	:2;	/* Reserved */
	unsigned	reserved3_6	:1;	/* TrmIOP - Reserved */
	unsigned	reserved3_7	:1;	/* AENC - Reserved */
	u8		additional_length;	/* Additional Length (total_length-4) */
	u8		rsv5, rsv6, rsv7;	/* Reserved */
	u8		vendor_id[8];		/* Vendor Identification */
	u8		product_id[16];		/* Product Identification */
	u8		revision_level[4];	/* Revision Level */
	u8		vendor_specific[20];	/* Vendor Specific - Optional */
	u8		reserved56t95[40];	/* Reserved - Optional */
						/* Additional information may be returned */
} idetape_inquiry_result_t;

/*
 *	READ POSITION packet command - Data Format (From Table 6-57)
 */
typedef struct {
	unsigned	reserved0_10	:2;	/* Reserved */
	unsigned	bpu		:1;	/* Block Position Unknown */	
	unsigned	reserved0_543	:3;	/* Reserved */
	unsigned	eop		:1;	/* End Of Partition */
	unsigned	bop		:1;	/* Beginning Of Partition */
	u8		partition;		/* Partition Number */
	u8		reserved2, reserved3;	/* Reserved */
	u32		first_block;		/* First Block Location */
	u32		last_block;		/* Last Block Location (Optional) */
	u8		reserved12;		/* Reserved */
	u8		blocks_in_buffer[3];	/* Blocks In Buffer - (Optional) */
	u32		bytes_in_buffer;	/* Bytes In Buffer (Optional) */
} idetape_read_position_result_t;

/*
 *	REQUEST SENSE packet command result - Data Format.
 */
typedef struct {
	unsigned	error_code	:7;	/* Current of deferred errors */
	unsigned	valid		:1;	/* The information field conforms to QIC-157C */
	u8		reserved1	:8;	/* Segment Number - Reserved */
	unsigned	sense_key	:4;	/* Sense Key */
	unsigned	reserved2_4	:1;	/* Reserved */
	unsigned	ili		:1;	/* Incorrect Length Indicator */
	unsigned	eom		:1;	/* End Of Medium */
	unsigned	filemark 	:1;	/* Filemark */
	u32		information __attribute__ ((packed));
	u8		asl;			/* Additional sense length (n-7) */
	u32		command_specific;	/* Additional command specific information */
	u8		asc;			/* Additional Sense Code */
	u8		ascq;			/* Additional Sense Code Qualifier */
	u8		replaceable_unit_code;	/* Field Replaceable Unit Code */
	unsigned	sk_specific1 	:7;	/* Sense Key Specific */
	unsigned	sksv		:1;	/* Sense Key Specific information is valid */
	u8		sk_specific2;		/* Sense Key Specific */
	u8		sk_specific3;		/* Sense Key Specific */
	u8		pad[2];			/* Padding to 20 bytes */
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
	u8		mode_data_length;	/* Length of the following data transfer */
	u8		medium_type;		/* Medium Type */
	u8		dsp;			/* Device Specific Parameter */
	u8		bdl;			/* Block Descriptor Length */
} idetape_mode_parameter_header_t;

/*
 *	Mode Parameter Block Descriptor the MODE SENSE packet command
 *
 *	Support for block descriptors is optional.
 */
typedef struct {
	u8		density_code;		/* Medium density code */
	u8		blocks[3];		/* Number of blocks */
	u8		reserved4;		/* Reserved */
	u8		length[3];		/* Block Length */
} idetape_parameter_block_descriptor_t;

/*
 *	The Data Compression Page, as returned by the MODE SENSE packet command.
 */
typedef struct {
	unsigned	page_code	:6;	/* Page Code - Should be 0xf */
	unsigned	reserved0	:1;	/* Reserved */
	unsigned	ps		:1;
	u8		page_length;		/* Page Length - Should be 14 */
	unsigned	reserved2	:6;	/* Reserved */
	unsigned	dcc		:1;	/* Data Compression Capable */
	unsigned	dce		:1;	/* Data Compression Enable */
	unsigned	reserved3	:5;	/* Reserved */
	unsigned	red		:2;	/* Report Exception on Decompression */
	unsigned	dde		:1;	/* Data Decompression Enable */
	u32		ca;			/* Compression Algorithm */
	u32		da;			/* Decompression Algorithm */
	u8		reserved[4];		/* Reserved */
} idetape_data_compression_page_t;

/*
 *	The Medium Partition Page, as returned by the MODE SENSE packet command.
 */
typedef struct {
	unsigned	page_code	:6;	/* Page Code - Should be 0x11 */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	ps		:1;
	u8		page_length;		/* Page Length - Should be 6 */
	u8		map;			/* Maximum Additional Partitions - Should be 0 */
	u8		apd;			/* Additional Partitions Defined - Should be 0 */
	unsigned	reserved4_012	:3;	/* Reserved */
	unsigned	psum		:2;	/* Should be 0 */
	unsigned	idp		:1;	/* Should be 0 */
	unsigned	sdp		:1;	/* Should be 0 */
	unsigned	fdp		:1;	/* Fixed Data Partitions */
	u8		mfr;			/* Medium Format Recognition */
	u8		reserved[2];		/* Reserved */
} idetape_medium_partition_page_t;

/*
 *	Run time configurable parameters.
 */
typedef struct {
	int	dsc_rw_frequency;
	int	dsc_media_access_frequency;
	int	nr_stages;
} idetape_config_t;

/*
 *	The variables below are used for the character device interface.
 *	Additional state variables are defined in our ide_drive_t structure.
 */
static idetape_chrdev_t idetape_chrdevs[MAX_HWIFS * MAX_DRIVES];
static int idetape_chrdev_present = 0;

/*
 *	Too bad. The drive wants to send us data which we are not ready to accept.
 *	Just throw it away.
 */
static void idetape_discard_data (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		IN_BYTE (IDE_DATA_REG);
}

static void idetape_input_buffers (ide_drive_t *drive, idetape_pc_t *pc, unsigned int bcount)
{
	struct buffer_head *bh = pc->bh;
	int count;

	while (bcount) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk (KERN_ERR "ide-tape: bh == NULL in idetape_input_buffers\n");
			idetape_discard_data (drive, bcount);
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = IDE_MIN (bh->b_size - bh->b_count, bcount);
		atapi_input_bytes (drive, bh->b_data + bh->b_count, count);
		bcount -= count; bh->b_count += count;
		if (bh->b_count == bh->b_size) {
			bh = bh->b_reqnext;
			if (bh)
				bh->b_count = 0;
		}
	}
	pc->bh = bh;
}

static void idetape_output_buffers (ide_drive_t *drive, idetape_pc_t *pc, unsigned int bcount)
{
	struct buffer_head *bh = pc->bh;
	int count;

	while (bcount) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk (KERN_ERR "ide-tape: bh == NULL in idetape_output_buffers\n");
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = IDE_MIN (pc->b_count, bcount);
		atapi_output_bytes (drive, pc->b_data, count);
		bcount -= count; pc->b_data += count; pc->b_count -= count;
		if (!pc->b_count) {
			pc->bh = bh = bh->b_reqnext;
			if (bh) {
				pc->b_data = bh->b_data;
				pc->b_count = bh->b_count;
			}
		}
	}
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static void idetape_update_buffers (idetape_pc_t *pc)
{
	struct buffer_head *bh = pc->bh;
	int count, bcount = pc->actually_transferred;

	if (test_bit (PC_WRITING, &pc->flags))
		return;
	while (bcount) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk (KERN_ERR "ide-tape: bh == NULL in idetape_update_buffers\n");
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = IDE_MIN (bh->b_size, bcount);
		bh->b_count = count;
		if (bh->b_count == bh->b_size)
			bh = bh->b_reqnext;
		bcount -= count;
	}
	pc->bh = bh;
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

/*
 *	idetape_postpone_request postpones the current request so that
 *	ide.c will be able to service requests from another device on
 *	the same hwgroup while we are polling for DSC.
 */
static void idetape_postpone_request (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	tape->postponed_rq = HWGROUP(drive)->rq;
	ide_stall_queue(drive, tape->dsc_polling_frequency);
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
static void idetape_queue_pc_head (ide_drive_t *drive,idetape_pc_t *pc,struct request *rq)
{
	ide_init_drive_cmd (rq);
	rq->buffer = (char *) pc;
	rq->cmd = IDETAPE_PC_RQ1;
	(void) ide_do_drive_cmd (drive, rq, ide_preempt);
}

/*
 *	idetape_next_pc_storage returns a pointer to a place in which we can
 *	safely store a packet command, even though we intend to leave the
 *	driver. A storage space for a maximum of IDETAPE_PC_STACK packet
 *	commands is allocated at initialization time.
 */
static idetape_pc_t *idetape_next_pc_storage (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "ide-tape: pc_stack_index=%d\n",tape->pc_stack_index);
#endif /* IDETAPE_DEBUG_LOG */
	if (tape->pc_stack_index==IDETAPE_PC_STACK)
		tape->pc_stack_index=0;
	return (&tape->pc_stack[tape->pc_stack_index++]);
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
 
static struct request *idetape_next_rq_storage (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "ide-tape: rq_stack_index=%d\n",tape->rq_stack_index);
#endif /* IDETAPE_DEBUG_LOG */
	if (tape->rq_stack_index==IDETAPE_PC_STACK)
		tape->rq_stack_index=0;
	return (&tape->rq_stack[tape->rq_stack_index++]);
}

/*
 *	Pipeline related functions
 */

static inline int idetape_pipeline_active (idetape_tape_t *tape)
{
	return tape->active_data_request != NULL;
}

/*
 *	idetape_kfree_stage calls kfree to completely free a stage, along with
 *	its related buffers.
 */
static void __idetape_kfree_stage (idetape_stage_t *stage)
{
	struct buffer_head *prev_bh, *bh = stage->bh;
	int size;

	while (bh != NULL) {
		if (bh->b_data != NULL) {
			size = (int) bh->b_size;
			while (size > 0) {
				free_page ((unsigned long) bh->b_data);
				size -= PAGE_SIZE;
				bh->b_data += PAGE_SIZE;
			}
		}
		prev_bh = bh;
		bh = bh->b_reqnext;
		kfree (prev_bh);
	}
	kfree (stage);
}

static void idetape_kfree_stage (idetape_tape_t *tape, idetape_stage_t *stage)
{
	if (tape->cache_stage == NULL)
		tape->cache_stage = stage;
	else
		__idetape_kfree_stage (stage);
}

/*
 *	idetape_kmalloc_stage uses __get_free_page to allocate a pipeline
 *	stage, along with all the necessary small buffers which together make
 *	a buffer of size tape->stage_size (or a bit more). We attempt to
 *	combine sequential pages as much as possible.
 *
 *	Returns a pointer to the new allocated stage, or NULL if we
 *	can't (or don't want to) allocate a stage.
 *
 *	Pipeline stages are optional and are used to increase performance.
 *	If we can't allocate them, we'll manage without them.
 */
static idetape_stage_t *__idetape_kmalloc_stage (idetape_tape_t *tape)
{
	idetape_stage_t *stage;
	struct buffer_head *prev_bh, *bh;
	int pages = tape->pages_per_stage;
	char *b_data;

	if ((stage = (idetape_stage_t *) kmalloc (sizeof (idetape_stage_t),GFP_KERNEL)) == NULL)
		return NULL;
	stage->next = NULL;

	bh = stage->bh = (struct buffer_head *) kmalloc (sizeof (struct buffer_head), GFP_KERNEL);
	if (bh == NULL)
		goto abort;
	bh->b_reqnext = NULL;
	if ((bh->b_data = (char *) __get_free_page (GFP_KERNEL)) == NULL)
		goto abort;
	bh->b_size = PAGE_SIZE;
	set_bit (BH_Lock, &bh->b_state);

	while (--pages) {
		if ((b_data = (char *) __get_free_page (GFP_KERNEL)) == NULL)
			goto abort;
		if (bh->b_data == b_data + PAGE_SIZE && virt_to_bus (bh->b_data) == virt_to_bus (b_data) + PAGE_SIZE) {
			bh->b_size += PAGE_SIZE;
			bh->b_data -= PAGE_SIZE;
			continue;
		}
		if (b_data == bh->b_data + bh->b_size && virt_to_bus (b_data) == virt_to_bus (bh->b_data) + bh->b_size) {
			bh->b_size += PAGE_SIZE;
			continue;
		}
		prev_bh = bh;
		if ((bh = (struct buffer_head *) kmalloc (sizeof (struct buffer_head), GFP_KERNEL)) == NULL) {
			free_page ((unsigned long) b_data);
			goto abort;
		}
		bh->b_reqnext = NULL;
		bh->b_data = b_data;
		bh->b_size = PAGE_SIZE;
		set_bit (BH_Lock, &bh->b_state);
		prev_bh->b_reqnext = bh;
	}
	bh->b_size -= tape->excess_bh_size;
	return stage;
abort:
	__idetape_kfree_stage (stage);
	return NULL;
}

static idetape_stage_t *idetape_kmalloc_stage (idetape_tape_t *tape)
{
	idetape_stage_t *cache_stage = tape->cache_stage;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_kmalloc_stage\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->nr_stages >= tape->max_stages)
		return NULL;
	if (cache_stage != NULL) {
		tape->cache_stage = NULL;
		return cache_stage;
	}
	return __idetape_kmalloc_stage (tape);
}

static void idetape_copy_stage_from_user (idetape_tape_t *tape, idetape_stage_t *stage, const char *buf, int n)
{
	struct buffer_head *bh = tape->bh;
	int count;

	while (n) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk (KERN_ERR "ide-tape: bh == NULL in idetape_copy_stage_from_user\n");
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = IDE_MIN (bh->b_size - bh->b_count, n);
		copy_from_user (bh->b_data + bh->b_count, buf, count);
		n -= count; bh->b_count += count; buf += count;
		if (bh->b_count == bh->b_size) {
			bh = bh->b_reqnext;
			if (bh)
				bh->b_count = 0;
		}
	}
	tape->bh = bh;
}

static void idetape_copy_stage_to_user (idetape_tape_t *tape, char *buf, idetape_stage_t *stage, int n)
{
	struct buffer_head *bh = tape->bh;
	int count;

	while (n) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk (KERN_ERR "ide-tape: bh == NULL in idetape_copy_stage_to_user\n");
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = IDE_MIN (tape->b_count, n);
		copy_to_user (buf, tape->b_data, count);
		n -= count; tape->b_data += count; tape->b_count -= count; buf += count;
		if (!tape->b_count) {
			tape->bh = bh = bh->b_reqnext;
			if (bh) {
				tape->b_data = bh->b_data;
				tape->b_count = bh->b_count;
			}
		}
	}
}

static void idetape_init_merge_stage (idetape_tape_t *tape)
{
	struct buffer_head *bh = tape->merge_stage->bh;
	
	tape->bh = bh;
	if (tape->chrdev_direction == idetape_direction_write)
		bh->b_count = 0;
	else {
		tape->b_data = bh->b_data;
		tape->b_count = bh->b_count;
	}
}

static void idetape_switch_buffers (idetape_tape_t *tape, idetape_stage_t *stage)
{
	struct buffer_head *tmp;

	tmp = stage->bh;
	stage->bh = tape->merge_stage->bh;
	tape->merge_stage->bh = tmp;
	idetape_init_merge_stage (tape);
}

/*
 *	idetape_increase_max_pipeline_stages is a part of the feedback
 *	loop which tries to find the optimum number of stages. In the
 *	feedback loop, we are starting from a minimum maximum number of
 *	stages, and if we sense that the pipeline is empty, we try to
 *	increase it, until we reach the user compile time memory limit.
 */
static void idetape_increase_max_pipeline_stages (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	int increase = (tape->max_pipeline - tape->min_pipeline) / 10;
	
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_increase_max_pipeline_stages\n");
#endif /* IDETAPE_DEBUG_LOG */

	tape->max_stages += increase;
	tape->max_stages = IDE_MAX(tape->max_stages, tape->min_pipeline);
	tape->max_stages = IDE_MIN(tape->max_stages, tape->max_pipeline);
}

/*
 *	idetape_add_stage_tail adds a new stage at the end of the pipeline.
 */
static void idetape_add_stage_tail (ide_drive_t *drive,idetape_stage_t *stage)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;
	
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_add_stage_tail\n");
#endif /* IDETAPE_DEBUG_LOG */
	spin_lock_irqsave(&HWGROUP(drive)->spinlock, flags);
	stage->next=NULL;
	if (tape->last_stage != NULL)
		tape->last_stage->next=stage;
	else
		tape->first_stage=tape->next_stage=stage;
	tape->last_stage=stage;
	if (tape->next_stage == NULL)
		tape->next_stage=tape->last_stage;
	tape->nr_stages++;
	tape->nr_pending_stages++;
	spin_unlock_irqrestore(&HWGROUP(drive)->spinlock, flags);
}

/*
 *	idetape_remove_stage_head removes tape->first_stage from the pipeline.
 *	The caller should avoid race conditions.
 */
static void idetape_remove_stage_head (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage;
	
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_remove_stage_head\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (tape->first_stage == NULL) {
		printk (KERN_ERR "ide-tape: bug: tape->first_stage is NULL\n");
		return;		
	}
	if (tape->active_stage == tape->first_stage) {
		printk (KERN_ERR "ide-tape: bug: Trying to free our active pipeline stage\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	stage=tape->first_stage;
	tape->first_stage=stage->next;
	idetape_kfree_stage (tape, stage);
	tape->nr_stages--;
	if (tape->first_stage == NULL) {
		tape->last_stage=NULL;
#if IDETAPE_DEBUG_BUGS
		if (tape->next_stage != NULL)
			printk (KERN_ERR "ide-tape: bug: tape->next_stage != NULL\n");
		if (tape->nr_stages)
			printk (KERN_ERR "ide-tape: bug: nr_stages should be 0 now\n");
#endif /* IDETAPE_DEBUG_BUGS */
	}
}

/*
 *	idetape_active_next_stage will declare the next stage as "active".
 */
static void idetape_active_next_stage (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage=tape->next_stage;
	struct request *rq = &stage->rq;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_active_next_stage\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (stage == NULL) {
		printk (KERN_ERR "ide-tape: bug: Trying to activate a non existing stage\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */	

	rq->buffer = NULL;
	rq->bh = stage->bh;
	tape->active_data_request=rq;
	tape->active_stage=stage;
	tape->next_stage=stage->next;
}

/*
 *	idetape_insert_pipeline_into_queue is used to start servicing the
 *	pipeline stages, starting from tape->next_stage.
 */
static void idetape_insert_pipeline_into_queue (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	if (tape->next_stage == NULL)
		return;
	if (!idetape_pipeline_active (tape)) {
		idetape_active_next_stage (drive);
		(void) ide_do_drive_cmd (drive, tape->active_data_request, ide_end);
	}
}

static void idetape_abort_pipeline (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage = tape->next_stage;

	while (stage) {
		stage->rq.cmd = IDETAPE_ABORTED_WRITE_RQ;
		stage = stage->next;
	}
}

/*
 *	idetape_end_request is used to finish servicing a request, and to
 *	insert a pending pipeline request into the main device queue.
 */
static void idetape_end_request (byte uptodate, ide_hwgroup_t *hwgroup)
{
	ide_drive_t *drive = hwgroup->drive;
	struct request *rq = hwgroup->rq;
	idetape_tape_t *tape = drive->driver_data;
	int error;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_end_request\n");
#endif /* IDETAPE_DEBUG_LOG */

	switch (uptodate) {
		case 0:	error = IDETAPE_ERROR_GENERAL; break;
		case 1: error = 0; break;
		default: error = uptodate;
	}
	rq->errors = error;
	if (error)
		tape->failed_pc = NULL;

	if (tape->active_data_request == rq) {		/* The request was a pipelined data transfer request */
		tape->active_stage = NULL;
		tape->active_data_request = NULL;
		tape->nr_pending_stages--;
		if (rq->cmd == IDETAPE_WRITE_RQ) {
			if (error) {
				set_bit (IDETAPE_PIPELINE_ERROR, &tape->flags);
				if (error == IDETAPE_ERROR_EOD)
					idetape_abort_pipeline (drive);
			}
			idetape_remove_stage_head (drive);
		}
		if (tape->next_stage != NULL) {
			idetape_active_next_stage (drive);

			/*
			 *	Insert the next request into the request queue.
			 *	The choice of using ide_next or ide_end is now left to the user.
			 */
#if IDETAPE_LOW_TAPE_PRIORITY
			(void) ide_do_drive_cmd (drive, tape->active_data_request, ide_end);
#else
			(void) ide_do_drive_cmd (drive, tape->active_data_request, ide_next);
#endif /* IDETAPE_LOW_TAPE_PRIORITY */
		} else if (!error)
			idetape_increase_max_pipeline_stages (drive);
	}
	ide_end_drive_cmd (drive, 0, 0);
}

/*
 *	idetape_analyze_error is called on each failed packet command retry
 *	to analyze the request sense. We currently do not utilize this
 *	information.
 */
static void idetape_analyze_error (ide_drive_t *drive,idetape_request_sense_result_t *result)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->failed_pc;
		
	tape->sense_key = result->sense_key; tape->asc = result->asc; tape->ascq = result->ascq;
#if IDETAPE_DEBUG_LOG
	/*
	 *	Without debugging, we only log an error if we decided to
	 *	give up retrying.
	 */
	printk (KERN_INFO "ide-tape: pc = %x, sense key = %x, asc = %x, ascq = %x\n",pc->c[0],result->sense_key,result->asc,result->ascq);
#endif /* IDETAPE_DEBUG_LOG */

#ifdef CONFIG_BLK_DEV_IDEDMA

	/*
	 *	Correct pc->actually_transferred by asking the tape.
	 */
	if (test_bit (PC_DMA_ERROR, &pc->flags)) {
		pc->actually_transferred = pc->request_transfer - tape->tape_block_size * ntohl (get_unaligned (&result->information));
		idetape_update_buffers (pc);
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */
	if (pc->c[0] == IDETAPE_READ_CMD && result->filemark) {
		pc->error = IDETAPE_ERROR_FILEMARK;
		set_bit (PC_ABORT, &pc->flags);
	}
	if (pc->c[0] == IDETAPE_WRITE_CMD) {
		if (result->eom || (result->sense_key == 0xd && result->asc == 0x0 && result->ascq == 0x2)) {
			pc->error = IDETAPE_ERROR_EOD;
			set_bit (PC_ABORT, &pc->flags);
		}
	}
	if (pc->c[0] == IDETAPE_READ_CMD || pc->c[0] == IDETAPE_WRITE_CMD) {
		if (result->sense_key == 8) {
			pc->error = IDETAPE_ERROR_EOD;
			set_bit (PC_ABORT, &pc->flags);
		}
		if (!test_bit (PC_ABORT, &pc->flags) && pc->actually_transferred)
			pc->retries = IDETAPE_MAX_PC_RETRIES + 1;
	}
}

static void idetape_request_sense_callback (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "ide-tape: Reached idetape_request_sense_callback\n");
#endif /* IDETAPE_DEBUG_LOG */
	if (!tape->pc->error) {
		idetape_analyze_error (drive,(idetape_request_sense_result_t *) tape->pc->buffer);
		idetape_end_request (1,HWGROUP (drive));
	} else {
		printk (KERN_ERR "Error in REQUEST SENSE itself - Aborting request!\n");
		idetape_end_request (0,HWGROUP (drive));
	}
}

/*
 *	idetape_init_pc initializes a packet command.
 */
static void idetape_init_pc (idetape_pc_t *pc)
{
	memset (pc->c, 0, 12);
	pc->retries = 0;
	pc->flags = 0;
	pc->request_transfer = 0;
	pc->buffer = pc->pc_buffer;
	pc->buffer_size = IDETAPE_PC_BUFFER_SIZE;
	pc->bh = NULL;
	pc->b_data = NULL;
}

static void idetape_create_request_sense_cmd (idetape_pc_t *pc)
{
	idetape_init_pc (pc);	
	pc->c[0] = IDETAPE_REQUEST_SENSE_CMD;
	pc->c[4] = 255;
	pc->request_transfer = 18;
	pc->callback = &idetape_request_sense_callback;
}

/*
 *	idetape_retry_pc is called when an error was detected during the
 *	last packet command. We queue a request sense packet command in
 *	the head of the request list.
 */
static void idetape_retry_pc (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc;
	struct request *rq;
	idetape_error_reg_t error;

	error.all = IN_BYTE (IDE_ERROR_REG);
	pc = idetape_next_pc_storage (drive);
	rq = idetape_next_rq_storage (drive);
	idetape_create_request_sense_cmd (pc);
	set_bit (IDETAPE_IGNORE_DSC, &tape->flags);
	idetape_queue_pc_head (drive, pc, rq);
}

/*
 *	idetape_pc_intr is the usual interrupt handler which will be called
 *	during a packet command. We will transfer some of the data (as
 *	requested by the drive) and will re-point interrupt handler to us.
 *	When data transfer is finished, we will act according to the
 *	algorithm described before idetape_issue_packet_command.
 *
 */
static void idetape_pc_intr (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_status_reg_t status;
	idetape_bcount_reg_t bcount;
	idetape_ireason_reg_t ireason;
	idetape_pc_t *pc=tape->pc;
	unsigned int temp;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "ide-tape: Reached idetape_pc_intr interrupt handler\n");
#endif /* IDETAPE_DEBUG_LOG */	

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_bit (PC_DMA_IN_PROGRESS, &pc->flags)) {
		if (HWIF(drive)->dmaproc(ide_dma_end, drive)) {
			/*
			 * A DMA error is sometimes expected. For example,
			 * if the tape is crossing a filemark during a
			 * READ command, it will issue an irq and position
			 * itself before the filemark, so that only a partial
			 * data transfer will occur (which causes the DMA
			 * error). In that case, we will later ask the tape
			 * how much bytes of the original request were
			 * actually transferred (we can't receive that
			 * information from the DMA engine on most chipsets).
			 */
			set_bit (PC_DMA_ERROR, &pc->flags);
		} else {
			pc->actually_transferred=pc->request_transfer;
			idetape_update_buffers (pc);
		}
#if IDETAPE_DEBUG_LOG
		printk (KERN_INFO "ide-tape: DMA finished\n");
#endif /* IDETAPE_DEBUG_LOG */
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */

	status.all = GET_STAT();					/* Clear the interrupt */

	if (!status.b.drq) {						/* No more interrupts */
#if IDETAPE_DEBUG_LOG
		printk (KERN_INFO "Packet command completed, %d bytes transferred\n", pc->actually_transferred);
#endif /* IDETAPE_DEBUG_LOG */
		clear_bit (PC_DMA_IN_PROGRESS, &pc->flags);

		ide__sti();	/* local CPU only */

		if (status.b.check && pc->c[0] == IDETAPE_REQUEST_SENSE_CMD)
			status.b.check = 0;
		if (status.b.check || test_bit (PC_DMA_ERROR, &pc->flags)) {	/* Error detected */
#if IDETAPE_DEBUG_LOG
			printk (KERN_INFO "ide-tape: %s: I/O error, ",tape->name);
#endif /* IDETAPE_DEBUG_LOG */
			if (pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
				printk (KERN_ERR "ide-tape: I/O error in request sense command\n");
				ide_do_reset (drive);
				return;
			}
			idetape_retry_pc (drive);				/* Retry operation */
			return;
		}
		pc->error = 0;
		if (test_bit (PC_WAIT_FOR_DSC, &pc->flags) && !status.b.dsc) {	/* Media access command */
			tape->dsc_polling_start = jiffies;
			tape->dsc_polling_frequency = IDETAPE_DSC_MA_FAST;
			tape->dsc_timeout = jiffies + IDETAPE_DSC_MA_TIMEOUT;
			idetape_postpone_request (drive);		/* Allow ide.c to handle other requests */
			return;
		}
		if (tape->failed_pc == pc)
			tape->failed_pc=NULL;
		pc->callback(drive);			/* Command finished - Call the callback function */
		return;
	}
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_and_clear_bit (PC_DMA_IN_PROGRESS, &pc->flags)) {
		printk (KERN_ERR "ide-tape: The tape wants to issue more interrupts in DMA mode\n");
		printk (KERN_ERR "ide-tape: DMA disabled, reverting to PIO\n");
		(void) HWIF(drive)->dmaproc(ide_dma_off, drive);
		ide_do_reset (drive);
		return;
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */
	bcount.b.high=IN_BYTE (IDE_BCOUNTH_REG);			/* Get the number of bytes to transfer */
	bcount.b.low=IN_BYTE (IDE_BCOUNTL_REG);				/* on this interrupt */
	ireason.all=IN_BYTE (IDE_IREASON_REG);

	if (ireason.b.cod) {
		printk (KERN_ERR "ide-tape: CoD != 0 in idetape_pc_intr\n");
		ide_do_reset (drive);
		return;
	}
	if (ireason.b.io == test_bit (PC_WRITING, &pc->flags)) {	/* Hopefully, we will never get here */
		printk (KERN_ERR "ide-tape: We wanted to %s, ", ireason.b.io ? "Write":"Read");
		printk (KERN_ERR "but the tape wants us to %s !\n",ireason.b.io ? "Read":"Write");
		ide_do_reset (drive);
		return;
	}
	if (!test_bit (PC_WRITING, &pc->flags)) {			/* Reading - Check that we have enough space */
		temp = pc->actually_transferred + bcount.all;
		if ( temp > pc->request_transfer) {
			if (temp > pc->buffer_size) {
				printk (KERN_ERR "ide-tape: The tape wants to send us more data than expected - discarding data\n");
				idetape_discard_data (drive,bcount.all);
				ide_set_handler (drive,&idetape_pc_intr,IDETAPE_WAIT_CMD);
				return;
			}
#if IDETAPE_DEBUG_LOG
			printk (KERN_NOTICE "ide-tape: The tape wants to send us more data than expected - allowing transfer\n");
#endif /* IDETAPE_DEBUG_LOG */
		}
	}
	if (test_bit (PC_WRITING, &pc->flags)) {
		if (pc->bh != NULL)
			idetape_output_buffers (drive, pc, bcount.all);
		else
			atapi_output_bytes (drive,pc->current_position,bcount.all);	/* Write the current buffer */
	} else {
		if (pc->bh != NULL)
			idetape_input_buffers (drive, pc, bcount.all);
		else
			atapi_input_bytes (drive,pc->current_position,bcount.all);	/* Read the current buffer */
	}
	pc->actually_transferred+=bcount.all;					/* Update the current position */
	pc->current_position+=bcount.all;

	ide_set_handler (drive,&idetape_pc_intr,IDETAPE_WAIT_CMD);		/* And set the interrupt handler again */
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

static void idetape_transfer_pc(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->pc;
	idetape_ireason_reg_t ireason;
	int retries = 100;

	if (ide_wait_stat (drive,DRQ_STAT,BUSY_STAT,WAIT_READY)) {
		printk (KERN_ERR "ide-tape: Strange, packet command initiated yet DRQ isn't asserted\n");
		return;
	}
	ireason.all=IN_BYTE (IDE_IREASON_REG);
	while (retries-- && (!ireason.b.cod || ireason.b.io)) {
		printk(KERN_ERR "ide-tape: (IO,CoD != (0,1) while issuing a packet command, retrying\n");
		udelay(100);
		ireason.all = IN_BYTE(IDE_IREASON_REG);
		if (retries == 0) {
			printk(KERN_ERR "ide-tape: (IO,CoD != (0,1) while issuing a packet command, ignoring\n");
			ireason.b.cod = 1;
			ireason.b.io = 0;
		}
	}
	if (!ireason.b.cod || ireason.b.io) {
		printk (KERN_ERR "ide-tape: (IO,CoD) != (0,1) while issuing a packet command\n");
		ide_do_reset (drive);
		return;
	}
	ide_set_handler(drive, &idetape_pc_intr, IDETAPE_WAIT_CMD);	/* Set the interrupt routine */
	atapi_output_bytes (drive,pc->c,12);			/* Send the actual packet */
}

static void idetape_issue_packet_command (ide_drive_t *drive, idetape_pc_t *pc)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_bcount_reg_t bcount;
	int dma_ok=0;

#if IDETAPE_DEBUG_BUGS
	if (tape->pc->c[0] == IDETAPE_REQUEST_SENSE_CMD && pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
		printk (KERN_ERR "ide-tape: possible ide-tape.c bug - Two request sense in serial were issued\n");
	}
#endif /* IDETAPE_DEBUG_BUGS */

	if (tape->failed_pc == NULL && pc->c[0] != IDETAPE_REQUEST_SENSE_CMD)
		tape->failed_pc=pc;
	tape->pc=pc;							/* Set the current packet command */

	if (pc->retries > IDETAPE_MAX_PC_RETRIES || test_bit (PC_ABORT, &pc->flags)) {
		/*
		 *	We will "abort" retrying a packet command in case
		 *	a legitimate error code was received (crossing a
		 *	filemark, or DMA error in the end of media, for
		 *	example).
		 */
		if (!test_bit (PC_ABORT, &pc->flags)) {
			printk (KERN_ERR "ide-tape: %s: I/O error, pc = %2x, key = %2x, asc = %2x, ascq = %2x\n",
				tape->name, pc->c[0], tape->sense_key, tape->asc, tape->ascq);
			pc->error = IDETAPE_ERROR_GENERAL;		/* Giving up */
		}
		tape->failed_pc=NULL;
		pc->callback(drive);
		return;
	}
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Retry number - %d\n",pc->retries);
#endif /* IDETAPE_DEBUG_LOG */

	pc->retries++;
	pc->actually_transferred=0;					/* We haven't transferred any data yet */
	pc->current_position=pc->buffer;
	bcount.all=pc->request_transfer;				/* Request to transfer the entire buffer at once */

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_and_clear_bit (PC_DMA_ERROR, &pc->flags)) {
		printk (KERN_WARNING "ide-tape: DMA disabled, reverting to PIO\n");
		(void) HWIF(drive)->dmaproc(ide_dma_off, drive);
	}
	if (test_bit (PC_DMA_RECOMMENDED, &pc->flags) && drive->using_dma)
		dma_ok=!HWIF(drive)->dmaproc(test_bit (PC_WRITING, &pc->flags) ? ide_dma_write : ide_dma_read, drive);
#endif /* CONFIG_BLK_DEV_IDEDMA */

	OUT_BYTE (drive->ctl,IDE_CONTROL_REG);
	OUT_BYTE (dma_ok ? 1:0,IDE_FEATURE_REG);			/* Use PIO/DMA */
	OUT_BYTE (bcount.b.high,IDE_BCOUNTH_REG);
	OUT_BYTE (bcount.b.low,IDE_BCOUNTL_REG);
	OUT_BYTE (drive->select.all,IDE_SELECT_REG);
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (dma_ok) {						/* Begin DMA, if necessary */
		set_bit (PC_DMA_IN_PROGRESS, &pc->flags);
		(void) (HWIF(drive)->dmaproc(ide_dma_begin, drive));
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */
	if (test_bit(IDETAPE_DRQ_INTERRUPT, &tape->flags)) {
		ide_set_handler(drive, &idetape_transfer_pc, IDETAPE_WAIT_CMD);
		OUT_BYTE(WIN_PACKETCMD, IDE_COMMAND_REG);
	} else {
		OUT_BYTE(WIN_PACKETCMD, IDE_COMMAND_REG);
		idetape_transfer_pc(drive);
	}
}

static void idetape_media_access_finished (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->pc;
	idetape_status_reg_t status;

	status.all = GET_STAT();
	if (status.b.dsc) {
		if (status.b.check) {					/* Error detected */
			printk (KERN_ERR "ide-tape: %s: I/O error, ",tape->name);
			idetape_retry_pc (drive);			/* Retry operation */
			return;
		}
		pc->error = 0;
		if (tape->failed_pc == pc)
			tape->failed_pc = NULL;
	} else {
		pc->error = IDETAPE_ERROR_GENERAL;
		tape->failed_pc = NULL;
	}
	pc->callback (drive);
}

/*
 *	General packet command callback function.
 */
static void idetape_pc_callback (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "ide-tape: Reached idetape_pc_callback\n");
#endif /* IDETAPE_DEBUG_LOG */

	idetape_end_request (tape->pc->error ? 0:1, HWGROUP(drive));
}

static void idetape_rw_callback (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	int blocks = tape->pc->actually_transferred / tape->tape_block_size;

#if IDETAPE_DEBUG_LOG	
	printk (KERN_INFO "ide-tape: Reached idetape_rw_callback\n");
#endif /* IDETAPE_DEBUG_LOG */

	tape->block_address += blocks;
	rq->current_nr_sectors -= blocks;

	if (!tape->pc->error)
		idetape_end_request (1, HWGROUP (drive));
	else
		idetape_end_request (tape->pc->error, HWGROUP (drive));
}

static void idetape_create_locate_cmd (idetape_pc_t *pc, unsigned int block, byte partition)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_LOCATE_CMD;
	pc->c[1] = 2;
	put_unaligned (htonl (block), (unsigned int *) &pc->c[3]);
	pc->c[8] = partition;
	set_bit (PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_rewind_cmd (idetape_pc_t *pc)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_REWIND_CMD;
	set_bit (PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

/*
 *	A mode sense command is used to "sense" tape parameters.
 */
static void idetape_create_mode_sense_cmd (idetape_pc_t *pc, byte page_code)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_MODE_SENSE_CMD;
	pc->c[1] = 8;				/* DBD = 1 - Don't return block descriptors for now */
	pc->c[2] = page_code;
	pc->c[3] = 255;				/* Don't limit the returned information */
	pc->c[4] = 255;				/* (We will just discard data in that case) */
	if (page_code == IDETAPE_CAPABILITIES_PAGE)
		pc->request_transfer = 24;
#if IDETAPE_DEBUG_BUGS
	else
		printk (KERN_ERR "ide-tape: unsupported page code in create_mode_sense_cmd\n");
#endif /* IDETAPE_DEBUG_BUGS */
	pc->callback = &idetape_pc_callback;
}

/*
 *	idetape_create_write_filemark_cmd will:
 *
 *		1.	Write a filemark if write_filemark=1.
 *		2.	Flush the device buffers without writing a filemark
 *			if write_filemark=0.
 *
 */
static void idetape_create_write_filemark_cmd (idetape_pc_t *pc,int write_filemark)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_WRITE_FILEMARK_CMD;
	pc->c[4] = write_filemark;
	set_bit (PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_load_unload_cmd (idetape_pc_t *pc,int cmd)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_LOAD_UNLOAD_CMD;
	pc->c[4] = cmd;
	set_bit (PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_erase_cmd (idetape_pc_t *pc)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_ERASE_CMD;
	pc->c[1] = 1;
	set_bit (PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_read_cmd (idetape_tape_t *tape, idetape_pc_t *pc, unsigned int length, struct buffer_head *bh)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_READ_CMD;
	put_unaligned (htonl (length), (unsigned int *) &pc->c[1]);
	pc->c[1] = 1;
	pc->callback = &idetape_rw_callback;
	pc->bh = bh;
	bh->b_count = 0;
	pc->buffer = NULL;
	pc->request_transfer = pc->buffer_size = length * tape->tape_block_size;
	if (pc->request_transfer == tape->stage_size)
		set_bit (PC_DMA_RECOMMENDED, &pc->flags);
}

static void idetape_create_space_cmd (idetape_pc_t *pc,int count,byte cmd)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_SPACE_CMD;
	put_unaligned (htonl (count), (unsigned int *) &pc->c[1]);
	pc->c[1] = cmd;
	set_bit (PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_write_cmd (idetape_tape_t *tape, idetape_pc_t *pc, unsigned int length, struct buffer_head *bh)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_WRITE_CMD;
	put_unaligned (htonl (length), (unsigned int *) &pc->c[1]);
	pc->c[1] = 1;
	pc->callback = &idetape_rw_callback;
	set_bit (PC_WRITING, &pc->flags);
	pc->bh = bh;
	pc->b_data = bh->b_data;
	pc->b_count = bh->b_count;
	pc->buffer = NULL;
	pc->request_transfer = pc->buffer_size = length * tape->tape_block_size;
	if (pc->request_transfer == tape->stage_size)
		set_bit (PC_DMA_RECOMMENDED, &pc->flags);
}

static void idetape_read_position_callback (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_read_position_result_t *result;
	
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "ide-tape: Reached idetape_read_position_callback\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (!tape->pc->error) {
		result = (idetape_read_position_result_t *) tape->pc->buffer;
#if IDETAPE_DEBUG_LOG
		printk (KERN_INFO "BOP - %s\n",result->bop ? "Yes":"No");
		printk (KERN_INFO "EOP - %s\n",result->eop ? "Yes":"No");
#endif /* IDETAPE_DEBUG_LOG */
		if (result->bpu) {
			printk (KERN_INFO "ide-tape: Block location is unknown to the tape\n");
			clear_bit (IDETAPE_ADDRESS_VALID, &tape->flags);
			idetape_end_request (0,HWGROUP (drive));
		} else {
#if IDETAPE_DEBUG_LOG
			printk (KERN_INFO "Block Location - %lu\n", ntohl (result->first_block));
#endif /* IDETAPE_DEBUG_LOG */
			tape->partition = result->partition;
			tape->block_address = ntohl (result->first_block);
			set_bit (IDETAPE_ADDRESS_VALID, &tape->flags);
			idetape_end_request (1,HWGROUP (drive));
		}
	} else
		idetape_end_request (0,HWGROUP (drive));
}

static void idetape_create_read_position_cmd (idetape_pc_t *pc)
{
	idetape_init_pc (pc);
	pc->c[0] = IDETAPE_READ_POSITION_CMD;
	pc->request_transfer = 20;
	pc->callback = &idetape_read_position_callback;
}

/*
 *	idetape_do_request is our request handling function.	
 */
static void idetape_do_request (ide_drive_t *drive, struct request *rq, unsigned long block)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc;
	struct request *postponed_rq = tape->postponed_rq;
	idetape_status_reg_t status;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "rq_status: %d, rq_dev: %u, cmd: %d, errors: %d\n",rq->rq_status,(unsigned int) rq->rq_dev,rq->cmd,rq->errors);
	printk (KERN_INFO "sector: %ld, nr_sectors: %ld, current_nr_sectors: %ld\n",rq->sector,rq->nr_sectors,rq->current_nr_sectors);
#endif /* IDETAPE_DEBUG_LOG */

	if (!IDETAPE_RQ_CMD (rq->cmd)) {
		/*
		 *	We do not support buffer cache originated requests.
		 */
		printk (KERN_NOTICE "ide-tape: %s: Unsupported command in request queue\n", drive->name);
		ide_end_request (0,HWGROUP (drive));			/* Let the common code handle it */
		return;
	}

	/*
	 *	Retry a failed packet command
	 */
	if (tape->failed_pc != NULL && tape->pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
		idetape_issue_packet_command (drive, tape->failed_pc);
		return;
	}
#if IDETAPE_DEBUG_BUGS
	if (postponed_rq != NULL)
		if (rq != postponed_rq) {
			printk (KERN_ERR "ide-tape: ide-tape.c bug - Two DSC requests were queued\n");
			idetape_end_request (0,HWGROUP (drive));
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */

	tape->postponed_rq = NULL;

	/*
	 *	If the tape is still busy, postpone our request and service
	 *	the other device meanwhile.
	 */
	status.all = GET_STAT();
	if (!drive->dsc_overlap && rq->cmd != IDETAPE_PC_RQ2)
		set_bit (IDETAPE_IGNORE_DSC, &tape->flags);
	if (!test_and_clear_bit (IDETAPE_IGNORE_DSC, &tape->flags) && !status.b.dsc) {
		if (postponed_rq == NULL) {
			tape->dsc_polling_start = jiffies;
			tape->dsc_polling_frequency = tape->best_dsc_rw_frequency;
			tape->dsc_timeout = jiffies + IDETAPE_DSC_RW_TIMEOUT;
		} else if ((signed long) (jiffies - tape->dsc_timeout) > 0) {
			printk (KERN_ERR "ide-tape: %s: DSC timeout\n", tape->name);
			if (rq->cmd == IDETAPE_PC_RQ2)
				idetape_media_access_finished (drive);
			else
				ide_do_reset (drive);
			return;
		} else if (jiffies - tape->dsc_polling_start > IDETAPE_DSC_MA_THRESHOLD)
			tape->dsc_polling_frequency = IDETAPE_DSC_MA_SLOW;
		idetape_postpone_request (drive);
		return;
	}
	switch (rq->cmd) {
		case IDETAPE_READ_RQ:
			pc=idetape_next_pc_storage (drive);
			idetape_create_read_cmd (tape, pc, rq->current_nr_sectors, rq->bh);
			break;
		case IDETAPE_WRITE_RQ:
			pc=idetape_next_pc_storage (drive);
			idetape_create_write_cmd (tape, pc, rq->current_nr_sectors, rq->bh);
			break;
		case IDETAPE_ABORTED_WRITE_RQ:
			rq->cmd = IDETAPE_WRITE_RQ;
			rq->errors = IDETAPE_ERROR_EOD;
			idetape_end_request (1, HWGROUP(drive));
			return;
		case IDETAPE_PC_RQ1:
			pc=(idetape_pc_t *) rq->buffer;
			rq->cmd = IDETAPE_PC_RQ2;
			break;
		case IDETAPE_PC_RQ2:
			idetape_media_access_finished (drive);
			return;
		default:
			printk (KERN_ERR "ide-tape: bug in IDETAPE_RQ_CMD macro\n");
			idetape_end_request (0,HWGROUP (drive));
			return;
	}
	idetape_issue_packet_command (drive, pc);
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
static int idetape_queue_pc_tail (ide_drive_t *drive,idetape_pc_t *pc)
{
	struct request rq;

	ide_init_drive_cmd (&rq);
	rq.buffer = (char *) pc;
	rq.cmd = IDETAPE_PC_RQ1;
	return ide_do_drive_cmd (drive, &rq, ide_wait);
}

/*
 *	idetape_wait_for_request installs a semaphore in a pending request
 *	and sleeps until it is serviced.
 *
 *	The caller should ensure that the request will not be serviced
 *	before we install the semaphore (usually by disabling interrupts).
 */
static void idetape_wait_for_request (ide_drive_t *drive, struct request *rq)
{
	struct semaphore sem = MUTEX_LOCKED;

#if IDETAPE_DEBUG_BUGS
	if (rq == NULL || !IDETAPE_RQ_CMD (rq->cmd)) {
		printk (KERN_ERR "ide-tape: bug: Trying to sleep on non-valid request\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	rq->sem = &sem;
	spin_unlock(&HWGROUP(drive)->spinlock);
	down(&sem);
	spin_lock_irq(&HWGROUP(drive)->spinlock);
}

/*
 *	idetape_queue_rw_tail generates a read/write request for the block
 *	device interface and wait for it to be serviced.
 */
static int idetape_queue_rw_tail (ide_drive_t *drive, int cmd, int blocks, struct buffer_head *bh)
{
	idetape_tape_t *tape = drive->driver_data;
	struct request rq;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "idetape_queue_rw_tail: cmd=%d\n",cmd);
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (idetape_pipeline_active (tape)) {
		printk (KERN_ERR "ide-tape: bug: the pipeline is active in idetape_queue_rw_tail\n");
		return (0);
	}
#endif /* IDETAPE_DEBUG_BUGS */	

	ide_init_drive_cmd (&rq);
	rq.bh = bh;
	rq.cmd = cmd;
	rq.sector = tape->block_address;
	rq.nr_sectors = rq.current_nr_sectors = blocks;
	(void) ide_do_drive_cmd (drive, &rq, ide_wait);

	idetape_init_merge_stage (tape);
	if (rq.errors == IDETAPE_ERROR_GENERAL)
		return -EIO;
	return (tape->tape_block_size * (blocks-rq.current_nr_sectors));
}

/*
 *	idetape_add_chrdev_read_request is called from idetape_chrdev_read
 *	to service a character device read request and add read-ahead
 *	requests to our pipeline.
 */
static int idetape_add_chrdev_read_request (ide_drive_t *drive,int blocks)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *new_stage;
	unsigned long flags;
	struct request rq,*rq_ptr;
	int bytes_read;
	
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_add_chrdev_read_request\n");
#endif /* IDETAPE_DEBUG_LOG */

	ide_init_drive_cmd (&rq);
	rq.cmd = IDETAPE_READ_RQ;
	rq.sector = tape->block_address;
	rq.nr_sectors = rq.current_nr_sectors = blocks;

	if (idetape_pipeline_active (tape) || tape->nr_stages <= tape->max_stages / 4) {
		new_stage=idetape_kmalloc_stage (tape);
		while (new_stage != NULL) {
			new_stage->rq=rq;
			idetape_add_stage_tail (drive,new_stage);
			new_stage=idetape_kmalloc_stage (tape);
		}
		if (!idetape_pipeline_active (tape))
			idetape_insert_pipeline_into_queue (drive);
	}
	if (tape->first_stage == NULL) {
		/*
		 *	Linux is short on memory. Revert to non-pipelined
		 *	operation mode for this request.
		 */
		return (idetape_queue_rw_tail (drive, IDETAPE_READ_RQ, blocks, tape->merge_stage->bh));
	}
	spin_lock_irqsave(&HWGROUP(drive)->spinlock, flags);
	if (tape->active_stage == tape->first_stage)
		idetape_wait_for_request(drive, tape->active_data_request);
	spin_unlock_irqrestore(&HWGROUP(drive)->spinlock, flags);

	rq_ptr = &tape->first_stage->rq;
	bytes_read = tape->tape_block_size * (rq_ptr->nr_sectors - rq_ptr->current_nr_sectors);
	rq_ptr->nr_sectors = rq_ptr->current_nr_sectors = 0;

	idetape_switch_buffers (tape, tape->first_stage);

	if (rq_ptr->errors != IDETAPE_ERROR_FILEMARK) {
		clear_bit (IDETAPE_FILEMARK, &tape->flags);
		idetape_remove_stage_head (drive);
	} else
		set_bit (IDETAPE_FILEMARK, &tape->flags);
#if IDETAPE_DEBUG_BUGS
	if (bytes_read > blocks*tape->tape_block_size) {
		printk (KERN_ERR "ide-tape: bug: trying to return more bytes than requested\n");
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
static int idetape_add_chrdev_write_request (ide_drive_t *drive, int blocks)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *new_stage;
	unsigned long flags;
	struct request *rq;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_add_chrdev_write_request\n");
#endif /* IDETAPE_DEBUG_LOG */

     	/*
     	 *	Attempt to allocate a new stage.
	 *	Pay special attention to possible race conditions.
	 */
	while ((new_stage = idetape_kmalloc_stage (tape)) == NULL) {
		spin_lock_irqsave(&HWGROUP(drive)->spinlock, flags);
		if (idetape_pipeline_active (tape)) {
			idetape_wait_for_request(drive, tape->active_data_request);
			spin_unlock_irqrestore(&HWGROUP(drive)->spinlock, flags);
		} else {
			spin_unlock_irqrestore(&HWGROUP(drive)->spinlock, flags);
			idetape_insert_pipeline_into_queue (drive);
			if (idetape_pipeline_active (tape))
				continue;
			/*
			 *	Linux is short on memory. Fallback to
			 *	non-pipelined operation mode for this request.
			 */
			return idetape_queue_rw_tail (drive, IDETAPE_WRITE_RQ, blocks, tape->merge_stage->bh);
		}
	}
	rq = &new_stage->rq;
	ide_init_drive_cmd (rq);
	rq->cmd = IDETAPE_WRITE_RQ;
	rq->sector = tape->block_address;	/* Doesn't actually matter - We always assume sequential access */
	rq->nr_sectors = rq->current_nr_sectors = blocks;

	idetape_switch_buffers (tape, new_stage);
	idetape_add_stage_tail (drive,new_stage);

	/*
	 *	Check if we are currently servicing requests in the bottom
	 *	part of the driver.
	 *
	 *	If not, wait for the pipeline to be full enough (75%) before
	 *	starting to service requests, so that we will be able to
	 *	keep up with the higher speeds of the tape.
	 */
	if (!idetape_pipeline_active (tape) && tape->nr_stages >= (3 * tape->max_stages) / 4)
		idetape_insert_pipeline_into_queue (drive);

	if (test_and_clear_bit (IDETAPE_PIPELINE_ERROR, &tape->flags))		/* Return a deferred error */
		return -EIO;
	return blocks;
}

static void idetape_discard_read_pipeline (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;

#if IDETAPE_DEBUG_BUGS
	if (tape->chrdev_direction != idetape_direction_read) {
		printk (KERN_ERR "ide-tape: bug: Trying to discard read pipeline, but we are not reading.\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	tape->merge_stage_size = 0;
	if (tape->merge_stage != NULL) {
		__idetape_kfree_stage (tape->merge_stage);
		tape->merge_stage = NULL;
	}
	tape->chrdev_direction = idetape_direction_none;
	
	if (tape->first_stage == NULL)
		return;

	spin_lock_irqsave(&HWGROUP(drive)->spinlock, flags);
	tape->next_stage = NULL;
	if (idetape_pipeline_active (tape))
		idetape_wait_for_request(drive, tape->active_data_request);
	spin_unlock_irqrestore(&HWGROUP(drive)->spinlock, flags);

	while (tape->first_stage != NULL)
		idetape_remove_stage_head (drive);
	tape->nr_pending_stages = 0;
	tape->max_stages = tape->min_pipeline;
}

/*
 *	idetape_wait_for_pipeline will wait until all pending pipeline
 *	requests are serviced. Typically called on device close.
 */
static void idetape_wait_for_pipeline (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;

	if (!idetape_pipeline_active (tape))
		idetape_insert_pipeline_into_queue (drive);

	spin_lock_irqsave(&HWGROUP(drive)->spinlock, flags);
	if (!idetape_pipeline_active (tape))
		goto abort;
#if IDETAPE_DEBUG_BUGS
	if (tape->last_stage == NULL)
		printk ("ide-tape: tape->last_stage == NULL\n");
	else
#endif /* IDETAPE_DEBUG_BUGS */
	idetape_wait_for_request(drive, &tape->last_stage->rq);
abort:
	spin_unlock_irqrestore(&HWGROUP(drive)->spinlock, flags);
}

static void idetape_pad_zeros (ide_drive_t *drive, int bcount)
{
	idetape_tape_t *tape = drive->driver_data;
	struct buffer_head *bh;
	int count, blocks;
	
	while (bcount) {
		bh = tape->merge_stage->bh;
		count = IDE_MIN (tape->stage_size, bcount);
		bcount -= count;
		blocks = count / tape->tape_block_size;
		while (count) {
			bh->b_count = IDE_MIN (count, bh->b_size);
			memset (bh->b_data, 0, bh->b_count);
			count -= bh->b_count;
			bh = bh->b_reqnext;
		}
		idetape_queue_rw_tail (drive, IDETAPE_WRITE_RQ, blocks, tape->merge_stage->bh);
	}
}

static void idetape_empty_write_pipeline (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	int blocks, i;
	
#if IDETAPE_DEBUG_BUGS
	if (tape->chrdev_direction != idetape_direction_write) {
		printk (KERN_ERR "ide-tape: bug: Trying to empty write pipeline, but we are not writing.\n");
		return;
	}
	if (tape->merge_stage_size > tape->stage_size) {
		printk (KERN_ERR "ide-tape: bug: merge_buffer too big\n");
		tape->merge_stage_size = tape->stage_size;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	if (tape->merge_stage_size) {
		blocks=tape->merge_stage_size/tape->tape_block_size;
		if (tape->merge_stage_size % tape->tape_block_size) {
			blocks++;
			i = tape->tape_block_size - tape->merge_stage_size % tape->tape_block_size;
			memset (tape->bh->b_data + tape->bh->b_count, 0, i);
			tape->bh->b_count += i;
		}
		(void) idetape_add_chrdev_write_request (drive, blocks);
		tape->merge_stage_size = 0;
	}
	idetape_wait_for_pipeline (drive);
	if (tape->merge_stage != NULL) {
		__idetape_kfree_stage (tape->merge_stage);
		tape->merge_stage = NULL;
	}
	clear_bit (IDETAPE_PIPELINE_ERROR, &tape->flags);
	tape->chrdev_direction=idetape_direction_none;

	/*
	 *	On the next backup, perform the feedback loop again.
	 *	(I don't want to keep sense information between backups,
	 *	 as some systems are constantly on, and the system load
	 *	 can be totally different on the next backup).
	 */
	tape->max_stages = tape->min_pipeline;
#if IDETAPE_DEBUG_BUGS
	if (tape->first_stage != NULL || tape->next_stage != NULL || tape->last_stage != NULL || tape->nr_stages != 0) {
		printk (KERN_ERR "ide-tape: ide-tape pipeline bug\n");		
	}
#endif /* IDETAPE_DEBUG_BUGS */
}

static int idetape_pipeline_size (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage;
	struct request *rq;
	int size = 0;

	idetape_wait_for_pipeline (drive);
	stage = tape->first_stage;
	while (stage != NULL) {
		rq = &stage->rq;
		size += tape->tape_block_size * (rq->nr_sectors-rq->current_nr_sectors);
		if (rq->errors == IDETAPE_ERROR_FILEMARK)
			size += tape->tape_block_size;
		stage = stage->next;
	}
	size += tape->merge_stage_size;
	return size;
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
static int idetape_position_tape (ide_drive_t *drive, unsigned int block, byte partition)
{
	int retval;
	idetape_pc_t pc;

	idetape_create_locate_cmd (&pc, block, partition);
	retval=idetape_queue_pc_tail (drive,&pc);
	if (retval) return (retval);

	idetape_create_read_position_cmd (&pc);
	return (idetape_queue_pc_tail (drive,&pc));
}

/*
 *	Rewinds the tape to the Beginning Of the current Partition (BOP).
 *
 *	We currently support only one partition.
 */ 
static int idetape_rewind_tape (ide_drive_t *drive)
{
	int retval;
	idetape_pc_t pc;
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_rewind_tape\n");
#endif /* IDETAPE_DEBUG_LOG */	
	
	idetape_create_rewind_cmd (&pc);
	retval=idetape_queue_pc_tail (drive,&pc);
	if (retval) return (retval);

	idetape_create_read_position_cmd (&pc);
	return (idetape_queue_pc_tail (drive,&pc));
}

static int idetape_flush_tape_buffers (ide_drive_t *drive)
{
	idetape_pc_t pc;

	idetape_create_write_filemark_cmd (&pc,0);
	return (idetape_queue_pc_tail (drive,&pc));
}

/*
 *	Our special ide-tape ioctl's.
 *
 *	Currently there aren't any ioctl's.
 *	mtio.h compatible commands should be issued to the character device
 *	interface.
 */
static int idetape_blkdev_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file,
				 unsigned int cmd, unsigned long arg)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_config_t config;

#if IDETAPE_DEBUG_LOG	
	printk (KERN_INFO "ide-tape: Reached idetape_blkdev_ioctl\n");
#endif /* IDETAPE_DEBUG_LOG */
	switch (cmd) {
		case 0x0340:
			if (copy_from_user ((char *) &config, (char *) arg, sizeof (idetape_config_t)))
				return -EFAULT;
			tape->best_dsc_rw_frequency = config.dsc_rw_frequency;
			tape->max_stages = config.nr_stages;
			break;
		case 0x0350:
			config.dsc_rw_frequency = (int) tape->best_dsc_rw_frequency;
			config.nr_stages = tape->max_stages; 
			if (copy_to_user ((char *) arg, (char *) &config, sizeof (idetape_config_t)))
				return -EFAULT;
			break;
		default:
			return -EIO;
	}
	return 0;
}

/*
 *	The block device interface should not be used for data transfers.
 *	However, we still allow opening it so that we can issue general
 *	ide driver configuration ioctl's, such as the interrupt unmask feature.
 */
static int idetape_blkdev_open (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static void idetape_blkdev_release (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	MOD_DEC_USE_COUNT;
}

/*
 *	idetape_pre_reset is called before an ATAPI/ATA software reset.
 */
static void idetape_pre_reset (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	if (tape != NULL)
		set_bit (IDETAPE_IGNORE_DSC, &tape->flags);
}

/*
 *	Character device interface functions
 */
static ide_drive_t *get_drive_ptr (kdev_t i_rdev)
{
	unsigned int i = MINOR(i_rdev) & ~0x80;

	if (i >= MAX_HWIFS * MAX_DRIVES)
		return NULL;
	return (idetape_chrdevs[i].drive);
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
static int idetape_space_over_filemarks (ide_drive_t *drive,short mt_op,int mt_count)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	unsigned long flags;
	int retval,count=0;

	if (tape->chrdev_direction == idetape_direction_read) {

		/*
		 *	We have a read-ahead buffer. Scan it for crossed
		 *	filemarks.
		 */
		tape->merge_stage_size = 0;
		clear_bit (IDETAPE_FILEMARK, &tape->flags);
		while (tape->first_stage != NULL) {
			/*
			 *	Wait until the first read-ahead request
			 *	is serviced.
			 */
			spin_lock_irqsave(&HWGROUP(drive)->spinlock, flags);
			if (tape->active_stage == tape->first_stage)
				idetape_wait_for_request(drive, tape->active_data_request);
			spin_unlock_irqrestore(&HWGROUP(drive)->spinlock, flags);

			if (tape->first_stage->rq.errors == IDETAPE_ERROR_FILEMARK)
				count++;
			if (count == mt_count) {
				switch (mt_op) {
					case MTFSF:
						idetape_remove_stage_head (drive);
					case MTFSFM:
						return (0);
					default:
						break;
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
	switch (mt_op) {
		case MTFSF:
			idetape_create_space_cmd (&pc,mt_count-count,IDETAPE_SPACE_OVER_FILEMARK);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTFSFM:
			if (!tape->capabilities.sprev)
				return (-EIO);
			retval = idetape_space_over_filemarks (drive, MTFSF, mt_count-count);
			if (retval) return (retval);
			return (idetape_space_over_filemarks (drive, MTBSF, 1));
		case MTBSF:
			if (!tape->capabilities.sprev)
				return (-EIO);
			idetape_create_space_cmd (&pc,-(mt_count+count),IDETAPE_SPACE_OVER_FILEMARK);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTBSFM:
			if (!tape->capabilities.sprev)
				return (-EIO);
			retval = idetape_space_over_filemarks (drive, MTBSF, mt_count+count);
			if (retval) return (retval);
			return (idetape_space_over_filemarks (drive, MTFSF, 1));
		default:
			printk (KERN_ERR "ide-tape: MTIO operation %d not supported\n",mt_op);
			return (-EIO);
	}
}


/*
 *	Our character device read / write functions.
 *
 *	The tape is optimized to maximize throughput when it is transferring
 *	an integral number of the "continuous transfer limit", which is
 *	a parameter of the specific tape (26 KB on my particular tape).
 *
 *	As of version 1.3 of the driver, the character device provides an
 *	abstract continuous view of the media - any mix of block sizes (even 1
 *	byte) on the same backup/restore procedure is supported. The driver
 *	will internally convert the requests to the recommended transfer unit,
 *	so that an unmatch between the user's block size to the recommended
 *	size will only result in a (slightly) increased driver overhead, but
 *	will no longer hit performance.
 */
static ssize_t idetape_chrdev_read (struct file *file, char *buf,
				    size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	ide_drive_t *drive = get_drive_ptr (inode->i_rdev);
	idetape_tape_t *tape = drive->driver_data;
	ssize_t bytes_read,temp,actually_read=0;

	if (ppos != &file->f_pos) {
		/* "A request was outside the capabilities of the device." */
		return -ENXIO;
	}

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_chrdev_read\n");
#endif /* IDETAPE_DEBUG_LOG */
	
	if (tape->chrdev_direction != idetape_direction_read) {		/* Initialize read operation */
		if (tape->chrdev_direction == idetape_direction_write) {
			idetape_empty_write_pipeline (drive);
			idetape_flush_tape_buffers (drive);
		}
#if IDETAPE_DEBUG_BUGS
		if (tape->merge_stage || tape->merge_stage_size) {
			printk (KERN_ERR "ide-tape: merge_stage_size should be 0 now\n");
			tape->merge_stage_size = 0;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		if ((tape->merge_stage = __idetape_kmalloc_stage (tape)) == NULL)
			return -ENOMEM;
		tape->chrdev_direction = idetape_direction_read;

		/*
		 *	Issue a read 0 command to ensure that DSC handshake
		 *	is switched from completion mode to buffer available
		 *	mode.
		 */
		bytes_read = idetape_queue_rw_tail (drive, IDETAPE_READ_RQ, 0, tape->merge_stage->bh);
		if (bytes_read < 0) {
			kfree (tape->merge_stage);
			tape->merge_stage = NULL;
			tape->chrdev_direction = idetape_direction_none;
			return bytes_read;
		}
		if (test_bit (IDETAPE_DETECT_BS, &tape->flags))
			if (count > tape->tape_block_size && (count % tape->tape_block_size) == 0)
				tape->user_bs_factor = count / tape->tape_block_size;
	}
	if (count==0)
		return (0);
	if (tape->merge_stage_size) {
		actually_read=IDE_MIN (tape->merge_stage_size,count);
		idetape_copy_stage_to_user (tape, buf, tape->merge_stage, actually_read);
		buf += actually_read; tape->merge_stage_size -= actually_read; count-=actually_read;
	}
	while (count >= tape->stage_size) {
		bytes_read=idetape_add_chrdev_read_request (drive, tape->capabilities.ctl);
		if (bytes_read <= 0)
			goto finish;
		idetape_copy_stage_to_user (tape, buf, tape->merge_stage, bytes_read);
		buf += bytes_read; count -= bytes_read; actually_read += bytes_read;
	}
	if (count) {
		bytes_read=idetape_add_chrdev_read_request (drive, tape->capabilities.ctl);
		if (bytes_read <= 0)
			goto finish;
		temp=IDE_MIN (count,bytes_read);
		idetape_copy_stage_to_user (tape, buf, tape->merge_stage, temp);
		actually_read+=temp;
		tape->merge_stage_size=bytes_read-temp;
	}
finish:
	if (!actually_read && test_bit (IDETAPE_FILEMARK, &tape->flags))
		idetape_space_over_filemarks (drive, MTFSF, 1);
	return (actually_read);
}
 
static ssize_t idetape_chrdev_write (struct file *file, const char *buf,
				     size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	ide_drive_t *drive = get_drive_ptr (inode->i_rdev);
	idetape_tape_t *tape = drive->driver_data;
	ssize_t retval,actually_written=0;

	if (ppos != &file->f_pos) {
		/* "A request was outside the capabilities of the device." */
		return -ENXIO;
	}

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_chrdev_write\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->chrdev_direction != idetape_direction_write) {	/* Initialize write operation */
		if (tape->chrdev_direction == idetape_direction_read)
			idetape_discard_read_pipeline (drive);
#if IDETAPE_DEBUG_BUGS
		if (tape->merge_stage || tape->merge_stage_size) {
			printk (KERN_ERR "ide-tape: merge_stage_size should be 0 now\n");
			tape->merge_stage_size = 0;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		if ((tape->merge_stage = __idetape_kmalloc_stage (tape)) == NULL)
			return -ENOMEM;
		tape->chrdev_direction = idetape_direction_write;
		idetape_init_merge_stage (tape);

		/*
		 *	Issue a write 0 command to ensure that DSC handshake
		 *	is switched from completion mode to buffer available
		 *	mode.
		 */
		retval = idetape_queue_rw_tail (drive, IDETAPE_WRITE_RQ, 0, tape->merge_stage->bh);
		if (retval < 0) {
			kfree (tape->merge_stage);
			tape->merge_stage = NULL;
			tape->chrdev_direction = idetape_direction_none;
			return retval;
		}
		if (test_bit (IDETAPE_DETECT_BS, &tape->flags))
			if (count > tape->tape_block_size && (count % tape->tape_block_size) == 0)
				tape->user_bs_factor = count / tape->tape_block_size;
	}
	if (count==0)
		return (0);
	if (tape->merge_stage_size) {
#if IDETAPE_DEBUG_BUGS
		if (tape->merge_stage_size >= tape->stage_size) {
			printk (KERN_ERR "ide-tape: bug: merge buffer too big\n");
			tape->merge_stage_size=0;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		actually_written=IDE_MIN (tape->stage_size-tape->merge_stage_size,count);
		idetape_copy_stage_from_user (tape, tape->merge_stage, buf, actually_written);
		buf+=actually_written;tape->merge_stage_size+=actually_written;count-=actually_written;

		if (tape->merge_stage_size == tape->stage_size) {
			tape->merge_stage_size = 0;
			retval=idetape_add_chrdev_write_request (drive, tape->capabilities.ctl);
			if (retval <= 0)
				return (retval);
		}
	}
	while (count >= tape->stage_size) {
		idetape_copy_stage_from_user (tape, tape->merge_stage, buf, tape->stage_size);
		buf+=tape->stage_size;count-=tape->stage_size;
		retval=idetape_add_chrdev_write_request (drive, tape->capabilities.ctl);
		actually_written+=tape->stage_size;
		if (retval <= 0)
			return (retval);
	}
	if (count) {
		actually_written+=count;
		idetape_copy_stage_from_user (tape, tape->merge_stage, buf, count);
		tape->merge_stage_size+=count;
	}
	return (actually_written);
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
 *	MTLOAD	-	Loads the tape.
 *
 *	MTOFFL	-	Puts the tape drive "Offline": Rewinds the tape and
 *	MTUNLOAD	prevents further access until the media is replaced.
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
 *	MTSETBLK - 	Sets the user block size to mt_count bytes. If
 *			mt_count is 0, we will attempt to autodetect
 *			the block size.
 *
 *	MTSEEK	-	Positions the tape in a specific block number, where
 *			each block is assumed to contain which user_block_size
 *			bytes.
 *
 *	MTSETPART - 	Switches to another tape partition.
 *
 *	The following commands are currently not supported:
 *
 *	MTFSR, MTBSR, MTFSS, MTBSS, MTWSM, MTSETDENSITY,
 *	MTSETDRVBUFFER, MT_ST_BOOLEANS, MT_ST_WRITE_THRESHOLD.
 */
static int idetape_mtioctop (ide_drive_t *drive,short mt_op,int mt_count)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	int i,retval;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Handling MTIOCTOP ioctl: mt_op=%d, mt_count=%d\n",mt_op,mt_count);
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
		case MTLOAD:
			idetape_create_load_unload_cmd (&pc, IDETAPE_LU_LOAD_MASK);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTUNLOAD:
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
			(void) idetape_rewind_tape (drive);
			idetape_create_erase_cmd (&pc);
			return (idetape_queue_pc_tail (drive,&pc));
		case MTSETBLK:
			if (mt_count) {
				if (mt_count < tape->tape_block_size || mt_count % tape->tape_block_size)
					return -EIO;
				tape->user_bs_factor = mt_count / tape->tape_block_size;
				clear_bit (IDETAPE_DETECT_BS, &tape->flags);
			} else
				set_bit (IDETAPE_DETECT_BS, &tape->flags);
			return 0;
		case MTSEEK:
			return (idetape_position_tape (drive, mt_count * tape->user_bs_factor, tape->partition));
		case MTSETPART:
			return (idetape_position_tape (drive, 0, mt_count));
		default:
			printk (KERN_ERR "ide-tape: MTIO operation %d not supported\n",mt_op);
			return (-EIO);
	}
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
 *			will be set to (user block size in bytes <<
 *			MT_ST_BLKSIZE_SHIFT) & MT_ST_BLKSIZE_MASK.
 *
 *			The mt_blkno is set to the current user block number.
 *			The other mtget fields are not supported.
 *
 *	MTIOCPOS -	The current tape "block position" is returned. We
 *			assume that each block contains user_block_size
 *			bytes.
 *
 *	Our own ide-tape ioctls are supported on both interfaces.
 */
static int idetape_chrdev_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	ide_drive_t *drive = get_drive_ptr (inode->i_rdev);
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	struct mtop mtop;
	struct mtget mtget;
	struct mtpos mtpos;
	int retval, block_offset = 0;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_chrdev_ioctl, cmd=%u\n",cmd);
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->chrdev_direction == idetape_direction_write) {
		idetape_empty_write_pipeline (drive);
		idetape_flush_tape_buffers (drive);
	}
	if (cmd == MTIOCGET || cmd == MTIOCPOS) {
		block_offset = idetape_pipeline_size (drive) / (tape->tape_block_size * tape->user_bs_factor);
		idetape_create_read_position_cmd (&pc);
		retval=idetape_queue_pc_tail (drive,&pc);
		if (retval) return (retval);
	}
	switch (cmd) {
		case MTIOCTOP:
			if (copy_from_user ((char *) &mtop, (char *) arg, sizeof (struct mtop)))
				return -EFAULT;
			return (idetape_mtioctop (drive,mtop.mt_op,mtop.mt_count));
		case MTIOCGET:
			memset (&mtget, 0, sizeof (struct mtget));
			mtget.mt_blkno = tape->block_address / tape->user_bs_factor - block_offset;
			mtget.mt_dsreg = ((tape->tape_block_size * tape->user_bs_factor) << MT_ST_BLKSIZE_SHIFT) & MT_ST_BLKSIZE_MASK;
			if (copy_to_user ((char *) arg,(char *) &mtget, sizeof (struct mtget)))
				return -EFAULT;
			return 0;
		case MTIOCPOS:
			mtpos.mt_blkno = tape->block_address / tape->user_bs_factor - block_offset;
			if (copy_to_user ((char *) arg,(char *) &mtpos, sizeof (struct mtpos)))
				return -EFAULT;
			return 0;
		default:
			if (tape->chrdev_direction == idetape_direction_read)
				idetape_discard_read_pipeline (drive);
			return (idetape_blkdev_ioctl (drive,inode,file,cmd,arg));
	}
}

/*
 *	Our character device open function.
 */
static int idetape_chrdev_open (struct inode *inode, struct file *filp)
{
	ide_drive_t *drive;
	idetape_tape_t *tape;
	idetape_pc_t pc;
			
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_chrdev_open\n");
#endif /* IDETAPE_DEBUG_LOG */
	
	if ((drive = get_drive_ptr (inode->i_rdev)) == NULL)
		return -ENXIO;
	tape = drive->driver_data;

	if (test_and_set_bit (IDETAPE_BUSY, &tape->flags))
		return -EBUSY;
	MOD_INC_USE_COUNT;
	idetape_create_read_position_cmd (&pc);
	(void) idetape_queue_pc_tail (drive,&pc);
	if (!test_bit (IDETAPE_ADDRESS_VALID, &tape->flags))
		(void) idetape_rewind_tape (drive);
	MOD_DEC_USE_COUNT;

	if (tape->chrdev_direction == idetape_direction_none)
		MOD_INC_USE_COUNT;
	return 0;
}

/*
 *	Our character device release function.
 */
static int idetape_chrdev_release (struct inode *inode, struct file *filp)
{
	ide_drive_t *drive = get_drive_ptr (inode->i_rdev);
	idetape_tape_t *tape = drive->driver_data;
	unsigned int minor=MINOR (inode->i_rdev);
	idetape_pc_t pc;
			
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Reached idetape_chrdev_release\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->chrdev_direction == idetape_direction_write) {
		idetape_empty_write_pipeline (drive);
		tape->merge_stage = __idetape_kmalloc_stage (tape);
		if (tape->merge_stage != NULL) {
			idetape_pad_zeros (drive, tape->tape_block_size * (tape->user_bs_factor - 1));
			__idetape_kfree_stage (tape->merge_stage);
			tape->merge_stage = NULL;
		}
		idetape_create_write_filemark_cmd (&pc,1);	/* Write a filemark */
		if (idetape_queue_pc_tail (drive,&pc))
			printk (KERN_ERR "ide-tape: Couldn't write a filemark\n");
	}
	if (tape->chrdev_direction == idetape_direction_read) {
		if (minor < 128)
			idetape_discard_read_pipeline (drive);
		else
			idetape_wait_for_pipeline (drive);
	}
	if (tape->cache_stage != NULL) {
		__idetape_kfree_stage (tape->cache_stage);
		tape->cache_stage = NULL;
	}
	if (minor < 128)
		(void) idetape_rewind_tape (drive);

	clear_bit (IDETAPE_BUSY, &tape->flags);
	if (tape->chrdev_direction == idetape_direction_none)
		MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	idetape_identify_device is called to check the contents of the
 *	ATAPI IDENTIFY command results. We return:
 *
 *	1	If the tape can be supported by us, based on the information
 *		we have so far.
 *
 *	0 	If this tape driver is not currently supported by us.
 */
static int idetape_identify_device (ide_drive_t *drive,struct hd_driveid *id)
{
	struct idetape_id_gcw gcw;
#if IDETAPE_DEBUG_LOG
	unsigned short mask,i;
#endif /* IDETAPE_DEBUG_LOG */

	if (!id)
		return 0;

	*((unsigned short *) &gcw) = id->config;

#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Dumping ATAPI Identify Device tape parameters\n");
	printk (KERN_INFO "Protocol Type: ");
	switch (gcw.protocol) {
		case 0: case 1: printk (KERN_INFO "ATA\n");break;
		case 2:	printk (KERN_INFO "ATAPI\n");break;
		case 3: printk (KERN_INFO "Reserved (Unknown to ide-tape)\n");break;
	}
	printk (KERN_INFO "Device Type: %x - ",gcw.device_type);	
	switch (gcw.device_type) {
		case 0: printk (KERN_INFO "Direct-access Device\n");break;
		case 1: printk (KERN_INFO "Streaming Tape Device\n");break;
		case 2: case 3: case 4: printk (KERN_INFO "Reserved\n");break;
		case 5: printk (KERN_INFO "CD-ROM Device\n");break;
		case 6: printk (KERN_INFO "Reserved\n");
		case 7: printk (KERN_INFO "Optical memory Device\n");break;
		case 0x1f: printk (KERN_INFO "Unknown or no Device type\n");break;
		default: printk (KERN_INFO "Reserved\n");
	}
	printk (KERN_INFO "Removable: %s",gcw.removable ? "Yes\n":"No\n");	
	printk (KERN_INFO "Command Packet DRQ Type: ");
	switch (gcw.drq_type) {
		case 0: printk (KERN_INFO "Microprocessor DRQ\n");break;
		case 1: printk (KERN_INFO "Interrupt DRQ\n");break;
		case 2: printk (KERN_INFO "Accelerated DRQ\n");break;
		case 3: printk (KERN_INFO "Reserved\n");break;
	}
	printk (KERN_INFO "Command Packet Size: ");
	switch (gcw.packet_size) {
		case 0: printk (KERN_INFO "12 bytes\n");break;
		case 1: printk (KERN_INFO "16 bytes\n");break;
		default: printk (KERN_INFO "Reserved\n");break;
	}
	printk (KERN_INFO "Model: %.40s\n",id->model);
	printk (KERN_INFO "Firmware Revision: %.8s\n",id->fw_rev);
	printk (KERN_INFO "Serial Number: %.20s\n",id->serial_no);
	printk (KERN_INFO "Write buffer size: %d bytes\n",id->buf_size*512);
	printk (KERN_INFO "DMA: %s",id->capability & 0x01 ? "Yes\n":"No\n");
	printk (KERN_INFO "LBA: %s",id->capability & 0x02 ? "Yes\n":"No\n");
	printk (KERN_INFO "IORDY can be disabled: %s",id->capability & 0x04 ? "Yes\n":"No\n");
	printk (KERN_INFO "IORDY supported: %s",id->capability & 0x08 ? "Yes\n":"Unknown\n");
	printk (KERN_INFO "ATAPI overlap supported: %s",id->capability & 0x20 ? "Yes\n":"No\n");
	printk (KERN_INFO "PIO Cycle Timing Category: %d\n",id->tPIO);
	printk (KERN_INFO "DMA Cycle Timing Category: %d\n",id->tDMA);
	printk (KERN_INFO "Single Word DMA supported modes: ");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_1word & mask)
			printk (KERN_INFO "%d ",i);
		if (id->dma_1word & (mask << 8))
			printk (KERN_INFO "(active) ");
	}
	printk (KERN_INFO "\n");
	printk (KERN_INFO "Multi Word DMA supported modes: ");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_mword & mask)
			printk (KERN_INFO "%d ",i);
		if (id->dma_mword & (mask << 8))
			printk (KERN_INFO "(active) ");
	}
	printk (KERN_INFO "\n");
	if (id->field_valid & 0x0002) {
		printk (KERN_INFO "Enhanced PIO Modes: %s\n",id->eide_pio_modes & 1 ? "Mode 3":"None");
		printk (KERN_INFO "Minimum Multi-word DMA cycle per word: ");
		if (id->eide_dma_min == 0)
			printk (KERN_INFO "Not supported\n");
		else
			printk (KERN_INFO "%d ns\n",id->eide_dma_min);

		printk (KERN_INFO "Manufacturer\'s Recommended Multi-word cycle: ");
		if (id->eide_dma_time == 0)
			printk (KERN_INFO "Not supported\n");
		else
			printk (KERN_INFO "%d ns\n",id->eide_dma_time);

		printk (KERN_INFO "Minimum PIO cycle without IORDY: ");
		if (id->eide_pio == 0)
			printk (KERN_INFO "Not supported\n");
		else
			printk (KERN_INFO "%d ns\n",id->eide_pio);

		printk (KERN_INFO "Minimum PIO cycle with IORDY: ");
		if (id->eide_pio_iordy == 0)
			printk (KERN_INFO "Not supported\n");
		else
			printk (KERN_INFO "%d ns\n",id->eide_pio_iordy);
		
	} else
		printk (KERN_INFO "According to the device, fields 64-70 are not valid.\n");
#endif /* IDETAPE_DEBUG_LOG */

	/* Check that we can support this device */

	if (gcw.protocol !=2 )
		printk (KERN_ERR "ide-tape: Protocol is not ATAPI\n");
	else if (gcw.device_type != 1)
		printk (KERN_ERR "ide-tape: Device type is not set to tape\n");
	else if (!gcw.removable)
		printk (KERN_ERR "ide-tape: The removable flag is not set\n");
	else if (gcw.packet_size != 0) {
		printk (KERN_ERR "ide-tape: Packet size is not 12 bytes long\n");
		if (gcw.packet_size == 1)
			printk (KERN_ERR "ide-tape: Sorry, padding to 16 bytes is still not supported\n");
	} else
		return 1;
	return 0;
}

/*
 *	idetape_get_mode_sense_results asks the tape about its various
 *	parameters. In particular, we will adjust our data transfer buffer
 *	size to the recommended value as returned by the tape.
 */
static void idetape_get_mode_sense_results (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	idetape_mode_parameter_header_t *header;
	idetape_capabilities_page_t *capabilities;
	
	idetape_create_mode_sense_cmd (&pc,IDETAPE_CAPABILITIES_PAGE);
	if (idetape_queue_pc_tail (drive,&pc)) {
		printk (KERN_ERR "ide-tape: Can't get tape parameters - assuming some default values\n");
		tape->tape_block_size = 512; tape->capabilities.ctl = 52;
		tape->capabilities.speed = 450; tape->capabilities.buffer_size = 6 * 52;
		return;
	}
	header = (idetape_mode_parameter_header_t *) pc.buffer;
	capabilities = (idetape_capabilities_page_t *) (pc.buffer + sizeof(idetape_mode_parameter_header_t) + header->bdl);

	capabilities->max_speed = ntohs (capabilities->max_speed);
	capabilities->ctl = ntohs (capabilities->ctl);
	capabilities->speed = ntohs (capabilities->speed);
	capabilities->buffer_size = ntohs (capabilities->buffer_size);

	if (!capabilities->speed) {
		printk("ide-tape: %s: overriding capabilities->speed (assuming 650KB/sec)\n", drive->name);
		capabilities->speed = 650;
	}
	if (!capabilities->max_speed) {
		printk("ide-tape: %s: overriding capabilities->max_speed (assuming 650KB/sec)\n", drive->name);
		capabilities->max_speed = 650;
	}

	tape->capabilities = *capabilities;		/* Save us a copy */
	tape->tape_block_size = capabilities->blk512 ? 512:1024;
#if IDETAPE_DEBUG_LOG
	printk (KERN_INFO "Dumping the results of the MODE SENSE packet command\n");
	printk (KERN_INFO "Mode Parameter Header:\n");
	printk (KERN_INFO "Mode Data Length - %d\n",header->mode_data_length);
	printk (KERN_INFO "Medium Type - %d\n",header->medium_type);
	printk (KERN_INFO "Device Specific Parameter - %d\n",header->dsp);
	printk (KERN_INFO "Block Descriptor Length - %d\n",header->bdl);
	
	printk (KERN_INFO "Capabilities and Mechanical Status Page:\n");
	printk (KERN_INFO "Page code - %d\n",capabilities->page_code);
	printk (KERN_INFO "Page length - %d\n",capabilities->page_length);
	printk (KERN_INFO "Read only - %s\n",capabilities->ro ? "Yes":"No");
	printk (KERN_INFO "Supports reverse space - %s\n",capabilities->sprev ? "Yes":"No");
	printk (KERN_INFO "Supports erase initiated formatting - %s\n",capabilities->efmt ? "Yes":"No");
	printk (KERN_INFO "Supports QFA two Partition format - %s\n",capabilities->qfa ? "Yes":"No");
	printk (KERN_INFO "Supports locking the medium - %s\n",capabilities->lock ? "Yes":"No");
	printk (KERN_INFO "The volume is currently locked - %s\n",capabilities->locked ? "Yes":"No");
	printk (KERN_INFO "The device defaults in the prevent state - %s\n",capabilities->prevent ? "Yes":"No");
	printk (KERN_INFO "Supports ejecting the medium - %s\n",capabilities->eject ? "Yes":"No");
	printk (KERN_INFO "Supports error correction - %s\n",capabilities->ecc ? "Yes":"No");
	printk (KERN_INFO "Supports data compression - %s\n",capabilities->cmprs ? "Yes":"No");
	printk (KERN_INFO "Supports 512 bytes block size - %s\n",capabilities->blk512 ? "Yes":"No");
	printk (KERN_INFO "Supports 1024 bytes block size - %s\n",capabilities->blk1024 ? "Yes":"No");
	printk (KERN_INFO "Restricted byte count for PIO transfers - %s\n",capabilities->slowb ? "Yes":"No");
	printk (KERN_INFO "Maximum supported speed in KBps - %d\n",capabilities->max_speed);
	printk (KERN_INFO "Continuous transfer limits in blocks - %d\n",capabilities->ctl);
	printk (KERN_INFO "Current speed in KBps - %d\n",capabilities->speed);	
	printk (KERN_INFO "Buffer size - %d\n",capabilities->buffer_size*512);
#endif /* IDETAPE_DEBUG_LOG */
}

static void idetape_add_settings(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

/*
 *			drive	setting name	read/write	ioctl	ioctl		data type	min			max			mul_factor			div_factor			data pointer				set function
 */
	ide_add_setting(drive,	"buffer",	SETTING_READ,	-1,	-1,		TYPE_SHORT,	0,			0xffff,			1,				2,				&tape->capabilities.buffer_size,	NULL);
	ide_add_setting(drive,	"pipeline_min",	SETTING_RW,	-1,	-1,		TYPE_INT,	0,			0xffff,			tape->stage_size / 1024,	1,				&tape->min_pipeline,			NULL);
	ide_add_setting(drive,	"pipeline",	SETTING_RW,	-1,	-1,		TYPE_INT,	0,			0xffff,			tape->stage_size / 1024,	1,				&tape->max_stages,			NULL);
	ide_add_setting(drive,	"pipeline_max",	SETTING_RW,	-1,	-1,		TYPE_INT,	0,			0xffff,			tape->stage_size / 1024,	1,				&tape->max_pipeline,			NULL);
	ide_add_setting(drive,	"pipeline_used",SETTING_READ,	-1,	-1,		TYPE_INT,	0,			0xffff,			tape->stage_size / 1024,	1,				&tape->nr_stages,			NULL);
	ide_add_setting(drive,	"speed",	SETTING_READ,	-1,	-1,		TYPE_SHORT,	0,			0xffff,			1,				1,				&tape->capabilities.speed,		NULL);
	ide_add_setting(drive,	"stage",	SETTING_READ,	-1,	-1,		TYPE_INT,	0,			0xffff,			1,				1024,				&tape->stage_size,			NULL);
	ide_add_setting(drive,	"tdsc",		SETTING_RW,	-1,	-1,		TYPE_INT,	IDETAPE_DSC_RW_MIN,	IDETAPE_DSC_RW_MAX,	1000,				HZ,				&tape->best_dsc_rw_frequency,		NULL);
	ide_add_setting(drive,	"dsc_overlap",	SETTING_RW,	-1,	-1,		TYPE_BYTE,	0,			1,			1,				1,				&drive->dsc_overlap,			NULL);
}

/*
 *	ide_setup is called to:
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
static void idetape_setup (ide_drive_t *drive, idetape_tape_t *tape, int minor)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long t1, tmid, tn, t;
	u16 speed;
	struct idetape_id_gcw gcw;

	drive->driver_data = tape;
	drive->ready_stat = 0;			/* An ATAPI device ignores DRDY */
	drive->dsc_overlap = 1;
	memset (tape, 0, sizeof (idetape_tape_t));
	tape->drive = drive;
	tape->minor = minor;
	tape->name[0] = 'h'; tape->name[1] = 't'; tape->name[2] = '0' + minor;
	tape->chrdev_direction = idetape_direction_none;
	tape->pc = tape->pc_stack;
	tape->min_pipeline = IDETAPE_MIN_PIPELINE_STAGES;
	tape->max_pipeline = IDETAPE_MAX_PIPELINE_STAGES;
	tape->max_stages = tape->min_pipeline;
	*((unsigned short *) &gcw) = drive->id->config;
	if (gcw.drq_type == 1)
		set_bit(IDETAPE_DRQ_INTERRUPT, &tape->flags);

	idetape_get_mode_sense_results (drive);

	tape->user_bs_factor = 1;
	tape->stage_size = tape->capabilities.ctl * tape->tape_block_size;
	while (tape->stage_size > 0xffff) {
		printk (KERN_NOTICE "ide-tape: decreasing stage size\n");
		tape->capabilities.ctl /= 2;
		tape->stage_size = tape->capabilities.ctl * tape->tape_block_size;
	}
	tape->pages_per_stage = tape->stage_size / PAGE_SIZE;
	if (tape->stage_size % PAGE_SIZE) {
		tape->pages_per_stage++;
		tape->excess_bh_size = PAGE_SIZE - tape->stage_size % PAGE_SIZE;
	}

	/*
	 *	Select the "best" DSC read/write polling frequency.
	 *	The following algorithm attempts to find a balance between
	 *	good latency and good system throughput. It will be nice to
	 *	have all this configurable in run time at some point.
	 */
	speed = IDE_MAX (tape->capabilities.speed, tape->capabilities.max_speed);
	t1 = (tape->stage_size * HZ) / (speed * 1000);
	tmid = (tape->capabilities.buffer_size * 32 * HZ) / (speed * 125);
	tn = (IDETAPE_FIFO_THRESHOLD * tape->stage_size * HZ) / (speed * 1000);

	if (tape->max_stages) {
		if (drive->using_dma)
			t = tmid;
		else {
			if (hwif->drives[drive->select.b.unit ^ 1].present || hwif->next != hwif)
				t = (tn + tmid) / 2;
			else
				t = tn;
		}
	} else
		t = t1;
	t = IDE_MIN (t, tmid);

	/*
	 *	Ensure that the number we got makes sense.
	 */
	tape->best_dsc_rw_frequency = IDE_MAX (IDE_MIN (t, IDETAPE_DSC_RW_MAX), IDETAPE_DSC_RW_MIN);
	if (tape->best_dsc_rw_frequency != t) {
		printk (KERN_NOTICE "ide-tape: Although the recommended polling period is %lu jiffies\n", t);
		printk (KERN_NOTICE "ide-tape: we will use %lu jiffies\n", tape->best_dsc_rw_frequency);
	}
	printk (KERN_INFO "ide-tape: %s <-> %s, %dKBps, %d*%dkB buffer, %dkB pipeline, %lums tDSC%s\n",
		drive->name, tape->name, tape->capabilities.speed, (tape->capabilities.buffer_size * 512) / tape->stage_size,
		tape->stage_size / 1024, tape->max_stages * tape->stage_size / 1024,
		tape->best_dsc_rw_frequency * 1000 / HZ, drive->using_dma ? ", DMA":"");

	idetape_add_settings(drive);
}

static int idetape_cleanup (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	int minor = tape->minor;
	unsigned long flags;

	save_flags (flags);	/* all CPUs (overkill?) */
	cli();			/* all CPUs (overkill?) */
	if (test_bit (IDETAPE_BUSY, &tape->flags) || tape->first_stage != NULL || tape->merge_stage_size || drive->usage) {
		restore_flags(flags);	/* all CPUs (overkill?) */
		return 1;
	}
	idetape_chrdevs[minor].drive = NULL;
	restore_flags (flags);	/* all CPUs (overkill?) */
	DRIVER(drive)->busy = 0;
	(void) ide_unregister_subdriver (drive);
	drive->driver_data = NULL;
	kfree (tape);
	for (minor = 0; minor < MAX_HWIFS * MAX_DRIVES; minor++)
		if (idetape_chrdevs[minor].drive != NULL)
			return 0;
	unregister_chrdev (IDETAPE_MAJOR, "ht");
	idetape_chrdev_present = 0;
	return 0;
}

#ifdef CONFIG_PROC_FS

static int proc_idetape_read_name
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	idetape_tape_t	*tape = drive->driver_data;
	char		*out = page;
	int		len;

	len = sprintf(out,"%s\n", tape->name);
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static ide_proc_entry_t idetape_proc[] = {
	{ "name",	S_IFREG|S_IRUGO,	proc_idetape_read_name,	NULL },
	{ NULL, 0, NULL, NULL }
};

#else

#define	idetape_proc	NULL

#endif

/*
 *	IDE subdriver functions, registered with ide.c
 */
static ide_driver_t idetape_driver = {
	"ide-tape",		/* name */
	IDETAPE_VERSION,	/* version */
	ide_tape,		/* media */
	1,			/* busy */
	1,			/* supports_dma */
	1,			/* supports_dsc_overlap */
	idetape_cleanup,	/* cleanup */
	idetape_do_request,	/* do_request */
	idetape_end_request,	/* end_request */
	idetape_blkdev_ioctl,	/* ioctl */
	idetape_blkdev_open,	/* open */
	idetape_blkdev_release,	/* release */
	NULL,			/* media_change */
	idetape_pre_reset,	/* pre_reset */
	NULL,			/* capacity */
	NULL,			/* special */
	idetape_proc		/* proc */
};

int idetape_init (void);
static ide_module_t idetape_module = {
	IDE_DRIVER_MODULE,
	idetape_init,
	&idetape_driver,
	NULL
};

/*
 *	Our character device supporting functions, passed to register_chrdev.
 */
static struct file_operations idetape_fops = {
	NULL,			/* lseek - default */
	idetape_chrdev_read,	/* read  */
	idetape_chrdev_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll */
	idetape_chrdev_ioctl,	/* ioctl */
	NULL,			/* mmap */
	idetape_chrdev_open,	/* open */
	NULL,			/* flush */
	idetape_chrdev_release,	/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

/*
 *	idetape_init will register the driver for each tape.
 */
int idetape_init (void)
{
	ide_drive_t *drive;
	idetape_tape_t *tape;
	int minor, failed = 0, supported = 0;

	MOD_INC_USE_COUNT;
	if (!idetape_chrdev_present)
		for (minor = 0; minor < MAX_HWIFS * MAX_DRIVES; minor++ )
			idetape_chrdevs[minor].drive = NULL;

	if ((drive = ide_scan_devices (ide_tape, idetape_driver.name, NULL, failed++)) == NULL) {
		ide_register_module (&idetape_module);
		MOD_DEC_USE_COUNT;
		return 0;
	}
	if (!idetape_chrdev_present && register_chrdev (IDETAPE_MAJOR, "ht", &idetape_fops)) {
		printk (KERN_ERR "ide-tape: Failed to register character device interface\n");
		MOD_DEC_USE_COUNT;
		return -EBUSY;
	}
	do {
		if (!idetape_identify_device (drive, drive->id)) {
			printk (KERN_ERR "ide-tape: %s: not supported by this version of ide-tape\n", drive->name);
			continue;
		}
		tape = (idetape_tape_t *) kmalloc (sizeof (idetape_tape_t), GFP_KERNEL);
		if (tape == NULL) {
			printk (KERN_ERR "ide-tape: %s: Can't allocate a tape structure\n", drive->name);
			continue;
		}
		if (ide_register_subdriver (drive, &idetape_driver, IDE_SUBDRIVER_VERSION)) {
			printk (KERN_ERR "ide-tape: %s: Failed to register the driver with ide.c\n", drive->name);
			kfree (tape);
			continue;
		}
		for (minor = 0; idetape_chrdevs[minor].drive != NULL; minor++);
		idetape_setup (drive, tape, minor);
		idetape_chrdevs[minor].drive = drive;
		supported++; failed--;
	} while ((drive = ide_scan_devices (ide_tape, idetape_driver.name, NULL, failed++)) != NULL);
	if (!idetape_chrdev_present && !supported) {
		unregister_chrdev (IDETAPE_MAJOR, "ht");
	} else
		idetape_chrdev_present = 1;
	ide_register_module (&idetape_module);
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE
int init_module (void)
{
	return idetape_init ();
}

void cleanup_module (void)
{
	ide_drive_t *drive;
	int minor;

	for (minor = 0; minor < MAX_HWIFS * MAX_DRIVES; minor++) {
		drive = idetape_chrdevs[minor].drive;
		if (drive != NULL && idetape_cleanup (drive))
			printk (KERN_ERR "ide-tape: %s: cleanup_module() called while still busy\n", drive->name);
	}
	ide_unregister_module(&idetape_module);
}
#endif /* MODULE */
