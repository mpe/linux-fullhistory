/* $Id: systbls.S,v 1.24 1997/08/22 20:12:06 davem Exp $
 * systbls.S: System call entry point tables for OS compatibility.
 *            The native Linux system call table lives here also.
 *
 * Copyright (C) 1995, 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */

	.text
	.align	1024

	/* First, the 32-bit Linux native syscall table. */

	.globl sys_call_table32
sys_call_table32:
/*0*/	.word sys_setup, sys_exit, sys_fork, sys32_read, sys32_write
/*5*/	.word sys32_open, sys_close, sys32_wait4, sys32_creat, sys32_link
/*10*/  .word sys32_unlink, sunos_execv, sys32_chdir, sys_nis_syscall, sys32_mknod
/*15*/	.word sys32_chmod, sys32_chown, sparc32_brk, sys_nis_syscall, sys32_lseek
/*20*/	.word sys_getpid, sys_nis_syscall, sys_nis_syscall, sys_setuid, sys_getuid
/*25*/	.word sys32_time, sys_ptrace, sys_alarm, sys_nis_syscall, sys_pause
/*30*/	.word sys32_utime, sys_stty, sys_gtty, sys32_access, sys_nice
	.word sys_ftime, sys_sync, sys_kill, sys32_newstat, sys_nis_syscall
/*40*/	.word sys32_newlstat, sys_dup, sys_pipe, sys32_times, sys_profil
	.word sys_nis_syscall, sys_setgid, sys_getgid, sys32_signal, sys_geteuid
/*50*/	.word sys_getegid, sys32_acct, sys_nis_syscall, sys_nis_syscall, sys32_ioctl
	.word sys32_reboot, sys_nis_syscall, sys32_symlink, sys32_readlink, sys32_execve
/*60*/	.word sys_umask, sys32_chroot, sys32_newfstat, sys_nis_syscall, sys_getpagesize
	.word sys32_msync, sys_vfork, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*70*/	.word sys_nis_syscall, sys32_mmap, sys_nis_syscall, sys32_munmap, sys32_mprotect
	.word sys_nis_syscall, sys_vhangup, sys_nis_syscall, sys_nis_syscall, sys32_getgroups
/*80*/	.word sys32_setgroups, sys_getpgrp, sys_nis_syscall, sys32_setitimer, sys_nis_syscall
	.word sys32_swapon, sys32_getitimer, sys_nis_syscall, sys32_sethostname, sys_nis_syscall
/*90*/	.word sys_dup2, sys_nis_syscall, sys32_fcntl, sys32_select, sys_nis_syscall
	.word sys_fsync, sys_setpriority, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*100*/	.word sys_getpriority, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*110*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.word sys_nis_syscall, sys32_gettimeofday, sys32_getrusage, sys_nis_syscall, sys_nis_syscall
/*120*/	.word sys32_readv, sys32_writev, sys32_settimeofday, sys_fchown, sys_fchmod
	.word sys_nis_syscall, sys32_setreuid, sys_setregid, sys32_rename, sys32_truncate
/*130*/	.word sys32_ftruncate, sys_flock, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.word sys_nis_syscall, sys32_mkdir, sys32_rmdir, sys_nis_syscall, sys_nis_syscall
/*140*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys32_getrlimit
	.word sys32_setrlimit, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*150*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.word sys_nis_syscall, sys_nis_syscall, sys32_statfs, sys32_fstatfs, sys32_umount
/*160*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys32_setdomainname, sys_nis_syscall
	.word sys32_quotactl, sys_nis_syscall, sys32_mount, sys32_ustat, sys_nis_syscall
/*170*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys32_getdents
	.word sys_setsid, sys_fchdir, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*180*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys32_sigpending, sys32_query_module
	.word sys_setpgid, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys32_newuname
/*190*/	.word sys32_init_module, sys32_personality, sys_prof, sys_break, sys_lock
	.word sys_mpx, sys_ulimit, sys_getppid, sparc32_sigaction, sys_sgetmask
/*200*/	.word sys_ssetmask, sys_sigsuspend, sys32_newlstat, sys32_uselib, old32_readdir
	.word sys_nis_syscall, sys32_socketcall, sys32_syslog, sys32_olduname, sys_nis_syscall
/*210*/	.word sys_idle, sys_nis_syscall, sys32_waitpid, sys32_swapoff, sys32_sysinfo
	.word sys32_ipc, sys_sigreturn, sys_clone, sys_nis_syscall, sys32_adjtimex
/*220*/	.word sys32_sigprocmask, sys32_create_module, sys32_delete_module, sys32_get_kernel_syms, sys_getpgid
	.word sys32_bdflush, sys32_sysfs, sys_nis_syscall, sys_setfsuid, sys_setfsgid
/*230*/	.word sys32_llseek, sys32_time, sys_nis_syscall, sys_stime, sys_nis_syscall
	.word sys_nis_syscall, sys32_llseek, sys32_mlock, sys32_munlock, sys_mlockall
/*240*/	.word sys_munlockall, sys32_sched_setparam, sys32_sched_getparam, sys_nis_syscall, sys_nis_syscall
	.word sys_nis_syscall, sys_sched_get_priority_max, sys_sched_get_priority_min, sys32_sched_rr_get_interval, sys32_nanosleep
/*250*/	.word sys32_mremap, sys_sysctl, sys_getsid, sys_fdatasync, sys32_nfsservctl
	.word sys_aplib

	/* Now the 64-bit native Linux syscall table. */

	.align	1024
	.globl sys_call_table64, sys_call_table
sys_call_table64:
sys_call_table:
/*0*/	.word sys_setup, sys_exit, sys_fork, sys_read, sys_write
/*5*/	.word sys_open, sys_close, sys_wait4, sys_creat, sys_link
/*10*/  .word sys_unlink, sunos_execv, sys_chdir, sys_nis_syscall, sys_mknod
/*15*/	.word sys_chmod, sys_chown, sparc_brk, sys_nis_syscall, sys_lseek
/*20*/	.word sys_getpid, sys_nis_syscall, sys_nis_syscall, sys_setuid, sys_getuid
/*25*/	.word sys_time, sys_ptrace, sys_alarm, sys_nis_syscall, sys_pause
/*30*/	.word sys_utime, sys_stty, sys_gtty, sys_access, sys_nice
	.word sys_ftime, sys_sync, sys_kill, sys_newstat, sys_nis_syscall
/*40*/	.word sys_newlstat, sys_dup, sys_pipe, sys_times, sys_profil
	.word sys_nis_syscall, sys_setgid, sys_getgid, sys_signal, sys_geteuid
/*50*/	.word sys_getegid, sys_acct, sys_nis_syscall, sys_nis_syscall, sys_ioctl
	.word sys_reboot, sys_nis_syscall, sys_symlink, sys_readlink, sys_execve
/*60*/	.word sys_umask, sys_chroot, sys_newfstat, sys_nis_syscall, sys_getpagesize
	.word sys_nis_syscall, sys_vfork, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*70*/	.word sys_nis_syscall, sys_mmap, sys_nis_syscall, sys_munmap, sys_mprotect
	.word sys_nis_syscall, sys_vhangup, sys_nis_syscall, sys_nis_syscall, sys_getgroups
/*80*/	.word sys_setgroups, sys_getpgrp, sys_nis_syscall, sys_setitimer, sys_nis_syscall
	.word sys_swapon, sys_getitimer, sys_nis_syscall, sys_sethostname, sys_nis_syscall
/*90*/	.word sys_dup2, sys_nis_syscall, sys_fcntl, sys_select, sys_nis_syscall
	.word sys_fsync, sys_setpriority, sys_socket, sys_connect, sys_accept
/*100*/	.word sys_getpriority, sys_send, sys_recv, sys_nis_syscall, sys_bind
	.word sys_setsockopt, sys_listen, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*110*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_recvmsg, sys_sendmsg
	.word sys_nis_syscall, sys_gettimeofday, sys_getrusage, sys_getsockopt, sys_nis_syscall
/*120*/	.word sys_readv, sys_writev, sys_settimeofday, sys_fchown, sys_fchmod
	.word sys_recvfrom, sys_setreuid, sys_setregid, sys_rename, sys_truncate
/*130*/	.word sys_ftruncate, sys_flock, sys_nis_syscall, sys_sendto, sys_shutdown
	.word sys_socketpair, sys_mkdir, sys_rmdir, sys_nis_syscall, sys_nis_syscall
/*140*/	.word sys_nis_syscall, sys_getpeername, sys_nis_syscall, sys_nis_syscall, sys_getrlimit
	.word sys_setrlimit, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*150*/	.word sys_getsockname, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
	.word sys_nis_syscall, sys_nis_syscall, sys_statfs, sys_fstatfs, sys_umount
/*160*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_setdomainname, sys_nis_syscall
	.word sys_quotactl, sys_nis_syscall, sys_mount, sys_ustat, sys_nis_syscall
/*170*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_getdents
	.word sys_setsid, sys_fchdir, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*180*/	.word sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_sigpending, sys_query_module
	.word sys_setpgid, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall, sys_newuname
/*190*/	.word sys_init_module, sys_personality, sys_prof, sys_break, sys_lock
	.word sys_mpx, sys_ulimit, sys_getppid, sparc_sigaction, sys_sgetmask
/*200*/	.word sys_ssetmask, sys_sigsuspend, sys_newlstat, sys_uselib, sys_nis_syscall
	.word sys_nis_syscall, sys_nis_syscall, sys_syslog, sys_nis_syscall, sys_nis_syscall
/*210*/	.word sys_idle, sys_nis_syscall, sys_waitpid, sys_swapoff, sys_sysinfo
	.word sys_ipc, sys_sigreturn, sys_clone, sys_nis_syscall, sys_adjtimex
/*220*/	.word sys_sigprocmask, sys_create_module, sys_delete_module, sys_get_kernel_syms, sys_getpgid
	.word sys_bdflush, sys_sysfs, sys_nis_syscall, sys_setfsuid, sys_setfsgid
/*230*/	.word sys_llseek, sys_time, sys_nis_syscall, sys_stime, sys_nis_syscall
	.word sys_nis_syscall, sys_llseek, sys_mlock, sys_munlock, sys_mlockall
/*240*/	.word sys_munlockall, sys_sched_setparam, sys_sched_getparam, sys_nis_syscall, sys_nis_syscall
	.word sys_nis_syscall, sys_sched_get_priority_max, sys_sched_get_priority_min, sys_sched_rr_get_interval, sys_nanosleep
/*250*/	.word sys_mremap, sys_sysctl, sys_getsid, sys_fdatasync, sys_nfsservctl
	.word sys_aplib

	/* Now the 32-bit SunOS syscall table. */

	.align 1024
	.globl sunos_sys_table
sunos_sys_table:
/*0*/	.word sunos_indir, sys_exit, sys_fork
	.word sunos_read, sunos_write, sunos_open
	.word sys_close, sunos_wait4, sys32_creat
	.word sys32_link, sys32_unlink, sunos_execv
	.word sys32_chdir, sunos_nosys, sys32_mknod
	.word sys32_chmod, sys32_chown, sunos_brk
	.word sunos_nosys, sys32_lseek, sunos_getpid
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_getuid, sunos_nosys, sys_ptrace
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sys32_access, sunos_nosys, sunos_nosys
	.word sys_sync, sys_kill, sys32_newstat
	.word sunos_nosys, sys32_newlstat, sys_dup
	.word sys_pipe, sunos_nosys, sys_profil
	.word sunos_nosys, sunos_nosys, sunos_getgid
	.word sunos_nosys, sunos_nosys
/*50*/	.word sunos_nosys, sys32_acct, sunos_nosys
	.word sunos_mctl, sunos_ioctl, sys32_reboot
	.word sunos_nosys, sys32_symlink, sys32_readlink
	.word sys32_execve, sys_umask, sys32_chroot
	.word sys32_newfstat, sunos_nosys, sys_getpagesize
	.word sys32_msync, sys_vfork, sunos_nosys
	.word sunos_nosys, sunos_sbrk, sunos_sstk
	.word sunos_mmap, sunos_vadvise, sys32_munmap
	.word sys32_mprotect, sunos_madvise, sys_vhangup
	.word sunos_nosys, sunos_mincore, sys32_getgroups
	.word sys32_setgroups, sys_getpgrp, sunos_setpgrp
	.word sys32_setitimer, sunos_nosys, sys32_swapon
	.word sys32_getitimer, sys32_gethostname, sys32_sethostname
	.word sunos_getdtablesize, sys_dup2, sunos_nop
	.word sys32_fcntl, sunos_select, sunos_nop
	.word sys_fsync, sys_setpriority, sys_socket
	.word sys32_connect, sunos_accept
/*100*/	.word sys_getpriority, sunos_send, sunos_recv
	.word sunos_nosys, sys32_bind, sunos_setsockopt
	.word sys_listen, sunos_nosys, sunos_sigaction
	.word sunos_sigblock, sunos_sigsetmask, sys_sigpause
	.word sys32_sigstack, sys32_recvmsg, sys32_sendmsg
	.word sunos_nosys, sys32_gettimeofday, sys32_getrusage
	.word sunos_getsockopt, sunos_nosys, sunos_readv
	.word sunos_writev, sys32_settimeofday, sys_fchown
	.word sys_fchmod, sys32_recvfrom, sys32_setreuid
	.word sys_setregid, sys32_rename, sys32_truncate
	.word sys32_ftruncate, sys_flock, sunos_nosys
	.word sys32_sendto, sys_shutdown, sys_socketpair
	.word sys32_mkdir, sys32_rmdir, sys32_utimes
	.word sys_sigreturn, sunos_nosys, sys32_getpeername
	.word sunos_gethostid, sunos_nosys, sys32_getrlimit
	.word sys32_setrlimit, sunos_killpg, sunos_nosys
	.word sunos_nosys, sunos_nosys
/*150*/	.word sys32_getsockname, sunos_nosys, sunos_nosys
	.word sys32_poll, sunos_nosys, sunos_nosys
	.word sunos_getdirentries, sys32_statfs, sys32_fstatfs
	.word sys32_umount, sunos_nosys, sunos_nosys
	.word sunos_getdomainname, sys32_setdomainname
	.word sunos_nosys, sys32_quotactl, sunos_nosys
	.word sunos_mount, sys32_ustat, sunos_semsys
	.word sunos_nosys, sunos_shmsys, sunos_audit
	.word sunos_nosys, sunos_getdents, sys_setsid
	.word sys_fchdir, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sys32_sigpending, sunos_nosys
	.word sys_setpgid, sunos_pathconf, sunos_fpathconf
	.word sunos_sysconf, sunos_uname, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
/*200*/	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys
/*250*/	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sys_aplib
