/*********************************************************************
 *                
 * Filename:      esi.c
 * Version:       1.4
 * Description:   Driver for the Extended Systems JetEye PC dongle
 * Status:        Experimental.
 * Author:        Thomas Davis, <ratbert@radiks.net>
 * Created at:    Sat Feb 21 18:54:38 1998
 * Modified at:   Mon May 10 15:13:12 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:	  esi.c
 *
 *     Copyright (c) 1998-1999, Dag Brattli, <dagb@cs.uit.no>
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>,
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

static void esi_open(struct irda_device *idev, int type);
static void esi_close(struct irda_device *driver);
static void esi_change_speed(struct irda_device *idev, int baud);
static void esi_reset(struct irda_device *idev, int unused);
static void esi_qos_init(struct irda_device *idev, struct qos_info *qos);

static struct dongle dongle = {
	ESI_DONGLE,
	esi_open,
	esi_close,
	esi_reset,
	esi_change_speed,
	esi_qos_init,
};

__initfunc(int esi_init(void))
{
	return irda_device_register_dongle(&dongle);
}

void esi_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

static void esi_open(struct irda_device *idev, int type)
{
	strcat(idev->description, " <-> esi");

	idev->io.dongle_id = type;
	idev->flags |= IFF_DONGLE;

	MOD_INC_USE_COUNT;
}

static void esi_close(struct irda_device *idev)
{		
	/* Power off dongle */
	irda_device_set_dtr_rts(idev, FALSE, FALSE);

	MOD_DEC_USE_COUNT;
}

/*
 * Function esi_change_speed (tty, baud)
 *
 *    Set the speed for the Extended Systems JetEye PC ESI-9680 type dongle
 *
 */
static void esi_change_speed(struct irda_device *idev, int baud)
{
	int dtr, rts;
	
	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	switch (baud) {
	case 19200:
		dtr = TRUE;
		rts = FALSE;
		break;
	case 115200:
		dtr = rts = TRUE;
		break;
	case 9600:
	default:
		dtr = FALSE;
		rts = TRUE;
		break;
	}

	/* Change speed of dongle */
	irda_device_set_dtr_rts(idev, dtr, rts);
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
static void esi_qos_init(struct irda_device *idev, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_115200;
	qos->min_turn_time.bits &= 0x01; /* Needs at least 10 ms */
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Extended Systems JetEye PC dongle driver");

/*
 * Function init_module (void)
 *
 *    Initialize ESI module
 *
 */
int init_module(void)
{
	return esi_init();
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

