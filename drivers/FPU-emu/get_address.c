/*---------------------------------------------------------------------------+
 |  get_address.c                                                            |
 |                                                                           |
 | Get the effective address from an FPU instruction.                        |
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


#include <linux/stddef.h>

#include <asm/segment.h>

#include "fpu_system.h"
#include "exception.h"
#include "fpu_emu.h"

static int reg_offset[] = {
	offsetof(struct info,___eax),
	offsetof(struct info,___ecx),
	offsetof(struct info,___edx),
	offsetof(struct info,___ebx),
	offsetof(struct info,___esp),
	offsetof(struct info,___ebp),
	offsetof(struct info,___esi),
	offsetof(struct info,___edi)
};

#define REG_(x) (*(long *)(reg_offset[(x)]+(char *) FPU_info))


/* Decode the SIB byte. This function assumes mod != 0 */
static void *sib(int mod)
{
  unsigned char ss,index,base;
  long offset;

  RE_ENTRANT_CHECK_OFF;
  FPU_code_verify_area(1);
  base = get_fs_byte((char *) FPU_EIP);   /* The SIB byte */
  RE_ENTRANT_CHECK_ON;
  FPU_EIP++;
  ss = base >> 6;
  index = (base >> 3) & 7;
  base &= 7;

  if ((mod == 0) && (base == 5))
    offset = 0;              /* No base register */
  else
    offset = REG_(base);

  if (index == 4)
    {
      /* No index register */
      /* A non-zero ss is illegal */
      if ( ss )
	EXCEPTION(EX_Invalid);
    }
  else
    {
      offset += (REG_(index)) << ss;
    }

  if (mod == 1)
    {
      /* 8 bit signed displacement */
      RE_ENTRANT_CHECK_OFF;
      FPU_code_verify_area(1);
      offset += (signed char) get_fs_byte((char *) FPU_EIP);
      RE_ENTRANT_CHECK_ON;
      FPU_EIP++;
    }
  else if (mod == 2 || base == 5) /* The second condition also has mod==0 */
    {
      /* 32 bit displacment */
      RE_ENTRANT_CHECK_OFF;
      FPU_code_verify_area(4);
      offset += (signed) get_fs_long((unsigned long *) FPU_EIP);
      RE_ENTRANT_CHECK_ON;
      FPU_EIP += 4;
    }

  return (void *) offset;
}


/*
       MOD R/M byte:  MOD == 3 has a special use for the FPU
                      SIB byte used iff R/M = 100b

       7   6   5   4   3   2   1   0
       .....   .........   .........
        MOD    OPCODE(2)     R/M


       SIB byte

       7   6   5   4   3   2   1   0
       .....   .........   .........
        SS      INDEX        BASE

*/

void get_address(unsigned char FPU_modrm, overrides override)
{
  unsigned char mod;
  long *cpu_reg_ptr;
  int offset = 0;     /* Initialized just to stop compiler warnings. */

#ifndef PECULIAR_486
  /* This is a reasonable place to do this */
  FPU_data_selector = FPU_DS;
#endif PECULIAR_486

  mod = (FPU_modrm >> 6) & 3;

  if (FPU_rm == 4 && mod != 3)
    {
      FPU_data_address = sib(mod);
      return;
    }

  cpu_reg_ptr = & REG_(FPU_rm);
  switch (mod)
    {
    case 0:
      if (FPU_rm == 5)
	{
	  /* Special case: disp16 or disp32 */
	  RE_ENTRANT_CHECK_OFF;
	  if ( override.address_size == ADDR_SIZE_PREFIX )
	    {
	      FPU_code_verify_area(2);
	      offset = get_fs_word((unsigned short *) FPU_EIP);
	      FPU_EIP += 2;
	    }
	  else
	    {
	      FPU_code_verify_area(4);
	      offset = get_fs_long((unsigned long *) FPU_EIP);
	      FPU_EIP += 4;
	    }
	  RE_ENTRANT_CHECK_ON;
	  FPU_data_address = (void *) offset;
	  return;
	}
      else
	{
	  FPU_data_address = (void *)*cpu_reg_ptr;  /* Just return the contents
						   of the cpu register */
	  return;
	}
    case 1:
      /* 8 bit signed displacement */
      RE_ENTRANT_CHECK_OFF;
      FPU_code_verify_area(1);
      offset = (signed char) get_fs_byte((char *) FPU_EIP);
      RE_ENTRANT_CHECK_ON;
      FPU_EIP++;
      break;
    case 2:
      /* 16 or 32 bit displacement */
      RE_ENTRANT_CHECK_OFF;
      if ( override.address_size == ADDR_SIZE_PREFIX )
	{
	  FPU_code_verify_area(2);
	  offset = (signed) get_fs_word((unsigned short *) FPU_EIP);
	  FPU_EIP += 2;
	}
      else
	{
	  FPU_code_verify_area(4);
	  offset = (signed) get_fs_long((unsigned long *) FPU_EIP);
	  FPU_EIP += 4;
	}
      RE_ENTRANT_CHECK_ON;
      break;
    case 3:
      /* Not legal for the FPU */
      EXCEPTION(EX_Invalid);
    }

  FPU_data_address = offset + (char *)*cpu_reg_ptr;
  if ( override.address_size == ADDR_SIZE_PREFIX )
    FPU_data_address = (void *)((long)FPU_data_address & 0xffff);
}
