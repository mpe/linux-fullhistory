/*********************************************************************
 *                
 * Filename:      irlpt_client.h
 * Version:       0.1
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Feb 21 18:54:38 1998
 * Modified at:   Mon Jan 11 15:58:16 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>
 *     Copyright (c) 1998, Dag Brattli, 
 *     All Rights Reserved
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     I, Thomas Davis, provide no warranty for any of this software. This
 *     material is provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#ifndef IRLPT_CLIENT_H
#define IRLPT_CLIENT_H

/* Debug function */

/* int  client_init( struct device *dev); */

/*
 * if it's static, it doesn't go in here.
 */

void irlpt_client_get_value_confirm(__u16 obj_id, 
				    struct ias_value *value, void *priv);
void irlpt_client_connect_indication( void *instance, void *sap, 
				      struct qos_info *qos, 
				      int max_seg_size,
				      struct sk_buff *skb);
void irlpt_client_connect_request( struct irlpt_cb *self);

extern hashbin_t *irlpt_clients;

#endif
