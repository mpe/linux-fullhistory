/* atm_tcp.h - Driver-specific declarations of the ATMTCP driver (for use by
	       driver-specific utilities) */

/* Written 1997,1998 by Werner Almesberger, EPFL LRC/ICA */


#ifndef LINUX_ATM_TCP_H
#define LINUX_ATM_TCP_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif
#include <linux/atmioc.h>


/*
 * All values are in network byte order
 */

struct atmtcp_hdr {
	uint16_t	vpi;
	uint16_t	vci;
	uint32_t	length;		/* ... of data part */
};


#define SIOCSIFATMTCP	_IO('a',ATMIOC_ITF)	/* set ATMTCP mode */
#define ATMTCP_CREATE	_IO('a',ATMIOC_ITF+14)	/* create persistent ATMTCP
						   interface */
#define ATMTCP_REMOVE	_IO('a',ATMIOC_ITF+15)	/* destroy persistent ATMTCP
						   interface*/


#ifdef __KERNEL__

struct atm_tcp_ops {
	int (*attach)(struct atm_vcc *vcc,int itf);
	int (*create_persistent)(int itf);
	int (*remove_persistent)(int itf);
};

extern struct atm_tcp_ops atm_tcp_ops;

#endif

#endif
