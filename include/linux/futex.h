#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

/* Second argument to futex syscall */
#define FUTEX_WAIT (0)
#define FUTEX_WAKE (1)
#define FUTEX_FD (2)

extern asmlinkage int sys_futex(void *uaddr, int op, int val, struct timespec *utime);

#endif
