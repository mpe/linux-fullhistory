/*
 * NET3:	Garbage Collector For AF_UNIX sockets (STUB)
 *
 * Authors:	
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 * Fixes:
 *
 */
 
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <asm/segment.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/af_unix.h>
#include <linux/proc_fs.h>
/*
 *	Garbage Collector Stubs
 */
 

int unix_gc_free=128;		/* GC slots free */

void unix_gc_remove(struct file *fp)
{
	;
}

void unix_gc_add(struct sock *sk, struct file *fp)
{
	;
}
