#ifndef	__LAPBETHER_H
#define	__LAPBETHER_H

/*
 * 	Defines for the LAPBETHER pseudo device driver
 */

#ifndef __LINUX_IF_ETHER_H
#include <linux/if_ether.h>
#endif

#define SIOCSLAPBETHADDR	(SIOCDEVPRIVATE+1)
 
struct lapbeth_ethaddr {
	unsigned char destination[ETH_ALEN];
	unsigned char accept[ETH_ALEN];
};

#endif
