/*
 *  attr.h
 *  Header file for attr.c
 *
 *  Copyright (C) 1997 Régis Duchesne
 */

int ntfs_extend_attr(ntfs_inode *ino, ntfs_attribute *attr, int *len,
  int flags);
int ntfs_resize_attr(ntfs_inode *ino, ntfs_attribute *attr, int newsize);
int ntfs_insert_attribute(ntfs_inode *ino, unsigned char* attrdata);
int ntfs_read_compressed(ntfs_inode *ino, ntfs_attribute *attr, int offset,
  ntfs_io *dest);
int ntfs_write_compressed(ntfs_inode *ino, ntfs_attribute *attr, int offset,
  ntfs_io *dest);
int ntfs_create_attr(ntfs_inode *ino, int anum, char *aname, void *data,
  int dsize, ntfs_attribute **rattr);
int ntfs_read_zero(ntfs_io *dest,int size);
int ntfs_make_attr_nonresident(ntfs_inode *ino, ntfs_attribute *attr);
int ntfs_attr_allnonresident(ntfs_inode *ino);
