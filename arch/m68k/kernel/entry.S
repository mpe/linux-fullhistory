/* -*- mode: asm -*-
 *
 *  linux/arch/m68k/kernel/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 * 680x0 support by Hamish Macdonald
 *
 */

/*
 * entry.S  contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * Stack layout in 'ret_from_exception':
 *
 *	This allows access to the syscall arguments in registers d1-d5
 *
 *	 0(sp) - d1
 *	 4(sp) - d2
 *	 8(sp) - d3
 *	 C(sp) - d4
 *	10(sp) - d5
 *	14(sp) - a0
 *	18(sp) - a1
 *	1C(sp) - d0
 *	20(sp) - orig_d0
 *	24(sp) - stack adjustment
 *	28(sp) - sr
 *	2A(sp) - pc
 *	2E(sp) - format & vector
 */

/*
 * 12/03/96 Jes: Currently we only support m68k single-cpu systems, so
 *               all pointers that used to be 'current' are now entry
 *               number 0 in the 'current_set' list.
 */

#include <linux/sys.h>

#include <asm/segment.h>

LCF_MASK	= 0x0001

LENOSYS = 38

/*
 * these are offsets into the task-struct
 */
LTASK_STATE	=  0
LTASK_COUNTER	=  4
LTASK_PRIORITY	=  8
LTASK_SIGNAL	= 12
LTASK_BLOCKED	= 16
LTASK_FLAGS	= 20
LTASK_ERRNO	= 24

#include <linux/config.h>
#include <linux/linkage.h>

/* the following macro is used when enabling interrupts */
#if defined(CONFIG_ATARI) && !defined(CONFIG_AMIGA) && !defined(CONFIG_MAC)
	/* block out HSYNC on the atari */
#define ALLOWINT 0xfbff
#else
	/* portable version */
#define ALLOWINT 0xf8ff
#endif /* machine compilation types */ 

LD0		= 0x1C
LORIG_D0	= 0x20
LSR		= 0x28
LFORMATVEC	= 0x2E

/*
 * This defines the normal kernel pt-regs layout.
 *
 * regs are a2-a6 and d6-d7 preserved by C code
 * the kernel doesn't mess with usp unless it needs to
 */
#define SAVE_ALL			\
	clrl	%sp@-;    /* stk_adj */	\
	movel	%d0,%sp@-; /* orig d0 */	\
	movel	%d0,%sp@-; /* d0 */	\
	moveml	%d1-%d5/%a0-%a1,%sp@-

#define RESTORE_ALL			\
	moveml	%sp@+,%a0-%a1/%d1-%d5;	\
	movel	%sp@+,%d0;		\
	addql	#4,%sp;	 /* orig d0 */	\
	addl	%sp@+,%sp; /* stk adj */	\
	rte

#define SWITCH_STACK_SIZE (7*4+4)	/* includes return address */

#define SAVE_SWITCH_STACK \
	moveml	%a2-%a6/%d6-%d7,%sp@-

#define RESTORE_SWITCH_STACK \
	moveml	%sp@+,%a2-%a6/%d6-%d7

.globl SYMBOL_NAME(system_call), SYMBOL_NAME(buserr), SYMBOL_NAME(trap)
.globl SYMBOL_NAME(resume), SYMBOL_NAME(ret_from_exception)
.globl SYMBOL_NAME(ret_from_signal)
.globl SYMBOL_NAME(inthandler), SYMBOL_NAME(sys_call_table)
.globl SYMBOL_NAME(sys_fork), SYMBOL_NAME(sys_clone)
.globl SYMBOL_NAME(ret_from_interrupt), SYMBOL_NAME(bad_interrupt)

.text
ENTRY(buserr)
	SAVE_ALL
	moveq	#-1,%d0
	movel	%d0,%sp@(LORIG_D0)	| a -1 in the ORIG_D0 field
					| signifies that the stack frame
					| is NOT for syscall

	movel	%sp,%sp@- 		| stack frame pointer argument
	bsrl	SYMBOL_NAME(buserr_c)
	addql	#4,%sp
	jra	SYMBOL_NAME(ret_from_exception)

ENTRY(trap)
	SAVE_ALL
	moveq	#-1,%d0
	movel	%d0,%sp@(LORIG_D0)	| a -1 in the ORIG_D0 field
					| signifies that the stack frame
					| is NOT for syscall
	movel	%sp,%sp@- 		| stack frame pointer argument
	bsrl	SYMBOL_NAME(trap_c)
	addql	#4,%sp
	jra	SYMBOL_NAME(ret_from_exception)

ENTRY(reschedule)
	| save top of frame
	pea	%sp@
	jbsr	SYMBOL_NAME(set_esp0)
	addql	#4,%sp

	pea	SYMBOL_NAME(ret_from_exception)
	jmp	SYMBOL_NAME(schedule)

ENTRY(system_call)
	SAVE_ALL
	movel	#-LENOSYS,LD0(%sp)	| default return value in d0
					| original D0 is in orig_d0
	movel	%d0,%d2

	| save top of frame
	pea	%sp@
	jbsr	SYMBOL_NAME(set_esp0)
	addql	#4,%sp

	cmpl	#NR_syscalls,%d2
	jcc	SYMBOL_NAME(ret_from_exception)
	lea	SYMBOL_NAME(sys_call_table),%a0
	movel	%a0@(%d2:l:4),%d3
	jeq	SYMBOL_NAME(ret_from_exception)
	andw	#~LCF_MASK,%sp@(LSR)	| assume syscall success
	movel	SYMBOL_NAME(current_set),%a0
	clrl	%a0@(LTASK_ERRNO)
	btst	#5,%a0@(LTASK_FLAGS+3)	| PF_TRACESYS
	bnes	1f
	movel	%d3,%a0
	jbsr	%a0@
	movel	%d0,%sp@(LD0)		| save the return value
	jpl	2f
	orw	#LCF_MASK,%sp@(LSR)	| set carry to indicate error
2:
	movel	SYMBOL_NAME(current_set),%a0
	movel	%a0@(LTASK_ERRNO),%d1
	negl	%d1
	jeq	SYMBOL_NAME(ret_from_exception)
	movel	%d1,%sp@(LD0)
	orw	#LCF_MASK,%sp@(LSR)	| set carry to indicate error
	jra	SYMBOL_NAME(ret_from_exception)
1:
	subql	#4,%sp
	SAVE_SWITCH_STACK
	jbsr	SYMBOL_NAME(syscall_trace)
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	movel	%d3,%a0
	jbsr	%a0@
	movel	%d0,%sp@(LD0)		| save the return value
	jpl	2f
	orw	#LCF_MASK,%sp@(LSR)	| set carry to indicate error
2:
	movel	SYMBOL_NAME(current_set),%a0
	movel	%a0@(LTASK_ERRNO),%d1
	negl	%d1
	jeq	2f
	movel	%d1,%sp@(LD0)
	orw	#LCF_MASK,%sp@(LSR)	| set carry to indicate error
2:	subql	#4,%sp			| dummy return address
	SAVE_SWITCH_STACK
	jbsr	SYMBOL_NAME(syscall_trace)

SYMBOL_NAME_LABEL(ret_from_signal)
	RESTORE_SWITCH_STACK
	addql	#4,%sp

SYMBOL_NAME_LABEL(ret_from_exception)
	btst	#5,%sp@(LSR)		| check if returning to kernel
	bnes	2f			| if so, skip resched, signals
	tstl	SYMBOL_NAME(need_resched)
	jne	SYMBOL_NAME(reschedule)
	movel	SYMBOL_NAME(current_set),%a0
	cmpl	#SYMBOL_NAME(task),%a0	| task[0] cannot have signals
	jeq	2f
	bclr	#5,%a0@(LTASK_FLAGS+1)	| check for delayed trace
	jeq	1f
	bclr	#7,%sp@(LSR)		| clear trace bit in SR
	pea	1			| send SIGTRAP
	movel	%a0,%sp@-
	pea	5
	jbsr	SYMBOL_NAME(send_sig)
	addql	#8,%sp
	addql	#4,%sp

1:
	moveq	#0,%d0
	movel	SYMBOL_NAME(current_set),%a0
	cmpl	%a0@(LTASK_STATE),%d0     | state
	jne	SYMBOL_NAME(reschedule)
	cmpl	%a0@(LTASK_COUNTER),%d0   | counter
	jeq	SYMBOL_NAME(reschedule)

	movel	%a0@(LTASK_BLOCKED),%d0
	movel	%d0,%d1			| save blocked in d1 for sig handling
	notl	%d0
	andl	%a0@(LTASK_SIGNAL),%d0
	jne	Lsignal_return
2:	RESTORE_ALL

Lsignal_return:
	subql	#4,%sp			| dummy return address
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	movel	%d1,%sp@-
	bsrl	SYMBOL_NAME(do_signal)
	addql	#8,%sp
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	RESTORE_ALL

/*
** This is the main interrupt handler, responsible for calling process_int()
*/
SYMBOL_NAME_LABEL(inthandler)
	SAVE_ALL
	moveq	#-1,%d0
	movel	%d0,%sp@(LORIG_D0)	| a -1 in the ORIG_D0 field
					| signifies that the stack frame
					| is NOT for syscall
	
	addql	#1,SYMBOL_NAME(intr_count)

	movew	%sp@(LFORMATVEC),%d0      |  put exception # in d0
	andil	#0xfff,%d0		|  mask out format nybble

	movel	%sp,%sp@-		
	movel	%d0,%sp@- 		|  put vector # on stack
	jbsr	SYMBOL_NAME(process_int)|  process the IRQ
	addql	#8,%sp			|  pop parameters off stack

SYMBOL_NAME_LABEL(ret_from_interrupt)
	/* check if we need to do software interrupts */
1:
	movel	SYMBOL_NAME(intr_count),%d2
	subql	#1,%d2
	jne	2f

	movel	SYMBOL_NAME(bh_active),%d0
	andl	SYMBOL_NAME(bh_mask),%d0
	jne	3f

	movel	%d2,SYMBOL_NAME(intr_count)

	jra	SYMBOL_NAME(ret_from_exception)
					| deliver signals, reschedule etc..

2:	movel	%d2,SYMBOL_NAME(intr_count)
	RESTORE_ALL
3:
	movew	%sr,%sp@-
	andiw	#(ALLOWINT),%sr		| allow interrupts
	jbsr	SYMBOL_NAME(do_bottom_half)
	movew	%sp@+,%sr
	jra	1b


/* Handler for uninitialized and spurious interrupts */

SYMBOL_NAME_LABEL(bad_interrupt)
	addql	#1,SYMBOL_NAME(num_spurious)
	rte

ENTRY(sys_fork)
	SAVE_SWITCH_STACK	
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	SYMBOL_NAME(m68k_fork)
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_clone)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	SYMBOL_NAME(m68k_clone)
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_sigsuspend)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	SYMBOL_NAME(do_sigsuspend)
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_sigreturn)
	SAVE_SWITCH_STACK
	jbsr	SYMBOL_NAME(do_sigreturn)
	RESTORE_SWITCH_STACK
	rts

LFLUSH_I_AND_D = 0x00000808
LBI_CPU = 4
LTSS_KSP	= 0
LTSS_USP	= 4
LTSS_SR		= 8
LTSS_FS		= 10
LTSS_CRP	= 20
LTSS_FPCTXT	= 32

SYMBOL_NAME_LABEL(resume)
	/*
	 * Beware - when entering resume, offset of tss is in a1 and
	 * next (the new task) is in d1, so don't chance these
	 * registers before their contents have been used.
	 */
	
	/* current tasks task_struct */
 	movel	SYMBOL_NAME(current_set),%a0

	/* offset of tss struct (processor state) from beginning
	   of task struct */
	addl	%a1,%a0

	/* save sr */
	movew	%sr,%a0@(LTSS_SR)

	/* disable interrupts */
	oriw	#0x0700,%sr

	/* save fs (sfc,%dfc) (may be pointing to kernel memory) */
	movec	%sfc,%d0
	movew	%d0,%a0@(LTSS_FS)

	/* save usp */
	/* it is better to use a movel here instead of a movew 8*) */
	movec	%usp,%d0
	movel	%d0,%a0@(LTSS_USP)

	/* load new task (before adjusting stack) */
	/* The task has already been put in d1 by switch_to (Jes) */
/*
	movel	%sp@(4),%d1
*/
	/* save non-scratch registers on stack */
	SAVE_SWITCH_STACK

	/* save current kernel stack pointer */
	movel	%sp,%a0@(LTSS_KSP)

	/* save floating point context */
	fsave	%a0@(LTSS_FPCTXT+27*4)
	tstb	%a0@(LTSS_FPCTXT+27*4)
	jeq	1f
	fmovemx	%fp0-%fp7,%a0@(LTSS_FPCTXT)
	fmoveml	%fpcr/%fpsr/%fpiar,%a0@(LTSS_FPCTXT+24*4)
1:

	/* get pointer to tss struct (d1 contains new task) */
	movel	%d1,SYMBOL_NAME(current_set)
	movel	%d1,%a0
	addl	%a1,%a0

	/* 68040 or 68060 ? */
	btst	#2,SYMBOL_NAME(boot_info)+LBI_CPU+3
	bnes	1f
	btst	#3,SYMBOL_NAME(boot_info)+LBI_CPU+3
	bnes	1f

	/*
	 * switch address space
	 */

	/* flush MC68030/MC68020 caches (they are virtually addressed) */
	movec	%cacr,%d0
	oril	#LFLUSH_I_AND_D,%d0
	movec	%d0,%cacr

	/* switch the root pointer */
	pmove	%a0@(LTSS_CRP),%crp

	/* flush address translation cache (probably not needed */
	pflusha

	jra	2f	/* skip m68040 stuff */

1:
	/*
	 * switch address space
	 */

	/* flush address translation cache (user entries) */
	.word	0xf510			/* pflushan */

	/* switch the root pointer */
	movel	%a0@(LTSS_CRP+4),%d0
	.long	0x4e7b0806		/* movec d0,urp */

	/* is it a '060 ? */
	btst	#3,SYMBOL_NAME(boot_info)+LBI_CPU+3
	beqs	2f
	/* clear user entries in the branch cache */
	movec	%cacr,%d0
	orl	#0x00200000,%d0
	movec	%d0,%cacr

2:
	/* restore floating point context */
	tstb	%a0@(LTSS_FPCTXT+27*4)
	jeq	1f
	fmovemx	%a0@(LTSS_FPCTXT),%fp0-%fp7
	fmoveml	%a0@(LTSS_FPCTXT+24*4),%fpcr/%fpsr/%fpiar
1:	frestore %a0@(LTSS_FPCTXT+27*4)

	/* restore the kernel stack pointer */
	movel	%a0@(LTSS_KSP),%sp

	/* restore non-scratch registers */
	RESTORE_SWITCH_STACK

	/* restore user stack pointer */
	movel	%a0@(LTSS_USP),%d0
	movec	%d0,%usp

	/* restore fs (sfc,%dfc) */
	movew	%a0@(LTSS_FS),%a1
	movec	%a1,%sfc
	movec	%a1,%dfc

	/* restore status register */
	movew	%a0@(LTSS_SR),%sr

	rts

.data
ALIGN
SYMBOL_NAME_LABEL(sys_call_table)
	.long SYMBOL_NAME(sys_setup)		/* 0 */
	.long SYMBOL_NAME(sys_exit)
	.long SYMBOL_NAME(sys_fork)
	.long SYMBOL_NAME(sys_read)
	.long SYMBOL_NAME(sys_write)
	.long SYMBOL_NAME(sys_open)		/* 5 */
	.long SYMBOL_NAME(sys_close)
	.long SYMBOL_NAME(sys_waitpid)
	.long SYMBOL_NAME(sys_creat)
	.long SYMBOL_NAME(sys_link)
	.long SYMBOL_NAME(sys_unlink)		/* 10 */
	.long SYMBOL_NAME(sys_execve)
	.long SYMBOL_NAME(sys_chdir)
	.long SYMBOL_NAME(sys_time)
	.long SYMBOL_NAME(sys_mknod)
	.long SYMBOL_NAME(sys_chmod)		/* 15 */
	.long SYMBOL_NAME(sys_chown)
	.long SYMBOL_NAME(sys_break)
	.long SYMBOL_NAME(sys_stat)
	.long SYMBOL_NAME(sys_lseek)
	.long SYMBOL_NAME(sys_getpid)		/* 20 */
	.long SYMBOL_NAME(sys_mount)
	.long SYMBOL_NAME(sys_umount)
	.long SYMBOL_NAME(sys_setuid)
	.long SYMBOL_NAME(sys_getuid)
	.long SYMBOL_NAME(sys_stime)		/* 25 */
	.long SYMBOL_NAME(sys_ptrace)
	.long SYMBOL_NAME(sys_alarm)
	.long SYMBOL_NAME(sys_fstat)
	.long SYMBOL_NAME(sys_pause)
	.long SYMBOL_NAME(sys_utime)		/* 30 */
	.long SYMBOL_NAME(sys_stty)
	.long SYMBOL_NAME(sys_gtty)
	.long SYMBOL_NAME(sys_access)
	.long SYMBOL_NAME(sys_nice)
	.long SYMBOL_NAME(sys_ftime)		/* 35 */
	.long SYMBOL_NAME(sys_sync)
	.long SYMBOL_NAME(sys_kill)
	.long SYMBOL_NAME(sys_rename)
	.long SYMBOL_NAME(sys_mkdir)
	.long SYMBOL_NAME(sys_rmdir)		/* 40 */
	.long SYMBOL_NAME(sys_dup)
	.long SYMBOL_NAME(sys_pipe)
	.long SYMBOL_NAME(sys_times)
	.long SYMBOL_NAME(sys_prof)
	.long SYMBOL_NAME(sys_brk)		/* 45 */
	.long SYMBOL_NAME(sys_setgid)
	.long SYMBOL_NAME(sys_getgid)
	.long SYMBOL_NAME(sys_signal)
	.long SYMBOL_NAME(sys_geteuid)
	.long SYMBOL_NAME(sys_getegid)		/* 50 */
	.long SYMBOL_NAME(sys_acct)
	.long SYMBOL_NAME(sys_phys)
	.long SYMBOL_NAME(sys_lock)
	.long SYMBOL_NAME(sys_ioctl)
	.long SYMBOL_NAME(sys_fcntl)		/* 55 */
	.long SYMBOL_NAME(sys_mpx)
	.long SYMBOL_NAME(sys_setpgid)
	.long SYMBOL_NAME(sys_ulimit)
	.long SYMBOL_NAME(sys_olduname)
	.long SYMBOL_NAME(sys_umask)		/* 60 */
	.long SYMBOL_NAME(sys_chroot)
	.long SYMBOL_NAME(sys_ustat)
	.long SYMBOL_NAME(sys_dup2)
	.long SYMBOL_NAME(sys_getppid)
	.long SYMBOL_NAME(sys_getpgrp)		/* 65 */
	.long SYMBOL_NAME(sys_setsid)
	.long SYMBOL_NAME(sys_sigaction)
	.long SYMBOL_NAME(sys_sgetmask)
	.long SYMBOL_NAME(sys_ssetmask)
	.long SYMBOL_NAME(sys_setreuid)		/* 70 */
	.long SYMBOL_NAME(sys_setregid)
	.long SYMBOL_NAME(sys_sigsuspend)
	.long SYMBOL_NAME(sys_sigpending)
	.long SYMBOL_NAME(sys_sethostname)
	.long SYMBOL_NAME(sys_setrlimit)	/* 75 */
	.long SYMBOL_NAME(sys_getrlimit)
	.long SYMBOL_NAME(sys_getrusage)
	.long SYMBOL_NAME(sys_gettimeofday)
	.long SYMBOL_NAME(sys_settimeofday)
	.long SYMBOL_NAME(sys_getgroups)	/* 80 */
	.long SYMBOL_NAME(sys_setgroups)
	.long SYMBOL_NAME(old_select)
	.long SYMBOL_NAME(sys_symlink)
	.long SYMBOL_NAME(sys_lstat)
	.long SYMBOL_NAME(sys_readlink)		/* 85 */
	.long SYMBOL_NAME(sys_uselib)
	.long SYMBOL_NAME(sys_swapon)
	.long SYMBOL_NAME(sys_reboot)
	.long SYMBOL_NAME(old_readdir)
	.long SYMBOL_NAME(old_mmap)		/* 90 */
	.long SYMBOL_NAME(sys_munmap)
	.long SYMBOL_NAME(sys_truncate)
	.long SYMBOL_NAME(sys_ftruncate)
	.long SYMBOL_NAME(sys_fchmod)
	.long SYMBOL_NAME(sys_fchown)		/* 95 */
	.long SYMBOL_NAME(sys_getpriority)
	.long SYMBOL_NAME(sys_setpriority)
	.long SYMBOL_NAME(sys_profil)
	.long SYMBOL_NAME(sys_statfs)
	.long SYMBOL_NAME(sys_fstatfs)		/* 100 */
	.long SYMBOL_NAME(sys_ioperm)
	.long SYMBOL_NAME(sys_socketcall)
	.long SYMBOL_NAME(sys_syslog)
	.long SYMBOL_NAME(sys_setitimer)
	.long SYMBOL_NAME(sys_getitimer)	/* 105 */
	.long SYMBOL_NAME(sys_newstat)
	.long SYMBOL_NAME(sys_newlstat)
	.long SYMBOL_NAME(sys_newfstat)
	.long SYMBOL_NAME(sys_uname)
	.long SYMBOL_NAME(sys_ni_syscall)	/* iopl for i386 */ /* 110 */
	.long SYMBOL_NAME(sys_vhangup)
	.long SYMBOL_NAME(sys_idle)
	.long SYMBOL_NAME(sys_ni_syscall)	/* vm86 for i386 */
	.long SYMBOL_NAME(sys_wait4)
	.long SYMBOL_NAME(sys_swapoff)		/* 115 */
	.long SYMBOL_NAME(sys_sysinfo)
	.long SYMBOL_NAME(sys_ipc)
	.long SYMBOL_NAME(sys_fsync)
	.long SYMBOL_NAME(sys_sigreturn)
	.long SYMBOL_NAME(sys_clone)		/* 120 */
	.long SYMBOL_NAME(sys_setdomainname)
	.long SYMBOL_NAME(sys_newuname)
	.long SYMBOL_NAME(sys_cacheflush)	/* modify_ldt for i386 */
	.long SYMBOL_NAME(sys_adjtimex)
	.long SYMBOL_NAME(sys_mprotect)		/* 125 */
	.long SYMBOL_NAME(sys_sigprocmask)
	.long SYMBOL_NAME(sys_create_module)
	.long SYMBOL_NAME(sys_init_module)
	.long SYMBOL_NAME(sys_delete_module)
	.long SYMBOL_NAME(sys_get_kernel_syms)	/* 130 */
	.long SYMBOL_NAME(sys_quotactl)
	.long SYMBOL_NAME(sys_getpgid)
	.long SYMBOL_NAME(sys_fchdir)
	.long SYMBOL_NAME(sys_bdflush)
	.long SYMBOL_NAME(sys_sysfs)		/* 135 */
	.long SYMBOL_NAME(sys_personality)
	.long SYMBOL_NAME(sys_ni_syscall)	/* for afs_syscall */
	.long SYMBOL_NAME(sys_setfsuid)
	.long SYMBOL_NAME(sys_setfsgid)
	.long SYMBOL_NAME(sys_llseek)		/* 140 */
	.long SYMBOL_NAME(sys_getdents)
	.long SYMBOL_NAME(sys_select)
	.long SYMBOL_NAME(sys_flock)
	.long SYMBOL_NAME(sys_msync)
	.long SYMBOL_NAME(sys_readv)		/* 145 */
	.long SYMBOL_NAME(sys_writev)
	.long SYMBOL_NAME(sys_getsid)
	.long SYMBOL_NAME(sys_fdatasync)
	.long SYMBOL_NAME(sys_sysctl)
	.long SYMBOL_NAME(sys_mlock)		/* 150 */
	.long SYMBOL_NAME(sys_munlock)
	.long SYMBOL_NAME(sys_mlockall)
	.long SYMBOL_NAME(sys_munlockall)
	.long SYMBOL_NAME(sys_sched_setparam)
	.long SYMBOL_NAME(sys_sched_getparam)   /* 155 */
	.long SYMBOL_NAME(sys_sched_setscheduler)
	.long SYMBOL_NAME(sys_sched_getscheduler)
	.long SYMBOL_NAME(sys_sched_yield)
	.long SYMBOL_NAME(sys_sched_get_priority_max)
	.long SYMBOL_NAME(sys_sched_get_priority_min)  /* 160 */
	.long SYMBOL_NAME(sys_sched_rr_get_interval)
	.long SYMBOL_NAME(sys_nanosleep)
	.long SYMBOL_NAME(sys_mremap)
	.space (NR_syscalls-163)*4
