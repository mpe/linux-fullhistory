/*
 *  linux/arch/i386/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * I changed all the .align's to 4 (16 byte alignment), as that's faster
 * on a 486.
 *
 * Stack layout in 'ret_from_system_call':
 * 	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be 
 *	updated in fork.c:copy_process, signal.c:do_signal,
 *	ptrace.c and ptrace.h
 *
 *	 0(%esp) - %ebx
 *	 4(%esp) - %ecx
 *	 8(%esp) - %edx
 *       C(%esp) - %esi
 *	10(%esp) - %edi
 *	14(%esp) - %ebp
 *	18(%esp) - %eax
 *	1C(%esp) - %ds
 *	20(%esp) - %es
 *      24(%esp) - %fs
 *	28(%esp) - %gs
 *	2C(%esp) - orig_eax
 *	30(%esp) - %eip
 *	34(%esp) - %cs
 *	38(%esp) - %eflags
 *	3C(%esp) - %oldesp
 *	40(%esp) - %oldss
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#define ASSEMBLY
#include <asm/smp.h>

EBX		= 0x00
ECX		= 0x04
EDX		= 0x08
ESI		= 0x0C
EDI		= 0x10
EBP		= 0x14
EAX		= 0x18
DS		= 0x1C
ES		= 0x20
FS		= 0x24
GS		= 0x28
ORIG_EAX	= 0x2C
EIP		= 0x30
CS		= 0x34
EFLAGS		= 0x38
OLDESP		= 0x3C
OLDSS		= 0x40

CF_MASK		= 0x00000001
IF_MASK		= 0x00000200
NT_MASK		= 0x00004000
VM_MASK		= 0x00020000

/*
 * these are offsets into the task-struct.
 */
state		=  0
counter		=  4
priority	=  8
signal		= 12
blocked		= 16
flags		= 20
dbgreg6		= 52
dbgreg7		= 56
exec_domain	= 60

ENOSYS = 38

#define SAVE_ALL \
	cld; \
	push %gs; \
	push %fs; \
	push %es; \
	push %ds; \
	pushl %eax; \
	pushl %ebp; \
	pushl %edi; \
	pushl %esi; \
	pushl %edx; \
	pushl %ecx; \
	pushl %ebx; \
	movl $(KERNEL_DS),%edx; \
	mov %dx,%ds; \
	mov %dx,%es; \
	movl $(USER_DS),%edx; \
	mov %dx,%fs;

#ifdef	__SMP__

#define GET_PROCESSOR_ID \
	movl SYMBOL_NAME(apic_reg), %edx; \
	movl 32(%edx), %eax;\
	movl %eax,SYMBOL_NAME(apic_retval); \
	shrl $24,%eax; \
	andb $0x0F,%al;

/*
 *	Get the processor ID multiplied by 4
 */

#define GET_PROCESSOR_OFFSET(x) \
	movl SYMBOL_NAME(apic_reg), x ; \
	movl 32( x ), x ; \
	shrl $22, x ; \
	andl $0x3C, x ;

/* macro LEAVE_KERNEL decrements kernel_counter and resets kernel_flag and
   saves processor variables if zero */
#define	LEAVE_KERNEL \
	pushfl; \
	cli; \
	GET_PROCESSOR_ID \
	btrl $ SMP_FROM_SYSCALL,SYMBOL_NAME(smp_proc_in_lock)(,%eax,4); \
	decl SYMBOL_NAME(syscall_count); \
	decl SYMBOL_NAME(kernel_counter); \
	jnz 1f;	\
	movb $(NO_PROC_ID), SYMBOL_NAME(active_kernel_processor); \
	lock; \
	btrl $0, SYMBOL_NAME(kernel_flag); \
1:	popfl;

/* macro ENTER_KERNEL waits for entering the kernel, increments
   kernel_counter, and reloads the processor variables if necessary
   uses : %eax, %edx (pushed and popped) 

   Note: We go to great pains to minimise the number of locked operations.
   We want to spin without locking, and lock when we attempt an update.
   The pentium has a MESI cache so the spin without lock will exit when
   another CPU write invalidates our cache, and the lock is avoided when
   possible so we don't play ping-pong games with the cache line.

*/

#ifndef __SMP_PROF__

#define SMP_PROF_A
#define SMP_PROF_B 

#else

#define SMP_PROF_A movl $0,SYMBOL_NAME(smp_spins_syscall_cur)(,%eax,4);
#define SMP_PROF_B incl SYMBOL_NAME(smp_spins_syscall)(,%eax,4); \
        incl SYMBOL_NAME(smp_spins_syscall_cur)(,%eax,4);
#endif

#define	ENTER_KERNEL \
	pushl %eax; \
	pushl %edx; \
	pushfl; \
	cli; \
	GET_PROCESSOR_ID \
	btsl $ SMP_FROM_SYSCALL,SYMBOL_NAME(smp_proc_in_lock)(,%eax,4); \
	SMP_PROF_A \
1:	lock; \
	btsl $0, SYMBOL_NAME(kernel_flag); \
	jnc 3f; \
	cmpb SYMBOL_NAME(active_kernel_processor), %al; \
	je 4f; \
2:	SMP_PROF_B \
	btl %al, SYMBOL_NAME(smp_invalidate_needed); \
	jnc 5f; \
	lock; \
	btrl %al, SYMBOL_NAME(smp_invalidate_needed); \
	jnc 5f; \
	movl %cr3,%edx; \
	movl %edx,%cr3; \
5:	btl $0, SYMBOL_NAME(kernel_flag); \
	jc 2b; \
	jmp 1b; \
3:	movb %al, SYMBOL_NAME(active_kernel_processor); \
4:	incl SYMBOL_NAME(kernel_counter); \
	incl SYMBOL_NAME(syscall_count); \
	popfl; \
	popl %edx; \
	popl %eax;


#define RESTORE_ALL \
	cmpw $(KERNEL_CS),CS(%esp); \
	je 1f;   \
	GET_PROCESSOR_OFFSET(%edx) \
	movl SYMBOL_NAME(current_set)(,%edx), %eax ; ; \
	movl dbgreg7(%eax),%ebx; \
	movl %ebx,%db7;	\
1:	LEAVE_KERNEL \
	popl %ebx; \
	popl %ecx; \
	popl %edx; \
	popl %esi; \
	popl %edi; \
	popl %ebp; \
	popl %eax; \
	pop %ds; \
	pop %es; \
	pop %fs; \
	pop %gs; \
	addl $4,%esp; \
	iret

#else

#define RESTORE_ALL \
	cmpw $(KERNEL_CS),CS(%esp); \
	je 1f;   \
	movl SYMBOL_NAME(current_set),%eax; \
	movl dbgreg7(%eax),%ebx; \
	movl %ebx,%db7;	\
1:	\
	popl %ebx; \
	popl %ecx; \
	popl %edx; \
	popl %esi; \
	popl %edi; \
	popl %ebp; \
	popl %eax; \
	pop %ds; \
	pop %es; \
	pop %fs; \
	pop %gs; \
	addl $4,%esp; \
	iret
#endif


ENTRY(lcall7)
	pushfl			# We get a different stack layout with call gates,
	pushl %eax		# which has to be cleaned up later..
	SAVE_ALL
#ifdef __SMP__
	ENTER_KERNEL
#endif
	movl EIP(%esp),%eax	# due to call gates, this is eflags, not eip..
	movl CS(%esp),%edx	# this is eip..
	movl EFLAGS(%esp),%ecx	# and this is cs..
	movl %eax,EFLAGS(%esp)	#
	movl %edx,EIP(%esp)	# Now we move them to their "normal" places
	movl %ecx,CS(%esp)	#
	movl %esp,%eax
#ifdef __SMP__
	GET_PROCESSOR_OFFSET(%edx)	# Processor offset into edx
	movl SYMBOL_NAME(current_set)(,%edx),%edx
#else
	movl SYMBOL_NAME(current_set),%edx
#endif
	pushl %eax
	movl exec_domain(%edx),%edx	# Get the execution domain
	movl 4(%edx),%edx	# Get the lcall7 handler for the domain
	call *%edx
	popl %eax
	jmp ret_from_sys_call

	ALIGN
handle_bottom_half:
	incl SYMBOL_NAME(intr_count)
	call SYMBOL_NAME(do_bottom_half)
	decl SYMBOL_NAME(intr_count)
	jmp 9f
	ALIGN
reschedule:
	pushl $ret_from_sys_call
	jmp SYMBOL_NAME(schedule)    # test

ENTRY(system_call)
	pushl %eax			# save orig_eax
	SAVE_ALL
#ifdef __SMP__
	ENTER_KERNEL
#endif
	movl $-ENOSYS,EAX(%esp)
	cmpl $(NR_syscalls),%eax
	jae ret_from_sys_call
	movl SYMBOL_NAME(sys_call_table)(,%eax,4),%eax
	testl %eax,%eax
	je ret_from_sys_call
#ifdef __SMP__
	GET_PROCESSOR_OFFSET(%edx)
	movl SYMBOL_NAME(current_set)(,%edx),%ebx
#else
	movl SYMBOL_NAME(current_set),%ebx
#endif
	andl $~CF_MASK,EFLAGS(%esp)	# clear carry - assume no errors
	movl %db6,%edx
	movl %edx,dbgreg6(%ebx)  # save current hardware debugging status
	testb $0x20,flags(%ebx)		# PF_TRACESYS
	jne 1f
	call *%eax
	movl %eax,EAX(%esp)		# save the return value
	jmp ret_from_sys_call
	ALIGN
1:	call SYMBOL_NAME(syscall_trace)
	movl ORIG_EAX(%esp),%eax
	call SYMBOL_NAME(sys_call_table)(,%eax,4)
	movl %eax,EAX(%esp)		# save the return value
#ifdef __SMP__
	GET_PROCESSOR_OFFSET(%eax)
	movl SYMBOL_NAME(current_set)(,%eax),%eax
#else
	movl SYMBOL_NAME(current_set),%eax
#endif
	call SYMBOL_NAME(syscall_trace)

	ALIGN
	.globl ret_from_sys_call
ret_from_sys_call:
	cmpl $0,SYMBOL_NAME(intr_count)
	jne 2f
9:	movl SYMBOL_NAME(bh_mask),%eax
	andl SYMBOL_NAME(bh_active),%eax
	jne handle_bottom_half
	movl EFLAGS(%esp),%eax		# check VM86 flag: CS/SS are
	testl $(VM_MASK),%eax		# different then
	jne 1f
	cmpw $(KERNEL_CS),CS(%esp)	# was old code segment supervisor ?
	je 2f
1:	sti
	orl $(IF_MASK),%eax		# these just try to make sure
	andl $~NT_MASK,%eax		# the program doesn't do anything
	movl %eax,EFLAGS(%esp)		# stupid
	cmpl $0,SYMBOL_NAME(need_resched)
	jne reschedule
#ifdef __SMP__
	GET_PROCESSOR_OFFSET(%eax)
	movl SYMBOL_NAME(current_set)(,%eax), %eax
#else
	movl SYMBOL_NAME(current_set),%eax
#endif
	cmpl SYMBOL_NAME(task),%eax	# task[0] cannot have signals
	je 2f
	movl blocked(%eax),%ecx
	movl %ecx,%ebx			# save blocked in %ebx for signal handling
	notl %ecx
	andl signal(%eax),%ecx
	jne signal_return
2:	RESTORE_ALL
	ALIGN
signal_return:
	movl %esp,%ecx
	pushl %ecx
	testl $(VM_MASK),EFLAGS(%ecx)
	jne v86_signal_return
	pushl %ebx
	call SYMBOL_NAME(do_signal)
	popl %ebx
	popl %ebx
	RESTORE_ALL
	ALIGN
v86_signal_return:
	call SYMBOL_NAME(save_v86_state)
	movl %eax,%esp
	pushl %eax
	pushl %ebx
	call SYMBOL_NAME(do_signal)
	popl %ebx
	popl %ebx
	RESTORE_ALL

ENTRY(divide_error)
	pushl $0		# no error code
	pushl $ SYMBOL_NAME(do_divide_error)
	ALIGN
error_code:
	push %fs
	push %es
	push %ds
	pushl %eax
	pushl %ebp
	pushl %edi
	pushl %esi
	pushl %edx
	pushl %ecx
	pushl %ebx
	movl $0,%eax
	movl %eax,%db7			# disable hardware debugging...
	cld
	movl $-1, %eax
	xchgl %eax, ORIG_EAX(%esp)	# orig_eax (get the error code. )
	xorl %ebx,%ebx			# zero ebx
	mov %gs,%bx			# get the lower order bits of gs
	xchgl %ebx, GS(%esp)		# get the address and save gs.
	pushl %eax			# push the error code
	lea 4(%esp),%edx
	pushl %edx
	movl $(KERNEL_DS),%edx
	mov %dx,%ds
	mov %dx,%es
	movl $(USER_DS),%edx
	mov %dx,%fs
#ifdef __SMP__
	ENTER_KERNEL
#endif
	pushl %eax
#ifdef __SMP__
	GET_PROCESSOR_OFFSET(%eax)
	movl SYMBOL_NAME(current_set)(,%eax), %eax
#else
	movl SYMBOL_NAME(current_set),%eax
#endif
	movl %db6,%edx
	movl %edx,dbgreg6(%eax)  # save current hardware debugging status
	popl %eax
	call *%ebx
	addl $8,%esp
	jmp ret_from_sys_call

ENTRY(coprocessor_error)
	pushl $0
	pushl $ SYMBOL_NAME(do_coprocessor_error)
	jmp error_code

ENTRY(device_not_available)
	pushl $-1		# mark this as an int
	SAVE_ALL
#ifdef __SMP__
	ENTER_KERNEL
#endif
	pushl $ret_from_sys_call
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je SYMBOL_NAME(math_state_restore)
	pushl $0		# temporary storage for ORIG_EIP
	call  SYMBOL_NAME(math_emulate)
	addl $4,%esp
	ret

ENTRY(debug)
	pushl $0
	pushl $ SYMBOL_NAME(do_debug)
	jmp error_code

ENTRY(nmi)
	pushl $0
	pushl $ SYMBOL_NAME(do_nmi)
	jmp error_code

ENTRY(int3)
	pushl $0
	pushl $ SYMBOL_NAME(do_int3)
	jmp error_code

ENTRY(overflow)
	pushl $0
	pushl $ SYMBOL_NAME(do_overflow)
	jmp error_code

ENTRY(bounds)
	pushl $0
	pushl $ SYMBOL_NAME(do_bounds)
	jmp error_code

ENTRY(invalid_op)
	pushl $0
	pushl $ SYMBOL_NAME(do_invalid_op)
	jmp error_code

ENTRY(coprocessor_segment_overrun)
	pushl $0
	pushl $ SYMBOL_NAME(do_coprocessor_segment_overrun)
	jmp error_code

ENTRY(reserved)
	pushl $0
	pushl $ SYMBOL_NAME(do_reserved)
	jmp error_code

ENTRY(double_fault)
	pushl $ SYMBOL_NAME(do_double_fault)
	jmp error_code

ENTRY(invalid_TSS)
	pushl $ SYMBOL_NAME(do_invalid_TSS)
	jmp error_code

ENTRY(segment_not_present)
	pushl $ SYMBOL_NAME(do_segment_not_present)
	jmp error_code

ENTRY(stack_segment)
	pushl $ SYMBOL_NAME(do_stack_segment)
	jmp error_code

ENTRY(general_protection)
	pushl $ SYMBOL_NAME(do_general_protection)
	jmp error_code

ENTRY(alignment_check)
	pushl $ SYMBOL_NAME(do_alignment_check)
	jmp error_code

ENTRY(page_fault)
	pushl $ SYMBOL_NAME(do_page_fault)
	jmp error_code

.data
ENTRY(sys_call_table)
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
	.long SYMBOL_NAME(sys_iopl)		/* 110 */
	.long SYMBOL_NAME(sys_vhangup)
	.long SYMBOL_NAME(sys_idle)
	.long SYMBOL_NAME(sys_vm86)
	.long SYMBOL_NAME(sys_wait4)
	.long SYMBOL_NAME(sys_swapoff)		/* 115 */
	.long SYMBOL_NAME(sys_sysinfo)
	.long SYMBOL_NAME(sys_ipc)
	.long SYMBOL_NAME(sys_fsync)
	.long SYMBOL_NAME(sys_sigreturn)
	.long SYMBOL_NAME(sys_clone)		/* 120 */
	.long SYMBOL_NAME(sys_setdomainname)
	.long SYMBOL_NAME(sys_newuname)
	.long SYMBOL_NAME(sys_modify_ldt)
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
	.long 0					/* for afs_syscall */
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
