/*********************************************************************
 *                
 * Filename:      actisys.c
 * Version:       0.8
 * Description:   Implementation for the ACTiSYS IR-220L and IR-220L+ 
 *                dongles
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Oct 21 20:02:35 1998
 * Modified at:   Mon Oct 18 23:37:06 1999
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

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irda_device.h>

static int  actisys_change_speed(struct irda_task *task);
static int  actisys_reset(struct irda_task *task);
static void actisys_open(dongle_t *self, struct qos_info *qos);
static void actisys_close(dongle_t *self);

/* These are the baudrates supported */
static __u32 baud_rates[] = { 9600, 19200, 57600, 115200, 38400 };

static struct dongle_reg dongle = {
	Q_NULL,
	IRDA_ACTISYS_DONGLE,
	actisys_open,
	actisys_close,
	actisys_reset,
	actisys_change_speed,
};

static struct dongle_reg dongle_plus = {
	Q_NULL,
	IRDA_ACTISYS_PLUS_DONGLE,
	actisys_open,
	actisys_close,
	actisys_reset,
	actisys_change_speed,
};

int __init actisys_init(void)
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

static void actisys_open(dongle_t *self, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;

	/* Remove support for 38400 if this is not a 220L+ dongle */
	if (self->issue->type == IRDA_ACTISYS_DONGLE)
		qos->baud_rate.bits &= ~IR_38400;
	
	qos->min_turn_time.bits &= 0x40; /* Needs 0.01 ms */

	MOD_INC_USE_COUNT;
}

static void actisys_close(dongle_t *self)
{
	/* Power off dongle */
	self->set_dtr_rts(self->dev, FALSE, FALSE);

	MOD_DEC_USE_COUNT;
}

/*
 * Function actisys_change_speed (tty, baud)
 *
 *    Change speed of the ACTiSYS IR-220L and IR-220L+ type IrDA dongles.
 *    To cycle through the available baud rates, pulse RTS low for a few
 *    ms.  
 */
static int actisys_change_speed(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u32 speed = (__u32) task->param;
        __u32 current_speed;
        int index = 0;
	int ret = 0;
	
	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	current_speed = self->speed;

	/* Find the correct baudrate index for the currently used baudrate */
	while (current_speed != baud_rates[index])
		index++;

        IRDA_DEBUG(4, __FUNCTION__ "(), index=%d\n", index);

	switch (task->state) {
	case IRDA_TASK_INIT:
		/* Lock dongle */
		if (irda_lock((void *) &self->busy) == FALSE) {
			IRDA_DEBUG(0, __FUNCTION__ "(), busy!\n");
			ret = MSECS_TO_JIFFIES(100);
			break;
		}
	
                IRDA_DEBUG(4, __FUNCTION__ "(), current baudrate = %d\n",
		      baud_rates[index]);
		
		/* Set DTR, clear RTS */
		self->set_dtr_rts(self->dev, TRUE, FALSE);
		
		irda_task_next_state(task, IRDA_TASK_WAIT1);

		/* Wait at a few ms */
		ret = MSECS_TO_JIFFIES(20);
		break;
	case IRDA_TASK_WAIT1:
		/* Set DTR, Set RTS */
		self->set_dtr_rts(self->dev, TRUE, TRUE);
		
		irda_task_next_state(task, IRDA_TASK_WAIT2);

		/* Wait at a few ms again */
		ret = MSECS_TO_JIFFIES(20);
		break;
	case IRDA_TASK_WAIT2:
                /* Go to next baudrate */
		if (self->issue->type == IRDA_ACTISYS_DONGLE)
                        index = (index+1) % 4; /* IR-220L */
		else
                        index = (index+1) % 5; /* IR-220L+ */

                current_speed = baud_rates[index];

		/* Check if we need to go some more rounds */
		if (current_speed != speed)
			irda_task_next_state(task, IRDA_TASK_INIT);
		else {
			irda_task_next_state(task, IRDA_TASK_DONE);
			self->busy = 0;
		}
		break;
	default:
		ERROR(__FUNCTION__ "(), unknown state %d\n", task->state);
		irda_task_next_state(task, IRDA_TASK_DONE);
		self->busy = 0;
		ret = -1;
		break;
        }

	self->speed = speed;

	IRDA_DEBUG(4, __FUNCTION__ "(), current baudrate = %d\n", 
	      baud_rates[index]);

	return ret;
}

/*
 * Function actisys_reset (dev)
 *
 *      Reset the Actisys type dongle. Warning, this function must only be
 *      called with a process context!
 *
 *    	1. Clear DTR for a few ms.
 */
static int actisys_reset(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	int ret = 0;

	ASSERT(task != NULL, return -1;);

	switch (task->state) {
	case IRDA_TASK_INIT:
		/* Clear DTR */
		self->set_dtr_rts(self->dev, FALSE, TRUE);
		
		irda_task_next_state(task, IRDA_TASK_WAIT);

		/* Sleep 10-20 ms*/
		ret = MSECS_TO_JIFFIES(20);
		break;
	case IRDA_TASK_WAIT:			
		/* Go back to normal mode */
		self->set_dtr_rts(self->dev, TRUE, TRUE);
	
		irda_task_next_state(task, IRDA_TASK_DONE);

		self->speed = 9600;
		break;
	default:
		ERROR(__FUNCTION__ "(), unknown state %d\n", task->state);
		irda_task_next_state(task, IRDA_TASK_DONE);
		ret = -1;
		break;
	}
	return ret;
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
#endif /* MODULE */
