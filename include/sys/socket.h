#ifndef _SOCKET_H
#define _SOCKET_H

struct sockaddr {
	u_short sa_family;		/* address family, AF_xxx */
	char sa_data[14];		/* 14 bytes of protocol address */
};

/*
 * socket types
 */
#define SOCK_STREAM	1		/* stream (connection) socket */
#define SOCK_DGRAM	2		/* datagram (connectionless) socket */
#define SOCK_SEQPACKET	3		/* sequential packet socket */
#define SOCK_RAW	4		/* raw socket */

/*
 * supported address families
 */
#define AF_UNSPEC	0
#define AF_UNIX		1
#define AF_INET		2

/*
 * protocol families, same as address families
 */
#define PF_UNIX		AF_UNIX
#define PF_INET		AF_INET

int socket(int family, int type, int protocol);
int socketpair(int family, int type, int protocol, int sockvec[2]);
int bind(int sockfd, struct sockaddr *my_addr, int addrlen);
int connect(int sockfd, struct sockaddr *serv_addr, int addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *peer, int *paddrlen);
int getsockname(int sockfd, struct sockaddr *addr, int *paddrlen);
int getpeername(int sockfd, struct sockaddr *peer, int *paddrlen);

#endif /* _SOCKET_H */
