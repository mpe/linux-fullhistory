/* net/atm/tunable.h - Tunable parameters of ATM support */

/* Written 1995-1999 by Werner Almesberger, EPFL LRC/ICA */


#ifndef NET_ATM_TUNABLE_H
#define NET_ATM_TUNABLE_H

#define ATM_RXBQ_DEF	(  64*1024)  /* default RX buffer quota, in bytes */
#define ATM_TXBQ_DEF	(  64*1024)  /* default TX buffer quota, in bytes */
#define ATM_RXBQ_MIN	(   1*1024)  /* RX buffer minimum, in bytes */
#define ATM_TXBQ_MIN	(   1*1024)  /* TX buffer minimum, in bytes */
#define ATM_RXBQ_MAX	(1024*1024)  /* RX buffer quota limit, in bytes */
#define ATM_TXBQ_MAX	(1024*1024)  /* TX buffer quota limit, in bytes */

#endif
