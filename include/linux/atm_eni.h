/* atm_eni.h - Driver-specific declarations of the ENI driver (for use by
	       driver-specific utilities) */

/* Written 1995-1997 by Werner Almesberger, EPFL LRC */


#ifndef LINUX_ATM_ENI_H
#define LINUX_ATM_ENI_H

#include <linux/atmioc.h>

#define ENI_MEMDUMP     _IOW('a',ATMIOC_SARPRV,struct atmif_sioc)
                                                /* printk memory map */

#endif
