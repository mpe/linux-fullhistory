/* $Id: byteorder.h,v 1.9 1996/08/30 05:21:34 davem Exp $ */
#ifndef _SPARC_BYTEORDER_H
#define _SPARC_BYTEORDER_H

#define ntohl(x) x
#define ntohs(x) x
#define htonl(x) x
#define htons(x) x

/* Some programs depend upon these being around. */
#define __constant_ntohl(x) x
#define __constant_ntohs(x) x
#define __constant_htonl(x) x
#define __constant_htons(x) x

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif

#ifndef __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN_BITFIELD
#endif

#endif /* !(_SPARC_BYTEORDER_H) */
