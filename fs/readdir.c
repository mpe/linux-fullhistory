/*
 *  fs/readdir.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#ifdef CONFIG_TRANS_NAMES
#include <linux/nametrans.h>
#endif
#include <linux/dalloc.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

/* [T.Schoebel-Theuer] I am assuming that directories never get too large.
 * The problem is that getdents() delivers d_offset's that can be used
 * for lseek() by the user, so I must encode the status information for
 * name translation and dcache baskets in the offset.
 * Note that the linux man page getdents(2) does not mention that
 * the d_offset is fs-specific and can be used for lseek().
 */
#define BASKET_BIT (1<<30) /* 31 is already used by affs */
#define TRANS_BIT  (1<<29)

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
	struct file * file;
	int translate;
	off_t oldoffset;
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
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
#ifdef CONFIG_TRANS_NAMES
	if(!buf->translate) {
		char * cut;
#ifdef CONFIG_TRANS_RESTRICT
		struct inode * inode = buf->file->f_inode;
		cut = testname(inode && inode->i_gid != CONFIG_TRANS_GID, dirent->d_name);
#else
		cut = testname(1, dirent->d_name);
#endif
		if(cut) {
			put_user(0, cut);
			buf->translate = 1;
		}
	}
#endif
	put_user(ino, &dirent->d_ino);
	put_user(offset, &dirent->d_offset);
	put_user(namlen, &dirent->d_namlen);
	return 0;
}

asmlinkage int old_readdir(unsigned int fd, void * dirent, unsigned int count)
{
	int error = -EBADF;
	struct file * file;
	struct readdir_callback buf;
	off_t oldpos;

	lock_kernel();
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		goto out;
	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;
	error = verify_area(VERIFY_WRITE, dirent, sizeof(struct old_linux_dirent));
	if (error)
		goto out;
	oldpos = file->f_pos;
	buf.file = file;
	buf.dirent = dirent;
	buf.count = 0;
	buf.translate = 0;
	if(file->f_pos & TRANS_BIT) {
		file->f_pos &= ~TRANS_BIT;
		buf.translate = 1;
	}
	error = file->f_op->readdir(file->f_inode, file, &buf, fillonedir);
	if (error < 0)
		goto out;
	if(buf.translate) {
		file->f_pos = oldpos | TRANS_BIT;
	}
	error = buf.count;
out:
	unlock_kernel();
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
	struct file * file;
	int count;
        int error;
        int restricted;
	int do_preload;
};

static int filldir(void * __buf, const char * name, int namlen, off_t offset, ino_t ino)
{
	struct linux_dirent * dirent;
	struct getdents_callback * buf = (struct getdents_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	/* Do not touch buf->error any more if everything is ok! */
	if (reclen > buf->count)
		return 	(buf->error = -EINVAL);
#ifdef CONFIG_DCACHE_PRELOAD
	if(buf->do_preload && (name[0] != '.' || namlen > 2)) {
		struct qstr qname = { name, namlen };
		struct inode * dir = buf->file->f_inode;
		d_entry_preliminary(dir->i_dentry, &qname, ino);
	}
#endif
	dirent = buf->current_dir;
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
#ifdef CONFIG_TRANS_NAMES
	{
		char * cut;
#ifdef CONFIG_TRANS_RESTRICT
		cut = testname(buf->restricted, dirent->d_name);
#else
		cut = testname(1, dirent->d_name);
#endif
		if(cut) {
			int newlen = (int)cut - (int)dirent->d_name;
			int newreclen = ROUND_UP(NAME_OFFSET(dirent) + newlen + 1);
			/* Either both must fit or none. This way we need
			 * no status information in f_pos */
			if (reclen+newlen > buf->count)
				return -EINVAL;
			put_user(0, cut);
			put_user(ino, &dirent->d_ino);
			put_user(newreclen, &dirent->d_reclen);
			put_user(offset, &dirent->d_off);
			((char *) dirent) += newreclen;
			buf->count -= newreclen;
			put_user(offset, &dirent->d_off);
			copy_to_user(dirent->d_name, name, namlen);
			put_user(0, dirent->d_name + namlen);
		}
	}
#endif
	put_user(ino, &dirent->d_ino);
	put_user(reclen, &dirent->d_reclen);
	if (buf->previous)
		put_user(buf->file->f_pos, &buf->previous->d_off);
	buf->previous = dirent;
	((char *) dirent) += reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage int sys_getdents(unsigned int fd, void * dirent, unsigned int count)
{
	struct file * file;
	struct getdents_callback buf;
	int error = -EBADF;

	lock_kernel();
	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		goto out;
	error = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;
	error = verify_area(VERIFY_WRITE, dirent, count);
	if (error)
		goto out;
	buf.file = file;
	buf.current_dir = (struct linux_dirent *) dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;
	buf.restricted = 0;
#ifdef CONFIG_TRANS_RESTRICT
	buf.restricted = file->f_inode && file->f_inode->i_gid != CONFIG_TRANS_GID;
#endif
	buf.do_preload = 0;
#ifdef CONFIG_DCACHE_PRELOAD
	if(file->f_inode && file->f_inode->i_dentry &&
	   !(file->f_inode->i_sb->s_type->fs_flags & (FS_NO_DCACHE|FS_NO_PRELIM)) &&
	   !(file->f_inode->i_dentry->d_flag & D_PRELOADED))
		buf.do_preload = 1;
#endif
	
	if(!(file->f_pos & BASKET_BIT)) {
		int oldcount;
		do {
			oldcount = buf.count;
			error = file->f_op->readdir(file->f_inode, file, &buf, filldir);
			if (error < 0)
				goto out;
		} while(!buf.error && buf.count != oldcount);
	}
	if(!buf.error) {
		int nr = 0;
		struct dentry * list = file->f_inode ?
			d_basket(file->f_inode->i_dentry) : NULL;
		struct dentry * ptr = list;
#ifdef CONFIG_DCACHE_PRELOAD
		if(buf.do_preload) {
			buf.do_preload = 0;
			file->f_inode->i_dentry->d_flag |= D_PRELOADED;
		}
#endif
		if(ptr) {
			if(!(file->f_pos & BASKET_BIT))
				file->f_pos = BASKET_BIT;
			do {
				struct dentry * next = ptr->d_basket_next;
				struct inode * inode;
				/* vfs_locks() are missing here */
				inode = d_inode(&ptr);
				if(inode) {
					nr++;
					if(nr > (file->f_pos & ~BASKET_BIT)) {
						int err = filldir(&buf, ptr->d_name,
								  ptr->d_len,
								  file->f_pos,
								  inode->i_ino);
						if(err)
							break;
						file->f_pos++;
					}
					iput(inode);
				}
				ptr = next;
			} while(ptr != list);
		}
	}
	if (!buf.previous) {
		error = buf.error;
	} else {
		put_user(file->f_pos, &buf.previous->d_off);
		error = count - buf.count;
	}
out:
	unlock_kernel();
	return error;
}
