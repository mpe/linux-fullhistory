/*
 *  linux/lib/write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <linux/unistd.h>
#include <sys/types.h>

_syscall3(int,write,int,fd,const char *,buf,off_t,count)

