/*
 *  linux/fs/stat.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

static void cp_old_stat(struct inode * inode, struct old_stat * statbuf)
{
	struct old_stat tmp;

	if (inode->i_ino & 0xffff0000)
		printk("Warning: using old stat() call on bigfs\n");
	verify_area(statbuf,sizeof (*statbuf));
	tmp.st_dev = inode->i_dev;
	tmp.st_ino = inode->i_ino;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlink;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = inode->i_rdev;
	tmp.st_size = inode->i_size;
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;
	memcpy_tofs(statbuf,&tmp,sizeof(tmp));
}

static void cp_new_stat(struct inode * inode, struct new_stat * statbuf)
{
	struct new_stat tmp = {0, };

	verify_area(statbuf,sizeof (*statbuf));
	tmp.st_dev = inode->i_dev;
	tmp.st_ino = inode->i_ino;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlink;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = inode->i_rdev;
	tmp.st_size = inode->i_size;
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;
	memcpy_tofs(statbuf,&tmp,sizeof(tmp));
}

int sys_stat(char * filename, struct old_stat * statbuf)
{
	struct inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	cp_old_stat(inode,statbuf);
	iput(inode);
	return 0;
}

int sys_newstat(char * filename, struct new_stat * statbuf)
{
	struct inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	cp_new_stat(inode,statbuf);
	iput(inode);
	return 0;
}

int sys_lstat(char * filename, struct old_stat * statbuf)
{
	struct inode * inode;

	if (!(inode = lnamei(filename)))
		return -ENOENT;
	cp_old_stat(inode,statbuf);
	iput(inode);
	return 0;
}

int sys_newlstat(char * filename, struct new_stat * statbuf)
{
	struct inode * inode;

	if (!(inode = lnamei(filename)))
		return -ENOENT;
	cp_new_stat(inode,statbuf);
	iput(inode);
	return 0;
}

int sys_fstat(unsigned int fd, struct old_stat * statbuf)
{
	struct file * f;
	struct inode * inode;

	if (fd >= NR_OPEN || !(f=current->filp[fd]) || !(inode=f->f_inode))
		return -EBADF;
	cp_old_stat(inode,statbuf);
	return 0;
}

int sys_newfstat(unsigned int fd, struct new_stat * statbuf)
{
	struct file * f;
	struct inode * inode;

	if (fd >= NR_OPEN || !(f=current->filp[fd]) || !(inode=f->f_inode))
		return -EBADF;
	cp_new_stat(inode,statbuf);
	return 0;
}

int sys_readlink(const char * path, char * buf, int bufsiz)
{
	struct inode * inode;

	if (bufsiz <= 0)
		return -EINVAL;
	verify_area(buf,bufsiz);
	if (!(inode = lnamei(path)))
		return -ENOENT;
	if (!inode->i_op || !inode->i_op->readlink) {
		iput(inode);
		return -EINVAL;
	}
	return inode->i_op->readlink(inode,buf,bufsiz);
}
