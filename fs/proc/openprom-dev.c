/*
 *  linux/fs/proc/openprom-dev.c
 *
 *  handling of devices attached to openpromfs.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>

struct openpromfs_dev *openprom_devices = NULL;
static ino_t openpromdev_ino = PROC_OPENPROMD_FIRST;

int proc_openprom_regdev(struct openpromfs_dev *d)
{
	if (openpromdev_ino == PROC_OPENPROMD_FIRST + PROC_NOPENPROMD)
		return -1;
	d->next = openprom_devices;
	d->inode = openpromdev_ino++;
	openprom_devices = d;
	return 0;
}

int proc_openprom_unregdev(struct openpromfs_dev *d)
{
	if (d == openprom_devices) {
		openprom_devices = d->next;
	} else if (!openprom_devices)
		return -1;
	else {
		struct openpromfs_dev *p;
		
		for (p = openprom_devices; p->next != d && p->next; p = p->next);
		if (!p->next) return -1;
		p->next = d->next;
	}
	return 0;
}

#if defined(CONFIG_SUN_OPENPROMFS_MODULE)
EXPORT_SYMBOL(openprom_devices);
#endif
