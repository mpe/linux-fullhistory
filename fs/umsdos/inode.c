/*
 *  linux/fs/umsdos/inode.c
 *
 *      Written 1993 by Jacques Gelinas
 *      Inspired from linux/fs/msdos/... by Werner Almesberger
 *
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/umsdos_fs.h>
#include <linux/list.h>

extern struct inode_operations umsdos_rdir_inode_operations;


struct inode *pseudo_root = NULL;	/* Useful to simulate the pseudo DOS */

				/* directory. See UMSDOS_readdir_x() */

/* #Specification: convention / PRINTK Printk and printk
 * Here is the convention for the use of printk inside fs/umsdos
 * 
 * printk carry important message (error or status).
 * Printk is for debugging (it is a macro defined at the beginning of
 * most source.
 * PRINTK is a nulled Printk macro.
 * 
 * This convention makes the source easier to read, and Printk easier
 * to shut off.
 */
#define PRINTK(x)
#define Printk(x) printk x




/*
 * makes return inode->i_dentry
 *
 */

inline struct dentry *geti_dentry (struct inode *inode)
{
	struct dentry *ret;
	if (!inode) {
		printk (KERN_ERR "geti_dentry: inode is NULL!\n");
		return NULL;
	}
	ret = list_entry (inode->i_dentry.next, struct dentry, d_alias); /* FIXME: does this really work ? :) */
	Printk ((KERN_DEBUG "geti_dentry : inode %lu: i_dentry is %p\n", inode->i_ino, ret));
	return ret;
}

/*
 * makes inode->i_count++
 *
 */

inline void inc_count (struct inode *inode)
{
	inode->i_count++;
	Printk ((KERN_DEBUG "inc_count: inode %lu incremented count to %d\n", inode->i_ino, inode->i_count));
}


/*
 * makes empty filp
 *
 */

void fill_new_filp (struct file *filp, struct dentry *dentry)
{
	Printk (("/mn/ fill_new_filp: filling empty filp at %p\n", filp));
	if (dentry)
		Printk (("     dentry=%.*s\n", (int) dentry->d_name.len, dentry->d_name.name));
	else
		Printk (("     dentry is NULL ! you must fill it later...\n"));

	memset (filp, 0, sizeof (struct file));

	filp->f_pos = 0;
	filp->f_reada = 1;
	filp->f_flags = O_RDWR;
	filp->f_dentry = dentry;
	filp->f_op = &umsdos_file_operations;	/* /mn/ - we have to fill it with SOMETHING */
}

/*
 * check a superblock
 */

void check_sb (struct super_block *sb, const char c)
{
	if (sb) {
		Printk ((" (has %c_sb=%d, %d)", c, MAJOR (sb->s_dev), MINOR (sb->s_dev)));
	} else {
		Printk ((" (%c_sb is NULL)", c));
	}
} 

/*
 * check an inode
 */

void check_inode (struct inode *inode)
{
	if (inode) {
		Printk ((KERN_DEBUG "*   inode is %lu (i_count=%d)", inode->i_ino, inode->i_count));
		check_sb (inode->i_sb, 'i');
		
		if (inode->i_dentry.next) {	/* FIXME: does this work ? */
			Printk ((" (has i_dentry)"));
		} else {
			Printk ((" (NO i_dentry)"));
		}
		
		if (inode->i_op == NULL) {
			Printk ((" (i_op is NULL)\n"));
		} else if (inode->i_op == &umsdos_dir_inode_operations) {
			Printk ((" (i_op is umsdos_dir_inode_operations)\n"));
		} else if (inode->i_op == &umsdos_file_inode_operations) {
			Printk ((" (i_op is umsdos_file_inode_operations)\n"));
		} else if (inode->i_op == &umsdos_file_inode_operations_no_bmap) {
			Printk ((" (i_op is umsdos_file_inode_operations_no_bmap)\n"));
		} else if (inode->i_op == &umsdos_file_inode_operations_readpage) {
			Printk ((" (i_op is umsdos_file_inode_operations_readpage)\n"));
		} else if (inode->i_op == &umsdos_rdir_inode_operations) {
			Printk ((" (i_op is umsdos_rdir_inode_operations)\n"));
		} else if (inode->i_op == &umsdos_symlink_inode_operations) {
			Printk ((" (i_op is umsdos_symlink_inode_operations)\n"));
		} else {
			Printk ((" (i_op is UNKNOWN: %p)\n", inode->i_op));
		}
	} else {
		Printk ((KERN_DEBUG "*   inode is NULL\n"));
	}
}


/*
 * internal part of check_dentry. does the real job.
 *
 */

void check_dent_int (struct dentry *dentry, int parent)
{
	if (parent) {
		Printk ((KERN_DEBUG "*  parent dentry: %.*s\n", (int) dentry->d_name.len, dentry->d_name.name));
	} else {
		Printk ((KERN_DEBUG "\n*** checking dentry: %.*s\n", (int) dentry->d_name.len, dentry->d_name.name));
	}
	check_inode (dentry->d_inode);
	Printk ((KERN_DEBUG "*   d_count=%d", dentry->d_count));
	check_sb (dentry->d_sb, 'd');
	if (dentry->d_op == NULL) {
		Printk ((" (d_op is NULL)\n"));
	} else {
		Printk ((" (d_op is UNKNOWN: %p)\n", dentry->d_op));
	}
}

/*
 * checks dentry and prints info
 *
 */

void check_dentry (struct dentry *dentry)
{
	if (!dentry) {
		Printk ((KERN_DEBUG "\n*** checking dentry... it is NULL !\n"));
		return;
	}
	check_dent_int (dentry, 0);

	if (dentry->d_parent) {
		check_dent_int (dentry->d_parent, 1);
	} else {
		Printk ((KERN_DEBUG "*   has no parent.\n"));
	}

	Printk ((KERN_DEBUG "*** end checking dentry\n"));
}


/*
 * makes dentry. for name name with length len. /mn/
 * if inode is not NULL, puts it also.
 *
 */

struct dentry *creat_dentry (const char *name, const int len, struct inode *inode, struct dentry *parent)
{
/* FIXME /mn/: parent is not passed many times... if it is not, dentry should be destroyed before someone else gets to use it */

	struct dentry *ret;
	struct qstr qname;

	if (inode)
		Printk ((KERN_DEBUG "/mn/ creat_dentry: creating dentry with inode=%lu for %.*s\n", inode->i_ino, len, name));
	else
		Printk ((KERN_DEBUG "/mn/ creat_dentry: creating empty dentry for %.*s\n", len, name));

	qname.name = name;
	qname.len = len;
	qname.hash = 0;

	ret = d_alloc (parent, &qname);		/* create new dentry */
	ret->d_inode = NULL;

	if (!parent) {
		ret->d_parent = ret;
		Printk ((KERN_WARNING "creat_dentry: WARNING: NO parent! faking! beware !\n"));
	}


	if (inode) {
		ret->d_sb = inode->i_sb;	/* try to fill it in if avalaible. If avalaible in parent->d_sb, d_alloc will add it automatically */
		d_add (ret, inode);
	}

	return ret;
}


/*
 * removes temporary dentry created by creat_dentry
 *
 */

void kill_dentry (struct dentry *dentry)
{
	if (dentry) {
		Printk (("/mn/ kill_dentry: kill_dentry %.*s :", (int) dentry->d_name.len, dentry->d_name.name));
		if (dentry->d_inode)
			Printk (("inode=%lu (i_count=%d)\n", dentry->d_inode->i_ino, dentry->d_inode->i_count));
		else
			Printk (("inode is NULL\n"));

		/* FIXME: is this ok ?! /mn/ */
		/* d_drop (dentry); */
		/* d_invalidate (dentry); */
		/*dput (dentry); */
		/*d_delete (dentry) */
	} else {
		Printk (("/mn/ kill_dentry: dentry is NULL ?!\n"));
	}


	Printk ((KERN_DEBUG "/mn/ kill_dentry: exiting...\n"));
	return;
}






void UMSDOS_put_inode (struct inode *inode)
{
	PRINTK ((KERN_DEBUG "put inode %p (%lu) owner %lu pos %lu dir %lu count=%d\n", inode, inode->i_ino
		 ,inode->u.umsdos_i.i_emd_owner, inode->u.umsdos_i.pos
		 ,inode->u.umsdos_i.i_emd_dir, inode->i_count));

	if (inode && pseudo_root && inode == pseudo_root) {
		printk (KERN_ERR "Umsdos: Oops releasing pseudo_root. Notify jacques@solucorp.qc.ca\n");
	}

	fat_put_inode (inode);
}


void UMSDOS_put_super (struct super_block *sb)
{
	Printk ((KERN_DEBUG "UMSDOS_put_super: entering\n"));
	msdos_put_super (sb);
	MOD_DEC_USE_COUNT;
}



/*
 * Call msdos_lookup, but set back the original msdos function table.
 * Return 0 if ok, or a negative error code if not.
 */
int umsdos_real_lookup (
			       struct inode *dir,
			       struct dentry *dentry
)
{				/* Will hold inode of the file, if successful */
	int ret;

	PRINTK ((KERN_DEBUG "umsdos_real_lookup: looking for %s /", dentry->d_name.name));
	inc_count (dir);	/* should be here ? Causes all kind of missing iput()'s all around, but panics w/o it /mn/ */
	ret = msdos_lookup (dir, dentry);
	PRINTK (("/ returned %d\n", ret));

	return ret;
}

/*
 * Complete the setup of an directory inode.
 * First, it completes the function pointers, then
 * it locates the EMD file. If the EMD is there, then plug the
 * umsdos function table. If not, use the msdos one.
 */
void umsdos_setup_dir_inode (struct inode *inode)
{
	inode->u.umsdos_i.i_emd_dir = 0;
	{
		struct inode *emd_dir;

		emd_dir = umsdos_emd_dir_lookup (inode, 0);
		Printk ((KERN_DEBUG "umsdos_setup_dir_inode: umsdos_emd_dir_lookup for inode=%p returned %p\n", inode, emd_dir));

		if (emd_dir == NULL) {
			Printk ((KERN_DEBUG "umsdos_setup_dir_inode /mn/: Setting up rdir_inode_ops --> eg. NOT using EMD.\n"));
			inode->i_op = &umsdos_rdir_inode_operations;
		} else {
			Printk ((KERN_DEBUG "umsdos_setup_dir_inode /mn/: Setting up dir_inode_ops --> eg. using EMD.\n"));
			inode->i_op = &umsdos_dir_inode_operations;
		}

		iput (emd_dir);	/* FIXME? OK? */
	}
}


/*
 * Add some info into an inode so it can find its owner quickly
 */
void umsdos_set_dirinfo (
				struct inode *inode,
				struct inode *dir,
				off_t f_pos)
{
	struct inode *emd_owner;

	/* FIXME, I don't have a clue on this one - /mn/ hmmm ? ok ? */
/*    Printk ((KERN_WARNING "umsdos_set_dirinfo: /mn/ FIXME: no clue. inode=%lu dir=%lu\n", inode->i_ino, dir->i_ino)); */
	emd_owner = umsdos_emd_dir_lookup (dir, 1);
	Printk (("umsdos_set_dirinfo: emd_owner is %lu for dir %lu\n", emd_owner->i_ino, dir->i_ino));
	inode->u.umsdos_i.i_dir_owner = dir->i_ino;
	inode->u.umsdos_i.i_emd_owner = emd_owner->i_ino;
	iput (emd_owner); /* FIXME? */
	inode->u.umsdos_i.pos = f_pos;
}


/*
 * Tells if an Umsdos inode has been "patched" once.
 * Return != 0 if so.
 */
int umsdos_isinit (struct inode *inode)
{
#if	1
	return inode->u.umsdos_i.i_emd_owner != 0;
#elif 0
	return inode->i_atime != 0;
#else
	return atomic_read (&inode->i_count) > 1;
#endif
}


/*
 * Connect the proper tables in the inode and add some info.
 */
void umsdos_patch_inode (
				struct inode *inode,
				struct inode *dir,	/* May be NULL */
				off_t f_pos)
{
	/*
	 * This function is called very early to setup the inode, somewhat
	 * too early (called by UMSDOS_read_inode). At this point, we can't
	 * do to much, such as lookup up EMD files and so on. This causes
	 * confusion in the kernel. This is why some initialisation
	 * will be done when dir != NULL only.
	 * 
	 * UMSDOS do run piggy back on top of msdos fs. It looks like something
	 * is missing in the VFS to accommodate stacked fs. Still unclear what
	 * (quite honestly).
	 * 
	 * Well, maybe one! A new entry "may_unmount" which would allow
	 * the stacked fs to allocate some inode permanently and release
	 * them at the end. Doing that now introduce a problem. unmount
	 * always fail because some inodes are in use.
	 */

	Printk ((KERN_DEBUG "Entering umsdos_patch_inode for inode=%lu\n", inode->i_ino));

	if (!umsdos_isinit (inode)) {
		inode->u.umsdos_i.i_emd_dir = 0;
		if (S_ISREG (inode->i_mode)) {
			if (MSDOS_SB (inode->i_sb)->cvf_format) {
				if (MSDOS_SB (inode->i_sb)->cvf_format->flags & CVF_USE_READPAGE) {
					Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: seting i_op = umsdos_file_inode_operations_readpage\n"));
					inode->i_op = &umsdos_file_inode_operations_readpage;
				} else {
					Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: seting i_op = umsdos_file_inode_operations_no_bmap\n"));
					inode->i_op = &umsdos_file_inode_operations_no_bmap;
				}
			} else {
				if (inode->i_op->bmap != NULL) {
					Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: seting i_op = umsdos_file_inode_operations\n"));
					inode->i_op = &umsdos_file_inode_operations;
				} else {
					Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: seting i_op = umsdos_file_inode_operations_no_bmap\n"));
					inode->i_op = &umsdos_file_inode_operations_no_bmap;
				}
			}
		} else if (S_ISDIR (inode->i_mode)) {
			if (dir != NULL) {
				umsdos_setup_dir_inode (inode);
			}
		} else if (S_ISLNK (inode->i_mode)) {
			Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: seting i_op = umsdos_symlink_inode_operations\n"));
			inode->i_op = &umsdos_symlink_inode_operations;
		} else if (S_ISCHR (inode->i_mode)) {
			Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: seting i_op = chrdev_inode_operations\n"));
			inode->i_op = &chrdev_inode_operations;
		} else if (S_ISBLK (inode->i_mode)) {
			Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: seting i_op = blkdev_inode_operations\n"));
			inode->i_op = &blkdev_inode_operations;
		} else if (S_ISFIFO (inode->i_mode)) {
			Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: uhm, init_fifo\n"));
			init_fifo (inode);
		}
		if (dir != NULL) {
			/* #Specification: inode / umsdos info
			 * The first time an inode is seen (inode->i_count == 1),
			 * the inode number of the EMD file which control this inode
			 * is tagged to this inode. It allows operation such
			 * as notify_change to be handled.
			 */
			/*
			 * This is done last because it also control the
			 * status of umsdos_isinit()
			 */
			Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: here we go: calling umsdos_set_dirinfo (%p,%p,%lu)\n", inode, dir, f_pos));
			umsdos_set_dirinfo (inode, dir, f_pos);
		}
	} else if (dir != NULL) {
		/*
		 * Test to see if the info is maintained.
		 * This should be removed when the file system will be proven.
		 */
		/* FIXME, again, not a clue */
		struct inode *emd_owner;

		Printk ((KERN_WARNING "umsdos_patch_inode: /mn/ Warning: untested emd_owner thingy...\n"));
		emd_owner = umsdos_emd_dir_lookup (dir, 1);
		iput (emd_owner); /* FIXME? */
		if (emd_owner->i_ino != inode->u.umsdos_i.i_emd_owner) {
			printk ("UMSDOS: *** EMD_OWNER ??? *** ino = %ld %ld <> %ld "
				,inode->i_ino, emd_owner->i_ino, inode->u.umsdos_i.i_emd_owner);
		}
	}
}


/*
 * Get the inode of the directory which owns this inode.
 * Return 0 if ok, -EIO if error.
 */
int umsdos_get_dirowner (
				struct inode *inode,
				struct inode **result)
{				/* Hold NULL if any error */
	/* else, the inode of the directory */
	int ret = -EIO;
	unsigned long ino = inode->u.umsdos_i.i_dir_owner;

	*result = NULL;
	if (ino == 0) {
		printk ("UMSDOS: umsdos_get_dirowner ino == 0\n");
	} else {
		struct inode *dir = *result = iget (inode->i_sb, ino);

		if (dir != NULL) {
			umsdos_patch_inode (dir, NULL, 0);
			/* iput (dir);	/ * FIXME: /mn/ added this. Is it ok ? */
			ret = 0;
		}
	}
	return ret;
}



/*
 * Load an inode from disk.
 */
void UMSDOS_read_inode (struct inode *inode)
{
	Printk ((KERN_DEBUG "UMSDOS_read_inode %p ino = %lu ", inode, inode->i_ino));
	msdos_read_inode (inode);
	Printk (("ino after msdos_read_inode= %lu i_count=%d\n", inode->i_ino, inode->i_count));
	if (S_ISDIR (inode->i_mode)
	    && (inode->u.umsdos_i.u.dir_info.creating != 0
		|| inode->u.umsdos_i.u.dir_info.looking != 0
		|| waitqueue_active (&inode->u.umsdos_i.u.dir_info.p))) {
		Printk (("read inode %d %d %p\n"
			 ,inode->u.umsdos_i.u.dir_info.creating
			 ,inode->u.umsdos_i.u.dir_info.looking
			 ,inode->u.umsdos_i.u.dir_info.p));
	}
	/* #Specification: Inode / post initialisation
	 * To completely initialise an inode, we need access to the owner
	 * directory, so we can locate more info in the EMD file. This is
	 * not available the first time the inode is access, we use
	 * a value in the inode to tell if it has been finally initialised.
	 * 
	 * At first, we have tried testing i_count but it was causing
	 * problem. It is possible that two or more process use the
	 * newly accessed inode. While the first one block during
	 * the initialisation (probably while reading the EMD file), the
	 * others believe all is well because i_count > 1. They go banana
	 * with a broken inode. See umsdos_lookup_patch and umsdos_patch_inode.
	 */
	umsdos_patch_inode (inode, NULL, 0);
}


int internal_notify_change (struct inode *inode, struct iattr *attr)
{
	int ret = 0;
	struct inode *root;

	PRINTK ((KERN_DEBUG "UMSDOS_notify_change: entering\n"));

	if ((ret = inode_change_ok (inode, attr)) != 0)
		return ret;

	if (inode->i_nlink > 0) {
		/* #Specification: notify_change / i_nlink > 0
		 * notify change is only done for inode with nlink > 0. An inode
		 * with nlink == 0 is no longer associated with any entry in
		 * the EMD file, so there is nothing to update.
		 */
		unsigned long i_emd_owner = inode->u.umsdos_i.i_emd_owner;

		root = iget (inode->i_sb, UMSDOS_ROOT_INO);
		if (inode == root) {
			/* #Specification: root inode / attributes
			 * I don't know yet how this should work. Normally
			 * the attributes (permissions bits, owner, times) of
			 * a directory are stored in the EMD file of its parent.
			 * 
			 * One thing we could do is store the attributes of the root
			 * inode in its own EMD file. A simple entry named "." could
			 * be used for this special case. It would be read once
			 * when the file system is mounted and update in
			 * UMSDOS_notify_change() (right here).
			 * 
			 * I am not sure of the behavior of the root inode for
			 * a real UNIX file system. For now, this is a nop.
			 */
		} else if (i_emd_owner != 0xffffffff && i_emd_owner != 0) {
			/* This inode is not a EMD file nor an inode used internally
			 * by MSDOS, so we can update its status.
			 * See emd.c
			 */
			struct inode *emd_owner;

			emd_owner = iget (inode->i_sb, i_emd_owner);
			Printk (("notify change %p ", inode));
			if (emd_owner == NULL) {
				printk ("UMSDOS: emd_owner = NULL ???");
				ret = -EPERM;
			} else {
				struct file filp;
				struct umsdos_dirent entry;
				struct dentry *emd_dentry;
				loff_t offs;

				emd_dentry = creat_dentry ("notify_emd", 10, emd_owner, NULL);
				fill_new_filp (&filp, emd_dentry);

				filp.f_pos = inode->u.umsdos_i.pos;
				filp.f_reada = 0;
				offs = filp.f_pos;	/* FIXME: /mn/ is this ok ? */
				Printk (("pos = %Lu ", filp.f_pos));
				/* Read only the start of the entry since we don't touch */
				/* the name */
				ret = umsdos_emd_dir_read (emd_owner, &filp, (char *) &entry, UMSDOS_REC_SIZE, &offs);
				if (ret == 0) {
					if (attr->ia_valid & ATTR_UID)
						entry.uid = attr->ia_uid;
					if (attr->ia_valid & ATTR_GID)
						entry.gid = attr->ia_gid;
					if (attr->ia_valid & ATTR_MODE)
						entry.mode = attr->ia_mode;
					if (attr->ia_valid & ATTR_ATIME)
						entry.atime = attr->ia_atime;
					if (attr->ia_valid & ATTR_MTIME)
						entry.mtime = attr->ia_mtime;
					if (attr->ia_valid & ATTR_CTIME)
						entry.ctime = attr->ia_ctime;

					entry.nlink = inode->i_nlink;
					filp.f_pos = inode->u.umsdos_i.pos;
					offs = filp.f_pos;	/* FIXME: /mn/ is this ok ? */
					ret = umsdos_emd_dir_write (emd_owner, &filp, (char *) &entry, UMSDOS_REC_SIZE, &offs);

					Printk (("notify pos %lu ret %d nlink %d "
						 ,inode->u.umsdos_i.pos
						 ,ret, entry.nlink));
					/* #Specification: notify_change / msdos fs
					 * notify_change operation are done only on the
					 * EMD file. The msdos fs is not even called.
					 */
				}
				iput (emd_owner);	/* FIXME? /mn/ */
			}
			Printk (("\n"));
		}
		iput (root);	/* FIXME - /mn/ this is hopefully ok */
	}
	if (ret == 0)
		inode_setattr (inode, attr);

	return ret;
}


int UMSDOS_notify_change (struct dentry *dentry, struct iattr *attr)
{
	return internal_notify_change (dentry->d_inode, attr);
}

/*
 * Update the disk with the inode content
 */
void UMSDOS_write_inode (struct inode *inode)
{
	struct iattr newattrs;

	PRINTK (("UMSDOS_write_inode emd %d (FIXME: missing notify_change)\n", inode->u.umsdos_i.i_emd_owner));
	fat_write_inode (inode);
	newattrs.ia_mtime = inode->i_mtime;
	newattrs.ia_atime = inode->i_atime;
	newattrs.ia_ctime = inode->i_ctime;
	newattrs.ia_valid = ATTR_MTIME | ATTR_ATIME | ATTR_CTIME;
	/*
	 * UMSDOS_notify_change is convenient to call here
	 * to update the EMD entry associated with this inode.
	 * But it has the side effect to re"dirt" the inode.
	 */

/*      
 * internal_notify_change (inode, &newattrs);
 * inode->i_state &= ~I_DIRTY; / * FIXME: this doesn't work... we need to remove ourselfs from list on dirty inodes /mn/ */
}





/* #Specification: function name / convention
 * A simple convention for function name has been used in
 * the UMSDOS file system. First all function use the prefix
 * umsdos_ to avoid name clash with other part of the kernel.
 * 
 * And standard VFS entry point use the prefix UMSDOS (upper case)
 * so it's easier to tell them apart.
 * N.B. (FIXME) PTW, the order and contents of this struct changed
 */

static struct super_operations umsdos_sops =
{
	UMSDOS_read_inode,	/* read_inode */
	UMSDOS_write_inode,	/* write_inode */
	UMSDOS_put_inode,	/* put_inode */
	fat_delete_inode,	/* delete_inode */
	UMSDOS_notify_change,	/* notify_change */
	UMSDOS_put_super,	/* put_super */
	NULL,			/* write_super */
	fat_statfs,		/* statfs */
	NULL			/* remount_fs */
};

/*
 * Read the super block of an Extended MS-DOS FS.
 */
struct super_block *UMSDOS_read_super (
					      struct super_block *sb,
					      void *data,
					      int silent)
{
	/* #Specification: mount / options
	 * Umsdos run on top of msdos. Currently, it supports no
	 * mount option, but happily pass all option received to
	 * the msdos driver. I am not sure if all msdos mount option
	 * make sense with Umsdos. Here are at least those who
	 * are useful.
	 * uid=
	 * gid=
	 * 
	 * These options affect the operation of umsdos in directories
	 * which do not have an EMD file. They behave like normal
	 * msdos directory, with all limitation of msdos.
	 */
	struct super_block *res;
	struct inode *pseudo = NULL;

	Printk ((KERN_DEBUG "UMSDOS /mn/: starting UMSDOS_read_super\n"));
	MOD_INC_USE_COUNT;
	PRINTK ((KERN_DEBUG "UMSDOS /mn/: sb = %p\n", sb));
	res = msdos_read_super (sb, data, silent);
	PRINTK ((KERN_DEBUG "UMSDOS /mn/: res = %p\n", res));
	printk (KERN_INFO "UMSDOS dentry-WIP-Beta 0.82-4 (compatibility level %d.%d, fast msdos)\n", UMSDOS_VERSION, UMSDOS_RELEASE);

	if (res == NULL) {
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	MSDOS_SB (res)->options.dotsOK = 0;	/* disable hidden==dotfile */
	res->s_op = &umsdos_sops;
	Printk ((KERN_DEBUG "umsdos /mn/: here goes the iget ROOT_INO\n"));

	pseudo = iget (res, UMSDOS_ROOT_INO);
	Printk ((KERN_DEBUG "umsdos_read_super %p\n", pseudo));

	umsdos_setup_dir_inode (pseudo);
	Printk ((KERN_DEBUG "umsdos_setup_dir_inode passed. pseudo i_count=%d\n", pseudo->i_count));

	/* if (s == super_blocks){ FIXME, super_blocks no longer exported */
	if (pseudo) {
		/* #Specification: pseudo root / mount
		 * When a umsdos fs is mounted, a special handling is done
		 * if it is the root partition. We check for the presence
		 * of the file /linux/etc/init or /linux/etc/rc or
		 * /linux/sbin/init. If one is there, we do a chroot("/linux").
		 * 
		 * We check both because (see init/main.c) the kernel
		 * try to exec init at different place and if it fails
		 * it tries /bin/sh /etc/rc. To be consistent with
		 * init/main.c, many more test would have to be done
		 * to locate init. Any complain ?
		 * 
		 * The chroot is done manually in init/main.c but the
		 * info (the inode) is located at mount time and store
		 * in a global variable (pseudo_root) which is used at
		 * different place in the umsdos driver. There is no
		 * need to store this variable elsewhere because it
		 * will always be one, not one per mount.
		 * 
		 * This feature allows the installation
		 * of a linux system within a DOS system in a subdirectory.
		 * 
		 * A user may install its linux stuff in c:\linux
		 * avoiding any clash with existing DOS file and subdirectory.
		 * When linux boots, it hides this fact, showing a normal
		 * root directory with /etc /bin /tmp ...
		 * 
		 * The word "linux" is hardcoded in /usr/include/linux/umsdos_fs.h
		 * in the macro UMSDOS_PSDROOT_NAME.
		 */
		struct dentry *root, *etc, *etc_rc, *sbin, *init = NULL;

		root = creat_dentry (UMSDOS_PSDROOT_NAME, strlen (UMSDOS_PSDROOT_NAME), NULL, NULL);
		sbin = creat_dentry ("sbin", 4, NULL, NULL);

		Printk ((KERN_DEBUG "Mounting root\n"));
		if (umsdos_real_lookup (pseudo, root) == 0
		    && (root->d_inode != NULL)
		    && S_ISDIR (root->d_inode->i_mode)) {

			int pseudo_ok = 0;

			Printk ((KERN_DEBUG "/%s is there\n", UMSDOS_PSDROOT_NAME));
			etc = creat_dentry ("etc", 3, NULL, root);


			if (umsdos_real_lookup (pseudo, etc) == 0
			    && S_ISDIR (etc->d_inode->i_mode)) {

				Printk ((KERN_DEBUG "/%s/etc is there\n", UMSDOS_PSDROOT_NAME));

				init = creat_dentry ("init", 4, NULL, etc);
				etc_rc = creat_dentry ("rc", 2, NULL, etc);

				/* if ((umsdos_real_lookup (etc,"init",4,init)==0 */
				if ((umsdos_real_lookup (pseudo, init) == 0
				     && S_ISREG (init->d_inode->i_mode))
				/*       || (umsdos_real_lookup (etc,"rc",2,&rc)==0 */
				    || (umsdos_real_lookup (pseudo, etc_rc) == 0
				 && S_ISREG (etc_rc->d_inode->i_mode))) {
					pseudo_ok = 1;
				}
				/* FIXME !!!!!! */
				/* iput(init); */
				/* iput(rc); */
			}
			if (!pseudo_ok
			/* && umsdos_real_lookup (pseudo, "sbin", 4, sbin)==0 */
			    && umsdos_real_lookup (pseudo, sbin) == 0
			    && S_ISDIR (sbin->d_inode->i_mode)) {

				Printk ((KERN_DEBUG "/%s/sbin is there\n", UMSDOS_PSDROOT_NAME));
				/* if (umsdos_real_lookup (sbin,"init",4,init)==0 */
				if (umsdos_real_lookup (pseudo, init) == 0
				    && S_ISREG (init->d_inode->i_mode)) {
					pseudo_ok = 1;
				}
				/* FIXME !!! 
				 * iput (init); */
			}
			if (pseudo_ok) {
				umsdos_setup_dir_inode (pseudo);
				Printk ((KERN_INFO "Activating pseudo root /%s\n", UMSDOS_PSDROOT_NAME));
				pseudo_root = pseudo;
				inc_count (pseudo);
				pseudo = NULL;
			}
			/* FIXME 
			 * 
			 * iput (sbin);
			 * iput (etc);
			 */
		}
		iput (pseudo);
	}
	Printk ((KERN_DEBUG "umsdos_read_super /mn/: (pseudo=%lu, i_count=%d) returning %p\n", pseudo->i_ino, pseudo->i_count, res));
	return res;
}



static struct file_system_type umsdos_fs_type =
{
	"umsdos",
	FS_REQUIRES_DEV,
	UMSDOS_read_super,
	NULL
};

__initfunc (int init_umsdos_fs (void))
{
	return register_filesystem (&umsdos_fs_type);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module (void)
{
	return init_umsdos_fs ();
}

void cleanup_module (void)
{
	unregister_filesystem (&umsdos_fs_type);
}

#endif
