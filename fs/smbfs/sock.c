/*
 *  sock.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#include <linux/sched.h>
#include <linux/smb_fs.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <net/scm.h>
#include <net/ip.h>

#include <linux/smb.h>
#include <linux/smbno.h>

#include <asm/uaccess.h>

#define SMBFS_PARANOIA 1
/* #define SMBFS_DEBUG_VERBOSE 1 */

#define _S(nr) (1<<((nr)-1))

static int
_recvfrom(struct socket *socket, unsigned char *ubuf, int size,
	  unsigned flags)
{
	struct iovec iov;
	struct msghdr msg;
	struct scm_cookie scm;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	iov.iov_base = ubuf;
	iov.iov_len = size;
	
	memset(&scm, 0,sizeof(scm));
	size=socket->ops->recvmsg(socket, &msg, size, flags, &scm);
	if(size>=0)
		scm_recv(socket,&msg,&scm,flags);
	return size;
}

static int
_send(struct socket *socket, const void *buff, int len)
{
	struct iovec iov;
	struct msghdr msg;
	struct scm_cookie scm;
	int err;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	
	iov.iov_base = (void *)buff;
	iov.iov_len = len;

	msg.msg_flags = 0;

	err = scm_send(socket, &msg, &scm);
        if (err >= 0)
	{
		err = socket->ops->sendmsg(socket, &msg, len, &scm);
		scm_destroy(&scm);
	}
	return err;
}

/*
 * N.B. What happens if we're in here when the socket closes??
 */
static void
smb_data_callback(struct sock *sk, int len)
{
	struct socket *socket = sk->socket;
	unsigned char peek_buf[4];
	int result;
	unsigned long fs;

	fs = get_fs();
	set_fs(get_ds());

	while (1)
	{
		if (sk->dead)
		{
			printk("smb_data_callback: sock dead!\n");
			return;
		}
		result = _recvfrom(socket, (void *) peek_buf, 1,
				   MSG_PEEK | MSG_DONTWAIT);
		if (result == -EAGAIN)
			break;
		if (peek_buf[0] != 0x85)
			break;

		/* got SESSION KEEP ALIVE */
		result = _recvfrom(socket, (void *) peek_buf, 4,
				   MSG_DONTWAIT);

		pr_debug("smb_data_callback: got SESSION KEEPALIVE\n");

		if (result == -EAGAIN)
			break;
	}
	set_fs(fs);

	if (result != -EAGAIN)
	{
		wake_up_interruptible(sk->sleep);
	}
}

static struct socket *
server_sock(struct smb_sb_info *server)
{
	struct file *file;
	struct inode *inode;

	if (server				&& 
	    (file = server->sock_file)		&&
	    (inode = file->f_dentry->d_inode)	&& 
	    S_ISSOCK(inode->i_mode)		&& 
	    inode->u.socket_i.type == SOCK_STREAM)
		return &(inode->u.socket_i);
	return NULL;
}

int
smb_catch_keepalive(struct smb_sb_info *server)
{
	struct socket *socket;
	struct sock *sk;
	void *data_ready;
	int error;

	error = -EINVAL;
	socket = server_sock(server);
	if (!socket)
	{
		printk("smb_catch_keepalive: did not get valid server!\n");
		server->data_ready = NULL;
		goto out;
	}

	sk = socket->sk;
	if (sk == NULL)
	{
		pr_debug("smb_catch_keepalive: sk == NULL");
		server->data_ready = NULL;
		goto out;
	}
	pr_debug("smb_catch_keepalive.: sk->d_r = %x, server->d_r = %x\n",
		 (unsigned int) (sk->data_ready),
		 (unsigned int) (server->data_ready));

	/*
	 * Install the callback atomically to avoid races ...
	 */
	data_ready = xchg(&sk->data_ready, smb_data_callback);
	if (data_ready != smb_data_callback)
	{
		server->data_ready = data_ready;
		error = 0;
	} else
		printk(KERN_ERR "smb_catch_keepalive: already done\n");
out:
	return error;
}

int
smb_dont_catch_keepalive(struct smb_sb_info *server)
{
	struct socket *socket;
	struct sock *sk;
	void * data_ready;
	int error;

	error = -EINVAL;
	socket = server_sock(server);
	if (!socket)
	{
		printk("smb_dont_catch_keepalive: did not get valid server!\n");
		goto out;
	}

	sk = socket->sk;
	if (sk == NULL)
	{
		printk("smb_dont_catch_keepalive: sk == NULL");
		goto out;
	}

	/* Is this really an error?? */
	if (server->data_ready == NULL)
	{
		printk("smb_dont_catch_keepalive: "
		       "server->data_ready == NULL\n");
		goto out;
	}
	pr_debug("smb_dont_catch_keepalive: sk->d_r = %x, server->d_r = %x\n",
		 (unsigned int) (sk->data_ready),
		 (unsigned int) (server->data_ready));

	/*
	 * Restore the original callback atomically to avoid races ...
	 */
	data_ready = xchg(&sk->data_ready, server->data_ready);
	server->data_ready = NULL;
	if (data_ready != smb_data_callback)
	{
		printk("smb_dont_catch_keepalive: "
		       "sk->data_callback != smb_data_callback\n");
	}
	error = 0;
out:
	return error;
}

/*
 * Called with the server locked.
 */
void
smb_close_socket(struct smb_sb_info *server)
{
	struct file * file = server->sock_file;

	if (file)
	{
		struct socket * socket = server_sock(server);

		printk("smb_close_socket: closing socket %p\n", socket);
		/*
		 * We need a way to check for tasks running the callback!
		 */
		if (socket->sk->data_ready == smb_data_callback)
			printk("smb_close_socket: still catching keepalives!\n");

		server->sock_file = NULL;
		close_fp(file);
	}
}

static int
smb_send_raw(struct socket *socket, unsigned char *source, int length)
{
	int result;
	int already_sent = 0;

	while (already_sent < length)
	{
		result = _send(socket,
			       (void *) (source + already_sent),
			       length - already_sent);

		if (result == 0)
		{
			return -EIO;
		}
		if (result < 0)
		{
			pr_debug("smb_send_raw: sendto error = %d\n",
				 -result);
			return result;
		}
		already_sent += result;
	}
	return already_sent;
}

static int
smb_receive_raw(struct socket *socket, unsigned char *target, int length)
{
	int result;
	int already_read = 0;

	while (already_read < length)
	{
		result = _recvfrom(socket,
				   (void *) (target + already_read),
				   length - already_read, 0);

		if (result == 0)
		{
			return -EIO;
		}
		if (result < 0)
		{
			pr_debug("smb_receive_raw: recvfrom error = %d\n",
				 -result);
			return result;
		}
		already_read += result;
	}
	return already_read;
}

static int
smb_get_length(struct socket *socket, unsigned char *header)
{
	int result;
	unsigned char peek_buf[4];
	unsigned long fs;

      re_recv:
	fs = get_fs();
	set_fs(get_ds());
	result = smb_receive_raw(socket, peek_buf, 4);
	set_fs(fs);

	if (result < 0)
	{
		pr_debug("smb_get_length: recv error = %d\n", -result);
		return result;
	}
	switch (peek_buf[0])
	{
	case 0x00:
	case 0x82:
		break;

	case 0x85:
		pr_debug("smb_get_length: Got SESSION KEEP ALIVE\n");
		goto re_recv;

	default:
		pr_debug("smb_get_length: Invalid NBT packet\n");
		return -EIO;
	}

	if (header != NULL)
	{
		memcpy(header, peek_buf, 4);
	}
	/* The length in the RFC NB header is the raw data length */
	return smb_len(peek_buf);
}

/*
 * smb_receive
 * fs points to the correct segment
 */
static int
smb_receive(struct smb_sb_info *server)
{
	struct socket *socket = server_sock(server);
	int len;
	int result;
	unsigned char peek_buf[4];

	len = smb_get_length(socket, peek_buf);

	if (len < 0)
	{
		return len;
	}
	if (len + 4 > server->packet_size)
	{
		/* Some servers do not care about our max_xmit. They
		   send larger packets */
		pr_debug("smb_receive: Increase packet size from %d to %d\n",
			server->packet_size, len + 4);
		smb_vfree(server->packet);
		server->packet = 0;
		server->packet_size = 0;
		server->packet = smb_vmalloc(len + 4);
		if (server->packet == NULL)
		{
			return -ENOMEM;
		}
		server->packet_size = len + 4;
	}
	memcpy(server->packet, peek_buf, 4);
	result = smb_receive_raw(socket, server->packet + 4, len);

	if (result < 0)
	{
		pr_debug("smb_receive: receive error: %d\n", result);
		return result;
	}
	server->rcls = *(server->packet+9);
	server->err = WVAL(server->packet, 11);

#ifdef SMBFS_DEBUG_VERBOSE
if (server->rcls != 0)
printk("smb_receive: rcls=%d, err=%d\n", server->rcls, server->err);
#endif
	return result;
}

static int
smb_receive_trans2(struct smb_sb_info *server,
		   int *ldata, unsigned char **data,
		   int *lparam, unsigned char **param)
{
	int total_data = 0;
	int total_param = 0;
	int result;
	unsigned char *rcv_buf;
	int buf_len;
	int data_len = 0;
	int param_len = 0;

	if ((result = smb_receive(server)) < 0)
	{
		return result;
	}
	if (server->rcls != 0)
	{
		*param = *data = server->packet;
		*ldata = *lparam = 0;
		return 0;
	}
	total_data = WVAL(server->packet, smb_tdrcnt);
	total_param = WVAL(server->packet, smb_tprcnt);

	pr_debug("smb_receive_trans2: td=%d,tp=%d\n", total_data, total_param);

	if ((total_data > TRANS2_MAX_TRANSFER)
	    || (total_param > TRANS2_MAX_TRANSFER))
	{
		pr_debug("smb_receive_trans2: data/param too long\n");
		return -EIO;
	}
	buf_len = total_data + total_param;
	if (server->packet_size > buf_len)
	{
		buf_len = server->packet_size;
	}
	if ((rcv_buf = smb_vmalloc(buf_len)) == NULL)
	{
		pr_debug("smb_receive_trans2: could not alloc data area\n");
		return -ENOMEM;
	}
	*param = rcv_buf;
	*data = rcv_buf + total_param;

	while (1)
	{
		unsigned char *inbuf = server->packet;

		if (WVAL(inbuf, smb_prdisp) + WVAL(inbuf, smb_prcnt)
		    > total_param)
		{
			pr_debug("smb_receive_trans2: invalid parameters\n");
			result = -EIO;
			goto fail;
		}
		memcpy(*param + WVAL(inbuf, smb_prdisp),
		       smb_base(inbuf) + WVAL(inbuf, smb_proff),
		       WVAL(inbuf, smb_prcnt));
		param_len += WVAL(inbuf, smb_prcnt);

		if (WVAL(inbuf, smb_drdisp) + WVAL(inbuf, smb_drcnt)
		    > total_data)
		{
			pr_debug("smb_receive_trans2: invalid data block\n");
			result = -EIO;
			goto fail;
		}
		pr_debug("disp: %d, off: %d, cnt: %d\n",
			 WVAL(inbuf, smb_drdisp), WVAL(inbuf, smb_droff),
			 WVAL(inbuf, smb_drcnt));

		memcpy(*data + WVAL(inbuf, smb_drdisp),
		       smb_base(inbuf) + WVAL(inbuf, smb_droff),
		       WVAL(inbuf, smb_drcnt));
		data_len += WVAL(inbuf, smb_drcnt);

		if ((WVAL(inbuf, smb_tdrcnt) > total_data)
		    || (WVAL(inbuf, smb_tprcnt) > total_param))
		{
			pr_debug("smb_receive_trans2: data/params grew!\n");
			result = -EIO;
			goto fail;
		}
		/* the total lengths might shrink! */
		total_data = WVAL(inbuf, smb_tdrcnt);
		total_param = WVAL(inbuf, smb_tprcnt);

#ifdef SMBFS_PARANOIA
if ((data_len >= total_data || param_len >= total_param) &&
   !(data_len >= total_data && param_len >= total_param))
printk("smb_receive_trans2: dlen=%d, tdata=%d, plen=%d, tlen=%d\n",
data_len, total_data, param_len, total_param);
#endif
		/* shouldn't this be an OR test? don't want to overrun */
		if ((data_len >= total_data) && (param_len >= total_param))
		{
			break;
		}
		if ((result = smb_receive(server)) < 0)
		{
			goto fail;
		}
		result = -EIO;
		if (server->rcls != 0)
			goto fail;
	}
	*ldata = data_len;
	*lparam = param_len;

	smb_vfree(server->packet);
	server->packet = rcv_buf;
	server->packet_size = buf_len;
	return 0;

      fail:
	smb_vfree(rcv_buf);
	return result;
}

/*
 * Called with the server locked
 */
int
smb_request(struct smb_sb_info *server)
{
	unsigned long old_mask;
	unsigned long fs;
	int len, result;
	unsigned char *buffer;

	result = -EBADF;
	if (!server) /* this can't happen */
		goto bad_no_server;
		
	buffer = server->packet;
	if (!buffer)
		goto bad_no_packet;

	result = -EIO;
	if (server->state != CONN_VALID)
		goto bad_no_conn;

	if ((result = smb_dont_catch_keepalive(server)) != 0)
		goto bad_conn;

	len = smb_len(buffer) + 4;
	pr_debug("smb_request: len = %d cmd = 0x%X\n", len, buffer[8]);

	old_mask = current->blocked;
	current->blocked |= ~(_S(SIGKILL) | _S(SIGSTOP));
	fs = get_fs();
	set_fs(get_ds());

	result = smb_send_raw(server_sock(server), (void *) buffer, len);
	if (result > 0)
	{
		result = smb_receive(server);
	}
	/* read/write errors are handled by errno */
	current->signal &= ~_S(SIGPIPE);
	current->blocked = old_mask;
	set_fs(fs);

	if (result >= 0)
	{
		int result2 = smb_catch_keepalive(server);
		if (result2 < 0)
		{
			printk("smb_request: catch keepalive failed\n");
			result = result2;
		}
	}
	if (result < 0)
		goto bad_conn;

out:
	pr_debug("smb_request: result = %d\n", result);
	return result;
	
bad_conn:
	printk("smb_request: result %d, setting invalid\n", result);
	server->state = CONN_INVALID;
	smb_invalidate_inodes(server);
	goto out;		
bad_no_server:
	printk("smb_request: no server!\n");
	goto out;
bad_no_packet:
	printk("smb_request: no packet!\n");
	goto out;
bad_no_conn:
	printk("smb_request: connection %d not valid!\n", server->state);
	goto out;
}

#define ROUND_UP(x) (((x)+3) & ~3)
static int
smb_send_trans2(struct smb_sb_info *server, __u16 trans2_command,
		int ldata, unsigned char *data,
		int lparam, unsigned char *param)
{
	struct socket *sock = server_sock(server);
	struct scm_cookie scm;
	int err;

	/* I know the following is very ugly, but I want to build the
	   smb packet as efficiently as possible. */

	const int smb_parameters = 15;
	const int oparam =
	ROUND_UP(SMB_HEADER_LEN + 2 * smb_parameters + 2 + 3);
	const int odata =
	ROUND_UP(oparam + lparam);
	const int bcc =
	odata + ldata - (SMB_HEADER_LEN + 2 * smb_parameters + 2);
	const int packet_length =
	SMB_HEADER_LEN + 2 * smb_parameters + bcc + 2;

	unsigned char padding[4] =
	{0,};
	char *p;

	struct iovec iov[4];
	struct msghdr msg;

	if ((bcc + oparam) > server->opt.max_xmit)
	{
		return -ENOMEM;
	}
	p = smb_setup_header(server, SMBtrans2, smb_parameters, bcc);

	WSET(server->packet, smb_tpscnt, lparam);
	WSET(server->packet, smb_tdscnt, ldata);
	WSET(server->packet, smb_mprcnt, TRANS2_MAX_TRANSFER);
	WSET(server->packet, smb_mdrcnt, TRANS2_MAX_TRANSFER);
	WSET(server->packet, smb_msrcnt, 0);
	WSET(server->packet, smb_flags, 0);
	DSET(server->packet, smb_timeout, 0);
	WSET(server->packet, smb_pscnt, lparam);
	WSET(server->packet, smb_psoff, oparam - 4);
	WSET(server->packet, smb_dscnt, ldata);
	WSET(server->packet, smb_dsoff, odata - 4);
	WSET(server->packet, smb_suwcnt, 1);
	WSET(server->packet, smb_setup0, trans2_command);
	*p++ = 0;		/* null smb_name for trans2 */
	*p++ = 'D';		/* this was added because OS/2 does it */
	*p++ = ' ';


	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 4;
	msg.msg_flags = 0;
	
	iov[0].iov_base = (void *) server->packet;
	iov[0].iov_len = oparam;
	iov[1].iov_base = (param == NULL) ? padding : param;
	iov[1].iov_len = lparam;
	iov[2].iov_base = padding;
	iov[2].iov_len = odata - oparam - lparam;
	iov[3].iov_base = (data == NULL) ? padding : data;
	iov[3].iov_len = ldata;

	err = scm_send(sock, &msg, &scm);
        if (err >= 0)
	{
		err = sock->ops->sendmsg(sock, &msg, packet_length, &scm);
		scm_destroy(&scm);
	}
	return err;
}

/*
 * This is not really a trans2 request, we assume that you only have
 * one packet to send.
 */
int
smb_trans2_request(struct smb_sb_info *server, __u16 trans2_command,
		   int ldata, unsigned char *data,
		   int lparam, unsigned char *param,
		   int *lrdata, unsigned char **rdata,
		   int *lrparam, unsigned char **rparam)
{
	unsigned long old_mask;
	unsigned long fs;
	int result;

	pr_debug("smb_trans2_request: com=%d, ld=%d, lp=%d\n",
		 trans2_command, ldata, lparam);

	/*
	 * These are initialized in smb_request_ok, but not here??
	 */
	server->rcls = 0;
	server->err = 0;

	result = -EIO;
	if (server->state != CONN_VALID)
		goto out;

	if ((result = smb_dont_catch_keepalive(server)) != 0)
		goto bad_conn;

	old_mask = current->blocked;
	current->blocked |= ~(_S(SIGKILL) | _S(SIGSTOP));
	fs = get_fs();
	set_fs(get_ds());

	result = smb_send_trans2(server, trans2_command,
				 ldata, data, lparam, param);
	if (result >= 0)
	{
		result = smb_receive_trans2(server,
					    lrdata, rdata, lrparam, rparam);
	}
	/* read/write errors are handled by errno */
	current->signal &= ~_S(SIGPIPE);
	current->blocked = old_mask;
	set_fs(fs);

	if (result >= 0)
	{
		int result2 = smb_catch_keepalive(server);
		if (result2 < 0)
		{
			result = result2;
		}
	}
	if (result < 0)
		goto bad_conn;
	pr_debug("smb_trans2_request: result = %d\n", result);

out:
	return result;

bad_conn:
	printk("smb_trans2_request: connection bad, setting invalid\n");
	server->state = CONN_INVALID;
	smb_invalidate_inodes(server);
	goto out;
}
