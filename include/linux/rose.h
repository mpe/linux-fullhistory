/*
 * These are the public elements of the Linux kernel Rose implementation.
 * For kernel AX.25 see the file ax25.h. This file requires ax25.h for the
 * definition of the ax25_address structure.
 */
 
#ifndef	ROSE_KERNEL_H
#define	ROSE_KERNEL_H

#define PF_ROSE		AF_ROSE
#define ROSE_MTU	128

#define ROSE_T0		1
#define ROSE_T1		2
#define	ROSE_T2		3
#define	ROSE_T3		4
#define	ROSE_IDLE	5
#define	ROSE_HDRINCL	6
#define	ROSE_HOLDBACK	7

#define	ROSE_KILL	99

#define	SIOCRSCTLCON		(SIOCPROTOPRIVATE+0)

typedef struct {
	char rose_addr[5];
} rose_address;

struct sockaddr_rose {
	sa_family_t	srose_family;
	rose_address	srose_addr;
	ax25_address	srose_call;
	int		srose_ndigis;
	ax25_address	srose_digi;
};

struct rose_route_struct {
	rose_address  address;
	ax25_address  neighbour;
	char          device[16];
	unsigned char ndigis;
	ax25_address  digipeaters[AX25_MAX_DIGIS];
};

struct rose_ctl_struct {
	unsigned int  lci;
	char          dev[20];
	unsigned int  cmd;
	unsigned long arg;
};

#endif
