/*
 * linux/drivers/block/ide-tape.h	Version 1.5 - ALPHA	Apr  12, 1996
 *
 * Copyright (C) 1995, 1996 Gadi Oxman <gadio@netvision.net.il>
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
 *	This is probably the most important configuration option.
 *
 *	Pipelined operation mode has the potential to maximize the
 *	performance of the driver and thus to saturate the throughput
 *	to the maximum value supported by the tape.
 *
 *	In pipelined mode we are servicing requests without blocking the
 *	user backup program. For example, on a write request, we will add it
 *	to the pipeline and return without waiting for it to complete. The
 *	user program will then have enough time to prepare the next blocks
 *	while the tape is still busy working on the previous requests.
 *
 *	Pipelined operation mode is enabled by default, but since it has a
 *	few downfalls as well, you may wish to disable it.
 *	Further explanation of pipelined mode is available in ide-tape.c .
 */

#define	IDETAPE_PIPELINE	1

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
 */
 
#define	IDETAPE_MIN_PIPELINE_STAGES		100
#define	IDETAPE_MAX_PIPELINE_STAGES		200
#define	IDETAPE_INCREASE_STAGES_RATE		0.2

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
		 
#define IDETAPE_LOW_TAPE_PRIORITY		0

/*
 *	It seems that dynamically allocating buffers of about 32KB
 *	each is doomed to fail, unless we are in or very near the
 *	initialization stage. Take care when changing this value, as it
 *	is now optimized with the design of kmalloc, so that we will not
 *	allocate parts of a page. Setting the size to 512 bytes, for example,
 *	would cause kmalloc to allocate for us 1024 bytes, and to
 *	unnecessarily waste double amount of memory.
 */

#if PAGE_SIZE == 4096
	#define	IDETAPE_ALLOCATION_BLOCK		500
#elif PAGE_SIZE == 8192
	#define	IDETAPE_ALLOCATION_BLOCK		496
#else /* ??? Not defined by linux/mm/kmalloc.c */
	#define IDETAPE_ALLOCATION_BLOCK		512
#endif

/*
 *	ide-tape currently uses two continuous buffers, each of the size of
 *	one stage. By default, those buffers are allocated at initialization
 *	time and never released, since dynamic allocation of pages bigger
 *	than PAGE_SIZE may fail as memory becomes fragmented.
 *
 *	This results in about 100 KB memory usage when the tape is idle.
 *	Setting IDETAPE_MINIMIZE_IDLE_MEMORY_USAGE to 1 will let ide-tape
 *	to dynamically allocate those buffers, resulting in about 20 KB idle
 *	memory usage.
 */
 
#define	IDETAPE_MINIMIZE_IDLE_MEMORY_USAGE	0

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
 
#define	IDETAPE_DEBUG_LOG		0
#define	IDETAPE_DEBUG_BUGS		1

/*
 *	After each failed packet command we issue a request sense command
 *	and retry the packet command IDETAPE_MAX_PC_RETRIES times.
 *
 *	Setting IDETAPE_MAX_PC_RETRIES to 0 will disable retries.
 */

#define	IDETAPE_MAX_PC_RETRIES	3

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
 *	large safety margin.
 */

#define	IDETAPE_PC_STACK	20+IDETAPE_MAX_PC_RETRIES

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
 *		We can now automatically select the "best" polling frequency.
 *		Have a look at IDETAPE_ANTICIPATE_READ_WRITE_DSC below.
 *
 *		In case you don't want to use the automatic selection,
 *		choose it to be relatively fast. The default fallback
 *		frequency is 1/50 msec.
 *
 *	2.	After the successful initialization of a "media access
 *		packet command", which is a command which can take a long
 *		time to complete (it can be several seconds or even an hour).
 *
 *		Again, we postpone our request in the middle to free the bus
 *		for the other device. The polling frequency here should be
 *		lower than the read/write frequency since those media access
 *		commands are slow. We start from a "fast" frequency -
 *		IDETAPE_DSC_FAST_MEDIA_ACCESS_FREQUENCY (one second), and
 *		if we don't receive DSC after IDETAPE_FAST_SLOW_THRESHOLD
 *		(5 minutes), we switch it to a lower frequency -
 *		IDETAPE_DSC_SLOW_MEDIA_ACCESS_FREQUENCY (1 minute).
 *		
 *	We also set a timeout for the timer, in case something goes wrong.
 *	The timeout should be longer then the maximum execution time of a
 *	tape operation. I still have to measure exactly how much time does
 *	it take to space over a far filemark, etc. It seemed that 15 minutes
 *	was way too low, so I am meanwhile setting it to a rather large
 *	timeout - 2 Hours ...
 *
 */

/*
 *	Setting IDETAPE_ANTICIPATE_READ_WRITE_DSC to 1 will allow ide-tape
 *	to cleverly select the lowest possible frequency which will
 *	not affect performance, based on the tape parameters and our operation
 *	mode. This has potential to dramatically decrease our polling load
 *	on Linux.
 *
 *	However, for the cases in which our calculation fails, setting
 *	the following option to 0 will force the use of the "fallback"
 *	polling period defined below (defaults to 50 msec).
 *
 *	In any case, the frequency will be between the "lowest" value
 *	to the "fallback" value, to ensure that our selected "best" frequency
 *	is reasonable.
 */

#define IDETAPE_ANTICIPATE_READ_WRITE_DSC	1

/*
 *	DSC timings.
 */
 
#define	IDETAPE_DSC_READ_WRITE_FALLBACK_FREQUENCY   5*HZ/100	/* 50 msec */
#define IDETAPE_DSC_READ_WRITE_LOWEST_FREQUENCY	30*HZ/100	/* 300 msec */
#define	IDETAPE_DSC_FAST_MEDIA_ACCESS_FREQUENCY	1*HZ		/* 1 second */
#define	IDETAPE_FAST_SLOW_THRESHOLD		5*60*HZ		/* 5 minutes */
#define IDETAPE_DSC_SLOW_MEDIA_ACCESS_FREQUENCY	60*HZ		/* 1 minute */
#define	IDETAPE_DSC_TIMEOUT			2*60*60*HZ	/* 2 hours */

/*************************** End of tunable parameters ***********************/

/*
 *	Definitions which are already needed in ide.h
 */

/*
 *	Current character device data transfer direction.
 */
  
typedef enum {idetape_direction_none,idetape_direction_read,idetape_direction_write} chrdev_direction_t;

struct ide_drive_s;				/* Forward declaration - Will be defined later in ide.h */
typedef void (idetape_pc_completed_t)(struct ide_drive_s *);

/*
 *	Our view of a packet command.
 */

typedef struct idetape_packet_command_s {
	byte c [12];				/* Actual packet bytes */
	
	byte retries;				/* On each retry, we increment retries */
	byte error;				/* Error code */
	byte abort;				/* Set when an error is considered normal - We won't retry */
	byte wait_for_dsc;			/* 1 When polling for DSC on a media access command */
	byte dma_recommended;			/* 1 when we prefer to use DMA if possible */
	byte dma_in_progress;			/* 1 while DMA in progress */
	byte dma_error;				/* 1 when encountered problem during DMA */
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
 *	A pipeline stage contains several small buffers of type
 *	idetape_buffer_head_t. This is necessary since dynamical allocation
 *	of large (32 KB or so) continuous memory blocks will usually fail.
 */
 
typedef struct idetape_buffer_head_s {
	char *data;					/* Pointer to data (512 bytes by default) */
	struct idetape_buffer_head_s *next;
} idetape_buffer_head_t;

/*
 *	A pipeline stage.
 *
 *	In a pipeline stage we have a request, pointer to a list of small
 *	buffers, and pointers to the near stages.
 */

typedef struct idetape_pipeline_stage_s {
	struct request rq;				/* The corresponding request */
	idetape_buffer_head_t *bh;			/* The data buffers */
	struct idetape_pipeline_stage_s *next,*prev;	/* Pointers to the next and previous stages */
} idetape_pipeline_stage_t;

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
	 *	However, our requests don't originate in the buffer cache but
	 *	rather in ide-tape.c itself. Here we provide safe storage for
	 *	such requests.
	 */

	struct request rq_stack [IDETAPE_PC_STACK];
	byte rq_stack_index;			/* We implement a circular array */

	/*
	 *	While polling for DSC we use postponed_rq to postpone the
	 *	current request so that ide.c will be able to service
	 *	pending requests on the other device. Note that at most
	 *	we will have only one DSC (usually data transfer) request
	 *	in the device request queue. Additional request can be
	 *	queued in our internal pipeline, but they will be visible
	 *	to ide.c only one at a time.
	 */

	struct request *postponed_rq;
	
	/*
	 *	DSC polling variables.
	 */
	 
	byte dsc_count;				/* We received DSC dsc_count times in a row */
	unsigned long dsc_polling_start;	/* The time in which we started polling for DSC */
	struct timer_list dsc_timer;		/* Timer used to poll for dsc */

	/*
	 *	We can now be much more clever in our selection of the
	 *	read/write polling frequency. This is used along with
	 *	the compile time option IDETAPE_ANTICIPATE_DSC.
	 */
 
	unsigned long best_dsc_rw_frequency;	/* Read/Write dsc polling frequency */

	unsigned long dsc_polling_frequency;	/* The current polling frequency */
	unsigned long dsc_timeout;		/* Maximum waiting time */
	byte dsc_received;			/* Set when we receive DSC */

	byte request_status;
	byte last_status;			/* Contents of the tape status register */
						/* before the current request (saved for us */
						/* by ide.c) */
	/*
	 *	After an ATAPI software reset, the status register will be
	 *	locked, and thus we need to ignore it when checking DSC for
	 *	the first time.
	 */
	 
	byte reset_issued;

	/* Position information */
	
	byte partition_num;			/* Currently not used */
	unsigned long block_address;		/* Current block */
	byte block_address_valid;		/* 0 When the tape position is unknown */
						/* (To the tape or to us) */
	/* Last error information */
	
	byte sense_key,asc,ascq;

	/* Character device operation */

	chrdev_direction_t chrdev_direction;	/* Current character device data transfer direction */
	byte busy;				/* Device already opened */

	/* Device information */
	
	unsigned short tape_block_size;			/* Usually 512 or 1024 bytes */
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
	char *data_buffer;			/* The corresponding data buffer (for read/write requests) */
	int data_buffer_size;			/* Data buffer size (chosen based on the tape's recommendation */

	char *merge_buffer;			/* Temporary buffer for user <-> kernel space data transfer */
	int merge_buffer_offset;
	int merge_buffer_size;
	
	/*
	 *	Pipeline parameters.
	 *
	 *	To accomplish non-pipelined mode, we simply set the following
	 *	variables to zero (or NULL, where appropriate).
	 */
		
	int current_number_of_stages;		/* Number of currently used stages */
	int max_number_of_stages;		/* We will not allocate more than this number of stages */
	idetape_pipeline_stage_t *first_stage;	/* The first stage which will be removed from the pipeline */
	idetape_pipeline_stage_t *active_stage;	/* The currently active stage */
	idetape_pipeline_stage_t *next_stage;	/* Will be serviced after the currently active request */
	idetape_pipeline_stage_t *last_stage;	/* New requests will be added to the pipeline here */
	int error_in_pipeline_stage;		/* Set when an error was detected in one of the pipeline stages */	
	
} idetape_tape_t;

/*
 *	The following is used to have a quick look at the tape's status
 *	register between requests of the other device.
 */
 
#define POLL_HWIF_TAPE_DRIVE							\
	if (hwif->tape_drive != NULL) {						\
		if (hwif->tape_drive->tape.request_status) {			\
			SELECT_DRIVE(hwif,hwif->tape_drive);			\
			hwif->tape_drive->tape.last_status=GET_STAT();		\
			hwif->tape_drive->tape.request_status=0;		\
		}								\
	}

#endif /* IDETAPE_H */
