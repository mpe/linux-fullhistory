#include "os.h"
#define __KERNEL_SYSCALLS__
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/unistd.h>
#include <asm/uaccess.h>

static int errno;
static int do_mod_firmware_load(const char *fn, char **fp)
{
	int fd;
	long l;
	char *dp;

	fd = open(fn, 0, 0);
	if (fd == -1)
	{
		printk(KERN_INFO "Unable to load '%s'.\n", fn);
		return 0;
	}
	l = lseek(fd, 0L, 2);
	if (l <= 0 || l > 131072)
	{
		printk(KERN_INFO "Invalid firmware '%s'\n", fn);
		sys_close(fd);
		return 0;
	}
	lseek(fd, 0L, 0);
	dp = vmalloc(l);
	if (dp == NULL)
	{
		printk(KERN_INFO "Out of memory loading '%s'.\n", fn);
		sys_close(fd);
		return 0;
	}
	if (read(fd, dp, l) != l)
	{
		printk(KERN_INFO "Failed to read '%s'.\n", fn);
		vfree(dp);
		sys_close(fd);
		return 0;
	}
	close(fd);
	*fp = dp;
	return (int) l;
}

int mod_firmware_load(const char *fn, char **fp)
{
	int r;
	mm_segment_t fs = get_fs();

	set_fs(get_ds());
	r = do_mod_firmware_load(fn, fp);
	set_fs(fs);
	return r;
}

