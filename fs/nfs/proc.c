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
#include <linux/nfs.h>
#include <linux/nfs2.h>
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
	fattr->valid = 0;
	status = rpc_call(server->client, NFSPROC_GETATTR, fhandle, fattr, 0);
	dprintk("NFS reply getattr\n");
	return status;
}

int
nfs_proc_setattr(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr, struct iattr *sattr)
{
	struct nfs_sattrargs	arg = { fhandle, sattr };
	int	status;

	dprintk("NFS call  setattr\n");
	fattr->valid = 0;
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
	fattr->valid = 0;
	status = rpc_call(server->client, NFSPROC_LOOKUP, &arg, &res, 0);
	dprintk("NFS reply lookup: %d\n", status);
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
	fattr->valid = 0;
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
	struct nfs_writeargs	arg = { fhandle, offset, count,
					NFS_FILE_SYNC, 1,
					{{(void *) buffer, count}, {0,0}, {0,0}, {0,0},
					{0,0}, {0,0}, {0,0}, {0,0}}};
	struct nfs_writeverf	verf;
	struct nfs_writeres	res = {fattr, &verf, count};
	int			status;

	dprintk("NFS call  write %d @ %ld\n", count, offset);
	fattr->valid = 0;
	status = rpc_call(server->client, NFSPROC_WRITE, &arg, &res,
			swap? (RPC_TASK_SWAPPER|RPC_TASK_ROOTCREDS) : 0);
	dprintk("NFS reply read: %d\n", status);
	return status < 0? status : count;
}

int
nfs_proc_create(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct iattr *sattr,
			struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_createargs	arg = { dir, name, sattr };
	struct nfs_diropok	res = { fhandle, fattr };
	int			status;

	dprintk("NFS call  create %s\n", name);
	fattr->valid = 0;
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
			struct iattr *sattr)
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
			const char *name, struct iattr *sattr,
			struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_createargs	arg = { dir, name, sattr };
	struct nfs_diropok	res = { fhandle, fattr };
	int			status;

	dprintk("NFS call  mkdir %s\n", name);
	fattr->valid = 0;
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
 * the receive iovec. The decode function just parses the reply to make
 * sure it is syntactically correct; the entries itself are decoded
 * from nfs_readdir by calling the decode_entry function directly.
 */
int
nfs_proc_readdir(struct dentry *dir, struct nfs_fattr *dir_attr,
		 __u64 cookie, void *entry, unsigned int size, int plus)
{
	struct nfs_readdirargs	arg;
	struct nfs_readdirres	res;
	struct rpc_message	msg = { NFSPROC_READDIR, &arg, &res, NULL };
	struct nfs_server       *server = NFS_DSERVER(dir);
	int			status;

	if (server->rsize < size)
		size = server->rsize;

	dir_attr->valid = 0;
	arg.fh = NFS_FH(dir);
	arg.cookie = cookie;
	arg.buffer = entry;
	arg.bufsiz = size;
	res.buffer = entry;
	res.bufsiz = size;

	dir_attr->valid = 0;
	dprintk("NFS call  readdir %d\n", (unsigned int)cookie);
	status = rpc_call_sync(NFS_CLIENT(dir->d_inode), &msg, 0);

	dprintk("NFS reply readdir: %d\n", status);
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
