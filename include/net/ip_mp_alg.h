/* ip_mp_alg.h: IPV4 multipath algorithm support.
 *
 * Copyright (C) 2004, 2005 Einar Lueck <elueck@de.ibm.com>
 * Copyright (C) 2005 David S. Miller <davem@davemloft.net>
 */

#ifndef _IP_MP_ALG_H
#define _IP_MP_ALG_H

#include <linux/config.h>
#include <net/flow.h>

static int inline multipath_comparekeys(const struct flowi *flp1,
					const struct flowi *flp2)
{
	return flp1->fl4_dst == flp2->fl4_dst &&
		flp1->fl4_src == flp2->fl4_src &&
		flp1->oif == flp2->oif &&
#ifdef CONFIG_IP_ROUTE_FWMARK
		flp1->fl4_fwmark == flp2->fl4_fwmark &&
#endif
		!((flp1->fl4_tos ^ flp2->fl4_tos) &
		  (IPTOS_RT_MASK | RTO_ONLINK));
}

#endif /* _IP_MP_ALG_H */
