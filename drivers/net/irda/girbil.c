/*********************************************************************
 *                
 * Filename:      girbil.c
 * Version:       1.1
 * Description:   Implementation for the Greenwich GIrBIL dongle
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Feb  6 21:02:33 1999
 * Modified at:   Tue Jun  1 08:47:41 1999
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

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irda_device.h>
#include <net/irda/irtty.h>
#include <net/irda/dongle.h>

static void girbil_reset(struct irda_device *dev);
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

int __init girbil_init(void)
{
	return irda_device_register_dongle(&dongle);
}

void girbil_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

static void girbil_open(struct irda_device *idev, int type)
{
	strcat(idev->description, " <-> girbil");

	idev->io.dongle_id = type;
	idev->flags |= IFF_DONGLE;
	
	MOD_INC_USE_COUNT;
}

static void girbil_close(struct irda_device *idev)
{
	/* Power off dongle */
	irda_device_set_dtr_rts(idev, FALSE, FALSE);

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
	__u8 control[2];
	
	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	switch (speed) {
	case 9600:
	default:
		control[0] = GIRBIL_9600;
		break;
	case 19200:
		control[0] = GIRBIL_19200;
		break;
	case 34800:
		control[0] = GIRBIL_38400;
		break;
	case 57600:
		control[0] = GIRBIL_57600;
		break;
	case 115200:
		control[0] = GIRBIL_115200;
		break;
	}
	control[1] = GIRBIL_LOAD;

	/* Set DTR and Clear RTS to enter command mode */
	irda_device_set_dtr_rts(idev, FALSE, TRUE);

	/* Write control bytes */
	irda_device_raw_write(idev, control, 2);

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(2);
	
	/* Go back to normal mode */
	irda_device_set_dtr_rts(idev, TRUE, TRUE);
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
void girbil_reset(struct irda_device *idev)
{
	__u8 control = GIRBIL_TXEN | GIRBIL_RXEN;

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	/* Reset dongle */
	irda_device_set_dtr_rts(idev, TRUE, FALSE);

	/* Sleep at least 5 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));
	
	/* Set DTR and clear RTS to enter command mode */
	irda_device_set_dtr_rts(idev, FALSE, TRUE);

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));

	/* Write control byte */
	irda_device_raw_write(idev, &control, 1);

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));

	/* Go back to normal mode */
	irda_device_set_dtr_rts(idev, TRUE, TRUE);

       	/* Make sure the IrDA chip also goes to defalt speed */
	if (idev->change_speed)
		idev->change_speed(idev, 9600);
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
	qos->min_turn_time.bits &= 0x03;
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
	return girbil_init();
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
