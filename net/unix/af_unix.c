/*
 * NET3:	Implementation of BSD Unix domain sockets.
 *
 * Authors:	Alan Cox, <alan.cox@linux.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Version:	$Id: af_unix.c,v 1.73 1999/01/15 06:55:48 davem Exp $
 *
 * Fixes:
 *		Linus Torvalds	:	Assorted bug cures.
 *		Niibe Yutaka	:	async I/O support.
 *		Carsten Paeth	:	PF_UNIX check, address fixes.
 *		Alan Cox	:	Limit size of allocated blocks.
 *		Alan Cox	:	Fixed the stupid socketpair bug.
 *		Alan Cox	:	BSD compatibility fine tuning.
 *		Alan Cox	:	Fixed a bug in connect when interrupted.
 *		Alan Cox	:	Sorted out a proper draft version of
 *					file descriptor passing hacked up from
 *					Mike Shaver's work.
 *		Marty Leisner	:	Fixes to fd passing
 *		Nick Nevin	:	recvmsg bugfix.
 *		Alan Cox	:	Started proper garbage collector
 *		Heiko EiBfeldt	:	Missing verify_area check
 *		Alan Cox	:	Started POSIXisms
 *		Andreas Schwab	:	Replace inode by dentry for proper
 *					reference counting
 *		Kirk Petersen	:	Made this a module
 *	    Christoph Rohland	:	Elegant non-blocking accept/connect algorithm.
 *					Lots of bug fixes.
 *	     Alexey Kuznetosv	:	Repaired (I hope) bugs introduces
 *					by above two patches.
 *
 * Known differences from reference BSD that was tested:
 *
 *	[TO FIX]
 *	ECONNREFUSED is not returned from one end of a connected() socket to the
 *		other the moment one end closes.
 *	fstat() doesn't return st_dev=NODEV, and give the blksize as high water mark
 *		and a fake inode identifier (nor the BSD first socket fstat twice bug).
 *	[NOT TO FIX]
 *	accept() returns a path name even if the connecting socket has closed
 *		in the meantime (BSD loses the path and gives up).
 *	accept() returns 0 length path for an unbound connector. BSD returns 16
 *		and a null first byte in the path (but not for gethost/peername - BSD bug ??)
 *	socketpair(...SOCK_RAW..) doesn't panic the kernel.
 *	BSD af_unix apparently has connect forgetting to block properly.
 *		(need to check this with the POSIX spec in detail)
 *
 * Differences from 2.0.0-11-... (ANK)
 *	Bug fixes and improvements.
 *		- client shutdown killed server socket.
 *		- removed all useless cli/sti pairs.
 *
 *	Semantic changes/extensions.
 *		- generic control message passing.
 *		- SCM_CREDENTIALS control message.
 *		- "Abstract" (not FS based) socket bindings.
 *		  Abstract names are sequences of bytes (not zero terminated)
 *		  started by 0, so that this name space does not intersect
 *		  with BSD names.
 */

#include <linux/module.h>
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
#include <asm/uaccess.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/af_unix.h>
#include <linux/proc_fs.h>
#include <net/scm.h>
#include <linux/init.h>
#include <linux/poll.h>

#include <asm/checksum.h>

#define min(a,b)	(((a)<(b))?(a):(b))

int sysctl_unix_delete_delay = HZ;
int sysctl_unix_destroy_delay = 10*HZ;

unix_socket *unix_socket_table[UNIX_HASH_SIZE+1];

#define unix_sockets_unbound	(unix_socket_table[UNIX_HASH_SIZE])

#define UNIX_ABSTRACT(sk)	((sk)->protinfo.af_unix.addr->hash!=UNIX_HASH_SIZE)

static void unix_destroy_socket(unix_socket *sk);
static void unix_stream_write_space(struct sock *sk);

extern __inline__ unsigned unix_hash_fold(unsigned hash)
{
	hash ^= hash>>16;
	hash ^= hash>>8;
	hash ^= hash>>4;
	return hash;
}

#define unix_peer(sk) ((sk)->pair)

extern __inline__ int unix_our_peer(unix_socket *sk, unix_socket *osk)
{
	return unix_peer(osk) == sk;
}

extern __inline__ int unix_may_send(unix_socket *sk, unix_socket *osk)
{
	return (unix_peer(osk) == NULL || unix_our_peer(sk, osk));
}

extern __inline__ void unix_lock(unix_socket *sk)
{
	atomic_inc(&sk->sock_readers);
}

extern __inline__ void unix_unlock(unix_socket *sk)
{
	atomic_dec(&sk->sock_readers);
}

extern __inline__ int unix_locked(unix_socket *sk)
{
	return atomic_read(&sk->sock_readers);
}

extern __inline__ void unix_release_addr(struct unix_address *addr)
{
	if (addr)
	{
		if (atomic_dec_and_test(&addr->refcnt))
			kfree(addr);
	}
}

static void unix_destruct_addr(struct sock *sk)
{
	struct unix_address *addr = sk->protinfo.af_unix.addr;

	unix_release_addr(addr);
}

/*
 *	Check unix socket name:
 *		- should be not zero length.
 *	        - if started by not zero, should be NULL terminated (FS object)
 *		- if started by zero, it is abstract name.
 */
 
static int unix_mkname(struct sockaddr_un * sunaddr, int len, unsigned *hashp)
{
	if (len <= sizeof(short) || len > sizeof(*sunaddr))
		return -EINVAL;
	if (!sunaddr || sunaddr->sun_family != AF_UNIX)
		return -EINVAL;
	if (sunaddr->sun_path[0])
	{
		/*
		 *	This may look like an off by one error but it is
		 *	a bit more subtle. 108 is the longest valid AF_UNIX
		 *	path for a binding. sun_path[108] doesnt as such
		 *	exist. However in kernel space we are guaranteed that
		 *	it is a valid memory location in our kernel
		 *	address buffer.
		 */
		if (len > sizeof(*sunaddr))
			len = sizeof(*sunaddr);
		((char *)sunaddr)[len]=0;
		len = strlen(sunaddr->sun_path)+1+sizeof(short);
		return len;
	}

	*hashp = unix_hash_fold(csum_partial((char*)sunaddr, len, 0));
	return len;
}

static void unix_remove_socket(unix_socket *sk)
{
	unix_socket **list = sk->protinfo.af_unix.list;
	if (sk->next)
		sk->next->prev = sk->prev;
	if (sk->prev)
		sk->prev->next = sk->next;
	if (*list == sk)
		*list = sk->next;
	sk->protinfo.af_unix.list = NULL;
	sk->prev = NULL;
	sk->next = NULL;
}

static void unix_insert_socket(unix_socket *sk)
{
	unix_socket **list = sk->protinfo.af_unix.list;
	sk->prev = NULL;
	sk->next = *list;
	if (*list)
		(*list)->prev = sk;
	*list=sk;
}

static unix_socket *unix_find_socket_byname(struct sockaddr_un *sunname,
					    int len, int type, unsigned hash)
{
	unix_socket *s;

	for (s=unix_socket_table[(hash^type)&0xF]; s; s=s->next)
	{
		if(s->protinfo.af_unix.addr->len==len &&
		   memcmp(s->protinfo.af_unix.addr->name, sunname, len) == 0 &&
		   s->type == type)
		{
			unix_lock(s);
			return(s);
		}
	}
	return(NULL);
}

static unix_socket *unix_find_socket_byinode(struct inode *i)
{
	unix_socket *s;

	for (s=unix_socket_table[i->i_ino & 0xF]; s; s=s->next)
	{
		struct dentry *dentry = s->protinfo.af_unix.dentry;

		if(dentry && dentry->d_inode == i)
		{
			unix_lock(s);
			return(s);
		}
	}
	return(NULL);
}

/*
 *	Delete a unix socket. We have to allow for deferring this on a timer.
 */

static void unix_destroy_timer(unsigned long data)
{
	unix_socket *sk=(unix_socket *)data;
	if(!unix_locked(sk) && atomic_read(&sk->wmem_alloc) == 0)
	{
		sk_free(sk);
	
		/* socket destroyed, decrement count		      */
		MOD_DEC_USE_COUNT;
		return;
	}
	
	/*
	 *	Retry;
	 */
	 
	sk->timer.expires=jiffies+sysctl_unix_destroy_delay;	/* No real hurry try it every 10 seconds or so */
	add_timer(&sk->timer);
}
	 
	 
static void unix_delayed_delete(unix_socket *sk)
{
	sk->timer.data=(unsigned long)sk;
	sk->timer.expires=jiffies+sysctl_unix_delete_delay;		/* Normally 1 second after will clean up. After that we try every 10 */
	sk->timer.function=unix_destroy_timer;
	add_timer(&sk->timer);
}

static int unix_release_sock (unix_socket *sk)
{
	unix_socket *skpair;

	sk->state_change(sk);
	sk->dead=1;
	sk->socket = NULL;

	skpair=unix_peer(sk);

	if (skpair!=NULL)
	{
		if (sk->type==SOCK_STREAM && unix_our_peer(sk, skpair))
		{
			skpair->state_change(skpair);
			skpair->shutdown=SHUTDOWN_MASK;	/* No more writes*/
		}
		unix_unlock(skpair); /* It may now die */
	}

	/* Try to flush out this socket. Throw out buffers at least */
	unix_destroy_socket(sk);

	/*
	 * Fixme: BSD difference: In BSD all sockets connected to use get
	 *	  ECONNRESET and we die on the spot. In Linux we behave
	 *	  like files and pipes do and wait for the last
	 *	  dereference.
	 *
	 * Can't we simply set sock->err?
	 *
	 *	  What the above comment does talk about? --ANK(980817)
	 */

	unix_gc();		/* Garbage collect fds */	
	return 0;
}
	
static void unix_destroy_socket(unix_socket *sk)
{
	struct sk_buff *skb;

	unix_remove_socket(sk);

	while((skb=skb_dequeue(&sk->receive_queue))!=NULL)
	{
		if(sk->state==TCP_LISTEN)
			unix_release_sock(skb->sk);
		/* passed fds are erased in the kfree_skb hook	      */
		kfree_skb(skb);
	}
	
	if(sk->protinfo.af_unix.dentry!=NULL)
	{
		dput(sk->protinfo.af_unix.dentry);
		sk->protinfo.af_unix.dentry=NULL;
	}
	
	if(!unix_locked(sk) && atomic_read(&sk->wmem_alloc) == 0)
	{
		sk_free(sk);
	
		/* socket destroyed, decrement count		      */
		MOD_DEC_USE_COUNT;
	}
	else
	{
		sk->state=TCP_CLOSE;
		sk->dead=1;
		unix_delayed_delete(sk);	/* Try every so often until buffers are all freed */
	}

}

static int unix_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;

	if (sock->state != SS_UNCONNECTED) 
		return(-EINVAL);
	if (sock->type!=SOCK_STREAM)
		return -EOPNOTSUPP;		/* Only stream sockets accept */
	if (!sk->protinfo.af_unix.addr)
		return -EINVAL;			/* No listens on an unbound socket */
	sk->max_ack_backlog=backlog;
	sk->state=TCP_LISTEN;
	sock->flags |= SO_ACCEPTCON;
	/* set credentials so connect can copy them */
	sk->peercred.pid = current->pid;
	sk->peercred.uid = current->euid;
	sk->peercred.gid = current->egid;
	return 0;
}

extern struct proto_ops unix_stream_ops;
extern struct proto_ops unix_dgram_ops;

static struct sock * unix_create1(struct socket *sock, int stream)
{
	struct sock *sk;

	MOD_INC_USE_COUNT;
	sk = sk_alloc(PF_UNIX, GFP_KERNEL, 1);
	if (!sk) {
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	sock_init_data(sock,sk);

	if (stream)
		sk->write_space = unix_stream_write_space; 

	sk->destruct = unix_destruct_addr;
	sk->protinfo.af_unix.family=PF_UNIX;
	sk->protinfo.af_unix.dentry=NULL;
	sk->protinfo.af_unix.readsem=MUTEX;	/* single task reading lock */
	sk->protinfo.af_unix.list=&unix_sockets_unbound;
	unix_insert_socket(sk);

	return sk;
}

static int unix_create(struct socket *sock, int protocol)
{
	int stream = 0;

	if (protocol && protocol != PF_UNIX)
		return -EPROTONOSUPPORT;

	sock->state = SS_UNCONNECTED;

	switch (sock->type) {
	case SOCK_STREAM:
		sock->ops = &unix_stream_ops;
		stream = 1;
		break;
		/*
		 *	Believe it or not BSD has AF_UNIX, SOCK_RAW though
		 *	nothing uses it.
		 */
	case SOCK_RAW:
		sock->type=SOCK_DGRAM;
	case SOCK_DGRAM:
		sock->ops = &unix_dgram_ops;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	return unix_create1(sock, stream) ? 0 : -ENOMEM;
}

static int unix_release(struct socket *sock, struct socket *peer)
{
	unix_socket *sk = sock->sk;

	if (!sk)
		return 0;
	
	sock->sk = NULL;
	if (sock->state != SS_UNCONNECTED)
		sock->state = SS_DISCONNECTING;

	return unix_release_sock (sk);
}

static int unix_autobind(struct socket *sock)
{
	struct sock *sk = sock->sk;
	static u32 ordernum = 1;
	struct unix_address * addr;
	unix_socket *osk;

	addr = kmalloc(sizeof(*addr) + sizeof(short) + 16, GFP_KERNEL);
	if (!addr)
		return -ENOMEM;
	if (sk->protinfo.af_unix.addr || sk->protinfo.af_unix.dentry)
	{
		kfree(addr);
		return -EINVAL;
	}
	memset(addr, 0, sizeof(*addr) + sizeof(short) + 16);
	addr->name->sun_family = AF_UNIX;
	atomic_set(&addr->refcnt, 1);

retry:
	addr->len = sprintf(addr->name->sun_path+1, "%08x", ordernum) + 1 + sizeof(short);
	addr->hash = unix_hash_fold(csum_partial((void*)addr->name, addr->len, 0));
	ordernum++;

	if ((osk=unix_find_socket_byname(addr->name, addr->len, sock->type,
					 addr->hash)) != NULL)
	{
		unix_unlock(osk);
		goto retry;
	}

	sk->protinfo.af_unix.addr = addr;
	unix_remove_socket(sk);
	sk->protinfo.af_unix.list = &unix_socket_table[(addr->hash ^ sk->type)&0xF];
	unix_insert_socket(sk);
	return 0;
}

static unix_socket *unix_find_other(struct sockaddr_un *sunname, int len,
				    int type, unsigned hash, int *error)
{
	unix_socket *u;
	
	if (sunname->sun_path[0])
	{
		struct dentry *dentry;
		dentry = open_namei(sunname->sun_path, 2, S_IFSOCK);
		if (IS_ERR(dentry)) {
			*error = PTR_ERR(dentry);
			return NULL;
		}
		u=unix_find_socket_byinode(dentry->d_inode);
		dput(dentry);
		if (u && u->type != type)
		{
			*error=-EPROTOTYPE;
			unix_unlock(u);
			return NULL;
		}
	}
	else
		u=unix_find_socket_byname(sunname, len, type, hash);

	if (u==NULL)
	{
		*error=-ECONNREFUSED;
		return NULL;
	}
	return u;
}


static int unix_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_un *sunaddr=(struct sockaddr_un *)uaddr;
	struct dentry * dentry;
	int err;
	unsigned hash;
	struct unix_address *addr;
	
	if (sk->protinfo.af_unix.addr || sk->protinfo.af_unix.dentry ||
	    sunaddr->sun_family != AF_UNIX)
		return -EINVAL;

	if (addr_len==sizeof(short))
		return unix_autobind(sock);

	addr_len = unix_mkname(sunaddr, addr_len, &hash);
	if (addr_len < 0)
		return addr_len;

	addr = kmalloc(sizeof(*addr)+addr_len, GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	/* We slept; recheck ... */

	if (sk->protinfo.af_unix.addr || sk->protinfo.af_unix.dentry)
	{
		kfree(addr);
		return -EINVAL;		/* Already bound */
	}

	memcpy(addr->name, sunaddr, addr_len);
	addr->len = addr_len;
	addr->hash = hash;
	atomic_set(&addr->refcnt, 1);

	if (!sunaddr->sun_path[0])
	{
		unix_socket *osk = unix_find_socket_byname(sunaddr, addr_len,
							   sk->type, hash);
		if (osk)
		{
			unix_unlock(osk);
			kfree(addr);
			return -EADDRINUSE;
		}
		unix_remove_socket(sk);
		sk->protinfo.af_unix.addr = addr;
		sk->protinfo.af_unix.list = &unix_socket_table[(hash^sk->type)&0xF];
		unix_insert_socket(sk);
		return 0;
	}

	addr->hash = UNIX_HASH_SIZE;
	sk->protinfo.af_unix.addr = addr;
	

	dentry = do_mknod(sunaddr->sun_path, S_IFSOCK|sock->inode->i_mode, 0);
	if (IS_ERR(dentry))
	{
		err = PTR_ERR(dentry);
		unix_release_addr(addr);
		sk->protinfo.af_unix.addr = NULL;
		if (err==-EEXIST)
			return -EADDRINUSE;
		else
			return err;
	}
	unix_remove_socket(sk);
	sk->protinfo.af_unix.list = &unix_socket_table[dentry->d_inode->i_ino & 0xF];
	sk->protinfo.af_unix.dentry = dentry;
	unix_insert_socket(sk);

	return 0;
}

static int unix_dgram_connect(struct socket *sock, struct sockaddr *addr,
			      int alen, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_un *sunaddr=(struct sockaddr_un*)addr;
	struct sock *other;
	unsigned hash;
	int err;

	/*
	 *	1003.1g breaking connected state with AF_UNSPEC
	 */

	if(addr->sa_family==AF_UNSPEC)
	{
		if(unix_peer(sk))
		{
			unix_unlock(unix_peer(sk));
			unix_peer(sk) = NULL;
			sock->state=SS_UNCONNECTED;
		}
		return 0;
	}
		
	alen = unix_mkname(sunaddr, alen, &hash);
	if (alen < 0)
		return alen;

	other=unix_find_other(sunaddr, alen, sock->type, hash, &err);
	if (!other)
		return err;
	if (!unix_may_send(sk, other))
	{
		unix_unlock(other);
		return -EINVAL;
	}

	/*
	 * If it was connected, reconnect.
	 */
	if (unix_peer(sk))
	{
		unix_unlock(unix_peer(sk));
		unix_peer(sk)=NULL;
	}
	unix_peer(sk)=other;
	if (sock->passcred && !sk->protinfo.af_unix.addr)
		unix_autobind(sock);
	return 0;
}

static int unix_stream_connect(struct socket *sock, struct sockaddr *uaddr,
			       int addr_len, int flags)
{
	struct sockaddr_un *sunaddr=(struct sockaddr_un *)uaddr;
	struct sock *sk = sock->sk, *newsk;
	unix_socket *other;
	struct sk_buff *skb;
	int err;
	unsigned hash;

	addr_len = unix_mkname(sunaddr, addr_len, &hash);
	if (addr_len < 0)
		return addr_len;

	/* First of all allocate resources.
	   If we will make it after state checks,
	   we will have to recheck all again in any case.
	 */

	/*  Find listening sock */
	other=unix_find_other(sunaddr, addr_len, sk->type, hash, &err);

	/* create new sock for complete connection */
	newsk = unix_create1(NULL, 1);

	/* Allocate skb for sending to listening sock */
	skb = NULL;
	if (newsk)
		skb = sock_wmalloc(newsk, 1, 0, GFP_KERNEL);

	switch (sock->state) 
	{
		case SS_UNCONNECTED:
			/* This is ok... continue with connect */
			break;
		case SS_CONNECTED:
			/* Socket is already connected */
			err = -EISCONN;
			goto out;
		default:
			err = -EINVAL;
			goto out;
	}

	err = -EINVAL;
	if (sk->state != TCP_CLOSE)
		goto out;

	/* Check that listener is in valid state. */
	err = -ECONNREFUSED;
	if (other == NULL || other->dead || other->state != TCP_LISTEN)
		goto out;

	err = -ENOMEM;
	if (newsk == NULL || skb == NULL)
		goto out;

	UNIXCB(skb).attr = MSG_SYN;

	/* set up connecting socket */
	sock->state=SS_CONNECTED;
	if (!sk->protinfo.af_unix.addr)
		unix_autobind(sock);
	unix_peer(sk)=newsk;
	unix_lock(sk);
	sk->state=TCP_ESTABLISHED;
	/* Set credentials */
	sk->peercred = other->peercred;

	/* set up newly created sock */
	unix_peer(newsk)=sk;
	unix_lock(newsk);
	newsk->state=TCP_ESTABLISHED;
	newsk->type=SOCK_STREAM;
	newsk->peercred.pid = current->pid;
	newsk->peercred.uid = current->euid;
	newsk->peercred.gid = current->egid;

	/* copy address information from listening to new sock*/
	if (other->protinfo.af_unix.addr)
	{
		atomic_inc(&other->protinfo.af_unix.addr->refcnt);
		newsk->protinfo.af_unix.addr=other->protinfo.af_unix.addr;
	}
	if (other->protinfo.af_unix.dentry)
		newsk->protinfo.af_unix.dentry=dget(other->protinfo.af_unix.dentry);

	/* send info to listening sock */
	other->ack_backlog++;
	skb_queue_tail(&other->receive_queue,skb);
	other->data_ready(other,0);		/* Wake up !	      */
	unix_unlock(other);
	return 0;

out:
	if (skb)
		kfree_skb(skb);
	if (newsk)
		unix_destroy_socket(newsk);
	if (other)
		unix_unlock(other);
	return err;
}

static int unix_socketpair(struct socket *socka, struct socket *sockb)
{
	struct sock *ska=socka->sk, *skb = sockb->sk;

	/* Join our sockets back to back */
	unix_lock(ska);
	unix_lock(skb);
	unix_peer(ska)=skb;
	unix_peer(skb)=ska;

	if (ska->type != SOCK_DGRAM)
	{
		ska->state=TCP_ESTABLISHED;
		skb->state=TCP_ESTABLISHED;
		socka->state=SS_CONNECTED;
		sockb->state=SS_CONNECTED;
	}
	return 0;
}

static int unix_accept(struct socket *sock, struct socket *newsock, int flags)
{
	unix_socket *sk = sock->sk;
	unix_socket *newsk = newsock->sk;
	unix_socket *tsk;
	struct sk_buff *skb;
	
	if (sock->state != SS_UNCONNECTED)
		return(-EINVAL); 
	if (!(sock->flags & SO_ACCEPTCON)) 
		return(-EINVAL);

	if (sock->type!=SOCK_STREAM)
		return -EOPNOTSUPP;
	if (sk->state!=TCP_LISTEN)
		return -EINVAL;
		
	for (;;)
	{
		skb=skb_dequeue(&sk->receive_queue);
		if(skb==NULL)
		{
			if(flags&O_NONBLOCK)
				return -EAGAIN;
			interruptible_sleep_on(sk->sleep);
			if(signal_pending(current))
				return -ERESTARTSYS;
			continue;
		}
		if (!(UNIXCB(skb).attr & MSG_SYN))
		{
			tsk=skb->sk;
			tsk->state_change(tsk);
			kfree_skb(skb);
			continue;
		}
		tsk = skb->sk;
		sk->ack_backlog--;
		kfree_skb(skb);
		if (!tsk->dead) 
			break;
		unix_release_sock(tsk);
	}


	/* attach accepted sock to socket */
	newsock->state=SS_CONNECTED;
	newsock->sk=tsk;
	tsk->sleep=newsk->sleep;
	tsk->socket=newsock;

	/* destroy handed sock */
	newsk->socket = NULL;
	unix_destroy_socket(newsk);

	return 0;
}


static int unix_getname(struct socket *sock, struct sockaddr *uaddr, int *uaddr_len, int peer)
{
	struct sock *sk = sock->sk;
	struct sockaddr_un *sunaddr=(struct sockaddr_un *)uaddr;
	
	if (peer)
	{
		if (!unix_peer(sk))
			return -ENOTCONN;
		sk=unix_peer(sk);
	}
	if (!sk->protinfo.af_unix.addr)
	{
		sunaddr->sun_family = AF_UNIX;
		sunaddr->sun_path[0] = 0;
		*uaddr_len = sizeof(short);
		return 0;		/* Not bound */
	}
	*uaddr_len = sk->protinfo.af_unix.addr->len;
	memcpy(sunaddr, sk->protinfo.af_unix.addr->name, *uaddr_len);
	return 0;
}

static void unix_detach_fds(struct scm_cookie *scm, struct sk_buff *skb)
{
	int i;

	scm->fp = UNIXCB(skb).fp;
	skb->destructor = sock_wfree;
	UNIXCB(skb).fp = NULL;

	for (i=scm->fp->count-1; i>=0; i--)
		unix_notinflight(scm->fp->fp[i]);
}

static void unix_destruct_fds(struct sk_buff *skb)
{
	struct scm_cookie scm;
	memset(&scm, 0, sizeof(scm));
	unix_detach_fds(&scm, skb);
	scm_destroy(&scm);
	sock_wfree(skb);
}

static void unix_attach_fds(struct scm_cookie *scm, struct sk_buff *skb)
{
	int i;
	for (i=scm->fp->count-1; i>=0; i--)
		unix_inflight(scm->fp->fp[i]);
	UNIXCB(skb).fp = scm->fp;
	skb->destructor = unix_destruct_fds;
	scm->fp = NULL;
}


/*
 *	Send AF_UNIX data.
 */

static int unix_dgram_sendmsg(struct socket *sock, struct msghdr *msg, int len,
			      struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_un *sunaddr=msg->msg_name;
	unix_socket *other;
	int namelen = 0; /* fake GCC */
	int err;
	unsigned hash;
	struct sk_buff *skb;

	if (msg->msg_flags&MSG_OOB)
		return -EOPNOTSUPP;

	if (msg->msg_flags&~(MSG_DONTWAIT|MSG_NOSIGNAL))
		return -EINVAL;

	if (msg->msg_namelen) {
		namelen = unix_mkname(sunaddr, msg->msg_namelen, &hash);
		if (namelen < 0)
			return namelen;
	} else {
		sunaddr = NULL;
		if (!unix_peer(sk))
			return -ENOTCONN;
	}

	if (sock->passcred && !sk->protinfo.af_unix.addr)
		unix_autobind(sock);

	skb = sock_alloc_send_skb(sk, len, 0, msg->msg_flags&MSG_DONTWAIT, &err);
	if (skb==NULL)
		goto out;

	memcpy(UNIXCREDS(skb), &scm->creds, sizeof(struct ucred));
	UNIXCB(skb).attr = msg->msg_flags;
	if (scm->fp)
		unix_attach_fds(scm, skb);

	skb->h.raw = skb->data;
	err = memcpy_fromiovec(skb_put(skb,len), msg->msg_iov, len);
	if (err)
		goto out_free;

	other = unix_peer(sk);
	if (other && other->dead)
	{
		/*
		 *	Check with 1003.1g - what should
		 *	datagram error
		 */
		unix_unlock(other);
		unix_peer(sk)=NULL;
		other = NULL;
		err = -ECONNRESET;
		if (sunaddr == NULL)
			goto out_free;
	}
	if (!other)
	{
		other = unix_find_other(sunaddr, namelen, sk->type, hash, &err);
		if (other==NULL)
			goto out_free;
		err = -EINVAL;
		if (!unix_may_send(sk, other))
			goto out_unlock;
	}

	skb_queue_tail(&other->receive_queue, skb);
	other->data_ready(other,len);
	
	if (!unix_peer(sk))
		unix_unlock(other);
	return len;

out_unlock:
	unix_unlock(other);
out_free:
	kfree_skb(skb);
out:
	return err;
}

		
static int unix_stream_sendmsg(struct socket *sock, struct msghdr *msg, int len,
			       struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	unix_socket *other;
	struct sockaddr_un *sunaddr=msg->msg_name;
	int err,size;
	struct sk_buff *skb;
	int limit=0;
	int sent=0;

	if (sock->flags & SO_ACCEPTCON) 
		return(-EINVAL);

	if (msg->msg_flags&MSG_OOB)
		return -EOPNOTSUPP;

	if (msg->msg_flags&~(MSG_DONTWAIT|MSG_NOSIGNAL))
		return -EINVAL;

	if (msg->msg_namelen) {
		if (sk->state==TCP_ESTABLISHED)
			return -EISCONN;
		else
			return -EOPNOTSUPP;
	} else {
		sunaddr = NULL;
		if (!unix_peer(sk))
			return -ENOTCONN;
	}

	if (sk->shutdown&SEND_SHUTDOWN) {
		if (!(msg->msg_flags&MSG_NOSIGNAL))
			send_sig(SIGPIPE,current,0);
		return -EPIPE;
	}

	while(sent < len)
	{
		/*
		 *	Optimisation for the fact that under 0.01% of X messages typically
		 *	need breaking up.
		 */
		 
		size=len-sent;

		/* Keep two messages in the pipe so it schedules better */
		if (size > sk->sndbuf/2 - 16)
			size = sk->sndbuf/2 - 16;

		/*
		 *	Keep to page sized kmalloc()'s as various people
		 *	have suggested. Big mallocs stress the vm too
		 *	much.
		 */

		if (size > 4096-16)
			limit = 4096-16; /* Fall back to a page if we can't grab a big buffer this instant */
		else
			limit = 0;	/* Otherwise just grab and wait */

		/*
		 *	Grab a buffer
		 */
		 
		skb=sock_alloc_send_skb(sk,size,limit,msg->msg_flags&MSG_DONTWAIT, &err);
		
		if (skb==NULL)
		{
			if (sent)
				goto out;
			return err;
		}

		/*
		 *	If you pass two values to the sock_alloc_send_skb
		 *	it tries to grab the large buffer with GFP_BUFFER
		 *	(which can fail easily), and if it fails grab the
		 *	fallback size buffer which is under a page and will
		 *	succeed. [Alan]
		 */
		size = min(size, skb_tailroom(skb));

		memcpy(UNIXCREDS(skb), &scm->creds, sizeof(struct ucred));
		UNIXCB(skb).attr = msg->msg_flags;
		if (scm->fp)
			unix_attach_fds(scm, skb);

		if (memcpy_fromiovec(skb_put(skb,size), msg->msg_iov, size)) {
			kfree_skb(skb);
			if (sent)
				goto out;
			return -EFAULT;
		}

		other=unix_peer(sk);

		if (other->dead || (sk->shutdown & SEND_SHUTDOWN))
		{
			kfree_skb(skb);
			if(sent)
				goto out;
			if (!(msg->msg_flags&MSG_NOSIGNAL))
				send_sig(SIGPIPE,current,0);
			return -EPIPE;
		}

		skb_queue_tail(&other->receive_queue, skb);
		other->data_ready(other,size);
		sent+=size;
	}
out:
	return sent;
}

/*
 *	Sleep until data has arrive. But check for races..
 */
 
static void unix_data_wait(unix_socket * sk)
{
	if (!skb_peek(&sk->receive_queue))
	{
		sk->socket->flags |= SO_WAITDATA;
		interruptible_sleep_on(sk->sleep);
		sk->socket->flags &= ~SO_WAITDATA;
	}
}

static int unix_dgram_recvmsg(struct socket *sock, struct msghdr *msg, int size,
			      int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	int noblock = flags & MSG_DONTWAIT;
	struct sk_buff *skb;
	int err;

	if (flags&MSG_OOB)
		return -EOPNOTSUPP;

	msg->msg_namelen = 0;

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

	if (msg->msg_name)
	{
		msg->msg_namelen = sizeof(short);
		if (skb->sk->protinfo.af_unix.addr)
		{
			msg->msg_namelen=skb->sk->protinfo.af_unix.addr->len;
			memcpy(msg->msg_name,
				skb->sk->protinfo.af_unix.addr->name,
				skb->sk->protinfo.af_unix.addr->len);
		}
	}

	if (size > skb->len)
		size = skb->len;
	else if (size < skb->len)
		msg->msg_flags |= MSG_TRUNC;

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, size);
	if (err)
		goto out_free;

	scm->creds = *UNIXCREDS(skb);

	if (!(flags & MSG_PEEK))
	{
		if (UNIXCB(skb).fp)
			unix_detach_fds(scm, skb);
	}
	else 
	{
		/* It is questionable: on PEEK we could:
		   - do not return fds - good, but too simple 8)
		   - return fds, and do not return them on read (old strategy,
		     apparently wrong)
		   - clone fds (I choosed it for now, it is the most universal
		     solution)
		
	           POSIX 1003.1g does not actually define this clearly
	           at all. POSIX 1003.1g doesn't define a lot of things
	           clearly however!		     
		   
		*/
		if (UNIXCB(skb).fp)
			scm->fp = scm_fp_dup(UNIXCB(skb).fp);
	}
	err = size;

out_free:
	skb_free_datagram(sk,skb);
out:
	return err;
}


static int unix_stream_recvmsg(struct socket *sock, struct msghdr *msg, int size,
			       int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	int noblock = flags & MSG_DONTWAIT;
	struct sockaddr_un *sunaddr=msg->msg_name;
	int copied = 0;
	int check_creds = 0;
	int target = 1;

	if (sock->flags & SO_ACCEPTCON) 
		return(-EINVAL);

	if (flags&MSG_OOB)
		return -EOPNOTSUPP;
	if (flags&MSG_WAITALL)
		target = size;
		
		
	msg->msg_namelen = 0;

	/* Lock the socket to prevent queue disordering
	 * while sleeps in memcpy_tomsg
	 */

	down(&sk->protinfo.af_unix.readsem);

	do
	{
		int chunk;
		struct sk_buff *skb;

		skb=skb_dequeue(&sk->receive_queue);
		if (skb==NULL)
		{
			if (copied >= target)
				break;

			/*
			 *	POSIX 1003.1g mandates this order.
			 */
			 
			if (sk->err) 
			{
				up(&sk->protinfo.af_unix.readsem);
				return sock_error(sk);
			}

			if (sk->shutdown & RCV_SHUTDOWN)
				break;
			up(&sk->protinfo.af_unix.readsem);
			if (noblock)
				return -EAGAIN;
			unix_data_wait(sk);
			if (signal_pending(current))
				return -ERESTARTSYS;
			down(&sk->protinfo.af_unix.readsem);
			continue;
		}

		/* Never glue messages from different writers */
		if (check_creds &&
		    memcmp(UNIXCREDS(skb), &scm->creds, sizeof(scm->creds)) != 0)
		{
			skb_queue_head(&sk->receive_queue, skb);
			break;
		}

		/* Copy address just once */
		if (sunaddr)
		{
			msg->msg_namelen = sizeof(short);
			if (skb->sk->protinfo.af_unix.addr)
			{
				msg->msg_namelen=skb->sk->protinfo.af_unix.addr->len;
				memcpy(sunaddr,
					skb->sk->protinfo.af_unix.addr->name,
					skb->sk->protinfo.af_unix.addr->len);
			}
			sunaddr = NULL;
		}

		chunk = min(skb->len, size);
		if (memcpy_toiovec(msg->msg_iov, skb->data, chunk)) {
			skb_queue_head(&sk->receive_queue, skb);
			if (copied == 0)
				copied = -EFAULT;
			break;
		}
		copied += chunk;
		size -= chunk;

		/* Copy credentials */
		scm->creds = *UNIXCREDS(skb);
		check_creds = 1;

		/* Mark read part of skb as used */
		if (!(flags & MSG_PEEK))
		{
			skb_pull(skb, chunk);

			if (UNIXCB(skb).fp)
				unix_detach_fds(scm, skb);

			/* put the skb back if we didn't use it up.. */
			if (skb->len)
			{
				skb_queue_head(&sk->receive_queue, skb);
				break;
			}

			kfree_skb(skb);

			if (scm->fp)
				break;
		}
		else
		{
			/* It is questionable, see note in unix_dgram_recvmsg.
			   
			 */
			if (UNIXCB(skb).fp)
				scm->fp = scm_fp_dup(UNIXCB(skb).fp);

			/* put message back and return */
			skb_queue_head(&sk->receive_queue, skb);
			break;
		}
	} while (size);

	up(&sk->protinfo.af_unix.readsem);
	return copied;
}

static int unix_shutdown(struct socket *sock, int mode)
{
	struct sock *sk = sock->sk;
	unix_socket *other=unix_peer(sk);
	
	mode = (mode+1)&(RCV_SHUTDOWN|SEND_SHUTDOWN);

	if (mode) {
		sk->shutdown |= mode;
		sk->state_change(sk);
		if (other && sk->type == SOCK_STREAM &&
		    unix_our_peer(sk, other)) {
			int peer_mode = 0;

			if (mode&RCV_SHUTDOWN)
				peer_mode |= SEND_SHUTDOWN;
			if (mode&SEND_SHUTDOWN)
				peer_mode |= RCV_SHUTDOWN;
			other->shutdown |= peer_mode;
			other->state_change(other);
		}
	}
	return 0;
}

		
static int unix_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	long amount=0;
			
	switch(cmd)
	{
	
		case TIOCOUTQ:
			amount = sk->sndbuf - atomic_read(&sk->wmem_alloc);
			if(amount<0)
				amount=0;
			return put_user(amount, (int *)arg);
		case TIOCINQ:
		{
			struct sk_buff *skb;
			if(sk->state==TCP_LISTEN)
				return -EINVAL;
			/*
			 *	These two are safe on current systems as
			 *	only user tasks fiddle here
			 */
			if((skb=skb_peek(&sk->receive_queue))!=NULL)
				amount=skb->len;
			return put_user(amount, (int *)arg);
		}

		default:
			return -EINVAL;
	}
	/*NOTREACHED*/
	return(0);
}

static unsigned int unix_poll(struct file * file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask;

	poll_wait(file, sk->sleep, wait);
	mask = 0;

	/* exceptional events? */
	if (sk->err)
		mask |= POLLERR;
	if (sk->shutdown & RCV_SHUTDOWN)
		mask |= POLLHUP;

	/* readable? */
	if (!skb_queue_empty(&sk->receive_queue))
		mask |= POLLIN | POLLRDNORM;

	/* Connection-based need to check for termination and startup */
	if (sk->type == SOCK_STREAM && sk->state==TCP_CLOSE)
		mask |= POLLHUP;

	/*
	 * we set writable also when the other side has shut down the
	 * connection. This prevents stuck sockets.
	 */
	if (sk->sndbuf - (int)atomic_read(&sk->wmem_alloc) >= MIN_WRITE_SPACE)
			mask |= POLLOUT | POLLWRNORM | POLLWRBAND;

	return mask;
}

static void unix_stream_write_space(struct sock *sk)
{
	if (sk->dead)  
		return;
	wake_up_interruptible(sk->sleep);
	if (sk->sndbuf - (int)atomic_read(&sk->wmem_alloc) >= MIN_WRITE_SPACE)
		sock_wake_async(sk->socket, 2);
}

#ifdef CONFIG_PROC_FS
static int unix_read_proc(char *buffer, char **start, off_t offset,
			  int length, int *eof, void *data)
{
	off_t pos=0;
	off_t begin=0;
	int len=0;
	int i;
	unix_socket *s;
	
	len+= sprintf(buffer,"Num       RefCount Protocol Flags    Type St "
	    "Inode Path\n");
	
	forall_unix_sockets (i,s)
	{
		len+=sprintf(buffer+len,"%p: %08X %08X %08lX %04X %02X %5ld",
			s,
			atomic_read(&s->sock_readers),
			0,
			s->socket ? s->socket->flags : 0,
			s->type,
			s->socket ? s->socket->state :
			     (s->state == TCP_ESTABLISHED ?
			      SS_CONNECTING : SS_DISCONNECTING),
			s->socket ? s->socket->inode->i_ino : 0);

		if (s->protinfo.af_unix.addr)
		{
			buffer[len++] = ' ';
			memcpy(buffer+len, s->protinfo.af_unix.addr->name->sun_path,
			       s->protinfo.af_unix.addr->len-sizeof(short));
			if (!UNIX_ABSTRACT(s))
				len--;
			else
				buffer[len] = '@';
			len += s->protinfo.af_unix.addr->len - sizeof(short);
		}
		buffer[len++]='\n';
		
		pos = begin + len;
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			goto done;
	}
	*eof = 1;
done:
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	if (len < 0)
		len = 0;
	return len;
}
#endif

struct proto_ops unix_stream_ops = {
	PF_UNIX,
	
	sock_no_dup,
	unix_release,
	unix_bind,
	unix_stream_connect,
	unix_socketpair,
	unix_accept,
	unix_getname,
	unix_poll,
	unix_ioctl,
	unix_listen,
	unix_shutdown,
	sock_no_setsockopt,
	sock_no_getsockopt,
	sock_no_fcntl,
	unix_stream_sendmsg,
	unix_stream_recvmsg
};

struct proto_ops unix_dgram_ops = {
	PF_UNIX,
	
	sock_no_dup,
	unix_release,
	unix_bind,
	unix_dgram_connect,
	unix_socketpair,
	sock_no_accept,
	unix_getname,
	datagram_poll,
	unix_ioctl,
	sock_no_listen,
	unix_shutdown,
	sock_no_setsockopt,
	sock_no_getsockopt,
	sock_no_fcntl,
	unix_dgram_sendmsg,
	unix_dgram_recvmsg
};

struct net_proto_family unix_family_ops = {
	PF_UNIX,
	unix_create
};

#ifdef MODULE
#ifdef CONFIG_SYSCTL
extern void unix_sysctl_register(void);
extern void unix_sysctl_unregister(void);
#endif

int init_module(void)
#else
__initfunc(void unix_proto_init(struct net_proto *pro))
#endif
{
	struct sk_buff *dummy_skb;
	struct proc_dir_entry *ent;
	
	printk(KERN_INFO "NET4: Unix domain sockets 1.0 for Linux NET4.0.\n");
	if (sizeof(struct unix_skb_parms) > sizeof(dummy_skb->cb))
	{
		printk(KERN_CRIT "unix_proto_init: panic\n");
#ifdef MODULE
		return -1;
#else
		return;
#endif
	}
	sock_register(&unix_family_ops);
#ifdef CONFIG_PROC_FS
	ent = create_proc_entry("net/unix", 0, 0);
	ent->read_proc = unix_read_proc;
#endif

#ifdef MODULE
#ifdef CONFIG_SYSCTL
	unix_sysctl_register();
#endif

	return 0;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
	sock_unregister(PF_UNIX);
#ifdef CONFIG_SYSCTL
	unix_sysctl_unregister();
#endif
#ifdef CONFIG_PROC_FS
	remove_proc_entry("net/unix", 0);
#endif
}
#endif

/*
 * Local variables:
 *  compile-command: "gcc -g -D__KERNEL__ -Wall -O6 -I/usr/src/linux/include -c af_unix.c"
 * End:
 */
