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
#include <asm/segment.h>

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
errno		= 24
dbgreg6		= 52
dbgreg7		= 56
exec_domain	= 60

ENOSYS = 38

.globl _system_call,_lcall7
.globl _device_not_available, _coprocessor_error
.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_reserved
.globl _alignment_check,_page_fault
.globl ret_from_sys_call, _sys_call_table

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

#define RESTORE_ALL \
	cmpw $(KERNEL_CS),CS(%esp); \
	je 1f;   \
	movl _current,%eax; \
	movl dbgreg7(%eax),%ebx; \
	movl %ebx,%db7;	\
1:	popl %ebx; \
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

.align 4
_lcall7:
	pushfl			# We get a different stack layout with call gates,
	pushl %eax		# which has to be cleaned up later..
	SAVE_ALL
	movl EIP(%esp),%eax	# due to call gates, this is eflags, not eip..
	movl CS(%esp),%edx	# this is eip..
	movl EFLAGS(%esp),%ecx	# and this is cs..
	movl %eax,EFLAGS(%esp)	#
	movl %edx,EIP(%esp)	# Now we move them to their "normal" places
	movl %ecx,CS(%esp)	#
	movl %esp,%eax
	movl _current,%edx
	pushl %eax
	movl exec_domain(%edx),%edx	# Get the execution domain
	movl 4(%edx),%edx	# Get the lcall7 handler for the domain
	call *%edx
	popl %eax
	jmp ret_from_sys_call

.align 4
handle_bottom_half:
	pushfl
	incl _intr_count
	sti
	call _do_bottom_half
	popfl
	decl _intr_count
	jmp 9f
.align 4
reschedule:
	pushl $ret_from_sys_call
	jmp _schedule
.align 4
_system_call:
	pushl %eax			# save orig_eax
	SAVE_ALL
	movl $-ENOSYS,EAX(%esp)
	cmpl $(NR_syscalls),%eax
	jae ret_from_sys_call
	movl _sys_call_table(,%eax,4),%eax
	testl %eax,%eax
	je ret_from_sys_call
	movl _current,%ebx
	andl $~CF_MASK,EFLAGS(%esp)	# clear carry - assume no errors
	movl $0,errno(%ebx)
	movl %db6,%edx
	movl %edx,dbgreg6(%ebx)  # save current hardware debugging status
	testb $0x20,flags(%ebx)		# PF_TRACESYS
	jne 1f
	call *%eax
	movl %eax,EAX(%esp)		# save the return value
	movl errno(%ebx),%edx
	negl %edx
	je ret_from_sys_call
	movl %edx,EAX(%esp)
	orl $(CF_MASK),EFLAGS(%esp)	# set carry to indicate error
	jmp ret_from_sys_call
.align 4
1:	call _syscall_trace
	movl ORIG_EAX(%esp),%eax
	call _sys_call_table(,%eax,4)
	movl %eax,EAX(%esp)		# save the return value
	movl _current,%eax
	movl errno(%eax),%edx
	negl %edx
	je 1f
	movl %edx,EAX(%esp)
	orl $(CF_MASK),EFLAGS(%esp)	# set carry to indicate error
1:	call _syscall_trace

	.align 4,0x90
ret_from_sys_call:
	cmpl $0,_intr_count
	jne 2f
9:	movl _bh_mask,%eax
	andl _bh_active,%eax
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
	cmpl $0,_need_resched
	jne reschedule
	movl _current,%eax
	cmpl _task,%eax			# task[0] cannot have signals
	je 2f
	cmpl $0,state(%eax)		# state
	jne reschedule
	cmpl $0,counter(%eax)		# counter
	je reschedule
	movl blocked(%eax),%ecx
	movl %ecx,%ebx			# save blocked in %ebx for signal handling
	notl %ecx
	andl signal(%eax),%ecx
	jne signal_return
2:	RESTORE_ALL
.align 4
signal_return:
	movl %esp,%ecx
	pushl %ecx
	testl $(VM_MASK),EFLAGS(%ecx)
	jne v86_signal_return
	pushl %ebx
	call _do_signal
	popl %ebx
	popl %ebx
	RESTORE_ALL
.align 4
v86_signal_return:
	call _save_v86_state
	movl %eax,%esp
	pushl %eax
	pushl %ebx
	call _do_signal
	popl %ebx
	popl %ebx
	RESTORE_ALL

.align 4
_divide_error:
	pushl $0		# no error code
	pushl $_do_divide_error
.align 4,0x90
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
	pushl %eax
	movl _current,%eax
	movl %db6,%edx
	movl %edx,dbgreg6(%eax)  # save current hardware debugging status
	popl %eax
	call *%ebx
	addl $8,%esp
	jmp ret_from_sys_call

.align 4
_coprocessor_error:
	pushl $0
	pushl $_do_coprocessor_error
	jmp error_code

.align 4
_device_not_available:
	pushl $-1		# mark this as an int
	SAVE_ALL
	pushl $ret_from_sys_call
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je _math_state_restore
	pushl $0		# temporary storage for ORIG_EIP
	call _math_emulate
	addl $4,%esp
	ret

.align 4
_debug:
	pushl $0
	pushl $_do_debug
	jmp error_code

.align 4
_nmi:
	pushl $0
	pushl $_do_nmi
	jmp error_code

.align 4
_int3:
	pushl $0
	pushl $_do_int3
	jmp error_code

.align 4
_overflow:
	pushl $0
	pushl $_do_overflow
	jmp error_code

.align 4
_bounds:
	pushl $0
	pushl $_do_bounds
	jmp error_code

.align 4
_invalid_op:
	pushl $0
	pushl $_do_invalid_op
	jmp error_code

.align 4
_coprocessor_segment_overrun:
	pushl $0
	pushl $_do_coprocessor_segment_overrun
	jmp error_code

.align 4
_reserved:
	pushl $0
	pushl $_do_reserved
	jmp error_code

.align 4
_double_fault:
	pushl $_do_double_fault
	jmp error_code

.align 4
_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code

.align 4
_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code

.align 4
_stack_segment:
	pushl $_do_stack_segment
	jmp error_code

.align 4
_general_protection:
	pushl $_do_general_protection
	jmp error_code

.align 4
_alignment_check:
	pushl $_do_alignment_check
	jmp error_code

.align 4
_page_fault:
	pushl $_do_page_fault
	jmp error_code

.data
.align 4
_sys_call_table:
	.long _sys_setup		/* 0 */
	.long _sys_exit
	.long _sys_fork
	.long _sys_read
	.long _sys_write
	.long _sys_open			/* 5 */
	.long _sys_close
	.long _sys_waitpid
	.long _sys_creat
	.long _sys_link
	.long _sys_unlink		/* 10 */
	.long _sys_execve
	.long _sys_chdir
	.long _sys_time
	.long _sys_mknod
	.long _sys_chmod		/* 15 */
	.long _sys_chown
	.long _sys_break
	.long _sys_stat
	.long _sys_lseek
	.long _sys_getpid		/* 20 */
	.long _sys_mount
	.long _sys_umount
	.long _sys_setuid
	.long _sys_getuid
	.long _sys_stime		/* 25 */
	.long _sys_ptrace
	.long _sys_alarm
	.long _sys_fstat
	.long _sys_pause
	.long _sys_utime		/* 30 */
	.long _sys_stty
	.long _sys_gtty
	.long _sys_access
	.long _sys_nice
	.long _sys_ftime		/* 35 */
	.long _sys_sync
	.long _sys_kill
	.long _sys_rename
	.long _sys_mkdir
	.long _sys_rmdir		/* 40 */
	.long _sys_dup
	.long _sys_pipe
	.long _sys_times
	.long _sys_prof
	.long _sys_brk			/* 45 */
	.long _sys_setgid
	.long _sys_getgid
	.long _sys_signal
	.long _sys_geteuid
	.long _sys_getegid		/* 50 */
	.long _sys_acct
	.long _sys_phys
	.long _sys_lock
	.long _sys_ioctl
	.long _sys_fcntl		/* 55 */
	.long _sys_mpx
	.long _sys_setpgid
	.long _sys_ulimit
	.long _sys_olduname
	.long _sys_umask		/* 60 */
	.long _sys_chroot
	.long _sys_ustat
	.long _sys_dup2
	.long _sys_getppid
	.long _sys_getpgrp		/* 65 */
	.long _sys_setsid
	.long _sys_sigaction
	.long _sys_sgetmask
	.long _sys_ssetmask
	.long _sys_setreuid		/* 70 */
	.long _sys_setregid
	.long _sys_sigsuspend
	.long _sys_sigpending
	.long _sys_sethostname
	.long _sys_setrlimit		/* 75 */
	.long _sys_getrlimit
	.long _sys_getrusage
	.long _sys_gettimeofday
	.long _sys_settimeofday
	.long _sys_getgroups		/* 80 */
	.long _sys_setgroups
	.long _sys_select
	.long _sys_symlink
	.long _sys_lstat
	.long _sys_readlink		/* 85 */
	.long _sys_uselib
	.long _sys_swapon
	.long _sys_reboot
	.long _sys_readdir
	.long _sys_mmap			/* 90 */
	.long _sys_munmap
	.long _sys_truncate
	.long _sys_ftruncate
	.long _sys_fchmod
	.long _sys_fchown		/* 95 */
	.long _sys_getpriority
	.long _sys_setpriority
	.long _sys_profil
	.long _sys_statfs
	.long _sys_fstatfs		/* 100 */
	.long _sys_ioperm
	.long _sys_socketcall
	.long _sys_syslog
	.long _sys_setitimer
	.long _sys_getitimer		/* 105 */
	.long _sys_newstat
	.long _sys_newlstat
	.long _sys_newfstat
	.long _sys_uname
	.long _sys_iopl			/* 110 */
	.long _sys_vhangup
	.long _sys_idle
	.long _sys_vm86
	.long _sys_wait4
	.long _sys_swapoff		/* 115 */
	.long _sys_sysinfo
	.long _sys_ipc
	.long _sys_fsync
	.long _sys_sigreturn
	.long _sys_clone		/* 120 */
	.long _sys_setdomainname
	.long _sys_newuname
	.long _sys_modify_ldt
	.long _sys_adjtimex
	.long _sys_mprotect		/* 125 */
	.long _sys_sigprocmask
	.long _sys_create_module
	.long _sys_init_module
	.long _sys_delete_module
	.long _sys_get_kernel_syms	/* 130 */
	.long _sys_quotactl
	.long _sys_getpgid
	.long _sys_fchdir
	.long _sys_bdflush
	.long _sys_sysfs		/* 135 */
	.long _sys_personality
	.long 0				/* for afs_syscall */
	.long _sys_setfsuid
	.long _sys_setfsgid
	.long _sys_llseek		/* 140 */
	.space (NR_syscalls-140)*4
