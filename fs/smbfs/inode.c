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
#include <linux/smb_fs.h>
#include <linux/smbno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#define SB_of(server) ((struct super_block *) ((char *)(server) - \
	(unsigned long)(&((struct super_block *)0)->u.smbfs_sb)))

extern int close_fp(struct file *filp);

static void smb_put_inode(struct inode *);
static void smb_delete_inode(struct inode *);
static void smb_read_inode(struct inode *);
static void smb_put_super(struct super_block *);
static int smb_statfs(struct super_block *, struct statfs *, int);

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

static struct smb_fattr *read_fattr;
static struct semaphore read_semaphore = MUTEX;

struct inode *
smb_iget(struct super_block *sb, struct smb_fattr *fattr)
{
	struct inode *result;

	pr_debug("smb_iget: %p\n", fattr);

	down(&read_semaphore);
	read_fattr = fattr;
	result = iget(sb, fattr->f_ino);
	up(&read_semaphore);
	return result;
}

static void
smb_set_inode_attr(struct inode *inode, struct smb_fattr *fattr)
{
	inode->i_dev	= inode->i_sb->s_dev;
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
}

static void
smb_read_inode(struct inode *inode)
{
	pr_debug("smb_iget: %p\n", read_fattr);

	if ((atomic_read(&read_semaphore.count) == 1) ||
	    (inode->i_ino != read_fattr->f_ino))
	{
		printk("smb_read_inode called from invalid point\n");
		return;
	}

	smb_set_inode_attr(inode, read_fattr);
	memset(&(inode->u.smbfs_i), 0, sizeof(inode->u.smbfs_i));

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
	printk("smb_invalidate_inodes\n");
	shrink_dcache(); /* should shrink only this sb */
	invalidate_inodes(SB_of(server));
}

int
smb_revalidate_inode(struct inode *ino)
{
	struct dentry * dentry = ino->u.smbfs_i.dentry;
	int error = 0;

	pr_debug("smb_revalidate_inode\n");
	if (!ino)
		goto bad_no_inode;
	dentry = ino->u.smbfs_i.dentry;
#if 0
	if (dentry)
	{
		printk("smb_revalidate: checking %s/%s\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
	}
#endif
out:
	return error;

bad_no_inode:
	printk("smb_revalidate: no inode!\n");
	error = -EINVAL;
	goto out;
}

int
smb_refresh_inode(struct inode *ino)
{
	struct dentry * dentry = ino->u.smbfs_i.dentry;
	struct smb_fattr fattr;
	int error;

	pr_debug("smb_refresh_inode\n");
	error = -EIO;
	if (!dentry) {
		printk("smb_refresh_inode: no dentry, can't refresh\n");
		goto out;
	}

	/* N.B. Should check for changes of important fields! cf. NFS */
	error = smb_proc_getattr(dentry->d_parent, &(dentry->d_name), &fattr);
	if (!error)
	{
		smb_set_inode_attr(ino, &fattr);
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
	struct dentry * dentry;
	pr_debug("smb_put_inode: count = %d\n", ino->i_count);

	if (ino->i_count > 1) {
		/*
		 * Check whether the dentry still holds this inode. 
		 * This looks scary, but should work ... d_inode is 
		 * cleared before iput() in the dcache. 
		 */
		dentry = (struct dentry *) ino->u.smbfs_i.dentry;
		if (dentry && dentry->d_inode != ino) {
			ino->u.smbfs_i.dentry = NULL;
#if 1
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

	if (server->packet)
		smb_vfree(server->packet);
	sb->s_dev = 0;

	unlock_super(sb);

	MOD_DEC_USE_COUNT;
}

struct super_block *
smb_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct smb_mount_data *data = (struct smb_mount_data *)raw_data;
	struct smb_fattr root;
	kdev_t dev = sb->s_dev;
	unsigned char *packet;
	struct inode *root_inode;
	struct dentry *dentry;

	MOD_INC_USE_COUNT;

	if (!data)
		goto out_no_data;
		
	if (data->version != SMB_MOUNT_VERSION)
		goto out_wrong_data;

	packet = smb_vmalloc(SMB_INITIAL_PACKET_SIZE);	
	if (!packet)
		goto out_no_mem;

	lock_super(sb);

	sb->s_blocksize = 1024;	/* Eh...  Is this correct? */
	sb->s_blocksize_bits = 10;
	sb->s_magic = SMB_SUPER_MAGIC;
	sb->s_dev = dev;
	sb->s_op = &smb_sops;

	sb->u.smbfs_sb.sock_file = NULL;
	sb->u.smbfs_sb.sem = MUTEX;
	sb->u.smbfs_sb.conn_pid = 0;
	sb->u.smbfs_sb.packet = packet;
	sb->u.smbfs_sb.packet_size = SMB_INITIAL_PACKET_SIZE;
	sb->u.smbfs_sb.generation = 0;

	sb->u.smbfs_sb.m = *data;
	sb->u.smbfs_sb.m.file_mode = (sb->u.smbfs_sb.m.file_mode &
				      (S_IRWXU | S_IRWXG | S_IRWXO)) | S_IFREG;
	sb->u.smbfs_sb.m.dir_mode = (sb->u.smbfs_sb.m.dir_mode &
				     (S_IRWXU | S_IRWXG | S_IRWXO)) | S_IFDIR;

	smb_init_root_dirent(&(sb->u.smbfs_sb), &root);

	sb->s_root = NULL;
	unlock_super(sb);

	root_inode = smb_iget(sb, &root);
	if (!root_inode)
		goto out_no_root;
	dentry = d_alloc_root(root_inode, NULL);
	if (!dentry)
		goto out_no_root;
	root_inode->u.smbfs_i.dentry = dentry;
	sb->s_root = dentry;
	return sb;

out_no_data:
	printk("smb_read_super: missing data argument\n");
	goto out;
out_wrong_data:
	printk(KERN_ERR "smb_read_super: wrong data argument."
	       " Recompile smbmount.\n");
	goto out;
out_no_mem:
	pr_debug("smb_read_super: could not alloc packet\n");
	goto out;
out_no_root:
	sb->s_dev = 0; /* iput() might block */
	printk(KERN_ERR "smb_read_super: get root inode failed\n");
	iput(root_inode);
	smb_vfree(packet);
out:
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
	struct dentry *dentry = inode->u.smbfs_i.dentry;
	int error;

	error = -EIO;
	if (!dentry)
	{
		printk("smb_notify_change: no dentry for inode!\n");
		goto out;
	}

	if ((error = inode_change_ok(inode, attr)) < 0)
		goto out;

	error = -EPERM;
	if (((attr->ia_valid & ATTR_UID) &&
	     (attr->ia_uid != SMB_SERVER(inode)->m.uid)))
		goto out;

	if (((attr->ia_valid & ATTR_GID) &&
	     (attr->ia_uid != SMB_SERVER(inode)->m.gid)))
		goto out;

	if (((attr->ia_valid & ATTR_MODE) &&
	(attr->ia_mode & ~(S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO))))
		goto out;

	/*
	 * Assume success and invalidate the parent's dir cache
	 */ 
	smb_invalid_dir_cache(dentry->d_parent->d_inode);

	if ((attr->ia_valid & ATTR_SIZE) != 0)
	{
		if ((error = smb_open(dentry, O_WRONLY)) < 0)
			goto out;

		if ((error = smb_proc_trunc(SMB_SERVER(inode),
					    inode->u.smbfs_i.fileid,
					    attr->ia_size)) < 0)
			goto out;
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

		error = smb_proc_setattr(SMB_SERVER(inode), dentry, &fattr);
		if (error >= 0)
		{
			inode->i_ctime = fattr.f_ctime;
			inode->i_mtime = fattr.f_mtime;
			inode->i_atime = fattr.f_atime;
		}
	}
out:
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

	smb_init_dir_cache();
	read_semaphore = MUTEX;

	return init_smb_fs();
}

void
cleanup_module(void)
{
	pr_debug("smbfs: cleanup_module called\n");
	smb_free_dir_cache();
	unregister_filesystem(&smb_fs_type);
#ifdef DEBUG_SMB_MALLOC
	printk(KERN_DEBUG "smb_malloced: %d\n", smb_malloced);
	printk(KERN_DEBUG "smb_current_kmalloced: %d\n",smb_current_kmalloced);
	printk(KERN_DEBUG "smb_current_vmalloced: %d\n",smb_current_vmalloced);
#endif
}

#endif
