/*
 *  scsi.h Copyright (C) 1992 Drew Eckhardt 
 *         Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *  generic SCSI package header file by
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *       Modified by Eric Youngdale eric@aib.com to
 *       add scatter-gather, multiple outstanding request, and other
 *       enhancements.
 */

#ifndef _SCSI_H
#define _SCSI_H

#include <linux/config.h> /* for CONFIG_SCSI_LOGGING */

/*
 * Some of the public constants are being moved to this file.
 * We include it here so that what came from where is transparent.
 */
#include <scsi/scsi.h>

#include <linux/random.h>

#include <asm/hardirq.h>
#include <asm/scatterlist.h>
#include <asm/io.h>

/*
 * Some defs, in case these are not defined elsewhere.
 */
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

#define MAX_SCSI_DEVICE_CODE 10
extern const char *const scsi_device_types[MAX_SCSI_DEVICE_CODE];

#ifdef DEBUG
    #define SCSI_TIMEOUT (5*HZ)
#else
    #define SCSI_TIMEOUT (2*HZ)
#endif

/*
 *  Use these to separate status msg and our bytes
 *
 *  These are set by:
 *
 *      status byte = set from target device
 *      msg_byte    = return status from host adapter itself.
 *      host_byte   = set by low-level driver to indicate status.
 *      driver_byte = set by mid-level.
 */
#define status_byte(result) (((result) >> 1) & 0x1f)
#define msg_byte(result)    (((result) >> 8) & 0xff)
#define host_byte(result)   (((result) >> 16) & 0xff)
#define driver_byte(result) (((result) >> 24) & 0xff)
#define suggestion(result)  (driver_byte(result) & SUGGEST_MASK)

#define sense_class(sense)  (((sense) >> 4) & 0x7)
#define sense_error(sense)  ((sense) & 0xf)
#define sense_valid(sense)  ((sense) & 0x80);

#define NEEDS_RETRY     0x2001
#define SUCCESS         0x2002
#define FAILED          0x2003
#define QUEUED          0x2004
#define SOFT_ERROR      0x2005
#define ADD_TO_MLQUEUE  0x2006

/*
 * These are the values that scsi_cmd->state can take.
 */
#define SCSI_STATE_TIMEOUT         0x1000
#define SCSI_STATE_FINISHED        0x1001
#define SCSI_STATE_FAILED          0x1002
#define SCSI_STATE_QUEUED          0x1003
#define SCSI_STATE_UNUSED          0x1006
#define SCSI_STATE_DISCONNECTING   0x1008
#define SCSI_STATE_INITIALIZING    0x1009
#define SCSI_STATE_BHQUEUE         0x100a
#define SCSI_STATE_MLQUEUE         0x100b

/*
 * These are the values that the owner field can take.
 * They are used as an indication of who the command belongs to.
 */
#define SCSI_OWNER_HIGHLEVEL      0x100
#define SCSI_OWNER_MIDLEVEL       0x101
#define SCSI_OWNER_LOWLEVEL       0x102
#define SCSI_OWNER_ERROR_HANDLER  0x103
#define SCSI_OWNER_BH_HANDLER     0x104
#define SCSI_OWNER_NOBODY         0x105

#define COMMAND_SIZE(opcode) scsi_command_size[((opcode) >> 5) & 7]

#define IDENTIFY_BASE       0x80
#define IDENTIFY(can_disconnect, lun)   (IDENTIFY_BASE |\
		     ((can_disconnect) ?  0x40 : 0) |\
		     ((lun) & 0x07)) 

		 
/*
 * This defines the scsi logging feature.  It is a means by which the
 * user can select how much information they get about various goings on,
 * and it can be really useful for fault tracing.  The logging word is divided
 * into 8 nibbles, each of which describes a loglevel.  The division of things
 * is somewhat arbitrary, and the division of the word could be changed if it
 * were really needed for any reason.  The numbers below are the only place where these
 * are specified.  For a first go-around, 3 bits is more than enough, since this
 * gives 8 levels of logging (really 7, since 0 is always off).  Cutting to 2 bits
 * might be wise at some point.
 */

#define SCSI_LOG_ERROR_SHIFT              0
#define SCSI_LOG_TIMEOUT_SHIFT            3
#define SCSI_LOG_SCAN_SHIFT               6
#define SCSI_LOG_MLQUEUE_SHIFT            9
#define SCSI_LOG_MLCOMPLETE_SHIFT         12
#define SCSI_LOG_LLQUEUE_SHIFT            15
#define SCSI_LOG_LLCOMPLETE_SHIFT         18
#define SCSI_LOG_HLQUEUE_SHIFT            21
#define SCSI_LOG_HLCOMPLETE_SHIFT         24
#define SCSI_LOG_IOCTL_SHIFT              27

#define SCSI_LOG_ERROR_BITS               3
#define SCSI_LOG_TIMEOUT_BITS             3
#define SCSI_LOG_SCAN_BITS                3
#define SCSI_LOG_MLQUEUE_BITS             3
#define SCSI_LOG_MLCOMPLETE_BITS          3
#define SCSI_LOG_LLQUEUE_BITS             3
#define SCSI_LOG_LLCOMPLETE_BITS          3
#define SCSI_LOG_HLQUEUE_BITS             3
#define SCSI_LOG_HLCOMPLETE_BITS          3
#define SCSI_LOG_IOCTL_BITS               3

#if CONFIG_SCSI_LOGGING

#define SCSI_CHECK_LOGGING(SHIFT, BITS, LEVEL, CMD)     \
{                                                       \
        unsigned int mask;                              \
                                                        \
        mask = (1 << (BITS)) - 1;                       \
        if( ((scsi_logging_level >> (SHIFT)) & mask) > (LEVEL) ) \
        {                                               \
                (CMD);                                  \
        }						\
}

#define SCSI_SET_LOGGING(SHIFT, BITS, LEVEL)            \
{                                                       \
        unsigned int mask;                              \
                                                        \
        mask = ((1 << (BITS)) - 1) << SHIFT;            \
        scsi_logging_level = ((scsi_logging_level & ~mask) \
                              | ((LEVEL << SHIFT) & mask));     \
}



#else

/*
 * With no logging enabled, stub these out so they don't do anything.
 */
#define SCSI_SET_LOGGING(SHIFT, BITS, LEVEL)

#define SCSI_CHECK_LOGGING(SHIFT, BITS, LEVEL, CMD)
#endif

/*
 * These are the macros that are actually used throughout the code to
 * log events.  If logging isn't enabled, they are no-ops and will be
 * completely absent from the user's code.
 *
 * The 'set' versions of the macros are really intended to only be called
 * from the /proc filesystem, and in production kernels this will be about
 * all that is ever used.  It could be useful in a debugging environment to
 * bump the logging level when certain strange events are detected, however.
 */
#define SCSI_LOG_ERROR_RECOVERY(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_ERROR_SHIFT, SCSI_LOG_ERROR_BITS, LEVEL,CMD);
#define SCSI_LOG_TIMEOUT(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_TIMEOUT_SHIFT, SCSI_LOG_TIMEOUT_BITS, LEVEL,CMD);
#define SCSI_LOG_SCAN_BUS(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_SCAN_SHIFT, SCSI_LOG_SCAN_BITS, LEVEL,CMD);
#define SCSI_LOG_MLQUEUE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_MLQUEUE_SHIFT, SCSI_LOG_MLQUEUE_BITS, LEVEL,CMD);
#define SCSI_LOG_MLCOMPLETE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_MLCOMPLETE_SHIFT, SCSI_LOG_MLCOMPLETE_BITS, LEVEL,CMD);
#define SCSI_LOG_LLQUEUE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_LLQUEUE_SHIFT, SCSI_LOG_LLQUEUE_BITS, LEVEL,CMD);
#define SCSI_LOG_LLCOMPLETE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_LLCOMPLETE_SHIFT, SCSI_LOG_LLCOMPLETE_BITS, LEVEL,CMD);
#define SCSI_LOG_HLQUEUE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_HLQUEUE_SHIFT, SCSI_LOG_HLQUEUE_BITS, LEVEL,CMD);
#define SCSI_LOG_HLCOMPLETE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_HLCOMPLETE_SHIFT, SCSI_LOG_HLCOMPLETE_BITS, LEVEL,CMD);
#define SCSI_LOG_IOCTL(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_IOCTL_SHIFT, SCSI_LOG_IOCTL_BITS, LEVEL,CMD);
    

#define SCSI_SET_ERROR_RECOVERY_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_ERROR_SHIFT, SCSI_LOG_ERROR_BITS, LEVEL);
#define SCSI_SET_TIMEOUT_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_TIMEOUT_SHIFT, SCSI_LOG_TIMEOUT_BITS, LEVEL);
#define SCSI_SET_SCAN_BUS_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_SCAN_SHIFT, SCSI_LOG_SCAN_BITS, LEVEL);
#define SCSI_SET_MLQUEUE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_MLQUEUE_SHIFT, SCSI_LOG_MLQUEUE_BITS, LEVEL);
#define SCSI_SET_MLCOMPLETE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_MLCOMPLETE_SHIFT, SCSI_LOG_MLCOMPLETE_BITS, LEVEL);
#define SCSI_SET_LLQUEUE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_LLQUEUE_SHIFT, SCSI_LOG_LLQUEUE_BITS, LEVEL);
#define SCSI_SET_LLCOMPLETE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_LLCOMPLETE_SHIFT, SCSI_LOG_LLCOMPLETE_BITS, LEVEL);
#define SCSI_SET_HLQUEUE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_HLQUEUE_SHIFT, SCSI_LOG_HLQUEUE_BITS, LEVEL);
#define SCSI_SET_HLCOMPLETE_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_HLCOMPLETE_SHIFT, SCSI_LOG_HLCOMPLETE_BITS, LEVEL);
#define SCSI_SET_IOCTL_LOGGING(LEVEL)  \
        SCSI_SET_LOGGING(SCSI_LOG_IOCTL_SHIFT, SCSI_LOG_IOCTL_BITS, LEVEL);
    
/*
 *  the return of the status word will be in the following format :
 *  The low byte is the status returned by the SCSI command, 
 *  with vendor specific bits masked.
 *  
 *  The next byte is the message which followed the SCSI status.
 *  This allows a stos to be used, since the Intel is a little
 *  endian machine.
 *  
 *  The final byte is a host return code, which is one of the following.
 *  
 *  IE 
 *  lsb     msb
 *  status  msg host code   
 *  
 *  Our errors returned by OUR driver, NOT SCSI message.  Or'd with
 *  SCSI message passed back to driver <IF any>.
 */


#define DID_OK          0x00 /* NO error                                */
#define DID_NO_CONNECT  0x01 /* Couldn't connect before timeout period  */
#define DID_BUS_BUSY    0x02 /* BUS stayed busy through time out period */
#define DID_TIME_OUT    0x03 /* TIMED OUT for other reason              */
#define DID_BAD_TARGET  0x04 /* BAD target.                             */
#define DID_ABORT       0x05 /* Told to abort for some other reason     */
#define DID_PARITY      0x06 /* Parity error                            */
#define DID_ERROR       0x07 /* Internal error                          */
#define DID_RESET       0x08 /* Reset by somebody.                      */
#define DID_BAD_INTR    0x09 /* Got an interrupt we weren't expecting.  */ 
#define DID_PASSTHROUGH 0x0a /* Force command past mid-layer            */
#define DID_SOFT_ERROR  0x0b /* The low level driver just wish a retry  */
#define DRIVER_OK       0x00 /* Driver status                           */ 

/*
 *  These indicate the error that occurred, and what is available.
 */

#define DRIVER_BUSY         0x01
#define DRIVER_SOFT         0x02
#define DRIVER_MEDIA        0x03
#define DRIVER_ERROR        0x04    

#define DRIVER_INVALID      0x05
#define DRIVER_TIMEOUT      0x06
#define DRIVER_HARD         0x07
#define DRIVER_SENSE	    0x08

#define SUGGEST_RETRY       0x10
#define SUGGEST_ABORT       0x20 
#define SUGGEST_REMAP       0x30
#define SUGGEST_DIE         0x40
#define SUGGEST_SENSE       0x80
#define SUGGEST_IS_OK       0xff

#define DRIVER_MASK         0x0f
#define SUGGEST_MASK        0xf0

#define MAX_COMMAND_SIZE    12

/*
 *  SCSI command sets
 */

#define SCSI_UNKNOWN    0
#define SCSI_1          1
#define SCSI_1_CCS      2
#define SCSI_2          3

/*
 *  Every SCSI command starts with a one byte OP-code.
 *  The next byte's high three bits are the LUN of the
 *  device.  Any multi-byte quantities are stored high byte
 *  first, and may have a 5 bit MSB in the same byte
 *  as the LUN.
 */

/*
 *  As the scsi do command functions are intelligent, and may need to
 *  redo a command, we need to keep track of the last command
 *  executed on each one.
 */

#define WAS_RESET       0x01
#define WAS_TIMEDOUT    0x02
#define WAS_SENSE       0x04
#define IS_RESETTING    0x08
#define IS_ABORTING     0x10
#define ASKED_FOR_SENSE 0x20
#define SYNC_RESET      0x40

#if defined(__mc68000__) || defined(CONFIG_APUS)
#include <asm/pgtable.h>
#define CONTIGUOUS_BUFFERS(X,Y) \
	(virt_to_phys((X)->b_data+(X)->b_size-1)+1==virt_to_phys((Y)->b_data))
#else
#define CONTIGUOUS_BUFFERS(X,Y) ((X->b_data+X->b_size) == Y->b_data)
#endif


/*
 * This is the crap from the old error handling code.  We have it in a special
 * place so that we can more easily delete it later on.
 */
#include "scsi_obsolete.h"

/*
 * Add some typedefs so that we can prototyope a bunch of the functions.
 */
typedef struct scsi_device Scsi_Device;
typedef struct scsi_cmnd   Scsi_Cmnd;

/*
 * Here is where we prototype most of the mid-layer.
 */

/*
 *  Initializes all SCSI devices.  This scans all scsi busses.
 */ 

extern int scsi_dev_init (void);



void *   scsi_malloc(unsigned int);
int      scsi_free(void *, unsigned int);
extern unsigned int scsi_logging_level; /* What do we log? */
extern unsigned int scsi_dma_free_sectors;  /* How much room do we have left */
extern unsigned int scsi_need_isa_buffer;   /* True if some devices need indirection
					* buffers */
extern void scsi_make_blocked_list(void);
extern volatile int in_scan_scsis;
extern const unsigned char scsi_command_size[8];

/*
 * These are the error handling functions defined in scsi_error.c
 */
extern void scsi_add_timer(Scsi_Cmnd * SCset, int timeout, 
                                void (*complete)(Scsi_Cmnd *));
extern void scsi_done (Scsi_Cmnd *SCpnt);
extern int  scsi_delete_timer(Scsi_Cmnd * SCset);
extern void scsi_error_handler(void * host);
extern int  scsi_retry_command(Scsi_Cmnd *);
extern void scsi_finish_command(Scsi_Cmnd *);
extern int  scsi_sense_valid(Scsi_Cmnd *);
extern int  scsi_decide_disposition (Scsi_Cmnd * SCpnt);
extern int  scsi_block_when_processing_errors(Scsi_Device *);
extern void scsi_sleep(int);

/*
 *  scsi_abort aborts the current command that is executing on host host.
 *  The error code, if non zero is returned in the host byte, otherwise 
 *  DID_ABORT is returned in the hostbyte.
 */

extern void scsi_do_cmd (Scsi_Cmnd *, const void *cmnd ,
			 void *buffer, unsigned bufflen, 
			 void (*done)(struct scsi_cmnd *),
			 int timeout, int retries);


extern Scsi_Cmnd * scsi_allocate_device(struct request **, Scsi_Device *, int);

extern Scsi_Cmnd * scsi_request_queueable(struct request *, Scsi_Device *);

extern void scsi_release_command(Scsi_Cmnd *);

extern int max_scsi_hosts;

extern void proc_print_scsidevice(Scsi_Device *, char *, int *, int);

extern void print_command(unsigned char *);
extern void print_sense(const char *, Scsi_Cmnd *);
extern void print_driverbyte(int scsiresult);
extern void print_hostbyte(int scsiresult);

/*
 *  The scsi_device struct contains what we know about each given scsi
 *  device.
 */

struct scsi_device {
/* private: */
    /*
     * This information is private to the scsi mid-layer.  Wrapping it in a
     * struct private is a way of marking it in a sort of C++ type of way.
     */
    struct scsi_device * next;      /* Used for linked list */
    struct scsi_device * prev;      /* Used for linked list */
    struct wait_queue  * device_wait;/* Used to wait if
                                                      device is busy */
    struct Scsi_Host   * host;
    volatile unsigned short device_busy;   /* commands actually active on low-level */
    void              (* scsi_request_fn)(void);  /* Used to jumpstart things after an 
                                     * ioctl */
    Scsi_Cmnd          * device_queue;    /* queue of SCSI Command structures */

/* public: */
    unsigned char id, lun, channel;

    unsigned int manufacturer;      /* Manufacturer of device, for using 
				     * vendor-specific cmd's */
    int attached;                   /* # of high level drivers attached to 
				     * this */
    int access_count;               /* Count of open channels/mounts */

    void *hostdata;                 /* available to low-level driver */
    char type;
    char scsi_level;
    char vendor[8], model[16], rev[4];
    unsigned char current_tag;      /* current tag */
    unsigned char sync_min_period;  /* Not less than this period */
    unsigned char sync_max_offset;  /* Not greater than this offset */
    unsigned char queue_depth;	    /* How deep a queue to use */

    unsigned online:1;
    unsigned writeable:1;
    unsigned removable:1; 
    unsigned random:1;
    unsigned has_cmdblocks:1;
    unsigned changed:1;             /* Data invalid due to media change */
    unsigned busy:1;                /* Used to prevent races */
    unsigned lockable:1;            /* Able to prevent media removal */
    unsigned borken:1;              /* Tell the Seagate driver to be 
				     * painfully slow on this device */ 
    unsigned tagged_supported:1;    /* Supports SCSI-II tagged queuing */
    unsigned tagged_queue:1;        /* SCSI-II tagged queuing enabled */
    unsigned disconnect:1;          /* can disconnect */
    unsigned soft_reset:1;          /* Uses soft reset option */
    unsigned sync:1;                /* Negotiate for sync transfers */
    unsigned wide:1;                /* Negotiate for WIDE transfers */
    unsigned single_lun:1;          /* Indicates we should only allow I/O to
                                     * one of the luns for the device at a 
                                     * time. */
    unsigned was_reset:1;           /* There was a bus reset on the bus for 
                                     * this device */
    unsigned expecting_cc_ua:1;     /* Expecting a CHECK_CONDITION/UNIT_ATTN
                                     * because we did a bus reset. */
    unsigned device_blocked:1;      /* Device returned QUEUE_FULL. */
};


/*
 * The Scsi_Cmnd structure is used by scsi.c internally, and for communication
 * with low level drivers that support multiple outstanding commands.
 */
typedef struct scsi_pointer {
    char * ptr;                     /* data pointer */
    int this_residual;              /* left in this buffer */
    struct scatterlist *buffer;     /* which buffer */
    int buffers_residual;           /* how many buffers left */
    
    volatile int Status;
    volatile int Message;
    volatile int have_data_in;
    volatile int sent_command;
    volatile int phase;
} Scsi_Pointer;


struct scsi_cmnd {
/* private: */
    /*
     * This information is private to the scsi mid-layer.  Wrapping it in a
     * struct private is a way of marking it in a sort of C++ type of way.
     */
    struct Scsi_Host * host;
    unsigned short     state;
    unsigned short     owner;
    Scsi_Device      * device;
    struct scsi_cmnd * next;
    struct scsi_cmnd * reset_chain;
    
    int                 eh_state; /* Used for state tracking in error handlr */
    void               (*done)(struct scsi_cmnd *);  /* Mid-level done function */
    /*
      A SCSI Command is assigned a nonzero serial_number when internal_cmnd
      passes it to the driver's queue command function.  The serial_number
      is cleared when scsi_done is entered indicating that the command has
      been completed.  If a timeout occurs, the serial number at the moment
      of timeout is copied into serial_number_at_timeout.  By subsequently
      comparing the serial_number and serial_number_at_timeout fields
      during abort or reset processing, we can detect whether the command
      has already completed.  This also detects cases where the command has
      completed and the SCSI Command structure has already being reused
      for another command, so that we can avoid incorrectly aborting or
      resetting the new command.
      */
    
    unsigned long      serial_number;
    unsigned long      serial_number_at_timeout;
    
    int                retries;
    int                allowed;
    int                timeout_per_command;
    int                timeout_total;
    int                timeout;
    
    /*
     * We handle the timeout differently if it happens when a reset, 
     * abort, etc are in process. 
     */
    unsigned volatile char internal_timeout;
    struct scsi_cmnd  * bh_next;  /* To enumerate the commands waiting 
                                     to be processed. */
    
/* public: */

    unsigned char      target;
    unsigned char      lun;
    unsigned char      channel;
    unsigned char      cmd_len;
    unsigned char      old_cmd_len;

    /* These elements define the operation we are about to perform */
    unsigned char      cmnd[12];
    unsigned           request_bufflen;	/* Actual request size */
    
    struct timer_list  eh_timeout;         /* Used to time out the command. */
    void             * request_buffer;	/* Actual requested buffer */
    
    /* These elements define the operation we ultimately want to perform */
    unsigned char      data_cmnd[12];
    unsigned short     old_use_sg;	/* We save  use_sg here when requesting
                                         * sense info */
    unsigned short     use_sg;          /* Number of pieces of scatter-gather */
    unsigned short     sglist_len;	/* size of malloc'd scatter-gather list */
    unsigned short     abort_reason;    /* If the mid-level code requests an
                                         * abort, this is the reason. */
    unsigned           bufflen;		/* Size of data buffer */
    void             * buffer;		/* Data buffer */
    
    unsigned           underflow;	/* Return error if less than
                                           this amount is transfered */
    
    unsigned           transfersize;	/* How much we are guaranteed to
                                           transfer with each SCSI transfer
                                           (ie, between disconnect / 
                                           reconnects.	 Probably == sector
                                           size */
    
    
    struct request     request;           /* A copy of the command we are
                                             working on */

    unsigned char      sense_buffer[16];  /* Sense for this command, 
                                             needed */
    
    unsigned           flags;
    
    /*
     * These two flags are used to track commands that are in the
     * mid-level queue.  The idea is that a command can be there for
     * one of two reasons - either the host is busy or the device is
     * busy.  Thus when a command on the host finishes, we only try
     * and requeue commands that we might expect to be queueable.
     */
    unsigned           host_wait:1;
    unsigned           device_wait:1;

    /* These variables are for the cdrom only. Once we have variable size 
     * buffers in the buffer cache, they will go away. */
    int                this_count; 
    /* End of special cdrom variables */
    
    /* Low-level done function - can be used by low-level driver to point
     *	to completion function.	 Not used by mid/upper level code. */
    void               (*scsi_done)(struct scsi_cmnd *);  
    
    /*
     * The following fields can be written to by the host specific code. 
     * Everything else should be left alone. 
     */
    
    Scsi_Pointer       SCp;	   /* Scratchpad used by some host adapters */
    
    unsigned char    * host_scribble; /* The host adapter is allowed to
				    * call scsi_malloc and get some memory
				    * and hang it here.	 The host adapter
				    * is also expected to call scsi_free
				    * to release this memory.  (The memory
				    * obtained by scsi_malloc is guaranteed
				    * to be at an address < 16Mb). */
    
    int                result;	   /* Status code from lower level driver */
    
    unsigned char      tag;	   /* SCSI-II queued command tag */
    unsigned long      pid;	   /* Process ID, starts at 0 */
};


/*
 * Definitions and prototypes used for scsi mid-level queue.
 */
#define SCSI_MLQUEUE_HOST_BUSY   0x1055
#define SCSI_MLQUEUE_DEVICE_BUSY 0x1056

extern int scsi_mlqueue_insert(Scsi_Cmnd * cmd, int reason);
extern int scsi_mlqueue_finish(struct Scsi_Host * host, Scsi_Device * device);


#if defined(MAJOR_NR) && (MAJOR_NR != SCSI_TAPE_MAJOR)
#include "hosts.h"

static Scsi_Cmnd * end_scsi_request(Scsi_Cmnd * SCpnt, int uptodate, int sectors)
{
    struct request * req;
    struct buffer_head * bh;
    
    req = &SCpnt->request;
    req->errors = 0;
    if (!uptodate) {
	printk(DEVICE_NAME " I/O error: dev %s, sector %lu\n",
	       kdevname(req->rq_dev), req->sector);
    }
    
    do {
	if ((bh = req->bh) != NULL) {
	    req->bh = bh->b_reqnext;
	    req->nr_sectors -= bh->b_size >> 9;
	    req->sector += bh->b_size >> 9;
	    bh->b_reqnext = NULL;
	    bh->b_end_io(bh, uptodate);
	    sectors -= bh->b_size >> 9;
	    if ((bh = req->bh) != NULL) {
		req->current_nr_sectors = bh->b_size >> 9;
		if (req->nr_sectors < req->current_nr_sectors) {
		    req->nr_sectors = req->current_nr_sectors;
		    printk("end_scsi_request: buffer-list destroyed\n");
		}
	    }
	}
    } while(sectors && bh);
    if (req->bh){
	req->buffer = bh->b_data;
	return SCpnt;
    }
    DEVICE_OFF(req->rq_dev);
    if (req->sem != NULL) {
	up(req->sem);
    }
    add_blkdev_randomness(MAJOR(req->rq_dev));
    
    if (SCpnt->host->block) {
	struct Scsi_Host * next;
	
	for (next = SCpnt->host->block; next != SCpnt->host;
	     next = next->block)
	    wake_up(&next->host_wait);
    }
    
    wake_up(&wait_for_request);
    wake_up(&SCpnt->device->device_wait);
    scsi_release_command(SCpnt);
    return NULL;
}


/* This is just like INIT_REQUEST, but we need to be aware of the fact
 * that an interrupt may start another request, so we run this with interrupts
 * turned off 
 */
#if MAJOR_NR == SCSI_DISK0_MAJOR
#define CHECK_INITREQ_SD_MAJOR(major) SCSI_DISK_MAJOR(major)
#else
#define CHECK_INITREQ_SD_MAJOR(major) ((major) == MAJOR_NR)
#endif

#define INIT_SCSI_REQUEST       			\
    if (!CURRENT) {             			\
	CLEAR_INTR;             			\
	return;                 			\
    }                           			\
    if (!CHECK_INITREQ_SD_MAJOR(MAJOR(CURRENT->rq_dev)))\
	panic(DEVICE_NAME ": request list destroyed");	\
    if (CURRENT->bh) {                                	\
	if (!buffer_locked(CURRENT->bh))              	\
	    panic(DEVICE_NAME ": block not locked");  	\
    }
#endif

#define SCSI_SLEEP(QUEUE, CONDITION) {		    \
    if (CONDITION) {			            \
	struct wait_queue wait = { current, NULL};  \
	add_wait_queue(QUEUE, &wait);		    \
	for(;;) {			            \
	current->state = TASK_UNINTERRUPTIBLE;	    \
	if (CONDITION) {		            \
            if (in_interrupt())	                    \
	        panic("scsi: trying to call schedule() in interrupt" \
		      ", file %s, line %d.\n", __FILE__, __LINE__);  \
	    schedule();			\
        }				\
	else			        \
	    break;      		\
	}			        \
	remove_wait_queue(QUEUE, &wait);\
	current->state = TASK_RUNNING;	\
    }; }

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
