/*
 *  inode.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/dcache.h>
#include <linux/smb_fs.h>
#include <linux/smbno.h>
#include <linux/smb_mount.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#define SMBFS_PARANOIA 1
/* #define SMBFS_DEBUG_VERBOSE 1 */

static void smb_read_inode(struct inode *);
static void smb_put_inode(struct inode *);
static void smb_delete_inode(struct inode *);
static void smb_put_super(struct super_block *);
static int  smb_statfs(struct super_block *, struct statfs *, int);

static struct super_operations smb_sops =
{
	smb_read_inode,		/* read inode */
	NULL,			/* write inode */
	smb_put_inode,		/* put inode */
	smb_delete_inode,	/* delete inode */
	smb_notify_change,	/* notify change */
	smb_put_super,		/* put superblock */
	NULL,			/* write superblock */
	smb_statfs,		/* stat filesystem */
	NULL			/* remount filesystem */
};

/* FIXME: Look at all inodes whether so that we do not get duplicate
 * inode numbers. */

unsigned long
smb_invent_inos(unsigned long n)
{
	static unsigned long ino = 1;

	if (ino + 2*n < ino)
	{
		/* wrap around */
		ino += n;
	}
	ino += n;
	return ino;
}

static struct smb_fattr *read_fattr = NULL;
static struct semaphore read_semaphore = MUTEX;

struct inode *
smb_iget(struct super_block *sb, struct smb_fattr *fattr)
{
	struct inode *result;

	pr_debug("smb_iget: %p\n", fattr);

	down(&read_semaphore);
	read_fattr = fattr;
	result = iget(sb, fattr->f_ino);
	read_fattr = NULL;
	up(&read_semaphore);
	return result;
}

static void
smb_set_inode_attr(struct inode *inode, struct smb_fattr *fattr)
{
	inode->i_mode	= fattr->f_mode;
	inode->i_nlink	= fattr->f_nlink;
	inode->i_uid	= fattr->f_uid;
	inode->i_gid	= fattr->f_gid;
	inode->i_rdev	= fattr->f_rdev;
	inode->i_size	= fattr->f_size;
	inode->i_mtime	= fattr->f_mtime;
	inode->i_ctime	= fattr->f_ctime;
	inode->i_atime	= fattr->f_atime;
	inode->i_blksize= fattr->f_blksize;
	inode->i_blocks = fattr->f_blocks;
	/*
	 * Update the "last time refreshed" field for revalidation.
	 */
	inode->u.smbfs_i.oldmtime = jiffies;
}

static void
smb_read_inode(struct inode *inode)
{
	pr_debug("smb_iget: %p\n", read_fattr);

	if (!read_fattr || inode->i_ino != read_fattr->f_ino)
	{
		printk("smb_read_inode called from invalid point\n");
		return;
	}

	inode->i_dev = inode->i_sb->s_dev;
	memset(&(inode->u.smbfs_i), 0, sizeof(inode->u.smbfs_i));
	smb_set_inode_attr(inode, read_fattr);

	if (S_ISREG(inode->i_mode))
		inode->i_op = &smb_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &smb_dir_inode_operations;
	else
		inode->i_op = NULL;
}

/*
 * This is called if the connection has gone bad ...
 * try to kill off all the current inodes.
 */
void
smb_invalidate_inodes(struct smb_sb_info *server)
{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_invalidate_inodes\n");
#endif
	shrink_dcache_sb(SB_of(server));
	invalidate_inodes(SB_of(server));
}

/*
 * This is called when we want to check whether the inode
 * has changed on the server.  If it has changed, we must
 * invalidate our local caches.
 */
int
smb_revalidate_inode(struct inode *inode)
{
	time_t last_time;
	int error = 0;

	pr_debug("smb_revalidate_inode\n");
	/*
	 * Check whether we've recently refreshed the inode.
	 */
	if (jiffies < inode->u.smbfs_i.oldmtime + HZ/10)
	{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_revalidate_inode: up-to-date, jiffies=%lu, oldtime=%lu\n",
jiffies, inode->u.smbfs_i.oldmtime);
#endif
		goto out;
	}

	/*
	 * Save the last modified time, then refresh the inode.
	 * (Note: a size change should have a different mtime.)
	 */
	last_time = inode->i_mtime;
	error = smb_refresh_inode(inode);
	if (error || inode->i_mtime != last_time)
	{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_revalidate: %s/%s changed, old=%ld, new=%ld\n",
((struct dentry *)inode->u.smbfs_i.dentry)->d_parent->d_name.name,
((struct dentry *)inode->u.smbfs_i.dentry)->d_name.name,
(long) last_time, (long) inode->i_mtime);
#endif
		if (!S_ISDIR(inode->i_mode))
			invalidate_inode_pages(inode);
		else
			smb_invalid_dir_cache(inode);
	}
out:
	return error;
}

/*
 * This is called to update the inode attributes after
 * we've made changes to a file or directory.
 */
int
smb_refresh_inode(struct inode *inode)
{
	struct dentry * dentry = inode->u.smbfs_i.dentry;
	struct smb_fattr fattr;
	int error;

	pr_debug("smb_refresh_inode\n");
	if (!dentry)
	{
		printk("smb_refresh_inode: no dentry, can't refresh\n");
		error = -EIO;
		goto out;
	}

	/*
	 * Kludge alert ... for some reason we can't get attributes
	 * for the root directory, so just return success.
	 */
	error = 0;
	if (IS_ROOT(dentry))
		goto out;

	error = smb_proc_getattr(dentry->d_parent, &(dentry->d_name), &fattr);
	if (!error)
	{
		smb_renew_times(dentry);
		/*
		 * Check whether the type part of the mode changed,
		 * and don't update the attributes if it did.
		 */
		if ((inode->i_mode & S_IFMT) == (fattr.f_mode & S_IFMT))
			smb_set_inode_attr(inode, &fattr);
		else
		{
			/*
			 * Big trouble! The inode has become a new object,
			 * so any operations attempted on it are invalid.
			 *
			 * Take a couple of steps to limit damage:
			 * (1) Mark the inode as bad so that subsequent
			 *     lookup validations will fail.
			 * (2) Clear i_nlink so the inode will be released
			 *     at iput() time. (Unhash it as well?)
			 * We also invalidate the caches for good measure.
			 */
#ifdef SMBFS_PARANOIA
printk("smb_refresh_inode: %s/%s changed mode, %07o to %07o\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
inode->i_mode, fattr.f_mode);
#endif
			fattr.f_mode = inode->i_mode; /* save mode */
			make_bad_inode(inode);
			inode->i_mode = fattr.f_mode; /* restore mode */
			inode->i_nlink = 0;
			/*
			 * No need to worry about unhashing the dentry: the
			 * lookup validation will see that the inode is bad.
			 * But we do want to invalidate the caches ...
			 */
			if (!S_ISDIR(inode->i_mode))
				invalidate_inode_pages(inode);
			else
				smb_invalid_dir_cache(inode);
			error = -EIO;
		}
	}
out:
	return error;

}

/*
 * This routine is called for every iput().
 */
static void
smb_put_inode(struct inode *ino)
{
	pr_debug("smb_put_inode: count = %d\n", ino->i_count);

	if (ino->i_count > 1) {
		struct dentry * dentry;
		/*
		 * Check whether the dentry still holds this inode. 
		 * This looks scary, but should work ... if this is
		 * the last use, d_inode == NULL or d_count == 0. 
		 */
		dentry = (struct dentry *) ino->u.smbfs_i.dentry;
		if (dentry && (dentry->d_inode != ino || dentry->d_count == 0))
		{
			ino->u.smbfs_i.dentry = NULL;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_put_inode: cleared dentry for %s/%s (%ld), count=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, ino->i_ino, ino->i_count);
#endif
		}
	} else {
		/*
		 * Last use ... clear i_nlink to force
		 * smb_delete_inode to be called.
	 	*/
		ino->i_nlink = 0;
	}
}

/*
 * This routine is called when i_nlink == 0 and i_count goes to 0.
 * All blocking cleanup operations need to go here to avoid races.
 */
static void
smb_delete_inode(struct inode *ino)
{
	pr_debug("smb_delete_inode\n");
	if (smb_close(ino))
		printk("smb_delete_inode: could not close inode %ld\n",
			ino->i_ino);
	clear_inode(ino);
}

static void
smb_put_super(struct super_block *sb)
{
	struct smb_sb_info *server = &(sb->u.smbfs_sb);

	lock_super(sb);

	if (server->sock_file) {
		smb_proc_disconnect(server);
		smb_dont_catch_keepalive(server);
		close_fp(server->sock_file);
	}

	if (server->conn_pid)
	       kill_proc(server->conn_pid, SIGTERM, 0);

	kfree(server->mnt);
	if (server->packet)
		smb_vfree(server->packet);
	sb->s_dev = 0;

	unlock_super(sb);

	MOD_DEC_USE_COUNT;
}

struct super_block *
smb_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct smb_mount_data *mnt, *data = (struct smb_mount_data *) raw_data;
	struct smb_fattr root;
	kdev_t dev = sb->s_dev;
	struct inode *root_inode;
	struct dentry *dentry;

	MOD_INC_USE_COUNT;

	if (!data)
		goto out_no_data;
		
	if (data->version != SMB_MOUNT_VERSION)
		goto out_wrong_data;

	lock_super(sb);

	sb->s_blocksize = 1024;	/* Eh...  Is this correct? */
	sb->s_blocksize_bits = 10;
	sb->s_magic = SMB_SUPER_MAGIC;
	sb->s_dev = dev; /* shouldn't need this ... */
	sb->s_op = &smb_sops;

	sb->u.smbfs_sb.sock_file = NULL;
	sb->u.smbfs_sb.sem = MUTEX;
	sb->u.smbfs_sb.wait = NULL;
	sb->u.smbfs_sb.conn_pid = 0;
	sb->u.smbfs_sb.state = CONN_INVALID; /* no connection yet */
	sb->u.smbfs_sb.generation = 0;
	sb->u.smbfs_sb.packet_size = smb_round_length(SMB_INITIAL_PACKET_SIZE);	
	sb->u.smbfs_sb.packet = smb_vmalloc(sb->u.smbfs_sb.packet_size);
	if (!sb->u.smbfs_sb.packet)
		goto out_no_mem;

	mnt = kmalloc(sizeof(struct smb_mount_data), GFP_KERNEL);
	if (!mnt)
		goto out_no_mount;
	*mnt = *data;
	mnt->version = 0; /* dynamic flags */
#ifdef CONFIG_SMB_WIN95
	mnt->version |= 1;
#endif
	mnt->file_mode &= (S_IRWXU | S_IRWXG | S_IRWXO);
	mnt->file_mode |= S_IFREG;
	mnt->dir_mode  &= (S_IRWXU | S_IRWXG | S_IRWXO);
	mnt->dir_mode  |= S_IFDIR;
	sb->u.smbfs_sb.mnt = mnt;

	/*
	 * Keep the super block locked while we get the root inode.
	 */
	smb_init_root_dirent(&(sb->u.smbfs_sb), &root);
	root_inode = smb_iget(sb, &root);
	if (!root_inode)
		goto out_no_root;

	dentry = d_alloc_root(root_inode, NULL);
	if (!dentry)
		goto out_no_root;
	root_inode->u.smbfs_i.dentry = dentry;
	sb->s_root = dentry;

	unlock_super(sb);
	return sb;

out_no_root:
	printk(KERN_ERR "smb_read_super: get root inode failed\n");
	iput(root_inode);
	kfree(sb->u.smbfs_sb.mnt);
out_no_mount:
	smb_vfree(sb->u.smbfs_sb.packet);
	goto out_unlock;
out_no_mem:
	printk("smb_read_super: could not alloc packet\n");
out_unlock:
	unlock_super(sb);
	goto out_fail;
out_wrong_data:
	printk("smb_read_super: need mount version %d\n", SMB_MOUNT_VERSION);
	goto out_fail;
out_no_data:
	printk("smb_read_super: missing data argument\n");
out_fail:
	sb->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return NULL;
}

static int
smb_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs attr;

	memset(&attr, 0, sizeof(attr));

	smb_proc_dskattr(sb, &attr);

	attr.f_type = SMB_SUPER_MAGIC;
	attr.f_files = -1;
	attr.f_ffree = -1;
	attr.f_namelen = SMB_MAXPATHLEN;
	return copy_to_user(buf, &attr, bufsiz) ? -EFAULT : 0;
}

int
smb_notify_change(struct inode *inode, struct iattr *attr)
{
	struct smb_sb_info *server = SMB_SERVER(inode);
	struct dentry *dentry = inode->u.smbfs_i.dentry;
	unsigned int mask = (S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO);
	int error, refresh = 0;

	error = -EIO;
	if (!dentry)
	{
		printk("smb_notify_change: no dentry for inode!\n");
		goto out;
	}

	/*
	 * Make sure our inode is up-to-date ...
	 */
	error = smb_revalidate_inode(inode);
	if (error)
		goto out;

	if ((error = inode_change_ok(inode, attr)) < 0)
		goto out;

	error = -EPERM;
	if ((attr->ia_valid & ATTR_UID) && (attr->ia_uid != server->mnt->uid))
		goto out;

	if ((attr->ia_valid & ATTR_GID) && (attr->ia_uid != server->mnt->gid))
		goto out;

	if ((attr->ia_valid & ATTR_MODE) && (attr->ia_mode & ~mask))
		goto out;

	if ((attr->ia_valid & ATTR_SIZE) != 0)
	{
		error = smb_open(dentry, O_WRONLY);
		if (error)
			goto out;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_notify_change: changing %s/%s, old size=%ld, new size=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
(long) inode->i_size, (long) attr->ia_size);
#endif
		error = smb_proc_trunc(server, inode->u.smbfs_i.fileid,
					 attr->ia_size);
		if (error)
			goto out;
		/*
		 * We don't implement an i_op->truncate operation,
		 * so we have to update the page cache here.
		 */
		if (attr->ia_size < inode->i_size) {
			truncate_inode_pages(inode, attr->ia_size);
			inode->i_size = attr->ia_size;
		}
		refresh = 1;
	}

	if ((attr->ia_valid & (ATTR_CTIME | ATTR_MTIME | ATTR_ATIME)) != 0)
	{
		struct smb_fattr fattr;

		fattr.attr = 0;
		fattr.f_size = inode->i_size;
		fattr.f_blksize = inode->i_blksize;
		fattr.f_ctime = inode->i_ctime;
		fattr.f_mtime = inode->i_mtime;
		fattr.f_atime = inode->i_atime;

		if ((attr->ia_valid & ATTR_CTIME) != 0)
			fattr.f_ctime = attr->ia_ctime;

		if ((attr->ia_valid & ATTR_MTIME) != 0)
			fattr.f_mtime = attr->ia_mtime;

		if ((attr->ia_valid & ATTR_ATIME) != 0)
			fattr.f_atime = attr->ia_atime;

		error = smb_proc_setattr(server, dentry, &fattr);
		if (error)
			goto out;
		refresh = 1;
	}
	error = 0;

out:
	if (refresh)
	{
		/*
		 * N.B. Currently we're only using the dir cache for
		 * file names, so we don't need to invalidate here.
		 */
#if 0
		smb_invalid_dir_cache(dentry->d_parent->d_inode);
#endif
		smb_refresh_inode(inode);
	}
	return error;
}

#ifdef DEBUG_SMB_MALLOC
int smb_malloced;
int smb_current_kmalloced;
int smb_current_vmalloced;
#endif

static struct file_system_type smb_fs_type = {
	"smbfs",
	0 /* FS_NO_DCACHE doesn't work correctly */,
	smb_read_super,
	NULL
};

__initfunc(int init_smb_fs(void))
{
	return register_filesystem(&smb_fs_type);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int
init_module(void)
{
	pr_debug("smbfs: init_module called\n");

#ifdef DEBUG_SMB_MALLOC
	smb_malloced = 0;
	smb_current_kmalloced = 0;
	smb_current_vmalloced = 0;
#endif

	read_semaphore = MUTEX;

	return init_smb_fs();
}

void
cleanup_module(void)
{
	pr_debug("smbfs: cleanup_module called\n");
	unregister_filesystem(&smb_fs_type);
#ifdef DEBUG_SMB_MALLOC
	printk(KERN_DEBUG "smb_malloced: %d\n", smb_malloced);
	printk(KERN_DEBUG "smb_current_kmalloced: %d\n",smb_current_kmalloced);
	printk(KERN_DEBUG "smb_current_vmalloced: %d\n",smb_current_vmalloced);
#endif
}

#endif
