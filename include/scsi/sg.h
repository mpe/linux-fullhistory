#ifndef _SCSI_GENERIC_H
#define _SCSI_GENERIC_H

/*
   History:
    Started: Aug 9 by Lawrence Foard (entropy@world.std.com), to allow user 
     process control of SCSI devices. 
    Development Sponsored by Killy Corp. NY NY
Original driver (sg.h):
*       Copyright (C) 1992 Lawrence Foard
2.x extensions to driver:
*       Copyright (C) 1998, 1999 Douglas Gilbert


    Version: 2.3.35 (990708)
    This version for 2.3 series kernels. It only differs from sg version
    2.1.35 used in the 2.2 series kernels by changes to wait_queue. This
    in an internal kernel interface and should not effect users.
        D. P. Gilbert (dgilbert@interlog.com, dougg@triode.net.au)

    Changes since 2.1.34 (990603)
        - add queuing info into struct sg_scsi_id
        - block negative timeout values
        - add back write() wait on previous read() when no cmd queuing
    Changes since 2.1.33 (990521)
        - implement SG_SET_RESERVED_SIZE and associated memory re-org.
        - add SG_NEXT_CMD_LEN to override SCSI command lengths
        - add SG_GET_VERSION_NUM to get version expressed as an integer
    Changes since 2.1.32 (990501)
        - fix race condition in sg_read() and sg_open()
    Changes since 2.1.31 (990327)
        - add ioctls SG_GET_UNDERRUN_FLAG and _SET_. Change the default
          to _not_ flag underruns (affects aic7xxx driver)
        - clean up logging of pointers to use %p (for 64 bit architectures)
        - rework usage of get_user/copy_to_user family of kernel calls
        - "disown" scsi_command blocks before releasing them
    Changes since 2.1.30 (990320)
        - memory tweaks: change flags on kmalloc (GFP_KERNEL to GFP_ATOMIC)
        -                increase max allowable mid-level pool usage


    New features and changes:
        - per file descriptor (fd) write-read sequencing and command queues.
        - command queuing supported (SG_MAX_QUEUE is maximum per fd).
        - scatter-gather supported (allowing potentially megabyte transfers).
        - the SCSI target, host and driver status are returned
          in unused fields of sg_header (maintaining its original size).
        - asynchronous notification support added (SIGPOLL, SIGIO) for
          read()s (write()s should never block).
        - pack_id logic added so read() can wait for a specific pack_id. 
        - uses memory > ISA_DMA_THRESHOLD if adapter allows it (e.g. a
          pci scsi adapter).
        - this driver no longer uses a single SG_BIG_BUFF sized buffer
          obtained at driver/module init time. Rather it tries to obtain a 
          SG_DEF_RESERVED_SIZE buffer when a fd is open()ed and frees it
          at the corresponding release() (ie per fd). Actually the "buffer"
          may be a collection of buffers if scatter-gather is being used.
        - add SG_SET_RESERVED_SIZE ioctl allowing the user to request a
          large buffer for duration of current file descriptor's lifetime.
        - SG_GET_RESERVED_SIZE ioctl can be used to find out how much
          actually has been reserved.
        - add SG_NEXT_CMD_LEN ioctl to override SCSI command length on
          the next write() to this file descriptor.
        - SG_GET_RESERVED_SIZE's presence as a symbol can be used for
          compile time identification of the version 2 sg driver.
          However, it is recommended that run time identification based on
          calling the ioctl of the same name is a more flexible and
          safer approach.
        - adds several ioctl calls, see ioctl section below.
 
 Good documentation on the original "sg" device interface and usage can be
 found in the Linux HOWTO document: "SCSI Programming HOWTO" (version 0.5)
 by Heiko Eissfeldt; last updated 7 May 1996. Here is a quick summary of
 sg basics:
 An SG device is accessed by writing SCSI commands plus any associated 
 outgoing data to it; the resulting status codes and any incoming data
 are then obtained by a read call. The device can be opened O_NONBLOCK
 (non-blocking) and poll() used to monitor its progress. The device may be
 opened O_EXCL which excludes other "sg" users from this device (but not 
 "sd", "st" or "sr" users). The buffer given to the write() call is made
 up as follows:
        - struct sg_header image (see below)
        - scsi command (6, 10 or 12 bytes long)
        - data to be written to the device (if any)

 The buffer received from the corresponding read() call contains:
        - struct sg_header image (check results + sense_buffer)
        - data read back from device (if any)

 The given SCSI command has its LUN field overwritten internally by the
 value associated with the device that has been opened.

 This device currently uses "indirect IO" in the sense that data is
 DMAed into kernel buffers from the hardware and afterwards is
 transferred into the user space (or vice versa if you are writing).
 Transfer speeds or up to 20 to 30MBytes/sec have been measured using
 indirect IO. For faster throughputs "direct IO" which cuts out the
 double handling of data is required. This will also need a new interface.

 Grabbing memory for those kernel buffers used in this driver for DMA may
 cause the dreaded ENOMEM error. This error seems to be more prevalent 
 under early 2.2.x kernels than under the 2.0.x kernel series. For a given 
 (large) transfer the memory obtained by this driver must be contiguous or
 scatter-gather must be used (if supported by the adapter). [Furthermore, 
 ISA SCSI adapters can only use memory below the 16MB level on a i386.]

 When a "sg" device is open()ed O_RDWR then this driver will attempt to
 reserve a buffer of SG_DEF_RESERVED_SIZE that will be used by subsequent
 write()s on this file descriptor as long as:
    -  it is not already in use (eg when command queuing is in use)
    -  the write() does not call for a buffer size larger than the
       reserved size.
 In these cases the write() will attempt to find the memory it needs for
 DMA buffers dynamically and in the worst case will fail with ENOMEM.
 The amount of memory actually reserved depends on various dynamic factors
 and can be checked with the SG_GET_RESERVED_SIZE ioctl(). [In a very
 tight memory situation it may yield 0!] The size of the reserved buffer
 can be changed with the SG_SET_RESERVED_SIZE ioctl(). It should be
 followed with a call to the SG_GET_RESERVED_SIZE ioctl() to find out how
 much was actually reserved.

 More documentation plus test and utility programs can be found at 
 http://www.torque.net/sg
*/

#define SG_MAX_SENSE 16   /* too little, unlikely to change in 2.2.x */

struct sg_header
{
    int pack_len;    /* [o] reply_len (ie useless), ignored as input */
    int reply_len;   /* [i] max length of expected reply (inc. sg_header) */
    int pack_id;     /* [io] id number of packet (use ints >= 0) */
    int result;      /* [o] 0==ok, else (+ve) Unix errno (best ignored) */
    unsigned int twelve_byte:1; 
        /* [i] Force 12 byte command length for group 6 & 7 commands  */
    unsigned int target_status:5;   /* [o] scsi status from target */
    unsigned int host_status:8;     /* [o] host status (see "DID" codes) */
    unsigned int driver_status:8;   /* [o] driver status+suggestion */
    unsigned int other_flags:10;    /* unused */
    unsigned char sense_buffer[SG_MAX_SENSE]; /* [o] Output in 3 cases:
           when target_status is CHECK_CONDITION or 
           when target_status is COMMAND_TERMINATED or
           when (driver_status & DRIVER_SENSE) is true. */
};      /* This structure is 36 bytes long on i386 */


typedef struct sg_scsi_id {
    int host_no;        /* as in "scsi<n>" where 'n' is one of 0, 1, 2 etc */
    int channel;
    int scsi_id;        /* scsi id of target device */
    int lun;
    int scsi_type;      /* TYPE_... defined in scsi/scsi.h */
    short h_cmd_per_lun;/* host (adapter) maximum commands per lun */
    short d_queue_depth;/* device (or adapter) maximum queue length */
    int unused1;        /* probably find a good use, set 0 for now */
    int unused2;        /* ditto */
} Sg_scsi_id;

/* IOCTLs: ( _GET_s yield result via 'int *' 3rd argument unless 
             otherwise indicated) */
#define SG_SET_TIMEOUT 0x2201  /* unit: jiffies (10ms on i386) */
#define SG_GET_TIMEOUT 0x2202  /* yield timeout as _return_ value */

#define SG_EMULATED_HOST 0x2203 /* true for emulated host adapter (ATAPI) */

/* Used to configure SCSI command transformation layer for ATAPI devices */
#define SG_SET_TRANSFORM 0x2204
#define SG_GET_TRANSFORM 0x2205

#define SG_SET_RESERVED_SIZE 0x2275  /* request a new reserved buffer size */
#define SG_GET_RESERVED_SIZE 0x2272  /* actual size of reserved buffer */

/* The following ioctl takes a 'Sg_scsi_id *' object as its 3rd argument. */
#define SG_GET_SCSI_ID 0x2276   /* Yields fd's bus, chan, dev, lun + type */
/* SCSI id information can also be obtained from SCSI_IOCTL_GET_IDLUN */

/* Override host setting and always DMA using low memory ( <16MB on i386) */
#define SG_SET_FORCE_LOW_DMA 0x2279  /* 0-> use adapter setting, 1-> force */
#define SG_GET_LOW_DMA 0x227a   /* 0-> use all ram for dma; 1-> low dma ram */

/* When SG_SET_FORCE_PACK_ID set to 1, pack_id is input to read() which
   will attempt to read that pack_id or block (or return EAGAIN). If 
   pack_id is -1 then read oldest waiting. When ...FORCE_PACK_ID set to 0
   then pack_id ignored by read() and oldest readable fetched. */ 
#define SG_SET_FORCE_PACK_ID 0x227b
#define SG_GET_PACK_ID 0x227c /* Yields oldest readable pack_id (or -1) */

#define SG_GET_NUM_WAITING 0x227d /* Number of commands awaiting read() */

/* Turn on error sense trace (1..8), dump this device to log/console (9)
   or dump all sg device states ( >9 ) to log/console */
#define SG_SET_DEBUG 0x227e    /* 0 -> turn off debug */

/* Yields max scatter gather tablesize allowed by current host adapter */
#define SG_GET_SG_TABLESIZE 0x227F  /* 0 implies can't do scatter gather */

/* Control whether sequencing per file descriptor or per device */
#define SG_GET_MERGE_FD 0x2274   /* 0-> per fd, 1-> per device */
#define SG_SET_MERGE_FD 0x2273   /* Attempt to change sequencing state,
  if more than current fd open on device, will fail with EBUSY */

/* Get/set command queuing state per fd (default is SG_DEF_COMMAND_Q) */
#define SG_GET_COMMAND_Q 0x2270   /* Yields 0 (queuing off) or 1 (on) */
#define SG_SET_COMMAND_Q 0x2271   /* Change queuing state with 0 or 1 */

/* Get/set whether DMA underrun will cause an error (DID_ERROR). This only
   currently applies to the [much-used] aic7xxx driver. */
#define SG_GET_UNDERRUN_FLAG 0x2280 /* Yields 0 (don't flag) or 1 (flag) */
#define SG_SET_UNDERRUN_FLAG 0x2281 /* Change flag underrun state */

#define SG_GET_VERSION_NUM 0x2282 /* Example: version 2.1.34 yields 20134 */
#define SG_NEXT_CMD_LEN 0x2283  /* override SCSI command length with given
                   number on the next write() on this file descriptor */

/* Returns -EBUSY if occupied else takes as input: 0 -> do nothing,
   1 -> device reset or  2 -> bus reset (may not be activated yet) */
#define SG_SCSI_RESET 0x2284


#define SG_SCATTER_SZ (8 * 4096)  /* PAGE_SIZE not available to user */
/* Largest size (in bytes) a single scatter-gather list element can have.
   The value must be a power of 2 and <= (PAGE_SIZE * 32) [131072 bytes on 
   i386]. The minimum value is PAGE_SIZE. If scatter-gather not supported
   by adapter then this value is the largest data block that can be
   read/written by a single scsi command. The user can find the value of
   PAGE_SIZE by calling getpagesize() defined in unistd.h . */

#define SG_DEFAULT_TIMEOUT (60*HZ) /* HZ == 'jiffies in 1 second' */
#define SG_DEFAULT_RETRIES 1

/* Defaults, commented if they differ from original sg driver */
#define SG_DEF_COMMAND_Q 0
#define SG_DEF_MERGE_FD 0       /* was 1 -> per device sequencing */
#define SG_DEF_FORCE_LOW_DMA 0  /* was 1 -> memory below 16MB on i386 */
#define SG_DEF_FORCE_PACK_ID 0
#define SG_DEF_UNDERRUN_FLAG 0
#define SG_DEF_RESERVED_SIZE SG_SCATTER_SZ

/* maximum outstanding requests, write() yields EDOM if exceeded */
#define SG_MAX_QUEUE 16

#define SG_BIG_BUFF SG_DEF_RESERVED_SIZE    /* for backward compatibility */

#endif
