/*********************************************************************
 *                
 * Filename:      girbil.c
 * Version:       1.0
 * Description:   Implementation for the Greenwich GIrBIL dongle
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Feb  6 21:02:33 1999
 * Modified at:   Sat Apr 10 19:53:12 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/ioctls.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irda_device.h>
#include <net/irda/irtty.h>
#include <net/irda/dongle.h>

static void girbil_reset(struct irda_device *dev, int unused);
static void girbil_open(struct irda_device *dev, int type);
static void girbil_close(struct irda_device *dev);
static void girbil_change_speed(struct irda_device *dev, int baud);
static void girbil_init_qos(struct irda_device *idev, struct qos_info *qos);

/* Control register 1 */
#define GIRBIL_TXEN    0x01 /* Enable transmitter */
#define GIRBIL_RXEN    0x02 /* Enable receiver */
#define GIRBIL_ECAN    0x04 /* Cancel self emmited data */
#define GIRBIL_ECHO    0x08 /* Echo control characters */

/* LED Current Register (0x2) */
#define GIRBIL_HIGH    0x20
#define GIRBIL_MEDIUM  0x21
#define GIRBIL_LOW     0x22

/* Baud register (0x3) */
#define GIRBIL_2400    0x30
#define GIRBIL_4800    0x31	
#define GIRBIL_9600    0x32
#define GIRBIL_19200   0x33
#define GIRBIL_38400   0x34	
#define GIRBIL_57600   0x35	
#define GIRBIL_115200  0x36

/* Mode register (0x4) */
#define GIRBIL_IRDA    0x40
#define GIRBIL_ASK     0x41

/* Control register 2 (0x5) */
#define GIRBIL_LOAD    0x51 /* Load the new baud rate value */

static struct dongle dongle = {
	GIRBIL_DONGLE,
	girbil_open,
	girbil_close,
	girbil_reset,
	girbil_change_speed,
	girbil_init_qos,
};

__initfunc(void girbil_init(void))
{
	irtty_register_dongle(&dongle);
}

void girbil_cleanup(void)
{
	irtty_unregister_dongle(&dongle);
}

static void girbil_open(struct irda_device *idev, int type)
{
	strcat( idev->description, " <-> girbil");

	idev->io.dongle_id = type;
	idev->flags |= IFF_DONGLE;
	
	MOD_INC_USE_COUNT;
}

static void girbil_close(struct irda_device *dev)
{
	MOD_DEC_USE_COUNT;
}

/*
 * Function girbil_change_speed (dev, speed)
 *
 *    Set the speed for the Girbil type dongle. Warning, this 
 *    function must be called with a process context!
 *
 */
static void girbil_change_speed(struct irda_device *idev, int speed)
{
	struct irtty_cb *self;
	struct tty_struct *tty;
	struct termios old_termios;
	int cflag;
	__u8 control[2];
	
	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	self = (struct irtty_cb *) idev->priv;
	
	ASSERT(self != NULL, return;); 
	ASSERT(self->magic == IRTTY_MAGIC, return;);
	
	if (!self->tty)
		return;

	tty = self->tty;
	
	old_termios = *(tty->termios);
	cflag = tty->termios->c_cflag;

	cflag &= ~CBAUD;

	switch (speed) {
	case 9600:
	default:
		cflag |= B9600;
		control[0] = GIRBIL_9600;
		break;
	case 19200:
		cflag |= B19200;
		control[0] = GIRBIL_19200;
		break;
	case 34800:
		cflag |= B38400;
		control[0] = GIRBIL_38400;
		break;
	case 57600:
		cflag |= B57600;
		control[0] = GIRBIL_57600;
		break;
	case 115200:
		cflag |= B115200;
		control[0] = GIRBIL_115200;
		break;
	}
	control[1] = GIRBIL_LOAD;

	/* Set DTR and Clear RTS to enter command mode */
	irtty_set_dtr_rts(tty, FALSE, TRUE);

	/* Write control bytes */
	if (tty->driver.write)
		tty->driver.write(self->tty, 0, control, 2);

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(2);
	
	/* Go back to normal mode */
	irtty_set_dtr_rts(tty, TRUE, TRUE);

	/* Now change the speed of the serial port */
	tty->termios->c_cflag = cflag;
	tty->driver.set_termios(tty, &old_termios);	
}

/*
 * Function girbil_reset (driver)
 *
 *      This function resets the girbil dongle. Warning, this function 
 *      must be called with a process context!! 
 *
 *      Algorithm:
 *    	  0. set RTS, and wait at least 5 ms 
 *        1. clear RTS 
 */
void girbil_reset(struct irda_device *idev, int unused)
{
	struct irtty_cb *self;
	struct tty_struct *tty;
	__u8 control = GIRBIL_TXEN | GIRBIL_RXEN;

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	self = (struct irtty_cb *) idev->priv;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRTTY_MAGIC, return;);

	tty = self->tty;
	if (!tty)
		return;

	/* Reset dongle */
	irtty_set_dtr_rts(tty, TRUE, FALSE);

	/* Sleep at least 5 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(2);
	
	/* Set DTR and clear RTS to enter command mode */
	irtty_set_dtr_rts(tty, FALSE, TRUE);

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(2);

	/* Write control byte */
	if (tty->driver.write)
		tty->driver.write(self->tty, 0, &control, 1);

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(2);

	/* Go back to normal mode */
	irtty_set_dtr_rts(tty, TRUE, TRUE);
}

/*
 * Function girbil_init_qos (qos)
 *
 *    Initialize QoS capabilities
 *
 */
static void girbil_init_qos(struct irda_device *idev, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits &= 0xfe; /* All except 0 ms */
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Greenwich GIrBIL dongle driver");
	
/*
 * Function init_module (void)
 *
 *    Initialize Girbil module
 *
 */
int init_module(void)
{
	girbil_init();
	return(0);
}

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Girbil module
 *
 */
void cleanup_module(void)
{
        girbil_cleanup();
}

#endif /* MODULE */
