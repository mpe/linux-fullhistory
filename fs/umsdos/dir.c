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

#define UMSDOS_SPECIAL_DIRFPOS	3
extern struct inode *pseudo_root;



/*
 * This needs to have the parent dentry passed to it.
 * N.B. Try to get rid of this soon!
 */
int compat_msdos_create (struct inode *dir, const char *name, int len, 
			int mode, struct inode **inode)
{
	int ret;
	struct dentry *dentry, *d_dir;

	check_inode (dir);
	ret = -ENOMEM;
	d_dir = geti_dentry (dir);
	if (!d_dir) {
printk(KERN_ERR "compat_msdos_create: flaky i_dentry didn't work\n");
		goto out;
	}
	dget(d_dir);
	dentry = creat_dentry (name, len, NULL, d_dir);
	dput(d_dir);
	if (!dentry)
		goto out;

	check_dentry_path (dentry, "compat_msdos_create START");
	ret = msdos_create (dir, dentry, mode);
	check_dentry_path (dentry, "compat_msdos_create END");
	if (ret)
		goto out;
	if (inode != NULL)
		*inode = dentry->d_inode;

	check_inode (dir);
out:
	return ret;
}


/*
 * So  grep *  doesn't complain in the presence of directories.
 */
 
int dummy_dir_read (struct file *filp, char *buff, size_t size, loff_t *count)
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
		PRINTK ((KERN_DEBUG "dir_once :%.*s: offset %Ld\n", 
			len, name, offset));
		ret = d->filldir (d->dirbuf, name, len, offset, ino);
		d->stop = ret < 0;
		d->count = 1;
	}
	return ret;
}


/*
 * Read count directory entries from directory filp
 * Return a negative value from linux/errno.h.
 * Return > 0 if success (the number of bytes written by filldir).
 * 
 * This function is used by the normal readdir VFS entry point and by
 * some function who try to find out info on a file from a pure MSDOS
 * inode. See umsdos_locate_ancestor() below.
 */
 
static int umsdos_readdir_x (struct inode *dir, struct file *filp,
				void *dirbuf, int internal_read,
				struct umsdos_dirent *u_entry,
				int follow_hlink, filldir_t filldir)
{
	struct dentry *demd;
	off_t start_fpos;
	int ret = 0;
	struct file new_filp;

	umsdos_startlookup (dir);

	if (filp->f_pos == UMSDOS_SPECIAL_DIRFPOS &&
	    dir == pseudo_root && !internal_read) {

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
		if (filldir (dirbuf, "DOS", 3, 
				UMSDOS_SPECIAL_DIRFPOS, UMSDOS_ROOT_INO) == 0) {
			filp->f_pos++;
		}
		goto out_end;
	}

	if (filp->f_pos < 2 || 
	    (dir->i_ino != UMSDOS_ROOT_INO && filp->f_pos == 32)) {
	
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
		goto out_end;
	}

	Printk (("umsdos_readdir_x: normal file /mn/?\n"));

	/* get the EMD dentry */
	demd = umsdos_get_emd_dentry(filp->f_dentry);
	ret = PTR_ERR(demd);
	if (IS_ERR(demd))
		goto out_end;
	ret = 0;
	if (!demd->d_inode)
		goto out_dput;

	/* set up our private filp ... */
	fill_new_filp(&new_filp, demd);
	new_filp.f_pos = filp->f_pos;
	start_fpos = filp->f_pos;

	if (new_filp.f_pos <= UMSDOS_SPECIAL_DIRFPOS + 1)
		new_filp.f_pos = 0;
Printk (("f_pos %Ld i_size %ld\n", new_filp.f_pos, demd->d_inode->i_size));
	ret = 0;
	while (new_filp.f_pos < demd->d_inode->i_size) {
		off_t cur_f_pos = new_filp.f_pos;
		struct umsdos_info info;
		struct dentry *dret;
		struct umsdos_dirent entry;

		ret = -EIO;
		if (umsdos_emd_dir_readentry (&new_filp, &entry) != 0)
			break;

		if (entry.name_len == 0)
			goto remove_name;

		umsdos_parse (entry.name, entry.name_len, &info);
		info.f_pos = cur_f_pos;
		umsdos_manglename (&info);
		dret = umsdos_lookup_dentry(filp->f_dentry, info.fake.fname,
						 info.fake.len);
		ret = PTR_ERR(dret);
		if (IS_ERR(dret))
			break;

Printk (("Looking for inode of %s/%s, flags=%x\n",
dret->d_parent->d_name.name, info.fake.fname, entry.flags));
		if ((entry.flags & UMSDOS_HLINK) && follow_hlink) {
			dret = umsdos_solve_hlink (dret);
			ret = PTR_ERR(dret);
			if (IS_ERR(dret))
				break;
		}
					
		/* #Specification:  pseudo root / reading real root
		 * The pseudo root (/linux) is logically
		 * erased from the real root.  This means that
		 * ls /DOS, won't show "linux". This avoids
		 * infinite recursion (/DOS/linux/DOS/linux/...) while
		 * walking the file system.
		 */
		if (dret->d_inode != pseudo_root &&
		    (internal_read || !(entry.flags & UMSDOS_HIDDEN))) {
			Printk ((KERN_DEBUG "filldir now\n"));
			if (filldir (dirbuf, entry.name, entry.name_len,
				 cur_f_pos, dret->d_inode->i_ino) < 0) {
				new_filp.f_pos = cur_f_pos;
			}
Printk (("Found %s/%s(%ld)\n",
dret->d_parent->d_name.name, dret->d_name.name, dret->d_inode->i_ino));
			if (u_entry != NULL)
				*u_entry = entry;
			dput(dret);
			break;
		}
		dput(dret);
		continue;

	remove_name:
		/* #Specification:  umsdos / readdir / not in MSDOS
		 * During a readdir operation, if the file is not
		 * in the MS-DOS directory any more, the entry is
		 * removed from the EMD file silently.
		 */
		Printk (("'Silently' removing EMD for file\n"));
		ret = umsdos_delentry(filp->f_dentry, &info, 1);
		if (ret)
			break;
		continue;
	}
	/*
	 * If the fillbuf has failed, f_pos is back to 0.
	 * To avoid getting back into the . and .. state
	 * (see comments at the beginning), we put back
	 * the special offset.
	 */
	filp->f_pos = new_filp.f_pos;
	if (filp->f_pos == 0)
		filp->f_pos = start_fpos;
out_dput:
	dput(demd);

out_end:
	umsdos_endlookup (dir);
	
	Printk ((KERN_DEBUG "read dir %p pos %Ld ret %d\n",
		dir, filp->f_pos, ret));
	return ret;
}


/*
 * Read count directory entries from directory filp.
 * Return a negative value from linux/errno.h.
 * Return 0 or positive if successful.
 */
 
static int UMSDOS_readdir (struct file *filp, void *dirbuf, filldir_t filldir)
{
	struct inode *dir = filp->f_dentry->d_inode;
	int ret = 0, count = 0;
	struct UMSDOS_DIR_ONCE bufk;

	bufk.dirbuf = dirbuf;
	bufk.filldir = filldir;
	bufk.stop = 0;

	Printk (("UMSDOS_readdir in\n"));
	while (ret == 0 && bufk.stop == 0) {
		struct umsdos_dirent entry;

		bufk.count = 0;
		PRINTK (("UMSDOS_readdir: calling _x (%p,%p,%p,%d,%p,%d,%p)\n",
			dir, filp, &bufk, 0, &entry, 1, umsdos_dir_once));
		ret = umsdos_readdir_x (dir, filp, &bufk, 0, &entry, 1, 
					umsdos_dir_once);
		if (bufk.count == 0)
			break;
		count += bufk.count;
	}
	Printk (("UMSDOS_readdir out %d count %d pos %Ld\n", 
		ret, count, filp->f_pos));
	return count ? : ret;
}


/*
 * Complete the inode content with info from the EMD file.
 *
 * This function modifies the state of a dir inode.  It decides
 * whether the dir is a UMSDOS or DOS directory.  This is done
 * deeper in umsdos_patch_inode() called at the end of this function.
 * 
 * Because it is does disk access, umsdos_patch_inode() may block.
 * At the same time, another process may get here to initialise
 * the same directory inode. There are three cases.
 * 
 * 1) The inode is already initialised.  We do nothing.
 * 2) The inode is not initialised.  We lock access and do it.
 * 3) Like 2 but another process has locked the inode, so we try
 * to lock it and check right afterward check whether
 * initialisation is still needed.
 * 
 * 
 * Thanks to the "mem" option of the kernel command line, it was
 * possible to consistently reproduce this problem by limiting
 * my memory to 4 MB and running X.
 *
 * Do this only if the inode is freshly read, because we will lose
 * the current (updated) content.
 *
 * A lookup of a mount point directory yield the inode into
 * the other fs, so we don't care about initialising it. iget()
 * does this automatically.
 */

void umsdos_lookup_patch (struct inode *dir, struct inode *inode,
			 struct umsdos_dirent *entry, off_t emd_pos)
{
	if (inode->i_sb != dir->i_sb)
		goto out;
	if (umsdos_isinit (inode))
		goto out;

	if (S_ISDIR (inode->i_mode))
		umsdos_lockcreate (inode);
	if (umsdos_isinit (inode))
		goto out_unlock;

	if (S_ISREG (entry->mode))
		entry->mtime = inode->i_mtime;
	inode->i_mode = entry->mode;
	inode->i_rdev = to_kdev_t (entry->rdev);
	inode->i_atime = entry->atime;
	inode->i_ctime = entry->ctime;
	inode->i_mtime = entry->mtime;
	inode->i_uid = entry->uid;
	inode->i_gid = entry->gid;

	MSDOS_I (inode)->i_binary = 1;
	/* #Specification: umsdos / i_nlink
	 * The nlink field of an inode is maintained by the MSDOS file system
	 * for directory and by UMSDOS for other files.  The logic is that
	 * MSDOS is already figuring out what to do for directories and
	 * does nothing for other files.  For MSDOS, there are no hard links
	 * so all file carry nlink==1.  UMSDOS use some info in the
	 * EMD file to plug the correct value.
	 */
	if (!S_ISDIR (entry->mode)) {
		if (entry->nlink > 0) {
			inode->i_nlink = entry->nlink;
		} else {
			printk (KERN_ERR 
				"UMSDOS:  lookup_patch entry->nlink < 1 ???\n");
		}
	}
	umsdos_patch_inode (inode, dir, emd_pos);

out_unlock:
	if (S_ISDIR (inode->i_mode))
		umsdos_unlockcreate (inode);
	if (inode->u.umsdos_i.i_emd_owner == 0)
		printk (KERN_WARNING "UMSDOS:  emd_owner still 0?\n");
out:
	return;
}


/*
 * The preferred interface to the above routine ...
 */
void umsdos_lookup_patch_new(struct dentry *dentry, struct umsdos_dirent *entry,
				off_t emd_pos)
{
	umsdos_lookup_patch(dentry->d_parent->d_inode, dentry->d_inode, entry,
				emd_pos);
}


struct UMSDOS_DIRENT_K {
	off_t f_pos;		/* will hold the offset of the entry in EMD */
	ino_t ino;
};


/*
 * Just to record the offset of one entry.
 */

static int umsdos_filldir_k (	    void *buf,
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

static int umsdos_dir_search (	     void *buf,
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
 * Locate the directory entry for a dentry in its parent directory.
 * Return 0 or a negative error code.
 * 
 * Normally, this function must succeed.  It means a strange corruption
 * in the file system if not.
 */

int umsdos_dentry_to_entry(struct dentry *dentry, struct umsdos_dirent *entry)
{
	struct dentry *parent = dentry->d_parent;
	struct inode *inode = dentry->d_inode;
	int ret = -ENOENT, err;
	struct file filp;
	struct UMSDOS_DIR_SEARCH bufsrch;
	struct UMSDOS_DIRENT_K bufk;

	if (pseudo_root && inode == pseudo_root) {
		/*
		 * Quick way to find the name.
		 * Also umsdos_readdir_x won't show /linux anyway
		 */
		memcpy (entry->name, UMSDOS_PSDROOT_NAME, UMSDOS_PSDROOT_LEN + 1);
		entry->name_len = UMSDOS_PSDROOT_LEN;
		ret = 0;
		goto out;
	}

	/* initialize the file */
	fill_new_filp (&filp, parent);

	if (!umsdos_have_emd(parent)) {
		/* This is a DOS directory. */
		filp.f_pos = 0;
		bufsrch.entry = entry;
		bufsrch.search_ino = inode->i_ino;
		fat_readdir (&filp, &bufsrch, umsdos_dir_search);
		if (bufsrch.found) {
			ret = 0;
			inode->u.umsdos_i.i_dir_owner = parent->d_inode->i_ino;
			inode->u.umsdos_i.i_emd_owner = 0;
if (!S_ISDIR(inode->i_mode))
printk("UMSDOS: %s/%s not a directory!\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
			/* N.B. why call this? not always a dir ... */
			umsdos_setup_dir(dentry);
		}
		goto out;
	}

	/* skip . and .. see umsdos_readdir_x() */
	filp.f_pos = UMSDOS_SPECIAL_DIRFPOS;
	while (1) {
		err = umsdos_readdir_x (parent->d_inode, &filp, &bufk, 1, 
				entry, 0, umsdos_filldir_k);
		if (err < 0) { 
			printk ("UMSDOS: can't locate inode %ld in EMD??\n",
				inode->i_ino);
			break;
		}
		if (bufk.ino == inode->i_ino) {
			ret = 0;
			umsdos_lookup_patch_new(dentry, entry, bufk.f_pos);
			break;
		}
	}
out:
	return ret;
}

/*
 * Deprecated. Try to get rid of this soon!
 */
int umsdos_inode2entry (struct inode *dir, struct inode *inode,
			       struct umsdos_dirent *entry)
{
	int ret = -ENOENT;
	struct inode *emddir;
	struct dentry *i2e;
	struct file filp;
	struct UMSDOS_DIR_SEARCH bufsrch;
	struct UMSDOS_DIRENT_K bufk;

	if (pseudo_root && inode == pseudo_root) {
		/*
		 * Quick way to find the name.
		 * Also umsdos_readdir_x won't show /linux anyway
		 */
		memcpy (entry->name, UMSDOS_PSDROOT_NAME, UMSDOS_PSDROOT_LEN + 1);
		entry->name_len = UMSDOS_PSDROOT_LEN;
		ret = 0;
		goto out;
	}

	emddir = umsdos_emd_dir_lookup (dir, 0);
	if (emddir == NULL) {
		/* This is a DOS directory. */
		i2e = creat_dentry ("@i2e.nul@", 9, dir, NULL);
		fill_new_filp (&filp, i2e);
		filp.f_reada = 1;
		filp.f_pos = 0;
		bufsrch.entry = entry;
		bufsrch.search_ino = inode->i_ino;
		fat_readdir (&filp, &bufsrch, umsdos_dir_search);
		if (bufsrch.found) {
			ret = 0;
			inode->u.umsdos_i.i_dir_owner = dir->i_ino;
			inode->u.umsdos_i.i_emd_owner = 0;
			umsdos_setup_dir_inode (inode);
		}
		goto out;
	}

	/* skip . and .. see umsdos_readdir_x() */

	i2e = creat_dentry ("@i2e.nn@", 8, dir, NULL);
	fill_new_filp (&filp, i2e);
	filp.f_reada = 1;
	filp.f_pos = UMSDOS_SPECIAL_DIRFPOS;
	while (1) {
		if (umsdos_readdir_x (dir, &filp, &bufk, 1, 
				entry, 0, umsdos_filldir_k) < 0) {
			printk ("UMSDOS: can't locate inode %ld in EMD??\n",
				inode->i_ino);
			break;
		}
		if (bufk.ino == inode->i_ino) {
			ret = 0;
			umsdos_lookup_patch (dir, inode, entry, bufk.f_pos);
			break;
		}
	}
	iput (emddir);
out:
	return ret;
}


/*
 * Return != 0 if an entry is the pseudo DOS entry in the pseudo root.
 */

int umsdos_is_pseudodos (struct inode *dir, struct dentry *dentry)
{
	/* #Specification: pseudo root / DOS hard coded
	 * The pseudo sub-directory DOS in the pseudo root is hard coded.
	 * The name is DOS. This is done this way to help standardised
	 * the umsdos layout. The idea is that from now on /DOS is
	 * a reserved path and nobody will think of using such a path
	 * for a package.
	 */
	return dir == pseudo_root
	    && dentry->d_name.len == 3
	    && dentry->d_name.name[0] == 'D'
	    && dentry->d_name.name[1] == 'O'
	    && dentry->d_name.name[2] == 'S';
}


/*
 * Check whether a file exists in the current directory.
 * Return 0 if OK, negative error code if not (ex: -ENOENT).
 *
 * fills dentry->d_inode with found inode, and increments its count.
 * if not found, return -ENOENT.
 */
/* #Specification: umsdos / lookup
 * A lookup for a file is done in two steps.  First, we
 * locate the file in the EMD file.  If not present, we
 * return an error code (-ENOENT).  If it is there, we
 * repeat the operation on the msdos file system. If
 * this fails, it means that the file system is not in
 * sync with the EMD file.   We silently remove this
 * entry from the EMD file, and return ENOENT.
 */

int umsdos_lookup_x (struct inode *dir, struct dentry *dentry, int nopseudo)
{				
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	struct dentry *dret = NULL;
	struct inode *inode;
	int ret = -ENOENT;
	struct umsdos_info info;

	umsdos_startlookup (dir);
	/* this shouldn't happen ... */
	if (len == 1 && name[0] == '.') {
		printk("umsdos_lookup_x: UMSDOS broken, please report!\n");
		goto out;
	}

	/* this shouldn't happen ... */
	if (len == 2 && name[0] == '.' && name[1] == '.') {
		printk("umsdos_lookup_x: UMSDOS broken, please report!\n");
		goto out;
	}

	if (umsdos_is_pseudodos (dir, dentry)) {
		/* #Specification: pseudo root / lookup(DOS)
		 * A lookup of DOS in the pseudo root will always succeed
		 * and return the inode of the real root.
		 */
		inode = iget(dir->i_sb, UMSDOS_ROOT_INO);
		if (inode)
			goto out_add;
		ret = -ENOMEM;
		goto out;
	}

	ret = umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);
	if (ret)
		goto out;
	ret = umsdos_findentry (dentry->d_parent, &info, 0);
Printk (("lookup %.*s pos %lu ret %d len %d ", 
info.fake.len, info.fake.fname, info.f_pos, ret, info.fake.len));
	if (ret)
		goto out;


	dret = umsdos_lookup_dentry(dentry->d_parent, info.fake.fname,
					info.fake.len);
	ret = PTR_ERR(dret);
	if (IS_ERR(dret))
		goto out;
	if (!dret->d_inode)
		goto out_remove;

	umsdos_lookup_patch_new(dret, &info.entry, info.f_pos);

	/* Check for a hard link */
	if (info.entry.flags & UMSDOS_HLINK) {
Printk (("checking hard link %s/%s, ino=%ld, flags=%x\n", 
dret->d_parent->d_name.name, dret->d_name.name,
dret->d_inode->i_ino, info.entry.flags));
		dret = umsdos_solve_hlink (dret);
		ret = PTR_ERR(dret);
		if (IS_ERR(dret))
			goto out;
	}
	/* N.B. can dentry be negative after resolving hlinks? */

	if (pseudo_root && dret->d_inode == pseudo_root && !nopseudo) {
		/* #Specification: pseudo root / dir lookup
		 * For the same reason as readdir, a lookup in /DOS for
		 * the pseudo root directory (linux) will fail.
		 */
		/*
		 * This has to be allowed for resolving hard links
		 * which are recorded independently of the pseudo-root
		 * mode.
		 */
		Printk (("umsdos_lookup_x: untested Pseudo_root\n"));
		ret = -ENOENT;
		goto out_dput;
	} else {
		/* We've found it OK.  Now put inode in dentry. */
		inode = dret->d_inode;
	}

	/*
	 * Hash the dentry with the inode.
	 */
out_add:
	inode->i_count++;
	d_add (dentry, inode);
	ret = 0;

out_dput:
	dput(dret);
out:
	umsdos_endlookup (dir);
	return ret;

out_remove:
	printk(KERN_WARNING "UMSDOS:  entry %s/%s out of sync, erased\n",
		dentry->d_name.name, info.fake.fname);
	umsdos_delentry (dentry->d_parent, &info, S_ISDIR (info.entry.mode));
	ret = -ENOENT;
	goto out;
}


/*
 * Check whether a file exists in the current directory.
 * Return 0 if OK, negative error code if not (ex: -ENOENT).
 * 
 * called by VFS. should fill dentry->d_inode (via d_add), and 
 * set (increment) dentry->d_inode->i_count.
 *
 */

int UMSDOS_lookup (struct inode *dir, struct dentry *dentry)
{
	int ret;

	ret = umsdos_lookup_x (dir, dentry, 0);

	/* Create negative dentry if not found. */
	if (ret == -ENOENT) {
		Printk ((KERN_DEBUG 
			"UMSDOS_lookup: converting -ENOENT to negative\n"));
		d_add (dentry, NULL);
		ret = 0;
	}
	return ret;
}


/*
 * Lookup or create a dentry from within the filesystem.
 *
 * We need to use this instead of lookup_dentry, as the 
 * directory semaphore lock is already held.
 */
struct dentry *umsdos_lookup_dentry(struct dentry *parent, char *name, int len)
{
	struct dentry *result, *dentry;
	int error;
	struct qstr qstr;

	qstr.name = name;
	qstr.len  = len;
	qstr.hash = full_name_hash(name, len);
	result = d_lookup(parent, &qstr);
	if (!result) {
		result = ERR_PTR(-ENOMEM);
		dentry = d_alloc(parent, &qstr);
		if (dentry) {
			result = dentry;
			error = umsdos_real_lookup(parent->d_inode, result);
			if (error)
				goto out_fail;
		}
	}
out:
	return result;

out_fail:
	dput(result);
	result = ERR_PTR(error);
	goto out;
}


/*
 * gets dentry which points to pseudo-hardlink
 *
 * it should try to find file it points to
 * if file is found, it should dput() original dentry and return new one
 * (with d_count = i_count = 1)
 * Otherwise, it should return with error, with dput()ed original dentry.
 *
 */

struct dentry *umsdos_solve_hlink (struct dentry *hlink)
{
	/* root is our root for resolving pseudo-hardlink */
	struct dentry *base = hlink->d_sb->s_root;
	struct dentry *final, *dir, *dentry_dst;
	char *path, *pt;
	unsigned long len;
	int ret = -EIO;
	struct file filp;

  	check_dentry_path (hlink, "HLINK BEGIN hlink");

	final = ERR_PTR (-ENOMEM);
	path = (char *) kmalloc (PATH_MAX, GFP_KERNEL);
	if (path == NULL)
		goto out;

	fill_new_filp (&filp, hlink);
	filp.f_flags = O_RDONLY;
	filp.f_pos = 0;

	Printk (("hlink2inode "));
	len = umsdos_file_read_kmem (&filp, path, hlink->d_inode->i_size);
	if (len != hlink->d_inode->i_size)
		goto out_noread;

	/* start at root dentry */
	dir = dget(base);
	path[hlink->d_inode->i_size] = '\0';
	pt = path;
	while (1) {
		char *start = pt;
		int len;

		while (*pt != '\0' && *pt != '/') pt++;
		len = (int) (pt - start);
		if (*pt == '/') *pt++ = '\0';

		dentry_dst = umsdos_lookup_dentry(dir, start, len);
		if (IS_ERR(dentry_dst))
			break;
		if (dir->d_inode->u.umsdos_i.i_emd_dir == 0) {
			/* This is a DOS directory */
			ret = umsdos_rlookup_x (dir->d_inode, dentry_dst, 1);
		} else {
			ret = umsdos_lookup_x (dir->d_inode, dentry_dst, 1);
		}
		Printk (("  returned %d\n", ret));
		dput (dir);	/* dir no longer needed */
		dir = dentry_dst;

		Printk (("h2n lookup :%s: -> %d ", start, ret));
		final = ERR_PTR (ret);
		if (ret != 0) {
			/* path component not found! */
			break;
		}
		if (*pt == '\0') {	/* we're finished! */
			final = umsdos_lookup_dentry(hlink->d_parent,
						(char *) hlink->d_name.name,
					 	hlink->d_name.len);
			break;
		}
	} /* end while */
	/*
	 * See whether we found the path ...
	 */
	if (!IS_ERR(final)) {
		if (!final->d_inode) {
			d_instantiate(final, dir->d_inode);
			/* we need inode to survive. */
			dir->d_inode->i_count++;	
		} else {
			printk ("umsdos_solve_hlink: %s/%s already exists\n",
				final->d_parent->d_name.name,
				final->d_name.name);
		}
printk ("umsdos_solve_hlink: ret = %d, %s/%s -> %s/%s\n",
ret, hlink->d_parent->d_name.name, hlink->d_name.name,
final->d_parent->d_name.name, final->d_name.name);
	}
	dput(dir);

out_free:
	kfree (path);

out:
	dput(hlink);	/* original hlink no longer needed */
  	check_dentry_path (hlink, "HLINK FIN hlink");
  	check_dentry_path (final, "HLINK RET final");
	return final;

out_noread:
	printk("umsdos_solve_hlink: failed reading pseudolink!\n");
	goto out_free;
}	


static struct file_operations umsdos_dir_operations =
{
	NULL,			/* lseek - default */
	dummy_dir_read,		/* read */
	NULL,			/* write - bad */
	UMSDOS_readdir,		/* readdir */
	NULL,			/* poll - default */
	UMSDOS_ioctl_dir,	/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* fsync */
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
	generic_readpage,	/* readpage */
	NULL,			/* writepage */
	fat_bmap,		/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	NULL,			/* revalidate */

};
