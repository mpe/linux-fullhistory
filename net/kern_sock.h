#ifndef _KERN_SOCK_H
#define _KERN_SOCK_H

#define NSOCKETS 128			/* should be dynamic, later... */

typedef enum {
	SS_FREE = 0,			/* not allocated */
	SS_UNCONNECTED,			/* unconnected to any socket */
	SS_CONNECTING,			/* in process of connecting */
	SS_CONNECTED,			/* connected to socket */
	SS_DISCONNECTING,		/* in process of disconnecting */
} socket_state;

#define SO_ACCEPTCON	(1<<16)		/* performed a listen */

/*
 * internel representation of a socket. not all the fields are used by
 * all configurations:
 *
 *		server			client
 * conn		client connected to	server connected to
 * iconn	list of clients		-unused-
 *		 awaiting connections
 * wait		sleep for clients,	sleep for connection,
 *		sleep for i/o		sleep for i/o
 */
struct socket {
	short type;			/* SOCK_STREAM, ... */
	socket_state state;
	long flags;
	struct proto_ops *ops;		/* protocols do most everything */
	char *data;			/* protocol data */
	struct socket *conn;		/* server socket connected to */
	struct socket *iconn;		/* incomplete client connections */
	struct socket *next;
	struct wait_queue **wait;	/* ptr to place to wait on */
	void *dummy;
};

struct proto_ops {
	int (*init)(void);
	int (*create)(struct socket *sock, int protocol);
	int (*dup)(struct socket *newsock, struct socket *oldsock);
	int (*release)(struct socket *sock, struct socket *peer);
	int (*bind)(struct socket *sock, struct sockaddr *umyaddr,
		    int sockaddr_len);
	int (*connect)(struct socket *sock, struct sockaddr *uservaddr,
		       int sockaddr_len);
	int (*socketpair)(struct socket *sock1, struct socket *sock2);
	int (*accept)(struct socket *sock, struct socket *newsock);
	int (*getname)(struct socket *sock, struct sockaddr *uaddr,
		       int *usockaddr_len, int peer);
	int (*read)(struct socket *sock, char *ubuf, int size, int nonblock);
	int (*write)(struct socket *sock, char *ubuf, int size, int nonblock);
	int (*select)(struct socket *sock, int sel_type, select_table * wait);
	int (*ioctl)(struct socket *sock, unsigned int cmd, unsigned long arg);
};

extern int sock_awaitconn(struct socket *mysock, struct socket *servsock);

#ifdef SOCK_DEBUG
#define PRINTK printk
#else
#define PRINTK (void)
#endif

#endif /* _KERN_SOCK_H */
