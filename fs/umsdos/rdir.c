/*
 *  linux/fs/umsdos/rdir.c
 *
 *  Written 1994 by Jacques Gelinas
 *
 *  Extended MS-DOS directory pure MS-DOS handling functions
 *  (For directory without EMD file).
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/limits.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#include <asm/uaccess.h>


extern struct dentry *saved_root;
extern struct inode *pseudo_root;
extern struct dentry_operations umsdos_dentry_operations;

struct RDIR_FILLDIR {
	void *dirbuf;
	filldir_t filldir;
	int real_root;
};

static int rdir_filldir (	void *buf,
				const char *name,
				int name_len,
				off_t offset,
				ino_t ino)
{
	int ret = 0;
	struct RDIR_FILLDIR *d = (struct RDIR_FILLDIR *) buf;

	if (d->real_root) {
		PRINTK ((KERN_DEBUG "rdir_filldir /mn/: real root!\n"));
		/* real root of a pseudo_rooted partition */
		if (name_len != UMSDOS_PSDROOT_LEN
		    || memcmp (name, UMSDOS_PSDROOT_NAME, UMSDOS_PSDROOT_LEN) != 0) {
			/* So it is not the /linux directory */
			if (name_len == 2 && name[0] == '.' && name[1] == '.') {
				/* Make sure the .. entry points back to the pseudo_root */
				ino = pseudo_root->i_ino;
			}
			ret = d->filldir (d->dirbuf, name, name_len, offset, ino);
		}
	} else {
		/* Any DOS directory */
		ret = d->filldir (d->dirbuf, name, name_len, offset, ino);
	}
	return ret;
}


static int UMSDOS_rreaddir (struct file *filp, void *dirbuf, filldir_t filldir)
{
	struct inode *dir = filp->f_dentry->d_inode;
	struct RDIR_FILLDIR bufk;

	bufk.filldir = filldir;
	bufk.dirbuf = dirbuf;
	bufk.real_root = pseudo_root && (dir == saved_root->d_inode);
	return fat_readdir (filp, &bufk, rdir_filldir);
}


/*
 * Lookup into a non promoted directory.
 * If the result is a directory, make sure we find out if it is
 * a promoted one or not (calling umsdos_setup_dir_inode(inode)).
 */
/* #Specification: pseudo root / DOS/..
 * In the real root directory (c:\), the directory ..
 * is the pseudo root (c:\linux).
 */
int umsdos_rlookup_x ( struct inode *dir, struct dentry *dentry, int nopseudo)
{
	int ret;

	if (saved_root && dir == saved_root->d_inode && !nopseudo &&
	    dentry->d_name.len == UMSDOS_PSDROOT_LEN &&
	    memcmp (dentry->d_name.name, UMSDOS_PSDROOT_NAME, UMSDOS_PSDROOT_LEN) == 0) {
		/* #Specification: pseudo root / DOS/linux
		 * Even in the real root directory (c:\), the directory
		 * /linux won't show
		 */
		 
		ret = -ENOENT;
		goto out;
	}

	ret = msdos_lookup (dir, dentry);
	if (ret) {
		printk(KERN_WARNING
			"umsdos_rlookup_x: %s/%s failed, ret=%d\n",
			dentry->d_parent->d_name.name, dentry->d_name.name,ret);
		goto out;
	}
	if (dentry->d_inode) {
		/* We must install the proper function table
		 * depending on whether this is an MS-DOS or 
		 * a UMSDOS directory
		 */
Printk ((KERN_DEBUG "umsdos_rlookup_x: patch_dentry_inode %s/%s\n",
dentry->d_parent->d_name.name, dentry->d_name.name));
		umsdos_patch_dentry_inode(dentry, 0);

	}
out:
	/* always install our dentry ops ... */
	dentry->d_op = &umsdos_dentry_operations;
	return ret;
}


int UMSDOS_rlookup ( struct inode *dir, struct dentry *dentry)
{
	return umsdos_rlookup_x (dir, dentry, 0);
}


/* #Specification: dual mode / rmdir in a DOS directory
 * In a DOS (not EMD in it) directory, we use a reverse strategy
 * compared with a UMSDOS directory. We assume that a subdirectory
 * of a DOS directory is also a DOS directory. This is not always
 * true (umssync may be used anywhere), but makes sense.
 * 
 * So we call msdos_rmdir() directly. If it failed with a -ENOTEMPTY
 * then we check if it is a Umsdos directory. We check if it is
 * really empty (only . .. and --linux-.--- in it). If it is true
 * we remove the EMD and do a msdos_rmdir() again.
 * 
 * In a Umsdos directory, we assume all subdirectories are also
 * Umsdos directories, so we check the EMD file first.
 */
/* #Specification: pseudo root / rmdir /DOS
 * The pseudo sub-directory /DOS can't be removed!
 * This is done even if the pseudo root is not a Umsdos
 * directory anymore (very unlikely), but an accident (under
 * MS-DOS) is always possible.
 * 
 * EPERM is returned.
 */
static int UMSDOS_rrmdir ( struct inode *dir, struct dentry *dentry)
{
	int ret, empty;

	ret = -EPERM;
	if (umsdos_is_pseudodos (dir, dentry))
		goto out;

	ret = -EBUSY;
	if (!list_empty(&dentry->d_hash))
		goto out;

	ret = msdos_rmdir (dir, dentry);
	if (ret != -ENOTEMPTY)
		goto out;

	empty = umsdos_isempty (dentry);
	if (empty == 1) {
		struct dentry *demd;
		/* We have to remove the EMD file. */
		demd = umsdos_get_emd_dentry(dentry);
		ret = PTR_ERR(demd);
		if (!IS_ERR(demd)) {
			ret = 0;
			if (demd->d_inode)
				ret = msdos_unlink (dentry->d_inode, demd);
			dput(demd);
		}
	}
	if (ret)
		goto out;

	/* now retry the original ... */
	ret = msdos_rmdir (dir, dentry);

out:
	return ret;
}

/* #Specification: dual mode / introduction
 * One goal of UMSDOS is to allow a practical and simple coexistence
 * between MS-DOS and Linux in a single partition. Using the EMD file
 * in each directory, UMSDOS adds Unix semantics and capabilities to
 * a normal DOS filesystem. To help and simplify coexistence, here is
 * the logic related to the EMD file.
 * 
 * If it is missing, then the directory is managed by the MS-DOS driver.
 * The names are limited to DOS limits (8.3). No links, no device special
 * and pipe and so on.
 * 
 * If it is there, it is the directory. If it is there but empty, then
 * the directory looks empty. The utility umssync allows synchronisation
 * of the real DOS directory and the EMD.
 * 
 * Whenever umssync is applied to a directory without EMD, one is
 * created on the fly.  The directory is promoted to full Unix semantics.
 * Of course, the ls command will show exactly the same content as before
 * the umssync session.
 * 
 * It is believed that the user/admin will promote directories to Unix
 * semantics as needed.
 * 
 * The strategy to implement this is to use two function table (struct
 * inode_operations). One for true UMSDOS directory and one for directory
 * with missing EMD.
 * 
 * Functions related to the DOS semantic (but aware of UMSDOS) generally
 * have a "r" prefix (r for real) such as UMSDOS_rlookup, to differentiate
 * from the one with full UMSDOS semantics.
 */
static struct file_operations umsdos_rdir_operations =
{
	NULL,			/* lseek - default */
	dummy_dir_read,		/* read */
	NULL,			/* write - bad */
	UMSDOS_rreaddir,	/* readdir */
	NULL,			/* poll - default */
	UMSDOS_ioctl_dir,	/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* fsync */
};

struct inode_operations umsdos_rdir_inode_operations =
{
	&umsdos_rdir_operations,	/* default directory file-ops */
	msdos_create,		/* create */
	UMSDOS_rlookup,		/* lookup */
	NULL,			/* link */
	msdos_unlink,		/* unlink */
	NULL,			/* symlink */
	msdos_mkdir,		/* mkdir */
	UMSDOS_rrmdir,		/* rmdir */
	NULL,			/* mknod */
	msdos_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	NULL,			/* revalidate */
};
