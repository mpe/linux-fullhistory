#ifndef _LINUX_IF_PPP_H
#define _LINUX_IF_PPP_H

/* definitions for kernel PPP module
   Michael Callahan <callahan@maths.ox.ac.uk>
   Nov. 4 1993 */

/* how many PPP units? */
#ifdef CONFIG_PPP_LOTS
#define PPP_NRUNIT     16
#else
#define PPP_NRUNIT     4
#endif

#define PPP_VERSION  "0.2.7"

/* line discipline number */
#define N_PPP	       3

/* Magic value for the ppp structure */
#define PPP_MAGIC 0x5002

#define	PPPIOCGFLAGS	 0x5490	/* get configuration flags */
#define	PPPIOCSFLAGS	 0x5491	/* set configuration flags */
#define	PPPIOCGASYNCMAP	 0x5492	/* get async map */
#define	PPPIOCSASYNCMAP	 0x5493	/* set async map */
#define	PPPIOCGUNIT	 0x5494	/* get ppp unit number */
#define PPPIOCSINPSIG	 0x5495	/* set input ready signal */
#define PPPIOCSDEBUG	 0x5497	/* set debug level */
#define PPPIOCGDEBUG	 0x5498	/* get debug level */
#define PPPIOCGSTAT	 0x5499	/* read PPP statistic information */
#define PPPIOCGTIME	 0x549A	/* read time delta information */
#define	PPPIOCGXASYNCMAP 0x549B	/* get async table */
#define	PPPIOCSXASYNCMAP 0x549C	/* set async table */
#define PPPIOCSMRU	 0x549D	/* set receive unit size for PPP */
#define PPPIOCRASYNCMAP	 0x549E	/* set receive async map */
#define PPPIOCSMAXCID    0x549F /* set the maximum compression slot id */

/* special characters in the framing protocol */
#define	PPP_ALLSTATIONS	0xff	/* All-Stations broadcast address */
#define	PPP_UI		0x03	/* Unnumbered Information */
#define PPP_FLAG	0x7E	/* frame delimiter -- marks frame boundaries */
#define PPP_ADDRESS	0xFF	/* first character of frame   <--  (may be   */
#define PPP_CONTROL	0x03	/* second character of frame  <-- compressed)*/
#define	PPP_TRANS	0x20	/* Asynchronous transparency modifier */
#define PPP_ESC		0x7d	/* escape character -- next character is
				   data, and the PPP_TRANS bit should be
				   toggled. PPP_ESC PPP_FLAG is illegal */

/* protocol numbers */
#define PROTO_IP       0x0021
#define PROTO_VJCOMP   0x002d
#define PROTO_VJUNCOMP 0x002f

/* FCS support */
#define PPP_FCS_INIT   0xffff
#define PPP_FCS_GOOD   0xf0b8

/* initial MTU */
#define PPP_MTU	       1500

/* initial MRU */
#define PPP_MRU	       PPP_MTU

/* flags */
#define SC_COMP_PROT	0x00000001	/* protocol compression (output) */
#define SC_COMP_AC	0x00000002	/* header compression (output) */
#define	SC_COMP_TCP	0x00000004	/* TCP (VJ) compression (output) */
#define SC_NO_TCP_CCID	0x00000008	/* disable VJ connection-id comp. */
#define SC_REJ_COMP_AC	0x00000010	/* reject adrs/ctrl comp. on input */
#define SC_REJ_COMP_TCP	0x00000020	/* reject TCP (VJ) comp. on input */
#define SC_ENABLE_IP	0x00000100	/* IP packets may be exchanged */
#define SC_IP_DOWN	0x00000200	/* give ip frames to pppd */
#define SC_IP_FLUSH	0x00000400	/* "next time" flag for IP_DOWN */
#define SC_DEBUG	0x00010000	/* enable debug messages */
#define SC_LOG_INPKT	0x00020000	/* log contents of good pkts recvd */
#define SC_LOG_OUTPKT	0x00040000	/* log contents of pkts sent */
#define SC_LOG_RAWIN	0x00080000	/* log all chars received */
#define SC_LOG_FLUSH	0x00100000	/* log all chars flushed */

/* Flag bits to determine state of input characters */
#define SC_RCV_B7_0	0x01000000	/* have rcvd char with bit 7 = 0 */
#define SC_RCV_B7_1	0x02000000	/* have rcvd char with bit 7 = 0 */
#define SC_RCV_EVNP	0x04000000	/* have rcvd char with even parity */
#define SC_RCV_ODDP	0x08000000	/* have rcvd char with odd parity */

#define	SC_MASK		0x0fffffff	/* bits that user can change */

/* flag for doing transmitter lockout */
#define SC_XMIT_BUSY	0x10000000	/* ppp_write_wakeup is active */

/*
 * This is the format of the data buffer of a LQP packet. The packet data
 * is sent/received to the peer.
 */

struct ppp_lqp_packet_hdr {
  __u32		LastOutLQRs;	/* Copied from PeerOutLQRs	 */
  __u32		LastOutPackets; /* Copied from PeerOutPackets	 */
  __u32		LastOutOctets;	/* Copied from PeerOutOctets	 */
  __u32		PeerInLQRs;	/* Copied from SavedInLQRs	 */
  __u32		PeerInPackets;	/* Copied from SavedInPackets	 */
  __u32		PeerInDiscards; /* Copied from SavedInDiscards	 */
  __u32		PeerInErrors;	/* Copied from SavedInErrors	 */
  __u32		PeerInOctets;	/* Copied from SavedInOctets	 */
  __u32		PeerOutLQRs;	/* Copied from OutLQRs, plus 1	 */
  __u32		PeerOutPackets; /* Current ifOutUniPackets, + 1	 */
  __u32		PeerOutOctets;	/* Current ifOutOctets + LQR	 */
  };

/*
 * This data is not sent to the remote. It is updated by the driver when
 * a packet is received.
 */

struct ppp_lqp_packet_trailer {
  __u32		SaveInLQRs;	/* Current InLQRs on reception	 */
  __u32		SaveInPackets;	/* Current ifInUniPackets	 */
  __u32		SaveInDiscards; /* Current ifInDiscards		 */
  __u32		SaveInErrors;	/* Current ifInErrors		 */
  __u32		SaveInOctets;	/* Current ifInOctects		 */
};

/*
 * PPP LQP packet. The packet is changed by the driver immediately prior
 * to transmission and updated upon reception with the current values.
 * So, it must be known to the driver as well as the pppd software.
 */

struct ppp_lpq_packet {
  __u32				magic;	/* current magic value		 */
  struct ppp_lqp_packet_hdr	hdr;	/* Header fields for structure	 */
  struct ppp_lqp_packet_trailer tail;	/* Trailer fields (not sent)	 */
};

/*
 * PPP interface statistics. (used by LQP / pppstats)
 */

struct ppp_stats {
  __u32		rbytes;		/* bytes received		 */
  __u32		rcomp;		/* compressed packets received	 */
  __u32		runcomp;	/* uncompressed packets received */
  __u32		rothers;	/* non-ip frames received	 */
  __u32		rerrors;	/* received errors		 */
  __u32		roverrun;	/* "buffer overrun" counter	 */
  __u32		tossed;		/* packets discarded		 */
  __u32		runts;		/* frames too short to process	 */
  __u32		rgiants;	/* frames too large to process	 */
  __u32		sbytes;		/* bytes sent			 */
  __u32		scomp;		/* compressed packets sent	 */
  __u32		suncomp;	/* uncompressed packets sent	 */
  __u32		sothers;	/* non-ip frames sent		 */
  __u32		serrors;	/* transmitter errors		 */
  __u32		sbusy;		/* "transmitter busy" counter	 */
};

/*
 * Demand dial fields
 */

struct ppp_ddinfo {
  unsigned long		ip_sjiffies;	/* time when last IP frame sent */
  unsigned long		ip_rjiffies;	/* time when last IP frame recvd*/
  unsigned long		nip_sjiffies;	/* time when last NON-IP sent	*/
  unsigned long		nip_rjiffies;	/* time when last NON-IP recvd	*/
};

#ifdef __KERNEL__

struct ppp {
  int			magic;		/* magic value for structure	*/

  /* Bitmapped flag fields. */
  char			sending;	/* "channel busy" indicator	*/
  char			escape;		/* 0x20 if prev char was PPP_ESC*/
  char			toss;		/* toss this frame		*/
  unsigned long		inuse;		/* are we allocated?		*/

  unsigned int		flags;		/* miscellany			*/

  __u32			xmit_async_map[8]; /* 1 bit means that given control 
					   character is quoted on output*/

  __u32			recv_async_map; /* 1 bit means that given control 
					   character is ignored on input*/
  int			mtu;		/* maximum xmit frame size	*/
  int			mru;		/* maximum receive frame size	*/
  unsigned short	fcs;		/* FCS field of current frame	*/

  /* Various fields. */
  int			line;		/* PPP channel number		*/
  struct tty_struct	*tty;		/* ptr to TTY structure		*/
  struct device		*dev;		/* easy for intr handling	*/
  struct slcompress	*slcomp;	/* for header compression	*/
  __u32			last_xmit;	/* time of last transmission	*/

  /* These are pointers to the malloc()ed frame buffers.
     These buffers are used while processing a packet.	If a packet
     has to hang around for the user process to read it, it lingers in
     the user buffers below. */
  unsigned char		*rbuff;		/* receiver buffer		*/
  unsigned char		*xbuff;		/* transmitter buffer		*/
  unsigned char		*cbuff;		/* compression buffer		*/

  /* These are the various pointers into the buffers. */
  unsigned char		*rhead;		/* RECV buffer pointer (head)	*/
  unsigned char		*rend;		/* RECV buffer pointer (end)	*/
  int			rcount;		/* PPP receive counter		*/
  unsigned char		*xhead;		/* XMIT buffer pointer (head)	*/
  unsigned char 	*xtail;		/* XMIT buffer pointer (end) 	*/

  /* Structures for interfacing with the user process. */
#define RBUFSIZE 4000
  unsigned char		*us_rbuff;	/* circular incoming packet buf.*/
  unsigned char		*us_rbuff_end;	/* end of allocated space	*/
  unsigned char		*us_rbuff_head; /* head of waiting packets	*/
  unsigned char		*us_rbuff_tail; /* tail of waiting packets	*/
  unsigned long		us_rbuff_lock;	/* lock: bit 0 head bit 1 tail	*/
  int			inp_sig;	/* input ready signal for pgrp	*/
  int			inp_sig_pid;	/* process to get notified	*/

  /* items to support the select() function */
  struct wait_queue	*write_wait;	/* queue for reading processes	*/
  struct wait_queue	*read_wait;	/* queue for writing processes	*/

  /* PPP interface statistics. */
  struct ppp_stats	stats;		/* statistic information	*/

  /* PPP demand dial information. */
  struct ppp_ddinfo	ddinfo;		/* demand dial information	*/
};

#endif	/* __KERNEL__ */
#endif	/* _LINUX_PPP_H */


