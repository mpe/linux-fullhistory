#ifndef __LINUX_USB_H
#define __LINUX_USB_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/sched.h>

static __inline__ void wait_ms(unsigned int ms)
{
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(1 + ms / 10);
}


typedef struct {
  unsigned char requesttype;
  unsigned char request;
  unsigned short value;
  unsigned short index;
  unsigned short length;
} devrequest;

/*
 * Class codes
 */
#define USB_CLASS_HUB			9

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

/*
 * Request target types.
 */
#define USB_RT_DEVICE			0x00
#define USB_RT_INTERFACE		0x01
#define USB_RT_ENDPOINT			0x02

#define USB_RT_HUB			(USB_TYPE_CLASS | USB_RECIP_DEVICE)
#define USB_RT_PORT			(USB_TYPE_CLASS | USB_RECIP_OTHER)

#define USB_RT_HIDD			(USB_TYPE_CLASS | USB_RECIP_INTERFACE)

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
 *
 * Make this MUCH dynamic, right now
 * it contains enough information for
 * a USB floppy controller, and nothing
 * else.
 *
 * I'm not proud. I just want this dang
 * thing to start working.
 */
#define USB_MAXCONFIG		2
#define USB_MAXINTERFACES	8
#define USB_MAXENDPOINTS	4

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
};

/* Endpoint descriptor */
struct usb_endpoint_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bEndpointAddress;
	__u8  bmAttributes;
	__u16 wMaxPacketSize;
	__u8  bInterval;
};

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

	struct usb_endpoint_descriptor endpoint[USB_MAXENDPOINTS];
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

	struct usb_interface_descriptor interface[USB_MAXINTERFACES];
};

/* String descriptor */
struct usb_string_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
};

/* Hub descriptor */
struct usb_hub_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bNbrPorts;
	__u16 wHubCharacteristics;
	__u8  bPwrOn2PwrGood;
	__u8  bHubContrCurrent;
	/* DeviceRemovable and PortPwrCtrlMask want to be variable-length 
	   bitmaps that hold max 256 entries, but for now they're ignored */
	__u8  filler;
};

struct usb_device;

struct usb_driver {
	const char * name;
	int (*probe)(struct usb_device *);
	void (*disconnect)(struct usb_device *);
	struct list_head driver_list;
};

/*
 * Pointer to a device endpoint interrupt function -greg
 */
typedef int (*usb_device_irq)(int, void *, void *);

struct usb_operations {
	struct usb_device *(*allocate)(struct usb_device *);
	int (*deallocate)(struct usb_device *);
	int (*control_msg)(struct usb_device *, unsigned int, void *, void *, int);
	int (*request_irq)(struct usb_device *, unsigned int, usb_device_irq, int, void *);
};

/*
 * Allocated per bus we have
 */
struct usb_bus {
	struct usb_devmap devmap;       /* Device map */
	struct usb_operations *op;      /* Operations (specific to the HC) */
	struct usb_device *root_hub;    /* Root hub */
	void *hcpriv;                   /* Host Controller private data */
};


#define USB_MAXCHILDREN (8)

struct usb_device {
	int devnum;						/* Device number on USB bus */
	int slow;						/* Slow device? */
	int maxpacketsize;					/* Maximum packet size */

	struct usb_bus *bus;					/* Bus we're apart of */
	struct usb_driver *driver;				/* Driver */
	struct usb_device_descriptor descriptor;		/* Descriptor */
	struct usb_config_descriptor config[USB_MAXCONFIG];	/* All of the configs */
	struct usb_device *parent;
  
	/*
	 * Child devices - these can be either new devices
	 * (if this is a hub device), or different instances
	 * of this same device.
	 *
	 * Each instance needs its own set of data structuctures.
	 */

	int maxchild;			/* Number of ports if hub */
	struct usb_device *children[USB_MAXCHILDREN];

	void *hcpriv;			/* Host Controller private data */
	void *private;			/* Upper layer private data */
};

extern int usb_register(struct usb_driver *);
extern void usb_deregister(struct usb_driver *);

extern int usb_request_irq(struct usb_device *, unsigned int, usb_device_irq, int, void *);

extern void usb_init_root_hub(struct usb_device *dev);
extern void usb_connect(struct usb_device *dev);
extern void usb_disconnect(struct usb_device **);
extern void usb_device_descriptor(struct usb_device *dev);

extern int  usb_parse_configuration(struct usb_device *dev, void *buf, int len);

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
 *  - device:		bits 8-14
 *  - endpoint:		bits 15-18
 *  - Data0/1:		bit 19
 *  - direction:	bit 7		(0 = Host-to-Device, 1 = Device-to-Host)
 *  - speed:		bit 26		(0 = High, 1 = Low Speed)
 *  - max size:		bits 0-1	(00 = 8, 01 = 16, 10 = 32, 11 = 64)
 *  - pipe type:	bits 30-31	(00 = isochronous, 01 = interrupt, 10 = control, 11 = bulk)
 *
 * Why? Because it's arbitrary, and whatever encoding we select is really
 * up to us. This one happens to share a lot of bit positions with the UCHI
 * specification, so that much of the uhci driver can just mask the bits
 * appropriately.
 */

#define usb_maxpacket(pipe)	(8 << ((pipe) & 3))
#define usb_packetid(pipe)	(((pipe) & 0x80) ? 0x69 : 0xE1)

#define usb_pipedevice(pipe)	(((pipe) >> 8) & 0x7f)
#define usb_pipeendpoint(pipe)	(((pipe) >> 15) & 0xf)
#define usb_pipedata(pipe)	(((pipe) >> 19) & 1)
#define usb_pipeout(pipe)	(((pipe) & 0x80) == 0)
#define usb_pipeslow(pipe)	(((pipe) >> 26) & 1)

#define usb_pipetype(pipe)	(((pipe) >> 30) & 3)
#define usb_pipeisoc(pipe)	(usb_pipetype((pipe)) == 0)
#define usb_pipeint(pipe)	(usb_pipetype((pipe)) == 1)
#define usb_pipecontrol(pipe)	(usb_pipetype((pipe)) == 2)
#define usb_pipebulk(pipe)	(usb_pipetype((pipe)) == 3)

#define usb_pipe_endpdev(pipe)	(((pipe) >> 8) & 0x7ff)

static inline unsigned int __create_pipe(struct usb_device *dev, unsigned int endpoint)
{
	return (dev->devnum << 8) | (endpoint << 15) | (dev->slow << 26) | dev->maxpacketsize;
}

static inline unsigned int __default_pipe(struct usb_device *dev)
{
	return (dev->slow << 26);
}

/* Create control pipes.. */
#define usb_sndctrlpipe(dev,endpoint)	((2 << 30) | __create_pipe(dev,endpoint))
#define usb_rcvctrlpipe(dev,endpoint)	((2 << 30) | __create_pipe(dev,endpoint) | 0x80)
#define usb_snddefctrl(dev)		((2 << 30) | __default_pipe(dev))
#define usb_rcvdefctrl(dev)		((2 << 30) | __default_pipe(dev) | 0x80)

/* Create .. */

/*
 * Send and receive control messages..
 */
void usb_new_device(struct usb_device *dev);
int usb_set_address(struct usb_device *dev);
int usb_get_descriptor(struct usb_device *dev, unsigned char desctype, unsigned
char descindex, void *buf, int size);
int usb_get_device_descriptor(struct usb_device *dev);
int usb_get_hub_descriptor(struct usb_device *dev, void *data, int size);
int usb_clear_port_feature(struct usb_device *dev, int port, int feature);
int usb_set_port_feature(struct usb_device *dev, int port, int feature);
int usb_get_hub_status(struct usb_device *dev, void *data);
int usb_get_port_status(struct usb_device *dev, int port, void *data);
int usb_get_protocol(struct usb_device *dev);
int usb_set_protocol(struct usb_device *dev, int protocol);
int usb_set_idle(struct usb_device *dev, int duration, int report_id);
int usb_set_configuration(struct usb_device *dev, int configuration);
int usb_get_report(struct usb_device *dev);

/*
 * Debugging helpers..
 */
void usb_show_device_descriptor(struct usb_device_descriptor *);
void usb_show_config_descriptor(struct usb_config_descriptor *);
void usb_show_interface_descriptor(struct usb_interface_descriptor *);
void usb_show_endpoint_descriptor(struct usb_endpoint_descriptor *);
void usb_show_hub_descriptor(struct usb_hub_descriptor *);
void usb_show_device(struct usb_device *);

/*
 * Audio parsing helpers
 */

#ifdef CONFIG_USB_AUDIO
void usb_audio_interface(struct usb_interface_descriptor *, u8 *);
void usb_audio_endpoint(struct usb_endpoint_descriptor *, u8 *);
#else
extern inline void usb_audio_interface(struct usb_interface_descriptor *, u8 *) {}
extern inline void usb_audio_endpoint(struct usb_endpoint_descriptor *, u8 *) {}
#endif

#endif

