/*
 * NET3:	Implementation of BSD Unix domain sockets.
 *
 * Authors:	Alan Cox, <alan@cymru.net>
 *
 *		Currently this contains all but the file descriptor passing code.
 *		Before that goes in the odd bugs in the iovec handlers need 
 *		fixing, and this bit testing. BSD fd passing is a trivial part
 *		of the exercise.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Fixes:
 *		Linus Torvalds	:	Assorted bug cures.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <asm/segment.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/af_unix.h>
#include <linux/proc_fs.h>

static unix_socket *volatile unix_socket_list=NULL;

#define min(a,b)	(((a)<(b))?(a):(b))

/*
 * Make sure the unix name is null-terminated.
 */
static inline void unix_mkname(struct sockaddr_un * sun, unsigned long len)
{
	if (len >= sizeof(*sun))
		len = sizeof(*sun)-1;
	((char *)sun)[len]=0;
}

/*
 *	Note: Sockets may not be removed _during_ an interrupt or net_bh
 *	handler using this technique. They can be added although we do not
 *	use this facility.
 */
 
static void unix_remove_socket(unix_socket *sk)
{
	unix_socket *s;
	
	cli();
	s=unix_socket_list;
	if(s==sk)
	{
		unix_socket_list=s->next;
		sti();
		return;
	}
	while(s && s->next)
	{
		if(s->next==sk)
		{
			s->next=sk->next;
			sti();
			return;
		}
		s=s->next;
	}
	sti();
}

static void unix_insert_socket(unix_socket *sk)
{
	cli();
	sk->next=unix_socket_list;
	unix_socket_list=sk;
	sti();
}

static unix_socket *unix_find_socket(struct inode *i)
{
	unix_socket *s;
	cli();
	s=unix_socket_list;
	while(s)
	{
		if(s->protinfo.af_unix.inode==i)
		{
			sti();
			return(s);
		}
		s=s->next;
	}
	sti();
	return(NULL);
}

/*
 *	Delete a unix socket. We have to allow for deferring this on a timer.
 */

static void unix_destroy_timer(unsigned long data)
{
	unix_socket *sk=(unix_socket *)data;
	if(sk->protinfo.af_unix.locks==0 && sk->wmem_alloc==0)
	{
		if(sk->protinfo.af_unix.name)
			kfree(sk->protinfo.af_unix.name);
		kfree_s(sk,sizeof(*sk));
		return;
	}
	
	/*
	 *	Retry;
	 */
	 
	init_timer(&sk->timer);
	sk->timer.expires=jiffies+10*HZ;	/* No real hurry try it every 10 seconds or so */
	add_timer(&sk->timer);
}
	 
	 
static void unix_delayed_delete(unix_socket *sk)
{
	init_timer(&sk->timer);
	sk->timer.data=(unsigned long)sk;
	sk->timer.expires=jiffies+HZ;		/* Normally 1 second after will clean up. After that we try every 10 */
	sk->timer.function=unix_destroy_timer;
	add_timer(&sk->timer);
}
	
static void unix_destroy_socket(unix_socket *sk)
{
	struct sk_buff *skb;
	unix_remove_socket(sk);
	
	while((skb=skb_dequeue(&sk->receive_queue))!=NULL)
	{
		if(sk->state==TCP_LISTEN)
		{
			unix_socket *osk=skb->sk;
			osk->state=TCP_CLOSE;
			kfree_skb(skb, FREE_WRITE);	/* Now surplus - free the skb first before the socket */
			osk->state_change(osk);		/* So the connect wakes and cleans up (if any) */
			/* osk will be destroyed when it gets to close or the timer fires */			
		}
		else
		{
/*			unix_kill_credentials(skb);	*//* Throw out any passed fd's */
			kfree_skb(skb,FREE_WRITE);
		}
	}
	
	if(sk->protinfo.af_unix.inode!=NULL)
	{
		iput(sk->protinfo.af_unix.inode);
		sk->protinfo.af_unix.inode=NULL;
	}
	
	if(--sk->protinfo.af_unix.locks==0 && sk->wmem_alloc==0)
	{
		if(sk->protinfo.af_unix.name)
			kfree(sk->protinfo.af_unix.name);
		kfree_s(sk,sizeof(*sk));
	}
	else
	{
		sk->dead=1;
		unix_delayed_delete(sk);	/* Try every so often until buffers are all freed */
	}
}

/*
 *	Fixme: We need async I/O on AF_UNIX doing next.
 */
 
static int unix_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

/*
 *	Yes socket options work with the new unix domain socketry!!!!!!!
 */
 
static int unix_setsockopt(struct socket *sock, int level, int optname, char *optval, int optlen)
{
	unix_socket *sk=sock->data;
	if(level!=SOL_SOCKET)
		return -EOPNOTSUPP;
	return sock_setsockopt(sk,level,optname,optval,optlen);	
}

static int unix_getsockopt(struct socket *sock, int level, int optname, char *optval, int *optlen)
{
	unix_socket *sk=sock->data;
	if(level!=SOL_SOCKET)
		return -EOPNOTSUPP;
	return sock_getsockopt(sk,level,optname,optval,optlen);
}

static int unix_listen(struct socket *sock, int backlog)
{
	unix_socket *sk=sock->data;
	if(sk->type!=SOCK_STREAM)
		return -EOPNOTSUPP;		/* Only stream sockets accept */
	sk->max_ack_backlog=backlog;
	sk->state=TCP_LISTEN;
	return 0;
}

static void def_callback1(struct sock *sk)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static void def_callback2(struct sock *sk, int len)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static int unix_create(struct socket *sock, int protocol)
{
	unix_socket *sk;
/*	printk("Unix create\n");*/
	if(protocol)
		return -EPROTONOSUPPORT;
	sk=(unix_socket *)kmalloc(sizeof(*sk),GFP_KERNEL);
	if(sk==NULL)
		return -ENOMEM;
	sk->type=sock->type;
	switch(sock->type)
	{
		case SOCK_STREAM:
			break;
		case SOCK_DGRAM:
			break;
		default:
			kfree_s(sk,sizeof(*sk));
			return -ESOCKTNOSUPPORT;
	}
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->back_log);
	sk->protinfo.af_unix.family=AF_UNIX;
	sk->protinfo.af_unix.inode=NULL;
	sk->protinfo.af_unix.locks=1;	/* Us */
	sk->protinfo.af_unix.readsem=MUTEX;	/* single task reading lock */
	sk->protinfo.af_unix.name=NULL;
	sk->protinfo.af_unix.other=NULL;
	sk->protocol=0;
	sk->rmem_alloc=0;
	sk->wmem_alloc=0;
	sk->dead=0;
	sk->next=NULL;
	sk->broadcast=0;
	sk->rcvbuf=SK_RMEM_MAX;
	sk->sndbuf=SK_WMEM_MAX;
	sk->inuse=0;
	sk->debug=0;
	sk->prot=NULL;
	sk->err=0;
	sk->localroute=0;
	sk->send_head=NULL;
	sk->state=TCP_CLOSE;
	sk->priority=SOPRI_NORMAL;
	sk->ack_backlog=0;
	sk->shutdown=0;
	sk->state_change=def_callback1;
	sk->data_ready=def_callback2;
	sk->write_space=def_callback1;
	sk->error_report=def_callback1;
	sk->mtu=4096;
	sk->socket=sock;
	sock->data=(void *)sk;
	sk->sleep=sock->wait;
	sk->zapped=0;
	unix_insert_socket(sk);
	return 0;
}

static int unix_dup(struct socket *newsock, struct socket *oldsock)
{
	return unix_create(newsock,0);
}

static int unix_release(struct socket *sock, struct socket *peer)
{
	unix_socket *sk=sock->data;
	unix_socket *skpair;
	
	/* May not have data attached */
	
	if(sk==NULL)
		return 0;
		
	sk->state_change(sk);
	sk->dead=1;
	skpair=(unix_socket *)sk->protinfo.af_unix.other;	/* Person we send to (default) */
	if(sk->type==SOCK_STREAM && skpair!=NULL && skpair->state!=TCP_LISTEN)
	{
		skpair->shutdown=SHUTDOWN_MASK;		/* No more writes */
		skpair->state_change(skpair);		/* Wake any blocked writes */
	}
	if(skpair!=NULL)
		skpair->protinfo.af_unix.locks--;		/* It may now die */
	sk->protinfo.af_unix.other=NULL;			/* No pair */
	unix_destroy_socket(sk);			/* Try and flush out this socket. Throw our buffers at least */
	return 0;
}


static unix_socket *unix_find_other(char *path, int *error)
{
	int old_fs;
	int err;
	struct inode *inode;
	unix_socket *u;
	
	old_fs=get_fs();
	set_fs(get_ds());
	err = open_namei(path, 2, S_IFSOCK, &inode, NULL);
	set_fs(old_fs);
	if(err<0)
	{
		*error=err;
		return NULL;
	}
	u=unix_find_socket(inode);
	iput(inode);
	if(u==NULL)
	{
		*error=-ECONNREFUSED;
		return NULL;
	}
	return u;
}


static int unix_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_un *sun=(struct sockaddr_un *)uaddr;
	unix_socket *sk=sock->data;
	int old_fs;
	int err;
	
	if(addr_len>sizeof(struct sockaddr_un) || addr_len<3 || sun->sun_family!=AF_UNIX)
		return -EINVAL;
	unix_mkname(sun, addr_len);
	/*
	 *	Put ourselves in the filesystem
	 */
	if(sk->protinfo.af_unix.inode!=NULL)
		return -EINVAL;
	
	sk->protinfo.af_unix.name=kmalloc(addr_len+1, GFP_KERNEL);
	if(sk->protinfo.af_unix.name==NULL)
		return -ENOMEM;
	memcpy(sk->protinfo.af_unix.name, sun->sun_path, addr_len+1);
	
	old_fs=get_fs();
	set_fs(get_ds());
	
	err=do_mknod(sk->protinfo.af_unix.name,S_IFSOCK|S_IRWXUGO,0);
	if(err==0)
		err=open_namei(sk->protinfo.af_unix.name, 2, S_IFSOCK, &sk->protinfo.af_unix.inode, NULL);
	
	set_fs(old_fs);
	
	if(err<0)
	{
		kfree_s(sk->protinfo.af_unix.name,addr_len+1);
		sk->protinfo.af_unix.name=NULL;
		if(err==-EEXIST)
			return -EADDRINUSE;
		else
			return err;
	}
	
	return 0;
	
}

static int unix_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags)
{
	unix_socket *sk=sock->data;
	struct sockaddr_un *sun=(struct sockaddr_un *)uaddr;
	unix_socket *other;
	struct sk_buff *skb;
	int err;

	unix_mkname(sun, addr_len);
	if(sk->type==SOCK_STREAM && sk->protinfo.af_unix.other)
	{
		if(sock->state==SS_CONNECTING && sk->state==TCP_ESTABLISHED)
		{
			sock->state=SS_CONNECTED;
			return 0;
		}
		if(sock->state==SS_CONNECTING && sk->state == TCP_CLOSE)
		{
			sock->state=SS_UNCONNECTED;
			return -ECONNREFUSED;
		}
		if(sock->state==SS_CONNECTING)
			return -EALREADY;
		return -EISCONN;
	}
	
	if(sun->sun_family!=AF_UNIX)
		return -EINVAL;

	if(sk->type==SOCK_DGRAM && sk->protinfo.af_unix.other)
	{
		sk->protinfo.af_unix.other->protinfo.af_unix.locks--;
		sk->protinfo.af_unix.other=NULL;
		sock->state=SS_UNCONNECTED;
	}

	if(sock->type==SOCK_DGRAM)
	{
		sock->state=SS_CONNECTED;
		sk->state=TCP_ESTABLISHED;
		return 0;			/* Done */
	}
	

	if(sock->state==SS_UNCONNECTED)
	{
		/*
		 *	Now ready to connect
		 */
	 
		skb=sock_alloc_send_skb(sk, 0, 0, &err); /* Marker object */
		if(skb==NULL)
			return err;
		skb->sk=sk;				/* So they know it is us */
		skb->free=1;
		sk->state=TCP_CLOSE;
		unix_mkname(sun, addr_len);
		other=unix_find_other(sun->sun_path, &err);
		if(other==NULL)
		{
			kfree_skb(skb, FREE_WRITE);
			return err;
		}
		other->protinfo.af_unix.locks++;		/* Lock the other socket so it doesn't run off for a moment */
		other->ack_backlog++;
		sk->protinfo.af_unix.other=other;
		skb_queue_tail(&other->receive_queue,skb);
		sk->state=TCP_SYN_SENT;
		sock->state=SS_CONNECTING;
		sti();
		other->data_ready(other,0);		/* Wake up ! */		
	}
			
	
	/* Wait for an accept */
	
	cli();
	while(sk->state==TCP_SYN_SENT)
	{
		if(flags&O_NONBLOCK)
		{
			sti();
			return -EINPROGRESS;
		}
		interruptible_sleep_on(sk->sleep);
		if(current->signal & ~current->blocked)
		{
			sti();
			return -ERESTARTSYS;
		}
	}
	
	/*
	 *	Has the other end closed on us ?
	 */
	 
	if(sk->state==TCP_CLOSE)
	{
		sk->protinfo.af_unix.other->protinfo.af_unix.locks--;
		sk->protinfo.af_unix.other=NULL;
		sock->state=SS_UNCONNECTED;
		return -ECONNREFUSED;
	}
	
	/*
	 *	Amazingly it has worked
	 */
	 
	sock->state=SS_CONNECTED;
	return 0;
	
}

static int unix_socketpair(struct socket *a, struct socket *b)
{
	int err;
	unix_socket *ska,*skb;	
	
	err=unix_create(a, 0);
	if(err)
		return err;
	err=unix_create(b, 0);
	if(err)
	{
		unix_release(a, NULL);
		a->data=NULL;
		return err;
	}

	ska=a->data;
	skb=b->data;

	/* Join our sockets back to back */
	ska->protinfo.af_unix.locks++;
	skb->protinfo.af_unix.locks++;
	ska->protinfo.af_unix.other=skb;
	skb->protinfo.af_unix.other=ska;
	ska->state=TCP_ESTABLISHED;
	skb->state=TCP_ESTABLISHED;
	return 0;
}

static int unix_accept(struct socket *sock, struct socket *newsock, int flags)
{
	unix_socket *sk=sock->data;
	unix_socket *newsk, *tsk;
	struct sk_buff *skb;
	
	if(sk->type!=SOCK_STREAM)
	{
		return -EOPNOTSUPP;
	}
	if(sk->state!=TCP_LISTEN)
	{
		return -EINVAL;
	}
		
	newsk=newsock->data;
	if(sk->protinfo.af_unix.name!=NULL)
	{
		newsk->protinfo.af_unix.name=kmalloc(strlen(sk->protinfo.af_unix.name)+1, GFP_KERNEL);
		if(newsk->protinfo.af_unix.name==NULL)
			return -ENOMEM;
		strcpy(newsk->protinfo.af_unix.name, sk->protinfo.af_unix.name);
	}
		
	do
	{
		cli();
		skb=skb_dequeue(&sk->receive_queue);
		if(skb==NULL)
		{
			if(flags&O_NONBLOCK)
			{
				sti();
				return -EAGAIN;
			}
			interruptible_sleep_on(sk->sleep);
			if(current->signal & ~current->blocked)
			{
				sti();
				return -ERESTARTSYS;
			}
			sti();
		}
	}
	while(skb==NULL);
	tsk=skb->sk;
	kfree_skb(skb, FREE_WRITE);	/* The buffer is just used as a tag */
	sk->ack_backlog--;
	newsk->protinfo.af_unix.other=tsk;
	tsk->protinfo.af_unix.other=newsk;
	tsk->state=TCP_ESTABLISHED;
	newsk->state=TCP_ESTABLISHED;
	newsk->protinfo.af_unix.locks++;	/* Swap lock over */
	sk->protinfo.af_unix.locks--;	/* Locked to child socket not master */
	tsk->protinfo.af_unix.locks++;	/* Back lock */
	sti();
	tsk->state_change(tsk);		/* Wake up any sleeping connect */
	return 0;
}

static int unix_getname(struct socket *sock, struct sockaddr *uaddr, int *uaddr_len, int peer)
{
	unix_socket *sk=sock->data;
	struct sockaddr_un *sun=(struct sockaddr_un *)uaddr;
	
	if(peer)
	{
		if(sk->protinfo.af_unix.other==NULL)
			return -ENOTCONN;
		sk=sk->protinfo.af_unix.other;
	}
	sun->sun_family=AF_UNIX;
	if(sk->protinfo.af_unix.name==NULL)
	{
		*sun->sun_path=0;
		*uaddr_len=3;
		return 0;		/* Not bound */
	}
	*uaddr_len=sizeof(short)+strlen(sk->protinfo.af_unix.name)+1;
	strcpy(sun->sun_path,sk->protinfo.af_unix.name);		/* 108 byte limited */
	return 0;
}

static int unix_sendmsg(struct socket *sock, struct msghdr *msg, int len, int nonblock, int flags)
{
	unix_socket *sk=sock->data;
	unix_socket *other;
	struct sockaddr_un *sun=msg->msg_name;
	int err,size;
	struct sk_buff *skb;

	if(sk->err)
	{
		cli();
		err=sk->err;
		sk->err=0;
		sti();
		return -err;
	}
	
	if(flags || msg->msg_accrights)	/* For now */
		return -EINVAL;
		
	if(sun!=NULL)
	{
		if(sock->type==SOCK_STREAM)
		{
			if(sk->state==TCP_ESTABLISHED)
				return -EISCONN;
			else
				return -EOPNOTSUPP;
		}
	}
	if(sun==NULL)
	{
		if(sk->protinfo.af_unix.other==NULL)
			return -EINVAL;
	}

	/*
	 *	Optimisation for the fact that under 0.01% of X messages typically
	 *	need breaking up.
	 */

	if(len>(sk->sndbuf-sizeof(struct sk_buff))/2)	/* Keep two messages in the pipe so it schedules better */
	{
		if(sock->type==SOCK_DGRAM)
			return -EMSGSIZE;
		len=(sk->sndbuf-sizeof(struct sk_buff))/2;
	}
	 
	size=/*protocol_size(&proto_unix)+*/len;
	skb=sock_alloc_send_skb(sk,size,nonblock, &err);
	if(skb==NULL)
		return err;
/*	protocol_adjust(skb,&proto_unix);*/
	skb->sk=sk;
	skb->free=1;
	memcpy_fromiovec(skb_put(skb,len),msg->msg_iov, len);

	cli();
	if(sun==NULL)
	{
		other=sk->protinfo.af_unix.other;
	}
	else
	{
		unix_mkname(sun, msg->msg_namelen);
		other=unix_find_other(sun->sun_path, &err);
		if(other==NULL)
		{
			kfree_skb(skb, FREE_WRITE);
			return err;
		}
	}
	skb_queue_tail(&other->receive_queue, skb);
	sti();
	other->data_ready(other,len);
	return len;
}
		
static int unix_recvmsg(struct socket *sock, struct msghdr *msg, int size, int noblock, int flags, int *addr_len)
{
	unix_socket *sk=sock->data;
	struct sockaddr_un *sun=msg->msg_name;
	int err;
	struct sk_buff *skb;
	int copied=0;
	unsigned char *sp;
	int len;
	int num;
	struct iovec *iov=msg->msg_iov;
	int ct=msg->msg_iovlen;
	
	if(addr_len)
		*addr_len=0;
		
	if(sk->err)
	{
		cli();
		err=sk->err;
		sk->err=0;
		sti();
		return -err;
	}
	
/*	printk("get rcv sem\n");*/
	down(&sk->protinfo.af_unix.readsem);		/* Lock the socket */
/*	printk("got rcv sem\n");*/

	while(ct--)
	{
		int done=0;
		sp=iov->iov_base;
		len=iov->iov_len;
		iov++;
		
		while(done<len)
		{
			if(copied & (flags&MSG_PEEK))
			{
				up(&sk->protinfo.af_unix.readsem);
				return copied;
			}
			cli();
			skb=skb_peek(&sk->receive_queue);
			if(skb==NULL)
			{
				up(&sk->protinfo.af_unix.readsem);
				if(sk->shutdown & RCV_SHUTDOWN)
					return copied;
				if(copied)
					return copied;
				if(noblock)
				{
					return -EAGAIN;
				}
				interruptible_sleep_on(sk->sleep);
				if( current->signal & ~current->blocked)
				{
					sti();
					if(copied)
						return copied;
					return -ERESTARTSYS;
				}
				sti();
				down(&sk->protinfo.af_unix.readsem);
				continue;
			}
			if(msg->msg_name!=NULL)
			{
				sun->sun_family=AF_UNIX;
				if(skb->sk->protinfo.af_unix.name)
				{
					memcpy(sun->sun_path, skb->sk->protinfo.af_unix.name, 108);
					if(addr_len)
						*addr_len=strlen(sun->sun_path)+sizeof(short);
				}
				else
					if(addr_len)
						*addr_len=sizeof(short);
			}
			num=min(skb->len,size-copied);
			copied+=num;
			done+=num;
			if(flags&MSG_PEEK)
			{
				memcpy_tofs(sp, skb->data, num);
				break;
			}
			else
			{
				memcpy_tofs(sp, skb->data,num);
				skb_pull(skb,num);
				sp+=num;
				if(skb->len==0)
				{
					skb_unlink(skb);
					kfree_skb(skb, FREE_WRITE);
					if(sock->type==SOCK_DGRAM)
						break;
				}
			}
		}	
	}	
	up(&sk->protinfo.af_unix.readsem);
	return copied;
}

static int unix_shutdown(struct socket *sock, int mode)
{
	unix_socket *sk=(unix_socket *)sock->data;
	unix_socket *other=sk->protinfo.af_unix.other;
	if(mode&SEND_SHUTDOWN)
	{
		sk->shutdown|=SEND_SHUTDOWN;
		sk->state_change(sk);
		if(other)
		{
			other->shutdown|=RCV_SHUTDOWN;
			other->state_change(other);
		}
	}
	other=sk->protinfo.af_unix.other;
	if(mode&RCV_SHUTDOWN)
	{
		sk->shutdown|=RCV_SHUTDOWN;
		sk->state_change(sk);
		if(other)
		{
			other->shutdown|=SEND_SHUTDOWN;
			other->state_change(other);
		}
	}
	return 0;
}

		
static int unix_select(struct socket *sock,  int sel_type, select_table *wait)
{
	return datagram_select(sock->data,sel_type,wait);
}

static int unix_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	unix_socket *sk=sock->data;
	int err;
	long amount=0;
			
	switch(cmd)
	{
	
		case TIOCOUTQ:
			err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(unsigned long));
			if(err)
				return err;
			amount=sk->sndbuf-sk->wmem_alloc;
			if(amount<0)
				amount=0;
			put_fs_long(amount,(unsigned long *)arg);
			return 0;
		case TIOCINQ:
		{
			struct sk_buff *skb;
			if(sk->state==TCP_LISTEN)
				return -EINVAL;
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if((skb=skb_peek(&sk->receive_queue))!=NULL)
				amount=skb->len;
			err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(unsigned long));
			put_fs_long(amount,(unsigned long *)arg);
			return 0;
		}

		default:
			return -EINVAL;
	}
	/*NOTREACHED*/
	return(0);
}

static int unix_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	off_t pos=0;
	off_t begin=0;
	int len=0;
	unix_socket *s=unix_socket_list;
	
	len+= sprintf(buffer,"Num       RefCount Protocol Flags    Type St Path\n");
	
	while(s!=NULL)
	{
		len+=sprintf(buffer+len,"%p: %08X %08X %08lX %04X %02X",
			s,
			s->protinfo.af_unix.locks,
			0,
			s->socket->flags,
			s->socket->type,
			s->socket->state);
		if(s->protinfo.af_unix.name!=NULL)
			len+=sprintf(buffer+len, " %s\n", s->protinfo.af_unix.name);
		else
			buffer[len++]='\n';
		
		pos=begin+len;
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
		s=s->next;
	}
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	return len;
}

/*
 *	For AF_UNIX we flip everything into an iovec. If this doesnt do any speed harm then it will
 *	be easier for all the low levels to be totally iovec based.
 */
 
static int unix_recvfrom(struct socket *sock, void *ubuf, int size, int noblock, unsigned flags,
		struct sockaddr *sa, int *addr_len)
{
	struct iovec iov;
	struct msghdr msg;
	iov.iov_base=ubuf;
	iov.iov_len=size;
	msg.msg_name=(void *)sa;
	msg.msg_namelen=0;
	if (addr_len)
		msg.msg_namelen = *addr_len;
	msg.msg_accrights=NULL;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	return unix_recvmsg(sock,&msg,size,noblock,flags,addr_len);	
}

static int unix_read(struct socket *sock, char *ubuf, int size, int noblock)
{
	return unix_recvfrom(sock,ubuf,size,noblock,0,NULL,NULL);
}

static int unix_recv(struct socket *sock, void *ubuf, int size, int noblock, unsigned int flags)
{
	return unix_recvfrom(sock,ubuf,size,noblock,flags,NULL,NULL);
}

static int unix_sendto(struct socket *sock, const void *ubuf, int size, int noblock, unsigned flags,
		struct sockaddr *sa, int addr_len)
{
	struct iovec iov;
	struct msghdr msg;
	iov.iov_base=(void *)ubuf;
	iov.iov_len=size;
	msg.msg_name=(void *)sa;
	msg.msg_namelen=addr_len;
	msg.msg_accrights=NULL;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	return unix_sendmsg(sock,&msg,size,noblock,flags);	
}

static int unix_write(struct socket *sock, const char *ubuf, int size, int noblock)
{	
	return unix_sendto(sock,ubuf,size,noblock, 0, NULL, 0);
}

static int unix_send(struct socket *sock, const void *ubuf, int size, int noblock, unsigned int flags)
{
	return unix_sendto(sock,ubuf,size,noblock, flags, NULL, 0);
}


static struct proto_ops unix_proto_ops = {
	AF_UNIX,
	
	unix_create,
	unix_dup,
	unix_release,
	unix_bind,
	unix_connect,
	unix_socketpair,
	unix_accept,
	unix_getname,
	unix_read,
	unix_write,
	unix_select,
	unix_ioctl,
	unix_listen,
	unix_send,
	unix_recv,
	unix_sendto,
	unix_recvfrom,
	unix_shutdown,
	unix_setsockopt,
	unix_getsockopt,
	unix_fcntl,
	unix_sendmsg,
	unix_recvmsg
};


void unix_proto_init(struct net_proto *pro)
{
	printk("NET3: Unix domain sockets 0.07 BETA for Linux NET3.030.\n");
	sock_register(unix_proto_ops.family, &unix_proto_ops);
	proc_net_register(&(struct proc_dir_entry)
			  { PROC_NET_UNIX,  4, "unix", unix_get_info });
}
/*
 * Local variables:
 *  compile-command: "gcc -g -D__KERNEL__ -Wall -O6 -I/usr/src/linux/include -c af_unix.c"
 * End:
 */
