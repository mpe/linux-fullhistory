/*
 * $Id: arbitrary.c,v 1.2 1997/06/05 01:27:47 davem Exp $
 *
 * linux/fs/proc/arbitrary.c - lookup() for arbitrary inodes.
 * Copyright (C) 1997, Thomas Schoebel-Theuer,
 * <schoebel@informatik.uni-stuttgart.de>.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>

/* Format of dev/inode pairs that can be used as file names:
 * [<dev_number_in_hex]:<inode_number_in_decimal>
 * (the same format that is already in use in /proc/<pid>/exe,
 * /proc/<pid>/cwd and /proc/<pid>/root).
 */
/* Note that readdir does not supply such names, so they must be used
 * either "blind" or must be queried another way, for example
 * as result of a virtual symlink (see linux/proc/link.c).
 */
int proc_arbitrary_lookup(struct inode * dir, const char * name,
			  int len, struct inode ** result)
{
	int dev, ino;
	char * ptr = (char*)name;
	kdev_t kdev;
	int i;
	int error = -EINVAL;
	
	if(*ptr++ != '[')
		goto done;
	dev = simple_strtoul(ptr, &ptr, 16);
	if(*ptr++ != ']')
		goto done;
	if(*ptr++ != ':')
		goto done;
	ino = simple_strtoul(ptr, &ptr, 0);
	if((long)ptr - (long)name != len)
		goto done;

	error = -ENOENT;
	kdev = to_kdev_t(dev);
	if(!kdev)
		goto done;
	for(i = 0; i < NR_SUPER; i++)
		if(super_blocks[i].s_dev == kdev)
			break;
	if(i < NR_SUPER) {
		*result = iget(&super_blocks[i], ino);
		if(*result)
			error = 0;
	}
done:
	return error;
}
