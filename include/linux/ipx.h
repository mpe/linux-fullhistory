#ifndef _IPX_H_
#define _IPX_H_

struct sockaddr_ipx
{
	short sipx_family;
	unsigned long  sipx_network;
	unsigned char sipx_node[6];
	short sipx_port;
	unsigned char	sipx_type;
};

struct ipx_route_def
{
	unsigned long ipx_network;
	unsigned long ipx_router_network;
#define IPX_ROUTE_NO_ROUTER	0
	unsigned char ipx_router_node[6];
	unsigned char ipx_device[16];
	unsigned short ipx_flags;
#define IPX_RT_SNAP		8
#define IPX_RT_8022		4
#define IPX_RT_BLUEBOOK		2
#define IPX_RT_ROUTED		1
};

#define IPX_MTU		576

#endif

