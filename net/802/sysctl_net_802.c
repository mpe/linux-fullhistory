/* -*- linux-c -*-
 * sysctl_net_802.c: sysctl interface to net 802 subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/802 directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

ctl_table e802_table[] = {
	{0}
};
