/*
 * The Linux BAYCOM driver for the Baycom serial 1200 baud modem
 * and the parallel 9600 baud modem
 * (C) 1996 by Thomas Sailer, HB9JNX
 */

#ifndef _BAYCOM_H
#define _BAYCOM_H

#include <linux/ioctl.h>

/* -------------------------------------------------------------------- */

struct baycom_statistics {
	unsigned long rx_packets, tx_packets;
	unsigned long ptt_keyed;
	unsigned long rx_bufferoverrun, tx_bufferoverrun;
};

struct baycom_params {
	int modem_type;
	int iobase;
	int irq;
	int options;
	int tx_delay;  /* the transmitter keyup delay in 10ms units */
	int tx_tail;   /* the transmitter keyoff delay in 10ms units */
	int slottime;  /* the slottime in 10ms; usually 10 = 100ms */
	int ppersist;  /* the p-persistence 0..255 */
	int fulldup;   /* the driver does not support full duplex, setting */
	               /* this just makes the driver send even if DCD is on */
};	

/* -------------------------------------------------------------------- */

#define BAYCOM_MAJOR 		51

/* maximum packet length, excluding CRC */
#define BAYCOM_MAXFLEN 		400	

/* the ioctl type of this driver */
#define BAYCOM_IOCTL_TYPE       'B'

#define KISS_FEND   ((unsigned char)0300)
#define KISS_FESC   ((unsigned char)0333)
#define KISS_TFEND  ((unsigned char)0334)
#define KISS_TFESC  ((unsigned char)0335)

#define KISS_CMD_DATA       0
#define KISS_CMD_TXDELAY    1
#define KISS_CMD_PPERSIST   2
#define KISS_CMD_SLOTTIME   3
#define KISS_CMD_TXTAIL     4
#define KISS_CMD_FULLDUP    5

/*
 * use bottom halves? (HDLC processing done with interrupts on or off)
 */
#define BAYCOM_USE_BH

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
 * ioctl constants
 */
#define BAYCOMCTL_GETDCD           _IOR(BAYCOM_IOCTL_TYPE, 0, unsigned char)
#define BAYCOMCTL_GETPTT           _IOR(BAYCOM_IOCTL_TYPE, 1, unsigned char)
#define BAYCOMCTL_PARAM_TXDELAY    _IO(BAYCOM_IOCTL_TYPE, 2)
#define BAYCOMCTL_PARAM_PPERSIST   _IO(BAYCOM_IOCTL_TYPE, 3)
#define BAYCOMCTL_PARAM_SLOTTIME   _IO(BAYCOM_IOCTL_TYPE, 4)
#define BAYCOMCTL_PARAM_TXTAIL     _IO(BAYCOM_IOCTL_TYPE, 5)
#define BAYCOMCTL_PARAM_FULLDUP    _IO(BAYCOM_IOCTL_TYPE, 6)

#define BAYCOMCTL_GETSTAT          _IOR(BAYCOM_IOCTL_TYPE, 7, \
					struct baycom_statistics)

#define BAYCOMCTL_GETPARAMS        _IOR(BAYCOM_IOCTL_TYPE, 8, \
					struct baycom_params)
#define BAYCOMCTL_SETPARAMS        _IOR(BAYCOM_IOCTL_TYPE, 9, \
					struct baycom_params)

#define BAYCOMCTL_CALIBRATE        _IO(BAYCOM_IOCTL_TYPE, 10)

#ifdef BAYCOM_DEBUG
/*
 * these are mainly for debugging purposes
 */
#define BAYCOMCTL_GETSAMPLES       _IOR(BAYCOM_IOCTL_TYPE, 16, unsigned char)
#define BAYCOMCTL_GETBITS          _IOR(BAYCOM_IOCTL_TYPE, 17, unsigned char)

#define BAYCOMCTL_DEBUG1           _IOR(BAYCOM_IOCTL_TYPE, 18, unsigned long)
#define BAYCOMCTL_DEBUG2           _IOR(BAYCOM_IOCTL_TYPE, 19, unsigned long)
#define BAYCOMCTL_DEBUG3           _IOR(BAYCOM_IOCTL_TYPE, 20, unsigned long)
#endif /* BAYCOM_DEBUG */

/* -------------------------------------------------------------------- */

#endif /* _BAYCOM_H */

/* --------------------------------------------------------------------- */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */
