/*********************************************************************
 *                
 * Filename:      airport.c
 * Version:       0.2
 * Description:   Implementation for the Adaptec Airport 1000 and 2000 
 *                dongles
 * Status:        Experimental.
 * Author:        Fons Botman <budely@tref.nl>
 * Created at:    Wed May 19 23:14:34 CEST 1999
 * Based on:      actisys.c
 * By:            Dag Brattli <dagb@cs.uit.no>
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
#include <net/irda/dongle.h>


static void airport_reset(struct irda_device *dev);
static void airport_open(struct irda_device *idev, int type);
static void airport_close(struct irda_device *dev);
static void airport_change_speed( struct irda_device *dev, __u32 speed);
static void airport_init_qos(struct irda_device *idev, struct qos_info *qos);


static struct dongle dongle = {
	AIRPORT_DONGLE,
	airport_open,
	airport_close,
	airport_reset,
	airport_change_speed,
	airport_init_qos,
};


int __init airport_init(void)
{
	int ret;

	DEBUG(2, __FUNCTION__ "()\n");
	ret = irda_device_register_dongle(&dongle);
	if (ret < 0)
		return ret;
	return 0;
}

void airport_cleanup(void)
{
	DEBUG(2, __FUNCTION__ "()\n");
	irda_device_unregister_dongle(&dongle);
}

static void airport_open(struct irda_device *idev, int type)
{
	DEBUG(2, __FUNCTION__ "(,%d)\n", type);
	if (strlen(idev->description) < sizeof(idev->description) - 13)
	  strcat(idev->description, " <-> airport");
	else
	  DEBUG(0, __FUNCTION__ " description too long: %s\n", 
		idev->description);

        idev->io.dongle_id = type;
	idev->flags |= IFF_DONGLE;

	MOD_INC_USE_COUNT;
}

static void airport_close(struct irda_device *idev)
{
	DEBUG(2, __FUNCTION__ "()\n");
	/* Power off dongle */
	irda_device_set_dtr_rts(idev, FALSE, FALSE);

	MOD_DEC_USE_COUNT;
}

static void airport_set_command_mode(struct irda_device *idev)
{
	DEBUG(2, __FUNCTION__ "()\n");
	irda_device_set_dtr_rts(idev, FALSE, TRUE);
}

static void airport_set_normal_mode(struct irda_device *idev)
{
	DEBUG(2, __FUNCTION__ "()\n");
    	irda_device_set_dtr_rts(idev, TRUE, TRUE);
}


void airport_write_char(struct irda_device *idev, unsigned char c)
{
	int actual;
	DEBUG(2, __FUNCTION__ "(,0x%x)\n", c & 0xff);
	actual = idev->raw_write(idev, &c, 1);
	ASSERT(actual == 1, return;);
}

#define JIFFIES_TO_MSECS(j) ((j)*1000/HZ)

static int airport_waitfor_char(struct irda_device *idev, unsigned char c)
{
	int i, found = FALSE;
	int before;
	DEBUG(2, __FUNCTION__ "(,0x%x)\n", c);

	/* Sleep approx. 10 ms */
	before = jiffies;
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));
	DEBUG(4, __FUNCTION__ " waited %ldms\n", 
	      JIFFIES_TO_MSECS(jiffies - before));

	for ( i = 0 ; !found && i < idev->rx_buff.len ; i++ ) {
		/* DEBUG(6, __FUNCTION__ " 0x02x\n", idev->rx_buff.data[i]); */
		found = c == idev->rx_buff.data[i];
	}
	idev->rx_buff.len = 0;

	DEBUG(2, __FUNCTION__ " returns %s\n", (found ? "true" : "false"));
	return found;
}

static int airport_check_command_mode(struct irda_device *idev)
{
	int i;
	int found = FALSE;
  
	DEBUG(2, __FUNCTION__ "()\n");
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));
	airport_set_command_mode(idev);

	/* Loop until the time expires (200ms) or we get the magic char. */

	for ( i = 0 ; i < 25 ; i++ ) {
	  	airport_write_char(idev, 0xff);
		if (airport_waitfor_char(idev, 0xc3)) {
			found = TRUE;
			break;
		}
	}

	if (found) {
		DEBUG(2, __FUNCTION__ " OK. (%d)\n", i);
	} else {
		DEBUG(0, __FUNCTION__ " FAILED!\n");
	}
	return found;
}


static int airport_write_register(struct irda_device *idev, unsigned char reg)
{
	int ok = FALSE;
	int i;

	DEBUG(4, __FUNCTION__ "(,0x%x)\n", reg);
	airport_check_command_mode(idev);

	for ( i = 0 ; i < 6 ; i++ ) {
		airport_write_char(idev, reg);
		if (!airport_waitfor_char(idev, reg)) 
			continue;

		/* Now read it back */
		airport_write_char(idev, (reg << 4) | 0x0f);
		if (airport_waitfor_char(idev, reg)) {
			ok = TRUE;
			break;
		}
	}

	airport_set_normal_mode(idev);
	if (ok) {
		DEBUG(4, __FUNCTION__ "(,0x%x) returns OK\n", reg);
	} else {
		DEBUG(0, __FUNCTION__ "(,0x%x) returns False!\n", reg);
	}
	return ok;
}


/*
 * Function airport_change_speed (tty, baud)
 *
 *    Change speed of the Airport type IrDA dongles.
 */
static void airport_change_speed(struct irda_device *idev, __u32 speed)
{
        __u32 current_baudrate;
        int baudcode;
	
	DEBUG(4, __FUNCTION__ "(,%d)\n", speed);

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
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
		DEBUG(0, __FUNCTION__ " bad baud rate: %d\n", speed);
		return;
	}

	current_baudrate = idev->qos.baud_rate.value;
	DEBUG(4, __FUNCTION__ " current baudrate: %d\n", current_baudrate);

	/* The dongle falls back to 9600 baud */
	if (current_baudrate != 9600) {
		DEBUG(4, __FUNCTION__ " resetting speed to 9600 baud\n");
		ASSERT(idev->change_speed , return;);
		idev->change_speed(idev, 9600);
		idev->qos.baud_rate.value = 9600;
	}

	if (idev->set_raw_mode)
		idev->set_raw_mode(idev, TRUE);

	/* Set the new speed in both registers */
	if (airport_write_register(idev, baudcode)) {
		if (airport_write_register(idev, baudcode|0x01)) {
			/* ok */
		} else {
			DEBUG(0, __FUNCTION__ 
			      " Cannot set new speed in second register\n");
		}
	} else {
		DEBUG(0, __FUNCTION__ 
		      " Cannot set new speed in first register\n");
	}
	
	if (idev->set_raw_mode)
		idev->set_raw_mode(idev, FALSE);

	/* How do I signal an error in these functions? */

	DEBUG(4, __FUNCTION__ " returning\n");
}


/*
 * Function airport_reset (idev)
 *
 *      Reset the Airport type dongle. Warning, this function must only be
 *      called with a process context!
 *
 */
static void airport_reset(struct irda_device *idev)
{
	int ok;

	DEBUG(2, __FUNCTION__ "()\n");
	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	ASSERT(idev->set_raw_mode /* The airport needs this */, return;);

	if (idev->set_raw_mode)
		idev->set_raw_mode(idev, TRUE);

	airport_set_normal_mode(idev);

	/* Sleep 2000 ms */
	DEBUG(2, __FUNCTION__ " waiting for powerup\n");
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(2000));
	DEBUG(2, __FUNCTION__ " finished waiting for powerup\n");
	
	/* set dongle speed to 9600 */
	ok = TRUE;

	if (ok)
		ok = airport_write_register(idev, 0x30);
	if (!ok)
		MESSAGE(__FUNCTION__ "() dongle not connected?\n");
	if (ok)
		ok = airport_write_register(idev, 0x31);

	if (ok)
		ok = airport_write_register(idev, 0x02);
	if (ok)
		ok = airport_write_register(idev, 0x03);

	if (ok) {
		ok = airport_check_command_mode(idev);

		if (ok) {
			airport_write_char(idev, 0x04);
			ok = airport_waitfor_char(idev, 0x04);
		}
		airport_set_normal_mode(idev);
	}
		
	if (idev->set_raw_mode)
		idev->set_raw_mode(idev, FALSE);
	

	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(MSECS_TO_JIFFIES(20));
	DEBUG(4, __FUNCTION__ " waited 20ms\n");

	idev->qos.baud_rate.value = 9600;
	if (!ok)
		MESSAGE(__FUNCTION__ "() failed.\n");
	DEBUG(2, __FUNCTION__ " returning.\n");
}

/*
 * Function airport_init_qos (qos)
 *
 *    Initialize QoS capabilities
 *
 */
static void airport_init_qos(struct irda_device *idev, struct qos_info *qos)
{
	qos->baud_rate.bits &= 
		IR_2400|IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	/* May need 1ms */
	qos->min_turn_time.bits &= 0x07;
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
