/*---------------------------------------------------------------------------+
 |  fpu_trig.c                                                               |
 |                                                                           |
 | Implementation of the FPU "transcendental" functions.                     |
 |                                                                           |
 | Copyright (C) 1992,1993,1994                                              |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@vaxc.cc.monash.edu.au    |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "fpu_system.h"
#include "exception.h"
#include "fpu_emu.h"
#include "status_w.h"
#include "control_w.h"
#include "reg_constant.h"	


static void rem_kernel(unsigned long long st0, unsigned long long *y,
		       unsigned long long st1,
		       unsigned long long q, int n);

#define BETTER_THAN_486

#define FCOS  4
/* Not needed now with new code
#define FPTAN 1
 */

/* Used only by fptan, fsin, fcos, and fsincos. */
/* This routine produces very accurate results, similar to
   using a value of pi with more than 128 bits precision. */
/* Limited measurements show no results worse than 64 bit precision
   except for the results for arguments close to 2^63, where the
   precision of the result sometimes degrades to about 63.9 bits */
static int trig_arg(FPU_REG *X, int even)
{
  FPU_REG tmp;
  unsigned long long q;
  int old_cw = control_word, saved_status = partial_status;

  if ( X->exp >= EXP_BIAS + 63 )
    {
      partial_status |= SW_C2;     /* Reduction incomplete. */
      return -1;
    }

  control_word &= ~CW_RC;
  control_word |= RC_CHOP;

  reg_div(X, &CONST_PI2, &tmp, PR_64_BITS | RC_CHOP | 0x3f);
  round_to_int(&tmp);  /* Fortunately, this can't overflow
			  to 2^64 */
  q = significand(&tmp);
  if ( q )
    {
      rem_kernel(significand(X),
		 &significand(&tmp),
		 significand(&CONST_PI2),
		 q, X->exp - CONST_PI2.exp);
      tmp.exp = CONST_PI2.exp;
      normalize(&tmp);
      reg_move(&tmp, X);
    }

#ifdef FPTAN
  if ( even == FPTAN )
    {
      if ( ((X->exp >= EXP_BIAS) ||
	    ((X->exp == EXP_BIAS-1)
	     && (X->sigh >= 0xc90fdaa2))) ^ (q & 1) )
	even = FCOS;
      else
	even = 0;
    }
#endif FPTAN

  if ( (even && !(q & 1)) || (!even && (q & 1)) )
    {
      reg_sub(&CONST_PI2, X, X, FULL_PRECISION);
#ifdef BETTER_THAN_486
      /* So far, the results are exact but based upon a 64 bit
	 precision approximation to pi/2. The technique used
	 now is equivalent to using an approximation to pi/2 which
	 is accurate to about 128 bits. */
      if ( (X->exp <= CONST_PI2extra.exp + 64) || (q > 1) )
	{
	  /* This code gives the effect of having p/2 to better than
	     128 bits precision. */
	  significand(&tmp) = q + 1;
	  tmp.exp = EXP_BIAS + 63;
	  tmp.tag = TW_Valid;
	  normalize(&tmp);
	  reg_mul(&CONST_PI2extra, &tmp, &tmp, FULL_PRECISION);
	  reg_add(X, &tmp,  X, FULL_PRECISION);
	  if ( X->sign == SIGN_NEG )
	    {
	      /* CONST_PI2extra is negative, so the result of the addition
		 can be negative. This means that the argument is actually
		 in a different quadrant. The correction is always < pi/2,
		 so it can't overflow into yet another quadrant. */
	      X->sign = SIGN_POS;
	      q++;
	    }
	}
#endif BETTER_THAN_486
    }
#ifdef BETTER_THAN_486
  else
    {
      /* So far, the results are exact but based upon a 64 bit
	 precision approximation to pi/2. The technique used
	 now is equivalent to using an approximation to pi/2 which
	 is accurate to about 128 bits. */
      if ( ((q > 0) && (X->exp <= CONST_PI2extra.exp + 64)) || (q > 1) )
	{
	  /* This code gives the effect of having p/2 to better than
	     128 bits precision. */
	  significand(&tmp) = q;
	  tmp.exp = EXP_BIAS + 63;
	  tmp.tag = TW_Valid;
	  normalize(&tmp);
	  reg_mul(&CONST_PI2extra, &tmp, &tmp, FULL_PRECISION);
	  reg_sub(X, &tmp, X, FULL_PRECISION);
	  if ( (X->exp == CONST_PI2.exp) &&
	      ((X->sigh > CONST_PI2.sigh)
	       || ((X->sigh == CONST_PI2.sigh)
		   && (X->sigl > CONST_PI2.sigl))) )
	    {
	      /* CONST_PI2extra is negative, so the result of the
		 subtraction can be larger than pi/2. This means
		 that the argument is actually in a different quadrant.
		 The correction is always < pi/2, so it can't overflow
		 into yet another quadrant. */
	      reg_sub(&CONST_PI, X, X, FULL_PRECISION);
	      q++;
	    }
	}
    }
#endif BETTER_THAN_486

  control_word = old_cw;
  partial_status = saved_status & ~SW_C2;     /* Reduction complete. */

  return (q & 3) | even;
}


/* Convert a long to register */
void convert_l2reg(long const *arg, FPU_REG *dest)
{
  long num = *arg;

  if (num == 0)
    { reg_move(&CONST_Z, dest); return; }

  if (num > 0)
    dest->sign = SIGN_POS;
  else
    { num = -num; dest->sign = SIGN_NEG; }

  dest->sigh = num;
  dest->sigl = 0;
  dest->exp = EXP_BIAS + 31;
  dest->tag = TW_Valid;
  normalize(dest);
}


static void single_arg_error(FPU_REG *st0_ptr)
{
  switch ( st0_ptr->tag )
    {
    case TW_NaN:
      if ( !(st0_ptr->sigh & 0x40000000) )   /* Signaling ? */
	{
	  EXCEPTION(EX_Invalid);
	  if ( control_word & CW_Invalid )
	    st0_ptr->sigh |= 0x40000000;	  /* Convert to a QNaN */
	}
      break;              /* return with a NaN in st(0) */
    case TW_Empty:
      stack_underflow();  /* Puts a QNaN in st(0) */
      break;
#ifdef PARANOID
    default:
      EXCEPTION(EX_INTERNAL|0x0112);
#endif PARANOID
    }
}


static void single_arg_2_error(FPU_REG *st0_ptr)
{
  FPU_REG *st_new_ptr;

  switch ( st0_ptr->tag )
    {
    case TW_NaN:
      if ( !(st0_ptr->sigh & 0x40000000) )   /* Signaling ? */
	{
	  EXCEPTION(EX_Invalid);
	  if ( control_word & CW_Invalid )
	    {
	      /* The masked response */
	      /* Convert to a QNaN */
	      st0_ptr->sigh |= 0x40000000;
	      st_new_ptr = &st(-1);
	      push();
	      reg_move(&st(1), st_new_ptr);
	    }
	}
      else
	{
	  /* A QNaN */
	  st_new_ptr = &st(-1);
	  push();
	  reg_move(&st(1), st_new_ptr);
	}
      break;              /* return with a NaN in st(0) */
#ifdef PARANOID
    default:
      EXCEPTION(EX_INTERNAL|0x0112);
#endif PARANOID
    }
}


/*---------------------------------------------------------------------------*/

static void f2xm1(FPU_REG *st0_ptr)
{
  clear_C1();
  switch ( st0_ptr->tag )
    {
    case TW_Valid:
      {
	if ( st0_ptr->exp >= 0 )
	  {
	    /* For an 80486 FPU, the result is undefined. */
	  }
#ifdef DENORM_OPERAND
	else if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	  return;
#endif DENORM_OPERAND
	else
	  {
	    /* poly_2xm1(x) requires 0 < x < 1. */
	    poly_2xm1(st0_ptr, st0_ptr);
	  }
	if ( st0_ptr->exp <= EXP_UNDER )
	  {
	    /* A denormal result has been produced.
	       Precision must have been lost, this is always
	       an underflow. */
	    arith_underflow(st0_ptr);
	  }
	set_precision_flag_up();   /* 80486 appears to always do this */
	return;
      }
    case TW_Zero:
      return;
    case TW_Infinity:
      if ( st0_ptr->sign == SIGN_NEG )
	{
	  /* -infinity gives -1 (p16-10) */
	  reg_move(&CONST_1, st0_ptr);
	  st0_ptr->sign = SIGN_NEG;
	}
      return;
    default:
      single_arg_error(st0_ptr);
    }
}


static void fptan(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag;
  FPU_REG *st_new_ptr;
  int q;
  char arg_sign = st0_ptr->sign;

  /* Stack underflow has higher priority */
  if ( st0_tag == TW_Empty )
    {
      stack_underflow();  /* Puts a QNaN in st(0) */
      if ( control_word & CW_Invalid )
	{
	  st_new_ptr = &st(-1);
	  push();
	  stack_underflow();  /* Puts a QNaN in the new st(0) */
	}
      return;
    }

  if ( STACK_OVERFLOW )
    { stack_overflow(); return; }

  switch ( st0_tag )
    {
    case TW_Valid:
      if ( st0_ptr->exp > EXP_BIAS - 40 )
	{
	  st0_ptr->sign = SIGN_POS;
	  if ( (q = trig_arg(st0_ptr, 0)) != -1 )
	    {
	      poly_tan(st0_ptr, st0_ptr);
	      st0_ptr->sign = (q & 1) ^ arg_sign;
	    }
	  else
	    {
	      /* Operand is out of range */
	      st0_ptr->sign = arg_sign;         /* restore st(0) */
	      return;
	    }
	  set_precision_flag_up();  /* We do not really know if up or down */
	}
      else
	{
	  /* For a small arg, the result == the argument */
	  /* Underflow may happen */

	  if ( st0_ptr->exp <= EXP_UNDER )
	    {
#ifdef DENORM_OPERAND
	      if ( denormal_operand() )
		return;
#endif DENORM_OPERAND
	      /* A denormal result has been produced.
		 Precision must have been lost, this is always
		 an underflow. */
	      if ( arith_underflow(st0_ptr) )
		return;
	    }
	  set_precision_flag_down();  /* Must be down. */
	}
      push();
      reg_move(&CONST_1, st_new_ptr);
      return;
      break;
    case TW_Infinity:
      /* The 80486 treats infinity as an invalid operand */
      arith_invalid(st0_ptr);
      if ( control_word & CW_Invalid )
	{
	  st_new_ptr = &st(-1);
	  push();
	  arith_invalid(st_new_ptr);
	}
      return;
    case TW_Zero:
      push();
      reg_move(&CONST_1, st_new_ptr);
      setcc(0);
      break;
    default:
      single_arg_2_error(st0_ptr);
      break;
    }
}


static void fxtract(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag;
  FPU_REG *st_new_ptr;
  register FPU_REG *st1_ptr = st0_ptr;  /* anticipate */

  if ( STACK_OVERFLOW )
    {  stack_overflow(); return; }
  clear_C1();
  if ( !(st0_tag ^ TW_Valid) )
    {
      long e;

#ifdef DENORM_OPERAND
      if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	return;
#endif DENORM_OPERAND
	  
      push();
      reg_move(st1_ptr, st_new_ptr);
      st_new_ptr->exp = EXP_BIAS;
      e = st1_ptr->exp - EXP_BIAS;
      convert_l2reg(&e, st1_ptr);
      return;
    }
  else if ( st0_tag == TW_Zero )
    {
      char sign = st0_ptr->sign;
      if ( divide_by_zero(SIGN_NEG, st0_ptr) )
	return;
      push();
      reg_move(&CONST_Z, st_new_ptr);
      st_new_ptr->sign = sign;
      return;
    }
  else if ( st0_tag == TW_Infinity )
    {
      char sign = st0_ptr->sign;
      st0_ptr->sign = SIGN_POS;
      push();
      reg_move(&CONST_INF, st_new_ptr);
      st_new_ptr->sign = sign;
      return;
    }
  else if ( st0_tag == TW_NaN )
    {
      if ( real_2op_NaN(st0_ptr, st0_ptr, st0_ptr) )
	return;
      push();
      reg_move(st1_ptr, st_new_ptr);
      return;
    }
  else if ( st0_tag == TW_Empty )
    {
      /* Is this the correct behaviour? */
      if ( control_word & EX_Invalid )
	{
	  stack_underflow();
	  push();
	  stack_underflow();
	}
      else
	EXCEPTION(EX_StackUnder);
    }
#ifdef PARANOID
  else
    EXCEPTION(EX_INTERNAL | 0x119);
#endif PARANOID
}


static void fdecstp(FPU_REG *st0_ptr)
{
  clear_C1();
  top--;  /* st0_ptr will be fixed in math_emulate() before the next instr */
}

static void fincstp(FPU_REG *st0_ptr)
{
  clear_C1();
  top++;  /* st0_ptr will be fixed in math_emulate() before the next instr */
}


static void fsqrt_(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag;

  clear_C1();
  if ( !(st0_tag ^ TW_Valid) )
    {
      int expon;
      
      if (st0_ptr->sign == SIGN_NEG)
	{
	  arith_invalid(st0_ptr);  /* sqrt(negative) is invalid */
	  return;
	}

#ifdef DENORM_OPERAND
      if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	return;
#endif DENORM_OPERAND

      expon = st0_ptr->exp - EXP_BIAS;
      st0_ptr->exp = EXP_BIAS + (expon & 1);  /* make st(0) in  [1.0 .. 4.0) */
      
      wm_sqrt(st0_ptr, control_word);	/* Do the computation */
      
      st0_ptr->exp += expon >> 1;
      st0_ptr->sign = SIGN_POS;
    }
  else if ( st0_tag == TW_Zero )
    return;
  else if ( st0_tag == TW_Infinity )
    {
      if ( st0_ptr->sign == SIGN_NEG )
	arith_invalid(st0_ptr);  /* sqrt(-Infinity) is invalid */
      return;
    }
  else
    { single_arg_error(st0_ptr); return; }

}


static void frndint_(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag;
  int flags;

  if ( !(st0_tag ^ TW_Valid) )
    {
      if (st0_ptr->exp > EXP_BIAS+63)
	return;

#ifdef DENORM_OPERAND
      if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	return;
#endif DENORM_OPERAND

      /* Fortunately, this can't overflow to 2^64 */
      if ( (flags = round_to_int(st0_ptr)) )
	set_precision_flag(flags);

      st0_ptr->exp = EXP_BIAS + 63;
      normalize(st0_ptr);
      return;
    }
  else if ( (st0_tag == TW_Zero) || (st0_tag == TW_Infinity) )
    return;
  else
    single_arg_error(st0_ptr);
}


static void fsin(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag;
  char arg_sign = st0_ptr->sign;

  if ( st0_tag == TW_Valid )
    {
      FPU_REG rv;
      int q;

      if ( st0_ptr->exp > EXP_BIAS - 40 )
	{
	  st0_ptr->sign = SIGN_POS;
	  if ( (q = trig_arg(st0_ptr, 0)) != -1 )
	    {

	      poly_sine(st0_ptr, &rv);

	      if (q & 2)
		rv.sign ^= SIGN_POS ^ SIGN_NEG;
	      rv.sign ^= arg_sign;
	      reg_move(&rv, st0_ptr);

	      /* We do not really know if up or down */
	      set_precision_flag_up();
	      return;
	    }
	  else
	    {
	      /* Operand is out of range */
	      st0_ptr->sign = arg_sign;         /* restore st(0) */
	      return;
	    }
	}
      else
	{
	  /* For a small arg, the result == the argument */
	  /* Underflow may happen */

	  if ( st0_ptr->exp <= EXP_UNDER )
	    {
#ifdef DENORM_OPERAND
	      if ( denormal_operand() )
		return;
#endif DENORM_OPERAND
	      /* A denormal result has been produced.
		 Precision must have been lost, this is always
		 an underflow. */
	      arith_underflow(st0_ptr);
	      return;
	    }

	  set_precision_flag_up();  /* Must be up. */
	}
    }
  else if ( st0_tag == TW_Zero )
    {
      setcc(0);
      return;
    }
  else if ( st0_tag == TW_Infinity )
    {
      /* The 80486 treats infinity as an invalid operand */
      arith_invalid(st0_ptr);
      return;
    }
  else
    single_arg_error(st0_ptr);
}


static int f_cos(FPU_REG *arg)
{
  char arg_sign = arg->sign;

  if ( arg->tag == TW_Valid )
    {
      FPU_REG rv;
      int q;

      if ( arg->exp > EXP_BIAS - 40 )
	{
	  arg->sign = SIGN_POS;
	  if ( (arg->exp < EXP_BIAS)
	      || ((arg->exp == EXP_BIAS)
		  && (significand(arg) <= 0xc90fdaa22168c234LL)) )
	    {
	      poly_cos(arg, &rv);
	      reg_move(&rv, arg);

	      /* We do not really know if up or down */
	      set_precision_flag_down();
	  
	      return 0;
	    }
	  else if ( (q = trig_arg(arg, FCOS)) != -1 )
	    {
	      poly_sine(arg, &rv);

	      if ((q+1) & 2)
		rv.sign ^= SIGN_POS ^ SIGN_NEG;
	      reg_move(&rv, arg);

	      /* We do not really know if up or down */
	      set_precision_flag_down();
	  
	      return 0;
	    }
	  else
	    {
	      /* Operand is out of range */
	      arg->sign = arg_sign;         /* restore st(0) */
	      return 1;
	    }
	}
      else
	{
#ifdef DENORM_OPERAND
	  if ( (arg->exp <= EXP_UNDER) && (denormal_operand()) )
	    return 1;
#endif DENORM_OPERAND

	  setcc(0);
	  reg_move(&CONST_1, arg);
#ifdef PECULIAR_486
	  set_precision_flag_down();  /* 80486 appears to do this. */
#else
	  set_precision_flag_up();  /* Must be up. */
#endif PECULIAR_486
	  return 0;
	}
    }
  else if ( arg->tag == TW_Zero )
    {
      reg_move(&CONST_1, arg);
      setcc(0);
      return 0;
    }
  else if ( arg->tag == TW_Infinity )
    {
      /* The 80486 treats infinity as an invalid operand */
      arith_invalid(arg);
      return 1;
    }
  else
    {
      single_arg_error(arg);  /* requires arg == &st(0) */
      return 1;
    }
}


static void fcos(FPU_REG *st0_ptr)
{
  f_cos(st0_ptr);
}


static void fsincos(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag;
  FPU_REG *st_new_ptr;
  FPU_REG arg;

  /* Stack underflow has higher priority */
  if ( st0_tag == TW_Empty )
    {
      stack_underflow();  /* Puts a QNaN in st(0) */
      if ( control_word & CW_Invalid )
	{
	  st_new_ptr = &st(-1);
	  push();
	  stack_underflow();  /* Puts a QNaN in the new st(0) */
	}
      return;
    }

  if ( STACK_OVERFLOW )
    { stack_overflow(); return; }

  if ( st0_tag == TW_NaN )
    {
      single_arg_2_error(st0_ptr);
      return;
    }
  else if ( st0_tag == TW_Infinity )
    {
      /* The 80486 treats infinity as an invalid operand */
      if ( !arith_invalid(st0_ptr) )
	{
	  /* unmasked response */
	  push();
	  arith_invalid(st_new_ptr);
	}
      return;
    }

  reg_move(st0_ptr,&arg);
  if ( !f_cos(&arg) )
    {
      fsin(st0_ptr);
      push();
      reg_move(&arg,st_new_ptr);
    }

}


/*---------------------------------------------------------------------------*/
/* The following all require two arguments: st(0) and st(1) */

/* A lean, mean kernel for the fprem instructions. This relies upon
   the division and rounding to an integer in do_fprem giving an
   exact result. Because of this, rem_kernel() needs to deal only with
   the least significant 64 bits, the more significant bits of the
   result must be zero.
 */
static void rem_kernel(unsigned long long st0, unsigned long long *y,
		       unsigned long long st1,
		       unsigned long long q, int n)
{
  unsigned long long x;

  x = st0 << n;

  /* Do the required multiplication and subtraction in the one operation */
  asm volatile ("movl %2,%%eax; mull %4; subl %%eax,%0; sbbl %%edx,%1;
                 movl %3,%%eax; mull %4; subl %%eax,%1;
                 movl %2,%%eax; mull %5; subl %%eax,%1;"
		:"=m" (x), "=m" (((unsigned *)&x)[1])
		:"m" (st1),"m" (((unsigned *)&st1)[1]),
		 "m" (q),"m" (((unsigned *)&q)[1])
		:"%ax","%dx");

  *y = x;
}


/* Remainder of st(0) / st(1) */
/* This routine produces exact results, i.e. there is never any
   rounding or truncation, etc of the result. */
static void do_fprem(FPU_REG *st0_ptr, int round)
{
  FPU_REG *st1_ptr = &st(1);
  char st1_tag = st1_ptr->tag;
  char st0_tag = st0_ptr->tag;
  char sign = st0_ptr->sign;

  if ( !((st0_tag ^ TW_Valid) | (st1_tag ^ TW_Valid)) )
    {
      FPU_REG tmp;
      int old_cw = control_word;
      int expdif = st0_ptr->exp - st1_ptr->exp;
      long long q;
      unsigned short saved_status;
      int cc = 0;

#ifdef DENORM_OPERAND
      if ( ((st0_ptr->exp <= EXP_UNDER) ||
	    (st1_ptr->exp <= EXP_UNDER)) && (denormal_operand()) )
	return;
#endif DENORM_OPERAND
      
      /* We want the status following the denorm tests, but don't want
	 the status changed by the arithmetic operations. */
      saved_status = partial_status;
      control_word &= ~CW_RC;
      control_word |= RC_CHOP;

      if (expdif < 64)
	{
	  /* This should be the most common case */

	  if ( expdif > -2 )
	    {
	      reg_div(st0_ptr, st1_ptr, &tmp, PR_64_BITS | RC_CHOP | 0x3f);

	      if ( tmp.exp >= EXP_BIAS )
		{
		  round_to_int(&tmp);  /* Fortunately, this can't overflow
					  to 2^64 */
		  q = significand(&tmp);

		  rem_kernel(significand(st0_ptr),
			     &significand(&tmp),
			     significand(st1_ptr),
			     q, expdif);

		  tmp.exp = st1_ptr->exp;
		}
	      else
		{
		  reg_move(st0_ptr, &tmp);
		  q = 0;
		}
	      tmp.sign = sign;

	      if ( (round == RC_RND) && (tmp.sigh & 0xc0000000) )
		{
		  /* We may need to subtract st(1) once more,
		     to get a result <= 1/2 of st(1). */
		  unsigned long long x;
		  expdif = st1_ptr->exp - tmp.exp;
		  if ( expdif <= 1 )
		    {
		      if ( expdif == 0 )
			x = significand(st1_ptr) - significand(&tmp);
		      else /* expdif is 1 */
			x = (significand(st1_ptr) << 1) - significand(&tmp);
		      if ( (x < significand(&tmp)) ||
			  /* or equi-distant (from 0 & st(1)) and q is odd */
			  ((x == significand(&tmp)) && (q & 1) ) )
			{
			  tmp.sign ^= (SIGN_POS^SIGN_NEG);
			  significand(&tmp) = x;
			  q++;
			}
		    }
		}

	      if (q & 4) cc |= SW_C0;
	      if (q & 2) cc |= SW_C3;
	      if (q & 1) cc |= SW_C1;
	    }
	  else
	    {
	      control_word = old_cw;
	      setcc(0);
	      return;
	    }
	}
      else
	{
	  /* There is a large exponent difference ( >= 64 ) */
	  /* To make much sense, the code in this section should
	     be done at high precision. */
	  int exp_1;

	  /* prevent overflow here */
	  /* N is 'a number between 32 and 63' (p26-113) */
	  reg_move(st0_ptr, &tmp);
	  tmp.exp = EXP_BIAS + 56;
	  exp_1 = st1_ptr->exp;      st1_ptr->exp = EXP_BIAS;
	  expdif -= 56;

	  reg_div(&tmp, st1_ptr, &tmp, PR_64_BITS | RC_CHOP | 0x3f);
	  st1_ptr->exp = exp_1;

	  round_to_int(&tmp);  /* Fortunately, this can't overflow to 2^64 */

	  rem_kernel(significand(st0_ptr),
		     &significand(&tmp),
		     significand(st1_ptr),
		     significand(&tmp),
		     tmp.exp - EXP_BIAS
		     ); 
	  tmp.exp = exp_1 + expdif;
	  tmp.sign = sign;

	  /* It is possible for the operation to be complete here.
	     What does the IEEE standard say? The Intel 80486 manual
	     implies that the operation will never be completed at this
	     point, and the behaviour of a real 80486 confirms this.
	   */
	  if ( !(tmp.sigh | tmp.sigl) )
	    {
	      /* The result is zero */
	      control_word = old_cw;
	      partial_status = saved_status;
	      reg_move(&CONST_Z, st0_ptr);
	      st0_ptr->sign = sign;
#ifdef PECULIAR_486
	      setcc(SW_C2);
#else
	      setcc(0);
#endif PECULIAR_486
	      return;
	    }
	  cc = SW_C2;
	}

      control_word = old_cw;
      partial_status = saved_status;
      normalize_nuo(&tmp);
      reg_move(&tmp, st0_ptr);
      setcc(cc);

      /* The only condition to be looked for is underflow,
	 and it can occur here only if underflow is unmasked. */
      if ( (st0_ptr->exp <= EXP_UNDER) && (st0_ptr->tag != TW_Zero)
	  && !(control_word & CW_Underflow) )
	arith_underflow(st0_ptr);

      return;
    }
  else if ( (st0_tag == TW_Empty) | (st1_tag == TW_Empty) )
    {
      stack_underflow();
      return;
    }
  else if ( st0_tag == TW_Zero )
    {
      if ( st1_tag == TW_Valid )
	{
#ifdef DENORM_OPERAND
	  if ( (st1_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  setcc(0); return;
	}
      else if ( st1_tag == TW_Zero )
	{ arith_invalid(st0_ptr); return; } /* fprem(?,0) always invalid */
      else if ( st1_tag == TW_Infinity )
	{ setcc(0); return; }
    }
  else if ( st0_tag == TW_Valid )
    {
      if ( st1_tag == TW_Zero )
	{
	  arith_invalid(st0_ptr); /* fprem(Valid,Zero) is invalid */
	  return;
	}
      else if ( st1_tag != TW_NaN )
	{
#ifdef DENORM_OPERAND
	  if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  if ( st1_tag == TW_Infinity )
	    {
	      /* fprem(Valid,Infinity) is o.k. */
	      setcc(0); return;
	    }
	}
    }
  else if ( st0_tag == TW_Infinity )
    {
      if ( st1_tag != TW_NaN )
	{
	  arith_invalid(st0_ptr); /* fprem(Infinity,?) is invalid */
	  return;
	}
    }

  /* One of the registers must contain a NaN is we got here. */

#ifdef PARANOID
  if ( (st0_tag != TW_NaN) && (st1_tag != TW_NaN) )
      EXCEPTION(EX_INTERNAL | 0x118);
#endif PARANOID

  real_2op_NaN(st1_ptr, st0_ptr, st0_ptr);

}


/* ST(1) <- ST(1) * log ST;  pop ST */
static void fyl2x(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag;
  FPU_REG *st1_ptr = &st(1), exponent;
  char st1_tag = st1_ptr->tag;
  int e;

  clear_C1();
  if ( !((st0_tag ^ TW_Valid) | (st1_tag ^ TW_Valid)) )
    {
      if ( st0_ptr->sign == SIGN_POS )
	{
#ifdef DENORM_OPERAND
	  if ( ((st0_ptr->exp <= EXP_UNDER) ||
		(st1_ptr->exp <= EXP_UNDER)) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  if ( (st0_ptr->sigh == 0x80000000) && (st0_ptr->sigl == 0) )
	    {
	      /* Special case. The result can be precise. */
	      e = st0_ptr->exp - EXP_BIAS;
	      if ( e > 0 )
		{
		  exponent.sigh = e;
		  exponent.sign = SIGN_POS;
		}
	      else
		{
		  exponent.sigh = -e;
		  exponent.sign = SIGN_NEG;
		}
	      exponent.sigl = 0;
	      exponent.exp = EXP_BIAS + 31;
	      exponent.tag = TW_Valid;
	      normalize_nuo(&exponent);
	      reg_mul(&exponent, st1_ptr, st1_ptr, FULL_PRECISION);
	    }
	  else
	    {
	      /* The usual case */
	      poly_l2(st0_ptr, st1_ptr, st1_ptr);
	      if ( st1_ptr->exp <= EXP_UNDER )
		{
		  /* A denormal result has been produced.
		     Precision must have been lost, this is always
		     an underflow. */
		  arith_underflow(st1_ptr);
		}
	      else
		set_precision_flag_up();  /* 80486 appears to always do this */
	    }
	  pop();
	  return;
	}
      else
	{
	  /* negative */
	  if ( !arith_invalid(st1_ptr) )
	    pop();
	  return;
	}
    }
  else if ( (st0_tag == TW_Empty) || (st1_tag == TW_Empty) )
    {
      stack_underflow_pop(1);
      return;
    }
  else if ( (st0_tag == TW_NaN) || (st1_tag == TW_NaN) )
    {
      if ( !real_2op_NaN(st0_ptr, st1_ptr, st1_ptr) )
	pop();
      return;
    }
  else if ( (st0_tag <= TW_Zero) && (st1_tag <= TW_Zero) )
    {
      /* one of the args is zero, the other valid, or both zero */
      if ( st0_tag == TW_Zero )
	{
	  if ( st1_tag == TW_Zero )
	    {
	      /* Both args zero is invalid */
	      if ( !arith_invalid(st1_ptr) )
		pop();
	    }
#ifdef PECULIAR_486
	  /* This case is not specifically covered in the manual,
	     but divide-by-zero would seem to be the best response.
	     However, a real 80486 does it this way... */
	  else if ( st0_ptr->tag == TW_Infinity )
	    {
	      reg_move(&CONST_INF, st1_ptr);
	      pop();
	    }
#endif PECULIAR_486
	  else
	    {
	      if ( !divide_by_zero(st1_ptr->sign^SIGN_NEG^SIGN_POS, st1_ptr) )
		pop();
	    }
	  return;
	}
      else
	{
	  /* st(1) contains zero, st(0) valid <> 0 */
	  /* Zero is the valid answer */
	  char sign = st1_ptr->sign;

	  if ( st0_ptr->sign == SIGN_NEG )
	    {
	      /* log(negative) */
	      if ( !arith_invalid(st1_ptr) )
		pop();
	      return;
	    }

#ifdef DENORM_OPERAND
	  if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  if ( st0_ptr->exp < EXP_BIAS ) sign ^= SIGN_NEG^SIGN_POS;
	  pop(); st0_ptr = &st(0);
	  reg_move(&CONST_Z, st0_ptr);
	  st0_ptr->sign = sign;
	  return;
	}
    }
  /* One or both arg must be an infinity */
  else if ( st0_tag == TW_Infinity )
    {
      if ( (st0_ptr->sign == SIGN_NEG) || (st1_tag == TW_Zero) )
	{
	  /* log(-infinity) or 0*log(infinity) */
	  if ( !arith_invalid(st1_ptr) )
	    pop();
	  return;
	}
      else
	{
	  char sign = st1_ptr->sign;

#ifdef DENORM_OPERAND
	  if ( (st1_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  pop(); st0_ptr = &st(0);
	  reg_move(&CONST_INF, st0_ptr);
	  st0_ptr->sign = sign;
	  return;
	}
    }
  /* st(1) must be infinity here */
  else if ( (st0_tag == TW_Valid) && (st0_ptr->sign == SIGN_POS) )
    {
      if ( st0_ptr->exp >= EXP_BIAS )
	{
	  if ( (st0_ptr->exp == EXP_BIAS) &&
	      (st0_ptr->sigh == 0x80000000) &&
	      (st0_ptr->sigl == 0) )
	    {
	      /* st(0) holds 1.0 */
	      /* infinity*log(1) */
	      if ( !arith_invalid(st1_ptr) )
		pop();
	      return;
	    }
	  /* st(0) is positive and > 1.0 */
	  pop();
	}
      else
	{
	  /* st(0) is positive and < 1.0 */

#ifdef DENORM_OPERAND
	  if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  st1_ptr->sign ^= SIGN_NEG;
	  pop();
	}
      return;
    }
  else
    {
      /* st(0) must be zero or negative */
      if ( st0_ptr->tag == TW_Zero )
	{
	  /* This should be invalid, but a real 80486 is happy with it. */
#ifndef PECULIAR_486
	  if ( !divide_by_zero(st1_ptr->sign, st1_ptr) )
#endif PECULIAR_486
	    {
	      st1_ptr->sign ^= SIGN_NEG^SIGN_POS;
	      pop();
	    }
	}
      else
	{
	  /* log(negative) */
	  if ( !arith_invalid(st1_ptr) )
	    pop();
	}
      return;
    }
}


static void fpatan(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag;
  FPU_REG *st1_ptr = &st(1);
  char st1_tag = st1_ptr->tag;

  clear_C1();
  if ( !((st0_tag ^ TW_Valid) | (st1_tag ^ TW_Valid)) )
    {
#ifdef DENORM_OPERAND
      if ( ((st0_ptr->exp <= EXP_UNDER) ||
	    (st1_ptr->exp <= EXP_UNDER)) && (denormal_operand()) )
	return;
#endif DENORM_OPERAND

      poly_atan(st0_ptr, st1_ptr, st1_ptr);

      if ( st1_ptr->exp <= EXP_UNDER )
	{
	  /* A denormal result has been produced.
	     Precision must have been lost.
	     This is by definition an underflow. */
	  arith_underflow(st1_ptr);
	  pop();
	  return;
	}
    }
  else if ( (st0_tag == TW_Empty) || (st1_tag == TW_Empty) )
    {
      stack_underflow_pop(1);
      return;
    }
  else if ( (st0_tag == TW_NaN) || (st1_tag == TW_NaN) )
    {
      if ( !real_2op_NaN(st0_ptr, st1_ptr, st1_ptr) )
	  pop();
      return;
    }
  else if ( (st0_tag == TW_Infinity) || (st1_tag == TW_Infinity) )
    {
      char sign = st1_ptr->sign;
      if ( st0_tag == TW_Infinity )
	{
	  if ( st1_tag == TW_Infinity )
	    {
	      if ( st0_ptr->sign == SIGN_POS )
		{ reg_move(&CONST_PI4, st1_ptr); }
	      else
		reg_add(&CONST_PI4, &CONST_PI2, st1_ptr, FULL_PRECISION);
	    }
	  else
	    {
#ifdef DENORM_OPERAND
	      if ( st1_tag != TW_Zero )
		{
		  if ( (st1_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
		    return;
		}
#endif DENORM_OPERAND

	      if ( st0_ptr->sign == SIGN_POS )
		{
		  reg_move(&CONST_Z, st1_ptr);
		  st1_ptr->sign = sign;   /* An 80486 preserves the sign */
		  pop();
		  return;
		}
	      else
		reg_move(&CONST_PI, st1_ptr);
	    }
	}
      else
	{
	  /* st(1) is infinity, st(0) not infinity */
#ifdef DENORM_OPERAND
	  if ( st0_tag != TW_Zero )
	    {
	      if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
		return;
	    }
#endif DENORM_OPERAND

	  reg_move(&CONST_PI2, st1_ptr);
	}
      st1_ptr->sign = sign;
    }
  else if ( st1_tag == TW_Zero )
    {
      /* st(0) must be valid or zero */
      char sign = st1_ptr->sign;

#ifdef DENORM_OPERAND
      if ( st0_tag != TW_Zero )
	{
	  if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
	}
#endif DENORM_OPERAND

      if ( st0_ptr->sign == SIGN_POS )
	{ /* An 80486 preserves the sign */ pop(); return; }
      else
	reg_move(&CONST_PI, st1_ptr);
      st1_ptr->sign = sign;
    }
  else if ( st0_tag == TW_Zero )
    {
      /* st(1) must be TW_Valid here */
      char sign = st1_ptr->sign;

#ifdef DENORM_OPERAND
      if ( (st1_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	return;
#endif DENORM_OPERAND

      reg_move(&CONST_PI2, st1_ptr);
      st1_ptr->sign = sign;
    }
#ifdef PARANOID
  else
    EXCEPTION(EX_INTERNAL | 0x125);
#endif PARANOID

  pop();
  set_precision_flag_up();  /* We do not really know if up or down */
}


static void fprem(FPU_REG *st0_ptr)
{
  do_fprem(st0_ptr, RC_CHOP);
}


static void fprem1(FPU_REG *st0_ptr)
{
  do_fprem(st0_ptr, RC_RND);
}


static void fyl2xp1(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag, sign;
  FPU_REG *st1_ptr = &st(1);
  char st1_tag = st1_ptr->tag;

  clear_C1();
  if ( !((st0_tag ^ TW_Valid) | (st1_tag ^ TW_Valid)) )
    {
#ifdef DENORM_OPERAND
      if ( ((st0_ptr->exp <= EXP_UNDER) ||
	    (st1_ptr->exp <= EXP_UNDER)) && denormal_operand() )
	return;
#endif DENORM_OPERAND

      if ( poly_l2p1(st0_ptr, st1_ptr, st1_ptr) )
	{
#ifdef PECULIAR_486   /* Stupid 80486 doesn't worry about log(negative). */
	  st1_ptr->sign ^= SIGN_POS^SIGN_NEG;
#else
	  if ( arith_invalid(st1_ptr) )  /* poly_l2p1() returned invalid */
	    return;
#endif PECULIAR_486
	}
      if ( st1_ptr->exp <= EXP_UNDER )
	{
	  /* A denormal result has been produced.
	     Precision must have been lost, this is always
	     an underflow. */
	  sign = st1_ptr->sign;
	  arith_underflow(st1_ptr);
	  st1_ptr->sign = sign;
	}
      else
	set_precision_flag_up();   /* 80486 appears to always do this */
      pop();
      return;
    }
  else if ( (st0_tag == TW_Empty) | (st1_tag == TW_Empty) )
    {
      stack_underflow_pop(1);
      return;
    }
  else if ( st0_tag == TW_Zero )
    {
      if ( st1_tag <= TW_Zero )
	{
#ifdef DENORM_OPERAND
	  if ( (st1_tag == TW_Valid) && (st1_ptr->exp <= EXP_UNDER) &&
	      (denormal_operand()) )
	    return;
#endif DENORM_OPERAND
	  
	  st0_ptr->sign ^= st1_ptr->sign;
	  reg_move(st0_ptr, st1_ptr);
	}
      else if ( st1_tag == TW_Infinity )
	{
	  /* Infinity*log(1) */
	  if ( !arith_invalid(st1_ptr) )
	    pop();
	  return;
	}
      else if ( st1_tag == TW_NaN )
	{
	  if ( !real_2op_NaN(st0_ptr, st1_ptr, st1_ptr) )
	    pop();
	  return;
	}
#ifdef PARANOID
      else
	{
	  EXCEPTION(EX_INTERNAL | 0x116);
	  return;
	}
#endif PARANOID
      pop(); return;
    }
  else if ( st0_tag == TW_Valid )
    {
      if ( st1_tag == TW_Zero )
	{
	  if ( st0_ptr->sign == SIGN_NEG )
	    {
	      if ( st0_ptr->exp >= EXP_BIAS )
		{
		  /* st(0) holds <= -1.0 */
#ifdef PECULIAR_486   /* Stupid 80486 doesn't worry about log(negative). */
		  st1_ptr->sign ^= SIGN_POS^SIGN_NEG;
#else
		  if ( arith_invalid(st1_ptr) ) return;
#endif PECULIAR_486
		  pop(); return;
		}
#ifdef DENORM_OPERAND
	      if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
		return;
#endif DENORM_OPERAND
	      st1_ptr->sign ^= SIGN_POS^SIGN_NEG;
	      pop(); return;
	    }
#ifdef DENORM_OPERAND
	  if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND
	  pop(); return;
	}
      if ( st1_tag == TW_Infinity )
	{
	  if ( st0_ptr->sign == SIGN_NEG )
	    {
	      if ( (st0_ptr->exp >= EXP_BIAS) &&
		  !((st0_ptr->sigh == 0x80000000) &&
		    (st0_ptr->sigl == 0)) )
		{
		  /* st(0) holds < -1.0 */
#ifdef PECULIAR_486   /* Stupid 80486 doesn't worry about log(negative). */
		  st1_ptr->sign ^= SIGN_POS^SIGN_NEG;
#else
		  if ( arith_invalid(st1_ptr) ) return;
#endif PECULIAR_486
		  pop(); return;
		}
#ifdef DENORM_OPERAND
	      if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
		return;
#endif DENORM_OPERAND
	      st1_ptr->sign ^= SIGN_POS^SIGN_NEG;
	      pop(); return;
	    }
#ifdef DENORM_OPERAND
	  if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND
	  pop(); return;
	}
      if ( st1_tag == TW_NaN )
	{
	  if ( !real_2op_NaN(st0_ptr, st1_ptr, st1_ptr) )
	    pop();
	  return;
	}
    }
  else if ( st0_tag == TW_NaN )
    {
      if ( !real_2op_NaN(st0_ptr, st1_ptr, st1_ptr) )
	pop();
      return;
    }
  else if ( st0_tag == TW_Infinity )
    {
      if ( st1_tag == TW_NaN )
	{
	  if ( !real_2op_NaN(st0_ptr, st1_ptr, st1_ptr) )
	    pop();
	  return;
	}
      else if ( st0_ptr->sign == SIGN_NEG )
	{
	  int exponent = st1_ptr->exp;
#ifndef PECULIAR_486
	  /* This should have higher priority than denormals, but... */
	  if ( arith_invalid(st1_ptr) )  /* log(-infinity) */
	    return;
#endif PECULIAR_486
#ifdef DENORM_OPERAND
	  if ( st1_tag != TW_Zero )
	    {
	      if ( (exponent <= EXP_UNDER) && (denormal_operand()) )
		return;
	    }
#endif DENORM_OPERAND
#ifdef PECULIAR_486
	  /* Denormal operands actually get higher priority */
	  if ( arith_invalid(st1_ptr) )  /* log(-infinity) */
	    return;
#endif PECULIAR_486
	  pop();
	  return;
	}
      else if ( st1_tag == TW_Zero )
	{
	  /* log(infinity) */
	  if ( !arith_invalid(st1_ptr) )
	    pop();
	  return;
	}
	
      /* st(1) must be valid here. */

#ifdef DENORM_OPERAND
      if ( (st1_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	return;
#endif DENORM_OPERAND

      /* The Manual says that log(Infinity) is invalid, but a real
	 80486 sensibly says that it is o.k. */
      { char sign = st1_ptr->sign;
	reg_move(&CONST_INF, st1_ptr);
	st1_ptr->sign = sign;
      }
      pop();
      return;
    }
#ifdef PARANOID
  else
    {
      EXCEPTION(EX_INTERNAL | 0x117);
    }
#endif PARANOID
}


static void fscale(FPU_REG *st0_ptr)
{
  char st0_tag = st0_ptr->tag;
  FPU_REG *st1_ptr = &st(1);
  char st1_tag = st1_ptr->tag;
  int old_cw = control_word;
  char sign = st0_ptr->sign;

  clear_C1();
  if ( !((st0_tag ^ TW_Valid) | (st1_tag ^ TW_Valid)) )
    {
      long scale;
      FPU_REG tmp;

#ifdef DENORM_OPERAND
      if ( ((st0_ptr->exp <= EXP_UNDER) ||
	    (st1_ptr->exp <= EXP_UNDER)) && (denormal_operand()) )
	return;
#endif DENORM_OPERAND

      if ( st1_ptr->exp > EXP_BIAS + 30 )
	{
	  /* 2^31 is far too large, would require 2^(2^30) or 2^(-2^30) */
	  char sign;

	  if ( st1_ptr->sign == SIGN_POS )
	    {
	      EXCEPTION(EX_Overflow);
	      sign = st0_ptr->sign;
	      reg_move(&CONST_INF, st0_ptr);
	      st0_ptr->sign = sign;
	    }
	  else
	    {
	      EXCEPTION(EX_Underflow);
	      sign = st0_ptr->sign;
	      reg_move(&CONST_Z, st0_ptr);
	      st0_ptr->sign = sign;
	    }
	  return;
	}

      control_word &= ~CW_RC;
      control_word |= RC_CHOP;
      reg_move(st1_ptr, &tmp);
      round_to_int(&tmp);               /* This can never overflow here */
      control_word = old_cw;
      scale = st1_ptr->sign ? -tmp.sigl : tmp.sigl;
      scale += st0_ptr->exp;
      st0_ptr->exp = scale;

      /* Use round_reg() to properly detect under/overflow etc */
      round_reg(st0_ptr, 0, control_word);

      return;
    }
  else if ( st0_tag == TW_Valid )
    {
      if ( st1_tag == TW_Zero )
	{

#ifdef DENORM_OPERAND
	  if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  return;
	}
      if ( st1_tag == TW_Infinity )
	{
#ifdef DENORM_OPERAND
	  if ( (st0_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  if ( st1_ptr->sign == SIGN_POS )
	    { reg_move(&CONST_INF, st0_ptr); }
	  else
	      reg_move(&CONST_Z, st0_ptr);
	  st0_ptr->sign = sign;
	  return;
	}
      if ( st1_tag == TW_NaN )
	{ real_2op_NaN(st0_ptr, st1_ptr, st0_ptr); return; }
    }
  else if ( st0_tag == TW_Zero )
    {
      if ( st1_tag == TW_Valid )
	{

#ifdef DENORM_OPERAND
	  if ( (st1_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  return;
	}
      else if ( st1_tag == TW_Zero ) { return; }
      else if ( st1_tag == TW_Infinity )
	{
	  if ( st1_ptr->sign == SIGN_NEG )
	    return;
	  else
	    {
	      arith_invalid(st0_ptr); /* Zero scaled by +Infinity */
	      return;
	    }
	}
      else if ( st1_tag == TW_NaN )
	{ real_2op_NaN(st0_ptr, st1_ptr, st0_ptr); return; }
    }
  else if ( st0_tag == TW_Infinity )
    {
      if ( st1_tag == TW_Valid )
	{

#ifdef DENORM_OPERAND
	  if ( (st1_ptr->exp <= EXP_UNDER) && (denormal_operand()) )
	    return;
#endif DENORM_OPERAND

	  return;
	}
      if ( ((st1_tag == TW_Infinity) && (st1_ptr->sign == SIGN_POS))
	  || (st1_tag == TW_Zero) )
	return;
      else if ( st1_tag == TW_Infinity )
	{
	  arith_invalid(st0_ptr); /* Infinity scaled by -Infinity */
	  return;
	}
      else if ( st1_tag == TW_NaN )
	{ real_2op_NaN(st0_ptr, st1_ptr, st0_ptr); return; }
    }
  else if ( st0_tag == TW_NaN )
    {
      if ( st1_tag != TW_Empty )
	{ real_2op_NaN(st0_ptr, st1_ptr, st0_ptr); return; }
    }

#ifdef PARANOID
  if ( !((st0_tag == TW_Empty) || (st1_tag == TW_Empty)) )
    {
      EXCEPTION(EX_INTERNAL | 0x115);
      return;
    }
#endif

  /* At least one of st(0), st(1) must be empty */
  stack_underflow();

}


/*---------------------------------------------------------------------------*/

static FUNC_ST0 const trig_table_a[] = {
  f2xm1, fyl2x, fptan, fpatan, fxtract, fprem1, fdecstp, fincstp
};

void trig_a(void)
{
  (trig_table_a[FPU_rm])(&st(0));
}


static FUNC_ST0 const trig_table_b[] =
  {
    fprem, fyl2xp1, fsqrt_, fsincos, frndint_, fscale, fsin, fcos
  };

void trig_b(void)
{
  (trig_table_b[FPU_rm])(&st(0));
}
