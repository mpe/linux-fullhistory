/*---------------------------------------------------------------------------+
 |  reg_constant.c                                                           |
 |                                                                           |
 | All of the constant FPU_REGs                                              |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1996                                         |
 |                     W. Metzenthen, 22 Parker St, Ormond, Vic 3163,        |
 |                     Australia.  E-mail   billm@jacobi.maths.monash.edu.au |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "fpu_system.h"
#include "fpu_emu.h"
#include "status_w.h"
#include "reg_constant.h"
#include "control_w.h"


FPU_REG const CONST_1    = { SIGN_POS, TW_Valid, EXP_BIAS,
			    0x00000000, 0x80000000 };
FPU_REG const CONST_2    = { SIGN_POS, TW_Valid, EXP_BIAS+1,
			    0x00000000, 0x80000000 };
FPU_REG const CONST_HALF = { SIGN_POS, TW_Valid, EXP_BIAS-1,
			    0x00000000, 0x80000000 };
FPU_REG const CONST_L2T  = { SIGN_POS, TW_Valid, EXP_BIAS+1,
			    0xcd1b8afe, 0xd49a784b };
FPU_REG const CONST_L2E  = { SIGN_POS, TW_Valid, EXP_BIAS,
			    0x5c17f0bc, 0xb8aa3b29 };
FPU_REG const CONST_PI   = { SIGN_POS, TW_Valid, EXP_BIAS+1,
			    0x2168c235, 0xc90fdaa2 };
FPU_REG const CONST_PI2  = { SIGN_POS, TW_Valid, EXP_BIAS,
			    0x2168c235, 0xc90fdaa2 };
FPU_REG const CONST_PI4  = { SIGN_POS, TW_Valid, EXP_BIAS-1,
			    0x2168c235, 0xc90fdaa2 };
FPU_REG const CONST_LG2  = { SIGN_POS, TW_Valid, EXP_BIAS-2,
			    0xfbcff799, 0x9a209a84 };
FPU_REG const CONST_LN2  = { SIGN_POS, TW_Valid, EXP_BIAS-1,
			    0xd1cf79ac, 0xb17217f7 };

/* Extra bits to take pi/2 to more than 128 bits precision. */
FPU_REG const CONST_PI2extra = { SIGN_NEG, TW_Valid, EXP_BIAS-66,
			    0xfc8f8cbb, 0xece675d1 };

/* Only the sign (and tag) is used in internal zeroes */
FPU_REG const CONST_Z    = { SIGN_POS, TW_Zero, EXP_UNDER, 0x0, 0x0 };

/* Only the sign and significand (and tag) are used in internal NaNs */
/* The 80486 never generates one of these 
FPU_REG const CONST_SNAN = { SIGN_POS, TW_NaN, EXP_OVER, 0x00000001, 0x80000000 };
 */
/* This is the real indefinite QNaN */
FPU_REG const CONST_QNaN = { SIGN_NEG, TW_NaN, EXP_OVER, 0x00000000, 0xC0000000 };

/* Only the sign (and tag) is used in internal infinities */
FPU_REG const CONST_INF  = { SIGN_POS, TW_Infinity, EXP_OVER, 0x00000000, 0x80000000 };



static void fld_const(FPU_REG const *c, int adj)
{
  FPU_REG *st_new_ptr;

  if ( STACK_OVERFLOW )
    {
      stack_overflow();
      return;
    }
  push();
  reg_move(c, st_new_ptr);
  st_new_ptr->sigl += adj;  /* For all our fldxxx constants, we don't need to
			       borrow or carry. */
  clear_C1();
}

/* A fast way to find out whether x is one of RC_DOWN or RC_CHOP
   (and not one of RC_RND or RC_UP).
   */
#define DOWN_OR_CHOP(x)  (x & RC_DOWN)

static void fld1(int rc)
{
  fld_const(&CONST_1, 0);
}

static void fldl2t(int rc)
{
  fld_const(&CONST_L2T, (rc == RC_UP) ? 1 : 0);
}

static void fldl2e(int rc)
{
  fld_const(&CONST_L2E, DOWN_OR_CHOP(rc) ? -1 : 0);
}

static void fldpi(int rc)
{
  fld_const(&CONST_PI, DOWN_OR_CHOP(rc) ? -1 : 0);
}

static void fldlg2(int rc)
{
  fld_const(&CONST_LG2, DOWN_OR_CHOP(rc) ? -1 : 0);
}

static void fldln2(int rc)
{
  fld_const(&CONST_LN2, DOWN_OR_CHOP(rc) ? -1 : 0);
}

static void fldz(int rc)
{
  fld_const(&CONST_Z, 0);
}

typedef void (*FUNC_RC)(int);

static FUNC_RC constants_table[] = {
  fld1, fldl2t, fldl2e, fldpi, fldlg2, fldln2, fldz, (FUNC_RC)FPU_illegal
};

void fconst(void)
{
  (constants_table[FPU_rm])(control_word & CW_RC);
}
