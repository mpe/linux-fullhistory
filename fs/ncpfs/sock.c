/*
 *  linux/fs/ncpfs/sock.c
 *
 *  Copyright (C) 1992, 1993  Rick Sladkey
 *
 *  Modified 1995, 1996 by Volker Lendecke to be usable for ncp
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *
 */

#include <linux/config.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <asm/uaccess.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/signal.h>
#include <net/scm.h>
#include <net/sock.h>
#include <linux/ipx.h>
#include <linux/poll.h>
#include <linux/file.h>

#include <linux/ncp.h>
#include <linux/ncp_fs.h>
#include <linux/ncp_fs_sb.h>

#ifdef CONFIG_NCPFS_PACKET_SIGNING
#include "ncpsign_kernel.h"
#endif

static int _recv(struct socket *sock, unsigned char *ubuf, int size,
		 unsigned flags)
{
	struct iovec iov;
	struct msghdr msg;
	struct scm_cookie scm;

	memset(&scm, 0, sizeof(scm));

	iov.iov_base = ubuf;
	iov.iov_len = size;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	return sock->ops->recvmsg(sock, &msg, size, flags, &scm);
}

static int _send(struct socket *sock, const void *buff, int len)
{
	struct iovec iov;
	struct msghdr msg;
	struct scm_cookie scm;
	int err;

	iov.iov_base = (void *) buff;
	iov.iov_len = len;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	err = scm_send(sock, &msg, &scm);
	if (err < 0) {
		return err;
	}
	err = sock->ops->sendmsg(sock, &msg, len, &scm);
	scm_destroy(&scm);
	return err;
}

#define NCP_SLACK_SPACE 1024

static int do_ncp_rpc_call(struct ncp_server *server, int size)
{
	struct file *file;
	struct inode *inode;
	struct socket *sock;
	mm_segment_t fs;
	int result;
	char *start = server->packet;
	poll_table wait_table;
	struct poll_table_entry entry;
	int init_timeout, max_timeout;
	int timeout; long tmp_timeout;
	int retrans;
	int major_timeout_seen;
	int acknowledge_seen;
	int n;
	sigset_t old_set;
	unsigned long mask, flags;

	/* We have to check the result, so store the complete header */
	struct ncp_request_header request =
	*((struct ncp_request_header *) (server->packet));

	struct ncp_reply_header reply;

	file = server->ncp_filp;
	inode = file->f_dentry->d_inode;
	sock = &inode->u.socket_i;
	/* N.B. this isn't needed ... check socket type? */
	if (!sock) {
		printk(KERN_ERR "ncp_rpc_call: socki_lookup failed\n");
		return -EBADF;
	}

	init_timeout = server->m.time_out;
	max_timeout = NCP_MAX_RPC_TIMEOUT;
	retrans = server->m.retry_count;
	major_timeout_seen = 0;
	acknowledge_seen = 0;

	spin_lock_irqsave(&current->sigmask_lock, flags);
	old_set = current->blocked;
	mask = sigmask(SIGKILL) | sigmask(SIGSTOP);
	if (server->m.flags & NCP_MOUNT_INTR) {
		/* FIXME: This doesn't seem right at all.  So, like,
		   we can't handle SIGINT and get whatever to stop?
		   What if we've blocked it ourselves?  What about
		   alarms?  Why, in fact, are we mucking with the
		   sigmask at all? -- r~ */
		if (current->sig->action[SIGINT - 1].sa.sa_handler == SIG_DFL)
			mask |= sigmask(SIGINT);
		if (current->sig->action[SIGQUIT - 1].sa.sa_handler == SIG_DFL)
			mask |= sigmask(SIGQUIT);
	}
	siginitsetinv(&current->blocked, mask);
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, flags);

	fs = get_fs();
	set_fs(get_ds());
	for (n = 0, timeout = init_timeout;; n++, timeout <<= 1) {
		/*
		DDPRINTK(KERN_DEBUG "ncpfs: %08lX:%02X%02X%02X%02X%02X%02X:%04X\n",
			 htonl(server->m.serv_addr.sipx_network),
			 server->m.serv_addr.sipx_node[0],
			 server->m.serv_addr.sipx_node[1],
			 server->m.serv_addr.sipx_node[2],
			 server->m.serv_addr.sipx_node[3],
			 server->m.serv_addr.sipx_node[4],
			 server->m.serv_addr.sipx_node[5],
			 ntohs(server->m.serv_addr.sipx_port));
		*/
		DDPRINTK(KERN_DEBUG "ncpfs: req.typ: %04X, con: %d, "
			 "seq: %d",
			 request.type,
			 (request.conn_high << 8) + request.conn_low,
			 request.sequence);
		DDPRINTK(KERN_DEBUG " func: %d\n",
			 request.function);

		result = _send(sock, (void *) start, size);
		if (result < 0) {
			printk(KERN_ERR "ncp_rpc_call: send error = %d\n", result);
			break;
		}
	      re_select:
		wait_table.nr = 0;
		wait_table.entry = &entry;
		current->state = TASK_INTERRUPTIBLE;
		if (!(file->f_op->poll(file, &wait_table) & POLLIN)) {
			int timed_out;
			if (timeout > max_timeout) {
				/* JEJB/JSP 2/7/94
				 * This is useful to see if the system is
				 * hanging */
				if (acknowledge_seen == 0) {
					printk(KERN_WARNING "NCP max timeout\n");
				}
				timeout = max_timeout;
			}
			timed_out = !schedule_timeout(timeout);
			remove_wait_queue(entry.wait_address, &entry.wait);
			fput(file);
			current->state = TASK_RUNNING;
			if (signal_pending(current)) {
				result = -ERESTARTSYS;
				break;
			}
			if (timed_out) {
				if (n < retrans)
					continue;
				if (server->m.flags & NCP_MOUNT_SOFT) {
					printk(KERN_WARNING "NCP server not responding\n");
					result = -EIO;
					break;
				}
				n = 0;
				timeout = init_timeout;
				init_timeout <<= 1;
				if (!major_timeout_seen) {
					printk(KERN_WARNING "NCP server not responding\n");
				}
				major_timeout_seen = 1;
				continue;
			}
		} else if (wait_table.nr) {
			remove_wait_queue(entry.wait_address, &entry.wait);
			fput(file);
		}
		current->state = TASK_RUNNING;

		/* Get the header from the next packet using a peek, so keep it
		 * on the recv queue.  If it is wrong, it will be some reply
		 * we don't now need, so discard it */
		result = _recv(sock, (void *) &reply, sizeof(reply),
			       MSG_PEEK | MSG_DONTWAIT);
		if (result < 0) {
			if (result == -EAGAIN) {
				DDPRINTK(KERN_DEBUG "ncp_rpc_call: bad select ready\n");
				goto re_select;
			}
			if (result == -ECONNREFUSED) {
				DPRINTK(KERN_WARNING "ncp_rpc_call: server playing coy\n");
				goto re_select;
			}
			if (result != -ERESTARTSYS) {
				printk(KERN_ERR "ncp_rpc_call: recv error = %d\n",
				       -result);
			}
			break;
		}
		if ((result == sizeof(reply))
		    && (reply.type == NCP_POSITIVE_ACK)) {
			/* Throw away the packet */
			DPRINTK(KERN_DEBUG "ncp_rpc_call: got positive acknowledge\n");
			_recv(sock, (void *) &reply, sizeof(reply),
			      MSG_DONTWAIT);
			n = 0;
			timeout = max_timeout;
			acknowledge_seen = 1;
			goto re_select;
		}
		DDPRINTK(KERN_DEBUG "ncpfs: rep.typ: %04X, con: %d, tsk: %d,"
			 "seq: %d\n",
			 reply.type,
			 (reply.conn_high << 8) + reply.conn_low,
			 reply.task,
			 reply.sequence);

		if ((result >= sizeof(reply))
		    && (reply.type == NCP_REPLY)
		    && ((request.type == NCP_ALLOC_SLOT_REQUEST)
			|| ((reply.sequence == request.sequence)
			    && (reply.conn_low == request.conn_low)
/* seem to get wrong task from NW311 && (reply.task      == request.task) */
			    && (reply.conn_high == request.conn_high)))) {
			if (major_timeout_seen)
				printk(KERN_NOTICE "NCP server OK\n");
			break;
		}
		/* JEJB/JSP 2/7/94
		 * we have xid mismatch, so discard the packet and start
		 * again.  What a hack! but I can't call recvfrom with
		 * a null buffer yet. */
		_recv(sock, (void *) &reply, sizeof(reply), MSG_DONTWAIT);

		DPRINTK(KERN_WARNING "ncp_rpc_call: reply mismatch\n");
		goto re_select;
	}
	/* 
	 * we have the correct reply, so read into the correct place and
	 * return it
	 */
	result = _recv(sock, (void *) start, server->packet_size, MSG_DONTWAIT);
	if (result < 0) {
		printk(KERN_WARNING "NCP: notice message: result=%d\n", result);
	} else if (result < sizeof(struct ncp_reply_header)) {
		printk(KERN_ERR "NCP: just caught a too small read memory size..., "
		       "email to NET channel\n");
		printk(KERN_ERR "NCP: result=%d\n", result);
		result = -EIO;
	}

	spin_lock_irqsave(&current->sigmask_lock, flags);
	current->blocked = old_set;
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, flags);
	
	set_fs(fs);
	return result;
}

/*
 * We need the server to be locked here, so check!
 */

static int ncp_do_request(struct ncp_server *server, int size)
{
	int result;

	if (server->lock == 0) {
		printk(KERN_ERR "ncpfs: Server not locked!\n");
		return -EIO;
	}
	if (!ncp_conn_valid(server)) {
		return -EIO;
	}
#ifdef CONFIG_NCPFS_PACKET_SIGNING
	if (server->sign_active)
	{
		sign_packet(server, &size);
	}
#endif /* CONFIG_NCPFS_PACKET_SIGNING */
	result = do_ncp_rpc_call(server, size);

	DDPRINTK(KERN_DEBUG "do_ncp_rpc_call returned %d\n", result);

	if (result < 0) {
		/* There was a problem with I/O, so the connections is
		 * no longer usable. */
		ncp_invalidate_conn(server);
	}
	return result;
}

/* ncp_do_request assures that at least a complete reply header is
 * received. It assumes that server->current_size contains the ncp
 * request size
 */
int ncp_request(struct ncp_server *server, int function)
{
	struct ncp_request_header *h;
	struct ncp_reply_header *reply;
	int request_size = server->current_size
			 - sizeof(struct ncp_request_header);
	int result;

	h = (struct ncp_request_header *) (server->packet);
	if (server->has_subfunction != 0) {
		*(__u16 *) & (h->data[0]) = htons(request_size - 2);
	}
	h->type = NCP_REQUEST;

	server->sequence += 1;
	h->sequence = server->sequence;
	h->conn_low = (server->connection) & 0xff;
	h->conn_high = ((server->connection) & 0xff00) >> 8;
	/*
	 * The server shouldn't know or care what task is making a
	 * request, so we always use the same task number.
	 */
	h->task = 2; /* (current->pid) & 0xff; */
	h->function = function;

	result = ncp_do_request(server, request_size + sizeof(*h));
	if (result < 0) {
		DPRINTK(KERN_WARNING "ncp_request_error: %d\n", result);
		goto out;
	}
	reply = (struct ncp_reply_header *) (server->packet);
	server->completion = reply->completion_code;
	server->conn_status = reply->connection_state;
	server->reply_size = result;
	server->ncp_reply_size = result - sizeof(struct ncp_reply_header);

	result = reply->completion_code;

#ifdef NCPFS_PARANOIA
if (result != 0)
printk(KERN_DEBUG "ncp_request: completion code=%x\n", result);
#endif
out:
	return result;
}

int ncp_connect(struct ncp_server *server)
{
	struct ncp_request_header *h;
	int result;

	h = (struct ncp_request_header *) (server->packet);
	h->type = NCP_ALLOC_SLOT_REQUEST;

	server->sequence = 0;
	h->sequence	= server->sequence;
	h->conn_low	= 0xff;
	h->conn_high	= 0xff;
	h->task		= 2; /* see above */
	h->function	= 0;

	result = ncp_do_request(server, sizeof(*h));
	if (result < 0)
		goto out;
	server->sequence = 0;
	server->connection = h->conn_low + (h->conn_high * 256);
	result = 0;
out:
	return result;
}

int ncp_disconnect(struct ncp_server *server)
{
	struct ncp_request_header *h;

	h = (struct ncp_request_header *) (server->packet);
	h->type = NCP_DEALLOC_SLOT_REQUEST;

	server->sequence += 1;
	h->sequence	= server->sequence;
	h->conn_low	= (server->connection) & 0xff;
	h->conn_high	= ((server->connection) & 0xff00) >> 8;
	h->task		= 2; /* see above */
	h->function	= 0;

	return ncp_do_request(server, sizeof(*h));
}

void ncp_lock_server(struct ncp_server *server)
{
#if 0
	/* For testing, only 1 process */
	if (server->lock != 0) {
		DPRINTK(KERN_WARNING "ncpfs: server locked!!!\n");
	}
#endif
	while (server->lock)
		sleep_on(&server->wait);
	server->lock = 1;
}

void ncp_unlock_server(struct ncp_server *server)
{
	if (server->lock != 1) {
		printk(KERN_WARNING "ncp_unlock_server: was not locked!\n");
	}
	server->lock = 0;
	wake_up(&server->wait);
}
