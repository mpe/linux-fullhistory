#ifndef __LINUX_IF_PACKET_H
#define __LINUX_IF_PACKET_H

struct sockaddr_pkt
{
	unsigned short spkt_family;
	unsigned char spkt_device[14];
	unsigned short spkt_protocol;
};

#endif
