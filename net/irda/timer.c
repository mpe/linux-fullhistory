/*********************************************************************
 *                
 * Filename:      timer.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Aug 16 00:59:29 1997
 * Modified at:   Mon Sep 20 11:32:37 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997, 1999 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
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

#include <asm/system.h>
#include <linux/delay.h>

#include <net/irda/timer.h>
#include <net/irda/irda.h>
#include <net/irda/irtty.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp_event.h>

static void irlap_slot_timer_expired( void* data);
static void irlap_query_timer_expired( void* data);
static void irlap_final_timer_expired( void* data);
static void irlap_wd_timer_expired( void* data);
static void irlap_backoff_timer_expired( void* data);

static void irda_device_media_busy_expired( void* data); 
/*
 * Function irda_start_timer (timer, timeout)
 *
 *    Start an IrDA timer
 *
 */
void irda_start_timer(struct timer_list *ptimer, int timeout, void *data,
		      TIMER_CALLBACK callback) 
{
	del_timer(ptimer);
 
	ptimer->data = (unsigned long) data;

	/* 
	 * For most architectures void * is the same as unsigned long, but
	 * at least we try to use void * as long as possible. Since the 
	 * timer functions use unsigned long, we cast the function here
	 */
	ptimer->function = (void (*)(unsigned long)) callback;
	ptimer->expires = jiffies + timeout;
	
	add_timer(ptimer);
}

inline void irlap_start_slot_timer(struct irlap_cb *self, int timeout)
{
	irda_start_timer(&self->slot_timer, timeout, (void *) self, 
			 irlap_slot_timer_expired);
}

inline void irlap_start_query_timer(struct irlap_cb *self, int timeout)
{
	irda_start_timer( &self->query_timer, timeout, (void *) self, 
			  irlap_query_timer_expired);
}

inline void irlap_start_final_timer(struct irlap_cb *self, int timeout)
{
	irda_start_timer(&self->final_timer, timeout, (void *) self, 
			 irlap_final_timer_expired);
}

inline void irlap_start_wd_timer(struct irlap_cb *self, int timeout)
{
	irda_start_timer(&self->wd_timer, timeout, (void *) self, 
			 irlap_wd_timer_expired);
}

inline void irlap_start_backoff_timer(struct irlap_cb *self, int timeout)
{
	irda_start_timer(&self->backoff_timer, timeout, (void *) self, 
			 irlap_backoff_timer_expired);
}

inline void irda_device_start_mbusy_timer(struct irda_device *self) 
{
	irda_start_timer(&self->media_busy_timer, MEDIABUSY_TIMEOUT, 
			 (void *) self, 
			 irda_device_media_busy_expired);
	
}

inline void irlmp_start_watchdog_timer(struct lsap_cb *self, int timeout) 
{
	irda_start_timer(&self->watchdog_timer, timeout, (void *) self,
			 irlmp_watchdog_timer_expired);
}

inline void irlmp_start_discovery_timer(struct irlmp_cb *self, int timeout) 
{
	irda_start_timer(&self->discovery_timer, timeout, (void *) self,
			 irlmp_discovery_timer_expired);
}

inline void irlmp_start_idle_timer(struct lap_cb *self, int timeout) 
{
	irda_start_timer(&self->idle_timer, timeout, (void *) self,
			 irlmp_idle_timer_expired);
}

/*
 * Function irlap_slot_timer_expired (data)
 *
 *    IrLAP slot timer has expired
 *
 */
static void irlap_slot_timer_expired(void *data)
{
	struct irlap_cb *self = (struct irlap_cb *) data;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == LAP_MAGIC, return;);

	irlap_do_event(self, SLOT_TIMER_EXPIRED, NULL, NULL);
} 

/*
 * Function irlap_query_timer_expired (data)
 *
 *    IrLAP query timer has expired
 *
 */
static void irlap_query_timer_expired(void *data)
{
	struct irlap_cb *self = (struct irlap_cb *) data;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == LAP_MAGIC, return;);

	irlap_do_event(self, QUERY_TIMER_EXPIRED, NULL, NULL);
} 

/*
 * Function irda_final_timer_expired (data)
 *
 *    
 *
 */
static void irlap_final_timer_expired(void *data)
{
	struct irlap_cb *self = (struct irlap_cb *) data;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == LAP_MAGIC, return;);

	irlap_do_event(self, FINAL_TIMER_EXPIRED, NULL, NULL);
}

/*
 * Function irda_wd_timer_expired (data)
 *
 *    
 *
 */
static void irlap_wd_timer_expired(void *data)
{
	struct irlap_cb *self = (struct irlap_cb *) data;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == LAP_MAGIC, return;);
	
	irlap_do_event(self, WD_TIMER_EXPIRED, NULL, NULL);
}

/*
 * Function irda_backoff_timer_expired (data)
 *
 *    
 *
 */
static void irlap_backoff_timer_expired(void *data)
{
	struct irlap_cb *self = (struct irlap_cb *) data;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == LAP_MAGIC, return;);
	
	irlap_do_event(self, BACKOFF_TIMER_EXPIRED, NULL, NULL);
}


/*
 * Function irtty_media_busy_expired (data)
 *
 *    
 */
void irda_device_media_busy_expired(void* data)
{
	struct irda_device *self = (struct irda_device *) data;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRDA_DEVICE_MAGIC, return;);

	irda_device_set_media_busy(&self->netdev, FALSE);
}
