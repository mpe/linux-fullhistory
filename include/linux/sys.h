/*
 * system call entry points
 */

#define sys_clone sys_fork

#ifdef __cplusplus
extern "C" {
#endif

extern int sys_setup();
extern int sys_exit();
extern int sys_fork();
extern int sys_read();
extern int sys_write();
extern int sys_open();
extern int sys_close();
extern int sys_waitpid();
extern int sys_creat();
extern int sys_link();
extern int sys_unlink();
extern int sys_execve();
extern int sys_chdir();
extern int sys_time();
extern int sys_mknod();
extern int sys_chmod();
extern int sys_chown();
extern int sys_break();
extern int sys_stat();
extern int sys_lseek();
extern int sys_getpid();
extern int sys_mount();
extern int sys_umount();
extern int sys_setuid();
extern int sys_getuid();
extern int sys_stime();
extern int sys_ptrace();
extern int sys_alarm();
extern int sys_fstat();
extern int sys_pause();
extern int sys_utime();
extern int sys_stty();
extern int sys_gtty();
extern int sys_access();
extern int sys_nice();
extern int sys_ftime();
extern int sys_sync();
extern int sys_kill();
extern int sys_rename();
extern int sys_mkdir();
extern int sys_rmdir();
extern int sys_dup();
extern int sys_pipe();
extern int sys_times();
extern int sys_prof();
extern int sys_brk();
extern int sys_setgid();
extern int sys_getgid();
extern int sys_signal();
extern int sys_geteuid();
extern int sys_getegid();
extern int sys_acct();
extern int sys_phys();
extern int sys_lock();
extern int sys_ioctl();
extern int sys_fcntl();
extern int sys_mpx();
extern int sys_setpgid();
extern int sys_ulimit();
extern int sys_uname();
extern int sys_umask();
extern int sys_chroot();
extern int sys_ustat();
extern int sys_dup2();
extern int sys_getppid();
extern int sys_getpgrp();
extern int sys_setsid();
extern int sys_sigaction();
extern int sys_sgetmask();
extern int sys_ssetmask();
extern int sys_setreuid();
extern int sys_setregid();
extern int sys_sigpending();
extern int sys_sigsuspend();
extern int sys_sethostname();
extern int sys_setrlimit();
extern int sys_getrlimit();
extern int sys_getrusage();
extern int sys_gettimeofday();
extern int sys_settimeofday();
extern int sys_getgroups();
extern int sys_setgroups();
extern int sys_select();
extern int sys_symlink();
extern int sys_lstat();
extern int sys_readlink();
extern int sys_uselib();
extern int sys_swapon();
extern int sys_reboot();
extern int sys_readdir();
extern int sys_mmap();
extern int sys_munmap();
extern int sys_truncate();
extern int sys_ftruncate();
extern int sys_fchmod();
extern int sys_fchown();
extern int sys_getpriority();
extern int sys_setpriority();
extern int sys_profil();
extern int sys_statfs();
extern int sys_fstatfs();
extern int sys_ioperm();
extern int sys_socketcall();
extern int sys_syslog();
extern int sys_getitimer();
extern int sys_setitimer();
extern int sys_newstat();
extern int sys_newlstat();
extern int sys_newfstat();
extern int sys_newuname();
extern int sys_iopl();
extern int sys_vhangup();
extern int sys_idle();
extern int sys_vm86();
extern int sys_wait4();
extern int sys_swapoff();
extern int sys_sysinfo();
extern int sys_ipc();
extern int sys_fsync();
extern int sys_sigreturn();
extern int sys_setdomainname();
extern int sys_olduname();
extern int sys_old_syscall();
extern int sys_modify_ldt();
extern int sys_adjtimex();

/*
 * These are system calls that will be removed at some time
 * due to newer versions existing..
 */
#ifdef notdef
#define sys_waitpid	sys_old_syscall	/* sys_wait4	*/
#define sys_olduname	sys_old_syscall /* sys_newuname	*/
#define sys_stat	sys_old_syscall /* sys_newstat	*/
#define sys_fstat	sys_old_syscall	/* sys_newfstat	*/
#define sys_lstat	sys_old_syscall /* sys_newlstat	*/
#define sys_signal	sys_old_syscall	/* sys_sigaction */
#endif

typedef int (*fn_ptr)();

#ifdef __cplusplus
}
#endif

