#ifndef _SPARC_UNISTD_H
#define _SPARC_UNISTD_H

/*
 * System calls under the Sparc.
 *
 * Don't be scared by the ugly clobbers, it is the only way I can
 * think of right now to force the arguments into fixed registers
 * before the trap into the system call with gcc 'asm' statements.
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

/* XXX - _foo needs to be __foo, while __NR_bar could be _NR_bar. */
#define _syscall0(type,name) \
type name(void) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%o0\n\t" \
		  "t 0xa\n\t" \
		  : "=r" (__res) \
		  : "0" (__NR_##name) \
		  : "o0"); \
if (__res >= 0) \
    return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall1(type,name,type1,arg1) \
type name(type1 arg1) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%o0\n\t" \
		  "or %%g0, %1, %%o1\n\t" \
		  "t 0xa\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)) \
		  : "0" (__NR_##name),"1" ((long)(arg1)) \
		  : "o0", "o1"); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall2(type,name,type1,arg1,type2,arg2) \
type name(type1 arg1,type2 arg2) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%o0\n\t" \
		  "or %%g0, %1, %%o1\n\t" \
		  "or %%g0, %2, %%o2\n\t" \
		  "t 0xa\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)), "=r" ((long)(args)) \
		  : "0" (__NR_##name),"1" ((long)(arg1)),"2" ((long)(arg2)) \
		  : "o0", "o1", "o2"); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3) \
type name(type1 arg1,type2 arg2,type3 arg3) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%o0\n\t" \
		  "or %%g0, %1, %%o1\n\t" \
		  "or %%g0, %2, %%o2\n\t" \
		  "or %%g0, %3, %%o3\n\t" \
		  "t 0xa\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)), "=r" ((long)(arg2)), \
		    "=r" ((long)(arg3)) \
		  : "0" (__NR_##name), "1" ((long)(arg1)), "2" ((long)(arg2)), \
		    "3" ((long)(arg3)) \
		  : "o0", "o1", "o2", "o3"); \
if (__res>=0) \
	return (type) __res; \
errno=-__res; \
return -1; \
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
type name (type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%o0\n\t" \
		  "or %%g0, %1, %%o1\n\t" \
		  "or %%g0, %2, %%o2\n\t" \
		  "or %%g0, %3, %%o3\n\t" \
		  "or %%g0, %4, %%o4\n\t" \
		  "t 0xa\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)), "=r" ((long)(arg2)), \
		    "=r" ((long)(arg3)), "=r" ((long)(arg4)) \
		  : "0" (__NR_##name),"1" ((long)(arg1)),"2" ((long)(arg2)), \
		    "3" ((long)(arg3)),"4" ((long)(arg4)) \
		  : "o0", "o1", "o2", "o3", "o4"); \
if (__res>=0) \
	return (type) __res; \
errno=-__res; \
return -1; \
} 

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
	  type5,arg5) \
type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%o0\n\t" \
		  "or %%g0, %1, %%o1\n\t" \
		  "or %%g0, %2, %%o2\n\t" \
		  "or %%g0, %3, %%o3\n\t" \
		  "or %%g0, %4, %%o4\n\t" \
		  "or %%g0, %5, %%o5\n\t" \
		  "t 0xa\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)), "=r" ((long)(arg2)), \
		    "=r" ((long)(arg3)), "=r" ((long)(arg4)), "=r" ((long)(arg5)) \
		  : "0" (__NR_##name),"1" ((long)(arg1)),"2" ((long)(arg2)), \
		    "3" ((long)(arg3)),"4" ((long)(arg4)),"5" ((long)(arg5)) \
		  : "o0", "o1", "o2", "o3", "o4", "o5"); \
if (__res>=0) \
	return (type) __res; \
errno=-__res; \
return -1; \
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
#define __NR__exit __NR_exit
static inline _syscall0(int,idle)
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)
static inline _syscall0(int,setup)
static inline _syscall0(int,sync)
static inline _syscall0(pid_t,setsid)
static inline _syscall3(int,write,int,fd,const char *,buf,off_t,count)
static inline _syscall1(int,dup,int,fd)
static inline _syscall3(int,execve,const char *,file,char **,argv,char **,envp)
static inline _syscall3(int,open,const char *,file,int,flag,int,mode)
static inline _syscall1(int,close,int,fd)
static inline _syscall1(int,_exit,int,exitcode)
static inline _syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)

static inline pid_t wait(int * wait_stat)
{
	return waitpid(-1,wait_stat,0);
}

#endif

#endif /* _SPARC_UNISTD_H */
