/*
 *
 * Kernel support for NCP
 *
 * Mark Evans 1994
 *
 */

#ifndef _NCP_H
#define _NCP_H

#include <linux/ncp.h>

struct ncp_info
{
	unsigned short 	conn;		/* connection number */
	unsigned char	seq;		/* sequence number */
	ipx_socket	*ncp;		/* ncp socket */
	ipx_socket	*watchdog;	/* watchdog socket */
	ipx_socket	*mail;		/* mail socket */
};

#define NCP_TIMEOUT (3*HZ)
#define MAX_TIMEOUT 15

#endif	/* _NCP_H */
