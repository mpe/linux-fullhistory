/*
 * NET3:	Sysctl interface to net af_unix subsystem.
 *
 * Authors:	Mike Shaver.
 *
 *		Added /proc/sys/net/unix directory entry (empty =) ).
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

extern int sysctl_unix_destroy_delay;
extern int sysctl_unix_delete_delay;

ctl_table unix_table[] = {
	{NET_UNIX_DESTROY_DELAY, "destroy_delay",
	&sysctl_unix_destroy_delay, sizeof(int), 0644, NULL, 
	 &proc_dointvec_jiffies},
	{NET_UNIX_DELETE_DELAY, "delete_delay",
	&sysctl_unix_delete_delay, sizeof(int), 0644, NULL, 
	 &proc_dointvec_jiffies},
	{0}
};
