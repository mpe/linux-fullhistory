/*
 *  linux/fs/affs/symlink.c
 *
 *  (C) 1995  Joerg Dorchain Modified for Amiga FFS filesystem
 *            based on:
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  isofs symlink handling code.  This is only used with the Rock Ridge
 *  extensions to iso9660
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include <linux/affs_fs.h>

#include "amigaffs.h"

static int affs_readlink(struct inode *, char *, int);
static int affs_follow_link(struct inode *, struct inode *, int, int, struct inode **);

/*
 * symlinks can't do much...
 */
struct inode_operations affs_symlink_inode_operations = {
	NULL,			/* no file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	affs_readlink,		/* readlink */
	affs_follow_link,	/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int affs_follow_link(struct inode * dir, struct inode * inode,
	int flag, int mode, struct inode ** res_inode)
{
	int error;
	char * pnt;
	struct buffer_head *bh;
	struct symlink_front *sy_data;

	if (!dir) {
		dir = current->fs->root;
		dir->i_count++;
	}
	if (!inode) {
		iput(dir);
		*res_inode = NULL;
		return -ENOENT;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	if (current->link_count > 5) {
		iput(dir);
		iput(inode);
		*res_inode = NULL;
		return -ELOOP;
	}
	if (!(bh = affs_pread(inode,inode->i_ino,(void **)&sy_data))) {
		printk("affs: unable to read block %ld",inode->i_ino);
		return 0;
	}

	pnt = sy_data->symname;
	iput(inode);
	current->link_count++;
	error = open_namei(pnt,flag,mode,res_inode,dir);
	current->link_count--;
	brelse(bh);
	return error;
}

static char *affs_conv_path(char *affs_path)
{
static char unix_path[1024]="/";
int up,ap;
char dp,slash;


dp=1;
slash=1;
ap=0;
up=1;
if (affs_path[0] == 0)
  unix_path[up++]='.';
while ((up < 1020) && (affs_path[ap]!=0))
 {
  switch (affs_path[ap]) {
    case ':':
      if (dp == 0) {
        slash=0;
        unix_path[up++]=':';
      }
      else {
        dp=0;
        slash=1;
        unix_path[up++]='/';
      }
      break;
    case '/':
      if (slash==0) {
        slash=1;
        unix_path[up++]='/';
      }
      else {
        unix_path[up++]='.';
        unix_path[up++]='.';
        unix_path[up++]='/';
      }
      break;
    default:
      slash=0;
      unix_path[up++]=affs_path[ap];
      break;
  }
  ap++;
 }
unix_path[up]=0;
return unix_path+dp;
}


static int affs_readlink(struct inode * inode, char * buffer, int buflen)
{
        char * pnt;
	int i;
	char c;
	struct buffer_head *bh;
	struct symlink_front *sy_data;

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}

	if (buflen > 1023)
		buflen = 1023;
	
	if (!(bh = affs_pread(inode,inode->i_ino,(void **)&sy_data))) {
		printk("affs: unable to read block %ld\n",inode->i_ino);
		return -ENOENT;
	}
	
	iput(inode);

	pnt = sy_data->symname;
	if (inode->i_sb->u.affs_sb.s_options.conv_links != 0)
	  pnt = affs_conv_path(pnt);

	i = 0;

	while (i<buflen && (c = pnt[i])) {
		i++;
		put_fs_byte(c,buffer++);
	}
	
	brelse(bh);

	return i;
}
