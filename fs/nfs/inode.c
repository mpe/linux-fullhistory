/*
 *  linux/fs/nfs/inode.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs inode and superblock handling functions
 *
 *  Modularised by Alan Cox <Alan.Cox@linux.org>, while hacking some
 *  experimental NFS changes. Modularisation taken straight from SYS5 fs.
 *
 *  Change to nfs_read_super() to permit NFS mounts to multi-homed hosts.
 *  J.S.Peatfield@damtp.cam.ac.uk
 *
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/nfs_fs.h>
#include <linux/nfsiod.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/segment.h>

/* This is for kernel_thread */
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

extern int close_fp(struct file *filp);

static int nfs_notify_change(struct inode *, struct iattr *);
static void nfs_put_inode(struct inode *);
static void nfs_put_super(struct super_block *);
static void nfs_read_inode(struct inode *);
static void nfs_statfs(struct super_block *, struct statfs *, int bufsiz);

static struct super_operations nfs_sops = { 
	nfs_read_inode,		/* read inode */
	nfs_notify_change,	/* notify change */
	NULL,			/* write inode */
	nfs_put_inode,		/* put inode */
	nfs_put_super,		/* put superblock */
	NULL,			/* write superblock */
	nfs_statfs,		/* stat filesystem */
	NULL
};

/*
 * The "read_inode" function doesn't actually do anything:
 * the real data is filled in later in nfs_fhget. Here we
 * just mark the cache times invalid, and zero out i_mode
 * (the latter makes "nfs_refresh_inode" do the right thing
 * wrt pipe inodes)
 */
static void nfs_read_inode(struct inode * inode)
{
	int rsize = inode->i_sb->u.nfs_sb.s_server.rsize;
	int size = inode->i_sb->u.nfs_sb.s_server.wsize;

	if (rsize > size)
		size = rsize;
	inode->i_blksize = size;
	inode->i_mode = 0;
	inode->i_op = NULL;
	NFS_CACHEINV(inode);
}

static void nfs_put_inode(struct inode * inode)
{
	if (NFS_RENAMED_DIR(inode))
		nfs_sillyrename_cleanup(inode);
	if (inode->i_pipe)
		clear_inode(inode);
}

void nfs_put_super(struct super_block *sb)
{
	close_fp(sb->u.nfs_sb.s_server.file);
	rpc_closesock(sb->u.nfs_sb.s_server.rsock);
	lock_super(sb);
	sb->s_dev = 0;
	unlock_super(sb);
	MOD_DEC_USE_COUNT;
}

/*
 * The way this works is that the mount process passes a structure
 * in the data argument which contains an open socket to the NFS
 * server and the root file handle obtained from the server's mount
 * daemon.  We stash these away in the private superblock fields.
 * Later we can add other mount parameters like caching values.
 */

struct super_block *nfs_read_super(struct super_block *sb, void *raw_data,
				   int silent)
{
	struct nfs_mount_data *data = (struct nfs_mount_data *) raw_data;
	struct nfs_server *server;
	unsigned int fd;
	struct file *filp;

	kdev_t dev = sb->s_dev;

	MOD_INC_USE_COUNT;
	if (!data) {
		printk("nfs_read_super: missing data argument\n");
		sb->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	fd = data->fd;
	if (data->version != NFS_MOUNT_VERSION) {
		printk("nfs warning: mount version %s than kernel\n",
			data->version < NFS_MOUNT_VERSION ? "older" : "newer");
	}
	if (fd >= NR_OPEN || !(filp = current->files->fd[fd])) {
		printk("nfs_read_super: invalid file descriptor\n");
		sb->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	if (!S_ISSOCK(filp->f_inode->i_mode)) {
		printk("nfs_read_super: not a socket\n");
		sb->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	filp->f_count++;
	lock_super(sb);

	sb->s_blocksize = 1024; /* XXX */
	sb->s_blocksize_bits = 10;
	sb->s_magic = NFS_SUPER_MAGIC;
	sb->s_dev = dev;
	sb->s_op = &nfs_sops;
	server = &sb->u.nfs_sb.s_server;
	server->file = filp;
	server->lock = 0;
	server->wait = NULL;
	server->flags = data->flags;
	server->rsize = data->rsize;
	if (server->rsize <= 0)
		server->rsize = NFS_DEF_FILE_IO_BUFFER_SIZE;
	else if (server->rsize >= NFS_MAX_FILE_IO_BUFFER_SIZE)
		server->rsize = NFS_MAX_FILE_IO_BUFFER_SIZE;
	server->wsize = data->wsize;
	if (server->wsize <= 0)
		server->wsize = NFS_DEF_FILE_IO_BUFFER_SIZE;
	else if (server->wsize >= NFS_MAX_FILE_IO_BUFFER_SIZE)
		server->wsize = NFS_MAX_FILE_IO_BUFFER_SIZE;
	server->timeo = data->timeo*HZ/10;
	server->retrans = data->retrans;
	server->acregmin = data->acregmin*HZ;
	server->acregmax = data->acregmax*HZ;
	server->acdirmin = data->acdirmin*HZ;
	server->acdirmax = data->acdirmax*HZ;
	strcpy(server->hostname, data->hostname);

	/* Start of JSP NFS patch */
	/* Check if passed address in data->addr */
	if (data->addr.sin_addr.s_addr == INADDR_ANY) {  /* No address passed */
	  if (((struct sockaddr_in *)(&server->toaddr))->sin_addr.s_addr == INADDR_ANY) {
	    printk("NFS: Error passed unconnected socket and no address\n") ;
	    MOD_DEC_USE_COUNT;
	    return NULL ;
	  } else {
	    /* Need access to socket internals  JSP */
	    struct socket *sock;
	    int dummylen ;

	 /*   printk("NFS: using socket address\n") ;*/

	    sock = &((filp->f_inode)->u.socket_i);

	    /* extract the other end of the socket into server->toaddr */
	    sock->ops->getname(sock, &(server->toaddr), &dummylen, 1) ;
	  }
	} else {
	/*  printk("NFS: copying passed addr to server->toaddr\n") ;*/
	  memcpy((char *)&(server->toaddr),(char *)(&data->addr),sizeof(server->toaddr));
	}
	/* End of JSP NFS patch */

	if ((server->rsock = rpc_makesock(filp)) == NULL) {
		printk("NFS: cannot create RPC socket.\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	sb->u.nfs_sb.s_root = data->root;
	unlock_super(sb);
	if (!(sb->s_mounted = nfs_fhget(sb, &data->root, NULL))) {
		sb->s_dev = 0;
		printk("nfs_read_super: get root inode failed\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	return sb;
}

void nfs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	int error;
	struct nfs_fsinfo res;
	struct statfs tmp;

	error = nfs_proc_statfs(&sb->u.nfs_sb.s_server, &sb->u.nfs_sb.s_root,
		&res);
	if (error) {
		printk("nfs_statfs: statfs error = %d\n", -error);
		res.bsize = res.blocks = res.bfree = res.bavail = 0;
	}
	tmp.f_type = NFS_SUPER_MAGIC;
	tmp.f_bsize = res.bsize;
	tmp.f_blocks = res.blocks;
	tmp.f_bfree = res.bfree;
	tmp.f_bavail = res.bavail;
	tmp.f_files = 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = NAME_MAX;
	memcpy_tofs(buf, &tmp, bufsiz);
}

/*
 * This is our own version of iget that looks up inodes by file handle
 * instead of inode number.  We use this technique instead of using
 * the vfs read_inode function because there is no way to pass the
 * file handle or current attributes into the read_inode function.
 * We just have to be careful not to subvert iget's special handling
 * of mount points.
 */

struct inode *nfs_fhget(struct super_block *sb, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr)
{
	struct nfs_fattr newfattr;
	int error;
	struct inode *inode;

	if (!sb) {
		printk("nfs_fhget: super block is NULL\n");
		return NULL;
	}
	if (!fattr) {
		error = nfs_proc_getattr(&sb->u.nfs_sb.s_server, fhandle,
			&newfattr);
		if (error) {
			printk("nfs_fhget: getattr error = %d\n", -error);
			return NULL;
		}
		fattr = &newfattr;
	}
	if (!(inode = iget(sb, fattr->fileid))) {
		printk("nfs_fhget: iget failed\n");
		return NULL;
	}
	if (inode->i_dev == sb->s_dev) {
		if (inode->i_ino != fattr->fileid) {
			printk("nfs_fhget: unexpected inode from iget\n");
			return inode;
		}
		*NFS_FH(inode) = *fhandle;
		nfs_refresh_inode(inode, fattr);
	}
	return inode;
}

int nfs_notify_change(struct inode *inode, struct iattr *attr)
{
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;
	int error;

	sattr.mode = (unsigned) -1;
	if (attr->ia_valid & ATTR_MODE) 
		sattr.mode = attr->ia_mode;

	sattr.uid = (unsigned) -1;
	if (attr->ia_valid & ATTR_UID)
		sattr.uid = attr->ia_uid;

	sattr.gid = (unsigned) -1;
	if (attr->ia_valid & ATTR_GID)
		sattr.gid = attr->ia_gid;


	sattr.size = (unsigned) -1;
	if (attr->ia_valid & ATTR_SIZE)
		sattr.size = S_ISREG(inode->i_mode) ? attr->ia_size : -1;

	sattr.mtime.seconds = sattr.mtime.useconds = (unsigned) -1;
	if (attr->ia_valid & ATTR_MTIME) {
		sattr.mtime.seconds = attr->ia_mtime;
		sattr.mtime.useconds = 0;
	}

	sattr.atime.seconds = sattr.atime.useconds = (unsigned) -1;
	if (attr->ia_valid & ATTR_ATIME) {
		sattr.atime.seconds = attr->ia_atime;
		sattr.atime.useconds = 0;
	}

	error = nfs_proc_setattr(NFS_SERVER(inode), NFS_FH(inode),
		&sattr, &fattr);
	if (!error)
		nfs_refresh_inode(inode, &fattr);
	inode->i_dirt = 0;
	return error;
}

/* Every kernel module contains stuff like this. */

static struct file_system_type nfs_fs_type = {
	nfs_read_super, "nfs", 0, NULL
};

/*
 * Start up an nfsiod process. This is an awful hack, because when running
 * as a module, we will keep insmod's memory. Besides, the current->comm
 * hack won't work in this case
 * The best would be to have a syscall for nfs client control that (among
 * other things) forks biod's.
 * Alternatively, we might want to have the idle task spawn biod's on demand.
 */
static int run_nfsiod(void *dummy)
{
	int	ret;

#ifdef __SMP__
	lock_kernel();
	syscall_count++;
#endif

	MOD_INC_USE_COUNT;
	exit_mm(current);
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "nfsiod");
	ret = nfsiod();
	MOD_DEC_USE_COUNT;
	return ret;
}

int init_nfs_fs(void)
{
	/* Fork four biod's */
	kernel_thread(run_nfsiod, NULL, 0);
	kernel_thread(run_nfsiod, NULL, 0);
	kernel_thread(run_nfsiod, NULL, 0);
	kernel_thread(run_nfsiod, NULL, 0);
        return register_filesystem(&nfs_fs_type);
}

#ifdef MODULE
int init_module(void)
{
	int status;

	if ((status = init_nfs_fs()) == 0)
		register_symtab(0);
	return status;
}

void cleanup_module(void)
{
	unregister_filesystem(&nfs_fs_type);
	nfs_kfree_cache();
}

#endif
