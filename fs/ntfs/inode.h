/*
 *  inode.h
 *  Header file for inode.c
 *
 *  Copyright (C) 1997 Régis Duchesne
 */

ntfs_attribute *ntfs_find_attr(ntfs_inode *ino, int type, char *name);
int ntfs_read_attr(ntfs_inode *ino, int type, char *name, int offset,
  ntfs_io *buf);
int ntfs_write_attr(ntfs_inode *ino, int type, char *name, int offset,
  ntfs_io *buf);
int ntfs_init_inode(ntfs_inode *ino,ntfs_volume *vol,int inum);
void ntfs_clear_inode(ntfs_inode *ino);
int ntfs_check_mft_record(ntfs_volume *vol,char *record);
int ntfs_alloc_inode (ntfs_inode *dir, ntfs_inode *result, char *filename,
  int namelen);
int ntfs_update_inode(ntfs_inode *ino);
int ntfs_vcn_to_lcn(ntfs_inode *ino, int vcn);
int ntfs_readwrite_attr(ntfs_inode *ino, ntfs_attribute *attr, int offset,
  ntfs_io *dest);
int ntfs_allocate_attr_number(ntfs_inode *ino, int *result);
int ntfs_decompress_run(unsigned char **data, int *length, int *cluster,
  int *ctype);
void ntfs_decompress(unsigned char *dest, unsigned char *src, ntfs_size_t l);
