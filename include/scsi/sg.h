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


    Version: 2.1.32 (990501)
    This version for later 2.1.x series and 2.2.x kernels
    D. P. Gilbert (dgilbert@interlog.com, dougg@triode.net.au)

    Changes since 2.1.31 (990327)
        - add ioctls SG_GET_UNDERRUN_FLAG and _SET_. Change the default
          to _not_ flag underruns (affects aic7xxx driver)
        - clean up logging of pointers to use %p (for 64 bit architectures)
        - rework usage of get_user/copy_to_user family of kernel calls
        - "disown" scsi_command blocks before releasing them
    Changes since 2.1.30 (990320)
        - memory tweaks: change flags on kmalloc (GFP_KERNEL to GFP_ATOMIC)
        -                increase max allowable mid-level pool usage
    Changes since 2.1.21 (990315)
        - skipped to 2.1.30 indicating interface change (revert to 2.1.9)
        - remove attempt to accomodate cdrecord 1.8, will fix app
        - keep SG_?ET_RESERVED_SIZE naming for clarity
    Changes since 2.1.20 (990313)
        - ommission: left out logic for SG_?ET_ALT_INTERFACE, now added
    Changes since 2.1.9 (990309)
        - skipped to version 2.1.20 to indicate some interface changes
        - incorporate sg changes to make cdrecord 1.8 work (had its
          own patches that were different from the original)
        - change SG_?ET_BUFF_SIZE to SG_?ET_RESERVED_SIZE for clarity
    Changes since 2.1.8 (990303)
        - debug ">9" option dumps debug for _all_ active sg devices
        - increase allowable dma pool usage + increase minimum threshhold
        - pad out sg_scsi_id structure
    Changes since 2.1.7 (990227)
        - command queuing now "non-default" [back. compat. with cdparanoia]
        - Tighten access on some ioctls


    New features and changes:
        - per file descriptor (fd) write-read sequencing and command queues.
        - command queuing supported (SG_MAX_QUEUE is maximum per fd).
        - scatter-gather supported (allowing potentially megabyte transfers).
        - the SCSI target, host and driver status are returned
          in unused fields of sg_header (maintaining its original size).
        - asynchronous notification support added (SIGPOLL, SIGIO) for
          read()s ( write()s should never block).
        - pack_id logic added so read() can be made to wait for a specific
          pack_id. 
        - uses memory > ISA_DMA_THRESHOLD if adapter allows it (e.g. a
          pci scsi adapter).
        - this driver no longer uses a single SG_BIG_BUFF sized buffer
          obtained at driver/module init time. Rather it obtains a 
          SG_SCATTER_SZ buffer when a fd is open()ed and frees it at
          the corresponding release() (ie pr fd). Hence open() can return
          ENOMEM! If write() request > SG_SCATTER_SZ bytes for data then
          it can fail with ENOMEM as well (if so, scale back).
        - adds several ioctl calls, see ioctl section below.
        - SG_SCATTER_SZ's presence indicates this version of "sg" driver.
 
 Good documentation on the original "sg" device interface and usage can be
 found in the Linux HOWTO document: "SCSI Programming HOWTO" by Heiko
 Eissfeldt; last updated 7 May 1996. I will add more info on using the
 extensions in this driver as required. A quick summary:
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

 Memory (RAM) is used within this driver for direct memory access (DMA)
 in transferring data to and from the SCSI device. The dreaded ENOMEM
 seems to be more prevalent under early 2.2.x kernels than under the
 2.0.x kernel series. For a given (large) transfer the memory obtained by
 this driver must be contiguous or scatter-gather must be used (if
 supported by the adapter). [Furthermore, ISA SCSI adapters can only use
 memory below the 16MB level on a i386.]
 This driver tries hard to find some suitable memory before admitting
 defeat and returning ENOMEM. All is not lost if application writers
 then back off the amount they are requesting. The value returned by
 the SG_GET_RESERVED_SIZE ioctl is guaranteed to be available (one
 per fd). This driver does the following:
   -  attempts to reserve a SG_SCATTER_SZ sized buffer on open(). The
      actual amount reserved is given by the SG_GET_RESERVED_SIZE ioctl().
   -  each write() needs to reserve a DMA buffer of the size of the
      data buffer indicated (excluding sg_header and command overhead).
      This buffer, depending on its size, adapter type (ISA or not) and
      the amount of memory available will be obtained from the kernel
      directly (get_free_pages or kmalloc) or the from the scsi mid-level
      dma pool (taking care not to exhaust it).
      If the buffer requested is > SG_SCATTER_SZ or memory is tight then
      scatter-gather will be used if supported by the adapter.
  -   write() will also attempt to use the buffer reserved on open()
      if it is large enough.
 The above strategy ensures that a write() can always depend on a buffer 
 of the size indicated by the SG_GET_RESERVED_SIZE ioctl() (which could be
 0, but at least the app knows things are tight in advance).
 Hence application writers can adopt quite aggressive strategies (e.g. 
 requesting 512KB) and scale them back in the face of ENOMEM errors.
 N.B. Queuing up commands also ties up kernel memory.

 More documentation can be found at www.torque.net/sg
*/

#define SG_MAX_SENSE 16   /* too little, unlikely to change in 2.2.x */

struct sg_header
{
    int pack_len;    /* [o] reply_len (ie useless), ignored as input */
    int reply_len;   /* [i] max length of expected reply (inc. sg_header) */
    int pack_id;     /* [io] id number of packet (use ints >= 0) */
    int result;      /* [o] 0==ok, else (+ve) Unix errno code (e.g. EIO) */
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
    int unused1;        /* probably find a good use, set 0 for now */
    int unused2;        /* ditto */
    int unused3;  
} Sg_scsi_id;

/* ioctls  ( _GET_s yield result via 'int *' 3rd argument unless 
            otherwise indicated */
#define SG_SET_TIMEOUT 0x2201  /* unit: jiffies, 10ms on i386 */
#define SG_GET_TIMEOUT 0x2202  /* yield timeout as _return_ value */

#define SG_EMULATED_HOST 0x2203 /* true for emulated host adapter (ATAPI) */

/* Used to configure SCSI command transformation layer for ATAPI devices */
#define SG_SET_TRANSFORM 0x2204
#define SG_GET_TRANSFORM 0x2205

#define SG_SET_RESERVED_SIZE 0x2275  /* currently ignored, future addition */
/* Following yields buffer reserved by open(): 0 <= x <= SG_SCATTER_SZ */
#define SG_GET_RESERVED_SIZE 0x2272

/* The following ioctl takes a 'Sg_scsi_id *' object as its 3rd argument. */
#define SG_GET_SCSI_ID 0x2276   /* Yields fd's bus,chan,dev,lun+type */
/* SCSI id information can also be obtained from SCSI_IOCTL_GET_IDLUN */

/* Override adapter setting and always DMA using low memory ( <16MB on i386).
   Default is 0 (off - use adapter setting) */
#define SG_SET_FORCE_LOW_DMA 0x2279  /* 0-> use adapter setting, 1-> force */
#define SG_GET_LOW_DMA 0x227a   /* 0-> use all ram for dma; 1-> low dma ram */

/* When SG_SET_FORCE_PACK_ID set to 1, pack_id is input to read() which
   will attempt to read that pack_id or block (or return EAGAIN). If 
   pack_id is -1 then read oldest waiting. When ...FORCE_PACK_ID set to 0
   (default) then pack_id ignored by read() and oldest readable fetched. */ 
#define SG_SET_FORCE_PACK_ID 0x227b
#define SG_GET_PACK_ID 0x227c /* Yields oldest readable pack_id (or -1) */

#define SG_GET_NUM_WAITING 0x227d /* Number of commands awaiting read() */

/* Turn on error sense trace (1..8), dump this device to log/console (9)
   or dump all sg device states ( >9 ) to log/console */
#define SG_SET_DEBUG 0x227e    /* 0 -> turn off debug */

/* Yields max scatter gather tablesize allowed by current host adapter */
#define SG_GET_SG_TABLESIZE 0x227F  /* 0 implies can't do scatter gather */

/* Control whether sequencing per file descriptor (default) or per device */
#define SG_GET_MERGE_FD 0x2274   /* 0-> per fd (default), 1-> per device */
#define SG_SET_MERGE_FD 0x2273   /* Attempt to change sequencing state,
  if more than 1 fd open on device, will fail with EBUSY */

/* Get/set command queuing state per fd (default is SG_DEF_COMMAND_Q) */
#define SG_GET_COMMAND_Q 0x2270   /* Yields 0 (queuing off) or 1 (on) */
#define SG_SET_COMMAND_Q 0x2271   /* Change queuing state with 0 or 1 */

/* Get/set whether DMA underrun will cause an error (DID_ERROR) [this only
   currently applies to the [much-used] aic7xxx driver) */
#define SG_GET_UNDERRUN_FLAG 0x2280 /* Yields 0 (don't flag) or 1 (flag) */
#define SG_SET_UNDERRUN_FLAG 0x2281 /* Change flag underrun state */


#define SG_DEFAULT_TIMEOUT (60*HZ) /* HZ == 'jiffies in 1 second' */
#define SG_DEFAULT_RETRIES 1

/* Default modes, commented if they differ from original sg driver */
#define SG_DEF_COMMAND_Q 0
#define SG_DEF_MERGE_FD 0       /* was 1 -> per device sequencing */
#define SG_DEF_FORCE_LOW_DMA 0  /* was 1 -> memory below 16MB on i386 */
#define SG_DEF_FORCE_PACK_ID 0
#define SG_DEF_UNDERRUN_FLAG 0

/* maximum outstanding requests, write() yields EDOM if exceeded */
#define SG_MAX_QUEUE 16

#define SG_SCATTER_SZ (8 * 4096)  /* PAGE_SIZE not available to user */
/* Largest size (in bytes) a single scatter-gather list element can have.
   The value must be a power of 2 and <= (PAGE_SIZE * 32) [131072 bytes on 
   i386]. The minimum value is PAGE_SIZE. If scatter-gather not supported
   by adapter then this value is the largest data block that can be
   read/written by a single scsi command. Max number of scatter-gather
   list elements seems to be limited to 255. */

#define SG_BIG_BUFF SG_SCATTER_SZ       /* for backward compatibility */
/* #define SG_BIG_BUFF (SG_SCATTER_SZ * 8) */ /* =256KB, if you want */

#endif
