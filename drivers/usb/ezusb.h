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

struct ezusb_ctrltransfer {
	/* keep in sync with usb.h:devrequest */
	unsigned char requesttype;
	unsigned char request;
	unsigned short value;
	unsigned short index;
	unsigned short length;
	/* pointer to data */
	unsigned dlen;
	void *data;
};

#define EZUSB_CONTROL        _IOWR('E', 0, struct ezusb_ctrltransfer)

struct ezusb_bulktransfer {
	unsigned int ep;
	unsigned int len;
	void *data;
};

#define EZUSB_BULK           _IOWR('E', 2, struct ezusb_bulktransfer)

#define EZUSB_RESETEP        _IOR('E', 3, unsigned int)

struct ezusb_setinterface {
	unsigned int interface;
	unsigned int altsetting;
};

#define EZUSB_SETINTERFACE   _IOR('E', 4, struct ezusb_setinterface)

struct ezusb_isotransfer {
	unsigned int ep;
	unsigned int pktsz;
	unsigned int framesperint;
};

struct ezusb_isodata {
	unsigned int ep;
	unsigned int size;
	unsigned int bufqueued;
	unsigned int buffree;
	void *data;
};

#define EZUSB_STARTISO       _IOR('E', 8, struct ezusb_isotransfer)
#define EZUSB_STOPISO        _IOR('E', 9, unsigned int)
#define EZUSB_ISODATA        _IOWR('E', 10, struct ezusb_isodata)
#define EZUSB_PAUSEISO       _IOR('E', 11, unsigned int)
#define EZUSB_RESUMEISO      _IOR('E', 12, unsigned int)

/* --------------------------------------------------------------------- */
#endif /* _LINUX_EZUSB_H */
