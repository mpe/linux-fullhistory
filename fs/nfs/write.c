/*
 * linux/fs/nfs/write.c
 *
 * Writing file data over NFS.
 *
 * We do it like this: When a (user) process wishes to write data to an
 * NFS file, a write request is allocated that contains the RPC task data
 * plus some info on the page to be written, and added to the inode's
 * write chain. If the process writes past the end of the page, an async
 * RPC call to write the page is scheduled immediately; otherwise, the call
 * is delayed for a few seconds.
 *
 * Just like readahead, no async I/O is performed if wsize < PAGE_SIZE.
 *
 * Write requests are kept on the inode's writeback list. Each entry in
 * that list references the page (portion) to be written. When the
 * cache timeout has expired, the RPC task is woken up, and tries to
 * lock the page. As soon as it manages to do so, the request is moved
 * from the writeback list to the writelock list.
 *
 * Note: we must make sure never to confuse the inode passed in the
 * write_page request with the one in page->inode. As far as I understand
 * it, these are different when doing a swap-out.
 *
 * To understand everything that goes on here and in the NFS read code,
 * one should be aware that a page is locked in exactly one of the following
 * cases:
 *
 *  -	A write request is in progress.
 *  -	A user process is in generic_file_write/nfs_update_page
 *  -	A user process is in generic_file_read
 *
 * Also note that because of the way pages are invalidated in
 * nfs_revalidate_inode, the following assertions hold:
 *
 *  -	If a page is dirty, there will be no read requests (a page will
 *	not be re-read unless invalidated by nfs_revalidate_inode).
 *  -	If the page is not uptodate, there will be no pending write
 *	requests, and no process will be in nfs_update_page.
 *
 * FIXME: Interaction with the vmscan routines is not optimal yet.
 * Either vmscan must be made nfs-savvy, or we need a different page
 * reclaim concept that supports something like FS-independent
 * buffer_heads with a b_ops-> field.
 *
 * Copyright (C) 1996, 1997, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/swap.h>
#include <linux/pagemap.h>

#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <asm/uaccess.h>

#define NFS_PARANOIA 1
#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

static void			nfs_wback_begin(struct rpc_task *task);
static void			nfs_wback_result(struct rpc_task *task);
static void			nfs_cancel_request(struct nfs_wreq *req);

/*
 * Cache parameters
 */
#define NFS_WRITEBACK_DELAY	(10 * HZ)
#define NFS_WRITEBACK_MAX	64

/*
 * Limit number of delayed writes
 */
static int			nr_write_requests = 0;
static struct rpc_wait_queue	write_queue = RPC_INIT_WAITQ("write_chain");

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif

/*
 * Write a page synchronously.
 * Offset is the data offset within the page.
 */
static int
nfs_writepage_sync(struct dentry *dentry, struct inode *inode,
		struct page *page, unsigned long offset, unsigned int count)
{
	unsigned int	wsize = NFS_SERVER(inode)->wsize;
	int		result, refresh = 0, written = 0;
	u8		*buffer;
	struct nfs_fattr fattr;

	dprintk("NFS:      nfs_writepage_sync(%s/%s %d@%ld)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		count, page->offset + offset);

	buffer = (u8 *) page_address(page) + offset;
	offset += page->offset;

	do {
		if (count < wsize && !IS_SWAPFILE(inode))
			wsize = count;

		result = nfs_proc_write(NFS_DSERVER(dentry), NFS_FH(dentry),
					IS_SWAPFILE(inode), offset, wsize,
					buffer, &fattr);

		if (result < 0) {
			/* Must mark the page invalid after I/O error */
			clear_bit(PG_uptodate, &page->flags);
			goto io_error;
		}
		if (result != wsize)
			printk("NFS: short write, wsize=%u, result=%d\n",
			wsize, result);
		refresh = 1;
		buffer  += wsize;
		offset  += wsize;
		written += wsize;
		count   -= wsize;
		/*
		 * If we've extended the file, update the inode
		 * now so we don't invalidate the cache.
		 */
		if (offset > inode->i_size)
			inode->i_size = offset;
	} while (count);

io_error:
	/* Note: we don't refresh if the call failed (fattr invalid) */
	if (refresh && result >= 0) {
		/* See comments in nfs_wback_result */
		/* N.B. I don't think this is right -- sync writes in order */
		if (fattr.size < inode->i_size)
			fattr.size = inode->i_size;
		if (fattr.mtime.seconds < inode->i_mtime)
			printk("nfs_writepage_sync: prior time??\n");
		/* Solaris 2.5 server seems to send garbled
		 * fattrs occasionally */
		if (inode->i_ino == fattr.fileid) {
			/*
			 * We expect the mtime value to change, and
			 * don't want to invalidate the caches.
			 */
			inode->i_mtime = fattr.mtime.seconds;
			nfs_refresh_inode(inode, &fattr);
		} 
		else
			printk("nfs_writepage_sync: inode %ld, got %u?\n",
				inode->i_ino, fattr.fileid);
	}

	return written? written : result;
}

/*
 * Append a writeback request to a list
 */
static inline void
append_write_request(struct nfs_wreq **q, struct nfs_wreq *wreq)
{
	dprintk("NFS:      append_write_request(%p, %p)\n", q, wreq);
	rpc_append_list(q, wreq);
}

/*
 * Remove a writeback request from a list
 */
static inline void
remove_write_request(struct nfs_wreq **q, struct nfs_wreq *wreq)
{
	dprintk("NFS:      remove_write_request(%p, %p)\n", q, wreq);
	rpc_remove_list(q, wreq);
}

/*
 * Find a non-busy write request for a given page to
 * try to combine with.
 */
static inline struct nfs_wreq *
find_write_request(struct inode *inode, struct page *page)
{
	pid_t pid = current->pid;
	struct nfs_wreq	*head, *req;

	dprintk("NFS:      find_write_request(%x/%ld, %p)\n",
				inode->i_dev, inode->i_ino, page);
	if (!(req = head = NFS_WRITEBACK(inode)))
		return NULL;
	do {
		/*
		 * We can't combine with canceled requests or
		 * requests that have already been started..
		 */
		if (req->wb_flags & (NFS_WRITE_CANCELLED | NFS_WRITE_INPROGRESS))
			continue;

		if (req->wb_page == page && req->wb_pid == pid)
			return req;

		/*
		 * Ehh, don't keep too many tasks queued..
		 */
		rpc_wake_up_task(&req->wb_task);

	} while ((req = WB_NEXT(req)) != head);
	return NULL;
}

/*
 * Find and release all failed requests for this inode.
 */
int
nfs_check_failed_request(struct inode * inode)
{
	/* FIXME! */
	return 0;
}

/*
 * Try to merge adjacent write requests. This works only for requests
 * issued by the same user.
 */
static inline int
update_write_request(struct nfs_wreq *req, unsigned int first,
			unsigned int bytes)
{
	unsigned int	rqfirst = req->wb_offset,
			rqlast = rqfirst + req->wb_bytes,
			last = first + bytes;

	dprintk("nfs:      trying to update write request %p\n", req);

	/* not contiguous? */
	if (rqlast < first || last < rqfirst)
		return 0;

	/* Check the credentials associated with this write request.
	 * If the buffer is owned by the same user, we can happily
	 * add our data without risking server permission problems.
	 * Note that I'm not messing around with RPC root override creds
	 * here, because they're used by swap requests only which
	 * always write out full pages. */
	if (!rpcauth_matchcred(&req->wb_task, req->wb_task.tk_cred)) {
		dprintk("NFS:      update failed (cred mismatch)\n");
		return 0;
	}

	if (first < rqfirst)
		rqfirst = first;
	if (rqlast < last)
		rqlast = last;

	req->wb_offset = rqfirst;
	req->wb_bytes  = rqlast - rqfirst;
	req->wb_count++;

	return 1;
}

static inline void
free_write_request(struct nfs_wreq * req)
{
	if (!--req->wb_count)
		kfree(req);
}

/*
 * Create and initialize a writeback request
 */
static inline struct nfs_wreq *
create_write_request(struct dentry *dentry, struct inode *inode,
		struct page *page, unsigned int offset, unsigned int bytes)
{
	struct rpc_clnt	*clnt = NFS_CLIENT(inode);
	struct nfs_wreq *wreq;
	struct rpc_task	*task;

	dprintk("NFS:      create_write_request(%s/%s, %ld+%d)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		page->offset + offset, bytes);

	/* FIXME: Enforce hard limit on number of concurrent writes? */
	wreq = (struct nfs_wreq *) kmalloc(sizeof(*wreq), GFP_KERNEL);
	if (!wreq)
		goto out_fail;
	memset(wreq, 0, sizeof(*wreq));

	task = &wreq->wb_task;
	rpc_init_task(task, clnt, nfs_wback_result, RPC_TASK_NFSWRITE);
	task->tk_calldata = wreq;
	task->tk_action = nfs_wback_begin;

	rpcauth_lookupcred(task);	/* Obtain user creds */
	if (task->tk_status < 0)
		goto out_req;

	/* Put the task on inode's writeback request list. */
	wreq->wb_dentry = dentry;
	wreq->wb_inode  = inode;
	wreq->wb_pid    = current->pid;
	wreq->wb_page   = page;
	wreq->wb_offset = offset;
	wreq->wb_bytes  = bytes;
	wreq->wb_count	= 2;		/* One for the IO, one for us */

	append_write_request(&NFS_WRITEBACK(inode), wreq);

	if (nr_write_requests++ > NFS_WRITEBACK_MAX*3/4)
		rpc_wake_up_next(&write_queue);

	return wreq;

out_req:
	rpc_release_task(task);
	kfree(wreq);
out_fail:
	return NULL;
}

/*
 * Schedule a writeback RPC call.
 * If the server is congested, don't add to our backlog of queued
 * requests but call it synchronously.
 * The function returns whether we should wait for the thing or not.
 *
 * FIXME: Here we could walk the inode's lock list to see whether the
 * page we're currently writing to has been write-locked by the caller.
 * If it is, we could schedule an async write request with a long
 * delay in order to avoid writing back the page until the lock is
 * released.
 */
static inline int
schedule_write_request(struct nfs_wreq *req, int sync)
{
	struct rpc_task	*task = &req->wb_task;
	struct inode	*inode = req->wb_inode;

	if (NFS_CONGESTED(inode) || nr_write_requests >= NFS_WRITEBACK_MAX)
		sync = 1;

	if (sync) {
		sigset_t	oldmask;
		struct rpc_clnt *clnt = NFS_CLIENT(inode);
		dprintk("NFS: %4d schedule_write_request (sync)\n",
					task->tk_pid);
		/* Page is already locked */
		rpc_clnt_sigmask(clnt, &oldmask);
		rpc_execute(task);
		rpc_clnt_sigunmask(clnt, &oldmask);
	} else {
		dprintk("NFS: %4d schedule_write_request (async)\n",
					task->tk_pid);
		task->tk_flags |= RPC_TASK_ASYNC;
		task->tk_timeout = NFS_WRITEBACK_DELAY;
		rpc_sleep_on(&write_queue, task, NULL, NULL);
	}

	return sync;
}

/*
 * Wait for request to complete.
 */
static int
wait_on_write_request(struct nfs_wreq *req)
{
	struct rpc_clnt		*clnt = NFS_CLIENT(req->wb_inode);
	struct wait_queue	wait = { current, NULL };
	sigset_t		oldmask;
	int retval;

	/* Make sure it's started.. */
	if (!WB_INPROGRESS(req))
		rpc_wake_up_task(&req->wb_task);

	rpc_clnt_sigmask(clnt, &oldmask);
	add_wait_queue(&req->wb_wait, &wait);
	for (;;) {
		current->state = TASK_INTERRUPTIBLE;
		retval = 0;
		if (req->wb_flags & NFS_WRITE_COMPLETE)
			break;
		retval = -ERESTARTSYS;
		if (signalled())
			break;
		schedule();
	}
	remove_wait_queue(&req->wb_wait, &wait);
	current->state = TASK_RUNNING;
	rpc_clnt_sigunmask(clnt, &oldmask);
	return retval;
}

/*
 * Write a page to the server. This will be used for NFS swapping only
 * (for now), and we currently do this synchronously only.
 */
int
nfs_writepage(struct file * file, struct page *page)
{
	struct dentry *dentry = file->f_dentry;
	return nfs_writepage_sync(dentry, dentry->d_inode, page, 0, PAGE_SIZE);
}

/*
 * Update and possibly write a cached page of an NFS file.
 *
 * XXX: Keep an eye on generic_file_read to make sure it doesn't do bad
 * things with a page scheduled for an RPC call (e.g. invalidate it).
 */
int
nfs_updatepage(struct file *file, struct page *page, unsigned long offset, unsigned int count, int sync)
{
	struct dentry	*dentry = file->f_dentry;
	struct inode	*inode = dentry->d_inode;
	struct nfs_wreq	*req;
	int		synchronous = sync;
	int		retval;

	dprintk("NFS:      nfs_updatepage(%s/%s %d@%ld, sync=%d)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		count, page->offset+offset, sync);

	/*
	 * Try to find a corresponding request on the writeback queue.
	 * If there is one, we can be sure that this request is not
	 * yet being processed, because we hold a lock on the page.
	 *
	 * If the request was created by us, update it. Otherwise,
	 * transfer the page lock and flush out the dirty page now.
	 * After returning, generic_file_write will wait on the
	 * page and retry the update.
	 */
	req = find_write_request(inode, page);
	if (req && update_write_request(req, offset, count))
		goto updated;

	/*
	 * If wsize is smaller than page size, update and write
	 * page synchronously.
	 */
	if (NFS_SERVER(inode)->wsize < PAGE_SIZE)
		return nfs_writepage_sync(dentry, inode, page, offset, count);

	/* Create the write request. */
	req = create_write_request(dentry, inode, page, offset, count);
	if (!req)
		return -ENOBUFS;

	/*
	 * Ok, there's another user of this page with the new request..
	 * The IO completion will then free the page and the dentry.
	 */
	atomic_inc(&page->count);
	dget(dentry);

	/* Schedule request */
	synchronous = schedule_write_request(req, sync);

updated:
	if (req->wb_bytes == PAGE_SIZE)
		set_bit(PG_uptodate, &page->flags);

	retval = count;
	if (synchronous) {
		int status = wait_on_write_request(req);
		if (status) {
			nfs_cancel_request(req);
			retval = status;
		} else {
			status = req->wb_status;
			if (status < 0)
				retval = status;
		}

		if (retval < 0)
			clear_bit(PG_uptodate, &page->flags);
	}

	free_write_request(req);
	return retval;
}

/*
 * Cancel a write request. We always mark it cancelled,
 * but if it's already in progress there's no point in
 * calling rpc_exit, and we don't want to overwrite the
 * tk_status field.
 */ 
static void
nfs_cancel_request(struct nfs_wreq *req)
{
	req->wb_flags |= NFS_WRITE_CANCELLED;
	if (!WB_INPROGRESS(req)) {
		rpc_exit(&req->wb_task, 0);
		rpc_wake_up_task(&req->wb_task);
	}
}

/*
 * Cancel all writeback requests, both pending and in progress.
 */
static void
nfs_cancel_dirty(struct inode *inode, pid_t pid)
{
	struct nfs_wreq *head, *req;

	req = head = NFS_WRITEBACK(inode);
	while (req != NULL) {
		if (pid == 0 || req->wb_pid == pid)
			nfs_cancel_request(req);
		if ((req = WB_NEXT(req)) == head)
			break;
	}
}

/*
 * If we're waiting on somebody else's request
 * we need to increment the counter during the
 * wait so that the request doesn't disappear
 * from under us during the wait..
 */
static int FASTCALL(wait_on_other_req(struct nfs_wreq *));
static int wait_on_other_req(struct nfs_wreq *req)
{
	int retval;
	req->wb_count++;
	retval = wait_on_write_request(req);
	free_write_request(req);
	return retval;
}

/*
 * This writes back a set of requests according to the condition.
 *
 * If this ever gets much more convoluted, use a fn pointer for
 * the condition..
 */
#define NFS_WB(inode, cond) { int retval = 0 ; \
	do { \
		struct nfs_wreq *req = NFS_WRITEBACK(inode); \
		struct nfs_wreq *head = req; \
		if (!req) break; \
		for (;;) { \
			if (!(req->wb_flags & NFS_WRITE_COMPLETE)) \
				if (cond) break; \
			req = WB_NEXT(req); \
			if (req == head) goto out; \
		} \
		retval = wait_on_other_req(req); \
	} while (!retval); \
out:	return retval; \
}

int
nfs_wb_all(struct inode *inode)
{
	NFS_WB(inode, 1);
}

/*
 * Write back all requests on one page - we do this before reading it.
 */
int
nfs_wb_page(struct inode *inode, struct page *page)
{
	NFS_WB(inode, req->wb_page == page);
}

/*
 * Write back all pending writes for one user.. 
 */
int
nfs_wb_pid(struct inode *inode, pid_t pid)
{
	NFS_WB(inode, req->wb_pid == pid);
}

/*
 * Flush all write requests for truncation:
 * 	Simplification of the comparison has the side-effect of
 *	causing all writes in an infested page to be waited upon.
 */
int
nfs_flush_trunc(struct inode *inode, unsigned long from)
{
	from &= PAGE_MASK;
	NFS_WB(inode, req->wb_page->offset >= from);
}

void
nfs_inval(struct inode *inode)
{
	nfs_cancel_dirty(inode,0);
}

/*
 * Check if a previous write operation returned an error
 */
int
nfs_check_error(struct inode *inode)
{
	/* FIXME! */
	return 0;
}

/*
 * The following procedures make up the writeback finite state machinery:
 *
 * 1.	Try to lock the page if not yet locked by us,
 *	set up the RPC call info, and pass to the call FSM.
 */
static void
nfs_wback_begin(struct rpc_task *task)
{
	struct nfs_wreq	*req = (struct nfs_wreq *) task->tk_calldata;
	struct page	*page = req->wb_page;
	struct dentry	*dentry = req->wb_dentry;

	dprintk("NFS: %4d nfs_wback_begin (%s/%s, status=%d flags=%x)\n",
		task->tk_pid, dentry->d_parent->d_name.name,
		dentry->d_name.name, task->tk_status, req->wb_flags);

	task->tk_status = 0;

	/* Setup the task struct for a writeback call */
	req->wb_flags |= NFS_WRITE_INPROGRESS;
	req->wb_args.fh     = NFS_FH(dentry);
	req->wb_args.offset = page->offset + req->wb_offset;
	req->wb_args.count  = req->wb_bytes;
	req->wb_args.buffer = (void *) (page_address(page) + req->wb_offset);

	rpc_call_setup(task, NFSPROC_WRITE, &req->wb_args, &req->wb_fattr, 0);

	return;
}

/*
 * 2.	Collect the result
 */
static void
nfs_wback_result(struct rpc_task *task)
{
	struct nfs_wreq *req = (struct nfs_wreq *) task->tk_calldata;
	struct inode	*inode = req->wb_inode;
	struct page	*page  = req->wb_page;
	int		status = task->tk_status;

	dprintk("NFS: %4d nfs_wback_result (%s/%s, status=%d, flags=%x)\n",
		task->tk_pid, req->wb_dentry->d_parent->d_name.name,
		req->wb_dentry->d_name.name, status, req->wb_flags);

	/* Set the WRITE_COMPLETE flag, but leave WRITE_INPROGRESS set */
	req->wb_flags |= NFS_WRITE_COMPLETE;
	req->wb_status = status;

	if (status < 0) {
		req->wb_flags |= NFS_WRITE_INVALIDATE;
	} else if (!WB_CANCELLED(req)) {
		struct nfs_fattr *fattr = &req->wb_fattr;
		/* Update attributes as result of writeback. 
		 * Beware: when UDP replies arrive out of order, we
		 * may end up overwriting a previous, bigger file size.
		 *
		 * When the file size shrinks we cancel all pending
		 * writebacks. 
		 */
		if (fattr->mtime.seconds >= inode->i_mtime) {
			if (fattr->size < inode->i_size)
				fattr->size = inode->i_size;

			/* possible Solaris 2.5 server bug workaround */
			if (inode->i_ino == fattr->fileid) {
				/*
				 * We expect these values to change, and
				 * don't want to invalidate the caches.
				 */
				inode->i_size  = fattr->size;
				inode->i_mtime = fattr->mtime.seconds;
				nfs_refresh_inode(inode, fattr);
			}
			else
				printk("nfs_wback_result: inode %ld, got %u?\n",
					inode->i_ino, fattr->fileid);
		}
	}

	rpc_release_task(task);

	if (WB_INVALIDATE(req))
		clear_bit(PG_uptodate, &page->flags);

	__free_page(page);
	remove_write_request(&NFS_WRITEBACK(inode), req);
	nr_write_requests--;
	dput(req->wb_dentry);

	wake_up(&req->wb_wait);
	
	/*
	 * FIXME!
	 *
	 * We should not free the request here if it has pending
	 * error status on it. We should just leave it around, to
	 * let the error be collected later. However, the error
	 * collecting routines are too stupid for that right now,
	 * so we just drop the error on the floor at this point
	 * for any async writes.
	 *
	 * This should not be a major headache to fix, but I want
	 * to validate basic operations first.
	 */
	free_write_request(req);
}
