/*
 *  sock.c
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#include <linux/config.h>
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

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

		result = sock->ops->recvfrom(sock, (void *)peek_buf, 1, 1,
                                             MSG_PEEK, NULL, NULL);

                while ((result != -EAGAIN) && (peek_buf[0] == 0x85)) {

                        /* got SESSION KEEP ALIVE */
                        result = sock->ops->recvfrom(sock, (void *)peek_buf,
                                                     4, 1, 0, NULL, NULL);

                        DDPRINTK("smb_data_callback:"
                                 " got SESSION KEEP ALIVE\n");

                        if (result == -EAGAIN)
                                break;

                        result = sock->ops->recvfrom(sock, (void *)peek_buf,
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
 * smb_receive
 * fs points to the correct segment, server != NULL, sock!=NULL
 */
static int
smb_receive(struct smb_server *server, struct socket *sock)
{
        int len, result;
        unsigned char peek_buf[4];

 re_recv:

        result = sock->ops->recvfrom(sock, (void *)peek_buf, 4, 0,
                                     MSG_PEEK, NULL, NULL);

        if (result < 0) {
                DPRINTK("smb_receive: recv error = %d\n", -result);
                return result;
        }

        if (result == 0) {
                DPRINTK("smb_receive: got 0 bytes\n");
                return -EIO;
        }

        switch (peek_buf[0]) {

        case 0x00:
        case 0x82:
                break;

        case 0x85:
                DPRINTK("smb_receive: Got SESSION KEEP ALIVE\n");
                sock->ops->recvfrom(sock, (void *)peek_buf, 4, 1,
                                    0, NULL, NULL);
                goto re_recv;
                
        default:
                printk("smb_receive: Invalid packet\n");
                return -EIO;
        }

        /* Length not including first four bytes. */
	len = smb_len(peek_buf) + 4; 
        if (len > server->max_xmit) { 
                printk("smb_receive: Received length (%d) > max_xmit (%d)!\n", 
		       len, server->max_xmit);
                return -EIO;
	}
        else
        {
                int already_read = 0;

                while (already_read < len) {
                
                        result = sock->ops->
                                recvfrom(sock,
                                         (void *)(server->packet+already_read),
                                         len - already_read, 0, 0,
                                         NULL, NULL);
   
                        if (result < 0) {
                                printk("SMB: notice message: error = %d\n",
                                       -result);
                                return result;
                        }

                        already_read += result;
                }
                result = already_read;
        }

        server->rcls = *((unsigned char *)(server->packet+9));
        server->err  = *((unsigned short *)(server->packet+11));

        if (server->rcls != 0) {
                DPRINTK("smb_response: rcls=%d, err=%d\n",
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
/*  This routine was once taken from nfs, wich is for udp. Here TCP does     */
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
        	
#if 0
	while (server->lock)
		sleep_on(&server->wait);
	server->lock = 1;
#endif

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

        result = sock->ops->send(sock, (void *)buffer, len, 0, 0);
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

#if 0
	server->lock = 0;
	wake_up(&server->wait);
#endif

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
        	
#if 0
	while (server->lock)
		sleep_on(&server->wait);
	server->lock = 1;
#endif

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

        result = sock->ops->send(sock, (void *)buffer, len, 0, 0);
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

#if 0
	server->lock = 0;
	wake_up(&server->wait);
#endif

        if (result < 0) {
                server->state = CONN_INVALID;
                smb_invalidate_all_inodes(server);
        }
        
        DDPRINTK("smb_trans2_request: result = %d\n", result);

	return result;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */
