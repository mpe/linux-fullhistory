/*
   The compile-time configurable defaults for the Linux SCSI tape driver.

   Copyright 1995 Kai Makisara.

   Last modified: Thu Dec 14 21:51:27 1995 by root@kai.makisara.fi
*/

#ifndef _ST_OPTIONS_H
#define _ST_OPTIONS_H

/* The driver allocates the tape buffers when needed if ST_RUNTIME_BUFFERS
   is nonzero. Otherwise a number of buffers are allocated at initialization.
   The drawback of runtime allocation is that allocation may fail. In any
   case the driver tries to allocate a new tape buffer when none is free. */
#define ST_RUNTIME_BUFFERS 0

/* The minimum limit for the number of SCSI tape devices is determined by
   ST_MAX_TAPES. If the number of tape devices and the "slack" defined by
   ST_EXTRA_DEVS exceeds ST_MAX_TAPES, the large number is used. */
#define ST_MAX_TAPES 4

/* The driver does not wait for some operations to finish before returning
   to the user program if ST_NOWAIT is non-zero. This helps if the SCSI
   adapter does not support multiple outstanding commands. However, the user
   should not give a new tape command before the previous one has finished. */
#define ST_NOWAIT 0

/* If ST_IN_FILE_POS is nonzero, the driver positions the tape after the
   record been read by the user program even if the tape has moved further
   because of buffered reads. Should be set to zero to support also drives
   that can't space backwards over records. NOTE: The tape will be
   spaced backwards over an "accidentally" crossed filemark in any case. */
#define ST_IN_FILE_POS 0

/* If ST_RECOVERED_WRITE_FATAL is non-zero, recovered errors while writing
   are considered "hard errors". */
#define ST_RECOVERED_WRITE_FATAL 0

/* The "guess" for the block size for devices that don't support MODE
   SENSE. */
#define ST_DEFAULT_BLOCK 0

/* The tape driver buffer size in kilobytes. */
#define ST_BUFFER_BLOCKS 32

/* The number of kilobytes of data in the buffer that triggers an
   asynchronous write in fixed block mode. See also ST_ASYNC_WRITES
   below. */
#define ST_WRITE_THRESHOLD_BLOCKS 30

/* The maximum number of tape buffers the driver allocates. The number
   is also constrained by the number of drives detected. Determines the
   maximum number of concurrently active tape drives. */
#define ST_MAX_BUFFERS (2 + ST_EXTRA_DEVS)


/* The following lines define defaults for properties that can be set
   separately for each drive using the MTSTOPTIONS ioctl. */

/* If ST_TWO_FM is non-zero, the driver writes two filemarks after a
   file being written. Some drives can't handle two filemarks at the
   end of data. */
#define ST_TWO_FM 0

/* If ST_BUFFER_WRITES is non-zero, writes in fixed block mode are
   buffered until the driver buffer is full or asynchronous write is
   triggered. May make detection of End-Of-Medium early enough fail. */
#define ST_BUFFER_WRITES 1

/* If ST_ASYNC_WRITES is non-zero, the SCSI write command may be started
   without waiting for it to finish. May cause problems in multiple
   tape backups. */
#define ST_ASYNC_WRITES 1

/* If ST_READ_AHEAD is non-zero, blocks are read ahead in fixed block
   mode. */
#define ST_READ_AHEAD 1

/* If ST_AUTO_LOCK is non-zero, the drive door is locked at the first
   read or write command after the device is opened. The door is opened
   when the device is closed. */
#define ST_AUTO_LOCK 0

/* If ST_FAST_MTEOM is non-zero, the MTEOM ioctl is done using the
   direct SCSI command. The file number status is lost but this method
   is fast with some drives. Otherwise MTEOM is done by spacing over
   files and the file number status is retained. */
#define ST_FAST_MTEOM 0

#endif
