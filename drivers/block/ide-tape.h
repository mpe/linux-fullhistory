/*
 * linux/drivers/block/ide-tape.h	Version 1.0 - ALPHA	Dec  3, 1995
 *
 * Copyright (C) 1995 Gadi Oxman <tgud@tochnapc2.technion.ac.il>
 */

/*
 * Include file for the IDE ATAPI streaming tape driver.
 *
 * This file contains various ide-tape related structures and function
 * prototypes which are already used in ide.h.
 *
 * The various compile time options are described below.
 */

#ifndef IDETAPE_H
#define IDETAPE_H 

/**************************** Tunable parameters *****************************/

/*
 *	Setting IDETAPE_DEBUG to 1 will:
 *
 *		1.	Generally log all driver actions.
 *		2.	Enable self-sanity checks in some places.
 *
 *	Use IDETAPE_DEBUG when encountering a problem with the driver.
 *
 *	Setting IDETAPE_DEBUG to 0 will restore normal operation mode:
 *
 *		1.	Disable logging normal successful operations.
 *		2.	Disable self-sanity checks.
 *		3.	Errors will still be logged, of course.
 */
 
#define	IDETAPE_DEBUG		0

/*
 *	After each failed packet command we issue a request sense command
 *	and retry the packet command IDETAPE_MAX_PC_RETRIES times.
 *
 *	Setting IDETAPE_MAX_PC_RETRIES to 0 will disable retries.
 */

#define	IDETAPE_MAX_PC_RETRIES	2

/*
 *	In case the tape is not at the requested block, we re-position the
 *	tape. Repeat the procedure for IDETAPE_LOCATE_RETRIES times before
 *	we give up and abort the request. Note that this should not usually
 *	happen when using only the character device interface.
 */

#define	IDETAPE_LOCATE_RETRIES	1

/*
 *	With each packet command, we allocate a buffer of
 *	IDETAPE_TEMP_BUFFER_SIZE bytes. This is used for several packet
 *	commands (Not for READ/WRITE commands).
 *
 *	The default below is too high - We should be using around 100 bytes
 *	typically, but I didn't check all the cases, so I rather be on the
 *	safe size.
 */
 
#define	IDETAPE_TEMP_BUFFER_SIZE 256

/*
 *	In various places in the driver, we need to allocate storage
 *	for packet commands and requests, which will remain valid while
 *	we leave the driver to wait for an interrupt or a timeout event.
 *
 *	In the corresponding ide_drive_t structure, we pre-allocate storage
 *	for IDETAPE_PC_STACK packet commands and requests. This storage is
 *	used as a circular array - Each time we reach the last entry, we
 *	warp around to the first.
 *
 *	It is crucial that we have enough entries for the maximum number
 *	of packet commands / sub-requests which we need to allocate during
 *	the handling of a specific request.
 *
 *	Follows a worse case calculation of the required storage, with a
 *	large safety margin. Hopefully. :-)
 */

#define	IDETAPE_PC_STACK	10+\
				IDETAPE_MAX_PC_RETRIES+\
				3*IDETAPE_LOCATE_RETRIES*IDETAPE_MAX_PC_RETRIES
/*
 *	Media access packet command (like the LOCATE command) have immediate
 *	status with a delayed (and usually long) execution. The tape doesn't
 *	issue an interrupt when the command is actually complete (so that the
 *	bus is freed to use the other IDE device on the same interface), so we
 *	must for poll for this event.
 *
 *	We set a timer with polling frequency of 1/IDETAPE_DSC_MEDIA_ACCESS_FREQUENCY
 *	in this case. We also poll for DSC *before* read/write commands. At
 *	this time the DSC role is changed and instead of signalling command
 *	completion, it will signal buffer availability. Since read/write
 *	commands are fast in comparision to media access commands, the polling
 *	frequency here should be much higher.
 *
 *	We will insist of reading DSC=1 for IDETAPE_DSC_COUNT times in a row,
 *	to accommodate for random fluctuations in the sampling of DSC.
 *	We will also set IDETAPE_DSC_POLLING_FREQUENCY to a rather low
 *	frequency which besides freeing the CPU and the bus will let
 *	random fluctuations a time to settle down.
 *
 *	We also set a timeout for the timer, in case something goes wrong.
 *	The timeout should be longer then the maximum execution time of a
 *	tape operation. I still have to measure exactly how much time does
 *	it take to space over a far filemark, etc. It seemed that 15 minutes
 *	was way too low, so I am meanwhile setting it to a rather large
 *	timeout - 2 Hours.
 *
 *	Once we pass a threshold, the polling frequency will change to
 *	a slow frequency: On relatively fast operations, there is a point
 *	in polling fast, but if we sense that the operation is taking too
 *	much time, we will poll at a lower frequency.
 */

#define	IDETAPE_DSC_FAST_MEDIA_ACCESS_FREQUENCY	1*HZ		/* 1 second */
#define	IDETAPE_FAST_SLOW_THRESHOLD		5*60*HZ		/* 5 minutes */
#define IDETAPE_DSC_SLOW_MEDIA_ACCESS_FREQUENCY	60*HZ		/* 1 minute */
#define	IDETAPE_DSC_READ_WRITE_FREQUENCY	HZ/20		/* 50 msec */
#define	IDETAPE_DSC_TIMEOUT			2*60*60*HZ	/* 2 hours */
#define IDETAPE_DSC_COUNT			1		/* Assume no DSC fluctuations */

/*
 *	As explained in many places through the code, we provide both a block
 *	device and a character device interface to the tape. The block device
 *	interface is needed for compatibility with ide.c. The character device
 *	interface is the higher level of the driver, and passes requests
 *	to the lower part of the driver which interfaces with ide.c.
 *	Using the block device interface, we can bypass this high level
 *	of the driver, talking directly with the lower level part.
 *
 *	It is intended that the character device interface will be used by
 *	the user. To prevent mistakes in this regard, opening of the block
 *	device interface will be refused if ALLOW_OPENING_BLOCK_DEVICE is 0.
 *
 *	Do not change the following parameter unless you are developing
 *	the driver itself.
 */
 
#define IDETAPE_ALLOW_OPENING_BLOCK_DEVICE	0

/*************************** End of tunable parameters ***********************/

/*
 *	Definitions which are already needed in ide.h
 */
 
/*
 *	The following is currently not used.
 */

typedef enum {no_excess_data,excess_data_read,excess_data_write} excess_data_status_t;

 
struct ide_drive_s;				/* Forward declaration - Will be defined later in ide.h */
typedef void (idetape_pc_completed_t)(struct ide_drive_s *);

/*
 *	Our view of a packet command.
 */

typedef struct idetape_packet_command_s {
	byte c [12];				/* Actual packet bytes */
	
	byte retries;				/* On each retry, we increment retries */
	byte error;				/* Set when an error occured */
	byte active;				/* Set when a packet command is in progress */
	byte wait_for_dsc;			/* 1 When polling for DSC */
	byte dsc_count;		
	unsigned long request_transfer;		/* Bytes to transfer */
	unsigned long actually_transferred; 	/* Bytes actually transferred */
	unsigned long buffer_size;		/* Size of our data buffer */
	byte *buffer;				/* Data buffer */
	byte *current_position;			/* Pointer into the above buffer */
	byte writing;				/* Data direction */		
	idetape_pc_completed_t *callback; 	/* Called when this packet command is completed */
	byte temp_buffer [IDETAPE_TEMP_BUFFER_SIZE];	/* Temporary buffer */
} idetape_packet_command_t;

/*
 *	Capabilities and Mechanical Status Page
 */

typedef struct {
	unsigned page_code	:6;	/* Page code - Should be 0x2a */
	unsigned reserved1_67	:2;
	byte page_length;		/* Page Length - Should be 0x12 */
	byte reserved2;	
	byte reserved3;	
	unsigned ro		:1;	/* Read Only Mode */
	unsigned reserved4_1234	:4;
	unsigned sprev		:1;	/* Supports SPACE in the reverse direction */
	unsigned reserved4_67	:2;
	unsigned reserved5_012	:3;
	unsigned efmt		:1;	/* Supports ERASE command initiated formatting */
	unsigned reserved5_4	:1;
	unsigned qfa		:1;	/* Supports the QFA two partition formats */
	unsigned reserved5_67	:2;
	unsigned lock		:1;	/* Supports locking the volume */
	unsigned locked		:1;	/* The volume is locked */
	unsigned prevent	:1;	/* The device defaults in the prevent state after power up */	
	unsigned eject		:1;	/* The device can eject the volume */
	unsigned reserved6_45	:2;	/* Reserved */	
	unsigned ecc		:1;	/* Supports error correction */
	unsigned cmprs		:1;	/* Supports data compression */
	unsigned reserved7_0	:1;
	unsigned blk512		:1;	/* Supports 512 bytes block size */
	unsigned blk1024	:1;	/* Supports 1024 bytes block size */
	unsigned reserved7_3_6	:4;
	unsigned slowb		:1;	/* The device restricts the byte count for PIO */
					/* transfers for slow buffer memory ??? */
	unsigned short max_speed;	/* Maximum speed supported in KBps */
	byte reserved10;
	byte reserved11;
	unsigned short ctl;		/* Continuous Transfer Limit in blocks */
	unsigned short speed;		/* Current Speed, in KBps */
	unsigned short buffer_size;	/* Buffer Size, in 512 bytes */
	byte reserved18;
	byte reserved19;
} idetape_capabilities_page_t;

/*
 *	Most of our global data which we need to save even as we leave the
 *	driver due to an interrupt or a timer event is stored in a variable
 *	of type tape_info, defined below.
 *
 *	Additional global variables which provide the link between the
 *	character device interface to this structure are defined in
 *	ide-tape.c
 */
 
typedef struct {	

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

	idetape_packet_command_t *pc;		/* Current packet command */
	idetape_packet_command_t *failed_pc; 	/* Last failed packet command */
	idetape_packet_command_t pc_stack [IDETAPE_PC_STACK]; /* Packet command stack */
	byte pc_stack_index;			/* Next free packet command storage space */

	/* 
	 *	The Linux ide driver basically traverses the request lists
	 *	of the ide block devices, finds the next request, completes
	 *	it, and passes to the next one. This is done in ide_do_request.
	 *
	 *	In this regard, ide-tape.c is fully compatible with the rest of
	 *	the ide driver - From the point of view of ide.c, we are just
	 *	another ide block device which receives requests and completes
	 *	them.
	 *
	 *	However, our requests usually don't originate in the buffer
	 *	cache but rather in ide-tape.c itself. Here we provide safe
	 *	storage for such requests.
	 */

	struct request rq_stack [IDETAPE_PC_STACK];
	byte rq_stack_index;			/* We implement a circular array */

	/*
	 *	While polling for DSC we use postponed_rq to postpone the
	 *	current request so that ide.c will be able to service
	 *	pending requests on the other device.
	 */

	struct request *postponed_rq;
	byte dsc_count;		
	unsigned long dsc_polling_start;
	struct timer_list dsc_timer;		/* Timer used to poll for dsc */
	unsigned long dsc_polling_frequency;
	unsigned long dsc_timeout;		/* Maximum waiting time */
	byte dsc_received;

	/* Position information */
	
	byte partition_num;			/* Currently not used */
	unsigned long block_address;		/* Current block */
	byte block_address_valid;		/* 0 When the tape position is unknown */
						/* (To the tape or to us) */
	unsigned long locate_to;		/* We want to reach this block, as a part */
						/* of handling the current request */
	byte locate_retries;			/* Each time, increase locate_retries */

	unsigned long last_written_block;	/* Once writing started, we don't allow read */
	byte last_written_valid;		/* access beyond the last written block */
						/* ??? Should I remove this ? */

	/* Last error information */
	
	byte sense_key,asc,ascq;

	/* Character device operation */

	unsigned char last_dt_was_write;	/* Last character device data transfer was a write */
	byte busy;				/* Device already opened */	

	/* Device information */
	
	unsigned short tape_block_size;
	idetape_capabilities_page_t capabilities;	/* Capabilities and Mechanical Page */

	/* Data buffer */
	
	char *data_buffer;

} idetape_tape_t;

#endif /* IDETAPE_H */
