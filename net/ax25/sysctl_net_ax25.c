/* -*- linux-c -*-
 * sysctl_net_ax25.c: sysctl interface to net AX.25 subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ax25 directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

ctl_table ax25_table[] = {
	{0}
};
