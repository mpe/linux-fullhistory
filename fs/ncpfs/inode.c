/*
 *  inode.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#include <linux/module.h>
#include <linux/config.h>

#include <asm/system.h>
#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/ncp_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/fcntl.h>
#include <linux/malloc.h>
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif
#include "ncplib_kernel.h"

extern int close_fp(struct file *filp);

static void ncp_put_inode(struct inode *);
static void ncp_read_inode(struct inode *);
static void ncp_put_super(struct super_block *);
static void ncp_statfs(struct super_block *sb, struct statfs *buf, int bufsiz);
static int ncp_notify_change(struct inode *inode, struct iattr *attr);

static struct super_operations ncp_sops = {
	ncp_read_inode,         /* read inode */
	ncp_notify_change,	/* notify change */
	NULL,			/* write inode */
	ncp_put_inode,		/* put inode */
	ncp_put_super,		/* put superblock */
	NULL,			/* write superblock */
	ncp_statfs,		/* stat filesystem */
	NULL
};

/* ncp_read_inode: Called from iget, it only traverses the allocated
   ncp_inode_info's and initializes the inode from the data found
   there.  It does not allocate or deallocate anything. */

static void
ncp_read_inode(struct inode *inode)
{
        /* Our task should be extremely simple here. We only have to
           look up the information somebody else (ncp_iget) put into
           the inode tree. The address of this information is the
           inode->i_ino. Just to make sure everything went well, we
           check it's there. */

        struct ncp_inode_info *inode_info = ncp_find_inode(inode);

	if (inode_info == NULL)
	{
		/* Ok, now we're in trouble. The inode info is not there. What
		   should we do now??? */
		printk("ncp_read_inode: inode info not found\n");
		return;
	}

        inode_info->state = NCP_INODE_VALID;

        NCP_INOP(inode) = inode_info;
	inode_info->inode = inode;

        if (NCP_ISTRUCT(inode)->attributes & aDIR)
	{
                inode->i_mode = NCP_SERVER(inode)->m.dir_mode;
		/* for directories dataStreamSize seems to be some
		   Object ID ??? */
		inode->i_size = 512;
	}
	else
	{
                inode->i_mode = NCP_SERVER(inode)->m.file_mode;
		inode->i_size = NCP_ISTRUCT(inode)->dataStreamSize;
	}

        DDPRINTK("ncp_read_inode: inode->i_mode = %u\n", inode->i_mode);

        inode->i_nlink   = 1;
        inode->i_uid     = NCP_SERVER(inode)->m.uid;
        inode->i_gid     = NCP_SERVER(inode)->m.gid;
        inode->i_blksize = 512;
        inode->i_rdev    = 0;

        if ((inode->i_blksize != 0) && (inode->i_size != 0))
	{
                inode->i_blocks =
                        (inode->i_size - 1) / inode->i_blksize + 1;
	}
        else
	{
                inode->i_blocks = 0;
	}

	inode->i_mtime = ncp_date_dos2unix(NCP_ISTRUCT(inode)->modifyTime,
					   NCP_ISTRUCT(inode)->modifyDate);
	inode->i_ctime = ncp_date_dos2unix(NCP_ISTRUCT(inode)->creationTime,
					   NCP_ISTRUCT(inode)->creationDate);
	inode->i_atime = ncp_date_dos2unix(0,
					   NCP_ISTRUCT(inode)->lastAccessDate);

        if (S_ISREG(inode->i_mode))
	{
                inode->i_op = &ncp_file_inode_operations;
	}
        else if (S_ISDIR(inode->i_mode))
	{
                inode->i_op = &ncp_dir_inode_operations;
	}
        else
	{
                inode->i_op = NULL;
	}
}

static void
ncp_put_inode(struct inode *inode)
{
        struct nw_file_info *finfo = NCP_FINFO(inode);
	struct super_block *sb = inode->i_sb;

	lock_super(sb);
        if (finfo->opened != 0)
	{
                if (ncp_close_file(NCP_SERVER(inode), finfo->file_handle)!=0)
		{
                        /* We can't do anything but complain. */
                        printk("ncp_put_inode: could not close\n");
                }
        }

	DDPRINTK("ncp_put_inode: put %s\n",
		finfo->i.entryName);

        ncp_free_inode_info(NCP_INOP(inode));

        if (S_ISDIR(inode->i_mode))
	{
                DDPRINTK("ncp_put_inode: put directory %ld\n",
			 inode->i_ino);
                ncp_invalid_dir_cache(inode);
        }                

	clear_inode(inode);
	unlock_super(sb);
}

struct super_block *
ncp_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct ncp_mount_data *data = (struct ncp_mount_data *) raw_data;
        struct ncp_server *server;
	struct file *ncp_filp;
	struct file *wdog_filp;
	struct file *msg_filp;
	kdev_t dev = sb->s_dev;
	int error;

	if (data == NULL)
	{
		printk("ncp_read_super: missing data argument\n");
		sb->s_dev = 0;
		return NULL;
	}

	if (data->version != NCP_MOUNT_VERSION)
	{
		printk("ncp warning: mount version %s than kernel\n",
		       (data->version < NCP_MOUNT_VERSION) ?
                       "older" : "newer");
		sb->s_dev = 0;
		return NULL;
	}

	if (   (data->ncp_fd >= NR_OPEN)
	    || ((ncp_filp = current->files->fd[data->ncp_fd]) == NULL)
	    || (!S_ISSOCK(ncp_filp->f_inode->i_mode)))
	{
		printk("ncp_read_super: invalid ncp socket\n");
		sb->s_dev = 0;
		return NULL;
	}

	if (   (data->wdog_fd >= NR_OPEN)
	    || ((wdog_filp = current->files->fd[data->wdog_fd]) == NULL)
	    || (!S_ISSOCK(wdog_filp->f_inode->i_mode)))
	{
		printk("ncp_read_super: invalid wdog socket\n");
		sb->s_dev = 0;
		return NULL;
	}

	if (   (data->message_fd >= NR_OPEN)
	    || ((msg_filp = current->files->fd[data->message_fd]) == NULL)
	    || (!S_ISSOCK(msg_filp->f_inode->i_mode)))
	{
		printk("ncp_read_super: invalid wdog socket\n");
		sb->s_dev = 0;
		return NULL;
	}

        /* We must malloc our own super-block info */
        server = (struct ncp_server *)ncp_kmalloc(sizeof(struct ncp_server),
                                                   GFP_KERNEL);

        if (server == NULL)
	{
                printk("ncp_read_super: could not alloc ncp_server\n");
                return NULL;
        }

	ncp_filp->f_count += 1;
	wdog_filp->f_count += 1;
	msg_filp->f_count += 1;

	lock_super(sb);

        NCP_SBP(sb) = server;
        
	sb->s_blocksize = 1024; /* Eh...  Is this correct? */
	sb->s_blocksize_bits = 10;
	sb->s_magic = NCP_SUPER_MAGIC;
	sb->s_dev = dev;
	sb->s_op = &ncp_sops;

	server->ncp_filp    = ncp_filp;
	server->wdog_filp   = wdog_filp;
	server->msg_filp    = msg_filp;
	server->lock        = 0;
	server->wait        = NULL;
        server->packet      = NULL;
	server->buffer_size = 0;
	server->conn_status = 0;

        server->m = *data;
	server->m.file_mode = (server->m.file_mode &
			       (S_IRWXU|S_IRWXG|S_IRWXO)) | S_IFREG;
	server->m.dir_mode  = (server->m.dir_mode &
			       (S_IRWXU|S_IRWXG|S_IRWXO)) | S_IFDIR;

	/* protect against invalid mount points */
	server->m.mount_point[sizeof(server->m.mount_point)-1] = '\0';

	server->packet_size = NCP_PACKET_SIZE;
	server->packet      = ncp_kmalloc(NCP_PACKET_SIZE, GFP_KERNEL);

	if (server->packet == NULL)
	{
		printk("ncpfs: could not alloc packet\n");
		error = -ENOMEM;
		unlock_super(sb);
		goto fail;
	}
   
        /*
         * Make the connection to the server
         */

	if (ncp_catch_watchdog(server) != 0)
	{
		printk("ncp_read_super: Could not catch watchdog\n");
		error = -EINVAL;
		unlock_super(sb);
		goto fail;
	}

	if (ncp_catch_message(server) != 0)
	{
		printk("ncp_read_super: Could not catch messages\n");
		ncp_dont_catch_watchdog(server);
		error = -EINVAL;
		unlock_super(sb);
		goto fail;
	}

	ncp_lock_server(server);
	error = ncp_connect(server);
	ncp_unlock_server(server);
	unlock_super(sb);

	if (error < 0)
	{
		sb->s_dev = 0;
		printk("ncp_read_super: Failed connection, bailing out "
                       "(error = %d).\n", -error);
                ncp_kfree_s(server->packet, server->packet_size);
		ncp_dont_catch_watchdog(server);
                goto fail;
	}

        DPRINTK("ncp_read_super: NCP_SBP(sb) = %x\n", (int)NCP_SBP(sb));

	ncp_init_root(server);

	if (!(sb->s_mounted = iget(sb, ncp_info_ino(server, &(server->root)))))
	{
		sb->s_dev = 0;
		printk("ncp_read_super: get root inode failed\n");
                goto disconnect;
	}

	if (ncp_negotiate_buffersize(server, NCP_DEFAULT_BUFSIZE,
				     &(server->buffer_size)) != 0)
	{
		sb->s_dev = 0;
		printk("ncp_read_super: could not get bufsize\n");
		goto disconnect;
	}

	DPRINTK("ncpfs: bufsize = %d\n", server->buffer_size);

        MOD_INC_USE_COUNT;
	return sb;

 disconnect:
	ncp_lock_server(server);
	ncp_disconnect(server);
	ncp_unlock_server(server);
	ncp_kfree_s(server->packet, server->packet_size);
	ncp_dont_catch_watchdog(server);
 fail:
	ncp_filp->f_count -= 1;
	wdog_filp->f_count -= 1;
	msg_filp->f_count -= 1;
        ncp_kfree_s(NCP_SBP(sb), sizeof(struct ncp_server));
        return NULL;
}

static void
ncp_put_super(struct super_block *sb)
{
        struct ncp_server *server = NCP_SBP(sb);

	lock_super(sb);

	ncp_lock_server(server);
        ncp_disconnect(server);
	ncp_unlock_server(server);

	close_fp(server->ncp_filp);

	ncp_dont_catch_watchdog(server);
	close_fp(server->wdog_filp);
	close_fp(server->msg_filp);

        ncp_free_all_inodes(server);

        ncp_kfree_s(server->packet, server->packet_size);

	sb->s_dev = 0;
        ncp_kfree_s(NCP_SBP(sb), sizeof(struct ncp_server));
	NCP_SBP(sb) = NULL;

	unlock_super(sb);

        MOD_DEC_USE_COUNT;
}

/* This routine is called from an interrupt in ncp_msg_data_ready. So
 * we have to be careful NOT to sleep here! */
void
ncp_trigger_message(struct ncp_server *server)
{
#ifdef CONFIG_KERNELD
	char command[ sizeof(server->m.mount_point)
		     + sizeof(NCP_MSG_COMMAND) + 2];
#endif

	if (server == NULL)
	{
		printk("ncp_trigger_message: invalid server!\n");
		return;
	}

	DPRINTK("ncp_trigger_message: on %s\n",
		server->m.mount_point);

#ifdef CONFIG_KERNELD
	strcpy(command, NCP_MSG_COMMAND);
	strcat(command, " ");
	strcat(command, server->m.mount_point);
	DPRINTK("ksystem: %s\n", command);
	ksystem(command, KERNELD_NOWAIT);
#endif
}

static void 
ncp_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
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
	memcpy_tofs(buf, &tmp, bufsiz);
}

static int
ncp_notify_change(struct inode *inode, struct iattr *attr)
{
	int result = 0;
	int info_mask;
	struct nw_modify_dos_info info;

	if (!ncp_conn_valid(NCP_SERVER(inode)))
	{
		return -EIO;
	}

	if ((result = inode_change_ok(inode, attr)) < 0)
		return result;

	if (((attr->ia_valid & ATTR_UID) && 
	     (attr->ia_uid != NCP_SERVER(inode)->m.uid)))
		return -EPERM;

	if (((attr->ia_valid & ATTR_GID) && 
	     (attr->ia_uid != NCP_SERVER(inode)->m.gid)))
                return -EPERM;

	if (((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode &
	      ~(S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO))))
		return -EPERM;

	info_mask = 0;
	memset(&info, 0, sizeof(info));

	if ((attr->ia_valid & ATTR_CTIME) != 0)
	{
		info_mask |= (DM_CREATE_TIME|DM_CREATE_DATE);
		ncp_date_unix2dos(attr->ia_ctime,
				  &(info.creationTime), &(info.creationDate));
	}
	
	if ((attr->ia_valid & ATTR_MTIME) != 0)
	{
		info_mask |= (DM_MODIFY_TIME|DM_MODIFY_DATE);
		ncp_date_unix2dos(attr->ia_mtime,
				  &(info.modifyTime), &(info.modifyDate));
	}
	
	if ((attr->ia_valid & ATTR_ATIME) != 0)
	{
		__u16 dummy;
		info_mask |= (DM_LAST_ACCESS_DATE);
		ncp_date_unix2dos(attr->ia_ctime,
				  &(dummy), &(info.lastAccessDate));
	}

	if (info_mask != 0)
	{
		if ((result =
		     ncp_modify_file_or_subdir_dos_info(NCP_SERVER(inode),
							NCP_ISTRUCT(inode),
							info_mask,
							&info)) != 0)
		{
			result = -EACCES;

			if (info_mask == (DM_CREATE_TIME|DM_CREATE_DATE))
			{
				/* NetWare seems not to allow this. I
                                   do not know why. So, just tell the
                                   user everything went fine. This is
                                   a terrible hack, but I do not know
                                   how to do this correctly. */
				result = 0;
			}
		}
	}

        if ((attr->ia_valid & ATTR_SIZE) != 0)
	{
		int written;

		DPRINTK("ncpfs: trying to change size of %s to %ld\n",
			NCP_ISTRUCT(inode)->entryName, attr->ia_size);

		if ((result = ncp_make_open(inode, O_RDWR)) < 0)
		{
			return -EACCES;
		}

		ncp_write(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle,
			  attr->ia_size, 0, "", &written);

		/* According to ndir, the changes only take effect after
		   closing the file */
		ncp_close_file(NCP_SERVER(inode),
			       NCP_FINFO(inode)->file_handle);
		NCP_FINFO(inode)->opened = 0;

		result = 0;
	}

        ncp_invalid_dir_cache(NCP_INOP(inode)->dir->inode);

	return result;
}
		
#ifdef DEBUG_NCP_MALLOC
int ncp_malloced;
int ncp_current_malloced;
#endif

static struct file_system_type ncp_fs_type = {
        ncp_read_super, "ncpfs", 0, NULL
        };

int init_ncp_fs(void)
{
        return register_filesystem(&ncp_fs_type);
}

#ifdef MODULE
int
init_module( void)
{
	int status;

        DPRINTK("ncpfs: init_module called\n");

#ifdef DEBUG_NCP_MALLOC
        ncp_malloced = 0;
        ncp_current_malloced = 0;
#endif
        ncp_init_dir_cache();

	if ((status = init_ncp_fs()) == 0)
		register_symtab(0);
	return status;
}

void
cleanup_module(void)
{
        DPRINTK("ncpfs: cleanup_module called\n");
        ncp_free_dir_cache();
        unregister_filesystem(&ncp_fs_type);
#ifdef DEBUG_NCP_MALLOC
        printk("ncp_malloced: %d\n", ncp_malloced);
        printk("ncp_current_malloced: %d\n", ncp_current_malloced);
#endif
}

#endif
