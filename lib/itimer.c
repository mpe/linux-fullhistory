/*
 *  linux/lib/itimer.c
 *
 *  (C) 1992  Darren Senn
 */

#define __LIBRARY__
#include <linux/unistd.h>
#include <sys/time.h>

_syscall2(int,getitimer,int,which,struct itimerval *,value)
_syscall3(int,setitimer,int,which,struct itimerval *,value,struct itimerval *,ovalue)
