#ifndef _IDE_CD_H
#define _IDE_CD_H
/*
 *  linux/drivers/block/ide_modes.h
 *
 *  Copyright (C) 1996  Erik Andersen
 */

/* Turn this on to have the driver print out the meanings of the
   ATAPI error codes.  This will use up additional kernel-space
   memory, though. */

#ifndef VERBOSE_IDE_CD_ERRORS
#define VERBOSE_IDE_CD_ERRORS 0
#endif


/* Turn this on to have the driver print out the meanings of the
   ATAPI error codes.  This will use up additional kernel-space
   memory, though. */

#ifndef VERBOSE_IDE_CD_ERRORS
#define VERBOSE_IDE_CD_ERRORS 0
#endif


/* Turning this on will remove code to work around various nonstandard
   ATAPI implementations.  If you know your drive follows the standard,
   this will give you a slightly smaller kernel. */

#ifndef STANDARD_ATAPI
#define STANDARD_ATAPI 0
#endif


/* Turning this on will disable the door-locking functionality.
   This is apparently needed for supermount. */

#ifndef NO_DOOR_LOCKING
#define NO_DOOR_LOCKING 0
#endif


/* Size of buffer to allocate, in blocks, for audio reads. */

#ifndef CDROM_NBLOCKS_BUFFER
#define CDROM_NBLOCKS_BUFFER 8
#endif


/************************************************************************/

#define SECTOR_SIZE 512
#define SECTOR_BITS 9
#define SECTORS_PER_FRAME (CD_FRAMESIZE / SECTOR_SIZE)

#define MIN(a,b) ((a) < (b) ? (a) : (b))

/* special command codes for strategy routine. */
#define PACKET_COMMAND        4315
#define REQUEST_SENSE_COMMAND 4316
#define RESET_DRIVE_COMMAND   4317

/*
 * For controlling drive spindown time.
 */
#define CDROMGETSPINDOWN        0x531d
#define CDROMSETSPINDOWN        0x531e
 

/* Some ATAPI command opcodes (just like SCSI).
   (Some other cdrom-specific codes are in cdrom.h.) */
#define TEST_UNIT_READY         0x00
#define REQUEST_SENSE           0x03
#define START_STOP              0x1b
#define ALLOW_MEDIUM_REMOVAL    0x1e
#define READ_CAPACITY		0x25
#define READ_10                 0x28
#define SEEK			0x2b
#define MODE_SENSE_10           0x5a
#define MODE_SELECT_10          0x55
#define READ_CD                 0xbe

#define LOAD_UNLOAD             0xa6
#define MECHANISM_STATUS        0xbd


/* Page codes for mode sense/set */

#define PAGE_READERR            0x01
#define PAGE_CDROM              0x0d
#define PAGE_AUDIO              0x0e
#define PAGE_CAPABILITIES       0x2a
#define PAGE_ALL                0x3f


/* ATAPI sense keys (mostly copied from scsi.h). */

#define NO_SENSE                0x00
#define RECOVERED_ERROR         0x01
#define NOT_READY               0x02
#define MEDIUM_ERROR            0x03
#define HARDWARE_ERROR          0x04
#define ILLEGAL_REQUEST         0x05
#define UNIT_ATTENTION          0x06
#define DATA_PROTECT            0x07
#define ABORTED_COMMAND         0x0b
#define MISCOMPARE              0x0e

/* We want some additional flags for cd-rom drives.
   To save space in the ide_drive_t struct, use some fields which
   doesn't make sense for cd-roms -- `bios_cyl' and `bios_head'. */

/* Configuration flags.  These describe the capabilities of the drive.
   They generally do not change after initialization, unless we learn
   more about the drive from stuff failing. */
struct ide_cd_config_flags {
	__u8 drq_interrupt    : 1; /* Device sends an interrupt when ready
				      for a packet command. */
	__u8 no_doorlock      : 1; /* Drive cannot lock the door. */
	__u8 nec260           : 1; /* Drive is a pre-1.2 NEC 260 drive. */
	__u8 playmsf_as_bcd   : 1; /* PLAYMSF command takes BCD args. */
	__u8 tocaddr_as_bcd   : 1; /* TOC addresses are in BCD. */
	__u8 toctracks_as_bcd : 1; /* TOC track numbers are in BCD. */
	__u8 subchan_as_bcd   : 1; /* Subchannel info is in BCD. */
	__u8 is_changer       : 1; /* Drive is a changer. */
	__u8 supp_disc_present: 1; /* Changer can report exact contents
				      of slots. */
	__u8 seeking          : 1; /* Seeking in progress */
	__u8 reserved         : 6;
};
#define CDROM_CONFIG_FLAGS(drive) ((struct ide_cd_config_flags *)&((drive)->bios_cyl))

 
/* State flags.  These give information about the current state of the
   drive, and will change during normal operation. */
struct ide_cd_state_flags {
	__u8 media_changed : 1; /* Driver has noticed a media change. */
	__u8 toc_valid     : 1; /* Saved TOC information is current. */
	__u8 door_locked   : 1; /* We think that the drive door is locked. */
	__u8 sanyo_slot    : 2; /* Sanyo 3 CD changer support */
	__u8 reserved      : 3;
};
#define CDROM_STATE_FLAGS(drive)  ((struct ide_cd_state_flags *)&((drive)->bios_head))


struct atapi_request_sense {
	unsigned char error_code : 7;
	unsigned char valid      : 1;
	byte reserved1;
	unsigned char sense_key  : 4;
	unsigned char reserved2  : 1;
	unsigned char ili        : 1;
	unsigned char reserved3  : 2;
	byte info[4];
	byte sense_len;
	byte command_info[4];
	byte asc;
	byte ascq;
	byte fru;
	byte sense_key_specific[3];
};

struct packet_command {
	char *buffer;
	int buflen;
	int stat;
	struct atapi_request_sense *sense_data;
	unsigned char c[12];
};

/* Structure of a MSF cdrom address. */
struct atapi_msf {
	byte reserved;
	byte minute;
	byte second;
	byte frame;
};

/* Space to hold the disk TOC. */
#define MAX_TRACKS 99
struct atapi_toc_header {
	unsigned short toc_length;
	byte first_track;
	byte last_track;
};

struct atapi_toc_entry {
	byte reserved1;
	unsigned control : 4;
	unsigned adr     : 4;
	byte track;
	byte reserved2;
	union {
		unsigned lba;
		struct atapi_msf msf;
	} addr;
};

struct atapi_toc {
	int    last_session_lba;
	int    xa_flag;
	unsigned capacity;
	struct atapi_toc_header hdr;
	struct atapi_toc_entry  ent[MAX_TRACKS+1];
	  /* One extra for the leadout. */
};


/* This structure is annoyingly close to, but not identical with,
   the cdrom_subchnl structure from cdrom.h. */
struct atapi_cdrom_subchnl {
 	u_char  acdsc_reserved;
 	u_char  acdsc_audiostatus;
 	u_short acdsc_length;
	u_char  acdsc_format;

	u_char  acdsc_adr:	4;
	u_char  acdsc_ctrl:	4;
	u_char  acdsc_trk;
	u_char  acdsc_ind;
	union {
		struct atapi_msf msf;
		int	lba;
	} acdsc_absaddr;
	union {
		struct atapi_msf msf;
		int	lba;
	} acdsc_reladdr;
};


typedef enum {
	mechtype_caddy = 0,
	mechtype_tray  = 1,
	mechtype_popup = 2,
	mechtype_individual_changer = 4,
	mechtype_cartridge_changer  = 5
} mechtype_t;


struct atapi_capabilities_page {
	unsigned page_code           : 6;
	unsigned reserved1           : 1;
	unsigned parameters_saveable : 1;

	byte     page_length;

	/* Drive supports read from CD-R discs (orange book, part II) */
        unsigned cd_r_read           : 1; /* reserved in 1.2 */
	/* Drive supports read from CD-E discs (orange book, part III) */
        unsigned cd_e_read           : 1; /* reserved in 1.2 */
	/* Drive supports reading CD-R discs with addressing method 2 */
	unsigned method2             : 1; /* reserved in 1.2 */
	unsigned reserved2           : 5;

	/* Drive supports write to CD-R discs (orange book, part II) */
        unsigned cd_r_write          : 1; /* reserved in 1.2 */
	/* Drive supports write to CD-E discs (orange book, part III) */
        unsigned cd_e_write          : 1; /* reserved in 1.2 */
	unsigned reserved3           : 6;

	/* Drive supports audio play operations. */
	unsigned audio_play          : 1;
	/* Drive can deliver a composite audio/video data stream. */
	unsigned composite           : 1;
	/* Drive supports digital output on port 1. */
	unsigned digport1            : 1;
	/* Drive supports digital output on port 2. */
	unsigned digport2            : 1;
	/* Drive can read mode 2, form 1 (XA) data. */
	unsigned mode2_form1         : 1;
	/* Drive can read mode 2, form 2 data. */
	unsigned mode2_form2         : 1;
	/* Drive can read multisession discs. */
	unsigned multisession        : 1;
	unsigned reserved4           : 1;

	/* Drive can read Red Book audio data. */
	unsigned cdda                : 1;
	/* Drive can continue a read cdda operation from a loss of streaming.*/
	unsigned cdda_accurate       : 1;
	/* Subchannel reads can return combined R-W information. */
	unsigned rw_supported        : 1;
	/* R-W data will be returned deinterleaved and error corrected. */
	unsigned rw_corr             : 1;
	/* Drive supports C2 error pointers. */
	unsigned c2_pointers         : 1;
	/* Drive can return International Standard Recording Code info. */
	unsigned isrc                : 1;
	/* Drive can return Media Catalog Number (UPC) info. */
	unsigned upc                 : 1;
	unsigned reserved5           : 1;

	/* Drive can lock the door. */
	unsigned lock                : 1;
	/* Present state of door lock. */
	unsigned lock_state          : 1;
	/* State of prevent/allow jumper. */
	unsigned prevent_jumper      : 1;
	/* Drive can eject a disc or changer cartridge. */
	unsigned eject               : 1;
	unsigned reserved6           : 1;
	/* Drive mechanism types. */
	mechtype_t mechtype          : 3;

	/* Audio level for each channel can be controlled independently. */
	unsigned separate_volume     : 1;
	/* Audio for each channel can be muted independently. */
	unsigned separate_mute       : 1;
	/* Changer can report exact contents of slots. */
	unsigned disc_present        : 1;  /* reserved in 1.2 */
	/* Drive supports software slot selection. */
	unsigned sss                 : 1;  /* reserved in 1.2 */
	unsigned reserved7           : 4;

	/* Note: the following four fields are returned in big-endian form. */
	/* Maximum speed (in kB/s). */
	unsigned short maxspeed;
	/* Number of discrete volume levels. */
	unsigned short n_vol_levels;
	/* Size of cache in drive, in kB. */
	unsigned short buffer_size;
	/* Current speed (in kB/s). */
	unsigned short curspeed;

	/* Truncate the structure here, so we don't have headaches reading
	   from older drives. */
};


struct atapi_mechstat_header {
	unsigned curslot       : 5;
	unsigned changer_state : 2;
	unsigned fault         : 1;

	unsigned reserved1     : 5;
	unsigned mech_state    : 3;

	byte     curlba[3];
	byte     nslots;
	unsigned short slot_tablelen;
};


struct atapi_slot {
	unsigned change       : 1;
	unsigned reserved1    : 6;
	unsigned disc_present : 1;

	byte reserved2[3];
};


struct atapi_changer_info {
	struct atapi_mechstat_header hdr;
	struct atapi_slot slots[0];
};


/* Extra per-device info for cdrom drives. */
struct cdrom_info {

	/* Buffer for table of contents.  NULL if we haven't allocated
	   a TOC buffer for this device yet. */

	struct atapi_toc *toc;

	/* Sector buffer.  If a read request wants only the first part
	   of a cdrom block, we cache the rest of the block here,
	   in the expectation that that data is going to be wanted soon.
	   SECTOR_BUFFERED is the number of the first buffered sector,
	   and NSECTORS_BUFFERED is the number of sectors in the buffer.
	   Before the buffer is allocated, we should have
	   SECTOR_BUFFER == NULL and NSECTORS_BUFFERED == 0. */

	unsigned long sector_buffered;
	unsigned long nsectors_buffered;
	char *sector_buffer;

	/* The result of the last successful request sense command
	   on this device. */
	struct atapi_request_sense sense_data;

	struct request request_sense_request;
	struct packet_command request_sense_pc;
	int dma;
	unsigned long last_block;
	unsigned long start_seek;
	/* Buffer to hold mechanism status and changer slot table. */
	struct atapi_changer_info *changer_info;


        /* Per-device info needed by cdrom.c generic driver. */
        struct cdrom_device_info devinfo;
};


#define SECTOR_BUFFER_SIZE CD_FRAMESIZE


/****************************************************************************
 * Descriptions of ATAPI error codes.
 */

#define ARY_LEN(a) ((sizeof(a) / sizeof(a[0])))

#if VERBOSE_IDE_CD_ERRORS

/* From Table 124 of the ATAPI 1.2 spec.
   Unchanged in Table 140 of the ATAPI 2.6 draft standard. */

char *sense_key_texts[16] = {
	"No sense data",
	"Recovered error",
	"Not ready",
	"Medium error",
	"Hardware error",
	"Illegal request",
	"Unit attention",
	"Data protect",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"Aborted command",
	"(reserved)",
	"(reserved)",
	"Miscompare",
	"(reserved)",
};


/* From Table 125 of the ATAPI 1.2 spec.,
   with additions from Tables 141 and 142 of the ATAPI 2.6 draft standard. */

struct {
	short asc_ascq;
	char *text;
} sense_data_texts[] = {
	{ 0x0000, "No additional sense information" },

	{ 0x0011, "Audio play operation in progress" },
	{ 0x0012, "Audio play operation paused" },
	{ 0x0013, "Audio play operation successfully completed" },
	{ 0x0014, "Audio play operation stopped due to error" },
	{ 0x0015, "No current audio status to return" },

	{ 0x0100, "Mechanical positioning or changer error" },

	{ 0x0200, "No seek complete" },

	{ 0x0400, "Logical unit not ready - cause not reportable" },
	{ 0x0401,
	  "Logical unit not ready - in progress (sic) of becoming ready" },
	{ 0x0402, "Logical unit not ready - initializing command required" },
	{ 0x0403, "Logical unit not ready - manual intervention required" },

	{ 0x0501, "Media load - eject failed" },

	{ 0x0600, "No reference position found" },

	{ 0x0900, "Track following error" },
	{ 0x0901, "Tracking servo failure" },
	{ 0x0902, "Focus servo failure" },
	{ 0x0903, "Spindle servo failure" },

	{ 0x1100, "Unrecovered read error" },
	{ 0x1106, "CIRC unrecovered error" },

	{ 0x1500, "Random positioning error" },
	{ 0x1501, "Mechanical positioning or changer error" },
	{ 0x1502, "Positioning error detected by read of medium" },

	{ 0x1700, "Recovered data with no error correction applied" },
	{ 0x1701, "Recovered data with retries" },
	{ 0x1702, "Recovered data with positive head offset" },
	{ 0x1703, "Recovered data with negative head offset" },
	{ 0x1704, "Recovered data with retries and/or CIRC applied" },
	{ 0x1705, "Recovered data using previous sector ID" },

	{ 0x1800, "Recovered data with error correction applied" },
	{ 0x1801, "Recovered data with error correction and retries applied" },
	{ 0x1802, "Recovered data - the data was auto-reallocated" },
	{ 0x1803, "Recovered data with CIRC" },
	{ 0x1804, "Recovered data with L-EC" },
	/* Following two not in 2.6. */
	{ 0x1805, "Recovered data - recommend reassignment" },
	{ 0x1806, "Recovered data - recommend rewrite" },

	{ 0x1a00, "Parameter list length error" },

	{ 0x2000, "Invalid command operation code" },

	{ 0x2100, "Logical block address out of range" },

	{ 0x2400, "Invalid field in command packet" },

	{ 0x2600, "Invalid field in parameter list" },
	{ 0x2601, "Parameter not supported" },
	{ 0x2602, "Parameter value invalid" },
	/* Following code not in 2.6. */
	{ 0x2603, "Threshold parameters not supported" },

	{ 0x2800, "Not ready to ready transition, medium may have changed" },

	{ 0x2900, "Power on, reset or bus device reset occurred" },

	{ 0x2a00, "Parameters changed" },
	{ 0x2a01, "Mode parameters changed" },

	{ 0x3000, "Incompatible medium installed" },
	{ 0x3001, "Cannot read medium - unknown format" },
	{ 0x3002, "Cannot read medium - incompatible format" },

	/* Following code not in 2.6. */
	{ 0x3700, "Rounded parameter" },

	{ 0x3900, "Saving parameters not supported" },

	{ 0x3a00, "Medium not present" },

	{ 0x3f00, "ATAPI CD-ROM drive operating conditions have changed" },
	{ 0x3f01, "Microcode has been changed" },
	/* Following two not in 2.6. */
	{ 0x3f02, "Changed operating definition" },
	{ 0x3f03, "Inquiry data has changed" },

	{ 0x4000, "Diagnostic failure on component (ASCQ)" },

	{ 0x4400, "Internal ATAPI CD-ROM drive failure" },

	{ 0x4e00, "Overlapped commands attempted" },

	{ 0x5300, "Media load or eject failed" },
	{ 0x5302, "Medium removal prevented" },

	{ 0x5700, "Unable to recover table of contents" },

	{ 0x5a00, "Operator request or state change input (unspecified)" },
	{ 0x5a01, "Operator medium removal request" },

	/* Following two not in 2.6. */
	{ 0x5b00, "Threshold condition met" },
	{ 0x5c00, "Status change" },

	{ 0x6300, "End of user area encountered on this track" },

	{ 0x6400, "Illegal mode for this track" },

	{ 0xb900, "Play operation oborted (sic)" },

	{ 0xbf00, "Loss of streaming" },
};
#endif


#endif /* _IDE_CD_H */
