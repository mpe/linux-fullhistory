/*
 *  linux/lib/_exit.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#define __LIBRARY__
#include <linux/unistd.h>

#define exit __sys_exit
static inline _syscall0(int, exit)
#undef exit

volatile void _exit(int exit_code)
{
fake_volatile:
	__sys_exit();
	goto fake_volatile;
}


