/* -*- linux-c -*-
 * sysctl_net_ax25.c: sysctl interface to net AX.25 subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ax25 directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <net/ax25.h>

static int min_ax25[] = {0, 0, 0, 0, 0, 1,  1,  1 * PR_SLOWHZ,  1 * PR_SLOWHZ,
	   0 * PR_SLOWHZ,     0 * PR_SLOWHZ,  1,   1, 0x00, 1};
static int max_ax25[] = {1, 1, 1, 1, 1, 7, 63, 30 * PR_SLOWHZ, 20 * PR_SLOWHZ,
	3600 * PR_SLOWHZ, 65535 * PR_SLOWHZ, 31, 512, 0x03, 20};

static struct ctl_table_header *ax25_table_header;

static ctl_table ax25_table[AX25_MAX_DEVICES + 1];

static ctl_table ax25_dir_table[] = {
	{NET_AX25, "ax25", NULL, 0, 0555, ax25_table},
	{0}
};

static ctl_table ax25_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, ax25_dir_table},
	{0}
};

void ax25_register_sysctl(void)
{
	int i, n;

	memset(ax25_table, 0x00, (AX25_MAX_DEVICES + 1) * sizeof(ctl_table));

	for (n = 0, i = 0; i < AX25_MAX_DEVICES; i++) {
		if (ax25_device[i].dev != NULL) {
			ax25_table[n].ctl_name     = n + 1;
			ax25_table[n].procname     = ax25_device[i].name;
			ax25_table[n].data         = &ax25_device[i].values;
			ax25_table[n].maxlen       = AX25_MAX_VALUES * sizeof(int);
			ax25_table[n].mode         = 0644;
			ax25_table[n].child        = NULL;
			ax25_table[n].proc_handler = &proc_dointvec_minmax;
			ax25_table[n].strategy     = &sysctl_intvec;
			ax25_table[n].de           = NULL;
			ax25_table[n].extra1       = &min_ax25;
			ax25_table[n].extra2       = &max_ax25;
			n++;
		}
	}

	ax25_table_header = register_sysctl_table(ax25_root_table, 1);
}

void ax25_unregister_sysctl(void)
{
	unregister_sysctl_table(ax25_table_header);
}
