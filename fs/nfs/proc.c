/*
 *  linux/fs/nfs/proc.c
 *
 *  Copyright (C) 1992, 1993, 1994  Rick Sladkey
 *
 *  OS-independent nfs remote procedure call functions
 *
 *  Tuned by Alan Cox <A.Cox@swansea.ac.uk> for >3K buffers
 *  so at last we can have decent(ish) throughput off a 
 *  Sun server.
 *
 *  Coding optimized and cleaned up by Florian La Roche.
 *  Note: Error returns are optimized for NFS_OK, which isn't translated via
 *  nfs_stat_to_errno(), but happens to be already the right return code.
 *
 *  FixMe: We ought to define a sensible small max size for
 *  things like getattr that are tiny packets and use the
 *  old get_free_page stuff with it.
 *
 *  Also, the code currently doesn't check the size of the packet, when
 *  it decodes the packet.
 *
 *  Feel free to fix it and mail me the diffs if it worries you.
 *
 *  Completely rewritten to support the new RPC call interface;
 *  rewrote and moved the entire XDR stuff to xdr.c
 *  --Olaf Kirch June 1996
 */

#define NFS_NEED_XDR_TYPES

#include <linux/param.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>

#include <asm/segment.h>

#ifdef NFS_DEBUG
# define NFSDBG_FACILITY	NFSDBG_PROC
#endif


/*
 * One function for each procedure in the NFS protocol.
 */
int
nfs_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr)
{
	int	status;

	dprintk("NFS call  getattr\n");
	status = rpc_call(server->client, NFSPROC_GETATTR, fhandle, fattr, 0);
	dprintk("NFS reply getattr\n");
	return status;
}

int
nfs_proc_setattr(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_sattr *sattr, struct nfs_fattr *fattr)
{
	struct nfs_sattrargs	arg = { fhandle, sattr };
	int	status;

	dprintk("NFS call  setattr\n");
	status = rpc_call(server->client, NFSPROC_SETATTR, &arg, fattr, 0);
	dprintk("NFS reply setattr\n");
	return status;
}

int
nfs_proc_lookup(struct nfs_server *server, struct nfs_fh *dir, const char *name,
		    struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_diropargs	arg = { dir, name };
	struct nfs_diropok	res = { fhandle, fattr };
	int			status;

	dprintk("NFS call  lookup %s\n", name);
	status = rpc_call(server->client, NFSPROC_LOOKUP, &arg, &res, 0);
	dprintk("NFS reply lookup: %d\n", status);
	return status;
}

int
nfs_proc_readlink(struct nfs_server *server, struct nfs_fh *fhandle,
			void **p0, char **string, unsigned int *len,
			unsigned int maxlen)
{
	struct nfs_readlinkres	res = { string, len, maxlen, NULL };
	int			status;

	dprintk("NFS call  readlink\n");
	status = rpc_call(server->client, NFSPROC_READLINK, fhandle, &res, 0);
	dprintk("NFS reply readlink: %d\n", status);
	if (!status)
		*p0 = res.buffer;
	else if (res.buffer)
		kfree(res.buffer);
	return status;
}

int
nfs_proc_read(struct nfs_server *server, struct nfs_fh *fhandle, int swap,
			  unsigned long offset, unsigned int count,
			  void *buffer, struct nfs_fattr *fattr)
{
	struct nfs_readargs	arg = { fhandle, offset, count, buffer };
	struct nfs_readres	res = { fattr, count };
	int			status;

	dprintk("NFS call  read %d @ %ld\n", count, offset);
	status = rpc_call(server->client, NFSPROC_READ, &arg, &res,
			swap? NFS_RPC_SWAPFLAGS : 0);
	dprintk("NFS reply read: %d\n", status);
	return status;
}

int
nfs_proc_write(struct nfs_server *server, struct nfs_fh *fhandle, int swap,
			unsigned long offset, unsigned int count,
			const void *buffer, struct nfs_fattr *fattr)
{
	struct nfs_writeargs	arg = { fhandle, offset, count, buffer };
	int			status;

	dprintk("NFS call  write %d @ %ld\n", count, offset);
	status = rpc_call(server->client, NFSPROC_WRITE, &arg, fattr,
			swap? (RPC_TASK_SWAPPER|RPC_TASK_ROOTCREDS) : 0);
	dprintk("NFS reply read: %d\n", status);
	return status < 0? status : count;
}

int
nfs_proc_create(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct nfs_sattr *sattr,
			struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_createargs	arg = { dir, name, sattr };
	struct nfs_diropok	res = { fhandle, fattr };
	int			status;

	dprintk("NFS call  create %s\n", name);
	status = rpc_call(server->client, NFSPROC_CREATE, &arg, &res, 0);
	dprintk("NFS reply create: %d\n", status);
	return status;
}

int
nfs_proc_remove(struct nfs_server *server, struct nfs_fh *dir, const char *name)
{
	struct nfs_diropargs	arg = { dir, name };
	int			status;

	dprintk("NFS call  remove %s\n", name);
	status = rpc_call(server->client, NFSPROC_REMOVE, &arg, NULL, 0);
	dprintk("NFS reply remove: %d\n", status);
	return status;
}

int
nfs_proc_rename(struct nfs_server *server,
		struct nfs_fh *old_dir, const char *old_name,
		struct nfs_fh *new_dir, const char *new_name)
{
	struct nfs_renameargs	arg = { old_dir, old_name, new_dir, new_name };
	int			status;

	dprintk("NFS call  rename %s -> %s\n", old_name, new_name);
	status = rpc_call(server->client, NFSPROC_RENAME, &arg, NULL, 0);
	dprintk("NFS reply rename: %d\n", status);
	return status;
}

int
nfs_proc_link(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fh *dir, const char *name)
{
	struct nfs_linkargs	arg = { fhandle, dir, name };
	int			status;

	dprintk("NFS call  link %s\n", name);
	status = rpc_call(server->client, NFSPROC_LINK, &arg, NULL, 0);
	dprintk("NFS reply link: %d\n", status);
	return status;
}

int
nfs_proc_symlink(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, const char *path,
			struct nfs_sattr *sattr)
{
	struct nfs_symlinkargs	arg = { dir, name, path, sattr };
	int			status;

	dprintk("NFS call  symlink %s -> %s\n", name, path);
	status = rpc_call(server->client, NFSPROC_SYMLINK, &arg, NULL, 0);
	dprintk("NFS reply symlink: %d\n", status);
	return status;
}

int
nfs_proc_mkdir(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct nfs_sattr *sattr,
			struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_createargs	arg = { dir, name, sattr };
	struct nfs_diropok	res = { fhandle, fattr };
	int			status;

	dprintk("NFS call  mkdir %s\n", name);
	status = rpc_call(server->client, NFSPROC_MKDIR, &arg, &res, 0);
	dprintk("NFS reply mkdir: %d\n", status);
	return status;
}

int
nfs_proc_rmdir(struct nfs_server *server, struct nfs_fh *dir, const char *name)
{
	struct nfs_diropargs	arg = { dir, name };
	int			status;

	dprintk("NFS call  rmdir %s\n", name);
	status = rpc_call(server->client, NFSPROC_RMDIR, &arg, NULL, 0);
	dprintk("NFS reply rmdir: %d\n", status);
	return status;
}

/*
 * The READDIR implementation is somewhat hackish - we pass a temporary
 * buffer to the encode function, which installs it in the receive
 * iovec. The dirent buffer itself is passed in the result struct.
 */
int
nfs_proc_readdir(struct nfs_server *server, struct nfs_fh *fhandle,
			u32 cookie, unsigned int size, __u32 *entry)
{
	struct nfs_readdirargs	arg;
	struct nfs_readdirres	res;
	void *			buffer;
	unsigned int		buf_size = PAGE_SIZE;
	int			status;

	/* First get a temp buffer for the readdir reply */
	/* N.B. does this really need to be cleared? */
	status = -ENOMEM;
	buffer = (void *) get_free_page(GFP_KERNEL);
	if (!buffer)
		goto out;

	/*
	 * Calculate the effective size the buffer.  To make sure
	 * that the returned data will fit into the user's buffer,
	 * we decrease the buffer size as necessary.
	 *
	 * Note: NFS returns three __u32 values for each entry,
	 * and we assume that the data is packed into the user
	 * buffer with the same efficiency. 
	 */
	if (size < buf_size)
		buf_size = size;
	if (server->rsize < buf_size)
		buf_size = server->rsize;
#if 0
printk("nfs_proc_readdir: user size=%d, rsize=%d, buf_size=%d\n",
size, server->rsize, buf_size);
#endif

	arg.fh = fhandle;
	arg.cookie = cookie;
	arg.buffer = buffer;
	arg.bufsiz = buf_size;
	res.buffer = entry;
	res.bufsiz = size;

	dprintk("NFS call  readdir %d\n", cookie);
	status = rpc_call(server->client, NFSPROC_READDIR, &arg, &res, 0);
	dprintk("NFS reply readdir: %d\n", status);
	free_page((unsigned long) buffer);
out:
	return status;
}

int
nfs_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fsinfo *info)
{
	int	status;

	dprintk("NFS call  statfs\n");
	status = rpc_call(server->client, NFSPROC_STATFS, fhandle, info, 0);
	dprintk("NFS reply statfs: %d\n", status);
	return status;
}
