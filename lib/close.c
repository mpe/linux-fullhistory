/*
 *  linux/lib/close.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <linux/unistd.h>

_syscall1(int,close,int,fd)

