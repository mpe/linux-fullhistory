#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/fpu.h>

#define add_ssaaaa(sh, sl, ah, al, bh, bl) \
  ((sl) = (al) + (bl), (sh) = (ah) + (bh) + ((sl) < (al)))

#define sub_ddmmss(sh, sl, ah, al, bh, bl) \
  ((sl) = (al) - (bl), (sh) = (ah) - (bh) - ((al) < (bl)))

#define umul_ppmm(wh, wl, u, v)			\
  __asm__ ("mulq %2,%3,%1; umulh %2,%3,%0"	\
	   : "=r" ((UDItype)(wh)),		\
	     "=&r" ((UDItype)(wl))		\
	   : "r" ((UDItype)(u)),		\
	     "r" ((UDItype)(v)))

extern void udiv128(unsigned long, unsigned long,
		    unsigned long, unsigned long,
		    unsigned long *,
		    unsigned long *);

#define udiv_qrnnd(q, r, n1, n0, d)		\
  do {						\
    unsigned long xr, xi;			\
    udiv128((n0), (n1), 0, (d), &xr, &xi);	\
    (q) = xr; 					\
    (r) = xi;					\
  } while (0)

#define UDIV_NEEDS_NORMALIZATION 1  

#define abort()			goto bad_insn

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN -1
#endif
#define __BYTE_ORDER __LITTLE_ENDIAN
