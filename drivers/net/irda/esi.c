/*********************************************************************
 *                
 * Filename:      esi.c
 * Version:       1.1
 * Description:   Driver for the Extended Systems JetEye PC
 * Status:        Experimental.
 * Author:        Thomas Davis, <ratbert@radiks.net>
 * Created at:    Sat Feb 21 18:54:38 1998
 * Modified at:   Mon Jan 18 11:30:32 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:	  esi.c
 *
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>,
 *     Copyright (c) 1998, Dag Brattli,  <dagb@cs.uit.no>
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     I, Thomas Davis, provide no warranty for any of this software.
 *     This material is provided "AS-IS" and at no charge.
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

static void esi_open( struct irda_device *idev, int type);
static void esi_close( struct irda_device *driver);
static void esi_change_speed( struct irda_device *idev, int baud);
static void esi_reset( struct irda_device *idev, int unused);
static void esi_qos_init( struct irda_device *idev, struct qos_info *qos);

static struct dongle dongle = {
	ESI_DONGLE,
	esi_open,
	esi_close,
	esi_reset,
	esi_change_speed,
	esi_qos_init,
};

__initfunc(void esi_init(void))
{
	irtty_register_dongle( &dongle);
}

void esi_cleanup(void)
{
	irtty_unregister_dongle( &dongle);
}

static void esi_open( struct irda_device *idev, int type)
{
	strcat( idev->description, " <-> esi");

	idev->io.dongle_id = type;

	MOD_INC_USE_COUNT;
}

static void esi_close( struct irda_device *driver)
{
	MOD_DEC_USE_COUNT;
}

/*
 * Function esi_change_speed (tty, baud)
 *
 *    Set the speed for the Extended Systems JetEye PC ESI-9680 type dongle
 *
 */
static void esi_change_speed( struct irda_device *idev, int baud)
{
	struct irtty_cb *self;
	struct tty_struct *tty;
	int arg = TIOCM_OUT2;
        struct termios old_termios;
	int cflag;
	mm_segment_t fs;
	
	ASSERT( idev != NULL, return;);
	ASSERT( idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	self = (struct irtty_cb *) idev->priv;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRTTY_MAGIC, return;);

	if ( !self->tty)
		return;

	tty = self->tty;
	
	old_termios = *(tty->termios);
	cflag = tty->termios->c_cflag;

	cflag &= ~CBAUD;

	switch (baud) {
	case 19200:
		cflag |= B19200;
		arg |= TIOCM_DTR;
		break;
	case 115200:
		cflag |= B115200;
		arg |= TIOCM_RTS | TIOCM_DTR;
		break;
	case 9600:
	default:
		cflag |= B9600;
		arg |= TIOCM_RTS;
		break;
	}
		
	tty->termios->c_cflag = cflag;
	tty->driver.set_termios( tty, &old_termios);

	/*
	 *  The ioctl function, or actually set_modem_info in serial.c
	 *  expects a pointer to the argument in user space. To hack us
	 *  around this we use the set_fs function to fool the routines 
	 *  that check if they are called from user space. We also need 
	 *  to send a pointer to the argument so get_user() gets happy. 
	 *  DB.
	 */
	fs = get_fs();
	set_fs( get_ds());

	if ( tty->driver.ioctl( tty, NULL, TIOCMSET, (unsigned long) &arg)) { 
		DEBUG( 0, __FUNCTION__ "(), error setting ESI speed!\n");
	}
	set_fs(fs);
}

static void esi_reset( struct irda_device *idev, int unused)
{
	/* Empty */
}

/*
 * Function esi_qos_init (qos)
 *
 *    Init QoS capabilities for the dongle
 *
 */
static void esi_qos_init( struct irda_device *idev, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_115200;
}

#ifdef MODULE
		
/*
 * Function init_module (void)
 *
 *    Initialize ESI module
 *
 */
int init_module(void)
{
	esi_init();
	return(0);
}

/*
 * Function cleanup_module (void)
 *
 *    Cleanup ESI module
 *
 */
void cleanup_module(void)
{
        esi_cleanup();
}

#endif

