/*
 *  linux/fs/umsdos/namei.c
 *
 *      Written 1993 by Jacques Gelinas 
 *      Inspired from linux/fs/msdos/... by Werner Almesberger
 *
 * Maintain and access the --linux alternate directory file.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#if 1
/*
 * Wait for creation exclusivity.
 * Return 0 if the dir was already available.
 * Return 1 if a wait was necessary.
 * When 1 is return, it means a wait was done. It does not
 * mean the directory is available.
 */
static int umsdos_waitcreate (struct inode *dir)
{
	int ret = 0;

	if (dir->u.umsdos_i.u.dir_info.creating
	    && dir->u.umsdos_i.u.dir_info.pid != current->pid) {
		sleep_on (&dir->u.umsdos_i.u.dir_info.p);
		ret = 1;
	}
	return ret;
}

/*
 * Wait for any lookup process to finish
 */
static void umsdos_waitlookup (struct inode *dir)
{
	while (dir->u.umsdos_i.u.dir_info.looking) {
		sleep_on (&dir->u.umsdos_i.u.dir_info.p);
	}
}

/*
 * Lock all other process out of this directory.
 */
/* #Specification: file creation / not atomic
 * File creation is a two step process. First we create (allocate)
 * an entry in the EMD file and then (using the entry offset) we
 * build a unique name for MSDOS. We create this name in the msdos
 * space.
 * 
 * We have to use semaphore (sleep_on/wake_up) to prevent lookup
 * into a directory when we create a file or directory and to
 * prevent creation while a lookup is going on. Since many lookup
 * may happen at the same time, the semaphore is a counter.
 * 
 * Only one creation is allowed at the same time. This protection
 * may not be necessary. The problem arise mainly when a lookup
 * or a readdir is done while a file is partially created. The
 * lookup process see that as a "normal" problem and silently
 * erase the file from the EMD file. Normal because a file
 * may be erased during a MSDOS session, but not removed from
 * the EMD file.
 * 
 * The locking is done on a directory per directory basis. Each
 * directory inode has its wait_queue.
 * 
 * For some operation like hard link, things even get worse. Many
 * creation must occur at once (atomic). To simplify the design
 * a process is allowed to recursively lock the directory for
 * creation. The pid of the locking process is kept along with
 * a counter so a second level of locking is granted or not.
 */
void umsdos_lockcreate (struct inode *dir)
{
	/*
	 * Wait for any creation process to finish except
	 * if we (the process) own the lock
	 */
	while (umsdos_waitcreate (dir) != 0);
	dir->u.umsdos_i.u.dir_info.creating++;
	dir->u.umsdos_i.u.dir_info.pid = current->pid;
	umsdos_waitlookup (dir);
}

/*
 * Lock all other process out of those two directories.
 */
static void umsdos_lockcreate2 (struct inode *dir1, struct inode *dir2)
{
	/*
	 * We must check that both directory are available before
	 * locking anyone of them. This is to avoid some deadlock.
	 * Thanks to dglaude@is1.vub.ac.be (GLAUDE DAVID) for pointing
	 * this to me.
	 */
	while (1) {
		if (umsdos_waitcreate (dir1) == 0
		    && umsdos_waitcreate (dir2) == 0) {
			/* We own both now */
			dir1->u.umsdos_i.u.dir_info.creating++;
			dir1->u.umsdos_i.u.dir_info.pid = current->pid;
			dir2->u.umsdos_i.u.dir_info.creating++;
			dir2->u.umsdos_i.u.dir_info.pid = current->pid;
			break;
		}
	}
	umsdos_waitlookup (dir1);
	umsdos_waitlookup (dir2);
}

/*
 * Wait until creation is finish in this directory.
 */
void umsdos_startlookup (struct inode *dir)
{
	while (umsdos_waitcreate (dir) != 0);
	dir->u.umsdos_i.u.dir_info.looking++;
}

/*
 * Unlock the directory.
 */
void umsdos_unlockcreate (struct inode *dir)
{
	dir->u.umsdos_i.u.dir_info.creating--;
	if (dir->u.umsdos_i.u.dir_info.creating < 0) {
		printk ("UMSDOS: dir->u.umsdos_i.u.dir_info.creating < 0: %d"
			,dir->u.umsdos_i.u.dir_info.creating);
	}
	wake_up (&dir->u.umsdos_i.u.dir_info.p);
}

/*
 * Tell directory lookup is over.
 */
void umsdos_endlookup (struct inode *dir)
{
	dir->u.umsdos_i.u.dir_info.looking--;
	if (dir->u.umsdos_i.u.dir_info.looking < 0) {
		printk ("UMSDOS: dir->u.umsdos_i.u.dir_info.looking < 0: %d"
			,dir->u.umsdos_i.u.dir_info.looking);
	}
	wake_up (&dir->u.umsdos_i.u.dir_info.p);
}

#else
static void umsdos_lockcreate (struct inode *dir)
{
}
static void umsdos_lockcreate2 (struct inode *dir1, struct inode *dir2)
{
}
void umsdos_startlookup (struct inode *dir)
{
}
static void umsdos_unlockcreate (struct inode *dir)
{
}
void umsdos_endlookup (struct inode *dir)
{
}

#endif

/*
 * Check whether we can delete from the directory.
 */
static int is_sticky(struct inode *dir, int uid)
{
	return !((dir->i_mode & S_ISVTX) == 0 || 
		capable (CAP_FOWNER) ||
		current->fsuid == uid ||
		current->fsuid == dir->i_uid);
}


static int umsdos_nevercreat (struct inode *dir, struct dentry *dentry,
				int errcod)
{
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	int ret = 0;

	if (umsdos_is_pseudodos (dir, dentry)) {
		/* #Specification: pseudo root / any file creation /DOS
		 * The pseudo sub-directory /DOS can't be created!
		 * EEXIST is returned.
		 * 
		 * The pseudo sub-directory /DOS can't be removed!
		 * EPERM is returned.
		 */
		ret = -EPERM;
		ret = errcod;
	} else if (name[0] == '.'
		   && (len == 1 || (len == 2 && name[1] == '.'))) {
		/* #Specification: create / . and ..
		 * If one try to creates . or .., it always fail and return
		 * EEXIST.
		 * 
		 * If one try to delete . or .., it always fail and return
		 * EPERM.
		 * 
		 * This should be test at the VFS layer level to avoid
		 * duplicating this in all file systems. Any comments ?
		 */
		ret = errcod;
	}
	return ret;
}

/*
 * Add a new file (ordinary or special) into the alternate directory.
 * The file is added to the real MSDOS directory. If successful, it
 * is then added to the EMD file.
 * 
 * Return the status of the operation. 0 mean success.
 *
 * #Specification: create / file exist in DOS
 * Here is a situation. Trying to create a file with
 * UMSDOS. The file is unknown to UMSDOS but already
 * exists in the DOS directory.
 * 
 * Here is what we are NOT doing:
 * 
 * We could silently assume that everything is fine
 * and allows the creation to succeed.
 * 
 * It is possible not all files in the partition
 * are meant to be visible from linux. By trying to create
 * those file in some directory, one user may get access
 * to those file without proper permissions. Looks like
 * a security hole to me. Off course sharing a file system
 * with DOS is some kind of security hole :-)
 * 
 * So ?
 * 
 * We return EEXIST in this case.
 * The same is true for directory creation.
 */
static int umsdos_create_any (struct inode *dir, struct dentry *dentry,
				int mode, int rdev, char flags)
{
	struct dentry *fake;
	struct inode *inode;
	int ret;
	struct umsdos_info info;

if (dentry->d_inode)
printk("umsdos_create_any: %s/%s not negative!\n",
dentry->d_parent->d_name.name, dentry->d_name.name);

Printk (("umsdos_create_any /mn/: create %.*s in dir=%lu - nevercreat=/",
(int) dentry->d_name.len, dentry->d_name.name, dir->i_ino));

	check_dentry_path (dentry, "umsdos_create_any");
	ret = umsdos_nevercreat (dir, dentry, -EEXIST);
	if (ret) {
Printk (("%d/\n", ret));
		goto out;
	}

	ret = umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);
	if (ret)
		goto out;

	info.entry.mode = mode;
	info.entry.rdev = rdev;
	info.entry.flags = flags;
	info.entry.uid = current->fsuid;
	info.entry.gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
	info.entry.ctime = info.entry.atime = info.entry.mtime = CURRENT_TIME;
	info.entry.nlink = 1;
	umsdos_lockcreate (dir);
	ret = umsdos_newentry (dentry->d_parent, &info);
	if (ret)
		goto out_unlock;

	/* create short name dentry */
	fake = umsdos_lookup_dentry(dentry->d_parent, info.fake.fname, 
					info.fake.len);
	ret = PTR_ERR(fake);
	if (IS_ERR(fake))
		goto out_unlock;

	/* should not exist yet ... */
	ret = -EEXIST;
	if (fake->d_inode)
		goto out_remove;

	ret = msdos_create (dir, fake, S_IFREG | 0777);
	if (ret)
		goto out_remove;

	inode = fake->d_inode;
	umsdos_lookup_patch_new(fake, &info.entry, info.f_pos);

Printk (("inode %p[%lu], count=%d ", inode, inode->i_ino, inode->i_count));
Printk (("Creation OK: [dir %lu] %.*s pid=%d pos %ld\n",
dir->i_ino, info.fake.len, info.fake.fname, current->pid, info.f_pos));

	check_dentry_path (dentry, "umsdos_create_any: BEG dentry");
	check_dentry_path (fake, "umsdos_create_any: BEG fake");

	/*
	 * Note! The long and short name might be the same,
	 * so check first before doing the instantiate ...
	 */
	if (dentry != fake) {
		/* long name also gets inode info */
		inode->i_count++;
		d_instantiate (dentry, inode);
	}

	check_dentry_path (dentry, "umsdos_create_any: END dentry");
	check_dentry_path (fake, "umsdos_create_any: END fake");
	goto out_dput;

out_remove:
if (ret == -EEXIST)
printk("UMSDOS: out of sync, error [%ld], deleting %.*s %d %d pos %ld\n",
dir->i_ino ,info.fake.len, info.fake.fname, -ret, current->pid, info.f_pos);
	umsdos_delentry (dentry->d_parent, &info, 0);

out_dput:
Printk (("umsdos_create %.*s ret = %d pos %ld\n",
info.fake.len, info.fake.fname, ret, info.f_pos));
	/* N.B. any value in keeping short name dentries? */
	if (dentry != fake)
		d_drop(fake);
	dput(fake);

out_unlock:
	umsdos_unlockcreate (dir);
out:
	return ret;
}

/*
 * Initialise the new_entry from the old for a rename operation.
 * (Only useful for umsdos_rename_f() below).
 */
static void umsdos_ren_init (struct umsdos_info *new_info,
			     struct umsdos_info *old_info, int flags)
{
	/* != 0, this is the value of flags */
	new_info->entry.mode = old_info->entry.mode;
	new_info->entry.rdev = old_info->entry.rdev;
	new_info->entry.uid = old_info->entry.uid;
	new_info->entry.gid = old_info->entry.gid;
	new_info->entry.ctime = old_info->entry.ctime;
	new_info->entry.atime = old_info->entry.atime;
	new_info->entry.mtime = old_info->entry.mtime;
	new_info->entry.flags = flags ? flags : old_info->entry.flags;
	new_info->entry.nlink = old_info->entry.nlink;
}

#define chkstk() \
if (STACK_MAGIC != *(unsigned long *)current->kernel_stack_page){\
    printk(KERN_ALERT "UMSDOS: %s magic %x != %lx ligne %d\n" \
	   , current->comm,STACK_MAGIC \
	   ,*(unsigned long *)current->kernel_stack_page \
	   ,__LINE__); \
}

#undef chkstk
#define chkstk() do { } while (0);

/*
 * Rename a file (move) in the file system.
 */
 
static int umsdos_rename_f (struct inode *old_dir, struct dentry *old_dentry,
			    struct inode *new_dir, struct dentry *new_dentry,
			    int flags)
{
	int old_ret, new_ret;
	struct dentry *old, *new, *dret;
	struct inode *oldid = NULL;
	int ret = -EPERM;
	struct umsdos_info old_info;
	struct umsdos_info new_info;

	old_ret = umsdos_parse (old_dentry->d_name.name,
				old_dentry->d_name.len, &old_info);
	if (old_ret)
		goto out;
	new_ret = umsdos_parse (new_dentry->d_name.name,
				new_dentry->d_name.len, &new_info);
	if (new_ret)
		goto out;

	check_dentry_path (old_dentry, "umsdos_rename_f OLD");
	check_dentry_path (new_dentry, "umsdos_rename_f OLD");

	chkstk ();
Printk (("umsdos_rename %d %d ", old_ret, new_ret));
	umsdos_lockcreate2 (old_dir, new_dir);
	chkstk ();

	ret = umsdos_findentry(old_dentry->d_parent, &old_info, 0);
	chkstk ();
	if (ret) {
Printk (("ret %d ", ret));
		goto out_unlock;
	}

	/* check sticky bit on old_dir */
	ret = -EPERM;
	if (is_sticky(old_dir, old_info.entry.uid)) {
		Printk (("sticky set on old "));
		goto out_unlock;
	}

	/* Does new_name already exist? */
	new_ret = umsdos_findentry(new_dentry->d_parent, &new_info, 0);
	/* if destination file exists, are we allowed to replace it ? */
	if (new_ret == 0 && is_sticky(new_dir, new_info.entry.uid)) {
		Printk (("sticky set on new "));
		goto out_unlock;
	}

	Printk (("new newentry "));
	umsdos_ren_init (&new_info, &old_info, flags);
	ret = umsdos_newentry (new_dentry->d_parent, &new_info);
	chkstk ();
	if (ret) {
Printk (("ret %d %d ", ret, new_info.fake.len));
		goto out_unlock;
	}

	dret = umsdos_lookup_dentry(old_dentry->d_parent, old_info.fake.fname, 
					old_info.fake.len);
	ret = PTR_ERR(dret);
	if (IS_ERR(dret))
		goto out_unlock;
#if 0
	/* This is the same as dret */
	oldid = dret->d_inode;
	old = creat_dentry (old_info.fake.fname, old_info.fake.len,
				oldid, old_dentry->d_parent);
#endif
	old = dret;
	new = umsdos_lookup_dentry(new_dentry->d_parent, new_info.fake.fname, 
					new_info.fake.len);
	ret = PTR_ERR(new);
	if (IS_ERR(new))
		goto out_dput;

	Printk (("msdos_rename "));
	check_dentry_path (old, "umsdos_rename_f OLD2");
	check_dentry_path (new, "umsdos_rename_f NEW2");
	ret = msdos_rename (old_dir, old, new_dir, new);
	chkstk ();
printk("after m_rename ret %d ", ret);
	/* dput(old); */
	dput(new);

	if (ret != 0) {
		umsdos_delentry (new_dentry->d_parent, &new_info,
				 S_ISDIR (new_info.entry.mode));
		chkstk ();
		goto out_dput;
	}

	ret = umsdos_delentry (old_dentry->d_parent, &old_info,
				 S_ISDIR (old_info.entry.mode));
	chkstk ();
	if (ret)
		goto out_dput;
#if 0
	/*
	 * Update f_pos so notify_change will succeed
	 * if the file was already in use.
	 */
	umsdos_set_dirinfo (new_dentry->d_inode, new_dir, new_info.f_pos);
#endif
	if (old_dentry == dret) {
printk("umsdos_rename_f: old dentries match -- skipping d_move\n");
		goto out_dput;
	}
	d_move (old_dentry, new_dentry);

out_dput:
	dput(dret);

out_unlock:
	Printk ((KERN_DEBUG "umsdos_rename_f: unlocking dirs...\n"));
	umsdos_unlockcreate (old_dir);
	umsdos_unlockcreate (new_dir);

out:
	check_dentry_path (old_dentry, "umsdos_rename_f OLD3");
	check_dentry_path (new_dentry, "umsdos_rename_f NEW3");
	Printk ((" _ret=%d\n", ret));
	return ret;
}

/*
 * Setup a Symbolic link or a (pseudo) hard link
 * Return a negative error code or 0 if OK.
 */
/* #Specification: symbolic links / strategy
 * A symbolic link is simply a file which hold a path. It is
 * implemented as a normal MSDOS file (not very space efficient :-()
 * 
 * I see 2 different way to do it. One is to place the link data
 * in unused entry of the EMD file. The other is to have a separate
 * file dedicated to hold all symbolic links data.
 * 
 * Let's go for simplicity...
 */

static int umsdos_symlink_x (struct inode *dir, struct dentry *dentry,
			const char *symname, int mode, char flags)
{
	int ret, len;
	struct file filp;

	ret = umsdos_create_any (dir, dentry, mode, 0, flags);
	if (ret) {
Printk (("umsdos_symlink ret %d ", ret));
		goto out;
	}

	len = strlen (symname);

	fill_new_filp (&filp, dentry);
	filp.f_pos = 0;

	/* Make the inode acceptable to MSDOS FIXME */
Printk ((KERN_WARNING "   symname=%s ; dentry name=%.*s (ino=%lu)\n",
symname, (int) dentry->d_name.len, dentry->d_name.name, dentry->d_inode->i_ino));
	ret = umsdos_file_write_kmem_real (&filp, symname, len);
	
	if (ret >= 0) {
		if (ret != len) {
			ret = -EIO;
			printk ("UMSDOS: "
			     "Can't write symbolic link data\n");
		} else {
			ret = 0;
		}
	}
	if (ret != 0) {
		UMSDOS_unlink (dir, dentry);
	}

out:
	Printk (("\n"));
	return ret;
}

/*
 * Setup a Symbolic link.
 * Return a negative error code or 0 if OK.
 */
int UMSDOS_symlink ( struct inode *dir, struct dentry *dentry,
		 const char *symname)
{
	return umsdos_symlink_x (dir, dentry, symname, S_IFLNK | 0777, 0);
}

/*
 * Add a link to an inode in a directory
 */
int UMSDOS_link (struct dentry *olddentry, struct inode *dir,
		 struct dentry *dentry)
{
	struct inode *oldinode = olddentry->d_inode;
	struct inode *olddir;
	char *path;
	struct dentry *temp;
	unsigned long buffer;
	int ret;
	struct umsdos_dirent entry;

	ret = -EPERM;
	if (S_ISDIR (oldinode->i_mode))
		goto out;

	ret = umsdos_nevercreat (dir, dentry, -EPERM);
	if (ret)
		goto out;

	ret = -ENOMEM;
	buffer = get_free_page(GFP_KERNEL);
	if (!buffer)
		goto out;

	olddir = olddentry->d_parent->d_inode;
	umsdos_lockcreate2 (dir, olddir);

	/* get the entry for the old name */
	ret = umsdos_dentry_to_entry(olddentry, &entry);
	if (ret)
		goto out_unlock;
Printk (("umsdos_link :%.*s: ino %lu flags %d ",
entry.name_len, entry.name ,oldinode->i_ino, entry.flags));

	if (!(entry.flags & UMSDOS_HIDDEN)) {
		struct umsdos_info info;

		ret = umsdos_newhidden (olddentry->d_parent, &info);
		if (ret)
			goto out_unlock;

		ret = umsdos_rename_f (olddentry->d_inode, olddentry, 
					dir, dentry, UMSDOS_HIDDEN);
		if (ret)
			goto out_unlock;
		path = d_path(olddentry, (char *) buffer, PAGE_SIZE);
		if (!path)
			goto out_unlock;
		temp = umsdos_lookup_dentry(olddentry->d_parent, entry.name,
						entry.name_len); 
		if (IS_ERR(temp))
			goto out_unlock;
		ret = umsdos_symlink_x (olddir, temp, path, 
					S_IFREG | 0777, UMSDOS_HLINK);
		if (ret == 0) {
			ret = umsdos_symlink_x (dir, dentry, path,
					S_IFREG | 0777, UMSDOS_HLINK);
		}
		dput(temp);
		goto out_unlock;
	}
	path = d_path(olddentry, (char *) buffer, PAGE_SIZE);
	if (path) {
		ret = umsdos_symlink_x (dir, dentry, path, 
				S_IFREG | 0777, UMSDOS_HLINK);
	}

out_unlock:
	umsdos_unlockcreate (olddir);
	umsdos_unlockcreate (dir);
	free_page(buffer);
out:
	if (ret == 0) {
		struct iattr newattrs;

		oldinode->i_nlink++;
		newattrs.ia_valid = 0;
		ret = UMSDOS_notify_change (olddentry, &newattrs);
	}
	Printk (("umsdos_link %d\n", ret));
	return ret;
}


/*
 * Add a new file into the alternate directory.
 * The file is added to the real MSDOS directory. If successful, it
 * is then added to the EMD file.
 * 
 * Return the status of the operation. 0 mean success.
 */
int UMSDOS_create (struct inode *dir, struct dentry *dentry, int mode)
{
	int ret;
	Printk ((KERN_ERR "UMSDOS_create: entering\n"));
	check_dentry_path (dentry, "UMSDOS_create START");
	ret = umsdos_create_any (dir, dentry, mode, 0, 0);
	check_dentry_path (dentry, "UMSDOS_create END");
	return ret;
}


/*
 * Add a sub-directory in a directory
 */
/* #Specification: mkdir / Directory already exist in DOS
 * We do the same thing as for file creation.
 * For all user it is an error.
 */
/* #Specification: mkdir / umsdos directory / create EMD
 * When we created a new sub-directory in a UMSDOS
 * directory (one with full UMSDOS semantics), we
 * create immediately an EMD file in the new
 * sub-directory so it inherits UMSDOS semantics.
 */
int UMSDOS_mkdir (struct inode *dir, struct dentry *dentry, int mode)
{
	struct dentry *temp;
	struct inode *inode;
	int ret, err;
	struct umsdos_info info;

	ret = umsdos_nevercreat (dir, dentry, -EEXIST);
	if (ret)
		goto out;

	ret = umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);
	if (ret) {
Printk (("umsdos_mkdir %d\n", ret));
		goto out;
	}

	umsdos_lockcreate (dir);
	info.entry.mode = mode | S_IFDIR;
	info.entry.rdev = 0;
	info.entry.uid = current->fsuid;
	info.entry.gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
	info.entry.ctime = info.entry.atime = info.entry.mtime = CURRENT_TIME;
	info.entry.flags = 0;
	info.entry.nlink = 1;
	ret = umsdos_newentry (dentry->d_parent, &info);
	if (ret) {
Printk (("newentry %d ", ret));
		goto out_unlock;
	}

	/* lookup the short name dentry */
	temp = umsdos_lookup_dentry(dentry->d_parent, info.fake.fname, 
					info.fake.len);
	ret = PTR_ERR(temp);
	if (IS_ERR(temp))
		goto out_unlock;

	/* Make sure the short name doesn't exist */
	ret = -EEXIST;
	if (temp->d_inode) {
printk("umsdos_mkdir: short name %s/%s exists\n",
dentry->d_parent->d_name.name, info.fake.fname);
		goto out_remove;
	}

	ret = msdos_mkdir (dir, temp, mode);
	if (ret)
		goto out_remove;

	inode = temp->d_inode;
	umsdos_lookup_patch_new(temp, &info.entry, info.f_pos);

	/*
	 * Note! The long and short name might be the same,
	 * so check first before doing the instantiate ...
	 */
	if (dentry != temp) {
		if (!dentry->d_inode) {
			inode->i_count++;
			d_instantiate(dentry, inode);
		} else {
			printk("umsdos_mkdir: not negative??\n");
		}
	} else {
		printk("umsdos_mkdir: dentries match, skipping inst\n");
	}

	/* create the EMD file */
	err = umsdos_make_emd(dentry);

	/* 
	 * set up the dir so it is promoted to EMD,
	 * with the EMD file invisible inside it.
	 */
	umsdos_setup_dir(temp);
	goto out_dput;

out_remove:
	umsdos_delentry (dentry->d_parent, &info, 1);

out_dput:
	/* kill off the short name dentry */ 
	if (temp != dentry)
		d_drop(temp);
	dput(temp);

out_unlock:
	umsdos_unlockcreate (dir);
	Printk (("umsdos_mkdir %d\n", ret));
out:
	return ret;
}

/*
 * Add a new device special file into a directory.
 *
 * #Specification: Special files / strategy
 * Device special file, pipes, etc ... are created like normal
 * file in the msdos file system. Of course they remain empty.
 * 
 * One strategy was to create those files only in the EMD file
 * since they were not important for MSDOS. The problem with
 * that, is that there were not getting inode number allocated.
 * The MSDOS filesystems is playing a nice game to fake inode
 * number, so why not use it.
 * 
 * The absence of inode number compatible with those allocated
 * for ordinary files was causing major trouble with hard link
 * in particular and other parts of the kernel I guess.
 */
int UMSDOS_mknod (struct inode *dir, struct dentry *dentry,
		 int mode, int rdev)
{
	int ret;
	check_dentry_path (dentry, "UMSDOS_mknod START");
	ret = umsdos_create_any (dir, dentry, mode, rdev, 0);
	check_dentry_path (dentry, "UMSDOS_mknod END");
	return ret;
}

/*
 * Remove a sub-directory.
 */
int UMSDOS_rmdir (struct inode *dir, struct dentry *dentry)
{
	struct dentry *temp;
	int ret, err, empty;
	struct umsdos_info info;

	ret = umsdos_nevercreat (dir, dentry, -EPERM);
	if (ret)
		goto out;

#if 0	/* no need for lookup ... we have a dentry ... */
	ret = umsdos_lookup_x (dir, dentry, 0);
	Printk (("rmdir lookup %d ", ret));
	if (ret != 0)
		goto out;
#endif

	umsdos_lockcreate (dir);
	ret = -EBUSY;
	if (dentry->d_count > 1) {
		shrink_dcache_parent(dentry);
		if (dentry->d_count > 1) {
printk("umsdos_rmdir: %s/%s busy\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
			goto out_unlock;
		}
	}

	/* check whether the EMD is empty */
	empty = umsdos_isempty (dentry);
	ret = -ENOTEMPTY;
	if (empty == 0)
		goto out_unlock;

	/* Have to remove the EMD file? */
	if (empty == 1) {
		struct dentry *demd;
		/* check sticky bit */
		ret = -EPERM;
		if (is_sticky(dir, dentry->d_inode->i_uid)) {
printk("umsdos_rmdir: %s/%s is sticky\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
			goto out_unlock;
		}

		ret = -ENOTEMPTY;
		/* see if there's an EMD file ... */
		demd = umsdos_get_emd_dentry(dentry);
		if (IS_ERR(demd))
			goto out_unlock;
printk("umsdos_rmdir: got EMD dentry %s/%s, inode=%p\n",
demd->d_parent->d_name.name, demd->d_name.name, demd->d_inode);

		err = msdos_unlink (dentry->d_inode, demd);
Printk (("UMSDOS_rmdir: unlinking empty EMD err=%d", err));
		dput(demd);
		if (err)
			goto out_unlock;
	}

	umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);
	/* Call findentry to complete the mangling */
	umsdos_findentry (dentry->d_parent, &info, 2);
	temp = umsdos_lookup_dentry(dentry->d_parent, info.fake.fname, 
					info.fake.len);
	ret = PTR_ERR(temp);
	if (IS_ERR(temp))
		goto out_unlock;
	/*
	 * If the short name matches the dentry, dput() it now.
	 */
	if (temp == dentry) {
		dput(temp);
printk("umsdos_rmdir: %s/%s, short matches long\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
	}

	/*
	 * Attempt to remove the msdos name.
	 */
	ret = msdos_rmdir (dir, temp);
	if (ret && ret != -ENOENT)
		goto out_dput;

	/* OK so far ... remove the name from the EMD */
	ret = umsdos_delentry (dentry->d_parent, &info, 1);

out_dput:
	/* dput() temp if we didn't do it above */
	if (temp != dentry) {
		d_drop(temp);
		dput(temp);
		if (!ret)
			d_delete (dentry);
printk("umsdos_rmdir: %s/%s, short=%s dput\n",
dentry->d_parent->d_name.name, dentry->d_name.name, info.fake.fname);
	}

out_unlock:
	umsdos_unlockcreate (dir);

out:
	Printk (("umsdos_rmdir %d\n", ret));
	return ret;
}


/*
 * Remove a file from the directory.
 *
 * #Specification: hard link / deleting a link
 * When we delete a file, and this file is a link
 * we must subtract 1 to the nlink field of the
 * hidden link.
 * 
 * If the count goes to 0, we delete this hidden
 * link too.
 */
int UMSDOS_unlink (struct inode *dir, struct dentry *dentry)
{
	struct dentry *temp;
	struct inode *inode;
	int ret;
	struct umsdos_info info;

Printk (("UMSDOS_unlink: entering %s/%s\n",
dentry->d_parent->d_name.name, dentry->d_name.name));

	ret = umsdos_nevercreat (dir, dentry, -EPERM);
	if (ret)
		goto out;

	ret = umsdos_parse (dentry->d_name.name, dentry->d_name.len, &info);
	if (ret)
		goto out;

	umsdos_lockcreate (dir);
	ret = umsdos_findentry (dentry->d_parent, &info, 1);
	if (ret) {
printk("UMSDOS_unlink: findentry returned %d\n", ret);
		goto out_unlock;
	}

Printk (("UMSDOS_unlink %.*s ", info.fake.len, info.fake.fname));
	ret = -EPERM;
	/* check sticky bit */
	if (is_sticky(dir, info.entry.uid)) {
printk("umsdos_unlink: %s/%s is sticky\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
		goto out_unlock;
	}

	ret = 0;
	if (info.entry.flags & UMSDOS_HLINK) {
printk("UMSDOS_unlink: hard link %s/%s, fake=%s\n",
dentry->d_parent->d_name.name, dentry->d_name.name, info.fake.fname);
		/*
		 * First, get the inode of the hidden link
		 * using the standard lookup function.
		 */

		ret = umsdos_lookup_x (dir, dentry, 0);
		inode = dentry->d_inode;
		if (ret)
			goto out_unlock;

		Printk (("unlink nlink = %d ", inode->i_nlink));
		inode->i_nlink--;
		if (inode->i_nlink == 0) {
			struct umsdos_dirent entry;

			ret = umsdos_dentry_to_entry (dentry, &entry);
			if (ret == 0) {
				ret = UMSDOS_unlink (dentry->d_parent->d_inode,
							dentry);
			}
		} else {
			struct iattr newattrs;
			newattrs.ia_valid = 0;
			ret = UMSDOS_notify_change (dentry, &newattrs);
		}
	}
	if (ret)
		goto out_unlock;

	/* get the short name dentry */
	temp = umsdos_lookup_dentry(dentry->d_parent, info.fake.fname, 
					info.fake.len);
	if (IS_ERR(temp))
		goto out_unlock;

	/*
	 * If the short name matches the long,
	 * dput() it now so it's not busy.
	 */
	if (temp == dentry) {
printk("UMSDOS_unlink: %s/%s, short matches long\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
		dput(temp);
	}

	ret = umsdos_delentry (dentry->d_parent, &info, 0);
	if (ret && ret != -ENOENT)
		goto out_dput;

printk("UMSDOS: Before msdos_unlink %.*s ",
info.fake.len, info.fake.fname);
	ret = msdos_unlink_umsdos (dir, temp);

Printk (("msdos_unlink %.*s %o ret %d ",
info.fake.len, info.fake.fname ,info.entry.mode, ret));

	/* dput() temp if we didn't do it above */
out_dput:
	if (temp != dentry) {
		d_drop(temp);
		dput(temp);
		if (!ret)
			d_delete (dentry);
printk("umsdos_unlink: %s/%s, short=%s dput\n",
dentry->d_parent->d_name.name, dentry->d_name.name, info.fake.fname);
	}

out_unlock:
	umsdos_unlockcreate (dir);
out:
	Printk (("umsdos_unlink %d\n", ret));
	return ret;
}


/*
 * Rename (move) a file.
 */
int UMSDOS_rename (struct inode *old_dir, struct dentry *old_dentry,
		   struct inode *new_dir, struct dentry *new_dentry)
{
	int ret;

	ret = umsdos_nevercreat (new_dir, new_dentry, -EEXIST);
	if (ret)
		goto out;

	ret = umsdos_rename_f(old_dir, old_dentry, new_dir, new_dentry, 0);
	if (ret != -EEXIST)
		goto out;

	/* This is not terribly efficient but should work. */
	ret = UMSDOS_unlink (new_dir, new_dentry);
	chkstk ();
	Printk (("rename unlink ret %d -- ", ret));
	if (ret == -EISDIR) {
		ret = UMSDOS_rmdir (new_dir, new_dentry);
		chkstk ();
		Printk (("rename rmdir ret %d -- ", ret));
	}
	if (ret)
		goto out;

	/* this time the rename should work ... */
	ret = umsdos_rename_f (old_dir, old_dentry, new_dir, new_dentry, 0);

out:
	return ret;
}
