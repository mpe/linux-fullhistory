/*
  Structure of the proc filesystem:
  /proc/dasd/
  /proc/dasd/devices                  # List of devices
  /proc/dasd/ddabcd                   # Device node for devno abcd            
  /proc/dasd/ddabcd1                  # Device node for partition abcd  
  /proc/dasd/abcd                     # Device information for devno abcd
*/

#include <linux/proc_fs.h>

#include <linux/dasd.h>

#include "dasd_types.h"

int dasd_proc_read_devices ( char *, char **, off_t, int);
#ifdef DASD_PROFILE
extern int dasd_proc_read_statistics ( char *, char **, off_t, int);
extern int dasd_proc_read_debug ( char *, char **, off_t, int);
#endif /* DASD_PROFILE */

struct proc_dir_entry dasd_proc_root_entry = {
	0,
	4,"dasd",
	S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	1,0,0,
	0,
	NULL,
};

struct proc_dir_entry dasd_proc_devices_entry = {
	0,
	7,"devices",
	S_IFREG | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	1,0,0,
	0,
	NULL,
	&dasd_proc_read_devices,
};

#ifdef DASD_PROFILE
struct proc_dir_entry dasd_proc_stats_entry = {
	0,
	10,"statistics",
	S_IFREG | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	1,0,0,
	0,
	NULL,
	&dasd_proc_read_statistics,
};

struct proc_dir_entry dasd_proc_debug_entry = {
	0,
	5,"debug",
	S_IFREG | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	1,0,0,
	0,
	NULL,
	&dasd_proc_read_debug,
};
#endif /* DASD_PROFILE */

struct proc_dir_entry dasd_proc_device_template = {
	0,
	6,"dd????",
	S_IFBLK | S_IRUGO | S_IWUSR | S_IWGRP,
	1,0,0,
	0,
	NULL,
};

void
dasd_proc_init ( void )
{
	proc_register( & proc_root, & dasd_proc_root_entry);
	proc_register( & dasd_proc_root_entry, & dasd_proc_devices_entry);
#ifdef DASD_PROFILE
 	proc_register( & dasd_proc_root_entry, & dasd_proc_stats_entry); 
 	proc_register( & dasd_proc_root_entry, & dasd_proc_debug_entry); 
#endif /* DASD_PROFILE */
}


int 
dasd_proc_read_devices ( char * buf, char **start, off_t off, int len)
{
	int i;
	len = sprintf ( buf, "dev# MAJ minor node        Format\n");
	for ( i = 0; i < DASD_MAX_DEVICES; i++ ) {
		dasd_information_t *info = dasd_info[i];
		if ( ! info ) 
			continue;
		if ( len >= PAGE_SIZE - 80 )
			len += sprintf ( buf + len, "terminated...\n");
		len += sprintf ( buf + len,
				 "%04X %3d %5d /dev/dasd%c",
				 dasd_info[i]->info.devno,
				 DASD_MAJOR,
				 i << PARTN_BITS,
				 'a' + i );
                if (info->flags == DASD_INFO_FLAGS_NOT_FORMATTED) {
			len += sprintf ( buf + len, "    n/a");
		} else {
			len += sprintf ( buf + len, " %6d", 
					 info->sizes.bp_block);
		}
		len += sprintf ( buf + len, "\n");
	} 
	return len;
}


void 
dasd_proc_add_node (int di) 
{
}
