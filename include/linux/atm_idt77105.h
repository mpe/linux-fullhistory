/* atm_idt77105.h - Driver-specific declarations of the IDT77105 driver (for
 * use by driver-specific utilities) */

/* Written 1999 by Greg Banks <gnb@linuxfan.com>. Copied from atm_suni.h. */


#ifndef LINUX_ATM_IDT77105_H
#define LINUX_ATM_IDT77105_H

#include <asm/types.h>
#include <linux/atmioc.h>

/*
 * Structure for IDT77105_GETSTAT and IDT77105_GETSTATZ ioctls.
 * Pointed to by `arg' in atmif_sioc.
 */
struct idt77105_stats {
        __u32 symbol_errors;  /* wire symbol errors */
        __u32 tx_cells;       /* cells transmitted */
        __u32 rx_cells;       /* cells received */
        __u32 rx_hec_errors;  /* Header Error Check errors on receive */
};

#define IDT77105_GETLOOP	_IOW('a',ATMIOC_PHYPRV,struct atmif_sioc)	/* get loopback mode */
#define IDT77105_SETLOOP	_IOW('a',ATMIOC_PHYPRV+1,struct atmif_sioc)	/* set loopback mode */
#define IDT77105_GETSTAT	_IOW('a',ATMIOC_PHYPRV+2,struct atmif_sioc)	/* get stats */
#define IDT77105_GETSTATZ	_IOW('a',ATMIOC_PHYPRV+3,struct atmif_sioc)	/* get stats and zero */


/*
 * TODO: what we need is a global loopback mode get/set ioctl for
 * all devices, not these device-specific hacks -- Greg Banks
 */
#define IDT77105_LM_NONE	0	/* no loopback */
#define IDT77105_LM_DIAG	1	/* diagnostic (i.e. loop TX to RX)
					 * (a.k.a. local loopback) */
#define IDT77105_LM_LOOP	2	/* line (i.e. loop RX to TX)
					 * (a.k.a. remote loopback) */

#endif
