/*
 * Network block device - make block devices work over TCP
 *
 * Note that you can not swap over this thing, yet. Seems to work but
 * deadlocks sometimes - you can not swap over TCP in general.
 * 
 * Copyright 1997 Pavel Machek <pavel@atrey.karlin.mff.cuni.cz>
 * 
 * (part of code stolen from loop.c)
 *
 * 97-3-25 compiled 0-th version, not yet tested it 
 *   (it did not work, BTW) (later that day) HEY! it works!
 *   (bit later) hmm, not that much... 2:00am next day:
 *   yes, it works, but it gives something like 50kB/sec
 * 97-4-01 complete rewrite to make it possible for many requests at 
 *   once to be processed
 * 97-4-11 Making protocol independent of endianity etc.
 * 97-9-13 Cosmetic changes
 *
 * possible FIXME: make set_sock / set_blksize / set_size / do_it one syscall
 * why not: would need verify_area and friends, would share yet another 
 *          structure with userland
 */

#define PARANOIA
#include <linux/major.h>
#define MAJOR_NR NBD_MAJOR
#include <linux/nbd.h>

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>

#include <asm/segment.h>
#include <asm/uaccess.h>

static int nbd_blksizes[MAX_NBD] = {1024, 1024,};
static int nbd_sizes[MAX_NBD] = {0x7fffffff, 0x7fffffff,};

static struct nbd_device nbd_dev[MAX_NBD];

#define DEBUG( s )
/* #define DEBUG( s ) printk( s ) 
 */

#ifdef PARANOIA
static int requests_in;
static int requests_out;
#endif

static int nbd_open(struct inode *inode, struct file *file)
{
	int dev;

	if (!inode)
		return -EINVAL;
	dev = MINOR(inode->i_rdev);
	if (dev >= MAX_NBD)
		return -ENODEV;
	nbd_dev[dev].refcnt++;
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 *  Send or receive packet.
 */
static
int nbd_xmit(int send, struct socket *sock, char *buf, int size)
{
	mm_segment_t oldfs;
	int result;
	struct msghdr msg;
	struct iovec iov;

	oldfs = get_fs();
	set_fs(get_ds());
	do {
		int save;

		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_namelen = 0;
		msg.msg_flags = 0;

		save = current->blocked;
		current->blocked = ~0UL;
		if (send)
			result = sock_sendmsg(sock, &msg, size);
		else
			result = sock_recvmsg(sock, &msg, size, 0);
		current->blocked = save;
		if (result <= 0) {
#ifdef PARANOIA
			printk(KERN_ERR "NBD: %s - sock=%d at buf=%d, size=%d returned %d.\n",
			       send ? "send" : "receive", (int) sock, (int) buf, size, result);
#endif
			break;
		}
		size -= result;
		buf += result;
	} while (size > 0);
	set_fs(oldfs);
	return result;
}

#define FAIL( s ) { printk( KERN_ERR "NBD: " s "(result %d)\n", result ); goto error_out; }

void nbd_send_req(struct socket *sock, struct request *req)
{
	int result;
	struct nbd_request request;

	DEBUG("NBD: sending control, ");
	request.magic = htonl(NBD_REQUEST_MAGIC);
	request.type = htonl(req->cmd);
	request.from = htonl(req->sector * 512);
	request.len = htonl(req->current_nr_sectors << 9);
	memcpy(request.handle, &req, sizeof(req));

	result = nbd_xmit(1, sock, (char *) &request, sizeof(request));
	if (result <= 0)
		FAIL("Sendmsg failed for control.");

	if (req->cmd == WRITE) {
		DEBUG("data, ");
		result = nbd_xmit(1, sock, req->buffer, req->current_nr_sectors << 9);
		if (result <= 0)
			FAIL("Send data failed.");
	}
	return;

      error_out:
	req->errors++;
}

#define HARDFAIL( s ) { printk( KERN_ERR "NBD: " s "(result %d)\n", result ); lo->harderror = result; return NULL; }
struct request *		/* NULL returned = something went wrong, inform userspace       */
 nbd_read_stat(struct nbd_device *lo)
{
	int result;
	struct nbd_reply reply;
	struct request *xreq, *req;

	DEBUG("reading control, ");
	reply.magic = 0;
	result = nbd_xmit(0, lo->sock, (char *) &reply, sizeof(reply));
	req = lo->tail;
	if (result <= 0)
		HARDFAIL("Recv control failed.");
	memcpy(&xreq, reply.handle, sizeof(xreq));

	if (xreq != req)
		FAIL("Unexpected handle received.\n");

	DEBUG("ok, ");
	if (ntohl(reply.magic) != NBD_REPLY_MAGIC)
		HARDFAIL("Not enough magic.");
	if (ntohl(reply.error))
		FAIL("Other side returned error.");
	if (req->cmd == READ) {
		DEBUG("data, ");
		result = nbd_xmit(0, lo->sock, req->buffer, req->current_nr_sectors << 9);
		if (result <= 0)
			HARDFAIL("Recv data failed.");
	}
	DEBUG("done.\n");
	return req;

/* Can we get here? Yes, if other side returns error */
      error_out:
	req->errors++;
	return req;
}

void nbd_do_it(struct nbd_device *lo)
{
	struct request *req;

	while (1) {
		req = nbd_read_stat(lo);
		if (!req)
			return;
#ifdef PARANOIA
		if (req != lo->tail) {
			printk(KERN_ALERT "NBD: I have problem...\n");
		}
		if (lo != &nbd_dev[MINOR(req->rq_dev)]) {
			printk(KERN_ALERT "NBD: request corrupted!\n");
			continue;
		}
		if (lo->magic != LO_MAGIC) {
			printk(KERN_ALERT "NBD: nbd_dev[] corrupted: Not enough magic\n");
			return;
		}
#endif
		nbd_end_request(req);
		if (lo->tail == lo->head) {
#ifdef PARANOIA
			if (lo->tail->next)
				printk(KERN_ERR "NBD: I did not expect this\n");
#endif
			lo->head = NULL;
		}
		lo->tail = lo->tail->next;
	}
}

void nbd_clear_que(struct nbd_device *lo)
{
	struct request *req;

	while (1) {
		req = lo->tail;
		if (!req)
			return;
#ifdef PARANOIA
		if (lo != &nbd_dev[MINOR(req->rq_dev)]) {
			printk(KERN_ALERT "NBD: request corrupted when clearing!\n");
			continue;
		}
		if (lo->magic != LO_MAGIC) {
			printk(KERN_ERR "NBD: nbd_dev[] corrupted: Not enough magic when clearing!\n");
			return;
		}
#endif
		req->errors++;
		nbd_end_request(req);
		if (lo->tail == lo->head) {
#ifdef PARANOIA
			if (lo->tail->next)
				printk(KERN_ERR "NBD: I did not assume this\n");
#endif
			lo->head = NULL;
		}
		lo->tail = lo->tail->next;
	}
}

/*
 * We always wait for result of write, for now. It would be nice to make it optional
 * in future
 * if ((req->cmd == WRITE) && (lo->flags & NBD_WRITE_NOCHK)) 
 *   { printk( "Warning: Ignoring result!\n"); nbd_end_request( req ); }
 */

#undef FAIL
#define FAIL( s ) { printk( KERN_ERR "NBD, minor %d: " s "\n", dev ); goto error_out; }

static void do_nbd_request(void)
{
	struct request *req;
	int dev;
	struct nbd_device *lo;

	while (CURRENT) {
		req = CURRENT;
		dev = MINOR(req->rq_dev);
#ifdef PARANOIA
		if (dev >= MAX_NBD)
			FAIL("Minor too big.");		/* Probably can not happen */
#endif
		lo = &nbd_dev[dev];
		if (!lo->file)
			FAIL("Request when not-ready.");
		if ((req->cmd == WRITE) && (lo->flags && NBD_READ_ONLY))
			FAIL("Write on read-only");
#ifdef PARANOIA
		if (lo->magic != LO_MAGIC)
			FAIL("nbd[] is not magical!");
		requests_in++;
#endif
		req->errors = 0;

		nbd_send_req(lo->sock, req);	/* Why does this block?         */
		CURRENT = CURRENT->next;
		req->next = NULL;
		if (lo->head == NULL) {
			lo->head = req;
			lo->tail = req;
		} else {
			lo->head->next = req;
			lo->head = req;
		}
		continue;

	      error_out:
		req->errors++;
		nbd_end_request(req);
		CURRENT = CURRENT->next;
	}
	return;
}

static int nbd_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct nbd_device *lo;
	int dev;

	if (!suser())
		return -EPERM;
	if (!inode)
		return -EINVAL;
	dev = MINOR(inode->i_rdev);
	if (dev >= MAX_NBD)
		return -ENODEV;
	lo = &nbd_dev[dev];
	switch (cmd) {
	case NBD_CLEAR_SOCK:
		if (lo->head || lo->tail) {
			printk(KERN_ERR "nbd: Some requests are in progress -> can not turn off.\n");
			return -EBUSY;
		}
		if (!lo->file)
			return -EINVAL;
		lo->file->f_count--;
		lo->file = NULL;
		lo->sock = NULL;
		return 0;
	case NBD_SET_SOCK:
		file = current->files->fd[arg];
		inode = file->f_dentry->d_inode;
		file->f_count++;
		lo->sock = &inode->u.socket_i;
		lo->file = file;
		return 0;
	case NBD_SET_BLKSIZE:
		if ((arg & 511) || (arg > PAGE_SIZE))
			return -EINVAL;
		nbd_blksizes[dev] = arg;
		return 0;
	case NBD_SET_SIZE:
		nbd_sizes[dev] = arg;
		return 0;
	case NBD_DO_IT:
		if (!lo->file)
			return -EINVAL;
		nbd_do_it(lo);
		return lo->harderror;
	case NBD_CLEAR_QUE:
		nbd_clear_que(lo);
		return 0;
#ifdef PARANOIA
	case NBD_PRINT_DEBUG:
		printk(KERN_INFO "NBD device %d: head = %x, tail = %x. Global: in %d, out %d\n",
		       dev, (int) lo->head, (int) lo->tail, requests_in, requests_out);
		return 0;
#endif
	}
	return -EINVAL;
}

static int nbd_release(struct inode *inode, struct file *file)
{
	struct nbd_device *lo;
	int dev;

	if (!inode)
		return -ENODEV;
	dev = MINOR(inode->i_rdev);
	if (dev >= MAX_NBD)
		return -ENODEV;
	fsync_dev(inode->i_rdev);
	lo = &nbd_dev[dev];
	if (lo->refcnt <= 0)
		printk(KERN_ALERT "nbd_release: refcount(%d) <= 0\n", lo->refcnt);
	lo->refcnt--;
	MOD_DEC_USE_COUNT;
	return 0;
}

static struct file_operations nbd_fops =
{
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	nbd_ioctl,		/* ioctl */
	NULL,			/* mmap */
	nbd_open,		/* open */
	nbd_release		/* release */
};

/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

#ifdef MODULE
#define nbd_init init_module
#endif

int nbd_init(void)
{
	int i;

	if (register_blkdev(MAJOR_NR, "nbd", &nbd_fops)) {
		printk("Unable to get major number %d for NBD\n",
		       MAJOR_NR);
		return -EIO;
	}
#ifdef MODULE
	printk("nbd: registered device at major %d\n", MAJOR_NR);
#endif
	blksize_size[MAJOR_NR] = nbd_blksizes;
	blk_size[MAJOR_NR] = nbd_sizes;
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	for (i = 0; i < MAX_NBD; i++) {
		nbd_dev[i].refcnt = 0;
		nbd_dev[i].file = NULL;
		nbd_dev[i].magic = LO_MAGIC;
		nbd_dev[i].flags = 0;
	}
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	if (unregister_blkdev(MAJOR_NR, "nbd") != 0)
		printk("nbd: cleanup_module failed\n");
	else
		printk("nbd: module cleaned up.\n");
}
#endif
