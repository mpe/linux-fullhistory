/*
 *  linux/lib/dup.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <linux/unistd.h>

_syscall1(int,dup,int,fd)

