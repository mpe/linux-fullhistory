/* $Id: isdn_concap.h,v 1.2 1998/01/31 22:49:21 keil Exp $
 */
extern struct concap_device_ops isdn_concap_reliable_dl_dops;
extern struct concap_device_ops isdn_concap_demand_dial_dops;
extern struct concap_proto * isdn_concap_new( int );


