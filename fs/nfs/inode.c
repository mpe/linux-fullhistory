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
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#define CONFIG_NFS_SNAPSHOT 1
#define NFSDBG_FACILITY		NFSDBG_VFS
#define NFS_PARANOIA 1

static struct inode * __nfs_fhget(struct super_block *, struct nfs_fattr *);
void nfs_zap_caches(struct inode *);
static void nfs_invalidate_inode(struct inode *);

static void nfs_read_inode(struct inode *);
static void nfs_put_inode(struct inode *);
static void nfs_delete_inode(struct inode *);
static void nfs_put_super(struct super_block *);
static void nfs_umount_begin(struct super_block *);
static int  nfs_statfs(struct super_block *, struct statfs *);

static struct super_operations nfs_sops = { 
	read_inode:	nfs_read_inode,
	put_inode:	nfs_put_inode,
	delete_inode:	nfs_delete_inode,
	put_super:	nfs_put_super,
	statfs:		nfs_statfs,
	umount_begin:	nfs_umount_begin,
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
	NFS_FILEID(inode) = 0;
	NFS_FSID(inode) = 0;
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

	lock_kernel();
	if (S_ISDIR(inode->i_mode)) {
		nfs_free_dircache(inode);
	} else {
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
	}

	failed = nfs_check_failed_request(inode);
	if (failed)
		printk("NFS: inode %ld had %d failed requests\n",
			inode->i_ino, failed);
	unlock_kernel();

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

extern struct nfs_fh *nfs_fh_alloc(void);
extern void nfs_fh_free(struct nfs_fh *p);

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

	sb->s_flags |= MS_ODD_RENAME; /* This should go away */

	sb->s_magic      = NFS_SUPER_MAGIC;
	sb->s_op         = &nfs_sops;
	sb->s_blocksize  = nfs_block_size(data->bsize, &sb->s_blocksize_bits);
	sb->u.nfs_sb.s_root = data->root;
	server           = &sb->u.nfs_sb.s_server;
	server->rsize    = nfs_block_size(data->rsize, NULL);
	server->wsize    = nfs_block_size(data->wsize, NULL);
	server->flags    = data->flags;

	if (data->flags & NFS_MOUNT_NOAC) {
		data->acregmin = data->acregmax = 0;
		data->acdirmin = data->acdirmax = 0;
	}
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
	root_fh = nfs_fh_alloc();
	if (!root_fh)
		goto out_no_fh;
	*root_fh = data->root;

	if (nfs_proc_getattr(server, root_fh, &fattr) != 0)
		goto out_no_fattr;

	root_inode = __nfs_fhget(sb, &fattr);
	if (!root_inode)
		goto out_no_root;
	sb->s_root = d_alloc_root(root_inode);
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
	nfs_fh_free(root_fh);
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
nfs_statfs(struct super_block *sb, struct statfs *buf)
{
	int error;
	struct nfs_fsinfo res;

	error = nfs_proc_statfs(&sb->u.nfs_sb.s_server, &sb->u.nfs_sb.s_root,
		&res);
	if (error) {
		printk("nfs_statfs: statfs error = %d\n", -error);
		res.bsize = res.blocks = res.bfree = res.bavail = -1;
	}
	buf->f_type = NFS_SUPER_MAGIC;
	buf->f_bsize = res.bsize;
	buf->f_blocks = res.blocks;
	buf->f_bfree = res.bfree;
	buf->f_bavail = res.bavail;
	buf->f_namelen = NAME_MAX;
	return 0;
}

/*
 * Free all unused dentries in an inode's alias list.
 *
 * Subtle note: we have to be very careful not to cause
 * any IO operations with the stale dentries, as this
 * could cause file corruption. But since the dentry
 * count is 0 and all pending IO for a dentry has been
 * flushed when the count went to 0, we're safe here.
 * Also returns the number of unhashed dentries
 */
static int
nfs_free_dentries(struct inode *inode)
{
	struct list_head *tmp, *head = &inode->i_dentry;
	int unhashed;

restart:
	tmp = head;
	unhashed = 0;
	while ((tmp = tmp->next) != head) {
		struct dentry *dentry = list_entry(tmp, struct dentry, d_alias);
		dprintk("nfs_free_dentries: found %s/%s, d_count=%d, hashed=%d\n",
			dentry->d_parent->d_name.name, dentry->d_name.name,
			dentry->d_count, !d_unhashed(dentry));
		if (!list_empty(&dentry->d_subdirs))
			shrink_dcache_parent(dentry);
		if (!dentry->d_count) {
			dget(dentry);
			d_drop(dentry);
			dput(dentry);
			goto restart;
		}
		if (d_unhashed(dentry))
			unhashed++;
	}
	return unhashed;
}

/*
 * Invalidate the local caches
 */
void
nfs_zap_caches(struct inode *inode)
{
	NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
	NFS_CACHEINV(inode);

	invalidate_inode_pages(inode);
	if (S_ISDIR(inode->i_mode))
		nfs_flush_dircache(inode);
}

/*
 * Invalidate, but do not unhash, the inode
 */
static void
nfs_invalidate_inode(struct inode *inode)
{
	umode_t save_mode = inode->i_mode;

	make_bad_inode(inode);
	inode->i_mode = save_mode;
	nfs_inval(inode);
	nfs_zap_caches(inode);
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
		NFS_FILEID(inode) = fattr->fileid;
		NFS_FSID(inode) = fattr->fsid;
		inode->i_mode = fattr->mode;
		/* Why so? Because we want revalidate for devices/FIFOs, and
		 * that's precisely what we have in nfs_file_inode_operations.
		 */
		inode->i_op = &nfs_file_inode_operations;
		if (S_ISREG(inode->i_mode)) {
			inode->i_fop = &nfs_file_operations;
			inode->i_data.a_ops = &nfs_file_aops;
		} else if (S_ISDIR(inode->i_mode)) {
			inode->i_op = &nfs_dir_inode_operations;
			inode->i_fop = &nfs_dir_operations;
		} else if (S_ISLNK(inode->i_mode))
			inode->i_op = &nfs_symlink_inode_operations;
		else
			init_special_inode(inode, inode->i_mode, fattr->rdev);
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
 * In NFSv3 we can have 64bit inode numbers. In order to support
 * this, and re-exported directories (also seen in NFSv2)
 * we are forced to allow 2 different inodes to have the same
 * i_ino.
 */
static int
nfs_find_actor(struct inode *inode, unsigned long ino, void *opaque)
{
	struct nfs_fattr *fattr = (struct nfs_fattr *)opaque;
	if (NFS_FSID(inode) != fattr->fsid)
		return 0;
	if (NFS_FILEID(inode) != fattr->fileid)
		return 0;
	return 1;
}

static int
nfs_inode_is_stale(struct inode *inode, struct nfs_fattr *fattr)
{
	int unhashed;
	int is_stale = 0;

	if (inode->i_mode &&
	    (fattr->mode & S_IFMT) != (inode->i_mode & S_IFMT))
		is_stale = 1;

	if (is_bad_inode(inode))
		is_stale = 1;

	/*
	 * If the inode seems stale, free up cached dentries.
	 */
	unhashed = nfs_free_dentries(inode);

	/* Assume we're holding an i_count
	 *
	 * NB: sockets sometimes have volatile file handles
	 *     don't invalidate their inodes even if all dentries are
	 *     unhashed.
	 */
	if (unhashed && inode->i_count == unhashed + 1
	    && !S_ISSOCK(inode->i_mode) && !S_ISFIFO(inode->i_mode))
		is_stale = 1;

	return is_stale;
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
	struct inode *inode = NULL;
	unsigned long ino;

	if (!fattr->nlink) {
		printk("NFS: Buggy server - nlink == 0!\n");
		goto out_no_inode;
	}

	ino = fattr->fileid;

	while((inode = iget4(sb, ino, nfs_find_actor, fattr)) != NULL) {

		/*
		 * Check for busy inodes, and attempt to get rid of any
		 * unused local references. If successful, we release the
		 * inode and try again.
		 *
		 * Note that the busy test uses the values in the fattr,
		 * as the inode may have become a different object.
		 * (We can probably handle modes changes here, too.)
		 */
		if (!nfs_inode_is_stale(inode,fattr))
			break;

		dprintk("__nfs_fhget: inode %ld still busy, i_count=%d\n",
		       inode->i_ino, inode->i_count);
		nfs_zap_caches(inode);
		remove_inode_hash(inode);
		iput(inode);
	}

	if (!inode)
		goto out_no_inode;

	nfs_fill_inode(inode, fattr);
	dprintk("NFS: __nfs_fhget(%x/%ld ct=%d)\n",
		inode->i_dev, inode->i_ino, inode->i_count);

out:
	return inode;

out_no_inode:
	printk("__nfs_fhget: iget failed\n");
	goto out;
}

int
nfs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct nfs_fattr fattr;
	int error;

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

	if (!S_ISREG(inode->i_mode))
		attr->ia_valid &= ~ATTR_SIZE;

	error = nfs_wb_all(inode);
	if (error)
		goto out;

	error = nfs_proc_setattr(NFS_DSERVER(dentry), NFS_FH(dentry),
				&fattr, attr);
	if (error)
		goto out;
	/*
	 * If we changed the size or mtime, update the inode
	 * now to avoid invalidating the page cache.
	 */
	if (attr->ia_valid & ATTR_SIZE) {
		if (attr->ia_size != fattr.size)
			printk("nfs_notify_change: attr=%Ld, fattr=%d??\n",
			       (long long) attr->ia_size, fattr.size);
		inode->i_mtime = fattr.mtime.seconds;
		vmtruncate(inode, attr->ia_size);
	}
	if (attr->ia_valid & ATTR_MTIME)
		inode->i_mtime = fattr.mtime.seconds;
	error = nfs_refresh_inode(inode, &fattr);
out:
	return error;
}

/*
 * Wait for the inode to get unlocked.
 * (Used for NFS_INO_LOCKED and NFS_INO_REVALIDATING).
 */
int
nfs_wait_on_inode(struct inode *inode, int flag)
{
	struct task_struct	*tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	int			intr, error = 0;

	intr = NFS_SERVER(inode)->flags & NFS_MOUNT_INTR;
	add_wait_queue(&inode->i_wait, &wait);
	for (;;) {
		set_task_state(tsk, (intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE));
		error = 0;
		if (!(NFS_FLAGS(inode) & flag))
			break;
		error = -ERESTARTSYS;
		if (intr && signalled())
			break;
		schedule();
	}
	set_task_state(tsk, TASK_RUNNING);
	remove_wait_queue(&inode->i_wait, &wait);
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
 * These are probably going to contain hooks for
 * allocating and releasing RPC credentials for
 * the file. I'll have to think about Tronds patch
 * a bit more..
 */
int nfs_open(struct inode *inode, struct file *filp)
{
	return 0;
}

int nfs_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/*
 * This function is called whenever some part of NFS notices that
 * the cached attributes have to be refreshed.
 */
int
__nfs_revalidate_inode(struct nfs_server *server, struct dentry *dentry)
{
	struct inode	*inode = dentry->d_inode;
	int		 status = 0;
	struct nfs_fattr fattr;

	dfprintk(PAGECACHE, "NFS: revalidating %s/%s, ino=%ld\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		inode->i_ino);

	if (!inode || is_bad_inode(inode))
		return -ESTALE;

	while (NFS_REVALIDATING(inode)) {
		status = nfs_wait_on_inode(inode, NFS_INO_REVALIDATING);
		if (status < 0)
			return status;
		if (time_before(jiffies,NFS_READTIME(inode)+NFS_ATTRTIMEO(inode)))
			return 0;
	}
	NFS_FLAGS(inode) |= NFS_INO_REVALIDATING;

	status = nfs_proc_getattr(server, NFS_FH(dentry), &fattr);
	if (status) {
		int error;
		u32 *fh;
		struct nfs_fh fhandle;
		dfprintk(PAGECACHE, "nfs_revalidate_inode: %s/%s getattr failed, ino=%ld, error=%d\n",
			dentry->d_parent->d_name.name,
			dentry->d_name.name, inode->i_ino, status);
		if (status != -ESTALE)
			goto out;
		/*
		 * A "stale filehandle" error ... show the current fh
		 * and find out what the filehandle should be.
		 */
		fh = (u32 *) NFS_FH(dentry);
		dfprintk(PAGECACHE, "NFS: bad fh %08x%08x%08x%08x%08x%08x%08x%08x\n",
			fh[0],fh[1],fh[2],fh[3],fh[4],fh[5],fh[6],fh[7]);
		error = nfs_proc_lookup(server, NFS_FH(dentry->d_parent), 
					dentry->d_name.name, &fhandle, &fattr);
		if (error) {
			dfprintk(PAGECACHE, "NFS: lookup failed, error=%d\n", error);
			goto out;
		}
		fh = (u32 *) &fhandle;
		dfprintk(PAGECACHE, "            %08x%08x%08x%08x%08x%08x%08x%08x\n",
			fh[0],fh[1],fh[2],fh[3],fh[4],fh[5],fh[6],fh[7]);
		goto out;
	}

	status = nfs_refresh_inode(inode, &fattr);
	if (status) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: %s/%s refresh failed, ino=%ld, error=%d\n",
			dentry->d_parent->d_name.name,
			dentry->d_name.name, inode->i_ino, status);
		goto out;
	}
	dfprintk(PAGECACHE, "NFS: %s/%s revalidation complete\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
out:
	NFS_FLAGS(inode) &= ~NFS_INO_REVALIDATING;
	wake_up(&inode->i_wait);
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
	/*
	 * No need to worry about unhashing the dentry, as the
	 * lookup validation will know that the inode is bad.
	 */
	nfs_invalidate_inode(inode);
	goto out;

out_invalid:
#ifdef NFS_DEBUG_VERBOSE
printk("nfs_refresh_inode: invalidating %ld pages\n", inode->i_nrpages);
#endif
	nfs_zap_caches(inode);
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

extern int nfs_init_fhcache(void);
extern int nfs_init_wreqcache(void);

/*
 * Initialize NFS
 */
int
init_nfs_fs(void)
{
	int err;

	err = nfs_init_fhcache();
	if (err)
		return err;

	err = nfs_init_wreqcache();
	if (err)
		return err;

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
}
#endif
