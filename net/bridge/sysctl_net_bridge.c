/* -*- linux-c -*-
 * sysctl_net_bridge.c: sysctl interface to net bridge subsystem.
 *
 * Begun June 1, 1996, Mike Shaver.
 * Added /proc/sys/net/bridge directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

ctl_table bridge_table[] = {
	{0}
};
