/*********************************************************************
 *                
 * Filename:      client.h
 * Version:       0.3
 * Description:   IrDA LAN access layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Mon Oct 19 12:37:20 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
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

#ifndef IRLAN_CLIENT_H
#define IRLAN_CLIENT_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

int  irlan_client_init(void);
void irlan_client_cleanup(void);

void irlan_discovery_indication( DISCOVERY*);
void irlan_client_disconnect_indication( void *instance, void *sap, 
					 LM_REASON reason, struct sk_buff *);

void irlan_client_data_indication( void *instance, void *sap, 
				   struct sk_buff *skb);

void irlan_client_control_data_indication( void *instance, void *sap, 
					   struct sk_buff *skb);

void irlan_client_connect_confirm( void *instance, void *sap, 
				   struct qos_info *qos, int max_sdu_size,
				   struct sk_buff *);
void irlan_client_connect_indication( void *instance, void *sap, 
				      struct sk_buff *);
void irlan_client_connect_response( void *instance, void *sap, 
				    int max_sdu_size, struct sk_buff *skb);

void irlan_client_open_tsaps( struct irlan_cb *self);

void irlan_client_extract_params( struct irlan_cb *self, 
				  struct sk_buff *skb);
void check_response_param( struct irlan_cb *self, char *param, 
			   char *value, int val_len);
void handle_request( struct irlan_cb *self);
void irlan_client_register_server(void);
void irlan_client_get_value_confirm( __u16 obj_id, struct ias_value *value, 
				     void *priv);

#endif
