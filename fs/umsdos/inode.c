/*
 *  linux/fs/umsdos/inode.c
 *
 *      Written 1993 by Jacques Gelinas
 *      Inspired from linux/fs/msdos/... by Werner Almesberger
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


/*
 * returns inode->i_dentry
 * Note: Deprecated; won't work reliably
 */

struct dentry *geti_dentry (struct inode *inode)
{
	struct dentry *ret;

	if (!inode) {
		printk (KERN_ERR "geti_dentry: ERROR: inode is NULL!\n");
		return NULL;
	}
	if (list_empty(&inode->i_dentry)) {
		printk (KERN_WARNING 
			"geti_dentry: WARNING: no dentry for inode %ld\n",
			inode->i_ino);
		return NULL;
	}
	ret = list_entry (inode->i_dentry.next, struct dentry, d_alias);

	PRINTK ((KERN_DEBUG "geti_dentry : inode %lu, dentry is %s/%s\n", 
		inode->i_ino, ret->d_parent->d_name.name, ret->d_name.name));
	return ret;
}


/*
 * Initialize a private filp
 */
void fill_new_filp (struct file *filp, struct dentry *dentry)
{
	if (!dentry)
		printk("fill_new_filp: NULL dentry!\n");

	memset (filp, 0, sizeof (struct file));
	filp->f_reada = 1;
	filp->f_flags = O_RDWR;
	filp->f_dentry = dentry;
	filp->f_op = &umsdos_file_operations;
}


/*
 * makes dentry. for name name with length len.
 * if inode is not NULL, puts it also.
 * Note: Deprecated; use umsdos_lookup_dentry
 */

struct dentry *creat_dentry (const char *name, const int len, 
				struct inode *inode, struct dentry *parent)
{
/* FIXME /mn/: parent is not passed many times... if it is not, dentry should be destroyed before someone else gets to use it */

	struct dentry *ret;
	struct qstr qname;

	if (inode)
		Printk ((KERN_DEBUG "creat_dentry: creating dentry with inode=%lu for %.*s\n", inode->i_ino, len, name));
	else
		Printk ((KERN_DEBUG "creat_dentry: creating empty dentry for %.*s\n", len, name));

	qname.name = name;
	qname.len = len;
	qname.hash = full_name_hash (name, len);

	ret = d_alloc (parent, &qname);		/* create new dentry */

	if (parent) {
#if 0
		Printk ((KERN_DEBUG "creat_dentry: cloning parent d_op !\n"));
		ret->d_op = parent->d_op;
#else
		ret->d_op = NULL;
#endif		
	} else {
		ret->d_parent = ret;
		Printk ((KERN_WARNING "creat_dentry: WARNING: NO parent! faking root! beware !\n"));
	}


	if (inode) {
		/* try to fill it in if available. If available in 
		 * parent->d_sb, d_alloc will add it automatically
		 */
		if (!ret->d_sb)	ret->d_sb = inode->i_sb;
		d_add (ret, inode);
	}

	if (!ret->d_sb) {
		printk (KERN_ERR "creat_dentry: ERROR: NO d_sb !\n");
	} else if (!ret->d_sb->s_dev) {
		printk (KERN_WARNING "creat_dentry: WARNING: NO s_dev. Ugh. !\n");
	}
	
	return ret;
}


void UMSDOS_put_inode (struct inode *inode)
{
	PRINTK ((KERN_DEBUG 
		"put inode %p (%lu) owner %lu pos %lu dir %lu count=%d\n"
		 ,inode, inode->i_ino
		 ,inode->u.umsdos_i.i_emd_owner, inode->u.umsdos_i.pos
		 ,inode->u.umsdos_i.i_emd_dir, inode->i_count));

	if (inode && pseudo_root && inode == pseudo_root) {
		printk (KERN_ERR "Umsdos: Oops releasing pseudo_root."
			" Notify jacques@solucorp.qc.ca\n");
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
 * Return 0 if OK, or a negative error code if not.
 * Dentry will hold inode of the file, if successful
 */
int umsdos_real_lookup (struct inode *dir, struct dentry *dentry)
{
	int ret;

	PRINTK ((KERN_DEBUG "umsdos_real_lookup: looking for %s/%s /",
		dentry->d_parent->d_name.name, dentry->d_name.name));
	ret = msdos_lookup (dir, dentry);
	PRINTK (("/ returned %d\n", ret));

	return ret;
}

/*
 * Complete the setup of an directory dentry.
 * First, it completes the function pointers, then
 * it locates the EMD file. If the EMD is there, then plug the
 * umsdos function table. If not, use the msdos one.
 *
 * {i,d}_counts are untouched by this function.
 */
void umsdos_setup_dir(struct dentry *dir)
{
	struct inode *inode = dir->d_inode;

	if (!S_ISDIR(inode->i_mode))
		printk(KERN_ERR "umsdos_setup_dir: %s/%s not a dir!\n",
			dir->d_parent->d_name.name, dir->d_name.name);

	inode->u.umsdos_i.i_emd_dir = 0;
	inode->i_op = &umsdos_rdir_inode_operations;
	if (umsdos_have_emd(dir)) {
Printk((KERN_DEBUG "umsdos_setup_dir: %s/%s using EMD\n",
dir->d_parent->d_name.name, dir->d_name.name));
		inode->i_op = &umsdos_dir_inode_operations;
	}
}

/*
 * Complete the setup of an directory inode.
 * First, it completes the function pointers, then
 * it locates the EMD file. If the EMD is there, then plug the
 * umsdos function table. If not, use the msdos one.
 *
 * {i,d}_counts are untouched by this function.
 * Note: Deprecated; use above function if possible.
 */
void umsdos_setup_dir_inode (struct inode *inode)
{
	struct inode *emd_dir;

	inode->u.umsdos_i.i_emd_dir = 0;

	Printk ((KERN_DEBUG 
		"umsdos_setup_dir_inode: Entering for inode=%lu\n",
		 inode->i_ino));
	check_inode (inode);
	emd_dir = umsdos_emd_dir_lookup (inode, 0);
	Printk ((KERN_DEBUG "umsdos_setup_dir_inode: "
		"umsdos_emd_dir_lookup for inode=%lu returned %p\n",
		inode->i_ino, emd_dir));
	check_inode (inode);
	check_inode (emd_dir);

	inode->i_op = &umsdos_rdir_inode_operations;
	if (emd_dir) {
		Printk ((KERN_DEBUG "umsdos_setup_dir_inode: using EMD.\n"));
		inode->i_op = &umsdos_dir_inode_operations;
		iput (emd_dir);
	}
}


/*
 * Add some info into an inode so it can find its owner quickly
 */
void umsdos_set_dirinfo (struct inode *inode, struct inode *dir, off_t f_pos)
{
	struct inode *emd_owner = umsdos_emd_dir_lookup (dir, 1);

	if (!emd_owner)
		goto out;
	Printk (("umsdos_set_dirinfo: emd_owner is %lu for dir %lu\n", 
		emd_owner->i_ino, dir->i_ino));
	inode->u.umsdos_i.i_dir_owner = dir->i_ino;
	inode->u.umsdos_i.i_emd_owner = emd_owner->i_ino;
	inode->u.umsdos_i.pos = f_pos;
	iput (emd_owner);
out:
	return;
}


/*
 * Tells if an Umsdos inode has been "patched" once.
 * Return != 0 if so.
 */
int umsdos_isinit (struct inode *inode)
{
	return inode->u.umsdos_i.i_emd_owner != 0;
}


/*
 * Connect the proper tables in the inode and add some info.
 * i_counts is not changed.
 *
 * This function is called very early to setup the inode, somewhat
 * too early (called by UMSDOS_read_inode). At this point, we can't
 * do too much, such as lookup up EMD files and so on. This causes
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
/* #Specification: inode / umsdos info
 * The first time an inode is seen (inode->i_count == 1),
 * the inode number of the EMD file which controls this inode
 * is tagged to this inode. It allows operations such as
 * notify_change to be handled.
 */
void umsdos_patch_inode (struct inode *inode, struct inode *dir, off_t f_pos)
{
	Printk ((KERN_DEBUG "Entering umsdos_patch_inode for inode=%lu\n",
		inode->i_ino));

	if (umsdos_isinit (inode))
		goto already_init;

	inode->u.umsdos_i.i_emd_dir = 0;
	if (S_ISREG (inode->i_mode)) {
		if (MSDOS_SB (inode->i_sb)->cvf_format) {
			if (MSDOS_SB (inode->i_sb)->cvf_format->flags & CVF_USE_READPAGE) {
Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: setting i_op = umsdos_file_inode_operations_readpage\n"));
				inode->i_op = &umsdos_file_inode_operations_readpage;
			} else {
Printk ((KERN_DEBUG "umsdos_patch_inode: umsdos_file_inode_ops_no_bmap\n"));
				inode->i_op = &umsdos_file_inode_operations_no_bmap;
			}
		} else {
			if (inode->i_op->bmap != NULL) {
Printk ((KERN_DEBUG "umsdos_patch_inode: umsdos_file_inode_ops\n"));
				inode->i_op = &umsdos_file_inode_operations;
			} else {
Printk ((KERN_DEBUG "umsdos_patch_inode: umsdos_file_inode_ops_no_bmap\n"));
				inode->i_op = &umsdos_file_inode_operations_no_bmap;
			}
		}
	} else if (S_ISDIR (inode->i_mode)) {
		if (dir != NULL) {
			umsdos_setup_dir_inode (inode);
		}
	} else if (S_ISLNK (inode->i_mode)) {
		Printk ((KERN_DEBUG 
			"umsdos_patch_inode: umsdos_symlink_inode_ops\n"));
		inode->i_op = &umsdos_symlink_inode_operations;
	} else if (S_ISCHR (inode->i_mode)) {
		Printk ((KERN_DEBUG "umsdos_patch_inode: chrdev_inode_ops\n"));
		inode->i_op = &chrdev_inode_operations;
	} else if (S_ISBLK (inode->i_mode)) {
		Printk ((KERN_DEBUG "umsdos_patch_inode: blkdev_inode_ops\n"));
		inode->i_op = &blkdev_inode_operations;
	} else if (S_ISFIFO (inode->i_mode)) {
		Printk ((KERN_DEBUG "umsdos_patch_inode /mn/: uhm, init_fifo\n"));
		init_fifo (inode);
	}
	if (dir != NULL) {
		/*
		 * This is done last because it also control the
		 * status of umsdos_isinit()
		 */
		Printk ((KERN_DEBUG 
			"umsdos_patch_inode: call x_set_dirinfo(%p,%p,%lu)\n",
			 inode, dir, f_pos));
		umsdos_set_dirinfo (inode, dir, f_pos);
	}
	return;

already_init:
	if (dir != NULL) {
		/*
		 * Test to see if the info is maintained.
		 * This should be removed when the file system will be proven.
		 */
		struct inode *emd_owner = umsdos_emd_dir_lookup (dir, 1);
		if (!emd_owner)
			goto out;
		if (emd_owner->i_ino != inode->u.umsdos_i.i_emd_owner) {
printk ("UMSDOS: *** EMD_OWNER ??? *** ino = %ld %ld <> %ld ",
inode->i_ino, emd_owner->i_ino, inode->u.umsdos_i.i_emd_owner);
		}
		iput (emd_owner);
	out:
		return;
	}
}

/*
 * Patch the inode in a dentry.
 */
void umsdos_patch_dentry_inode(struct dentry *dentry, off_t f_pos)
{
	umsdos_patch_inode(dentry->d_inode, dentry->d_parent->d_inode, f_pos);
}


/*
 * Load an inode from disk.
 */
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
void UMSDOS_read_inode (struct inode *inode)
{
	PRINTK ((KERN_DEBUG "UMSDOS_read_inode %p ino = %lu ",
		inode, inode->i_ino));
	msdos_read_inode (inode);
	PRINTK (("ino after msdos_read_inode= %lu i_count=%d\n",
		inode->i_ino, inode->i_count));
	if (S_ISDIR (inode->i_mode)
	    && (inode->u.umsdos_i.u.dir_info.creating != 0
		|| inode->u.umsdos_i.u.dir_info.looking != 0
		|| waitqueue_active (&inode->u.umsdos_i.u.dir_info.p))) {
		Printk (("read inode %d %d %p\n"
			 ,inode->u.umsdos_i.u.dir_info.creating
			 ,inode->u.umsdos_i.u.dir_info.looking
			 ,inode->u.umsdos_i.u.dir_info.p));
	}

	/* N.B. Defer this until we have a dentry ... */
	umsdos_patch_inode (inode, NULL, 0);
}


/* #Specification: notify_change / i_nlink > 0
 * notify change is only done for inode with nlink > 0. An inode
 * with nlink == 0 is no longer associated with any entry in
 * the EMD file, so there is nothing to update.
 */
static int internal_notify_change (struct inode *inode, struct iattr *attr)
{
	unsigned long i_emd_owner = inode->u.umsdos_i.i_emd_owner;
	int ret;

	Printk ((KERN_DEBUG "UMSDOS_notify_change: entering\n"));

	if ((ret = inode_change_ok (inode, attr)) != 0)
		goto out;

	if (inode->i_nlink == 0)
		goto out_nolink;

	if (inode->i_ino == UMSDOS_ROOT_INO)
		goto out_nolink;

	if (i_emd_owner != 0xffffffff && i_emd_owner != 0) {
		/* This inode is not a EMD file nor an inode used internally
		 * by MSDOS, so we can update its status.
		 * See emd.c
		 */
		struct inode *emd_owner;
		struct file filp;
		struct umsdos_dirent entry;
		struct dentry *emd_dentry;

		Printk (("notify change %p ", inode));
		ret = -EPERM;
		emd_owner = iget (inode->i_sb, i_emd_owner);
		if (!emd_owner) {
			printk ("UMSDOS: emd_owner = NULL ???");
			goto out_nolink;
		}
		emd_dentry = geti_dentry (emd_owner); /* FIXME? */
		fill_new_filp (&filp, emd_dentry);

		filp.f_pos = inode->u.umsdos_i.pos;
		filp.f_reada = 0;
		Printk (("pos = %Lu ", filp.f_pos));
		/* Read only the start of the entry since we don't touch */
		/* the name */
		ret = umsdos_emd_dir_read (&filp, (char *) &entry, 
						UMSDOS_REC_SIZE);
		if (!ret) {
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
			ret = umsdos_emd_dir_write (&filp, (char *) &entry,
							 UMSDOS_REC_SIZE);

			Printk (("notify pos %lu ret %d nlink %d ",
				inode->u.umsdos_i.pos, ret, entry.nlink));
			/* #Specification: notify_change / msdos fs
			 * notify_change operation are done only on the
			 * EMD file. The msdos fs is not even called.
			 */
		}
		iput(emd_owner);
	}
out_nolink:
	if (ret == 0)
		inode_setattr (inode, attr);
out:
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

	PRINTK (("UMSDOS_write_inode emd %d (FIXME: missing notify_change)\n",
		inode->u.umsdos_i.i_emd_owner));
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
 * inode->i_state &= ~I_DIRTY; / * FIXME: this doesn't work.  We need to remove ourselves from list on dirty inodes. /mn/ */
}


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
struct super_block *UMSDOS_read_super (struct super_block *sb, void *data,
				      int silent)
{
	struct super_block *res;
	struct inode *pseudo = NULL;

	MOD_INC_USE_COUNT;
	MSDOS_SB(sb)->options.isvfat = 0;
	/*
	 * Call msdos-fs to mount the disk.
	 * Note: this returns res == sb or NULL
	 */
	res = msdos_read_super (sb, data, silent);
	if (!res)
		goto out_fail;

	printk (KERN_INFO "UMSDOS dentry-WIP-Beta 0.82-7 "
		"(compatibility level %d.%d, fast msdos)\n", 
		UMSDOS_VERSION, UMSDOS_RELEASE);

	sb->s_op = &umsdos_sops;
	MSDOS_SB(sb)->options.dotsOK = 0;	/* disable hidden==dotfile */

	/* FIXME:?? clear d_op on root so it will not be inherited */
	sb->s_root->d_op = NULL;

	pseudo = sb->s_root->d_inode;
	umsdos_setup_dir(sb->s_root);

#if 0
	if (pseudo) {
		pseudo_root_stuff();
	}
#endif

	/* if d_count is not 1, mount will fail with -EBUSY! */
	if (sb->s_root->d_count > 1) {
		shrink_dcache_sb(sb);
	}
	return sb;

out_fail:
	printk(KERN_INFO "UMSDOS: msdos_read_super failed, mount aborted.\n");
	sb->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return NULL;
}

/*
 * FIXME URGENT:
 * disable pseudo root-for the moment of testing. 
 * re-enable this before release !
 */
#if 0
void pseudo_root_stuff(void)
{
	struct dentry *root, *etc, *etc_rc, *sbin, *init = NULL;

	root = creat_dentry (UMSDOS_PSDROOT_NAME, 
				strlen (UMSDOS_PSDROOT_NAME),
			 	NULL, res->s_root);
	sbin = creat_dentry ("sbin", 4, NULL, root);

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

			if ((umsdos_real_lookup (pseudo, init) == 0
			     && S_ISREG (init->d_inode->i_mode))
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
}
#endif	


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
