This file contains brief information about the SCSI tape driver.
Last modified: Tue Jan 10 21:32:35 1995 by root@kai.home

BASICS

The driver is generic. The state of a drive is not modified when the
driver is initialized or a device is opened. The mode parameters of the
drive can be modified with ioctls. The driver supports fixed and
variable block size (within buffer limits). Both the auto-rewind
(minor equals device number) and non-rewind devices (minor is 128 +
device number) are implemented.

By default the driver writes one filemark when the device is closed after
writing and the last operation has been a write. Two filemarks can be
optionally written. In both cases end of data is signified by
returning zero bytes for two consecutive reads.

BUFFERING

The driver uses a buffer allocated at system initialization. The size
of the buffer is selectable at compile and/or boot time. The buffer is
used to store the data being transferred to/from the SCSI adapter. The
following buffering options are selectable at compile time and/or at run
time (via ioctl):

Buffering of data to be written across write calls for fixed block
mode (define ST_BUFFER_WRITES). This should be disabled if reliable
detection of end of media (EOM) for fixed block mode is desired.

Asynchronous writing. Writing the buffer contents to the tape is
started and the write call returns immediately. The status is checked
at the next tape operation. Should not used if reliable EOM detection
is desired.

Read ahead for fixed block mode (ST_READ_AHEAD). Filling the buffer is
attempted even if the user does not want to get all of the data at
this read command. Should be disabled for those drives that don't like
a filemark to truncate a read request or that don't like backspacing.

The buffer size is defined (in 1024 byte units) by ST_BUFFER_BLOCKS or
at boot time. The maximum number of buffers allocated is defined by
ST_MAX_BUFFERS. One buffer is allocated for each detected drive up to
the maximum. The threshold for triggering asynchronous write is
defined by ST_WRITE_THRESHOLD.


BOOT TIME CONFIGURATION

The buffer size, write threshold, and the maximum number of allocated buffers
are configurable at boot time using, e.g., the LILO command line. The option
syntax is the following:

           st=aa[,bb[,cc]]

where
  aa is the buffer size in 1024 byte units
  bb is the write threshold in 1024 byte units
  cc is the maximum number of tape buffers to allocate (the number of
        buffers is bounded also by the number of drives detected)


IOCTLS

The tape is positioned and the drive parameters are set with ioctls
defined in mtio.h The tape control program 'mt' uses these ioctls. Try
to find an mt that supports all of the Linux SCSI tape ioctls and
opens the device for writing if the tape contents will be modified
(look for a package mt-st* from the Linux ftp sites; the GNU mt does
not open for writing for, e.g., erase).

The supported ioctls are:

The following use the structure mtop:

MTFSF   Space forward over count filemarks. Tape positioned after filemark.
MTFSFM  As above but tape positioned before filemark.
MTBSF	Space backward over count filemarks. Tape positioned before
        filemark.
MTBSFM  As above but ape positioned after filemark.
MTFSR   Space forward over count records.
MTBSR   Space backward over count records.
MTFSS   Space forward over count setmarks.
MTBSS   Space backward over count setmarks.
MTWEOF  Write count filemarks.
MTWSM   Write count setmarks.
MTREW   Rewind tape.
MTOFFL  Set device off line (often rewind plus eject).
MTNOP   Do nothing except flush the buffers.
MTRETEN Retension tape.
MTEOM   Space to end of recorded data.
MTERASE Erase tape.
MTSEEK	Seek to tape block count. Uses Tandberg-compatible seek (QFA)
        for SCSI-1 drives and SCSI-2 seek for SCSI-2 drives. The file and
	block numbers in the status are not valid after a seek.
MTSETBLK Set the drive block size. Setting to zero sets the drive into
        variable block mode (if applicable).
MTSETDENSITY Sets the drive density code to arg. See drive
        documentation for available codes.
MTSETDRVBUFFER
	Is used for several things. The command is obtained from count
        with mask MT_SET_OPTIONS, the low order bits are used as argument.
	The subcommands are:
	0
           The drive buffer option is set to the argument. Zero means
           no buffering.
        MT_ST_BOOLEANS
           Sets the buffering options. The bits are the new states
           (enabled/disabled) of the write buffering (MT_ST_BUFFER_WRITES),
           asynchronous writes (MT_ST_ASYNC_WRITES), read ahead
           (MT_ST_READ_AHEAD), writing of two filemark (ST_TWO_FM),
	   using the SCSI spacing to EOD (MT_ST_FAST_EOM), and
	   debugging (MT_ST_DEBUGGING ; debugging must be compiled into the
	   driver).
        MT_ST_WRITE_THRESHOLD
           Sets the write threshold for this device to kilobytes
           specified by the lowest bits.

The following ioctl uses the structure mtpos:
MTIOCPOS Reads the current position from the drive. Uses
        Tandberg-compatible QFA for SCSI-1 drives and the SCSI-2
        command for the SCSI-2 drives.

The following ioctl uses the structure mtget to return the status:
MTIOCGET Returns some status information.
        The file number and block number within file are returned. The
        block is -1 when it can't be determined (e.g., after MTBSF).
        The drive type is either MTISSCSI1 or MTISSCSI2.
        The number of recovered errors since the previous status call
        is stored in the lower word of the field mt_erreg.
        The current block size and the density code are stored in the field
        mt_dsreg (shifts for the subfields are MT_ST_BLKSIZE_SHIFT and
        MT_ST_DENSITY_SHIFT).
	The GMT_xxx status bits reflect the drive status. GMT_DR_OPEN
	is set if there is no tape in the drive. GMT_EOD means either
	end of recorded data or end of tape. GMT_EOT means end of tape.


MISCELLANEOUS COMPILE OPTIONS

The recovered write errors are considered fatal if ST_RECOVERED_WRITE_FATAL
is defined.

Immediate return from tape positioning SCSI commands can be enabled by
defining ST_NOWAIT.

The MTEOM command is by default implemented as spacing over 32767
filemarks. With this method the file number in the status is
correct. The user can request using direct spacing to EOD by setting
ST_FAST_EOM 1 (or using the MT_ST_OPTIONS ioctl). In this case the file
number will be invalid.

When using read ahead or buffered writes the position within the file
may not be correct after the file is closed (correct position may
require backspacing over more than one record). The correct position
within file can be obtained if ST_IN_FILE_POS is defined. (The
driver always backs over a filemark crossed by read ahead if the user
does not request data that far.)

Kai M{kisara
