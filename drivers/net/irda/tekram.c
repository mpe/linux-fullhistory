/*********************************************************************
 *                
 * Filename:      tekram.c
 * Version:       0.4
 * Description:   Implementation of the Tekram IrMate IR-210B dongle
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Oct 21 20:02:35 1998
 * Modified at:   Mon Jan 18 11:30:38 1999
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
#include <net/irda/irmod.h>
#include <net/irda/irda_device.h>
#include <net/irda/irtty.h>
#include <net/irda/dongle.h>

static void tekram_reset( struct irda_device *dev, int unused);
static void tekram_open( struct irda_device *dev, int type);
static void tekram_close( struct irda_device *dev);
static void tekram_change_speed( struct irda_device *dev, int baud);
static void tekram_init_qos( struct irda_device *idev, struct qos_info *qos);

static struct dongle dongle = {
	TEKRAM_DONGLE,
	tekram_open,
	tekram_close,
	tekram_reset,
	tekram_change_speed,
	tekram_init_qos,
};

__initfunc(void tekram_init(void))
{
	irtty_register_dongle( &dongle);
}

void tekram_cleanup(void)
{
	irtty_unregister_dongle( &dongle);
}

static void tekram_open( struct irda_device *dev, int type)
{
	strcat( dev->name, " <-> tekram");
	
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
	int arg = 0;
	int cflag;
	__u8 byte;
	int actual;
	mm_segment_t fs;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( dev != NULL, return;);
	ASSERT( dev->magic == IRDA_DEVICE_MAGIC, return;);
	
	self = (struct irtty_cb *) dev->priv;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);
	
	if ( !self->tty)
		return;

	tty = self->tty;
	
	old_termios = *(tty->termios);
	cflag = tty->termios->c_cflag;

	cflag &= ~CBAUD;

	switch (baud) {
	case 9600:
	default:
		cflag |= B9600;
		byte = 4;
		break;
	case 19200:
		cflag |= B19200;
		byte = 3;
		break;
	case 34800:
		cflag |= B38400;
		byte = 2;
		break;
	case 57600:
		cflag |= B57600;
		byte = 1;
		break;
	case 115200:
		cflag |= B115200;
		byte = 0;
		break;
	}

	/* Set DTR, Clear RTS */
	DEBUG( 0, __FUNCTION__ "(), Setting DTR, Clearing RTS\n");
	arg = TIOCM_DTR | TIOCM_OUT2;
	
	fs = get_fs();
	set_fs( get_ds());
	
	if ( tty->driver.ioctl( tty, NULL, TIOCMSET, 
                                (unsigned long) &arg)) { 
		DEBUG( 0, "error setting Tekram speed!\n");
	}
	set_fs(fs);
	
	/* Wait at least 7us */
	udelay( 7);

	DEBUG( 0, __FUNCTION__ "(), Writing control byte\n");
	/* Write control byte */
	if ( tty->driver.write)
		actual = tty->driver.write( self->tty, 0, &byte, 1);
	
	/* Wait at least 100 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout( 10);
        
	/* Set DTR, Set RTS */
	DEBUG( 0, __FUNCTION__ "(), Setting DTR, Setting RTS\n");
	arg = TIOCM_DTR | TIOCM_RTS | TIOCM_OUT2;
	
	fs = get_fs();
	set_fs( get_ds());
	
	if ( tty->driver.ioctl( tty, NULL, TIOCMSET, 
				(unsigned long) &arg)) { 
		DEBUG( 0, "error setting Tekram speed!\n");
	}
	set_fs(fs);

	DEBUG( 0, __FUNCTION__ "(), Setting new speed on serial port\n");
	/* Now change the speed of the serial port */
	tty->termios->c_cflag = cflag;
	tty->driver.set_termios( tty, &old_termios);	
}

/*
 * Function tekram_reset (driver)
 *
 *      This function resets the tekram dongle. Warning, this function 
 *      must be called with a process context!! 
 *
 *      Algorithm:
 *    	  0. set RTS and DTR, and wait 50 ms 
 *           ( power off the IR-210 )
 *        1. clear RTS 
 *        2. set DTR, and wait at least 1 ms 
 *        3. clear DTR to SPACE state, wait at least 50 us for further 
 *         operation
 */
void tekram_reset( struct irda_device *dev, int unused)
{
	struct irtty_cb *self;
	struct tty_struct *tty;
	int arg = 0;
	mm_segment_t fs;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( dev != NULL, return;);
	ASSERT( dev->magic == IRDA_DEVICE_MAGIC, return;);
	
	self = (struct irtty_cb *) dev->priv;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);

	tty = self->tty;
	if ( !tty)
		return;

	DEBUG( 0, __FUNCTION__ "(), Power off dongle\n");
	arg = TIOCM_RTS | TIOCM_DTR | TIOCM_OUT2;
	
	fs = get_fs();
	set_fs( get_ds());
	
	if ( tty->driver.ioctl( tty, NULL, TIOCMSET, 
				(unsigned long) &arg)) 
	{ 
		DEBUG(0, "error setting ESI speed!\n");
	}
	set_fs(fs);

	/* Sleep 50 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(5);
	
	DEBUG( 0, __FUNCTION__ "(), Set DTR, clear RTS\n");
	/* Set DTR, clear RTS */
	arg = TIOCM_DTR | TIOCM_OUT2;
	
	fs = get_fs();
	set_fs( get_ds());
	
	if ( tty->driver.ioctl( tty, NULL, TIOCMSET, 
				(unsigned long) &arg)) { 
		DEBUG( 0, "Error setting Tekram speed!\n");
	}
	set_fs(fs);

	/* Should sleep 1 ms, but 10-20 should not do any harm */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(2);

	DEBUG( 0, __FUNCTION__ "(), STATE3\n");
	/* Clear DTR, clear RTS */
	arg = TIOCM_OUT2;

	fs = get_fs();
	set_fs( get_ds());
	
	if ( tty->driver.ioctl( tty, NULL, TIOCMSET, (unsigned long) &arg)) { 
		DEBUG( 0, "error setting Tekram speed!\n");
	}
	set_fs(fs);
	
	/* Finished! */
}

/*
 * Function tekram_init_qos (qos)
 *
 *    Initialize QoS capabilities
 *
 */
static void tekram_init_qos( struct irda_device *idev, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits &= 0xfe; /* All except 0 ms */
}

#ifdef MODULE
		
/*
 * Function init_module (void)
 *
 *    Initialize Tekram module
 *
 */
int init_module(void)
{
	tekram_init();
	return(0);
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

#endif
