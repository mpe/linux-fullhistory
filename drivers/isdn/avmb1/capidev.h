/*
 * $Id: capidev.h,v 1.1 1997/03/04 21:50:30 calle Exp $
 *
 * CAPI 2.0 Interface for Linux
 *
 * (c) Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * $Log: capidev.h,v $
 * Revision 1.1  1997/03/04 21:50:30  calle
 * Frirst version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 */

struct capidev {
	int is_open;
	int is_registered;
	__u16 applid;
	struct sk_buff_head recv_queue;
	struct wait_queue *recv_wait;
	__u16 errcode;
};

#define CAPI_MAXMINOR	CAPI_MAXAPPL
