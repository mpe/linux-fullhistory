/*---------------------------------------------------------------------------+
 |  precision.c                                                              |
 |                                                                           |
 | The functions which adjust the precision of a result.                     |
 |                                                                           |
 | Copyright (C) 1993    W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail apm233m@vaxc.cc.monash.edu.au    |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/


#include <asm/segment.h>

#include "fpu_system.h"
#include "exception.h"
#include "reg_constant.h"
#include "fpu_emu.h"
#include "control_w.h"


/* Round the result to 53 bits */
int round_to_53_bits(FPU_REG *reg)
{

  if (reg->tag == TW_Valid)
    {
      unsigned long increment = 0;	/* avoid gcc warnings */

      switch (control_word & CW_RC)
	{
	case RC_RND:
	  /* Rounding can get a little messy.. */
	  increment = ((reg->sigl & 0x7ff) > 0x400) |	/* nearest */
	    ((reg->sigl & 0xc00) == 0xc00);           	/* odd -> even */
	  break;
	case RC_DOWN:   /* towards -infinity */
	  increment = (reg->sign == SIGN_POS) ? 0 : reg->sigl & 0x7ff;
	  break;
	case RC_UP:     /* towards +infinity */
	  increment = (reg->sign == SIGN_POS) ? reg->sigl & 0x7ff : 0;
	  break;
	case RC_CHOP:
	  increment = 0;
	  break;
	}

      /* Truncate the mantissa */
      reg->sigl &= 0xfffff800;

      if ( increment )
	{
	  if ( reg->sigl >= 0xfffff800 )
	    {
	      /* the sigl part overflows */
	      if ( reg->sigh == 0xffffffff )
		{
		  /* The sigh part overflows */
		  reg->sigh = 0x80000000;
		  reg->exp++;
		  if (reg->exp >= EXP_OVER)
		    { arith_overflow(reg); return 1; }
		}
	      else
		{
		  reg->sigh ++;
		}
	      reg->sigl = 0x00000000;
	    }
	  else
	    {
	      /* We only need to increment sigl */
	      reg->sigl += 0x00000800;
	    }
	}
    }

  return 0;

}


/* Round the result to 24 bits */
int round_to_24_bits(FPU_REG *reg)
{

  if (reg->tag == TW_Valid)
    {
      unsigned long increment = 0;		/* avoid gcc warnings */
      unsigned long sigh = reg->sigh;
      unsigned long sigl = reg->sigl;

      switch (control_word & CW_RC)
	{
	case RC_RND:
	  increment = ((sigh & 0xff) > 0x80)           /* more than half */
	    || (((sigh & 0xff) == 0x80) && sigl)       /* more than half */
	    || ((sigh & 0x180) == 0x180);              /* round to even */
	  break;
	case RC_DOWN:   /* towards -infinity */
	  increment = (reg->sign == SIGN_POS) ? 0 : (sigl | (sigh & 0xff));
	  break;
	case RC_UP:     /* towards +infinity */
	  increment = (reg->sign == SIGN_POS) ? (sigl | (sigh & 0xff)) : 0;
	  break;
	case RC_CHOP:
	  increment = 0;
	  break;
	}

      /* Truncate the mantissa */
      reg->sigl = 0;

      if (increment)
	{
	  if ( sigh >= 0xffffff00 )
	    {
	      /* The sigh part overflows */
	      reg->sigh = 0x80000000;
	      reg->exp++;
	      if (reg->exp >= EXP_OVER)
		{ arith_overflow(reg); return 1; }
	    }
	  else
	    {
	      reg->sigh &= 0xffffff00;
	      reg->sigh += 0x100;
	    }
	}
    }

  return 0;

}
