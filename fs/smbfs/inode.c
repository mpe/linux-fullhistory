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
#include <linux/file.h>
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
	static unsigned long ino = 2;

	if (ino + 2*n < ino)
	{
		/* wrap around */
		ino = 2;
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

/*
 * Copy the inode data to a smb_fattr structure.
 */
void
smb_get_inode_attr(struct inode *inode, struct smb_fattr *fattr)
{
	memset(fattr, 0, sizeof(struct smb_fattr));
	fattr->f_mode	= inode->i_mode;
	fattr->f_nlink	= inode->i_nlink;
	fattr->f_ino	= inode->i_ino;
	fattr->f_uid	= inode->i_uid;
	fattr->f_gid	= inode->i_gid;
	fattr->f_rdev	= inode->i_rdev;
	fattr->f_size	= inode->i_size;
	fattr->f_mtime	= inode->i_mtime;
	fattr->f_ctime	= inode->i_ctime;
	fattr->f_atime	= inode->i_atime;
	fattr->f_blksize= inode->i_blksize;
	fattr->f_blocks	= inode->i_blocks;

	fattr->attr	= inode->u.smbfs_i.attr;
	/*
	 * Keep the attributes in sync with the inode permissions.
	 */
	if (fattr->f_mode & S_IWUSR)
		fattr->attr &= ~aRONLY;
	else
		fattr->attr |= aRONLY;
}

static void
smb_set_inode_attr(struct inode *inode, struct smb_fattr *fattr)
{
	inode->i_mode	= fattr->f_mode;
	inode->i_nlink	= fattr->f_nlink;
	inode->i_uid	= fattr->f_uid;
	inode->i_gid	= fattr->f_gid;
	inode->i_rdev	= fattr->f_rdev;
	inode->i_ctime	= fattr->f_ctime;
	inode->i_blksize= fattr->f_blksize;
	inode->i_blocks = fattr->f_blocks;
	/*
	 * Don't change the size and mtime/atime fields
	 * if we're writing to the file.
	 */
	if (!(inode->u.smbfs_i.cache_valid & SMB_F_LOCALWRITE))
	{
		inode->i_size  = fattr->f_size;
		inode->i_mtime = fattr->f_mtime;
		inode->i_atime = fattr->f_atime;
	}

	inode->u.smbfs_i.attr = fattr->attr;
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
 * This is called to update the inode attributes after
 * we've made changes to a file or directory.
 */
static int
smb_refresh_inode(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int error;
	struct smb_fattr fattr;

	error = smb_proc_getattr(dentry, &fattr);
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
			 * To limit damage, mark the inode as bad so that
			 * subsequent lookup validations will fail.
			 */
#ifdef SMBFS_PARANOIA
printk("smb_refresh_inode: %s/%s changed mode, %07o to %07o\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
inode->i_mode, fattr.f_mode);
#endif
			fattr.f_mode = inode->i_mode; /* save mode */
			make_bad_inode(inode);
			inode->i_mode = fattr.f_mode; /* restore mode */
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
	return error;
}

/*
 * This is called when we want to check whether the inode
 * has changed on the server.  If it has changed, we must
 * invalidate our local caches.
 */
int
smb_revalidate_inode(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	time_t last_time;
	int error = 0;

	pr_debug("smb_revalidate_inode\n");
	/*
	 * If this is a file opened with write permissions,
	 * the inode will be up-to-date.
	 */
	if (S_ISREG(inode->i_mode) && smb_is_open(inode))
	{
		if (inode->u.smbfs_i.access != SMB_O_RDONLY)
			goto out;
	}

	/*
	 * Check whether we've recently refreshed the inode.
	 */
	if (time_before(jiffies, inode->u.smbfs_i.oldmtime + HZ/10))
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
	error = smb_refresh_inode(dentry);
	if (error || inode->i_mtime != last_time)
	{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_revalidate: %s/%s changed, old=%ld, new=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
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
 * This routine is called for every iput(). We clear i_nlink
 * on the last use to force a call to delete_inode.
 */
static void
smb_put_inode(struct inode *ino)
{
	pr_debug("smb_put_inode: count = %d\n", ino->i_count);
	if (ino->i_count == 1)
		ino->i_nlink = 0;
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

	if (server->sock_file) {
		smb_proc_disconnect(server);
		smb_dont_catch_keepalive(server);
		fput(server->sock_file);
	}

	if (server->conn_pid)
	       kill_proc(server->conn_pid, SIGTERM, 1);

	kfree(server->mnt);
	kfree(sb->u.smbfs_sb.temp_buf);
	if (server->packet)
		smb_vfree(server->packet);

	MOD_DEC_USE_COUNT;
}

struct super_block *
smb_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct smb_mount_data *mnt;
	struct inode *root_inode;
	struct smb_fattr root;

	MOD_INC_USE_COUNT;

	if (!raw_data)
		goto out_no_data;
	if (((struct smb_mount_data *) raw_data)->version != SMB_MOUNT_VERSION)
		goto out_wrong_data;

	lock_super(sb);

	sb->s_blocksize = 1024;	/* Eh...  Is this correct? */
	sb->s_blocksize_bits = 10;
	sb->s_magic = SMB_SUPER_MAGIC;
	sb->s_flags = 0;
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

	/* Allocate the global temp buffer */
	sb->u.smbfs_sb.temp_buf = kmalloc(SMB_MAXPATHLEN + 20, GFP_KERNEL);
	if (!sb->u.smbfs_sb.temp_buf)
		goto out_no_temp;

	/* Allocate the mount data structure */
	mnt = kmalloc(sizeof(struct smb_mount_data), GFP_KERNEL);
	if (!mnt)
		goto out_no_mount;
	*mnt = *((struct smb_mount_data *) raw_data);
	/* ** temp ** pass config flags in file mode */
	mnt->version = (mnt->file_mode >> 9);
#ifdef CONFIG_SMB_WIN95
	mnt->version |= SMB_FIX_WIN95;
#endif
	mnt->file_mode &= (S_IRWXU | S_IRWXG | S_IRWXO);
	mnt->file_mode |= S_IFREG;
	mnt->dir_mode  &= (S_IRWXU | S_IRWXG | S_IRWXO);
	mnt->dir_mode  |= S_IFDIR;
	sb->u.smbfs_sb.mnt = mnt;
	/*
	 * Display the enabled options
	 */
	if (mnt->version & SMB_FIX_WIN95)
		printk("SMBFS: Win 95 bug fixes enabled\n");
	if (mnt->version & SMB_FIX_OLDATTR)
		printk("SMBFS: Using core getattr (Win 95 speedup)\n");
	else if (mnt->version & SMB_FIX_DIRATTR)
		printk("SMBFS: Using dir ff getattr\n");

	/*
	 * Keep the super block locked while we get the root inode.
	 */
	smb_init_root_dirent(&(sb->u.smbfs_sb), &root);
	root_inode = smb_iget(sb, &root);
	if (!root_inode)
		goto out_no_root;

	sb->s_root = d_alloc_root(root_inode, NULL);
	if (!sb->s_root)
		goto out_no_root;

	unlock_super(sb);
	return sb;

out_no_root:
	iput(root_inode);
	kfree(sb->u.smbfs_sb.mnt);
out_no_mount:
	kfree(sb->u.smbfs_sb.temp_buf);
out_no_temp:
	smb_vfree(sb->u.smbfs_sb.packet);
out_no_mem:
	printk(KERN_ERR "smb_read_super: allocation failure\n");
	unlock_super(sb);
	goto out_fail;
out_wrong_data:
	printk(KERN_ERR "SMBFS: need mount version %d\n", SMB_MOUNT_VERSION);
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
smb_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct smb_sb_info *server = server_from_dentry(dentry);
	unsigned int mask = (S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO);
	int error, changed, refresh = 0;
	struct smb_fattr fattr;

	error = smb_revalidate_inode(dentry);
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
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_notify_change: changing %s/%s, old size=%ld, new size=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
(long) inode->i_size, (long) attr->ia_size);
#endif
		error = smb_open(dentry, O_WRONLY);
		if (error)
			goto out;
		error = smb_proc_trunc(server, inode->u.smbfs_i.fileid,
					 attr->ia_size);
		if (error)
			goto out;
		/*
		 * We don't implement an i_op->truncate operation,
		 * so we have to update the page cache here.
		 */
		if (attr->ia_size < inode->i_size)
		{
			truncate_inode_pages(inode, attr->ia_size);
			inode->i_size = attr->ia_size;
		}
		refresh = 1;
	}

	/*
	 * Initialize the fattr and check for changed fields.
	 * Note: CTIME under SMB is creation time rather than
	 * change time, so we don't attempt to change it.
	 */
	smb_get_inode_attr(inode, &fattr);

	changed = 0;
	if ((attr->ia_valid & ATTR_MTIME) != 0)
	{
		fattr.f_mtime = attr->ia_mtime;
		changed = 1;
	}
	if ((attr->ia_valid & ATTR_ATIME) != 0)
	{
		fattr.f_atime = attr->ia_atime;
		/* Earlier protocols don't have an access time */
		if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2)
			changed = 1;
	}
	if (changed)
	{
		error = smb_proc_settime(dentry, &fattr);
		if (error)
			goto out;
		refresh = 1;
	}

	/*
	 * Check for mode changes ... we're extremely limited in
	 * what can be set for SMB servers: just the read-only bit.
	 */
	if ((attr->ia_valid & ATTR_MODE) != 0)
	{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_notify_change: %s/%s mode change, old=%x, new=%lx\n",
dentry->d_parent->d_name.name, dentry->d_name.name, fattr.f_mode,attr->ia_mode);
#endif
		changed = 0;
		if (attr->ia_mode & S_IWUSR)
		{
			if (fattr.attr & aRONLY)
			{
				fattr.attr &= ~aRONLY;
				changed = 1;
			}
		} else
		{
			if (!(fattr.attr & aRONLY))
			{
				fattr.attr |= aRONLY;
				changed = 1;
			}
		}
		if (changed)
		{
			error = smb_proc_setattr(dentry, &fattr);
			if (error)
				goto out;
			refresh = 1;
		}
	}
	error = 0;

out:
	if (refresh)
		smb_refresh_inode(dentry);
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
