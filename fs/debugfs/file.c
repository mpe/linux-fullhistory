/*
 *  file.c - part of debugfs, a tiny little debug file system
 *
 *  Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *  Copyright (C) 2004 IBM Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 *  debugfs is for people to use instead of /proc or /sys.
 *  See Documentation/DocBook/kernel-api for more details.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/debugfs.h>

static ssize_t default_read_file(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t default_write_file(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	return count;
}

static int default_open(struct inode *inode, struct file *file)
{
	if (inode->u.generic_ip)
		file->private_data = inode->u.generic_ip;

	return 0;
}

struct file_operations debugfs_file_operations = {
	.read =		default_read_file,
	.write =	default_write_file,
	.open =		default_open,
};

#define simple_type(type, format, temptype, strtolfn)				\
static ssize_t read_file_##type(struct file *file, char __user *user_buf,	\
				size_t count, loff_t *ppos)			\
{										\
	char buf[32];								\
	type *val = file->private_data;						\
										\
	snprintf(buf, sizeof(buf), format, *val);				\
	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));\
}										\
static ssize_t write_file_##type(struct file *file, const char __user *user_buf,\
				 size_t count, loff_t *ppos)			\
{										\
	char *endp;								\
	char buf[32];								\
	int buf_size;								\
	type *val = file->private_data;						\
	temptype tmp;								\
										\
	memset(buf, 0x00, sizeof(buf));						\
	buf_size = min(count, (sizeof(buf)-1));					\
	if (copy_from_user(buf, user_buf, buf_size))				\
		return -EFAULT;							\
										\
	tmp = strtolfn(buf, &endp, 0);						\
	if ((endp == buf) || ((type)tmp != tmp))				\
		return -EINVAL;							\
	*val = tmp;								\
	return count;								\
}										\
static struct file_operations fops_##type = {					\
	.read =		read_file_##type,					\
	.write =	write_file_##type,					\
	.open =		default_open,						\
};
simple_type(u8, "%c", unsigned long, simple_strtoul);
simple_type(u16, "%hi", unsigned long, simple_strtoul);
simple_type(u32, "%i", unsigned long, simple_strtoul);

/**
 * debugfs_create_u8 - create a file in the debugfs filesystem that is used to read and write a unsigned 8 bit value.
 *
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this paramater is NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * NULL or !NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_u8(const char *name, mode_t mode,
				 struct dentry *parent, u8 *value)
{
	return debugfs_create_file(name, mode, parent, value, &fops_u8);
}
EXPORT_SYMBOL_GPL(debugfs_create_u8);

/**
 * debugfs_create_u16 - create a file in the debugfs filesystem that is used to read and write a unsigned 8 bit value.
 *
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this paramater is NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * NULL or !NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_u16(const char *name, mode_t mode,
				  struct dentry *parent, u16 *value)
{
	return debugfs_create_file(name, mode, parent, value, &fops_u16);
}
EXPORT_SYMBOL_GPL(debugfs_create_u16);

/**
 * debugfs_create_u32 - create a file in the debugfs filesystem that is used to read and write a unsigned 8 bit value.
 *
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this paramater is NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * NULL or !NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_u32(const char *name, mode_t mode,
				 struct dentry *parent, u32 *value)
{
	return debugfs_create_file(name, mode, parent, value, &fops_u32);
}
EXPORT_SYMBOL_GPL(debugfs_create_u32);

static ssize_t read_file_bool(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	char buf[3];
	u32 *val = file->private_data;
	
	if (val)
		buf[0] = 'Y';
	else
		buf[0] = 'N';
	buf[1] = '\n';
	buf[2] = 0x00;
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t write_file_bool(struct file *file, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char buf[32];
	int buf_size;
	u32 *val = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	switch (buf[0]) {
	case 'y':
	case 'Y':
	case '1':
		*val = 1;
		break;
	case 'n':
	case 'N':
	case '0':
		*val = 0;
		break;
	}
	
	return count;
}

static struct file_operations fops_bool = {
	.read =		read_file_bool,
	.write =	write_file_bool,
	.open =		default_open,
};

/**
 * debugfs_create_bool - create a file in the debugfs filesystem that is used to read and write a boolean value.
 *
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this paramater is NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * NULL or !NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_bool(const char *name, mode_t mode,
				   struct dentry *parent, u32 *value)
{
	return debugfs_create_file(name, mode, parent, value, &fops_bool);
}
EXPORT_SYMBOL_GPL(debugfs_create_bool);

