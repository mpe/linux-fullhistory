/*********************************************************************
 *                
 * Filename:      server_fsm.h<2>
 * Version:       0.1
 * Sources:       irlan_event.h
 * 
 *     Copyright (c) 1997 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *
 *     I, Thomas Davis, provide no warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#ifndef IRLPT_EVENT_H
#define IRLPT_EVENT_H

#include <linux/kernel.h>
#include <linux/skbuff.h>

void irlpt_server_do_event( struct irlpt_cb *self,  IRLPT_EVENT event, 
			    struct sk_buff *skb, struct irlpt_info *info);
void irlpt_server_next_state( struct irlpt_cb *self, IRLPT_SERVER_STATE state);

#endif
