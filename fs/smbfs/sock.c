/*
 *  sock.c
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#include <linux/sched.h>
#include <linux/smb_fs.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <asm/segment.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <net/ip.h>

#include <linux/smb.h>
#include <linux/smbno.h>


#define _S(nr) (1<<((nr)-1))

static int _recvfrom(struct socket *sock, unsigned char *ubuf, int size, int noblock, unsigned flags,
                struct sockaddr_in *sa, int *addr_len)
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

static int _send(struct socket *sock, const void *buff, int len, int nonblock, unsigned flags) {
        struct iovec iov;
        struct msghdr msg;

        iov.iov_base = (void *)buff;
        iov.iov_len  = len;

        msg.msg_name      = NULL;
        msg.msg_namelen   = 0;
        msg.msg_control = NULL;
        msg.msg_iov       = &iov;
        msg.msg_iovlen    = 1;

        return sock->ops->sendmsg(sock, &msg, len, nonblock, flags);
}

static void
smb_data_callback(struct sock *sk,int len)
{
        struct socket *sock = sk->socket;

	if(!sk->dead)
	{
                unsigned char peek_buf[4];
                int result;
                unsigned short fs;

                fs = get_fs();
                set_fs(get_ds());

		result = _recvfrom(sock, (void *)peek_buf, 1, 1,
                                             MSG_PEEK, NULL, NULL);

                while ((result != -EAGAIN) && (peek_buf[0] == 0x85)) {

                        /* got SESSION KEEP ALIVE */
                        result = _recvfrom(sock, (void *)peek_buf,
                                                     4, 1, 0, NULL, NULL);

                        DDPRINTK("smb_data_callback:"
                                 " got SESSION KEEP ALIVE\n");

                        if (result == -EAGAIN)
                                break;

                        result = _recvfrom(sock, (void *)peek_buf,
                                                     1, 1, MSG_PEEK,
                                                     NULL, NULL);

                }

                set_fs(fs);

                if (result != -EAGAIN) {
                        wake_up_interruptible(sk->sleep);
                }
	}
}

int
smb_catch_keepalive(struct smb_server *server)
{
        struct file   *file;
        struct inode  *inode;
        struct socket *sock;
        struct sock   *sk;

        if (   (server == NULL)
            || ((file  = server->sock_file) == NULL)
            || ((inode = file->f_inode) == NULL)
            || (!S_ISSOCK(inode->i_mode))) {

                printk("smb_catch_keepalive: did not get valid server!\n");
                server->data_ready = NULL;
                return -EINVAL;
        }

        sock = &(inode->u.socket_i);

        if (sock->type != SOCK_STREAM) {
                printk("smb_catch_keepalive: did not get SOCK_STREAM\n");
                server->data_ready = NULL;
                return -EINVAL;
        }

        sk   = (struct sock *)(sock->data);

        if (sk == NULL) {
                printk("smb_catch_keepalive: sk == NULL");
                server->data_ready = NULL;
                return -EINVAL;
        }

        DDPRINTK("smb_catch_keepalive.: sk->d_r = %x, server->d_r = %x\n",
                 (unsigned int)(sk->data_ready),
                 (unsigned int)(server->data_ready));

        if (sk->data_ready == smb_data_callback) {
                printk("smb_catch_keepalive: already done\n");
                return -EINVAL;
        }

        server->data_ready = sk->data_ready;
        sk->data_ready = smb_data_callback;
        return 0;
}
                
int
smb_dont_catch_keepalive(struct smb_server *server)
{
        struct file   *file;
        struct inode  *inode;
        struct socket *sock;
        struct sock   *sk;

        if (   (server == NULL)
            || ((file  = server->sock_file) == NULL)
            || ((inode = file->f_inode) == NULL)
            || (!S_ISSOCK(inode->i_mode))) {

                printk("smb_dont_catch_keepalive: "
                       "did not get valid server!\n");
                return -EINVAL;
        }

        sock = &(inode->u.socket_i);

        if (sock->type != SOCK_STREAM) {
                printk("smb_dont_catch_keepalive: did not get SOCK_STREAM\n");
                return -EINVAL;
        }

        sk   = (struct sock *)(sock->data);

        if (sk == NULL) {
                printk("smb_dont_catch_keepalive: sk == NULL");
                return -EINVAL;
        }

        if (server->data_ready == NULL) {
                printk("smb_dont_catch_keepalive: "
                       "server->data_ready == NULL\n");
                return -EINVAL;
        }

        if (sk->data_ready != smb_data_callback) {
                printk("smb_dont_catch_keepalive: "
                       "sk->data_callback != smb_data_callback\n");
                return -EINVAL;
        }

        DDPRINTK("smb_dont_catch_keepalive: sk->d_r = %x, server->d_r = %x\n",
                 (unsigned int)(sk->data_ready),
                 (unsigned int)(server->data_ready));

        sk->data_ready = server->data_ready;
        server->data_ready = NULL;
        return 0;
}

/*
 * smb_receive_raw
 * fs points to the correct segment, sock != NULL, target != NULL
 * The smb header is only stored if want_header != 0.
 */
static int
smb_receive_raw(struct socket *sock, unsigned char *target,
                int max_raw_length, int want_header)
{
        int len, result;
        int already_read;
        unsigned char peek_buf[4];
        unsigned short fs;      /* We fool the kernel to believe
                                   we call from user space. */


 re_recv:

	fs = get_fs();
	set_fs(get_ds());
        result = _recvfrom(sock, (void *)peek_buf, 4, 0,
                                     0, NULL, NULL);
        set_fs(fs);

        if (result < 0) {
                DPRINTK("smb_receive_raw: recv error = %d\n", -result);
                return result;
        }

        if (result < 4) {
                DPRINTK("smb_receive_raw: got less than 4 bytes\n");
                return -EIO;
        }

        switch (peek_buf[0]) {

        case 0x00:
        case 0x82:
                break;

        case 0x85:
                DPRINTK("smb_receive_raw: Got SESSION KEEP ALIVE\n");
                goto re_recv;
                
        default:
                printk("smb_receive_raw: Invalid packet\n");
                return -EIO;
        }

        /* The length in the RFC NB header is the raw data length */
	len = smb_len(peek_buf); 
        if (len > max_raw_length) { 
                printk("smb_receive_raw: Received length (%d) > max_xmit (%d)!\n", 
		       len, max_raw_length);
                return -EIO;
	}

        if (want_header != 0) {
                memcpy_tofs(target, peek_buf, 4);
                target += 4;
        }

        already_read = 0;

        while (already_read < len) {
                
                result = _recvfrom(sock,
                                 (void *)(target+already_read),
                                 len - already_read, 0, 0,
                                 NULL, NULL);
   
                if (result < 0) {
                        printk("smb_receive_raw: recvfrom error = %d\n",
                               -result);
                        return result;
                }

                already_read += result;
        }
        return already_read;
}

/*
 * smb_receive
 * fs points to the correct segment, server != NULL, sock!=NULL
 */
static int
smb_receive(struct smb_server *server, struct socket *sock)
{
        int result;

        result = smb_receive_raw(sock, server->packet,
                                 server->max_xmit - 4, /* max_xmit in server
                                                          includes NB header */
                                 1); /* We want the header */

        if (result < 0) {
                printk("smb_receive: receive error: %d\n", result);
                return result;
        }

        server->rcls = *((unsigned char *)(server->packet+9));
        server->err  = *((unsigned short *)(server->packet+11));

        if (server->rcls != 0) {
                DPRINTK("smb_receive: rcls=%d, err=%d\n",
                        server->rcls, server->err);
        }

        return result;
}


/*
 * smb_receive's preconditions also apply here.
 */
static int
smb_receive_trans2(struct smb_server *server, struct socket *sock,
                   int *data_len, int *param_len,
                   char **data, char **param)
{
        int total_data=0;
        int total_param=0;
        int result;
        unsigned char *inbuf = server->packet;

        *data_len = *param_len = 0;

        DDPRINTK("smb_receive_trans2: enter\n");
        
        if ((result = smb_receive(server, sock)) < 0) {
                return result;
        }

        if (server->rcls != 0) {
                return result;
        }

        /* parse out the lengths */
        total_data = WVAL(inbuf,smb_tdrcnt);
        total_param = WVAL(inbuf,smb_tprcnt);

        if (   (total_data  > TRANS2_MAX_TRANSFER)
            || (total_param > TRANS2_MAX_TRANSFER)) {
                printk("smb_receive_trans2: data/param too long\n");
                return -EIO;
        }

        /* allocate it */
        if ((*data  = smb_kmalloc(total_data, GFP_KERNEL)) == NULL) {
                printk("smb_receive_trans2: could not alloc data area\n");
                return -ENOMEM;
        }

        if ((*param = smb_kmalloc(total_param, GFP_KERNEL)) == NULL) {
                printk("smb_receive_trans2: could not alloc param area\n");
                smb_kfree_s(*data, total_data);
                return -ENOMEM;
        }

        DDPRINTK("smb_rec_trans2: total_data/param: %d/%d\n",
                 total_data, total_param);

        while (1)
        {
                if (WVAL(inbuf,smb_prdisp)+WVAL(inbuf, smb_prcnt)
                    > total_param) {
                        printk("smb_receive_trans2: invalid parameters\n");
                        result = -EIO;
                        goto fail;
                }
                memcpy(*param + WVAL(inbuf,smb_prdisp),
                       smb_base(inbuf) + WVAL(inbuf,smb_proff),
                       WVAL(inbuf,smb_prcnt));
                *param_len += WVAL(inbuf,smb_prcnt);


                if (WVAL(inbuf,smb_drdisp)+WVAL(inbuf, smb_drcnt)>total_data) {
                        printk("smb_receive_trans2: invalid data block\n");
                        result = -EIO;
                        goto fail;
                }
                memcpy(*data + WVAL(inbuf,smb_drdisp),
                       smb_base(inbuf) + WVAL(inbuf,smb_droff),
                       WVAL(inbuf,smb_drcnt));
                *data_len += WVAL(inbuf,smb_drcnt);

                DDPRINTK("smb_rec_trans2: drcnt/prcnt: %d/%d\n",
                         WVAL(inbuf, smb_drcnt), WVAL(inbuf, smb_prcnt));

                /* parse out the total lengths again - they can shrink! */

                if (   (WVAL(inbuf,smb_tdrcnt) > total_data)
                    || (WVAL(inbuf,smb_tprcnt) > total_param)) {
                        printk("smb_receive_trans2: data/params grew!\n");
                        result = -EIO;
                        goto fail;
                }

                total_data = WVAL(inbuf,smb_tdrcnt);
                total_param = WVAL(inbuf,smb_tprcnt);

                if (total_data <= *data_len && total_param <= *param_len)
                        break;

                if ((result = smb_receive(server, sock)) < 0) {
                        goto fail;
                }
                if (server->rcls != 0) {
                        result = -EIO;
                        goto fail;
                }
        }

        DDPRINTK("smb_receive_trans2: normal exit\n");

        return 0;

 fail:
        DPRINTK("smb_receive_trans2: failed exit\n");

        smb_kfree_s(*param, 0); *param = NULL;
        smb_kfree_s(*data, 0);  *data = NULL;
        return result;
}

static inline struct socket *
server_sock(struct smb_server *server)
{
        struct file *file;
        struct inode *inode;

        if (server == NULL)
                return NULL;
        if ((file = server->sock_file) == NULL)
                return NULL;
        if ((inode = file->f_inode) == NULL)
                return NULL;
        return &(inode->u.socket_i);
}

int
smb_release(struct smb_server *server)
{
        struct socket *sock = server_sock(server);
        int result;

        if (sock == NULL)
                return -EINVAL;

        result = sock->ops->release(sock, NULL);
        DPRINTK("smb_release: sock->ops->release = %d\n", result);

        /* inet_release does not set sock->state.  Maybe someone is
           confused about sock->state being SS_CONNECTED while there
           is nothing behind it, so I set it to SS_UNCONNECTED.*/
        sock->state = SS_UNCONNECTED;

        result = sock->ops->create(sock, 0);
        DPRINTK("smb_release: sock->ops->create = %d\n", result);
        return result;
}

int
smb_connect(struct smb_server *server)
{
        struct socket *sock = server_sock(server);
        if (sock == NULL)
                return -EINVAL;
        if (sock->state != SS_UNCONNECTED) {
                DPRINTK("smb_connect: socket is not unconnected: %d\n",
                        sock->state);
        }
        return sock->ops->connect(sock, (struct sockaddr *)&(server->m.addr),
                                  sizeof(struct sockaddr_in), 0);
}
        
/*****************************************************************************/
/*                                                                           */
/*  This routine was once taken from nfs, which is for udp. Here TCP does     */
/*  most of the ugly stuff for us (thanks, Alan!)                            */
/*                                                                           */
/*****************************************************************************/
int
smb_request(struct smb_server *server)
{
	unsigned long old_mask;
	unsigned short fs;      /* We fool the kernel to believe
                                   we call from user space. */
	int len, result, result2;

	struct socket *sock = server_sock(server);
	unsigned char *buffer = (server == NULL) ? NULL : server->packet;

	if ((sock == NULL) || (buffer == NULL)) {
		printk("smb_request: Bad server!\n");
		return -EBADF;
	}

        if (server->state != CONN_VALID)
                return -EIO;
        	
        if ((result = smb_dont_catch_keepalive(server)) != 0) {
                server->state = CONN_INVALID;
                smb_invalidate_all_inodes(server);
                return result;
        }

        len = smb_len(buffer) + 4;

        DDPRINTK("smb_request: len = %d cmd = 0x%X\n", len, buffer[8]);

	old_mask = current->blocked;
	current->blocked |= ~(_S(SIGKILL) | _S(SIGSTOP));
	fs = get_fs();
	set_fs(get_ds());

        result = _send(sock, (void *)buffer, len, 0, 0);
        if (result < 0) {
                printk("smb_request: send error = %d\n", result);
        }
        else {
                result = smb_receive(server, sock);
        }

        /* read/write errors are handled by errno */
        current->signal &= ~_S(SIGPIPE);

	current->blocked = old_mask;
	set_fs(fs);

        if ((result2 = smb_catch_keepalive(server)) < 0) {
                result = result2;
        }

        if (result < 0) {
                server->state = CONN_INVALID;
                smb_invalidate_all_inodes(server);
        }
        
        DDPRINTK("smb_request: result = %d\n", result);

	return result;
}

/*
 * This is not really a trans2 request, we assume that you only have
 * one packet to send.
 */ 
int
smb_trans2_request(struct smb_server *server,
                   int *data_len, int *param_len,
                   char **data, char **param)
{
	unsigned long old_mask;
	unsigned short fs;      /* We fool the kernel to believe
                                   we call from user space. */
	int len, result, result2;

	struct socket *sock = server_sock(server);
	unsigned char *buffer = (server == NULL) ? NULL : server->packet;

	if ((sock == NULL) || (buffer == NULL)) {
		printk("smb_trans2_request: Bad server!\n");
		return -EBADF;
	}

        if (server->state != CONN_VALID)
                return -EIO;
        	
        if ((result = smb_dont_catch_keepalive(server)) != 0) {
                server->state = CONN_INVALID;
                smb_invalidate_all_inodes(server);
                return result;
        }

        len = smb_len(buffer) + 4;

	old_mask = current->blocked;
	current->blocked |= ~(_S(SIGKILL) | _S(SIGSTOP));
	fs = get_fs();
	set_fs(get_ds());

        DDPRINTK("smb_request: len = %d cmd = 0x%X\n", len, buffer[8]);

        result = _send(sock, (void *)buffer, len, 0, 0);
        if (result < 0) {
                printk("smb_trans2_request: send error = %d\n", result);
        }
        else {
                result = smb_receive_trans2(server, sock,
                                            data_len, param_len,
                                            data, param);
        }

        /* read/write errors are handled by errno */
        current->signal &= ~_S(SIGPIPE);

	current->blocked = old_mask;
	set_fs(fs);

        if ((result2 = smb_catch_keepalive(server)) < 0) {
                result = result2;
        }

        if (result < 0) {
                server->state = CONN_INVALID;
                smb_invalidate_all_inodes(server);
        }
        
        DDPRINTK("smb_trans2_request: result = %d\n", result);

	return result;
}

/* target must be in user space */
int
smb_request_read_raw(struct smb_server *server,
                     unsigned char *target, int max_len)
{
	unsigned long old_mask;
	int len, result, result2;
	unsigned short fs;      /* We fool the kernel to believe
                                   we call from user space. */

	struct socket *sock = server_sock(server);
	unsigned char *buffer = (server == NULL) ? NULL : server->packet;

	if ((sock == NULL) || (buffer == NULL)) {
		printk("smb_request_read_raw: Bad server!\n");
		return -EBADF;
	}

        if (server->state != CONN_VALID)
                return -EIO;
        	
        if ((result = smb_dont_catch_keepalive(server)) != 0) {
                server->state = CONN_INVALID;
                smb_invalidate_all_inodes(server);
                return result;
        }

        len = smb_len(buffer) + 4;

	old_mask = current->blocked;
	current->blocked |= ~(_S(SIGKILL) | _S(SIGSTOP));
	fs = get_fs();
	set_fs(get_ds());

        DPRINTK("smb_request_read_raw: len = %d cmd = 0x%X\n",
                len, buffer[8]);
        DPRINTK("smb_request_read_raw: target=%X, max_len=%d\n",
                (unsigned int)target, max_len);
        DPRINTK("smb_request_read_raw: buffer=%X, sock=%X\n",
                (unsigned int)buffer, (unsigned int)sock);

        result = _send(sock, (void *)buffer, len, 0, 0);

        DPRINTK("smb_request_read_raw: send returned %d\n", result);

	set_fs(fs);             /* We recv into user space */

        if (result < 0) {
                printk("smb_request_read_raw: send error = %d\n", result);
        }
        else {
                result = smb_receive_raw(sock, target, max_len, 0);
        }

        /* read/write errors are handled by errno */
        current->signal &= ~_S(SIGPIPE);
	current->blocked = old_mask;

        if ((result2 = smb_catch_keepalive(server)) < 0) {
                result = result2;
        }

        if (result < 0) {
                server->state = CONN_INVALID;
                smb_invalidate_all_inodes(server);
        }
        
        DPRINTK("smb_request_read_raw: result = %d\n", result);

	return result;
}

/* Source must be in user space. smb_request_write_raw assumes that
 * the request SMBwriteBraw has been completed successfully, so that
 * we can send the raw data now.  */
int
smb_request_write_raw(struct smb_server *server,
                      unsigned const char *source, int length)
{
	unsigned long old_mask;
	int result, result2;
	unsigned short fs;      /* We fool the kernel to believe
                                   we call from user space. */
        byte nb_header[4];

	struct socket *sock = server_sock(server);
	unsigned char *buffer = (server == NULL) ? NULL : server->packet;

	if ((sock == NULL) || (buffer == NULL)) {
                printk("smb_request_write_raw: Bad server!\n");
		return -EBADF;
	}

        if (server->state != CONN_VALID)
                return -EIO;
        	
        if ((result = smb_dont_catch_keepalive(server)) != 0) {
                server->state = CONN_INVALID;
                smb_invalidate_all_inodes(server);
                return result;
        }

	old_mask = current->blocked;
	current->blocked |= ~(_S(SIGKILL) | _S(SIGSTOP));
	fs = get_fs();
	set_fs(get_ds());

        smb_encode_smb_length(nb_header, length);

        result = _send(sock, (void *)nb_header, 4, 0, 0);

        if (result == 4) {
                set_fs(fs);     /* source is in user-land */
                result = _send(sock, (void *)source, length, 0, 0);
                set_fs(get_ds());
        } else {
                result = -EIO;
        }

        DPRINTK("smb_request_write_raw: send returned %d\n", result);

        if (result == length) {
                result = smb_receive(server, sock);
        } else {
                result = -EIO;
        }

        /* read/write errors are handled by errno */
        current->signal &= ~_S(SIGPIPE);
	current->blocked = old_mask;
	set_fs(fs);

        if ((result2 = smb_catch_keepalive(server)) < 0) {
                result = result2;
        }

        if (result < 0) {
                server->state = CONN_INVALID;
                smb_invalidate_all_inodes(server);
        }

        if (result > 0) {
                result = length;
        }
        
        DPRINTK("smb_request_write_raw: result = %d\n", result);

	return result;
}
