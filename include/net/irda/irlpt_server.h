/*********************************************************************
 *                
 * Filename:      server.h
 * Version:       0.1
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Feb 21 18:54:38 1998
 * Modified at:   Tue Sep 22 11:41:42 1998
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

#ifndef IRLPT_SERVER_H
#define IRLPT_SERVER_H

#include "qos.h"
#include "irmod.h"

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/miscdevice.h>

/* int  server_init( struct device *dev); */

extern struct irlpt_cb *irlpt_server;

#endif
