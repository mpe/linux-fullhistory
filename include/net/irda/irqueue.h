/*********************************************************************
 *                
 * Filename:      irqueue.h
 * Version:       0.3
 * Description:   General queue implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Jun  9 13:26:50 1998
 * Modified at:   Sun Oct 25 00:26:31 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (C) 1998, Aage Kvalnes <aage@cs.uit.no>
 *     Copyright (c) 1998, Dag Brattli
 *     All Rights Reserved.
 *      
 *     This code is taken from the Vortex Operating System written by Aage
 *     Kvalnes and has been ported to Linux and Linux/IR by Dag Brattli
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

#include <linux/types.h>
#include <asm/spinlock.h>

#include <net/irda/irda.h>

#ifndef QUEUE_H
#define QUEUE_H

#define NAME_SIZE      32

/*
 * Hash types
 */
#define HB_NOLOCK      0
#define HB_GLOBAL      1
#define HB_LOCAL       2
#define HB_SORTED      4

/*
 * Hash defines
 */
#define HASHBIN_SIZE   8
#define HASHBIN_MASK   0x7

typedef void (*FREE_FUNC)( void *arg);

/*
 * Hashbin
 */
#define GET_HASHBIN(x) ( x & HASHBIN_MASK )

#define QUEUE struct queue_t
struct queue_t {
	QUEUE* q_next;
	QUEUE* q_prev;

	char   q_name[ NAME_SIZE];
	__u32  q_hash;
};

typedef struct hashbin_t {
	int    magic;
	int    hb_type;
	int    hb_size;
	spinlock_t hb_mutex[ HASHBIN_SIZE ] ALIGN;
	QUEUE*     hb_queue[ HASHBIN_SIZE ] ALIGN;

	QUEUE* hb_current;
} hashbin_t;

hashbin_t *hashbin_new( int type);
int      hashbin_delete( hashbin_t* hashbin, FREE_FUNC func);
int      hashbin_clear( hashbin_t* hashbin, FREE_FUNC free_func);
void     hashbin_insert( hashbin_t* hashbin, QUEUE* entry, __u32 hashv, 
			 char* name);
void*    hashbin_find( hashbin_t* hashbin, __u32 hashv, char* name);
void*    hashbin_remove( hashbin_t* hashbin, __u32 hashv, char* name);
void*    hashbin_remove_first( hashbin_t *hashbin);
QUEUE   *hashbin_get_first( hashbin_t *hashbin);
QUEUE   *hashbin_get_next( hashbin_t *hashbin);

void enqueue_last(QUEUE **queue, QUEUE* element);
void enqueue_first(QUEUE **queue, QUEUE* element);
QUEUE *dequeue_first(QUEUE **queue);

/*
 * Function hashbin_get_size (hashbin)
 *
 *    Returns the number of elements in the hashbin
 *
 */
extern __inline__ int  hashbin_get_size( hashbin_t* hashbin) 
{
	return hashbin->hb_size;
}

#endif
