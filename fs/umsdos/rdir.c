/*
 *  linux/fs/umsdos/rdir.c
 *
 *  Written 1994 by Jacques Gelinas
 *
 *  Extended MS-DOS directory pure MS-DOS handling functions
 *  (For directory without EMD file).
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/limits.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#define PRINTK(x)
#define Printk(x) printk x


extern struct inode *pseudo_root;

static int UMSDOS_rreaddir (
	struct inode *dir,
	struct file *filp,
    struct dirent *dirent,
	int count)
{
	int ret = 0;
	while (1){
		int len = -1;
		ret = msdos_readdir(dir,filp,dirent,count);
		if (ret > 0) len = get_fs_word(&dirent->d_reclen);
		if (len == 5
			&& pseudo_root != NULL
			&& dir->i_sb->s_mounted == pseudo_root->i_sb->s_mounted){
			/*
				In pseudo root mode, we must eliminate logically
				the directory linux from the real root.
			*/
			char name[5];
			memcpy_fromfs (name,dirent->d_name,5);
			if (memcmp(name,UMSDOS_PSDROOT_NAME,UMSDOS_PSDROOT_LEN)!=0) break;
		}else{
			if (pseudo_root != NULL
				&& len == 2
				&& dir == dir->i_sb->s_mounted
				&& dir == pseudo_root->i_sb->s_mounted){
				char name[2];
				memcpy_fromfs (name,dirent->d_name,2);
				if (name[0] == '.' && name[1] == '.'){
					put_fs_long (pseudo_root->i_ino,&dirent->d_ino);
				}
			}
			break;
		}
	}
	return ret;
}

/*
	Lookup into a non promoted directory.
	If the result is a directory, make sure we find out if it is
	a promoted one or not (calling umsdos_setup_dir_inode(inode)).
*/
int umsdos_rlookup_x(
	struct inode *dir,
	const char *name,
	int len,
	struct inode **result,	/* Will hold inode of the file, if successful */
	int nopseudo)			/* Don't care about pseudo root mode */
							/* so locating "linux" will work */
{
	int ret;
	if (pseudo_root != NULL
		&& len == 2
		&& name[0] == '.'
		&& name[1] == '.'
		&& dir == dir->i_sb->s_mounted
		&& dir == pseudo_root->i_sb->s_mounted){
		*result = pseudo_root;
		pseudo_root->i_count++;
		ret = 0;
		/* #Specification: pseudo root / DOS/..
			In the real root directory (c:\), the directory ..
			is the pseudo root (c:\linux).
		*/
	}else{
		ret = umsdos_real_lookup (dir,name,len,result);
		if (ret == 0){
			struct inode *inode = *result;
			if (inode == pseudo_root && !nopseudo){
				/* #Specification: pseudo root / DOS/linux
					Even in the real root directory (c:\), the directory
					/linux won't show
				*/
				ret = -ENOENT;
				iput (pseudo_root);
				*result = NULL;
			}else if (S_ISDIR(inode->i_mode)){
				/* We must place the proper function table */
				/* depending if this is a MsDOS directory or an UMSDOS directory */
				umsdos_setup_dir_inode(inode);
			}
		}
	}
	iput (dir);
	return ret;
}
int UMSDOS_rlookup(
	struct inode *dir,
	const char *name,
	int len,
	struct inode **result)	/* Will hold inode of the file, if successful */
{
	return umsdos_rlookup_x(dir,name,len,result,0);
}

static int UMSDOS_rrmdir (
	struct inode *dir,
	const char *name,
	int len)
{
	/* #Specification: dual mode / rmdir in a DOS directory
		In a DOS (not EMD in it) directory, we use a reverse strategy
		compared with an Umsdos directory. We assume that a subdirectory
		of a DOS directory is also a DOS directory. This is not always
		true (umssync may be used anywhere), but make sense.

		So we call msdos_rmdir() directly. If it failed with a -ENOTEMPTY
		then we check if it is a Umsdos directory. We check if it is
		really empty (only . .. and --linux-.--- in it). If it is true
		we remove the EMD and do a msdos_rmdir() again.

		In a Umsdos directory, we assume all subdirectory are also
		Umsdos directory, so we check the EMD file first.
	*/
	int ret;
	if (umsdos_is_pseudodos(dir,name,len)){
		/* #Specification: pseudo root / rmdir /DOS
			The pseudo sub-directory /DOS can't be removed!
			This is done even if the pseudo root is not a Umsdos
			directory anymore (very unlikely), but an accident (under
			MsDOS) is always possible.

			EPERM is returned.
		*/
		ret = -EPERM;
	}else{
		umsdos_lockcreate (dir);
		dir->i_count++;
		ret = msdos_rmdir (dir,name,len);
		if (ret == -ENOTEMPTY){
			struct inode *sdir;
			dir->i_count++;
			ret = UMSDOS_rlookup (dir,name,len,&sdir);
			PRINTK (("rrmdir lookup %d ",ret));
			if (ret == 0){
				int empty;
				if ((empty = umsdos_isempty (sdir)) != 0){
					PRINTK (("isempty %d i_count %d ",empty,sdir->i_count));
					if (empty == 2){
						/*
							Not a Umsdos directory, so the previous msdos_rmdir
							was not lying :-)
						*/
						ret = -ENOTEMPTY;
					}else if (empty == 1){
						/* We have to removed the EMD file */
						ret = msdos_unlink(sdir,UMSDOS_EMD_FILE
							,UMSDOS_EMD_NAMELEN);
						sdir = NULL;
						if (ret == 0){
							dir->i_count++;
							ret = msdos_rmdir (dir,name,len);
						}
					}
				}else{
					ret = -ENOTEMPTY;
				}
				iput (sdir);
			}
		}
		umsdos_unlockcreate (dir);
	}
	iput (dir);
	return ret;
}

/* #Specification: dual mode / introduction
	One goal of UMSDOS is to allow a practical and simple coexistence
	between MsDOS and Linux in a single partition. Using the EMD file
	in each directory, UMSDOS add Unix semantics and capabilities to
	normal DOS file system. To help and simplify coexistence, here is
	the logic related to the EMD file.

	If it is missing, then the directory is managed by the MsDOS driver.
	The names are limited to DOS limits (8.3). No links, no device special
	and pipe and so on.

	If it is there, it is the directory. If it is there but empty, then
	the directory looks empty. The utility umssync allows synchronisation
	of the real DOS directory and the EMD.

	Whenever umssync is applied to a directory without EMD, one is
	created on the fly. The directory is promoted to full unix semantic.
	Of course, the ls command will show exactly the same content as before
	the umssync session.

	It is believed that the user/admin will promote directories to unix
	semantic as needed.

	The strategy to implement this is to use two function table (struct
	inode_operations). One for true UMSDOS directory and one for directory
	with missing EMD.

	Functions related to the DOS semantic (but aware of UMSDOS) generally
	have a "r" prefix (r for real) such as UMSDOS_rlookup, to differentiate
	from the one with full UMSDOS semantic.
*/
static struct file_operations umsdos_rdir_operations = {
	NULL,				/* lseek - default */
	UMSDOS_dir_read,	/* read */
	NULL,				/* write - bad */
	UMSDOS_rreaddir,	/* readdir */
	NULL,				/* select - default */
	UMSDOS_ioctl_dir,	/* ioctl - default */
	NULL,				/* mmap */
	NULL,				/* no special open code */
	NULL,				/* no special release code */
	NULL				/* fsync */
};

struct inode_operations umsdos_rdir_inode_operations = {
	&umsdos_rdir_operations,	/* default directory file-ops */
	msdos_create,		/* create */
	UMSDOS_rlookup,		/* lookup */
	NULL,				/* link */
	msdos_unlink,		/* unlink */
	NULL,				/* symlink */
	msdos_mkdir,		/* mkdir */
	UMSDOS_rrmdir,		/* rmdir */
	NULL,				/* mknod */
	msdos_rename,		/* rename */
	NULL,				/* readlink */
	NULL,				/* follow_link */
	NULL,				/* bmap */
	NULL,				/* truncate */
	NULL				/* permission */
};


