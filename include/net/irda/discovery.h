/*********************************************************************
 *                
 * Filename:      discovery.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Apr  6 16:53:53 1999
 * Modified at:   Thu Apr 22 11:04:56 1999
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

#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <asm/param.h>

#include <net/irda/irda.h>
#include <net/irda/irqueue.h>

#define DISCOVERY_EXPIRE_TIMEOUT 6*HZ
#define DISCOVERY_DEFAULT_SLOTS  0

/*
 * The DISCOVERY structure is used for both discovery requests and responses
 */
typedef struct {
	QUEUE queue;             /* Must be first! */

	__u32      saddr;        /* Which link the device was discovered */
	__u32      daddr;        /* Remote device address */
	LAP_REASON condition;    /* More info about the discovery */

	__u16_host_order hints;  /* Discovery hint bits */
	__u8       charset;
	char       info[32];     /* Usually the name of the device */
	__u8       info_len;     /* Length of device info field */

	int        gen_addr_bit; /* Need to generate a new device address? */
	int        nslots;       /* Number of slots to use when discovering */
	int        timestamp;    /* Time discovered */
} discovery_t;

void irlmp_add_discovery(hashbin_t *cachelog, discovery_t *discovery);
void irlmp_add_discovery_log(hashbin_t *cachelog, hashbin_t *log);
void irlmp_expire_discoveries(hashbin_t *log, int saddr, int force);

#endif
