/*
    NetWinder Floating Point Emulator
    (c) Corel Computer Corporation, 1998
    (c) Philip Blundell, 1998-1999

    Direct questions, comments to Scott Bambrough <scottb@corelcomputer.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "config.h"

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

/* XXX */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/spinlock.h>
#include <asm/atomic.h>
#include <asm/pgtable.h>
/* XXX */

#include "softfloat.h"
#include "fpopcode.h"
#include "fpmodule.h"
#include "fpa11.h"
#include "fpa11.inl"

/* external data */
extern FPA11 *fpa11;

/* kernel symbols required for signal handling */
typedef struct task_struct*	PTASK;

#ifdef MODULE
int fp_printk(const char *,...);
void fp_send_sig(unsigned long sig, PTASK p, int priv);
#if LINUX_VERSION_CODE > 0x20115
MODULE_AUTHOR("Scott Bambrough <scottb@corelcomputer.com>");
MODULE_DESCRIPTION("NWFPE floating point emulator");
#endif

#else
#define fp_printk	printk
#define fp_send_sig	send_sig
#define kern_fp_enter	fp_enter
#endif

/* kernel function prototypes required */
void C_SYMBOL_NAME(fp_setup)(void);

/* external declarations for saved kernel symbols */
extern unsigned int C_SYMBOL_NAME(kern_fp_enter);

/* forward declarations */
extern void nwfpe_enter(void);

/* Original value of fp_enter from kernel before patched by fpe_init. */ 
static unsigned int orig_fp_enter;

/* Address of user registers on the kernel stack. */
unsigned int *userRegisters;

void __init C_SYMBOL_NAME(fpe_version)(void)
{
  static const char szTitle[] = "<4>NetWinder Floating Point Emulator ";
  static const char szVersion[] = "V0.94.1 ";
  static const char szCopyright[] = "(c) 1998 Corel Computer Corp.\n";
  C_SYMBOL_NAME(fp_printk)(szTitle);
  C_SYMBOL_NAME(fp_printk)(szVersion);
  C_SYMBOL_NAME(fp_printk)(szCopyright);
}

int __init fpe_init(void)
{
  /* Display title, version and copyright information. */
  C_SYMBOL_NAME(fpe_version)();

  /* Save pointer to the old FP handler and then patch ourselves in */
  orig_fp_enter = C_SYMBOL_NAME(kern_fp_enter);
  C_SYMBOL_NAME(kern_fp_enter) = (unsigned int)C_SYMBOL_NAME(nwfpe_enter);

  return 0;
}

#ifdef MODULE
int init_module(void)
{
  return(fpe_init());
}

void cleanup_module(void)
{
  /* Restore the values we saved earlier. */
  C_SYMBOL_NAME(kern_fp_enter) = orig_fp_enter;
}
#endif

#define _ARM_pc 60
#define _ARM_cpsr 64

/*
ScottB:  November 4, 1998

Moved this function out of softfloat-specialize into fpmodule.c.
This effectively isolates all the changes required for integrating with the
Linux kernel into fpmodule.c.  Porting to NetBSD should only require modifying
fpmodule.c to integrate with the NetBSD kernel (I hope!).

[1/1/99: Not quite true any more unfortunately.  There is Linux-specific
code to access data in user space in some other source files at the 
moment.  --philb]

float_exception_flags is a global variable in SoftFloat.

This function is called by the SoftFloat routines to raise a floating
point exception.  We check the trap enable byte in the FPSR, and raise
a SIGFPE exception if necessary.  If not the relevant bits in the 
cumulative exceptions flag byte are set and we return.
*/

void float_raise(signed char flags)
{
#if 0
  printk(KERN_DEBUG "NWFPE: exception %08x at %08x from %08x\n", flags,
	 __builtin_return_address(0), userRegisters[15]);
#endif

  float_exception_flags |= flags;
  if (readFPSR() & (flags << 16))
  {
    /* raise exception */
    C_SYMBOL_NAME(fp_send_sig)(SIGFPE,C_SYMBOL_NAME(current),1);
  }
  else
  {
    /* set the cumulative exceptions flags */
    writeFPSR(flags);
  }
}
