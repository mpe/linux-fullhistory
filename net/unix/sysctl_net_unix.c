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
#include <linux/config.h>

#ifdef CONFIG_SYSCTL

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

#ifdef MODULE
static struct ctl_table_header * unix_sysctl_header;
static struct ctl_table unix_root_table[];
static struct ctl_table unix_net_table[];

ctl_table unix_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, unix_net_table},
	{0}
};

ctl_table unix_net_table[] = {
	{NET_UNIX, "unix", NULL, 0, 0555, unix_table},
	{0}
};

void unix_sysctl_register(void)
{
	unix_sysctl_header = register_sysctl_table(unix_root_table, 0);
}

void unix_sysctl_unregister(void)
{
	unregister_sysctl_table(unix_sysctl_header);
}
#endif	/* MODULE */

#endif	/* CONFIG_SYSCTL */
