/*
 *  linux/lib/setsid.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <sys/types.h>
#include <linux/unistd.h>

_syscall0(pid_t,setsid)

