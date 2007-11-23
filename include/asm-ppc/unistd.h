/* * Last edited: Nov 17 16:28 1995 (cort) */
#ifndef _ASM_PPC_UNISTD_H_
#define _ASM_PPC_UNISTD_H_

#define _NR(n) #n
#define _lisc(n) "li 0," _NR(n)

/*
 * This file contains the system call numbers.
 */

#define __NR_setup		  0	/* used only by init, to get system going */
#define __NR_exit		  1
#define __NR_fork		  2
#define __NR_read		  3
#define __NR_write		  4
#define __NR_open		  5
#define __NR_close		  6
#define __NR_waitpid		  7
#define __NR_creat		  8
#define __NR_link		  9
#define __NR_unlink		 10
#define __NR_execve		 11
#define __NR_chdir		 12
#define __NR_time		 13
#define __NR_mknod		 14
#define __NR_chmod		 15
#define __NR_chown		 16
#define __NR_break		 17
#define __NR_oldstat		 18
#define __NR_lseek		 19
#define __NR_getpid		 20
#define __NR_mount		 21
#define __NR_umount		 22
#define __NR_setuid		 23
#define __NR_getuid		 24
#define __NR_stime		 25
#define __NR_ptrace		 26
#define __NR_alarm		 27
#define __NR_oldfstat		 28
#define __NR_pause		 29
#define __NR_utime		 30
#define __NR_stty		 31
#define __NR_gtty		 32
#define __NR_access		 33
#define __NR_nice		 34
#define __NR_ftime		 35
#define __NR_sync		 36
#define __NR_kill		 37
#define __NR_rename		 38
#define __NR_mkdir		 39
#define __NR_rmdir		 40
#define __NR_dup		 41
#define __NR_pipe		 42
#define __NR_times		 43
#define __NR_prof		 44
#define __NR_brk		 45
#define __NR_setgid		 46
#define __NR_getgid		 47
#define __NR_signal		 48
#define __NR_geteuid		 49
#define __NR_getegid		 50
#define __NR_acct		 51
#define __NR_phys		 52
#define __NR_lock		 53
#define __NR_ioctl		 54
#define __NR_fcntl		 55
#define __NR_mpx		 56
#define __NR_setpgid		 57
#define __NR_ulimit		 58
#define __NR_oldolduname	 59
#define __NR_umask		 60
#define __NR_chroot		 61
#define __NR_ustat		 62
#define __NR_dup2		 63
#define __NR_getppid		 64
#define __NR_getpgrp		 65
#define __NR_setsid		 66
#define __NR_sigaction		 67
#define __NR_sgetmask		 68
#define __NR_ssetmask		 69
#define __NR_setreuid		 70
#define __NR_setregid		 71
#define __NR_sigsuspend		 72
#define __NR_sigpending		 73
#define __NR_sethostname	 74
#define __NR_setrlimit		 75
#define __NR_getrlimit		 76
#define __NR_getrusage		 77
#define __NR_gettimeofday	 78
#define __NR_settimeofday	 79
#define __NR_getgroups		 80
#define __NR_setgroups		 81
#define __NR_select		 82
#define __NR_symlink		 83
#define __NR_oldlstat		 84
#define __NR_readlink		 85
#define __NR_uselib		 86
#define __NR_swapon		 87
#define __NR_reboot		 88
#define __NR_readdir		 89
#define __NR_mmap		 90
#define __NR_munmap		 91
#define __NR_truncate		 92
#define __NR_ftruncate		 93
#define __NR_fchmod		 94
#define __NR_fchown		 95
#define __NR_getpriority	 96
#define __NR_setpriority	 97
#define __NR_profil		 98
#define __NR_statfs		 99
#define __NR_fstatfs		100
#define __NR_ioperm		101
#define __NR_socketcall		102
#define __NR_syslog		103
#define __NR_setitimer		104
#define __NR_getitimer		105
#define __NR_stat		106
#define __NR_lstat		107
#define __NR_fstat		108
#define __NR_olduname		109
#define __NR_iopl		110
#define __NR_vhangup		111
#define __NR_idle		112
#define __NR_vm86		113
#define __NR_wait4		114
#define __NR_swapoff		115
#define __NR_sysinfo		116
#define __NR_ipc		117
#define __NR_fsync		118
#define __NR_sigreturn		119
#define __NR_clone		120
#define __NR_setdomainname	121
#define __NR_uname		122
#define __NR_modify_ldt		123
#define __NR_adjtimex		124
#define __NR_mprotect		125
#define __NR_sigprocmask	126
#define __NR_create_module	127
#define __NR_init_module	128
#define __NR_delete_module	129
#define __NR_get_kernel_syms	130
#define __NR_quotactl		131
#define __NR_getpgid		132
#define __NR_fchdir		133
#define __NR_bdflush		134
#define __NR_sysfs		135
#define __NR_personality	136
#define __NR_afs_syscall	137 /* Syscall for Andrew File System */
#define __NR_setfsuid		138
#define __NR_setfsgid		139
#define __NR__llseek		140
#define __NR_getdents		141
#define __NR__newselect		142
#define __NR_flock		143
#define __NR_msync		144
/*#define __NR_kclone		145*/


/* XXX - _foo needs to be __foo, while __NR_bar could be _NR_bar. */
#define _syscall0(type,name) \
type name(void) \
{ \
 __asm__ (_lisc(__NR_##name)); \
 __asm__ ("sc"); \
 __asm__ ("mr 31,3"); \
 __asm__ ("bns 10f"); \
 __asm__ ("mr 0,3"); \
 __asm__ ("lis 3,errno@ha"); \
 __asm__ ("stw 0,errno@l(3)"); \
 __asm__ ("li 3,-1"); \
 __asm__ ("10:"); \
}

#define _syscall1(type,name,type1,arg1) \
type name(type1 arg1) \
{ \
 __asm__ (_lisc(__NR_##name)); \
 __asm__ ("sc"); \
 __asm__ ("mr 31,3"); \
 __asm__ ("bns 10f"); \
 __asm__ ("mr 0,3"); \
 __asm__ ("lis 3,errno@ha"); \
 __asm__ ("stw 0,errno@l(3)"); \
 __asm__ ("li 3,-1"); \
 __asm__ ("10:"); \
}

#define _syscall2(type,name,type1,arg1,type2,arg2) \
type name(type1 arg1,type2 arg2) \
{ \
 __asm__ (_lisc(__NR_##name)); \
 __asm__ ("sc"); \
 __asm__ ("mr 31,3"); \
 __asm__ ("bns 10f"); \
 __asm__ ("mr 0,3"); \
 __asm__ ("lis 3,errno@ha"); \
 __asm__ ("stw 0,errno@l(3)"); \
 __asm__ ("li 3,-1"); \
 __asm__ ("10:"); \
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3) \
type name(type1 arg1,type2 arg2,type3 arg3) \
{ \
 __asm__ (_lisc(__NR_##name)); \
 __asm__ ("sc"); \
 __asm__ ("mr 31,3"); \
 __asm__ ("bns 10f"); \
 __asm__ ("mr 0,3"); \
 __asm__ ("lis 3,errno@ha"); \
 __asm__ ("stw 0,errno@l(3)"); \
 __asm__ ("li 3,-1"); \
 __asm__ ("10:"); \
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
type name (type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
 __asm__ (_lisc(__NR_##name)); \
 __asm__ ("sc"); \
 __asm__ ("mr 31,3"); \
 __asm__ ("bns 10f"); \
 __asm__ ("mr 0,3"); \
 __asm__ ("lis 3,errno@ha"); \
 __asm__ ("stw 0,errno@l(3)"); \
 __asm__ ("li 3,-1"); \
 __asm__ ("10:"); \
} 

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
	  type5,arg5) \
type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{ \
 __asm__ (_lisc(__NR_##name)); \
 __asm__ ("sc"); \
 __asm__ ("mr 31,3"); \
 __asm__ ("bns 10f"); \
 __asm__ ("mr 0,3"); \
 __asm__ ("lis 3,errno@ha"); \
 __asm__ ("stw 0,errno@l(3)"); \
 __asm__ ("li 3,-1"); \
 __asm__ ("10:"); \
}

#ifdef __KERNEL_SYSCALLS__

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
/*
   some of these had problems getting the right arguments (namely sys_clone())
   when they were inline.
             -- Cort
 */
#define __NR__exit __NR_exit
static inline _syscall0(int,idle) /* made inline "just in case" -- Cort */
static inline _syscall0(int,fork) /* needs to be inline */
static inline _syscall0(int,pause) /* needs to be inline */
static inline _syscall0(int,setup) /* called in init before execve */
static inline _syscall0(int,sync)
static inline _syscall0(pid_t,setsid)
static /*inline*/ _syscall3(int,write,int,fd,const char *,buf,off_t,count)
static /*inline*/ _syscall1(int,dup,int,fd)
static /*inline*/ _syscall3(int,execve,const char *,file,char **,argv,char **,envp)
static /*inline*/ _syscall3(int,open,const char *,file,int,flag,int,mode)
static inline _syscall1(int,close,int,fd)
static /*inline*/ _syscall1(int,_exit,int,exitcode)
static inline _syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)
/*static inline _syscall2(int,clone,unsigned long,flags,char *,esp)*/

/*
   syscalls from kernel mode is a little strange and I can't get used to
   the idea -- this makes me feel better.   -- Cort
 */
/*static inline int kclone (void)
{
 __asm__ (_lisc(__NR_kclone)); 
 __asm__ ("sc"); 
 __asm__ ("mr 31,3"); 
 __asm__ ("bns 10f"); 
 __asm__ ("mr 0,3"); 
 __asm__ ("lis 3,errno@ha"); 
 __asm__ ("stw 0,errno@l(3)"); 
 __asm__ ("li 3,-1"); 
 __asm__ ("10:");   
}*/

static inline int clone (unsigned long flags,char *esp)
{
/*  printk("unistd.h: clone(): flags = %x, esp = %x\n", flags, esp);*/
 __asm__ (_lisc(__NR_clone)); 
 __asm__ ("sc");
 __asm__ ("mr 31,3"); 

/* this is a hack to get the damned thing to return something even though inlined
   -- Cort
   */
 __asm__ ("mr 0,3"); 
 __asm__ ("lis 3,errno@ha"); 
 __asm__ ("stw 0,errno@l(3)"); 
 
 __asm__ ("bns 10f"); 
 __asm__ ("mr 0,3"); 
 __asm__ ("lis 3,errno@ha"); 
 __asm__ ("stw 0,errno@l(3)"); 
 __asm__ ("li 3,-1"); 
 __asm__ ("10:");
 return errno;
}


/* called from init before execve -- need to be inline? -- Cort */
static inline pid_t wait(int * wait_stat) 
{
	return waitpid(-1,wait_stat,0);
}

#endif

#endif /* _ASM_PPC_UNISTD_H_ */


