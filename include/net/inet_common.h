#ifndef _INET_COMMON_H
#define _INET_COMMON_H

extern struct proto_ops		inet_proto_ops;
extern struct sock *		tcp_sock_array[SOCK_ARRAY_SIZE];
extern struct sock *		udp_sock_array[SOCK_ARRAY_SIZE];


/*
 *	INET4 prototypes used by INET6
 */

extern void			inet_remove_sock(struct sock *sk1);
extern void			inet_put_sock(unsigned short num, 
					      struct sock *sk);
extern int			inet_release(struct socket *sock, 
					     struct socket *peer);
extern int			inet_connect(struct socket *sock, 
					     struct sockaddr * uaddr,
					     int addr_len, int flags);
extern int			inet_accept(struct socket *sock, 
					    struct socket *newsock, int flags);
extern int			inet_recvmsg(struct socket *sock, 
					     struct msghdr *ubuf, 
					     int size, int noblock, 
					     int flags, int *addr_len );
extern int			inet_sendmsg(struct socket *sock, 
					     struct msghdr *msg, 
					     int size, int noblock, 
					     int flags);
extern int			inet_shutdown(struct socket *sock, int how);
extern int			inet_select(struct socket *sock, int sel_type,
					    select_table *wait);
extern int			inet_setsockopt(struct socket *sock, int level,
						int optname, char *optval, 
						int optlen);
extern int			inet_getsockopt(struct socket *sock, int level,
						int optname, char *optval, 
						int *optlen);
extern int			inet_fcntl(struct socket *sock, 
					   unsigned int cmd, 
					   unsigned long arg);
extern int			inet_listen(struct socket *sock, int backlog);

#endif


