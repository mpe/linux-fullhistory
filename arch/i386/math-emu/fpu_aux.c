/*---------------------------------------------------------------------------+
 |  fpu_aux.c                                                                |
 |                                                                           |
 | Code to implement some of the FPU auxiliary instructions.                 |
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


static void fnop(void)
{
}

void fclex(void)
{
  partial_status &= ~(SW_Backward|SW_Summary|SW_Stack_Fault|SW_Precision|
		   SW_Underflow|SW_Overflow|SW_Zero_Div|SW_Denorm_Op|
		   SW_Invalid);
  no_ip_update = 1;
}

/* Needs to be externally visible */
void finit()
{
  int r;
  control_word = 0x037f;
  partial_status = 0;
  top = 0;            /* We don't keep top in the status word internally. */
  for (r = 0; r < 8; r++)
    {
      regs[r].tag = TW_Empty;
    }
  /* The behaviour is different to that detailed in
     Section 15.1.6 of the Intel manual */
  operand_address.offset = 0;
  operand_address.selector = 0;
  instruction_address.offset = 0;
  instruction_address.selector = 0;
  instruction_address.opcode = 0;
  no_ip_update = 1;
}

/*
 * These are nops on the i387..
 */
#define feni fnop
#define fdisi fnop
#define fsetpm fnop

static FUNC const finit_table[] = {
  feni, fdisi, fclex, finit,
  fsetpm, FPU_illegal, FPU_illegal, FPU_illegal
};

void finit_()
{
  (finit_table[FPU_rm])();
}


static void fstsw_ax(void)
{
  *(short *) &FPU_EAX = status_word();
  no_ip_update = 1;
}

static FUNC const fstsw_table[] = {
  fstsw_ax, FPU_illegal, FPU_illegal, FPU_illegal,
  FPU_illegal, FPU_illegal, FPU_illegal, FPU_illegal
};

void fstsw_()
{
  (fstsw_table[FPU_rm])();
}


static FUNC const fp_nop_table[] = {
  fnop, FPU_illegal, FPU_illegal, FPU_illegal,
  FPU_illegal, FPU_illegal, FPU_illegal, FPU_illegal
};

void fp_nop()
{
  (fp_nop_table[FPU_rm])();
}


void fld_i_()
{
  FPU_REG *st_new_ptr;

  if ( STACK_OVERFLOW )
    { stack_overflow(); return; }

  /* fld st(i) */
  if ( NOT_EMPTY(FPU_rm) )
    { reg_move(&st(FPU_rm), st_new_ptr); push(); }
  else
    {
      if ( control_word & CW_Invalid )
	{
	  /* The masked response */
	  stack_underflow();
	}
      else
	EXCEPTION(EX_StackUnder);
    }

}


void fxch_i()
{
  /* fxch st(i) */
  FPU_REG t;
  register FPU_REG *sti_ptr = &st(FPU_rm), *st0_ptr = &st(0);

  if ( st0_ptr->tag == TW_Empty )
    {
      if ( sti_ptr->tag == TW_Empty )
	{
	  stack_underflow();
	  stack_underflow_i(FPU_rm);
	  return;
	}
      if ( control_word & CW_Invalid )
	reg_move(sti_ptr, st0_ptr);   /* Masked response */
      stack_underflow_i(FPU_rm);
      return;
    }
  if ( sti_ptr->tag == TW_Empty )
    {
      if ( control_word & CW_Invalid )
	reg_move(st0_ptr, sti_ptr);   /* Masked response */
      stack_underflow();
      return;
    }
  clear_C1();
  reg_move(st0_ptr, &t);
  reg_move(sti_ptr, st0_ptr);
  reg_move(&t, sti_ptr);
}


void ffree_()
{
  /* ffree st(i) */
  st(FPU_rm).tag = TW_Empty;
}


void ffreep()
{
  /* ffree st(i) + pop - unofficial code */
  st(FPU_rm).tag = TW_Empty;
  pop();
}


void fst_i_()
{
  /* fst st(i) */
  reg_move(&st(0), &st(FPU_rm));
}


void fstp_i()
{
  /* fstp st(i) */
  reg_move(&st(0), &st(FPU_rm));
  pop();
}

