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
	printk("[%d]fork()\n",current->pid);
	return kernel_fork();
}

extern void sys_idle(void);
static inline void idle(void)
{
	printk("[%d]idle()\n",current->pid);
	sys_idle();
	for(;;);
}

extern int sys_setup(void);
static inline int setup(void)
{
	int retval;

	printk("[%d]setup()\n",current->pid);
	retval = sys_setup();
	printk("[%d]setup() returned %d\n",current->pid, retval);
}

extern int sys_open(const char *, int, int);
static inline int open(const char * name, int mode, int flags)
{
	int fd;
	printk("[%d]open(%s,%d,%d)\n",current->pid, name, mode, flags);
	fd = sys_open(name, mode, flags);
	printk("[%d]open(%s,%d,%d)=%d\n",current->pid, name, mode, flags, fd);
	return fd;
}

extern int sys_dup(int);
static inline int dup(int fd)
{
	int newfd = sys_dup(fd);
	printk("[%d]dup(%d)=%d\n",current->pid, fd, newfd);
	return newfd;
}

static inline int close(int fd)
{
	printk("[%d]close(%d)\n",current->pid,fd);
	return sys_close(fd);
}

extern int sys_exit(int);
static inline int _exit(int value)
{
	printk("[%d]_exit(%d)\n", current->pid, value);
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
	int res = sys_read(fd, buf, nr);
	printk("[%d]read(%d,%s,%d)=%d\n",current->pid, fd, buf, nr, res);
	return res;
}

#define execve(x,y,z)	({ printk("[%d]execve(%s,%p,%p)\n",current->pid, x, y, z); -1; })
#define waitpid(x,y,z)	sys_waitpid(x,y,z)
#define setsid()	({ printk("[%d]setsid()\n",current->pid); -1; })
#define sync()		({ printk("[%d]sync()\n",current->pid); -1; })

extern int sys_waitpid(int, int *, int);
static inline pid_t wait(int * wait_stat)
{
	long retval, i;
	printk("[%d]wait(%p)\n", current->pid, wait_stat);
	retval = waitpid(-1,wait_stat,0);
	printk("[%d]wait(%p) returned %ld\n", current->pid, wait_stat, retval);
	for (i = 0; i < 1000000000; i++);
	return retval;
}

#endif

#endif /* _ALPHA_UNISTD_H */
