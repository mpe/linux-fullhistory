/*********************************************************************
 *                
 * Filename:      timer.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Aug 16 00:59:29 1997
 * Modified at:   Mon Nov  2 14:31:55 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997, 1998 Dag Brattli <dagb@cs.uit.no>, 
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

#ifndef TIMER_H
#define TIMER_H

#include "irda.h"
#include "irmod.h"
#include "irlap.h"
#include "irlmp.h"
#include "irda_device.h"

/* 
 *  Timeout definitions, some defined in IrLAP p. 92
 */
#define POLL_TIMEOUT        45             /* Must never exceed 500 ms */
#define FINAL_TIMEOUT       50             /* Must never exceed 500 ms */

/* 
 *  Normally twice of p-timer. Note 3, IrLAP p. 60 suggests at least twice 
 *  duration of the P-timer.
 */
#define WD_TIMEOUT          POLL_TIMEOUT*2
#define MEDIABUSY_TIMEOUT   50             /* 500 msec */

/*
 *  Slot timer must never exceed 85 ms, and must always be at least 25 ms, 
 *  suggested to  75-85 msec by IrDA lite
 */
#define SLOT_TIMEOUT             8
#define QUERY_TIMEOUT          100

#define WATCHDOG_TIMEOUT      2000                /* 20 sec */
#define RETRY_TIMEOUT           51

typedef void (*TIMER_CALLBACK)( unsigned long);

void irda_start_timer( struct timer_list *ptimer, int timeout, int data,
		       TIMER_CALLBACK callback);

inline void irlap_start_slot_timer( struct irlap_cb *self, int timeout);
inline void irlap_start_query_timer( struct irlap_cb *self, int timeout);
inline void irlap_start_final_timer( struct irlap_cb *self, int timeout);
inline void irlap_start_wd_timer( struct irlap_cb *self, int timeout);
inline void irlap_start_backoff_timer( struct irlap_cb *self, int timeout);

inline void irda_device_start_mbusy_timer( struct irda_device *self);

struct lsap_cb;
inline void irlmp_start_watchdog_timer( struct lsap_cb *, int timeout);
inline void irlmp_start_discovery_timer( struct irlmp_cb *, int timeout); 

struct irobex_cb;
inline void irobex_start_watchdog_timer( struct irobex_cb *, int timeout);

#endif

