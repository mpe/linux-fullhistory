/*
 * NET		An implementation of the SOCKET network access protocol.
 *
 * Version:	@(#)socket.c	1.1.93	18/02/95
 *
 * Authors:	Orest Zborowski, <obz@Kodak.COM>
 *		Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Anonymous	:	NOTSOCK/BADF cleanup. Error fix in
 *					shutdown()
 *		Alan Cox	:	verify_area() fixes
 *		Alan Cox	: 	Removed DDI
 *		Jonathan Kamens	:	SOCK_DGRAM reconnect bug
 *		Alan Cox	:	Moved a load of checks to the very
 *					top level.
 *		Alan Cox	:	Move address structures to/from user
 *					mode above the protocol layers.
 *		Rob Janssen	:	Allow 0 length sends.
 *		Alan Cox	:	Asynchronous I/O support (cribbed from the
 *					tty drivers).
 *		Niibe Yutaka	:	Asynchronous I/O for writes (4.4BSD style)
 *		Jeff Uphoff	:	Made max number of sockets command-line
 *					configurable.
 *		Matti Aarnio	:	Made the number of sockets dynamic,
 *					to be allocated when needed, and mr.
 *					Uphoff's max is used as max to be
 *					allowed to allocate.
 *		Linus		:	Argh. removed all the socket allocation
 *					altogether: it's in the inode now.
 *		Alan Cox	:	Made sock_alloc()/sock_release() public
 *					for NetROM and future kernel nfsd type
 *					stuff.
 *		Alan Cox	:	sendmsg/recvmsg basics.
 *		Tom Dyas	:	Export net symbols.
 *		Marcin Dalecki	:	Fixed problems with CONFIG_NET="n".
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *
 *	This module is effectively the top level interface to the BSD socket
 *	paradigm. Because it is very simple it works well for Unix domain sockets,
 *	but requires a whole layer of substructure for the other protocols.
 *
 *	In addition it lacks an effective kernel -> kernel interface to go with
 *	the user one.
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/firewall.h>

#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

#include <net/netlink.h>

#include <asm/system.h>
#include <asm/segment.h>

#if defined(CONFIG_MODULES) && defined(CONFIG_NET)
extern void export_net_symbols(void);
#endif

static int sock_lseek(struct inode *inode, struct file *file, off_t offset,
		      int whence);
static int sock_read(struct inode *inode, struct file *file, char *buf,
		     int size);
static int sock_write(struct inode *inode, struct file *file, const char *buf,
		      int size);

static void sock_close(struct inode *inode, struct file *file);
static int sock_select(struct inode *inode, struct file *file, int which, select_table *seltable);
static int sock_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg);
static int sock_fasync(struct inode *inode, struct file *filp, int on);


/*
 *	Socket files have a set of 'special' operations as well as the generic file ones. These don't appear
 *	in the operation structures but are done directly via the socketcall() multiplexor.
 */

static struct file_operations socket_file_ops = {
	sock_lseek,
	sock_read,
	sock_write,
	NULL,			/* readdir */
	sock_select,
	sock_ioctl,
	NULL,			/* mmap */
	NULL,			/* no special open code... */
	sock_close,
	NULL,			/* no fsync */
	sock_fasync
};

/*
 *	The protocol list. Each protocol is registered in here.
 */
static struct proto_ops *pops[NPROTO];
/*
 *	Statistics counters of the socket lists
 */
static int sockets_in_use  = 0;

/*
 *	Support routines. Move socket addresses back and forth across the kernel/user
 *	divide and look after the messy bits.
 */

#define MAX_SOCK_ADDR	128		/* 108 for Unix domain - 16 for IP, 16 for IPX, about 80 for AX.25 */
 
int move_addr_to_kernel(void *uaddr, int ulen, void *kaddr)
{
	int err;
	if(ulen<0||ulen>MAX_SOCK_ADDR)
		return -EINVAL;
	if(ulen==0)
		return 0;
	if((err=verify_area(VERIFY_READ,uaddr,ulen))<0)
		return err;
	memcpy_fromfs(kaddr,uaddr,ulen);
	return 0;
}

int move_addr_to_user(void *kaddr, int klen, void *uaddr, int *ulen)
{
	int err;
	int len;

		
	if((err=verify_area(VERIFY_WRITE,ulen,sizeof(*ulen)))<0)
		return err;
	len=get_user(ulen);
	if(len>klen)
		len=klen;
	if(len<0 || len> MAX_SOCK_ADDR)
		return -EINVAL;
	if(len)
	{
		if((err=verify_area(VERIFY_WRITE,uaddr,len))<0)
			return err;
		memcpy_tofs(uaddr,kaddr,len);
	}
 	put_user(len,ulen);
 	return 0;
}

/*
 *	Obtains the first available file descriptor and sets it up for use. 
 */

static int get_fd(struct inode *inode)
{
	int fd;
	struct file *file;

	/*
	 *	Find a file descriptor suitable for return to the user. 
	 */

	file = get_empty_filp();
	if (!file) 
		return(-1);

	for (fd = 0; fd < NR_OPEN; ++fd)
		if (!current->files->fd[fd]) 
			break;
	if (fd == NR_OPEN) 
	{
		file->f_count = 0;
		return(-1);
	}

	FD_CLR(fd, &current->files->close_on_exec);
		current->files->fd[fd] = file;
	file->f_op = &socket_file_ops;
	file->f_mode = 3;
	file->f_flags = O_RDWR;
	file->f_count = 1;
	file->f_inode = inode;
	if (inode) 
		inode->i_count++;
	file->f_pos = 0;
	return(fd);
}


/*
 *	Go from an inode to its socket slot.
 *
 * The original socket implementation wasn't very clever, which is
 * why this exists at all..
 */

__inline struct socket *socki_lookup(struct inode *inode)
{
	return &inode->u.socket_i;
}

/*
 *	Go from a file number to its socket slot.
 */

extern __inline struct socket *sockfd_lookup(int fd, struct file **pfile)
{
	struct file *file;
	struct inode *inode;

	if (fd < 0 || fd >= NR_OPEN || !(file = current->files->fd[fd])) 
		return NULL;

	inode = file->f_inode;
	if (!inode || !inode->i_sock)
		return NULL;

	if (pfile) 
		*pfile = file;

	return socki_lookup(inode);
}

/*
 *	Allocate a socket.
 */

struct socket *sock_alloc(void)
{
	struct inode * inode;
	struct socket * sock;

	inode = get_empty_inode();
	if (!inode)
		return NULL;

	inode->i_mode = S_IFSOCK;
	inode->i_sock = 1;
	inode->i_uid = current->uid;
	inode->i_gid = current->gid;

	sock = &inode->u.socket_i;
	sock->state = SS_UNCONNECTED;
	sock->flags = 0;
	sock->ops = NULL;
	sock->data = NULL;
	sock->conn = NULL;
	sock->iconn = NULL;
	sock->next = NULL;
	sock->file = NULL;
	sock->wait = &inode->i_wait;
	sock->inode = inode;		/* "backlink": we could use pointer arithmetic instead */
	sock->fasync_list = NULL;
	sockets_in_use++;
	return sock;
}

/*
 *	Release a socket.
 */

static inline void sock_release_peer(struct socket *peer)
{
	peer->state = SS_DISCONNECTING;
	wake_up_interruptible(peer->wait);
	sock_wake_async(peer, 1);
}

void sock_release(struct socket *sock)
{
	int oldstate;
	struct socket *peersock, *nextsock;

	if ((oldstate = sock->state) != SS_UNCONNECTED)
		sock->state = SS_DISCONNECTING;

	/*
	 *	Wake up anyone waiting for connections. 
	 */

	for (peersock = sock->iconn; peersock; peersock = nextsock) 
	{
		nextsock = peersock->next;
		sock_release_peer(peersock);
	}

	/*
	 * Wake up anyone we're connected to. First, we release the
	 * protocol, to give it a chance to flush data, etc.
	 */

	peersock = (oldstate == SS_CONNECTED) ? sock->conn : NULL;
	if (sock->ops) 
		sock->ops->release(sock, peersock);
	if (peersock)
		sock_release_peer(peersock);
	--sockets_in_use;	/* Bookkeeping.. */
	sock->file=NULL;
	iput(SOCK_INODE(sock));
}

/*
 *	Sockets are not seekable.
 */

static int sock_lseek(struct inode *inode, struct file *file, off_t offset, int whence)
{
	return(-ESPIPE);
}

/*
 *	Read data from a socket. ubuf is a user mode pointer. We make sure the user
 *	area ubuf...ubuf+size-1 is writable before asking the protocol.
 */

static int sock_read(struct inode *inode, struct file *file, char *ubuf, int size)
{
	struct socket *sock;
	int err;
	struct iovec iov;
	struct msghdr msg;
  
	sock = socki_lookup(inode); 
	if (sock->flags & SO_ACCEPTCON) 
		return(-EINVAL);

	if(size<0)
		return -EINVAL;
	if(size==0)		/* Match SYS5 behaviour */
		return 0;
	if ((err=verify_area(VERIFY_WRITE,ubuf,size))<0)
	  	return err;
	msg.msg_name=NULL;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	iov.iov_base=ubuf;
	iov.iov_len=size;

	return(sock->ops->recvmsg(sock, &msg, size,(file->f_flags & O_NONBLOCK), 0,&msg.msg_namelen));
}

/*
 *	Write data to a socket. We verify that the user area ubuf..ubuf+size-1 is
 *	readable by the user process.
 */

static int sock_write(struct inode *inode, struct file *file, const char *ubuf, int size)
{
	struct socket *sock;
	int err;
	struct msghdr msg;
	struct iovec iov;
	
	sock = socki_lookup(inode); 

	if (sock->flags & SO_ACCEPTCON) 
		return(-EINVAL);
	
	if(size<0)
		return -EINVAL;
	if(size==0)		/* Match SYS5 behaviour */
		return 0;
	
	if ((err=verify_area(VERIFY_READ,ubuf,size))<0)
	  	return err;
	
	msg.msg_name=NULL;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	iov.iov_base=(void *)ubuf;
	iov.iov_len=size;
	
	return(sock->ops->sendmsg(sock, &msg, size,(file->f_flags & O_NONBLOCK),0));
}

/*
 *	With an ioctl arg may well be a user mode pointer, but we don't know what to do
 *	with it - that's up to the protocol still.
 */

int sock_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	   unsigned long arg)
{
	struct socket *sock;
	sock = socki_lookup(inode); 
  	return(sock->ops->ioctl(sock, cmd, arg));
}


static int sock_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
	struct socket *sock;

	sock = socki_lookup(inode);

	/*
	 *	We can't return errors to select, so it's either yes or no. 
	 */

	if (sock->ops->select)
		return(sock->ops->select(sock, sel_type, wait));
	return(0);
}


void sock_close(struct inode *inode, struct file *filp)
{
	/*
	 *	It's possible the inode is NULL if we're closing an unfinished socket. 
	 */

	if (!inode) 
		return;
	sock_fasync(inode, filp, 0);
	sock_release(socki_lookup(inode));
}

/*
 *	Update the socket async list
 */
 
static int sock_fasync(struct inode *inode, struct file *filp, int on)
{
	struct fasync_struct *fa, *fna=NULL, **prev;
	struct socket *sock;
	unsigned long flags;
	
	if (on)
	{
		fna=(struct fasync_struct *)kmalloc(sizeof(struct fasync_struct), GFP_KERNEL);
		if(fna==NULL)
			return -ENOMEM;
	}

	sock = socki_lookup(inode);
	
	prev=&(sock->fasync_list);
	
	save_flags(flags);
	cli();
	
	for(fa=*prev; fa!=NULL; prev=&fa->fa_next,fa=*prev)
		if(fa->fa_file==filp)
			break;
	
	if(on)
	{
		if(fa!=NULL)
		{
			kfree_s(fna,sizeof(struct fasync_struct));
			restore_flags(flags);
			return 0;
		}
		fna->fa_file=filp;
		fna->magic=FASYNC_MAGIC;
		fna->fa_next=sock->fasync_list;
		sock->fasync_list=fna;
	}
	else
	{
		if(fa!=NULL)
		{
			*prev=fa->fa_next;
			kfree_s(fa,sizeof(struct fasync_struct));
		}
	}
	restore_flags(flags);
	return 0;
}

int sock_wake_async(struct socket *sock, int how)
{
	if (!sock || !sock->fasync_list)
		return -1;
	switch (how)
	{
		case 0:
			kill_fasync(sock->fasync_list, SIGIO);
			break;
		case 1:
			if (!(sock->flags & SO_WAITDATA))
				kill_fasync(sock->fasync_list, SIGIO);
			break;
		case 2:
			if (sock->flags & SO_NOSPACE)
			{
				kill_fasync(sock->fasync_list, SIGIO);
				sock->flags &= ~SO_NOSPACE;
			}
			break;
	}
	return 0;
}


/*
 *	Perform the socket system call. we locate the appropriate
 *	family, then create a fresh socket.
 */

static int find_protocol_family(int family)
{
	register int i;
	for (i = 0; i < NPROTO; i++)
	{
		if (pops[i] == NULL)
			continue;
		if (pops[i]->family == family)
			return i;
	}
	return -1;
}

asmlinkage int sys_socket(int family, int type, int protocol)
{
	int i, fd;
	struct socket *sock;
	struct proto_ops *ops;

	/* Locate the correct protocol family. */
	i = find_protocol_family(family);

#ifdef CONFIG_KERNELD
	/* Attempt to load a protocol module if the find failed. */
	if (i < 0)
	{
		char module_name[30];
		sprintf(module_name,"net-pf-%d",family);
		request_module(module_name);
		i = find_protocol_family(family);
	}
#endif

	if (i < 0)
	{
  		return -EINVAL;
	}

	ops = pops[i];

/*
 *	Check that this is a type that we know how to manipulate and
 *	the protocol makes sense here. The family can still reject the
 *	protocol later.
 */
  
	if ((type != SOCK_STREAM && type != SOCK_DGRAM &&
		type != SOCK_SEQPACKET && type != SOCK_RAW &&
		type != SOCK_PACKET) || protocol < 0)
			return(-EINVAL);

/*
 *	Allocate the socket and allow the family to set things up. if
 *	the protocol is 0, the family is instructed to select an appropriate
 *	default.
 */

	if (!(sock = sock_alloc())) 
	{
		printk(KERN_WARNING "socket: no more sockets\n");
		return(-ENOSR);	/* Was: EAGAIN, but we are out of
				   system resources! */
	}

	sock->type = type;
	sock->ops = ops;
	if ((i = sock->ops->create(sock, protocol)) < 0) 
	{
		sock_release(sock);
		return(i);
	}

	if ((fd = get_fd(SOCK_INODE(sock))) < 0) 
	{
		sock_release(sock);
		return(-EINVAL);
	}

	sock->file=current->files->fd[fd];

	return(fd);
}

/*
 *	Create a pair of connected sockets.
 */

asmlinkage int sys_socketpair(int family, int type, int protocol, int usockvec[2])
{
	int fd1, fd2, i;
	struct socket *sock1, *sock2;
	int er;

	/*
	 * Obtain the first socket and check if the underlying protocol
	 * supports the socketpair call.
	 */

	if ((fd1 = sys_socket(family, type, protocol)) < 0) 
		return(fd1);
	sock1 = sockfd_lookup(fd1, NULL);
	if (!sock1->ops->socketpair) 
	{
		sys_close(fd1);
		return(-EINVAL);
	}

	/*
	 *	Now grab another socket and try to connect the two together. 
	 */

	if ((fd2 = sys_socket(family, type, protocol)) < 0) 
	{
		sys_close(fd1);
		return(-EINVAL);
	}

	sock2 = sockfd_lookup(fd2, NULL);
	if ((i = sock1->ops->socketpair(sock1, sock2)) < 0) 
	{
		sys_close(fd1);
		sys_close(fd2);
		return(i);
	}

	sock1->conn = sock2;
	sock2->conn = sock1;
	sock1->state = SS_CONNECTED;
	sock2->state = SS_CONNECTED;

	er=verify_area(VERIFY_WRITE, usockvec, sizeof(usockvec));
	if(er)
	{
		sys_close(fd1);
		sys_close(fd2);
	 	return er;
	}
	put_user(fd1, &usockvec[0]);
	put_user(fd2, &usockvec[1]);

	return(0);
}


/*
 *	Bind a name to a socket. Nothing much to do here since it's
 *	the protocol's responsibility to handle the local address.
 *
 *	We move the socket address to kernel space before we call
 *	the protocol layer (having also checked the address is ok).
 */
 
asmlinkage int sys_bind(int fd, struct sockaddr *umyaddr, int addrlen)
{
	struct socket *sock;
	int i;
	char address[MAX_SOCK_ADDR];
	int err;

	if (fd < 0 || fd >= NR_OPEN || current->files->fd[fd] == NULL)
		return(-EBADF);
	
	if (!(sock = sockfd_lookup(fd, NULL))) 
		return(-ENOTSOCK);
  
	if((err=move_addr_to_kernel(umyaddr,addrlen,address))<0)
	  	return err;
  
	if ((i = sock->ops->bind(sock, (struct sockaddr *)address, addrlen)) < 0) 
	{
		return(i);
	}
	return(0);
}


/*
 *	Perform a listen. Basically, we allow the protocol to do anything
 *	necessary for a listen, and if that works, we mark the socket as
 *	ready for listening.
 */

asmlinkage int sys_listen(int fd, int backlog)
{
	struct socket *sock;
	int err=-EOPNOTSUPP;
	
	if (fd < 0 || fd >= NR_OPEN || current->files->fd[fd] == NULL)
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL))) 
		return(-ENOTSOCK);

	if (sock->state != SS_UNCONNECTED) 
		return(-EINVAL);

	if (sock->ops && sock->ops->listen)
	{
		err=sock->ops->listen(sock, backlog);
		if(!err)
			sock->flags |= SO_ACCEPTCON;
	}
	return(err);
}


/*
 *	For accept, we attempt to create a new socket, set up the link
 *	with the client, wake up the client, then return the new
 *	connected fd. We collect the address of the connector in kernel
 *	space and move it to user at the very end. This is buggy because
 *	we open the socket then return an error.
 */

asmlinkage int sys_accept(int fd, struct sockaddr *upeer_sockaddr, int *upeer_addrlen)
{
	struct file *file;
	struct socket *sock, *newsock;
	int i;
	char address[MAX_SOCK_ADDR];
	int len;

	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
  	if (!(sock = sockfd_lookup(fd, &file))) 
		return(-ENOTSOCK);
	if (sock->state != SS_UNCONNECTED) 
	{
		return(-EINVAL);
	}
	if (!(sock->flags & SO_ACCEPTCON)) 
	{
		return(-EINVAL);
	}

	if (!(newsock = sock_alloc())) 
	{
		printk(KERN_WARNING "accept: no more sockets\n");
		return(-ENOSR);	/* Was: EAGAIN, but we are out of system
				   resources! */
	}
	newsock->type = sock->type;
	newsock->ops = sock->ops;
	if ((i = sock->ops->dup(newsock, sock)) < 0) 
	{
		sock_release(newsock);
		return(i);
	}

	i = newsock->ops->accept(sock, newsock, file->f_flags);
	if ( i < 0) 
	{
		sock_release(newsock);
		return(i);
	}

	if ((fd = get_fd(SOCK_INODE(newsock))) < 0) 
	{
		sock_release(newsock);
		return(-EINVAL);
	}
	newsock->file=current->files->fd[fd];
	
	if (upeer_sockaddr)
	{
		newsock->ops->getname(newsock, (struct sockaddr *)address, &len, 1);
		move_addr_to_user(address,len, upeer_sockaddr, upeer_addrlen);
	}
	return(fd);
}


/*
 *	Attempt to connect to a socket with the server address.  The address
 *	is in user space so we verify it is OK and move it to kernel space.
 */
 
asmlinkage int sys_connect(int fd, struct sockaddr *uservaddr, int addrlen)
{
	struct socket *sock;
	struct file *file;
	int i;
	char address[MAX_SOCK_ADDR];
	int err;

	if (fd < 0 || fd >= NR_OPEN || (file=current->files->fd[fd]) == NULL)
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, &file)))
		return(-ENOTSOCK);

	if((err=move_addr_to_kernel(uservaddr,addrlen,address))<0)
	  	return err;
  
	switch(sock->state) 
	{
		case SS_UNCONNECTED:
			/* This is ok... continue with connect */
			break;
		case SS_CONNECTED:
			/* Socket is already connected */
			if(sock->type == SOCK_DGRAM) /* Hack for now - move this all into the protocol */
				break;
			return -EISCONN;
		case SS_CONNECTING:
			/* Not yet connected... we will check this. */
		
			/*
			 *	FIXME:  for all protocols what happens if you start
			 *	an async connect fork and both children connect. Clean
			 *	this up in the protocols!
			 */
			break;
		default:
			return(-EINVAL);
	}
	i = sock->ops->connect(sock, (struct sockaddr *)address, addrlen, file->f_flags);
	if (i < 0) 
	{
		return(i);
	}
	return(0);
}

/*
 *	Get the local address ('name') of a socket object. Move the obtained
 *	name to user space.
 */

asmlinkage int sys_getsockname(int fd, struct sockaddr *usockaddr, int *usockaddr_len)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	int len;
	int err;
	
	if (fd < 0 || fd >= NR_OPEN || current->files->fd[fd] == NULL)
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL)))
		return(-ENOTSOCK);

	err=sock->ops->getname(sock, (struct sockaddr *)address, &len, 0);
	if(err)
		return err;
	if((err=move_addr_to_user(address,len, usockaddr, usockaddr_len))<0)
	  	return err;
	return 0;
}

/*
 *	Get the remote address ('name') of a socket object. Move the obtained
 *	name to user space.
 */
 
asmlinkage int sys_getpeername(int fd, struct sockaddr *usockaddr, int *usockaddr_len)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	int len;
	int err;

	if (fd < 0 || fd >= NR_OPEN || current->files->fd[fd] == NULL)
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL)))
		return(-ENOTSOCK);

	err=sock->ops->getname(sock, (struct sockaddr *)address, &len, 1);
	if(err)
	  	return err;
	if((err=move_addr_to_user(address,len, usockaddr, usockaddr_len))<0)
	  	return err;
	return 0;
}

/*
 *	Send a datagram down a socket. The datagram as with write() is
 *	in user space. We check it can be read.
 */

asmlinkage int sys_send(int fd, void * buff, int len, unsigned flags)
{
	struct socket *sock;
	struct file *file;
	int err;
	struct msghdr msg;
	struct iovec iov;

	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL))) 
		return(-ENOTSOCK);

	if(len<0)
		return -EINVAL;
	err=verify_area(VERIFY_READ, buff, len);
	if(err)
		return err;
		
	iov.iov_base=buff;
	iov.iov_len=len;
	msg.msg_name=NULL;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	return(sock->ops->sendmsg(sock, &msg, len, (file->f_flags & O_NONBLOCK), flags));
}

/*
 *	Send a datagram to a given address. We move the address into kernel
 *	space and check the user space data area is readable before invoking
 *	the protocol.
 */

asmlinkage int sys_sendto(int fd, void * buff, int len, unsigned flags,
	   struct sockaddr *addr, int addr_len)
{
	struct socket *sock;
	struct file *file;
	char address[MAX_SOCK_ADDR];
	int err;
	struct msghdr msg;
	struct iovec iov;
	
	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL)))
		return(-ENOTSOCK);

	if(len<0)
		return -EINVAL;
	err=verify_area(VERIFY_READ,buff,len);
	if(err)
	  	return err;
  	
	if((err=move_addr_to_kernel(addr,addr_len,address))<0)
	  	return err;
	  	
	iov.iov_base=buff;
	iov.iov_len=len;
	msg.msg_name=address;
	msg.msg_namelen=addr_len;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	return(sock->ops->sendmsg(sock, &msg, len, (file->f_flags & O_NONBLOCK),
		flags));
}


/*
 *	Receive a datagram from a socket. Call the protocol recvmsg method
 */

asmlinkage int sys_recv(int fd, void * ubuf, int size, unsigned flags)
{
	struct iovec iov;
	struct msghdr msg;
	struct socket *sock;
	struct file *file;
	int err;

	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);

	if (!(sock = sockfd_lookup(fd, NULL))) 
		return(-ENOTSOCK);
		
	if(size<0)
		return -EINVAL;
	if(size==0)
		return 0;
	err=verify_area(VERIFY_WRITE, ubuf, size);
	if(err)
		return err;
		
	msg.msg_name=NULL;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	iov.iov_base=ubuf;
	iov.iov_len=size;

	return(sock->ops->recvmsg(sock, &msg, size,(file->f_flags & O_NONBLOCK), flags,&msg.msg_namelen));
}

/*
 *	Receive a frame from the socket and optionally record the address of the 
 *	sender. We verify the buffers are writable and if needed move the
 *	sender address from kernel to user space.
 */

asmlinkage int sys_recvfrom(int fd, void * ubuf, int size, unsigned flags,
	     struct sockaddr *addr, int *addr_len)
{
	struct socket *sock;
	struct file *file;
	struct iovec iov;
	struct msghdr msg;
	char address[MAX_SOCK_ADDR];
	int err;
	int alen;
	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL))) 
	  	return(-ENOTSOCK);
	if(size<0)
		return -EINVAL;
	if(size==0)
		return 0;

	err=verify_area(VERIFY_WRITE,ubuf,size);
	if(err)
	  	return err;
  
  	msg.msg_control=NULL;
  	msg.msg_iovlen=1;
  	msg.msg_iov=&iov;
  	iov.iov_len=size;
  	iov.iov_base=ubuf;
  	msg.msg_name=address;
  	msg.msg_namelen=MAX_SOCK_ADDR;
	size=sock->ops->recvmsg(sock, &msg, size, (file->f_flags & O_NONBLOCK),
		     flags, &alen);

	if(size<0)
	 	return size;
	if(addr!=NULL && (err=move_addr_to_user(address,alen, addr, addr_len))<0)
	  	return err;

	return size;
}

/*
 *	Set a socket option. Because we don't know the option lengths we have
 *	to pass the user mode parameter for the protocols to sort out.
 */
 
asmlinkage int sys_setsockopt(int fd, int level, int optname, char *optval, int optlen)
{
	struct socket *sock;
	struct file *file;
	
	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL))) 
		return(-ENOTSOCK);

	return(sock->ops->setsockopt(sock, level, optname, optval, optlen));
}

/*
 *	Get a socket option. Because we don't know the option lengths we have
 *	to pass a user mode parameter for the protocols to sort out.
 */

asmlinkage int sys_getsockopt(int fd, int level, int optname, char *optval, int *optlen)
{
	struct socket *sock;
	struct file *file;

	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL)))
		return(-ENOTSOCK);
	    
	if (!sock->ops->getsockopt) 
		return(0);
	return(sock->ops->getsockopt(sock, level, optname, optval, optlen));
}


/*
 *	Shutdown a socket.
 */
 
asmlinkage int sys_shutdown(int fd, int how)
{
	struct socket *sock;
	struct file *file;

	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL))) 
		return(-ENOTSOCK);

	return(sock->ops->shutdown(sock, how));
}

/*
 *	BSD sendmsg interface
 */
 
asmlinkage int sys_sendmsg(int fd, struct msghdr *msg, unsigned int flags)
{
	struct socket *sock;
	struct file *file;
	char address[MAX_SOCK_ADDR];
	struct iovec iov[UIO_MAXIOV];
	struct msghdr msg_sys;
	int err;
	int total_len;
	
	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL)))
		return(-ENOTSOCK);
	
	if(sock->ops->sendmsg==NULL)
		return -EOPNOTSUPP;


	err=verify_area(VERIFY_READ, msg,sizeof(struct msghdr));
	if(err)
		return err;

	memcpy_fromfs(&msg_sys,msg,sizeof(struct msghdr));

	/* do not move before msg_sys is valid */
	if(msg_sys.msg_iovlen>UIO_MAXIOV)
		return -EINVAL;

	/* This will also move the address data into kernel space */
	err = verify_iovec(&msg_sys, iov, address, VERIFY_READ);
	if (err < 0)
		return err;
	total_len=err;

	return sock->ops->sendmsg(sock, &msg_sys, total_len, (file->f_flags&O_NONBLOCK), flags);
}

/*
 *	BSD recvmsg interface
 */
 
asmlinkage int sys_recvmsg(int fd, struct msghdr *msg, unsigned int flags)
{
	struct socket *sock;
	struct file *file;
	struct iovec iov[UIO_MAXIOV];
	struct msghdr msg_sys;
	int err;
	int total_len;
	int len;

	/* kernel mode address */
	char addr[MAX_SOCK_ADDR];
	int addr_len;

	/* user mode address pointers */
	struct sockaddr *uaddr;
	int *uaddr_len;
	
	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL)))
		return(-ENOTSOCK);
	
	err=verify_area(VERIFY_READ, msg,sizeof(struct msghdr));
	if(err)
		return err;
	memcpy_fromfs(&msg_sys,msg,sizeof(struct msghdr));
	if(msg_sys.msg_iovlen>UIO_MAXIOV)
		return -EINVAL;

	/*
	 * save the user-mode address (verify_iovec will change the
	 * kernel msghdr to use the kernel address space)
	 */
	uaddr = msg_sys.msg_name;
	uaddr_len = &msg->msg_namelen;
	err=verify_iovec(&msg_sys,iov,addr, VERIFY_WRITE);
	if(err<0)
		return err;

	total_len=err;
	
	if(sock->ops->recvmsg==NULL)
		return -EOPNOTSUPP;
	len=sock->ops->recvmsg(sock, &msg_sys, total_len, (file->f_flags&O_NONBLOCK), flags, &addr_len);
	if(len<0)
		return len;

	if (uaddr != NULL) {
		err = move_addr_to_user(addr, addr_len, uaddr, uaddr_len);
		if (err)
			return err;
	}
	return len;
}


/*
 *	Perform a file control on a socket file descriptor.
 */

int sock_fcntl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct socket *sock;

	sock = socki_lookup (filp->f_inode);
	if (sock != NULL && sock->ops != NULL && sock->ops->fcntl != NULL)
		return(sock->ops->fcntl(sock, cmd, arg));
	return(-EINVAL);
}


/*
 *	System call vectors. Since I (RIB) want to rewrite sockets as streams,
 *	we have this level of indirection. Not a lot of overhead, since more of
 *	the work is done via read/write/select directly.
 *
 *	I'm now expanding this up to a higher level to separate the assorted
 *	kernel/user space manipulations and global assumptions from the protocol
 *	layers proper - AC.
 *
 *	Argument checking cleaned up. Saved 20% in size.
 */

asmlinkage int sys_socketcall(int call, unsigned long *args)
{
	int er;
	unsigned char nargs[18]={0,3,3,3,2,3,3,3,
				 4,4,4,6,6,2,5,5,3,3};

	unsigned long a0,a1;
				 
	if(call<1||call>SYS_RECVMSG)
		return -EINVAL;
		
	er=verify_area(VERIFY_READ, args, nargs[call] * sizeof(unsigned long));
	if(er)
		return er;
		
	a0=get_user(args);
	a1=get_user(args+1);
	
		
	switch(call) 
	{
		case SYS_SOCKET:
			return(sys_socket(a0,a1,get_user(args+2)));
		case SYS_BIND:
			return(sys_bind(a0,(struct sockaddr *)a1,
					get_user(args+2)));
		case SYS_CONNECT:
			return(sys_connect(a0, (struct sockaddr *)a1,
					   get_user(args+2)));
		case SYS_LISTEN:
			return(sys_listen(a0,a1));
		case SYS_ACCEPT:
			return(sys_accept(a0,(struct sockaddr *)a1,
					  (int *)get_user(args+2)));
		case SYS_GETSOCKNAME:
			return(sys_getsockname(a0,(struct sockaddr *)a1,
					       (int *)get_user(args+2)));
		case SYS_GETPEERNAME:
			return(sys_getpeername(a0, (struct sockaddr *)a1,
					       (int *)get_user(args+2)));
		case SYS_SOCKETPAIR:
			return(sys_socketpair(a0,a1,
					      get_user(args+2),
					      (int *)get_user(args+3)));
		case SYS_SEND:
			return(sys_send(a0,
				(void *)a1,
				get_user(args+2),
				get_user(args+3)));
		case SYS_SENDTO:
			return(sys_sendto(a0,(void *)a1,
				get_user(args+2),
				get_user(args+3),
				(struct sockaddr *)get_user(args+4),
				get_user(args+5)));
		case SYS_RECV:
			return(sys_recv(a0,
				(void *)a1,
				get_user(args+2),
				get_user(args+3)));
		case SYS_RECVFROM:
			return(sys_recvfrom(a0,
				(void *)a1,
				get_user(args+2),
				get_user(args+3),
				(struct sockaddr *)get_user(args+4),
				(int *)get_user(args+5)));
		case SYS_SHUTDOWN:
			return(sys_shutdown(a0,a1));
		case SYS_SETSOCKOPT:
			return(sys_setsockopt(a0,
				a1,
				get_user(args+2),
				(char *)get_user(args+3),
				get_user(args+4)));
		case SYS_GETSOCKOPT:
			return(sys_getsockopt(a0,
				a1,
				get_user(args+2),
				(char *)get_user(args+3),
				(int *)get_user(args+4)));
		case SYS_SENDMSG:
				return sys_sendmsg(a0,
					(struct msghdr *) a1,
					get_user(args+2));
		case SYS_RECVMSG:
				return sys_recvmsg(a0,
					(struct msghdr *) a1,
					get_user(args+2));
	}
	return -EINVAL; /* to keep gcc happy */
}

/*
 *	This function is called by a protocol handler that wants to
 *	advertise its address family, and have it linked into the
 *	SOCKET module.
 */
 
int sock_register(int family, struct proto_ops *ops)
{
	int i;

	cli();
	for(i = 0; i < NPROTO; i++) 
	{
		if (pops[i] != NULL) 
			continue;
		pops[i] = ops;
		pops[i]->family = family;
		sti();
		return(i);
	}
	sti();
	return(-ENOMEM);
}

/*
 *	This function is called by a protocol handler that wants to
 *	remove its address family, and have it unlinked from the
 *	SOCKET module.
 */
 
int sock_unregister(int family)
{
	int i;

	cli();
	for(i = 0; i < NPROTO; i++) 
	{
		if (pops[i] == NULL) 
			continue;
		if (pops[i]->family == family)
		{
			pops[i]=NULL;
			sti();
			return(i);
		}
	}
	sti();
	return(-ENOENT);
}

void proto_init(void)
{
	extern struct net_proto protocols[];	/* Network protocols */
	struct net_proto *pro;

	/* Kick all configured protocols. */
	pro = protocols;
	while (pro->name != NULL) 
	{
		(*pro->init_func)(pro);
		pro++;
	}
	/* We're all done... */
}


void sock_init(void)
{
	int i;

	printk(KERN_INFO "Swansea University Computer Society NET3.035 for Linux 2.0\n");

	/*
	 *	Initialize all address (protocol) families. 
	 */
	 
	for (i = 0; i < NPROTO; ++i) pops[i] = NULL;
	
	/*
	 *	The netlink device handler may be needed early.
	 */

#ifdef CONFIG_NETLINK
	init_netlink();
#endif		 
	/*
	 *	Attach the routing/device information port.
	 */

#if defined(CONFIG_RTNETLINK)
	netlink_attach(NETLINK_ROUTE, netlink_donothing);
#endif

	/*
	 *	Attach the firewall module if configured
	 */
	 
#ifdef CONFIG_FIREWALL	 
	fwchain_init();
#endif

	/*
	 *	Initialize the protocols module. 
	 */

	proto_init();

	/*
	 *	Export networking symbols to the world.
	 */

#if defined(CONFIG_MODULES) && defined(CONFIG_NET)
	export_net_symbols();
#endif
}

int socket_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len = sprintf(buffer, "sockets: used %d\n", sockets_in_use);
	if (offset >= len)
	{
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	return len;
}
