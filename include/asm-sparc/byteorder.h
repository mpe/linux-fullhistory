/* $Id: byteorder.h,v 1.6 1995/11/25 02:31:20 davem Exp $ */
#ifndef _SPARC_BYTEORDER_H
#define _SPARC_BYTEORDER_H

#define ntohl(x) x
#define ntohs(x) x
#define htonl(x) x
#define htons(x) x

#ifdef __KERNEL__
#define __BIG_ENDIAN
#endif
#define __BIG_ENDIAN_BITFIELD

#endif /* !(_SPARC_BYTEORDER_H) */
