/*********************************************************************
 *                
 * Filename:      wrapper.h
 * Version:       
 * Description:   IrDA Wrapper layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Thu Nov 19 13:17:56 1998
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

#ifndef WRAPPER_H
#define WRAPPER_H

#include <linux/types.h>
#include <linux/skbuff.h>

#include "irda_device.h"

#define BOF  0xc0 /* Beginning of frame */
#define XBOF 0xff
#define EOF  0xc1 /* End of frame */
#define CE   0x7d /* Control escape */

#define STA BOF  /* Start flag */
#define STO EOF  /* End flag */

#define IR_TRANS 0x20    /* Asynchronous transparency modifier */       

#define SOP BOF  /* Start of */
#define EOP EOF  /* End of */

enum {
	OUTSIDE_FRAME = 1, 
	BEGIN_FRAME, 
	LINK_ESCAPE, 
	INSIDE_FRAME
};

/* Proto definitions */
int  async_wrap_skb( struct sk_buff *skb, __u8 *tx_buff, int buffsize);
void async_unwrap_char( struct irda_device *, __u8 byte);

#endif
