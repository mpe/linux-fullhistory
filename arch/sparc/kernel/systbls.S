/* $Id: systbls.S,v 1.22 1995/11/25 00:58:42 davem Exp $
 * systbls.S: System call entry point tables for OS compatability.
 *            The native Linux system call table lives here also.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */

#include <asm/cprefix.h>

	.data
	.align 4

	/* First, the Linux native syscall table. */

	.globl C_LABEL(sys_call_table)
C_LABEL(sys_call_table):
	.long C_LABEL(sys_setup)		/* 0 */
	.long C_LABEL(sys_exit)
	.long C_LABEL(sys_fork)
	.long C_LABEL(sys_read)
	.long C_LABEL(sys_write)
	.long C_LABEL(sys_open)			/* 5 */
	.long C_LABEL(sys_close)
	.long C_LABEL(sys_wait4)
	.long C_LABEL(sys_creat)
	.long C_LABEL(sys_link)
	.long C_LABEL(sys_unlink)		/* 10 */
	.long C_LABEL(sunos_execv)
	.long C_LABEL(sys_chdir)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_mknod)
	.long C_LABEL(sys_chmod)		/* 15 */
	.long C_LABEL(sys_chown)
	.long C_LABEL(sys_brk)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_lseek)
	.long C_LABEL(sys_getpid)		/* 20 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_setuid)
	.long C_LABEL(sys_getuid)
	.long C_LABEL(sys_ni_syscall)		/* 25 */
	.long C_LABEL(sys_ni_syscall)		/* this will be sys_ptrace() */
	.long C_LABEL(sys_alarm)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_pause)
	.long C_LABEL(sys_utime)		/* 30 */
	.long C_LABEL(sys_stty)
	.long C_LABEL(sys_gtty)
	.long C_LABEL(sys_access)
	.long C_LABEL(sys_nice)
	.long C_LABEL(sys_ftime)		/* 35 */
	.long C_LABEL(sys_sync)
	.long C_LABEL(sys_kill)
	.long C_LABEL(sys_newstat)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_newlstat)		/* 40 */
	.long C_LABEL(sys_dup)
	.long C_LABEL(sys_pipe)
	.long C_LABEL(sys_times)
	.long C_LABEL(sys_profil)
	.long C_LABEL(sys_ni_syscall)		/* 45 */
	.long C_LABEL(sys_setgid)
	.long C_LABEL(sys_getgid)
	.long C_LABEL(sys_signal)
	.long C_LABEL(sys_geteuid)
	.long C_LABEL(sys_getegid)		/* 50 */
	.long C_LABEL(sys_acct)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ioctl)
	.long C_LABEL(sys_reboot)		/* 55 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_symlink)
	.long C_LABEL(sys_readlink)
	.long C_LABEL(sys_execve)
	.long C_LABEL(sys_umask)		/* 60 */
	.long C_LABEL(sys_chroot)
	.long C_LABEL(sys_newfstat)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_getpagesize)
	.long C_LABEL(sys_ni_syscall)		/* 65 */
	.long C_LABEL(sys_vfork)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 70 */
	.long C_LABEL(sunos_mmap)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_munmap)
	.long C_LABEL(sys_mprotect)
	.long C_LABEL(sys_ni_syscall)		/* 75 */
	.long C_LABEL(sys_vhangup)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_getgroups)
	.long C_LABEL(sys_setgroups)		/* 80 */
	.long C_LABEL(sys_getpgrp)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_setitimer)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_swapon)		/* 85 */
	.long C_LABEL(sys_getitimer)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_sethostname)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_dup2)			/* 90 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_fcntl)
	.long C_LABEL(sys_select)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_fsync)		/* 95 */
	.long C_LABEL(sys_setpriority)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_getpriority)		/* 100 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 105 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 110 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 115 */
	.long C_LABEL(sys_gettimeofday)
	.long C_LABEL(sys_getrusage)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 120 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_settimeofday)
	.long C_LABEL(sys_fchown)
	.long C_LABEL(sys_fchmod)
	.long C_LABEL(sys_ni_syscall)		/* 125 */
	.long C_LABEL(sys_setreuid)
	.long C_LABEL(sys_setregid)
	.long C_LABEL(sys_rename)
	.long C_LABEL(sys_truncate)
	.long C_LABEL(sys_ftruncate)		/* 130 */
	.long C_LABEL(sys_flock)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 135 */
	.long C_LABEL(sys_mkdir)
	.long C_LABEL(sys_rmdir)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 140 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_getrlimit)
	.long C_LABEL(sys_setrlimit)		/* 145 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 150 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 155 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_statfs)
	.long C_LABEL(sys_fstatfs)
	.long C_LABEL(sys_umount)
	.long C_LABEL(sys_ni_syscall)		/* 160 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_setdomainname)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* sys_quotactl -- 165 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_mount)
	.long C_LABEL(sys_ustat)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 170 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_getdents)
	.long C_LABEL(sys_setsid)		/* 175 */
	.long C_LABEL(sys_fchdir)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 180 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_sigpending)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_setpgid)		/* 185 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_uname)
	.long C_LABEL(sys_init_module)		/* 190 */
	.long C_LABEL(sys_personality)
	.long C_LABEL(sys_prof)
	.long C_LABEL(sys_break)
	.long C_LABEL(sys_lock)
	.long C_LABEL(sys_mpx)			/* 195 */
	.long C_LABEL(sys_ulimit)
	.long C_LABEL(sys_getppid)
	.long C_LABEL(sys_sigaction)
	.long C_LABEL(sys_sgetmask)
	.long C_LABEL(sys_ssetmask)		/* 200 */
	.long C_LABEL(sys_sigsuspend)
	.long C_LABEL(sys_newlstat)
	.long C_LABEL(sys_uselib)
	.long C_LABEL(old_readdir)
	.long C_LABEL(sys_ni_syscall)		/* 205 */
	.long C_LABEL(sys_socketcall)
	.long C_LABEL(sys_syslog)
	.long C_LABEL(sys_olduname)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_idle)			/* 210 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_waitpid)
	.long C_LABEL(sys_swapoff)
	.long C_LABEL(sys_sysinfo)
	.long C_LABEL(sys_ipc)			/* 215 */
	.long C_LABEL(sys_sigreturn)
	.long C_LABEL(sys_clone)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_adjtimex)
	.long C_LABEL(sys_sigprocmask)		/* 220 */
	.long C_LABEL(sys_create_module)
	.long C_LABEL(sys_delete_module)
	.long C_LABEL(sys_get_kernel_syms)
	.long C_LABEL(sys_getpgid)
	.long C_LABEL(sys_bdflush)		/* 225 */
	.long C_LABEL(sys_sysfs)
	.long C_LABEL(sys_ni_syscall)		/* Andrew Filesystem Syscall */
	.long C_LABEL(sys_setfsuid)
	.long C_LABEL(sys_setfsgid)
	.long C_LABEL(sys_llseek)		/* 230 Should be newselect... */
	.long C_LABEL(sys_time)
	.long C_LABEL(sys_ni_syscall) 		/* Should be oldstat... */
	.long C_LABEL(sys_stime)
	.long C_LABEL(sys_ni_syscall)           /* Should be oldfstat */
	.long C_LABEL(sys_ni_syscall)		/* 235 - sys_phys */
	.long C_LABEL(sys_llseek)

	/* "We are the Knights of the Forest of Ni!!" */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 240 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 245 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 250 */
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)
	.long C_LABEL(sys_ni_syscall)		/* 255 */

	/* Now the SunOS syscall table. */

	.align 4
	.globl C_LABEL(sunos_sys_table)
C_LABEL(sunos_sys_table):
	.long C_LABEL(sunos_indir)		/* so stupid... */
	.long C_LABEL(sys_exit)
	.long C_LABEL(sys_fork)
	.long C_LABEL(sys_read)
	.long C_LABEL(sys_write)
	.long C_LABEL(sys_open)			/* 5 */
	.long C_LABEL(sys_close)
	.long C_LABEL(sunos_wait4)		/* broken... */
	.long C_LABEL(sys_creat)
	.long C_LABEL(sys_link)
	.long C_LABEL(sys_unlink)		/* 10 */
	.long C_LABEL(sunos_execv)
	.long C_LABEL(sys_chdir)
	.long C_LABEL(sunos_nosys)		/* Obsolete sys_time(), unused */
	.long C_LABEL(sys_mknod)
	.long C_LABEL(sys_chmod)		/* 15 */
	.long C_LABEL(sys_chown)
	.long C_LABEL(sunos_brk)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_lseek)
	.long C_LABEL(sunos_getpid)		/* 20 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_getuid)
	.long C_LABEL(sunos_nosys)		/* 25 */
	.long C_LABEL(sunos_nosys)		/* this will be sys_ptrace() */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* Old SunOS fstat, unsupported */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 30 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_access)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 35 */
	.long C_LABEL(sys_sync)
	.long C_LABEL(sys_kill)
	.long C_LABEL(sys_newstat)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_newlstat)		/* 40 */
	.long C_LABEL(sys_dup)
	.long C_LABEL(sys_pipe)
	.long C_LABEL(sunos_nosys)		/* SunOS sys_times() */
	.long C_LABEL(sys_profil)
	.long C_LABEL(sunos_nosys)		/* 45 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_getgid)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 50 */
	.long C_LABEL(sys_acct)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_mctl)
	.long C_LABEL(sunos_ioctl)
	.long C_LABEL(sys_reboot)		/* 55 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_symlink)
	.long C_LABEL(sys_readlink)
	.long C_LABEL(sys_execve)
	.long C_LABEL(sys_umask)		/* 60 */
	.long C_LABEL(sys_chroot)
	.long C_LABEL(sys_newfstat)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_getpagesize)
	.long C_LABEL(sunos_msync)		/* 65 */
	.long C_LABEL(sys_vfork)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_sbrk)
	.long C_LABEL(sunos_sstk)		/* 70 */
	.long C_LABEL(sunos_mmap)
	.long C_LABEL(sunos_vadvise)
	.long C_LABEL(sys_munmap)
	.long C_LABEL(sys_mprotect)
	.long C_LABEL(sunos_madvise)		/* 75 */
	.long C_LABEL(sys_vhangup)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_mincore)
	.long C_LABEL(sys_getgroups)
	.long C_LABEL(sys_setgroups)		/* 80 */
	.long C_LABEL(sys_getpgrp)
	.long C_LABEL(sunos_setpgrp)
	.long C_LABEL(sys_setitimer)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_swapon)		/* 85 */
	.long C_LABEL(sys_getitimer)
	.long C_LABEL(sys_gethostname)
	.long C_LABEL(sys_sethostname)
	.long C_LABEL(sunos_getdtablesize)
	.long C_LABEL(sys_dup2)			/* 90 */
	.long C_LABEL(sunos_nop)		/* getdopt() does nothing */
	.long C_LABEL(sys_fcntl)
	.long C_LABEL(sys_select)
	.long C_LABEL(sunos_nop)		/* setdopt() also does nothing */
	.long C_LABEL(sys_fsync)		/* 95 */
	.long C_LABEL(sys_setpriority)
	.long C_LABEL(sys_socket)
	.long C_LABEL(sys_connect)
	.long C_LABEL(sys_accept)
	.long C_LABEL(sys_getpriority)		/* 100 */
	.long C_LABEL(sys_send)
	.long C_LABEL(sys_recv)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_bind)
	.long C_LABEL(sys_setsockopt)		/* 105 */
	.long C_LABEL(sys_listen)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_sigaction)		/* sigvec */
	.long C_LABEL(sunos_sigblock)		/* sigblock */
	.long C_LABEL(sys_ssetmask)		/* sigsetmask -- 110 */
	.long C_LABEL(sys_sigpause)		/* sigpause */
	.long C_LABEL(sys_sigstack)		/* sigstack */
	.long C_LABEL(sunos_nosys)		/* recvmsg */
	.long C_LABEL(sunos_nosys)		/* sendmsg */
	.long C_LABEL(sunos_nosys)		/* vtrace -- 115 */
	.long C_LABEL(sys_gettimeofday)
	.long C_LABEL(sys_getrusage)
	.long C_LABEL(sys_getsockopt)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_readv)		/* 120 */
	.long C_LABEL(sys_writev)
	.long C_LABEL(sys_settimeofday)
	.long C_LABEL(sys_fchown)
	.long C_LABEL(sys_fchmod)
	.long C_LABEL(sys_recvfrom)		/* recvfrom -- 125 */
	.long C_LABEL(sys_setreuid)
	.long C_LABEL(sys_setregid)
	.long C_LABEL(sys_rename)
	.long C_LABEL(sys_truncate)
	.long C_LABEL(sys_ftruncate)		/* 130 */
	.long C_LABEL(sys_flock)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_sendto)		/* sendto */
	.long C_LABEL(sys_shutdown)		/* shutdown */
	.long C_LABEL(sunos_nosys)		/* socketpair -- 135 */
	.long C_LABEL(sys_mkdir)
	.long C_LABEL(sys_rmdir)
	.long C_LABEL(sys_utimes)		/* utimes */
	.long C_LABEL(sys_sigreturn)
	.long C_LABEL(sunos_nosys)		/* adjtime -- 140 */
	.long C_LABEL(sys_getpeername)		/* getpeername */
	.long C_LABEL(sunos_nosys)		/* gethostid */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_getrlimit)
	.long C_LABEL(sys_setrlimit)		/* 145 */
	.long C_LABEL(sunos_killpg)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_getsockname)		/* getsockname -- 150 */
	.long C_LABEL(sunos_nosys)		/* getmsg */
	.long C_LABEL(sunos_nosys)		/* putmsg */
	.long C_LABEL(sunos_nosys)		/* poll */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* nfssvc -- 155 */
	.long C_LABEL(sunos_nosys)		/* getdirectries */
	.long C_LABEL(sys_statfs)
	.long C_LABEL(sys_fstatfs)
	.long C_LABEL(sys_umount)
	.long C_LABEL(sunos_nosys)		/* async_daemon -- 160 */
	.long C_LABEL(sunos_nosys)		/* getfh */
	.long C_LABEL(sunos_getdomainname)	/* getdomainname */
	.long C_LABEL(sys_setdomainname)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* quotactl -- 165 */
	.long C_LABEL(sunos_nosys)		/* exportfs */
	.long C_LABEL(sunos_mount)
	.long C_LABEL(sys_ustat)
	.long C_LABEL(sunos_nosys)		/* semsys */
	.long C_LABEL(sunos_nosys)		/* msgsys -- 170 */
	.long C_LABEL(sunos_nosys)		/* shmsys */
	.long C_LABEL(sunos_audit)		/* auditsys */
	.long C_LABEL(sunos_nosys)		/* rfssys */
	.long C_LABEL(sunos_getdents)
	.long C_LABEL(sys_setsid)		/* 175 */
	.long C_LABEL(sys_fchdir)
	.long C_LABEL(sunos_nosys)		/* fchroot */
	.long C_LABEL(sunos_nosys)		/* vpixsys ???XXX */
	.long C_LABEL(sunos_nosys)		/* aioread */
	.long C_LABEL(sunos_nosys)		/* aiowrite -- 180 */
	.long C_LABEL(sunos_nosys)		/* aiowait */
	.long C_LABEL(sunos_nosys)		/* aiocancel */
	.long C_LABEL(sys_sigpending)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sys_setpgid)		/* 185 */
	.long C_LABEL(sunos_pathconf)		/* pathconf */
	.long C_LABEL(sunos_fpathconf)		/* fpathconf */
	.long C_LABEL(sunos_nosys)		/* sysconf */
	.long C_LABEL(sunos_uname)
	.long C_LABEL(sunos_nosys)		/* 190 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 195 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 200 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 205 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 210 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 215 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 220 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 225 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 230 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 235 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 240 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 245 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 250 */
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)
	.long C_LABEL(sunos_nosys)		/* 255 */
