/* Machine-dependent software floating-point definitions.  Sparc version.
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
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  

   Actually, this is a sparc (32bit) version, written based on the
   i386 and sparc64 versions, by me, 
   Peter Maydell (pmaydell@chiark.greenend.org.uk).
   Comments are by and large also mine, although they may be inaccurate.

   In picking out asm fragments I've gone with the lowest common
   denominator, which also happens to be the hardware I have :->
   That is, a SPARC without hardware multiply and divide.
 */


/* basic word size definitions */
#define _FP_W_TYPE_SIZE		32
#define _FP_W_TYPE		unsigned long
#define _FP_WS_TYPE		signed long
#define _FP_I_TYPE		long

/* You can optionally code some things like addition in asm. For
 * example, i386 defines __FP_FRAC_ADD_2 as asm. If you don't
 * then you get a fragment of C code [if you change an #ifdef 0
 * in op-2.h] or a call to add_ssaaaa (see below).
 * Good places to look for asm fragments to use are gcc and glibc.
 * gcc's longlong.h is useful.
 */

/* We need to know how to multiply and divide. If the host word size
 * is >= 2*fracbits you can use FP_MUL_MEAT_n_imm(t,R,X,Y) which
 * codes the multiply with whatever gcc does to 'a * b'.
 * _FP_MUL_MEAT_n_wide(t,R,X,Y,f) is used when you have an asm 
 * function that can multiply two 1W values and get a 2W result. 
 * Otherwise you're stuck with _FP_MUL_MEAT_n_hard(t,R,X,Y) which
 * does bitshifting to avoid overflow.
 * For division there is FP_DIV_MEAT_n_imm(t,R,X,Y,f) for word size
 * >= 2*fracbits, where f is either _FP_DIV_HELP_imm or 
 * _FP_DIV_HELP_ldiv (see op-1.h).
 * _FP_DIV_MEAT_udiv() is if you have asm to do 2W/1W => (1W, 1W).
 * [GCC and glibc have longlong.h which has the asm macro udiv_qrnnd
 * to do this.]
 * In general, 'n' is the number of words required to hold the type,
 * and 't' is either S, D or Q for single/double/quad.
 *           -- PMM
 */
/* Example: SPARC64:
 * #define _FP_MUL_MEAT_S(R,X,Y)	_FP_MUL_MEAT_1_imm(S,R,X,Y)
 * #define _FP_MUL_MEAT_D(R,X,Y)	_FP_MUL_MEAT_1_wide(D,R,X,Y,umul_ppmm)
 * #define _FP_MUL_MEAT_Q(R,X,Y)	_FP_MUL_MEAT_2_wide(Q,R,X,Y,umul_ppmm)
 *
 * #define _FP_DIV_MEAT_S(R,X,Y)	_FP_DIV_MEAT_1_imm(S,R,X,Y,_FP_DIV_HELP_imm)
 * #define _FP_DIV_MEAT_D(R,X,Y)	_FP_DIV_MEAT_1_udiv(D,R,X,Y)
 * #define _FP_DIV_MEAT_Q(R,X,Y)	_FP_DIV_MEAT_2_udiv_64(Q,R,X,Y)
 *
 * Example: i386:
 * #define _FP_MUL_MEAT_S(R,X,Y)   _FP_MUL_MEAT_1_wide(S,R,X,Y,_i386_mul_32_64)
 * #define _FP_MUL_MEAT_D(R,X,Y)   _FP_MUL_MEAT_2_wide(D,R,X,Y,_i386_mul_32_64)
 *
 * #define _FP_DIV_MEAT_S(R,X,Y)   _FP_DIV_MEAT_1_udiv(S,R,X,Y,_i386_div_64_32)
 * #define _FP_DIV_MEAT_D(R,X,Y)   _FP_DIV_MEAT_2_udiv_64(D,R,X,Y)
 */
#define _FP_MUL_MEAT_S(R,X,Y)   _FP_MUL_MEAT_1_wide(S,R,X,Y,umul_ppmm)
#define _FP_MUL_MEAT_D(R,X,Y)   _FP_MUL_MEAT_2_wide(D,R,X,Y,umul_ppmm)
/* FIXME: This is not implemented, but should be soon */
#define _FP_MUL_MEAT_Q(R,X,Y)   _FP_FRAC_SET_4(R, _FP_ZEROFRAC_4)
#define _FP_DIV_MEAT_S(R,X,Y)   _FP_DIV_MEAT_1_udiv(S,R,X,Y)
#define _FP_DIV_MEAT_D(R,X,Y)   _FP_DIV_MEAT_2_udiv_64(D,R,X,Y)
/* FIXME: This is not implemented, but should be soon */
#define _FP_DIV_MEAT_Q(R,X,Y)   _FP_FRAC_SET_4(R, _FP_ZEROFRAC_4)

/* These macros define what NaN looks like. They're supposed to expand to 
 * a comma-separated set of 32bit unsigned ints that encode NaN.
 */
#define _FP_NANFRAC_S		_FP_QNANBIT_S
#define _FP_NANFRAC_D		_FP_QNANBIT_D, 0
#define _FP_NANFRAC_Q           _FP_QNANBIT_Q, 0, 0, 0

#define _FP_KEEPNANFRACP 1

/* This macro appears to be called when both X and Y are NaNs, and 
 * has to choose one and copy it to R. i386 goes for the larger of the
 * two, sparc64 just picks Y. I don't understand this at all so I'll
 * go with sparc64 because it's shorter :->   -- PMM 
 */
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

#define __FP_UNPACK_RAW_4(fs, X, val)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
    	(union _FP_UNION_##fs *)val;			\
							\
    X##_f[0] = _flo->bits.frac0;			\
    X##_f[1] = _flo->bits.frac1;			\
    X##_f[2] = _flo->bits.frac2;			\
    X##_f[3] = _flo->bits.frac3;			\
    X##_e  = _flo->bits.exp;				\
    X##_s  = _flo->bits.sign;				\
  } while (0)

#define __FP_PACK_RAW_4(fs, val, X)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
    	(union _FP_UNION_##fs *)val;			\
							\
    _flo->bits.frac0 = X##_f[0];			\
    _flo->bits.frac1 = X##_f[1];			\
    _flo->bits.frac2 = X##_f[2];			\
    _flo->bits.frac3 = X##_f[3];			\
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
    __FP_UNPACK_RAW_2(D,X,val);		\
    _FP_UNPACK_CANONICAL(D,2,X);	\
  } while (0)

#define __FP_PACK_D(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(D,2,X);		\
    __FP_PACK_RAW_2(D,val,X);		\
  } while (0)

#define __FP_UNPACK_Q(X,val)		\
  do {					\
    __FP_UNPACK_RAW_4(Q,X,val);		\
    _FP_UNPACK_CANONICAL(Q,4,X);	\
  } while (0)

#define __FP_PACK_Q(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(Q,4,X);		\
    __FP_PACK_RAW_4(Q,val,X);		\
  } while (0)

/* the asm fragments go here: all these are taken from glibc-2.0.5's stdlib/longlong.h */

#include <linux/types.h>
#include <asm/byteorder.h>

/* add_ssaaaa is used in op-2.h and should be equivalent to
 * #define add_ssaaaa(sh,sl,ah,al,bh,bl) (sh = ah+bh+ (( sl = al+bl) < al))
 * add_ssaaaa(high_sum, low_sum, high_addend_1, low_addend_1,
 * high_addend_2, low_addend_2) adds two UWtype integers, composed by
 * HIGH_ADDEND_1 and LOW_ADDEND_1, and HIGH_ADDEND_2 and LOW_ADDEND_2
 * respectively.  The result is placed in HIGH_SUM and LOW_SUM.  Overflow
 * (i.e. carry out) is not stored anywhere, and is lost.
 */
#define add_ssaaaa(sh, sl, ah, al, bh, bl) \
  __asm__ ("addcc %r4,%5,%1
        addx %r2,%3,%0"                                                 \
           : "=r" ((USItype)(sh)),                                      \
             "=&r" ((USItype)(sl))                                      \
           : "%rJ" ((USItype)(ah)),                                     \
             "rI" ((USItype)(bh)),                                      \
             "%rJ" ((USItype)(al)),                                     \
             "rI" ((USItype)(bl))                                       \
           : "cc")


/* sub_ddmmss is used in op-2.h and udivmodti4.c and should be equivalent to
 * #define sub_ddmmss(sh, sl, ah, al, bh, bl) (sh = ah-bh - ((sl = al-bl) > al))
 * sub_ddmmss(high_difference, low_difference, high_minuend, low_minuend,
 * high_subtrahend, low_subtrahend) subtracts two two-word UWtype integers,
 * composed by HIGH_MINUEND_1 and LOW_MINUEND_1, and HIGH_SUBTRAHEND_2 and
 * LOW_SUBTRAHEND_2 respectively.  The result is placed in HIGH_DIFFERENCE
 * and LOW_DIFFERENCE.  Overflow (i.e. carry out) is not stored anywhere,
 * and is lost.
 */

#define sub_ddmmss(sh, sl, ah, al, bh, bl) \
  __asm__ ("subcc %r4,%5,%1
        subx %r2,%3,%0"                                                 \
           : "=r" ((USItype)(sh)),                                      \
             "=&r" ((USItype)(sl))                                      \
           : "rJ" ((USItype)(ah)),                                      \
             "rI" ((USItype)(bh)),                                      \
             "rJ" ((USItype)(al)),                                      \
             "rI" ((USItype)(bl))                                       \
           : "cc")


/* asm fragments for mul and div */	 
/* umul_ppmm(high_prod, low_prod, multipler, multiplicand) multiplies two
 * UWtype integers MULTIPLER and MULTIPLICAND, and generates a two UWtype
 * word product in HIGH_PROD and LOW_PROD.
 * These look ugly because the sun4/4c don't have umul/udiv/smul/sdiv in
 * hardware. 
 */
#define umul_ppmm(w1, w0, u, v) \
  __asm__ ("! Inlined umul_ppmm
        wr      %%g0,%2,%%y     ! SPARC has 0-3 delay insn after a wr
        sra     %3,31,%%g2      ! Don't move this insn
        and     %2,%%g2,%%g2    ! Don't move this insn
        andcc   %%g0,0,%%g1     ! Don't move this insn
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,%3,%%g1
        mulscc  %%g1,0,%%g1
        add     %%g1,%%g2,%0
        rd      %%y,%1"                                                 \
           : "=r" ((USItype)(w1)),                                      \
             "=r" ((USItype)(w0))                                       \
           : "%rI" ((USItype)(u)),                                      \
             "r" ((USItype)(v))                                         \
           : "%g1", "%g2", "cc")

/* udiv_qrnnd(quotient, remainder, high_numerator, low_numerator,
 * denominator) divides a UDWtype, composed by the UWtype integers
 * HIGH_NUMERATOR and LOW_NUMERATOR, by DENOMINATOR and places the quotient
 * in QUOTIENT and the remainder in REMAINDER.  HIGH_NUMERATOR must be less
 * than DENOMINATOR for correct operation.  If, in addition, the most
 * significant bit of DENOMINATOR must be 1, then the pre-processor symbol
 * UDIV_NEEDS_NORMALIZATION is defined to 1.
 */

#define udiv_qrnnd(q, r, n1, n0, d) \
  __asm__ ("! Inlined udiv_qrnnd
        mov     32,%%g1
        subcc   %1,%2,%%g0
1:      bcs     5f
         addxcc %0,%0,%0        ! shift n1n0 and a q-bit in lsb
        sub     %1,%2,%1        ! this kills msb of n
        addx    %1,%1,%1        ! so this can't give carry
        subcc   %%g1,1,%%g1
2:      bne     1b
         subcc  %1,%2,%%g0
        bcs     3f
         addxcc %0,%0,%0        ! shift n1n0 and a q-bit in lsb
        b       3f
         sub    %1,%2,%1        ! this kills msb of n
4:      sub     %1,%2,%1
5:      addxcc  %1,%1,%1
        bcc     2b
         subcc  %%g1,1,%%g1
! Got carry from n.  Subtract next step to cancel this carry.
        bne     4b
         addcc  %0,%0,%0        ! shift n1n0 and a 0-bit in lsb
        sub     %1,%2,%1
3:      xnor    %0,0,%0
        ! End of inline udiv_qrnnd"                                     \
           : "=&r" ((USItype) (q)),                                     \
             "=&r" ((USItype) (r))                                      \
           : "r" ((USItype) (d)),                                       \
             "1" ((USItype) (n1)),                                      \
             "0" ((USItype) (n0)) : "%g1", "cc")

#define UDIV_NEEDS_NORMALIZATION 0

#define abort()								\
	return 0

#ifdef __BIG_ENDIAN
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif

