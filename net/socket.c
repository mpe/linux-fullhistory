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
 *	paradigm. 
 *
 * PROBLEMS:
 *	- CLONE_FILES. Big problem, cloned thread can close file,
 *	  while other thread sleeps in kernel. It can be solved
 *	  by increasing f_count and releasing it on exit from syscall.
 *	  _HAS_ to be fixed before 2.2 is released. I assume whoever is
 *	  working on the CLONE stuff will fix that pile of accidents. If
 *	  you find this comment in a 2.2-preXXX kernel scream loudly.
 *
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

#if defined(CONFIG_KERNELD) && defined(CONFIG_NET)
#include <linux/kerneld.h>
#endif

#include <net/netlink.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/rarp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/scm.h>


static long long sock_lseek(struct inode *inode, struct file *file,
			    long long offset, int whence);
static long sock_read(struct inode *inode, struct file *file,
		      char *buf, unsigned long size);
static long sock_write(struct inode *inode, struct file *file,
		       const char *buf, unsigned long size);

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

static struct net_proto_family *net_families[NPROTO];

/*
 *	Statistics counters of the socket lists
 */

static int sockets_in_use  = 0;

/*
 *	Support routines. Move socket addresses back and forth across the kernel/user
 *	divide and look after the messy bits.
 */

#define MAX_SOCK_ADDR	128		/* 108 for Unix domain - 
					   16 for IP, 16 for IPX,
					   24 for IPv6,
					   about 80 for AX.25 */
 
int move_addr_to_kernel(void *uaddr, int ulen, void *kaddr)
{
	if(ulen<0||ulen>MAX_SOCK_ADDR)
		return -EINVAL;
	if(ulen==0)
		return 0;
	if(copy_from_user(kaddr,uaddr,ulen))
		return -EFAULT;
	return 0;
}

int move_addr_to_user(void *kaddr, int klen, void *uaddr, int *ulen)
{
	int err;
	int len;

	if((err=get_user(len, ulen)))
		return err;
	if(len>klen)
		len=klen;
	if(len<0 || len> MAX_SOCK_ADDR)
		return -EINVAL;
	if(len)
	{
		if(copy_to_user(uaddr,kaddr,len))
			return -EFAULT;
	}
	/*
	 *	"fromlen shall refer to the value before truncation.."
	 *			1003.1g
	 */
 	return put_user(klen, ulen);
}

/*
 *	Obtains the first available file descriptor and sets it up for use. 
 */

static int get_fd(struct inode *inode)
{
	int fd;

	/*
	 *	Find a file descriptor suitable for return to the user. 
	 */

	fd = get_unused_fd();
	if (fd >= 0) {
		struct file *file = get_empty_filp();

		if (!file) {
			put_unused_fd(fd);
			return -ENFILE;
		}

		current->files->fd[fd] = file;
		file->f_op = &socket_file_ops;
		file->f_mode = 3;
		file->f_flags = O_RDWR;
		file->f_count = 1;
		file->f_inode = inode;
		if (inode) 
			inode->i_count++;
		file->f_pos = 0;
	}
	return fd;
}

extern __inline__ struct socket *socki_lookup(struct inode *inode)
{
	return &inode->u.socket_i;
}

/*
 *	Go from a file number to its socket slot.
 */

extern __inline__ struct socket *sockfd_lookup(int fd, int *err)
{
	struct file *file;
	struct inode *inode;

	if (fd < 0 || fd >= NR_OPEN || !(file = current->files->fd[fd]))
	{
		*err = -EBADF;
		return NULL;
	}

	inode = file->f_inode;
	if (!inode || !inode->i_sock || !socki_lookup(inode))
	{
		*err = -ENOTSOCK;
		return NULL;
	}

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
	sock = socki_lookup(inode);

	inode->i_mode = S_IFSOCK;
	inode->i_sock = 1;
	inode->i_uid = current->uid;
	inode->i_gid = current->gid;

	sock->inode = inode;
	sock->fasync_list = NULL;
	sock->state = SS_UNCONNECTED;
	sock->flags = 0;
	sock->ops = NULL;
	sock->sk = NULL;
	sock->file = NULL;

	sockets_in_use++;
	return sock;
}

void sock_release(struct socket *sock)
{
	int oldstate;

	if ((oldstate = sock->state) != SS_UNCONNECTED)
		sock->state = SS_DISCONNECTING;

	if (sock->ops) 
		sock->ops->release(sock, NULL);

	--sockets_in_use;	/* Bookkeeping.. */
	sock->file=NULL;
	iput(sock->inode);
}

int sock_sendmsg(struct socket *sock, struct msghdr *msg, int size)
{
	int err;
	struct scm_cookie scm;

	if (!sock->ops->sendmsg)
		return -EOPNOTSUPP;

	err = scm_send(sock, msg, &scm);
	if (err < 0)
		return err;

	err = sock->ops->sendmsg(sock, msg, size, &scm);

	scm_destroy(&scm);

	return err;
}

int sock_recvmsg(struct socket *sock, struct msghdr *msg, int size, int flags)
{
	struct scm_cookie scm;

	if (!sock->ops->recvmsg)
		return -EOPNOTSUPP;

	memset(&scm, 0, sizeof(scm));

	size = sock->ops->recvmsg(sock, msg, size, flags, &scm);

	if (size < 0)
		return size;

	scm_recv(sock, msg, &scm, flags);

	return size;
}


/*
 *	Sockets are not seekable.
 */

static long long sock_lseek(struct inode *inode, struct file *file,
			    long long offset, int whence)
{
	return -ESPIPE;
}

/*
 *	Read data from a socket. ubuf is a user mode pointer. We make sure the user
 *	area ubuf...ubuf+size-1 is writable before asking the protocol.
 */

static long sock_read(struct inode *inode, struct file *file,
		      char *ubuf, unsigned long size)
{
	struct socket *sock;
	int err;
	struct iovec iov;
	struct msghdr msg;

	sock = socki_lookup(inode); 
  
	if (size==0)		/* Match SYS5 behaviour */
		return 0;
	if ((err=verify_area(VERIFY_WRITE,ubuf,size))<0)
	  	return err;
	msg.msg_name=NULL;
	msg.msg_namelen=0;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	msg.msg_controllen=0;
	iov.iov_base=ubuf;
	iov.iov_len=size;

	return sock_recvmsg(sock, &msg, size,
			    !(file->f_flags & O_NONBLOCK) ? 0 : MSG_DONTWAIT);
}


/*
 *	Write data to a socket. We verify that the user area ubuf..ubuf+size-1 is
 *	readable by the user process.
 */

static long sock_write(struct inode *inode, struct file *file,
		       const char *ubuf, unsigned long size)
{
	struct socket *sock;
	int err;
	struct msghdr msg;
	struct iovec iov;
	
	sock = socki_lookup(inode); 

	if(size==0)		/* Match SYS5 behaviour */
		return 0;
	
	if ((err=verify_area(VERIFY_READ,ubuf,size))<0)
	  	return err;
	
	msg.msg_name=NULL;
	msg.msg_namelen=0;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	msg.msg_controllen=0;
	msg.msg_flags=!(file->f_flags & O_NONBLOCK) ? 0 : MSG_DONTWAIT;
	iov.iov_base=(void *)ubuf;
	iov.iov_len=size;
	
	return sock_sendmsg(sock, &msg, size);
}

int sock_readv_writev(int type, struct inode * inode, struct file * file,
		      const struct iovec * iov, long count, long size)
{
	struct msghdr msg;
	struct socket *sock;

	sock = socki_lookup(inode);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov = (struct iovec *) iov;
	msg.msg_iovlen = count;
	msg.msg_flags = (file->f_flags & O_NONBLOCK) ? MSG_DONTWAIT : 0;

	/* read() does a VERIFY_WRITE */
	if (type == VERIFY_WRITE)
		return sock_recvmsg(sock, &msg, size, msg.msg_flags);
	return sock_sendmsg(sock, &msg, size);
}


/*
 *	With an ioctl arg may well be a user mode pointer, but we don't know what to do
 *	with it - that's up to the protocol still.
 */

int sock_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	   unsigned long arg)
{
	struct socket *sock = socki_lookup(inode);
  	return sock->ops->ioctl(sock, cmd, arg);
}


static int sock_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
	struct socket *sock;

	sock = socki_lookup(inode);

	/*
	 *	We can't return errors to select, so it's either yes or no. 
	 */

	if (sock->ops->select)
		return sock->ops->select(sock, sel_type, wait);
	return(0);
}


void sock_close(struct inode *inode, struct file *filp)
{
	/*
	 *	It was possible the inode is NULL we were 
	 *	closing an unfinished socket. 
	 */

	if (!inode)
	{
		printk(KERN_DEBUG "sock_close: NULL inode\n");
		return;
	}
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
	
	for (fa=*prev; fa!=NULL; prev=&fa->fa_next,fa=*prev)
		if (fa->fa_file==filp)
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
		if (fa!=NULL)
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
		if (net_families[i] == NULL)
			continue;
		if (net_families[i]->family == family)
			return i;
	}
	return -1;
}

asmlinkage int sys_socket(int family, int type, int protocol)
{
	int i, fd;
	struct socket *sock;

	/* Locate the correct protocol family. */
	i = find_protocol_family(family);

#if defined(CONFIG_KERNELD) && defined(CONFIG_NET)
	/* Attempt to load a protocol module if the find failed. 
	 * 
	 * 12/09/1996 Marcin: But! this makes REALLY only sense, if the user 
	 * requested real, full-featured networking support upon configuration.
	 * Otherwise module support will break!
	 */
	if (i < 0)
	{
		char module_name[30];
		sprintf(module_name,"net-pf-%d",family);
		request_module(module_name);
		i = find_protocol_family(family);
	}
#endif

	if (i < 0)
  		return -EINVAL;

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
		return(-ENFILE);	/* Not exactly a match, but its the
					   closest posix thing */
	}

	sock->type = type;

	if ((i = net_families[i]->create(sock, protocol)) < 0) 
	{
		sock_release(sock);
		return(i);
	}

	if ((fd = get_fd(sock->inode)) < 0) 
	{
		sock_release(sock);
		return(-EINVAL);
	}

	sock->file = current->files->fd[fd];

	return(fd);
}

/*
 *	Create a pair of connected sockets.
 */

asmlinkage int sys_socketpair(int family, int type, int protocol, int usockvec[2])
{
	int fd1, fd2, i;
	struct socket *sock1, *sock2;
	int err;

	/*
	 * Obtain the first socket and check if the underlying protocol
	 * supports the socketpair call.
	 */

	if ((fd1 = sys_socket(family, type, protocol)) < 0) 
		return(fd1);

	sock1 = sockfd_lookup(fd1, &err);
	if (!sock1)
		return err;
	if (!sock1->ops->socketpair) 
	{
		sys_close(fd1);
		return -EOPNOTSUPP;
	}

	/*
	 *	Now grab another socket and try to connect the two together. 
	 */

	if ((fd2 = sys_socket(family, type, protocol)) < 0) 
	{
		sys_close(fd1);
		return(-EINVAL);
	}

	sock2 = sockfd_lookup(fd2,&err);
	if (!sock2)
		return err;
	if ((i = sock1->ops->socketpair(sock1, sock2)) < 0) 
	{
		sys_close(fd1);
		sys_close(fd2);
		return(i);
	}

	err = put_user(fd1, &usockvec[0]); 
	if (!err) 
		err = put_user(fd2, &usockvec[1]);
	if (err) {
		sys_close(fd1);
		sys_close(fd2);
	}
	return err;
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

	if (!(sock = sockfd_lookup(fd,&err))) 
		return err;
  
	if((err=move_addr_to_kernel(umyaddr,addrlen,address))<0)
	  	return err;
  
	if ((i = sock->ops->bind(sock, (struct sockaddr *)address, addrlen)) < 0) 
		return(i);
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
	
	if (!(sock = sockfd_lookup(fd, &err))) 
		return err;

	if (sock->ops && sock->ops->listen)
		err=sock->ops->listen(sock, backlog);
	return err;
}


/*
 *	For accept, we attempt to create a new socket, set up the link
 *	with the client, wake up the client, then return the new
 *	connected fd. We collect the address of the connector in kernel
 *	space and move it to user at the very end. This is unclean because
 *	we open the socket then return an error.
 *
 *	1003.1g adds the ability to recvmsg() to query connection pending
 *	status to recvmsg. We need to add that support in a way thats
 *	clean when we restucture accept also.
 */

asmlinkage int sys_accept(int fd, struct sockaddr *upeer_sockaddr, int *upeer_addrlen)
{
	struct inode *inode;
	struct socket *sock, *newsock;
	int i;
	int err;
	char address[MAX_SOCK_ADDR];
	int len;

  	if (!(sock = sockfd_lookup(fd, &err)))
		return err;
	if (!sock->ops->accept)
		return -EOPNOTSUPP;

	if (!(newsock = sock_alloc())) 
	{
		printk(KERN_WARNING "accept: no more sockets\n");
		return(-ENOSR);	/* Was: EAGAIN, but we are out of system
				   resources! */
	}

	inode = newsock->inode;
	newsock->type = sock->type;

	if ((i = sock->ops->dup(newsock, sock)) < 0) 
	{
		sock_release(newsock);
		return(i);
	}

	i = newsock->ops->accept(sock, newsock, current->files->fd[fd]->f_flags);

	if (i < 0)
	{
		sock_release(newsock);
		return(i);
	}
	newsock = socki_lookup(inode);

	if ((fd = get_fd(inode)) < 0) 
	{
		sock_release(newsock);
		return(-EINVAL);
	}

	newsock->file = current->files->fd[fd];
	
	if (upeer_sockaddr)
	{
		newsock->ops->getname(newsock, (struct sockaddr *)address, &len, 1);
		move_addr_to_user(address,len, upeer_sockaddr, upeer_addrlen);
	}
	return fd;
}


/*
 *	Attempt to connect to a socket with the server address.  The address
 *	is in user space so we verify it is OK and move it to kernel space.
 *
 *	For 1003.1g we need to add clean support for a bind to AF_UNSPEC to
 *	break bindings
 *
 *	NOTE: 1003.1g draft 6.3 is broken with respect to AX.25/NetROM and
 *	other SEQPACKET protocols that take time to connect() as it doesn't
 *	include the -EINPROGRESS status for such sockets.
 */
 
asmlinkage int sys_connect(int fd, struct sockaddr *uservaddr, int addrlen)
{
	struct socket *sock;
	int i;
	char address[MAX_SOCK_ADDR];
	int err;

	if (!(sock = sockfd_lookup(fd,&err)))
		return err;

	if((err=move_addr_to_kernel(uservaddr,addrlen,address))<0)
	  	return err;
  
	i = sock->ops->connect(sock, (struct sockaddr *)address, addrlen,
			     current->files->fd[fd]->f_flags);
	if (i < 0)
		return(i);
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
	
	if (!(sock = sockfd_lookup(fd, &err)))
		return err;

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

	if (!(sock = sockfd_lookup(fd, &err)))
		return err;

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

asmlinkage int sys_send(int fd, void * buff, size_t len, unsigned flags)
{
	struct socket *sock;
	int err;
	struct msghdr msg;
	struct iovec iov;

	if (!(sock = sockfd_lookup(fd, &err))) 
		return err;

	if(len<0)
		return -EINVAL;
	err=verify_area(VERIFY_READ, buff, len);
	if(err)
		return err;
		
	iov.iov_base=buff;
	iov.iov_len=len;
	msg.msg_name=NULL;
	msg.msg_namelen=0;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	msg.msg_controllen=0;
	if (current->files->fd[fd]->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	msg.msg_flags=flags;

	return sock_sendmsg(sock, &msg, len);
}

/*
 *	Send a datagram to a given address. We move the address into kernel
 *	space and check the user space data area is readable before invoking
 *	the protocol.
 */

asmlinkage int sys_sendto(int fd, void * buff, size_t len, unsigned flags,
	   struct sockaddr *addr, int addr_len)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	int err;
	struct msghdr msg;
	struct iovec iov;
	
	if (!(sock = sockfd_lookup(fd,&err)))
		return err;

	err=verify_area(VERIFY_READ,buff,len);
	if(err)
	  	return err;
  	
	iov.iov_base=buff;
	iov.iov_len=len;
	msg.msg_name=NULL;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	msg.msg_controllen=0;
	msg.msg_namelen=addr_len;
	if(addr)
	{
		err=move_addr_to_kernel(addr,addr_len,address);
		if (err < 0)
			return err;
		msg.msg_name=address;
	}
	  	
	if (current->files->fd[fd]->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	msg.msg_flags=flags;

	return sock_sendmsg(sock, &msg, len);
}


/*
 *	Receive a datagram from a socket. Call the protocol recvmsg method
 */

asmlinkage int sys_recv(int fd, void * ubuf, size_t size, unsigned flags)
{
	struct iovec iov;
	struct msghdr msg;
	struct socket *sock;
	int err;

	if (!(sock = sockfd_lookup(fd, &err))) 
		return err;
		
	if(size==0)
		return 0;
	err=verify_area(VERIFY_WRITE, ubuf, size);
	if(err)
		return err;
		
	msg.msg_name=NULL;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	msg.msg_controllen=0;
	iov.iov_base=ubuf;
	iov.iov_len=size;

	return sock_recvmsg(sock, &msg, size,
			    (current->files->fd[fd]->f_flags & O_NONBLOCK) ? flags | MSG_DONTWAIT : flags);
}

/*
 *	Receive a frame from the socket and optionally record the address of the 
 *	sender. We verify the buffers are writable and if needed move the
 *	sender address from kernel to user space.
 */

asmlinkage int sys_recvfrom(int fd, void * ubuf, size_t size, unsigned flags,
	     struct sockaddr *addr, int *addr_len)
{
	struct socket *sock;
	struct iovec iov;
	struct msghdr msg;
	char address[MAX_SOCK_ADDR];
	int err;

	if (!(sock = sockfd_lookup(fd, &err)))
	  	return err;
	if (size==0)
		return 0;

	err=verify_area(VERIFY_WRITE,ubuf,size);
	if(err)
	  	return err;
  
  	msg.msg_control=NULL;
  	msg.msg_controllen=0;
  	msg.msg_iovlen=1;
  	msg.msg_iov=&iov;
  	iov.iov_len=size;
  	iov.iov_base=ubuf;
  	msg.msg_name=address;
  	msg.msg_namelen=MAX_SOCK_ADDR;
	err=sock_recvmsg(sock, &msg, size,
			  (current->files->fd[fd]->f_flags & O_NONBLOCK) ? (flags | MSG_DONTWAIT) : flags);

	if(err<0)
	 	return err;
	size=err;
	
	if(addr!=NULL && (err=move_addr_to_user(address, msg.msg_namelen, addr, addr_len))<0)
	  	return err;
	
	return size;
}

/*
 *	Set a socket option. Because we don't know the option lengths we have
 *	to pass the user mode parameter for the protocols to sort out.
 */
 
asmlinkage int sys_setsockopt(int fd, int level, int optname, char *optval, int optlen)
{
	int err;
	struct socket *sock;
	
	if (!(sock = sockfd_lookup(fd, &err))) 
		return err;

	if (level == SOL_SOCKET)
		return sock_setsockopt(sock,level,optname,optval,optlen);

	if (sock->ops->setsockopt)
		return sock->ops->setsockopt(sock, level, optname, optval, optlen);

	return -EOPNOTSUPP;
}

/*
 *	Get a socket option. Because we don't know the option lengths we have
 *	to pass a user mode parameter for the protocols to sort out.
 */

asmlinkage int sys_getsockopt(int fd, int level, int optname, char *optval, int *optlen)
{
	int err;
	struct socket *sock;

	if (!(sock = sockfd_lookup(fd, &err)))
		return err;
	    
	if (level == SOL_SOCKET)
		return sock_getsockopt(sock,level,optname,optval,optlen);

	if (sock->ops->getsockopt)
		return sock->ops->getsockopt(sock, level, optname, optval, optlen);

	return -EOPNOTSUPP;
}


/*
 *	Shutdown a socket.
 */
 
asmlinkage int sys_shutdown(int fd, int how)
{
	int err;
	struct socket *sock;

	if (!(sock = sockfd_lookup(fd, &err))) 
		return err;

	return sock->ops->shutdown(sock, how);
}

/*
 *	BSD sendmsg interface
 */
 
asmlinkage int sys_sendmsg(int fd, struct msghdr *msg, unsigned flags)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	struct iovec iov[UIO_FASTIOV];
	struct msghdr msg_sys;
	void * krn_msg_ctl = NULL;
	int err;
	int total_len;
	
	if (!(sock = sockfd_lookup(fd,&err)))
		return err;
	
	if (sock->ops->sendmsg==NULL)
		return -EOPNOTSUPP;

	if (copy_from_user(&msg_sys,msg,sizeof(struct msghdr)))
		return -EFAULT;

	/* do not move before msg_sys is valid */
	if (msg_sys.msg_iovlen>UIO_MAXIOV)
		return -EINVAL;

	/* This will also move the address data into kernel space */
	err = verify_iovec(&msg_sys, iov, address, VERIFY_READ);
	if (err < 0)
		return err;
	total_len=err;

	if (msg_sys.msg_control==NULL)
		msg_sys.msg_controllen = 0;

	if (msg_sys.msg_controllen)
	{
		krn_msg_ctl = kmalloc(msg_sys.msg_controllen, GFP_KERNEL);
		
		if (!krn_msg_ctl)
		{
			err = -ENOBUFS;
			goto flush_it;
		}		
		err = copy_from_user(krn_msg_ctl, msg_sys.msg_control,
				     msg_sys.msg_controllen);
		if (err)
			goto flush_it;
		msg_sys.msg_control = krn_msg_ctl;
	}

	msg_sys.msg_flags = flags;
	if (current->files->fd[fd]->f_flags & O_NONBLOCK)
		msg_sys.msg_flags |= MSG_DONTWAIT;

	err = sock_sendmsg(sock, &msg_sys, total_len);

flush_it:
	if (msg_sys.msg_iov != iov)
		kfree(iov);

	if (krn_msg_ctl)
	{
		kfree(krn_msg_ctl);
	}

	return err;
}

/*
 *	BSD recvmsg interface
 */
 
asmlinkage int sys_recvmsg(int fd, struct msghdr *msg, unsigned int flags)
{
	struct socket *sock;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack;
	struct msghdr msg_sys;
	void * krn_msg_ctl = NULL;
	void * usr_msg_ctl = NULL;
	int err;
	int total_len;
	int len = 0;

	/* kernel mode address */
	char addr[MAX_SOCK_ADDR];

	/* user mode address pointers */
	struct sockaddr *uaddr;
	int *uaddr_len;
	
	if (!(sock = sockfd_lookup(fd, &err)))
		return err;

	if (copy_from_user(&msg_sys,msg,sizeof(struct msghdr)))
		return -EFAULT; 

	if (msg_sys.msg_iovlen>UIO_MAXIOV)
		return -EINVAL;

	/*
	 *	Save the user-mode address (verify_iovec will change the
	 *	kernel msghdr to use the kernel address space)
	 */
	 
	uaddr = msg_sys.msg_name;
	uaddr_len = &msg->msg_namelen;
	err=verify_iovec(&msg_sys, iov, addr, VERIFY_WRITE);
	if (err<0)
		return err;

	total_len=err;

	msg_sys.msg_flags = 0;
	
	if (msg_sys.msg_control==NULL)
		msg_sys.msg_controllen = 0;

	if (msg_sys.msg_controllen)
	{
		/*
		 *	FIXME:
		 *	I'm assuming that the kernel may have to examine
		 *	the acciliary control messages passed by the user.
		 *	Find out what POSIX says about this...
		 */
		krn_msg_ctl = kmalloc(msg_sys.msg_controllen, GFP_KERNEL);
		
		if (!krn_msg_ctl)
		{
			err=-ENOBUFS;
			goto flush_it;		
		}
		err = copy_from_user(krn_msg_ctl, msg_sys.msg_control,
				     msg_sys.msg_controllen);
		if (err)
		{
			err = -EFAULT;
			goto flush_it;
		}
		usr_msg_ctl = msg_sys.msg_control;
		msg_sys.msg_control = krn_msg_ctl;
	}

	if (current->files->fd[fd]->f_flags&O_NONBLOCK)
		flags |= MSG_DONTWAIT;

	len=sock_recvmsg(sock, &msg_sys, total_len, flags);
	if (msg_sys.msg_iov != iov)
		kfree(iov);
	if (len<0)
	{
		err=len;
		goto flush_it;
	}
	
	if (uaddr != NULL)
	{
		err = move_addr_to_user(addr, msg_sys.msg_namelen, uaddr,
					uaddr_len);
	}
	
	if (err >= 0 && msg_sys.msg_controllen)
	{
		err = copy_to_user(usr_msg_ctl, krn_msg_ctl,
				   msg_sys.msg_controllen);
	}

flush_it:
	if (msg_sys.msg_iov != iov)
		kfree(iov);

	if (krn_msg_ctl)
	{
		kfree(krn_msg_ctl);
	}

	if (err < 0)
		return err;

	if (put_user(msg_sys.msg_flags, &msg->msg_flags))
		return -EFAULT;

	if (put_user(msg_sys.msg_controllen, &msg->msg_controllen))
		return -EFAULT;

	return len;
}


/*
 *	Perform a file control on a socket file descriptor.
 */

int sock_fcntl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct socket *sock;

	sock = socki_lookup (filp->f_inode);
	if (sock && sock->ops && sock->ops->fcntl)
		return sock->ops->fcntl(sock, cmd, arg);
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
	unsigned char nargs[18]={0,3,3,3,2,3,3,3,
				 4,4,4,6,6,2,5,5,3,3};
	unsigned long a[6];
	unsigned long a0,a1;
				 
	if(call<1||call>SYS_RECVMSG)
		return -EINVAL;
		
	if ((copy_from_user(a, args, nargs[call] * sizeof(unsigned long))))
		return -EFAULT;
		
	a0=a[0];
	a1=a[1];
	
		
	switch(call) 
	{
		case SYS_SOCKET:
			return(sys_socket(a0,a1,a[2]));
		case SYS_BIND:
			return(sys_bind(a0,(struct sockaddr *)a1,
					a[2]));
		case SYS_CONNECT:
			return(sys_connect(a0, (struct sockaddr *)a1,
					   a[2]));
		case SYS_LISTEN:
			return(sys_listen(a0,a1));
		case SYS_ACCEPT:
			return(sys_accept(a0,(struct sockaddr *)a1,
					  (int *)a[2]));
		case SYS_GETSOCKNAME:
			return(sys_getsockname(a0,(struct sockaddr *)a1,
					       (int *)a[2]));
		case SYS_GETPEERNAME:
			return(sys_getpeername(a0, (struct sockaddr *)a1,
					       (int *)a[2]));
		case SYS_SOCKETPAIR:
			return(sys_socketpair(a0,a1,
					      a[2],
					      (int *)a[3]));
		case SYS_SEND:
			return(sys_send(a0,
				(void *)a1,
				a[2],
				a[3]));
		case SYS_SENDTO:
			return(sys_sendto(a0,(void *)a1,
				a[2],
				a[3],
				(struct sockaddr *)a[4],
				a[5]));
		case SYS_RECV:
			return(sys_recv(a0,
				(void *)a1,
				a[2],
				a[3]));
		case SYS_RECVFROM:
			return(sys_recvfrom(a0,
				(void *)a1,
				a[2],
				a[3],
				(struct sockaddr *)a[4],
				(int *)a[5]));
		case SYS_SHUTDOWN:
			return(sys_shutdown(a0,a1));
		case SYS_SETSOCKOPT:
			return(sys_setsockopt(a0,
				a1,
				a[2],
				(char *)a[3],
				a[4]));
		case SYS_GETSOCKOPT:
			return(sys_getsockopt(a0,
				a1,
				a[2],
				(char *)a[3],
				(int *)a[4]));
		case SYS_SENDMSG:
				return sys_sendmsg(a0,
					(struct msghdr *) a1,
					a[2]);
		case SYS_RECVMSG:
				return sys_recvmsg(a0,
					(struct msghdr *) a1,
					a[2]);
	}
	return -EINVAL; /* to keep gcc happy */
}


/*
 *	This function is called by a protocol handler that wants to
 *	advertise its address family, and have it linked into the
 *	SOCKET module.
 */
 
int sock_register(struct net_proto_family *ops)
{
	int i;

	cli();
	for(i = 0; i < NPROTO; i++) 
	{
		if (net_families[i] != NULL) 
			continue;
		net_families[i] = ops;
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
		if (net_families[i] == NULL) 
			continue;
		if (net_families[i]->family == family)
		{
			net_families[i]=NULL;
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

	printk(KERN_INFO "Swansea University Computer Society NET3.038 for Linux 2.1\n");

	/*
	 *	Initialize all address (protocol) families. 
	 */
	 
	for (i = 0; i < NPROTO; i++) net_families[i] = NULL;
	
	/*
	 *	The netlink device handler may be needed early.
	 */

#ifdef CONFIG_NETLINK
	init_netlink();
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
