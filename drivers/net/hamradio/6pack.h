/*
 * 6pack.h	Define the 6pack device driver interface and constants.
 *
 * NOTE:	THIS FILE WILL BE MOVED TO THE LINUX INCLUDE DIRECTORY
 *		AS SOON AS POSSIBLE!
 *
 * Version:	@(#)6pack.h	0.3.0	04/07/98
 *
 * Fixes:
 *
 * Author:	Andreas Könsgen <ajk@iehk.rwth-aachen.de>
 *
 *		This file is derived from slip.h, written by
 *		Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */

#ifndef _LINUX_6PACK_H
#define _LINUX_6PACK_H

#define SIXPACK_VERSION    "Revision: 0.3.0"

#ifdef __KERNEL__

/* sixpack priority commands */
#define SIXP_SEOF	0x40	/* start and end of a 6pack frame */
#define SIXP_TX_URUN	0x48	/* transmit overrun */
#define SIXP_RX_ORUN	0x50	/* receive overrun */
#define SIXP_RX_BUF_OVL	0x58	/* receive buffer overflow */

#define SIXP_CHKSUM	0xFF	/* valid checksum of a 6pack frame */

/* masks to get certain bits out of the status bytes sent by the TNC */

#define SIXP_CMD_MASK		0xC0
#define SIXP_CHN_MASK		0x07
#define SIXP_PRIO_CMD_MASK	0x80
#define SIXP_STD_CMD_MASK	0x40
#define SIXP_PRIO_DATA_MASK	0x38
#define SIXP_TX_MASK		0x20
#define SIXP_RX_MASK		0x10
#define SIXP_RX_DCD_MASK	0x18
#define SIXP_LEDS_ON		0x78
#define SIXP_LEDS_OFF		0x60
#define SIXP_CON		0x08
#define SIXP_STA		0x10

#define SIXP_FOUND_TNC		0xe9
#define SIXP_CON_ON		0x68
#define SIXP_DCD_MASK		0x08
#define SIXP_DAMA_OFF		0

/* default level 2 parameters */
#define SIXP_TXDELAY			25	/* in 10 ms */
#define SIXP_PERSIST			50	/* in 256ths */
#define SIXP_SLOTTIME			10	/* in 10 ms */
#define SIXP_INIT_RESYNC_TIMEOUT	150	/* in 10 ms */
#define SIXP_RESYNC_TIMEOUT		500	/* in 10 ms */

/* 6pack configuration. */
#define SIXP_NRUNIT			256	/* MAX number of 6pack channels */
#define SIXP_MTU			256	/* Default MTU */

enum sixpack_flags {
	SIXPF_INUSE,	/* Channel in use	*/
	SIXPF_ERROR,	/* Parity, etc. error	*/
};

struct sixpack {
  int			magic;

  /* Various fields. */
  struct tty_struct	*tty;		/* ptr to TTY structure		*/
  struct device		*dev;		/* easy for intr handling	*/

  /* These are pointers to the malloc()ed frame buffers. */
  unsigned char		*rbuff;		/* receiver buffer		*/
  int                   rcount;         /* received chars counter       */
  unsigned char		*xbuff;		/* transmitter buffer		*/
  unsigned char         *xhead;         /* pointer to next byte to XMIT */
  int                   xleft;          /* bytes left in XMIT queue     */

  unsigned char		raw_buf[4];
  unsigned char		cooked_buf[400];

  unsigned int		rx_count;
  unsigned int		rx_count_cooked;

  /* 6pack interface statistics. */
  unsigned long		rx_packets;	/* inbound frames counter	*/
  unsigned long         tx_packets;     /* outbound frames counter      */
  unsigned long         rx_bytes;       /* inbound bytes counter        */
  unsigned long         tx_bytes;       /* outboud bytes counter        */
  unsigned long         rx_errors;      /* Parity, etc. errors          */
  unsigned long         tx_errors;      /* Planned stuff                */
  unsigned long         rx_dropped;     /* No memory for skb            */
  unsigned long         tx_dropped;     /* When MTU change              */
  unsigned long         rx_over_errors; /* Frame bigger then 6pack buf. */

  /* Detailed 6pack statistics. */

  int			mtu;		/* Our mtu (to spot changes!)   */
  int                   buffsize;       /* Max buffers sizes            */

  unsigned char		flags;		/* Flag values/ mode etc	*/
  unsigned char		mode;		/* 6pack mode			*/

/* 6pack stuff */
  
  unsigned char tx_delay;
  unsigned char persistance;
  unsigned char slottime;
  unsigned char duplex;
  unsigned char led_state;
  unsigned char status;
  unsigned char status1;
  unsigned char status2;
  unsigned char tx_enable;
  unsigned char tnc_ok;
  
/*  unsigned char retval; */

  struct timer_list tx_t;
  struct timer_list resync_t;
}; /* struct sixpack */


/* should later be moved to include/net/ax25.h */
#define AX25_6PACK_HEADER_LEN 0
#define SIXPACK_MAGIC 0x5304

extern int sixpack_init(struct device *dev);

#endif

#endif	/* _LINUX_6PACK.H */
