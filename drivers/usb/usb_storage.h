/* Driver for USB mass storage - include file
 *
 * (c) 1999 Michael Gee (michael@linuxspecific.com)
 * (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 */

#include <linux/config.h>

#define USB_STORAGE "usb-storage: "

extern int usb_stor_debug;

#ifdef CONFIG_USB_STORAGE_DEBUG
void us_show_command(Scsi_Cmnd *srb);
#define US_DEBUGP(x...) { if(usb_stor_debug) printk( KERN_DEBUG USB_STORAGE ## x ); }
#define US_DEBUGPX(x...) { if(usb_stor_debug) printk( ## x ); }
#define US_DEBUG(x)  { if(usb_stor_debug) x; }
#else
#define US_DEBUGP(x...)
#define US_DEBUGPX(x...)
#define US_DEBUG(x)
#endif

/* bit set if input */
extern unsigned char us_direction[256/8];
#define US_DIRECTION(x) ((us_direction[x>>3] >> (x & 7)) & 1)

/* Sub Classes */

#define US_SC_RBC	1		/* Typically, flash devices */
#define US_SC_8020	2		/* CD-ROM */
#define US_SC_QIC	3		/* QIC-157 Tapes */
#define US_SC_UFI	4		/* Floppy */
#define US_SC_8070	5		/* Removable media */
#define US_SC_SCSI	6		/* Transparent */
#define US_SC_MIN	US_SC_RBC
#define US_SC_MAX	US_SC_SCSI

/* Protocols */

#define US_PR_CB	1		/* Control/Bulk w/o interrupt */
#define US_PR_CBI	0		/* Control/Bulk/Interrupt */
#define US_PR_BULK	0x50		/* bulk only */

/*
 * Bulk only data structures (Zip 100, for example)
 */

/* command block wrapper */
struct bulk_cb_wrap {
	__u32	Signature;		/* contains 'USBC' */
	__u32	Tag;			/* unique per command id */
	__u32	DataTransferLength;	/* size of data */
	__u8	Flags;			/* direction in bit 0 */
	__u8	Lun;			/* LUN normally 0 */
	__u8	Length;			/* of of the CDB */
	__u8	CDB[16];		/* max command */
};

#define US_BULK_CB_WRAP_LEN 	31
#define US_BULK_CB_SIGN		0x43425355
#define US_BULK_FLAG_IN		1
#define US_BULK_FLAG_OUT	0

/* command status wrapper */
struct bulk_cs_wrap {
	__u32	Signature;		/* should = 'USBS' */
	__u32	Tag;			/* same as original command */
	__u32	Residue;		/* amount not transferred */
	__u8	Status;			/* see below */
	__u8	Filler[18];
};

#define US_BULK_CS_WRAP_LEN	13
#define US_BULK_CS_SIGN		0x53425355
#define US_BULK_STAT_OK		0
#define US_BULK_STAT_FAIL	1
#define US_BULK_STAT_PHASE	2

#define US_BULK_RESET		0xff
#define US_BULK_RESET_SOFT	1
#define US_BULK_RESET_HARD	0

/*
 * Transport return codes
 */

#define USB_STOR_TRANSPORT_GOOD    0    /* Transport good, command good    */
#define USB_STOR_TRANSPORT_FAILED  1    /* Transport good, command failed  */
#define USB_STOR_TRANSPORT_ERROR   2    /* Transport bad (i.e. device dead */

/*
 * CBI style
 */

#define US_CBI_ADSC		0

/* 
 * GUID definitions
 */

#define GUID(x) __u32 x[3]
#define GUID_EQUAL(x, y) (x[0] == y[0] && x[1] == y[1] && x[2] == y[2])
#define GUID_CLEAR(x) x[0] = x[1] = x[2] = 0;
#define GUID_NONE(x) (!x[0] && !x[1] && !x[2])
#define GUID_FORMAT "%08x%08x%08x"
#define GUID_ARGS(x) x[0], x[1], x[2]

static inline void make_guid( __u32 *pg, __u16 vendor, __u16 product, char *serial)
{
	pg[0] = (vendor << 16) | product;
	pg[1] = pg[2] = 0;
	while (*serial) {
		pg[1] <<= 4;
		pg[1] |= pg[2] >> 28;
		pg[2] <<= 4;
		if (*serial >= 'a')
			*serial -= 'a' - 'A';
		pg[2] |= (*serial <= '9' && *serial >= '0') ? *serial - '0'
			: *serial - 'A' + 10;
		serial++;
	}
}

/* Flag definitions */
#define US_FL_IP_STATUS	      0x00000001         /* status uses interrupt */
#define US_FL_FIXED_COMMAND   0x00000002 /* expand commands to fixed size */
#define US_FL_MODE_XLATE      0x00000004 /* translate _6 to _10 comands for
					    Win/MacOS compatibility */
