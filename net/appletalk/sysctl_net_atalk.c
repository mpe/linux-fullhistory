/* -*- linux-c -*-
 * sysctl_net_atalk.c: sysctl interface to net Appletalk subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/atalk directory entry (empty =) ). [MS]
 * Dynamic registration, added aarp entries. (5/30/97 Chris Horn)
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

extern int sysctl_aarp_expiry_time;
extern int sysctl_aarp_tick_time;
extern int sysctl_aarp_retransmit_limit;
extern int sysctl_aarp_resolve_time;


static ctl_table atalk_table[] = {
	{NET_ATALK_AARP_EXPIRY_TIME, "aarp-expiry-time",
	 &sysctl_aarp_expiry_time, sizeof(int), 0644, NULL, &proc_dointvec_jiffies},
	{NET_ATALK_AARP_TICK_TIME, "aarp-tick-time",
	 &sysctl_aarp_tick_time, sizeof(int), 0644, NULL, &proc_dointvec_jiffies},
	{NET_ATALK_AARP_RETRANSMIT_LIMIT, "aarp-retransmit-limit",
	 &sysctl_aarp_retransmit_limit, sizeof(int), 0644, NULL, &proc_dointvec},
	{NET_ATALK_AARP_RESOLVE_TIME, "aarp-resolve-time",
	 &sysctl_aarp_resolve_time, sizeof(int), 0644, NULL, &proc_dointvec_jiffies},
	{0}
};

static ctl_table atalk_dir_table[] = {
	{NET_ATALK, "appletalk", NULL, 0, 0555, atalk_table},
	{0}
};

static ctl_table atalk_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, atalk_dir_table},
	{0}
};

static struct ctl_table_header *atalk_table_header;

inline void atalk_register_sysctl(void)
{
	atalk_table_header = register_sysctl_table(atalk_root_table, 1);
}

inline void atalk_unregister_sysctl(void)
{
	unregister_sysctl_table(atalk_table_header);
}







