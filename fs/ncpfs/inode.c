/*
 *  inode.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include <linux/sched.h>
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

#include <linux/ncp_fs.h>
#include "ncplib_kernel.h"

static void ncp_read_inode(struct inode *);
static void ncp_put_inode(struct inode *);
static void ncp_delete_inode(struct inode *);
static void ncp_put_super(struct super_block *);
static int  ncp_statfs(struct super_block *, struct statfs *, int);

static struct super_operations ncp_sops =
{
	ncp_read_inode,		/* read inode */
	NULL,			/* write inode */
	ncp_put_inode,		/* put inode */
	ncp_delete_inode,       /* delete inode */
	ncp_notify_change,	/* notify change */
	ncp_put_super,		/* put superblock */
	NULL,			/* write superblock */
	ncp_statfs,		/* stat filesystem */
	NULL                    /* remount */
};

extern struct dentry_operations ncp_dentry_operations;

static struct nw_file_info *read_nwinfo = NULL;
static struct semaphore read_sem = MUTEX;

/*
 * Fill in the ncpfs-specific information in the inode.
 */
void ncp_update_inode(struct inode *inode, struct nw_file_info *nwinfo)
{
	NCP_FINFO(inode)->DosDirNum = nwinfo->i.DosDirNum;
	NCP_FINFO(inode)->dirEntNum = nwinfo->i.dirEntNum;
	NCP_FINFO(inode)->volNumber = nwinfo->i.volNumber;

	NCP_FINFO(inode)->opened = nwinfo->opened;
	NCP_FINFO(inode)->access = nwinfo->access;
	NCP_FINFO(inode)->server_file_handle = nwinfo->server_file_handle;
	memcpy(NCP_FINFO(inode)->file_handle, nwinfo->file_handle,
			sizeof(nwinfo->file_handle));
#ifdef NCPFS_DEBUG_VERBOSE
printk(KERN_DEBUG "ncp_update_inode: updated %s, volnum=%d, dirent=%u\n",
nwinfo->i.entryName, NCP_FINFO(inode)->volNumber, NCP_FINFO(inode)->dirEntNum);
#endif
}

void ncp_update_inode2(struct inode* inode, struct nw_file_info *nwinfo)
{
	struct nw_info_struct *nwi = &nwinfo->i;
	struct ncp_server *server = NCP_SERVER(inode);

	if (!NCP_FINFO(inode)->opened) {
		if (nwi->attributes & aDIR) {
			inode->i_mode = server->m.dir_mode;
			inode->i_size = 512;
		} else {
			inode->i_mode = server->m.file_mode;
			inode->i_size = le32_to_cpu(nwi->dataStreamSize);
		}
		if (nwi->attributes & aRONLY) inode->i_mode &= ~0222;
	}
	inode->i_blocks = 0;
	if ((inode->i_size)&&(inode->i_blksize)) {
		inode->i_blocks = (inode->i_size-1)/(inode->i_blksize)+1;
	}
	/* TODO: times? I'm not sure... */
	NCP_FINFO(inode)->DosDirNum = nwinfo->i.DosDirNum;
	NCP_FINFO(inode)->dirEntNum = nwinfo->i.dirEntNum;
	NCP_FINFO(inode)->volNumber = nwinfo->i.volNumber;
}

/*
 * Fill in the inode based on the nw_file_info structure.
 */
static void ncp_set_attr(struct inode *inode, struct nw_file_info *nwinfo)
{
	struct nw_info_struct *nwi = &nwinfo->i;
	struct ncp_server *server = NCP_SERVER(inode);

	if (nwi->attributes & aDIR) {
		inode->i_mode = server->m.dir_mode;
		/* for directories dataStreamSize seems to be some
		   Object ID ??? */
		inode->i_size = 512;
	} else {
		inode->i_mode = server->m.file_mode;
		inode->i_size = le32_to_cpu(nwi->dataStreamSize);
	}
	if (nwi->attributes & aRONLY) inode->i_mode &= ~0222;

	DDPRINTK(KERN_DEBUG "ncp_read_inode: inode->i_mode = %u\n", inode->i_mode);

	inode->i_nlink = 1;
	inode->i_uid = server->m.uid;
	inode->i_gid = server->m.gid;
	inode->i_blksize = 512;
	inode->i_rdev = 0;

	inode->i_blocks = 0;
	if ((inode->i_blksize != 0) && (inode->i_size != 0)) {
		inode->i_blocks =
		    (inode->i_size - 1) / inode->i_blksize + 1;
	}

	inode->i_mtime = ncp_date_dos2unix(le16_to_cpu(nwi->modifyTime),
			  		   le16_to_cpu(nwi->modifyDate));
	inode->i_ctime = ncp_date_dos2unix(le16_to_cpu(nwi->creationTime),
			    		   le16_to_cpu(nwi->creationDate));
	inode->i_atime = ncp_date_dos2unix(0,
					   le16_to_cpu(nwi->lastAccessDate));
	ncp_update_inode(inode, nwinfo);
}

/*
 * This is called from iget() with the read semaphore held. 
 * The global ncpfs_file_info structure has been set up by ncp_iget.
 */
static void ncp_read_inode(struct inode *inode)
{
	if (read_nwinfo == NULL) {
		printk(KERN_ERR "ncp_read_inode: invalid call\n");
		return;
	}

	ncp_set_attr(inode, read_nwinfo);

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &ncp_file_inode_operations;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ncp_dir_inode_operations;
	} else {
		inode->i_op = NULL;
	}
}

/*
 * Set up the ncpfs_inode_info pointer and get a new inode.
 */
struct inode * 
ncp_iget(struct super_block *sb, struct ncpfs_inode_info *info)
{
	struct inode *inode;

	if (info == NULL) {
		printk(KERN_ERR "ncp_iget: info is NULL\n");
		return NULL;
	}

	down(&read_sem);
	read_nwinfo = &info->nw_info;
	inode = iget(sb, info->ino);
	read_nwinfo = NULL;
	up(&read_sem);
	if (!inode)
		printk(KERN_ERR "ncp_iget: iget failed!\n");
	return inode;
}

static void ncp_put_inode(struct inode *inode)
{
	if (inode->i_count == 1)
		inode->i_nlink = 0;
}

static void
ncp_delete_inode(struct inode *inode)
{
	if (S_ISDIR(inode->i_mode)) {
		DDPRINTK(KERN_DEBUG "ncp_delete_inode: put directory %ld\n", inode->i_ino);
		ncp_invalid_dir_cache(inode);
	}

	if (NCP_FINFO(inode)->opened && ncp_make_closed(inode) != 0) {
		/* We can't do anything but complain. */
		printk(KERN_ERR "ncp_delete_inode: could not close\n");
	}
	clear_inode(inode);
}

static void ncp_init_root(struct ncp_server *server,
			struct ncpfs_inode_info *info)
{
	struct ncp_inode_info *root = &(server->root);
	struct nw_info_struct *i = &(root->finfo.i);
	unsigned short dummy;

	DPRINTK(KERN_DEBUG "ncp_init_root: i = %x\n", (int) i);

	i->attributes	 = aDIR;
	i->dataStreamSize= 1024;
	i->dirEntNum	 = 0;
	i->DosDirNum	 = 0;
	i->volNumber	 = NCP_NUMBER_OF_VOLUMES + 1;	/* illegal volnum */
	ncp_date_unix2dos(0, &(i->creationTime), &(i->creationDate));
	ncp_date_unix2dos(0, &(i->modifyTime  ), &(i->modifyDate));
	ncp_date_unix2dos(0, &(dummy          ), &(i->lastAccessDate));
	i->creationTime	 = le16_to_cpu(i->creationTime);
	i->creationDate	 = le16_to_cpu(i->creationDate);
	i->modifyTime	 = le16_to_cpu(i->modifyTime);
	i->modifyDate	 = le16_to_cpu(i->modifyDate);
	i->lastAccessDate= le16_to_cpu(i->lastAccessDate);
	i->nameLen	 = 0;
	i->entryName[0]  = '\0';

	root->finfo.opened= 0;
	info->ino = 2; /* tradition */
	info->nw_info = root->finfo;
	return;
}

struct super_block *
ncp_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct ncp_mount_data *data = (struct ncp_mount_data *) raw_data;
	struct ncp_server *server;
	struct file *ncp_filp;
	struct inode *root_inode;
	kdev_t dev = sb->s_dev;
	int error;
#ifdef CONFIG_NCPFS_PACKET_SIGNING
	int options;
#endif
	struct ncpfs_inode_info finfo;

	MOD_INC_USE_COUNT;
	if (data == NULL)
		goto out_no_data;
	if (data->version != NCP_MOUNT_VERSION)
		goto out_bad_mount;
	ncp_filp = fget(data->ncp_fd);
	if (!ncp_filp)
		goto out_bad_file;
	if (!S_ISSOCK(ncp_filp->f_dentry->d_inode->i_mode))
		goto out_bad_file2;

	lock_super(sb);

	sb->s_blocksize = 1024;	/* Eh...  Is this correct? */
	sb->s_blocksize_bits = 10;
	sb->s_magic = NCP_SUPER_MAGIC;
	sb->s_dev = dev;
	sb->s_op = &ncp_sops;

	/* We must malloc our own super-block info */
	server = (struct ncp_server *) ncp_kmalloc(sizeof(struct ncp_server),
						   GFP_KERNEL);
	if (server == NULL)
		goto out_no_server;
	NCP_SBP(sb) = server;

	server->ncp_filp = ncp_filp;
	server->lock = 0;
	server->wait = NULL;
	server->packet = NULL;
	server->buffer_size = 0;
	server->conn_status = 0;
	server->root_dentry = NULL;
	server->root_setuped = 0;
#ifdef CONFIG_NCPFS_PACKET_SIGNING
	server->sign_wanted = 0;
	server->sign_active = 0;
#endif
	server->auth.auth_type = NCP_AUTH_NONE;
	server->auth.object_name_len = 0;
	server->auth.object_name = NULL;
	server->auth.object_type = 0;
	server->priv.len = 0;
	server->priv.data = NULL;

	server->m = *data;
	/* Althought anything producing this is buggy, it happens
	   now because of PATH_MAX changes.. */
	if (server->m.time_out < 10) {
		server->m.time_out = 10;
		printk(KERN_INFO "You need to recompile your ncpfs utils..\n");
	}
	server->m.file_mode = (server->m.file_mode &
			       (S_IRWXU | S_IRWXG | S_IRWXO)) | S_IFREG;
	server->m.dir_mode = (server->m.dir_mode &
			      (S_IRWXU | S_IRWXG | S_IRWXO)) | S_IFDIR;

	server->packet_size = NCP_PACKET_SIZE;
	server->packet = ncp_kmalloc(NCP_PACKET_SIZE, GFP_KERNEL);
	if (server->packet == NULL)
		goto out_no_packet;

	ncp_lock_server(server);
	error = ncp_connect(server);
	ncp_unlock_server(server);
	if (error < 0)
		goto out_no_connect;
	DPRINTK(KERN_DEBUG "ncp_read_super: NCP_SBP(sb) = %x\n", (int) NCP_SBP(sb));

#ifdef CONFIG_NCPFS_PACKET_SIGNING
	if (ncp_negotiate_size_and_options(server, NCP_DEFAULT_BUFSIZE,
		NCP_DEFAULT_OPTIONS, &(server->buffer_size), &options) == 0)
	{
		if (options != NCP_DEFAULT_OPTIONS)
		{
			if (ncp_negotiate_size_and_options(server, 
				NCP_DEFAULT_BUFSIZE,
				options & 2, 
				&(server->buffer_size), &options) != 0)
				
			{
				goto out_no_bufsize;
			}
		}
		if (options & 2)
			server->sign_wanted = 1;
	}
	else 
#endif	/* CONFIG_NCPFS_PACKET_SIGNING */
	if (ncp_negotiate_buffersize(server, NCP_DEFAULT_BUFSIZE,
  				     &(server->buffer_size)) != 0)
		goto out_no_bufsize;
	DPRINTK(KERN_DEBUG "ncpfs: bufsize = %d\n", server->buffer_size);

	ncp_init_root(server, &finfo);
	server->name_space[finfo.nw_info.i.volNumber] = NW_NS_DOS;
        root_inode = ncp_iget(sb, &finfo);
        if (!root_inode)
		goto out_no_root;
	DPRINTK(KERN_DEBUG "ncp_read_super: root vol=%d\n", NCP_FINFO(root_inode)->volNumber);
        server->root_dentry = sb->s_root = d_alloc_root(root_inode, NULL);
        if (!sb->s_root)
		goto out_no_root;
	server->root_dentry->d_op = &ncp_dentry_operations;
	unlock_super(sb);
	return sb;

out_no_root:
	printk(KERN_ERR "ncp_read_super: get root inode failed\n");
	iput(root_inode);
	goto out_disconnect;
out_no_bufsize:
	printk(KERN_ERR "ncp_read_super: could not get bufsize\n");
out_disconnect:
	ncp_lock_server(server);
	ncp_disconnect(server);
	ncp_unlock_server(server);
	goto out_free_packet;
out_no_connect:
	printk(KERN_ERR "ncp_read_super: Failed connection, error=%d\n", error);
out_free_packet:
	ncp_kfree_s(server->packet, server->packet_size);
	goto out_free_server;
out_no_packet:
	printk(KERN_ERR "ncp_read_super: could not alloc packet\n");
out_free_server:
	ncp_kfree_s(NCP_SBP(sb), sizeof(struct ncp_server));
	goto out_unlock;
out_no_server:
	printk(KERN_ERR "ncp_read_super: could not alloc ncp_server\n");
out_unlock:
	/* 23/12/1998 Marcin Dalecki <dalecki@cs.net.pl>:
	 * 
	 * The previously used put_filp(ncp_filp); was bogous, since
	 * it doesn't proper unlocking.
	 */
	fput(ncp_filp);
	unlock_super(sb);
	goto out;

out_bad_file2:
	fput(ncp_filp);
out_bad_file:
	printk(KERN_ERR "ncp_read_super: invalid ncp socket\n");
	goto out;
out_bad_mount:
	printk(KERN_INFO "ncp_read_super: kernel requires mount version %d\n",
		NCP_MOUNT_VERSION);
	goto out;
out_no_data:
	printk(KERN_ERR "ncp_read_super: missing data argument\n");
out:
	sb->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return NULL;
}

static void ncp_put_super(struct super_block *sb)
{
	struct ncp_server *server = NCP_SBP(sb);

	ncp_lock_server(server);
	ncp_disconnect(server);
	ncp_unlock_server(server);

	fput(server->ncp_filp);
	kill_proc(server->m.wdog_pid, SIGTERM, 1);

	if (server->priv.data) 
		ncp_kfree_s(server->priv.data, server->priv.len);
	if (server->auth.object_name)
		ncp_kfree_s(server->auth.object_name, server->auth.object_name_len);
	ncp_kfree_s(server->packet, server->packet_size);

	ncp_kfree_s(NCP_SBP(sb), sizeof(struct ncp_server));

	MOD_DEC_USE_COUNT;
}

static int ncp_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;

	/* We cannot say how much disk space is left on a mounted
	   NetWare Server, because free space is distributed over
	   volumes, and the current user might have disk quotas. So
	   free space is not that simple to determine. Our decision
	   here is to err conservatively. */

	tmp.f_type = NCP_SUPER_MAGIC;
	tmp.f_bsize = 512;
	tmp.f_blocks = 0;
	tmp.f_bfree = 0;
	tmp.f_bavail = 0;
	tmp.f_files = -1;
	tmp.f_ffree = -1;
	tmp.f_namelen = 12;
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}

int ncp_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int result = 0;
	int info_mask;
	struct nw_modify_dos_info info;

	result = -EIO;
	if (!ncp_conn_valid(NCP_SERVER(inode)))
		goto out;

	result = inode_change_ok(inode, attr);
	if (result < 0)
		goto out;

	result = -EPERM;
	if (((attr->ia_valid & ATTR_UID) &&
	     (attr->ia_uid != NCP_SERVER(inode)->m.uid)))
		goto out;

	if (((attr->ia_valid & ATTR_GID) &&
	     (attr->ia_uid != NCP_SERVER(inode)->m.gid)))
		goto out;

	if (((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode &
	      ~(S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO))))
		goto out;

	info_mask = 0;
	memset(&info, 0, sizeof(info));

#if 1 
        if ((attr->ia_valid & ATTR_MODE) != 0)
        {
                if (!S_ISREG(inode->i_mode))
                {
                        return -EPERM;
                }
                else
                {
			umode_t newmode;

                        info_mask |= DM_ATTRIBUTES;
                        newmode=attr->ia_mode;
                        newmode &= NCP_SERVER(inode)->m.file_mode;

                        if (newmode & 0222) /* any write bit set */
                        {
                                info.attributes &= ~0x60001;
                        }
                        else
                        {
                                info.attributes |= 0x60001;
                        }
                }
        }
#endif

	if ((attr->ia_valid & ATTR_CTIME) != 0) {
		info_mask |= (DM_CREATE_TIME | DM_CREATE_DATE);
		ncp_date_unix2dos(attr->ia_ctime,
			     &(info.creationTime), &(info.creationDate));
		info.creationTime = le16_to_cpu(info.creationTime);
		info.creationDate = le16_to_cpu(info.creationDate);
	}
	if ((attr->ia_valid & ATTR_MTIME) != 0) {
		info_mask |= (DM_MODIFY_TIME | DM_MODIFY_DATE);
		ncp_date_unix2dos(attr->ia_mtime,
				  &(info.modifyTime), &(info.modifyDate));
		info.modifyTime = le16_to_cpu(info.modifyTime);
		info.modifyDate = le16_to_cpu(info.modifyDate);
	}
	if ((attr->ia_valid & ATTR_ATIME) != 0) {
		__u16 dummy;
		info_mask |= (DM_LAST_ACCESS_DATE);
		ncp_date_unix2dos(attr->ia_ctime,
				  &(dummy), &(info.lastAccessDate));
		info.lastAccessDate = le16_to_cpu(info.lastAccessDate);
	}
	if (info_mask != 0) {
		result = ncp_modify_file_or_subdir_dos_info(NCP_SERVER(inode),
				      inode, info_mask, &info);
		if (result != 0) {
			result = -EACCES;

			if (info_mask == (DM_CREATE_TIME | DM_CREATE_DATE)) {
				/* NetWare seems not to allow this. I
				   do not know why. So, just tell the
				   user everything went fine. This is
				   a terrible hack, but I do not know
				   how to do this correctly. */
				result = 0;
			}
		}
	}
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		int written;

		DPRINTK(KERN_DEBUG "ncpfs: trying to change size to %ld\n",
			attr->ia_size);

		if ((result = ncp_make_open(inode, O_RDWR)) < 0) {
			return -EACCES;
		}
		ncp_write(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle,
			  attr->ia_size, 0, "", &written);

		/* According to ndir, the changes only take effect after
		   closing the file */
		result = ncp_make_closed(inode);
	}
	ncp_invalid_dir_cache(dentry->d_parent->d_inode);
out:
	return result;
}

#ifdef DEBUG_NCP_MALLOC
int ncp_malloced;
int ncp_current_malloced;
#endif

static struct file_system_type ncp_fs_type = {
	"ncpfs",
	0 /* FS_NO_DCACHE doesn't work correctly */,
        ncp_read_super,
	NULL
};

__initfunc(int init_ncp_fs(void))
{
	return register_filesystem(&ncp_fs_type);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	DPRINTK(KERN_DEBUG "ncpfs: init_module called\n");

	read_sem = MUTEX;
	read_nwinfo = NULL;

#ifdef DEBUG_NCP_MALLOC
	ncp_malloced = 0;
	ncp_current_malloced = 0;
#endif
	ncp_init_dir_cache();

	return init_ncp_fs();
}

void cleanup_module(void)
{
	DPRINTK(KERN_DEBUG "ncpfs: cleanup_module called\n");
	ncp_free_dir_cache();
	unregister_filesystem(&ncp_fs_type);
#ifdef DEBUG_NCP_MALLOC
	printk(KERN_DEBUG "ncp_malloced: %d\n", ncp_malloced);
	printk(KERN_DEBUG "ncp_current_malloced: %d\n", ncp_current_malloced);
#endif
}

#endif
