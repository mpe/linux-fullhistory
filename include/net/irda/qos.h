/*********************************************************************
 *                
 * Filename:      qos.h
 * Version:       0.1
 * Description:   Quality of Service definitions
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Fri Sep 19 23:21:09 1997
 * Modified at:   Wed Dec  9 10:32:47 1998
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

#ifndef QOS_H
#define QOS_H

#include <linux/config.h>
#include <linux/skbuff.h>

#define PI_BAUD_RATE     0x01
#define PI_MAX_TURN_TIME 0x82
#define PI_DATA_SIZE     0x83
#define PI_WINDOW_SIZE   0x84
#define PI_ADD_BOFS      0x85
#define PI_MIN_TURN_TIME 0x86
#define PI_LINK_DISC     0x08
#define PI_COMPRESSION   0x07 /* Just a random pick */


#define IR_115200_MAX 0x3f

/* Baud rates (first byte) */
#define IR_2400    0x01
#define IR_9600    0x02
#define IR_19200   0x04
#define IR_38400   0x08
#define IR_57600   0x10
#define IR_115200  0x20
#define IR_576000  0x40
#define IR_1152000 0x80

/* Baud rates (second byte) */
#define IR_4000000 0x01

/* Quality of Service information */
typedef struct {
	int value;
	__u16 bits; /* LSB is first byte, MSB is second byte */
} qos_value_t;

struct qos_info {
	int  magic;

	qos_value_t baud_rate;       /* IR_11520O | ... */
	qos_value_t max_turn_time;
	qos_value_t data_size;
	qos_value_t window_size;
	qos_value_t additional_bofs;
	qos_value_t min_turn_time;
	qos_value_t link_disc_time;
	
	qos_value_t power;
#ifdef CONFIG_IRDA_COMPRESSION
	/* An experimental non IrDA field */
	qos_value_t compression;
#endif
};

extern int baud_rates[];
extern int data_size[];
extern int min_turn_time[];
extern int add_bofs[];
extern int compression[];

void irda_init_max_qos_capabilies( struct qos_info *qos);
void irda_qos_compute_intersection( struct qos_info *, struct qos_info *);
int  irda_insert_qos_negotiation_params( struct qos_info *qos, __u8 *fp);
void irda_qos_negotiate( struct qos_info *qos_rx, struct qos_info *qos_tx,
			 struct sk_buff *skb);

int msb_index ( __u16 byte);
int byte_value( __u8 byte, int *array);
int value_index( int value, int *array);
int index_value( int index, int *array);

void irda_qos_bits_to_value( struct qos_info *qos);

#endif





