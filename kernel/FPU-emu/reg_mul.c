/*---------------------------------------------------------------------------+
 |  reg_mul.c                                                                |
 |                                                                           |
 | Multiply one FPU_REG by another, put the result in a destination FPU_REG. |
 |                                                                           |
 | Copyright (C) 1992,1993                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail apm233m@vaxc.cc.monash.edu.au    |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | The destination may be any FPU_REG, including one of the source FPU_REGs. |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "reg_constant.h"
#include "fpu_emu.h"
#include "fpu_system.h"


/* This routine must be called with non-empty source registers */
void reg_mul(FPU_REG *a, FPU_REG *b, FPU_REG *dest, unsigned int control_w)
{
  char sign = (a->sign ^ b->sign);

  if (!(a->tag | b->tag))
    {
      /* This should be the most common case */
      reg_u_mul(a, b, dest, control_w);
      dest->sign = sign;
      return;
    }
  else if ((a->tag <= TW_Zero) && (b->tag <= TW_Zero))
    {
#ifdef DENORM_OPERAND
      if ( ((b->tag == TW_Valid) && (b->exp <= EXP_UNDER)) ||
	  ((a->tag == TW_Valid) && (a->exp <= EXP_UNDER)) )
	{
	  if ( denormal_operand() ) return;
	}
#endif DENORM_OPERAND
      /* Must have either both arguments == zero, or
	 one valid and the other zero.
	 The result is therefore zero. */
      reg_move(&CONST_Z, dest);
#ifdef PECULIAR_486
      /* The 80486 book says that the answer is +0, but a real
	 80486 appears to behave this way... */
      dest->sign = sign;
#endif PECULIAR_486
      return;
    }
#if 0  /* TW_Denormal is not used yet... perhaps never will be. */
  else if ((a->tag <= TW_Denormal) && (b->tag <= TW_Denormal))
    {
      /* One or both arguments are de-normalized */
      /* Internal de-normalized numbers are not supported yet */
      EXCEPTION(EX_INTERNAL|0x105);
      reg_move(&CONST_Z, dest);
    }
#endif
  else
    {
      /* Must have infinities, NaNs, etc */
      if ( (a->tag == TW_NaN) || (b->tag == TW_NaN) )
	{ real_2op_NaN(a, b, dest); return; }
      else if (a->tag == TW_Infinity)
	{
	  if (b->tag == TW_Zero)
	    { arith_invalid(dest); return; }  /* Zero*Infinity is invalid */
	  else
	    {
#ifdef DENORM_OPERAND
	      if ( (b->tag == TW_Valid) && (b->exp <= EXP_UNDER) &&
		  denormal_operand() )
		return;
#endif DENORM_OPERAND
	      reg_move(a, dest);
	      dest->sign = sign;
	    }
	  return;
	}
      else if (b->tag == TW_Infinity)
	{
	  if (a->tag == TW_Zero)
	    { arith_invalid(dest); return; }  /* Zero*Infinity is invalid */
	  else
	    {
#ifdef DENORM_OPERAND
	      if ( (a->tag == TW_Valid) && (a->exp <= EXP_UNDER) &&
		  denormal_operand() )
		return;
#endif DENORM_OPERAND
	      reg_move(b, dest);
	      dest->sign = sign;
	    }
	  return;
	}
#ifdef PARANOID
      else
	{
	  EXCEPTION(EX_INTERNAL|0x102);
	}
#endif PARANOID
    }
}
