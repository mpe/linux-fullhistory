/*
 *  linux/lib/open.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#define __LIBRARY__
#include <linux/unistd.h>
#include <stdarg.h>

#define open __sys_open
static inline _syscall3(int, open, const char *, filename, int, flag, int, mode)
#undef open

int open(const char * filename, int flag, ...)
{
	int res;
	va_list arg;

	va_start(arg,flag);
	res = __sys_open(filename, flag, va_arg(arg, int));
	va_end(arg);
	return res;
}
