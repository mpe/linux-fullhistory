#ifndef _M68K_BYTEORDER_H
#define _M68K_BYTEORDER_H

#ifdef __KERNEL__
#define __BIG_ENDIAN
#endif
#define __BIG_ENDIAN_BITFIELD

#define ntohl(x) x
#define ntohs(x) x
#define htonl(x) x
#define htons(x) x

#endif
