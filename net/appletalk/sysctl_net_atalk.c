/* -*- linux-c -*-
 * sysctl_net_atalk.c: sysctl interface to net Appletalk subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/atalk directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

ctl_table atalk_table[] = {
	{0}
};
