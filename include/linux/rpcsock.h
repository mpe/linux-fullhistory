/*
 *  rpcsock.h	Declarations for the RPC call interface.
 *
 *  Coypright (C) 1995 Olaf Kirch <okir@monad.swb.de>
 *
 */


#ifndef _LINUX_RPCSOCK_H
#define _LINUX_RPCSOCK_H

/* Maximum number of outstanding RPCs per socket.
 * With 32 slots, IP fragment reassembly would frequently
 * fail due to low memory.
 */
#define NRREQS		16

/* This describes a timeout strategy */
struct rpc_timeout {
	unsigned long		init_timeout,
				max_timeout,
				increment;
	int			retries;
	char			exponential;
};

/* Wait information */
struct rpc_wait {
	struct rpc_wait		*prev, *next;
	struct wait_queue	*wait;
	int			*buf;
	int			len;
	char			gotit;
	u32			xid;
};

struct rpc_sock {
	struct file		*file;
	struct socket		*sock;
	struct rpc_wait		waiting[NRREQS];
	struct rpc_wait		*head, *tail, *free;
	struct wait_queue	*backlog;
	struct wait_queue	*shutwait;
	int			shutdown;
};

#ifdef __KERNEL__

int			rpc_call(struct rpc_sock *, struct sockaddr *, int,
				const int *, int, int *, int,
				struct rpc_timeout *, int);
struct rpc_sock	*	rpc_makesock(struct file *);
int			rpc_closesock(struct rpc_sock *);

#endif /* __KERNEL__*/

#endif /* _LINUX_RPCSOCK_H */
