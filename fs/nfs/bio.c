/*
 * linux/fs/nfs/bio.c
 *
 * Block I/O for NFS
 *
 * Partial copy of Linus' read cache modifications to fs/nfs/file.c
 * modified for async RPC by okir@monad.swb.de
 *
 * We do an ugly hack here in order to return proper error codes to the
 * user program when a read request failed. This is a huge problem because
 * generic_file_read only checks the return value of inode->i_op->readpage()
 * which is usually 0 for async RPC. To overcome this obstacle, we set
 * the error bit of the page to 1 when an error occurs, and make nfs_readpage
 * transmit requests synchronously when encountering this.
 *
 * Another possible solution to this problem may be to have a cache of recent
 * RPC call results indexed by page pointer, or even a result code field
 * in struct page.
 *
 * June 96: Added retries of RPCs that seem to have failed for a transient
 * reason.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/nfs_fs.h>
#include <linux/nfsiod.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>

#include <asm/segment.h>
#include <asm/system.h>

#undef DEBUG_BIO
#ifdef DEBUG_BIO
#define dprintk(args...)	printk(## args)
#else
#define dprintk(args...)	/* nothing */
#endif

static inline int
do_read_nfs_sync(struct inode * inode, struct page * page)
{
	struct nfs_fattr fattr;
	int		result, refresh = 0;
	int		count = PAGE_SIZE;
	int		rsize = NFS_SERVER(inode)->rsize;
	char		*buf = (char *) page_address(page);
	unsigned long	pos = page->offset;

	dprintk("NFS: do_read_nfs_sync(%p)\n", page);

	set_bit(PG_locked, &page->flags);
	clear_bit(PG_error, &page->flags);

	do {
		if (count < rsize)
			rsize = count;
		result = nfs_proc_read(NFS_SERVER(inode), NFS_FH(inode), 
			pos, rsize, buf, &fattr);
		dprintk("nfs_proc_read(%s, (%x,%lx), %ld, %d, %p) = %d\n",
				NFS_SERVER(inode)->hostname,
				inode->i_dev, inode->i_ino,
				pos, rsize, buf, result);
		/*
		 * Even if we had a partial success we can't mark the page
		 * cache valid.
		 */
		if (result < 0)
			goto io_error;
		refresh = 1;
		count -= result;
		pos += result;
		buf += result;
		if (result < rsize)
			break;
	} while (count);

	memset(buf, 0, count);
	set_bit(PG_uptodate, &page->flags);
	result = 0;

io_error:
	if (refresh)
		nfs_refresh_inode(inode, &fattr);
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	return result;
}

/*
 * This is the function to (re-) transmit an NFS readahead request
 */
static int
nfsiod_read_setup(struct nfsiod_req *req)
{
	struct inode	*inode = req->rq_inode;
	struct page	*page = req->rq_page;

	return nfs_proc_read_request(&req->rq_rpcreq,
			NFS_SERVER(inode), NFS_FH(inode),
			page->offset, PAGE_SIZE, 
			(__u32 *) page_address(page));
}

/*
 * This is the callback from nfsiod telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
static int
nfsiod_read_result(int result, struct nfsiod_req *req)
{
	struct nfs_server *server = NFS_SERVER(req->rq_inode);
	struct page	*page = req->rq_page;
	static int	succ = 0, fail = 0;
	int		i;

	dprintk("BIO: received callback for page %p, result %d\n",
			page, result);

	if (result >= 0) {
		struct nfs_fattr	fattr;

		result = nfs_proc_read_reply(&req->rq_rpcreq, &fattr);
		if (result >= 0) {
			nfs_refresh_inode(req->rq_inode, &fattr);
			if (result < PAGE_SIZE)
				memset((u8 *) page_address(page)+result,
						0, PAGE_SIZE-result);
		}
	} else
	if (result == -ETIMEDOUT && !(server->flags & NFS_MOUNT_SOFT)) {
		/* XXX: Theoretically, we'd have to increment the initial
		 * timeo here; but I'm not going to bother with this now
		 * because this old nfsiod stuff will soon die anyway.
		 */
		result = -EAGAIN;
	}

	if (result == -EAGAIN && req->rq_retries--) {
		dprintk("BIO: retransmitting request.\n");
		memset(&req->rq_rpcreq, 0, sizeof(struct rpc_ioreq));
		while (rpc_reserve(server->rsock, &req->rq_rpcreq, 1) < 0)
			schedule();
		current->fsuid = req->rq_fsuid;
		current->fsgid = req->rq_fsgid;
		for (i = 0; i < NGROUPS; i++)
			current->groups[i] = req->rq_groups[i];
		nfsiod_read_setup(req);
		return 0;
	}
	if (result >= 0) {
		set_bit(PG_uptodate, &page->flags);
		succ++;
	} else {
		dprintk("BIO: %d successful reads, %d failures\n", succ, fail);
		set_bit(PG_error, &page->flags);
		fail++;
	}
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	free_page(page_address(page));
	return 1;
}

static inline int
do_read_nfs_async(struct inode *inode, struct page *page)
{
	struct nfsiod_req *req;
	int		result, i;

	dprintk("NFS: do_read_nfs_async(%p)\n", page);

	set_bit(PG_locked, &page->flags);
	clear_bit(PG_error, &page->flags);

	if (!(req = nfsiod_reserve(NFS_SERVER(inode))))
		return -EAGAIN;

	req->rq_retries = 5;
	req->rq_callback = nfsiod_read_result;
	req->rq_inode = inode;
	req->rq_page = page;

	req->rq_fsuid = current->fsuid;
	req->rq_fsgid = current->fsgid;
	for (i = 0; i < NGROUPS; i++)
		req->rq_groups[i] = current->groups[i];

	if ((result = nfsiod_read_setup(req)) >= 0) {
		page->count++;
		nfsiod_enqueue(req);
	} else {
		dprintk("NFS: deferring async READ request.\n");
		nfsiod_release(req);
		clear_bit(PG_locked, &page->flags);
		wake_up(&page->wait);
	}

	return result < 0? result : 0;
}

int
nfs_readpage(struct inode *inode, struct page *page)
{
	unsigned long	address;
	int		error = -1;

	dprintk("NFS: nfs_readpage %08lx\n", page_address(page));
	address = page_address(page);
	page->count++;
	if (!PageError(page) && NFS_SERVER(inode)->rsize >= PAGE_SIZE)
		error = do_read_nfs_async(inode, page);
	if (error < 0)		/* couldn't enqueue */
		error = do_read_nfs_sync(inode, page);
	free_page(address);
	return error;
}
