/*
 *  linux/fs/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

/* [Feb 1997 T. Schoebel-Theuer] Complete rewrite of the pathname
 * lookup logic.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/dalloc.h>
#include <linux/nametrans.h>
#include <linux/proc_fs.h>
#include <linux/omirr.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <asm/semaphore.h>
#include <asm/namei.h>

/* This can be removed after the beta phase. */
#define CACHE_SUPERVISE	/* debug the correctness of dcache entries */
#undef DEBUG		/* some other debugging */


/* local flags for __namei() */
#define NAM_SEMLOCK        8 /* set a semlock on the last dir */
#define NAM_TRANSCREATE   16 /* last component may be created, try "=CREATE#" suffix*/
#define NAM_NO_TRAILSLASH 32 /* disallow trailing slashes by returning EISDIR */
#define ACC_MODE(x) ("\000\004\002\006"[(x)&O_ACCMODE])

/* [Feb-1997 T. Schoebel-Theuer]
 * Fundamental changes in the pathname lookup mechanisms (namei)
 * were necessary because of omirr.  The reason is that omirr needs
 * to know the _real_ pathname, not the user-supplied one, in case
 * of symlinks (and also when transname replacements occur).
 *
 * The new code replaces the old recursive symlink resolution with
 * an iterative one (in case of non-nested symlink chains).  It does
 * this by looking up the symlink name from the particular filesystem,
 * and then follows this name as if it were a user-supplied one.  This
 * is done solely in the VFS level, such that <fs>_follow_link() is not
 * used any more and could be removed in future.  As a side effect,
 * dir_namei(), _namei() and follow_link() are now replaced with a single
 * function __namei() that can handle all the special cases of the former
 * code.
 *
 * With the new dcache, the pathname is stored at each inode, at least as
 * long as the refcount of the inode is positive.  As a side effect, the
 * size of the dcache depends on the inode cache and thus is dynamic.
 */

/* [24-Feb-97 T. Schoebel-Theuer] Side effects caused by new implementation:
 * New symlink semantics: when open() is called with flags O_CREAT | O_EXCL
 * and the name already exists in form of a symlink, try to create the new
 * name indicated by the symlink. The old code always complained that the
 * name already exists, due to not following the symlink even if its target
 * is non-existant.  The new semantics affects also mknod() and link() when
 * the name is a symlink pointing to a non-existant name.
 *
 * I don't know which semantics is the right one, since I have no access
 * to standards. But I found by trial that HP-UX 9.0 has the full "new"
 * semantics implemented, while SunOS 4.1.1 and Solaris (SunOS 5.4) have the
 * "old" one. Personally, I think the new semantics is much more logical.
 * Note that "ln old new" where "new" is a symlink pointing to a non-existing
 * file does succeed in both HP-UX and SunOs, but not in Solaris
 * and in the old Linux semantics.
 */

static char * quicklist = NULL;
static int quickcount = 0;
struct semaphore quicklock = MUTEX;

/* Tuning: increase locality by reusing same pages again...
 * if quicklist becomes too long on low memory machines, either a limit
 * should be added or after a number of cycles some pages should
 * be released again ...
 */
static inline char * get_page(void)
{
	char * res;
	down(&quicklock);
	res = quicklist;
	if(res) {
#ifdef DEBUG
		char * tmp = res;
		int i;
		for(i=0; i<quickcount; i++)
			tmp = *(char**)tmp;
		if(tmp)
			printk("bad quicklist %x\n", (int)tmp);
#endif
		quicklist = *(char**)res;
		quickcount--;
	}
	else
		res = (char*)__get_free_page(GFP_KERNEL);
	up(&quicklock);
	return res;
}

inline void putname(char * name)
{
	if(name) {
		down(&quicklock);
		*(char**)name = quicklist;
		quicklist = name;
		quickcount++;
		up(&quicklock);
	}
	/* if a quicklist limit is necessary to introduce, call
	 * free_page((unsigned long) name);
	 */
}

/* In order to reduce some races, while at the same time doing additional
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

int getname(const char * filename, char **result)
{
	char *tmp;
	int retval;

	tmp = get_page();
	if(!tmp)
		return -ENOMEM;
	retval = do_getname(filename, tmp);
	if (retval < 0)
		putname(tmp);
	else
		*result = tmp;
	return retval;
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
 * MAP_DENYWRITE mmappings simultaneously. The i_writecount field of an inode
 * can have the following values:
 * 0: no writers, no VM_DENYWRITE mappings
 * < 0: (-i_writecount) vm_area_structs with VM_DENYWRITE set exist
 * > 0: (i_writecount) users are writing to the file.
 */
int get_write_access(struct inode * inode)
{
	if (inode->i_writecount < 0)
		return -ETXTBSY;
	inode->i_writecount++;
	return 0;
}

void put_write_access(struct inode * inode)
{
	inode->i_writecount--;
}

static /*inline */ int concat(struct qstr * name, struct qstr * appendix, char * buf)
{
	int totallen = name->len;
	if(name->len > MAX_TRANS_FILELEN ||
	   appendix->len > MAX_TRANS_SUFFIX) {
		return -ENAMETOOLONG;
	}
	memcpy(buf, name->name, name->len);
	memcpy(buf + name->len, appendix->name, appendix->len);
	totallen += appendix->len;
	buf[totallen] = '\0';
	return totallen;
}

/* Internal lookup() using the new generic dcache.
 * buf must only be supplied if appendix!=NULL.
 */
static int cached_lookup(struct inode * dir, struct qstr * name,
			 struct qstr * appendix, char * buf,
			 struct qstr * res_name, struct dentry ** res_entry,
			 struct inode ** result)
{
	struct qstr tmp = { name->name, name->len };
	int error;
	struct dentry * cached;

	*result = NULL;
	if(name->len >= D_MAXLEN)
		return -ENAMETOOLONG;
	vfs_lock();
	cached = d_lookup(dir, name, appendix);
	if(cached) {
		struct inode *inode = NULL;

		if(cached->u.d_inode && (inode = d_inode(&cached))) {
			error = 0;
			if(appendix && res_name) {
				tmp.len = error = concat(name, appendix, buf);
				tmp.name = buf;
				if(error > 0)
					error = 0;
			}
		} else {
			error = -ENOENT;
		}
		vfs_unlock();
		if(res_entry)
			*res_entry = cached;

		/* Since we are bypassing the iget() mechanism, we have to
		 * fabricate the act of crossing any mount points.
		 */
		if(!error && inode && inode->i_mount) {
			do {
				struct inode *mnti = inode->i_mount;
				iinc(mnti);
				iput(inode);
				inode = mnti;
			} while(inode->i_mount);
		}
		*result = inode;
		goto done;
	} else
		vfs_unlock();

	if(appendix) {
		tmp.len = error = concat(name, appendix, buf);
		tmp.name = buf;
		if(error < 0)
			goto done;
	}
	atomic_inc(&dir->i_count);
	error = dir->i_op->lookup(dir, tmp.name, tmp.len, result);
	if(dir->i_dentry && tmp.len &&
	   (!error || (error == -ENOENT && (!dir->i_sb || !dir->i_sb->s_type ||
	    !(dir->i_sb->s_type->fs_flags & FS_NO_DCACHE))))) {
		struct dentry * res;
		vfs_lock();
		res = d_entry(dir->i_dentry, &tmp, error ? NULL : *result);
		vfs_unlock();
		if(res_entry)
			*res_entry = res;
	}
done:
	if(res_name) {
                if(error) {
		        res_name->name = name->name;
		        res_name->len = name->len;
                } else {
		        res_name->name = tmp.name;
		        res_name->len = tmp.len;
                }
	}
	return error;
}

#ifdef CONFIG_TRANS_NAMES
/* If a normal filename is seen, try to determine whether a
 * "#keyword=context#" file exists and return the new filename.
 * If the name is to be created (create_mode), check whether a
 * "#keyword=CREATE" name exists and optionally return the corresponding
 * context name even if it didn't exist before.
 */
static int check_suffixes(struct inode * dir, struct qstr * name,
			  int create_mode, char * buf,
			  struct qstr * res_name, struct dentry ** res_entry,
			  struct inode ** result)
{
	struct translations * trans;
	char * env;
	struct qstr * suffixes;
	int i;
	int error = -ENOENT;

	if(!buf)
		panic("buf==NULL");
	env = env_transl();
#ifdef CONFIG_TRANS_RESTRICT
	if(!env && dir->i_gid != CONFIG_TRANS_GID) {
		return error;
        }
#endif
	trans = get_translations(env);
	suffixes = create_mode ? trans->c_name : trans->name;
	for(i = 0; i < trans->count; i++) {
		error = cached_lookup(dir, name, &suffixes[i],
				      buf, res_name, res_entry, result);
		if(!error) {
			if(res_name && create_mode) {
				/* buf == res_name->name, but is writable */
				memcpy(buf + name->len,
				       trans->name[i].name,
				       trans->name[i].len);
				res_name->len = name->len + trans->name[i].len;
                                buf[res_name->len] = '\0';
			}
			break;
		}
	}
	if(env)
		free_page((unsigned long)trans);
	return error;
}

#endif

/* Any operations involving reserved names at the VFS level should go here. */
static /*inline*/ int reserved_lookup(struct inode * dir, struct qstr * name,
				      int create_mode, char * buf,
				      struct inode ** result)
{
	int error = -ENOENT;
	if(name->name[0] == '.') {
		if(name->len == 1) {
			*result = dir;
			error = 0;
		} else if (name->len==2 && name->name[1] == '.') {
			if (dir == current->fs->root) {
				*result = dir;
				error = 0;
			}
			else if(dir->i_dentry) {
				error = 0;
				*result = dir->i_dentry->d_parent->u.d_inode;
				if(!*result) {
					printk("dcache parent directory is lost");
					error = -ESTALE;	/* random error */
				}
			}
		}
		if(!error)
			atomic_inc(&(*result)->i_count);
	}
	return error;
}

/* In difference to the former version, lookup() no longer eats the dir. */
static /*inline*/ int lookup(struct inode * dir, struct qstr * name, int create_mode,
			     char * buf, struct qstr * res_name,
			     struct dentry ** res_entry, struct inode ** result)
{
	int perm;

	*result = NULL;
	perm = -ENOENT;
 	if (!dir)
		goto done;

	/* Check permissions before traversing mount-points. */
	perm = permission(dir,MAY_EXEC);
 	if (perm)
		goto done;
	perm = reserved_lookup(dir, name, create_mode, buf, result);
	if(!perm) {
		if(res_name) {
			res_name->name = name->name;
			res_name->len = name->len;
		}
		goto done;
	}
	perm = -ENOTDIR;
	if (!dir->i_op || !dir->i_op->lookup)
		goto done;
#ifdef CONFIG_TRANS_NAMES /* try suffixes */
	perm = check_suffixes(dir, name, 0, buf, res_name, res_entry, result);
	if(perm) /* try original name */
#endif
		perm = cached_lookup(dir, name, NULL, buf, res_name, res_entry, result);
#ifdef CONFIG_TRANS_NAMES
	if(perm == -ENOENT && create_mode) { /* try the =CREATE# suffix */
		struct inode * dummy;
		if(!check_suffixes(dir, name, 1, buf, res_name, NULL, &dummy)) {
			iput(dummy);
		}
	}
#endif
done:
	return perm;
}

/* [8-Feb-97 T. Schoebel-Theuer] follow_link() modified for generic operation
 * on the VFS layer: first call <fs>_readlink() and then open_namei().
 * All <fs>_follow_link() are not used any more and may be eliminated
 * (by Linus; I refrained in order to not break other patches).
 * Single exeption is procfs, where proc_follow_link() is used
 * internally (and perhaps should be rewritten).
 * Note: [partly obsolete] I removed parameters flag and mode, since now
 * __namei() is called instead of open_namei(). In the old semantics,
 * the _last_ instance of open_namei() did the real create() if O_CREAT was
 * set and the name existed already in form of a symlink. This has been
 * simplified now, and also the semantics when combined with O_EXCL has changed.
 ****************************************************************************
 * [13-Feb-97] Complete rewrite -> functionality of reading symlinks factored
 * out into _read_link(). The above notes remain valid in principle.
 */
static /*inline*/ int _read_link(struct inode * inode, char ** linkname, int loopcount)
{
	unsigned long old_fs;
	int error;

	error = -ENOSYS;
	if (!inode->i_op || !inode->i_op->readlink)
		goto done;
	error = -ELOOP;
	if (current->link_count + loopcount > 10)
		goto done;
	error = -ENOMEM;
	if(!*linkname && !(*linkname = get_page()))
		goto done;
	if (DO_UPDATE_ATIME(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	atomic_inc(&inode->i_count);
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	error = inode->i_op->readlink(inode, *linkname, PAGE_SIZE);
	set_fs(old_fs);
	if(!error) {
		error = -ENOENT; /* ? or other error code ? */
	} else if(error > 0) {
		(*linkname)[error] = '\0';
		error = 0;
	}
done:
	iput(inode);
	return error;
}

/* [13-Feb-97 T. Schoebel-Theuer] complete rewrite:
 * merged dir_name(), _namei() and follow_link() into one new routine
 * that obeys all the special cases hidden in the old routines in a
 * (hopefully) systematic way:
 * parameter retrieve_mode is bitwise or'ed of the ST_* flags.
 * if res_inode is a NULL pointer, dont try to retrieve the last component
 * at all. Parameters with prefix last_ are used only if res_inode is
 * non-NULL and refer to the last component of the path only.
 */
int __namei(int retrieve_mode, const char * name, struct inode * base,
	    char * buf, struct inode ** res_dir, struct inode ** res_inode,
	    struct qstr * last_name, struct dentry ** last_entry,
	    int * last_error)
{
	char c;
	struct qstr this;
	char * linkname = NULL;
	char * oldlinkname = NULL;
	int trail_flag = 0;
	int loopcount = 0;
	int error;
#ifdef DEBUG
	if(last_name) {
		last_name->name = "(Uninitialized)";
		last_name->len = 15;
	}
#endif
again:
	error = -ENOENT;
	this.name = name;
	if (this.name[0] == '/') {
		if(base)
			iput(base);
		if (__prefix_namei(retrieve_mode, this.name, base, buf,
				   res_dir, res_inode,
				   last_name, last_entry, last_error) == 0)
			return 0;
		base = current->fs->root;
		atomic_inc(&base->i_count);
		this.name++;
	} else if (!base) {
		base = current->fs->pwd;
		atomic_inc(&base->i_count);
	}
	for(;;) {
		struct inode * inode;
		const char * tmp = this.name;
		int len;

		for(len = 0; (c = *tmp++) && (c != '/'); len++) ;
		this.len = len;
		if(!c)
			break;
		while((c = *tmp) == '/') /* remove embedded/trailing slashes */
			tmp++;
		if(!c) {
			trail_flag = 1;
			if(retrieve_mode & NAM_NO_TRAILSLASH) {
				error = -EISDIR;
				goto alldone;
			}
			break;		
		}
#if 0
		if(atomic_read(&base->i_count) == 0)
			printk("vor lookup this=%s tmp=%s\n", this.name, tmp);
#endif
		error = lookup(base, &this, 0, buf, NULL, NULL, &inode);
#if 0
		if(atomic_read(&base->i_count) == 0)
			printk("nach lookup this=%s tmp=%s\n", this.name, tmp);
#endif
		if (error)
			goto alldone;
		if(S_ISLNK(inode->i_mode)) {
			error = _read_link(inode, &linkname, loopcount);
			if(error)
				goto alldone;
			current->link_count++;
			error = __namei((retrieve_mode & 
					 ~(NAM_SEMLOCK|NAM_TRANSCREATE|NAM_NO_TRAILSLASH))
					| NAM_FOLLOW_LINK,
					linkname, base, buf,
					&base, &inode, NULL, NULL, NULL);
			current->link_count--;
			if(error)
				goto alldone;
		}
#if 0
		if(atomic_read(&base->i_count) == 0)
			printk("this=%s tmp=%s\n", this.name, tmp);
#endif
		this.name = tmp;
		iput(base);
		base = inode;
	}
	if(res_inode) {
		if(retrieve_mode & NAM_SEMLOCK)
			down(&base->i_sem);
		error = lookup(base, &this, retrieve_mode & NAM_TRANSCREATE,
			       buf, last_name, last_entry, res_inode);
		if(!error && S_ISLNK((*res_inode)->i_mode) &&
		   ((retrieve_mode & NAM_FOLLOW_LINK) ||
		    (trail_flag && (retrieve_mode & NAM_FOLLOW_TRAILSLASH)))) {
			char * tmp;

			error = _read_link(*res_inode, &linkname, loopcount);
			if(error)
				goto lastdone;
			if(retrieve_mode & NAM_SEMLOCK)
				up(&base->i_sem);
			/* exchange pages */
			name = tmp = linkname;
			linkname = oldlinkname; oldlinkname = tmp;
			loopcount++;
			goto again; /* Tail recursion elimination "by hand",
				     * uses less dynamic memory.
				     */

			/* Note that trail_flag is not reset, so it
			 * does not matter in a symlink chain where a
			 * trailing slash indicates a directory endpoint.
			 */
		}
		if(!error && trail_flag && !S_ISDIR((*res_inode)->i_mode)) {
			iput(*res_inode);
			error = -ENOTDIR;
		}
	lastdone:
		if(last_error) {
			*last_error = error;
			error = 0;
		}
	}
alldone:
	if(!error && res_dir)
		*res_dir = base;
	else
		iput(base);
	putname(linkname);
	putname(oldlinkname);
	return error;
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */

/* [Feb 1997 T.Schoebel-Theuer] lnamei() completely removed; can be
 * simulated when calling with retrieve_mode==NAM_FOLLOW_TRAILSLASH.
 */
int namei(int retrieve_mode, const char *pathname, struct inode **res_inode)
{
	int error;
	char * tmp;

	error = getname(pathname, &tmp);
	if (!error) {
		char buf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
		error = __namei(retrieve_mode, tmp, NULL,
				buf, NULL, res_inode, NULL, NULL, NULL);
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
int open_namei(const char * pathname, int flag, int mode,
               struct inode ** res_inode, struct inode * base)
{
	char buf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	struct qstr last;
	int error;
	int lasterror;
	struct inode * dir, * inode;
	int namei_mode;

	mode &= S_IALLUGO & ~current->fs->umask;
	mode |= S_IFREG;

	namei_mode = NAM_FOLLOW_LINK;
	if(flag & O_CREAT)
		namei_mode |= NAM_SEMLOCK|NAM_TRANSCREATE|NAM_NO_TRAILSLASH;
	error = __namei(namei_mode, pathname, base, buf,
			&dir, &inode, &last, NULL, &lasterror);
	if (error)
		goto exit;
	error = lasterror;
	if (flag & O_CREAT) {
		if (!error) {
			if (flag & O_EXCL) {
				error = -EEXIST;
			}
		} else if (IS_RDONLY(dir))
			error = -EROFS;
		else if (!dir->i_op || !dir->i_op->create)
			error = -EACCES;
		else if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0)
			;	/* error is already set! */
		else {
			d_del(d_lookup(dir, &last, NULL), D_REMOVE);
			atomic_inc(&dir->i_count);	/* create eats the dir */
			if (dir->i_sb && dir->i_sb->dq_op)
				dir->i_sb->dq_op->initialize(dir, -1);
			error = dir->i_op->create(dir, last.name, last.len,
						  mode, res_inode);
#ifdef CONFIG_OMIRR
			if(!error)
				omirr_print(dir->i_dentry, NULL, &last,
					    " c %ld %d ", CURRENT_TIME, mode);
#endif
			up(&dir->i_sem);
			goto exit_dir;
		}
		up(&dir->i_sem);
	}
	if (error)
		goto exit_inode;

	if (S_ISDIR(inode->i_mode) && (flag & 2)) {
		error = -EISDIR;
		goto exit_inode;
	}
	if ((error = permission(inode,ACC_MODE(flag))) != 0) {
		goto exit_inode;
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
			error = -EACCES;
			goto exit_inode;
		}
		flag &= ~O_TRUNC;
	} else {
		if (IS_RDONLY(inode) && (flag & 2)) {
			error = -EROFS;
			goto exit_inode;
		}
	}
	/*
	 * An append-only file must be opened in append mode for writing.
	 */
	if (IS_APPEND(inode) && ((flag & FMODE_WRITE) && !(flag & O_APPEND))) {
		error = -EPERM;
		goto exit_inode;
	}
	if (flag & O_TRUNC) {
		if ((error = get_write_access(inode)))
			goto exit_inode;
		/*
		 * Refuse to truncate files with mandatory locks held on them.
		 */
		error = locks_verify_locked(inode);
		if (error)
			goto exit_inode;
		if (inode->i_sb && inode->i_sb->dq_op)
			inode->i_sb->dq_op->initialize(inode, -1);
			
		error = do_truncate(inode, 0);
		put_write_access(inode);
	} else
		if (flag & FMODE_WRITE)
			if (inode->i_sb && inode->i_sb->dq_op)
				inode->i_sb->dq_op->initialize(inode, -1);
exit_inode:
	if(error) {
		if(!lasterror)
			iput(inode);
	} else
		*res_inode = inode;
exit_dir:
	iput(dir);
exit:
	return error;
}

int do_mknod(const char * filename, int mode, dev_t dev)
{
	char buf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	struct qstr last;
	int error, lasterror;
	struct inode * dir;
	struct inode * inode;

	mode &= ~current->fs->umask;
	error = __namei(NAM_FOLLOW_LINK|NAM_TRANSCREATE|NAM_NO_TRAILSLASH,
			filename, NULL, buf,
			&dir, &inode, &last, NULL, &lasterror);
	if (error)
		goto exit;
	if(!lasterror) {
		error = -EEXIST;
		goto exit_inode;
	}
	if (!last.len) {
		error = -ENOENT;
		goto exit_inode;
	}
	if (IS_RDONLY(dir)) {
		error = -EROFS;
		goto exit_inode;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0)
		goto exit_inode;
	if (!dir->i_op || !dir->i_op->mknod) {
		error = -ENOSYS; /* instead of EPERM, what does Posix say? */
		goto exit_inode;
	}
	atomic_inc(&dir->i_count);
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	down(&dir->i_sem);
	d_del(d_lookup(dir, &last, NULL), D_REMOVE);
	error = dir->i_op->mknod(dir, last.name, last.len, mode, dev);
#ifdef CONFIG_OMIRR
	if(!error)
		omirr_print(dir->i_dentry, NULL, &last, " n %ld %d %d ",
			    CURRENT_TIME, mode, dev);
#endif
	up(&dir->i_sem);
exit_inode:
	if(!lasterror)
		iput(inode);
	iput(dir);
exit:
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

/* [Feb-97 T. Schoebel-Theuer] remove_trailing_slashes() is now obsolete,
 * its functionality is handled by observing trailing slashes in __namei().
 */
static inline int do_mkdir(const char * pathname, int mode)
{
	char buf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	struct qstr last;
	int error, lasterror;
	struct inode * dir;
	struct inode * inode;

	mode &= 0777 & ~current->fs->umask;

	error = __namei(NAM_FOLLOW_LINK|NAM_TRANSCREATE, pathname, NULL, buf,
			&dir, &inode, &last, NULL, &lasterror);
	if (error)
		goto exit;
	if(!lasterror) {
		error = -EEXIST;
		goto exit_inode;
	}
	if (!last.len) {
		error = -ENOENT;
		goto exit_inode;
	}
	if (IS_RDONLY(dir)) {
		error = -EROFS;
		goto exit_inode;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0)
		goto exit_inode;
	if (!dir->i_op || !dir->i_op->mkdir) {
		error = -ENOSYS; /* instead of EPERM, what does Posix say? */
		goto exit_inode;
	}
	atomic_inc(&dir->i_count);
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	down(&dir->i_sem);
	d_del(d_lookup(dir, &last, NULL), D_REMOVE);
	mode &= 01777 & ~current->fs->umask;
	error = dir->i_op->mkdir(dir, last.name, last.len, mode);
#ifdef CONFIG_OMIRR
	if(!error)
		omirr_print(dir->i_dentry, NULL, &last, " d %ld %d ",
			    CURRENT_TIME, mode);
#endif
	up(&dir->i_sem);
exit_inode:
	if(!lasterror)
		iput(inode);
	iput(dir);
exit:
	return error;
}

asmlinkage int sys_mkdir(const char * pathname, int mode)
{
	int error;
	char * tmp;

	lock_kernel();
	error = getname(pathname,&tmp);
	if (!error) {
		error = do_mkdir(tmp,mode);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

#if 0 /* We need a "deletefs", someone please write it.  -DaveM */
/* Perhaps this could be moved out into a new file. */
static void basket_name(struct inode * dir, struct dentry * entry)
{
	char prefix[32];
	struct qstr prename = { prefix, 14 };
	struct qstr entname = { entry->d_name.name, entry->d_name.len };
	struct inode * inode;
	struct dentry * old = entry; /* dummy */
	int i;
	if(!entry || !(inode = d_inode(&entry)))
		return;
#if 0
	if(atomic_read(&inode->i_count) > 2) {
		extern void printpath(struct dentry *entry);

		printk("Caution: in use ");
		if(inode->i_dentry)
			printpath(inode->i_dentry);
		printk(" i_nlink=%d i_count=%d i_ddir_count=%d i_dent_count=%d\n",
		       inode->i_nlink, atomic_read(&inode->i_count),
		       inode->i_ddir_count, inode->i_dent_count);
	}
#endif
        vfs_lock();
	for(i = 1; old; i++) {
		sprintf(prefix, ".deleted-%04d.", i);
		old = d_lookup(dir, &prename, &entname);
	}
	d_move(entry, dir, &prename, &entname);
        vfs_unlock();
	iput(inode);
}
#endif

static inline int do_rmdir(const char * name)
{
	char buf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	struct qstr last;
	struct dentry * lastent = NULL;
	int error;
	struct inode * dir;
	struct inode * inode;

	/* [T.Schoebel-Theuer] I'm not sure which flags to use here.
	 *  Try the following on different platforms:
	 * [0] rm -rf test test2
	 * [1] ln -s test2 test
	 * [2] mkdir test  || mkdir test2
	 * [3] rmdir test  && mkdir test2
	 * [4] rmdir test/
	 * Now the rusults:
	 * cmd  |   HP-UX   |  SunOS   |  Solaris | Old Linux | New Linux |
	 * ----------------------------------------------------------------
	 * [2]  |   (OK)    |  EEXIST  |  EEXIST  |  EEXIST   |  (OK)
	 * [3]  |  ENOTDIR  |  ENOTDIR |  ENOTDIR |  ENOTDIR  |  ENOTDIR
	 * [4]  |   (OK)    |  EINVAL  |  ENOTDIR |  ENOTDIR  |  (OK)
	 * So I implemented the HP-UX semantics. If this is not right
	 * for Posix compliancy, change the flags accordingly. If Posix
	 * let the question open, I'd suggest to stay at the new semantics.
	 * I'd even make case [3] work by adding 2 to the flags parameter
	 * if Posix tolerates that.
	 */
	error = __namei(NAM_FOLLOW_TRAILSLASH, name, NULL, buf,
			&dir, &inode, &last, &lastent, NULL);
	if (error)
		goto exit;
	if (IS_RDONLY(dir)) {
		error = -EROFS;
		goto exit_dir;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0)
		goto exit_dir;
	/*
	 * A subdirectory cannot be removed from an append-only directory.
	 */
	if (IS_APPEND(dir)) {
		error = -EPERM;
		goto exit_dir;
	}
	if (!dir->i_op || !dir->i_op->rmdir) {
		error = -ENOSYS; /* was EPERM */
		goto exit_dir;
	}
	/* Disallow removals of mountpoints. */
	if(inode->i_mount) {
		error = -EBUSY;
		goto exit_dir;
	}
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);

        down(&dir->i_sem);
#if 0
	if(lastent && d_isbasket(lastent)) {
		d_del(lastent, D_REMOVE);
		error = 0;
		goto exit_lock;
	}
#endif
	atomic_inc(&dir->i_count);
	error = dir->i_op->rmdir(dir, last.name, last.len);
#ifdef CONFIG_OMIRR
	if(!error)
		omirr_print(lastent, NULL, NULL, " r %ld ", CURRENT_TIME);
#endif
#if 0
	if(!error && lastent)
		basket_name(dir, lastent);
exit_lock:
#else
	if(!error && lastent)
		d_del(lastent, D_REMOVE);
#endif
        up(&dir->i_sem);
exit_dir:
	iput(inode);
	iput(dir);
exit:
	return error;
}

asmlinkage int sys_rmdir(const char * pathname)
{
	int error;
	char * tmp;

	lock_kernel();
	error = getname(pathname,&tmp);
	if (!error) {
		error = do_rmdir(tmp);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static inline int do_unlink(const char * name)
{
	char buf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	struct qstr last;
	struct dentry * lastent = NULL;
	int error;
	struct inode * dir;
	struct inode * inode;

	/* HP-UX shows a strange behaviour:
	 * touch y; ln -s y x; rm x/
	 * this succeeds and removes the file y, not the symlink x!
	 * Solaris and old Linux remove the symlink instead, and
	 * old SunOS complains ENOTDIR.
	 * I chose the SunOS behaviour (by not using NAM_FOLLOW_TRAILSLASH),
	 * but I'm not shure whether I should.
	 * The current code generally prohibits using trailing slashes with
	 * non-directories if the name already exists, but not if
	 * it is to be newly created. 
	 * Perhaps this should be further strengthened (by introducing
	 * an additional flag bit indicating whether trailing slashes are
	 * allowed) to get it as consistant as possible, but I don't know
	 * what Posix says.
	 */
	error = __namei(NAM_NO_TRAILSLASH, name, NULL, buf,
			&dir, &inode, &last, &lastent, NULL);
	if (error)
		goto exit;
	if (IS_RDONLY(dir)) {
		error = -EROFS;
		goto exit_dir;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0)
		goto exit_dir;
	/*
	 * A file cannot be removed from an append-only directory.
	 */
	if (IS_APPEND(dir)) {
		error = -EPERM;
		goto exit_dir;
	}
	if (!dir->i_op || !dir->i_op->unlink) {
		error = -ENOSYS; /* was EPERM */
		goto exit_dir;
	}
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);

        down(&dir->i_sem);
#if 0
	if(atomic_read(&inode->i_count) > 1) {
		extern void printpath(struct dentry *entry);

		printk("Fire ");
		if(lastent)
			printpath(lastent);
		printk(" i_nlink=%d i_count=%d i_ddir_count=%d i_dent_count=%d\n",
		       inode->i_nlink, atomic_read(&inode->i_count),
		       inode->i_ddir_count, inode->i_dent_count);
	}
#endif
#if 0
	if(lastent && d_isbasket(lastent)) {
		d_del(lastent, D_REMOVE);
		error = 0;
		goto exit_lock;
	}
#endif
	atomic_inc(&dir->i_count);
	error = dir->i_op->unlink(dir, last.name, last.len);
#ifdef CONFIG_OMIRR
	if(!error)
		omirr_print(lastent, NULL, NULL, " u %ld ", CURRENT_TIME);
#endif
#if 0
	if(!error && lastent)
		basket_name(dir, lastent);
exit_lock:
#else
	if(!error && lastent)
		d_del(lastent, D_REMOVE);
#endif
        up(&dir->i_sem);
exit_dir:
	iput(inode);
	iput(dir);
exit:
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

static inline int do_symlink(const char * oldname, const char * newname)
{
	char buf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	struct qstr last;
	int error, lasterror;
	struct inode * dir;
	struct inode * inode;

	/* The following works on HP-UX and Solaris, by producing
	 * a symlink chain:
	 * rm -rf ? ; mkdir z ; ln -s z y ; ln -s y x/
	 * Under old SunOS, the following occurs:
	 * ln: x/: No such file or directory
	 * Under old Linux, very strange things occur:
	 * ln: cannot create symbolic link `x//y' to `y': No such file or directory
	 * This is very probably a bug, but may be caused by the ln program
	 * when checking for a directory target.
	 *
	 * I'm not shure whether to add NAM_NO_TRAILSLASH to inhibit trailing
	 * slashes in the target generally.
	 */
	error = __namei(NAM_TRANSCREATE, newname, NULL, buf,
			&dir, &inode, &last, NULL, &lasterror);
	if (error)
		goto exit;
	if(!lasterror) {
		iput(inode);
		error = -EEXIST;
		goto exit_dir;
	}
	if (!last.len) {
		error = -ENOENT;
		goto exit_dir;
	}
	if (IS_RDONLY(dir)) {
		error = -EROFS;
		goto exit_dir;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0)
		goto exit_dir;
	if (!dir->i_op || !dir->i_op->symlink) {
		error = -ENOSYS; /* was EPERM */
		goto exit_dir;
	}
	atomic_inc(&dir->i_count);
	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	down(&dir->i_sem);
	d_del(d_lookup(dir, &last, NULL), D_REMOVE);
	error = dir->i_op->symlink(dir, last.name, last.len, oldname);
#ifdef CONFIG_OMIRR
	if(!error)
		omirr_print(dir->i_dentry, NULL, &last,
			    " s %ld %s\0", CURRENT_TIME, oldname);
#endif
	up(&dir->i_sem);
exit_dir:
	iput(dir);
exit:
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

static inline int do_link(const char * oldname, const char * newname)
{
	char oldbuf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	char newbuf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	struct qstr oldlast;
	struct qstr newlast;
	struct dentry * oldent = NULL;
	struct inode * oldinode;
	struct inode * newinode;
	struct inode * newdir;
	int error, lasterror;

	error = __namei(NAM_FOLLOW_LINK|NAM_NO_TRAILSLASH,
			oldname, NULL, oldbuf,
			NULL, &oldinode, &oldlast, &oldent, NULL);
	if (error)
		goto exit;

	error = __namei(NAM_FOLLOW_LINK|NAM_TRANSCREATE, newname, NULL, newbuf,
			&newdir, &newinode, &newlast, NULL, &lasterror);
	if (error)
		goto old_exit;
	if(!lasterror) {
		iput(newinode);
		error = -EEXIST;
		goto new_exit;
	}
	if (!newlast.len) {
		error = -EPERM;
		goto new_exit;
	}
	if (IS_RDONLY(newdir)) {
		error = -EROFS;
		goto new_exit;
	}
	if (newdir->i_dev != oldinode->i_dev) {
		error = -EXDEV;
		goto new_exit;
	}
	if ((error = permission(newdir,MAY_WRITE | MAY_EXEC)) != 0)
		goto new_exit;
	/*
	 * A link to an append-only or immutable file cannot be created.
	 */
	if (IS_APPEND(oldinode) || IS_IMMUTABLE(oldinode)) {
		error = -EPERM;
		goto new_exit;
	}
	if (!newdir->i_op || !newdir->i_op->link) {
		error = -ENOSYS; /* was EPERM */
		goto new_exit;
	}
	atomic_inc(&oldinode->i_count);
	atomic_inc(&newdir->i_count);
	if (newdir->i_sb && newdir->i_sb->dq_op)
		newdir->i_sb->dq_op->initialize(newdir, -1);
	down(&newdir->i_sem);
	d_del(d_lookup(newdir, &newlast, NULL), D_REMOVE);
	error = newdir->i_op->link(oldinode, newdir, newlast.name, newlast.len);
#ifdef CONFIG_OMIRR
	if(!error)
		omirr_print(oldent, newdir->i_dentry, &newlast,
			    " l %ld ", CURRENT_TIME);
#endif
	up(&newdir->i_sem);
new_exit:
	iput(newdir);
old_exit:
	iput(oldinode);
exit:
	return error;
}

asmlinkage int sys_link(const char * oldname, const char * newname)
{
	int error;
	char * from, * to;

	lock_kernel();
	error = getname(oldname,&from);
	if (!error) {
		error = getname(newname,&to);
		if (!error) {
			error = do_link(from,to);
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}

static inline int do_rename(const char * oldname, const char * newname)
{
	char oldbuf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	struct qstr oldlast;
	char newbuf[MAX_TRANS_FILELEN+MAX_TRANS_SUFFIX+2];
	struct qstr newlast;
	struct dentry * oldent = NULL;
	struct inode * olddir, * newdir;
	struct inode * oldinode, * newinode;
	int error, newlasterror;

	error = __namei(NAM_FOLLOW_TRAILSLASH, oldname, NULL, oldbuf,
			&olddir, &oldinode, &oldlast, &oldent, NULL);
	if (error)
		goto exit;
	if ((error = permission(olddir,MAY_WRITE | MAY_EXEC)) != 0)
		goto old_exit;
	if (!oldlast.len || (oldlast.name[0] == '.' &&
	    (oldlast.len == 1 || (oldlast.name[1] == '.' &&
	     oldlast.len == 2)))) {
		error = -EPERM;
		goto old_exit;
	}
	/* Disallow moves of mountpoints. */
	if(oldinode->i_mount) {
		error = -EBUSY;
		goto old_exit;
	}

	error = __namei(NAM_FOLLOW_LINK|NAM_TRANSCREATE, newname, NULL, newbuf,
			&newdir, &newinode, &newlast, NULL, &newlasterror);
	if (error)
		goto old_exit;
	if ((error = permission(newdir,MAY_WRITE | MAY_EXEC)) != 0)
		goto new_exit;
	if (!newlast.len || (newlast.name[0] == '.' &&
	    (newlast.len == 1 || (newlast.name[1] == '.' &&
	     newlast.len == 2)))) {
		error = -EPERM;
		goto new_exit;
	}
	if (newdir->i_dev != olddir->i_dev) {
		error = -EXDEV;
		goto new_exit;
	}
	if (IS_RDONLY(newdir) || IS_RDONLY(olddir)) {
		error = -EROFS;
		goto new_exit;
	}
	/*
	 * A file cannot be removed from an append-only directory.
	 */
	if (IS_APPEND(olddir)) {
		error = -EPERM;
		goto new_exit;
	}
	if (!olddir->i_op || !olddir->i_op->rename) {
		error = -ENOSYS; /* was EPERM */
		goto new_exit;
	}
#ifdef CONFIG_TRANS_NAMES
	/* if oldname has been translated, but newname not (and
	 * has not already a suffix), take over the suffix from oldname.
	 */
	if(oldlast.name == oldbuf && newlast.name != newbuf &&
	   newlast.name[newlast.len-1] != '#') {
		int i = oldlast.len - 2;
		while (i > 0 && oldlast.name[i] != '#')
			i--;
		memcpy(newbuf, newlast.name, newlast.len);
		memcpy(newbuf+newlast.len, oldlast.name+i, oldlast.len - i);
		newlast.len += oldlast.len - i;
		newlast.name = newbuf;
	}
#endif
	atomic_inc(&olddir->i_count);
	atomic_inc(&newdir->i_count);
	if (newdir->i_sb && newdir->i_sb->dq_op)
		newdir->i_sb->dq_op->initialize(newdir, -1);
	down(&newdir->i_sem);
	error = olddir->i_op->rename(olddir, oldlast.name, oldlast.len, 
		newdir, newlast.name, newlast.len);
#ifdef CONFIG_OMIRR
	if(!error)
		omirr_print(oldent, newdir->i_dentry, &newlast,
			    " m %ld ", CURRENT_TIME);
#endif
	if(!error) {
		d_del(d_lookup(newdir, &newlast, NULL), D_REMOVE);
		d_move(d_lookup(olddir, &oldlast, NULL), newdir, &newlast, NULL);
	}
	up(&newdir->i_sem);
new_exit:
	if(!newlasterror)
		iput(newinode);
	iput(newdir);
old_exit:
	iput(oldinode);
	iput(olddir);
exit:
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
			error = do_rename(from,to);
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}
