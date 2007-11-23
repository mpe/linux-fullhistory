/*
 *  linux/fs/readdir.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/file.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

int vfs_readdir(struct file *file,
		int (*filler)(void *,const char *,int,off_t,ino_t),
		void *buf)
{
	struct inode *inode = file->f_dentry->d_inode;
	int res = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;
	down(&inode->i_sem);
	down(&inode->i_zombie);
	res = -ENOENT;
	if (!IS_DEADDIR(inode))
		res = file->f_op->readdir(file, buf, filler);
	up(&inode->i_zombie);
	up(&inode->i_sem);
out:
	return res;
}

int dcache_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	int i;
	struct dentry *dentry = filp->f_dentry;

	i = filp->f_pos;
	switch (i) {
		case 0:
			if (filldir(dirent, ".", 1, i, dentry->d_inode->i_ino) < 0)
				break;
			i++;
			filp->f_pos++;
			/* fallthrough */
		case 1:
			if (filldir(dirent, "..", 2, i, dentry->d_parent->d_inode->i_ino) < 0)
				break;
			i++;
			filp->f_pos++;
			/* fallthrough */
		default: {
			struct list_head *list = dentry->d_subdirs.next;

			int j = i-2;
			for (;;) {
				if (list == &dentry->d_subdirs)
					return 0;
				if (!j)
					break;
				j--;
				list = list->next;
			}

			do {
				struct dentry *de = list_entry(list, struct dentry, d_child);

				if (!d_unhashed(de) && de->d_inode) {
					if (filldir(dirent, de->d_name.name, de->d_name.len, filp->f_pos, de->d_inode->i_ino) < 0)
						break;
				}
				filp->f_pos++;
				list = list->next;
			} while (list != &dentry->d_subdirs);
		}
	}
	return 0;
}

/*
 * Traditional linux readdir() handling..
 *
 * "count=1" is a special case, meaning that the buffer is one
 * dirent-structure in size and that the code can't handle more
 * anyway. Thus the special "fillonedir()" function for that
 * case (the low-level handlers don't need to care about this).
 */
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+sizeof(long)-1) & ~(sizeof(long)-1))

struct old_linux_dirent {
	unsigned long	d_ino;
	unsigned long	d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct readdir_callback {
	struct old_linux_dirent * dirent;
	int count;
};

static int fillonedir(void * __buf, const char * name, int namlen, off_t offset, ino_t ino)
{
	struct readdir_callback * buf = (struct readdir_callback *) __buf;
	struct old_linux_dirent * dirent;

	if (buf->count)
		return -EINVAL;
	buf->count++;
	dirent = buf->dirent;
	put_user(ino, &dirent->d_ino);
	put_user(offset, &dirent->d_offset);
	put_user(namlen, &dirent->d_namlen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	return 0;
}

asmlinkage int old_readdir(unsigned int fd, void * dirent, unsigned int count)
{
	int error;
	struct file * file;
	struct readdir_callback buf;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	lock_kernel();
	error = vfs_readdir(file, fillonedir, &buf);
	if (error >= 0)
		error = buf.count;
	unlock_kernel();

	fput(file);
out:
	return error;
}

/*
 * New, all-improved, singing, dancing, iBCS2-compliant getdents()
 * interface. 
 */
struct linux_dirent {
	unsigned long	d_ino;
	unsigned long	d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct getdents_callback {
	struct linux_dirent * current_dir;
	struct linux_dirent * previous;
	int count;
	int error;
};

static int filldir(void * __buf, const char * name, int namlen, off_t offset, ino_t ino)
{
	struct linux_dirent * dirent;
	struct getdents_callback * buf = (struct getdents_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent)
		put_user(offset, &dirent->d_off);
	dirent = buf->current_dir;
	buf->previous = dirent;
	put_user(ino, &dirent->d_ino);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage long sys_getdents(unsigned int fd, void * dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent * lastdirent;
	struct getdents_callback buf;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = (struct linux_dirent *) dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	lock_kernel();
	error = vfs_readdir(file, filldir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}

out_putf:
	unlock_kernel();
	fput(file);
out:
	return error;
}
