/*********************************************************************
 *                
 * Filename:      actisys.c
 * Version:       0.8
 * Description:   Implementation for the ACTiSYS IR-220L and IR-220L+ 
 *                dongles
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Oct 21 20:02:35 1998
 * Modified at:   Mon May 10 15:12:54 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
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
#include <net/irda/dongle.h>

static void actisys_reset(struct irda_device *dev, int unused);
static void actisys_open(struct irda_device *idev, int type);
static void actisys_close(struct irda_device *dev);
static void actisys_change_speed( struct irda_device *dev, int baudrate);
static void actisys_reset(struct irda_device *dev, int unused);
static void actisys_init_qos(struct irda_device *idev, struct qos_info *qos);

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

static struct dongle dongle_plus = {
	ACTISYS_PLUS_DONGLE,
	actisys_open,
	actisys_close,
	actisys_reset,
	actisys_change_speed,
	actisys_init_qos,
};

__initfunc(int actisys_init(void))
{
	int ret;

	ret = irda_device_register_dongle(&dongle);
	if (ret < 0)
		return ret;
	ret = irda_device_register_dongle(&dongle_plus);
	if (ret < 0) {
		irda_device_unregister_dongle(&dongle);
		return ret;
	}	
	return 0;
}

void actisys_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
	irda_device_unregister_dongle(&dongle_plus);
}

static void actisys_open(struct irda_device *idev, int type)
{
	strcat(idev->description, " <-> actisys");

        idev->io.dongle_id = type;
	idev->flags |= IFF_DONGLE;

	MOD_INC_USE_COUNT;
}

static void actisys_close(struct irda_device *idev)
{
	/* Power off dongle */
	irda_device_set_dtr_rts(idev, FALSE, FALSE);

	MOD_DEC_USE_COUNT;
}

/*
 * Function actisys_change_speed (tty, baud)
 *
 *    Change speed of the ACTiSYS IR-220L and IR-220L+ type IrDA dongles.
 *    To cycle through the available baud rates, pulse RTS low for a few
 *    ms.  
 */
static void actisys_change_speed(struct irda_device *idev, int baudrate)
{
        int current_baudrate;
        int index = 0;
	
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	current_baudrate = idev->qos.baud_rate.value;

	/* Find the correct baudrate index for the currently used baudrate */
	while (current_baudrate != baud_rates[index])
		index++;

        DEBUG( 4, __FUNCTION__ "(), index=%d\n", index);

	/* Cycle through avaiable baudrates until we reach the correct one */
        while (current_baudrate != baudrate) {	
                DEBUG(4, __FUNCTION__ "(), current baudrate = %d\n",
		      baud_rates[index]);
		
		/* Set DTR, clear RTS */
		irda_device_set_dtr_rts(idev, TRUE, FALSE);
		
		/* Wait at a few ms */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(2);

		/* Set DTR, Set RTS */
		irda_device_set_dtr_rts(idev, TRUE, TRUE);
		
		/* Wait at a few ms again */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(2);

                /* Go to next baudrate */
		if (idev->io.dongle_id == ACTISYS_DONGLE)
                        index = (index+1) % 4; /* IR-220L */
		else
                        index = (index+1) % 5; /* IR-220L+ */

                current_baudrate = baud_rates[index];
        }
	DEBUG(4, __FUNCTION__ "(), current baudrate = %d\n",baud_rates[index]);
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
static void actisys_reset(struct irda_device *idev, int unused)
{
	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	/* Clear DTR */
	irda_device_set_dtr_rts(idev, FALSE, TRUE);

	/* Sleep 10-20 ms*/
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(2);
	
	/* Go back to normal mode */
	irda_device_set_dtr_rts(idev, TRUE, TRUE);
	
	idev->qos.baud_rate.value = 9600;
}

/*
 * Function actisys_init_qos (qos)
 *
 *    Initialize QoS capabilities
 *
 */
static void actisys_init_qos(struct irda_device *idev, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;

	/* Remove support for 38400 if this is not a 220L+ dongle */
	if (idev->io.dongle_id == ACTISYS_DONGLE)
		qos->baud_rate.bits &= ~IR_38400;
	
	qos->min_turn_time.bits &= 0x40; /* Needs 0.01 ms */
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("ACTiSYS IR-220L and IR-220L+ dongle driver");	
		
/*
 * Function init_module (void)
 *
 *    Initialize Actisys module
 *
 */
int init_module(void)
{
	return actisys_init();
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
