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
 * This is the callback from nfsiod telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
static void
nfs_read_cb(int result, struct nfsiod_req *req)
{
	struct page	*page = (struct page *) req->rq_cdata;
	static int	succ = 0, fail = 0;

	dprintk("BIO: received callback for page %p, result %d\n",
			page, result);

	if (result >= 0
	 && (result = nfs_proc_read_reply(&req->rq_rpcreq)) >= 0) {
		succ++;
		set_bit(PG_uptodate, &page->flags);
	} else {
		fail++;
		printk("BIO: %d successful reads, %d failures\n", succ, fail);
		set_bit(PG_error, &page->flags);
	}
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	free_page(page_address(page));
}

static inline int
do_read_nfs_async(struct inode *inode, struct page *page)
{
	struct nfsiod_req *req;
	int		result = -1;	/* totally arbitrary */

	dprintk("NFS: do_read_nfs_async(%p)\n", page);

	set_bit(PG_locked, &page->flags);
	clear_bit(PG_error, &page->flags);

	if (!(req = nfsiod_reserve(NFS_SERVER(inode), nfs_read_cb)))
		goto done;
	result = nfs_proc_read_request(&req->rq_rpcreq,
			NFS_SERVER(inode), NFS_FH(inode),
			page->offset, PAGE_SIZE, 
			(__u32 *) page_address(page));
	if (result >= 0) {
		req->rq_cdata = page;
		page->count++;
		result = nfsiod_enqueue(req);
		if (result >= 0)
			dprintk("NFS: enqueued async READ request.\n");
	}
	if (result < 0) {
		dprintk("NFS: deferring async READ request.\n");
		nfsiod_release(req);
		clear_bit(PG_locked, &page->flags);
		wake_up(&page->wait);
	}

done:
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
