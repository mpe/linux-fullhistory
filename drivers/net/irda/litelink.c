/*********************************************************************
 *                
 * Filename:      litelink.c
 * Version:       1.0
 * Description:   Driver for the Parallax LiteLink dongle
 * Status:        Stable
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Fri May  7 12:50:33 1999
 * Modified at:   Mon May 10 15:12:18 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *     
 ********************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <asm/ioctls.h>
#include <asm/uaccess.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irda_device.h>
#include <net/irda/dongle.h>

static void litelink_reset(struct irda_device *dev, int unused);
static void litelink_open(struct irda_device *idev, int type);
static void litelink_close(struct irda_device *dev);
static void litelink_change_speed( struct irda_device *dev, int baudrate);
static void litelink_reset(struct irda_device *dev, int unused);
static void litelink_init_qos(struct irda_device *idev, struct qos_info *qos);

/* These are the baudrates supported */
static int baud_rates[] = { 115200, 57600, 38400, 19200, 9600 };

static struct dongle dongle = {
	LITELINK_DONGLE,
	litelink_open,
	litelink_close,
	litelink_reset,
	litelink_change_speed,
	litelink_init_qos,
};

__initfunc(int litelink_init(void))
{
	return irda_device_register_dongle(&dongle);
}

void litelink_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

static void litelink_open(struct irda_device *idev, int type)
{
	strcat(idev->description, " <-> litelink");

        idev->io.dongle_id = type;
	idev->flags |= IFF_DONGLE;

	MOD_INC_USE_COUNT;
}

static void litelink_close(struct irda_device *idev)
{
	/* Power off dongle */
	irda_device_set_dtr_rts(idev, FALSE, FALSE);

	MOD_DEC_USE_COUNT;
}

/*
 * Function litelink_change_speed (tty, baud)
 *
 *    Change speed of the Litelink dongle. To cycle through the available 
 *    baud rates, pulse RTS low for a few ms.  
 */
static void litelink_change_speed(struct irda_device *idev, int baudrate)
{
        int i;
	
	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	/* Clear RTS to reset dongle */
	irda_device_set_dtr_rts(idev, TRUE, FALSE);

	/* Sleep a minimum of 15 us */
	udelay(15);

	/* Go back to normal mode */
	irda_device_set_dtr_rts(idev, TRUE, TRUE);
	
	/* Sleep a minimum of 15 us */
	udelay(15);
	
	/* Cycle through avaiable baudrates until we reach the correct one */
	for (i=0; i<5 && baud_rates[i] != baudrate; i++) {

		/* Set DTR, clear RTS */
		irda_device_set_dtr_rts(idev, FALSE, TRUE);
		
		/* Sleep a minimum of 15 us */
		udelay(15);
		
		/* Set DTR, Set RTS */
		irda_device_set_dtr_rts(idev, TRUE, TRUE);
		
		/* Sleep a minimum of 15 us */
		udelay(15);
        }
}

/*
 * Function litelink_reset (dev)
 *
 *      Reset the Litelink type dongle. Warning, this function must only be
 *      called with a process context!
 *
 */
static void litelink_reset(struct irda_device *idev, int unused)
{
	struct irtty_cb *self;
        struct tty_struct *tty;

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);
	
	/* Power on dongle */
	irda_device_set_dtr_rts(idev, TRUE, TRUE);

	/* Sleep a minimum of 15 us */
	udelay(15);

	/* Clear RTS to reset dongle */
	irda_device_set_dtr_rts(idev, TRUE, FALSE);

	/* Sleep a minimum of 15 us */
	udelay(15);

	/* Go back to normal mode */
	irda_device_set_dtr_rts(idev, TRUE, TRUE);
	
	/* Sleep a minimum of 15 us */
	udelay(15);

	/* This dongles speed defaults to 115200 bps */
	idev->qos.baud_rate.value = 115200;
}

/*
 * Function litelink_init_qos (qos)
 *
 *    Initialize QoS capabilities
 *
 */
static void litelink_init_qos( struct irda_device *idev, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits &= 0x40; /* Needs 0.01 ms */
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Parallax Litelink dongle driver");	
		
/*
 * Function init_module (void)
 *
 *    Initialize Litelink module
 *
 */
int init_module(void)
{
	return litelink_init();
}

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Litelink module
 *
 */
void cleanup_module(void)
{
	litelink_cleanup();
}

#endif
