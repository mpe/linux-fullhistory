/*
 * linux/fs/nfs/read.c
 *
 * Block I/O for NFS
 *
 * Partial copy of Linus' read cache modifications to fs/nfs/file.c
 * modified for async RPC by okir@monad.swb.de
 *
 * We do an ugly hack here in order to return proper error codes to the
 * user program when a read request failed: since generic_file_read
 * only checks the return value of inode->i_op->readpage() which is always 0
 * for async RPC, we set the error bit of the page to 1 when an error occurs,
 * and make nfs_readpage transmit requests synchronously when encountering this.
 * This is only a small problem, though, since we now retry all operations
 * within the RPC code when root squashing is suspected.
 */

#define NFS_NEED_XDR_TYPES
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>

#include <asm/segment.h>
#include <asm/system.h>

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

struct nfs_rreq {
	struct inode *		ra_inode;	/* inode from which to read */
	struct page *		ra_page;	/* page to be read */
	struct nfs_readargs	ra_args;	/* XDR argument struct */
	struct nfs_readres	ra_res;		/* ... and result struct */
	struct nfs_fattr	ra_fattr;	/* fattr storage */
};

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif


/*
 * Set up the NFS read request struct
 */
static inline void
nfs_readreq_setup(struct nfs_rreq *req, struct nfs_fh *fh,
		  unsigned long offset, void *buffer, unsigned int rsize)
{
	req->ra_args.fh     = fh;
	req->ra_args.offset = offset;
	req->ra_args.count  = rsize;
	req->ra_args.buffer = buffer;
	req->ra_res.fattr   = &req->ra_fattr;
	req->ra_res.count   = rsize;
}


/*
 * Read a page synchronously.
 */
static int
nfs_readpage_sync(struct dentry *dentry, struct inode *inode, struct page *page)
{
	struct nfs_rreq	rqst;
	unsigned long	offset = page->offset;
	char		*buffer = (char *) page_address(page);
	int		rsize = NFS_SERVER(inode)->rsize;
	int		result, refresh = 0;
	int		count = PAGE_SIZE;
	int		flags = IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0;

	dprintk("NFS: nfs_readpage_sync(%p)\n", page);
	clear_bit(PG_error, &page->flags);

	do {
		if (count < rsize)
			rsize = count;

		dprintk("NFS: nfs_proc_read(%s, (%s/%s), %ld, %d, %p)\n",
			NFS_SERVER(inode)->hostname,
			dentry->d_parent->d_name.name, dentry->d_name.name,
			offset, rsize, buffer);

		/* Set up arguments and perform rpc call */
		nfs_readreq_setup(&rqst, NFS_FH(dentry), offset, buffer, rsize);
		result = rpc_call(NFS_CLIENT(inode), NFSPROC_READ,
					&rqst.ra_args, &rqst.ra_res, flags);

		/*
		 * Even if we had a partial success we can't mark the page
		 * cache valid.
		 */
		if (result < 0) {
			if (result == -EISDIR)
				result = -EINVAL;
			goto io_error;
		}
		refresh = 1;
		count  -= result;
		offset += result;
		buffer += result;
		if (result < rsize)	/* NFSv2ism */
			break;
	} while (count);

	memset(buffer, 0, count);
	set_bit(PG_uptodate, &page->flags);
	result = 0;

io_error:
	/* Note: we don't refresh if the call returned error */
	if (refresh && result >= 0)
		nfs_refresh_inode(inode, &rqst.ra_fattr);
	/* N.B. Use nfs_unlock_page here? */
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	return result;
}

/*
 * This is the callback from RPC telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
static void
nfs_readpage_result(struct rpc_task *task)
{
	struct nfs_rreq	*req = (struct nfs_rreq *) task->tk_calldata;
	struct page	*page = req->ra_page;
	unsigned long	address = page_address(page);
	int		result = task->tk_status;
	static int	succ = 0, fail = 0;

	dprintk("NFS: %4d received callback for page %lx, result %d\n",
			task->tk_pid, address, result);

	if (result >= 0) {
		result = req->ra_res.count;
		if (result < PAGE_SIZE) {
			memset((char *) address + result, 0, PAGE_SIZE - result);
		}
		nfs_refresh_inode(req->ra_inode, &req->ra_fattr);
		set_bit(PG_uptodate, &page->flags);
		succ++;
	} else {
		set_bit(PG_error, &page->flags);
		fail++;
		dprintk("NFS: %d successful reads, %d failures\n", succ, fail);
	}
	/* N.B. Use nfs_unlock_page here? */
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);

	free_page(address);

	rpc_release_task(task);
	kfree(req);
}

static inline int
nfs_readpage_async(struct dentry *dentry, struct inode *inode,
			struct page *page)
{
	unsigned long address = page_address(page);
	struct nfs_rreq	*req;
	int		result = -1, flags;

	dprintk("NFS: nfs_readpage_async(%p)\n", page);
	if (NFS_CONGESTED(inode))
		goto out_defer;

	/* N.B. Do we need to test? Never called for swapfile inode */
	flags = RPC_TASK_ASYNC | (IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0);
	req = (struct nfs_rreq *) rpc_allocate(flags, sizeof(*req));
	if (!req)
		goto out_defer;

	/* Initialize request */
	/* N.B. Will the dentry remain valid for life of request? */
	nfs_readreq_setup(req, NFS_FH(dentry), page->offset,
				(void *) address, PAGE_SIZE);
	req->ra_inode = inode;
	req->ra_page = page; /* count has been incremented by caller */

	/* Start the async call */
	dprintk("NFS: executing async READ request.\n");
	result = rpc_do_call(NFS_CLIENT(inode), NFSPROC_READ,
				&req->ra_args, &req->ra_res, flags,
				nfs_readpage_result, req);
	if (result < 0)
		goto out_free;
	result = 0;
out:
	return result;

out_defer:
	dprintk("NFS: deferring async READ request.\n");
	goto out;
out_free:
	dprintk("NFS: failed to enqueue async READ request.\n");
	kfree(req);
	goto out;
}

/*
 * Read a page over NFS.
 * We read the page synchronously in the following cases:
 *  -	The file is a swap file. Swap-ins are always sync operations,
 *	so there's no need bothering to make async reads 100% fail-safe.
 *  -	The NFS rsize is smaller than PAGE_SIZE. We could kludge our way
 *	around this by creating several consecutive read requests, but
 *	that's hardly worth it.
 *  -	The error flag is set for this page. This happens only when a
 *	previous async read operation failed.
 *  -	The server is congested.
 */
int
nfs_readpage(struct file *file, struct page *page)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	int		error;

	dprintk("NFS: nfs_readpage (%p %ld@%ld)\n",
		page, PAGE_SIZE, page->offset);
	atomic_inc(&page->count);
	set_bit(PG_locked, &page->flags);

	/*
	 * Try to flush any pending writes to the file..
	 *
	 * NOTE! Because we own the page lock, there cannot
	 * be any new pending writes generated at this point
	 * for this page (other pages can be written to).
	 */
	error = nfs_wb_page(inode, page);
	if (error)
		goto out_error;

	error = -1;
	if (!IS_SWAPFILE(inode) && !PageError(page) &&
	    NFS_SERVER(inode)->rsize >= PAGE_SIZE)
		error = nfs_readpage_async(dentry, inode, page);
	if (error >= 0)
		goto out;

	error = nfs_readpage_sync(dentry, inode, page);
	if (error < 0 && IS_SWAPFILE(inode))
		printk("Aiee.. nfs swap-in of page failed!\n");
	goto out_free;

out_error:
	clear_bit(PG_locked, &page->flags);
out_free:
	free_page(page_address(page));
out:
	return error;
}
