#ifndef _LINUX_SOCKET_H
#define _LINUX_SOCKET_H

#include <asm/socket.h>			/* arch-dependent defines	*/
#include <linux/sockios.h>		/* the SIOCxxx I/O controls	*/
#include <linux/uio.h>			/* iovec support		*/
#include <linux/types.h>		/* pid_t			*/

typedef unsigned short	sa_family_t;

/*
 *	1003.1g requires sa_family_t and that sa_data is char.
 */
 
struct sockaddr 
{
	sa_family_t	sa_family;	/* address family, AF_xxx	*/
	char		sa_data[14];	/* 14 bytes of protocol address	*/
};

struct linger {
	int		l_onoff;	/* Linger active		*/
	int		l_linger;	/* How long to linger for	*/
};

/*
 *	As we do 4.4BSD message passing we use a 4.4BSD message passing
 *	system, not 4.3. Thus msg_accrights(len) are now missing. They
 *	belong in an obscure libc emulation or the bin.
 */
 
struct msghdr 
{
	void	*	msg_name;	/* Socket name			*/
	int		msg_namelen;	/* Length of name		*/
	struct iovec *	msg_iov;	/* Data blocks			*/
	__kernel_size_t	msg_iovlen;	/* Number of blocks		*/
	void 	*	msg_control;	/* Per protocol magic (eg BSD file descriptor passing) */
	__kernel_size_t	msg_controllen;	/* Length of cmsg list */
	unsigned	msg_flags;
};

/*
 *	POSIX 1003.1g - ancillary data object information
 *	Ancillary data consits of a sequence of pairs of
 *	(cmsghdr, cmsg_data[])
 */

struct cmsghdr {
	__kernel_size_t	cmsg_len;	/* data byte count, including hdr */
        int		cmsg_level;	/* originating protocol */
        int		cmsg_type;	/* protocol-specific type */
	unsigned char	cmsg_data[0];
};

/*
 *	Ancilliary data object information MACROS
 *	Table 5-14 of POSIX 1003.1g
 */

#define CMSG_DATA(cmsg)		(cmsg)->cmsg_data
#define CMSG_NXTHDR(mhdr, cmsg) cmsg_nxthdr(mhdr, cmsg)

#define CMSG_ALIGN(len) ( ((len)+sizeof(long)-1) & ~(sizeof(long)-1) )

/* Stevens's Adv. API specifies CMSG_SPACE & CMSG_LENGTH,
 * I cannot understand, what the differenece? --ANK
 */

#define CMSG_SPACE(len) CMSG_ALIGN((len)+sizeof(struct cmsghdr))
#define CMSG_LENGTH(len) CMSG_ALIGN((len)+sizeof(struct cmsghdr))

#define	CMSG_FIRSTHDR(msg)	((msg)->msg_controllen >= sizeof(struct cmsghdr) ? \
				 (struct cmsghdr *)(msg)->msg_control : \
				 (struct cmsghdr *)NULL)

/*
 *	This mess will go away with glibc
 */
 
#ifdef __KERNEL__
#define KINLINE extern __inline__
#else
#define KINLINE static
#endif


/*
 *	Get the next cmsg header
 */
 
KINLINE struct cmsghdr * cmsg_nxthdr(struct msghdr *mhdr,
					       struct cmsghdr *cmsg)
{
	unsigned char * ptr;

	if (cmsg->cmsg_len < sizeof(struct cmsghdr))
	{
		return NULL;
	}
	ptr = ((unsigned char *) cmsg) +  CMSG_ALIGN(cmsg->cmsg_len);
	if (ptr >= (unsigned char *) mhdr->msg_control + mhdr->msg_controllen)
		return NULL;

	return (struct cmsghdr *) ptr;
}

/* "Socket"-level control message types: */

#define	SCM_RIGHTS	0x01		/* rw: access rights (array of int) */
#define SCM_CREDENTIALS 0x02		/* rw: struct ucred		*/
#define SCM_CONNECT	0x03		/* rw: struct scm_connect	*/

struct ucred
{
	__kernel_pid_t	pid;
	__kernel_uid_t	uid;
	__kernel_gid_t	gid;
};

/* Socket types. */

#define SOCK_STREAM	1		/* stream (connection) socket	*/
#define SOCK_DGRAM	2		/* datagram (conn.less) socket	*/
#define SOCK_RAW	3		/* raw socket			*/
#define SOCK_RDM	4		/* reliably-delivered message	*/
#define SOCK_SEQPACKET	5		/* sequential packet socket	*/
#define SOCK_PACKET	10		/* linux specific way of	*/
					/* getting packets at the dev	*/
					/* level.  For writing rarp and	*/
					/* other similar things on the	*/
					/* user level.			*/

/* Supported address families. */
#define AF_UNSPEC	0
#define AF_UNIX		1	/* Unix domain sockets 		*/
#define AF_LOCAL	1	/* POSIX name for AF_UNIX	*/
#define AF_INET		2	/* Internet IP Protocol 	*/
#define AF_AX25		3	/* Amateur Radio AX.25 		*/
#define AF_IPX		4	/* Novell IPX 			*/
#define AF_APPLETALK	5	/* Appletalk DDP 		*/
#define AF_NETROM	6	/* Amateur Radio NET/ROM 	*/
#define AF_BRIDGE	7	/* Multiprotocol bridge 	*/
#define AF_AAL5		8	/* Reserved for Werner's ATM 	*/
#define AF_X25		9	/* Reserved for X.25 project 	*/
#define AF_INET6	10	/* IP version 6			*/
#define AF_ROSE		11	/* Amateur Radio X.25 PLP	*/
#define AF_DECNET	12	/* Reserved for DECnet project	*/
#define AF_NETBEUI	13	/* Reserved for 802.2LLC project*/
#define AF_SECURITY	14	/* Security callback pseudo AF */
#define AF_MAX		32	/* For now.. */

/* Protocol families, same as address families. */
#define PF_UNSPEC	AF_UNSPEC
#define PF_UNIX		AF_UNIX
#define PF_LOCAL	AF_LOCAL
#define PF_INET		AF_INET
#define PF_AX25		AF_AX25
#define PF_IPX		AF_IPX
#define PF_APPLETALK	AF_APPLETALK
#define	PF_NETROM	AF_NETROM
#define PF_BRIDGE	AF_BRIDGE
#define PF_AAL5		AF_AAL5
#define PF_X25		AF_X25
#define PF_INET6	AF_INET6
#define PF_ROSE		AF_ROSE
#define PF_DECNET	AF_DECNET
#define PF_NETBEUI	AF_NETBEUI
#define PF_SECURITY	AF_SECURITY

#define PF_MAX		AF_MAX

/* Maximum queue length specifiable by listen.  */
#define SOMAXCONN	128

/* Flags we can use with send/ and recv. 
   Added those for 1003.1g not all are supported yet
 */
 
#define MSG_OOB		1
#define MSG_PEEK	2
#define MSG_DONTROUTE	4
#define MSG_CTRUNC	8
#define MSG_PROXY	0x10	/* Supply or ask second address. */
#define MSG_TRUNC	0x20
#define MSG_DONTWAIT	0x40	/* Nonblocking io		 */
#define MSG_EOR         0x80	/* End of record */
#define MSG_WAITALL	0x100	/* Wait for a full request */
#define MSG_FIN         0x200
#define MSG_SYN		0x400
#define MSG_URG		0x800
#define MSG_RST		0x1000

#define MSG_CTLIGNORE   0x80000000

#define MSG_EOF         MSG_FIN
#define MSG_CTLFLAGS	(MSG_OOB|MSG_URG|MSG_FIN|MSG_SYN|MSG_RST)


/* Setsockoptions(2) level. Thanks to BSD these must match IPPROTO_xxx */
#define SOL_IP		0
#define SOL_IPV6	41
#define SOL_ICMPV6	58
#define SOL_RAW		255
#define SOL_IPX		256
#define SOL_AX25	257
#define SOL_ATALK	258
#define SOL_NETROM	259
#define SOL_ROSE	260
#define SOL_DECNET	261
#define	SOL_X25		262
#define SOL_TCP		6
#define SOL_UDP		17

/* IPX options */
#define IPX_TYPE	1

/* TCP options - this way around because someone left a set in the c library includes */
#define TCP_NODELAY	1
#define TCP_MAXSEG	2

/* The various priorities. */
#define SOPRI_INTERACTIVE	0
#define SOPRI_NORMAL		1
#define SOPRI_BACKGROUND	2

#ifdef __KERNEL__
extern int memcpy_fromiovec(unsigned char *kdata, struct iovec *iov, int len);
extern int memcpy_fromiovecend(unsigned char *kdata, struct iovec *iov, 
				int offset, int len);
extern unsigned int csum_partial_copy_fromiovecend(unsigned char *kdata, 
						   struct iovec *iov, 
						   int offset, 
						   int len, int csum);

extern int verify_iovec(struct msghdr *m, struct iovec *iov, char *address, int mode);
extern int memcpy_toiovec(struct iovec *v, unsigned char *kdata, int len);
extern int move_addr_to_user(void *kaddr, int klen, void *uaddr, int *ulen);
extern int move_addr_to_kernel(void *uaddr, int ulen, void *kaddr);
extern void put_cmsg(struct msghdr*, int level, int type, int len, void *data);
#endif
#endif /* _LINUX_SOCKET_H */
