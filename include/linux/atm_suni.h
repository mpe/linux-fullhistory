/* atm_suni.h - Driver-specific declarations of the SUNI driver (for use by
		driver-specific utilities) */

/* Written 1998 by Werner Almesberger, EPFL ICA */


#ifndef LINUX_ATM_SUNI_H
#define LINUX_ATM_SUNI_H

#include <linux/atmioc.h>

#define SUNI_GETLOOP	_IOR('a',ATMIOC_PHYPRV,int)	/* get loopback mode */
#define SUNI_SETLOOP	_IO('a',ATMIOC_PHYPRV+1)	/* set loopback mode */

#define SUNI_LM_NONE	0	/* no loopback */
#define SUNI_LM_DIAG	1	/* diagnostic (i.e. loop TX to RX) */
#define SUNI_LM_LOOP	2	/* line (i.e. loop RX to TX) */

#endif
