/*
 *  linux/fs/ncpfs/sock.c
 *
 *  Copyright (C) 1992, 1993  Rick Sladkey
 *
 *  Modified 1995, 1996 by Volker Lendecke to be usable for ncp
 *
 */

#include <linux/sched.h>
#include <linux/ncp_fs.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <asm/segment.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/ipx.h>

#include <linux/ncp.h>
#include <linux/ncp_fs.h>
#include <linux/ncp_fs_sb.h>
#include <net/sock.h>


#define _S(nr) (1<<((nr)-1))
static int _recvfrom(struct socket *sock, unsigned char *ubuf,
		     int size, int noblock, unsigned flags,
		     struct sockaddr_ipx *sa, int *addr_len)
{
        struct iovec iov;
        struct msghdr msg;

        iov.iov_base = ubuf;
        iov.iov_len  = size;

        msg.msg_name      = (void *)sa;
        msg.msg_namelen   = 0;
        if (addr_len)
                msg.msg_namelen = *addr_len;
        msg.msg_control = NULL;
        msg.msg_iov       = &iov;
        msg.msg_iovlen    = 1;

        return sock->ops->recvmsg(sock, &msg, size, noblock, flags, addr_len);
}

static int _sendto(struct socket *sock, const void *buff,
		   int len, int nonblock, unsigned flags,
		   struct sockaddr_ipx *sa, int addr_len)

{
        struct iovec iov;
        struct msghdr msg;

        iov.iov_base = (void *)buff;
        iov.iov_len  = len;

        msg.msg_name      = (void *)sa;
        msg.msg_namelen   = addr_len;
        msg.msg_control = NULL;
        msg.msg_iov       = &iov;
        msg.msg_iovlen    = 1;

        return sock->ops->sendmsg(sock, &msg, len, nonblock, flags);
}


static void
ncp_wdog_data_ready(struct sock *sk, int len)
{
	struct socket *sock = sk->socket;

	if (!sk->dead)
	{
		unsigned char packet_buf[2];
		struct sockaddr_ipx sender;
		int addr_len = sizeof(struct sockaddr_ipx);
		int result;
		unsigned short fs;

		fs = get_fs();
		set_fs(get_ds());

		result = _recvfrom(sock, (void *)packet_buf, 2, 1, 0,
				   &sender, &addr_len);

		if (   (result != 2)
		    || (packet_buf[1] != '?')
		    /* How to check connection number here? */
		    )
		{
			printk("ncpfs: got strange packet on watchdog "
			       "socket\n");
		}
		else
		{
			int result;
			DDPRINTK("ncpfs: got watchdog from:\n");
			DDPRINTK("ncpfs: %08lX:%02X%02X%02X%02X%02X%02X:%04X,"
				" conn:%02X,type:%c\n",
				htonl(sender.sipx_network),
				sender.sipx_node[0], sender.sipx_node[1],
				sender.sipx_node[2], sender.sipx_node[3],
				sender.sipx_node[4], sender.sipx_node[5],
				ntohs(sender.sipx_port),
				packet_buf[0], packet_buf[1]);

			packet_buf[1] = 'Y';
			result = _sendto(sock, (void *)packet_buf, 2, 1, 0,
					 &sender, sizeof(sender));
			DDPRINTK("send result: %d\n", result);
		}
		set_fs(fs);
	}
}

int
ncp_catch_watchdog(struct ncp_server *server)
{
        struct file   *file;
        struct inode  *inode;
        struct socket *sock;
        struct sock   *sk;

        if (   (server == NULL)
            || ((file  = server->wdog_filp) == NULL)
            || ((inode = file->f_inode) == NULL)
            || (!S_ISSOCK(inode->i_mode)))
	{
                printk("ncp_catch_watchdog: did not get valid server!\n");
                server->data_ready = NULL;
                return -EINVAL;
        }

        sock = &(inode->u.socket_i);

        if (sock->type != SOCK_DGRAM)
	{
                printk("ncp_catch_watchdog: did not get SOCK_DGRAM\n");
                server->data_ready = NULL;
                return -EINVAL;
        }

        sk   = (struct sock *)(sock->data);

        if (sk == NULL)
	{
                printk("ncp_catch_watchdog: sk == NULL");
                server->data_ready = NULL;
                return -EINVAL;
        }

        DDPRINTK("ncp_catch_watchdog: sk->d_r = %x, server->d_r = %x\n",
                 (unsigned int)(sk->data_ready),
                 (unsigned int)(server->data_ready));

        if (sk->data_ready == ncp_wdog_data_ready)
	{
                printk("ncp_catch_watchdog: already done\n");
                return -EINVAL;
        }

        server->data_ready = sk->data_ready;
        sk->data_ready = ncp_wdog_data_ready;
	sk->allocation = GFP_ATOMIC;
        return 0;
}
                
int
ncp_dont_catch_watchdog(struct ncp_server *server)
{
        struct file   *file;
        struct inode  *inode;
        struct socket *sock;
        struct sock   *sk;

        if (   (server == NULL)
            || ((file  = server->wdog_filp) == NULL)
            || ((inode = file->f_inode) == NULL)
            || (!S_ISSOCK(inode->i_mode)))
	{
                printk("ncp_dont_catch_watchdog: "
                       "did not get valid server!\n");
                return -EINVAL;
        }

        sock = &(inode->u.socket_i);

        if (sock->type != SOCK_DGRAM)
	{
                printk("ncp_dont_catch_watchdog: did not get SOCK_DGRAM\n");
                return -EINVAL;
        }

        sk = (struct sock *)(sock->data);

        if (sk == NULL)
	{
                printk("ncp_dont_catch_watchdog: sk == NULL");
                return -EINVAL;
        }

        if (server->data_ready == NULL)
	{
                printk("ncp_dont_catch_watchdog: "
                       "server->data_ready == NULL\n");
                return -EINVAL;
        }

        if (sk->data_ready != ncp_wdog_data_ready)
	{
                printk("ncp_dont_catch_watchdog: "
                       "sk->data_callback != ncp_data_callback\n");
                return -EINVAL;
        }

        DDPRINTK("ncp_dont_catch_watchdog: sk->d_r = %x, server->d_r = %x\n",
                 (unsigned int)(sk->data_ready),
                 (unsigned int)(server->data_ready));

        sk->data_ready = server->data_ready;
	sk->allocation = GFP_KERNEL;
        server->data_ready = NULL;
        return 0;
}

static void
ncp_msg_data_ready(struct sock *sk, int len)
{
	struct socket *sock = sk->socket;

	if (!sk->dead)
	{
		unsigned char packet_buf[2];
		struct sockaddr_ipx sender;
		int addr_len = sizeof(struct sockaddr_ipx);
		int result;
		unsigned short fs;

		fs = get_fs();
		set_fs(get_ds());

		result = _recvfrom(sock, (void *)packet_buf, 2, 1, 0,
				   &sender, &addr_len);

		DPRINTK("ncpfs: got message of size %d from:\n", result);
		DPRINTK("ncpfs: %08lX:%02X%02X%02X%02X%02X%02X:%04X,"
			" conn:%02X,type:%c\n",
			htonl(sender.sipx_network),
			sender.sipx_node[0], sender.sipx_node[1],
			sender.sipx_node[2], sender.sipx_node[3],
			sender.sipx_node[4], sender.sipx_node[5],
			ntohs(sender.sipx_port),
			packet_buf[0], packet_buf[1]);

		ncp_trigger_message(sk->protinfo.af_ipx.ncp_server);

		set_fs(fs);
	}
}

int
ncp_catch_message(struct ncp_server *server)
{
        struct file   *file;
        struct inode  *inode;
        struct socket *sock;
        struct sock   *sk;

        if (   (server == NULL)
            || ((file  = server->msg_filp) == NULL)
            || ((inode = file->f_inode) == NULL)
            || (!S_ISSOCK(inode->i_mode)))
	{
                printk("ncp_catch_message: did not get valid server!\n");
                return -EINVAL;
        }

        sock = &(inode->u.socket_i);

        if (sock->type != SOCK_DGRAM)
	{
                printk("ncp_catch_message: did not get SOCK_DGRAM\n");
                return -EINVAL;
        }

        sk = (struct sock *)(sock->data);

        if (sk == NULL)
	{
                printk("ncp_catch_message: sk == NULL");
                return -EINVAL;
        }

        DDPRINTK("ncp_catch_message: sk->d_r = %x\n",
                 (unsigned int)(sk->data_ready));

        if (sk->data_ready == ncp_msg_data_ready)
	{
                printk("ncp_catch_message: already done\n");
                return -EINVAL;
        }

        sk->data_ready = ncp_msg_data_ready;
	sk->protinfo.af_ipx.ncp_server = server;
        return 0;
}
                
#define NCP_SLACK_SPACE 1024

#define _S(nr) (1<<((nr)-1))

static int
do_ncp_rpc_call(struct ncp_server *server, int size)
{
	struct file *file;
	struct inode *inode;
	struct socket *sock;
	unsigned short fs;
	int result;
	char *start = server->packet;
	select_table wait_table;
	struct select_table_entry entry;
	int (*select) (struct inode *, struct file *, int, select_table *);
	int init_timeout, max_timeout;
	int timeout;
	int retrans;
	int major_timeout_seen;
	int acknowledge_seen;
	char *server_name;
	int n;
	int addrlen;
	unsigned long old_mask;

	/* We have to check the result, so store the complete header */
	struct ncp_request_header request =
		*((struct ncp_request_header *)(server->packet));

	struct ncp_reply_header reply;	


	file = server->ncp_filp;
	inode = file->f_inode;
	select = file->f_op->select;
	sock = &inode->u.socket_i;
	if (!sock)
	{
		printk("ncp_rpc_call: socki_lookup failed\n");
		return -EBADF;
	}
	init_timeout = server->m.time_out;
	max_timeout = NCP_MAX_RPC_TIMEOUT;
	retrans = server->m.retry_count;
	major_timeout_seen = 0;
	acknowledge_seen = 0;
	server_name = server->m.server_name;
	old_mask = current->blocked;
	current->blocked |= ~(_S(SIGKILL)
#if 0
		| _S(SIGSTOP)
#endif
		| ((server->m.flags & NCP_MOUNT_INTR)
		? ((current->sig->action[SIGINT - 1].sa_handler == SIG_DFL
			? _S(SIGINT) : 0)
		| (current->sig->action[SIGQUIT - 1].sa_handler == SIG_DFL
			? _S(SIGQUIT) : 0))
		: 0));
	fs = get_fs();
	set_fs(get_ds());
	for (n = 0, timeout = init_timeout; ; n++, timeout <<= 1)
	{
		DDPRINTK("ncpfs: %08lX:%02X%02X%02X%02X%02X%02X:%04X\n",
			htonl(server->m.serv_addr.sipx_network),
			server->m.serv_addr.sipx_node[0],
			server->m.serv_addr.sipx_node[1],
			server->m.serv_addr.sipx_node[2],
			server->m.serv_addr.sipx_node[3],
			server->m.serv_addr.sipx_node[4],
			server->m.serv_addr.sipx_node[5],
			ntohs(server->m.serv_addr.sipx_port));
		DDPRINTK("ncpfs: req.typ: %04X, con: %d, "
			"seq: %d",
			request.type,
			(request.conn_high << 8) + request.conn_low,
			request.sequence);
		DDPRINTK(" func: %d\n",
			 request.function);

		result = _sendto(sock, (void *) start, size, 0, 0,
				 &(server->m.serv_addr),
				 sizeof(server->m.serv_addr));
		if (result < 0)
		{
			printk("ncp_rpc_call: send error = %d\n", result);
			break;
		}
	re_select:
		wait_table.nr = 0;
		wait_table.entry = &entry;
		current->state = TASK_INTERRUPTIBLE;
		if (   !select(inode, file, SEL_IN, &wait_table)
		    && !select(inode, file, SEL_IN, NULL))
		{
			if (timeout > max_timeout)
			{
				/* JEJB/JSP 2/7/94
				 * This is useful to see if the system is
				 * hanging */
				if (acknowledge_seen == 0)
				{
					printk("NCP max timeout reached on "
					       "%s\n", server_name);
				}
				timeout = max_timeout;
			}
			current->timeout = jiffies + timeout;
			schedule();
			remove_wait_queue(entry.wait_address, &entry.wait);
			current->state = TASK_RUNNING;
			if (current->signal & ~current->blocked)
			{
				current->timeout = 0;
				result = -ERESTARTSYS;
				break;
			}
			if (!current->timeout)
			{
				if (n < retrans)
					continue;
				if (server->m.flags & NCP_MOUNT_SOFT)
				{
					printk("NCP server %s not responding, "
						"timed out\n", server_name);
					result = -EIO;
					break;
				}
				n = 0;
				timeout = init_timeout;
				init_timeout <<= 1;
				if (!major_timeout_seen)
				{
					printk("NCP server %s not responding, "
					       "still trying\n", server_name);
				}
				major_timeout_seen = 1;
				continue;
			}
			else
				current->timeout = 0;
		}
		else if (wait_table.nr)
			remove_wait_queue(entry.wait_address, &entry.wait);
		current->state = TASK_RUNNING;
		addrlen = 0;

		/* Get the header from the next packet using a peek, so keep it
		 * on the recv queue.  If it is wrong, it will be some reply
		 * we don't now need, so discard it */
		result = _recvfrom(sock, (void *)&reply,
				   sizeof(reply), 1, MSG_PEEK,
				   NULL, &addrlen);
		if (result < 0)
		{
			if (result == -EAGAIN)
			{
				DPRINTK("ncp_rpc_call: bad select ready\n");
				goto re_select;
			}
			if (result == -ECONNREFUSED)
			{
				DPRINTK("ncp_rpc_call: server playing coy\n");
				goto re_select;
			}
			if (result != -ERESTARTSYS)
			{
				printk("ncp_rpc_call: recv error = %d\n",
					-result);
			}
			break;
		}
		if (   (result     == sizeof(reply))
		    && (reply.type == NCP_POSITIVE_ACK))
		{
			/* Throw away the packet */
			DPRINTK("ncp_rpc_call: got positive acknowledge\n");
			_recvfrom(sock, (void *)&reply, sizeof(reply), 1, 0,
				  NULL, &addrlen);
			n = 0;
			timeout = max_timeout;
			acknowledge_seen = 1;
			goto re_select;
		}

		DDPRINTK("ncpfs: rep.typ: %04X, con: %d, tsk: %d,"
			"seq: %d\n",
			reply.type,
			(reply.conn_high << 8) + reply.conn_low,
			reply.task,
			reply.sequence);
		
		if (   (result          >= sizeof(reply))
		    && (reply.type      == NCP_REPLY)
		    && (   (request.type    == NCP_ALLOC_SLOT_REQUEST)
			|| (   (reply.sequence  == request.sequence)
			    && (reply.conn_low  == request.conn_low)
/* seem to get wrong task from NW311 && (reply.task      == request.task)*/
			    && (reply.conn_high == request.conn_high))))
		{
			if (major_timeout_seen)
				printk("NCP server %s OK\n", server_name);
			break;
		}
		/* JEJB/JSP 2/7/94
		 * we have xid mismatch, so discard the packet and start
		 * again.  What a hack! but I can't call recvfrom with
		 * a null buffer yet. */
		_recvfrom(sock, (void *)&reply, sizeof(reply), 1, 0, NULL,
			  &addrlen);

		DPRINTK("ncp_rpc_call: reply mismatch\n");
		goto re_select;
	}
	/* 
	 * we have the correct reply, so read into the correct place and
	 * return it
	 */
	result = _recvfrom(sock, (void *)start, server->packet_size,
			   1, 0, NULL, &addrlen);
	if (result < 0)
	{
		printk("NCP: notice message: result=%d\n", result);
	}
	else if (result < sizeof(struct ncp_reply_header))
	{
		printk("NCP: just caught a too small read memory size..., "
		       "email to NET channel\n");
		printk("NCP: result=%d,addrlen=%d\n", result, addrlen);
		result = -EIO;
	}

	current->blocked = old_mask;
	set_fs(fs);
	return result;
}


/*
 * We need the server to be locked here, so check!
 */

static int
ncp_do_request(struct ncp_server *server, int size)
{
	int result;

	if (server->lock == 0)
	{
		printk("ncpfs: Server not locked!\n");
		return -EIO;
	}

	if (!ncp_conn_valid(server))
	{
		return -EIO;
	}

	result = do_ncp_rpc_call(server, size);

	DDPRINTK("do_ncp_rpc_call returned %d\n", result);

	if (result < 0)
	{
		/* There was a problem with I/O, so the connections is
                 * no longer usable. */
		ncp_invalidate_conn(server);
	}
	return result;
}

/* ncp_do_request assures that at least a complete reply header is
 * received. It assumes that server->current_size contains the ncp
 * request size */
int
ncp_request(struct ncp_server *server, int function)
{
	struct ncp_request_header *h
		= (struct ncp_request_header *)(server->packet);
	struct ncp_reply_header *reply
		= (struct ncp_reply_header *)(server->packet);

	int request_size = server->current_size
		- sizeof(struct ncp_request_header);

	int result;

	if (server->has_subfunction != 0)
	{
		*(__u16 *)&(h->data[0]) = request_size - 2;
	}

	h->type = NCP_REQUEST;
	
	server->sequence += 1;
	h->sequence  = server->sequence;
	h->conn_low  = (server->connection) & 0xff;
	h->conn_high = ((server->connection) & 0xff00) >> 8;
	h->task      = (current->pid) & 0xff;
	h->function  = function;

	if ((result = ncp_do_request(server, request_size + sizeof(*h))) < 0)
	{
		DPRINTK("ncp_request_error: %d\n", result);
		return result;
	}

	server->completion  = reply->completion_code;
	server->conn_status = reply->connection_state;
	server->reply_size  = result;
	server->ncp_reply_size = result - sizeof(struct ncp_reply_header);

	result = reply->completion_code;

	if (result != 0)
	{
		DPRINTK("ncp_completion_code: %x\n", result);
	}
	return result;	
}

int
ncp_connect(struct ncp_server *server)
{
	struct ncp_request_header *h
		= (struct ncp_request_header *)(server->packet);
	int result;

	h->type = NCP_ALLOC_SLOT_REQUEST;
	
	server->sequence = 0;
	h->sequence  = server->sequence;
	h->conn_low  = 0xff;
	h->conn_high = 0xff;
	h->task      = (current->pid) & 0xff;
	h->function  = 0;

	if ((result = ncp_do_request(server, sizeof(*h))) < 0)
	{
		return result;
	}

	server->sequence = 0;
	server->connection = h->conn_low + (h->conn_high * 256);
	return 0;
}
	
int
ncp_disconnect(struct ncp_server *server)
{
	struct ncp_request_header *h
		= (struct ncp_request_header *)(server->packet);

	h->type = NCP_DEALLOC_SLOT_REQUEST;
	
	server->sequence += 1;
	h->sequence  = server->sequence;
	h->conn_low  = (server->connection) & 0xff;
	h->conn_high = ((server->connection) & 0xff00) >> 8;
	h->task      = (current->pid) & 0xff;
	h->function  = 0;

	return ncp_do_request(server, sizeof(*h));
}

void
ncp_lock_server(struct ncp_server *server)
{
#if 0
	/* For testing, only 1 process */
	if (server->lock != 0)
	{
		DPRINTK("ncpfs: server locked!!!\n");
	}
#endif
        while (server->lock)
		sleep_on(&server->wait);
	server->lock = 1;
}

void
ncp_unlock_server(struct ncp_server *server)
{
        if (server->lock != 1)
	{
                printk("ncp_unlock_server: was not locked!\n");
        }

        server->lock = 0;
        wake_up(&server->wait);
}
        
