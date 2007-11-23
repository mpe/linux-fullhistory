/*---------------------------------------------------------------------------+
 |  reg_constant.c                                                           |
 |                                                                           |
 | All of the constant FPU_REGs                                              |
 |                                                                           |
 | Copyright (C) 1992,1993                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail apm233m@vaxc.cc.monash.edu.au    |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "fpu_system.h"
#include "fpu_emu.h"
#include "status_w.h"
#include "reg_constant.h"


FPU_REG CONST_1    = { SIGN_POS, TW_Valid, EXP_BIAS,
			    0x00000000, 0x80000000 };
FPU_REG CONST_2    = { SIGN_POS, TW_Valid, EXP_BIAS+1,
			    0x00000000, 0x80000000 };
FPU_REG CONST_HALF = { SIGN_POS, TW_Valid, EXP_BIAS-1,
			    0x00000000, 0x80000000 };
FPU_REG CONST_L2T  = { SIGN_POS, TW_Valid, EXP_BIAS+1,
			    0xcd1b8afe, 0xd49a784b };
FPU_REG CONST_L2E  = { SIGN_POS, TW_Valid, EXP_BIAS,
			    0x5c17f0bc, 0xb8aa3b29 };
FPU_REG CONST_PI   = { SIGN_POS, TW_Valid, EXP_BIAS+1,
			    0x2168c235, 0xc90fdaa2 };
FPU_REG CONST_PI2  = { SIGN_POS, TW_Valid, EXP_BIAS,
			    0x2168c235, 0xc90fdaa2 };
FPU_REG CONST_PI4  = { SIGN_POS, TW_Valid, EXP_BIAS-1,
			    0x2168c235, 0xc90fdaa2 };
FPU_REG CONST_LG2  = { SIGN_POS, TW_Valid, EXP_BIAS-2,
			    0xfbcff799, 0x9a209a84 };
FPU_REG CONST_LN2  = { SIGN_POS, TW_Valid, EXP_BIAS-1,
			    0xd1cf79ac, 0xb17217f7 };

/* Only the sign (and tag) is used in internal zeroes */
FPU_REG CONST_Z    = { SIGN_POS, TW_Zero, 0,          0x0,        0x0 };

/* Only the sign and significand (and tag) are used in internal NaNs */
/* The 80486 never generates one of these 
FPU_REG CONST_SNAN = { SIGN_POS, TW_NaN, EXP_OVER, 0x00000001, 0x80000000 };
 */
/* This is the real indefinite QNaN */
FPU_REG CONST_QNaN = { SIGN_NEG, TW_NaN, EXP_OVER, 0x00000000, 0xC0000000 };

/* Only the sign (and tag) is used in internal infinities */
FPU_REG CONST_INF  = { SIGN_POS, TW_Infinity, EXP_OVER, 0x00000000, 0x80000000 };



static void fld_const(FPU_REG *c)
{
  FPU_REG *st_new_ptr;

  if ( STACK_OVERFLOW )
    {
      stack_overflow();
      return;
    }
  push();
  reg_move(c, FPU_st0_ptr);
  status_word &= ~SW_C1;
}


static void fld1(void)
{
  fld_const(&CONST_1);
}

static void fldl2t(void)
{
  fld_const(&CONST_L2T);
}

static void fldl2e(void)
{
  fld_const(&CONST_L2E);
}

static void fldpi(void)
{
  fld_const(&CONST_PI);
}

static void fldlg2(void)
{
  fld_const(&CONST_LG2);
}

static void fldln2(void)
{
  fld_const(&CONST_LN2);
}

static void fldz(void)
{
  fld_const(&CONST_Z);
}

static FUNC constants_table[] = {
  fld1, fldl2t, fldl2e, fldpi, fldlg2, fldln2, fldz, Un_impl
};

void fconst(void)
{
  (constants_table[FPU_rm])();
}
