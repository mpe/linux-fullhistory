/*---------------------------------------------------------------------------+
 |  load_store.c                                                             |
 |                                                                           |
 | This file contains most of the code to interpret the FPU instructions     |
 | which load and store from user memory.                                    |
 |                                                                           |
 | Copyright (C) 1992,1993,1994                                              |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@vaxc.cc.monash.edu.au    |
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
#include "fpu_emu.h"
#include "status_w.h"
#include "control_w.h"


#define _NONE_ 0   /* st0_ptr etc not needed */
#define _REG0_ 1   /* Will be storing st(0) */
#define _PUSH_ 3   /* Need to check for space to push onto stack */
#define _null_ 4   /* Function illegal or not implemented */

#define pop_0()	{ st0_ptr->tag = TW_Empty; top++; }


static unsigned char const type_table[32] = {
  _PUSH_, _PUSH_, _PUSH_, _PUSH_,
  _null_, _null_, _null_, _null_,
  _REG0_, _REG0_, _REG0_, _REG0_,
  _REG0_, _REG0_, _REG0_, _REG0_,
  _NONE_, _null_, _NONE_, _PUSH_,
  _NONE_, _PUSH_, _null_, _PUSH_,
  _NONE_, _null_, _NONE_, _REG0_,
  _NONE_, _REG0_, _NONE_, _REG0_
  };

unsigned char const data_sizes_16[32] = {
  4,  4,  8,  2,  0,  0,  0,  0,
  4,  4,  8,  2,  4,  4,  8,  2,
  14, 0, 94, 10,  2, 10,  0,  8,  
  14, 0, 94, 10,  2, 10,  2,  8
};

unsigned char const data_sizes_32[32] = {
  4,  4,  8,  2,  0,  0,  0,  0,
  4,  4,  8,  2,  4,  4,  8,  2,
  28, 0,108, 10,  2, 10,  0,  8,  
  28, 0,108, 10,  2, 10,  2,  8
};

int load_store_instr(unsigned char type, fpu_addr_modes addr_modes,
		     void *data_address)
{
  FPU_REG loaded_data;
  FPU_REG *st0_ptr;

  st0_ptr = NULL;    /* Initialized just to stop compiler warnings. */

  if ( addr_modes.default_mode & PROTECTED )
    {
      if ( addr_modes.default_mode == SEG32 )
	{
	  if ( access_limit < data_sizes_32[type] )
	    math_abort(FPU_info,SIGSEGV);
	}
      else if ( addr_modes.default_mode == PM16 )
	{
	  if ( access_limit < data_sizes_16[type] )
	    math_abort(FPU_info,SIGSEGV);
	}
#ifdef PARANOID
      else
	EXCEPTION(EX_INTERNAL|0x140);
#endif PARANOID
    }

  switch ( type_table[type] )
    {
    case _NONE_:
      break;
    case _REG0_:
      st0_ptr = &st(0);       /* Some of these instructions pop after
				 storing */
      break;
    case _PUSH_:
      {
	st0_ptr = &st(-1);
	if ( st0_ptr->tag != TW_Empty )
	  { stack_overflow(); return 0; }
	top--;
      }
      break;
    case _null_:
      FPU_illegal();
      return 0;
#ifdef PARANOID
    default:
      EXCEPTION(EX_INTERNAL|0x141);
      return 0;
#endif PARANOID
    }

  switch ( type )
    {
    case 000:       /* fld m32real */
      clear_C1();
      reg_load_single((float *)data_address, &loaded_data);
      if ( (loaded_data.tag == TW_NaN) &&
	  real_2op_NaN(&loaded_data, &loaded_data, &loaded_data) )
	{
	  top++;
	  break;
	}
      reg_move(&loaded_data, st0_ptr);
      break;
    case 001:      /* fild m32int */
      clear_C1();
      reg_load_int32((long *)data_address, st0_ptr);
      break;
    case 002:      /* fld m64real */
      clear_C1();
      reg_load_double((double *)data_address, &loaded_data);
      if ( (loaded_data.tag == TW_NaN) &&
	  real_2op_NaN(&loaded_data, &loaded_data, &loaded_data) )
	{
	  top++;
	  break;
	}
      reg_move(&loaded_data, st0_ptr);
      break;
    case 003:      /* fild m16int */
      clear_C1();
      reg_load_int16((short *)data_address, st0_ptr);
      break;
    case 010:      /* fst m32real */
      clear_C1();
      reg_store_single((float *)data_address, st0_ptr);
      break;
    case 011:      /* fist m32int */
      clear_C1();
      reg_store_int32((long *)data_address, st0_ptr);
      break;
    case 012:     /* fst m64real */
      clear_C1();
      reg_store_double((double *)data_address, st0_ptr);
      break;
    case 013:     /* fist m16int */
      clear_C1();
      reg_store_int16((short *)data_address, st0_ptr);
      break;
    case 014:     /* fstp m32real */
      clear_C1();
      if ( reg_store_single((float *)data_address, st0_ptr) )
	pop_0();  /* pop only if the number was actually stored
		     (see the 80486 manual p16-28) */
      break;
    case 015:     /* fistp m32int */
      clear_C1();
      if ( reg_store_int32((long *)data_address, st0_ptr) )
	pop_0();  /* pop only if the number was actually stored
		     (see the 80486 manual p16-28) */
      break;
    case 016:     /* fstp m64real */
      clear_C1();
      if ( reg_store_double((double *)data_address, st0_ptr) )
	pop_0();  /* pop only if the number was actually stored
		     (see the 80486 manual p16-28) */
      break;
    case 017:     /* fistp m16int */
      clear_C1();
      if ( reg_store_int16((short *)data_address, st0_ptr) )
	pop_0();  /* pop only if the number was actually stored
		     (see the 80486 manual p16-28) */
      break;
    case 020:     /* fldenv  m14/28byte */
      fldenv(addr_modes, (char *)data_address);
      /* Ensure that the values just loaded are not changed by
	 fix-up operations. */
      return 1;
    case 022:     /* frstor m94/108byte */
      frstor(addr_modes, (char *)data_address);
      /* Ensure that the values just loaded are not changed by
	 fix-up operations. */
      return 1;
    case 023:     /* fbld m80dec */
      clear_C1();
      reg_load_bcd((char *)data_address, st0_ptr);
      break;
    case 024:     /* fldcw */
      RE_ENTRANT_CHECK_OFF;
      FPU_verify_area(VERIFY_READ, data_address, 2);
      control_word = get_fs_word((unsigned short *) data_address);
      RE_ENTRANT_CHECK_ON;
      if ( partial_status & ~control_word & CW_Exceptions )
	partial_status |= (SW_Summary | SW_Backward);
      else
	partial_status &= ~(SW_Summary | SW_Backward);
#ifdef PECULIAR_486
      control_word |= 0x40;  /* An 80486 appears to always set this bit */
#endif PECULIAR_486
      return 1;
    case 025:      /* fld m80real */
      clear_C1();
      reg_load_extended((long double *)data_address, st0_ptr);
      break;
    case 027:      /* fild m64int */
      clear_C1();
      reg_load_int64((long long *)data_address, st0_ptr);
      break;
    case 030:     /* fstenv  m14/28byte */
      fstenv(addr_modes, (char *)data_address);
      return 1;
    case 032:      /* fsave */
      fsave(addr_modes, (char *)data_address);
      return 1;
    case 033:      /* fbstp m80dec */
      clear_C1();
      if ( reg_store_bcd((char *)data_address, st0_ptr) )
	pop_0();  /* pop only if the number was actually stored
		     (see the 80486 manual p16-28) */
      break;
    case 034:      /* fstcw m16int */
      RE_ENTRANT_CHECK_OFF;
      FPU_verify_area(VERIFY_WRITE,data_address,2);
      put_fs_word(control_word, (short *) data_address);
      RE_ENTRANT_CHECK_ON;
      return 1;
    case 035:      /* fstp m80real */
      clear_C1();
      if ( reg_store_extended((long double *)data_address, st0_ptr) )
	pop_0();  /* pop only if the number was actually stored
		     (see the 80486 manual p16-28) */
      break;
    case 036:      /* fstsw m2byte */
      RE_ENTRANT_CHECK_OFF;
      FPU_verify_area(VERIFY_WRITE,data_address,2);
      put_fs_word(status_word(),(short *) data_address);
      RE_ENTRANT_CHECK_ON;
      return 1;
    case 037:      /* fistp m64int */
      clear_C1();
      if ( reg_store_int64((long long *)data_address, st0_ptr) )
	pop_0();  /* pop only if the number was actually stored
		     (see the 80486 manual p16-28) */
      break;
    }
  return 0;
}
