/*
 * slip.h	Define the SLIP device driver interface and constants.
 *
 * NOTE:	THIS FILE WILL BE MOVED TO THE LINUX INCLUDE DIRECTORY
 *		AS SOON AS POSSIBLE!
 *
 * Version:	@(#)slip.h	1.2.0	03/28/93
 *
 * Fixes:
 *		Alan Cox	: 	Added slip mtu field.
 *		Matt Dillon	:	Printable slip (borrowed from net2e)
 *		Alan Cox	:	Added SL_SLIP_LOTS
 *	Dmitry Gorodchanin	:	A lot of changes in the 'struct slip'
 *	Dmitry Gorodchanin	:	Added CSLIP statistics.
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */
#ifndef _LINUX_SLIP_H
#define _LINUX_SLIP_H

#include <linux/config.h>

#if defined(CONFIG_INET) && defined(CONFIG_SLIP_COMPRESSED)
# define SL_INCLUDE_CSLIP
#endif

#ifdef SL_INCLUDE_CSLIP
# define SL_MODE_DEFAULT SL_MODE_ADAPTIVE
#else
# define SL_MODE_DEFAULT SL_MODE_SLIP
#endif

/* SLIP configuration. */
#ifndef SL_SLIP_LOTS
#define SL_NRUNIT	4		/* number of SLIP channels	*/
#else
#define SL_NRUNIT	16
#endif
#define SL_MTU		296		/* 296; I am used to 600- FvK	*/

/* SLIP protocol characters. */
#define END             0300		/* indicates end of frame	*/
#define ESC             0333		/* indicates byte stuffing	*/
#define ESC_END         0334		/* ESC ESC_END means END 'data'	*/
#define ESC_ESC         0335		/* ESC ESC_ESC means ESC 'data'	*/


struct slip {
  int			magic;

  /* Various fields. */
  struct tty_struct	*tty;		/* ptr to TTY structure		*/
  struct device		*dev;		/* easy for intr handling	*/
#ifdef SL_INCLUDE_CSLIP
  struct slcompress	*slcomp;	/* for header compression 	*/
  unsigned char		*cbuff;		/* compression buffer		*/
#endif

  /* These are pointers to the malloc()ed frame buffers. */
  unsigned char		*rbuff;		/* receiver buffer		*/
  int                   rcount;         /* received chars counter       */
  unsigned char		*xbuff;		/* transmitter buffer		*/
  unsigned char         *xhead;         /* pointer to next byte to XMIT */
  int                   xleft;          /* bytes left in XMIT queue     */

  /* SLIP interface statistics. */
  unsigned long		rx_packets;	/* inbound frames counter	*/
  unsigned long         tx_packets;     /* outbound frames counter      */
  unsigned long         rx_errors;      /* Parity, etc. errors          */
  unsigned long         tx_errors;      /* Planned stuff                */
  unsigned long         rx_dropped;     /* No memory for skb            */
  unsigned long         tx_dropped;     /* When MTU change              */
  unsigned long         rx_over_errors; /* Frame bigger then SLIP buf.  */
#ifdef SL_INCLUDE_CSLIP
  unsigned long		tx_compressed;
  unsigned long		rx_compressed;
  unsigned long		tx_misses;
#endif
  /* Detailed SLIP statistics. */

  int			mtu;		/* Our mtu (to spot changes!)   */
  int                   buffsize;       /* Max buffers sizes            */

#ifdef CONFIG_SLIP_MODE_SLIP6
  int			xdata, xbits;	/* 6 bit slip controls 		*/
#endif

  unsigned char		flags;		/* Flag values/ mode etc	*/
#define SLF_INUSE	0		/* Channel in use               */
#define SLF_ESCAPE	1               /* ESC received                 */
#define SLF_ERROR	2               /* Parity, etc. error           */

  unsigned char		mode;		/* SLIP mode			*/
#define SL_MODE_SLIP	0
#define SL_MODE_CSLIP	1
#define SL_MODE_SLIP6	2		/* Matt Dillon's printable slip */
#define SL_MODE_CSLIP6	(SL_MODE_SLIP6|SL_MODE_CSLIP)
#define SL_MODE_AX25	4
#define SL_MODE_ADAPTIVE 8
};



#define SLIP_MAGIC 0x5302

extern int slip_init(struct device *dev);

#endif	/* _LINUX_SLIP.H */
