/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the Ethernet IEE 802.3 interface.
 *
 * Version:	@(#)if_ether.h	1.0.1	03/15/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_ETHER_H
#define _LINUX_IF_ETHER_H


/* IEEE 802.3 Ethernet magic constants. */
#define ETH_ALEN	6		/* #bytes in eth addr		*/
#define ETH_HLEN	14		/* #bytes in eth header		*/
#define ETH_ZLEN	64		/* min #bytes in frame		*/
#define ETH_FLEN	1536		/* max #bytes in frame		*/
#define ETH_DLEN	(ETH_FLEN - ETH_HLEN)	/* max #bytes of data	*/

/* These are the defined Ethernet Protocol ID's. */
#define ETH_P_LOOP	0x0060		/* Ethernet Loopback packet	*/
#define ETH_P_ECHO	0x0200		/* Ethernet Echo packet		*/
#define ETH_P_PUP	0x0400		/* Xerox PUP packet		*/
#define ETH_P_IP	0x0800		/* Internet Protocol packet	*/
#define ETH_P_ARP	0x0806		/* Address Resolution packet	*/
#define ETH_P_RARP      0x0835		/* Reverse Addr Res packet	*/

/* Define the Ethernet Broadcast Address (48 bits set to "1"). */
#define ETH_A_BCAST     "\377\377\377\377\377\377"

/* This is an Ethernet frame header. */
struct ethhdr {
  unsigned char		h_dest[ETH_ALEN];	/* destination eth addr	*/
  unsigned char		h_source[ETH_ALEN];	/* source ether addr	*/
  unsigned short	h_proto;		/* packet type ID field	*/
};

/* This is the complete Ethernet frame. */
struct ethframe {
  struct ethhdr		f_hdr;			/* frame header		*/
  char			f_data[ETH_DLEN];	/* frame data (variable)*/
};


/* Receiver modes */
#define ETH_MODE_MONITOR	1	/* Monitor mode - no receive	*/
#define ETH_MODE_PHYS		2	/* Physical address receive only */
#define ETH_MODE_BCAST		3	/* Broadcast receive + mode 2	*/
#define ETH_MODE_MCAST		4	/* Multicast receive + mode 3	*/
#define ETH_MODE_PROMISC	5	/* Promiscuous mode - receive all */


/* Ethernet statistics collection data. */
struct enet_statistics{
  int	rx_packets;			/* total packets received	*/
  int	tx_packets;			/* total packets transmitted	*/
  int	rx_errors;			/* bad packets received		*/
  int	tx_errors;			/* packet transmit problems	*/
  int	rx_dropped;			/* no space in linux buffers	*/
  int	tx_dropped;			/* no space available in linux	*/
  int	multicast;			/* multicast packets received	*/
  int	collisions;

  /* detailed rx_errors: */
  int	rx_length_errors;
  int	rx_over_errors;			/* receiver ring buff overflow	*/
  int	rx_crc_errors;			/* recved pkt with crc error	*/
  int	rx_frame_errors;		/* recv'd frame alignment error */
  int	rx_fifo_errors;			/* recv'r fifo overrun		*/
  int	rx_missed_errors;		/* receiver missed packet	*/

  /* detailed tx_errors */
  int	tx_aborted_errors;
  int	tx_carrier_errors;
  int	tx_fifo_errors;
  int	tx_heartbeat_errors;
  int	tx_window_errors;
};

#endif	/* _LINUX_IF_ETHER_H */
