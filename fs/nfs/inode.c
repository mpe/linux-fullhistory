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

#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/stats.h>
#include <linux/nfs_fs.h>
#include <linux/lockd/bind.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#define NFSDBG_FACILITY		NFSDBG_VFS

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

struct rpc_stat			nfs_rpcstat = { &nfs_program };

/*
 * The "read_inode" function doesn't actually do anything:
 * the real data is filled in later in nfs_fhget. Here we
 * just mark the cache times invalid, and zero out i_mode
 * (the latter makes "nfs_refresh_inode" do the right thing
 * wrt pipe inodes)
 */
static void
nfs_read_inode(struct inode * inode)
{
	inode->i_blksize = inode->i_sb->s_blocksize;
	inode->i_mode = 0;
	inode->i_op = NULL;
	NFS_CACHEINV(inode);
}

static void
nfs_put_inode(struct inode * inode)
{
	dprintk("NFS: put_inode(%x/%ld)\n", inode->i_dev, inode->i_ino);

	if (NFS_RENAMED_DIR(inode))
		nfs_sillyrename_cleanup(inode);
	if (inode->i_pipe)
		clear_inode(inode);
}

void
nfs_put_super(struct super_block *sb)
{
	struct nfs_server *server = &sb->u.nfs_sb.s_server;
	struct rpc_clnt	*rpc;

	if ((rpc = server->client) != NULL)
		rpc_shutdown_client(rpc);

	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_down();	/* release rpc.lockd */
	rpciod_down();		/* release rpciod */
	lock_super(sb);
	sb->s_dev = 0;
	unlock_super(sb);
	MOD_DEC_USE_COUNT;
}

/*
 * Compute and set NFS server blocksize
 */
static unsigned int
nfs_block_size(unsigned int bsize, unsigned char *nrbitsp)
{
	if (bsize < 1024)
		bsize = NFS_DEF_FILE_IO_BUFFER_SIZE;
	else if (bsize >= NFS_MAX_FILE_IO_BUFFER_SIZE)
		bsize = NFS_MAX_FILE_IO_BUFFER_SIZE;

	/* make sure blocksize is a power of two */
	if ((bsize & (bsize - 1)) || nrbitsp) {
		unsigned int	nrbits;

		for (nrbits = 31; nrbits && !(bsize & (1 << nrbits)); nrbits--)
			;
		bsize = 1 << nrbits;
		if (nrbitsp)
			*nrbitsp = nrbits;
		if (bsize < NFS_DEF_FILE_IO_BUFFER_SIZE)
			bsize = NFS_DEF_FILE_IO_BUFFER_SIZE;
	}

	return bsize;
}

/*
 * The way this works is that the mount process passes a structure
 * in the data argument which contains the server's IP address
 * and the root file handle obtained from the server's mount
 * daemon. We stash these away in the private superblock fields.
 */
struct super_block *
nfs_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct nfs_mount_data	*data = (struct nfs_mount_data *) raw_data;
	struct sockaddr_in	srvaddr;
	struct nfs_server	*server;
	struct rpc_timeout	timeparms;
	struct rpc_xprt		*xprt;
	struct rpc_clnt		*clnt;
	unsigned int		authflavor;
	int			tcp;
	kdev_t			dev = sb->s_dev;

	MOD_INC_USE_COUNT;
	if (!data) {
		printk("nfs_read_super: missing data argument\n");
		sb->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	if (data->version != NFS_MOUNT_VERSION) {
		printk("nfs warning: mount version %s than kernel\n",
			data->version < NFS_MOUNT_VERSION ? "older" : "newer");
		if (data->version < 2)
			data->namlen = 0;
		if (data->version < 3)
			data->bsize  = 0;
	}

	lock_super(sb);

	server           = &sb->u.nfs_sb.s_server;
	sb->s_magic      = NFS_SUPER_MAGIC;
	sb->s_dev        = dev;
	sb->s_op         = &nfs_sops;
	sb->s_blocksize  = nfs_block_size(data->bsize, &sb->s_blocksize_bits);
	server->rsize    = nfs_block_size(data->rsize, NULL);
	server->wsize    = nfs_block_size(data->wsize, NULL);
	server->flags    = data->flags;
	server->acregmin = data->acregmin*HZ;
	server->acregmax = data->acregmax*HZ;
	server->acdirmin = data->acdirmin*HZ;
	server->acdirmax = data->acdirmax*HZ;
	strcpy(server->hostname, data->hostname);
	sb->u.nfs_sb.s_root = data->root;

	/* We now require that the mount process passes the remote address */
	memcpy(&srvaddr, &data->addr, sizeof(srvaddr));
	if (srvaddr.sin_addr.s_addr == INADDR_ANY) {
		printk("NFS: mount program didn't pass remote address!\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	/* Which protocol do we use? */
	tcp   = (data->flags & NFS_MOUNT_TCP);

	/* Initialize timeout values */
	timeparms.to_initval = data->timeo * HZ / 10;
	timeparms.to_retries = data->retrans;
	timeparms.to_maxval  = tcp? RPC_MAX_TCP_TIMEOUT : RPC_MAX_UDP_TIMEOUT;
	timeparms.to_exponential = 1;

	/* Choose authentication flavor */
	if (data->flags & NFS_MOUNT_SECURE) {
		authflavor = RPC_AUTH_DES;
	} else if (data->flags & NFS_MOUNT_KERBEROS) {
		authflavor = RPC_AUTH_KRB;
	} else {
		authflavor = RPC_AUTH_UNIX;
	}

	/* Now create transport and client */
	xprt = xprt_create_proto(tcp? IPPROTO_TCP : IPPROTO_UDP,
						&srvaddr, &timeparms);
	if (xprt == NULL) {
		printk("NFS: cannot create RPC transport.\n");
		goto failure;
	}

	clnt = rpc_create_client(xprt, server->hostname, &nfs_program,
						NFS_VERSION, authflavor);
	if (clnt == NULL) {
		printk("NFS: cannot create RPC client.\n");
		xprt_destroy(xprt);
		goto failure;
	}

	clnt->cl_intr     = (data->flags & NFS_MOUNT_INTR)? 1 : 0;
	clnt->cl_softrtry = (data->flags & NFS_MOUNT_SOFT)? 1 : 0;
	clnt->cl_chatty   = 1;
	server->client    = clnt;

	/* Fire up rpciod if not yet running */
	rpciod_up();

	/* Unlock super block and try to get root fh attributes */
	unlock_super(sb);

	if ((sb->s_mounted = nfs_fhget(sb, &data->root, NULL)) != NULL) {
		/* We're airborne */
		if (!(server->flags & NFS_MOUNT_NONLM))
			lockd_up();
		return sb;
	}

	/* Yargs. It didn't work out. */
	printk("nfs_read_super: get root inode failed\n");
	rpc_shutdown_client(server->client);
	rpciod_down();

failure:
	MOD_DEC_USE_COUNT;
	if (sb->s_lock)
		unlock_super(sb);
	sb->s_dev = 0;
	return NULL;
}

static void
nfs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
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
	copy_to_user(buf, &tmp, bufsiz);
}

/*
 * This is our own version of iget that looks up inodes by file handle
 * instead of inode number.  We use this technique instead of using
 * the vfs read_inode function because there is no way to pass the
 * file handle or current attributes into the read_inode function.
 * We just have to be careful not to subvert iget's special handling
 * of mount points.
 */
struct inode *
nfs_fhget(struct super_block *sb, struct nfs_fh *fhandle,
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
	dprintk("NFS: fhget(%x/%ld ct=%d)\n",
		inode->i_dev, inode->i_ino,
		atomic_read(&inode->i_count));

	return inode;
}

int
nfs_notify_change(struct inode *inode, struct iattr *attr)
{
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;
	int error;

	sattr.mode = (u32) -1;
	if (attr->ia_valid & ATTR_MODE) 
		sattr.mode = attr->ia_mode;

	sattr.uid = (u32) -1;
	if (attr->ia_valid & ATTR_UID)
		sattr.uid = attr->ia_uid;

	sattr.gid = (u32) -1;
	if (attr->ia_valid & ATTR_GID)
		sattr.gid = attr->ia_gid;


	sattr.size = (u32) -1;
	if ((attr->ia_valid & ATTR_SIZE) && S_ISREG(inode->i_mode))
		sattr.size = attr->ia_size;

	sattr.mtime.seconds = sattr.mtime.useconds = (u32) -1;
	if (attr->ia_valid & ATTR_MTIME) {
		sattr.mtime.seconds = attr->ia_mtime;
		sattr.mtime.useconds = 0;
	}

	sattr.atime.seconds = sattr.atime.useconds = (u32) -1;
	if (attr->ia_valid & ATTR_ATIME) {
		sattr.atime.seconds = attr->ia_atime;
		sattr.atime.useconds = 0;
	}

	error = nfs_proc_setattr(NFS_SERVER(inode), NFS_FH(inode),
		&sattr, &fattr);
	if (!error) {
		nfs_truncate_dirty_pages(inode, sattr.size);
		nfs_refresh_inode(inode, &fattr);
	}
	inode->i_dirt = 0;
	return error;
}

/*
 * Externally visible revalidation function
 */
int
nfs_revalidate(struct inode *inode)
{
	return nfs_revalidate_inode(NFS_SERVER(inode), inode);
}

/*
 * This function is called whenever some part of NFS notices that
 * the cached attributes have to be refreshed.
 *
 * This is a bit tricky because we have to make sure all dirty pages
 * have been sent off to the server before calling invalidate_inode_pages.
 * To make sure no other process adds more write requests while we try
 * our best to flush them, we make them sleep during the attribute refresh.
 *
 * A very similar scenario holds for the dir cache.
 */
int
_nfs_revalidate_inode(struct nfs_server *server, struct inode *inode)
{
	struct nfs_fattr fattr;
	int		 status;

	if (jiffies - NFS_READTIME(inode) < NFS_ATTRTIMEO(inode))
		return 0;

	dfprintk(PAGECACHE, "NFS: revalidating %x/%ld inode\n",
			inode->i_dev, inode->i_ino);
	NFS_READTIME(inode) = jiffies;
	if ((status = nfs_proc_getattr(server, NFS_FH(inode), &fattr)) < 0)
		goto done;

	nfs_refresh_inode(inode, &fattr);
	if (fattr.mtime.seconds != NFS_OLDMTIME(inode)) {
		if (!S_ISDIR(inode->i_mode)) {
			/* This sends off all dirty pages off to the server.
			 * Note that this function must not sleep. */
			nfs_invalidate_pages(inode);
			invalidate_inode_pages(inode);
		} else {
			nfs_invalidate_dircache(inode);
		}

		NFS_OLDMTIME(inode)  = fattr.mtime.seconds;
		NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
	} else {
		/* Update attrtimeo value */
		if ((NFS_ATTRTIMEO(inode) <<= 1) > NFS_MAXATTRTIMEO(inode))
			NFS_ATTRTIMEO(inode) = NFS_MAXATTRTIMEO(inode);
	}
	status = 0;

done:
	dfprintk(PAGECACHE,
		"NFS: inode %x/%ld revalidation complete (status %d).\n",
				inode->i_dev, inode->i_ino, status);
	return status;
}

/*
 * File system information
 */
static struct file_system_type nfs_fs_type = {
	"nfs",
	FS_NO_DCACHE,
	nfs_read_super,
	NULL
};

/*
 * Initialize NFS
 */
int
init_nfs_fs(void)
{
#ifdef CONFIG_PROC_FS
	rpc_proc_register(&nfs_rpcstat);
#endif
        return register_filesystem(&nfs_fs_type);
}

/*
 * Every kernel module contains stuff like this.
 */
#ifdef MODULE

EXPORT_NO_SYMBOLS;
/* Not quite true; I just maintain it */
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");

int
init_module(void)
{
	return init_nfs_fs();
}

void
cleanup_module(void)
{
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister("nfs");
#endif
	unregister_filesystem(&nfs_fs_type);
	nfs_free_dircache();
}
#endif
