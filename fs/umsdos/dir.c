/*
 *  linux/fs/umsdos/dir.c
 *
 *  Written 1993 by Jacques Gelinas
 *      Inspired from linux/fs/msdos/... : Werner Almesberger
 *
 *  Extended MS-DOS directory handling functions
 */

#include <linux/sched.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/limits.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#include <asm/uaccess.h>

#define PRINTK(x)
#define Printk(x) printk x

#define UMSDOS_SPECIAL_DIRFPOS	3
extern struct inode *pseudo_root;


/* P.T.Waltenberg
 * I've retained this to facilitate the lookup of some of the hard-wired files/directories UMSDOS
 * uses. It's easier to do once than hack all the other instances. Probably safer as well
 */

/* FIXME: it returns inode with i_count of 0. this should be redesigned to return dentry instead,
   and correct dentry (with correct d_parent) */

int compat_umsdos_real_lookup (struct inode *dir, const char *name, int len, struct inode **inode)
{
	int rv;
	struct dentry *dentry;
	unsigned long ino;

	Printk ((KERN_DEBUG "compat_umsdos_real_lookup !!CNTx!!: start\n"));
	check_inode (dir);
	dentry = creat_dentry (name, len, NULL, NULL);
	rv = umsdos_real_lookup (dir, dentry);
	iput (dir);	/* should be here, because umsdos_real_lookup does inc_count(dir) */

	if (rv) {
		Printk ((KERN_WARNING "compat_umsdos_real_lookup failed with %d\n", rv));
		return rv;
	}

	if (!inode) {
		Printk ((KERN_ERR "inode should be set here. Arrgh! segfaulting...\n"));
	}
		
	ino = dentry->d_inode->i_ino;
	*inode = dentry->d_inode;

	dput (dentry);	/* we are done with it: FIXME: does this work /mn/ ? */
	
	check_dentry (dentry);
	check_inode (dir);

	Printk ((KERN_DEBUG "compat_umsdos_real_lookup !!CNTx!!: end\n"));
	
	return rv;
}


int compat_msdos_create (struct inode *dir, const char *name, int len, int mode, struct inode **inode)
{
	int rv;
	struct dentry *dentry;

	check_inode (dir);
	dentry = creat_dentry (name, len, NULL, NULL);
	check_dentry (dentry);
	rv = msdos_create (dir, dentry, mode);
	check_dentry (dentry);
	if (inode != NULL)
		*inode = dentry->d_inode;

	check_inode (dir);
	return rv;
}


/*
 * So  grep *  doesn't complain in the presence of directories.
 */
 
int UMSDOS_dir_read (struct file *filp, char *buff, size_t size, loff_t *count)
{
	return -EISDIR;
}


struct UMSDOS_DIR_ONCE {
	void *dirbuf;
	filldir_t filldir;
	int count;
	int stop;
};

/*
 * Record a single entry the first call.
 * Return -EINVAL the next one.
 * NOTE: filldir DOES NOT use a dentry
 */

static int umsdos_dir_once (	void *buf,
				const char *name,
				int len,
				off_t offset,
				ino_t ino)
{
	int ret = -EINVAL;
	struct UMSDOS_DIR_ONCE *d = (struct UMSDOS_DIR_ONCE *) buf;

	if (d->count == 0) {
		PRINTK ((KERN_DEBUG "dir_once :%.*s: offset %Ld\n", dentry->d_len, dentry->d_name, offset));
		ret = d->filldir (d->dirbuf, name, len, offset, ino);
		d->stop = ret < 0;
		d->count = 1;
	}
	return ret;
}


/*
 * Read count directory entries from directory filp
 * Return a negative value from linux/errno.h.
 * Return > 0 if success (The amount of byte written by filldir).
 * 
 * This function is used by the normal readdir VFS entry point and by
 * some function who try to find out info on a file from a pure MSDOS
 * inode. See umsdos_locate_ancestor() below.
 */
 
static int umsdos_readdir_x (
				    struct inode *dir,	/* Point to a description of the super block */
				    struct file *filp,	/* Point to a directory which is read */
				    void *dirbuf,	/* Will hold count directory entry */
							/* but filled by the filldir function */
				    int internal_read,	/* Called for internal purpose */
				    struct umsdos_dirent *u_entry,	/* Optional umsdos entry */
				    int follow_hlink,
				    filldir_t filldir)
{
	int ret = 0;

	umsdos_startlookup (dir);
	if (filp->f_pos == UMSDOS_SPECIAL_DIRFPOS
	    && pseudo_root
	    && dir == pseudo_root
	    && !internal_read) {

		Printk (("umsdos_readdir_x: what UMSDOS_SPECIAL_DIRFPOS /mn/?\n"));
		/*
		 * We don't need to simulate this pseudo directory
		 * when umsdos_readdir_x is called for internal operation
		 * of umsdos. This is why dirent_in_fs is tested
		 */
		/* #Specification: pseudo root / directory /DOS
		 * When umsdos operates in pseudo root mode (C:\linux is the
		 * linux root), it simulate a directory /DOS which points to
		 * the real root of the file system.
		 */
		if (filldir (dirbuf, "DOS", 3, UMSDOS_SPECIAL_DIRFPOS, UMSDOS_ROOT_INO) == 0) {
			filp->f_pos++;
		}
	} else if (filp->f_pos < 2 || (dir->i_ino != UMSDOS_ROOT_INO && filp->f_pos == 32)) {
	
	/* FIXME: that was in 2.0.x: else if (filp->f_pos < 2 || (dir != dir->i_sb->s_mounted && filp->f_pos == 32))
	 * I'm probably screwing up pseudo-root and stuff with this. It needs proper fix.
	 */
	                
	
		/* #Specification: readdir / . and ..
		 * The msdos filesystem manage the . and .. entry properly
		 * so the EMD file won't hold any info about it.
		 * 
		 * In readdir, we assume that for the root directory
		 * the read position will be 0 for ".", 1 for "..". For
		 * a non root directory, the read position will be 0 for "."
		 * and 32 for "..".
		 */
		/*
		 * This is a trick used by the msdos file system (fs/msdos/dir.c)
		 * to manage . and .. for the root directory of a file system.
		 * Since there is no such entry in the root, fs/msdos/dir.c
		 * use the following:
		 * 
		 * if f_pos == 0, return ".".
		 * if f_pos == 1, return "..".
		 * 
		 * So let msdos handle it
		 * 
		 * Since umsdos entries are much larger, we share the same f_pos.
		 * if f_pos is 0 or 1 or 32, we are clearly looking at . and
		 * ..
		 * 
		 * As soon as we get f_pos == 2 or f_pos == 64, then back to
		 * 0, but this time we are reading the EMD file.
		 * 
		 * Well, not so true. The problem, is that UMSDOS_REC_SIZE is
		 * also 64, so as soon as we read the first record in the
		 * EMD, we are back at offset 64. So we set the offset
		 * to UMSDOS_SPECIAL_DIRFPOS(3) as soon as we have read the
		 * .. entry from msdos.
		 * 
		 * Now (linux 1.3), umsdos_readdir can read more than one
		 * entry even if we limit (umsdos_dir_once) to only one:
		 * It skips over hidden file. So we switch to
		 * UMSDOS_SPECIAL_DIRFPOS as soon as we have read successfully
		 * the .. entry.
		 */
		int last_f_pos = filp->f_pos;
		struct UMSDOS_DIR_ONCE bufk;

		Printk (("umsdos_readdir_x: . or .. /mn/?\n"));

		bufk.dirbuf = dirbuf;
		bufk.filldir = filldir;
		bufk.count = 0;

		ret = fat_readdir (filp, &bufk, umsdos_dir_once);
		if (last_f_pos > 0 && filp->f_pos > last_f_pos)
			filp->f_pos = UMSDOS_SPECIAL_DIRFPOS;
		if (u_entry != NULL)
			u_entry->flags = 0;
	} else {
		struct inode *emd_dir;

		Printk (("umsdos_readdir_x: normal file /mn/?\n"));
		emd_dir = umsdos_emd_dir_lookup (dir, 0);
		if (emd_dir != NULL) {
			off_t start_fpos = filp->f_pos;

			Printk (("umsdos_readdir_x: emd_dir->i_ino=%ld\n", emd_dir->i_ino));
			if (filp->f_pos <= UMSDOS_SPECIAL_DIRFPOS + 1)
				filp->f_pos = 0;
			Printk (("f_pos %Ld i_size %ld\n", filp->f_pos, emd_dir->i_size));
			ret = 0;
			while (filp->f_pos < emd_dir->i_size) {
				struct umsdos_dirent entry;
				off_t cur_f_pos = filp->f_pos;

				if (umsdos_emd_dir_readentry (emd_dir, filp, &entry) != 0) {
					ret = -EIO;
					break;
				} else if (entry.name_len != 0) {
					/* #Specification: umsdos / readdir
					 * umsdos_readdir() should fill a struct dirent with
					 * an inode number. The cheap way to get it is to
					 * do a lookup in the MSDOS directory for each
					 * entry processed by the readdir() function.
					 * This is not very efficient, but very simple. The
					 * other way around is to maintain a copy of the inode
					 * number in the EMD file. This is a problem because
					 * this has to be maintained in sync using tricks.
					 * Remember that MSDOS (the OS) does not update the
					 * modification time (mtime) of a directory. There is
					 * no easy way to tell that a directory was modified
					 * during a DOS session and synchronise the EMD file.
					 * 
					 * Suggestion welcome.
					 * 
					 * So the easy way is used!
					 */
					struct umsdos_info info;
					struct inode *inode;

					int lret;

					umsdos_parse (entry.name, entry.name_len, &info);
					info.f_pos = cur_f_pos;
					umsdos_manglename (&info);
					lret = compat_umsdos_real_lookup (dir, info.fake.fname, info.fake.len, &inode);
					Printk (("Cherche inode de %s lret %d flags %d\n", info.fake.fname, lret, entry.flags));
					if (lret == 0
					  && (entry.flags & UMSDOS_HLINK)
					    && follow_hlink) {
						struct inode *rinode;

						Printk ((KERN_DEBUG "umsdos_hlink2inode now\n"));
						lret = umsdos_hlink2inode (inode, &rinode);
						inode = rinode;
					}
					if (lret == 0) {
						/* #Specification: pseudo root / reading real root
						 * The pseudo root (/linux) is logically
						 * erased from the real root. This mean that
						 * ls /DOS, won't show "linux". This avoids
						 * infinite recursion /DOS/linux/DOS/linux while
						 * walking the file system.
						 */
						if (inode != pseudo_root
						    && (internal_read
							|| !(entry.flags & UMSDOS_HIDDEN))) {
							Printk ((KERN_DEBUG "filldir now\n"));
							if (filldir (dirbuf, entry.name, entry.name_len, cur_f_pos, inode->i_ino) < 0) {
								filp->f_pos = cur_f_pos;
							}
							Printk (("Trouve ino %ld ", inode->i_ino));
							if (u_entry != NULL)
								*u_entry = entry;
							iput (inode); /* FIXME? */
							break;
						}
						Printk ((KERN_DEBUG " dir.c:Putting inode %lu with i_count=%d\n", inode->i_ino, inode->i_count));
						iput (inode); /* FIXME? */
					} else {
						/* #Specification: umsdos / readdir / not in MSDOS
						 * During a readdir operation, if the file is not
						 * in the MSDOS directory anymore, the entry is
						 * removed from the EMD file silently.
						 */
						Printk (("'Silently' removing EMD for file\n"));
						ret = umsdos_writeentry (dir, emd_dir, &info, 1);
						if (ret != 0) {
							break;
						}
					}
				}
			}
			/*
			 * If the fillbuf has failed, f_pos is back to 0.
			 * To avoid getting back into the . and .. state
			 * (see comments at the beginning), we put back
			 * the special offset.
			 */
			if (filp->f_pos == 0)
				filp->f_pos = start_fpos;
			Printk ((KERN_DEBUG " dir.c:Putting emd_dir %lu with i_count=%d\n", emd_dir->i_ino, emd_dir->i_count));
			iput (emd_dir); /* FIXME? */
		}
	}
	umsdos_endlookup (dir);
	
	Printk (("read dir %p pos %Ld ret %d\n", dir, filp->f_pos, ret));
	return ret;
}


/*
 * Read count directory entries from directory filp
 * Return a negative value from linux/errno.h.
 * Return 0 or positive if successful
 */
 
static int UMSDOS_readdir (
				  struct file *filp,	/* Point to a directory which is read */
				  void *dirbuf,		/* Will hold directory entries  */
				  filldir_t filldir)
{
	struct inode *dir = filp->f_dentry->d_inode;
	int ret = 0;
	int count = 0;
	struct UMSDOS_DIR_ONCE bufk;

	bufk.dirbuf = dirbuf;
	bufk.filldir = filldir;
	bufk.stop = 0;

	Printk (("UMSDOS_readdir in\n"));
	while (ret == 0 && bufk.stop == 0) {
		struct umsdos_dirent entry;

		bufk.count = 0;
		Printk (("UMSDOS_readdir: calling _x (%p,%p,%p,%d,%p,%d,%p)\n", dir, filp, &bufk, 0, &entry, 1, umsdos_dir_once));
		ret = umsdos_readdir_x (dir, filp, &bufk, 0, &entry, 1, umsdos_dir_once);
		if (bufk.count == 0)
			break;
		count += bufk.count;
	}
	Printk (("UMSDOS_readdir out %d count %d pos %Ld\n", ret, count, filp->f_pos));
	return count ? : ret;
}


/*
 * Complete the inode content with info from the EMD file
 */

void umsdos_lookup_patch (
				 struct inode *dir,
				 struct inode *inode,
				 struct umsdos_dirent *entry,
				 off_t emd_pos)
{
	/*
	 * This function modify the state of a dir inode. It decides
	 * if the dir is a umsdos dir or a dos dir. This is done
	 * deeper in umsdos_patch_inode() called at the end of this function.
	 * 
	 * umsdos_patch_inode() may block because it is doing disk access.
	 * At the same time, another process may get here to initialise
	 * the same dir inode. There is 3 cases.
	 * 
	 * 1-The inode is already initialised. We do nothing.
	 * 2-The inode is not initialised. We lock access and do it.
	 * 3-Like 2 but another process has lock the inode, so we try
	 * to lock it and right after check if initialisation is still
	 * needed.
	 * 
	 * 
	 * Thanks to the mem option of the kernel command line, it was
	 * possible to consistently reproduce this problem by limiting
	 * my mem to 4 meg and running X.
	 */
	/*
	 * Do this only if the inode is freshly read, because we will lose
	 * the current (updated) content.
	 */
	/*
	 * A lookup of a mount point directory yield the inode into
	 * the other fs, so we don't care about initialising it. iget()
	 * does this automatically.
	 */

	if (inode->i_sb == dir->i_sb && !umsdos_isinit (inode)) {
		if (S_ISDIR (inode->i_mode))
			umsdos_lockcreate (inode);
		if (!umsdos_isinit (inode)) {
			/* #Specification: umsdos / lookup / inode info
			 * After successfully reading an inode from the MSDOS
			 * filesystem, we use the EMD file to complete it.
			 * We update the following field.
			 * 
			 * uid, gid, atime, ctime, mtime, mode.
			 * 
			 * We rely on MSDOS for mtime. If the file
			 * was modified during an MSDOS session, at least
			 * mtime will be meaningful. We do this only for regular
			 * file.
			 * 
			 * We don't rely on MSDOS for mtime for directory because
			 * the MSDOS directory date is creation time (strange
			 * MSDOS behavior) which fit nowhere in the three UNIX
			 * time stamp.
			 */
			if (S_ISREG (entry->mode))
				entry->mtime = inode->i_mtime;
			inode->i_mode = entry->mode;
			inode->i_rdev = to_kdev_t (entry->rdev);
			inode->i_atime = entry->atime;
			inode->i_ctime = entry->ctime;
			inode->i_mtime = entry->mtime;
			inode->i_uid = entry->uid;
			inode->i_gid = entry->gid;
			/* #Specification: umsdos / conversion mode
			 * The msdos fs can do some inline conversion
			 * of the data of a file. It can translate
			 * silently from MsDOS text file format to Unix
			 * one (crlf -> lf) while reading, and the reverse
			 * while writing. This is activated using the mount
			 * option conv=....
			 * 
			 * This is not useful for Linux file in promoted
			 * directory. It can even be harmful. For this
			 * reason, the binary (no conversion) mode is
			 * always activated.
			 */
			/* #Specification: umsdos / conversion mode / todo
			 * A flag could be added to file and directories
			 * forcing an automatic conversion mode (as
			 * done with the msdos fs).
			 * 
			 * This flag could be setup on a directory basis
			 * (instead of file) and all file in it would
			 * logically inherited. If the conversion mode
			 * is active (conv=) then the i_binary flag would
			 * be left untouched in those directories.
			 * 
			 * It was proposed that the sticky bit was used
			 * to set this. The problem is that new file would
			 * be written incorrectly. The other problem is that
			 * the sticky bit has a meaning for directories. So
			 * another bit should be used (there is some space
			 * in the EMD file for it) and a special utilities
			 * would be used to assign the flag to a directory).
			 * I don't think it is useful to assign this flag
			 * on a single file.
			 */

			MSDOS_I (inode)->i_binary = 1;
			/* #Specification: umsdos / i_nlink
			 * The nlink field of an inode is maintain by the MSDOS file system
			 * for directory and by UMSDOS for other file. The logic is that
			 * MSDOS is already figuring out what to do for directories and
			 * does nothing for other files. For MSDOS, there are no hard link
			 * so all file carry nlink==1. UMSDOS use some info in the
			 * EMD file to plug the correct value.
			 */
			if (!S_ISDIR (entry->mode)) {
				if (entry->nlink > 0) {
					inode->i_nlink = entry->nlink;
				} else {
					printk (KERN_ERR "UMSDOS: lookup_patch entry->nlink < 1 ???\n");
				}
			}
			umsdos_patch_inode (inode, dir, emd_pos);
		}
		if (S_ISDIR (inode->i_mode))
			umsdos_unlockcreate (inode);
		if (inode->u.umsdos_i.i_emd_owner == 0)
			printk (KERN_WARNING "emd_owner still 0 ???\n");
	}
}



struct UMSDOS_DIRENT_K {
	off_t f_pos;		/* will hold the offset of the entry in EMD */
	ino_t ino;
};


/*
 * Just to record the offset of one entry.
 */

static int umsdos_filldir_k (
				    void *buf,
				    const char *name,
				    int len,
				    off_t offset,
				    ino_t ino)
{
	struct UMSDOS_DIRENT_K *d = (struct UMSDOS_DIRENT_K *) buf;

	d->f_pos = offset;
	d->ino = ino;
	return 0;
}

struct UMSDOS_DIR_SEARCH {
	struct umsdos_dirent *entry;
	int found;
	ino_t search_ino;
};

static int umsdos_dir_search (
				     void *buf,
				     const char *name,
				     int len,
				     off_t offset,
				     ino_t ino)
{
	int ret = 0;
	struct UMSDOS_DIR_SEARCH *d = (struct UMSDOS_DIR_SEARCH *) buf;

	if (d->search_ino == ino) {
		d->found = 1;
		memcpy (d->entry->name, name, len);
		d->entry->name[len] = '\0';
		d->entry->name_len = len;
		ret = 1;	/* So fat_readdir will terminate */
	}
	return ret;
}



/*
 * Locate entry of an inode in a directory.
 * Return 0 or a negative error code.
 * 
 * Normally, this function must succeed. It means a strange corruption
 * in the file system if not.
 */

int umsdos_inode2entry (
			       struct inode *dir,
			       struct inode *inode,
			       struct umsdos_dirent *entry)
{				/* Will hold the entry */
	int ret = -ENOENT;

	if (pseudo_root && inode == pseudo_root) {
		/*
		 * Quick way to find the name.
		 * Also umsdos_readdir_x won't show /linux anyway
		 */
		memcpy (entry->name, UMSDOS_PSDROOT_NAME, UMSDOS_PSDROOT_LEN + 1);
		entry->name_len = UMSDOS_PSDROOT_LEN;
		ret = 0;
	} else {
		struct inode *emddir = umsdos_emd_dir_lookup (dir, 0);

		iput (emddir); /* FIXME? */
		if (emddir == NULL) {
			/* This is a DOS directory */
			struct UMSDOS_DIR_SEARCH bufk;
			struct file filp;
			struct dentry *i2e;
			
			i2e = creat_dentry ("i2e.nul", 7, dir, NULL);

			fill_new_filp (&filp, i2e);

			Printk ((KERN_ERR "umsdos_inode2entry emddir==NULL: WARNING: Known filp problem. segfaulting :) fixed ?/mn/\n"));
			filp.f_reada = 1;
			filp.f_pos = 0;
			bufk.entry = entry;
			bufk.search_ino = inode->i_ino;
			fat_readdir (&filp, &bufk, umsdos_dir_search);
			if (bufk.found) {
				ret = 0;
				inode->u.umsdos_i.i_dir_owner = dir->i_ino;
				inode->u.umsdos_i.i_emd_owner = 0;
				umsdos_setup_dir_inode (inode);
			}
		} else {
			/* skip . and .. see umsdos_readdir_x() */
			struct file filp;
			struct dentry *i2e;

			i2e = creat_dentry ("i2e.nn", 6, dir, NULL);
			fill_new_filp (&filp, i2e);

			filp.f_reada = 1;
			filp.f_pos = UMSDOS_SPECIAL_DIRFPOS;
			Printk ((KERN_ERR "umsdos_inode2entry skip...: WARNING: Known filp problem. segfaulting :) fixed ?/mn/\n"));
			while (1) {
				struct UMSDOS_DIRENT_K bufk;

				if (umsdos_readdir_x (dir, &filp, &bufk
				   ,1, entry, 0, umsdos_filldir_k) < 0) {
					printk ("UMSDOS: can't locate inode %ld in EMD file???\n"
						,inode->i_ino);
					break;
				} else if (bufk.ino == inode->i_ino) {
					ret = 0;
					umsdos_lookup_patch (dir, inode, entry, bufk.f_pos);
					break;
				}
			}
		}
	}
	return ret;
}


/*
 * Locate the parent of a directory and the info on that directory
 * Return 0 or a negative error code.
 */

static int umsdos_locate_ancestor (
					  struct inode *dir,
					  struct inode **result,
					  struct umsdos_dirent *entry)
{
	int ret;

	umsdos_patch_inode (dir, NULL, 0);
	/* FIXME */
	ret = compat_umsdos_real_lookup (dir, "..", 2, result);
	Printk (("result %d %p ", ret, *result));
	if (ret == 0) {
		struct inode *adir = *result;

		ret = umsdos_inode2entry (adir, dir, entry);
	}
	Printk (("\n"));
	return ret;
}


/*
 * Build the path name of an inode (relative to the file system.
 * This function is need to set (pseudo) hard link.
 * 
 * It uses the same strategy as the standard getcwd().
 */

int umsdos_locate_path (
			       struct inode *inode,
			       char *path)
{
	int ret = 0;
	struct inode *dir = inode;
	struct inode *root_inode;
	char *bpath = (char *) kmalloc (PATH_MAX, GFP_KERNEL);

	root_inode = iget (inode->i_sb, UMSDOS_ROOT_INO);
	if (bpath == NULL) {
		ret = -ENOMEM;
	} else {
		struct umsdos_dirent entry;
		char *ptbpath = bpath + PATH_MAX - 1;

		*ptbpath = '\0';
		Printk (("locate_path mode %x ", inode->i_mode));
		if (!S_ISDIR (inode->i_mode)) {
			ret = umsdos_get_dirowner (inode, &dir);
			Printk (("locate_path ret %d ", ret));
			if (ret == 0) {
				ret = umsdos_inode2entry (dir, inode, &entry);
				if (ret == 0) {
					ptbpath -= entry.name_len;
					memcpy (ptbpath, entry.name, entry.name_len);
					Printk (("ptbpath :%.*s: ", entry.name_len, ptbpath));
				}
			}
		} else {
			inc_count (dir);
		}
		if (ret == 0) {
			while (dir != root_inode) {
				struct inode *adir;

				ret = umsdos_locate_ancestor (dir, &adir, &entry);
				/* iput (dir); FIXME */
				dir = NULL;
				Printk (("ancestor %d ", ret));
				if (ret == 0) {
					*--ptbpath = '/';
					ptbpath -= entry.name_len;
					memcpy (ptbpath, entry.name, entry.name_len);
					dir = adir;
					Printk (("ptbpath :%.*s: ", entry.name_len, ptbpath));
				} else {
					break;
				}
			}
		}
		strcpy (path, ptbpath);
		kfree (bpath);
	}
	Printk (("\n"));
	iput (dir); /* FIXME?? */
	return ret;
}


/*
 * Return != 0 if an entry is the pseudo DOS entry in the pseudo root.
 */

int umsdos_is_pseudodos (
				struct inode *dir,
				struct dentry *dentry)
{
	/* #Specification: pseudo root / DOS hard coded
	 * The pseudo sub-directory DOS in the pseudo root is hard coded.
	 * The name is DOS. This is done this way to help standardised
	 * the umsdos layout. The idea is that from now on /DOS is
	 * a reserved path and nobody will think of using such a path
	 * for a package.
	 */
	return pseudo_root
	    && dir == pseudo_root
	    && dentry->d_name.len == 3
	    && dentry->d_name.name[0] == 'D'
	    && dentry->d_name.name[1] == 'O'
	    && dentry->d_name.name[2] == 'S';
}


/*
 * Check if a file exist in the current directory.
 * Return 0 if ok, negative error code if not (ex: -ENOENT).
 */

int umsdos_lookup_x (
			    struct inode *dir,
			    struct dentry *dentry,
			    int nopseudo)
{				/* Don't care about pseudo root mode */
	int ret = -ENOENT;
	struct inode *root_inode;
	int len = dentry->d_name.len;
	const char *name = dentry->d_name.name;

	PRINTK ((KERN_DEBUG "umsdos_lookup_x: /mn/ name=%.*s, dir=%lu (i_count=%d), d_parent=%p\n", (int) dentry->d_name.len, dentry->d_name.name, dir->i_ino, dir->i_count, dentry->d_parent));		/* FIXME /mn/ debug only */
	if (dentry->d_parent)
		PRINTK ((KERN_DEBUG "   d_parent is %.*s\n", (int) dentry->d_parent->d_name.len, dentry->d_parent->d_name.name));	/* FIXME : delme /mn/ */

	root_inode = iget (dir->i_sb, UMSDOS_ROOT_INO);
	Printk ((KERN_ERR "umsdos_lookup_x (CNT!): entering root_count=%d, dir %lu _count=%d\n", root_inode->i_count, dir->i_ino, dir->i_count));	/* FIXME: DEBUG, DELME */

	d_instantiate (dentry, NULL);
	umsdos_startlookup (dir);
	if (len == 1 && name[0] == '.') {
		d_add (dentry, dir);
		inc_count (dir);
		ret = 0;
	} else if (len == 2 && name[0] == '.' && name[1] == '.') {
		if (pseudo_root && dir == pseudo_root) {
			/* #Specification: pseudo root / .. in real root
			 * Whenever a lookup is those in the real root for
			 * the directory .., and pseudo root is active, the
			 * pseudo root is returned.
			 */
			ret = 0;
			d_add (dentry, pseudo_root);
			inc_count (pseudo_root);
		} else {
			/* #Specification: locating .. / strategy
			 * We use the msdos filesystem to locate the parent directory.
			 * But it is more complicated than that.
			 * 
			 * We have to step back even further to
			 * get the parent of the parent, so we can get the EMD
			 * of the parent of the parent. Using the EMD file, we can
			 * locate all the info on the parent, such a permissions
			 * and owner.
			 */

			ret = compat_umsdos_real_lookup (dir, "..", 2, &dentry->d_inode);
			Printk (("ancestor ret %d dir %p *result %p ", ret, dir, dentry->d_inode));
			if (ret == 0
			    && dentry->d_inode != root_inode
			    && dentry->d_inode != pseudo_root) {
				struct inode *aadir;
				struct umsdos_dirent entry;

				ret = umsdos_locate_ancestor (dentry->d_inode, &aadir, &entry);
				iput (aadir);	/* FIXME */
			}
		}
	} else if (umsdos_is_pseudodos (dir, dentry)) {
		/* #Specification: pseudo root / lookup(DOS)
		 * A lookup of DOS in the pseudo root will always succeed
		 * and return the inode of the real root.
		 */
		d_add (dentry, root_inode);
		inc_count (dentry->d_inode);
		ret = 0;
	} else {
		struct umsdos_info info;

		ret = umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);
		if (ret == 0)
			ret = umsdos_findentry (dir, &info, 0);
		Printk (("lookup %.*s pos %lu ret %d len %d ", info.fake.len, info.fake.fname, info.f_pos, ret
			 ,info.fake.len));
		if (ret == 0) {
			/* #Specification: umsdos / lookup
			 * A lookup for a file is done in two step. First, we locate
			 * the file in the EMD file. If not present, we return
			 * an error code (-ENOENT). If it is there, we repeat the
			 * operation on the msdos file system. If this fails, it means
			 * that the file system is not in sync with the emd file.
			 * We silently remove this entry from the emd file,
			 * and return ENOENT.
			 */
			struct inode *inode;

			ret = compat_umsdos_real_lookup (dir, info.fake.fname, info.fake.len, &inode);

			Printk ((KERN_DEBUG "umsdos_lookup_x: compat_umsdos_real_lookup for %.*s returned %d with inode=%p\n", info.fake.len, info.fake.fname, ret, inode));

			if (inode == NULL) {
				printk (KERN_WARNING "UMSDOS: Erase entry %.*s, out of sync with MsDOS\n"
					,info.fake.len, info.fake.fname);
				umsdos_delentry (dir, &info, S_ISDIR (info.entry.mode));
			} else {
				Printk ((KERN_DEBUG "umsdos_lookup_x /mn/ debug: ino=%li\n", inode->i_ino));

				/* we've found it. now put inode in dentry */
				d_add (dentry, inode);

				umsdos_lookup_patch (dir, inode, &info.entry, info.f_pos);
				Printk (("lookup ino %ld flags %d\n", inode->i_ino, info.entry.flags));
				if (info.entry.flags & UMSDOS_HLINK) {
					Printk ((KERN_DEBUG "umsdos_lookup_x: here goes HLINK\n"));
					ret = umsdos_hlink2inode (inode, &dentry->d_inode);
				}
				if (pseudo_root && dentry->d_inode == pseudo_root && !nopseudo) {
					/* #Specification: pseudo root / dir lookup
					 * For the same reason as readdir, a lookup in /DOS for
					 * the pseudo root directory (linux) will fail.
					 */
					/*
					 * This has to be allowed for resolving hard link
					 * which are recorded independently of the pseudo-root
					 * mode.
					 */
					Printk ((KERN_ERR "umsdos_lookup_x: warning: untested /mn/ Pseudo_root thingy\n"));
					iput (pseudo_root); /* FIXME?? */
					d_instantiate (dentry, NULL);		/* FIXME: should be dput(dentry) ? */
					ret = -ENOENT;
				}
			}
		}
	}
	umsdos_endlookup (dir);
	PRINTK ((KERN_DEBUG "umsdos_lookup_x: returning %d : name=%.*s (i_count=%d), dir=%lu (i_count=%d)\n", ret, (int) dentry->d_name.len, dentry->d_name.name, dentry->d_inode->i_count, dir->i_ino, dir->i_count));
	Printk ((KERN_ERR "umsdos_lookup_x (CNT!): exiting root_count=%d, dir %lu _count=%d\n", root_inode->i_count, dir->i_ino, dir->i_count));	/* FIXME: DEBUG, DELME */
	return ret;
}


/*
 * Check if a file exist in the current directory.
 * Return 0 if ok, negative error code if not (ex: -ENOENT).
 * 
 * 
 */

int UMSDOS_lookup (
			  struct inode *dir,
			  struct dentry *dentry)
{
	int ret;

	check_dentry (dentry);
	ret = umsdos_lookup_x (dir, dentry, 0);
	check_dentry (dentry);

#if 1
	if (ret == -ENOENT) {
		Printk ((KERN_DEBUG "UMSDOS_lookup: converting -ENOENT to negative dentry !\n"));
		d_add (dentry, NULL);	/* create negative dentry if not found */
		ret = 0;
	}
#endif

	return ret;
}



/*
 * Locate the inode pointed by a (pseudo) hard link
 * Return 0 if ok, a negative error code if not.
 */

int umsdos_hlink2inode (struct inode *hlink, struct inode **result)
{
	struct inode *root_inode;
	int ret = -EIO;
	struct dentry *dentry_src, *dentry_dst;
	char *path;

#if 0				/* FIXME: DELME */
	Printk (("FIXME: just test. hlink2inode returning -ENOENT\n /mn/\n"));
	return -ENOENT;		/* /mn/ FIXME just for test */
#endif

	path = (char *) kmalloc (PATH_MAX, GFP_KERNEL);

	root_inode = iget (hlink->i_sb, UMSDOS_ROOT_INO);
	*result = NULL;
	if (path == NULL) {
		ret = -ENOMEM;
		iput (hlink); /* FIXME? */
	} else {
		struct file filp;
		loff_t offs = 0;

		dentry_src = creat_dentry ("hlink-mn", 8, hlink, NULL);

		fill_new_filp (&filp, dentry_src);
		filp.f_flags = O_RDONLY;

		Printk (("hlink2inode "));
		if (umsdos_file_read_kmem (hlink, &filp, path, hlink->i_size, &offs) == hlink->i_size) {
			struct inode *dir;
			char *pt = path;

			dir = root_inode;
			path[hlink->i_size] = '\0';
			iput (hlink); /* FIXME? */
			inc_count (dir);
			while (1) {
				char *start = pt;
				int len;

				while (*pt != '\0' && *pt != '/')
					pt++;
				len = (int) (pt - start);
				if (*pt == '/')
					*pt++ = '\0';
				/* FIXME. /mn/ fixed ? */

				dentry_dst = creat_dentry (start, len, NULL, NULL);

				if (dir->u.umsdos_i.i_emd_dir == 0) {
					/* This is a DOS directory */

					Printk (("hlink2inode /mn/: doing umsdos_rlookup_x on %.*s\n", (int) dentry_dst->d_name.len, dentry_dst->d_name.name));
					ret = umsdos_rlookup_x (dir, dentry_dst, 1);
				} else {
					Printk (("hlink2inode /mn/: doing umsdos_lookup_x on %.*s\n", (int) dentry_dst->d_name.len, dentry_dst->d_name.name));
					ret = umsdos_lookup_x (dir, dentry_dst, 1);
				}
				Printk (("  returned %d\n", ret));
				*result = dentry_dst->d_inode;	/* /mn/ ok ? */

				Printk (("h2n lookup :%s: -> %d ", start, ret));
				if (ret == 0 && *pt != '\0') {
					dir = *result;
				} else {
					break;
				}
			}
		} else {
			Printk (("umsdos_hlink2inode: all those iput's() frighten me /mn/. Whatabout dput() ? FIXME!\n"));
			iput (hlink); /* FIXME? */
		}
		Printk (("hlink2inode ret = %d %p -> %p\n", ret, hlink, *result));
		kfree (path);
	}
	return ret;
}


static struct file_operations umsdos_dir_operations =
{
	NULL,			/* lseek - default */
	UMSDOS_dir_read,	/* read */
	NULL,			/* write - bad */
	UMSDOS_readdir,		/* readdir */
	NULL,			/* poll - default */
	UMSDOS_ioctl_dir,	/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* fsync *//* in original NULL. changed to file_fsync. FIXME? /mn/ */
};

struct inode_operations umsdos_dir_inode_operations =
{
	&umsdos_dir_operations,	/* default directory file-ops */
	UMSDOS_create,		/* create */
	UMSDOS_lookup,		/* lookup */
	UMSDOS_link,		/* link */
	UMSDOS_unlink,		/* unlink */
	UMSDOS_symlink,		/* symlink */
	UMSDOS_mkdir,		/* mkdir */
	UMSDOS_rmdir,		/* rmdir */
	UMSDOS_mknod,		/* mknod */
	UMSDOS_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* followlink */
	generic_readpage,	/* readpage *//* in original NULL. changed to generic_readpage. FIXME? /mn/ */
	NULL,			/* writepage */
	fat_bmap,		/* bmap *//* in original NULL. changed to fat_bmap. FIXME? /mn/ */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	NULL,			/* revalidate */

};
