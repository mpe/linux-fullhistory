/*---------------------------------------------------------------------------+
 |  poly_l2.c                                                                |
 |                                                                           |
 | Compute the base 2 log of a FPU_REG, using a polynomial approximation.    |
 |                                                                           |
 | Copyright (C) 1992,1993,1994                                              |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@vaxc.cc.monash.edu.au    |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/


#include "exception.h"
#include "reg_constant.h"
#include "fpu_emu.h"
#include "control_w.h"
#include "poly.h"



static void log2_kernel(FPU_REG const *arg,
			Xsig *accum_result, long int *expon);


/*--- poly_l2() -------------------------------------------------------------+
 |   Base 2 logarithm by a polynomial approximation.                         |
 +---------------------------------------------------------------------------*/
void	poly_l2(FPU_REG const *arg, FPU_REG const *y, FPU_REG *result)
{
  long int	       exponent, expon, expon_expon;
  Xsig                 accumulator, expon_accum, yaccum;
  char		       sign;
  FPU_REG              x;


  exponent = arg->exp - EXP_BIAS;

  /* From arg, make a number > sqrt(2)/2 and < sqrt(2) */
  if ( arg->sigh > (unsigned)0xb504f334 )
    {
      /* Treat as  sqrt(2)/2 < arg < 1 */
      significand(&x) = - significand(arg);
      x.sign = SIGN_NEG;
      x.tag = TW_Valid;
      x.exp = EXP_BIAS-1;
      exponent++;
      normalize(&x);
    }
  else
    {
      /* Treat as  1 <= arg < sqrt(2) */
      x.sigh = arg->sigh - 0x80000000;
      x.sigl = arg->sigl;
      x.sign = SIGN_POS;
      x.tag = TW_Valid;
      x.exp = EXP_BIAS;
      normalize(&x);
    }

  if ( x.tag == TW_Zero )
    {
      expon = 0;
      accumulator.msw = accumulator.midw = accumulator.lsw = 0;
    }
  else
    {
      log2_kernel(&x, &accumulator, &expon);
    }

  sign = exponent < 0;
  if ( sign ) exponent = -exponent;
  expon_accum.msw = exponent; expon_accum.midw = expon_accum.lsw = 0;
  if ( exponent )
    {
      expon_expon = 31 + norm_Xsig(&expon_accum);
      shr_Xsig(&accumulator, expon_expon - expon);

      if ( sign ^ (x.sign == SIGN_NEG) )
	negate_Xsig(&accumulator);
      add_Xsig_Xsig(&accumulator, &expon_accum);
    }
  else
    {
      expon_expon = expon;
      sign = x.sign;
    }

  yaccum.lsw = 0; XSIG_LL(yaccum) = significand(y);
  mul_Xsig_Xsig(&accumulator, &yaccum);

  expon_expon += round_Xsig(&accumulator);

  if ( accumulator.msw == 0 )
    {
      reg_move(&CONST_Z, y);
    }
  else
    {
      result->exp = expon_expon + y->exp + 1;
      significand(result) = XSIG_LL(accumulator);
      result->tag = TW_Valid; /* set the tags to Valid */
      result->sign = sign ^ y->sign;
    }

  return;
}


/*--- poly_l2p1() -----------------------------------------------------------+
 |   Base 2 logarithm by a polynomial approximation.                         |
 |   log2(x+1)                                                               |
 +---------------------------------------------------------------------------*/
int	poly_l2p1(FPU_REG const *arg, FPU_REG const *y, FPU_REG *result)
{
  char                 sign;
  long int             exponent;
  Xsig                 accumulator, yaccum;


  sign = arg->sign;

  if ( arg->exp < EXP_BIAS )
    {
      log2_kernel(arg, &accumulator, &exponent);

      yaccum.lsw = 0;
      XSIG_LL(yaccum) = significand(y);
      mul_Xsig_Xsig(&accumulator, &yaccum);

      exponent += round_Xsig(&accumulator);

      result->exp = exponent + y->exp + 1;
      significand(result) = XSIG_LL(accumulator);
      result->tag = TW_Valid; /* set the tags to Valid */
      result->sign = sign ^ y->sign;

      return 0;
    }
  else
    {
      /* The magnitude of arg is far too large. */
      reg_move(y, result);
      if ( sign != SIGN_POS )
	{
	  /* Trying to get the log of a negative number. */
	  return 1;
	}
      else
	{
	  return 0;
	}
    }

}




#undef HIPOWER
#define	HIPOWER	10
static const unsigned long long logterms[HIPOWER] =
{
  0x2a8eca5705fc2ef0LL,
  0xf6384ee1d01febceLL,
  0x093bb62877cdf642LL,
  0x006985d8a9ec439bLL,
  0x0005212c4f55a9c8LL,
  0x00004326a16927f0LL,
  0x0000038d1d80a0e7LL,
  0x0000003141cc80c6LL,
  0x00000002b1668c9fLL,
  0x000000002c7a46aaLL
};

static const unsigned long leadterm = 0xb8000000;


/*--- log2_kernel() ---------------------------------------------------------+
 |   Base 2 logarithm by a polynomial approximation.                         |
 |   log2(x+1)                                                               |
 +---------------------------------------------------------------------------*/
static void log2_kernel(FPU_REG const *arg, Xsig *accum_result,
			long int *expon)
{
  char                 sign;
  long int             exponent, adj;
  unsigned long long   Xsq;
  Xsig                 accumulator, Numer, Denom, argSignif, arg_signif;

  sign = arg->sign;

  exponent = arg->exp - EXP_BIAS;
  Numer.lsw = Denom.lsw = 0;
  XSIG_LL(Numer) = XSIG_LL(Denom) = significand(arg);
  if ( sign == SIGN_POS )
    {
      shr_Xsig(&Denom, 2 - (1 + exponent));
      Denom.msw |= 0x80000000;
      div_Xsig(&Numer, &Denom, &argSignif);
    }
  else
    {
      shr_Xsig(&Denom, 1 - (1 + exponent));
      negate_Xsig(&Denom);
      if ( Denom.msw & 0x80000000 )
	{
	  div_Xsig(&Numer, &Denom, &argSignif);
	  exponent ++;
	}
      else
	{
	  /* Denom must be 1.0 */
	  argSignif.lsw = Numer.lsw; argSignif.midw = Numer.midw;
	  argSignif.msw = Numer.msw;
	}
    }

#ifndef PECULIAR_486
  /* Should check here that  |local_arg|  is within the valid range */
  if ( exponent >= -2 )
    {
      if ( (exponent > -2) ||
	  (argSignif.msw > (unsigned)0xafb0ccc0) )
	{
	  /* The argument is too large */
	}
    }
#endif PECULIAR_486

  arg_signif.lsw = argSignif.lsw; XSIG_LL(arg_signif) = XSIG_LL(argSignif);
  adj = norm_Xsig(&argSignif);
  accumulator.lsw = argSignif.lsw; XSIG_LL(accumulator) = XSIG_LL(argSignif);
  mul_Xsig_Xsig(&accumulator, &accumulator);
  shr_Xsig(&accumulator, 2*(-1 - (1 + exponent + adj)));
  Xsq = XSIG_LL(accumulator);
  if ( accumulator.lsw & 0x80000000 )
    Xsq++;

  accumulator.msw = accumulator.midw = accumulator.lsw = 0;
  /* Do the basic fixed point polynomial evaluation */
  polynomial_Xsig(&accumulator, &Xsq, logterms, HIPOWER-1);

  mul_Xsig_Xsig(&accumulator, &argSignif);
  shr_Xsig(&accumulator, 6 - adj);

  mul32_Xsig(&arg_signif, leadterm);
  add_two_Xsig(&accumulator, &arg_signif, &exponent);

  *expon = exponent + 1;
  accum_result->lsw = accumulator.lsw;
  accum_result->midw = accumulator.midw;
  accum_result->msw = accumulator.msw;

}
