/*
 * The Linux BAYCOM driver for the Baycom serial 1200 baud modem
 * and the parallel 9600 baud modem
 * (C) 1996 by Thomas Sailer, HB9JNX
 */

#ifndef _BAYCOM_H
#define _BAYCOM_H

#include <linux/sockios.h>
#include <linux/if_ether.h>

/* -------------------------------------------------------------------- */
/*
 * structs for the IOCTL commands
 */

struct baycom_debug_data {
	unsigned long debug1;
	unsigned long debug2;
	long debug3;
};

struct baycom_ioctl {
	int cmd;
	union {
		struct baycom_debug_data dbg;
	} data;
};

/* -------------------------------------------------------------------- */

/*
 * ioctl values change for baycom
 */
#define BAYCOMCTL_GETDEBUG       0x92

/* -------------------------------------------------------------------- */

#endif /* _BAYCOM_H */

/* --------------------------------------------------------------------- */
