/*
 *  inode.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
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

extern int close_fp(struct file *filp);

static void smb_put_inode(struct inode *);
static void smb_read_inode(struct inode *);
static void smb_put_super(struct super_block *);
static void smb_statfs(struct super_block *, struct statfs *, int bufsiz);

static struct super_operations smb_sops =
{
	smb_read_inode,		/* read inode */
	smb_notify_change,	/* notify change */
	NULL,			/* write inode */
	smb_put_inode,		/* put inode */
	smb_put_super,		/* put superblock */
	NULL,			/* write superblock */
	smb_statfs,		/* stat filesystem */
	NULL
};

/* smb_read_inode: Called from iget, it only traverses the allocated
   smb_inode_info's and initializes the inode from the data found
   there.  It does not allocate or deallocate anything. */

static void
smb_read_inode(struct inode *inode)
{
	/* Our task should be extremely simple here. We only have to
	   look up the information somebody else (smb_iget) put into
	   the inode tree. */
	struct smb_server *server = SMB_SERVER(inode);
	struct smb_inode_info *inode_info
	= smb_find_inode(server, inode->i_ino);

	if (inode_info == NULL)
	{
		/* Ok, now we're in trouble. The inode info is not
		   there. What should we do now??? */
		printk("smb_read_inode: inode info not found\n");
		return;
	}
	inode_info->state = SMB_INODE_VALID;

	SMB_INOP(inode) = inode_info;
	inode->i_mode = inode_info->finfo.f_mode;
	inode->i_nlink = inode_info->finfo.f_nlink;
	inode->i_uid = inode_info->finfo.f_uid;
	inode->i_gid = inode_info->finfo.f_gid;
	inode->i_rdev = inode_info->finfo.f_rdev;
	inode->i_size = inode_info->finfo.f_size;
	inode->i_mtime = inode_info->finfo.f_mtime;
	inode->i_ctime = inode_info->finfo.f_ctime;
	inode->i_atime = inode_info->finfo.f_atime;
	inode->i_blksize = inode_info->finfo.f_blksize;
	inode->i_blocks = inode_info->finfo.f_blocks;

	if (S_ISREG(inode->i_mode))
	{
		inode->i_op = &smb_file_inode_operations;
	} else if (S_ISDIR(inode->i_mode))
	{
		inode->i_op = &smb_dir_inode_operations;
	} else
	{
		inode->i_op = NULL;
	}
}

static void
smb_put_inode(struct inode *inode)
{
	struct smb_dirent *finfo = SMB_FINFO(inode);
	struct smb_server *server = SMB_SERVER(inode);
	struct smb_inode_info *info = SMB_INOP(inode);

	if (S_ISDIR(inode->i_mode))
	{
		smb_invalid_dir_cache(inode->i_ino);
	}
	if (finfo->opened != 0)
	{
		if (smb_proc_close(server, finfo->fileid, inode->i_mtime))
		{
			/* We can't do anything but complain. */
			DPRINTK("smb_put_inode: could not close\n");
		}
	}
	smb_free_inode_info(info);
	clear_inode(inode);
}

static void
smb_put_super(struct super_block *sb)
{
	struct smb_server *server = &(SMB_SBP(sb)->s_server);

	smb_proc_disconnect(server);
	smb_dont_catch_keepalive(server);
	close_fp(server->sock_file);

	lock_super(sb);

	smb_free_all_inodes(server);

	smb_vfree(server->packet);
	server->packet = NULL;

	sb->s_dev = 0;
	smb_kfree_s(SMB_SBP(sb), sizeof(struct smb_sb_info));

	unlock_super(sb);

	MOD_DEC_USE_COUNT;
}

struct smb_mount_data_v4
{
	int version;
	unsigned int fd;
	uid_t mounted_uid;
	struct sockaddr_in addr;

	char server_name[17];
	char client_name[17];
	char service[64];
	char root_path[64];

	char username[64];
	char password[64];

	unsigned short max_xmit;

	uid_t uid;
	gid_t gid;
	mode_t file_mode;
	mode_t dir_mode;
};

static int
smb_get_mount_data(struct smb_mount_data *target, void *source)
{
	struct smb_mount_data_v4 *v4 = (struct smb_mount_data_v4 *) source;
	struct smb_mount_data *cur = (struct smb_mount_data *) source;

	if (source == NULL)
	{
		return 1;
	}
	if (cur->version == SMB_MOUNT_VERSION)
	{
		memcpy(target, cur, sizeof(struct smb_mount_data));
		return 0;
	}
	if (v4->version == 4)
	{
		target->version = 5;
		target->fd = v4->fd;
		target->mounted_uid = v4->mounted_uid;
		target->addr = v4->addr;

		memcpy(target->server_name, v4->server_name, 17);
		memcpy(target->client_name, v4->client_name, 17);
		memcpy(target->service, v4->service, 64);
		memcpy(target->root_path, v4->root_path, 64);
		memcpy(target->username, v4->username, 64);
		memcpy(target->password, v4->password, 64);

		target->max_xmit = v4->max_xmit;
		target->uid = v4->uid;
		target->gid = v4->gid;
		target->file_mode = v4->file_mode;
		target->dir_mode = v4->dir_mode;

		memset(target->domain, 0, 64);
		strcpy(target->domain, "?");
		return 0;
	}
	return 1;
}

struct super_block *
smb_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct smb_mount_data data;
	struct smb_server *server;
	struct smb_sb_info *smb_sb;
	unsigned int fd;
	struct file *filp;
	kdev_t dev = sb->s_dev;
	int error;

	if (smb_get_mount_data(&data, raw_data) != 0)
	{
		printk("smb_read_super: wrong data argument\n");
		sb->s_dev = 0;
		return NULL;
	}
	fd = data.fd;
	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
	{
		printk("smb_read_super: invalid file descriptor\n");
		sb->s_dev = 0;
		return NULL;
	}
	if (!S_ISSOCK(filp->f_dentry->d_inode->i_mode))
	{
		printk("smb_read_super: not a socket!\n");
		sb->s_dev = 0;
		return NULL;
	}
	/* We must malloc our own super-block info */
	smb_sb = (struct smb_sb_info *) smb_kmalloc(sizeof(struct smb_sb_info),
						    GFP_KERNEL);

	if (smb_sb == NULL)
	{
		printk("smb_read_super: could not alloc smb_sb_info\n");
		return NULL;
	}
	filp->f_count++;

	lock_super(sb);

	SMB_SBP(sb) = smb_sb;

	sb->s_blocksize = 1024;	/* Eh...  Is this correct? */
	sb->s_blocksize_bits = 10;
	sb->s_magic = SMB_SUPER_MAGIC;
	sb->s_dev = dev;
	sb->s_op = &smb_sops;

	server = &(SMB_SBP(sb)->s_server);
	server->sock_file = filp;
	server->lock = 0;
	server->wait = NULL;
	server->packet = NULL;
	server->max_xmit = data.max_xmit;
	if (server->max_xmit <= 0)
	{
		server->max_xmit = SMB_DEF_MAX_XMIT;
	}
	server->tid = 0;
	server->pid = current->pid;
	server->mid = current->pid + 20;

	server->m = data;
	server->m.file_mode = (server->m.file_mode &
			       (S_IRWXU | S_IRWXG | S_IRWXO)) | S_IFREG;
	server->m.dir_mode = (server->m.dir_mode &
			      (S_IRWXU | S_IRWXG | S_IRWXO)) | S_IFDIR;

	smb_init_root(server);

	error = smb_proc_connect(server);

	unlock_super(sb);

	if (error < 0)
	{
		sb->s_dev = 0;
		DPRINTK("smb_read_super: Failed connection, bailing out "
			"(error = %d).\n", -error);
		goto fail;
	}
	if (server->protocol >= PROTOCOL_LANMAN2)
	{
		server->case_handling = CASE_DEFAULT;
	} else
	{
		server->case_handling = CASE_LOWER;
	}

	if ((error = smb_proc_dskattr(sb, &(SMB_SBP(sb)->s_attr))) < 0)
	{
		sb->s_dev = 0;
		printk("smb_read_super: could not get super block "
		       "attributes\n");
		goto fail;
	}
	smb_init_root_dirent(server, &(server->root.finfo));

	if (!(sb->s_root = d_alloc_root(iget(sb, 
	                                smb_info_ino(&(server->root))),NULL)))
	{
		sb->s_dev = 0;
		printk("smb_read_super: get root inode failed\n");
		goto fail;
	}
	MOD_INC_USE_COUNT;
	return sb;

      fail:
	if (server->packet != NULL)
	{
		smb_vfree(server->packet);
		server->packet = NULL;
	}
	put_filp(filp);
	smb_dont_catch_keepalive(server);
	smb_kfree_s(SMB_SBP(sb), sizeof(struct smb_sb_info));
	return NULL;
}

static void
smb_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	int error;
	struct smb_dskattr attr;
	struct statfs tmp;

	error = smb_proc_dskattr(sb, &attr);

	if (error)
	{
		printk("smb_statfs: dskattr error = %d\n", -error);
		attr.total = attr.allocblocks = attr.blocksize =
		    attr.free = 0;
	}
	tmp.f_type = SMB_SUPER_MAGIC;
	tmp.f_bsize = attr.blocksize * attr.allocblocks;
	tmp.f_blocks = attr.total;
	tmp.f_bfree = attr.free;
	tmp.f_bavail = attr.free;
	tmp.f_files = -1;
	tmp.f_ffree = -1;
	tmp.f_namelen = SMB_MAXPATHLEN;
	copy_to_user(buf, &tmp, bufsiz);
}

int
smb_notify_change(struct inode *inode, struct iattr *attr)
{
	int error = 0;

	if ((error = inode_change_ok(inode, attr)) < 0)
		return error;

	if (((attr->ia_valid & ATTR_UID) &&
	     (attr->ia_uid != SMB_SERVER(inode)->m.uid)))
		return -EPERM;

	if (((attr->ia_valid & ATTR_GID) &&
	     (attr->ia_uid != SMB_SERVER(inode)->m.gid)))
		return -EPERM;

	if (((attr->ia_valid & ATTR_MODE) &&
	(attr->ia_mode & ~(S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO))))
		return -EPERM;

	if ((attr->ia_valid & ATTR_SIZE) != 0)
	{

		if ((error = smb_make_open(inode, O_WRONLY)) < 0)
			goto fail;

		if ((error = smb_proc_trunc(SMB_SERVER(inode),
					    SMB_FINFO(inode)->fileid,
					    attr->ia_size)) < 0)
			goto fail;

	}
	if ((attr->ia_valid & (ATTR_CTIME | ATTR_MTIME | ATTR_ATIME)) != 0)
	{

		struct smb_dirent finfo;

		finfo.attr = 0;
		finfo.f_size = inode->i_size;
		finfo.f_blksize = inode->i_blksize;

		if ((attr->ia_valid & ATTR_CTIME) != 0)
			finfo.f_ctime = attr->ia_ctime;
		else
			finfo.f_ctime = inode->i_ctime;

		if ((attr->ia_valid & ATTR_MTIME) != 0)
			finfo.f_mtime = attr->ia_mtime;
		else
			finfo.f_mtime = inode->i_mtime;

		if ((attr->ia_valid & ATTR_ATIME) != 0)
			finfo.f_atime = attr->ia_atime;
		else
			finfo.f_atime = inode->i_atime;

		if ((error = smb_proc_setattr(SMB_SERVER(inode),
					      inode, &finfo)) >= 0)
		{
			inode->i_ctime = finfo.f_ctime;
			inode->i_mtime = finfo.f_mtime;
			inode->i_atime = finfo.f_atime;
		}
	}
      fail:
	smb_invalid_dir_cache(smb_info_ino(SMB_INOP(inode)->dir));

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
	DPRINTK("smbfs: init_module called\n");

#ifdef DEBUG_SMB_MALLOC
	smb_malloced = 0;
	smb_current_kmalloced = 0;
	smb_current_vmalloced = 0;
#endif

	smb_init_dir_cache();

	return init_smb_fs();
}

void
cleanup_module(void)
{
	DPRINTK("smbfs: cleanup_module called\n");
	smb_free_dir_cache();
	unregister_filesystem(&smb_fs_type);
#ifdef DEBUG_SMB_MALLOC
	printk("smb_malloced: %d\n", smb_malloced);
	printk("smb_current_kmalloced: %d\n", smb_current_kmalloced);
	printk("smb_current_vmalloced: %d\n", smb_current_vmalloced);
#endif
}

#endif
