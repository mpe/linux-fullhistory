#ifndef __NET_SPX_H
#define __NET_SPX_H

/*
 *	Internal definitions for the SPX protocol.
 */
 
/*
 *	The SPX header following an IPX header.
 */
 
struct spxhdr
{
	__u8 cctl;	
#define CCTL_SPXII_XHD	0x01	/* SPX2 extended header */
#define CCTL_SPX_UNKNOWN 0x02	/* Unknown (unused ??) */
#define CCTL_SPXII_NEG	0x04	/* Negotiate size */
#define CCTL_SPXII	0x08	/* Set for SPX2 */
#define CCTL_EOM	0x10	/* End of message marker */
#define CCTL_URG	0x20	/* Urgent marker in SPP (not used in SPX?) */
#define CCTL_ACK	0x40	/* Send me an ACK */
#define CCTL_CTL	0x80	/* Control message */
	__u8 dtype;
#define SPX_DTYPE_ECONN	0xFE	/* Finished */
#define SPX_DTYPE_ECACK	0xFF	/* Ok */
	__u16 sconn;	/* Connection ID */
	__u16 dconn;	/* Connection ID */
	__u16 sequence;
	__u16 ackseq;
	__u16 allocseq;
};

#define IPXTYPE_SPX	5

	
	
	
#endif
