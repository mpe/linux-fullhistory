/*********************************************************************
 *                
 * Filename:      irtty.h
 * Version:       1.0
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Dec  9 21:13:12 1997
 * Modified at:   Thu Jul  1 10:13:02 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *  
 *     Copyright (c) 1997, 1999 Dag Brattli, All Rights Reserved.
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

#ifndef IRTTY_H
#define IRTTY_H

#include <linux/if.h>
#include <linux/skbuff.h>
#include <linux/termios.h>

#include <net/irda/irda.h>
#include <net/irda/irqueue.h>
#include <net/irda/irda_device.h>

#define IRTTY_IOC_MAGIC 'e'
#define IRTTY_IOCTDONGLE  _IO(IRTTY_IOC_MAGIC, 1)
#define IRTTY_IOC_MAXNR   1

#ifndef N_IRDA
#define N_IRDA         11   /* This one should go in </asm/termio.h> */
#endif

struct irtty_cb {
	QUEUE   q;     /* Must be first */
	magic_t magic;
	
	struct  tty_struct  *tty;
	struct  irda_device idev;
};
 
int irtty_register_dongle(struct dongle *dongle);
void irtty_unregister_dongle(struct dongle *dongle);

#endif





