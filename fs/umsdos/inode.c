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

extern struct dentry_operations umsdos_dentry_operations;
extern struct inode_operations umsdos_rdir_inode_operations;


struct dentry *saved_root = NULL;	/* Original root if changed */
struct inode *pseudo_root = NULL;	/* Useful to simulate the pseudo DOS */
					/* directory. See UMSDOS_readdir_x() */

static struct dentry *check_pseudo_root(struct super_block *);


/*
 * Initialize a private filp
 */
void fill_new_filp (struct file *filp, struct dentry *dentry)
{
	if (!dentry)
		printk(KERN_ERR "fill_new_filp: NULL dentry!\n");

	memset (filp, 0, sizeof (struct file));
	filp->f_reada = 1;
	filp->f_flags = O_RDWR;
	filp->f_dentry = dentry;
	filp->f_op = &umsdos_file_operations;
}



void UMSDOS_put_inode (struct inode *inode)
{
	PRINTK ((KERN_DEBUG 
		"put inode %p (%lu) owner %lu pos %lu dir %lu count=%d\n"
		 ,inode, inode->i_ino
		 ,inode->u.umsdos_i.i_emd_owner, inode->u.umsdos_i.pos
		 ,inode->u.umsdos_i.i_emd_dir, inode->i_count));

	if (inode == pseudo_root) {
		printk (KERN_ERR "Umsdos: Oops releasing pseudo_root."
			" Notify jacques@solucorp.qc.ca\n");
	}

	inode->u.umsdos_i.i_patched = 0;
	fat_put_inode (inode);
}


void UMSDOS_put_super (struct super_block *sb)
{
	Printk ((KERN_DEBUG "UMSDOS_put_super: entering\n"));
	if (saved_root) {
		shrink_dcache_parent(saved_root);
printk("UMSDOS_put_super: freeing saved root, d_count=%d\n",
saved_root->d_count);
		dput(saved_root);
		saved_root = NULL;
		pseudo_root = NULL;
	}
	msdos_put_super (sb);
	MOD_DEC_USE_COUNT;
}


/*
 * Complete the setup of a directory dentry based on its
 * EMD/non-EMD status.  If it has an EMD, then plug the
 * umsdos function table. If not, use the msdos one.
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
 * Add some info into an inode so it can find its owner quickly
 */
void umsdos_set_dirinfo_new (struct dentry *dentry, off_t f_pos)
{
	struct inode *inode = dentry->d_inode;
	struct dentry *demd;

	inode->u.umsdos_i.i_emd_owner = 0;
	inode->u.umsdos_i.pos = f_pos;

	/* now check the EMD file */
	demd = umsdos_get_emd_dentry(dentry->d_parent);
	if (!IS_ERR(demd)) {
		if (demd->d_inode)
			inode->u.umsdos_i.i_emd_owner = demd->d_inode->i_ino;
		dput(demd);
	}
	return;
}



/*
 * Connect the proper tables in the inode and add some info.
 */
/* #Specification: inode / umsdos info
 * The first time an inode is seen (inode->i_count == 1),
 * the inode number of the EMD file which controls this inode
 * is tagged to this inode. It allows operations such as
 * notify_change to be handled.
 */
void umsdos_patch_dentry_inode(struct dentry *dentry, off_t f_pos)
{
	struct inode *inode = dentry->d_inode;

PRINTK (("umsdos_patch_dentry_inode: inode=%lu\n", inode->i_ino));

	/*
	 * Classify the inode based on EMD/non-EMD status.
	 */
PRINTK (("umsdos_patch_inode: call umsdos_set_dirinfo_new(%p,%lu)\n",
dentry, f_pos));
	umsdos_set_dirinfo_new(dentry, f_pos);

	inode->u.umsdos_i.i_emd_dir = 0;
	if (S_ISREG (inode->i_mode)) {
		if (MSDOS_SB (inode->i_sb)->cvf_format) {
			if (MSDOS_SB (inode->i_sb)->cvf_format->flags & CVF_USE_READPAGE) {
				inode->i_op = &umsdos_file_inode_operations_readpage;
			} else {
				inode->i_op = &umsdos_file_inode_operations_no_bmap;
			}
		} else {
			if (inode->i_op->bmap != NULL) {
				inode->i_op = &umsdos_file_inode_operations;
			} else {
				inode->i_op = &umsdos_file_inode_operations_no_bmap;
			}
		}
	} else if (S_ISDIR (inode->i_mode)) {
		umsdos_setup_dir(dentry);
	} else if (S_ISLNK (inode->i_mode)) {
		inode->i_op = &umsdos_symlink_inode_operations;
	} else if (S_ISCHR (inode->i_mode)) {
		inode->i_op = &chrdev_inode_operations;
	} else if (S_ISBLK (inode->i_mode)) {
		inode->i_op = &blkdev_inode_operations;
	} else if (S_ISFIFO (inode->i_mode)) {
		init_fifo (inode);
	}
}


/*
 * Load an inode from disk.
 */
/* #Specification: Inode / post initialisation
 * To completely initialise an inode, we need access to the owner
 * directory, so we can locate more info in the EMD file. This is
 * not available the first time the inode is accessed, so we use
 * a value in the inode to tell if it has been finally initialised.
 * 
 * New inodes are obtained by the lookup and create routines, and
 * each of these must ensure that the inode gets patched.
 */
void UMSDOS_read_inode (struct inode *inode)
{
	Printk ((KERN_DEBUG "UMSDOS_read_inode %p ino = %lu ",
		inode, inode->i_ino));
	msdos_read_inode (inode);

	/* inode needs patching */
	inode->u.umsdos_i.i_patched = 0;
}


int umsdos_notify_change_locked(struct dentry *, struct iattr *);
/*
 * lock the parent dir before starting ...
 */
int UMSDOS_notify_change (struct dentry *dentry, struct iattr *attr)
{
	struct inode *dir = dentry->d_parent->d_inode;
	int ret;

	down(&dir->i_sem);
	ret = umsdos_notify_change_locked(dentry, attr);
	up(&dir->i_sem);
	return ret;
}

/*
 * Must be called with the parent lock held.
 */
int umsdos_notify_change_locked(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct dentry *demd;
	int ret;
	struct file filp;
	struct umsdos_dirent entry;

Printk(("UMSDOS_notify_change: entering for %s/%s (%d)\n",
dentry->d_parent->d_name.name, dentry->d_name.name, inode->u.umsdos_i.i_patched));

	ret = inode_change_ok (inode, attr);
	if (ret) {
printk("UMSDOS_notify_change: %s/%s change not OK, ret=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, ret);
		goto out;
	}

	if (inode->i_nlink == 0)
		goto out;
	if (inode->i_ino == UMSDOS_ROOT_INO)
		goto out;

	/* get the EMD file dentry */
	demd = umsdos_get_emd_dentry(dentry->d_parent);
	ret = PTR_ERR(demd);
	if (IS_ERR(demd))
		goto out;
	ret = -EPERM;
	if (!demd->d_inode) {
		printk(KERN_WARNING
			"UMSDOS_notify_change: no EMD file %s/%s\n",
			demd->d_parent->d_name.name, demd->d_name.name);
		goto out_dput;
	}

	ret = 0;
	/* don't do anything if this is the EMD itself */
	if (inode == demd->d_inode)
		goto out_dput;

	/* This inode is not a EMD file nor an inode used internally
	 * by MSDOS, so we can update its status.
	 * See emd.c
	 */

	fill_new_filp (&filp, demd);
	filp.f_pos = inode->u.umsdos_i.pos;
Printk(("UMSDOS_notify_change: %s/%s reading at %d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, (int) filp.f_pos));

	/* Read only the start of the entry since we don't touch the name */
	ret = umsdos_emd_dir_read (&filp, (char *) &entry, UMSDOS_REC_SIZE);
	if (ret) {
		printk(KERN_WARNING
			"umsdos_notify_change: %s/%s EMD read error, ret=%d\n",
			dentry->d_parent->d_name.name, dentry->d_name.name,ret);
		goto out_dput;
	}
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
	ret = umsdos_emd_dir_write (&filp, (char *) &entry, UMSDOS_REC_SIZE);
	if (ret)
		printk(KERN_WARNING
			"umsdos_notify_change: %s/%s EMD write error, ret=%d\n",
			dentry->d_parent->d_name.name, dentry->d_name.name,ret);

	Printk (("notify pos %lu ret %d nlink %d ",
		inode->u.umsdos_i.pos, ret, entry.nlink));
	/* #Specification: notify_change / msdos fs
	 * notify_change operation are done only on the
	 * EMD file. The msdos fs is not even called.
	 */
#ifdef UMSDOS_DEBUG_VERBOSE
if (entry.flags & UMSDOS_HIDDEN)
printk("umsdos_notify_change: %s/%s hidden, nlink=%d, ret=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, entry.nlink, ret);
#endif

out_dput:
	dput(demd);
out:
	if (ret == 0)
		inode_setattr (inode, attr);
	return ret;
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
 * UMSDOS_notify_change (inode, &newattrs);

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
	struct dentry *new_root;

	MOD_INC_USE_COUNT;
	MSDOS_SB(sb)->options.isvfat = 0;
	/*
	 * Call msdos-fs to mount the disk.
	 * Note: this returns res == sb or NULL
	 */
	res = msdos_read_super (sb, data, silent);
	if (!res)
		goto out_fail;

	printk (KERN_INFO "UMSDOS dentry-pre 0.84 "
		"(compatibility level %d.%d, fast msdos)\n", 
		UMSDOS_VERSION, UMSDOS_RELEASE);

	sb->s_op = &umsdos_sops;
	MSDOS_SB(sb)->options.dotsOK = 0;	/* disable hidden==dotfile */

	/* install our dentry operations ... */
	sb->s_root->d_op = &umsdos_dentry_operations;
	umsdos_patch_dentry_inode(sb->s_root, 0);

	/* Check whether to change to the /linux root */
	new_root = check_pseudo_root(sb);

	if (new_root) {
		/* sanity check */
		if (new_root->d_op != &umsdos_dentry_operations)
			printk("umsdos_read_super: pseudo-root wrong ops!\n");

		pseudo_root = new_root->d_inode;

		saved_root = sb->s_root;
		sb->s_root = new_root;
		printk(KERN_INFO "UMSDOS: changed to alternate root\n");
	}

	/* if d_count is not 1, mount will fail with -EBUSY! */
	if (sb->s_root->d_count > 1) {
		shrink_dcache_sb(sb);
		if (sb->s_root->d_count > 1) {
			printk(KERN_ERR "UMSDOS: root count %d > 1 !", sb->s_root->d_count);
		}
	}
	return sb;

out_fail:
	printk(KERN_INFO "UMSDOS: msdos_read_super failed, mount aborted.\n");
	sb->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return NULL;
}

/*
 * Check for an alternate root if we're the root device.
 */
static struct dentry *check_pseudo_root(struct super_block *sb)
{
	struct dentry *root, *init;

	/*
	 * Check whether we're mounted as the root device.
	 * If so, this should be the only superblock.
	 */
	if (sb->s_list.next->next != &sb->s_list)
		goto out_noroot;
printk("check_pseudo_root: mounted as root\n");

	root = lookup_dentry(UMSDOS_PSDROOT_NAME, dget(sb->s_root), 0); 
	if (IS_ERR(root))
		goto out_noroot;
	if (!root->d_inode)
		goto out_dput;
printk("check_pseudo_root: found %s/%s\n",
root->d_parent->d_name.name, root->d_name.name);

	/* look for /sbin/init */
	init = lookup_dentry("sbin/init", dget(root), 0);
	if (!IS_ERR(init)) {
		if (init->d_inode)
			goto root_ok;
		dput(init);
	}
	/* check for other files? */
	goto out_dput;

root_ok:
printk("check_pseudo_root: found %s/%s, enabling pseudo-root\n",
init->d_parent->d_name.name, init->d_name.name);
	dput(init);
	return root;

	/* Alternate root not found ... */
out_dput:
	dput(root);
out_noroot:
	return NULL;
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
