#ifndef _LINUX_FD_H
#define _LINUX_FD_H

#define FDCLRPRM 0 /* clear user-defined parameters */
#define FDSETPRM 1 /* set user-defined parameters for current media */
#define FDSETMEDIAPRM 1
#define FDDEFPRM 2 /* set user-defined parameters until explicitly cleared */
#define FDDEFMEDIAPRM 2
#define FDGETPRM 3 /* get disk parameters */
#define FDGETMEDIAPRM 3
#define	FDMSGON  4 /* issue kernel messages on media type change */
#define	FDMSGOFF 5 /* don't issue kernel messages on media type change */
#define FDFMTBEG 6 /* begin formatting a disk */
#define	FDFMTTRK 7 /* format the specified track */
#define FDFMTEND 8 /* end formatting a disk */
#define FDSETEMSGTRESH	10	/* set fdc error reporting threshold */
#define FDFLUSH  11 /* flush buffers for media; either for verifying media, or for
                       handling a media change without closing the file
		       descriptor */
#define FDSETMAXERRS 12 /* set abortion and read_track threshold */
#define FDGETMAXERRS 14 /* get abortion and read_track threshold */
#define FDGETDRVTYP 16          /* get drive type: 5 1/4 or 3 1/2 */

#define FDSETDRVPRM 20 /* set drive parameters */
#define FDGETDRVPRM 21 /* get drive parameters */
#define FDGETDRVSTAT 22 /* get drive state */
#define FDPOLLDRVSTAT 23 /* get drive state */
#define FDRESET 24 /* reset FDC */

#define FD_RESET_IF_NEEDED 0
#define FD_RESET_IF_RAWCMD 1
#define FD_RESET_ALWAYS 2

#define FDGETFDCSTAT 25 /* get fdc state */
#define FDWERRORCLR  27 /* clear write error and badness information */
#define FDWERRORGET  28 /* get write error and badness information */

#define FDRAWCMD 30 /* send a raw command to the fdc */

#define FDTWADDLE 40 /* flicker motor-on bit before reading a sector */

/*
 * Maximum number of sectors in a track buffer. Track buffering is disabled
 * if tracks are bigger.
 */
#define MAX_BUFFER_SECTORS 24 /* was 18 -bb */

#define FD_FILL_BYTE 0xF6 /* format fill byte */

#define FD_2M 0x4
#define FD_SIZECODEMASK 0x38
#define FD_SIZECODE(floppy) (((((floppy)->rate&FD_SIZECODEMASK)>> 3)+ 2) %8)
#define FD_SECTSIZE(floppy) ( (floppy)->rate & FD_2M ? \
			     512 : 128 << FD_SIZECODE(floppy) )
#define FD_PERP 0x40

#ifndef __ASSEMBLY__
/* the following structure is used by FDSETPRM, FDDEFPRM and FDGETPRM */
struct floppy_struct {
	unsigned int	size,		/* nr of sectors total */
			sect,		/* sectors per track */
			head,		/* nr of heads */
			track,		/* nr of tracks */
			stretch;	/* !=0 means double track steps */
	unsigned char	gap,		/* gap1 size */
			rate,		/* data rate. |= 0x40 for perpendicular */
			spec1,		/* stepping rate, head unload time */
			fmt_gap;	/* gap2 size */
	char 	      * name; /* used only for predefined formats */
};

struct format_descr {
	unsigned int device,head,track;
};

struct floppy_max_errors {
  unsigned int
	  abort,      /* number of errors to be reached before aborting */
	  read_track, /* maximal number of errors permitted to read an
		       * entire track at once */
	  reset,      /* maximal number of errors before a reset is tried */
	  recal,      /* maximal number of errors before a recalibrate is
		       * tried */

	  /*
	   * Threshold for reporting FDC errors to the console.
	   * Setting this to zero may flood your screen when using
	   * ultra cheap floppies ;-)
	   */
	  reporting;

};

/* the following structure is used by FDSETDRVPRM and FDGETDRVPRM */
struct floppy_drive_params {
  char cmos;			/* cmos type */

  /* Spec2 is (HLD<<1 | ND), where HLD is head load time (1=2ms, 2=4 ms etc)
   * and ND is set means no DMA. Hardcoded to 6 (HLD=6ms, use DMA).
   */
  unsigned long max_dtr;	/* Step rate, usec */
  unsigned long hlt;		/* Head load/settle time, msec */
  unsigned long hut;		/* Head unload time (remnant of 8" drives) */
  unsigned long srt;		/* Step rate, usec */

  unsigned long spinup;		/* time needed for spinup ( in jiffies) */
  unsigned long spindown;	/* timeout needed for spindown */
  unsigned char spindown_offset;/* decides in which position the disk
				 * will stop */
  unsigned char select_delay;	/* delay to wait after select */
  unsigned char rps;	       	/* rotations per second */
  unsigned char tracks;	       	/* maximum number of tracks */
  unsigned long timeout;	/* timeout for interrupt requests */

  unsigned char interleave_sect;/* if there are more sectors, use interleave */

  struct floppy_max_errors max_errors;

  char flags;			/* various flags, including ftd_msg */
/*
 * Announce successful media type detection and media information loss after
 * disk changes.
 * Also used to enable/disable printing of overrun warnings.
 */

#define FTD_MSG 0x10
#define FD_BROKEN_DCL 0x20
#define FD_DEBUG 0x02
#define FD_SILENT_DCL_CLEAR 0x4
#define FD_INVERTED_DCL 0x80

  char read_track;		/* use readtrack during probing? */

/*
 * Auto-detection. Each drive type has eight formats which are
 * used in succession to try to read the disk. If the FDC cannot lock onto
 * the disk, the next format is tried. This uses the variable 'probing'.
 */
  short autodetect[8];		/* autodetected formats */

  int checkfreq; /* how often should the drive be checked for disk changes */
  int native_format; /* native format of this drive */
};

enum {
FD_NEED_TWADDLE_BIT, /* more magic */
FD_VERIFY_BIT, /* inquire for write protection */
FD_DISK_NEWCHANGE_BIT, /* change detected, and no action undertaken yet to
			  clear media change status */
FD_UNUSED_BIT,
FD_DISK_CHANGED_BIT, /* disk has been changed since last i/o */
FD_DISK_WRITABLE_BIT /* disk is writable */
};

/* values for these flags */
#define FD_NEED_TWADDLE (1 << FD_NEED_TWADDLE_BIT)
#define FD_VERIFY (1 << FD_VERIFY_BIT)
#define FD_DISK_NEWCHANGE (1 << FD_DISK_NEWCHANGE_BIT)
#define FD_DISK_CHANGED (1 << FD_DISK_CHANGED_BIT)
#define FD_DISK_WRITABLE (1 << FD_DISK_WRITABLE_BIT)

#define FD_DRIVE_PRESENT 0 /* keep fdpatch utils compiling */

struct floppy_drive_struct {
  signed char flags;
  unsigned long spinup_date;
  unsigned long select_date;
  unsigned long first_read_date;
  short probed_format;
  short track; /* current track */
  short maxblock; /* id of highest block read */
  short maxtrack; /* id of highest half track read */
  int generation; /* how many diskchanges? */

/*
 * (User-provided) media information is _not_ discarded after a media change
 * if the corresponding keep_data flag is non-zero. Positive values are
 * decremented after each probe.
 */
  int keep_data;

  /* Prevent "aliased" accesses. */
  int fd_ref;
  int fd_device;
  int last_checked; /* when was the drive last checked for a disk change? */


};

struct floppy_write_errors {
  /* Write error logging.
   *
   * These fields can be cleared with the FDWERRORCLR ioctl.
   * Only writes that were attempted but failed due to a physical media
   * error are logged.  write(2) calls that fail and return an error code
   * to the user process are not counted.
   */

  unsigned int write_errors;  /* number of physical write errors encountered */

  /* position of first and last write errors */
  unsigned long first_error_sector;
  int           first_error_generation;
  unsigned long last_error_sector;
  int           last_error_generation;

  unsigned int badness; /* highest retry count for a read or write operation */
};

struct floppy_fdc_state {	
	int spec1; /* spec1 value last used */
	int spec2; /* spec2 value last used */
	int dtr;
	unsigned char version;	/* FDC version code */
	unsigned char dor;
	int address; /* io address */
	unsigned int rawcmd:2;
	unsigned int reset:1;
	unsigned int need_configure:1;
	unsigned int perp_mode:2;
	unsigned int has_fifo:1;
};

struct floppy_raw_cmd {
  void *data;
  long length;

  unsigned char rate;
  unsigned char flags;
  unsigned char cmd_count;
  unsigned char cmd[9];
  unsigned char reply_count;
  unsigned char reply[7];
  int track;
};
#endif

/* meaning of the various bytes */

/* flags */
#define FD_RAW_READ 1
#define FD_RAW_WRITE 2
#define FD_RAW_NO_MOTOR 4
#define FD_RAW_DISK_CHANGE 4
#define FD_RAW_INTR 8
#define FD_RAW_SPIN 16
#define FD_RAW_NO_MOTOR_AFTER 32
#define FD_RAW_NEED_DISK 64
#define FD_RAW_NEED_SEEK 128

#endif
