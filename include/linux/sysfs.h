/*
 * sysfs.h - definitions for the device driver filesystem
 *
 * Copyright (c) 2001,2002 Patrick Mochel
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#ifndef _SYSFS_H_
#define _SYSFS_H_

struct kobject;

struct attribute {
	char			* name;
	mode_t			mode;
};

struct sysfs_ops {
	ssize_t	(*show)(struct kobject *, struct attribute *,char *, size_t, loff_t);
	ssize_t	(*store)(struct kobject *,struct attribute *,const char *, size_t, loff_t);
};

extern int
sysfs_create_dir(struct kobject *);

extern void
sysfs_remove_dir(struct kobject *);

extern int
sysfs_create_file(struct kobject *, struct attribute *);

extern void
sysfs_remove_file(struct kobject *, struct attribute *);

extern int 
sysfs_create_link(struct kobject * kobj, struct kobject * target, char * name);

extern void
sysfs_remove_link(struct kobject *, char * name);

#endif /* _SYSFS_H_ */
