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

struct baycom_modem_type {
	unsigned char modem_type;
	unsigned int options;
};

struct baycom_ioctl {
	int cmd;
	union {
		struct baycom_modem_type mt;
		struct baycom_debug_data dbg;
	} data;
};

/* -------------------------------------------------------------------- */

/*
 * modem types
 */
#define BAYCOM_MODEM_INVALID 0
#define BAYCOM_MODEM_SER12   1
#define BAYCOM_MODEM_PAR96   2

/*
 * modem options; bit mask
 */
#define BAYCOM_OPTIONS_SOFTDCD  1

/*
 * ioctl values change for baycom_net
 */
#define BAYCOMCTL_GETMODEMTYPE   0x90
#define BAYCOMCTL_SETMODEMTYPE   0x91
#define BAYCOMCTL_GETDEBUG       0x92

/* -------------------------------------------------------------------- */

#endif /* _BAYCOM_H */

/* --------------------------------------------------------------------- */
