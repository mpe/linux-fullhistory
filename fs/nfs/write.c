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
 * write_page request with the one in page->inode. As far as I understant
 * it, these are different when doing a swap-out.
 *
 * To understand everything that goes one here and in the nfs read code,
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

#define NFS_NEED_XDR_TYPES
#include <linux/config.h>
#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/swap.h>
#include <linux/pagemap.h>

#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <asm/uaccess.h>

/*
 * NOTE! We must NOT default to soft-mounting: that breaks too many
 * programs that depend on POSIX behaviour of uninterruptible reads
 * and writes.
 *
 * Until we have a per-mount soft/hard mount policy that we can honour
 * we must default to hard mounting!
 */
#define IS_SOFT 0

#define NFS_PARANOIA 1
#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

static void			nfs_wback_lock(struct rpc_task *task);
static void			nfs_wback_result(struct rpc_task *task);

/*
 * Cache parameters
 */
#define NFS_WRITEBACK_DELAY	(10 * HZ)
#define NFS_WRITEBACK_MAX	64

/*
 * Limit number of delayed writes
 */
static int			nr_write_requests = 0;
static int			nr_failed_requests = 0;
static struct rpc_wait_queue	write_queue = RPC_INIT_WAITQ("write_chain");
struct nfs_wreq *		nfs_failed_requests = NULL;

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif

/*
 * Unlock a page after writing it
 */
static inline void
nfs_unlock_page(struct page *page)
{
	dprintk("NFS:      unlock %ld\n", page->offset);
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);

#ifdef CONFIG_NFS_SWAP
	/* async swap-out support */
	if (test_and_clear_bit(PG_decr_after, &page->flags))
		atomic_dec(&page->count);
	if (test_and_clear_bit(PG_swap_unlock_after, &page->flags)) {
		/*
		 * We're doing a swap, so check that this page is
		 * swap-cached and do the necessary cleanup. 
		 */
		swap_after_unlock_page(page->offset);
	}
#endif
}

/*
 * Transfer a page lock to a write request waiting for it.
 */
static inline void
transfer_page_lock(struct nfs_wreq *req)
{
	dprintk("NFS:      transfer_page_lock\n");

	req->wb_flags &= ~NFS_WRITE_WANTLOCK;
	req->wb_flags |= NFS_WRITE_LOCKED;
	rpc_wake_up_task(&req->wb_task);

	dprintk("NFS:      wake up task %d (flags %x)\n",
			req->wb_task.tk_pid, req->wb_flags);
}

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

	nfs_unlock_page(page);
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
 * Find a write request for a given page
 */
static inline struct nfs_wreq *
find_write_request(struct inode *inode, struct page *page)
{
	struct nfs_wreq	*head, *req;

	dprintk("NFS:      find_write_request(%x/%ld, %p)\n",
				inode->i_dev, inode->i_ino, page);
	if (!(req = head = NFS_WRITEBACK(inode)))
		return NULL;
	do {
		if (req->wb_page == page)
			return req;
	} while ((req = WB_NEXT(req)) != head);
	return NULL;
}

/*
 * Find any requests for the specified dentry.
 */
int
nfs_find_dentry_request(struct inode *inode, struct dentry *dentry)
{
	struct nfs_wreq	*head, *req;
	int found = 0;

	req = head = NFS_WRITEBACK(inode);
	while (req != NULL) {
		if (req->wb_dentry == dentry) {
			found = 1;
			break;
		}
		if ((req = WB_NEXT(req)) == head)
			break;
	}
	return found;
}

/*
 * Find a failed write request by pid
 */
static struct nfs_wreq *
find_failed_request(struct inode *inode, pid_t pid)
{
	struct nfs_wreq	*head, *req;

	req = head = nfs_failed_requests;
	while (req != NULL) {
		if (req->wb_inode == inode && (pid == 0 || req->wb_pid == pid))
			return req;
		if ((req = WB_NEXT(req)) == head)
			break;
	}
	return NULL;
}

/*
 * Add a request to the failed list.
 */
static void
append_failed_request(struct nfs_wreq * req)
{
	static int old_max = 16;

	append_write_request(&nfs_failed_requests, req);
	nr_failed_requests++;
	if (nr_failed_requests >= old_max) {
		printk("NFS: %d failed requests\n", nr_failed_requests);
		old_max = old_max << 1;
	}
}

/*
 * Remove a request from the failed list and free it.
 */
static void
remove_failed_request(struct nfs_wreq * req)
{
	remove_write_request(&nfs_failed_requests, req);
	kfree(req);
	nr_failed_requests--;
}

/*
 * Find and release all failed requests for this inode.
 */
int
nfs_check_failed_request(struct inode * inode)
{
	struct nfs_wreq * req;
	int found = 0;

	while ((req = find_failed_request(inode, 0)) != NULL) {
		remove_failed_request(req);
		found++;
	}
	return found;
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

	return 1;
}

/*
 * Create and initialize a writeback request
 */
static inline struct nfs_wreq *
create_write_request(struct dentry *dentry, struct inode *inode,
		struct page *page, unsigned int offset, unsigned int bytes)
{
	struct nfs_wreq *wreq;
	struct rpc_clnt	*clnt = NFS_CLIENT(inode);
	struct rpc_task	*task;

	dprintk("NFS:      create_write_request(%s/%s, %ld+%d)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		page->offset + offset, bytes);

	/* FIXME: Enforce hard limit on number of concurrent writes? */

	wreq = (struct nfs_wreq *) kmalloc(sizeof(*wreq), GFP_USER);
	if (!wreq)
		goto out_fail;
	memset(wreq, 0, sizeof(*wreq));

	task = &wreq->wb_task;
	rpc_init_task(task, clnt, nfs_wback_result, RPC_TASK_NFSWRITE);
	task->tk_calldata = wreq;
	task->tk_action = nfs_wback_lock;

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

	atomic_inc(&page->count);

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
 * The function returns true if the page has been unlocked as the
 * consequence of a synchronous write call.
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
		dprintk("NFS: %4d schedule_write_request (sync)\n",
					task->tk_pid);
		/* Page is already locked */
		req->wb_flags |= NFS_WRITE_LOCKED;
		rpc_execute(task);
	} else {
		dprintk("NFS: %4d schedule_write_request (async)\n",
					task->tk_pid);
		task->tk_flags |= RPC_TASK_ASYNC;
		task->tk_timeout = NFS_WRITEBACK_DELAY;
		rpc_sleep_on(&write_queue, task, NULL, NULL);
	}

	return sync == 0;
}

/*
 * Wait for request to complete
 * This is almost a copy of __wait_on_page
 */
static inline int
wait_on_write_request(struct nfs_wreq *req)
{
	struct wait_queue	wait = { current, NULL };
	struct page		*page = req->wb_page;
	int			retval;

	add_wait_queue(&page->wait, &wait);
	atomic_inc(&page->count);
	for (;;) {
		current->state = IS_SOFT ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;
		retval = 0;
		if (!PageLocked(page))
			break;
		retval = -ERESTARTSYS;
		if (IS_SOFT && signalled())
			break;
		schedule();
	}
	remove_wait_queue(&page->wait, &wait);
	current->state = TASK_RUNNING;
	/* N.B. page may have been unused, so we must use free_page() */
	free_page(page_address(page));
	return retval;
}

/*
 * Write a page to the server. This will be used for NFS swapping only
 * (for now), and we currently do this synchronously only.
 */
int
nfs_writepage(struct dentry *dentry, struct page *page)
{
	return nfs_writepage_sync(dentry, dentry->d_inode, page, 0, PAGE_SIZE);
}

/*
 * Update and possibly write a cached page of an NFS file.
 *
 * XXX: Keep an eye on generic_file_read to make sure it doesn't do bad
 * things with a page scheduled for an RPC call (e.g. invalidate it).
 */
int
nfs_updatepage(struct dentry *dentry, struct page *page, const char *buffer,
			unsigned long offset, unsigned int count, int sync)
{
	struct inode	*inode = dentry->d_inode;
	u8		*page_addr = (u8 *) page_address(page);
	struct nfs_wreq	*req;
	int		status = 0, page_locked = 1;

	dprintk("NFS:      nfs_updatepage(%s/%s %d@%ld, sync=%d)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		count, page->offset+offset, sync);

	set_bit(PG_locked, &page->flags);

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
	if ((req = find_write_request(inode, page)) != NULL) {
		if (update_write_request(req, offset, count)) {
			/* N.B. check for a fault here and cancel the req */
			copy_from_user(page_addr + offset, buffer, count);
			goto updated;
		}
		dprintk("NFS:      wake up conflicting write request.\n");
		transfer_page_lock(req);
		return 0;
	}

	/* Copy data to page buffer. */
	status = -EFAULT;
	if (copy_from_user(page_addr + offset, buffer, count))
		goto done;

	/* If wsize is smaller than page size, update and write
	 * page synchronously.
	 */
	if (NFS_SERVER(inode)->wsize < PAGE_SIZE)
		return nfs_writepage_sync(dentry, inode, page, offset, count);

	/* Create the write request. */
	status = -ENOBUFS;
	req = create_write_request(dentry, inode, page, offset, count);
	if (!req)
		goto done;

	/* Schedule request */
	page_locked = schedule_write_request(req, sync);

updated:
	/*
	 * If we wrote up to the end of the chunk, transmit request now.
	 * We should be a bit more intelligent about detecting whether a
	 * process accesses the file sequentially or not.
	 */
	if (page_locked && (offset + count >= PAGE_SIZE || sync))
		req->wb_flags |= NFS_WRITE_WANTLOCK;

	/* If the page was written synchronously, return any error that
	 * may have happened; otherwise return the write count. */
	if (page_locked || (status = nfs_write_error(inode)) >= 0)
		status = count;

done:
	/* Unlock page and wake up anyone sleeping on it */
	if (page_locked) {
		if (req && WB_WANTLOCK(req)) {
			transfer_page_lock(req);
			/* rpc_execute(&req->wb_task); */
			if (sync) {
				/* N.B. if signalled, result not ready? */
				wait_on_write_request(req);
				if ((count = nfs_write_error(inode)) < 0)
					status = count;
			}
		} else {
			if (status < 0) {
printk("NFS: %s/%s write failed, clearing bit\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
				clear_bit(PG_uptodate, &page->flags);
			}
			nfs_unlock_page(page);
		}
	}

	dprintk("NFS:      nfs_updatepage returns %d (isize %ld)\n",
						status, inode->i_size);
	return status;
}

/*
 * Flush out a dirty page.
 */ 
static void
nfs_flush_request(struct nfs_wreq *req)
{
	struct page	*page = req->wb_page;

#ifdef NFS_DEBUG_VERBOSE
if (req->wb_inode != page->inode)
printk("NFS: inode %ld no longer has page %p\n", req->wb_inode->i_ino, page);
#endif
	dprintk("NFS:      nfs_flush_request(%s/%s, @%ld)\n",
		req->wb_dentry->d_parent->d_name.name,
		req->wb_dentry->d_name.name, page->offset);

	req->wb_flags |= NFS_WRITE_WANTLOCK;
	if (!test_and_set_bit(PG_locked, &page->flags)) {
		transfer_page_lock(req);
	} else {
		printk(KERN_WARNING "NFS oops in %s: can't lock page!\n",
					__FUNCTION__);
		rpc_wake_up_task(&req->wb_task);
	}
}

/*
 * Flush writeback requests. See nfs_flush_dirty_pages for details.
 */
static struct nfs_wreq *
nfs_flush_pages(struct inode *inode, pid_t pid, off_t offset, off_t len,
						int invalidate)
{
	struct nfs_wreq *head, *req, *last = NULL;
	off_t		rqoffset, rqend, end;

	end = len? offset + len : 0x7fffffffUL;

	req = head = NFS_WRITEBACK(inode);
	while (req != NULL) {
		dprintk("NFS: %4d nfs_flush inspect %x/%ld @%ld fl %x\n",
				req->wb_task.tk_pid,
				req->wb_inode->i_dev, req->wb_inode->i_ino,
				req->wb_page->offset, req->wb_flags);
		rqoffset = req->wb_page->offset + req->wb_offset;
		rqend    = rqoffset + req->wb_bytes;
		
		if (rqoffset < end && offset < rqend
			 && (pid == 0 || req->wb_pid == pid)) {
			if (!WB_INPROGRESS(req) && !WB_HAVELOCK(req)) {
#ifdef NFS_DEBUG_VERBOSE
printk("nfs_flush: flushing inode=%ld, %d @ %lu\n",
req->wb_inode->i_ino, req->wb_bytes, rqoffset);
#endif
				nfs_flush_request(req);
			}
			last = req;
		}
		if (invalidate)
			req->wb_flags |= NFS_WRITE_INVALIDATE;
		if ((req = WB_NEXT(req)) == head)
			break;
	}

	return last;
}

/*
 * Cancel all writeback requests, both pending and in progress.
 *
 * N.B. This doesn't seem to wake up the tasks -- are we sure
 * they will eventually complete? Also, this could overwrite a
 * failed status code from an already-completed task.
 */
static void
nfs_cancel_dirty(struct inode *inode, pid_t pid)
{
	struct nfs_wreq *head, *req;

	req = head = NFS_WRITEBACK(inode);
	while (req != NULL) {
		/* N.B. check for task already finished? */
		if (pid == 0 || req->wb_pid == pid) {
			req->wb_flags |= NFS_WRITE_CANCELLED;
			rpc_exit(&req->wb_task, 0);
		}
		if ((req = WB_NEXT(req)) == head)
			break;
	}
}

/*
 * Flush out all dirty pages belonging to a certain user process and
 * maybe wait for the RPC calls to complete.
 *
 * Another purpose of this function is sync()ing a file range before a
 * write lock is released. This is what offset and length are for, even if
 * this isn't used by the nlm module yet.
 */
int
nfs_flush_dirty_pages(struct inode *inode, pid_t pid, off_t offset, off_t len)
{
	struct nfs_wreq *last = NULL;
	int result = 0, cancel = 0;

	dprintk("NFS:      flush_dirty_pages(%x/%ld for pid %d %ld/%ld)\n",
				inode->i_dev, inode->i_ino, current->pid,
				offset, len);

	if (IS_SOFT && signalled()) {
		nfs_cancel_dirty(inode, pid);
		cancel = 1;
	}

	for (;;) {
		if (IS_SOFT && signalled()) {
			if (!cancel)
				nfs_cancel_dirty(inode, pid);
			result = -ERESTARTSYS;
			break;
		}

		/* Flush all pending writes for the pid and file region */
		last = nfs_flush_pages(inode, pid, offset, len, 0);
		if (last == NULL)
			break;
		wait_on_write_request(last);
	}

	return result;
}

/*
 * Flush out any pending write requests and flag that they be discarded
 * after the write is complete.
 *
 * This function is called from nfs_refresh_inode just before it calls
 * invalidate_inode_pages. After nfs_flush_pages returns, we can be sure
 * that all dirty pages are locked, so that invalidate_inode_pages does
 * not throw away any dirty pages.
 */
void
nfs_invalidate_pages(struct inode *inode)
{
	dprintk("NFS:      nfs_invalidate_pages(%x/%ld)\n",
				inode->i_dev, inode->i_ino);

	nfs_flush_pages(inode, 0, 0, 0, 1);
}

/*
 * Cancel any pending write requests after a given offset
 * (called from nfs_notify_change).
 */
int
nfs_truncate_dirty_pages(struct inode *inode, unsigned long offset)
{
	struct nfs_wreq *req, *head;
	unsigned long	rqoffset;

	dprintk("NFS:      truncate_dirty_pages(%x/%ld, %ld)\n",
				inode->i_dev, inode->i_ino, offset);

	req = head = NFS_WRITEBACK(inode);
	while (req != NULL) {
		rqoffset = req->wb_page->offset + req->wb_offset;

		if (rqoffset >= offset) {
			req->wb_flags |= NFS_WRITE_CANCELLED;
			rpc_exit(&req->wb_task, 0);
		} else if (rqoffset + req->wb_bytes >= offset) {
			req->wb_bytes = offset - rqoffset;
		}
		if ((req = WB_NEXT(req)) == head)
			break;
	}

	return 0;
}

/*
 * Check if a previous write operation returned an error
 */
int
nfs_check_error(struct inode *inode)
{
	struct nfs_wreq	*req;
	int		status = 0;

	dprintk("nfs:      checking for write error inode %04x/%ld\n",
			inode->i_dev, inode->i_ino);

	req = find_failed_request(inode, current->pid);
	if (req) {
		dprintk("nfs: write error %d inode %04x/%ld\n",
			req->wb_task.tk_status, inode->i_dev, inode->i_ino);

		status = req->wb_task.tk_status;
		remove_failed_request(req);
	}
	return status;
}

/*
 * The following procedures make up the writeback finite state machinery:
 *
 * 1.	Try to lock the page if not yet locked by us,
 *	set up the RPC call info, and pass to the call FSM.
 */
static void
nfs_wback_lock(struct rpc_task *task)
{
	struct nfs_wreq	*req = (struct nfs_wreq *) task->tk_calldata;
	struct page	*page = req->wb_page;
	struct dentry	*dentry = req->wb_dentry;

	dprintk("NFS: %4d nfs_wback_lock (%s/%s, status=%d flags=%x)\n",
		task->tk_pid, dentry->d_parent->d_name.name,
		dentry->d_name.name, task->tk_status, req->wb_flags);

	if (!WB_HAVELOCK(req))
		req->wb_flags |= NFS_WRITE_WANTLOCK;

	if (WB_WANTLOCK(req) && test_and_set_bit(PG_locked, &page->flags))
		goto out_locked;
	req->wb_flags &= ~NFS_WRITE_WANTLOCK;
	req->wb_flags |=  NFS_WRITE_LOCKED;
	task->tk_status = 0;

	if (req->wb_args == 0) {
		size_t	size = sizeof(struct nfs_writeargs)
			     + sizeof(struct nfs_fattr);
		void	*ptr;

		if (!(ptr = kmalloc(size, GFP_KERNEL)))
			goto out_no_args;
		req->wb_args = (struct nfs_writeargs *) ptr;
		req->wb_fattr = (struct nfs_fattr *) (req->wb_args + 1);
	}

	/* Setup the task struct for a writeback call */
	req->wb_args->fh = NFS_FH(dentry);
	req->wb_args->offset = page->offset + req->wb_offset;
	req->wb_args->count  = req->wb_bytes;
	req->wb_args->buffer = (void *) (page_address(page) + req->wb_offset);

	rpc_call_setup(task, NFSPROC_WRITE, req->wb_args, req->wb_fattr, 0);

	req->wb_flags |= NFS_WRITE_INPROGRESS;
	return;

out_locked:
	printk("NFS: page already locked in writeback_lock!\n");
	task->tk_timeout = 2 * HZ;
	rpc_sleep_on(&write_queue, task, NULL, NULL);
	return;
out_no_args:
	printk("NFS: can't alloc args, sleeping\n");
	task->tk_timeout = HZ;
	rpc_sleep_on(&write_queue, task, NULL, NULL);
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

	/* Set the WRITE_COMPLETE flag, but leave INPROGRESS set */
	req->wb_flags |= NFS_WRITE_COMPLETE;
	if (status < 0) {
		/*
		 * An error occurred. Report the error back to the
		 * application by adding the request to the failed
		 * requests list.
		 */
		if (find_failed_request(inode, req->wb_pid))
			status = 0;
		clear_bit(PG_uptodate, &page->flags);
	} else if (!WB_CANCELLED(req)) {
		struct nfs_fattr *fattr = req->wb_fattr;
		/* Update attributes as result of writeback. 
		 * Beware: when UDP replies arrive out of order, we
		 * may end up overwriting a previous, bigger file size.
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

	/*
	 * This call might block, so we defer removing the request
	 * from the inode's writeback list.
	 */
	rpc_release_task(task);

	if (WB_INVALIDATE(req))
		clear_bit(PG_uptodate, &page->flags);
	if (WB_HAVELOCK(req))
		nfs_unlock_page(page);

	if (req->wb_args) {
		kfree(req->wb_args);
		req->wb_args = 0;
	}

	/*
	 * Now it's safe to remove the request from the inode's 
	 * writeback list and wake up any tasks sleeping on it.
	 * If the request failed, add it to the failed list.
	 */
	remove_write_request(&NFS_WRITEBACK(inode), req);

	if (status >= 0)
		kfree(req);
	else {
		dprintk("NFS: %4d saving write failure code\n", task->tk_pid);
		append_failed_request(req);
	}

	free_page(page_address(page));
	nr_write_requests--;
}
