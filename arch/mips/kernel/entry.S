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
 * code is in arch/mips/kernel/<cputype>.S
 */
#include <linux/sys.h>

#include <asm/asm.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/stackframe.h>
#include <asm/processor.h>
#include <asm/unistd.h>

/*
 * These are offsets into the task-struct.
 */
state		=  0
counter		=  4
priority	=  8
signal		= 12
blocked		= 16
flags		= 20
errno		= 24
exec_domain	= 60

#ifdef __SMP__
#error "Fix this for SMP"
#else
#define current current_set
#endif

/*
 * Heia ...  The %lo, %hi and %HI stuff is too strong for the ELF assembler
 * and the ABI to cope with ...
 */
		.text
		.set	noreorder
		.align 4
handle_bottom_half:
		lui	s0,%hi(intr_count)
		lw	s1,%lo(intr_count)(s0)
		mfc0	s3,CP0_STATUS		# Enable IRQs
		addiu	s2,s1,1
		sw	s2,%lo(intr_count)(s0)
		ori	t0,s3,0x1f
		xori	t0,0x1e
		jal	do_bottom_half
		mtc0	t0,CP0_STATUS		# delay slot
		mtc0	s3,CP0_STATUS		# Restore old IRQ state
		b	9f
		sw	s1,%lo(intr_count)(s0)	# delay slot

reschedule:	jal	schedule 
		nop				# delay slot
EXPORT(ret_from_sys_call)
		lw	t0,intr_count		# bottom half
		bnez	t0,return

9:		lw	t0,bh_mask		# delay slot
		lw	t1,bh_active		# unused delay slot
		and	t0,t1
		bnez	t0,handle_bottom_half

		lw	t0,FR_STATUS(sp)	# returning to kernel mode?
		andi	t1,t0,0x10
		beqz	t1,return		# -> yes

		mfc0	t0,CP0_STATUS		# delay slot
		lw	t1,need_resched
		ori	t0,0x1f			# enable irqs
		xori	t0,0x1e
		bnez	t1,reschedule
		mtc0	t0,CP0_STATUS		# delay slot

		lw	s0,current
		lw	t0,task
		lw	a0,blocked(s0)
		beq	s0,t0,return		# task[0] cannot have signals
						# save blocked in a0 for
						# signal handling
		lw	t0,signal(s0)
		nor	t1,zero,a0
		and	t1,t0,t1
		beqz	t1,return
		nop

		jal	do_signal
		move	a1,sp			# delay slot
		
		.set	noat
EXPORT(return)	RESTORE_ALL
		ERET
		.set	at

/*
 * Beware: interrupt, fast_interrupt and bad_interrupt have unusual
 * calling conventions to speedup the mess.
 *
 * t1 - interrupt number
 * s2 - destroyed
 * return values:
 * v0 - return routine
 */
		.text
		.set	at
		.align	5
NESTED(interrupt, FR_SIZE, sp)
		move	s2,ra
		mfc0	t0,CP0_STATUS			# enable IRQs
		ori	t0,0x1f
		xori	t0,0x1e
		mtc0	t0,CP0_STATUS
		move	a0,t1
		jal	do_IRQ
		move	a1,sp				# delay slot
		mfc0	t0,CP0_STATUS			# disable IRQs
		ori	t0,1
		xori	t0,1
		la	v0,ret_from_sys_call
		jr	s2
		mtc0	t0,CP0_STATUS			# delay slot
		END(interrupt)

		.align	5
NESTED(fast_interrupt, FR_SIZE, sp)
		move	s2,ra
		move	a0,t1
		jal	do_fast_IRQ
		move	a1,sp				# delay slot
		lui	v0,%hi(return)
		jr	s2
		addiu	v0,%lo(return)			# delay slot
		END(fast_interrupt)

LEAF(bad_interrupt)
		/*
		 * Don't return & unblock the pic
		 */
		j	return
		nop
		END(bad_interrupt)

/*
 * do_syscall calls the function in a1 with upto 7 arguments.  If over
 * four arguments are being requested, the additional arguments will
 * be copied from the user stack pointed to by a0->reg29.
 * Note that this routine relies on the GNU assemblers weak instruction
 * scheduling abilities to generate the best possible code for all MIPS CPUs.
 *
 * a0 (struct pt_regs *)  pointer to user registers
 * a1 (syscall_t)         pointer to syscall to do
 * a2 (int)               number of arguments to syscall
 */
		.set	noreorder
		.text
NESTED(do_syscalls, 32, sp)
		subu	sp,32
		sw	ra,28(sp)
		sll	a2,a2,PTRLOG
		lw	t0,dst(a2)
		move	t2,a1
		jalr	t0
		lw	t0,FR_REG29(a0)		# get old user stack pointer

		.set	reorder			# for sake of R3000
7:		lw	t1,24(t0)		# parameter #7 from usp
		sw	t1,24(t0)
6:		lw	t1,20(t0)		# parameter #6 from usp
		sw	t1,20(t0)
5:		lw	t1,16(t0)		# parameter #5 from usp
		sw	t1,16(t0)
		.set	noreorder
4:		lw	a3,FR_REG7(a0)		# 4 args
3:		lw	a2,FR_REG6(a0)		# 3 args
2:		lw	a1,FR_REG5(a0)		# 2 args
1:		jalr	t2			# 1 args
		lw	a0,FR_REG4(a0)		# delay slot
		.set	reorder
		lw	ra,28(sp)
		addiu	sp,32
		jr	ra
0:		jalr	t2			# 0 args, just pass a0
		lw	ra,28(sp)
		addiu	sp,32
		jr	ra
		END(do_syscalls)
		.set	noreorder

		.rdata
		.align	PTRLOG
dst:		PTR	0b, 1b, 2b, 3b, 4b, 5b, 6b, 7b

/*
 * Build a default exception handler for the exceptions that don't need
 * special handlers.  If you didn't know yet - I *like* playing games with
 * the C preprocessor ...
 */
#define __BUILD_silent(exception)
#define __BUILD_verbose(exception)                                      \
		la	a1,8f;                                          \
		TEXT	(#exception);                                   \
		lw	a2,FR_EPC(sp);                                  \
		PRINT("Got %s at %08x.\n")
#define __BUILD_count(exception)                                        \
		.set	reorder;                                        \
		lw	t0,exception_count_##exception;                 \
		addiu	t0,1;                                           \
		sw	t0,exception_count_##exception;                 \
		.set	noreorder;                                      \
		.data;                                                  \
EXPORT(exception_count_##exception);                                    \
		.word	0;                                              \
		.text;
#define BUILD_HANDLER(exception,verbose)                                \
		.text;                                                  \
		.align	5;                                              \
		NESTED(handle_##exception, FR_SIZE, sp);                \
		.set	noat;                                           \
		SAVE_ALL;                                               \
		STI;                                                    \
		.set	at;                                             \
		__BUILD_##verbose(exception);                           \
		li	t0,-1;                   /* not a sys call */   \
		sw	t0,FR_ORIG_REG2(sp);                            \
		jal	do_##exception;                                 \
		move	a0,sp;                   /* delay slot */       \
		j	ret_from_sys_call;                              \
		nop;                             /* delay slot */       \
		END(handle_##exception)

		BUILD_HANDLER(adel,verbose)		/* #4  */
		BUILD_HANDLER(ades,verbose)		/* #5  */
		BUILD_HANDLER(ibe,verbose)		/* #6  */
		BUILD_HANDLER(dbe,verbose)		/* #7  */
		BUILD_HANDLER(sys,silent)		/* #8  */
		BUILD_HANDLER(bp,verbose)		/* #9  */
		BUILD_HANDLER(ri,verbose)		/* #10 */
		BUILD_HANDLER(cpu,silent)		/* #11 */
		BUILD_HANDLER(ov,verbose)		/* #12 */
		BUILD_HANDLER(tr,verbose)		/* #13 */
		BUILD_HANDLER(vcei,verbose)		/* #14 */
		BUILD_HANDLER(fpe,verbose)		/* #15 */
		BUILD_HANDLER(watch,verbose)		/* #23 */
		BUILD_HANDLER(vced,verbose)		/* #31 */
		BUILD_HANDLER(reserved,verbose)		/* others */

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
		/*
		 * Reserved space for all the SVR4, SVR, BSD43 and POSIX
		 * flavoured syscalls.
		 */
		.space	(__NR_Linux)*PTRSIZE

		/*
		 * Linux flavoured syscalls.
		 */
#define SYS(call, narg) PTR call
#include "syscalls.h"

/*
 * Number of arguments of each syscall
 */
EXPORT(sys_narg_table)
		/*
		 * Reserved space for all the SVR4, SVR, BSD43 and POSIX
		 * flavoured syscalls.
		 */
		.space	(__NR_Linux)

		/*
		 * Linux flavoured syscalls.
		 */
#undef SYS
#define SYS(call, narg) .byte narg
#include "syscalls.h"
