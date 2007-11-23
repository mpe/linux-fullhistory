/* $Id: isdn_x25iface.h,v 1.2 1998/01/31 22:49:23 keil Exp $
 */
#ifndef _LINUX_ISDN_X25IFACE_H
#define _LINUX_ISDN_X25IFACE_H

#define ISDN_X25IFACE_MAGIC 0x1e75a2b9
/* #define DEBUG_ISDN_X25 if you want isdn_x25 debugging messages */
#ifdef DEBUG_ISDN_X25
#   define IX25DEBUG(fmt,args...) printk(KERN_DEBUG fmt , ## args)
#else
#   define IX25DEBUG(fmt,args...)
#endif

#include <linux/skbuff.h>
#include <linux/wanrouter.h>
#include <linux/isdn.h>
#include <linux/concap.h>

extern struct concap_proto_ops * isdn_x25iface_concap_proto_ops_pt;
extern struct concap_proto     * isdn_x25iface_proto_new(void);



#endif








