/*********************************************************************
 *                
 * Filename:      irda.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Dec  9 21:13:12 1997
 * Modified at:   Sat Jan 16 01:23:15 1999
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

#ifndef IRDA_H
#define IRDA_H

#include <linux/config.h>
#include <linux/skbuff.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE 
#define FALSE 0
#endif

#define ALIGN __attribute__((aligned))
#define PACK __attribute__((packed))

/* use 0 for production, 1 for verification, >2 for debug */
#ifdef CONFIG_IRDA_DEBUG

extern __u32 irda_debug;

#define IRDA_DEBUG 3

#define DEBUG(n, args...) if (irda_debug >= (n)) printk( KERN_DEBUG args)
#define ASSERT(expr, func) \
if(!(expr)) { \
        printk( "Assertion failed! %s,%s,%s,line=%d\n",\
        #expr,__FILE__,__FUNCTION__,__LINE__); \
        ##func}
#else
#define DEBUG(n, args...)
#define ASSERT(expr, func)
#endif /* CONFIG_IRDA_DEBUG */

#ifdef CHECK_SKB
static unsigned int check_skb = CHECK_SKB;

#define SK_FREED_SKB    0x0DE2C0DE
#define SK_GOOD_SKB     0xDEC0DED1
#define SK_HEAD_SKB     0x12231298

extern int skb_check(struct sk_buff *skb,int,int, char *);

#ifdef IS_SKB
#undef IS_SKB
#endif

#define IS_SKB(skb, func) \
if( skb_check((skb), 0, __LINE__,__FILE__) == -1) { \
       ##func}

#ifdef IS_SKB_HEAD
#undef IS_SKB_HEAD
#endif

#define IS_SKB_HEAD(skb)  skb_check((skb), 1, __LINE__,__FILE__)

#define ALLOC_SKB_MAGIC(skb) \
if( check_skb) \
        skb->magic_debug_cookie = SK_GOOD_SKB;

#define FREE_SKB_MAGIC(skb) \
if( check_skb) {\
	skb->magic_debug_cookie = SK_FREED_SKB; \
}

#else
#undef IS_SKB
#define IS_SKB(skb, func)
#undef IS_SKB_HEAD
#define IS_SKB_HEAD(skb) 
#define ALLOC_SKB_MAGIC(skb) 
#define FREE_SKB_MAGIC(skb)
#endif /* CHECK_SKB */

/*
 *  Magic numbers used by Linux/IR. Random numbers which must be unique to 
 *  give the best protection
 */
#define IRTTY_MAGIC        0x2357
#define LAP_MAGIC          0x1357
#define LMP_MAGIC          0x4321
#define LMP_LSAP_MAGIC     0x69333
#define LMP_LAP_MAGIC      0x3432
#define IRDA_DEVICE_MAGIC  0x63454
#define IAS_MAGIC          0x007
#define TTP_MAGIC          0x241169
#define TTP_TSAP_MAGIC     0x4345
#define IROBEX_MAGIC       0x341324
#define HB_MAGIC           0x64534
#define IRLAN_MAGIC        0x754
#define IAS_OBJECT_MAGIC   0x34234
#define IAS_ATTRIB_MAGIC   0x45232

#define IAS_DEVICE_ID 0x5342
#define IAS_PNP_ID    0xd342
#define IAS_OBEX_ID   0x34323
#define IAS_IRLAN_ID  0x34234
#define IAS_IRCOMM_ID 0x2343
#define IAS_IRLPT_ID  0x9876

#endif /* IRDA_H */




























