#ifndef _ALPHA_UNISTD_H
#define _ALPHA_UNISTD_H

#define __NR_exit		  1
#define __NR_fork		  2
#define __NR_read		  3
#define __NR_write		  4
#define __NR_close		  6
#define __NR_wait4		  7
#define __NR_link		  9
#define __NR_unlink		 10
#define __NR_chdir		 12
#define __NR_fchdir		 13
#define __NR_mknod		 14
#define __NR_chmod		 15
#define __NR_chown		 16
#define __NR_brk		 17
#define __NR_lseek		 19
#define __NR_getxpid		 20
#define __NR_setuid		 23
#define __NR_getxuid		 24
#define __NR_ptrace		 26
#define __NR_access		 33
#define __NR_sync		 36
#define __NR_kill		 37
#define __NR_setpgid		 39
#define __NR_dup		 41
#define __NR_pipe		 42
#define __NR_open		 45
#define __NR_getxgid		 47
#define __NR_acct		 51
#define __NR_sigpending		 52
#define __NR_ioctl		 54
#define __NR_symlink		 57
#define __NR_readlink		 58
#define __NR_execve		 59
#define __NR_umask		 60
#define __NR_chroot		 61
#define __NR_getpgrp		 63
#define __NR_getpagesize	 64
#define __NR_stat		 67
#define __NR_lstat		 68
#define __NR_mmap		 71	/* OSF/1 mmap is superset of Linux */
#define __NR_munmap		 73
#define __NR_mprotect		 74
#define __NR_madvise		 75
#define __NR_vhangup		 76
#define __NR_getgroups		 79
#define __NR_setgroups		 80
#define __NR_setpgrp		 82	/* BSD alias for setpgid */
#define __NR_setitimer		 83
#define __NR_getitimer		 86
#define __NR_gethostname	 87
#define __NR_sethostname	 88
#define __NR_getdtablesize	 89
#define __NR_dup2		 90
#define __NR_fstat		 91
#define __NR_fcntl		 92
#define __NR_select		 93
#define __NR_fsync		 95
#define __NR_setpriority	 96
#define __NR_socket		 97
#define __NR_connect		 98
#define __NR_accept		 99
#define __NR_getpriority	100
#define __NR_send		101
#define __NR_recv		102
#define __NR_sigreturn		103
#define __NR_bind		104
#define __NR_setsockopt		105
#define __NR_listen		106
#define __NR_sigsuspend		111
#define __NR_recvmsg		113
#define __NR_sendmsg		114
#define __NR_gettimeofday	116
#define __NR_getrusage		117
#define __NR_getsockopt		118
#define __NR_readv		120
#define __NR_writev		121
#define __NR_settimeofday	122
#define __NR_fchown		123
#define __NR_fchmod		124
#define __NR_recvfrom		125
#define __NR_setreuid		126
#define __NR_setregid		127
#define __NR_rename		128
#define __NR_truncate		129
#define __NR_ftruncate		130
#define __NR_flock		131
#define __NR_setgid		132
#define __NR_sendto		133
#define __NR_shutdown		134
#define __NR_socketpair		135
#define __NR_mkdir		136
#define __NR_rmdir		137
#define __NR_utimes		138
#define __NR_getpeername	141
#define __NR_getrlimit		144
#define __NR_setrlimit		145
#define __NR_setsid		147
#define __NR_quotactl		148
#define __NR_getsockname	150
#define __NR_sigaction		156
#define __NR_setdomainname	166
#define __NR_msgctl		200
#define __NR_msgget		201
#define __NR_msgrcv		202
#define __NR_msgsnd		203
#define __NR_semctl		204
#define __NR_semget		205
#define __NR_semop		206
#define __NR_shmctl		210
#define __NR_shmdt		211
#define __NR_shmget		212

#define __NR_msync		217

#define __NR_getpgid		233
#define __NR_getsid		234

#define __NR_sysfs		254

/*
 * Linux-specific system calls begin at 300
 */
#define __NR_bdflush		300
#define __NR_sethae		301
#define __NR_mount		302
#define __NR_adjtimex		303
#define __NR_swapoff		304
#define __NR_getdents		305
#define __NR_create_module	306
#define __NR_init_module	307
#define __NR_delete_module	308
#define __NR_get_kernel_syms	309
#define __NR_syslog		310
#define __NR_reboot		311
#define __NR_clone		312
#define __NR_uselib		313
#define __NR_mlock		314
#define __NR_munlock		315
#define __NR_mlockall		316
#define __NR_munlockall		317
#define __NR_sysinfo		318
#define __NR__sysctl		319
#define __NR_idle		320
#define __NR_umount		321
#define __NR_swapon		322
#define __NR_times		323
#define __NR_personality	324
#define __NR_setfsuid		325
#define __NR_setfsgid		326
#define __NR_ustat		327
#define __NR_statfs		328
#define __NR_fstatfs		329
#define __NR_sched_setparam		330
#define __NR_sched_getparam		331
#define __NR_sched_setscheduler		332
#define __NR_sched_getscheduler		333
#define __NR_sched_yield		334
#define __NR_sched_get_priority_max	335
#define __NR_sched_get_priority_min	336
#define __NR_sched_rr_get_interval	337
#define __NR_afs_syscall		338
#define __NR_uname			339
#define __NR_nanosleep			340
#define __NR_mremap			341

#ifdef __LIBRARY__

/*
 * Duh, the alpha gcc compiler doesn't allow us to specify regs
 * yet. I'll have to see about this later..
 */

/* XXX - _foo needs to be __foo, while __NR_bar could be _NR_bar. */
#define _syscall0(type,name) \
type name(void) \
{ \
	return (type) -1; \
}

#define _syscall1(type,name,type1,arg1) \
type name(type1 arg1) \
{ \
	return (type) -1; \
}

#define _syscall2(type,name,type1,arg1,type2,arg2) \
type name(type1 arg1,type2 arg2) \
{ \
	return (type) -1; \
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3) \
type name(type1 arg1,type2 arg2,type3 arg3) \
{ \
	return (type) -1; \
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
type name (type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
	return (type) -1; \
} 

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
	  type5,arg5) \
type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{ \
	return (type) -1; \
}

#endif /* __LIBRARY__ */

#ifdef __KERNEL_SYSCALLS__

#include <linux/string.h>
#include <linux/signal.h>

extern long __kernel_thread(unsigned long, int (*)(void *), void *);

static inline long kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	return __kernel_thread(flags | CLONE_VM, fn, arg);
}

extern void sys_idle(void);
static inline void idle(void)
{
	sys_idle();
}

extern int sys_setup(void);
static inline int setup(void)
{
	return sys_setup();
}

extern int sys_open(const char *, int, int);
static inline int open(const char * name, int mode, int flags)
{
	return sys_open(name, mode, flags);
}

extern int sys_dup(int);
static inline int dup(int fd)
{
	return sys_dup(fd);
}

static inline int close(int fd)
{
	return sys_close(fd);
}

extern int sys_exit(int);
static inline int _exit(int value)
{
	return sys_exit(value);
}

#define exit(x) _exit(x)

extern int sys_write(int, const char *, int);
static inline int write(int fd, const char * buf, int nr)
{
	return sys_write(fd, buf, nr);
}

extern int sys_read(int, char *, int);
static inline int read(int fd, char * buf, int nr)
{
	return sys_read(fd, buf, nr);
}

extern int do_execve(char *, char **, char **, struct pt_regs *);
extern void ret_from_sys_call(void);
static inline int execve(char * file, char ** argvp, char ** envp)
{
	int i;
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	i = do_execve(file, argvp, envp, &regs);
	if (!i) {
		__asm__ __volatile__("bis %0,%0,$30\n\t"
				"bis %1,%1,$26\n\t"
				"ret $31,($26),1\n\t"
				: :"r" (&regs), "r" (ret_from_sys_call));
	}
	return -1;
}

extern int sys_setsid(void);
static inline int setsid(void)
{
	return sys_setsid();
}

extern int sys_sync(void);
static inline int sync(void)
{
	return sys_sync();
}

extern int sys_wait4(int, int *, int, struct rusage *);
static inline pid_t waitpid(int pid, int * wait_stat, int flags)
{
	return sys_wait4(pid, wait_stat, flags, NULL);
}

static inline pid_t wait(int * wait_stat)
{
	return waitpid(-1,wait_stat,0);
}

#endif

#endif /* _ALPHA_UNISTD_H */
