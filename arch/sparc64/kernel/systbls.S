/* $Id: systbls.S,v 1.2 1997/03/18 17:59:03 jj Exp $
 * systbls.S: System call entry point tables for OS compatibility.
 *            The native Linux system call table lives here also.
 *
 * Copyright (C) 1995, 1996 David S. Miller (davem@caip.rutgers.edu)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */

	.data
	.align	8

	/* First, the 32-bit Linux native syscall table. */

	.globl sys_call_table32
sys_call_table32:
/*0*/	.xword sys_setup, sys_exit, sys_fork, sys_read, sys_write
/*5*/	.xword sys_open, sys_close, sys_wait4, sys_creat, sys_link
/*10*/  .xword sys_unlink, sunos_execv, sys_chdir, sys_nis_syscall, sys_mknod
/*15*/	.xword sys_chmod, sys_chown, sparc_brk, sys_nis_syscall, sys_lseek
/*20*/	.xword sys_getpid, sys_nis_syscall, sys_nis_syscall, sys_setuid, sys_getuid
/*25*/	.xword sys_time, sys_ptrace, sys_alarm, sys_nis_syscall, sys_pause
/*30*/	.xword sys_utime, sys_stty, sys_gtty, sys_access, sys_nice
	.xword sys_ftime, sys_sync, sys_kill, sys_newstat, sys_nis_syscall
/*40*/	.xword sys_newlstat, sys_dup, sys_pipe, sys_times, sys_profil
	.xword sys_nis_syscall, sys_setgid, sys_getgid, sys_signal, sys_geteuid
/*50*/	.xword sys_getegid, sys_acct, sys_nis_syscall, sys_nis_syscall, sys_ioctl
	.xword sys_reboot, sys_nis_syscall, sys_symlink, sys_readlink, sys_execve
/*60*/	.xword sys_umask, sys_chroot, sys_newfstat, sys_nis_syscall, sys_getpagesize
	.xword sys_nis_syscall, sys_vfork, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*70*/	.xword sys_nis_syscall, sys_mmap, sys_nis_syscall, sys_munmap, sys_mprotect
	.xword sys_nis_syscall, sys_vhangup, sys_nis_syscall, sys_nis_syscall, sys_getgroups
/*80*/	.xword sys_setgroups, sys_getpgrp, sys_nis_syscall, sys_setitimer, sys_nis_syscall
	.xword sys_swapon, sys_getitimer, sys_nis_syscall, sys_sethostname, sys_nis_syscall
/*90*/	.xword sys_dup2, sys_nis_syscall, sys_fcntl, sys_select, sys_nis_syscall
	.xword sys_fsync, sys_setpriority, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*100*/	.xword sys_getpriority, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*110*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_gettimeofday, sys_getrusage, sys_nis_syscall, sys_nis_syscall
/*120*/	.xword sys_readv, sys_writev, sys_settimeofday, sys_fchown, sys_fchmod
	.xword sys_nis_syscall, sys_setreuid, sys_setregid, sys_rename, sys_truncate
/*130*/	.xword sys_ftruncate, sys_flock, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_mkdir, sys_rmdir, sys_nis_syscall, sys_nis_syscall
/*140*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_getrlimit
	.xword sys_setrlimit, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*150*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_nis_syscall, sys_statfs, sys_fstatfs, sys_umount
/*160*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_setdomainname, sys_nis_syscall
	.xword sys_quotactl, sys_nis_syscall, sys_mount, sys_ustat, sys_nis_syscall
/*170*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_getdents
	.xword sys_setsid, sys_fchdir, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*180*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_sigpending, sys_nis_syscall
	.xword sys_setpgid, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_newuname
/*190*/	.xword sys_init_module, sys_personality, sys_prof, sys_break, sys_lock
	.xword sys_mpx, sys_ulimit, sys_getppid, sparc_sigaction, sys_sgetmask
/*200*/	.xword sys_ssetmask, sys_sigsuspend, sys_newlstat, sys_uselib, old_readdir
	.xword sys_nis_syscall, sys_socketcall, sys_syslog, sys_olduname, sys_nis_syscall
/*210*/	.xword sys_idle, sys_nis_syscall, sys_waitpid, sys_swapoff, sys_sysinfo
	.xword sys_ipc, sys_sigreturn, sys_clone, sys_nis_syscall, sys_adjtimex
/*220*/	.xword sys_sigprocmask, sys_create_module, sys_delete_module, sys_get_kernel_syms, sys_getpgid
	.xword sys_bdflush, sys_sysfs, sys_nis_syscall, sys_setfsuid, sys_setfsgid
/*230*/	.xword sys_llseek, sys_time, sys_nis_syscall, sys_stime, sys_nis_syscall
	.xword sys_nis_syscall, sys_llseek, sys_mlock, sys_munlock, sys_mlockall
/*240*/	.xword sys_munlockall, sys_sched_setparam, sys_sched_getparam, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_sched_get_priority_max, sys_sched_get_priority_min, sys_sched_rr_get_interval, sys_nanosleep
/*250*/	.xword sys_mremap, sys_sysctl, sys_getsid, sys_fdatasync, sys_nis_syscall
	.xword sys_aplib, sys_nis_syscall

	/* Now the 64-bit native Linux syscall table. */

	.globl sys_call_table64
sys_call_table64:
/*0*/	.xword sys_setup, sys_exit, sys_fork, sys_read, sys_write
/*5*/	.xword sys_open, sys_close, sys_wait4, sys_creat, sys_link
/*10*/  .xword sys_unlink, sunos_execv, sys_chdir, sys_nis_syscall, sys_mknod
/*15*/	.xword sys_chmod, sys_chown, sparc_brk, sys_nis_syscall, sys_lseek
/*20*/	.xword sys_getpid, sys_nis_syscall, sys_nis_syscall, sys_setuid, sys_getuid
/*25*/	.xword sys_time, sys_ptrace, sys_alarm, sys_nis_syscall, sys_pause
/*30*/	.xword sys_utime, sys_stty, sys_gtty, sys_access, sys_nice
	.xword sys_ftime, sys_sync, sys_kill, sys_newstat, sys_nis_syscall
/*40*/	.xword sys_newlstat, sys_dup, sys_pipe, sys_times, sys_profil
	.xword sys_nis_syscall, sys_setgid, sys_getgid, sys_signal, sys_geteuid
/*50*/	.xword sys_getegid, sys_acct, sys_nis_syscall, sys_nis_syscall, sys_ioctl
	.xword sys_reboot, sys_nis_syscall, sys_symlink, sys_readlink, sys_execve
/*60*/	.xword sys_umask, sys_chroot, sys_newfstat, sys_nis_syscall, sys_getpagesize
	.xword sys_nis_syscall, sys_vfork, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*70*/	.xword sys_nis_syscall, sys_mmap, sys_nis_syscall, sys_munmap, sys_mprotect
	.xword sys_nis_syscall, sys_vhangup, sys_nis_syscall, sys_nis_syscall, sys_getgroups
/*80*/	.xword sys_setgroups, sys_getpgrp, sys_nis_syscall, sys_setitimer, sys_nis_syscall
	.xword sys_swapon, sys_getitimer, sys_nis_syscall, sys_sethostname, sys_nis_syscall
/*90*/	.xword sys_dup2, sys_nis_syscall, sys_fcntl, sys_select, sys_nis_syscall
	.xword sys_fsync, sys_setpriority, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*100*/	.xword sys_getpriority, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*110*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_gettimeofday, sys_getrusage, sys_nis_syscall, sys_nis_syscall
/*120*/	.xword sys_readv, sys_writev, sys_settimeofday, sys_fchown, sys_fchmod
	.xword sys_nis_syscall, sys_setreuid, sys_setregid, sys_rename, sys_truncate
/*130*/	.xword sys_ftruncate, sys_flock, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_mkdir, sys_rmdir, sys_nis_syscall, sys_nis_syscall
/*140*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_getrlimit
	.xword sys_setrlimit, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*150*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_nis_syscall, sys_statfs, sys_fstatfs, sys_umount
/*160*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_setdomainname, sys_nis_syscall
	.xword sys_quotactl, sys_nis_syscall, sys_mount, sys_ustat, sys_nis_syscall
/*170*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_getdents
	.xword sys_setsid, sys_fchdir, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*180*/	.xword sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_sigpending, sys_nis_syscall
	.xword sys_setpgid, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_newuname
/*190*/	.xword sys_init_module, sys_personality, sys_prof, sys_break, sys_lock
	.xword sys_mpx, sys_ulimit, sys_getppid, sparc_sigaction, sys_sgetmask
/*200*/	.xword sys_ssetmask, sys_sigsuspend, sys_newlstat, sys_uselib, old_readdir
	.xword sys_nis_syscall, sys_socketcall, sys_syslog, sys_olduname, sys_nis_syscall
/*210*/	.xword sys_idle, sys_nis_syscall, sys_waitpid, sys_swapoff, sys_sysinfo
	.xword sys_ipc, sys_sigreturn, sys_clone, sys_nis_syscall, sys_adjtimex
/*220*/	.xword sys_sigprocmask, sys_create_module, sys_delete_module, sys_get_kernel_syms, sys_getpgid
	.xword sys_bdflush, sys_sysfs, sys_nis_syscall, sys_setfsuid, sys_setfsgid
/*230*/	.xword sys_llseek, sys_time, sys_nis_syscall, sys_stime, sys_nis_syscall
	.xword sys_nis_syscall, sys_llseek, sys_mlock, sys_munlock, sys_mlockall
/*240*/	.xword sys_munlockall, sys_sched_setparam, sys_sched_getparam, sys_nis_syscall, sys_nis_syscall
	.xword sys_nis_syscall, sys_sched_get_priority_max, sys_sched_get_priority_min, sys_sched_rr_get_interval, sys_nanosleep
/*250*/	.xword sys_mremap, sys_sysctl, sys_getsid, sys_fdatasync, sys_nis_syscall
	.xword sys_aplib, sys_nis_syscall

	/* Now the 32-bit SunOS syscall table. */

	.align 4
	.globl sunos_sys_table
sunos_sys_table:
/*0*/	.xword sunos_indir, sys_exit, sys_fork
	.xword sunos_read, sunos_write, sunos_open
	.xword sys_close, sunos_wait4, sys_creat
	.xword sys_link, sys_unlink, sunos_execv
	.xword sys_chdir, sunos_nosys, sys_mknod
	.xword sys_chmod, sys_chown, sunos_brk
	.xword sunos_nosys, sys_lseek, sunos_getpid
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_getuid, sunos_nosys, sys_ptrace
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sys_access, sunos_nosys, sunos_nosys
	.xword sys_sync, sys_kill, sys_newstat
	.xword sunos_nosys, sys_newlstat, sys_dup
	.xword sys_pipe, sunos_nosys, sys_profil
	.xword sunos_nosys, sunos_nosys, sunos_getgid
	.xword sunos_nosys, sunos_nosys
/*50*/	.xword sunos_nosys, sys_acct, sunos_nosys
	.xword sunos_mctl, sunos_ioctl, sys_reboot
	.xword sunos_nosys, sys_symlink, sys_readlink
	.xword sys_execve, sys_umask, sys_chroot
	.xword sys_newfstat, sunos_nosys, sys_getpagesize
	.xword sys_msync, sys_vfork, sunos_nosys
	.xword sunos_nosys, sunos_sbrk, sunos_sstk
	.xword sunos_mmap, sunos_vadvise, sys_munmap
	.xword sys_mprotect, sunos_madvise, sys_vhangup
	.xword sunos_nosys, sunos_mincore, sys_getgroups
	.xword sys_setgroups, sys_getpgrp, sunos_setpgrp
	.xword sys_setitimer, sunos_nosys, sys_swapon
	.xword sys_getitimer, sys_gethostname, sys_sethostname
	.xword sunos_getdtablesize, sys_dup2, sunos_nop
	.xword sys_fcntl, sunos_select, sunos_nop
	.xword sys_fsync, sys_setpriority, sys_socket
	.xword sys_connect, sunos_accept
/*100*/	.xword sys_getpriority, sunos_send, sunos_recv
	.xword sunos_nosys, sys_bind, sunos_setsockopt
	.xword sys_listen, sunos_nosys, sunos_sigaction
	.xword sunos_sigblock, sunos_sigsetmask, sys_sigpause
	.xword sys_sigstack, sys_recvmsg, sys_sendmsg
	.xword sunos_nosys, sys_gettimeofday, sys_getrusage
	.xword sunos_getsockopt, sunos_nosys, sunos_readv
	.xword sunos_writev, sys_settimeofday, sys_fchown
	.xword sys_fchmod, sys_recvfrom, sys_setreuid
	.xword sys_setregid, sys_rename, sys_truncate
	.xword sys_ftruncate, sys_flock, sunos_nosys
	.xword sys_sendto, sys_shutdown, sys_socketpair
	.xword sys_mkdir, sys_rmdir, sys_utimes
	.xword sys_sigreturn, sunos_nosys, sys_getpeername
	.xword sunos_gethostid, sunos_nosys, sys_getrlimit
	.xword sys_setrlimit, sunos_killpg, sunos_nosys
	.xword sunos_nosys, sunos_nosys
/*150*/	.xword sys_getsockname, sunos_nosys, sunos_nosys
	.xword sunos_poll, sunos_nosys, sunos_nosys
	.xword sunos_getdirentries, sys_statfs, sys_fstatfs
	.xword sys_umount, sunos_nosys, sunos_nosys
	.xword sunos_getdomainname, sys_setdomainname
	.xword sunos_nosys, sys_quotactl, sunos_nosys
	.xword sunos_mount, sys_ustat, sunos_semsys
	.xword sunos_nosys, sunos_shmsys, sunos_audit
	.xword sunos_nosys, sunos_getdents, sys_setsid
	.xword sys_fchdir, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sys_sigpending, sunos_nosys
	.xword sys_setpgid, sunos_pathconf, sunos_fpathconf
	.xword sunos_sysconf, sunos_uname, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
/*200*/	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys
/*250*/	.xword sunos_nosys, sunos_nosys, sunos_nosys
	.xword sunos_nosys, sunos_nosys, sys_aplib
