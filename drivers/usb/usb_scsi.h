/* Driver for USB scsi - include file
 * 
 * (C) Michael Gee (michael@linuxspecific.com) 1999
 *
 * This driver is scitzoid  - it make a USB scanner appear as both a SCSI device
 * and a character device. The latter is only available if the device has an
 * interrupt endpoint, and is used specifically to receive interrupt events.
 *
 * In order to support various 'strange' scanners, this module supports plug in
 * device specific filter modules, which can do their own thing when required.
 *
 */

#define USB_SCSI "usbscsi: "

extern int usbscsi_debug;

#ifdef CONFIG_USB_SCSI_DEBUG
void us_show_command(Scsi_Cmnd *srb);
#define US_DEBUGP(x...) { if(usbscsi_debug) printk( KERN_DEBUG USB_SCSI ## x ); }
#define US_DEBUGPX(x...) { if(usbscsi_debug) printk( ## x ); }
#define US_DEBUG(x)  { if(usbscsi_debug) x; }
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
#define US_PR_ZIP	0x50		/* bulk only */
/* #define US_PR_BULK	?? */

/*
 * Bulk only data structures (Zip 100, for example)
 */

struct bulk_cb_wrap {
    __u32	Signature;		/* contains 'USBC' */
    __u32	Tag;			/* unique per command id */
    __u32	DataTransferLength;	/* size of data */
    __u8	Flags;	       		/* direction in bit 0 */
    __u8	Lun;			/* LUN normally 0 */
    __u8	Length;			/* of of the CDB */
    __u8	CDB[16];		/* max command */
};

#define US_BULK_CB_WRAP_LEN 	31
#define US_BULK_CB_SIGN		0x43425355
#define US_BULK_FLAG_IN		1
#define US_BULK_FLAG_OUT	0

struct bulk_cs_wrap {
    __u32	Signature;		/* should = 'USBS' */
    __u32	Tag;			/* same as original command */
    __u32	Residue;		/* amount not transferred */
    __u8	Status;			/* see below */
    __u8	Filler[18];
};

#define US_BULK_CS_WRAP_LEN	31
#define US_BULK_CS_SIGN		0x53425355
#define US_BULK_STAT_OK		0
#define US_BULK_STAT_FAIL	1
#define US_BULK_STAT_PHASE	2

#define US_BULK_RESET		0xff
#define US_BULK_RESET_SOFT	1
#define US_BULK_RESET_HARD	0

/*
 * CBI style
 */

#define US_CBI_ADSC		0

/*
 * Filter device definitions
 */
struct usb_scsi_filter {

	struct usb_scsi_filter * next;	/* usb_scsi driver only */
	char *name;			/* not really required */

        unsigned int flags;                     			/* Filter flags */
        void * (* probe) (struct usb_device *, char *, char *, char *);	/* probe device */
	void (* release)(void *);					/* device gone */
        int (* command)(void *, Scsi_Cmnd *);  /* all commands */
};

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
#define US_FL_IP_STATUS		0x00000001		/* status uses interrupt */
#define US_FL_FIXED_COMMAND	0x00000002		/* expand commands to fixed size */

/*
 * Called by filters to register/unregister the mini driver
 *
 * WARNING - the supplied probe function may be called before exiting this fn
 */
int usb_scsi_register(struct usb_scsi_filter *);
void usb_scsi_deregister(struct usb_scsi_filter *);

#ifdef CONFIG_USB_HP4100
int hp4100_init(void);
#endif
