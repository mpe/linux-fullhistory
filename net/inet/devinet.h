/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the INET bits of the interfaces handler.
 *
 */

#ifndef _DEVINET_H
#define _DEVINET_H

#ifndef _DEV_H
#include "dev.h"
#endif

extern int		ip_addr_match(unsigned long addr1, unsigned long addr2);
extern int		chk_addr(unsigned long addr);
extern struct device	*dev_check(unsigned long daddr);
extern unsigned long	my_addr(void);

#endif	/* _DEVINET_H */
