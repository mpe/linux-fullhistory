/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/waitq.c
 *
 *  Copyright 1997 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/malloc.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/auto_fs.h>

/* We make this a static variable rather than a part of the superblock; it
   is better if we don't reassign numbers easily even across filesystems */
static int autofs_next_wait_queue = 1;

void autofs_catatonic_mode(struct autofs_sb_info *sbi)
{
	struct autofs_wait_queue *wq, *nwq;

	DPRINTK(("autofs: entering catatonic mode\n"));

	sbi->catatonic = 1;
	wq = sbi->queues;
	sbi->queues = NULL;	/* Erase all wait queues */
	while ( wq ) {
		nwq = wq->next;
		wq->status = -ENOENT; /* Magic is gone - report failure */
		kfree(wq->name);
		wq->name = NULL;
		wake_up(&wq->queue);
		wq = nwq;
	}
}

static int autofs_write(struct file *file, const void *addr, int bytes)
{
	unsigned short fs;
	unsigned long old_signal;
	const char *data = (const char *)addr;
	int written;

	/** WARNING: this is not safe for writing more than PIPE_BUF bytes! **/

	/* Save pointer to user space and point back to kernel space */
	fs = get_fs();
	set_fs(KERNEL_DS);

	old_signal = current->signal;

	while ( bytes && (written = file->f_op->write(file->f_inode,file,data,bytes)) > 0 ) {
		data += written;
		bytes -= written;
	}

	if ( written == -EPIPE && !(old_signal & (1 << (SIGPIPE-1))) ) {
		/* Keep the currently executing process from receiving a
		   SIGPIPE unless it was already supposed to get one */
		current->signal &= ~(1 << (SIGPIPE-1));
	}
	set_fs(fs);

	return (bytes > 0);
}
	
static void autofs_notify_daemon(struct autofs_sb_info *sbi, struct autofs_wait_queue *wq)
{
	struct autofs_packet_missing pkt;

	DPRINTK(("autofs_wait: wait id = 0x%08lx, name = ", wq->wait_queue_token));
	autofs_say(wq->name,wq->len);

	pkt.hdr.proto_version = AUTOFS_PROTO_VERSION;
	pkt.hdr.type = autofs_ptype_missing;
	pkt.wait_queue_token = wq->wait_queue_token;
	pkt.len = wq->len;
        memcpy(pkt.name, wq->name, pkt.len);
	pkt.name[pkt.len] = '\0';

	if ( autofs_write(sbi->pipe,&pkt,sizeof(struct autofs_packet_missing)) )
		autofs_catatonic_mode(sbi);
}

int autofs_wait(struct autofs_sb_info *sbi, autofs_hash_t hash, const char *name, int len)
{
	struct autofs_wait_queue *wq;
	int status;

	for ( wq = sbi->queues ; wq ; wq = wq->next ) {
		if ( wq->hash == hash &&
		     wq->len == len &&
		     !memcmp(wq->name,name,len) )
			break;
	}
	
	if ( !wq ) {
		/* Create a new wait queue */
		wq = kmalloc(sizeof(struct autofs_wait_queue),GFP_KERNEL);
		if ( !wq )
			return -ENOMEM;

		wq->name = kmalloc(len,GFP_KERNEL);
		if ( !wq->name ) {
			kfree(wq);
			return -ENOMEM;
		}
		wq->wait_queue_token = autofs_next_wait_queue++;
		init_waitqueue(&wq->queue);
		wq->hash = hash;
		wq->len = len;
		memcpy(wq->name, name, len);
		wq->next = sbi->queues;
		sbi->queues = wq;

		/* autofs_notify_daemon() may block */
		wq->wait_ctr++;
		autofs_notify_daemon(sbi,wq);
	} else
		wq->wait_ctr++;

	if ( wq->name ) {
		/* wq->name is NULL if and only if the lock is released */
		interruptible_sleep_on(&wq->queue);
	} else {
		DPRINTK(("autofs_wait: skipped sleeping\n"));
	}

	status = (current->signal & ~current->blocked) ? -EINTR : wq->status;
	if ( ! --wq->wait_ctr )	/* Are we the last process to need status? */
		kfree(wq);

	return status;
}


int autofs_wait_release(struct autofs_sb_info *sbi, unsigned long wait_queue_token, int status)
{
	struct autofs_wait_queue *wq, **wql;

	for ( wql = &sbi->queues ; (wq = *wql) ; wql = &wq->next ) {
		if ( wq->wait_queue_token == wait_queue_token )
			break;
	}
	if ( !wq )
		return -EINVAL;

	*wql = wq->next;	/* Unlink from chain */
	kfree(wq->name);
	wq->name = NULL;	/* Do not wait on this queue */

	wq->status = status;

	wake_up(&wq->queue);

	return 0;
}

