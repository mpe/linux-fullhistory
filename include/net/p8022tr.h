#ifndef _NET_P8022TR_H
#define _NET_P8022TR_H

extern struct datalink_proto *register_8022tr_client(unsigned char type, int (*rcvfunc)(struct sk_buff *, struct device *, struct packet_type *));
extern void unregister_8022tr_client(unsigned char type);

#endif

