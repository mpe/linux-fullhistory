/*
 * ia64/kernel/entry.S
 *
 * Kernel entry points.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Asit Mallick <Asit.K.Mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <Don.Dugger@intel.com>
 */
/*
 * Global (preserved) predicate usage on syscall entry/exit path:
 *
 * 
 *	pEOI:		See entry.h.
 *	pKern:		See entry.h.
 *	pSys:		See entry.h.
 *	pNonSys:	!pSys
 *	p2:		(Alias of pKern!) True if any signals are pending.
 *	p16/p17:	Used by stubs calling ia64_do_signal to indicate if current task
 *			has PF_PTRACED flag bit set.  p16 is true if so, p17 is the complement.
 */

#include <linux/config.h>

#include <asm/errno.h>
#include <asm/offsets.h>
#include <asm/processor.h>
#include <asm/unistd.h>

#include "entry.h"

	.text
	.psr abi64
	.psr lsb
	.lsb

	/*
	 * execve() is special because in case of success, we need to
	 * setup a null register window frame.
	 */
	.align 16
	.proc ia64_execve
ia64_execve:
	alloc loc0=ar.pfs,3,2,4,0
	mov loc1=rp
	mov out0=in0			// filename
	;;				// stop bit between alloc and call
	mov out1=in1			// argv
	mov out2=in2			// envp
	add out3=16,sp			// regs
	br.call.sptk.few rp=sys_execve
.ret0:	cmp4.ge p6,p0=r8,r0
	mov ar.pfs=loc0			// restore ar.pfs
	;;
(p6)	mov ar.pfs=r0			// clear ar.pfs in case of success
	sxt4 r8=r8			// return 64-bit result
	mov rp=loc1

	br.ret.sptk.few rp
	.endp ia64_execve

	.align 16
	.global sys_clone
	.proc sys_clone
sys_clone:
	alloc r16=ar.pfs,2,2,3,0;;
	movl r28=1f
	mov loc1=rp
	br.cond.sptk.many save_switch_stack
1:
	mov loc0=r16				// save ar.pfs across do_fork
	adds out2=IA64_SWITCH_STACK_SIZE+16,sp
	adds r2=IA64_SWITCH_STACK_SIZE+IA64_PT_REGS_R12_OFFSET+16,sp
	cmp.eq p8,p9=in1,r0			// usp == 0?
	mov out0=in0				// out0 = clone_flags
	;;
(p8)	ld8 out1=[r2]				// fetch usp from pt_regs.r12
(p9)	mov out1=in1
	br.call.sptk.few rp=do_fork
.ret1:
	mov ar.pfs=loc0
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// pop the switch stack
	mov rp=loc1
	;;
	br.ret.sptk.many rp
	.endp sys_clone

/*
 * prev_task <- switch_to(struct task_struct *next)
 */
	.align 16
	.global ia64_switch_to
	.proc ia64_switch_to
ia64_switch_to:
	alloc r16=ar.pfs,1,0,0,0
	movl r28=1f
	br.cond.sptk.many save_switch_stack
1:
	// disable interrupts to ensure atomicity for next few instructions:
	mov r17=psr		// M-unit
	;;
	rsm psr.i		// M-unit
	dep r18=-1,r0,0,61	// build mask 0x1fffffffffffffff
	;;
	srlz.d
	;;
	adds r22=IA64_TASK_THREAD_KSP_OFFSET,r13
	adds r21=IA64_TASK_THREAD_KSP_OFFSET,in0
	;;
	st8 [r22]=sp		// save kernel stack pointer of old task
	ld8 sp=[r21]		// load kernel stack pointer of new task
	and r20=in0,r18		// physical address of "current"
	;;
	mov r8=r13		// return pointer to previously running task
	mov r13=in0		// set "current" pointer
	mov ar.k6=r20		// copy "current" into ar.k6
	;;
	// restore interrupts
	mov psr.l=r17
	;;
	srlz.d

	movl r28=1f
	br.cond.sptk.many load_switch_stack
1:
	br.ret.sptk.few rp
	.endp ia64_switch_to

	/*
	 * Like save_switch_stack, but also save the stack frame that is active
	 * at the time this function is called.
	 */
	.align 16
	.proc save_switch_stack_with_current_frame
save_switch_stack_with_current_frame:
1:	{
	  alloc r16=ar.pfs,0,0,0,0		// pass ar.pfs to save_switch_stack
	  mov r28=ip
	}
	;;
	adds r28=1f-1b,r28
	br.cond.sptk.many save_switch_stack
1:	br.ret.sptk.few rp
	.endp save_switch_stack_with_current_frame
/*
 * Note that interrupts are enabled during save_switch_stack and
 * load_switch_stack.  This means that we may get an interrupt with
 * "sp" pointing to the new kernel stack while ar.bspstore is still
 * pointing to the old kernel backing store area.  Since ar.rsc,
 * ar.rnat, ar.bsp, and ar.bspstore are all preserved by interrupts,
 * this is not a problem.
 */

/*
 * save_switch_stack:
 *	- r16 holds ar.pfs
 *	- r28 holds address to return to
 *	- rp (b0) holds return address to save
 */
	.align 16
	.global save_switch_stack
	.proc save_switch_stack
save_switch_stack:
	flushrs			// flush dirty regs to backing store (must be first in insn group)
	mov r17=ar.unat		// preserve caller's
	adds r2=-IA64_SWITCH_STACK_SIZE+16,sp	// r2 = &sw->caller_unat
	;;
	mov r18=ar.fpsr		// preserve fpsr
	mov ar.rsc=r0		// put RSE in mode: enforced lazy, little endian, pl 0
	;;
	mov r19=ar.rnat
	adds r3=-IA64_SWITCH_STACK_SIZE+24,sp	// r3 = &sw->ar_fpsr

	// Note: the instruction ordering is important here: we can't
	// store anything to the switch stack before sp is updated
	// as otherwise an interrupt might overwrite the memory!
	adds sp=-IA64_SWITCH_STACK_SIZE,sp
	;;
	st8 [r2]=r17,16
	st8 [r3]=r18,24
	;;
	stf.spill [r2]=f2,32
	stf.spill [r3]=f3,32
	mov r21=b0
	;;
	stf.spill [r2]=f4,32
	stf.spill [r3]=f5,32
	;;
	stf.spill [r2]=f10,32
	stf.spill [r3]=f11,32
	mov r22=b1
	;;
	stf.spill [r2]=f12,32
	stf.spill [r3]=f13,32
	mov r23=b2
	;;
	stf.spill [r2]=f14,32
	stf.spill [r3]=f15,32
	mov r24=b3
	;;
	stf.spill [r2]=f16,32
	stf.spill [r3]=f17,32
	mov r25=b4
	;;
	stf.spill [r2]=f18,32
	stf.spill [r3]=f19,32
	mov r26=b5
	;;
	stf.spill [r2]=f20,32
	stf.spill [r3]=f21,32
	mov r17=ar.lc				// I-unit
	;;
	stf.spill [r2]=f22,32
	stf.spill [r3]=f23,32
	;;
	stf.spill [r2]=f24,32
	stf.spill [r3]=f25,32
	;;
	stf.spill [r2]=f26,32
	stf.spill [r3]=f27,32
	;;
	stf.spill [r2]=f28,32
	stf.spill [r3]=f29,32
	;;
	stf.spill [r2]=f30,32
	stf.spill [r3]=f31,24
	;;
	st8.spill [r2]=r4,16
	st8.spill [r3]=r5,16
	;;
	st8.spill [r2]=r6,16
	st8.spill [r3]=r7,16
	;;
	st8 [r2]=r21,16		// save b0
	st8 [r3]=r22,16		// save b1
	/* since we're done with the spills, read and save ar.unat: */
	mov r18=ar.unat		// M-unit
	mov r20=ar.bspstore	// M-unit
	;;
	st8 [r2]=r23,16		// save b2
	st8 [r3]=r24,16		// save b3
	;;
	st8 [r2]=r25,16		// save b4
	st8 [r3]=r26,16		// save b5
	;;
	st8 [r2]=r16,16		// save ar.pfs
	st8 [r3]=r17,16		// save ar.lc
	mov r21=pr
	;;
	st8 [r2]=r18,16		// save ar.unat
	st8 [r3]=r19,16		// save ar.rnat
	mov b7=r28
	;;
	st8 [r2]=r20		// save ar.bspstore
	st8 [r3]=r21		// save predicate registers
	mov ar.rsc=3		// put RSE back into eager mode, pl 0
	br.cond.sptk.few b7
	.endp save_switch_stack

/*
 * load_switch_stack:
 *	- r28 holds address to return to
 */
	.align 16
	.proc load_switch_stack
load_switch_stack:
	invala			// invalidate ALAT
	adds r2=IA64_SWITCH_STACK_B0_OFFSET+16,sp	// get pointer to switch_stack.b0
	mov ar.rsc=r0		// put RSE into enforced lazy mode
	adds r3=IA64_SWITCH_STACK_B0_OFFSET+24,sp	// get pointer to switch_stack.b1
	;;
	ld8 r21=[r2],16		// restore b0
	ld8 r22=[r3],16		// restore b1
	;;
	ld8 r23=[r2],16		// restore b2
	ld8 r24=[r3],16		// restore b3
	;;
	ld8 r25=[r2],16		// restore b4
	ld8 r26=[r3],16		// restore b5
	;;
	ld8 r16=[r2],16		// restore ar.pfs
	ld8 r17=[r3],16		// restore ar.lc
	;;
	ld8 r18=[r2],16		// restore ar.unat
	ld8 r19=[r3],16		// restore ar.rnat
	mov b0=r21
	;;
	ld8 r20=[r2]		// restore ar.bspstore
	ld8 r21=[r3]		// restore predicate registers
	mov ar.pfs=r16
	;;
	mov ar.bspstore=r20
	;;
	loadrs			// invalidate stacked regs outside current frame
	adds r2=16-IA64_SWITCH_STACK_SIZE,r2	// get pointer to switch_stack.caller_unat
	;;			// stop bit for rnat dependency
	mov ar.rnat=r19
	mov ar.unat=r18		// establish unat holding the NaT bits for r4-r7
	adds r3=16-IA64_SWITCH_STACK_SIZE,r3	// get pointer to switch_stack.ar_fpsr
	;;
	ld8 r18=[r2],16		// restore caller's unat
	ld8 r19=[r3],24		// restore fpsr
	mov ar.lc=r17
	;;
	ldf.fill f2=[r2],32
	ldf.fill f3=[r3],32
	mov pr=r21,-1
	;;
	ldf.fill f4=[r2],32
	ldf.fill f5=[r3],32
	;;
	ldf.fill f10=[r2],32
	ldf.fill f11=[r3],32
	mov b1=r22
	;;
	ldf.fill f12=[r2],32
	ldf.fill f13=[r3],32
	mov b2=r23
	;;
	ldf.fill f14=[r2],32
	ldf.fill f15=[r3],32
	mov b3=r24
	;;
	ldf.fill f16=[r2],32
	ldf.fill f17=[r3],32
	mov b4=r25
	;;
	ldf.fill f18=[r2],32
	ldf.fill f19=[r3],32
	mov b5=r26
	;;
	ldf.fill f20=[r2],32
	ldf.fill f21=[r3],32
	;;
	ldf.fill f22=[r2],32
	ldf.fill f23=[r3],32
	;;
	ldf.fill f24=[r2],32
	ldf.fill f25=[r3],32
	;;
	ldf.fill f26=[r2],32
	ldf.fill f27=[r3],32
	;;
	ldf.fill f28=[r2],32
	ldf.fill f29=[r3],32
	;;
	ldf.fill f30=[r2],32
	ldf.fill f31=[r3],24
	;;
	ld8.fill r4=[r2],16
	ld8.fill r5=[r3],16
	mov b7=r28
	;;
	ld8.fill r6=[r2],16
	ld8.fill r7=[r3],16
	mov ar.unat=r18				// restore caller's unat
	mov ar.fpsr=r19				// restore fpsr
	mov ar.rsc=3				// put RSE back into eager mode, pl 0
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// pop switch_stack
	br.cond.sptk.few b7
	.endp load_switch_stack

	.align 16
	.global __ia64_syscall
	.proc __ia64_syscall
__ia64_syscall:
	.regstk 6,0,0,0
	mov r15=in5				// put syscall number in place
	break __BREAK_SYSCALL
	movl r2=errno
	cmp.eq p6,p7=-1,r10
	;;
(p6)	st4 [r2]=r8
(p6)	mov r8=-1
	br.ret.sptk.few rp
	.endp __ia64_syscall

	//
	// We invoke syscall_trace through this intermediate function to
	// ensure that the syscall input arguments are not clobbered.  We
	// also use it to preserve b6, which contains the syscall entry point.
	//
	.align 16
	.global invoke_syscall_trace
	.proc invoke_syscall_trace
invoke_syscall_trace:
	alloc loc0=ar.pfs,8,3,0,0
	;;			// WAW on CFM at the br.call
	mov loc1=rp
	br.call.sptk.many rp=save_switch_stack_with_current_frame	// must preserve b6!!
.ret2:	mov loc2=b6
	br.call.sptk.few rp=syscall_trace
.ret3:	adds sp=IA64_SWITCH_STACK_SIZE,sp	// drop switch_stack frame
	mov rp=loc1
	mov ar.pfs=loc0
	mov b6=loc2
	;;
	br.ret.sptk.few rp
	.endp invoke_syscall_trace

	//
	// Invoke a system call, but do some tracing before and after the call.
	// We MUST preserve the current register frame throughout this routine
	// because some system calls (such as ia64_execve) directly
	// manipulate ar.pfs.
	//
	// Input:
	//	r15 = syscall number
	//	b6  = syscall entry point
	//
	.global ia64_trace_syscall
	.global ia64_strace_leave_kernel
	.global ia64_strace_clear_r8

	.proc ia64_strace_clear_r8
ia64_strace_clear_r8:		// this is where we return after cloning when PF_TRACESYS is on
# ifdef CONFIG_SMP
	br.call.sptk.few rp=invoke_schedule_tail
# endif
	mov r8=0
	br strace_check_retval
	.endp ia64_strace_clear_r8

	.proc ia64_trace_syscall
ia64_trace_syscall:
	br.call.sptk.few rp=invoke_syscall_trace // give parent a chance to catch syscall args
.ret4:	br.call.sptk.few rp=b6			// do the syscall
strace_check_retval:
.ret5:	cmp.lt p6,p0=r8,r0			// syscall failed?
	;;
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp	// r2 = &pt_regs.r8
	adds r3=IA64_PT_REGS_R8_OFFSET+32,sp	// r3 = &pt_regs.r10
	mov r10=0
(p6)	br.cond.sptk.few strace_error		// syscall failed ->
	;;					// avoid RAW on r10
strace_save_retval:
	st8.spill [r2]=r8			// store return value in slot for r8
	st8.spill [r3]=r10			// clear error indication in slot for r10
ia64_strace_leave_kernel:
	br.call.sptk.few rp=invoke_syscall_trace // give parent a chance to catch return value
.ret6:	br.cond.sptk.many ia64_leave_kernel

strace_error:
	ld8 r3=[r2]				// load pt_regs.r8
	sub r9=0,r8				// negate return value to get errno value
	;;
	cmp.ne p6,p0=r3,r0			// is pt_regs.r8!=0?
	adds r3=16,r2				// r3=&pt_regs.r10
	;;
(p6)	mov r10=-1
(p6)	mov r8=r9
	br.cond.sptk.few strace_save_retval
	.endp ia64_trace_syscall

/*
 * A couple of convenience macros to help implement/understand the state
 * restoration that happens at the end of ia64_ret_from_syscall.
 */
#define rARPR		r31
#define rCRIFS		r30
#define rCRIPSR		r29
#define rCRIIP		r28
#define rARRSC		r27
#define rARPFS		r26
#define rARUNAT		r25
#define rARRNAT		r24
#define rARBSPSTORE	r23
#define rKRBS		r22
#define rB6		r21

	.align 16
	.global ia64_ret_from_syscall
	.global ia64_ret_from_syscall_clear_r8
	.global ia64_leave_kernel
	.proc ia64_ret_from_syscall
ia64_ret_from_syscall_clear_r8:
#ifdef CONFIG_SMP
	// In SMP mode, we need to call schedule_tail to complete the scheduling process.
	// Called by ia64_switch_to after do_fork()->copy_thread().  r8 contains the
	// address of the previously executing task.
	br.call.sptk.few rp=invoke_schedule_tail
.ret7:
#endif                  
	mov r8=0
	;;					// added stop bits to prevent r8 dependency
ia64_ret_from_syscall:
	cmp.ge p6,p7=r8,r0			// syscall executed successfully?
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp	// r2 = &pt_regs.r8
	adds r3=IA64_PT_REGS_R8_OFFSET+32,sp	// r3 = &pt_regs.r10
	;;
(p6)	st8.spill [r2]=r8	// store return value in slot for r8 and set unat bit
(p6)	st8.spill [r3]=r0	// clear error indication in slot for r10 and set unat bit
(p7)	br.cond.spnt.few handle_syscall_error	// handle potential syscall failure

ia64_leave_kernel:
	// check & deliver software interrupts:

#ifdef CONFIG_SMP
	adds	r2=IA64_TASK_PROCESSOR_OFFSET,r13
	movl	r3=softirq_state
	;;
	ld4	r2=[r2]
	;;
	shladd	r3=r2,3,r3
#else
	movl r3=softirq_state
#endif
	;;
	ld8 r2=[r3]		// r3 is guaranteed to be 8-byte aligned!
	;;
	shr r3=r2,32
	;;
	and r2=r2,r3
	;;
	cmp4.ne p6,p7=r2,r0
(p6)	br.call.spnt.many rp=invoke_do_softirq
1:
(pKern)	br.cond.dpnt.many restore_all	// yup -> skip check for rescheduling & signal delivery

	// call schedule() until we find a task that doesn't have need_resched set:

back_from_resched:
	{ .mii
	  adds r2=IA64_TASK_NEED_RESCHED_OFFSET,r13
	  mov r3=ip
	  adds r14=IA64_TASK_SIGPENDING_OFFSET,r13
	}
	;;
	ld8 r2=[r2]
	ld4 r14=[r14]
	mov rp=r3			// arrange for schedule() to return to back_from_resched
	;;
	/*
	 * If pEOI is set, we need to write the cr.eoi now and then
	 * clear pEOI because both invoke_schedule() and
	 * handle_signal_delivery() may call the scheduler.  Since
	 * we're returning to user-level, we get at most one nested
	 * interrupt of the same priority level, which doesn't tax the
	 * kernel stack too much.
	 */
(pEOI)	mov cr.eoi=r0
	cmp.ne p6,p0=r2,r0
	cmp.ne p2,p0=r14,r0		// NOTE: pKern is an alias for p2!!
(pEOI)	cmp.ne pEOI,p0=r0,r0		// clear pEOI before calling schedule()
	srlz.d
(p6)	br.call.spnt.many b6=invoke_schedule	// ignore return value
2:
	// check & deliver pending signals:
(p2)	br.call.spnt.few rp=handle_signal_delivery
restore_all:

	// start restoring the state saved on the kernel stack (struct pt_regs):

	adds r2=IA64_PT_REGS_R8_OFFSET+16,r12
	adds r3=IA64_PT_REGS_R8_OFFSET+24,r12
	;;
	ld8.fill r8=[r2],16
	ld8.fill r9=[r3],16
	;;
	ld8.fill r10=[r2],16
	ld8.fill r11=[r3],16
	;;
	ld8.fill r16=[r2],16
	ld8.fill r17=[r3],16
	;;
	ld8.fill r18=[r2],16
	ld8.fill r19=[r3],16
	;;
	ld8.fill r20=[r2],16
	ld8.fill r21=[r3],16
	;;
	ld8.fill r22=[r2],16
	ld8.fill r23=[r3],16
	;;
	ld8.fill r24=[r2],16
	ld8.fill r25=[r3],16
	;;
	ld8.fill r26=[r2],16
	ld8.fill r27=[r3],16
	;;
	ld8.fill r28=[r2],16
	ld8.fill r29=[r3],16
	;;
	ld8.fill r30=[r2],16
	ld8.fill r31=[r3],16
	;;
	ld8 r1=[r2],16		// ar.ccv
	ld8 r13=[r3],16		// ar.fpsr
	;;
	ld8 r14=[r2],16		// b0
	ld8 r15=[r3],16+8	// b7
	;;
	ldf.fill f6=[r2],32
	ldf.fill f7=[r3],32
	;;
	ldf.fill f8=[r2],32
	ldf.fill f9=[r3],32
	;;
	mov ar.ccv=r1
	mov ar.fpsr=r13
	mov b0=r14
	// turn off interrupts, interrupt collection, & data translation
	rsm psr.i | psr.ic | psr.dt
	;;
	srlz.i			// EAS 2.5
	mov b7=r15
	;;
	invala			// invalidate ALAT
	dep r12=0,r12,61,3	// convert sp to physical address
	bsw.0;;			// switch back to bank 0 (must be last in insn group)
	;;
#ifdef CONFIG_ITANIUM_ASTEP_SPECIFIC
	nop.i 0x0
	;;
	nop.i 0x0
	;;
	nop.i 0x0
	;;
#endif
	adds r16=16,r12
	adds r17=24,r12
	;;
	ld8 rCRIPSR=[r16],16	// load cr.ipsr
	ld8 rCRIIP=[r17],16	// load cr.iip
	;;
	ld8 rCRIFS=[r16],16	// load cr.ifs
	ld8 rARUNAT=[r17],16	// load ar.unat
	;;
	ld8 rARPFS=[r16],16	// load ar.pfs
	ld8 rARRSC=[r17],16	// load ar.rsc
	;;
	ld8 rARRNAT=[r16],16	// load ar.rnat (may be garbage)
	ld8 rARBSPSTORE=[r17],16	// load ar.bspstore (may be garbage)
	;;
	ld8 rARPR=[r16],16	// load predicates
	ld8 rB6=[r17],16	// load b6
	;;
	ld8 r18=[r16],16	// load ar.rsc value for "loadrs"
	ld8.fill r1=[r17],16	// load r1
	;;
	ld8.fill r2=[r16],16
	ld8.fill r3=[r17],16
	;;
	ld8.fill r12=[r16],16
	ld8.fill r13=[r17],16
	extr.u r19=rCRIPSR,32,2	// extract ps.cpl
	;;
	ld8.fill r14=[r16],16
	ld8.fill r15=[r17],16
	cmp.eq p6,p7=r0,r19	// are we returning to kernel mode? (psr.cpl==0)
	;;
	mov b6=rB6
	mov ar.pfs=rARPFS
(p6)	br.cond.dpnt.few skip_rbs_switch

	/*
	 * Restore user backing store.
	 *
	 * NOTE: alloc, loadrs, and cover can't be predicated.
	 *
	 * XXX This needs some scheduling/tuning once we believe it
	 *     really does work as intended.
	 */
	mov r16=ar.bsp			// get existing backing store pointer
(pNonSys) br.cond.dpnt.few dont_preserve_current_frame
	cover				// add current frame into dirty partition
	;;
	mov rCRIFS=cr.ifs		// fetch the cr.ifs value that "cover" produced
	mov r17=ar.bsp			// get new backing store pointer
	;;
	sub r16=r17,r16			// calculate number of bytes that were added to rbs
	;;
	shl r16=r16,16			// shift additional frame size into position for loadrs
	;;
	add r18=r16,r18			// adjust the loadrs value
	;;
#ifdef CONFIG_IA64_SOFTSDV_HACKS
	// Reset ITM if we've missed a timer tick.  Workaround for SoftSDV bug
	mov r16 = r2
	mov r2 = ar.itc
	mov r17 = cr.itm
	;; 
	cmp.gt p6,p7 = r2, r17
(p6)	addl r17 = 100, r2
	;;
	mov cr.itm = r17
	mov r2 = r16
#endif
dont_preserve_current_frame:
	alloc r16=ar.pfs,0,0,0,0	// drop the current call frame (noop for syscalls)
	;;
	mov ar.rsc=r18			// load ar.rsc to be used for "loadrs"
#ifdef CONFIG_IA32_SUPPORT
	tbit.nz p6,p0=rCRIPSR,IA64_PSR_IS_BIT
	;;
(p6)	mov ar.rsc=r0                   // returning to IA32 mode
#endif
 	;;
	loadrs
	;;
	mov ar.bspstore=rARBSPSTORE
	;;
	mov ar.rnat=rARRNAT	// must happen with RSE in lazy mode

skip_rbs_switch:
	mov ar.rsc=rARRSC
	mov ar.unat=rARUNAT
	mov cr.ifs=rCRIFS	// restore cr.ifs only if not a (synchronous) syscall
(pEOI)	mov cr.eoi=r0
	mov pr=rARPR,-1
	mov cr.iip=rCRIIP
	mov cr.ipsr=rCRIPSR
	;;
	rfi;;			// must be last instruction in an insn group

handle_syscall_error:
	/*
	 * Some system calls (e.g., ptrace, mmap) can return arbitrary
	 * values which could lead us to mistake a negative return
	 * value as a failed syscall.  Those syscall must deposit
	 * a non-zero value in pt_regs.r8 to indicate an error.
	 * If pt_regs.r8 is zero, we assume that the call completed
	 * successfully.
	 */
	ld8 r3=[r2]		// load pt_regs.r8
	sub r9=0,r8		// negate return value to get errno
	;;
	mov r10=-1		// return -1 in pt_regs.r10 to indicate error
	cmp.eq p6,p7=r3,r0	// is pt_regs.r8==0?
	adds r3=16,r2		// r3=&pt_regs.r10
	;;
(p6)	mov r9=r8
(p6)	mov r10=0
	;;
	st8.spill [r2]=r9	// store errno in pt_regs.r8 and set unat bit
	st8.spill [r3]=r10	// store error indication in pt_regs.r10 and set unat bit
	br.cond.sptk.many ia64_leave_kernel
	.endp __ret_from_syscall

#ifdef CONFIG_SMP
	/*
	 * Invoke schedule_tail(task) while preserving in0-in7, which may be needed
	 * in case a system call gets restarted.
	 */
	.proc invoke_schedule_tail
invoke_schedule_tail:
	alloc loc0=ar.pfs,8,2,1,0
	mov loc1=rp
	mov out0=r8				// Address of previous task
	;;
	br.call.sptk.few rp=schedule_tail
.ret8:
	mov ar.pfs=loc0
	mov rp=loc1
	br.ret.sptk.many rp
	.endp invoke_schedule_tail
#endif /* CONFIG_SMP */

	/*
	 * Invoke do_softirq() while preserving in0-in7, which may be needed
	 * in case a system call gets restarted.
	 */
	.proc invoke_do_softirq
invoke_do_softirq:
	alloc loc0=ar.pfs,8,2,0,0
	mov loc1=rp
	;;
	br.call.sptk.few rp=do_softirq
.ret9:
	mov ar.pfs=loc0
	mov rp=loc1
	br.ret.sptk.many rp
	.endp invoke_do_softirq

	/*
	 * Invoke schedule() while preserving in0-in7, which may be needed
	 * in case a system call gets restarted.
	 */
	.proc invoke_schedule
invoke_schedule:
	alloc loc0=ar.pfs,8,2,0,0
	mov loc1=rp
	;;
	br.call.sptk.few rp=schedule
.ret10:
	mov ar.pfs=loc0
	mov rp=loc1
	br.ret.sptk.many rp
	.endp invoke_schedule

	//
	// Setup stack and call ia64_do_signal.  Note that pSys and pNonSys need to
	// be set up by the caller.  We declare 8 input registers so the system call
	// args get preserved, in case we need to restart a system call.
	//
	.align 16
	.proc handle_signal_delivery
handle_signal_delivery:
	alloc loc0=ar.pfs,8,2,3,0 // preserve all eight input regs in case of syscall restart!
	mov r9=ar.unat

	// If the process is being ptraced, the signal may not actually be delivered to
	// the process.  Instead, SIGCHLD will be sent to the parent.  We need to
	// setup a switch_stack so ptrace can inspect the processes state if necessary.
	adds r2=IA64_TASK_FLAGS_OFFSET,r13
	;;
	ld8 r2=[r2]
	mov out0=0				// there is no "oldset"
	adds out1=16,sp				// out1=&pt_regs
	;;
(pSys)	mov out2=1				// out2==1 => we're in a syscall
	tbit.nz p16,p17=r2,PF_PTRACED_BIT
(p16)	br.cond.spnt.many setup_switch_stack
	;;
back_from_setup_switch_stack:
(pNonSys) mov out2=0				// out2==0 => not a syscall
	adds r3=-IA64_SWITCH_STACK_SIZE+IA64_SWITCH_STACK_CALLER_UNAT_OFFSET+16,sp
(p17)	adds sp=-IA64_SWITCH_STACK_SIZE,sp	// make space for (dummy) switch_stack
	;;
(p17)	st8 [r3]=r9				// save ar.unat in sw->caller_unat
	mov loc1=rp				// save return address
	br.call.sptk.few rp=ia64_do_signal
.ret11:
	adds r3=IA64_SWITCH_STACK_CALLER_UNAT_OFFSET+16,sp
	;;
	ld8 r9=[r3]				// load new unat from sw->caller_unat
	mov rp=loc1
	;;
(p17)	adds sp=IA64_SWITCH_STACK_SIZE,sp	// drop (dummy) switch_stack
(p17)	mov ar.unat=r9
(p17)	mov ar.pfs=loc0
(p17)	br.ret.sptk.many rp

	// restore the switch stack (ptrace may have modified it):
	movl r28=1f
	br.cond.sptk.many load_switch_stack
1:	br.ret.sptk.many rp
	// NOT REACHED

setup_switch_stack:
	movl r28=back_from_setup_switch_stack
	mov r16=loc0
	br.cond.sptk.many save_switch_stack
	// NOT REACHED

	.endp handle_signal_delivery

	.align 16
	.proc sys_rt_sigsuspend
	.global sys_rt_sigsuspend
sys_rt_sigsuspend:
	alloc loc0=ar.pfs,2,2,3,0
	mov r9=ar.unat

	// If the process is being ptraced, the signal may not actually be delivered to
	// the process.  Instead, SIGCHLD will be sent to the parent.  We need to
	// setup a switch_stack so ptrace can inspect the processes state if necessary.
	adds r2=IA64_TASK_FLAGS_OFFSET,r13
	;;
	ld8 r2=[r2]
	mov out0=in0				// mask
	mov out1=in1				// sigsetsize
	;;
	adds out2=16,sp				// out1=&pt_regs
	tbit.nz p16,p17=r2,PF_PTRACED_BIT
(p16)	br.cond.spnt.many sigsuspend_setup_switch_stack
	;;
back_from_sigsuspend_setup_switch_stack:
	adds r3=-IA64_SWITCH_STACK_SIZE+IA64_SWITCH_STACK_CALLER_UNAT_OFFSET+16,sp
(p17)	adds sp=-IA64_SWITCH_STACK_SIZE,sp	// make space for (dummy) switch_stack
	;;
(p17)	st8 [r3]=r9				// save ar.unat in sw->caller_unat
	mov loc1=rp				// save return address
	br.call.sptk.many rp=ia64_rt_sigsuspend
.ret12:
	adds r3=IA64_SWITCH_STACK_CALLER_UNAT_OFFSET+16,sp
	;;
	ld8 r9=[r3]				// load new unat from sw->caller_unat
	mov rp=loc1
	;;
(p17)	adds sp=IA64_SWITCH_STACK_SIZE,sp	// drop (dummy) switch_stack
(p17)	mov ar.unat=r9
(p17)	mov ar.pfs=loc0
(p17)	br.ret.sptk.many rp

	// restore the switch stack (ptrace may have modified it):
	movl r28=1f
	br.cond.sptk.many load_switch_stack
1:	br.ret.sptk.many rp
	// NOT REACHED

sigsuspend_setup_switch_stack:
	movl r28=back_from_sigsuspend_setup_switch_stack
	mov r16=loc0
	br.cond.sptk.many save_switch_stack
	// NOT REACHED

	.endp sys_rt_sigsuspend

	.align 16
	.proc sys_rt_sigreturn
sys_rt_sigreturn:
	alloc loc0=ar.pfs,8,1,1,0 // preserve all eight input regs in case of syscall restart!
	adds out0=16,sp				// out0 = &pt_regs
	;;
	adds sp=-IA64_SWITCH_STACK_SIZE,sp	// make space for unat and padding
	br.call.sptk.few rp=ia64_rt_sigreturn
.ret13:
	adds r3=IA64_SWITCH_STACK_CALLER_UNAT_OFFSET+16,sp
	;;
	ld8 r9=[r3]			// load new ar.unat
	mov rp=r8
	;;
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// drop (dummy) switch-stack frame
	mov ar.unat=r9
	mov ar.pfs=loc0
	br.ret.sptk.many rp
	.endp sys_rt_sigreturn

	.align 16
	.global ia64_prepare_handle_unaligned
	.proc ia64_prepare_handle_unaligned
ia64_prepare_handle_unaligned:
	movl r28=1f
	//
	// r16 = fake ar.pfs, we simply need to make sure 
	// privilege is still 0
	//
	mov r16=r0 				
	br.cond.sptk.few save_switch_stack
1: 	br.call.sptk.few rp=ia64_handle_unaligned // stack frame setup in ivt
.ret14:
	movl r28=2f
	br.cond.sptk.many load_switch_stack
2:	br.cond.sptk.many rp			  // goes to ia64_leave_kernel
	.endp ia64_prepare_handle_unaligned

#ifdef CONFIG_KDB
	//
	// This gets called from ivt.S with:
	//	SAVE MIN with cover done
	//	SAVE REST done
	//	no parameters
	//	r15 has return value = ia64_leave_kernel
	//
	.align 16
	.global ia64_invoke_kdb
	.proc ia64_invoke_kdb
ia64_invoke_kdb:
	alloc r16=ar.pfs,0,0,4,0
	movl r28=1f				// save_switch_stack protocol
	;;			// avoid WAW on CFM
	br.cond.sptk.many save_switch_stack	// to flushrs
1:	mov out0=4				// kdb entry reason
	mov out1=0				// err number
	adds out2=IA64_SWITCH_STACK_SIZE+16,sp	// pt_regs
	add out3=16,sp				// switch_stack
	br.call.sptk.few rp=kdb
.ret15:
	movl r28=1f				// load_switch_stack proto
	br.cond.sptk.many load_switch_stack
1:	br.ret.sptk.many rp
	.endp ia64_invoke_kdb

	//
	// When KDB is compiled in, we intercept each fault and give
	// kdb a chance to run before calling the normal fault handler.
	//
        .align 16
        .global ia64_invoke_kdb_fault_handler
        .proc ia64_invoke_kdb_fault_handler
ia64_invoke_kdb_fault_handler:
        alloc r16=ar.pfs,5,1,5,0
        movl r28=1f
	mov loc0=rp			// save this
	br.cond.sptk.many save_switch_stack // to flushrs
	;;			// avoid WAW on CFM
1:	mov out0=in0			// vector number
	mov out1=in1			// cr.isr
	mov out2=in2			// cr.ifa
	mov out3=in3			// cr.iim
	mov out4=in4			// cr.itir
	br.call.sptk.few rp=ia64_kdb_fault_handler
.ret16:

	movl r28=1f
	br.cond.sptk.many load_switch_stack
1:	cmp.ne p6,p0=r8,r0		// did ia64_kdb_fault_handler return 0?
	mov rp=loc0
(p6)	br.ret.spnt.many rp		// no, we're done
	;;		// avoid WAW on rp
	mov out0=in0			// vector number
	mov out1=in1			// cr.isr
	mov out2=in2			// cr.ifa
	mov out3=in3			// cr.iim
	mov out4=in4			// cr.itir
	mov in0=ar.pfs			// preserve ar.pfs returned by load_switch_stack
	br.call.sptk.few rp=ia64_fault	// yup -> we need to invoke normal fault handler now
.ret17:
	mov ar.pfs=in0
	mov rp=loc0
	br.ret.sptk.many rp

	.endp ia64_invoke_kdb_fault_handler

#endif /* CONFIG_KDB */

	.rodata
	.align 8
	.globl sys_call_table
sys_call_table:
	data8 sys_ni_syscall		//  This must be sys_ni_syscall!  See ivt.S.
	data8 sys_exit				// 1025
	data8 sys_read
	data8 sys_write
	data8 sys_open
	data8 sys_close
	data8 sys_creat				// 1030
	data8 sys_link
	data8 sys_unlink
	data8 ia64_execve
	data8 sys_chdir
	data8 sys_fchdir			// 1035
	data8 sys_utimes
	data8 sys_mknod
	data8 sys_chmod
	data8 sys_chown
	data8 sys_lseek				// 1040
	data8 sys_getpid
	data8 sys_getppid
	data8 sys_mount
	data8 sys_umount
	data8 sys_setuid			// 1045
	data8 sys_getuid
	data8 sys_geteuid
	data8 sys_ptrace
	data8 sys_access
	data8 sys_sync				// 1050
	data8 sys_fsync
	data8 sys_fdatasync
	data8 sys_kill
	data8 sys_rename
	data8 sys_mkdir				// 1055
	data8 sys_rmdir
	data8 sys_dup
	data8 sys_pipe
	data8 sys_times
	data8 ia64_brk				// 1060
	data8 sys_setgid
	data8 sys_getgid
	data8 sys_getegid
	data8 sys_acct
	data8 sys_ioctl				// 1065
	data8 sys_fcntl
	data8 sys_umask
	data8 sys_chroot
	data8 sys_ustat
	data8 sys_dup2				// 1070
	data8 sys_setreuid
	data8 sys_setregid
	data8 sys_getresuid
	data8 sys_setresuid
	data8 sys_getresgid			// 1075
	data8 sys_setresgid
	data8 sys_getgroups
	data8 sys_setgroups
	data8 sys_getpgid
	data8 sys_setpgid			// 1080
	data8 sys_setsid
	data8 sys_getsid
	data8 sys_sethostname
	data8 sys_setrlimit
	data8 sys_getrlimit			// 1085
	data8 sys_getrusage
	data8 sys_gettimeofday
	data8 sys_settimeofday
	data8 sys_select
	data8 sys_poll				// 1090
	data8 sys_symlink
	data8 sys_readlink
	data8 sys_uselib
	data8 sys_swapon
	data8 sys_swapoff			// 1095
	data8 sys_reboot
	data8 sys_truncate
	data8 sys_ftruncate
	data8 sys_fchmod
	data8 sys_fchown			// 1100
	data8 ia64_getpriority
	data8 sys_setpriority
	data8 sys_statfs
	data8 sys_fstatfs
	data8 sys_ioperm			// 1105
	data8 sys_semget
	data8 sys_semop
	data8 sys_semctl
	data8 sys_msgget
	data8 sys_msgsnd			// 1110
	data8 sys_msgrcv
	data8 sys_msgctl
	data8 sys_shmget
	data8 ia64_shmat
	data8 sys_shmdt				// 1115
	data8 sys_shmctl
	data8 sys_syslog
	data8 sys_setitimer
	data8 sys_getitimer
	data8 sys_newstat			// 1120
	data8 sys_newlstat
	data8 sys_newfstat
	data8 sys_vhangup
	data8 sys_lchown
	data8 sys_vm86				// 1125
	data8 sys_wait4
	data8 sys_sysinfo
	data8 sys_clone
	data8 sys_setdomainname
	data8 sys_newuname			// 1130
	data8 sys_adjtimex
	data8 sys_create_module
	data8 sys_init_module
	data8 sys_delete_module
	data8 sys_get_kernel_syms		// 1135
	data8 sys_query_module
	data8 sys_quotactl
	data8 sys_bdflush
	data8 sys_sysfs
	data8 sys_personality			// 1140
	data8 ia64_ni_syscall		// sys_afs_syscall
	data8 sys_setfsuid
	data8 sys_setfsgid
	data8 sys_getdents
	data8 sys_flock				// 1145
	data8 sys_readv
	data8 sys_writev
	data8 sys_pread
	data8 sys_pwrite
	data8 sys_sysctl			// 1150
	data8 sys_mmap
	data8 sys_munmap
	data8 sys_mlock
	data8 sys_mlockall
	data8 sys_mprotect			// 1155
	data8 sys_mremap
	data8 sys_msync
	data8 sys_munlock
	data8 sys_munlockall
	data8 sys_sched_getparam		// 1160
	data8 sys_sched_setparam
	data8 sys_sched_getscheduler
	data8 sys_sched_setscheduler
	data8 sys_sched_yield
	data8 sys_sched_get_priority_max	// 1165
	data8 sys_sched_get_priority_min
	data8 sys_sched_rr_get_interval
	data8 sys_nanosleep
	data8 sys_nfsservctl
	data8 sys_prctl				// 1170
	data8 sys_getpagesize
	data8 sys_mmap2
	data8 sys_pciconfig_read
	data8 sys_pciconfig_write
	data8 sys_perfmonctl			// 1175
	data8 sys_sigaltstack
	data8 sys_rt_sigaction
	data8 sys_rt_sigpending
	data8 sys_rt_sigprocmask
	data8 sys_rt_sigqueueinfo		// 1180
	data8 sys_rt_sigreturn
	data8 sys_rt_sigsuspend
	data8 sys_rt_sigtimedwait
	data8 sys_getcwd
	data8 sys_capget			// 1185
	data8 sys_capset
	data8 sys_sendfile
	data8 sys_ni_syscall		// sys_getpmsg (STREAMS)
	data8 sys_ni_syscall		// sys_putpmsg (STREAMS)
	data8 sys_socket			// 1190
	data8 sys_bind
	data8 sys_connect
	data8 sys_listen
	data8 sys_accept
	data8 sys_getsockname			// 1195
	data8 sys_getpeername
	data8 sys_socketpair 
	data8 sys_send
	data8 sys_sendto
	data8 sys_recv				// 1200
	data8 sys_recvfrom
	data8 sys_shutdown
	data8 sys_setsockopt
	data8 sys_getsockopt
	data8 sys_sendmsg			// 1205
	data8 sys_recvmsg
	data8 sys_pivot_root
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1210
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1215
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1220
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1225
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1230
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1235
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1240
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1245
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1250
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1255
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1260
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1265
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1270
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1275
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall

