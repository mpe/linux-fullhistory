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
#include <linux/head.h>

#include <asm/segment.h>

#include "fpu_system.h"
#include "exception.h"
#include "fpu_emu.h"


#define FPU_WRITE_BIT 0x10

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

static int reg_offset_vm86[] = {
	offsetof(struct info,___cs),
	offsetof(struct info,___vm86_ds),
	offsetof(struct info,___vm86_es),
	offsetof(struct info,___vm86_fs),
	offsetof(struct info,___vm86_gs),
	offsetof(struct info,___ss),
	offsetof(struct info,___vm86_ds)
      };

#define VM86_REG_(x) (*(unsigned short *) \
		      (reg_offset_vm86[((unsigned)x)]+(char *) FPU_info))

static int reg_offset_pm[] = {
	offsetof(struct info,___cs),
	offsetof(struct info,___ds),
	offsetof(struct info,___es),
	offsetof(struct info,___fs),
	offsetof(struct info,___gs),
	offsetof(struct info,___ss),
	offsetof(struct info,___ds)
      };

#define PM_REG_(x) (*(unsigned short *) \
		      (reg_offset_pm[((unsigned)x)]+(char *) FPU_info))


/* Decode the SIB byte. This function assumes mod != 0 */
static int sib(int mod, unsigned long *fpu_eip)
{
  unsigned char ss,index,base;
  long offset;

  RE_ENTRANT_CHECK_OFF;
  FPU_code_verify_area(1);
  base = get_fs_byte((char *) (*fpu_eip));   /* The SIB byte */
  RE_ENTRANT_CHECK_ON;
  (*fpu_eip)++;
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
      offset += (signed char) get_fs_byte((char *) (*fpu_eip));
      RE_ENTRANT_CHECK_ON;
      (*fpu_eip)++;
    }
  else if (mod == 2 || base == 5) /* The second condition also has mod==0 */
    {
      /* 32 bit displacement */
      RE_ENTRANT_CHECK_OFF;
      FPU_code_verify_area(4);
      offset += (signed) get_fs_long((unsigned long *) (*fpu_eip));
      RE_ENTRANT_CHECK_ON;
      (*fpu_eip) += 4;
    }

  return offset;
}


static unsigned long vm86_segment(unsigned char segment,
				  unsigned short *selector)
{ 
  segment--;
#ifdef PARANOID
  if ( segment > PREFIX_SS_ )
    {
      EXCEPTION(EX_INTERNAL|0x130);
      math_abort(FPU_info,SIGSEGV);
    }
#endif PARANOID
  *selector = VM86_REG_(segment);
  return (unsigned long)VM86_REG_(segment) << 4;
}


/* This should work for 16 and 32 bit protected mode. */
static long pm_address(unsigned char FPU_modrm, unsigned char segment,
		       unsigned short *selector, long offset)
{ 
  struct desc_struct descriptor;
  unsigned long base_address, limit, address, seg_top;

  segment--;
#ifdef PARANOID
  if ( segment > PREFIX_SS_ )
    {
      EXCEPTION(EX_INTERNAL|0x132);
      math_abort(FPU_info,SIGSEGV);
    }
#endif PARANOID

  *selector = PM_REG_(segment);

  descriptor = LDT_DESCRIPTOR(PM_REG_(segment));
  base_address = SEG_BASE_ADDR(descriptor);
  address = base_address + offset;
  limit = base_address
	+ (SEG_LIMIT(descriptor)+1) * SEG_GRANULARITY(descriptor) - 1;
  if ( limit < base_address ) limit = 0xffffffff;

  if ( SEG_EXPAND_DOWN(descriptor) )
    {
      if ( SEG_G_BIT(descriptor) )
	seg_top = 0xffffffff;
      else
	{
	  seg_top = base_address + (1 << 20);
	  if ( seg_top < base_address ) seg_top = 0xffffffff;
	}
      access_limit =
	(address <= limit) || (address >= seg_top) ? 0 :
	  ((seg_top-address) >= 255 ? 255 : seg_top-address);
    }
  else
    {
      access_limit =
	(address > limit) || (address < base_address) ? 0 :
	  ((limit-address) >= 254 ? 255 : limit-address+1);
    }
  if ( SEG_EXECUTE_ONLY(descriptor) ||
      (!SEG_WRITE_PERM(descriptor) && (FPU_modrm & FPU_WRITE_BIT)) )
    {
      access_limit = 0;
    }
  return address;
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

void *get_address(unsigned char FPU_modrm, unsigned long *fpu_eip,
		  struct address *addr,
/*		  unsigned short *selector, unsigned long *offset, */
		  fpu_addr_modes addr_modes)
{
  unsigned char mod;
  unsigned rm = FPU_modrm & 7;
  long *cpu_reg_ptr;
  int address = 0;     /* Initialized just to stop compiler warnings. */

  /* Memory accessed via the cs selector is write protected
     in `non-segmented' 32 bit protected mode. */
  if ( !addr_modes.default_mode && (FPU_modrm & FPU_WRITE_BIT)
      && (addr_modes.override.segment == PREFIX_CS_) )
    {
      math_abort(FPU_info,SIGSEGV);
    }

  addr->selector = FPU_DS;   /* Default, for 32 bit non-segmented mode. */

  mod = (FPU_modrm >> 6) & 3;

  if (rm == 4 && mod != 3)
    {
      address = sib(mod, fpu_eip);
    }
  else
    {
      cpu_reg_ptr = & REG_(rm);
      switch (mod)
	{
	case 0:
	  if (rm == 5)
	    {
	      /* Special case: disp32 */
	      RE_ENTRANT_CHECK_OFF;
	      FPU_code_verify_area(4);
	      address = get_fs_long((unsigned long *) (*fpu_eip));
	      (*fpu_eip) += 4;
	      RE_ENTRANT_CHECK_ON;
	      addr->offset = address;
	      return (void *) address;
	    }
	  else
	    {
	      address = *cpu_reg_ptr;  /* Just return the contents
					  of the cpu register */
	      addr->offset = address;
	      return (void *) address;
	    }
	case 1:
	  /* 8 bit signed displacement */
	  RE_ENTRANT_CHECK_OFF;
	  FPU_code_verify_area(1);
	  address = (signed char) get_fs_byte((char *) (*fpu_eip));
	  RE_ENTRANT_CHECK_ON;
	  (*fpu_eip)++;
	  break;
	case 2:
	  /* 32 bit displacement */
	  RE_ENTRANT_CHECK_OFF;
	  FPU_code_verify_area(4);
	  address = (signed) get_fs_long((unsigned long *) (*fpu_eip));
	  (*fpu_eip) += 4;
	  RE_ENTRANT_CHECK_ON;
	  break;
	case 3:
	  /* Not legal for the FPU */
	  EXCEPTION(EX_Invalid);
	}
      address += *cpu_reg_ptr;
    }

  addr->offset = address;

  switch ( addr_modes.default_mode )
    {
    case 0:
      break;
    case VM86:
      address += vm86_segment(addr_modes.override.segment,
			      (unsigned short *)&(addr->selector));
      break;
    case PM16:
    case SEG32:
      address = pm_address(FPU_modrm, addr_modes.override.segment,
			   (unsigned short *)&(addr->selector), address);
      break;
    default:
      EXCEPTION(EX_INTERNAL|0x133);
    }

  return (void *)address;
}


void *get_address_16(unsigned char FPU_modrm, unsigned long *fpu_eip,
		     struct address *addr,
/*		     unsigned short *selector, unsigned long *offset, */
		     fpu_addr_modes addr_modes)
{
  unsigned char mod;
  unsigned rm = FPU_modrm & 7;
  int address = 0;     /* Default used for mod == 0 */

  /* Memory accessed via the cs selector is write protected
     in `non-segmented' 32 bit protected mode. */
  if ( !addr_modes.default_mode && (FPU_modrm & FPU_WRITE_BIT)
      && (addr_modes.override.segment == PREFIX_CS_) )
    {
      math_abort(FPU_info,SIGSEGV);
    }

  addr->selector = FPU_DS;   /* Default, for 32 bit non-segmented mode. */

  mod = (FPU_modrm >> 6) & 3;

  switch (mod)
    {
    case 0:
      if (rm == 6)
	{
	  /* Special case: disp16 */
	  RE_ENTRANT_CHECK_OFF;
	  FPU_code_verify_area(2);
	  address = (unsigned short)get_fs_word((unsigned short *) (*fpu_eip));
	  (*fpu_eip) += 2;
	  RE_ENTRANT_CHECK_ON;
	  goto add_segment;
	}
      break;
    case 1:
      /* 8 bit signed displacement */
      RE_ENTRANT_CHECK_OFF;
      FPU_code_verify_area(1);
      address = (signed char) get_fs_byte((signed char *) (*fpu_eip));
      RE_ENTRANT_CHECK_ON;
      (*fpu_eip)++;
      break;
    case 2:
      /* 16 bit displacement */
      RE_ENTRANT_CHECK_OFF;
      FPU_code_verify_area(2);
      address = (unsigned) get_fs_word((unsigned short *) (*fpu_eip));
      (*fpu_eip) += 2;
      RE_ENTRANT_CHECK_ON;
      break;
    case 3:
      /* Not legal for the FPU */
      EXCEPTION(EX_Invalid);
      break;
    }
  switch ( rm )
    {
    case 0:
      address += FPU_info->___ebx + FPU_info->___esi;
      break;
    case 1:
      address += FPU_info->___ebx + FPU_info->___edi;
      break;
    case 2:
      address += FPU_info->___ebp + FPU_info->___esi;
      if ( addr_modes.override.segment == PREFIX_DEFAULT )
	addr_modes.override.segment = PREFIX_SS_;
      break;
    case 3:
      address += FPU_info->___ebp + FPU_info->___edi;
      if ( addr_modes.override.segment == PREFIX_DEFAULT )
	addr_modes.override.segment = PREFIX_SS_;
      break;
    case 4:
      address += FPU_info->___esi;
      break;
    case 5:
      address += FPU_info->___edi;
      break;
    case 6:
      address += FPU_info->___ebp;
      if ( addr_modes.override.segment == PREFIX_DEFAULT )
	addr_modes.override.segment = PREFIX_SS_;
      break;
    case 7:
      address += FPU_info->___ebx;
      break;
    }

 add_segment:
  address &= 0xffff;

  addr->offset = address;

  switch ( addr_modes.default_mode )
    {
    case 0:
      break;
    case VM86:
      address += vm86_segment(addr_modes.override.segment,
			      (unsigned short *)&(addr->selector));
      break;
    case PM16:
    case SEG32:
      address = pm_address(FPU_modrm, addr_modes.override.segment,
			   (unsigned short *)&(addr->selector), address);
      break;
    default:
      EXCEPTION(EX_INTERNAL|0x131);
    }

  return (void *)address ;
}
