/* $Id: systbls.S,v 1.54 1996/12/03 08:44:37 jj Exp $
 * systbls.S: System call entry point tables for OS compatibility.
 *            The native Linux system call table lives here also.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */

#include <asm/cprefix.h>

	/* READ THIS BEFORE DICKING WITH THIS TABLE...
	 *
	 * The format of these entries is kind of peculiar
	 * to optimize non-blocking easy syscalls.  If
	 * it is a difficult call or it will sleep the entry
	 * is just to word aligned address of the function
	 * routine to call.  If the lowest bit of the entry
	 * is set then (entry & ~1) is the address of the low
	 * in-trap-window assembler routine which will handle
	 * the system call at the lowest possible level.  For
	 * these low level optimized routines no state is saved
	 * at all and the usual restrictions reply.  Act as
	 * if you got called directly from the trap table.
	 * Some of these optimized routines try really hard
	 * to get around a state save, if you run into trouble
	 * you can still survive by branching to the label
	 * syscall_is_too_hard which is in entry.S  If you
	 * have to back out like this you _must_ preserve the
	 * value of %l0, %l1, %l2, and %l7 when you were called
	 * so be _careful_.
	 */

#define LOWSYS(func) (CONCAT(func, _low) + 1)

	.data
	.align 4

	/* First, the Linux native syscall table. */

	.globl C_LABEL(sys_call_table)
C_LABEL(sys_call_table):
/*0*/	.long C_LABEL(sys_setup), C_LABEL(sys_exit), C_LABEL(sys_fork)
	.long C_LABEL(sys_read), C_LABEL(sys_write)
/*5*/	.long C_LABEL(sys_open), C_LABEL(sys_close), C_LABEL(sys_wait4)
	.long C_LABEL(sys_creat), C_LABEL(sys_link)
/*10*/  .long C_LABEL(sys_unlink), C_LABEL(sunos_execv), C_LABEL(sys_chdir)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_mknod)
/*15*/	.long C_LABEL(sys_chmod), C_LABEL(sys_chown), C_LABEL(sparc_brk)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_lseek)
/*20*/	.long C_LABEL(sys_getpid), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_setuid), C_LABEL(sys_getuid)
/*25*/	.long C_LABEL(sys_time), C_LABEL(sys_ptrace), C_LABEL(sys_alarm)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_pause)
/*30*/	.long C_LABEL(sys_utime), C_LABEL(sys_stty), C_LABEL(sys_gtty)
	.long C_LABEL(sys_access), C_LABEL(sys_nice), C_LABEL(sys_ftime)
	.long C_LABEL(sys_sync), C_LABEL(sys_kill), C_LABEL(sys_newstat)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_newlstat), C_LABEL(sys_dup)
	.long C_LABEL(sys_pipe), C_LABEL(sys_times), C_LABEL(sys_profil)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_setgid), C_LABEL(sys_getgid)
	.long C_LABEL(sys_signal), C_LABEL(sys_geteuid)
/*50*/	.long C_LABEL(sys_getegid), C_LABEL(sys_acct), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_ioctl), C_LABEL(sys_reboot)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_symlink), C_LABEL(sys_readlink)
	.long C_LABEL(sys_execve), C_LABEL(sys_umask), C_LABEL(sys_chroot)
	.long C_LABEL(sys_newfstat), C_LABEL(sys_nis_syscall), C_LABEL(sys_getpagesize)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_vfork), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_mmap), C_LABEL(sys_nis_syscall), C_LABEL(sys_munmap)
	.long C_LABEL(sys_mprotect), C_LABEL(sys_nis_syscall), C_LABEL(sys_vhangup)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_getgroups)
	.long C_LABEL(sys_setgroups), C_LABEL(sys_getpgrp), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_setitimer), C_LABEL(sys_nis_syscall), C_LABEL(sys_swapon)
	.long C_LABEL(sys_getitimer), C_LABEL(sys_nis_syscall), C_LABEL(sys_sethostname)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_dup2), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_fcntl), C_LABEL(sys_select), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_fsync), C_LABEL(sys_setpriority), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*100*/	.long C_LABEL(sys_getpriority), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_gettimeofday), C_LABEL(sys_getrusage)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_readv)
	.long C_LABEL(sys_writev), C_LABEL(sys_settimeofday), C_LABEL(sys_fchown)
	.long C_LABEL(sys_fchmod), C_LABEL(sys_nis_syscall), C_LABEL(sys_setreuid)
	.long C_LABEL(sys_setregid), C_LABEL(sys_rename), C_LABEL(sys_truncate)
	.long C_LABEL(sys_ftruncate), C_LABEL(sys_flock), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_mkdir), C_LABEL(sys_rmdir), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_getrlimit)
	.long C_LABEL(sys_setrlimit), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*150*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_statfs), C_LABEL(sys_fstatfs)
	.long C_LABEL(sys_umount), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_setdomainname)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_quotactl), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_mount), C_LABEL(sys_ustat), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_getdents), C_LABEL(sys_setsid)
	.long C_LABEL(sys_fchdir), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_sigpending), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_setpgid), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_newuname), C_LABEL(sys_init_module)
	.long C_LABEL(sys_personality), C_LABEL(sys_prof), C_LABEL(sys_break)
	.long C_LABEL(sys_lock), C_LABEL(sys_mpx), C_LABEL(sys_ulimit)
	.long C_LABEL(sys_getppid), C_LABEL(sparc_sigaction), C_LABEL(sys_sgetmask)
/*200*/	.long C_LABEL(sys_ssetmask), C_LABEL(sys_sigsuspend), C_LABEL(sys_newlstat)
	.long C_LABEL(sys_uselib), C_LABEL(old_readdir), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_socketcall), C_LABEL(sys_syslog), C_LABEL(sys_olduname)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_idle), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_waitpid), C_LABEL(sys_swapoff), C_LABEL(sys_sysinfo)
	.long C_LABEL(sys_ipc), C_LABEL(sys_sigreturn), C_LABEL(sys_clone)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_adjtimex), C_LABEL(sys_sigprocmask)
	.long C_LABEL(sys_create_module), C_LABEL(sys_delete_module)
	.long C_LABEL(sys_get_kernel_syms), C_LABEL(sys_getpgid), C_LABEL(sys_bdflush)
	.long C_LABEL(sys_sysfs), C_LABEL(sys_nis_syscall), C_LABEL(sys_setfsuid)
	.long C_LABEL(sys_setfsgid), C_LABEL(sys_llseek), C_LABEL(sys_time)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_stime), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_llseek)
	/* "We are the Knights of the Forest of Ni!!" */
	.long C_LABEL(sys_mlock), C_LABEL(sys_munlock), C_LABEL(sys_mlockall)
	.long C_LABEL(sys_munlockall), C_LABEL(sys_sched_setparam)
	.long C_LABEL(sys_sched_getparam), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_sched_get_priority_max), C_LABEL(sys_sched_get_priority_min)
	.long C_LABEL(sys_sched_rr_get_interval), C_LABEL(sys_nanosleep)
/*250*/	.long C_LABEL(sys_mremap)
	.long C_LABEL(sys_sysctl)
	.long C_LABEL(sys_getsid), C_LABEL(sys_fdatasync), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)

	/* Now the SunOS syscall table. */

	.align 4
	.globl C_LABEL(sunos_sys_table)
C_LABEL(sunos_sys_table):
/*0*/	.long C_LABEL(sunos_indir), C_LABEL(sys_exit), C_LABEL(sys_fork)
	.long C_LABEL(sunos_read), C_LABEL(sunos_write), C_LABEL(sunos_open)
	.long C_LABEL(sys_close), C_LABEL(sunos_wait4), C_LABEL(sys_creat)
	.long C_LABEL(sys_link), C_LABEL(sys_unlink), C_LABEL(sunos_execv)
	.long C_LABEL(sys_chdir), C_LABEL(sunos_nosys), C_LABEL(sys_mknod)
	.long C_LABEL(sys_chmod), C_LABEL(sys_chown), C_LABEL(sunos_brk)
	.long C_LABEL(sunos_nosys), C_LABEL(sys_lseek), C_LABEL(sunos_getpid)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_getuid), C_LABEL(sunos_nosys), C_LABEL(sys_ptrace)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sys_access), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sys_sync), C_LABEL(sys_kill), C_LABEL(sys_newstat)
	.long C_LABEL(sunos_nosys), C_LABEL(sys_newlstat), C_LABEL(sys_dup)
	.long C_LABEL(sys_pipe), C_LABEL(sunos_nosys), C_LABEL(sys_profil)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_getgid)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
/*50*/	.long C_LABEL(sunos_nosys), C_LABEL(sys_acct), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_mctl), C_LABEL(sunos_ioctl), C_LABEL(sys_reboot)
	.long C_LABEL(sunos_nosys), C_LABEL(sys_symlink), C_LABEL(sys_readlink)
	.long C_LABEL(sys_execve), C_LABEL(sys_umask), C_LABEL(sys_chroot)
	.long C_LABEL(sys_newfstat), C_LABEL(sunos_nosys), C_LABEL(sys_getpagesize)
	.long C_LABEL(sys_msync), C_LABEL(sys_vfork), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_sbrk), C_LABEL(sunos_sstk)
	.long C_LABEL(sunos_mmap), C_LABEL(sunos_vadvise), C_LABEL(sys_munmap)
	.long C_LABEL(sys_mprotect), C_LABEL(sunos_madvise), C_LABEL(sys_vhangup)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_mincore), C_LABEL(sys_getgroups)
	.long C_LABEL(sys_setgroups), C_LABEL(sys_getpgrp), C_LABEL(sunos_setpgrp)
	.long C_LABEL(sys_setitimer), C_LABEL(sunos_nosys), C_LABEL(sys_swapon)
	.long C_LABEL(sys_getitimer), C_LABEL(sys_gethostname), C_LABEL(sys_sethostname)
	.long C_LABEL(sunos_getdtablesize), C_LABEL(sys_dup2), C_LABEL(sunos_nop)
	.long C_LABEL(sys_fcntl), C_LABEL(sunos_select), C_LABEL(sunos_nop)
	.long C_LABEL(sys_fsync), C_LABEL(sys_setpriority), C_LABEL(sys_socket)
	.long C_LABEL(sys_connect), C_LABEL(sunos_accept)
/*100*/	.long C_LABEL(sys_getpriority), C_LABEL(sunos_send), C_LABEL(sunos_recv)
	.long C_LABEL(sunos_nosys), C_LABEL(sys_bind), C_LABEL(sunos_setsockopt)
	.long C_LABEL(sys_listen), C_LABEL(sunos_nosys), C_LABEL(sunos_sigaction)
	.long C_LABEL(sunos_sigblock), C_LABEL(sunos_sigsetmask), C_LABEL(sys_sigpause)
	.long C_LABEL(sys_sigstack), C_LABEL(sys_recvmsg), C_LABEL(sys_sendmsg)
	.long C_LABEL(sunos_nosys), C_LABEL(sys_gettimeofday), C_LABEL(sys_getrusage)
	.long C_LABEL(sunos_getsockopt), C_LABEL(sunos_nosys), C_LABEL(sunos_readv)
	.long C_LABEL(sunos_writev), C_LABEL(sys_settimeofday), C_LABEL(sys_fchown)
	.long C_LABEL(sys_fchmod), C_LABEL(sys_recvfrom), C_LABEL(sys_setreuid)
	.long C_LABEL(sys_setregid), C_LABEL(sys_rename), C_LABEL(sys_truncate)
	.long C_LABEL(sys_ftruncate), C_LABEL(sys_flock), C_LABEL(sunos_nosys)
	.long C_LABEL(sys_sendto), C_LABEL(sys_shutdown), C_LABEL(sys_socketpair)
	.long C_LABEL(sys_mkdir), C_LABEL(sys_rmdir), C_LABEL(sys_utimes)
	.long C_LABEL(sys_sigreturn), C_LABEL(sunos_nosys), C_LABEL(sys_getpeername)
	.long C_LABEL(sunos_gethostid), C_LABEL(sunos_nosys), C_LABEL(sys_getrlimit)
	.long C_LABEL(sys_setrlimit), C_LABEL(sunos_killpg), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
/*150*/	.long C_LABEL(sys_getsockname), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_poll), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_getdirentries), C_LABEL(sys_statfs), C_LABEL(sys_fstatfs)
	.long C_LABEL(sys_umount), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_getdomainname), C_LABEL(sys_setdomainname)
	.long C_LABEL(sunos_nosys), C_LABEL(sys_quotactl), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_mount), C_LABEL(sys_ustat), C_LABEL(sunos_semsys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_shmsys), C_LABEL(sunos_audit)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_getdents), C_LABEL(sys_setsid)
	.long C_LABEL(sys_fchdir), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sys_sigpending), C_LABEL(sunos_nosys)
	.long C_LABEL(sys_setpgid), C_LABEL(sunos_pathconf), C_LABEL(sunos_fpathconf)
	.long C_LABEL(sunos_sysconf), C_LABEL(sunos_uname), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
/*200*/	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
/*250*/	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys), C_LABEL(sunos_nosys)

#if 0
/* Not used yet - {net, open}bsd is a TODO */
	/* {net, open}bsd system call table. */

	.align	4
	.globl	C_LABEL(bsd_sys_table)
C_LABEL(bsd_sys_table):
	.long C_LABEL(sunos_nosys)/*SYSCALL*/, C_LABEL(sunos_nosys)/*EXIT*/
	.long C_LABEL(sunos_nosys)/*FORK*/, C_LABEL(sunos_nosys)/*READ*/
	.long C_LABEL(sunos_nosys)/*WRITE*/, C_LABEL(sunos_nosys)/*OPEN*/
	.long C_LABEL(sunos_nosys)/*CLOSE*/, C_LABEL(sunos_nosys)/*WAIT4*/
	.long C_LABEL(sunos_nosys)/*CREAT*/, C_LABEL(sunos_nosys)/*LINK*/
	.long C_LABEL(sunos_nosys)/*UNLINK*/, C_LABEL(sunos_nosys)/*EXECV*/
	.long C_LABEL(sunos_nosys)/*CHDIR*/, C_LABEL(sunos_nosys)/*FCHDIR*/
	.long C_LABEL(sunos_nosys)/*MKNOD*/, C_LABEL(sunos_nosys)/*CHMOD*/
	.long C_LABEL(sunos_nosys)/*CHOWN*/, C_LABEL(sunos_nosys)/*BREAK*/
	.long C_LABEL(sunos_nosys)/*GETFSSTAT*/, C_LABEL(sunos_nosys)/*OLSEEK*/
	.long C_LABEL(sunos_nosys)/*GETPID*/, C_LABEL(sunos_nosys)/*MOUNT*/
	.long C_LABEL(sunos_nosys)/*UNMOUNT*/, C_LABEL(sunos_nosys)/*SETUID*/
	.long C_LABEL(sunos_nosys)/*GETUID*/, C_LABEL(sunos_nosys)/*GETEUID*/
	.long C_LABEL(sunos_nosys)/*PTRACE*/, C_LABEL(sunos_nosys)/*RECVMSG*/
	.long C_LABEL(sunos_nosys)/*SENDMSG*/, C_LABEL(sunos_nosys)/*RECVFROM*/
	.long C_LABEL(sunos_nosys)/*ACCEPT*/, C_LABEL(sunos_nosys)/*GETPEERNAME*/
	.long C_LABEL(sunos_nosys)/*GETSOCKNAME*/, C_LABEL(sunos_nosys)/*ACCESS*/
	.long C_LABEL(sunos_nosys)/*CHFLAGS*/, C_LABEL(sunos_nosys)/*FCHFLAGS*/
	.long C_LABEL(sunos_nosys)/*SYNC*/, C_LABEL(sunos_nosys)/*KILL*/
	.long C_LABEL(sunos_nosys)/*OSTAT*/, C_LABEL(sunos_nosys)/*GETPPID*/
	.long C_LABEL(sunos_nosys)/*OLSTAT*/, C_LABEL(sunos_nosys)/*DUP*/
	.long C_LABEL(sunos_nosys)/*PIPE*/, C_LABEL(sunos_nosys)/*GETEGID*/
	.long C_LABEL(sunos_nosys)/*PROFIL*/, C_LABEL(sunos_nosys)/*KTRACE*/
	.long C_LABEL(sunos_nosys)/*SIGACTION*/, C_LABEL(sunos_nosys)/*GETGID*/
	.long C_LABEL(sunos_nosys)/*SIGPROCMASK*/, C_LABEL(sunos_nosys)/*GETLOGIN*/
	.long C_LABEL(sunos_nosys)/*SETLOGIN*/, C_LABEL(sunos_nosys)/*ACCT*/
	.long C_LABEL(sunos_nosys)/*SIGPENDING*/, C_LABEL(sunos_nosys)/*SIGALTSTACK*/
	.long C_LABEL(sunos_nosys)/*IOCTL*/, C_LABEL(sunos_nosys)/*REBOOT*/
	.long C_LABEL(sunos_nosys)/*REVOKE*/, C_LABEL(sunos_nosys)/*SYMLINK*/
	.long C_LABEL(sunos_nosys)/*READLINK*/, C_LABEL(sunos_nosys)/*EXECVE*/
	.long C_LABEL(sunos_nosys)/*UMASK*/, C_LABEL(sunos_nosys)/*CHROOT*/
	.long C_LABEL(sunos_nosys)/*OFSTAT*/, C_LABEL(sunos_nosys)/*OGETKERNINFO*/
	.long C_LABEL(sunos_nosys)/*OGETPAGESIZE*/, C_LABEL(sunos_nosys)/*MSYNC*/
	.long C_LABEL(sunos_nosys)/*VFORK*/, C_LABEL(sunos_nosys)/*VREAD*/
	.long C_LABEL(sunos_nosys)/*VWRITE*/, C_LABEL(sunos_nosys)/*SBRK*/
	.long C_LABEL(sunos_nosys)/*SSTK*/, C_LABEL(sunos_nosys)/*OMMAP*/
	.long C_LABEL(sunos_nosys)/*VADVISE*/, C_LABEL(sunos_nosys)/*MUNMAP*/
	.long C_LABEL(sunos_nosys)/*MPROTECT*/, C_LABEL(sunos_nosys)/*MADVISE*/
	.long C_LABEL(sunos_nosys)/*VHANGUP*/, C_LABEL(sunos_nosys)/*VLIMIT*/
	.long C_LABEL(sunos_nosys)/*MINCORE*/, C_LABEL(sunos_nosys)/*GETGROUPS*/
	.long C_LABEL(sunos_nosys)/*SETGROUPS*/, C_LABEL(sunos_nosys)/*GETPGRP*/
	.long C_LABEL(sunos_nosys)/*SETPGID*/, C_LABEL(sunos_nosys)/*SETITIMER*/
	.long C_LABEL(sunos_nosys)/*OWAIT*/, C_LABEL(sunos_nosys)/*SWAPON*/
	.long C_LABEL(sunos_nosys)/*GETITIMER*/, C_LABEL(sunos_nosys)/*OGETHOSTNAME*/
	.long C_LABEL(sunos_nosys)/*OSETHOSTNAME*/, C_LABEL(sunos_nosys)/*OGETDTABLESIZE*/
	.long C_LABEL(sunos_nosys)/*DUP2*/, C_LABEL(sunos_nosys)/*GETDOPT*/
	.long C_LABEL(sunos_nosys)/*FCNTL*/, C_LABEL(sunos_nosys)/*SELECT*/
	.long C_LABEL(sunos_nosys)/*SETDOPT*/, C_LABEL(sunos_nosys)/*FSYNC*/
	.long C_LABEL(sunos_nosys)/*SETPRIORITY*/, C_LABEL(sunos_nosys)/*SOCKET*/
	.long C_LABEL(sunos_nosys)/*CONNECT*/, C_LABEL(sunos_nosys)/*OACCEPT*/
	.long C_LABEL(sunos_nosys)/*GETPRIORITY*/, C_LABEL(sunos_nosys)/*OSEND*/
	.long C_LABEL(sunos_nosys)/*ORECV*/, C_LABEL(sunos_nosys)/*SIGRETURN*/
	.long C_LABEL(sunos_nosys)/*BIND*/, C_LABEL(sunos_nosys)/*SETSOCKOPT*/
	.long C_LABEL(sunos_nosys)/*LISTEN*/, C_LABEL(sunos_nosys)/*VTIMES*/
	.long C_LABEL(sunos_nosys)/*OSIGVEC*/, C_LABEL(sunos_nosys)/*OSIGBLOCK*/
	.long C_LABEL(sunos_nosys)/*OSIGSETMASK*/, C_LABEL(sunos_nosys)/*SIGSUSPEND*/
	.long C_LABEL(sunos_nosys)/*OSIGSTACK*/, C_LABEL(sunos_nosys)/*ORECVMSG*/
	.long C_LABEL(sunos_nosys)/*OSENDMSG*/, C_LABEL(sunos_nosys)/*VTRACE*/
	.long C_LABEL(sunos_nosys)/*GETTIMEOFDAY*/, C_LABEL(sunos_nosys)/*GETRUSAGE*/
	.long C_LABEL(sunos_nosys)/*GETSOCKOPT*/, C_LABEL(sunos_nosys)/*ORESUBA*/
	.long C_LABEL(sunos_nosys)/*READV*/, C_LABEL(sunos_nosys)/*WRITEV*/
	.long C_LABEL(sunos_nosys)/*SETTIMEOFDAY*/, C_LABEL(sunos_nosys)/*FCHOWN*/
	.long C_LABEL(sunos_nosys)/*FCHMOD*/, C_LABEL(sunos_nosys)/*ORECVFROM*/
	.long C_LABEL(sunos_nosys)/*OSETREUID*/, C_LABEL(sunos_nosys)/*OSETREGID*/
	.long C_LABEL(sunos_nosys)/*RENAME*/, C_LABEL(sunos_nosys)/*OTRUNCATE*/
	.long C_LABEL(sunos_nosys)/*OFTRUNCATE*/, C_LABEL(sunos_nosys)/*FLOCK*/
	.long C_LABEL(sunos_nosys)/*MKFIFO*/, C_LABEL(sunos_nosys)/*SENDTO*/
	.long C_LABEL(sunos_nosys)/*SHUTDOWN*/, C_LABEL(sunos_nosys)/*SOCKETPAIR*/
	.long C_LABEL(sunos_nosys)/*MKDIR*/, C_LABEL(sunos_nosys)/*RMDIR*/
	.long C_LABEL(sunos_nosys)/*UTIMES*/, C_LABEL(sunos_nosys)/*OSIGRETURN*/
	.long C_LABEL(sunos_nosys)/*ADJTIME*/, C_LABEL(sunos_nosys)/*OGETPEERNAME*/
	.long C_LABEL(sunos_nosys)/*OGETHOSTID*/, C_LABEL(sunos_nosys)/*OSETHOSTID*/
	.long C_LABEL(sunos_nosys)/*OGETRLIMIT*/, C_LABEL(sunos_nosys)/*OSETRLIMIT*/
	.long C_LABEL(sunos_nosys)/*OKILLPG*/, C_LABEL(sunos_nosys)/*SETSID*/
	.long C_LABEL(sunos_nosys)/*QUOTACTL*/, C_LABEL(sunos_nosys)/*OQUOTA*/
	.long C_LABEL(sunos_nosys)/*OGETSOCKNAME*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NFSSVC*/
	.long C_LABEL(sunos_nosys)/*OGETDIRENTRIES*/, C_LABEL(sunos_nosys)/*STATFS*/
	.long C_LABEL(sunos_nosys)/*FSTATFS*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*GETFH*/
	.long C_LABEL(sunos_nosys)/*OGETDOMAINNAME*/
	.long C_LABEL(sunos_nosys)/*OSETDOMAINNAME*/
	.long C_LABEL(sunos_nosys)/*OUNAME*/, C_LABEL(sunos_nosys)/*SYSARCH*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*OSEMSYS*/
	.long C_LABEL(sunos_nosys)/*OMSGSYS*/, C_LABEL(sunos_nosys)/*OSHMSYS*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*SETGID*/
	.long C_LABEL(sunos_nosys)/*SETEGID*/, C_LABEL(sunos_nosys)/*SETEUID*/
	.long C_LABEL(sunos_nosys)/*LFS_BMAPV*/, C_LABEL(sunos_nosys)/*LFS_MARKV*/
	.long C_LABEL(sunos_nosys)/*LFS_SEGCLEAN*/, C_LABEL(sunos_nosys)/*LFS_SEGWAIT*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*STAT*/, C_LABEL(sunos_nosys)/*FSTAT*/
	.long C_LABEL(sunos_nosys)/*LSTAT*/, C_LABEL(sunos_nosys)/*PATHCONF*/
	.long C_LABEL(sunos_nosys)/*FPATHCONF*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*GETRLIMIT*/, C_LABEL(sunos_nosys)/*SETRLIMIT*/
	.long C_LABEL(sunos_nosys)/*GETDIRENTRIES*/, C_LABEL(sunos_nosys)/*MMAP*/
	.long C_LABEL(sunos_nosys)/*__SYSCALL*/, C_LABEL(sunos_nosys)/*LSEEK*/
	.long C_LABEL(sunos_nosys)/*TRUNCATE*/, C_LABEL(sunos_nosys)/*FTRUNCATE*/
	.long C_LABEL(sunos_nosys)/*__SYSCTL*/, C_LABEL(sunos_nosys)/*MLOCK*/
	.long C_LABEL(sunos_nosys)/*MUNLOCK*/, C_LABEL(sunos_nosys)/*UNDELETE*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*LKMNOSYS*/, C_LABEL(sunos_nosys)/*LKMNOSYS*/
	.long C_LABEL(sunos_nosys)/*LKMNOSYS*/, C_LABEL(sunos_nosys)/*LKMNOSYS*/
	.long C_LABEL(sunos_nosys)/*LKMNOSYS*/, C_LABEL(sunos_nosys)/*LKMNOSYS*/
	.long C_LABEL(sunos_nosys)/*LKMNOSYS*/, C_LABEL(sunos_nosys)/*LKMNOSYS*/
	.long C_LABEL(sunos_nosys)/*LKMNOSYS*/, C_LABEL(sunos_nosys)/*LKMNOSYS*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*__SEMCTL*/, C_LABEL(sunos_nosys)/*SEMGET*/
	.long C_LABEL(sunos_nosys)/*SEMOP*/, C_LABEL(sunos_nosys)/*SEMCONFIG*/
	.long C_LABEL(sunos_nosys)/*MSGCTL*/, C_LABEL(sunos_nosys)/*MSGGET*/
	.long C_LABEL(sunos_nosys)/*MSGSND*/, C_LABEL(sunos_nosys)/*MSGRCV*/
	.long C_LABEL(sunos_nosys)/*SHMAT*/, C_LABEL(sunos_nosys)/*SHMCTL*/
	.long C_LABEL(sunos_nosys)/*SHMDT*/, C_LABEL(sunos_nosys)/*SHMGET*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*NOTHING*/, C_LABEL(sunos_nosys)/*NOTHING*/
	.long C_LABEL(sunos_nosys)/*MINHERIT*/, C_LABEL(sunos_nosys)/*RFORK*/
#endif

#if 0
/* Not needed - iBCS has its own */
	/* One thing left, Solaris syscall table, TODO */
	.globl	C_LABEL(solaris_sys_table)
C_LABEL(solaris_sys_table):
/*0*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_exit), C_LABEL(sys_fork)
	.long C_LABEL(sys_read), C_LABEL(sys_write)
/*5*/	.long C_LABEL(solaris_open), C_LABEL(sys_close), C_LABEL(sys_wait4)
	.long C_LABEL(sys_creat), C_LABEL(sys_link)
/*10*/	.long C_LABEL(sys_unlink), C_LABEL(sys_nis_syscall), C_LABEL(sys_chdir)
	.long C_LABEL(sys_time), C_LABEL(sys_mknod)
/*15*/	.long C_LABEL(sys_chmod), C_LABEL(sys_chown), C_LABEL(sys_brk)
	.long C_LABEL(sys_stat), C_LABEL(sys_lseek)
/*20*/	.long C_LABEL(sunos_getpid), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_setuid), C_LABEL(sunos_getuid)
/*25*/	.long C_LABEL(sys_stime), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_alarm), C_LABEL(sys_nis_syscall), C_LABEL(sys_pause)
/*30*/	.long C_LABEL(sys_utime), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_access), C_LABEL(sys_nice)
/*35*/	.long C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_sync), C_LABEL(sys_kill), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall)
/*40*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*45*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*50*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*55*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*60*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*65*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*70*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*75*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*80*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*85*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*90*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*95*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*100*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*105*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*110*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*115*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*120*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*125*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*130*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*135*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*140*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*145*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*150*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*155*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*160*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*165*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*170*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*175*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*180*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*185*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*190*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*195*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*200*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*205*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*210*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*215*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*220*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*225*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*230*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*235*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*240*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*245*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*250*/	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
	.long C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall), C_LABEL(sys_nis_syscall)
/*255*/	.long C_LABEL(sys_nis_syscall)
#endif
