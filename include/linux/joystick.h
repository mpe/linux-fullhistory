#ifndef _LINUX_JOYSTICK_H
#define _LINUX_JOYSTICK_H

/*
 *  $Id: joystick.h,v 1.3 1998/03/30 11:10:40 mj Exp $
 *
 *  Copyright (C) 1997, 1998 Vojtech Pavlik
 */

#include <asm/types.h>

/*
 * Version
 */

#define JS_VERSION		0x00010007L		/* 1.0.7 BCD */

/*
 * IOCTL commands for joystick driver
 */

#define JSIOCGVERSION		_IOR('j', 0x01, __u32)				/* get driver version */

#define JSIOCGAXES		_IOR('j', 0x11, __u8)				/* get number of axes */
#define JSIOCGBUTTONS		_IOR('j', 0x12, __u8)				/* get number of buttons */

#define JSIOCSCORR		_IOW('j', 0x21, struct js_corr[4])		/* set correction values */
#define JSIOCGCORR		_IOR('j', 0x22, struct js_corr[4])		/* get correction values */

/*
 * Types and constants for get/set correction
 */

#define JS_CORR_NONE		0x00		/* returns raw values */
#define JS_CORR_BROKEN		0x01		/* broken line */

struct js_corr {
	__s32 coef[8];
	__u16 prec;
	__u16 type;
};

/*
 * Types and constants for reading from /dev/js
 */

#define JS_EVENT_BUTTON		0x01	/* button pressed/released */
#define JS_EVENT_AXIS		0x02	/* joystick moved */
#define JS_EVENT_INIT		0x80	/* initial state of device */

struct js_event {
        __u32 time;		/* time when event happened in miliseconds since open */
        __u16 value;		/* new value */
        __u8  type;		/* type of event, see above */
        __u8  number;		/* axis/button number */
};

/*
 * Backward (version 0.x) compatibility definitions
 */

#define JS_RETURN 	sizeof(struct JS_DATA_TYPE)
#define JS_TRUE 	1
#define JS_FALSE 	0
#define JS_X_0		0x01		/* bit mask for x-axis js0 */
#define JS_Y_0		0x02		/* bit mask for y-axis js0 */
#define JS_X_1		0x04		/* bit mask for x-axis js1 */
#define JS_Y_1		0x08		/* bit mask for y-axis js1 */
#define JS_MAX 		2		/* max number of joysticks */

struct JS_DATA_TYPE {
	int buttons;		/* immediate button state */
	int x;                  /* immediate x axis value */
	int y;                  /* immediate y axis value */
};


extern int js_init(void);

#endif /* _LINUX_JOYSTICK_H */
