/*
 * NET		An implementation of the SOCKET network access protocol.
 *
 * Version:	@(#)socket.c	1.0.5	05/25/93
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
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>

#include <asm/system.h>
#include <asm/segment.h>

static int sock_lseek(struct inode *inode, struct file *file, off_t offset,
		      int whence);
static int sock_read(struct inode *inode, struct file *file, char *buf,
		     int size);
static int sock_write(struct inode *inode, struct file *file, char *buf,
		      int size);
static int sock_readdir(struct inode *inode, struct file *file,
			struct dirent *dirent, int count);
static void sock_close(struct inode *inode, struct file *file);
static int sock_select(struct inode *inode, struct file *file, int which, select_table *seltable);
static int sock_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg);


/*
 *	Socket files have a set of 'special' operations as well as the generic file ones. These don't appear
 *	in the operation structures but are done directly via the socketcall() multiplexor.
 */

static struct file_operations socket_file_ops = {
	sock_lseek,
	sock_read,
	sock_write,
	sock_readdir,
	sock_select,
	sock_ioctl,
	NULL,			/* mmap */
	NULL,			/* no special open code... */
	sock_close
};

/*
 *	The list of sockets - make this atomic.
 */
static struct socket sockets[NSOCKETS];
/*
 *	Used to wait for a socket.
 */
static struct wait_queue *socket_wait_free = NULL;
/*
 *	The protocol list. Each protocol is registered in here.
 */
static struct proto_ops *pops[NPROTO];

#define last_socket	(sockets + NSOCKETS - 1)


/*
 *	Support routines. Move socket addresses back and forth across the kernel/user
 *	divide and look after the messy bits.
 */

#define MAX_SOCK_ADDR	128		/* 108 for Unix domain - 16 for IP, 16 for IPX, about 80 for AX.25 */
 
static int move_addr_to_kernel(void *uaddr, int ulen, void *kaddr)
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

static int move_addr_to_user(void *kaddr, int klen, void *uaddr, int *ulen)
{
	int err;
	int len;

		
	if((err=verify_area(VERIFY_WRITE,ulen,sizeof(*ulen)))<0)
		return err;
	len=get_fs_long(ulen);
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
 	put_fs_long(len,ulen);
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
	file->f_flags = 0;
	file->f_count = 1;
	file->f_inode = inode;
	if (inode) 
		inode->i_count++;
	file->f_pos = 0;
	return(fd);
}


/*
 * Reverses the action of get_fd() by releasing the file. it closes
 * the descriptor, but makes sure it does nothing more. Called when
 * an incomplete socket must be closed, along with sock_release().
 */
 
static inline void toss_fd(int fd)
{
	sys_close(fd);		/* the count protects us from iput */
}

/*
 *	Go from an inode to its socket slot.
 */

struct socket *socki_lookup(struct inode *inode)
{
	struct socket *sock;

	if ((sock = inode->i_socket) != NULL) 
	{
		if (sock->state != SS_FREE && SOCK_INODE(sock) == inode)
			return sock;
		printk("socket.c: uhhuh. stale inode->i_socket pointer\n");
	}
	for (sock = sockets; sock <= last_socket; ++sock)
		if (sock->state != SS_FREE && SOCK_INODE(sock) == inode) 
		{
			printk("socket.c: uhhuh. Found socket despite no inode->i_socket pointer\n");
			return(sock);
		}
		return(NULL);
}

/*
 *	Go from a file number to its socket slot.
 */

static inline struct socket *sockfd_lookup(int fd, struct file **pfile)
{
	struct file *file;

	if (fd < 0 || fd >= NR_OPEN || !(file = current->files->fd[fd])) 
		return(NULL);

	if (pfile) 
		*pfile = file;

	return(socki_lookup(file->f_inode));
}

/*
 *	Allocate a socket. Wait if we are out of sockets.
 */

static struct socket *sock_alloc(int wait)
{
	struct socket *sock;

	while (1) 
	{
		cli();
		for (sock = sockets; sock <= last_socket; ++sock) 
		{
			if (sock->state == SS_FREE) 
			{
			/*
			 *	Got one..
			 */
				sock->state = SS_UNCONNECTED;
				sti();
				sock->flags = 0;
				sock->ops = NULL;
				sock->data = NULL;
				sock->conn = NULL;
				sock->iconn = NULL;
			/*
			 * This really shouldn't be necessary, but everything
			 * else depends on inodes, so we grab it.
			 * Sleeps are also done on the i_wait member of this
			 * inode.  The close system call will iput this inode
			 * for us.
			 */
				if (!(SOCK_INODE(sock) = get_empty_inode())) 
				{
					printk("NET: sock_alloc: no more inodes\n");
					sock->state = SS_FREE;
					return(NULL);
				}
				SOCK_INODE(sock)->i_mode = S_IFSOCK;
				SOCK_INODE(sock)->i_uid = current->euid;
				SOCK_INODE(sock)->i_gid = current->egid;
				SOCK_INODE(sock)->i_socket = sock;

				sock->wait = &SOCK_INODE(sock)->i_wait;
				return(sock);
			}
		}
		sti();
		/*
		 *	If its a 'now or never request' then return.
		 */
		if (!wait) 
			return(NULL);
		/*
		 *	Sleep on the socket free'ing queue.
		 */
		interruptible_sleep_on(&socket_wait_free);
		/*
		 *	If we have been interrupted then return.
		 */
		if (current->signal & ~current->blocked) 
		{
			return(NULL);
		}
	}
}

/*
 *	Release a socket.
 */

static inline void sock_release_peer(struct socket *peer)
{
	peer->state = SS_DISCONNECTING;
	wake_up_interruptible(peer->wait);
}


static void sock_release(struct socket *sock)
{
	int oldstate;
	struct inode *inode;
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
	inode = SOCK_INODE(sock);
	sock->state = SS_FREE;		/* this really releases us */
	
	/*
	 *	This will wake anyone waiting for a free socket.
	 */
	wake_up_interruptible(&socket_wait_free);

	/*
	 *	We need to do this. If sock alloc was called we already have an inode. 
	 */
	 
	iput(inode);
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
  
	if (!(sock = socki_lookup(inode))) 
	{
		printk("NET: sock_read: can't find socket for inode!\n");
		return(-EBADF);
	}
	if (sock->flags & SO_ACCEPTCON) 
		return(-EINVAL);

	if(size<0)
		return -EINVAL;
	if(size==0)
		return 0;
	if ((err=verify_area(VERIFY_WRITE,ubuf,size))<0)
	  	return err;
	return(sock->ops->read(sock, ubuf, size, (file->f_flags & O_NONBLOCK)));
}

/*
 *	Write data to a socket. We verify that the user area ubuf..ubuf+size-1 is
 *	readable by the user process.
 */

static int sock_write(struct inode *inode, struct file *file, char *ubuf, int size)
{
	struct socket *sock;
	int err;
	
	if (!(sock = socki_lookup(inode))) 
	{
		printk("NET: sock_write: can't find socket for inode!\n");
		return(-EBADF);
	}

	if (sock->flags & SO_ACCEPTCON) 
		return(-EINVAL);
	
	if(size<0)
		return -EINVAL;
	if(size==0)
		return 0;
		
	if ((err=verify_area(VERIFY_READ,ubuf,size))<0)
	  	return err;
	return(sock->ops->write(sock, ubuf, size,(file->f_flags & O_NONBLOCK)));
}

/*
 *	You can't read directories from a socket!
 */
 
static int sock_readdir(struct inode *inode, struct file *file, struct dirent *dirent,
	     int count)
{
	return(-EBADF);
}

/*
 *	With an ioctl arg may well be a user mode pointer, but we don't know what to do
 *	with it - thats up to the protocol still.
 */

int sock_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	   unsigned long arg)
{
	struct socket *sock;

	if (!(sock = socki_lookup(inode))) 
	{
		printk("NET: sock_ioctl: can't find socket for inode!\n");
		return(-EBADF);
	}
  	return(sock->ops->ioctl(sock, cmd, arg));
}


static int sock_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
	struct socket *sock;

	if (!(sock = socki_lookup(inode))) 
	{
		printk("NET: sock_select: can't find socket for inode!\n");
		return(0);
	}

	/*
	 *	We can't return errors to select, so its either yes or no. 
	 */

	if (sock->ops && sock->ops->select)
		return(sock->ops->select(sock, sel_type, wait));
	return(0);
}


void sock_close(struct inode *inode, struct file *file)
{
	struct socket *sock;

	/*
	 *	It's possible the inode is NULL if we're closing an unfinished socket. 
	 */

	if (!inode) 
		return;

	if (!(sock = socki_lookup(inode))) 
	{
		printk("NET: sock_close: can't find socket for inode!\n");
		return;
	}

	sock_release(sock);
}

/*
 *	Wait for a connection.
 */

int sock_awaitconn(struct socket *mysock, struct socket *servsock)
{
	struct socket *last;

	/*
	 *	We must be listening
	 */
	if (!(servsock->flags & SO_ACCEPTCON)) 
	{
		return(-EINVAL);
	}

  	/*
  	 *	Put ourselves on the server's incomplete connection queue. 
  	 */
  	 
	mysock->next = NULL;
	cli();
	if (!(last = servsock->iconn)) 
		servsock->iconn = mysock;
	else 
	{
		while (last->next) 
			last = last->next;
		last->next = mysock;
	}
	mysock->state = SS_CONNECTING;
	mysock->conn = servsock;
	sti();

	/*
	 * Wake up server, then await connection. server will set state to
	 * SS_CONNECTED if we're connected.
	 */
	wake_up_interruptible(servsock->wait);
	if (mysock->state != SS_CONNECTED) 
	{
		interruptible_sleep_on(mysock->wait);
		if (mysock->state != SS_CONNECTED &&
		    mysock->state != SS_DISCONNECTING) 
		{
		/*
		 * if we're not connected we could have been
		 * 1) interrupted, so we need to remove ourselves
		 *    from the server list
		 * 2) rejected (mysock->conn == NULL), and have
		 *    already been removed from the list
		 */
			if (mysock->conn == servsock) 
			{
				cli();
				if ((last = servsock->iconn) == mysock)
					servsock->iconn = mysock->next;
				else 
				{
					while (last->next != mysock) 
						last = last->next;
					last->next = mysock->next;
				}
				sti();
			}
			return(mysock->conn ? -EINTR : -EACCES);
		}
	}
	return(0);
}


/*
 *	Perform the socket system call. we locate the appropriate
 *	family, then create a fresh socket.
 */

static int sock_socket(int family, int type, int protocol)
{
	int i, fd;
	struct socket *sock;
	struct proto_ops *ops;

	/* Locate the correct protocol family. */
	for (i = 0; i < NPROTO; ++i) 
	{
		if (pops[i] == NULL) continue;
		if (pops[i]->family == family) 
			break;
	}

	if (i == NPROTO) 
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

	if (!(sock = sock_alloc(1))) 
	{
		printk("sock_socket: no more sockets\n");
		return(-EAGAIN);
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

	return(fd);
}

/*
 *	Create a pair of connected sockets.
 */

static int sock_socketpair(int family, int type, int protocol, unsigned long usockvec[2])
{
	int fd1, fd2, i;
	struct socket *sock1, *sock2;
	int er;

	/*
	 * Obtain the first socket and check if the underlying protocol
	 * supports the socketpair call.
	 */

	if ((fd1 = sock_socket(family, type, protocol)) < 0) 
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

	if ((fd2 = sock_socket(family, type, protocol)) < 0) 
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

	er=verify_area(VERIFY_WRITE, usockvec, 2 * sizeof(int));
	if(er)
	 	return er;
	put_fs_long(fd1, &usockvec[0]);
	put_fs_long(fd2, &usockvec[1]);

	return(0);
}


/*
 *	Bind a name to a socket. Nothing much to do here since its
 *	the protocol's responsibility to handle the local address.
 *
 *	We move the socket address to kernel space before we call
 *	the protocol layer (having also checked the address is ok).
 */
 
static int sock_bind(int fd, struct sockaddr *umyaddr, int addrlen)
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

static int sock_listen(int fd, int backlog)
{
	struct socket *sock;

	if (fd < 0 || fd >= NR_OPEN || current->files->fd[fd] == NULL)
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL))) 
		return(-ENOTSOCK);

	if (sock->state != SS_UNCONNECTED) 
	{
		return(-EINVAL);
	}

	if (sock->ops && sock->ops->listen)
		sock->ops->listen(sock, backlog);
	sock->flags |= SO_ACCEPTCON;
	return(0);
}


/*
 *	For accept, we attempt to create a new socket, set up the link
 *	with the client, wake up the client, then return the new
 *	connected fd. We collect the address of the connector in kernel
 *	space and move it to user at the very end. This is buggy because
 *	we open the socket then return an error.
 */

static int sock_accept(int fd, struct sockaddr *upeer_sockaddr, int *upeer_addrlen)
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

	if (!(newsock = sock_alloc(0))) 
	{
		printk("NET: sock_accept: no more sockets\n");
		return(-EAGAIN);
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
 
static int sock_connect(int fd, struct sockaddr *uservaddr, int addrlen)
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
			return(sock->ops->connect(sock, uservaddr,
				  addrlen, file->f_flags));
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

static int sock_getsockname(int fd, struct sockaddr *usockaddr, int *usockaddr_len)
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
 
static int sock_getpeername(int fd, struct sockaddr *usockaddr, int *usockaddr_len)
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

static int sock_send(int fd, void * buff, int len, unsigned flags)
{
	struct socket *sock;
	struct file *file;
	int err;

	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL))) 
		return(-ENOTSOCK);

	if(len<0)
		return -EINVAL;
	if(len==0)
		return 0;
	err=verify_area(VERIFY_READ, buff, len);
	if(err)
		return err;
	return(sock->ops->send(sock, buff, len, (file->f_flags & O_NONBLOCK), flags));
}

/*
 *	Send a datagram to a given address. We move the address into kernel
 *	space and check the user space data area is readable before invoking
 *	the protocol.
 */

static int sock_sendto(int fd, void * buff, int len, unsigned flags,
	   struct sockaddr *addr, int addr_len)
{
	struct socket *sock;
	struct file *file;
	char address[MAX_SOCK_ADDR];
	int err;
	
	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL)))
		return(-ENOTSOCK);

	if(len<0)
		return -EINVAL;
	if(len==0)
		return 0;
	err=verify_area(VERIFY_READ,buff,len);
	if(err)
	  	return err;
  	
	if((err=move_addr_to_kernel(addr,addr_len,address))<0)
	  	return err;

	return(sock->ops->sendto(sock, buff, len, (file->f_flags & O_NONBLOCK),
		flags, (struct sockaddr *)address, addr_len));
}


/*
 *	Receive a datagram from a socket. This isn't really right. The BSD manual
 *	pages explicitly state that recv is recvfrom with a NULL to argument. The
 *	Linux stack gets the right results for the wrong reason and this need to
 *	be tidied in the inet layer and removed from here.
 *	We check the buffer is writable and valid.
 */

static int sock_recv(int fd, void * buff, int len, unsigned flags)
{
	struct socket *sock;
	struct file *file;
	int err;

	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);

	if (!(sock = sockfd_lookup(fd, NULL))) 
		return(-ENOTSOCK);
		
	if(len<0)
		return -EINVAL;
	if(len==0)
		return 0;
	err=verify_area(VERIFY_WRITE, buff, len);
	if(err)
		return err;

	return(sock->ops->recv(sock, buff, len,(file->f_flags & O_NONBLOCK), flags));
}

/*
 *	Receive a frame from the socket and optionally record the address of the 
 *	sender. We verify the buffers are writable and if needed move the
 *	sender address from kernel to user space.
 */

static int sock_recvfrom(int fd, void * buff, int len, unsigned flags,
	     struct sockaddr *addr, int *addr_len)
{
	struct socket *sock;
	struct file *file;
	char address[MAX_SOCK_ADDR];
	int err;
	int alen;
	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL))) 
	  	return(-ENOTSOCK);
	if(len<0)
		return -EINVAL;
	if(len==0)
		return 0;

	err=verify_area(VERIFY_WRITE,buff,len);
	if(err)
	  	return err;
  
	len=sock->ops->recvfrom(sock, buff, len, (file->f_flags & O_NONBLOCK),
		     flags, (struct sockaddr *)address, &alen);

	if(len<0)
	 	return len;
	if(addr!=NULL && (err=move_addr_to_user(address,alen, addr, addr_len))<0)
	  	return err;

	return len;
}

/*
 *	Set a socket option. Because we don't know the option lengths we have
 *	to pass the user mode parameter for the protocols to sort out.
 */
 
static int sock_setsockopt(int fd, int level, int optname, char *optval, int optlen)
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

static int sock_getsockopt(int fd, int level, int optname, char *optval, int *optlen)
{
	struct socket *sock;
	struct file *file;

	if (fd < 0 || fd >= NR_OPEN || ((file = current->files->fd[fd]) == NULL))
		return(-EBADF);
	if (!(sock = sockfd_lookup(fd, NULL)))
		return(-ENOTSOCK);
	    
	if (!sock->ops || !sock->ops->getsockopt) 
		return(0);
	return(sock->ops->getsockopt(sock, level, optname, optval, optlen));
}


/*
 *	Shutdown a socket.
 */
 
static int sock_shutdown(int fd, int how)
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
 */

asmlinkage int sys_socketcall(int call, unsigned long *args)
{
	int er;
	switch(call) 
	{
		case SYS_SOCKET:
			er=verify_area(VERIFY_READ, args, 3 * sizeof(long));
			if(er)
				return er;
			return(sock_socket(get_fs_long(args+0),
				get_fs_long(args+1),
				get_fs_long(args+2)));
		case SYS_BIND:
			er=verify_area(VERIFY_READ, args, 3 * sizeof(long));
			if(er)
				return er;
			return(sock_bind(get_fs_long(args+0),
				(struct sockaddr *)get_fs_long(args+1),
				get_fs_long(args+2)));
		case SYS_CONNECT:
			er=verify_area(VERIFY_READ, args, 3 * sizeof(long));
			if(er)
				return er;
			return(sock_connect(get_fs_long(args+0),
				(struct sockaddr *)get_fs_long(args+1),
				get_fs_long(args+2)));
		case SYS_LISTEN:
			er=verify_area(VERIFY_READ, args, 2 * sizeof(long));
			if(er)
				return er;
			return(sock_listen(get_fs_long(args+0),
				get_fs_long(args+1)));
		case SYS_ACCEPT:
			er=verify_area(VERIFY_READ, args, 3 * sizeof(long));
			if(er)
				return er;
			return(sock_accept(get_fs_long(args+0),
				(struct sockaddr *)get_fs_long(args+1),
				(int *)get_fs_long(args+2)));
		case SYS_GETSOCKNAME:
			er=verify_area(VERIFY_READ, args, 3 * sizeof(long));
			if(er)
				return er;
			return(sock_getsockname(get_fs_long(args+0),
				(struct sockaddr *)get_fs_long(args+1),
				(int *)get_fs_long(args+2)));
		case SYS_GETPEERNAME:
			er=verify_area(VERIFY_READ, args, 3 * sizeof(long));
			if(er)
				return er;
			return(sock_getpeername(get_fs_long(args+0),
				(struct sockaddr *)get_fs_long(args+1),
				(int *)get_fs_long(args+2)));
		case SYS_SOCKETPAIR:
			er=verify_area(VERIFY_READ, args, 4 * sizeof(long));
			if(er)
				return er;
			return(sock_socketpair(get_fs_long(args+0),
				get_fs_long(args+1),
				get_fs_long(args+2),
				(unsigned long *)get_fs_long(args+3)));
		case SYS_SEND:
			er=verify_area(VERIFY_READ, args, 4 * sizeof(unsigned long));
			if(er)
				return er;
			return(sock_send(get_fs_long(args+0),
				(void *)get_fs_long(args+1),
				get_fs_long(args+2),
				get_fs_long(args+3)));
		case SYS_SENDTO:
			er=verify_area(VERIFY_READ, args, 6 * sizeof(unsigned long));
			if(er)
				return er;
			return(sock_sendto(get_fs_long(args+0),
				(void *)get_fs_long(args+1),
				get_fs_long(args+2),
				get_fs_long(args+3),
				(struct sockaddr *)get_fs_long(args+4),
				get_fs_long(args+5)));
		case SYS_RECV:
			er=verify_area(VERIFY_READ, args, 4 * sizeof(unsigned long));
			if(er)
				return er;
			return(sock_recv(get_fs_long(args+0),
				(void *)get_fs_long(args+1),
				get_fs_long(args+2),
				get_fs_long(args+3)));
		case SYS_RECVFROM:
			er=verify_area(VERIFY_READ, args, 6 * sizeof(unsigned long));
			if(er)
				return er;
			return(sock_recvfrom(get_fs_long(args+0),
				(void *)get_fs_long(args+1),
				get_fs_long(args+2),
				get_fs_long(args+3),
				(struct sockaddr *)get_fs_long(args+4),
				(int *)get_fs_long(args+5)));
		case SYS_SHUTDOWN:
			er=verify_area(VERIFY_READ, args, 2* sizeof(unsigned long));
			if(er)
				return er;
			return(sock_shutdown(get_fs_long(args+0),
				get_fs_long(args+1)));
		case SYS_SETSOCKOPT:
			er=verify_area(VERIFY_READ, args, 5*sizeof(unsigned long));
			if(er)
				return er;
			return(sock_setsockopt(get_fs_long(args+0),
				get_fs_long(args+1),
				get_fs_long(args+2),
				(char *)get_fs_long(args+3),
				get_fs_long(args+4)));
		case SYS_GETSOCKOPT:
			er=verify_area(VERIFY_READ, args, 5*sizeof(unsigned long));
			if(er)
				return er;
			return(sock_getsockopt(get_fs_long(args+0),
				get_fs_long(args+1),
				get_fs_long(args+2),
				(char *)get_fs_long(args+3),
				(int *)get_fs_long(args+4)));
		default:
			return(-EINVAL);
	}
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
	struct socket *sock;
	int i;

	printk("Swansea University Computer Society NET3.016\n");

	/*
	 *	Release all sockets. 
	 */
	for (sock = sockets; sock <= last_socket; ++sock)
		sock->state = SS_FREE;

	/*
	 *	Initialize all address (protocol) families. 
	 */
	 
	for (i = 0; i < NPROTO; ++i) pops[i] = NULL;

	/*
	 *	Initialize the protocols module. 
	 */

	proto_init();

#ifdef CONFIG_NET
	/* 
	 *	Initialize the DEV module. 
	 */

	dev_init();
  
	/*
	 *	And the bottom half handler 
	 */

	bh_base[NET_BH].routine= net_bh;
#endif  
  
}
