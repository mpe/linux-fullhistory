#ifndef __LINUX_USB_H
#define __LINUX_USB_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* USB constants */

/*
 * Device and/or Interface Class codes
 */
#define USB_CLASS_PER_INTERFACE		0	/* for DeviceClass */
#define USB_CLASS_AUDIO			1
#define USB_CLASS_COMM			2
#define USB_CLASS_HID			3
#define USB_CLASS_PRINTER		7
#define USB_CLASS_MASS_STORAGE		8
#define USB_CLASS_HUB			9
#define USB_CLASS_DATA			10
#define USB_CLASS_VENDOR_SPEC		0xff

/*
 * Descriptor types
 */
#define USB_DT_DEVICE			0x01
#define USB_DT_CONFIG			0x02
#define USB_DT_STRING			0x03
#define USB_DT_INTERFACE		0x04
#define USB_DT_ENDPOINT			0x05

#define USB_DT_HUB			0x29
#define USB_DT_HID			0x21
#define USB_DT_REPORT			0x22
#define USB_DT_PHYSICAL			0x23

/*
 * Descriptor sizes per descriptor type
 */
#define USB_DT_DEVICE_SIZE		18
#define USB_DT_CONFIG_SIZE		9
#define USB_DT_INTERFACE_SIZE		9
#define USB_DT_ENDPOINT_SIZE		7
#define USB_DT_ENDPOINT_AUDIO_SIZE	9	/* Audio extension */
#define USB_DT_HUB_NONVAR_SIZE		7

/*
 * USB Request Type and Endpoint Directions
 */
#define USB_DIR_OUT			0
#define USB_DIR_IN			0x80

/*
 * USB Packet IDs (PIDs)
 */
#define USB_PID_OUT			0xe1
#define USB_PID_IN			0x69
#define USB_PID_SETUP			0x2d

/*
 * Standard requests
 */
#define USB_REQ_GET_STATUS		0x00
#define USB_REQ_CLEAR_FEATURE		0x01
/* 0x02 is reserved */
#define USB_REQ_SET_FEATURE		0x03
/* 0x04 is reserved */
#define USB_REQ_SET_ADDRESS		0x05
#define USB_REQ_GET_DESCRIPTOR		0x06
#define USB_REQ_SET_DESCRIPTOR		0x07
#define USB_REQ_GET_CONFIGURATION	0x08
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_REQ_GET_INTERFACE		0x0A
#define USB_REQ_SET_INTERFACE		0x0B
#define USB_REQ_SYNCH_FRAME		0x0C

/*
 * HIDD requests
 */
#define USB_REQ_GET_REPORT		0x01
#define USB_REQ_GET_IDLE		0x02
#define USB_REQ_GET_PROTOCOL		0x03
#define USB_REQ_SET_REPORT		0x09
#define USB_REQ_SET_IDLE		0x0A
#define USB_REQ_SET_PROTOCOL		0x0B

#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)

#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03

#define USB_HID_RPT_INPUT		0x01
#define USB_HID_RPT_OUTPUT		0x02
#define USB_HID_RPT_FEATURE		0x03

/*
 * Request target types.
 */
#define USB_RT_DEVICE			0x00
#define USB_RT_INTERFACE		0x01
#define USB_RT_ENDPOINT			0x02

#define USB_RT_HUB			(USB_TYPE_CLASS | USB_RECIP_DEVICE)
#define USB_RT_PORT			(USB_TYPE_CLASS | USB_RECIP_OTHER)

#define USB_RT_HIDD			(USB_TYPE_CLASS | USB_RECIP_INTERFACE)

/* /proc/bus/usb/xxx/yyy ioctl codes */

struct usb_proc_ctrltransfer {
	__u8 requesttype;
	__u8 request;
	__u16 value;
	__u16 index;
	__u16 length;
        /* pointer to data */
        void *data;
};

#define USB_PROC_CONTROL           _IOWR('U', 0, struct usb_proc_ctrltransfer)

struct usb_proc_bulktransfer {
        unsigned int ep;
        unsigned int len;
        void *data;
};

#define USB_PROC_BULK              _IOWR('U', 2, struct usb_proc_bulktransfer)

#define USB_PROC_RESETEP           _IOR('U', 3, unsigned int)

struct usb_proc_setinterface {
        unsigned int interface;
        unsigned int altsetting;
};

#define USB_PROC_SETINTERFACE      _IOR('U', 4, struct usb_proc_setinterface)

#define USB_PROC_SETCONFIGURATION  _IOR('U', 5, unsigned int)




#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/list.h>
#include <linux/sched.h>

extern int usb_hub_init(void);
extern int usb_kbd_init(void);
extern int usb_cpia_init(void);
extern int usb_mouse_init(void);
extern int usb_printer_init(void);

extern void usb_hub_cleanup(void);
extern void usb_mouse_cleanup(void);

static __inline__ void wait_ms(unsigned int ms)
{
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(1 + ms * HZ / 1000);
}

typedef struct {
	__u8 requesttype;
	__u8 request;
	__u16 value;
	__u16 index;
	__u16 length;
} devrequest __attribute__ ((packed));

/* 
 * Status codes (these follow OHCI controllers condition codes)
 */
#define USB_ST_NOERROR		0x0
#define USB_ST_CRC		0x1
#define USB_ST_BITSTUFF		0x2
#define USB_ST_DTMISMATCH	0x3	/* data toggle mismatch */
#define USB_ST_STALL		0x4
#define USB_ST_NORESPONSE	0x5	/* device not responding/handshaking */
#define USB_ST_PIDCHECK		0x6	/* Check bits on PID failed */
#define USB_ST_PIDUNDEF		0x7	/* PID unexpected/undefined */
#define USB_ST_DATAOVERRUN	0x8
#define USB_ST_DATAUNDERRUN	0x9
#define USB_ST_RESERVED1	0xA
#define USB_ST_RESERVED2	0xB
#define USB_ST_BUFFEROVERRUN	0xC
#define USB_ST_BUFFERUNDERRUN	0xD
#define USB_ST_RESERVED3	0xE
#define USB_ST_RESERVED4	0xF

/* internal errors */
#define USB_ST_REMOVED		0x100
#define USB_ST_TIMEOUT		0x110
#define USB_ST_INTERNALERROR	-1
#define USB_ST_NOTSUPPORTED	-2
#define USB_ST_BANDWIDTH_ERROR	-3

/*
 * USB device number allocation bitmap. There's one bitmap
 * per USB tree.
 */
struct usb_devmap {
	unsigned long devicemap[128 / (8*sizeof(unsigned long))];
};

/*
 * This is a USB device descriptor.
 *
 * USB device information
 */

/* Everything but the endpoint maximums are aribtrary */
#define USB_MAXCONFIG		8
#define USB_MAXALTSETTING	16
#define USB_MAXINTERFACES	32
#define USB_MAXENDPOINTS	32

/* All standard descriptors have these 2 fields in common */
struct usb_descriptor_header {
	__u8  bLength;
	__u8  bDescriptorType;
} __attribute__ ((packed));

/* Device descriptor */
struct usb_device_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 bcdUSB;
	__u8  bDeviceClass;
	__u8  bDeviceSubClass;
	__u8  bDeviceProtocol;
	__u8  bMaxPacketSize0;
	__u16 idVendor;
	__u16 idProduct;
	__u16 bcdDevice;
	__u8  iManufacturer;
	__u8  iProduct;
	__u8  iSerialNumber;
	__u8  bNumConfigurations;
} __attribute__ ((packed));

/* Endpoint descriptor */
struct usb_endpoint_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bEndpointAddress;
	__u8  bmAttributes;
	__u16 wMaxPacketSize;
	__u8  bInterval;
	__u8  bRefresh;
	__u8  bSynchAddress;
} __attribute__ ((packed));

/* HID descriptor */
struct usb_hid_class_descriptor {
	__u8  bDescriptorType;
	__u16 wDescriptorLength;
} __attribute__ ((packed));

struct usb_hid_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 bcdHID;
	__u8  bCountryCode;
	__u8  bNumDescriptors;

	struct usb_hid_class_descriptor desc[1];
} __attribute__ ((packed));

/* Interface descriptor */
struct usb_interface_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bInterfaceNumber;
	__u8  bAlternateSetting;
	__u8  bNumEndpoints;
	__u8  bInterfaceClass;
	__u8  bInterfaceSubClass;
	__u8  bInterfaceProtocol;
	__u8  iInterface;

	struct usb_hid_descriptor *hid;
	struct usb_endpoint_descriptor *endpoint;
} __attribute__ ((packed));

struct usb_interface {
	struct usb_interface_descriptor *altsetting;

	int act_altsetting;		/* active alternate setting */
	int num_altsetting;		/* number of alternate settings */
};

/* Configuration descriptor information.. */
struct usb_config_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 wTotalLength;
	__u8  bNumInterfaces;
	__u8  bConfigurationValue;
	__u8  iConfiguration;
	__u8  bmAttributes;
	__u8  MaxPower;

	struct usb_interface *interface;
} __attribute__ ((packed));

/* String descriptor */
struct usb_string_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 wData[1];
} __attribute__ ((packed));

struct usb_device;

struct usb_driver {
	const char *name;

	int (*probe)(struct usb_device *);
	void (*disconnect)(struct usb_device *);

	struct list_head driver_list;
};

/*
 * Pointer to a device endpoint interrupt function -greg
 *   Parameters:
 *     int status - This needs to be defined.  Right now each HCD
 *         passes different transfer status bits back.  Don't use it
 *         until we come up with a common meaning.
 *     void *buffer - This is a pointer to the data used in this
 *         USB transfer.
 *     int length - This is the number of bytes transferred in or out
 *         of the buffer by this transfer.  (-1 = unknown/unsupported)
 *     void *dev_id - This is a user defined pointer set when the IRQ
 *         is requested that is passed back.
 *
 *   Special Cases:
 *     if (status == USB_ST_REMOVED), don't trust buffer or len.
 */
typedef int (*usb_device_irq)(int, void *, int, void *);

/*
 * Isoc. support additions
 */
#define START_FRAME_FUDGE      3

#define USB_WRAP_FRAMENR(x) ((x) & 2047)

/* for start_type: */
enum {
	START_ASAP = 0,
	START_ABSOLUTE,
	START_RELATIVE
};
#define START_TYPE_MAX     START_RELATIVE

/*
 * Completion/Callback routine returns an enum,
 * which tells the interrupt handler what to do
 * with the completed frames (TDs).
 */
enum {
	CB_CONTINUE = 0,        /* OK, remove all TDs;
				   needs to be 0 to be consistent with
				   current callback function ret. values */
	CB_REUSE,               /* leave descriptors as NULL, not active */
	CB_RESTART,             /* leave descriptors as they are, alive */
	CB_ABORT,                /* kill this USB transfer request */
	CB_CONT_RUN    		/* append the isoc_desc at the end of all active isoc_desc */
};

struct isoc_frame_desc {
	int             frame_length;   /* may be 0 (i.e., allowed) */
			/* set by driver for OUTs to devices;
			 * set by USBD for INs from devices,
			 * after I/O complete */
	unsigned int    frame_status;
			/* set by USBD after I/O complete */
};

struct usb_isoc_desc {
	/*
	 * The following fields are set by the usb_init_isoc() call.
	 */
	struct usb_device *usb_dev;
	unsigned int    pipe;
	int             frame_count;
	void            *context;       /* driver context (private) ptr */
	int             frame_size;
	/*
	 * The following fields are set by the driver between the
	 * usb_init_isoc() and usb_run_isoc() calls
	 * (along with the "frames" array for OUTput).
	 */
	int             start_type;
	int             start_frame;    /* optional, depending on start_type */
	int             frame_spacing;  /* not using (yet?) */
	int             callback_frames; /* every # frames + last one */
				/* 0 means no callbacks until IOC on last frame */
	usb_device_irq  callback_fn;
	void            *data;
	int             buf_size;
	struct usb_isoc_desc *prev_isocdesc; /* previous isoc_desc, for CB_CONT_RUN */
	/*
	 * The following fields are set by the usb_run_isoc() call.
	 */
	int             end_frame;
	void            *td;            /* UHCI or OHCI TD ptr */
	/*
	 * The following fields are set by the USB HCD interrupt handler
	 * before calling the driver's callback function.
	 */
	int             total_completed_frames;
	int             prev_completed_frame;   /* from the previous callback */
	int             cur_completed_frame;    /* from this callback */
	int             total_length;           /* accumulated */
	int             error_count;            /* accumulated */
	struct isoc_frame_desc frames [0];      /* variable size: [frame_count] */
};

struct usb_operations {
	int (*allocate)(struct usb_device *);
	int (*deallocate)(struct usb_device *);
	int (*control_msg)(struct usb_device *, unsigned int, devrequest *, void *, int);
	int (*bulk_msg)(struct usb_device *, unsigned int, void *, int,unsigned long *);
	void *(*request_irq)(struct usb_device *, unsigned int, usb_device_irq, int, void *);
	int (*release_irq)(struct usb_device *, void *);
	void *(*request_bulk)(struct usb_device *, unsigned int, usb_device_irq,
 void *, int, void *);
	int (*terminate_bulk)(struct usb_device *, void *);
	int (*get_frame_number) (struct usb_device *usb_dev);
	int (*init_isoc) (struct usb_device *usb_dev, unsigned int pipe,
				int frame_count, void *context, struct usb_isoc_desc **isocdesc);
	void (*free_isoc) (struct usb_isoc_desc *isocdesc);
	int (*run_isoc) (struct usb_isoc_desc *isocdesc, struct usb_isoc_desc *pr_isocdesc);
	int (*kill_isoc) (struct usb_isoc_desc *isocdesc);
};

/*
 * Allocated per bus we have
 */
struct usb_bus {
	struct usb_devmap devmap;       /* Device map */
	struct usb_operations *op;      /* Operations (specific to the HC) */
	struct usb_device *root_hub;    /* Root hub */
	struct list_head bus_list;
	void *hcpriv;                   /* Host Controller private data */

	/* procfs entry */
	int proc_busnum;
	struct proc_dir_entry *proc_entry;
};

#define USB_MAXCHILDREN (8)	/* This is arbitrary */

struct usb_device {
	int devnum;			/* Device number on USB bus */
	int slow;			/* Slow device? */

	atomic_t refcnt;		/* Reference count */

	int maxpacketsize;		/* Maximum packet size; encoded as 0,1,2,3 = 8,16,32,64 */
	unsigned int toggle[2];		/* one bit for each endpoint ([0] = IN, [1] = OUT) */
	unsigned int halted[2];		/* endpoint halts; one bit per endpoint # & direction; */
					/* [0] = IN, [1] = OUT */
	struct usb_config_descriptor *actconfig;/* the active configuration */
	int epmaxpacketin[16];		/* INput endpoint specific maximums */
	int epmaxpacketout[16];		/* OUTput endpoint specific maximums */
	int ifnum;			/* active interface number */

	struct usb_device *parent;
	struct usb_bus *bus;		/* Bus we're part of */
	struct usb_driver *driver;	/* Driver */

	struct usb_device_descriptor descriptor;/* Descriptor */
	struct usb_config_descriptor *config;	/* All of the configs */

	char *string;			/* pointer to the last string read from the device */
	int string_langid;		/* language ID for strings */
  
	void *hcpriv;			/* Host Controller private data */
	void *private;			/* Upper layer private data */

	/* procfs entry */
	struct proc_dir_entry *proc_entry;

	/*
	 * Child devices - these can be either new devices
	 * (if this is a hub device), or different instances
	 * of this same device.
	 *
	 * Each instance needs its own set of data structures.
	 */

	int maxchild;			/* Number of ports if hub */
	struct usb_device *children[USB_MAXCHILDREN];
};

extern int usb_register(struct usb_driver *);
extern void usb_deregister(struct usb_driver *);

int usb_find_driver(struct usb_device *);
void usb_check_support(struct usb_device *);
void usb_driver_purge(struct usb_driver *, struct usb_device *);

extern struct usb_bus *usb_alloc_bus(struct usb_operations *);
extern void usb_free_bus(struct usb_bus *);
extern void usb_register_bus(struct usb_bus *);
extern void usb_deregister_bus(struct usb_bus *);

extern struct usb_device *usb_alloc_dev(struct usb_device *parent, struct usb_bus *);
extern void usb_free_dev(struct usb_device *);
extern void usb_inc_dev_use(struct usb_device *);
#define usb_dec_dev_use usb_free_dev

extern int usb_control_msg(struct usb_device *dev, unsigned int pipe, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size);

extern void *usb_request_irq(struct usb_device *, unsigned int, usb_device_irq, int, void *);
extern int usb_release_irq(struct usb_device *dev, void *handle);

extern void *usb_request_bulk(struct usb_device *, unsigned int, usb_device_irq, void *, int, void *);
extern int usb_terminate_bulk(struct usb_device *, void *);

extern void usb_init_root_hub(struct usb_device *dev);
extern void usb_connect(struct usb_device *dev);
extern void usb_disconnect(struct usb_device **);

extern void usb_destroy_configuration(struct usb_device *dev);

int usb_get_current_frame_number (struct usb_device *usb_dev);

int usb_init_isoc (struct usb_device *usb_dev,
			unsigned int pipe,
			int frame_count,
			void *context,
			struct usb_isoc_desc **isocdesc);

void usb_free_isoc (struct usb_isoc_desc *isocdesc);

int usb_run_isoc (struct usb_isoc_desc *isocdesc,
			struct usb_isoc_desc *pr_isocdesc);

int usb_kill_isoc (struct usb_isoc_desc *isocdesc);

/*
 * Calling this entity a "pipe" is glorifying it. A USB pipe
 * is something embarrassingly simple: it basically consists
 * of the following information:
 *  - device number (7 bits)
 *  - endpoint number (4 bits)
 *  - current Data0/1 state (1 bit)
 *  - direction (1 bit)
 *  - speed (1 bit)
 *  - max packet size (2 bits: 8, 16, 32 or 64)
 *  - pipe type (2 bits: control, interrupt, bulk, isochronous)
 *
 * That's 18 bits. Really. Nothing more. And the USB people have
 * documented these eighteen bits as some kind of glorious
 * virtual data structure.
 *
 * Let's not fall in that trap. We'll just encode it as a simple
 * unsigned int. The encoding is:
 *
 *  - max size:		bits 0-1	(00 = 8, 01 = 16, 10 = 32, 11 = 64)
 *  - direction:	bit 7		(0 = Host-to-Device [Out], 1 = Device-to-Host [In])
 *  - device:		bits 8-14
 *  - endpoint:		bits 15-18
 *  - Data0/1:		bit 19
 *  - speed:		bit 26		(0 = Full, 1 = Low Speed)
 *  - pipe type:	bits 30-31	(00 = isochronous, 01 = interrupt, 10 = control, 11 = bulk)
 *
 * Why? Because it's arbitrary, and whatever encoding we select is really
 * up to us. This one happens to share a lot of bit positions with the UHCI
 * specification, so that much of the uhci driver can just mask the bits
 * appropriately.
 */

#define usb_maxpacket(dev, pipe, out)	(out \
				? (dev)->epmaxpacketout[usb_pipeendpoint(pipe)] \
				: (dev)->epmaxpacketin [usb_pipeendpoint(pipe)] )
#define usb_packetid(pipe)	(((pipe) & USB_DIR_IN) ? USB_PID_IN : USB_PID_OUT)

#define usb_pipeout(pipe)	((((pipe) >> 7) & 1) ^ 1)
#define usb_pipein(pipe)	(((pipe) >> 7) & 1)
#define usb_pipedevice(pipe)	(((pipe) >> 8) & 0x7f)
#define usb_pipe_endpdev(pipe)	(((pipe) >> 8) & 0x7ff)
#define usb_pipeendpoint(pipe)	(((pipe) >> 15) & 0xf)
#define usb_pipedata(pipe)	(((pipe) >> 19) & 1)
#define usb_pipeslow(pipe)	(((pipe) >> 26) & 1)
#define usb_pipetype(pipe)	(((pipe) >> 30) & 3)
#define usb_pipeisoc(pipe)	(usb_pipetype((pipe)) == 0)
#define usb_pipeint(pipe)	(usb_pipetype((pipe)) == 1)
#define usb_pipecontrol(pipe)	(usb_pipetype((pipe)) == 2)
#define usb_pipebulk(pipe)	(usb_pipetype((pipe)) == 3)

#define PIPE_DEVEP_MASK		0x0007ff00

/* The D0/D1 toggle bits */
#define usb_gettoggle(dev, ep, out) (((dev)->toggle[out] >> ep) & 1)
#define	usb_dotoggle(dev, ep, out)  ((dev)->toggle[out] ^= (1 << ep))
#define usb_settoggle(dev, ep, out, bit) ((dev)->toggle[out] = ((dev)->toggle[out] & ~(1 << ep)) | ((bit) << ep))

/* Endpoint halt control/status */
#define usb_endpoint_out(ep_dir)	(((ep_dir >> 7) & 1) ^ 1)
#define usb_endpoint_halt(dev, ep, out) ((dev)->halted[out] |= (1 << (ep)))
#define usb_endpoint_running(dev, ep, out) ((dev)->halted[out] &= ~(1 << (ep)))
#define usb_endpoint_halted(dev, ep, out) ((dev)->halted[out] & (1 << (ep)))

static inline unsigned int __create_pipe(struct usb_device *dev, unsigned int endpoint)
{
	return (dev->devnum << 8) | (endpoint << 15) | (dev->slow << 26) | dev->maxpacketsize;
}

static inline unsigned int __default_pipe(struct usb_device *dev)
{
	return (dev->slow << 26);
}

/* Create various pipes... */
#define usb_sndctrlpipe(dev,endpoint)	((2 << 30) | __create_pipe(dev,endpoint))
#define usb_rcvctrlpipe(dev,endpoint)	((2 << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_sndisocpipe(dev,endpoint)	((0 << 30) | __create_pipe(dev,endpoint))
#define usb_rcvisocpipe(dev,endpoint)	((0 << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_sndbulkpipe(dev,endpoint)	((3 << 30) | __create_pipe(dev,endpoint))
#define usb_rcvbulkpipe(dev,endpoint)	((3 << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_snddefctrl(dev)		((2 << 30) | __default_pipe(dev))
#define usb_rcvdefctrl(dev)		((2 << 30) | __default_pipe(dev) | USB_DIR_IN)

/*
 * Send and receive control messages..
 */
int usb_new_device(struct usb_device *dev);
int usb_set_address(struct usb_device *dev);
int usb_get_descriptor(struct usb_device *dev, unsigned char desctype, unsigned
char descindex, void *buf, int size);
int usb_get_device_descriptor(struct usb_device *dev);
int usb_get_status (struct usb_device *dev, int type, int target, void *data);
int usb_get_protocol(struct usb_device *dev);
int usb_set_protocol(struct usb_device *dev, int protocol);
int usb_set_interface(struct usb_device *dev, int interface, int alternate);
int usb_set_idle(struct usb_device *dev, int duration, int report_id);
int usb_set_interface(struct usb_device *dev, int interface, int alternate);
int usb_set_configuration(struct usb_device *dev, int configuration);
int usb_get_report(struct usb_device *dev, unsigned char type, unsigned char id, unsigned char index, void *buf, int size);
char *usb_string(struct usb_device *dev, int index);
int usb_clear_halt(struct usb_device *dev, int endp);

/*
 * Debugging helpers..
 */
void usb_show_device_descriptor(struct usb_device_descriptor *);
void usb_show_config_descriptor(struct usb_config_descriptor *);
void usb_show_interface_descriptor(struct usb_interface_descriptor *);
void usb_show_hid_descriptor(struct usb_hid_descriptor * desc);
void usb_show_endpoint_descriptor(struct usb_endpoint_descriptor *);
void usb_show_device(struct usb_device *);
void usb_show_string(struct usb_device *dev, char *id, int index);

/*
 * procfs stuff
 */

#ifdef CONFIG_USB_PROC
void proc_usb_add_bus(struct usb_bus *bus);
void proc_usb_remove_bus(struct usb_bus *bus);
void proc_usb_add_device(struct usb_device *dev);
void proc_usb_remove_device(struct usb_device *dev);
#else
extern inline void proc_usb_add_bus(struct usb_bus *bus) {}
extern inline void proc_usb_remove_bus(struct usb_bus *bus) {}
extern inline void proc_usb_add_device(struct usb_device *dev) {}
extern inline void proc_usb_remove_device(struct usb_device *dev) {}
#endif

#endif  /* __KERNEL */

#endif

