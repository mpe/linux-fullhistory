#include <asm/offsets.h>
#include <asm/signal.h>

	.global ia32_ret_from_syscall
	.proc ia64_ret_from_syscall
ia32_ret_from_syscall:
	cmp.ge p6,p7=r8,r0                      // syscall executed successfully?
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp    // r2 = &pt_regs.r8
	;; 
	st8 [r2]=r8                             // store return value in slot for r8
	br.cond.sptk.few ia64_leave_kernel

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
	.global ia32_trace_syscall
	.proc ia32_trace_syscall
ia32_trace_syscall:
	br.call.sptk.few rp=invoke_syscall_trace // give parent a chance to catch syscall args
.Lret4:	br.call.sptk.few rp=b6			// do the syscall
.Lret5:	cmp.lt p6,p0=r8,r0			// syscall failed?
	adds r2=IA64_PT_REGS_R8_OFFSET+16,sp	// r2 = &pt_regs.r8
	;;
	st8.spill [r2]=r8			// store return value in slot for r8
	br.call.sptk.few rp=invoke_syscall_trace // give parent a chance to catch return value
.Lret6:	br.cond.sptk.many ia64_leave_kernel	// rp MUST be != ia64_leave_kernel!

	.endp ia32_trace_syscall

	.align 16
	.global sys32_fork
	.proc sys32_fork
sys32_fork:
	alloc r16=ar.pfs,2,2,3,0;;
	movl r28=1f
	mov loc1=rp
	br.cond.sptk.many save_switch_stack
1:
	mov loc0=r16				// save ar.pfs across do_fork
	adds out2=IA64_SWITCH_STACK_SIZE+16,sp
	adds r2=IA64_SWITCH_STACK_SIZE+IA64_PT_REGS_R12_OFFSET+16,sp
	mov out0=SIGCHLD			// out0 = clone_flags
	;;
	ld8 out1=[r2]				// fetch usp from pt_regs.r12
	br.call.sptk.few rp=do_fork
.ret1:
	mov ar.pfs=loc0
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// pop the switch stack
	mov rp=loc1
	;;
	br.ret.sptk.many rp
	.endp sys32_fork

	.rodata
	.align 8
	.globl ia32_syscall_table
ia32_syscall_table:	
	data8 sys_ni_syscall	  /* 0	-  old "setup(" system call*/
	data8 sys_exit
	data8 sys32_fork
	data8 sys_read
	data8 sys_write
	data8 sys_open		  /* 5 */
	data8 sys_close
	data8 sys32_waitpid
	data8 sys_creat
	data8 sys_link
	data8 sys_unlink	  /* 10 */
	data8 sys32_execve
	data8 sys_chdir
	data8 sys32_time
	data8 sys_mknod
	data8 sys_chmod		  /* 15 */
	data8 sys_lchown
	data8 sys_ni_syscall	  /* old break syscall holder */
	data8 sys_ni_syscall
	data8 sys_lseek
	data8 sys_getpid	  /* 20 */
	data8 sys_mount
	data8 sys_oldumount
	data8 sys_setuid
	data8 sys_getuid
	data8 sys_ni_syscall /* sys_stime is not supported on IA64 */  /* 25 */
	data8 sys_ptrace
	data8 sys32_alarm
	data8 sys_ni_syscall
	data8 sys_ni_syscall
	data8 sys_ni_syscall	  /* 30 */
	data8 sys_ni_syscall	  /* old stty syscall holder */
	data8 sys_ni_syscall	  /* old gtty syscall holder */
	data8 sys_access
	data8 sys_nice
	data8 sys_ni_syscall	  /* 35 */	  /* old ftime syscall holder */
	data8 sys_sync
	data8 sys_kill
	data8 sys_rename
	data8 sys_mkdir
	data8 sys_rmdir		  /* 40 */
	data8 sys_dup
	data8 sys32_pipe
	data8 sys_times
	data8 sys_ni_syscall	  /* old prof syscall holder */
	data8 sys_brk		  /* 45 */
	data8 sys_setgid
	data8 sys_getgid
	data8 sys_ni_syscall
	data8 sys_geteuid
	data8 sys_getegid	  /* 50 */
	data8 sys_acct
	data8 sys_umount	  /* recycled never used phys( */
	data8 sys_ni_syscall	  /* old lock syscall holder */
	data8 sys_ioctl
	data8 sys_fcntl		  /* 55 */
	data8 sys_ni_syscall	  /* old mpx syscall holder */
	data8 sys_setpgid
	data8 sys_ni_syscall	  /* old ulimit syscall holder */
	data8 sys_ni_syscall
	data8 sys_umask		  /* 60 */
	data8 sys_chroot
	data8 sys_ustat
	data8 sys_dup2
	data8 sys_getppid
	data8 sys_getpgrp	  /* 65 */
	data8 sys_setsid
	data8 sys_ni_syscall
	data8 sys_ni_syscall
	data8 sys_ni_syscall
	data8 sys_setreuid	  /* 70 */
	data8 sys_setregid
	data8 sys_ni_syscall
	data8 sys_sigpending
	data8 sys_sethostname
	data8 sys32_setrlimit	  /* 75 */
	data8 sys32_getrlimit
	data8 sys_getrusage
	data8 sys32_gettimeofday
	data8 sys32_settimeofday
	data8 sys_getgroups	  /* 80 */
	data8 sys_setgroups
	data8 old_select
	data8 sys_symlink
	data8 sys_ni_syscall
	data8 sys_readlink	  /* 85 */
	data8 sys_uselib
	data8 sys_swapon
	data8 sys_reboot
	data8 sys32_readdir
	data8 sys32_mmap	  /* 90 */
	data8 sys_munmap
	data8 sys_truncate
	data8 sys_ftruncate
	data8 sys_fchmod
	data8 sys_fchown	  /* 95 */
	data8 sys_getpriority
	data8 sys_setpriority
	data8 sys_ni_syscall	  /* old profil syscall holder */
	data8 sys32_statfs
	data8 sys32_fstatfs	  /* 100 */
	data8 sys_ioperm
	data8 sys32_socketcall
	data8 sys_syslog
	data8 sys32_setitimer
	data8 sys32_getitimer	  /* 105 */
	data8 sys32_newstat
	data8 sys32_newlstat
	data8 sys32_newfstat
	data8 sys_ni_syscall
	data8 sys_iopl		  /* 110 */
	data8 sys_vhangup
	data8 sys_ni_syscall		// used to be sys_idle
	data8 sys_ni_syscall
	data8 sys32_wait4
	data8 sys_swapoff	  /* 115 */
	data8 sys_sysinfo
	data8 sys32_ipc
	data8 sys_fsync
	data8 sys32_sigreturn
	data8 sys_clone		  /* 120 */
	data8 sys_setdomainname
	data8 sys_newuname
	data8 sys_modify_ldt
	data8 sys_adjtimex
	data8 sys32_mprotect	  /* 125 */
	data8 sys_sigprocmask
	data8 sys_create_module
	data8 sys_init_module
	data8 sys_delete_module
	data8 sys_get_kernel_syms  /* 130 */
	data8 sys_quotactl
	data8 sys_getpgid
	data8 sys_fchdir
	data8 sys_bdflush
	data8 sys_sysfs		  /* 135 */
	data8 sys_personality
	data8 sys_ni_syscall	  /* for afs_syscall */
	data8 sys_setfsuid
	data8 sys_setfsgid
	data8 sys_llseek	  /* 140 */
	data8 sys32_getdents
	data8 sys32_select
	data8 sys_flock
	data8 sys_msync
	data8 sys32_readv	  /* 145 */
	data8 sys32_writev
	data8 sys_getsid
	data8 sys_fdatasync
	data8 sys_sysctl
	data8 sys_mlock		  /* 150 */
	data8 sys_munlock
	data8 sys_mlockall
	data8 sys_munlockall
	data8 sys_sched_setparam
	data8 sys_sched_getparam  /* 155 */
	data8 sys_sched_setscheduler
	data8 sys_sched_getscheduler
	data8 sys_sched_yield
	data8 sys_sched_get_priority_max
	data8 sys_sched_get_priority_min	 /* 160 */
	data8 sys_sched_rr_get_interval
	data8 sys32_nanosleep
	data8 sys_mremap
	data8 sys_setresuid
	data8 sys_getresuid	  /* 165 */
	data8 sys_vm86
	data8 sys_query_module
	data8 sys_poll
	data8 sys_nfsservctl
	data8 sys_setresgid	  /* 170 */
	data8 sys_getresgid
	data8 sys_prctl
	data8 sys32_rt_sigreturn
	data8 sys32_rt_sigaction
	data8 sys32_rt_sigprocmask /* 175 */
	data8 sys_rt_sigpending
	data8 sys_rt_sigtimedwait
	data8 sys_rt_sigqueueinfo
	data8 sys_rt_sigsuspend
	data8 sys_pread		  /* 180 */
	data8 sys_pwrite
	data8 sys_chown
	data8 sys_getcwd
	data8 sys_capget
	data8 sys_capset	  /* 185 */
	data8 sys_sigaltstack
	data8 sys_sendfile
	data8 sys_ni_syscall		  /* streams1 */
	data8 sys_ni_syscall		  /* streams2 */
	data8 sys32_vfork	  /* 190 */
