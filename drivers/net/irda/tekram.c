/*********************************************************************
 *                
 * Filename:      tekram.c
 * Version:       1.0
 * Description:   Implementation of the Tekram IrMate IR-210B dongle
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Oct 21 20:02:35 1998
 * Modified at:   Tue Apr 13 16:33:54 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli, All Rights Reserved.
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
#include <net/irda/irda_device.h>
#include <net/irda/irtty.h>
#include <net/irda/dongle.h>

static void tekram_reset(struct irda_device *dev, int unused);
static void tekram_open(struct irda_device *dev, int type);
static void tekram_close(struct irda_device *dev);
static void tekram_change_speed(struct irda_device *dev, int baud);
static void tekram_init_qos(struct irda_device *idev, struct qos_info *qos);

#define TEKRAM_115200 0x00
#define TEKRAM_57600  0x01
#define TEKRAM_38400  0x02
#define TEKRAM_19200  0x03
#define TEKRAM_9600   0x04

#define TEKRAM_PW 0x10 /* Pulse select bit */

static struct dongle dongle = {
	TEKRAM_DONGLE,
	tekram_open,
	tekram_close,
	tekram_reset,
	tekram_change_speed,
	tekram_init_qos,
};

__initfunc(int tekram_init(void))
{
	return irtty_register_dongle(&dongle);
}

void tekram_cleanup(void)
{
	irtty_unregister_dongle( &dongle);
}

static void tekram_open( struct irda_device *idev, int type)
{
	strcat(idev->description, " <-> tekram");

	idev->io.dongle_id = type;
	idev->flags |= IFF_DONGLE;
	
	MOD_INC_USE_COUNT;
}

static void tekram_close( struct irda_device *dev)
{
	MOD_DEC_USE_COUNT;
}

/*
 * Function tekram_change_speed (tty, baud)
 *
 *    Set the speed for the Tekram IRMate 210 type dongle. Warning, this 
 *    function must be called with a process context!
 *
 *    Algorithm
 *    1. clear DTR 
 *    2. set RTS, and wait at least 7 us
 *    3. send Control Byte to the IR-210 through TXD to set new baud rate
 *       wait until the stop bit of Control Byte is sent (for 9600 baud rate, 
 *       it takes about 100 msec)
 *    5. clear RTS (return to NORMAL Operation)
 *    6. wait at least 50 us, new setting (baud rate, etc) takes effect here 
 *       after
 */
static void tekram_change_speed( struct irda_device *dev, int baud)
{
	struct irtty_cb *self;
	struct tty_struct *tty;
	struct termios old_termios;
	int cflag;
	__u8 byte;
	
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(dev != NULL, return;);
	ASSERT(dev->magic == IRDA_DEVICE_MAGIC, return;);
	
	self = (struct irtty_cb *) dev->priv;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRTTY_MAGIC, return;);
	
	if (!self->tty)
		return;

	tty = self->tty;
	
	old_termios = *(tty->termios);
	cflag = tty->termios->c_cflag;

	cflag &= ~CBAUD;

	switch (baud) {
	default:
		/* FALLTHROUGH */
	case 9600:
		cflag |= B9600;
		byte = TEKRAM_PW|TEKRAM_9600;
		break;
	case 19200:
		cflag |= B19200;
		byte = TEKRAM_PW|TEKRAM_19200;
		break;
	case 34800:
		cflag |= B38400;
		byte = TEKRAM_PW|TEKRAM_38400;
		break;
	case 57600:
		cflag |= B57600;
		byte = TEKRAM_PW|TEKRAM_57600;
		break;
	case 115200:
		cflag |= B115200;
		byte = TEKRAM_PW|TEKRAM_115200;
		break;
	}

	/* Set DTR, Clear RTS */
	irtty_set_dtr_rts(tty, TRUE, FALSE);
	
	/* Wait at least 7us */
	udelay(7);

	/* Write control byte */
	if (tty->driver.write)
		tty->driver.write(self->tty, 0, &byte, 1);
	
	/* Wait at least 100 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(100));
        
	/* Set DTR, Set RTS */
	irtty_set_dtr_rts(tty, TRUE, TRUE);

	/* Now change the speed of the serial port */
	tty->termios->c_cflag = cflag;
	tty->driver.set_termios(tty, &old_termios);	
}

/*
 * Function tekram_reset (driver)
 *
 *      This function resets the tekram dongle. Warning, this function 
 *      must be called with a process context!! 
 *
 *      Algorithm:
 *    	  0. Clear RTS and DTR, and wait 50 ms (power off the IR-210 )
 *        1. clear RTS 
 *        2. set DTR, and wait at least 1 ms 
 *        3. clear DTR to SPACE state, wait at least 50 us for further 
 *         operation
 */
void tekram_reset(struct irda_device *dev, int unused)
{
	struct irtty_cb *self;
	struct tty_struct *tty;

	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(dev != NULL, return;);
	ASSERT(dev->magic == IRDA_DEVICE_MAGIC, return;);
	
	self = (struct irtty_cb *) dev->priv;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRTTY_MAGIC, return;);

	tty = self->tty;
	if (!tty)
		return;

	/* Power off dongle */
	irtty_set_dtr_rts(tty, FALSE, FALSE);

	/* Sleep 50 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(50));

	/* Clear DTR, Set RTS */
	irtty_set_dtr_rts(tty, FALSE, TRUE); 

	/* Should sleep 1 ms, but 10-20 should not do any harm */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));

	/* Set DTR, Set RTS */
	irtty_set_dtr_rts(tty, TRUE, TRUE);
	
	udelay(50);

	/* Finished! */
}

/*
 * Function tekram_init_qos (qos)
 *
 *    Initialize QoS capabilities
 *
 */
static void tekram_init_qos(struct irda_device *idev, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits &= 0x01; /* Needs at least 10 ms */
	irda_qos_bits_to_value(qos);
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Tekram IrMate IR-210B dongle driver");
		
/*
 * Function init_module (void)
 *
 *    Initialize Tekram module
 *
 */
int init_module(void)
{
	return tekram_init();
}

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Tekram module
 *
 */
void cleanup_module(void)
{
        tekram_cleanup();
}

#endif /* MODULE */
