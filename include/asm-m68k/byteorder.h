#ifndef _M68K_BYTEORDER_H
#define _M68K_BYTEORDER_H

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif

#ifndef __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN_BITFIELD
#endif

#undef ntohl
#undef ntohs
#undef htonl
#undef htons

extern unsigned long int	ntohl(unsigned long int);
extern unsigned short int	ntohs(unsigned short int);
extern unsigned long int	htonl(unsigned long int);
extern unsigned short int	htons(unsigned short int);

extern __inline__ unsigned long int	__ntohl(unsigned long int);
extern __inline__ unsigned short int	__ntohs(unsigned short int);

extern __inline__ unsigned long int
__ntohl(unsigned long int x)
{
	return x;
}

extern __inline__ unsigned short int
__ntohs(unsigned short int x)
{
	return x;
}

#define __htonl(x) __ntohl(x)
#define __htons(x) __ntohs(x)

#ifdef __OPTIMIZE__
#define ntohl(x) __ntohl(x)
#define ntohs(x) __ntohs(x)
#define htonl(x) __htonl(x)
#define htons(x) __htons(x)
#endif

#endif
