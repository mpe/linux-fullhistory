/* -*- linux-c -*-
 * sysctl_net_unix.c: sysctl interface to net af_unix subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/unix directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

ctl_table unix_table[] = {
	{0}
};
