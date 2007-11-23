/*---------------------------------------------------------------------------+
 |  reg_ld_str.c                                                             |
 |                                                                           |
 | All of the functions which transfer data between user memory and FPU_REGs.|
 |                                                                           |
 | Copyright (C) 1992,1993                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail apm233m@vaxc.cc.monash.edu.au    |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | Note:                                                                     |
 |    The file contains code which accesses user memory.                     |
 |    Emulator static data may change when user memory is accessed, due to   |
 |    other processes using the emulator while swapping is in progress.      |
 +---------------------------------------------------------------------------*/

#include <asm/segment.h>

#include "fpu_system.h"
#include "exception.h"
#include "reg_constant.h"
#include "fpu_emu.h"
#include "control_w.h"


#define EXTENDED_Emax 0x3fff     /* largest valid exponent */
#define EXTENDED_Ebias 0x3fff
#define EXTENDED_Emin (-0x3ffe)  /* smallest valid exponent */

#define DOUBLE_Emax 1023         /* largest valid exponent */
#define DOUBLE_Ebias 1023
#define DOUBLE_Emin (-1022)      /* smallest valid exponent */

#define SINGLE_Emax 127          /* largest valid exponent */
#define SINGLE_Ebias 127
#define SINGLE_Emin (-126)       /* smallest valid exponent */


FPU_REG FPU_loaded_data;


/* Get a long double from user memory */
void reg_load_extended(void)
{
  long double *s = (long double *)FPU_data_address;
  unsigned long sigl, sigh, exp;

  RE_ENTRANT_CHECK_OFF
  /* Use temporary variables here because FPU_loaded data is
     static and hence re-entrancy problems can arise */
  sigl = get_fs_long((unsigned long *) s);
  sigh = get_fs_long(1 + (unsigned long *) s);
  exp = get_fs_word(4 + (unsigned short *) s);
  RE_ENTRANT_CHECK_ON

  FPU_loaded_data.sigl = sigl;
  FPU_loaded_data.sigh = sigh;
  FPU_loaded_data.exp = exp;

  if (FPU_loaded_data.exp & 0x8000)
    FPU_loaded_data.sign = SIGN_NEG;
  else
    FPU_loaded_data.sign = SIGN_POS;
  if ( (FPU_loaded_data.exp &= 0x7fff) == 0 )
    {
      if ( !(FPU_loaded_data.sigl | FPU_loaded_data.sigh) )
	{
	  FPU_loaded_data.tag = TW_Zero;
	  return;
	}
      /* The number is de-normal */
      /* The default behaviour will take care of this */
    }
  else if ( FPU_loaded_data.exp == 0x7fff )
    {
      FPU_loaded_data.exp = EXTENDED_Emax;
      if ( (FPU_loaded_data.sigh == 0x80000000)
	  && (FPU_loaded_data.sigl == 0) )
	{
	  FPU_loaded_data.tag = TW_Infinity;
	  return;
	}
      if ( !(FPU_loaded_data.sigh & 0x80000000) )
	{
	  /* Unsupported data type */
	  EXCEPTION(EX_Invalid);
	  FPU_loaded_data.tag = TW_NaN;
	  return;
	}
      FPU_loaded_data.tag = TW_NaN;
      return;
    }
  FPU_loaded_data.exp = (FPU_loaded_data.exp & 0x7fff) - EXTENDED_Ebias
    + EXP_BIAS;
  FPU_loaded_data.tag = TW_Valid;

  normalize(&FPU_loaded_data);
}


/* Get a double from user memory */
void reg_load_double(void)
{
  double *dfloat = (double *)FPU_data_address;
  int exp;
  unsigned m64, l64;

  RE_ENTRANT_CHECK_OFF
  m64 = get_fs_long(1 + (unsigned long *) dfloat);
  l64 = get_fs_long((unsigned long *) dfloat);
  RE_ENTRANT_CHECK_ON

  if (m64 & 0x80000000)
    FPU_loaded_data.sign = SIGN_NEG;
  else
    FPU_loaded_data.sign = SIGN_POS;
  exp = ((m64 & 0x7ff00000) >> 20) - DOUBLE_Ebias;
  m64 &= 0xfffff;
  if (exp > DOUBLE_Emax)
    {
      /* Infinity or NaN */
      if ((m64 == 0) && (l64 == 0))
	{
	  /* +- infinity */
	  FPU_loaded_data.exp = EXTENDED_Emax;
	  FPU_loaded_data.tag = TW_Infinity;
	  return;
	}
      else
	{
	  /* Must be a signaling or quiet NaN */
	  FPU_loaded_data.exp = EXTENDED_Emax;
	  FPU_loaded_data.tag = TW_NaN;
	  FPU_loaded_data.sigh = (m64 << 11) | 0x80000000;
	  FPU_loaded_data.sigh |= l64 >> 21;
	  FPU_loaded_data.sigl = l64 << 11;
	  return;
	}
    }
  else if ( exp < DOUBLE_Emin )
    {
      /* Zero or de-normal */
      if ((m64 == 0) && (l64 == 0))
	{
	  /* Zero */
	  int c = FPU_loaded_data.sign;
	  reg_move(&CONST_Z, &FPU_loaded_data);
	  FPU_loaded_data.sign = c;
	  return;
	}
      else
	{
	  /* De-normal */
	  FPU_loaded_data.exp = DOUBLE_Emin + EXP_BIAS;
	  FPU_loaded_data.tag = TW_Valid;
	  FPU_loaded_data.sigh = m64 << 11;
	  FPU_loaded_data.sigh |= l64 >> 21;
	  FPU_loaded_data.sigl = l64 << 11;
	  normalize(&FPU_loaded_data);
	  return;
	}
    }
  else
    {
      FPU_loaded_data.exp = exp + EXP_BIAS;
      FPU_loaded_data.tag = TW_Valid;
      FPU_loaded_data.sigh = (m64 << 11) | 0x80000000;
      FPU_loaded_data.sigh |= l64 >> 21;
      FPU_loaded_data.sigl = l64 << 11;

      return;
    }
}


/* Get a float from user memory */
void reg_load_single(void)
{
  float *single = (float *)FPU_data_address;
  unsigned m32;
  int exp;

  RE_ENTRANT_CHECK_OFF
  m32 = get_fs_long((unsigned long *) single);
  RE_ENTRANT_CHECK_ON

  if (m32 & 0x80000000)
    FPU_loaded_data.sign = SIGN_NEG;
  else
    FPU_loaded_data.sign = SIGN_POS;
  if (!(m32 & 0x7fffffff))
    {
      /* Zero */
      int c = FPU_loaded_data.sign;
      reg_move(&CONST_Z, &FPU_loaded_data);
      FPU_loaded_data.sign = c;
      return;
    }
  exp = ((m32 & 0x7f800000) >> 23) - SINGLE_Ebias;
  m32 = (m32 & 0x7fffff) << 8;
  if ( exp < SINGLE_Emin )
    {
      /* De-normals */
      FPU_loaded_data.exp = SINGLE_Emin + EXP_BIAS;
      FPU_loaded_data.tag = TW_Valid;
      FPU_loaded_data.sigh = m32;
      FPU_loaded_data.sigl = 0;
      normalize(&FPU_loaded_data);
      return;
    }
  else if ( exp > SINGLE_Emax )
    {
    /* Infinity or NaN */
      if ( m32 == 0 )
	{
	  /* +- infinity */
	  FPU_loaded_data.exp = EXTENDED_Emax;
	  FPU_loaded_data.tag = TW_Infinity;
	  return;
	}
      else
	{
	  /* Must be a signaling or quiet NaN */
	  FPU_loaded_data.exp = EXTENDED_Emax;
	  FPU_loaded_data.tag = TW_NaN;
	  FPU_loaded_data.sigh = m32 | 0x80000000;
	  FPU_loaded_data.sigl = 0;
	  return;
	}
    }
  else
    {
      FPU_loaded_data.exp = exp + EXP_BIAS;
      FPU_loaded_data.sigh = m32 | 0x80000000;
      FPU_loaded_data.sigl = 0;
      FPU_loaded_data.tag = TW_Valid;
    }
}


/* Get a long long from user memory */
void reg_load_int64(void)
{
  long long *_s = (long long *)FPU_data_address;
  int e;
  long long s;

  RE_ENTRANT_CHECK_OFF
  ((unsigned long *)&s)[0] = get_fs_long((unsigned long *) _s);
  ((unsigned long *)&s)[1] = get_fs_long(1 + (unsigned long *) _s);
  RE_ENTRANT_CHECK_ON

  if (s == 0)
    { reg_move(&CONST_Z, &FPU_loaded_data); return; }

  if (s > 0)
    FPU_loaded_data.sign = SIGN_POS;
  else
  {
    s = -s;
    FPU_loaded_data.sign = SIGN_NEG;
  }

  e = EXP_BIAS + 63;
  *((long long *)&FPU_loaded_data.sigl) = s;
  FPU_loaded_data.exp = e;
  FPU_loaded_data.tag = TW_Valid;
  normalize(&FPU_loaded_data);
}


/* Get a long from user memory */
void reg_load_int32(void)
{
  long *_s = (long *)FPU_data_address;
  long s;
  int e;

  RE_ENTRANT_CHECK_OFF
  s = (long)get_fs_long((unsigned long *) _s);
  RE_ENTRANT_CHECK_ON

  if (s == 0)
    { reg_move(&CONST_Z, &FPU_loaded_data); return; }

  if (s > 0)
    FPU_loaded_data.sign = SIGN_POS;
  else
  {
    s = -s;
    FPU_loaded_data.sign = SIGN_NEG;
  }

  e = EXP_BIAS + 31;
  FPU_loaded_data.sigh = s;
  FPU_loaded_data.sigl = 0;
  FPU_loaded_data.exp = e;
  FPU_loaded_data.tag = TW_Valid;
  normalize(&FPU_loaded_data);
}


/* Get a short from user memory */
void reg_load_int16(void)
{
  short *_s = (short *)FPU_data_address;
  int s, e;

  RE_ENTRANT_CHECK_OFF
  s = (int)get_fs_word((unsigned short *) _s);
  RE_ENTRANT_CHECK_ON

  if (s == 0)
    { reg_move(&CONST_Z, &FPU_loaded_data); return; }

  if (s > 0)
    FPU_loaded_data.sign = SIGN_POS;
  else
  {
    s = -s;
    FPU_loaded_data.sign = SIGN_NEG;
  }

  e = EXP_BIAS + 15;
  FPU_loaded_data.sigh = s << 16;

  FPU_loaded_data.sigl = 0;
  FPU_loaded_data.exp = e;
  FPU_loaded_data.tag = TW_Valid;
  normalize(&FPU_loaded_data);
}


/* Get a packed bcd array from user memory */
void reg_load_bcd(void)
{
  char *s = (char *)FPU_data_address;
  int pos;
  unsigned char bcd;
  long long l=0;

  for ( pos = 8; pos >= 0; pos--)
    {
      l *= 10;
      RE_ENTRANT_CHECK_OFF
      bcd = (unsigned char)get_fs_byte((unsigned char *) s+pos);
      RE_ENTRANT_CHECK_ON
      l += bcd >> 4;
      l *= 10;
      l += bcd & 0x0f;
    }
  
  /* Finish all access to user memory before putting stuff into
     the static FPU_loaded_data */
  RE_ENTRANT_CHECK_OFF
  FPU_loaded_data.sign =
    ((unsigned char)get_fs_byte((unsigned char *) s+9)) & 0x80 ?
      SIGN_NEG : SIGN_POS;
  RE_ENTRANT_CHECK_ON

  if (l == 0)
    {
      char sign = FPU_loaded_data.sign;
      reg_move(&CONST_Z, &FPU_loaded_data);
      FPU_loaded_data.sign = sign;
    }
  else
    {
      *((long long *)&FPU_loaded_data.sigl) = l;
      FPU_loaded_data.exp = EXP_BIAS + 63;
      FPU_loaded_data.tag = TW_Valid;
      normalize(&FPU_loaded_data);
    }
}

/*===========================================================================*/

/* Put a long double into user memory */
int reg_store_extended(void)
{
  long double *d = (long double *)FPU_data_address;
  long e = FPU_st0_ptr->exp - EXP_BIAS + EXTENDED_Ebias;
  unsigned short sign = FPU_st0_ptr->sign*0x8000;
  unsigned long ls, ms;


  if ( FPU_st0_tag == TW_Valid )
    {
      if ( e >= 0x7fff )
	{
	  EXCEPTION(EX_Overflow);  /* Overflow */
	  /* This is a special case: see sec 16.2.5.1 of the 80486 book */
	  if ( control_word & EX_Overflow )
	    {
	      /* Overflow to infinity */
	      ls = 0;
	      ms = 0x80000000;
	      e = 0x7fff;
	    }
	  else
	    return 0;
	}
      else if ( e <= 0 )
	{
	  if ( e == 0 )
	    {
	      EXCEPTION(EX_Denormal);  /* Pseudo de-normal */
	      ls = FPU_st0_ptr->sigl;
	      ms = FPU_st0_ptr->sigh;
	    }
	  else if ( e > -64 )
	    {
	      /* Make a de-normal */
	      FPU_REG tmp;
	      EXCEPTION(EX_Denormal);  /* De-normal */
	      reg_move(FPU_st0_ptr, &tmp);
	      tmp.exp += -EXTENDED_Emin + 64;  /* largest exp to be 63 */
	      round_to_int(&tmp);
	      e = 0;
	      ls = tmp.sigl;
	      ms = tmp.sigh;
	    }
	  else
	    {
	      EXCEPTION(EX_Underflow);  /* Underflow */
	      /* This is a special case: see sec 16.2.5.1 of the 80486 book */
	      if ( control_word & EX_Underflow )
		{
		  /* Underflow to zero */
		  ls = 0;
		  ms = 0;
		  e = 0;
		}
	      else
		return 0;
	    }
	}
      else
	{
	  ls = FPU_st0_ptr->sigl;
	  ms = FPU_st0_ptr->sigh;
	}
    }
  else if ( FPU_st0_tag == TW_Zero )
    {
      ls = ms = 0;
      e = 0;
    }
  else if ( FPU_st0_tag == TW_Infinity )
    {
      ls = 0;
      ms = 0x80000000;
      e = 0x7fff;
    }
  else if ( FPU_st0_tag == TW_NaN )
    {
      ls = FPU_st0_ptr->sigl;
      ms = FPU_st0_ptr->sigh;
      e = 0x7fff;
    }
  else if ( FPU_st0_tag == TW_Empty )
    {
      /* Empty register (stack underflow) */
      EXCEPTION(EX_StackUnder);
      if ( control_word & EX_Invalid )
	{
	  /* The masked response */
	  /* Put out the QNaN indefinite */
	  ls = 0;
	  ms = 0xc0000000;
	  e = 0xffff;
	}
      else
	return 0;
    }
  else
    {
      /* We don't use TW_Denormal yet ... perhaps never! */
      EXCEPTION(EX_Invalid);
      /* Store a NaN */
      e = 0x7fff;
      ls = 1;
      ms = 0x80000000;
    }
  RE_ENTRANT_CHECK_OFF
  verify_area(VERIFY_WRITE,d,10);
  put_fs_long(ls, (unsigned long *) d);
  put_fs_long(ms, 1 + (unsigned long *) d);
  put_fs_word((unsigned short)e | sign, 4 + (short *) d);
  RE_ENTRANT_CHECK_ON

  return 1;

}


/* Put a double into user memory */
int reg_store_double(void)
{
  double *dfloat = (double *)FPU_data_address;
  unsigned long l[2];

  if (FPU_st0_tag == TW_Valid)
    {
      int exp;
      FPU_REG tmp;

      reg_move(FPU_st0_ptr, &tmp);

      if ( tmp.sigl & 0x000007ff )
	{
	  unsigned long increment = 0;	/* avoid gcc warnings */
	  
	  switch (control_word & CW_RC)
	    {
	    case RC_RND:
	      /* Rounding can get a little messy.. */
	      increment = ((tmp.sigl & 0x7ff) > 0x400) |      /* nearest */
		((tmp.sigl & 0xc00) == 0xc00);                /* odd -> even */
	      break;
	    case RC_DOWN:   /* towards -infinity */
	      increment = (tmp.sign == SIGN_POS) ? 0 : tmp.sigl & 0x7ff;
	      break;
	    case RC_UP:     /* towards +infinity */
	      increment = (tmp.sign == SIGN_POS) ? tmp.sigl & 0x7ff : 0;
	      break;
	    case RC_CHOP:
	      increment = 0;
	      break;
	    }
	  
	  /* Truncate the mantissa */
	  tmp.sigl &= 0xfffff800;

	  if ( increment )
	    {
	      if ( tmp.sigl >= 0xfffff800 )
		{
		  /* the sigl part overflows */
		  if ( tmp.sigh == 0xffffffff )
		    {
		      /* The sigh part overflows */
		      tmp.sigh = 0x80000000;
		      tmp.exp++;
		      if (tmp.exp >= EXP_OVER)
			goto overflow;
		    }
		  else
		    {
		      tmp.sigh ++;
		    }
		  tmp.sigl = 0x00000000;
		}
	      else
		{
		  /* We only need to increment sigl */
		  tmp.sigl += 0x00000800;
		}
	    }
	}

      l[0] = (tmp.sigl >> 11) | (tmp.sigh << 21);
      l[1] = ((tmp.sigh >> 11) & 0xfffff);
      exp = tmp.exp - EXP_BIAS;

      if ( exp > DOUBLE_Emax )
	{
	overflow:
	  EXCEPTION(EX_Overflow);
	  /* This is a special case: see sec 16.2.5.1 of the 80486 book */
	  if ( control_word & EX_Overflow )
	    {
	      /* Overflow to infinity */
	      l[0] = 0x00000000;	/* Set to */
	      l[1] = 0x7ff00000;	/* + INF */
	    }
	  else
	    return 0;
	}
      else if ( exp < DOUBLE_Emin )
	{
	  if ( exp > DOUBLE_Emin-53 )
	    {
	      /* Make a de-normal */
	      FPU_REG tmp;
	      EXCEPTION(EX_Denormal);
	      reg_move(FPU_st0_ptr, &tmp);
	      tmp.exp += -DOUBLE_Emin + 52;  /* largest exp to be 51 */
	      round_to_int(&tmp);
	      l[0] = tmp.sigl;
	      l[1] = tmp.sigh;
	    }
	  else
	    {
	      EXCEPTION(EX_Underflow);
	      /* This is a special case: see sec 16.2.5.1 of the 80486 book */
	      if ( control_word & EX_Underflow )
		{
		  /* Underflow to zero */
		  l[0] = l[1] = 0;
		}
	      else
		return 0;
	    }
	}
      else
	{
	  /* Add the exponent */
	  l[1] |= (((exp+DOUBLE_Ebias) & 0x7ff) << 20);
	}
    }
  else if (FPU_st0_tag == TW_Zero)
    {
      /* Number is zero */
      l[0] = l[1] = 0;
    }
  else if (FPU_st0_tag == TW_Infinity)
    {
      l[0] = 0;
      l[1] = 0x7ff00000;
    }
  else if (FPU_st0_tag == TW_NaN)
    {
      /* See if we can get a valid NaN from the FPU_REG */
      l[0] = (FPU_st0_ptr->sigl >> 11) | (FPU_st0_ptr->sigh << 21);
      l[1] = ((FPU_st0_ptr->sigh >> 11) & 0xfffff);
      if ( !(l[0] | l[1]) )
	{
	  /* This case does not seem to be handled by the 80486 specs */
	  EXCEPTION(EX_Invalid);
	  /* Make the quiet NaN "real indefinite" */
	  goto put_indefinite;
	}
      l[1] |= 0x7ff00000;
    }
  else if ( FPU_st0_tag == TW_Empty )
    {
      /* Empty register (stack underflow) */
      EXCEPTION(EX_StackUnder);
      if ( control_word & EX_Invalid )
	{
	  /* The masked response */
	  /* Put out the QNaN indefinite */
put_indefinite:
	  RE_ENTRANT_CHECK_OFF
	  verify_area(VERIFY_WRITE,(void *)dfloat,8);
	  put_fs_long(0, (unsigned long *) dfloat);
	  put_fs_long(0xfff80000, 1 + (unsigned long *) dfloat);
	  RE_ENTRANT_CHECK_ON
	  return 1;
	}
      else
	return 0;
    }
  else if (FPU_st0_tag == TW_Denormal)
    {
      /* Extended real -> double real will always underflow */
      l[0] = l[1] = 0;
      EXCEPTION(EX_Underflow);
    }
  if (FPU_st0_ptr->sign)
    l[1] |= 0x80000000;

  RE_ENTRANT_CHECK_OFF
  verify_area(VERIFY_WRITE,(void *)dfloat,8);
  put_fs_long(l[0], (unsigned long *)dfloat);
  put_fs_long(l[1], 1 + (unsigned long *)dfloat);
  RE_ENTRANT_CHECK_ON

  return 1;
}


/* Put a float into user memory */
int reg_store_single(void)
{
  float *single = (float *)FPU_data_address;
  long templ;

  if (FPU_st0_tag == TW_Valid)
    {
      int exp;
      FPU_REG tmp;

      reg_move(FPU_st0_ptr, &tmp);

      if ( tmp.sigl | (tmp.sigh & 0x000000ff) )
	{
	  unsigned long increment = 0;		/* avoid gcc warnings */
	  unsigned long sigh = tmp.sigh;
	  unsigned long sigl = tmp.sigl;
	  
	  switch (control_word & CW_RC)
	    {
	    case RC_RND:
	      increment = ((sigh & 0xff) > 0x80)           /* more than half */
		|| (((sigh & 0xff) == 0x80) && sigl)       /* more than half */
		  || ((sigh & 0x180) == 0x180);            /* round to even */
	      break;
	    case RC_DOWN:   /* towards -infinity */
	      increment = (tmp.sign == SIGN_POS) ? 0 : (sigl | (sigh & 0xff));
	      break;
	    case RC_UP:     /* towards +infinity */
	      increment = (tmp.sign == SIGN_POS) ? (sigl | (sigh & 0xff)) : 0;
	      break;
	    case RC_CHOP:
	      increment = 0;
	      break;
	    }
	  
	  /* Truncate part of the mantissa */
	  tmp.sigl = 0;
	  
	  if (increment)
	    {
	      if ( sigh >= 0xffffff00 )
		{
		  /* The sigh part overflows */
		  tmp.sigh = 0x80000000;
		  tmp.exp++;
		  if (tmp.exp >= EXP_OVER)
		    goto overflow;
		}
	      else
		{
		  tmp.sigh &= 0xffffff00;
		  tmp.sigh += 0x100;
		}
	    }
	  else
	    tmp.sigh &= 0xffffff00;  /* Finish the truncation */
	}

      templ = (tmp.sigh >> 8) & 0x007fffff;
      exp = tmp.exp - EXP_BIAS;

      if ( exp > SINGLE_Emax )
	{
	overflow:
	  EXCEPTION(EX_Overflow);
	  /* This is a special case: see sec 16.2.5.1 of the 80486 book */
	  if ( control_word & EX_Overflow )
	    {
	      /* Overflow to infinity */
	      templ = 0x7f800000;
	    }
	  else
	    return 0;
	}
      else if ( exp < SINGLE_Emin )
	{
	  if ( exp > SINGLE_Emin-24 )
	    {
	      /* Make a de-normal */
	      FPU_REG tmp;
	      EXCEPTION(EX_Denormal);
	      reg_move(FPU_st0_ptr, &tmp);
	      tmp.exp += -SINGLE_Emin + 23;  /* largest exp to be 22 */
	      round_to_int(&tmp);
	      templ = tmp.sigl;
	    }
	  else
	    {
	      EXCEPTION(EX_Underflow);
	      /* This is a special case: see sec 16.2.5.1 of the 80486 book */
	      if ( control_word & EX_Underflow )
		{
		  /* Underflow to zero */
		  templ = 0;
		}
	      else
		return 0;
	    }
	}
      else
	templ |= ((exp+SINGLE_Ebias) & 0xff) << 23;
    }
  else if (FPU_st0_tag == TW_Zero)
    {
      templ = 0;
    }
  else if (FPU_st0_tag == TW_Infinity)
    {
      templ = 0x7f800000;
    }
  else if (FPU_st0_tag == TW_NaN)
    {
      /* See if we can get a valid NaN from the FPU_REG */
      templ = FPU_st0_ptr->sigh >> 8;
      if ( !(templ & 0x3fffff) )
	{
	  /* This case does not seem to be handled by the 80486 specs */
	  EXCEPTION(EX_Invalid);
	  /* Make the quiet NaN "real indefinite" */
	  goto put_indefinite;
	}
      templ |= 0x7f800000;
    }
  else if ( FPU_st0_tag == TW_Empty )
    {
      /* Empty register (stack underflow) */
      EXCEPTION(EX_StackUnder);
      if ( control_word & EX_Invalid )
	{
	  /* The masked response */
	  /* Put out the QNaN indefinite */
put_indefinite:
	  RE_ENTRANT_CHECK_OFF
	  verify_area(VERIFY_WRITE,(void *)single,4);
	  put_fs_long(0xffc00000, (unsigned long *) single);
	  RE_ENTRANT_CHECK_ON
	  return 1;
	}
      else
	return 0;
    }
  else if (FPU_st0_tag == TW_Denormal)
    {
      /* Extended real -> real will always underflow */
      templ = 0;
      EXCEPTION(EX_Underflow);
    }
#ifdef PARANOID
  else
    {
      EXCEPTION(EX_INTERNAL|0x106);
      return 0;
    }
#endif
  if (FPU_st0_ptr->sign)
    templ |= 0x80000000;

  RE_ENTRANT_CHECK_OFF
  verify_area(VERIFY_WRITE,(void *)single,4);
  put_fs_long(templ,(unsigned long *) single);
  RE_ENTRANT_CHECK_ON

  return 1;
}


/* Put a long long into user memory */
int reg_store_int64(void)
{
  long long *d = (long long *)FPU_data_address;
  FPU_REG t;
  long long tll;

  if ( FPU_st0_tag == TW_Empty )
    {
      /* Empty register (stack underflow) */
      EXCEPTION(EX_StackUnder);
      if ( control_word & EX_Invalid )
	{
	  /* The masked response */
	  /* Put out the QNaN indefinite */
	  goto put_indefinite;
	}
      else
	return 0;
    }

  reg_move(FPU_st0_ptr, &t);
  round_to_int(&t);
  ((long *)&tll)[0] = t.sigl;
  ((long *)&tll)[1] = t.sigh;
  if ( (t.sigh & 0x80000000) &&
      !((t.sigh == 0x80000000) && (t.sigl == 0) && (t.sign == SIGN_NEG)) )
    {
      EXCEPTION(EX_Invalid);
      /* This is a special case: see sec 16.2.5.1 of the 80486 book */
      if ( control_word & EX_Invalid )
	{
	  /* Produce "indefinite" */
put_indefinite:
	  ((long *)&tll)[1] = 0x80000000;
	  ((long *)&tll)[0] = 0;
	}
      else
	return 0;
    }
  else if ( t.sign )
    tll = - tll;

  RE_ENTRANT_CHECK_OFF
  verify_area(VERIFY_WRITE,(void *)d,8);
  put_fs_long(((long *)&tll)[0],(unsigned long *) d);
  put_fs_long(((long *)&tll)[1],1 + (unsigned long *) d);
  RE_ENTRANT_CHECK_ON

  return 1;
}


/* Put a long into user memory */
int reg_store_int32(void)
{
  long *d = (long *)FPU_data_address;
  FPU_REG t;

  if ( FPU_st0_tag == TW_Empty )
    {
      /* Empty register (stack underflow) */
      EXCEPTION(EX_StackUnder);
      if ( control_word & EX_Invalid )
	{
	  /* The masked response */
	  /* Put out the QNaN indefinite */
	  RE_ENTRANT_CHECK_OFF
	  verify_area(VERIFY_WRITE,d,4);
	  put_fs_long(0x80000000, (unsigned long *) d);
	  RE_ENTRANT_CHECK_ON
	  return 1;
	}
      else
	return 0;
    }

  reg_move(FPU_st0_ptr, &t);
  round_to_int(&t);
  if (t.sigh ||
      ((t.sigl & 0x80000000) &&
       !((t.sigl == 0x80000000) && (t.sign == SIGN_NEG))) )
    {
      EXCEPTION(EX_Invalid);
      /* This is a special case: see sec 16.2.5.1 of the 80486 book */
      if ( control_word & EX_Invalid )
	{
	  /* Produce "indefinite" */
	  t.sigl = 0x80000000;
	}
      else
	return 0;
    }
  else if ( t.sign )
    t.sigl = -(long)t.sigl;

  RE_ENTRANT_CHECK_OFF
  verify_area(VERIFY_WRITE,d,4);
  put_fs_long(t.sigl, (unsigned long *) d);
  RE_ENTRANT_CHECK_ON

  return 1;
}


/* Put a short into user memory */
int reg_store_int16(void)
{
  short *d = (short *)FPU_data_address;
  FPU_REG t;
  short ts;

  if ( FPU_st0_tag == TW_Empty )
    {
      /* Empty register (stack underflow) */
      EXCEPTION(EX_StackUnder);
      if ( control_word & EX_Invalid )
	{
	  /* The masked response */
	  /* Put out the QNaN indefinite */
	  RE_ENTRANT_CHECK_OFF
	  verify_area(VERIFY_WRITE,d,2);
	  put_fs_word(0x8000, (unsigned short *) d);
	  RE_ENTRANT_CHECK_ON
	  return 1;
	}
      else
	return 0;
    }

  reg_move(FPU_st0_ptr, &t);
  round_to_int(&t);
  if (t.sigh ||
      ((t.sigl & 0xffff8000) &&
       !((t.sigl == 0x8000) && (t.sign == SIGN_NEG))) )
    {
      EXCEPTION(EX_Invalid);
      /* This is a special case: see sec 16.2.5.1 of the 80486 book */
      if ( control_word & EX_Invalid )
	{
	  /* Produce "indefinite" */
	  ts = 0x8000;
	}
      else
	return 0;
    }
  else if ( t.sign )
    t.sigl = -t.sigl;

  RE_ENTRANT_CHECK_OFF
  verify_area(VERIFY_WRITE,d,2);
  put_fs_word((short)t.sigl,(short *) d);
  RE_ENTRANT_CHECK_ON

  return 1;
}


/* Put a packed bcd array into user memory */
int reg_store_bcd(void)
{
  char *d = (char *)FPU_data_address;
  FPU_REG t;
  long long ll;
  unsigned char b;
  int i;
  unsigned char sign = (FPU_st0_ptr->sign == SIGN_NEG) ? 0x80 : 0;

  if ( FPU_st0_tag == TW_Empty )
    {
      /* Empty register (stack underflow) */
      EXCEPTION(EX_StackUnder);
      if ( control_word & EX_Invalid )
	{
	  /* The masked response */
	  /* Put out the QNaN indefinite */
	  goto put_indefinite;
	}
      else
	return 0;
    }

  reg_move(FPU_st0_ptr, &t);
  round_to_int(&t);
  ll = *(long long *)(&t.sigl);

  /* Check for overflow, by comparing with 999999999999999999 decimal. */
  if ( (t.sigh > 0x0de0b6b3) ||
      ((t.sigh == 0x0de0b6b3) && (t.sigl > 0xa763ffff)) )
    {
      EXCEPTION(EX_Invalid);
      /* This is a special case: see sec 16.2.5.1 of the 80486 book */
      if ( control_word & EX_Invalid )
	{
put_indefinite:
	  /* Produce "indefinite" */
	  RE_ENTRANT_CHECK_OFF
	  verify_area(VERIFY_WRITE,d,10);
	  put_fs_byte(0xff,(unsigned char *) d+7);
	  put_fs_byte(0xff,(unsigned char *) d+8);
	  put_fs_byte(0xff,(unsigned char *) d+9);
	  RE_ENTRANT_CHECK_ON
	  return 1;
	}
      else
	return 0;
    }

  verify_area(VERIFY_WRITE,d,10);
  for ( i = 0; i < 9; i++)
    {
      b = div_small(&ll, 10);
      b |= (div_small(&ll, 10)) << 4;
      RE_ENTRANT_CHECK_OFF
      put_fs_byte(b,(unsigned char *) d+i);
      RE_ENTRANT_CHECK_ON
    }
  RE_ENTRANT_CHECK_OFF
  put_fs_byte(sign,(unsigned char *) d+9);
  RE_ENTRANT_CHECK_ON

  return 1;
}

/*===========================================================================*/

/* r gets mangled such that sig is int, sign: 
   it is NOT normalized*/
/* Overflow is signalled by a non-zero return value (in eax).
   In the case of overflow, the returned significand always has the
   the largest possible value */
/* The value returned in eax is never actually needed :-) */
int round_to_int(FPU_REG *r)
{
  char     very_big;
  unsigned eax;

  if (r->tag == TW_Zero)
    {
      /* Make sure that zero is returned */
      *(long long *)&r->sigl = 0;
      return 0;        /* o.k. */
    }
  
  if (r->exp > EXP_BIAS + 63)
    {
      r->sigl = r->sigh = ~0;      /* The largest representable number */
      return 1;        /* overflow */
    }

  eax = shrxs(&r->sigl, EXP_BIAS + 63 - r->exp);
  very_big = !(~(r->sigh) | ~(r->sigl));  /* test for 0xfff...fff */
#define	half_or_more	(eax & 0x80000000)
#define	frac_part	(eax)
#define more_than_half  ((eax & 0x80000001) == 0x80000001)
  switch (control_word & CW_RC)
    {
    case RC_RND:
      if ( more_than_half               	/* nearest */
	  || (half_or_more && (r->sigl & 1)) )	/* odd -> even */
	{
	  if ( very_big ) return 1;        /* overflow */
	  (*(long long *)(&r->sigl)) ++;
	}
      break;
    case RC_DOWN:
      if (frac_part && r->sign)
	{
	  if ( very_big ) return 1;        /* overflow */
	  (*(long long *)(&r->sigl)) ++;
	}
      break;
    case RC_UP:
      if (frac_part && !r->sign)
	{
	  if ( very_big ) return 1;        /* overflow */
	  (*(long long *)(&r->sigl)) ++;
	}
      break;
    case RC_CHOP:
      break;
    }

  return 0;           /* o.k. */
}

/*===========================================================================*/

char *fldenv(void)
{
  char *s = (char *)FPU_data_address;
  unsigned short tag_word = 0;
  unsigned char tag;
  int i;

  RE_ENTRANT_CHECK_OFF
  control_word = get_fs_word((unsigned short *) s);
  status_word = get_fs_word((unsigned short *) (s+4));
  tag_word = get_fs_word((unsigned short *) (s+8));
  ip_offset = get_fs_long((unsigned long *) (s+0x0c));
  cs_selector = get_fs_long((unsigned long *) (s+0x10));
  data_operand_offset = get_fs_long((unsigned long *) (s+0x14));
  operand_selector = get_fs_long((unsigned long *) (s+0x18));
  RE_ENTRANT_CHECK_ON


  for ( i = 7; i >= 0; i-- )
    {
      tag = tag_word & 3;
      tag_word <<= 2;

      switch ( tag )
	{
	case 0:
	  regs[i].tag = TW_Valid;
	  break;
	case 1:
	  regs[i].tag = TW_Zero;
	  break;
	case 2:
	  regs[i].tag = TW_NaN;
	  break;
	case 3:
	  regs[i].tag = TW_Empty;
	  break;
	}
    }

  FPU_data_address = (void *)data_operand_offset;  /* We want no net effect */
  FPU_entry_eip = ip_offset;               /* We want no net effect */

  return s + 0x1c;
}


void frstor(void)
{
  int i;
  unsigned char tag;
  FPU_REG *s = (FPU_REG *)fldenv();

  for ( i = 0; i < 8; i++ )
    {
      /* load each register */
      FPU_data_address = (void *)&(s[i]);
      reg_load_extended();
      tag = regs[i].tag;
      reg_move(&FPU_loaded_data, &regs[i]);
      if ( tag == TW_NaN )
	{
	  unsigned char t = regs[i].tag;
	  if ( (t == TW_Valid) || (t == TW_Zero) )
	    regs[i].tag = TW_NaN;
	}
      else
	regs[i].tag = tag;
    }

  FPU_data_address = (void *)data_operand_offset;  /* We want no net effect */
}


char *fstenv(void)
{
  char *d = (char *)FPU_data_address;
  unsigned short tag_word = 0;
  unsigned char tag;
  int i;

  verify_area(VERIFY_WRITE,d,28);

  for ( i = 7; i >= 0; i-- )
    {
      switch ( tag = regs[i].tag )
	{
	case TW_Denormal:
	case TW_Infinity:
	case TW_NaN:
	  tag = 2;
	  break;
	case TW_Empty:
	  tag = 3;
	  break;
	  /* TW_Valid and TW_Zero already have the correct value */
	}
      tag_word <<= 2;
      tag_word |= tag;
    }

  /* This is not what should be done ... but saves overheads. */
  *(unsigned short *)&cs_selector = FPU_CS;
  *(unsigned short *)&operand_selector = FPU_DS;

  RE_ENTRANT_CHECK_OFF
  put_fs_word(control_word, (unsigned short *) d);
  put_fs_word(status_word, (unsigned short *) (d+4));
  put_fs_word(tag_word, (unsigned short *) (d+8));
  put_fs_long(ip_offset, (unsigned long *) (d+0x0c));
  put_fs_long(cs_selector, (unsigned long *) (d+0x10));
  put_fs_long(data_operand_offset, (unsigned long *) (d+0x14));
  put_fs_long(operand_selector, (unsigned long *) (d+0x18));
  RE_ENTRANT_CHECK_ON

  return d + 0x1c;
}


void fsave(void)
{
  char *d;
  FPU_REG tmp, *rp;
  int i;
  short e;

  d = fstenv();
  verify_area(VERIFY_WRITE,d,80);
  for ( i = 0; i < 8; i++ )
    {
      /* store each register */
      rp = &regs[i];

      e = rp->exp - EXP_BIAS + EXTENDED_Ebias;

      if ( rp->tag == TW_Valid )
	{
	  if ( e >= 0x7fff )
	    {
	      /* Overflow to infinity */
	      RE_ENTRANT_CHECK_OFF
	      put_fs_long(0, (unsigned long *) (d+i*10+2));
	      put_fs_long(0x80000000, (unsigned long *) (d+i*10+6));
	      RE_ENTRANT_CHECK_ON
	      e = 0x7fff;
	    }
	  else if ( e <= 0 )
	    {
	      if ( e == 0 )
		{
		  /* Pseudo de-normal */
		  RE_ENTRANT_CHECK_OFF
		  put_fs_long(rp->sigl, (unsigned long *) (d+i*10+2));
		  put_fs_long(rp->sigh, (unsigned long *) (d+i*10+6));
		  RE_ENTRANT_CHECK_ON
		}
	      else if ( e > -64 )
		{
		  /* Make a de-normal */
		  reg_move(rp, &tmp);
		  tmp.exp += -EXTENDED_Emin + 64;  /* largest exp to be 63 */
		  round_to_int(&tmp);
		  e = 0;
		  RE_ENTRANT_CHECK_OFF
		  put_fs_long(tmp.sigl, (unsigned long *) (d+i*10+2));
		  put_fs_long(tmp.sigh, (unsigned long *) (d+i*10+6));
		  RE_ENTRANT_CHECK_ON
		}
	      else
		{
		  /* Underflow to zero */
		  RE_ENTRANT_CHECK_OFF
		  put_fs_long(0, (unsigned long *) (d+i*10+2));
		  put_fs_long(0, (unsigned long *) (d+i*10+6));
		  RE_ENTRANT_CHECK_ON
		  e = 0;
		}
	    }
	  else
	    {
	      RE_ENTRANT_CHECK_OFF
	      put_fs_long(rp->sigl, (unsigned long *) (d+i*10+2));
	      put_fs_long(rp->sigh, (unsigned long *) (d+i*10+6));
	      RE_ENTRANT_CHECK_ON
	    }
	}
      else if ( rp->tag == TW_Zero )
	{
	  RE_ENTRANT_CHECK_OFF
	  put_fs_long(0, (unsigned long *) (d+i*10+2));
	  put_fs_long(0, (unsigned long *) (d+i*10+6));
	  RE_ENTRANT_CHECK_ON
	  e = 0;
	}
      else if ( rp->tag == TW_Infinity )
	{
	  RE_ENTRANT_CHECK_OFF
	  put_fs_long(0, (unsigned long *) (d+i*10+2));
	  put_fs_long(0x80000000, (unsigned long *) (d+i*10+6));
	  RE_ENTRANT_CHECK_ON
	  e = 0x7fff;
	}
      else if ( rp->tag == TW_NaN )
	{
	  RE_ENTRANT_CHECK_OFF
	  put_fs_long(rp->sigl, (unsigned long *) (d+i*10+2));
	  put_fs_long(rp->sigh, (unsigned long *) (d+i*10+6));
	  RE_ENTRANT_CHECK_ON
	  e = 0x7fff;
	}
      else if ( rp->tag == TW_Empty )
	{
	  /* just copy the reg */
	  RE_ENTRANT_CHECK_OFF
	  put_fs_long(rp->sigl, (unsigned long *) (d+i*10+2));
	  put_fs_long(rp->sigh, (unsigned long *) (d+i*10+6));
	  RE_ENTRANT_CHECK_ON
	}
      RE_ENTRANT_CHECK_OFF
      put_fs_word(e, (unsigned short *) (d+i*10));
      RE_ENTRANT_CHECK_ON
    }

}

/*===========================================================================*/
