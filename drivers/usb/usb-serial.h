/*
 * USB Serial Converter driver
 *
 *	(C) Copyright (C) 1999, 2000
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 * 
 */


#ifndef __LINUX_USB_SERIAL_H
#define __LINUX_USB_SERIAL_H

#include <linux/config.h>

/* Module information */
MODULE_AUTHOR("Greg Kroah-Hartman, greg@kroah.com, http://www.kroah.com/linux-usb/");
MODULE_DESCRIPTION("USB Serial Driver");

#ifdef CONFIG_USB_SERIAL_GENERIC
static __u16	vendor	= 0x05f9;
static __u16	product	= 0xffff;
MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");
#endif


/* USB Serial devices vendor ids and device ids that this driver supports */
#define CONNECT_TECH_VENDOR_ID		0x0710
#define CONNECT_TECH_FAKE_WHITE_HEAT_ID	0x0001
#define CONNECT_TECH_WHITE_HEAT_ID	0x8001
#define HANDSPRING_VENDOR_ID		0x082d
#define HANDSPRING_VISOR_ID		0x0100
#define FTDI_VENDOR_ID			0x0403
#define FTDI_SIO_SERIAL_CONVERTER_ID	0x8372
#define KEYSPAN_VENDOR_ID		0x06cd
#define KEYSPAN_PDA_FAKE_ID		0x0103
#define KEYSPAN_PDA_ID			0x0103

#define SERIAL_TTY_MAJOR	188	/* Nice legal number now */
#define SERIAL_TTY_MINORS	16	/* Actually we are allowed 255, but this is good for now */


#define MAX_NUM_PORTS	8	/* The maximum number of ports one device can grab at once */

struct usb_serial {
	struct usb_device *		dev;
	struct usb_serial_device_type *	type;
	void *				irq_handle;
	unsigned int			irqpipe;
	struct tty_struct *		tty;			/* the coresponding tty for this device */
	unsigned char			minor;
	unsigned char			num_ports;		/* the number of ports this device has */
	char				active[MAX_NUM_PORTS];	/* someone has this device open */

	char			num_interrupt_in;		/* number of interrupt in endpoints we have */
	__u8			interrupt_in_interval[MAX_NUM_PORTS];
	unsigned char *		interrupt_in_buffer[MAX_NUM_PORTS];
	struct urb		control_urb[MAX_NUM_PORTS];

	char			num_bulk_in;			/* number of bulk in endpoints we have */
	unsigned char *		bulk_in_buffer[MAX_NUM_PORTS];
	struct urb		read_urb[MAX_NUM_PORTS];

	char			num_bulk_out;			/* number of bulk out endpoints we have */
	unsigned char *		bulk_out_buffer[MAX_NUM_PORTS];
	int			bulk_out_size[MAX_NUM_PORTS];
	struct urb		write_urb[MAX_NUM_PORTS];
};


#define MUST_HAVE_NOT	0x01
#define MUST_HAVE	0x02
#define DONT_CARE	0x03

#define	HAS		0x02
#define HAS_NOT		0x01

#define NUM_DONT_CARE	(-1)


/* This structure defines the individual serial converter. */
struct usb_serial_device_type {
	char	*name;
	__u16	*idVendor;
	__u16	*idProduct;
	char	needs_interrupt_in;
	char	needs_bulk_in;
	char	needs_bulk_out;
	char	num_interrupt_in;
	char	num_bulk_in;
	char	num_bulk_out;
	char	num_ports;		/* number of serial ports this device has */

	void	*private;		/* data private to the specific driver */
	
	/* function call to make before accepting driver */
	int (*startup) (struct usb_serial *serial);	/* return 0 to continue initialization, anything else to abort */
	
	/* serial function calls */
	int  (*open)(struct tty_struct * tty, struct file * filp);
	void (*close)(struct tty_struct * tty, struct file * filp);
	int  (*write)(struct tty_struct * tty, int from_user,const unsigned char *buf, int count);
	int  (*write_room)(struct tty_struct *tty);
	int  (*ioctl)(struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg);
	void (*set_termios)(struct tty_struct *tty, struct termios * old);
	int  (*chars_in_buffer)(struct tty_struct *tty);
	void (*throttle)(struct tty_struct * tty);
	void (*unthrottle)(struct tty_struct * tty);
	
	void (*read_bulk_callback)(struct urb *urb);
	void (*write_bulk_callback)(struct urb *urb);

};


/* function prototypes for a "generic" type serial converter (no flow control, not all endpoints needed) */
/* need to always compile these in, as some of the other devices use these functions as their own. */
/* if a driver does not provide a function pointer, the generic function will be called. */
static int  generic_serial_open		(struct tty_struct *tty, struct file *filp);
static void generic_serial_close	(struct tty_struct *tty, struct file *filp);
static int  generic_serial_write	(struct tty_struct *tty, int from_user, const unsigned char *buf, int count);
static int  generic_write_room		(struct tty_struct *tty);
static int  generic_ioctl		(struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg);
static void generic_set_termios		(struct tty_struct *tty, struct termios * old);
static int  generic_chars_in_buffer	(struct tty_struct *tty);
static void generic_throttle		(struct tty_struct *tty);
static void generic_unthrottle		(struct tty_struct *tty);
static void generic_read_bulk_callback	(struct urb *urb);
static void generic_write_bulk_callback	(struct urb *urb);


#ifdef CONFIG_USB_SERIAL_GENERIC
/* All of the device info needed for the Generic Serial Converter */
static struct usb_serial_device_type generic_device = {
	name:			"Generic",
	idVendor:		&vendor,		/* use the user specified vendor id */
	idProduct:		&product,		/* use the user specified product id */
	needs_interrupt_in:	DONT_CARE,		/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,		/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,		/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
};
#endif


#ifdef CONFIG_USB_SERIAL_WHITEHEAT
/* function prototypes for the Connect Tech WhiteHEAT serial converter */
static int  whiteheat_serial_open	(struct tty_struct *tty, struct file *filp);
static void whiteheat_serial_close	(struct tty_struct *tty, struct file *filp);
static void whiteheat_set_termios	(struct tty_struct *tty, struct termios * old);
static void whiteheat_throttle		(struct tty_struct *tty);
static void whiteheat_unthrottle	(struct tty_struct *tty);
static int  whiteheat_startup		(struct usb_serial *serial);

/* All of the device info needed for the Connect Tech WhiteHEAT */
static __u16	connecttech_vendor_id			= CONNECT_TECH_VENDOR_ID;
static __u16	connecttech_whiteheat_fake_product_id	= CONNECT_TECH_FAKE_WHITE_HEAT_ID;
static __u16	connecttech_whiteheat_product_id	= CONNECT_TECH_WHITE_HEAT_ID;
static struct usb_serial_device_type whiteheat_fake_device = {
	name:			"Connect Tech - WhiteHEAT - (prerenumeration)",
	idVendor:		&connecttech_vendor_id,			/* the Connect Tech vendor id */
	idProduct:		&connecttech_whiteheat_fake_product_id,	/* the White Heat initial product id */
	needs_interrupt_in:	DONT_CARE,				/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,				/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,				/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	startup:		whiteheat_startup	
};
static struct usb_serial_device_type whiteheat_device = {
	name:			"Connect Tech - WhiteHEAT",
	idVendor:		&connecttech_vendor_id,			/* the Connect Tech vendor id */
	idProduct:		&connecttech_whiteheat_product_id,	/* the White Heat real product id */
	needs_interrupt_in:	DONT_CARE,				/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,				/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,				/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		4,
	open:			whiteheat_serial_open,
	close:			whiteheat_serial_close,
	throttle:		whiteheat_throttle,
	unthrottle:		whiteheat_unthrottle,
	set_termios:		whiteheat_set_termios,
};
#endif


#ifdef CONFIG_USB_SERIAL_VISOR

/****************************************************************************
 * Handspring Visor Vendor specific request codes (bRequest values)
 * A big thank you to Handspring for providing the following information.
 * If anyone wants the original file where these values and structures came
 * from, send email to <greg@kroah.com>.
 ****************************************************************************/

/****************************************************************************
 * VISOR_REQUEST_BYTES_AVAILABLE asks the visor for the number of bytes that
 * are available to be transfered to the host for the specified endpoint.
 * Currently this is not used, and always returns 0x0001
 ****************************************************************************/
#define VISOR_REQUEST_BYTES_AVAILABLE		0x01

/****************************************************************************
 * VISOR_CLOSE_NOTIFICATION is set to the device to notify it that the host
 * is now closing the pipe. An empty packet is sent in response.
 ****************************************************************************/
#define VISOR_CLOSE_NOTIFICATION		0x02

/****************************************************************************
 * VISOR_GET_CONNECTION_INFORMATION is sent by the host during enumeration to
 * get the endpoints used by the connection.
 ****************************************************************************/
#define VISOR_GET_CONNECTION_INFORMATION	0x03


/****************************************************************************
 * VISOR_GET_CONNECTION_INFORMATION returns data in the following format
 ****************************************************************************/
struct visor_connection_info {
	__u16	num_ports;
	struct {
		__u8	port_function_id;
		__u8	port;
	} connections[2];
};


/* struct visor_connection_info.connection[x].port defines: */
#define VISOR_ENDPOINT_1		0x01
#define VISOR_ENDPOINT_2		0x02

/* struct visor_connection_info.connection[x].port_function_id defines: */
#define VISOR_FUNCTION_GENERIC		0x00
#define VISOR_FUNCTION_DEBUGGER		0x01
#define VISOR_FUNCTION_HOTSYNC		0x02
#define VISOR_FUNCTION_CONSOLE		0x03
#define VISOR_FUNCTION_REMOTE_FILE_SYS	0x04


/* function prototypes for a handspring visor */
static int  visor_serial_open		(struct tty_struct *tty, struct file *filp);
static void visor_serial_close		(struct tty_struct *tty, struct file *filp);
static void visor_throttle		(struct tty_struct *tty);
static void visor_unthrottle		(struct tty_struct *tty);
static int  visor_startup		(struct usb_serial *serial);

/* All of the device info needed for the Handspring Visor */
static __u16	handspring_vendor_id	= HANDSPRING_VENDOR_ID;
static __u16	handspring_product_id	= HANDSPRING_VISOR_ID;
static struct usb_serial_device_type handspring_device = {
	name:			"Handspring Visor",
	idVendor:		&handspring_vendor_id,	/* the Handspring vendor ID */
	idProduct:		&handspring_product_id,	/* the Handspring Visor product id */
	needs_interrupt_in:	MUST_HAVE_NOT,		/* this device must not have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	0,
	num_bulk_in:		2,
	num_bulk_out:		2,
	num_ports:		2,
	open:			visor_serial_open,
	close:			visor_serial_close,
	throttle:		visor_throttle,
	unthrottle:		visor_unthrottle,
	startup:		visor_startup,
};
#endif


#ifdef CONFIG_USB_SERIAL_FTDI_SIO
/* function prototypes for a FTDI serial converter */
static int  ftdi_sio_serial_open	(struct tty_struct *tty, struct file *filp);
static void ftdi_sio_serial_close	(struct tty_struct *tty, struct file *filp);

/* All of the device info needed for the FTDI SIO serial converter */
static __u16	ftdi_vendor_id		= FTDI_VENDOR_ID;
static __u16	ftdi_sio_product_id	= FTDI_SIO_SERIAL_CONVERTER_ID;
static struct usb_serial_device_type ftdi_sio_device = {
	name:			"FTDI SIO",
	idVendor:		&ftdi_vendor_id,	/* the FTDI vendor ID */
	idProduct:		&ftdi_sio_product_id,	/* the FTDI SIO product id */
	needs_interrupt_in:	MUST_HAVE_NOT,		/* this device must not have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	0,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	open:			ftdi_sio_serial_open,
	close:			ftdi_sio_serial_close,
};
#endif


#ifdef CONFIG_USB_SERIAL_KEYSPAN_PDA
/* function prototypes for a FTDI serial converter */
static int  keyspan_pda_serial_open	(struct tty_struct *tty, struct file *filp);
static void keyspan_pda_serial_close	(struct tty_struct *tty, struct file *filp);
static int  keyspan_pda_startup		(struct usb_serial *serial);

/* All of the device info needed for the Keyspan PDA serial converter */
static __u16	keyspan_vendor_id		= KEYSPAN_VENDOR_ID;
static __u16	keyspan_pda_fake_product_id	= KEYSPAN_PDA_FAKE_ID;
static __u16	keyspan_pda_product_id		= KEYSPAN_PDA_ID;
static struct usb_serial_device_type keyspan_pda_fake_device = {
	name:			"Keyspan PDA - (prerenumeration)",
	idVendor:		&keyspan_vendor_id,		/* the Keyspan PDA vendor ID */
	idProduct:		&keyspan_pda_fake_product_id,	/* the Keyspan PDA initial product id */
	needs_interrupt_in:	DONT_CARE,			/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,			/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,			/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	startup:		keyspan_pda_startup	
};
static struct usb_serial_device_type keyspan_pda_device = {
	name:			"Keyspan PDA",
	idVendor:		&keyspan_vendor_id,		/* the Keyspan PDA vendor ID */
	idProduct:		&keyspan_pda_product_id,	/* the Keyspan PDA product id */
	needs_interrupt_in:	MUST_HAVE_NOT,			/* this device must not have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,			/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,			/* this device must have a bulk out endpoint */
	num_interrupt_in:	0,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	open:			keyspan_pda_serial_open,
	close:			keyspan_pda_serial_close,
};
#endif

/* To add support for another serial converter, create a usb_serial_device_type
   structure for that device, and add it to this list, making sure that the last
   entry is NULL. */
static struct usb_serial_device_type *usb_serial_devices[] = {
#ifdef CONFIG_USB_SERIAL_GENERIC
	&generic_device,
#endif
#ifdef CONFIG_USB_SERIAL_WHITEHEAT
	&whiteheat_fake_device,
	&whiteheat_device,
#endif
#ifdef CONFIG_USB_SERIAL_VISOR
	&handspring_device,
#endif
#ifdef CONFIG_USB_SERIAL_FTDI_SIO
	&ftdi_sio_device,
#endif
#ifdef CONFIG_USB_SERIAL_KEYSPAN_PDA
	&keyspan_pda_fake_device,
	&keyspan_pda_device,
#endif
	NULL
};


/* determine if we should include the EzUSB loader functions */
#if defined(CONFIG_USB_SERIAL_KEYSPAN_PDA) || defined(CONFIG_USB_SERIAL_WHITEHEAT)
	#define	USES_EZUSB_FUNCTIONS
#else
	#undef 	USES_EZUSB_FUNCTIONS
#endif


/* used to mark that a pointer is empty (and not NULL) */
#define SERIAL_PTR_EMPTY ((void *)(-1))


#endif	/* ifdef __LINUX_USB_SERIAL_H */

