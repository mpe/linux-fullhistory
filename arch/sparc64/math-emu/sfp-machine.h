/* Machine-dependent software floating-point definitions.  Sparc64 version.
   Copyright (C) 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#define _FP_W_TYPE_SIZE		64
#define _FP_W_TYPE		unsigned long
#define _FP_WS_TYPE		signed long
#define _FP_I_TYPE		long

#define _FP_MUL_MEAT_S(R,X,Y)	_FP_MUL_MEAT_1_imm(S,R,X,Y)
#define _FP_MUL_MEAT_D(R,X,Y)	_FP_MUL_MEAT_1_wide(D,R,X,Y,umul_ppmm)
#define _FP_MUL_MEAT_Q(R,X,Y)	_FP_MUL_MEAT_2_wide(Q,R,X,Y,umul_ppmm)

#define _FP_DIV_MEAT_S(R,X,Y)	_FP_DIV_MEAT_1_imm(S,R,X,Y,_FP_DIV_HELP_imm)
#define _FP_DIV_MEAT_D(R,X,Y)	_FP_DIV_MEAT_1_udiv(D,R,X,Y)
#define _FP_DIV_MEAT_Q(R,X,Y)	_FP_DIV_MEAT_2_udiv_64(Q,R,X,Y)

#define _FP_NANFRAC_S		_FP_QNANBIT_S
#define _FP_NANFRAC_D		_FP_QNANBIT_D
#define _FP_NANFRAC_Q		_FP_QNANBIT_Q, 0

#define _FP_KEEPNANFRACP 1
#define _FP_CHOOSENAN(fs, wc, R, X, Y)				\
  do {								\
    R##_s = Y##_s;						\
    _FP_FRAC_COPY_##wc(R,Y);					\
    R##_c = FP_CLS_NAN;						\
  } while (0)

#define __FP_UNPACK_RAW_1(fs, X, val)				\
  do {								\
    union _FP_UNION_##fs *_flo =				\
    	(union _FP_UNION_##fs *)val;				\
								\
    X##_f = _flo->bits.frac;					\
    X##_e = _flo->bits.exp;					\
    X##_s = _flo->bits.sign;					\
  } while (0)

#define __FP_PACK_RAW_1(fs, val, X)				\
  do {								\
    union _FP_UNION_##fs *_flo =				\
    	(union _FP_UNION_##fs *)val;				\
								\
    _flo->bits.frac = X##_f;					\
    _flo->bits.exp  = X##_e;					\
    _flo->bits.sign = X##_s;					\
  } while (0)
  
#define __FP_UNPACK_RAW_2(fs, X, val)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
    	(union _FP_UNION_##fs *)val;			\
							\
    X##_f0 = _flo->bits.frac0;				\
    X##_f1 = _flo->bits.frac1;				\
    X##_e  = _flo->bits.exp;				\
    X##_s  = _flo->bits.sign;				\
  } while (0)

#define __FP_PACK_RAW_2(fs, val, X)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
    	(union _FP_UNION_##fs *)val;			\
							\
    _flo->bits.frac0 = X##_f0;				\
    _flo->bits.frac1 = X##_f1;				\
    _flo->bits.exp   = X##_e;				\
    _flo->bits.sign  = X##_s;				\
  } while (0)

#define __FP_UNPACK_S(X,val)		\
  do {					\
    __FP_UNPACK_RAW_1(S,X,val);		\
    _FP_UNPACK_CANONICAL(S,1,X);	\
  } while (0)

#define __FP_PACK_S(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(S,1,X);		\
    __FP_PACK_RAW_1(S,val,X);		\
  } while (0)

#define __FP_UNPACK_D(X,val)		\
  do {					\
    __FP_UNPACK_RAW_1(D,X,val);		\
    _FP_UNPACK_CANONICAL(D,1,X);	\
  } while (0)

#define __FP_PACK_D(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(D,1,X);		\
    __FP_PACK_RAW_1(D,val,X);		\
  } while (0)

#define __FP_UNPACK_Q(X,val)		\
  do {					\
    __FP_UNPACK_RAW_2(Q,X,val);		\
    _FP_UNPACK_CANONICAL(Q,2,X);	\
  } while (0)

#define __FP_PACK_Q(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(Q,2,X);		\
    __FP_PACK_RAW_2(Q,val,X);		\
  } while (0)

#include <linux/types.h>
#include <asm/byteorder.h>

#define add_ssaaaa(sh, sl, ah, al, bh, bl) 						\
  __asm__ ("addcc %4,%5,%1
  	    add %2,%3,%0
  	    bcs,a,pn %%xcc, 1f
  	    add %0, 1, %0
  	    1:"										\
	   : "=r" ((UDItype)(sh)),				      			\
	     "=&r" ((UDItype)(sl))				      			\
	   : "r" ((UDItype)(ah)),				     			\
	     "r" ((UDItype)(bh)),				      			\
	     "r" ((UDItype)(al)),				     			\
	     "r" ((UDItype)(bl))				       			\
	   : "cc")
	   
#define sub_ddmmss(sh, sl, ah, al, bh, bl) 						\
  __asm__ ("subcc %4,%5,%1
  	    sub %2,%3,%0
  	    bcs,a,pn %%xcc, 1f
  	    sub %0, 1, %0
  	    1:"										\
	   : "=r" ((UDItype)(sh)),				      			\
	     "=&r" ((UDItype)(sl))				      			\
	   : "r" ((UDItype)(ah)),				     			\
	     "r" ((UDItype)(bh)),				      			\
	     "r" ((UDItype)(al)),				     			\
	     "r" ((UDItype)(bl))				       			\
	   : "cc")
	   
#define umul_ppmm(wh, wl, u, v) 							\
  do {											\
  	long tmp1 = 0, tmp2 = 0, tmp3 = 0;						\
	  __asm__ ("mulx %2,%3,%1
  		    srlx %2,32,%4
	  	    srl %3,0,%5
  		    mulx %4,%5,%6
  		    srlx %3,32,%4
  		    srl %2,0,%5
  		    mulx %4,%5,%5
  		    srlx %2,32,%4
  		    add %5,%6,%6
  		    srlx %3,32,%5
  		    mulx %4,%5,%4
  		    srlx %6,32,%5
  		    add %4,%5,%0"							\
	   : "=r" ((UDItype)(wh)),				      			\
	     "=&r" ((UDItype)(wl))				      			\
	   : "r" ((UDItype)(u)),				     			\
	     "r" ((UDItype)(v)),				      			\
	     "r" ((UDItype)(tmp1)),				      			\
	     "r" ((UDItype)(tmp2)),				      			\
	     "r" ((UDItype)(tmp3))				      			\
	   : "cc");									\
  } while (0)
  
#define udiv_qrnnd(q, r, n1, n0, d) 							\
  do {                                                                  		\
    UWtype __d1, __d0, __q1, __q0, __r1, __r0, __m;                     		\
    __d1 = (d >> 32);                                           			\
    __d0 = (USItype)d;                                            			\
                                                                        		\
    __r1 = (n1) % __d1;                                                 		\
    __q1 = (n1) / __d1;                                                 		\
    __m = (UWtype) __q1 * __d0;                                         		\
    __r1 = (__r1 << 32) | (n0 >> 32);                          				\
    if (__r1 < __m)                                                     		\
      {                                                                 		\
        __q1--, __r1 += (d);                                            		\
        if (__r1 >= (d)) /* i.e. we didn't get carry when adding to __r1 */		\
          if (__r1 < __m)                                               		\
            __q1--, __r1 += (d);                                        		\
      }                                                                 		\
    __r1 -= __m;                                                        		\
                                                                        		\
    __r0 = __r1 % __d1;                                                 		\
    __q0 = __r1 / __d1;                                                 		\
    __m = (UWtype) __q0 * __d0;                                         		\
    __r0 = (__r0 << 32) | ((USItype)n0);                           			\
    if (__r0 < __m)                                                     		\
      {                                                                 		\
        __q0--, __r0 += (d);                                            		\
        if (__r0 >= (d))                                                		\
          if (__r0 < __m)                                               		\
            __q0--, __r0 += (d);                                        		\
      }                                                                 		\
    __r0 -= __m;                                                        		\
                                                                        		\
    (q) = (UWtype) (__q1 << 32)  | __q0;                                		\
    (r) = __r0;                                                         		\
  } while (0)

#define UDIV_NEEDS_NORMALIZATION 1  

#define abort()										\
	return 0

#ifdef __BIG_ENDIAN
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif
