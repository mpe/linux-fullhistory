/*
 * JFFS -- Journaling Flash File System, Linux implementation.
 *
 * Copyright (C) 1999, 2000  Axis Communications AB.
 *
 * Created by Finn Hakansson <finn@axis.com>.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * $Id: intrep.h,v 1.2 2000/05/24 13:13:56 alex Exp $
 *
 */

#ifndef __LINUX_JFFS_INTREP_H__
#define __LINUX_JFFS_INTREP_H__

inline int jffs_min(int a, int b);
inline int jffs_max(int a, int b);
__u32 jffs_checksum(const void *data, int size);

void jffs_cleanup_control(struct jffs_control *c);
int jffs_build_fs(struct super_block *sb);

int jffs_insert_node(struct jffs_control *c, struct jffs_file *f,
		     const struct jffs_raw_inode *raw_inode,
		     const char *name, struct jffs_node *node);
struct jffs_file *jffs_find_file(struct jffs_control *c, __u32 ino);
struct jffs_file *jffs_find_child(struct jffs_file *dir, const char *name, int len);

void jffs_free_node(struct jffs_node *node);

int jffs_foreach_file(struct jffs_control *c, int (*func)(struct jffs_file *));
int jffs_free_node_list(struct jffs_file *f);
int jffs_possibly_delete_file(struct jffs_file *f);
int jffs_build_file(struct jffs_file *f);
int jffs_insert_file_into_hash(struct jffs_file *f);
int jffs_insert_file_into_tree(struct jffs_file *f);
int jffs_unlink_file_from_hash(struct jffs_file *f);
int jffs_unlink_file_from_tree(struct jffs_file *f);
int jffs_remove_redundant_nodes(struct jffs_file *f);
int jffs_file_count(struct jffs_file *f);

int jffs_write_node(struct jffs_control *c, struct jffs_node *node,
		    struct jffs_raw_inode *raw_inode,
		    const char *name, const unsigned char *buf);
int jffs_read_data(struct jffs_file *f, char *buf, __u32 read_offset, __u32 size);

/* Garbage collection stuff.  */
int jffs_garbage_collect(struct jffs_control *c);

/* For debugging purposes.  */
void jffs_print_node(struct jffs_node *n);
void jffs_print_raw_inode(struct jffs_raw_inode *raw_inode);
int jffs_print_file(struct jffs_file *f);
void jffs_print_hash_table(struct jffs_control *c);
void jffs_print_tree(struct jffs_file *first_file, int indent);

struct buffer_head *jffs_get_write_buffer(kdev_t dev, int block);
void jffs_put_write_buffer(struct buffer_head *bh);

#endif /* __LINUX_JFFS_INTREP_H__  */
