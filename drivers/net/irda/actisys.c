/*********************************************************************
 *                
 * Filename:      actisys.c
 * Version:       0.4
 * Description:   Implementation for the ACTiSYS IR-220L and IR-220L+ 
 *                dongles
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Oct 21 20:02:35 1998
 * Modified at:   Mon Jan 18 11:30:25 1999
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

static void actisys_reset( struct irda_device *dev, int unused);
static void actisys_open( struct irda_device *idev, int type);
static void actisys_close( struct irda_device *dev);
static void actisys_change_speed( struct irda_device *dev, int baudrate);
static void actisys_reset( struct irda_device *dev, int unused);
static void actisys_init_qos( struct irda_device *idev, struct qos_info *qos);

/* These are the baudrates supported */
static int baud_rates[] = { 9600, 19200, 57600, 115200, 38400};

static struct dongle dongle = {
	ACTISYS_DONGLE,
	actisys_open,
	actisys_close,
	actisys_reset,
	actisys_change_speed,
	actisys_init_qos,
};

__initfunc(void actisys_init(void))
{
	irtty_register_dongle(&dongle);
}

void actisys_cleanup(void)
{
	irtty_unregister_dongle(&dongle);
}

static void actisys_open( struct irda_device *idev, int type)
{
	strcat( idev->name, " <-> actisys");

        idev->io.dongle_id = type;

	MOD_INC_USE_COUNT;
}

static void actisys_close( struct irda_device *dev)
{
	MOD_DEC_USE_COUNT;
}

/*
 * Function actisys_change_speed (tty, baud)
 *
 *    Change speed of the ACTiSYS IR-220L and IR-220L+ type IrDA dongles.  
 *    To cycle through the available baud rates, pulse RTS low for a few ms.
 *    To be compatible with the new IR-220L+, we have to reset the dongle 
 *    first since its not possible cycle around anymore and still be 
 *    compatible with both dongles :-(
 */
static void actisys_change_speed( struct irda_device *idev, int baudrate)
{
        struct irtty_cb *self;
        struct tty_struct *tty;
        int arg;
        struct termios old_termios;
	int cflag;
        int current_baudrate;
        int index = 0;
	mm_segment_t fs;
	
	DEBUG( 0, __FUNCTION__ "()\n");

	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	self = (struct irtty_cb *) idev->priv;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);

	current_baudrate = idev->qos.baud_rate.value;

	/* Find the correct baudrate index for the currently used baudrate */
	while ( current_baudrate != baud_rates[index])
		index++;

        DEBUG( 0, __FUNCTION__ "(), index=%d\n", index);

	if ( !self->tty)
		return;

	tty = self->tty;

	/* Cycle through avaiable baudrates until we reach the correct one */
        while ( current_baudrate != baudrate) {	
                DEBUG( 0, __FUNCTION__ "(), current baudrate = %d\n",
                       baud_rates[index]);
		DEBUG( 0, __FUNCTION__ "(), Clearing RTS\n");
		
		/* Set DTR, clear RTS */
		arg = TIOCM_DTR|TIOCM_OUT2;
		
		fs = get_fs();
		set_fs( get_ds());
		
		if ( tty->driver.ioctl( tty, NULL, TIOCMSET, 
					(unsigned long) &arg)) { 
			DEBUG( 0, __FUNCTION__ 
			       "Error clearing RTS!\n");
		}
		set_fs(fs);
		
		/* Wait at a few ms */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(2);

		/* Set DTR, Set RTS */
                arg = TIOCM_DTR | TIOCM_RTS |TIOCM_OUT2;

		fs = get_fs();
		set_fs( get_ds());
		
		if ( tty->driver.ioctl( tty, NULL, TIOCMSET, 
					(unsigned long) &arg)) { 
			DEBUG( 0, __FUNCTION__  "Error setting RTS!\n");
		}
		set_fs(fs);
		
		/* Wait at a few ms again */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout( 2);

                /* Go to next baudrate */
		if ( idev->io.dongle_id == ACTISYS_DONGLE)
                        index = (index+1) % 4; /* IR-220L */
		else
                        index = (index+1) % 5; /* IR-220L+ */

                current_baudrate = baud_rates[index];
        }
	DEBUG( 0, __FUNCTION__ "(), current baudrate = %d\n", 
                  baud_rates[index]);

	/* Now change the speed of the serial port */
	old_termios = *(tty->termios);
	cflag = tty->termios->c_cflag;

	cflag &= ~CBAUD;

        switch ( baudrate) {
        case 9600:
        default:
		cflag |= B9600;
		break;
	case 19200:
		cflag |= B19200;
		break;
	case 38400:
		cflag |= B38400;
		break;
	case 57600:
		cflag |= B57600;
		break;
	case 115200:
		cflag |= B115200;
		break;
	}

	tty->termios->c_cflag = cflag;

	DEBUG( 0, __FUNCTION__ "(), Setting the speed of the serial port\n");
	tty->driver.set_termios( tty, &old_termios);
}

/*
 * Function actisys_reset (dev)
 *
 *      Reset the Actisys type dongle. Warning, this function must only be
 *      called with a process context!
 *
 *    	1. Clear DTR for a few ms.
 *
 */
static void actisys_reset( struct irda_device *idev, int unused)
{
	struct irtty_cb *self;
        struct tty_struct *tty;
        int arg = 0;
	mm_segment_t fs;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	self = (struct irtty_cb *) idev->priv;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);

	tty = self->tty;
	if ( !tty)
		return;

	DEBUG( 0, __FUNCTION__ "(), Clearing DTR\n");
	arg = TIOCM_RTS | TIOCM_OUT2;

	fs = get_fs();
	set_fs( get_ds());
	
	if ( tty->driver.ioctl( tty, NULL, TIOCMSET, 
				(unsigned long) &arg)) 
	{ 
		DEBUG( 0, __FUNCTION__"(), ioctl error!\n");
	}
	set_fs(fs);

	/* Sleep 10-20 ms*/
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(2);
	
	DEBUG( 0, __FUNCTION__ "(), Setting DTR\n");
	arg = TIOCM_RTS | TIOCM_DTR | TIOCM_OUT2;
	
	fs = get_fs();
	set_fs( get_ds());
	
	if ( tty->driver.ioctl( tty, NULL, TIOCMSET, 
				(unsigned long) &arg)) 
	{ 
		DEBUG( 0, __FUNCTION__"(), ioctl error!\n");
	}
	set_fs(fs);

	idev->qos.baud_rate.value = 9600;
}

/*
 * Function actisys_init_qos (qos)
 *
 *    Initialize QoS capabilities
 *
 */
static void actisys_init_qos( struct irda_device *idev, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;

	/* Remove support for 38400 if this is not a 220L+ dongle */
	if ( idev->io.dongle_id == ACTISYS_DONGLE)
		qos->baud_rate.bits &= ~IR_38400;
		
	qos->min_turn_time.bits &= 0xfe; /* All except 0 ms */
}

#ifdef MODULE
		
/*
 * Function init_module (void)
 *
 *    Initialize Actisys module
 *
 */
int init_module(void)
{
	actisys_init();
	return(0);
}

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Actisys module
 *
 */
void cleanup_module(void)
{
	actisys_cleanup();
}

#endif
