/* $Id: proto.h,v 1.1 1996/09/23 01:53:52 fritz Exp $
 *
 * not much now - just the l3 proto discriminator
 *
 * $Log: proto.h,v $
 * Revision 1.1  1996/09/23 01:53:52  fritz
 * Bugfix: discard unknown frames (non-EDSS1 and non-1TR6).
 *
 */

#ifndef	PROTO_H
#define	PROTO_H

#define	PROTO_EURO		0x08
#define	PROTO_DIS_N0	0x40
#define	PROTO_DIS_N1	0x41

#endif
