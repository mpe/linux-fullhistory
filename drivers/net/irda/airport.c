/*********************************************************************
 *                
 * Filename:      airport.c
 * Version:       0.2
 * Description:   Implementation for the Adaptec Airport 1000 and 2000 
 *                dongles
 * Status:        Experimental.
 * Author:        Fons Botman <budely@tref.nl>
 * Created at:    Wed May 19 23:14:34 CEST 1999
 * Based on:      actisys.c by Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Fons Botman, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Fons Botman nor anyone else admit liability nor
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

static int  airport_reset_wrapper(struct irda_task *task);
static void airport_open(dongle_t *self, struct qos_info *qos);
static void airport_close(dongle_t *self);
static int  airport_change_speed_wrapper(struct irda_task *task);

static struct dongle_reg dongle = {
	Q_NULL,
	IRDA_AIRPORT_DONGLE,
	airport_open,
	airport_close,
	airport_reset_wrapper,
	airport_change_speed_wrapper,
};

int __init airport_init(void)
{
	int ret;

	IRDA_DEBUG(2, __FUNCTION__ "()\n");
	ret = irda_device_register_dongle(&dongle);
	if (ret < 0)
		return ret;
	return 0;
}

void airport_cleanup(void)
{
	IRDA_DEBUG(2, __FUNCTION__ "()\n");
	irda_device_unregister_dongle(&dongle);
}

static void airport_open(dongle_t *self, struct qos_info *qos)
{
	qos->baud_rate.bits &= 
		IR_2400|IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	/* May need 1ms */
	qos->min_turn_time.bits &= 0x07;

	MOD_INC_USE_COUNT;
}

static void airport_close(dongle_t *self)
{
	IRDA_DEBUG(2, __FUNCTION__ "()\n");
	/* Power off dongle */
	self->set_dtr_rts(self->dev, FALSE, FALSE);

	MOD_DEC_USE_COUNT;
}

static void airport_set_command_mode(dongle_t *self)
{
	IRDA_DEBUG(2, __FUNCTION__ "()\n");
	self->set_dtr_rts(self->dev, FALSE, TRUE);
}

static void airport_set_normal_mode(dongle_t *self)
{
	IRDA_DEBUG(2, __FUNCTION__ "()\n");
    	self->set_dtr_rts(self->dev, TRUE, TRUE);
}

void airport_write_char(dongle_t *self, unsigned char c)
{
	int actual;
	IRDA_DEBUG(2, __FUNCTION__ "(,0x%x)\n", c & 0xff);
	actual = self->write(self->dev, &c, 1);
	ASSERT(actual == 1, return;);
}

#define JIFFIES_TO_MSECS(j) ((j)*1000/HZ)

static int airport_waitfor_char(dongle_t *self, unsigned char c)
{
	__u8 buf[100];
	int i, found = FALSE;
	int before;
	int len;

	IRDA_DEBUG(2, __FUNCTION__ "(,0x%x)\n", c);

	/* Sleep approx. 10 ms */
	before = jiffies;
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));
	IRDA_DEBUG(4, __FUNCTION__ " waited %ldms\n", 
	      JIFFIES_TO_MSECS(jiffies - before));

	len = self->read(self->dev, buf, 100);

	for (i = 0; !found && i < len; i++ ) {
		/* IRDA_DEBUG(6, __FUNCTION__ " 0x02x\n", idev->rx_buff.data[i]); */
		found = c == buf[i];
	}

	IRDA_DEBUG(2, __FUNCTION__ " returns %s\n", (found ? "true" : "false"));
	return found;
}

static int airport_check_command_mode(dongle_t *self)
{
	int i;
	int found = FALSE;
  
	IRDA_DEBUG(2, __FUNCTION__ "()\n");
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));
	airport_set_command_mode(self);

	/* Loop until the time expires (200ms) or we get the magic char. */

	for ( i = 0 ; i < 25 ; i++ ) {
	  	airport_write_char(self, 0xff);
		if (airport_waitfor_char(self, 0xc3)) {
			found = TRUE;
			break;
		}
	}

	if (found) {
		IRDA_DEBUG(2, __FUNCTION__ " OK. (%d)\n", i);
	} else {
		IRDA_DEBUG(0, __FUNCTION__ " FAILED!\n");
	}
	return found;
}

static int airport_write_register(dongle_t *self, unsigned char reg)
{
	int ok = FALSE;
	int i;

	IRDA_DEBUG(4, __FUNCTION__ "(,0x%x)\n", reg);
	airport_check_command_mode(self);

	for ( i = 0 ; i < 6 ; i++ ) {
		airport_write_char(self, reg);
		if (!airport_waitfor_char(self, reg)) 
			continue;

		/* Now read it back */
		airport_write_char(self, (reg << 4) | 0x0f);
		if (airport_waitfor_char(self, reg)) {
			ok = TRUE;
			break;
		}
	}

	airport_set_normal_mode(self);
	if (ok) {
		IRDA_DEBUG(4, __FUNCTION__ "(,0x%x) returns OK\n", reg);
	} else {
		IRDA_DEBUG(0, __FUNCTION__ "(,0x%x) returns False!\n", reg);
	}
	return ok;
}


/*
 * Function airport_change_speed (self, speed)
 *
 *    Change speed of the Airport type IrDA dongles.
 */
static void airport_change_speed(dongle_t *self, __u32 speed)
{
        __u32 current_baudrate;
        int baudcode;
	
	IRDA_DEBUG(4, __FUNCTION__ "(,%d)\n", speed);

	ASSERT(self != NULL, return;);
	
	/* Find the correct baudrate code for the required baudrate */
	switch (speed) {
	case 2400:	baudcode = 0x10;	break;
	case 4800:	baudcode = 0x20;	break;
	case 9600:	baudcode = 0x30;	break;
	case 19200:	baudcode = 0x40;	break;
	case 38400:	baudcode = 0x50;	break;
	case 57600:	baudcode = 0x60;	break;
	case 115200:	baudcode = 0x70;	break;
	default:
		IRDA_DEBUG(0, __FUNCTION__ " bad baud rate: %d\n", speed);
		return;
	}

	current_baudrate = self->speed;
	IRDA_DEBUG(4, __FUNCTION__ " current baudrate: %d\n", current_baudrate);

	self->set_mode(self->dev, IRDA_RAW);

	/* Set the new speed in both registers */
	if (airport_write_register(self, baudcode)) {
		if (airport_write_register(self, baudcode|0x01)) {
			/* ok */
		} else {
			IRDA_DEBUG(0, __FUNCTION__ 
			      " Cannot set new speed in second register\n");
		}
	} else {
		IRDA_DEBUG(0, __FUNCTION__ 
		      " Cannot set new speed in first register\n");
	}
	
	self->set_mode(self->dev, IRDA_IRLAP);

	/* How do I signal an error in these functions? */

	IRDA_DEBUG(4, __FUNCTION__ " returning\n");
}

int airport_change_speed_wrapper(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u32 speed = (__u32) task->param;

	irda_execute_as_process(self, (TODO_CALLBACK) airport_change_speed, 
				speed);

	irda_task_next_state(task, IRDA_TASK_DONE);
	
	return 0;
}

/*
 * Function airport_reset (self)
 *
 *      Reset the Airport type dongle. Warning, this function must only be
 *      called with a process context!
 *
 */
static void airport_reset(dongle_t *self)
{
	int ok;

	IRDA_DEBUG(2, __FUNCTION__ "()\n");
	ASSERT(self != NULL, return;);

	self->set_mode(self->dev, IRDA_RAW);

	airport_set_normal_mode(self);

	/* Sleep 2000 ms */
	IRDA_DEBUG(2, __FUNCTION__ " waiting for powerup\n");
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(2000));
	IRDA_DEBUG(2, __FUNCTION__ " finished waiting for powerup\n");
	
	/* set dongle speed to 9600 */
	ok = TRUE;

	if (ok)
		ok = airport_write_register(self, 0x30);
	if (!ok)
		MESSAGE(__FUNCTION__ "() dongle not connected?\n");
	if (ok)
		ok = airport_write_register(self, 0x31);

	if (ok)
		ok = airport_write_register(self, 0x02);
	if (ok)
		ok = airport_write_register(self, 0x03);

	if (ok) {
		ok = airport_check_command_mode(self);

		if (ok) {
			airport_write_char(self, 0x04);
			ok = airport_waitfor_char(self, 0x04);
		}
		airport_set_normal_mode(self);
	}
		
	self->set_mode(self->dev, IRDA_IRLAP);
	
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));
	IRDA_DEBUG(4, __FUNCTION__ " waited 20ms\n");

	self->speed = 9600;
	if (!ok)
		MESSAGE(__FUNCTION__ "() failed.\n");
	IRDA_DEBUG(2, __FUNCTION__ " returning.\n");
}

int airport_reset_wrapper(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;

	irda_execute_as_process(self, (TODO_CALLBACK) airport_reset, 0);

	irda_task_next_state(task, IRDA_TASK_DONE);
	
	return 0;
}

#ifdef MODULE

MODULE_AUTHOR("Fons Botman <budely@tref.nl>");
MODULE_DESCRIPTION("Adaptec Airport 1000 and 2000 dongle driver");	
		
/*
 * Function init_module (void)
 *
 *    Initialize Airport module
 *
 */
int init_module(void)
{
	return airport_init();
}

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Airport module
 *
 */
void cleanup_module(void)
{
	airport_cleanup();
}

#endif
