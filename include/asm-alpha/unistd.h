#ifndef _ALPHA_UNISTD_H
#define _ALPHA_UNISTD_H

/*
 * ".long 131" is "PAL_callsys"..
 *
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

#ifdef __KERNEL_SYSCALLS__

extern unsigned long kernel_fork(void);
static inline unsigned long fork(void)
{
	return kernel_fork();
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

extern int sys_waitpid(int, int *, int);
static inline pid_t waitpid(int pid, int * wait_stat, int flags)
{
	return sys_waitpid(pid,wait_stat,flags);
}

static inline pid_t wait(int * wait_stat)
{
	return waitpid(-1,wait_stat,0);
}

#endif

#endif /* _ALPHA_UNISTD_H */
