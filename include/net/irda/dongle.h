/*********************************************************************
 *                
 * Filename:      dongle.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Oct 21 22:47:12 1998
 * Modified at:   Mon Dec 14 11:47:25 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli, All Rights Reserved.
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

#ifndef DONGLE_H
#define DONGLE_H

#include <net/irda/qos.h>
#include <net/irda/irda_device.h>

/* These are the currently known dongles */
typedef enum {
	TEKRAM_DONGLE,
	ESI_DONGLE,
	ACTISYS_DONGLE,
	ACTISYS_PLUS_DONGLE,
} DONGLE_T;

struct dongle {
	DONGLE_T type;
	void (*open)(struct irda_device *, int type);
	void (*close)(struct irda_device *);
	void (*reset)( struct irda_device *, int unused);
	void (*change_speed)( struct irda_device *, int baudrate);
	void (*qos_init)( struct irda_device *, struct qos_info *);
};

#endif
