/*---------------------------------------------------------------------------+
 |  fpu_system.h                                                             |
 |                                                                           |
 | Copyright (C) 1992    W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail apm233m@vaxc.cc.monash.edu.au    |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#ifndef _FPU_SYSTEM_H
#define _FPU_SYSTEM_H

/* system dependent definitions */

#include <linux/sched.h>
#include <linux/kernel.h>

#define I387			(current->tss.i387)
#define FPU_info		(I387.soft.info)

#define FPU_CS			(*(unsigned short *) &(FPU_info->___cs))
#define FPU_DS			(*(unsigned short *) &(FPU_info->___ds))
#define FPU_EAX			(FPU_info->___eax)
#define FPU_EFLAGS		(FPU_info->___eflags)
#define FPU_EIP			(FPU_info->___eip)
#define FPU_ORIG_EIP		(FPU_info->___orig_eip)

#define FPU_lookahead           (I387.soft.lookahead)
#define FPU_entry_eip           (I387.soft.entry_eip)

#define status_word		(I387.soft.swd)
#define control_word		(I387.soft.cwd)
#define regs			(I387.soft.regs)
#define top			(I387.soft.top)

#define ip_offset		(I387.soft.fip)
#define cs_selector		(I387.soft.fcs)
#define data_operand_offset	(I387.soft.foo)
#define operand_selector	(I387.soft.fos)

#endif
