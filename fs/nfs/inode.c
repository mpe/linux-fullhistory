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

#define CONFIG_NFS_SNAPSHOT 1
#define NFSDBG_FACILITY		NFSDBG_VFS
#define NFS_PARANOIA 1

static struct inode * __nfs_fhget(struct super_block *, struct nfs_fattr *);

static void nfs_read_inode(struct inode *);
static void nfs_put_inode(struct inode *);
static void nfs_delete_inode(struct inode *);
static int  nfs_notify_change(struct dentry *, struct iattr *);
static void nfs_put_super(struct super_block *);
static void nfs_umount_begin(struct super_block *);
static int  nfs_statfs(struct super_block *, struct statfs *, int);

static struct super_operations nfs_sops = { 
	nfs_read_inode,		/* read inode */
	NULL,			/* write inode */
	nfs_put_inode,		/* put inode */
	nfs_delete_inode,	/* delete inode */
	nfs_notify_change,	/* notify change */
	nfs_put_super,		/* put superblock */
	NULL,			/* write superblock */
	nfs_statfs,		/* stat filesystem */
	NULL,			/* no remount */
	NULL,			/* no clear inode */
	nfs_umount_begin	/* umount attempt begin */
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
	inode->i_rdev = 0;
	inode->i_op = NULL;
	NFS_CACHEINV(inode);
	NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
}

static void
nfs_put_inode(struct inode * inode)
{
	dprintk("NFS: put_inode(%x/%ld)\n", inode->i_dev, inode->i_ino);
	/*
	 * We want to get rid of unused inodes ...
	 */
	if (inode->i_count == 1)
		inode->i_nlink = 0;
}

static void
nfs_delete_inode(struct inode * inode)
{
	int failed;

	dprintk("NFS: delete_inode(%x/%ld)\n", inode->i_dev, inode->i_ino);
	/*
	 * Flush out any pending write requests ...
	 */
	if (NFS_WRITEBACK(inode) != NULL) {
		unsigned long timeout = jiffies + 5*HZ;
#ifdef NFS_DEBUG_VERBOSE
printk("nfs_delete_inode: inode %ld has pending RPC requests\n", inode->i_ino);
#endif
		nfs_inval(inode);
		while (NFS_WRITEBACK(inode) != NULL &&
		       time_before(jiffies, timeout)) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(HZ/10);
		}
		current->state = TASK_RUNNING;
		if (NFS_WRITEBACK(inode) != NULL)
			printk("NFS: Arghhh, stuck RPC requests!\n");
	}

	failed = nfs_check_failed_request(inode);
	if (failed)
		printk("NFS: inode %ld had %d failed requests\n",
			inode->i_ino, failed);
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
	/*
	 * Invalidate the dircache for this superblock.
	 */
	nfs_invalidate_dircache_sb(sb);

	kfree(server->hostname);

	MOD_DEC_USE_COUNT;
}

void
nfs_umount_begin(struct super_block *sb)
{
	struct nfs_server *server = &sb->u.nfs_sb.s_server;
	struct rpc_clnt	*rpc;

	/* -EIO all pending I/O */
	if ((rpc = server->client) != NULL)
		rpc_killall_tasks(rpc);
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
	struct nfs_server	*server;
	struct rpc_xprt		*xprt;
	struct rpc_clnt		*clnt;
	struct nfs_fh		*root_fh;
	struct inode		*root_inode;
	unsigned int		authflavor;
	int			tcp;
	struct sockaddr_in	srvaddr;
	struct rpc_timeout	timeparms;
	struct nfs_fattr	fattr;

	MOD_INC_USE_COUNT;
	if (!data)
		goto out_miss_args;

	if (data->version != NFS_MOUNT_VERSION) {
		printk("nfs warning: mount version %s than kernel\n",
			data->version < NFS_MOUNT_VERSION ? "older" : "newer");
		if (data->version < 2)
			data->namlen = 0;
		if (data->version < 3)
			data->bsize  = 0;
	}

	/* We now require that the mount process passes the remote address */
	memcpy(&srvaddr, &data->addr, sizeof(srvaddr));
	if (srvaddr.sin_addr.s_addr == INADDR_ANY)
		goto out_no_remote;

	lock_super(sb);

	sb->s_magic      = NFS_SUPER_MAGIC;
	sb->s_op         = &nfs_sops;
	sb->s_blocksize  = nfs_block_size(data->bsize, &sb->s_blocksize_bits);
	sb->u.nfs_sb.s_root = data->root;
	server           = &sb->u.nfs_sb.s_server;
	server->rsize    = nfs_block_size(data->rsize, NULL);
	server->wsize    = nfs_block_size(data->wsize, NULL);
	server->flags    = data->flags;
	server->acregmin = data->acregmin*HZ;
	server->acregmax = data->acregmax*HZ;
	server->acdirmin = data->acdirmin*HZ;
	server->acdirmax = data->acdirmax*HZ;

	server->hostname = kmalloc(strlen(data->hostname) + 1, GFP_KERNEL);
	if (!server->hostname)
		goto out_unlock;
	strcpy(server->hostname, data->hostname);

	/* Which protocol do we use? */
	tcp   = (data->flags & NFS_MOUNT_TCP);

	/* Initialize timeout values */
	timeparms.to_initval = data->timeo * HZ / 10;
	timeparms.to_retries = data->retrans;
	timeparms.to_maxval  = tcp? RPC_MAX_TCP_TIMEOUT : RPC_MAX_UDP_TIMEOUT;
	timeparms.to_exponential = 1;

	/* Now create transport and client */
	xprt = xprt_create_proto(tcp? IPPROTO_TCP : IPPROTO_UDP,
						&srvaddr, &timeparms);
	if (xprt == NULL)
		goto out_no_xprt;

	/* Choose authentication flavor */
	authflavor = RPC_AUTH_UNIX;
	if (data->flags & NFS_MOUNT_SECURE)
		authflavor = RPC_AUTH_DES;
	else if (data->flags & NFS_MOUNT_KERBEROS)
		authflavor = RPC_AUTH_KRB;

	clnt = rpc_create_client(xprt, server->hostname, &nfs_program,
						NFS_VERSION, authflavor);
	if (clnt == NULL)
		goto out_no_client;

	clnt->cl_intr     = (data->flags & NFS_MOUNT_INTR)? 1 : 0;
	clnt->cl_softrtry = (data->flags & NFS_MOUNT_SOFT)? 1 : 0;
	clnt->cl_chatty   = 1;
	server->client    = clnt;

	/* Fire up rpciod if not yet running */
	if (rpciod_up() != 0)
		goto out_no_iod;

	/*
	 * Keep the super block locked while we try to get 
	 * the root fh attributes.
	 */
	root_fh = kmalloc(sizeof(struct nfs_fh), GFP_KERNEL);
	if (!root_fh)
		goto out_no_fh;
	*root_fh = data->root;

	if (nfs_proc_getattr(server, root_fh, &fattr) != 0)
		goto out_no_fattr;

	root_inode = __nfs_fhget(sb, &fattr);
	if (!root_inode)
		goto out_no_root;
	sb->s_root = d_alloc_root(root_inode, NULL);
	if (!sb->s_root)
		goto out_no_root;
	sb->s_root->d_op = &nfs_dentry_operations;
	sb->s_root->d_fsdata = root_fh;

	/* We're airborne */
	unlock_super(sb);

	/* Check whether to start the lockd process */
	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_up();
	return sb;

	/* Yargs. It didn't work out. */
out_no_root:
	printk("nfs_read_super: get root inode failed\n");
	iput(root_inode);
	goto out_free_fh;

out_no_fattr:
	printk("nfs_read_super: get root fattr failed\n");
out_free_fh:
	kfree(root_fh);
out_no_fh:
	rpciod_down();
	goto out_shutdown;

out_no_iod:
	printk(KERN_WARNING "NFS: couldn't start rpciod!\n");
out_shutdown:
	rpc_shutdown_client(server->client);
	goto out_free_host;

out_no_client:
	printk(KERN_WARNING "NFS: cannot create RPC client.\n");
	xprt_destroy(xprt);
	goto out_free_host;

out_no_xprt:
	printk(KERN_WARNING "NFS: cannot create RPC transport.\n");

out_free_host:
	kfree(server->hostname);
out_unlock:
	unlock_super(sb);
	goto out_fail;

out_no_remote:
	printk("NFS: mount program didn't pass remote address!\n");
	goto out_fail;

out_miss_args:
	printk("nfs_read_super: missing data argument\n");

out_fail:
	sb->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return NULL;
}

static int
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
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}

/*
 * Free all unused dentries in an inode's alias list.
 *
 * Subtle note: we have to be very careful not to cause
 * any IO operations with the stale dentries, as this
 * could cause file corruption. But since the dentry
 * count is 0 and all pending IO for a dentry has been
 * flushed when the count went to 0, we're safe here.
 */
void nfs_free_dentries(struct inode *inode)
{
	struct list_head *tmp, *head = &inode->i_dentry;

restart:
	tmp = head;
	while ((tmp = tmp->next) != head) {
		struct dentry *dentry = list_entry(tmp, struct dentry, d_alias);
printk("nfs_free_dentries: found %s/%s, d_count=%d, hashed=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
dentry->d_count, !list_empty(&dentry->d_hash));
		if (!dentry->d_count) {
			dget(dentry);
			d_drop(dentry);
			dput(dentry);
			goto restart;
		}
	}
}

/*
 * Fill in inode information from the fattr.
 */
static void
nfs_fill_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	/*
	 * Check whether the mode has been set, as we only want to
	 * do this once. (We don't allow inodes to change types.)
	 */
	if (inode->i_mode == 0) {
		inode->i_mode = fattr->mode;
		if (S_ISREG(inode->i_mode))
			inode->i_op = &nfs_file_inode_operations;
		else if (S_ISDIR(inode->i_mode))
			inode->i_op = &nfs_dir_inode_operations;
		else if (S_ISLNK(inode->i_mode))
			inode->i_op = &nfs_symlink_inode_operations;
		else if (S_ISCHR(inode->i_mode)) {
			inode->i_op = &chrdev_inode_operations;
			inode->i_rdev = to_kdev_t(fattr->rdev);
		} else if (S_ISBLK(inode->i_mode)) {
			inode->i_op = &blkdev_inode_operations;
			inode->i_rdev = to_kdev_t(fattr->rdev);
		} else if (S_ISFIFO(inode->i_mode))
			init_fifo(inode);
		else
			inode->i_op = NULL;
		/*
		 * Preset the size and mtime, as there's no need
		 * to invalidate the caches.
		 */ 
		inode->i_size  = fattr->size;
		inode->i_mtime = fattr->mtime.seconds;
		NFS_OLDMTIME(inode) = fattr->mtime.seconds;
	}
	nfs_refresh_inode(inode, fattr);
}

/*
 * This is our own version of iget that looks up inodes by file handle
 * instead of inode number.  We use this technique instead of using
 * the vfs read_inode function because there is no way to pass the
 * file handle or current attributes into the read_inode function.
 *
 * We provide a special check for NetApp .snapshot directories to avoid
 * inode aliasing problems. All snapshot inodes are anonymous (unhashed).
 */
struct inode *
nfs_fhget(struct dentry *dentry, struct nfs_fh *fhandle,
				 struct nfs_fattr *fattr)
{
	struct super_block *sb = dentry->d_sb;

	dprintk("NFS: nfs_fhget(%s/%s fileid=%d)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		fattr->fileid);

	/* Install the file handle in the dentry */
	*((struct nfs_fh *) dentry->d_fsdata) = *fhandle;

#ifdef CONFIG_NFS_SNAPSHOT
	/*
	 * Check for NetApp snapshot dentries, and get an 
	 * unhashed inode to avoid aliasing problems.
	 */
	if ((dentry->d_parent->d_inode->u.nfs_i.flags & NFS_IS_SNAPSHOT) ||
	    (dentry->d_name.len == 9 &&
	     memcmp(dentry->d_name.name, ".snapshot", 9) == 0)) {
		struct inode *inode = get_empty_inode();
		if (!inode)
			goto out;	
		inode->i_sb = sb;
		inode->i_dev = sb->s_dev;
		inode->i_flags = 0;
		inode->i_ino = fattr->fileid;
		nfs_read_inode(inode);
		nfs_fill_inode(inode, fattr);
		inode->u.nfs_i.flags |= NFS_IS_SNAPSHOT;
		dprintk("NFS: nfs_fhget(snapshot ino=%ld)\n", inode->i_ino);
	out:
		return inode;
	}
#endif
	return __nfs_fhget(sb, fattr);
}

/*
 * Look up the inode by super block and fattr->fileid.
 *
 * Note carefully the special handling of busy inodes (i_count > 1).
 * With the kernel 2.1.xx dcache all inodes except hard links must
 * have i_count == 1 after iget(). Otherwise, it indicates that the
 * server has reused a fileid (i_ino) and we have a stale inode.
 */
static struct inode *
__nfs_fhget(struct super_block *sb, struct nfs_fattr *fattr)
{
	struct inode *inode;
	int max_count;

retry:
	inode = iget(sb, fattr->fileid);
	if (!inode)
		goto out_no_inode;
	/* N.B. This should be impossible ... */
	if (inode->i_ino != fattr->fileid)
		goto out_bad_id;

	/*
	 * Check for busy inodes, and attempt to get rid of any
	 * unused local references. If successful, we release the
	 * inode and try again.
	 *
	 * Note that the busy test uses the values in the fattr,
	 * as the inode may have become a different object.
	 * (We can probably handle modes changes here, too.)
	 */
	max_count = S_ISDIR(fattr->mode) ? 1 : fattr->nlink;
	if (inode->i_count > max_count) {
printk("__nfs_fhget: inode %ld busy, i_count=%d, i_nlink=%d\n",
inode->i_ino, inode->i_count, inode->i_nlink);
		nfs_free_dentries(inode);
		if (inode->i_count > max_count) {
printk("__nfs_fhget: inode %ld still busy, i_count=%d\n",
inode->i_ino, inode->i_count);
			if (!list_empty(&inode->i_dentry)) {
				struct dentry *dentry;
				dentry = list_entry(inode->i_dentry.next,
						 struct dentry, d_alias);
printk("__nfs_fhget: killing %s/%s filehandle\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
				memset(dentry->d_fsdata, 0, 
					sizeof(struct nfs_fh));
			} else
				printk("NFS: inode %ld busy, no aliases?\n",
					inode->i_ino);
			make_bad_inode(inode);
			remove_inode_hash(inode);
		}
		iput(inode);
		goto retry;
	}
	nfs_fill_inode(inode, fattr);
	dprintk("NFS: __nfs_fhget(%x/%ld ct=%d)\n",
		inode->i_dev, inode->i_ino, inode->i_count);

out:
	return inode;

out_no_inode:
	printk("__nfs_fhget: iget failed\n");
	goto out;
out_bad_id:
	printk("__nfs_fhget: unexpected inode from iget\n");
	goto out;
}

int
nfs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error;
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;

	/*
	 * Make sure the inode is up-to-date.
	 */
	error = nfs_revalidate(dentry);
	if (error) {
#ifdef NFS_PARANOIA
printk("nfs_notify_change: revalidate failed, error=%d\n", error);
#endif
		goto out;
	}

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
	if ((attr->ia_valid & ATTR_SIZE) && S_ISREG(inode->i_mode)) {
		sattr.size = attr->ia_size;
		nfs_flush_trunc(inode, sattr.size);
	}

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

	error = nfs_proc_setattr(NFS_DSERVER(dentry), NFS_FH(dentry),
				&sattr, &fattr);
	if (error)
		goto out;
	/*
	 * If we changed the size or mtime, update the inode
	 * now to avoid invalidating the page cache.
	 */
	if (sattr.size != (u32) -1) {
		if (sattr.size != fattr.size)
			printk("nfs_notify_change: sattr=%d, fattr=%d??\n",
				sattr.size, fattr.size);
		inode->i_size  = sattr.size;
		inode->i_mtime = fattr.mtime.seconds;
	}
	if (sattr.mtime.seconds != (u32) -1)
		inode->i_mtime = fattr.mtime.seconds;
	error = nfs_refresh_inode(inode, &fattr);
out:
	return error;
}

/*
 * Externally visible revalidation function
 */
int
nfs_revalidate(struct dentry *dentry)
{
	return nfs_revalidate_inode(NFS_DSERVER(dentry), dentry);
}

/*
 * This function is called whenever some part of NFS notices that
 * the cached attributes have to be refreshed.
 */
int
_nfs_revalidate_inode(struct nfs_server *server, struct dentry *dentry)
{
	struct inode	*inode = dentry->d_inode;
	int		 status = 0;
	struct nfs_fattr fattr;

	/* Don't bother revalidating if we've done it recently */
	if (jiffies - NFS_READTIME(inode) < NFS_ATTRTIMEO(inode))
		goto out;

	dfprintk(PAGECACHE, "NFS: revalidating %s/%s, ino=%ld\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		inode->i_ino);
	status = nfs_proc_getattr(server, NFS_FH(dentry), &fattr);
	if (status) {
		int error;
		u32 *fh;
		struct nfs_fh fhandle;
#ifdef NFS_PARANOIA
printk("nfs_revalidate_inode: %s/%s getattr failed, ino=%ld, error=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, inode->i_ino, status);
#endif
		if (status != -ESTALE)
			goto out;
		/*
		 * A "stale filehandle" error ... show the current fh
		 * and find out what the filehandle should be.
		 */
		fh = (u32 *) NFS_FH(dentry);
		printk("NFS: bad fh %08x%08x%08x%08x%08x%08x%08x%08x\n",
			fh[0],fh[1],fh[2],fh[3],fh[4],fh[5],fh[6],fh[7]);
		error = nfs_proc_lookup(server, NFS_FH(dentry->d_parent), 
					dentry->d_name.name, &fhandle, &fattr);
		if (error) {
			printk("NFS: lookup failed, error=%d\n", error);
			goto out;
		}
		fh = (u32 *) &fhandle;
		printk("            %08x%08x%08x%08x%08x%08x%08x%08x\n",
			fh[0],fh[1],fh[2],fh[3],fh[4],fh[5],fh[6],fh[7]);
		goto out;
	}

	status = nfs_refresh_inode(inode, &fattr);
	if (status) {
#ifdef NFS_PARANOIA
printk("nfs_revalidate_inode: %s/%s refresh failed, ino=%ld, error=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, inode->i_ino, status);
#endif
		goto out;
	}
	dfprintk(PAGECACHE, "NFS: %s/%s revalidation complete\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
out:
	return status;
}

/*
 * Many nfs protocol calls return the new file attributes after
 * an operation.  Here we update the inode to reflect the state
 * of the server's inode.
 *
 * This is a bit tricky because we have to make sure all dirty pages
 * have been sent off to the server before calling invalidate_inode_pages.
 * To make sure no other process adds more write requests while we try
 * our best to flush them, we make them sleep during the attribute refresh.
 *
 * A very similar scenario holds for the dir cache.
 */
int
nfs_refresh_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	int invalid = 0;
	int error = -EIO;

	dfprintk(VFS, "NFS: refresh_inode(%x/%ld ct=%d)\n",
		 inode->i_dev, inode->i_ino, inode->i_count);

	if (!inode || !fattr) {
		printk("nfs_refresh_inode: inode or fattr is NULL\n");
		goto out;
	}
	if (inode->i_ino != fattr->fileid) {
		printk("nfs_refresh_inode: mismatch, ino=%ld, fattr=%d\n",
			inode->i_ino, fattr->fileid);
		goto out;
	}

	/*
	 * Make sure the inode's type hasn't changed.
	 */
	if ((inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT))
		goto out_changed;

	inode->i_mode = fattr->mode;
	inode->i_nlink = fattr->nlink;
	inode->i_uid = fattr->uid;
	inode->i_gid = fattr->gid;

	inode->i_blocks = fattr->blocks;
	inode->i_atime = fattr->atime.seconds;
	inode->i_ctime = fattr->ctime.seconds;

	/*
	 * Update the read time so we don't revalidate too often.
	 */
	NFS_READTIME(inode) = jiffies;
	error = 0;

	/*
	 * If we have pending write-back entries, we don't want
	 * to look at the size or the mtime the server sends us
	 * too closely, as we're in the middle of modifying them.
	 */
	if (NFS_WRITEBACK(inode))
		goto out;

	if (inode->i_size != fattr->size) {
#ifdef NFS_DEBUG_VERBOSE
printk("NFS: size change on %x/%ld\n", inode->i_dev, inode->i_ino);
#endif
		inode->i_size = fattr->size;
		invalid = 1;
	}

	if (inode->i_mtime != fattr->mtime.seconds) {
#ifdef NFS_DEBUG_VERBOSE
printk("NFS: mtime change on %x/%ld\n", inode->i_dev, inode->i_ino);
#endif
		inode->i_mtime = fattr->mtime.seconds;
		invalid = 1;
	}

	if (invalid)
		goto out_invalid;

	/* Update attrtimeo value */
	if (fattr->mtime.seconds == NFS_OLDMTIME(inode)) {
		if ((NFS_ATTRTIMEO(inode) <<= 1) > NFS_MAXATTRTIMEO(inode))
			NFS_ATTRTIMEO(inode) = NFS_MAXATTRTIMEO(inode);
	}
	NFS_OLDMTIME(inode) = fattr->mtime.seconds;

out:
	return error;

out_changed:
	/*
	 * Big trouble! The inode has become a different object.
	 */
#ifdef NFS_PARANOIA
printk("nfs_refresh_inode: inode %ld mode changed, %07o to %07o\n",
inode->i_ino, inode->i_mode, fattr->mode);
#endif
	fattr->mode = inode->i_mode; /* save mode */
	make_bad_inode(inode);
	nfs_inval(inode);
	inode->i_mode = fattr->mode; /* restore mode */
	/*
	 * No need to worry about unhashing the dentry, as the
	 * lookup validation will know that the inode is bad.
	 * (But we fall through to invalidate the caches.)
	 */

out_invalid:
	/*
	 * Invalidate the local caches
	 */
#ifdef NFS_DEBUG_VERBOSE
printk("nfs_refresh_inode: invalidating %ld pages\n", inode->i_nrpages);
#endif
	if (!S_ISDIR(inode->i_mode))
		invalidate_inode_pages(inode);
	else
		nfs_invalidate_dircache(inode);
	NFS_CACHEINV(inode);
	NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
	goto out;
}

/*
 * File system information
 */
static struct file_system_type nfs_fs_type = {
	"nfs",
	0 /* FS_NO_DCACHE - this doesn't work right now*/,
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
	rpc_register_sysctl();
	rpc_proc_init();
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
