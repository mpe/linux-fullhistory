/*
 *  linux/fs/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <asm/namei.h>

#define ACC_MODE(x) ("\000\004\002\006"[(x)&O_ACCMODE])


/*
 * In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 */
static inline int do_getname(const char *filename, char *page)
{
	int retval;
	unsigned long len = PAGE_SIZE;

	if ((unsigned long) filename >= TASK_SIZE) {
		if (get_fs() != KERNEL_DS)
			return -EFAULT;
	} else if (TASK_SIZE - (unsigned long) filename < PAGE_SIZE)
		len = TASK_SIZE - (unsigned long) filename;

	retval = strncpy_from_user((char *)page, filename, len);
	if (retval > 0) {
		if (retval < len)
			return 0;
		return -ENAMETOOLONG;
	} else if (!retval)
		retval = -ENOENT;
	return retval;
}

/*
 * This is a single page for faster getname.
 *   If the page is available when entering getname, use it.
 *   If the page is not available, call __get_free_page instead.
 * This works even though do_getname can block (think about it).
 * -- Michael Chastain, based on idea of Linus Torvalds, 1 Dec 1996.
 */
static unsigned long name_page_cache = 0;

int getname(const char * filename, char **result)
{
	unsigned long page;
	int retval;

	page = name_page_cache;
	name_page_cache = 0;
	if (!page) {
		page = __get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
	}

	retval = do_getname(filename, (char *) page);
	if (retval < 0)
		putname( (char *) page );
	else
		*result = (char *) page;
	return retval;
}

void putname(char * name)
{
	if (name_page_cache == 0)
		name_page_cache = (unsigned long) name;
	else
		free_page((unsigned long) name);
}

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * We use "fsuid" for this, letting us set arbitrary permissions
 * for filesystem access without changing the "normal" uids which
 * are used for other things..
 */
int permission(struct inode * inode,int mask)
{
	int mode = inode->i_mode;

	if (inode->i_op && inode->i_op->permission)
		return inode->i_op->permission(inode, mask);
	else if ((mask & S_IWOTH) && IS_RDONLY(inode) &&
		 (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
		return -EROFS; /* Nobody gets write access to a read-only fs */
	else if ((mask & S_IWOTH) && IS_IMMUTABLE(inode))
		return -EACCES; /* Nobody gets write access to an immutable file */
	else if (current->fsuid == inode->i_uid)
		mode >>= 6;
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	if (((mode & mask & 0007) == mask) || fsuser())
		return 0;
	return -EACCES;
}

/*
 * get_write_access() gets write permission for a file.
 * put_write_access() releases this write permission.
 * This is used for regular files.
 * We cannot support write (and maybe mmap read-write shared) accesses and
 * MAP_DENYWRITE mmappings simultaneously.
 */
int get_write_access(struct inode * inode)
{
	struct task_struct * p;

	if ((inode->i_count > 1) && S_ISREG(inode->i_mode)) /* shortcut */
		for_each_task(p) {
		        struct vm_area_struct * mpnt;
			if (!p->mm)
				continue;
			for(mpnt = p->mm->mmap; mpnt; mpnt = mpnt->vm_next) {
				if (inode != mpnt->vm_inode)
					continue;
				if (mpnt->vm_flags & VM_DENYWRITE)
					return -ETXTBSY;
			}
		}
	inode->i_writecount++;
	return 0;
}

void put_write_access(struct inode * inode)
{
	inode->i_writecount--;
}

/*
 * lookup() looks up one part of a pathname, using the fs-dependent
 * routines (currently minix_lookup) for it. It also checks for
 * fathers (pseudo-roots, mount-points)
 */
int lookup(struct inode * dir,const char * name, int len,
           struct inode ** result)
{
	struct super_block * sb;
	int perm;

	*result = NULL;
	if (!dir)
		return -ENOENT;
/* check permissions before traversing mount-points */
	perm = permission(dir,MAY_EXEC);
	if (len==2 && get_unaligned((u16 *) name) == 0x2e2e) {
		if (dir == current->fs->root) {
			*result = dir;
			return 0;
		} else if ((sb = dir->i_sb) && (dir == sb->s_mounted)) {
			iput(dir);
			dir = sb->s_covered;
			if (!dir)
				return -ENOENT;
			dir->i_count++;
		}
	}
	if (!dir->i_op || !dir->i_op->lookup) {
		iput(dir);
		return -ENOTDIR;
	}
 	if (perm != 0) {
		iput(dir);
		return perm;
	}
	if (!len) {
		*result = dir;
		return 0;
	}
	return dir->i_op->lookup(dir, name, len, result);
}

int follow_link(struct inode * dir, struct inode * inode,
	int flag, int mode, struct inode ** res_inode)
{
	if (!dir || !inode) {
		iput(dir);
		iput(inode);
		*res_inode = NULL;
		return -ENOENT;
	}
	if (!inode->i_op || !inode->i_op->follow_link) {
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	return inode->i_op->follow_link(dir,inode,flag,mode,res_inode);
}

/*
 *	dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
static int dir_namei(const char *pathname, int *namelen, const char **name,
                     struct inode * base, struct inode **res_inode)
{
	unsigned char c;
	const char * thisname;
	int len,error;
	struct inode * inode;

	*res_inode = NULL;
	if (!base) {
		base = current->fs->pwd;
		base->i_count++;
	}
	if ((c = *pathname) == '/') {
		iput(base);
		base = current->fs->root;
		pathname++;
		base->i_count++;
	}
	while (1) {
		thisname = pathname;
		for(len=0;(c = *(pathname++))&&(c != '/');len++)
			/* nothing */ ;
		if (!c)
			break;
		base->i_count++;
		error = lookup(base, thisname, len, &inode);
		if (error) {
			iput(base);
			return error;
		}
		error = follow_link(base,inode,0,0,&base);
		if (error)
			return error;
	}
	if (!base->i_op || !base->i_op->lookup) {
		iput(base);
		return -ENOTDIR;
	}
	*name = thisname;
	*namelen = len;
	*res_inode = base;
	return 0;
}

static int _namei(const char * pathname, struct inode * base,
                  int follow_links, struct inode ** res_inode)
{
	const char *basename;
	int namelen,error;
	struct inode * inode;

	translate_namei(pathname, base, follow_links, res_inode);
	*res_inode = NULL;
	error = dir_namei(pathname, &namelen, &basename, base, &base);
	if (error)
		return error;
	base->i_count++;	/* lookup uses up base */
	error = lookup(base, basename, namelen, &inode);
	if (error) {
		iput(base);
		return error;
	}
	if (follow_links) {
		error = follow_link(base, inode, 0, 0, &inode);
		if (error)
			return error;
	} else
		iput(base);
	*res_inode = inode;
	return 0;
}

int lnamei(const char *pathname, struct inode **res_inode)
{
	int error;
	char * tmp;

	error = getname(pathname, &tmp);
	if (!error) {
		error = _namei(tmp, NULL, 0, res_inode);
		putname(tmp);
	}
	return error;
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
int namei(const char *pathname, struct inode **res_inode)
{
	int error;
	char * tmp;

	error = getname(pathname, &tmp);
	if (!error) {
		error = _namei(tmp, NULL, 1, res_inode);
		putname(tmp);
	}
	return error;
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 *
 * Note that the low bits of "flag" aren't the same as in the open
 * system call - they are 00 - no permissions needed
 *			  01 - read permission needed
 *			  10 - write permission needed
 *			  11 - read/write permissions needed
 * which is a lot more logical, and also allows the "no perm" needed
 * for symlinks (where the permissions are checked later).
 */
int
open_namei(const char * pathname, int flag, int mode,
               struct inode ** res_inode, struct inode * base)
{
	const char * basename;
	int namelen,error;
	struct inode * dir, *inode;

	translate_open_namei(pathname, flag, mode, res_inode, base);
	mode &= S_IALLUGO & ~current->fs->umask;
	mode |= S_IFREG;
	error = dir_namei(pathname, &namelen, &basename, base, &dir);
	if (error)
		return error;
	if (!namelen) {			/* special case: '/usr/' etc */
		if (flag & 2) {
			iput(dir);
			return -EISDIR;
		}
		/* thanks to Paul Pluzhnikov for noticing this was missing.. */
		if ((error = permission(dir,ACC_MODE(flag))) != 0) {
			iput(dir);
			return error;
		}
		*res_inode=dir;
		return 0;
	}
	dir->i_count++;		/* lookup eats the dir */
	if (flag & O_CREAT) {
		down(&dir->i_sem);
		error = lookup(dir, basename, namelen, &inode);
		if (!error) {
			if (flag & O_EXCL) {
				iput(inode);
				error = -EEXIST;
			}
		} else if (IS_RDONLY(dir))
			error = -EROFS;
		else if (!dir->i_op || !dir->i_op->create)
			error = -EACCES;
		else if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0)
			;	/* error is already set! */
		else {
			dir->i_count++;		/* create eats the dir */
			if (dir->i_sb && dir->i_sb->dq_op)
				dir->i_sb->dq_op->initialize(dir, -1);
			error = dir->i_op->create(dir, basename, namelen, mode, res_inode);
			up(&dir->i_sem);
			iput(dir);
			return error;
		}
		up(&dir->i_sem);
	} else
		error = lookup(dir, basename, namelen, &inode);
	if (error) {
		iput(dir);
		return error;
	}
	error = follow_link(dir,inode,flag,mode,&inode);
	if (error)
		return error;
	if (S_ISDIR(inode->i_mode) && (flag & 2)) {
		iput(inode);
		return -EISDIR;
	}
	if ((error = permission(inode,ACC_MODE(flag))) != 0) {
		iput(inode);
		return error;
	}
	if (S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
		/*
		 * 2-Feb-1995 Bruce Perens <Bruce@Pixar.com>
		 * Allow opens of Unix domain sockets and FIFOs for write on
		 * read-only filesystems. Their data does not live on the disk.
		 *
		 * If there was something like IS_NODEV(inode) for
		 * pipes and/or sockets I'd check it here.
		 */
	    	flag &= ~O_TRUNC;
	}
	else if (S_ISBLK(inode->i_mode) || S_ISCHR(inode->i_mode)) {
		if (IS_NODEV(inode)) {
			iput(inode);
			return -EACCES;
		}
		flag &= ~O_TRUNC;
	} else {
		if (IS_RDONLY(inode) && (flag & 2)) {
			iput(inode);
			return -EROFS;
		}
	}
	/*
	 * An append-only file must be opened in append mode for writing
	 */
	if (IS_APPEND(inode) && ((flag & FMODE_WRITE) && !(flag & O_APPEND))) {
		iput(inode);
		return -EPERM;
	}
	if (flag & O_TRUNC) {
		if ((error = get_write_access(inode))) {
			iput(inode);
			return error;
		}
		/*
		 * Refuse to truncate files with mandatory locks held on them
		 */
		error = locks_verify_locked(inode);
		if (error) {
			iput(inode);
			return error;
		}
		if (inode->i_sb && inode->i_sb->dq_op)
			inode->i_sb->dq_op->initialize(inode, -1);
			
		error = do_truncate(inode, 0);
		put_write_access(inode);
		if (error) {
			iput(inode);
			return error;
		}
	} else
		if (flag & FMODE_WRITE)
			if (inode->i_sb && inode->i_sb->dq_op)
				inode->i_sb->dq_op->initialize(inode, -1);
	*res_inode = inode;
	return 0;
}

int do_mknod(const char * filename, int mode, dev_t dev)
{
	const char * basename;
	int namelen, error;
	struct inode * dir;

	mode &= ~current->fs->umask;
	error = dir_namei(filename, &namelen, &basename, NULL, &dir);
	if (error)
		return error;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	if (!dir->i_op || !dir->i_op->mknod) {
		iput(dir);
		return -EPERM;
	}
	dir->i_count++;
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	down(&dir->i_sem);
	error = dir->i_op->mknod(dir,basename,namelen,mode,dev);
	up(&dir->i_sem);
	iput(dir);
	return error;
}

asmlinkage int sys_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	char * tmp;

	lock_kernel();
	error = -EPERM;
	if (S_ISDIR(mode) || (!S_ISFIFO(mode) && !fsuser()))
		goto out;
	error = -EINVAL;
	switch (mode & S_IFMT) {
	case 0:
		mode |= S_IFREG;
		break;
	case S_IFREG: case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
		break;
	default:
		goto out;
	}
	error = getname(filename,&tmp);
	if (!error) {
		error = do_mknod(tmp,mode,dev);
		putname(tmp);
	}
out:
	unlock_kernel();
	return error;
}

/*
 * Some operations need to remove trailing slashes for POSIX.1
 * conformance. For rename we also need to change the behaviour
 * depending on whether we had a trailing slash or not.. (we
 * cannot rename normal files with trailing slashes, only dirs)
 *
 * "dummy" is used to make sure we don't do "/" -> "".
 */
static int remove_trailing_slashes(char * name)
{
	int result;
	char dummy[1];
	char *remove = dummy+1;

	for (;;) {
		char c = *name;
		name++;
		if (!c)
			break;
		if (c != '/') {
			remove = NULL;
			continue;
		}
		if (remove)
			continue;
		remove = name;
	}

	result = 0;
	if (remove) {
		remove[-1] = 0;
		result = 1;
	}

	return result;
}

static int do_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen, error;
	struct inode * dir;

	error = dir_namei(pathname, &namelen, &basename, NULL, &dir);
	if (error)
		return error;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	if (!dir->i_op || !dir->i_op->mkdir) {
		iput(dir);
		return -EPERM;
	}
	dir->i_count++;
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	down(&dir->i_sem);
	error = dir->i_op->mkdir(dir, basename, namelen, mode & 01777 & ~current->fs->umask);
	up(&dir->i_sem);
	iput(dir);
	return error;
}

asmlinkage int sys_mkdir(const char * pathname, int mode)
{
	int error;
	char * tmp;

	lock_kernel();
	error = getname(pathname,&tmp);
	if (!error) {
		remove_trailing_slashes(tmp);
		error = do_mkdir(tmp,mode);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static int do_rmdir(const char * name)
{
	const char * basename;
	int namelen, error;
	struct inode * dir;

	error = dir_namei(name, &namelen, &basename, NULL, &dir);
	if (error)
		return error;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	/*
	 * A subdirectory cannot be removed from an append-only directory
	 */
	if (IS_APPEND(dir)) {
		iput(dir);
		return -EPERM;
	}
	if (!dir->i_op || !dir->i_op->rmdir) {
		iput(dir);
		return -EPERM;
	}
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	down(&dir->i_sem);
	error = dir->i_op->rmdir(dir,basename,namelen);
	up(&dir->i_sem);
	return error;
}

asmlinkage int sys_rmdir(const char * pathname)
{
	int error;
	char * tmp;

	lock_kernel();
	error = getname(pathname,&tmp);
	if (!error) {
		remove_trailing_slashes(tmp);
		error = do_rmdir(tmp);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static int do_unlink(const char * name)
{
	const char * basename;
	int namelen, error;
	struct inode * dir;

	error = dir_namei(name, &namelen, &basename, NULL, &dir);
	if (error)
		return error;
	if (!namelen) {
		iput(dir);
		return -EPERM;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	/*
	 * A file cannot be removed from an append-only directory
	 */
	if (IS_APPEND(dir)) {
		iput(dir);
		return -EPERM;
	}
	if (!dir->i_op || !dir->i_op->unlink) {
		iput(dir);
		return -EPERM;
	}
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	down(&dir->i_sem);
	error = dir->i_op->unlink(dir,basename,namelen);
	up(&dir->i_sem);
	return error;
}

asmlinkage int sys_unlink(const char * pathname)
{
	int error;
	char * tmp;

	lock_kernel();
	error = getname(pathname,&tmp);
	if (!error) {
		error = do_unlink(tmp);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static int do_symlink(const char * oldname, const char * newname)
{
	struct inode * dir;
	const char * basename;
	int namelen, error;

	error = dir_namei(newname, &namelen, &basename, NULL, &dir);
	if (error)
		return error;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	if (!dir->i_op || !dir->i_op->symlink) {
		iput(dir);
		return -EPERM;
	}
	dir->i_count++;
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	down(&dir->i_sem);
	error = dir->i_op->symlink(dir,basename,namelen,oldname);
	up(&dir->i_sem);
	iput(dir);
	return error;
}

asmlinkage int sys_symlink(const char * oldname, const char * newname)
{
	int error;
	char * from, * to;

	lock_kernel();
	error = getname(oldname,&from);
	if (!error) {
		error = getname(newname,&to);
		if (!error) {
			error = do_symlink(from,to);
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}

static int do_link(struct inode * oldinode, const char * newname)
{
	struct inode * dir;
	const char * basename;
	int namelen, error;

	error = dir_namei(newname, &namelen, &basename, NULL, &dir);
	if (error) {
		iput(oldinode);
		return error;
	}
	if (!namelen) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	if (IS_RDONLY(dir)) {
		iput(oldinode);
		iput(dir);
		return -EROFS;
	}
	if (dir->i_dev != oldinode->i_dev) {
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		iput(oldinode);
		return error;
	}
	/*
	 * A link to an append-only or immutable file cannot be created
	 */
	if (IS_APPEND(oldinode) || IS_IMMUTABLE(oldinode)) {
		iput(dir);
		iput(oldinode);
		return -EPERM;
	}
	if (!dir->i_op || !dir->i_op->link) {
		iput(dir);
		iput(oldinode);
		return -EPERM;
	}
	dir->i_count++;
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	down(&dir->i_sem);
	error = dir->i_op->link(oldinode, dir, basename, namelen);
	up(&dir->i_sem);
	iput(dir);
	return error;
}

asmlinkage int sys_link(const char * oldname, const char * newname)
{
	int error;
	char * to;
	struct inode * oldinode;

	lock_kernel();
	error = lnamei(oldname, &oldinode);
	if (error)
		goto out;
	error = getname(newname,&to);
	if (error) {
		iput(oldinode);
		goto out;
	}
	error = do_link(oldinode,to);
	putname(to);
out:
	unlock_kernel();
	return error;
}

static int do_rename(const char * oldname, const char * newname, int must_be_dir)
{
	struct inode * old_dir, * new_dir;
	const char * old_base, * new_base;
	int old_len, new_len, error;

	error = dir_namei(oldname, &old_len, &old_base, NULL, &old_dir);
	if (error)
		return error;
	if ((error = permission(old_dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(old_dir);
		return error;
	}
	if (!old_len || (old_base[0] == '.' &&
	    (old_len == 1 || (old_base[1] == '.' &&
	     old_len == 2)))) {
		iput(old_dir);
		return -EPERM;
	}
	error = dir_namei(newname, &new_len, &new_base, NULL, &new_dir);
	if (error) {
		iput(old_dir);
		return error;
	}
	if ((error = permission(new_dir,MAY_WRITE | MAY_EXEC)) != 0){
		iput(old_dir);
		iput(new_dir);
		return error;
	}
	if (!new_len || (new_base[0] == '.' &&
	    (new_len == 1 || (new_base[1] == '.' &&
	     new_len == 2)))) {
		iput(old_dir);
		iput(new_dir);
		return -EPERM;
	}
	if (new_dir->i_dev != old_dir->i_dev) {
		iput(old_dir);
		iput(new_dir);
		return -EXDEV;
	}
	if (IS_RDONLY(new_dir) || IS_RDONLY(old_dir)) {
		iput(old_dir);
		iput(new_dir);
		return -EROFS;
	}
	/*
	 * A file cannot be removed from an append-only directory
	 */
	if (IS_APPEND(old_dir)) {
		iput(old_dir);
		iput(new_dir);
		return -EPERM;
	}
	if (!old_dir->i_op || !old_dir->i_op->rename) {
		iput(old_dir);
		iput(new_dir);
		return -EPERM;
	}
	new_dir->i_count++;
	if (new_dir->i_sb && new_dir->i_sb->dq_op)
		new_dir->i_sb->dq_op->initialize(new_dir, -1);
	down(&new_dir->i_sem);
	error = old_dir->i_op->rename(old_dir, old_base, old_len, 
		new_dir, new_base, new_len, must_be_dir);
	up(&new_dir->i_sem);
	iput(new_dir);
	return error;
}

asmlinkage int sys_rename(const char * oldname, const char * newname)
{
	int error;
	char * from, * to;

	lock_kernel();
	error = getname(oldname,&from);
	if (!error) {
		error = getname(newname,&to);
		if (!error) {
			error = do_rename(from,to,
				remove_trailing_slashes(from) |
				remove_trailing_slashes(to));
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}
