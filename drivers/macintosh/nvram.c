/*
 * /dev/nvram driver for Power Macintosh.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/nvram.h>
#include <linux/init.h>
#include <asm/uaccess.h>

#define NVRAM_SIZE	8192

static long long nvram_llseek(struct inode *inode, struct file *file,
			      loff_t offset, int origin)
{
	switch (origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += NVRAM_SIZE;
		break;
	}
	if (offset < 0)
		return -EINVAL;
	file->f_pos = offset;
	return file->f_pos;
}

static long read_nvram(struct inode *inode, struct file *file,
	char *buf, unsigned long count)
{
	unsigned int i = file->f_pos;
	char *p = buf;

	for (; count > 0 && i < NVRAM_SIZE; ++i, ++p, --count)
		put_user(nvram_read_byte(i), p);
	file->f_pos = i;
	return p - buf;
}

static long write_nvram(struct inode *inode, struct file *file,
	const char *buf, unsigned long count)
{
	unsigned int i = file->f_pos;
	const char *p = buf;
	char c;

	for (; count > 0 && i < NVRAM_SIZE; ++i, ++p, --count) {
		get_user(c, p);
		nvram_write_byte(i, c);
	}
	file->f_pos = i;
	return p - buf;
}

static int nvram_open(struct inode *inode, struct file *file)
{
	return 0;
}

struct file_operations nvram_fops = {
	nvram_llseek,
	read_nvram,
	write_nvram,
	NULL,		/* nvram_readdir */
	NULL,		/* nvram_select */
	NULL,		/* nvram_ioctl */
	NULL,		/* nvram_mmap */
	nvram_open,
	NULL,		/* no special release code */
	NULL		/* fsync */
};

static struct miscdevice nvram_dev = {
	NVRAM_MINOR,
	"nvram",
	&nvram_fops
};

__initfunc(int nvram_init(void))
{
	misc_register(&nvram_dev);
	return 0;
}
