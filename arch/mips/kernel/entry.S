/*
 * Low level exception handling
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 by Ralf Baechle
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch. The ISA dependent TLB
 * code is in arch/mips/<ISA-level>/<cputype>.S
 */
#include <linux/config.h>
#include <linux/sys.h>

#include <asm/asm.h>
#include <asm/current.h>
#include <asm/errno.h>
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/stackframe.h>
#include <asm/processor.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/unistd.h>

/*
 * Heia ...  The %lo, %hi and %HI stuff is too strong for the ELF assembler
 * and the ABI to cope with ...
 */
		.text
		.set	noreorder
		.set	mips3
		.align 4
/* XXX cli/sti ??? */
handle_bottom_half:
		mfc0	s3,CP0_STATUS		# Enable IRQs
		ori	t0,s3, 0x1f
		xori	t0,0x1e

		jal	do_bottom_half
		 mtc0	t0,CP0_STATUS

		b	9f
		 mtc0	s3,CP0_STATUS		# Restore old IRQ state

reschedule:
		jal	schedule 
		 nop

EXPORT(ret_from_sys_call)
		 lw	t0,bh_mask
		lw	t1,bh_active		# unused delay slot
		and	t0,t1
		bnez	t0,handle_bottom_half
9:		 lw	t0,PT_STATUS(sp)	# returning to kernel mode?

		andi	t1,t0, 0x10
		beqz	t1,return		# -> yes
		 lw	t1,need_resched
		bnez	t1,reschedule
		 GET_CURRENT(s0)
		lw	t0,task
		lw	a0,TASK_BLOCKED(s0)

		beq	s0,t0,return		# task[0] cannot have signals
		 lw	t0,TASK_SIGNAL(s0)

		nor	t1,zero,a0
		and	t1,t0,t1
		beqz	t1,return
		 nop

		jal	do_signal
		 move	a1,sp

		.set	noat
EXPORT(return)
		RESTORE_ALL
		eret
		.set	at

/*
 * Beware: interrupt, fast_interrupt and bad_interrupt have unusual
 * calling conventions to speedup the mess.
 *
 * a0 - interrupt number
 * s2 - destroyed
 * return values:
 * v0 - return routine
 */
		.text
		.set	at
		.align	5
NESTED(interrupt, PT_SIZE, sp)
		move	s2,ra
		mfc0	t0,CP0_STATUS			# enable IRQs
		ori	t0,0x1f
		xori	t0,0x1e
		mtc0	t0,CP0_STATUS

		jal	do_IRQ
		 move	a1,sp

		mfc0	t0,CP0_STATUS			# disable IRQs
		ori	t0,1
		xori	t0,1
		mtc0	t0,CP0_STATUS
		.set	reorder
		la	v0,ret_from_sys_call
		jr	s2
		.set	noreorder
		END(interrupt)

		.align	5
NESTED(fast_interrupt, PT_SIZE, sp)
		move	s2,ra
		jal	do_fast_IRQ
		 nop

		.set	reorder
		la	v0,return
		jr	s2
		.set	noreorder
		END(fast_interrupt)

		/*
		 * Don't return & unblock the pic
		 */
LEAF(bad_interrupt)
		j	return
		END(bad_interrupt)

		.text
		.align  5
LEAF(spurious_interrupt)
		/*
		 * Someone tried to fool us by sending an interrupt but we
		 * couldn't find a cause for it.
		 */
		lui     t1,%hi(spurious_count)
		lw      t0,%lo(spurious_count)(t1)
		la      v0,return
		addiu   t0,1

		jr      ra
		 sw      t0,%lo(spurious_count)(t1)
		END(spurious_interrupt)

/*
 * Build a default exception handler for the exceptions that don't need
 * special handlers.  If you didn't know yet - I *like* playing games with
 * the C preprocessor ...
 */
#define __BUILD_clear_none(exception)
#define __BUILD_clear_fpe(exception)                                    \
		cfc1	a1,fcr31;                                       \
		li	a2,~(0x3f<<12);                                 \
		and	a2,a1;                                          \
		ctc1	a2,fcr31;
#define __BUILD_clear_ade(exception)                                    \
		MFC0	t0,CP0_BADVADDR;                                \
		REG_S	t0,PT_BVADDR(sp);
#define __BUILD_silent(exception)

#define fmt "Got %s at %016Lx.\n"

#define __BUILD_verbose(exception)                                      \
		la	a1,8f;                                          \
		TEXT	(#exception);                                   \
		REG_L	a2,PT_EPC(sp);                                  \
		PRINT(fmt)
#define __BUILD_count(exception)                                        \
		.set	reorder;                                        \
		lw	t0,exception_count_##exception;                 \
		addiu	t0, 1;                                          \
		sw	t0,exception_count_##exception;                 \
		.set	noreorder;                                      \
		.data;                                                  \
EXPORT(exception_count_##exception);                                    \
		.word	0;                                              \
		.previous;
#define BUILD_HANDLER(exception,handler,clear,verbose)                  \
		.align	5;                                              \
		NESTED(handle_##exception, PT_SIZE, sp);                \
		.set	noat;                                           \
		SAVE_ALL;                                               \
		__BUILD_clear_##clear(exception);                       \
		STI;                                                    \
		.set	at;                                             \
		__BUILD_##verbose(exception);                           \
		li	t0,-1;                    /* not a sys call */  \
		REG_S	t0,PT_OR2(sp);                                  \
		jal	do_##handler;                                   \
		 move	a0,sp;                                          \
		j	ret_from_sys_call;                              \
		 nop;                                                   \
		END(handle_##exception)

		BUILD_HANDLER(adel,ade,ade,silent)		/* #4  */
		BUILD_HANDLER(ades,ade,ade,silent)		/* #5  */
		BUILD_HANDLER(ibe,ibe,none,verbose)		/* #6  */
		BUILD_HANDLER(dbe,dbe,none,verbose)		/* #7  */
		BUILD_HANDLER(sys,sys,none,silent)		/* #8  */
		BUILD_HANDLER(bp,bp,none,silent)		/* #9  */
		BUILD_HANDLER(ri,ri,none,silent)		/* #10 */
		BUILD_HANDLER(cpu,cpu,none,silent)		/* #11 */
		BUILD_HANDLER(ov,ov,none,silent)		/* #12 */
		BUILD_HANDLER(tr,tr,none,silent)		/* #13 */
		BUILD_HANDLER(vcei,vcei,none,verbose)		/* #14 */
		BUILD_HANDLER(fpe,fpe,fpe,silent)		/* #15 */
		BUILD_HANDLER(watch,watch,none,verbose)		/* #23 */
		BUILD_HANDLER(vced,vced,none,verbose)		/* #31 */
		BUILD_HANDLER(reserved,reserved,none,verbose)	/* others */

/*
 * Exception handler table with 32 entries.
 * This might be extended to handle software exceptions
 */
		.bss
		.align	PTRLOG
EXPORT(exception_handlers)
		.fill	32,PTRSIZE,0

/*
 * Interrupt handler table with 16 entries.
 */
EXPORT(IRQ_vectors)
		.fill	16,PTRSIZE,0

/*
 * Table of syscalls
 */
		.data
		.align	PTRLOG
EXPORT(sys_call_table)
#define SYS(call, narg) PTR call

		/* Reserved space for all SVR4 syscalls. */
		.space	(1000)*PTRSIZE

#ifdef CONFIG_BINFMT_IRIX
		/* 32bit IRIX5 system calls. */
#include "irix5sys.h"
#else
		.space	(1000)*PTRSIZE		/* No IRIX syscalls */
#endif

		/* Reserved space for all the BSD43 and POSIX syscalls. */
		.space	(2000)*PTRSIZE

		/* Linux flavoured syscalls. */
#include "syscalls.h"

/*
 * Number of arguments of each syscall
 */
EXPORT(sys_narg_table)
#undef SYS
#define SYS(call, narg) .byte narg

		/* Reserved space for all SVR4 flavoured syscalls. */
		.space	(1000)

#ifdef CONFIG_BINFMT_IRIX
		/* 32bit IRIX5 system calls. */
#include "irix5sys.h"
#else
		.space	(1000)			/* No IRIX syscalls */
#endif

		/* Reserved space for all the BSD43 and POSIX syscalls. */
		.space	(2000)

		/* Linux flavoured syscalls. */
#include "syscalls.h"
