/* -*- linux-c -*-
 * sysctl_net_netrom.c: sysctl interface to net NETROM subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/netrom directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

ctl_table netrom_table[] = {
	{0}
};
