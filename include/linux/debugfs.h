/*
 *  debugfs.h - a tiny little debug file system
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
 */

#ifndef _DEBUGFS_H_
#define _DEBUGFS_H_

#if defined(CONFIG_DEBUG_FS)
struct dentry *debugfs_create_file(const char *name, mode_t mode,
				   struct dentry *parent, void *data,
				   struct file_operations *fops);

struct dentry *debugfs_create_dir(const char *name, struct dentry *parent);

void debugfs_remove(struct dentry *dentry);

struct dentry *debugfs_create_u8(const char *name, mode_t mode,
				 struct dentry *parent, u8 *value);
struct dentry *debugfs_create_u16(const char *name, mode_t mode,
				  struct dentry *parent, u16 *value);
struct dentry *debugfs_create_u32(const char *name, mode_t mode,
				  struct dentry *parent, u32 *value);
struct dentry *debugfs_create_bool(const char *name, mode_t mode,
				  struct dentry *parent, u32 *value);

#else
/* 
 * We do not return NULL from these functions if CONFIG_DEBUG_FS is not enabled
 * so users have a chance to detect if there was a real error or not.  We don't
 * want to duplicate the design decision mistakes of procfs and devfs again.
 */

static inline struct dentry *debugfs_create_file(const char *name, mode_t mode,
						 struct dentry *parent,
						 void *data,
						 struct file_operations *fops)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_dir(const char *name,
						struct dentry *parent)
{
	return ERR_PTR(-ENODEV);
}

static inline void debugfs_remove(struct dentry *dentry)
{ }

static inline struct dentry *debugfs_create_u8(const char *name, mode_t mode,
					       struct dentry *parent,
					       u8 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_u16(const char *name, mode_t mode,
						struct dentry *parent,
						u8 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_u32(const char *name, mode_t mode,
						struct dentry *parent,
						u8 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_bool(const char *name, mode_t mode,
						 struct dentry *parent,
						 u8 *value)
{
	return ERR_PTR(-ENODEV);
}

#endif

#endif
