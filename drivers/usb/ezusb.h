/*****************************************************************************/

/*
 *	ezusb.h  --  Firmware download miscdevice for Anchorchips EZUSB microcontrollers.
 *
 *	Copyright (C) 1999
 *          Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*****************************************************************************/

#ifndef _LINUX_EZUSB_H
#define _LINUX_EZUSB_H

#include <linux/ioctl.h>

/* --------------------------------------------------------------------- */

struct ezusb_old_ctrltransfer {
	/* keep in sync with usb.h:devrequest */
	unsigned char requesttype;
	unsigned char request;
	unsigned short value;
	unsigned short index;
	unsigned short length;
	unsigned int dlen;
	void *data;
};

struct ezusb_old_bulktransfer {
	unsigned int ep;
	unsigned int len;
	void *data;
};

struct ezusb_ctrltransfer {
	/* keep in sync with usb.h:devrequest */
	unsigned char requesttype;
	unsigned char request;
	unsigned short value;
	unsigned short index;
	unsigned short length;
	unsigned int timeout;  /* in milliseconds */
	void *data;
};

struct ezusb_bulktransfer {
	unsigned int ep;
	unsigned int len;
	unsigned int timeout;  /* in milliseconds */
	void *data;
};

struct ezusb_setinterface {
	unsigned int interface;
	unsigned int altsetting;
};

struct ezusb_isoframestat {
	unsigned int length;
	unsigned int status;
};

struct ezusb_asynccompleted {
	int status;
	unsigned length;
	void *context;
	struct ezusb_isoframestat isostat[0];
};

struct ezusb_asyncbulk {
	unsigned int ep;
	unsigned int len;
	void *context;
	void *data;
};

struct ezusb_asynciso {
	unsigned int ep;
	
	unsigned int framecnt;
	unsigned int startframe;

	void *context;
	void *data;
	struct ezusb_isoframestat isostat[0];
};

#define EZUSB_CONTROL           _IOWR('E', 1, struct ezusb_ctrltransfer)
#define EZUSB_BULK              _IOWR('E', 2, struct ezusb_bulktransfer)
#define EZUSB_OLD_CONTROL       _IOWR('E', 0, struct ezusb_old_ctrltransfer)
#define EZUSB_OLD_BULK          _IOWR('E', 2, struct ezusb_old_bulktransfer)
#define EZUSB_RESETEP           _IOR('E', 3, unsigned int)
#define EZUSB_SETINTERFACE      _IOR('E', 4, struct ezusb_setinterface)
#define EZUSB_SETCONFIGURATION  _IOR('E', 5, unsigned int)
#define EZUSB_ASYNCCOMPLETED    _IOW('E', 8, struct ezusb_asynccompleted)
#define EZUSB_ASYNCCOMPLETEDNB  _IOW('E', 9, struct ezusb_asynccompleted)
#define EZUSB_REQUESTBULK       _IOR('E', 16, struct ezusb_asyncbulk)
#define EZUSB_REQUESTISO        _IOR('E', 17, struct ezusb_asynciso)
#define EZUSB_TERMINATEASYNC    _IOR('E', 18, void *)
#define EZUSB_GETFRAMENUMBER    _IOW('E', 18, unsigned int)

/* --------------------------------------------------------------------- */
#endif /* _LINUX_EZUSB_H */
