/*********************************************************************
 *                
 * Filename:      qos.c
 * Version:       0.1
 * Description:   IrLAP QoS negotiation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Sep  9 00:00:26 1997
 * Modified at:   Sat Dec 12 12:21:42 1998
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

#include <linux/config.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/qos.h>
#include <net/irda/irlap.h>
#ifdef CONFIG_IRDA_COMPRESSION
#include <net/irda/irlap_comp.h>
#include "../../drivers/net/zlib.h"

#define CI_BZIP2  27 /* Random pick */
#endif

int min_turn_time[] = { 10000, 5000, 1000, 500, 100, 50, 10, 0 };
int baud_rates[] = { 2400, 9600, 19200, 38400, 57600, 115200, 576000, 1152000,
		     4000000 };
int data_size[]  = { 64, 128, 256, 512, 1024, 2048 };
int add_bofs[]   = { 48, 24, 12, 5, 3, 2, 1, 0 };
int max_turn_time[] = { 500, 250, 100, 50 };
int link_disc_time[] = { 3, 8, 12, 16, 20, 25, 30, 40 };

#ifdef CONFIG_IRDA_COMPRESSION
int compression[] = { CI_BZIP2, CI_DEFLATE, CI_DEFLATE_DRAFT };
#endif
/*
 * Function irda_qos_compute_intersection (qos, new)
 *
 *    Compute the intersection of the old QoS capabilites with new ones
 *
 */
void irda_qos_compute_intersection( struct qos_info *qos, struct qos_info *new)
{
	ASSERT( qos != NULL, return;);
	ASSERT( new != NULL, return;);

	/* Apply */
	qos->baud_rate.bits       &= new->baud_rate.bits;
	qos->window_size.bits     &= new->window_size.bits;
	qos->min_turn_time.bits   &= new->min_turn_time.bits;
	qos->max_turn_time.bits   &= new->max_turn_time.bits;
	qos->data_size.bits       &= new->data_size.bits;
	qos->link_disc_time.bits  &= new->link_disc_time.bits;
	qos->additional_bofs.bits &= new->additional_bofs.bits;

#ifdef CONFIG_IRDA_COMPRESSION
	qos->compression.bits     &= new->compression.bits;
#endif

	irda_qos_bits_to_value( qos);
}

/*
 * Function irda_init_max_qos_capabilies (qos)
 *
 *    The purpose of this function is for layers and drivers to be able to
 *    set the maximum QoS possible and then "and in" their own limitations
 * 
 */
void irda_init_max_qos_capabilies( struct qos_info *qos)
{
	/* 
	 *  These are the maximum supported values as specified on pages
	 *  39-43 in IrLAP
	 */

	/* LSB is first byte, MSB is second byte */
	qos->baud_rate.bits     = 0x01ff; 

	qos->window_size.bits     = 0x7f;
	qos->min_turn_time.bits   = 0xff;
	qos->max_turn_time.bits   = 0x0f;
	qos->data_size.bits       = 0x3f;
	qos->link_disc_time.bits  = 0xff;
	qos->additional_bofs.bits = 0xff;

#ifdef CONFIG_IRDA_COMPRESSION	
	qos->compression.bits     = 0x03;
#endif
}

/*
 * Function irlap_negotiate (qos_device, qos_session, skb)
 *
 *    Negotiate QoS values, not really that much negotiation :-)
 *    We just set the QoS capabilities for the peer station
 *
 */
void irda_qos_negotiate( struct qos_info *qos_rx, struct qos_info *qos_tx, 
			 struct sk_buff *skb) 
{
	int n=0;
#ifdef CONFIG_IRDA_COMPRESSION
	int comp_seen = FALSE;
#endif
	__u8 length;
	__u8 *frame;
	__u8 final_byte;
	__u8 code;
	__u8 byte;
 	__u16 final_word;
	__u16_host_order word;

	ASSERT( qos_tx != NULL, return;);
	ASSERT( qos_rx != NULL, return;);
	ASSERT( skb != NULL, return;);

	frame = skb->data;

	while( n < skb->len-2) {
		code = frame[n++];
		/* Length */
		length = frame[n++];

		/*  
		 *  Get the value, since baud_rate may need two bytes, we
		 *  Just use word size all the time
		 */
		switch( length) {
		case 1:
			byte = frame[n++];
			word.word = byte; /* To make things a little easier */
			break;
		case 2:
#ifdef __LITTLE_ENDIAN
			word.byte[0] = frame[n++];
			word.byte[1] = frame[n++];
#else ifdef __BIG_ENDIAN
			word.byte[1] = frame[n++];
			word.byte[0] = frame[n++];
#endif
			byte = 0;
			break;
		default:
			DEBUG( 0, __FUNCTION__ "Error\n");
			word.word = byte = 0;
			n += length;
			
			break;
		}

		switch( code) {
		case PI_BAUD_RATE:
			/* 
			 *  Stations must agree on baud rate, so calculate
                         *  intersection 
			 */
			DEBUG( 4, "Requested BAUD_RATE: 0x%04x\n", word.word);
			final_word = word.word & qos_rx->baud_rate.bits;
			DEBUG( 4, "Final BAUD_RATE: 0x%04x\n", final_word);
			qos_tx->baud_rate.bits = final_word;
			qos_rx->baud_rate.bits = final_word;
			break;
		case PI_MAX_TURN_TIME:
			/*
			 *  Negotiated independently for each station
			 */
			DEBUG( 4, "MAX_TURN_TIME: %02x\n", byte);
			qos_tx->max_turn_time.bits = byte;
			break;
		case PI_DATA_SIZE:
			/*
			 *  Negotiated independently for each station
			 */
			DEBUG( 4, "DATA_SIZE: %02x\n", byte);
			qos_tx->data_size.bits = byte;
			break;
		case PI_WINDOW_SIZE:
			/*
			 *  Negotiated independently for each station
			 */
			qos_tx->window_size.bits = byte;
			break;
		case PI_ADD_BOFS:
			/*
			 *  Negotiated independently for each station
			 */
			DEBUG( 4, "ADD_BOFS: %02x\n", byte);
			qos_tx->additional_bofs.bits = byte;	
			break;
		case PI_MIN_TURN_TIME:
			DEBUG( 4, "MIN_TURN_TIME: %02x\n", byte);
			qos_tx->min_turn_time.bits = byte;
			break;
		case PI_LINK_DISC:
			/*  
			 *  Stations must agree on link disconnect/threshold 
			 *  time.
			 */
			DEBUG( 4, "LINK_DISC: %02x\n", byte);
			
			final_byte = byte & qos_rx->link_disc_time.bits;
			DEBUG( 4, "Final LINK_DISC: %02x\n", final_byte);
			qos_tx->link_disc_time.bits = final_byte;
			qos_rx->link_disc_time.bits = final_byte;
			break;
#ifdef CONFIG_IRDA_COMPRESSION
		case PI_COMPRESSION:
			final_byte = byte & qos_rx->compression.bits;
			qos_rx->compression.bits = byte;
			qos_tx->compression.bits = byte;
			comp_seen = TRUE;
			break;
#endif
		default:
			DEBUG( 0, __FUNCTION__ "(), Unknown value\n");
			break;
		}
	}
#ifdef CONFIG_IRDA_COMPRESSION
	if ( !comp_seen) {
		DEBUG( 4, __FUNCTION__ "(), Compression not seen!\n");
		qos_tx->compression.bits = 0x00;
		qos_rx->compression.bits = 0x00;
	}
#endif
	/* Convert the negotiated bits to values */
	irda_qos_bits_to_value( qos_tx);
	irda_qos_bits_to_value( qos_rx);
		
	DEBUG( 4, "Setting BAUD_RATE to %d bps.\n", 
	       qos_tx->baud_rate.value);
	DEBUG( 4, "Setting DATA_SIZE to %d bytes\n",
	       qos_tx->data_size.value);
	DEBUG( 4, "Setting WINDOW_SIZE to %d\n", 
	       qos_tx->window_size.value);
	DEBUG( 4, "Setting XBOFS to %d\n", 
	       qos_tx->additional_bofs.value);
	DEBUG( 4, "Setting MAX_TURN_TIME to %d ms.\n",
	       qos_tx->max_turn_time.value);
	DEBUG( 4, "Setting MIN_TURN_TIME to %d usecs.\n",
	       qos_tx->min_turn_time.value);
	DEBUG( 4, "Setting LINK_DISC to %d secs.\n", 
	       qos_tx->link_disc_time.value);
#ifdef CONFIG_IRDA_COMPRESSION
	DEBUG( 4, "Setting COMPRESSION to %d\n", 
	       qos_tx->compression.value);
#endif
	
}

/*
 * Function irlap_insert_negotiation_params (qos, fp)
 *
 *    Insert QoS negotiaion pararameters into frame
 *
 */
int irda_insert_qos_negotiation_params( struct qos_info *qos, __u8 *frame)
{
	int n;
	__u16_host_order word;

	ASSERT( qos != NULL, return 0;);
	ASSERT( frame != NULL, return 0;);

	n = 0;

	/* Set baud rate */
	if (qos->baud_rate.bits < 256) {
		frame[n++] = PI_BAUD_RATE;
		frame[n++] = 0x01;   /* length 1 */
		frame[n++] = qos->baud_rate.bits;
	} else {
		frame[n++] = PI_BAUD_RATE;
		frame[n++] = 0x02;   /* length 2 */
		
		/* 
		 *  qos->baud_rate.bits is in host byte order, so make sure
		 *  we transmit it in little endian format 
		 */
		word.word = qos->baud_rate.bits;
#ifdef __LITTLE_ENDIAN
		frame[n++] = word.byte[0]; /* LSB */
		frame[n++] = word.byte[1]; /* MSB */
#else ifdef __BIG_ENDIAN
		frame[n++] = word.byte[1]; /* LSB */
		frame[n++] = word.byte[0]; /* MSB */
#endif
	}

	/* Set Maximum Turn Around Time */
	frame[n++] = PI_MAX_TURN_TIME;
	frame[n++] = 0x01;   /* length 1 */
	frame[n++] = qos->max_turn_time.bits;
	
	/* Set data size */
	frame[n++] = PI_DATA_SIZE;
	frame[n++] = 0x01;   /* length 1 */
	frame[n++] = qos->data_size.bits;

	/* Set window size */
	frame[n++] = PI_WINDOW_SIZE;
	frame[n++] = 0x01;   /* length 1 */
	frame[n++] = qos->window_size.bits;

	/* Set additional BOFs */
	frame[n++] = PI_ADD_BOFS;
	frame[n++] = 0x01;   /* length 1 */
	frame[n++] = qos->additional_bofs.bits;

	/* Set minimum turn around time */
	frame[n++] = PI_MIN_TURN_TIME;
	frame[n++] = 0x01;   /* length 1 */
	frame[n++] = qos->min_turn_time.bits;

	/* Set Link Disconnect/Threshold Time */
	frame[n++] = PI_LINK_DISC;
	frame[n++] = 0x01;   /* length 1 */
	frame[n++] = qos->link_disc_time.bits;
#ifdef CONFIG_IRDA_COMPRESSION
	/* Set compression bits*/
	if ( qos->compression.bits) {
		DEBUG( 4, __FUNCTION__ "(), inserting compresion bits\n");
		frame[n++] = PI_COMPRESSION;
		frame[n++] = 0x01;   /* length 1 */
		frame[n++] = qos->compression.bits;
	}
#endif
	return n;
}

int byte_value( __u8 byte, int *array) 
{
	int index;

	ASSERT( array != NULL, return -1;);

	index = msb_index( byte);
	return index_value( index, array);
}


/* __u8 value_byte( int value, int *array) */
/* { */
/* 	int index; */
/* 	__u8 byte; */

/* 	index = value_index( value, array); */

/* 	byte =  */
/* } */

/*
 * Function msb_index (word)
 *
 *    Returns index to most significant bit (MSB) in word
 *
 */
int msb_index ( __u16 word) 
{
	__u16 msb = 0x8000;
	int index = 15;   /* Current MSB */
	
	while( msb) {
		if ( word & msb)
			break;   /* Found it! */
		msb >>=1;
		index--;
	}

	return index;
}

/*
 * Function value_index (value, array)
 *
 *    Returns the index to the value in the specified array
 */
int value_index( int value, int *array) 
{
	int i;
	
	for( i=0;i<8;i++)
		if ( array[i] == value)
			break;
	return i;
}

/*
 * Function index_value (index, array)
 *
 *    Returns value to index in array, easy!
 *
 */
int index_value( int index, int *array) 
{
	return array[index];
}

void irda_qos_bits_to_value( struct qos_info *qos)
{
	int index;

	ASSERT( qos != NULL, return;);
	
	index = msb_index( qos->baud_rate.bits);
	qos->baud_rate.value = baud_rates[index];

	index = msb_index( qos->data_size.bits);
	qos->data_size.value = data_size[index];

	index = msb_index( qos->window_size.bits);
	qos->window_size.value = index+1;

	index = msb_index( qos->min_turn_time.bits);
	qos->min_turn_time.value = min_turn_time[index];
	
	index = msb_index( qos->max_turn_time.bits);
	qos->max_turn_time.value = max_turn_time[index];

	index = msb_index( qos->link_disc_time.bits);
	qos->link_disc_time.value = link_disc_time[index];
	
	index = msb_index( qos->additional_bofs.bits);
	qos->additional_bofs.value = add_bofs[index];

#ifdef CONFIG_IRDA_COMPRESSION
	index = msb_index( qos->compression.bits);
	if ( index >= 0)
		qos->compression.value = compression[index];
	else 
		qos->compression.value = 0;
#endif
}

