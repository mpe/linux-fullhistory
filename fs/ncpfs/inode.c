/*
 *  inode.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *  Modified 1998 Wolfram Pienkoss for NLS
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

static void ncp_put_inode(struct inode *);
static void ncp_delete_inode(struct inode *);
static void ncp_put_super(struct super_block *);
static int  ncp_statfs(struct super_block *, struct statfs *, int);

static struct super_operations ncp_sops =
{
	NULL,			/* read inode */
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
#ifdef CONFIG_NCPFS_EXTRAS
extern struct inode_operations ncp_symlink_inode_operations;
extern int ncp_symlink(struct inode*, struct dentry*, const char*);
#endif

/*
 * Fill in the ncpfs-specific information in the inode.
 */
void ncp_update_inode(struct inode *inode, struct ncp_entry_info *nwinfo)
{
	NCP_FINFO(inode)->DosDirNum = nwinfo->i.DosDirNum;
	NCP_FINFO(inode)->dirEntNum = nwinfo->i.dirEntNum;
	NCP_FINFO(inode)->volNumber = nwinfo->i.volNumber;

#ifdef CONFIG_NCPFS_STRONG
	NCP_FINFO(inode)->nwattr = nwinfo->i.attributes;
#endif
	NCP_FINFO(inode)->opened = nwinfo->opened;
	NCP_FINFO(inode)->access = nwinfo->access;
	NCP_FINFO(inode)->server_file_handle = nwinfo->server_file_handle;
	memcpy(NCP_FINFO(inode)->file_handle, nwinfo->file_handle,
			sizeof(nwinfo->file_handle));
	DPRINTK("ncp_update_inode: updated %s, volnum=%d, dirent=%u\n",
		nwinfo->i.entryName, NCP_FINFO(inode)->volNumber,
		NCP_FINFO(inode)->dirEntNum);
}

void ncp_update_inode2(struct inode* inode, struct ncp_entry_info *nwinfo)
{
	struct nw_info_struct *nwi = &nwinfo->i;
	struct ncp_server *server = NCP_SERVER(inode);

	if (!NCP_FINFO(inode)->opened) {
#ifdef CONFIG_NCPFS_STRONG
		NCP_FINFO(inode)->nwattr = nwi->attributes;
#endif
		if (nwi->attributes & aDIR) {
			inode->i_mode = server->m.dir_mode;
			inode->i_size = NCP_BLOCK_SIZE;
		} else {
			inode->i_mode = server->m.file_mode;
			inode->i_size = le32_to_cpu(nwi->dataStreamSize);
#ifdef CONFIG_NCPFS_EXTRAS
			if ((server->m.flags & (NCP_MOUNT_EXTRAS|NCP_MOUNT_SYMLINKS)) && (nwi->attributes & aSHARED)) {
				switch (nwi->attributes & (aHIDDEN|aSYSTEM)) {
					case aHIDDEN:
						if (server->m.flags & NCP_MOUNT_SYMLINKS) {
							if ( /* (inode->i_size >= NCP_MIN_SYMLINK_SIZE)
							 && */ (inode->i_size <= NCP_MAX_SYMLINK_SIZE)) {
								inode->i_mode = (inode->i_mode & ~S_IFMT) | S_IFLNK;
								break;
							}
						}
						/* FALLTHROUGH */
					case 0:
						if (server->m.flags & NCP_MOUNT_EXTRAS)
							inode->i_mode |= 0444;
						break;
					case aSYSTEM:
						if (server->m.flags & NCP_MOUNT_EXTRAS)
							inode->i_mode |= (inode->i_mode >> 2) & 0111;
						break;
					/* case aSYSTEM|aHIDDEN: */
					default:
						/* reserved combination */
						break;
				}
			}
#endif
		}
		if (nwi->attributes & aRONLY) inode->i_mode &= ~0222;
	}
	inode->i_blocks = 0;
	if ((inode->i_size)&&(inode->i_blksize)) {
		inode->i_blocks = (inode->i_size-1)/(inode->i_blksize)+1;
	}

	/* TODO: times? I'm not sure... */
	inode->i_mtime = ncp_date_dos2unix(le16_to_cpu(nwinfo->i.modifyTime),
			  		   le16_to_cpu(nwinfo->i.modifyDate));
	inode->i_ctime = ncp_date_dos2unix(le16_to_cpu(nwinfo->i.creationTime),
			    		   le16_to_cpu(nwinfo->i.creationDate));
	inode->i_atime = ncp_date_dos2unix(0,
					   le16_to_cpu(nwinfo->i.lastAccessDate));

	NCP_FINFO(inode)->DosDirNum = nwinfo->i.DosDirNum;
	NCP_FINFO(inode)->dirEntNum = nwinfo->i.dirEntNum;
	NCP_FINFO(inode)->volNumber = nwinfo->i.volNumber;
}

/*
 * Fill in the inode based on the ncp_entry_info structure.
 */
static void ncp_set_attr(struct inode *inode, struct ncp_entry_info *nwinfo)
{
	struct nw_info_struct *nwi = &nwinfo->i;
	struct ncp_server *server = NCP_SERVER(inode);

	if (nwi->attributes & aDIR) {
		inode->i_mode = server->m.dir_mode;
		/* for directories dataStreamSize seems to be some
		   Object ID ??? */
		inode->i_size = NCP_BLOCK_SIZE;
	} else {
		inode->i_mode = server->m.file_mode;
		inode->i_size = le32_to_cpu(nwi->dataStreamSize);
#ifdef CONFIG_NCPFS_EXTRAS
		if ((server->m.flags & (NCP_MOUNT_EXTRAS|NCP_MOUNT_SYMLINKS)) 
		 && (nwi->attributes & aSHARED)) {
			switch (nwi->attributes & (aHIDDEN|aSYSTEM)) {
				case aHIDDEN:
					if (server->m.flags & NCP_MOUNT_SYMLINKS) {
						if (/* (inode->i_size >= NCP_MIN_SYMLINK_SIZE)
						 && */ (inode->i_size <= NCP_MAX_SYMLINK_SIZE)) {
							inode->i_mode = (inode->i_mode & ~S_IFMT) | S_IFLNK;
							break;
						}
					}
					/* FALLTHROUGH */
				case 0:
					if (server->m.flags & NCP_MOUNT_EXTRAS)
						inode->i_mode |= 0444;
					break;
				case aSYSTEM:
					if (server->m.flags & NCP_MOUNT_EXTRAS)
						inode->i_mode |= (inode->i_mode >> 2) & 0111;
					break;
				/* case aSYSTEM|aHIDDEN: */
				default:
					/* reserved combination */
					break;
			}
		}
#endif
	}
	if (nwi->attributes & aRONLY) inode->i_mode &= ~0222;

	DDPRINTK("ncp_read_inode: inode->i_mode = %u\n", inode->i_mode);

	inode->i_nlink = 1;
	inode->i_uid = server->m.uid;
	inode->i_gid = server->m.gid;
	inode->i_blksize = NCP_BLOCK_SIZE;
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
 * Get a new inode.
 */
struct inode * 
ncp_iget(struct super_block *sb, struct ncp_entry_info *info)
{
	struct inode *inode;

	if (info == NULL) {
		printk(KERN_ERR "ncp_iget: info is NULL\n");
		return NULL;
	}

	inode = get_empty_inode();
	if (inode) {
		inode->i_sb = sb;
		inode->i_dev = sb->s_dev;
		inode->i_ino = info->ino;
		ncp_set_attr(inode, info);
		if (S_ISREG(inode->i_mode)) {
			inode->i_op = &ncp_file_inode_operations;
		} else if (S_ISDIR(inode->i_mode)) {
			inode->i_op = &ncp_dir_inode_operations;
#ifdef CONFIG_NCPFS_EXTRAS
		} else if (S_ISLNK(inode->i_mode)) {
			inode->i_op = &ncp_symlink_inode_operations;
#endif
		}
		insert_inode_hash(inode);
	} else
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
		DDPRINTK("ncp_delete_inode: put directory %ld\n", inode->i_ino);
	}

	if (NCP_FINFO(inode)->opened && ncp_make_closed(inode) != 0) {
		/* We can't do anything but complain. */
		printk(KERN_ERR "ncp_delete_inode: could not close\n");
	}
	clear_inode(inode);
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
	struct ncp_entry_info finfo;

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

	server = NCP_SBP(sb);
	memset(server, 0, sizeof(*server));

	server->ncp_filp = ncp_filp;
/*	server->lock = 0;	*/
	init_MUTEX(&server->sem);
	server->packet = NULL;
/*	server->buffer_size = 0;	*/
/*	server->conn_status = 0;	*/
/*	server->root_dentry = NULL;	*/
/*	server->root_setuped = 0;	*/
#ifdef CONFIG_NCPFS_PACKET_SIGNING
/*	server->sign_wanted = 0;	*/
/*	server->sign_active = 0;	*/
#endif
	server->auth.auth_type = NCP_AUTH_NONE;
/*	server->auth.object_name_len = 0;	*/
/*	server->auth.object_name = NULL;	*/
/*	server->auth.object_type = 0;		*/
/*	server->priv.len = 0;			*/
/*	server->priv.data = NULL;		*/

	server->m = *data;
	/* Althought anything producing this is buggy, it happens
	   now because of PATH_MAX changes.. */
	if (server->m.time_out < 1) {
		server->m.time_out = 10;
		printk(KERN_INFO "You need to recompile your ncpfs utils..\n");
	}
	server->m.time_out = server->m.time_out * HZ / 100;
	server->m.file_mode = (server->m.file_mode &
			       (S_IRWXU | S_IRWXG | S_IRWXO)) | S_IFREG;
	server->m.dir_mode = (server->m.dir_mode &
			      (S_IRWXU | S_IRWXG | S_IRWXO)) | S_IFDIR;

#ifdef CONFIG_NCPFS_NLS
	/* load the default NLS charsets */
	server->nls_vol = load_nls_default();
	server->nls_io = load_nls_default();
#endif /* CONFIG_NCPFS_NLS */

	server->dentry_ttl = 0;	/* no caching */

	server->packet_size = NCP_PACKET_SIZE;
	server->packet = ncp_kmalloc(NCP_PACKET_SIZE, GFP_KERNEL);
	if (server->packet == NULL)
		goto out_no_packet;

	ncp_lock_server(server);
	error = ncp_connect(server);
	ncp_unlock_server(server);
	if (error < 0)
		goto out_no_connect;
	DPRINTK("ncp_read_super: NCP_SBP(sb) = %x\n", (int) NCP_SBP(sb));

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
	DPRINTK("ncpfs: bufsize = %d\n", server->buffer_size);

	memset(&finfo, 0, sizeof(finfo));
	finfo.i.attributes	= aDIR;
	finfo.i.dataStreamSize	= NCP_BLOCK_SIZE;
	finfo.i.dirEntNum	= 0;
	finfo.i.DosDirNum	= 0;
#ifdef CONFIG_NCPFS_SMALLDOS
	finfo.i.NSCreator	= NW_NS_DOS;
#endif
	finfo.i.volNumber	= NCP_NUMBER_OF_VOLUMES + 1;	/* illegal volnum */
	/* set dates of mountpoint to Jan 1, 1986; 00:00 */
	finfo.i.creationTime	= finfo.i.modifyTime
				= cpu_to_le16(0x0000);
	finfo.i.creationDate	= finfo.i.modifyDate
				= finfo.i.lastAccessDate
				= cpu_to_le16(0x0C21);
	finfo.i.nameLen		= 0;
	finfo.i.entryName[0]	= '\0';

	finfo.opened		= 0;
	finfo.ino		= 2;	/* tradition */

	server->name_space[finfo.i.volNumber] = NW_NS_DOS;
        root_inode = ncp_iget(sb, &finfo);
        if (!root_inode)
		goto out_no_root;
	DPRINTK("ncp_read_super: root vol=%d\n", NCP_FINFO(root_inode)->volNumber);
	sb->s_root = d_alloc_root(root_inode);
        if (!sb->s_root)
		goto out_no_root;
	sb->s_root->d_op = &ncp_dentry_operations;
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
#ifdef CONFIG_NCPFS_NLS
	unload_nls(server->nls_io);
	unload_nls(server->nls_vol);
#endif
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

#ifdef CONFIG_NCPFS_NLS
	/* unload the NLS charsets */
	if (server->nls_vol)
	{
		unload_nls(server->nls_vol);
		server->nls_vol = NULL;
	}
	if (server->nls_io)
	{
		unload_nls(server->nls_io);
		server->nls_io = NULL;
	}
#endif /* CONFIG_NCPFS_NLS */

	fput(server->ncp_filp);
	kill_proc(server->m.wdog_pid, SIGTERM, 1);

	if (server->priv.data) 
		ncp_kfree_s(server->priv.data, server->priv.len);
	if (server->auth.object_name)
		ncp_kfree_s(server->auth.object_name, server->auth.object_name_len);
	ncp_kfree_s(server->packet, server->packet_size);

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
	tmp.f_bsize = NCP_BLOCK_SIZE;
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
	struct ncp_server *server;

	result = -EIO;

	server = NCP_SERVER(inode);
	if ((!server) || !ncp_conn_valid(server))
		goto out;

	/* ageing the dentry to force validation */
	ncp_age_dentry(server, dentry);

	result = inode_change_ok(inode, attr);
	if (result < 0)
		goto out;

	result = -EPERM;
	if (((attr->ia_valid & ATTR_UID) &&
	     (attr->ia_uid != server->m.uid)))
		goto out;

	if (((attr->ia_valid & ATTR_GID) &&
	     (attr->ia_gid != server->m.gid)))
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
                if (S_ISDIR(inode->i_mode)) {
                	umode_t newmode;

                	info_mask |= DM_ATTRIBUTES;
                	newmode = attr->ia_mode;
                	newmode &= NCP_SERVER(inode)->m.dir_mode;

                	if (newmode & 0222)
                		info.attributes &= ~(aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);
                	else
				info.attributes |=  (aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);
                } else if (!S_ISREG(inode->i_mode))
                {
                        return -EPERM;
                }
                else
                {
			umode_t newmode;
#ifdef CONFIG_NCPFS_EXTRAS			
			int extras;
			
			extras = server->m.flags & NCP_MOUNT_EXTRAS;
#endif
                        info_mask |= DM_ATTRIBUTES;
                        newmode=attr->ia_mode;
#ifdef CONFIG_NCPFS_EXTRAS
			if (!extras)
#endif
	                        newmode &= server->m.file_mode;

                        if (newmode & 0222) /* any write bit set */
                        {
                                info.attributes &= ~(aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);
                        }
                        else
                        {
                                info.attributes |=  (aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);
                        }
#ifdef CONFIG_NCPFS_EXTRAS
			if (extras) {
				if (newmode & 0111) /* any execute bit set */
					info.attributes |= aSHARED | aSYSTEM;
				/* read for group/world and not in default file_mode */
				else if (newmode & ~server->m.file_mode & 0444)
					info.attributes |= aSHARED;
			}
#endif
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
#ifdef CONFIG_NCPFS_STRONG		
		if ((!result) && (info_mask & DM_ATTRIBUTES))
			NCP_FINFO(inode)->nwattr = info.attributes;
#endif
	}
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		int written;

		DPRINTK("ncpfs: trying to change size to %ld\n",
			attr->ia_size);

		if ((result = ncp_make_open(inode, O_RDWR)) < 0) {
			return -EACCES;
		}
		ncp_write_kernel(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle,
			  attr->ia_size, 0, "", &written);

		/* According to ndir, the changes only take effect after
		   closing the file */
		result = ncp_make_closed(inode);
	}
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

int __init init_ncp_fs(void)
{
	return register_filesystem(&ncp_fs_type);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	DPRINTK("ncpfs: init_module called\n");

#ifdef DEBUG_NCP_MALLOC
	ncp_malloced = 0;
	ncp_current_malloced = 0;
#endif
	return init_ncp_fs();
}

void cleanup_module(void)
{
	DPRINTK("ncpfs: cleanup_module called\n");
	unregister_filesystem(&ncp_fs_type);
#ifdef DEBUG_NCP_MALLOC
	PRINTK("ncp_malloced: %d\n", ncp_malloced);
	PRINTK("ncp_current_malloced: %d\n", ncp_current_malloced);
#endif
}

#endif
