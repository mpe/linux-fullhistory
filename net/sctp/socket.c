/* Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines, Corp.
 * Copyright (c) 2001-2003 Intel Corp.
 * Copyright (c) 2001-2002 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * These functions interface with the sockets layer to implement the
 * SCTP Extensions for the Sockets API.
 *
 * Note that the descriptions from the specification are USER level
 * functions--this file is the functions which populate the struct proto
 * for SCTP which is the BOTTOM of the sockets interface.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Narasimha Budihal     <narsi@refcode.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Daisy Chang           <daisyc@us.ibm.com>
 *    Sridhar Samudrala     <samudrala@us.ibm.com>
 *    Inaky Perez-Gonzalez  <inaky.gonzalez@intel.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/ip.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>

#include <net/ip.h>
#include <net/icmp.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/inet_common.h>

#include <linux/socket.h> /* for sa_family_t */
#include <net/sock.h>
#include <net/sctp/sctp.h>

/* WARNING:  Please do not remove the SCTP_STATIC attribute to
 * any of the functions below as they are used to export functions
 * used by a project regression testsuite.
 */

/* Forward declarations for internal helper functions. */
static int sctp_writeable(struct sock *sk);
static inline int sctp_wspace(struct sctp_association *asoc);
static inline void sctp_set_owner_w(sctp_chunk_t *chunk);
static void sctp_wfree(struct sk_buff *skb);
static int sctp_wait_for_sndbuf(struct sctp_association *, long *timeo_p,
				int msg_len);
static int sctp_wait_for_packet(struct sock * sk, int *err, long *timeo_p);
static int sctp_wait_for_connect(struct sctp_association *, long *timeo_p);
static int sctp_wait_for_accept(struct sock *sk, long timeo);
static inline int sctp_verify_addr(struct sock *, union sctp_addr *, int);
static int sctp_bindx_add(struct sock *, struct sockaddr_storage *, int);
static int sctp_bindx_rem(struct sock *, struct sockaddr_storage *, int);
static int sctp_do_bind(struct sock *, union sctp_addr *, int);
static int sctp_autobind(struct sock *sk);
static void sctp_sock_migrate(struct sock *, struct sock *,
			      struct sctp_association *, sctp_socket_type_t);

/* Look up the association by its id.  If this is not a UDP-style
 * socket, the ID field is always ignored.
 */
sctp_association_t *sctp_id2assoc(struct sock *sk, sctp_assoc_t id)
{
	sctp_association_t *asoc = NULL;

	/* If this is not a UDP-style socket, assoc id should be 
	 * ignored.
	 */
	if (SCTP_SOCKET_UDP != sctp_sk(sk)->type) {
		if (!list_empty(&sctp_sk(sk)->ep->asocs))
			asoc = list_entry(sctp_sk(sk)->ep->asocs.next,
					  sctp_association_t, asocs);
		return asoc;
	}

	/* First, verify that this is a kernel address. */
	if (sctp_is_valid_kaddr((unsigned long) id)) {
		sctp_association_t *temp = (sctp_association_t *) id;

		/* Verify that this _is_ an sctp_association_t
		 * data structure and if so, that the socket matches.
		 */
		if ((SCTP_ASSOC_EYECATCHER == temp->eyecatcher) &&
		    (temp->base.sk == sk))
			asoc = temp;
	}

	return asoc;
}

/* API 3.1.2 bind() - UDP Style Syntax
 * The syntax of bind() is,
 *
 *   ret = bind(int sd, struct sockaddr *addr, int addrlen);
 *
 *   sd      - the socket descriptor returned by socket().
 *   addr    - the address structure (struct sockaddr_in or struct
 *             sockaddr_in6 [RFC 2553]),
 *   addrlen - the size of the address structure.
 *
 * The caller should use struct sockaddr_storage described in RFC 2553
 * to represent addr for portability reason.
 */
int sctp_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	int retval = 0;

	sctp_lock_sock(sk);

	SCTP_DEBUG_PRINTK("sctp_bind(sk: %p, uaddr: %p, addr_len: %d)\n",
			  sk, uaddr, addr_len);

	/* Disallow binding twice. */
	if (!sctp_sk(sk)->ep->base.bind_addr.port)
		retval = sctp_do_bind(sk, (union sctp_addr *)uaddr,
				      addr_len);
	else
		retval = -EINVAL;

	sctp_release_sock(sk);

	return retval;
}

static long sctp_get_port_local(struct sock *, union sctp_addr *);

/* Verify this is a valid sockaddr. */
static struct sctp_af *sctp_sockaddr_af(struct sctp_opt *opt,
					union sctp_addr *addr, int len)
{
	struct sctp_af *af;

	/* Check minimum size.  */
	if (len < sizeof (struct sockaddr))
		return NULL;

	/* Does this PF support this AF? */
	if (!opt->pf->af_supported(addr->sa.sa_family))
		return NULL;

	/* If we get this far, af is valid. */
	af = sctp_get_af_specific(addr->sa.sa_family);

	if (len < af->sockaddr_len)
		return NULL;

	return af;
}


/* Bind a local address either to an endpoint or to an association.  */
SCTP_STATIC int sctp_do_bind(struct sock *sk, union sctp_addr *addr, int len)
{
	struct sctp_opt *sp = sctp_sk(sk);
	sctp_endpoint_t *ep = sp->ep;
	sctp_bind_addr_t *bp = &ep->base.bind_addr;
	struct sctp_af *af;
	unsigned short snum;
	int ret = 0;

	SCTP_DEBUG_PRINTK("sctp_do_bind(sk: %p, newaddr: %p, len: %d)\n",
			  sk, addr, len);

	/* Common sockaddr verification. */
	af = sctp_sockaddr_af(sp, addr, len);
	if (!af)
		return -EINVAL;

	/* PF specific bind() address verification. */
	if (!sp->pf->bind_verify(sp, addr))
		return -EADDRNOTAVAIL;

	snum= ntohs(addr->v4.sin_port);

	SCTP_DEBUG_PRINTK("sctp_do_bind: port: %d, new port: %d\n",
			  bp->port, snum);

	/* We must either be unbound, or bind to the same port.  */
	if (bp->port && (snum != bp->port)) {
		SCTP_DEBUG_PRINTK("sctp_do_bind:"
				  " New port %d does not match existing port "
				  "%d.\n", snum, bp->port);
		return -EINVAL;
	}

	if (snum && snum < PROT_SOCK && !capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	/* Make sure we are allowed to bind here.
	 * The function sctp_get_port_local() does duplicate address
	 * detection.
	 */
	if ((ret = sctp_get_port_local(sk, addr))) {
		if (ret == (long) sk) {
			/* This endpoint has a conflicting address. */
			return -EINVAL;
		} else {
			return -EADDRINUSE;
		}
	}

	/* Refresh ephemeral port.  */
	if (!snum)
		snum = inet_sk(sk)->num;




	/* Add the address to the bind address list.  */
	sctp_local_bh_disable();
	sctp_write_lock(&ep->base.addr_lock);

	/* Use GFP_ATOMIC since BHs are disabled.  */
	addr->v4.sin_port = ntohs(addr->v4.sin_port);
	ret = sctp_add_bind_addr(bp, addr, GFP_ATOMIC);
	addr->v4.sin_port = htons(addr->v4.sin_port);
	if (!ret && !bp->port)
		bp->port = snum;

	sctp_write_unlock(&ep->base.addr_lock);
	sctp_local_bh_enable();

	/* Copy back into socket for getsockname() use. */
	if (!ret) {
		inet_sk(sk)->sport = htons(inet_sk(sk)->num);
		af->to_sk(addr, sk);
	}

	return ret;
}

/* API 8.1 sctp_bindx()
 *
 * The syntax of sctp_bindx() is,
 *
 *   ret = sctp_bindx(int sd,
 *                    struct sockaddr_storage *addrs,
 * 		      int addrcnt,
 * 		      int flags);
 *
 * If sd is an IPv4 socket, the addresses passed must be IPv4 addresses.
 * If the sd is an IPv6 socket, the addresses passed can either be IPv4
 * or IPv6 addresses.
 *
 * A single address may be specified as INADDR_ANY or IPV6_ADDR_ANY, see
 * section 3.1.2 for this usage.
 *
 * addrs is a pointer to an array of one or more socket addresses.  Each
 * address is contained in a struct sockaddr_storage, so each address is
 * fixed length. The caller specifies the number of addresses in the
 * array with addrcnt.
 *
 * On success, sctp_bindx() returns 0. On failure, sctp_bindx() returns -1,
 * and sets errno to the appropriate error code. [ Editor's note: need
 * to fill in all error code? ]
 *
 * For SCTP, the port given in each socket address must be the same, or
 * sctp_bindx() will fail, setting errno to EINVAL .
 *
 * The flags parameter is formed from the bitwise OR of zero or
 * more of the following currently defined flags:
 *
 *     SCTP_BINDX_ADD_ADDR
 *     SCTP_BINDX_REM_ADDR
 *
 * SCTP_BIND_ADD_ADDR directs SCTP to add the given addresses to the
 * association, and SCTP_BIND_REM_ADDR directs SCTP to remove the given
 * addresses from the association. The two flags are mutually exclusive;
 * if both are given, sctp_bindx() will fail with EINVAL.  A caller may not
 * remove all addresses from an association; sctp_bindx() will reject such
 * an attempt with EINVAL.
 *
 * An application can use sctp_bindx(SCTP_BINDX_ADD_ADDR) to associate
 * additional addresses with an endpoint after calling bind().  Or use
 * sctp_bindx(SCTP_BINDX_REM_ADDR) to remove some addresses a listening
 * socket is associated with so that no new association accepted will be
 * associated with those addresses.
 *
 * SCTP_BIND_ADD_ADDR is defined as 0, so that it becomes the default
 * behavior for sctp_bindx() when no flags are given.
 *
 * Adding and removing addresses from a connected association is optional
 * functionality. Implementations that do not support this functionality
 * should return EOPNOTSUPP.
 *
 * NOTE: This could be integrated into sctp_setsockopt_bindx(),
 * but keeping it this way makes it easier if sometime sys_bindx is
 * added.
 */

/* Unprotected by locks. Call only with socket lock sk->lock held! See
 * sctp_bindx() for a lock-protected call.
 */

static int __sctp_bindx(struct sock *sk, struct sockaddr_storage *addrs,
			int addrcnt, int flags)
{
	int retval = 0;

	SCTP_DEBUG_PRINTK("__sctp_bindx(sk: %p, addrs: %p, addrcnt: %d, "
			  "flags: %s)\n", sk, addrs, addrcnt,
			  (BINDX_ADD_ADDR == flags) ? "ADD" :
			  ((BINDX_REM_ADDR == flags) ? "REM" : "BOGUS"));

	switch (flags) {
	case BINDX_ADD_ADDR:
		retval = sctp_bindx_add(sk, addrs, addrcnt);
		break;

	case BINDX_REM_ADDR:
		retval = sctp_bindx_rem(sk, addrs, addrcnt);
		break;

	default:
		retval = -EINVAL;
		break;
        };

	return retval;
}

/* BINDX with locks.
 *
 * NOTE: Currently unused at all ...
 */
int sctp_bindx(struct sock *sk, struct sockaddr_storage *addrs, int addrcnt,
	       int flags)
{
	int retval;

	sctp_lock_sock(sk);
	retval = __sctp_bindx(sk, addrs, addrcnt, flags);
	sctp_release_sock(sk);

	return retval;
}

/* Add a list of addresses as bind addresses to local endpoint or
 * association.
 *
 * Basically run through each address specified in the addrs/addrcnt
 * array/length pair, determine if it is IPv6 or IPv4 and call
 * sctp_do_bind() on it.
 *
 * If any of them fails, then the operation will be reversed and the
 * ones that were added will be removed.
 *
 * Only __sctp_bindx() is supposed to call this function.
 */
int sctp_bindx_add(struct sock *sk, struct sockaddr_storage *addrs, int addrcnt)
{
	int cnt;
	int retval = 0;
	int addr_len;

	SCTP_DEBUG_PRINTK("sctp_bindx_add (sk: %p, addrs: %p, addrcnt: %d)\n",
			  sk, addrs, addrcnt);

	for (cnt = 0; cnt < addrcnt; cnt++) {
		/* The list may contain either IPv4 or IPv6 address;
		 * determine the address length for walking thru the list.
		 */
		switch (((struct sockaddr *)&addrs[cnt])->sa_family) {
		case AF_INET:
			addr_len = sizeof(struct sockaddr_in);
			break;

		case AF_INET6:
			addr_len = sizeof(struct sockaddr_in6);
			break;

		default:
			retval = -EINVAL;
			goto err_bindx_add;
		};

		retval = sctp_do_bind(sk, (union sctp_addr *)&addrs[cnt],
				      addr_len);

err_bindx_add:
		if (retval < 0) {
			/* Failed. Cleanup the ones that has been added */
			if (cnt > 0)
				sctp_bindx_rem(sk, addrs, cnt);
			return retval;
		}
	}

	/* Notify the peer(s), assuming we have (an) association(s).
	 * FIXME: for UDP, we have a 1-1-many mapping amongst sk, ep and asoc,
	 *        so we don't have to do much work on locating associations.
	 *
	 * However, when the separation of ep and asoc kicks in, especially
	 * for TCP style connection, it becomes n-1-n mapping.  We will need
	 * to do more fine work.  Until then, hold my peace.
	 *							--xguo
	 *
	 * Really, I don't think that will be a problem.  The bind()
	 * call on a socket will either know the endpoint
	 * (e.g. TCP-style listen()ing socket, or UDP-style socket),
	 * or exactly one association.  The former case is EXACTLY
	 * what we have now.  In the former case we know the
	 * association already.					--piggy
	 *
	 * This code will be working on either a UDP style or a TCP style
	 * socket, or say either an endpoint or an association. The socket
	 * type verification code need to be added later before calling the
	 * ADDIP code.
	 * 							--daisy
	 */

#if CONFIG_IP_SCTP_ADDIP
	/* Add these addresses to all associations on this endpoint.  */
	if (retval >= 0) {
		struct list_head *pos;
		sctp_endpoint_t *ep;
		sctp_association_t *asoc;
		ep = sctp_sk(sk)->ep;

		list_for_each(pos, &ep->asocs) {
			asoc = list_entry(pos, sctp_association_t, asocs);

			sctp_addip_addr_config(asoc,
					       SCTP_PARAM_ADD_IP,
					       addrs, addrcnt);
		}
	}
#endif

	return retval;
}

/* Remove a list of addresses from bind addresses list.  Do not remove the
 * last address.
 *
 * Basically run through each address specified in the addrs/addrcnt
 * array/length pair, determine if it is IPv6 or IPv4 and call
 * sctp_del_bind() on it.
 *
 * If any of them fails, then the operation will be reversed and the
 * ones that were removed will be added back.
 *
 * At least one address has to be left; if only one address is
 * available, the operation will return -EBUSY.
 *
 * Only __sctp_bindx() is supposed to call this function.
 */
int sctp_bindx_rem(struct sock *sk, struct sockaddr_storage *addrs, int addrcnt)
{
	struct sctp_opt *sp = sctp_sk(sk);
	sctp_endpoint_t *ep = sp->ep;
	int cnt;
	sctp_bind_addr_t *bp = &ep->base.bind_addr;
	int retval = 0;
	union sctp_addr saveaddr;

	SCTP_DEBUG_PRINTK("sctp_bindx_rem (sk: %p, addrs: %p, addrcnt: %d)\n",
			  sk, addrs, addrcnt);

	for (cnt = 0; cnt < addrcnt; cnt++) {
		/* If there is only one bind address, there is nothing more
		 * to be removed (we need at least one address here).
		 */
		if (list_empty(&bp->address_list)) {
			retval = -EBUSY;
			goto err_bindx_rem;
		}

		/* The list may contain either IPv4 or IPv6 address;
		 * determine the address length for walking thru the list.
		 */
		switch (((struct sockaddr *)&addrs[cnt])->sa_family) {
		case AF_INET:
			saveaddr = *((union sctp_addr *)
				     &addrs[cnt]);
			saveaddr.v4.sin_port = ntohs(saveaddr.v4.sin_port);
			/* Verify the port.  */
			if (saveaddr.v4.sin_port != bp->port) {
				retval = -EINVAL;
				goto err_bindx_rem;
			}
			break;

		case AF_INET6:
			saveaddr = *((union sctp_addr *)
				     &addrs[cnt]);
			saveaddr.v6.sin6_port =
				ntohs(saveaddr.v6.sin6_port);
			/* verify the port */
			if (saveaddr.v6.sin6_port != bp->port) {
				retval = -EINVAL;
				goto err_bindx_rem;
			}
			break;

		default:
			retval = -EINVAL;
			goto err_bindx_rem;
		};

		/* FIXME - There is probably a need to check if sk->saddr and
		 * sk->rcv_addr are currently set to one of the addresses to
		 * be removed. This is something which needs to be looked into
		 * when we are fixing the outstanding issues with multi-homing
		 * socket routing and failover schemes. Refer to comments in
		 * sctp_do_bind(). -daisy
		 */
		sctp_local_bh_disable();
		sctp_write_lock(&ep->base.addr_lock);

		retval = sctp_del_bind_addr(bp, &saveaddr);

		sctp_write_unlock(&ep->base.addr_lock);
		sctp_local_bh_enable();

err_bindx_rem:
		if (retval < 0) {
			/* Failed. Add the ones that has been removed back */
			if (cnt > 0)
				sctp_bindx_add(sk, addrs, cnt);
			return retval;
		}
	}

	/*
	 * This code will be working on either a UDP style or a TCP style
	 * socket, * or say either an endpoint or an association. The socket
	 * type verification code need to be added later before calling the
	 * ADDIP code.
	 * 							--daisy
	 */
#if CONFIG_IP_SCTP_ADDIP
	/* Remove these addresses from all associations on this endpoint.  */
	if (retval >= 0) {
		struct list_head *pos;
		sctp_endpoint_t *ep;
		sctp_association_t *asoc;

		ep = sctp_sk(sk)->ep;
		list_for_each(pos, &ep->asocs) {
			asoc = list_entry(pos, sctp_association_t, asocs);
			sctp_addip_addr_config(asoc, SCTP_PARAM_DEL_IP,
					       addrs, addrcnt);
		}
	}
#endif
	return retval;
}

/* Helper for tunneling sys_bindx() requests through sctp_setsockopt()
 *
 * Basically do nothing but copying the addresses from user to kernel
 * land and invoking sctp_bindx on the sk. This is used for tunneling
 * the sctp_bindx() [sys_bindx()] request through sctp_setsockopt()
 * from userspace.
 *
 * Note I don't use move_addr_to_kernel(): the reason is we would be
 * iterating over an array of struct sockaddr_storage passing always
 * what we know is a good size (sizeof (struct sock...)), so it is
 * pointless. Instead check the whole area for read access and copy
 * it.
 *
 * We don't use copy_from_user() for optimization: we first do the
 * sanity checks (buffer size -fast- and access check-healthy
 * pointer); if all of those succeed, then we can alloc the memory
 * (expensive operation) needed to copy the data to kernel. Then we do
 * the copying without checking the user space area
 * (__copy_from_user()).
 *
 * On exit there is no need to do sockfd_put(), sys_setsockopt() does
 * it.
 *
 * sk        The sk of the socket
 * addrs     The pointer to the addresses in user land
 * addrssize Size of the addrs buffer
 * op        Operation to perform (add or remove, see the flags of
 *           sctp_bindx)
 *
 * Returns 0 if ok, <0 errno code on error.
 */
SCTP_STATIC int sctp_setsockopt_bindx(struct sock* sk,
				      struct sockaddr_storage *addrs,
				      int addrssize, int op)
{
	struct sockaddr_storage *kaddrs;
	int err;
	size_t addrcnt;

	SCTP_DEBUG_PRINTK("sctp_do_setsocktopt_bindx: sk %p addrs %p"
			  " addrssize %d opt %d\n", sk, addrs, addrssize, op);

	/* Do we have an integer number of structs sockaddr_storage?  */
	if (unlikely(addrssize <= 0 ||
		     addrssize % sizeof(struct sockaddr_storage) != 0))
		return -EINVAL;

	/* Check the user passed a healthy pointer.  */
	if (unlikely(!access_ok(VERIFY_READ, addrs, addrssize)))
		return -EFAULT;

	/* Alloc space for the address array in kernel memory.  */
	kaddrs = (struct sockaddr_storage *) kmalloc(addrssize, GFP_KERNEL);
	if (unlikely(NULL == kaddrs))
		return -ENOMEM;

	if (copy_from_user(kaddrs, addrs, addrssize)) {
		kfree(kaddrs);
		return -EFAULT;
	}

	addrcnt = addrssize / sizeof(struct sockaddr_storage);
	err = __sctp_bindx(sk, kaddrs, addrcnt, op);   /* Do the work. */
	kfree(kaddrs);

	return err;
}

/* API 3.1.4 close() - UDP Style Syntax
 * Applications use close() to perform graceful shutdown (as described in
 * Section 10.1 of [SCTP]) on ALL the associations currently represented
 * by a UDP-style socket.
 *
 * The syntax is
 *
 *   ret = close(int sd);
 *
 *   sd      - the socket descriptor of the associations to be closed.
 *
 * To gracefully shutdown a specific association represented by the
 * UDP-style socket, an application should use the sendmsg() call,
 * passing no user data, but including the appropriate flag in the
 * ancillary data (see Section xxxx).
 *
 * If sd in the close() call is a branched-off socket representing only
 * one association, the shutdown is performed on that association only.
 */
SCTP_STATIC void sctp_close(struct sock *sk, long timeout)
{
	sctp_endpoint_t *ep;
	sctp_association_t *asoc;
	struct list_head *pos, *temp;

	SCTP_DEBUG_PRINTK("sctp_close(sk: 0x%p...)\n", sk);

	sctp_lock_sock(sk);
	sk->shutdown = SHUTDOWN_MASK;

	ep = sctp_sk(sk)->ep;

	/* Walk all associations on a socket, not on an endpoint.  */
	list_for_each_safe(pos, temp, &ep->asocs) {
		asoc = list_entry(pos, sctp_association_t, asocs);
		sctp_primitive_SHUTDOWN(asoc, NULL);
	}

	/* Clean up any skbs sitting on the receive queue.  */
	skb_queue_purge(&sk->receive_queue);
	skb_queue_purge(&sctp_sk(sk)->pd_lobby);

	/* This will run the backlog queue.  */
	sctp_release_sock(sk);

	/* Supposedly, no process has access to the socket, but
	 * the net layers still may.
	 */
	sctp_local_bh_disable();
	sctp_bh_lock_sock(sk);

	/* Hold the sock, since inet_sock_release() will put sock_put()
	 * and we have just a little more cleanup.
	 */
	sock_hold(sk);
	inet_sock_release(sk);

	sctp_bh_unlock_sock(sk);
	sctp_local_bh_enable();

	sock_put(sk);

	SCTP_DBG_OBJCNT_DEC(sock);
}

/* API 3.1.3 sendmsg() - UDP Style Syntax
 *
 * An application uses sendmsg() and recvmsg() calls to transmit data to
 * and receive data from its peer.
 *
 *  ssize_t sendmsg(int socket, const struct msghdr *message,
 *                  int flags);
 *
 *  socket  - the socket descriptor of the endpoint.
 *  message - pointer to the msghdr structure which contains a single
 *            user message and possibly some ancillary data.
 *
 *            See Section 5 for complete description of the data
 *            structures.
 *
 *  flags   - flags sent or received with the user message, see Section
 *            5 for complete description of the flags.
 *
 * Note:  This function could use a rewrite especially when explicit
 * connect support comes in.
 */
/* BUG:  We do not implement the equivalent of wait_for_tcp_memory(). */

SCTP_STATIC int sctp_msghdr_parse(const struct msghdr *, sctp_cmsgs_t *);

SCTP_STATIC int sctp_sendmsg(struct kiocb *iocb, struct sock *sk,
			     struct msghdr *msg, int msg_len)
{
	struct sctp_opt *sp;
	sctp_endpoint_t *ep;
	sctp_association_t *new_asoc=NULL, *asoc=NULL;
	struct sctp_transport *transport;
	sctp_chunk_t *chunk = NULL;
	union sctp_addr to;
	struct sockaddr *msg_name = NULL;
	struct sctp_sndrcvinfo default_sinfo = { 0 };
	struct sctp_sndrcvinfo *sinfo;
	struct sctp_initmsg *sinit;
	sctp_assoc_t associd = NULL;
	sctp_cmsgs_t cmsgs = { 0 };
	int err;
	sctp_scope_t scope;
	long timeo;
	__u16 sinfo_flags = 0;
	struct sk_buff_head chunks;

	SCTP_DEBUG_PRINTK("sctp_sendmsg(sk: %p, msg: %p, msg_len: %d)\n",
			  sk, msg, msg_len);

	err = 0;
	sp = sctp_sk(sk);
	ep = sp->ep;

	SCTP_DEBUG_PRINTK("Using endpoint: %s.\n", ep->debug_name);

	/* Parse out the SCTP CMSGs.  */
	err = sctp_msghdr_parse(msg, &cmsgs);

	if (err) {
		SCTP_DEBUG_PRINTK("msghdr parse err = %x\n", err);
		goto out_nounlock;
	}

	/* Fetch the destination address for this packet.  This
	 * address only selects the association--it is not necessarily
	 * the address we will send to.
	 * For a peeled-off socket, msg_name is ignored.
	 */
	if ((SCTP_SOCKET_UDP_HIGH_BANDWIDTH != sp->type) && msg->msg_name) {
		int msg_namelen = msg->msg_namelen;

		err = sctp_verify_addr(sk, (union sctp_addr *)msg->msg_name,
				       msg_namelen);
		if (err)
			return err;

		if (msg_namelen > sizeof(to))
			msg_namelen = sizeof(to);
		memcpy(&to, msg->msg_name, msg_namelen);
		SCTP_DEBUG_PRINTK("Just memcpy'd. msg_name is "
				  "0x%x:%u.\n",
				  to.v4.sin_addr.s_addr, to.v4.sin_port);

		to.v4.sin_port = ntohs(to.v4.sin_port);
		msg_name = msg->msg_name;
	}

	sinfo = cmsgs.info;
	sinit = cmsgs.init;

	/* Did the user specify SNDRCVINFO?  */
	if (sinfo) {
		sinfo_flags = sinfo->sinfo_flags;
		associd = sinfo->sinfo_assoc_id;
	}

	SCTP_DEBUG_PRINTK("msg_len: %Zd, sinfo_flags: 0x%x\n",
			  msg_len, sinfo_flags);

	/* If MSG_EOF is set, no data can be sent. Disallow sending zero
	 * length messages when MSG_EOF|MSG_ABORT is not set.
	 * If MSG_ABORT is set, the message length could be non zero with
	 * the msg_iov set to the user abort reason.
 	 */
	if (((sinfo_flags & MSG_EOF) && (msg_len > 0)) ||
	    (!(sinfo_flags & (MSG_EOF|MSG_ABORT)) && (msg_len == 0))) {
		err = -EINVAL;
		goto out_nounlock;
	}

	sctp_lock_sock(sk);

	transport = NULL;

	SCTP_DEBUG_PRINTK("About to look up association.\n");

	/* If a msg_name has been specified, assume this is to be used.  */
	if (msg_name) {
		/* Look for a matching association on the endpoint. */
		asoc = sctp_endpoint_lookup_assoc(ep, &to, &transport);
		if (!asoc) {
			/* If we could not find a matching association on the
			 * endpoint, make sure that there is no peeled-off
			 * association on another socket.
			 */
			if (sctp_endpoint_is_peeled_off(ep, &to)) {
				err = -EADDRNOTAVAIL;
				goto out_unlock;
			}
		}
	} else {
		asoc = sctp_id2assoc(sk, associd);
		if (!asoc) {
			err = -EINVAL;
			goto out_unlock;
		}
	}

	if (asoc) {
		SCTP_DEBUG_PRINTK("Just looked up association: "
				  "%s. \n", asoc->debug_name);
		if (sinfo_flags & MSG_EOF) {
			SCTP_DEBUG_PRINTK("Shutting down association: %p\n",
					  asoc);
			sctp_primitive_SHUTDOWN(asoc, NULL);
			err = 0;
			goto out_unlock;
		}
		if (sinfo_flags & MSG_ABORT) {
			SCTP_DEBUG_PRINTK("Aborting association: %p\n", asoc);
			sctp_primitive_ABORT(asoc, msg);
			err = 0;
			goto out_unlock;
		}
	}

	/* Do we need to create the association?  */
	if (!asoc) {
		SCTP_DEBUG_PRINTK("There is no association yet.\n");

		/* Check for invalid stream against the stream counts,
		 * either the default or the user specified stream counts.
		 */
		if (sinfo) {
			if (!sinit || (sinit && !sinit->sinit_num_ostreams)) {
				/* Check against the defaults. */
				if (sinfo->sinfo_stream >=
				    sp->initmsg.sinit_num_ostreams) {
					err = -EINVAL;
					goto out_unlock;
				}
			} else {
				/* Check against the requested.  */
				if (sinfo->sinfo_stream >=
				    sinit->sinit_num_ostreams) {
					err = -EINVAL;
					goto out_unlock;
				}
			}
		}

		/*
		 * API 3.1.2 bind() - UDP Style Syntax
		 * If a bind() or sctp_bindx() is not called prior to a
		 * sendmsg() call that initiates a new association, the
		 * system picks an ephemeral port and will choose an address
		 * set equivalent to binding with a wildcard address.
		 */
		if (!ep->base.bind_addr.port) {
			if (sctp_autobind(sk)) {
				err = -EAGAIN;
				goto out_unlock;
			}
		}

		scope = sctp_scope(&to);
		new_asoc = sctp_association_new(ep, sk, scope, GFP_KERNEL);
		if (!new_asoc) {
			err = -ENOMEM;
			goto out_unlock;
		}
		asoc = new_asoc;

		/* If the SCTP_INIT ancillary data is specified, set all
		 * the association init values accordingly.
		 */
		if (sinit) {
			if (sinit->sinit_num_ostreams) {
				asoc->c.sinit_num_ostreams =
					sinit->sinit_num_ostreams;
			}
			if (sinit->sinit_max_instreams) {
				asoc->c.sinit_max_instreams =
					sinit->sinit_max_instreams;
			}
			if (sinit->sinit_max_attempts) {
				asoc->max_init_attempts
					= sinit->sinit_max_attempts;
			}
			if (sinit->sinit_max_init_timeo) {
				asoc->max_init_timeo
					= sinit->sinit_max_init_timeo * HZ;
			}
		}

		/* Prime the peer's transport structures.  */
		transport = sctp_assoc_add_peer(asoc, &to, GFP_KERNEL);
		if (!transport) {
			err = -ENOMEM;
			goto out_free;
		}
		err = sctp_assoc_set_bind_addr_from_ep(asoc, GFP_KERNEL);
		if (err < 0) {
			err = -ENOMEM;
			goto out_free;
		}
	}

	/* ASSERT: we have a valid association at this point.  */
	SCTP_DEBUG_PRINTK("We have a valid association.\n");

	if (!sinfo) {
		/* If the user didn't specify SNDRCVINFO, make up one with
		 * some defaults.
		 */
		default_sinfo.sinfo_stream = asoc->defaults.stream;
		default_sinfo.sinfo_flags = asoc->defaults.flags;
		default_sinfo.sinfo_ppid = asoc->defaults.ppid;
		default_sinfo.sinfo_context = asoc->defaults.context;
		default_sinfo.sinfo_timetolive = asoc->defaults.timetolive;
		default_sinfo.sinfo_assoc_id = sctp_assoc2id(asoc);
		sinfo = &default_sinfo;
	}

	/* API 7.1.7, the sndbuf size per association bounds the
	 * maximum size of data that can be sent in a single send call.
	 */
	if (msg_len > sk->sndbuf) {
		err = -EMSGSIZE;
		goto out_free;
	}

	/* If fragmentation is disabled and the message length exceeds the
	 * association fragmentation point, return EMSGSIZE.  The I-D
	 * does not specify what this error is, but this looks like
	 * a great fit.
	 */
	if (sctp_sk(sk)->disable_fragments && (msg_len > asoc->frag_point)) {
		err = -EMSGSIZE;
		goto out_free;
	}

	if (sinfo) {
		/* Check for invalid stream. */
		if (sinfo->sinfo_stream >= asoc->c.sinit_num_ostreams) {
			err = -EINVAL;
			goto out_free;
		}
	}

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);
	if (!sctp_wspace(asoc)) {
		err = sctp_wait_for_sndbuf(asoc, &timeo, msg_len);
		if (err)
			goto out_free;
	}

	/* Break the message into multiple chunks of maximum size. */
	skb_queue_head_init(&chunks);
	err = sctp_datachunks_from_user(asoc, sinfo, msg, msg_len, &chunks);
	if (err)
		goto out_free;

	/* Auto-connect, if we aren't connected already. */
	if (SCTP_STATE_CLOSED == asoc->state) {
		err = sctp_primitive_ASSOCIATE(asoc, NULL);
		if (err < 0)
			goto out_free;
		SCTP_DEBUG_PRINTK("We associated primitively.\n");
	}

	/* Now send the (possibly) fragmented message. */
	while ((chunk = (sctp_chunk_t *)__skb_dequeue(&chunks))) {

		/* Do accounting for the write space.  */
		sctp_set_owner_w(chunk);

		/* This flag, in the UDP model, requests the SCTP stack to
		 * override the primary destination address with the
		 * address found with the sendto/sendmsg call.
		 */
		if (sinfo_flags & MSG_ADDR_OVER) {
			if (!msg->msg_name) {
				err = -EINVAL;
				goto out_free;
			}
			chunk->transport = sctp_assoc_lookup_paddr(asoc, &to);
			if (!chunk->transport) {
				err = -EINVAL;
				goto out_free;
			}
		}

		/* Send it to the lower layers.  */
		sctp_primitive_SEND(asoc, chunk);
		SCTP_DEBUG_PRINTK("We sent primitively.\n");
	}

	if (!err) {
		err = msg_len;
		goto out_unlock;
	}
	/* If we are already past ASSOCIATE, the lower
	 * layers are responsible for association cleanup.
	 */
	goto out_free_chunk;

out_free:
	if (new_asoc)
		sctp_association_free(asoc);

out_free_chunk:
	if (chunk)
		sctp_free_chunk(chunk);

out_unlock:
	sctp_release_sock(sk);

out_nounlock:
	return err;

#if 0
do_sock_err:
	if (msg_len)
		err = msg_len;
	else
		err = sock_error(sk);
	goto out;

do_interrupted:
	if (msg_len)
		err = msg_len;
	goto out;
#endif /* 0 */
}

/* This is an extended version of skb_pull() that removes the data from the
 * start of a skb even when data is spread across the list of skb's in the
 * frag_list. len specifies the total amount of data that needs to be removed.
 * when 'len' bytes could be removed from the skb, it returns 0.
 * If 'len' exceeds the total skb length,  it returns the no. of bytes that
 * could not be removed.
 */
static int sctp_skb_pull(struct sk_buff *skb, int len)
{
	struct sk_buff *list;
	int skb_len = skb_headlen(skb);
	int rlen;

	if (len <= skb_len) {
		__skb_pull(skb, len);
		return 0;
	}
	len -= skb_len;
	__skb_pull(skb, skb_len);

	for (list = skb_shinfo(skb)->frag_list; list; list = list->next) {
		rlen = sctp_skb_pull(list, len);
		skb->len -= (len-rlen);
		skb->data_len -= (len-rlen);

		if (!rlen)
			return 0;

		len = rlen;
	}

	return len;
}

/* API 3.1.3  recvmsg() - UDP Style Syntax
 *
 *  ssize_t recvmsg(int socket, struct msghdr *message,
 *                    int flags);
 *
 *  socket  - the socket descriptor of the endpoint.
 *  message - pointer to the msghdr structure which contains a single
 *            user message and possibly some ancillary data.
 *
 *            See Section 5 for complete description of the data
 *            structures.
 *
 *  flags   - flags sent or received with the user message, see Section
 *            5 for complete description of the flags.
 */
static struct sk_buff *sctp_skb_recv_datagram(struct sock *, int, int, int *);

SCTP_STATIC int sctp_recvmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
			     int len, int noblock, int flags, int *addr_len)
{
	struct sctp_ulpevent *event = NULL;
	struct sctp_opt *sp = sctp_sk(sk);
	struct sk_buff *skb;
	int copied;
	int err = 0;
	int skb_len;

	SCTP_DEBUG_PRINTK("sctp_recvmsg(%s: %p, %s: %p, %s: %d, %s: %d, %s: "
			  "0x%x, %s: %p)\n", "sk", sk, "msghdr", msg,
			  "len", len, "knoblauch", noblock,
			  "flags", flags, "addr_len", addr_len);

	sctp_lock_sock(sk);

	if ((SCTP_SOCKET_TCP == sp->type) &&
	    (SCTP_SS_ESTABLISHED != sk->state)) {
		err = -ENOTCONN;
		goto out;
	}
	
	skb = sctp_skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

	/* Get the total length of the skb including any skb's in the
	 * frag_list.
	 */
	skb_len = skb->len;

	copied = skb_len;
	if (copied > len)
		copied = len;

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	event = sctp_skb2event(skb);

	if (err)
		goto out_free;

	sock_recv_timestamp(msg, sk, skb);
	if (sctp_ulpevent_is_notification(event)) {
		msg->msg_flags |= MSG_NOTIFICATION;
		sp->pf->event_msgname(event, msg->msg_name, addr_len);
	} else {
		sp->pf->skb_msgname(skb, msg->msg_name, addr_len);
	}

	/* Check if we allow SCTP_SNDRCVINFO. */
	if (sp->subscribe.sctp_data_io_event)
		sctp_ulpevent_read_sndrcvinfo(event, msg);
#if 0
	/* FIXME: we should be calling IP/IPv6 layers.  */
	if (sk->protinfo.af_inet.cmsg_flags)
		ip_cmsg_recv(msg, skb);
#endif

	err = copied;

	/* If skb's length exceeds the user's buffer, update the skb and
	 * push it back to the receive_queue so that the next call to
	 * recvmsg() will return the remaining data. Don't set MSG_EOR.
	 */
	if (skb_len > copied) {
		msg->msg_flags &= ~MSG_EOR;
		if (flags & MSG_PEEK)
			goto out_free;
		sctp_skb_pull(skb, copied);
		skb_queue_head(&sk->receive_queue, skb);

		/* When only partial message is copied to the user, increase
		 * rwnd by that amount. If all the data in the skb is read,
		 * rwnd is updated when the skb's destructor is called via
		 * sctp_ulpevent_free().
		 */
		sctp_assoc_rwnd_increase(event->asoc, copied);
		goto out;
	} else if ((event->msg_flags & MSG_NOTIFICATION) ||
		   (event->msg_flags & MSG_EOR))
		msg->msg_flags |= MSG_EOR;
	else
		msg->msg_flags &= ~MSG_EOR;

out_free:
	sctp_ulpevent_free(event); /* Free the skb. */
out:
	sctp_release_sock(sk);
	return err;
}

static int sctp_setsockopt_disable_fragments(struct sock *sk,
						    char *optval, int optlen)
{
	int val;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(val, (int *)optval))
		return -EFAULT;

	sctp_sk(sk)->disable_fragments = (val == 0) ? 0 : 1;

	return 0;
}

static int sctp_setsockopt_events(struct sock *sk, char *optval,
					int optlen)
{
	if (optlen != sizeof(struct sctp_event_subscribe))
		return -EINVAL;
	if (copy_from_user(&sctp_sk(sk)->subscribe, optval, optlen))
		return -EFAULT;
	return 0;
}

static int sctp_setsockopt_autoclose(struct sock *sk, char *optval,
					    int optlen)
{
	struct sctp_opt *sp = sctp_sk(sk);

	/* Applicable to UDP-style socket only */
	if (SCTP_SOCKET_TCP == sp->type)
		return -EOPNOTSUPP;
	if (optlen != sizeof(int))
		return -EINVAL;
	if (copy_from_user(&sp->autoclose, optval, optlen))
		return -EFAULT;

	sp->ep->timeouts[SCTP_EVENT_TIMEOUT_AUTOCLOSE] = sp->autoclose * HZ;
	return 0;
}

static int sctp_setsockopt_peer_addr_params(struct sock *sk,
						   char *optval, int optlen)
{
	struct sctp_paddrparams params;
	sctp_association_t *asoc;
	union sctp_addr *addr;
	struct sctp_transport *trans;
	int error;

	if (optlen != sizeof(struct sctp_paddrparams))
		return -EINVAL;
	if (copy_from_user(&params, optval, optlen))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, params.spp_assoc_id);
	if (!asoc)
		return -EINVAL;

	addr = (union sctp_addr *) &(params.spp_address);

	trans = sctp_assoc_lookup_paddr(asoc, addr);
	if (!trans)
		return -ENOENT;

	/* Applications can enable or disable heartbeats for any peer address
	 * of an association, modify an address's heartbeat interval, force a
	 * heartbeat to be sent immediately, and adjust the address's maximum
	 * number of retransmissions sent before an address is considered
	 * unreachable.
	 *
	 * The value of the heartbeat interval, in milliseconds. A value of
	 * UINT32_MAX (4294967295), when modifying the parameter, specifies
	 * that a heartbeat should be sent immediately to the peer address,
	 * and the current interval should remain unchanged.
	 */
	if (0xffffffff == params.spp_hbinterval) {
		error = sctp_primitive_REQUESTHEARTBEAT (asoc, trans);
		if (error)
			return error;
	} else {
	/* The value of the heartbeat interval, in milliseconds. A value of 0,
	 * when modifying the parameter, specifies that the heartbeat on this
	 * address should be disabled.
	 */
		if (params.spp_hbinterval) {
			trans->hb_allowed = 1;
			trans->hb_interval = params.spp_hbinterval * HZ / 1000;
		} else
			trans->hb_allowed = 0;
	}

	/* spp_pathmaxrxt contains the maximum number of retransmissions
	 * before this address shall be considered unreachable.
	 */
	trans->error_threshold = params.spp_pathmaxrxt;

	return 0;
}

static int sctp_setsockopt_initmsg(struct sock *sk, char *optval,
					  int optlen)
{
	if (optlen != sizeof(struct sctp_initmsg))
		return -EINVAL;
	if (copy_from_user(&sctp_sk(sk)->initmsg, optval, optlen))
		return -EFAULT;
	return 0;
}

/*
 *
 * 7.1.15 Set default send parameters (SET_DEFAULT_SEND_PARAM)
 *
 *   Applications that wish to use the sendto() system call may wish to
 *   specify a default set of parameters that would normally be supplied
 *   through the inclusion of ancillary data.  This socket option allows
 *   such an application to set the default sctp_sndrcvinfo structure.
 *   The application that wishes to use this socket option simply passes
 *   in to this call the sctp_sndrcvinfo structure defined in Section
 *   5.2.2) The input parameters accepted by this call include
 *   sinfo_stream, sinfo_flags, sinfo_ppid, sinfo_context,
 *   sinfo_timetolive.  The user must provide the sinfo_assoc_id field in
 *   to this call if the caller is using the UDP model.
 */
static int sctp_setsockopt_default_send_param(struct sock *sk,
						char *optval, int optlen)
{
	struct sctp_sndrcvinfo info;
	sctp_association_t *asoc;

	if (optlen != sizeof(struct sctp_sndrcvinfo))
		return -EINVAL;
	if (copy_from_user(&info, optval, optlen))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, info.sinfo_assoc_id);
	if (!asoc)
		return -EINVAL;

	asoc->defaults.stream = info.sinfo_stream;
	asoc->defaults.flags = info.sinfo_flags;
	asoc->defaults.ppid = info.sinfo_ppid;
	asoc->defaults.context = info.sinfo_context;
	asoc->defaults.timetolive = info.sinfo_timetolive;
	return 0;
}

/* 7.1.10 Set Peer Primary Address (SCTP_SET_PEER_PRIMARY_ADDR)
 *
 * Requests that the local SCTP stack use the enclosed peer address as
 * the association primary.  The enclosed address must be one of the
 * association peer's addresses.
 */
static int sctp_setsockopt_peer_prim(struct sock *sk, char *optval, int optlen)
{
	struct sctp_setpeerprim prim;
	struct sctp_association *asoc;
	union sctp_addr *addr;
	struct sctp_transport *trans;

	if (optlen != sizeof(struct sctp_setpeerprim))
		return -EINVAL;

	if (copy_from_user(&prim, optval, sizeof(struct sctp_setpeerprim)))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, prim.sspp_assoc_id);
	if (!asoc)
		return -EINVAL;

	/* Find the requested address. */
	addr = (union sctp_addr *) &(prim.sspp_addr);

	trans = sctp_assoc_lookup_paddr(asoc, addr);
	if (!trans)
		return -ENOENT;

	sctp_assoc_set_primary(asoc, trans);

	return 0;
}

/*
 *
 * 7.1.5 SCTP_NODELAY
 *
 * Turn on/off any Nagle-like algorithm.  This means that packets are
 * generally sent as soon as possible and no unnecessary delays are
 * introduced, at the cost of more packets in the network.  Expects an
 *  integer boolean flag.
 */
static int sctp_setsockopt_nodelay(struct sock *sk, char *optval,
					int optlen)
{
	__u8 val;

	if (optlen < sizeof(__u8))
		return -EINVAL;

	if (get_user(val, (__u8 *)optval))
		return -EFAULT;

	sctp_sk(sk)->nodelay = (val == 0) ? 0 : 1;

	return 0;
}

/* API 6.2 setsockopt(), getsockopt()
 *
 * Applications use setsockopt() and getsockopt() to set or retrieve
 * socket options.  Socket options are used to change the default
 * behavior of sockets calls.  They are described in Section 7.
 *
 * The syntax is:
 *
 *   ret = getsockopt(int sd, int level, int optname, void *optval,
 *                    int *optlen);
 *   ret = setsockopt(int sd, int level, int optname, const void *optval,
 *                    int optlen);
 *
 *   sd      - the socket descript.
 *   level   - set to IPPROTO_SCTP for all SCTP options.
 *   optname - the option name.
 *   optval  - the buffer to store the value of the option.
 *   optlen  - the size of the buffer.
 */
SCTP_STATIC int sctp_setsockopt(struct sock *sk, int level, int optname,
				char *optval, int optlen)
{
	int retval = 0;
	char *tmp;

	SCTP_DEBUG_PRINTK("sctp_setsockopt(sk: %p... optname: %d)\n",
			  sk, optname);

	/* I can hardly begin to describe how wrong this is.  This is
	 * so broken as to be worse than useless.  The API draft
	 * REALLY is NOT helpful here...  I am not convinced that the
	 * semantics of setsockopt() with a level OTHER THAN SOL_SCTP
	 * are at all well-founded.
	 */
	if (level != SOL_SCTP) {
		struct sctp_af *af = sctp_sk(sk)->pf->af;
		retval = af->setsockopt(sk, level, optname, optval, optlen);
		goto out_nounlock;
	}

	sctp_lock_sock(sk);

	switch (optname) {
	case SCTP_SOCKOPT_DEBUG_NAME:
		/* BUG! we don't ever seem to free this memory. --jgrimm */
		if (NULL == (tmp = kmalloc(optlen + 1, GFP_KERNEL))) {
			retval = -ENOMEM;
			goto out_unlock;
		}

		if (copy_from_user(tmp, optval, optlen)) {
			retval = -EFAULT;
			goto out_unlock;
		}
		tmp[optlen] = '\000';
		sctp_sk(sk)->ep->debug_name = tmp;
		break;

	case SCTP_SOCKOPT_BINDX_ADD:
		/* 'optlen' is the size of the addresses buffer. */
		retval = sctp_setsockopt_bindx(sk, (struct sockaddr_storage *)
					       optval, optlen, BINDX_ADD_ADDR);
		break;

	case SCTP_SOCKOPT_BINDX_REM:
		/* 'optlen' is the size of the addresses buffer. */
		retval = sctp_setsockopt_bindx(sk, (struct sockaddr_storage *)
					       optval, optlen, BINDX_REM_ADDR);
		break;

	case SCTP_DISABLE_FRAGMENTS:
		retval = sctp_setsockopt_disable_fragments(sk, optval, optlen);
		break;

	case SCTP_SET_EVENTS:
		retval = sctp_setsockopt_events(sk, optval, optlen);
		break;

	case SCTP_AUTOCLOSE:
		retval = sctp_setsockopt_autoclose(sk, optval, optlen);
		break;

	case SCTP_SET_PEER_ADDR_PARAMS:
		retval = sctp_setsockopt_peer_addr_params(sk, optval, optlen);
		break;

	case SCTP_INITMSG:
		retval = sctp_setsockopt_initmsg(sk, optval, optlen);
		break;

	case SCTP_SET_DEFAULT_SEND_PARAM:
		retval = sctp_setsockopt_default_send_param(sk, optval,
							    optlen);
		break;

	case SCTP_SET_PEER_PRIMARY_ADDR:
		retval = sctp_setsockopt_peer_prim(sk, optval, optlen);
		break;

	case SCTP_NODELAY:
		retval = sctp_setsockopt_nodelay(sk, optval, optlen);
		break;

	default:
		retval = -ENOPROTOOPT;
		break;
	};

out_unlock:
	sctp_release_sock(sk);

out_nounlock:
	return retval;
}

/* API 3.1.6 connect() - UDP Style Syntax
 *
 * An application may use the connect() call in the UDP model to initiate an
 * association without sending data.
 *
 * The syntax is:
 *
 * ret = connect(int sd, const struct sockaddr *nam, socklen_t len);
 *
 * sd: the socket descriptor to have a new association added to.
 *
 * nam: the address structure (either struct sockaddr_in or struct
 *    sockaddr_in6 defined in RFC2553 [7]).
 *
 * len: the size of the address.
 */
SCTP_STATIC int sctp_connect(struct sock *sk, struct sockaddr *uaddr,
			     int addr_len)
{
	struct sctp_opt *sp;
	sctp_endpoint_t *ep;
	sctp_association_t *asoc;
	struct sctp_transport *transport;
	union sctp_addr to;
	sctp_scope_t scope;
	long timeo;
	int err = 0;

	sctp_lock_sock(sk);

	SCTP_DEBUG_PRINTK("%s - sk: %p, sockaddr: %p, addr_len: %d)\n",
			  __FUNCTION__, sk, uaddr, addr_len);

	sp = sctp_sk(sk);
	ep = sp->ep;

	/* connect() cannot be done on a socket that is already in ESTABLISHED
	 * state - UDP-style peeled off socket or a TCP-style socket that
	 * is already connected. 
	 * It cannot be done even on a TCP-style listening socket.
	 */
	if ((SCTP_SS_ESTABLISHED == sk->state) ||
	    ((SCTP_SOCKET_TCP == sp->type) &&
	     (SCTP_SS_LISTENING == sk->state))) {
		err = -EISCONN;
		goto out_unlock;
	}

	err = sctp_verify_addr(sk, (union sctp_addr *)uaddr, addr_len);
	if (err)
		goto out_unlock;

	if (addr_len > sizeof(to))
		addr_len = sizeof(to);
	memcpy(&to, uaddr, addr_len);
	to.v4.sin_port = ntohs(to.v4.sin_port);

	asoc = sctp_endpoint_lookup_assoc(ep, &to, &transport);
	if (asoc) {
		if (asoc->state >= SCTP_STATE_ESTABLISHED)
			err = -EISCONN;
		else
			err = -EALREADY;
		goto out_unlock;
	}

	/* If we could not find a matching association on the endpoint,
	 * make sure that there is no peeled-off association matching the
	 * peer address even on another socket.
	 */
	if (sctp_endpoint_is_peeled_off(ep, &to)) {
		err = -EADDRNOTAVAIL;
		goto out_unlock;
	}

	/* If a bind() or sctp_bindx() is not called prior to a connect()
	 * call, the system picks an ephemeral port and will choose an address
	 * set equivalent to binding with a wildcard address.
	 */
	if (!ep->base.bind_addr.port) {
		if (sctp_autobind(sk)) {
			err = -EAGAIN;
			goto out_unlock;
		}
	}

	scope = sctp_scope(&to);
	asoc = sctp_association_new(ep, sk, scope, GFP_KERNEL);
	if (!asoc) {
		err = -ENOMEM;
		goto out_unlock;
  	}

	/* Prime the peer's transport structures.  */
	transport = sctp_assoc_add_peer(asoc, &to, GFP_KERNEL);
	if (!transport) {
		sctp_association_free(asoc);
		goto out_unlock;
	}
	err = sctp_assoc_set_bind_addr_from_ep(asoc, GFP_KERNEL);
	if (err < 0) {
		sctp_association_free(asoc);
		goto out_unlock;
	}

	err = sctp_primitive_ASSOCIATE(asoc, NULL);
	if (err < 0) {
		sctp_association_free(asoc);
		goto out_unlock;
	}

	timeo = sock_sndtimeo(sk, sk->socket->file->f_flags & O_NONBLOCK);
	err = sctp_wait_for_connect(asoc, &timeo);

out_unlock:
	sctp_release_sock(sk);

	return err;
}

/* FIXME: Write comments. */
SCTP_STATIC int sctp_disconnect(struct sock *sk, int flags)
{
	return -EOPNOTSUPP; /* STUB */
}

/* 4.1.4 accept() - TCP Style Syntax
 *
 * Applications use accept() call to remove an established SCTP
 * association from the accept queue of the endpoint.  A new socket
 * descriptor will be returned from accept() to represent the newly
 * formed association.
 */
SCTP_STATIC struct sock *sctp_accept(struct sock *sk, int flags, int *err)
{
	struct sctp_opt *sp;
	struct sctp_endpoint *ep;
	struct sock *newsk = NULL;
	struct sctp_association *assoc;
	long timeo;
	int error = 0;
 
	sctp_lock_sock(sk);

	sp = sctp_sk(sk);
	ep = sp->ep;

	if (SCTP_SOCKET_TCP != sp->type) {
		error = -EOPNOTSUPP;
		goto out;
	}

	if (SCTP_SS_LISTENING != sk->state) {
		error = -EINVAL;
		goto out;
	}

	timeo = sock_rcvtimeo(sk, sk->socket->file->f_flags & O_NONBLOCK);

	error = sctp_wait_for_accept(sk, timeo);
	if (error)
		goto out;

	/* We treat the list of associations on the endpoint as the accept 
	 * queue and pick the first association on the list. 
	 */
	assoc = list_entry(ep->asocs.next, struct sctp_association, asocs);

	newsk = sp->pf->create_accept_sk(sk, assoc); 
	if (!newsk) {
		error = -ENOMEM;
		goto out;
	}

	/* Populate the fields of the newsk from the oldsk and migrate the
	 * assoc to the newsk.
	 */ 
	sctp_sock_migrate(sk, newsk, assoc, SCTP_SOCKET_TCP);

out:
	sctp_release_sock(sk);
 	*err = error;
	return newsk;
}

/* FIXME: Write Comments. */
SCTP_STATIC int sctp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	return -EOPNOTSUPP; /* STUB */
}

/* This is the function which gets called during socket creation to
 * initialized the SCTP-specific portion of the sock.
 * The sock structure should already be zero-filled memory.
 */
SCTP_STATIC int sctp_init_sock(struct sock *sk)
{
	sctp_endpoint_t *ep;
	struct sctp_protocol *proto;
	struct sctp_opt *sp;

	SCTP_DEBUG_PRINTK("sctp_init_sock(sk: %p)\n", sk);

	proto = sctp_get_protocol();

	sp = sctp_sk(sk);

	/* Initialize the SCTP per socket area.  */
	switch (sk->type) {
	case SOCK_SEQPACKET:
		sp->type = SCTP_SOCKET_UDP;
		break;
	case SOCK_STREAM:
		sp->type = SCTP_SOCKET_TCP;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	/* FIXME:  The next draft (04) of the SCTP Sockets Extensions
	 * should include a socket option for manipulating these
	 * message parameters (and a few others).
	 */
	sp->default_stream = 0;
	sp->default_ppid = 0;

	/* Initialize default setup parameters. These parameters
	 * can be modified with the SCTP_INITMSG socket option or
	 * overridden by the SCTP_INIT CMSG.
	 */
	sp->initmsg.sinit_num_ostreams   = proto->max_outstreams;
	sp->initmsg.sinit_max_instreams  = proto->max_instreams;
	sp->initmsg.sinit_max_attempts   = proto->max_retrans_init;
	sp->initmsg.sinit_max_init_timeo = proto->rto_max / HZ;

	/* Initialize default RTO related parameters.  These parameters can
	 * be modified for with the SCTP_RTOINFO socket option.
	 * FIXME: These are not used yet.
	 */
	sp->rtoinfo.srto_initial = proto->rto_initial;
	sp->rtoinfo.srto_max     = proto->rto_max;
	sp->rtoinfo.srto_min     = proto->rto_min;

	/* Initialize default event subscriptions.
	 * the struct sock is initialized to zero, so only
	 * enable the events needed.  By default, UDP-style
	 * sockets enable io and association change notifications.
	 */
	if (SCTP_SOCKET_UDP == sp->type) {
		sp->subscribe.sctp_data_io_event     = 1;
		sp->subscribe.sctp_association_event = 1;
	}

	/* Default Peer Address Parameters.  These defaults can
	 * be modified via SCTP_SET_PEER_ADDR_PARAMS
	 */
	sp->paddrparam.spp_hbinterval = proto->hb_interval / HZ;
	sp->paddrparam.spp_pathmaxrxt = proto->max_retrans_path;

	/* If enabled no SCTP message fragmentation will be performed.
	 * Configure through SCTP_DISABLE_FRAGMENTS socket option.
	 */
	sp->disable_fragments = 0;

	/* Turn on/off any Nagle-like algorithm.  */
	sp->nodelay           = 1;

	/* Auto-close idle associations after the configured
	 * number of seconds.  A value of 0 disables this
	 * feature.  Configure through the SCTP_AUTOCLOSE socket option,
	 * for UDP-style sockets only.
	 */
	sp->autoclose         = 0;
	sp->pf = sctp_get_pf_specific(sk->family);

	/* Control variables for partial data delivery. */
	sp->pd_mode           = 0;
	skb_queue_head_init(&sp->pd_lobby);

	/* Create a per socket endpoint structure.  Even if we
	 * change the data structure relationships, this may still
	 * be useful for storing pre-connect address information.
	 */
	ep = sctp_endpoint_new(proto, sk, GFP_KERNEL);
	if (NULL == ep)
		return -ENOMEM;

	sp->ep = ep;

	SCTP_DBG_OBJCNT_INC(sock);
	return 0;
}

/* Cleanup any SCTP per socket resources.  */
SCTP_STATIC int sctp_destroy_sock(struct sock *sk)
{
	sctp_endpoint_t *ep;

	SCTP_DEBUG_PRINTK("sctp_destroy_sock(sk: %p)\n", sk);

	/* Release our hold on the endpoint. */
	ep = sctp_sk(sk)->ep;
	sctp_endpoint_free(ep);

	return 0;
}

/* FIXME: Comments needed.  */
SCTP_STATIC void sctp_shutdown(struct sock *sk, int how)
{
	/* UDP-style sockets do not support shutdown. */
	/* STUB */
}

/* 7.2.1 Association Status (SCTP_STATUS)

 * Applications can retrieve current status information about an
 * association, including association state, peer receiver window size,
 * number of unacked data chunks, and number of data chunks pending
 * receipt.  This information is read-only.
 */
static int sctp_getsockopt_sctp_status(struct sock *sk, int len, char *optval,
				       int *optlen)
{
	struct sctp_status status;
	sctp_association_t *assoc = NULL;
	struct sctp_transport *transport;
	sctp_assoc_t associd;
	int retval = 0;

	if (len != sizeof(status)) {
		retval = -EINVAL;
		goto out;
	}

	if (copy_from_user(&status, optval, sizeof(status))) {
		retval = -EFAULT;
		goto out;
	}

	associd = status.sstat_assoc_id;
	assoc = sctp_id2assoc(sk, associd);
	if (!assoc) {
		retval = -EINVAL;
		goto out;
	}

	transport = assoc->peer.primary_path;

	status.sstat_assoc_id = sctp_assoc2id(assoc);
	status.sstat_state = assoc->state;
	status.sstat_rwnd =  assoc->peer.rwnd;
	status.sstat_unackdata = assoc->unack_data;
	status.sstat_penddata = assoc->peer.tsn_map.pending_data;
	status.sstat_instrms = assoc->c.sinit_max_instreams;
	status.sstat_outstrms = assoc->c.sinit_num_ostreams;
	status.sstat_fragmentation_point = assoc->frag_point;
	status.sstat_primary.spinfo_assoc_id = sctp_assoc2id(transport->asoc);
	memcpy(&status.sstat_primary.spinfo_address,
	       &(transport->ipaddr), sizeof(union sctp_addr));
	status.sstat_primary.spinfo_state = transport->active;
	status.sstat_primary.spinfo_cwnd = transport->cwnd;
	status.sstat_primary.spinfo_srtt = transport->srtt;
	status.sstat_primary.spinfo_rto = transport->rto;
	status.sstat_primary.spinfo_mtu = transport->pmtu;

	if (put_user(len, optlen)) {
		retval = -EFAULT;
		goto out;
	}

	SCTP_DEBUG_PRINTK("sctp_getsockopt_sctp_status(%d): %d %d %p\n",
			  len, status.sstat_state, status.sstat_rwnd,
			  status.sstat_assoc_id);

	if (copy_to_user(optval, &status, len)) {
		retval = -EFAULT;
		goto out;
	}

out:
	return (retval);
}

static int sctp_getsockopt_disable_fragments(struct sock *sk, int len,
						    char *optval, int *optlen)
{
	int val;

	if (len < sizeof(int))
		return -EINVAL;

	len = sizeof(int);
	val = (sctp_sk(sk)->disable_fragments == 1);
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;
	return 0;
}

static int sctp_getsockopt_set_events(struct sock *sk, int len, char *optval, int *optlen)
{
	if (len != sizeof(struct sctp_event_subscribe))
		return -EINVAL;
	if (copy_to_user(optval, &sctp_sk(sk)->subscribe, len))
		return -EFAULT;
	return 0;
}

static int sctp_getsockopt_autoclose(struct sock *sk, int len, char *optval, int *optlen)
{
	/* Applicable to UDP-style socket only */
	if (SCTP_SOCKET_TCP == sctp_sk(sk)->type)
		return -EOPNOTSUPP;
	if (len != sizeof(int))
		return -EINVAL;
	if (copy_to_user(optval, &sctp_sk(sk)->autoclose, len))
		return -EFAULT;
	return 0;
}

/* Helper routine to branch off an association to a new socket.  */
SCTP_STATIC int sctp_do_peeloff(sctp_association_t *assoc, struct socket **newsock)
{
	struct sock *oldsk = assoc->base.sk;
	struct sock *newsk;
	struct socket *tmpsock;
	int err = 0;

	/* An association cannot be branched off from an already peeled-off
	 * socket, nor is this supported for tcp style sockets.
	 */
	if (SCTP_SOCKET_UDP != sctp_sk(oldsk)->type)
		return -EOPNOTSUPP;

	/* Create a new socket.  */
	err = sock_create(oldsk->family, SOCK_SEQPACKET, IPPROTO_SCTP,
			  &tmpsock);
	if (err < 0)
		return err;

	newsk = tmpsock->sk;

	/* Populate the fields of the newsk from the oldsk and migrate the
	 * assoc to the newsk.
	 */ 
	sctp_sock_migrate(oldsk, newsk, assoc, SCTP_SOCKET_UDP_HIGH_BANDWIDTH);

	*newsock = tmpsock;

	return err;
}

static int sctp_getsockopt_peeloff(struct sock *sk, int len, char *optval, int *optlen)
{
	sctp_peeloff_arg_t peeloff;
	struct socket *newsock;
	int retval = 0;
	sctp_association_t *assoc;

	if (len != sizeof(sctp_peeloff_arg_t))
		return -EINVAL;
	if (copy_from_user(&peeloff, optval, len))
		return -EFAULT;

	assoc = sctp_id2assoc(sk, peeloff.associd);
	if (NULL == assoc) {
		retval = -EINVAL;
		goto out;
	}

	SCTP_DEBUG_PRINTK("%s: sk: %p assoc: %p\n", __FUNCTION__, sk, assoc);

	retval = sctp_do_peeloff(assoc, &newsock);
	if (retval < 0)
		goto out;

	/* Map the socket to an unused fd that can be returned to the user.  */
	retval = sock_map_fd(newsock);
	if (retval < 0) {
		sock_release(newsock);
		goto out;
	}

	SCTP_DEBUG_PRINTK("%s: sk: %p assoc: %p newsk: %p sd: %d\n",
			  __FUNCTION__, sk, assoc, newsock->sk, retval);

	/* Return the fd mapped to the new socket.  */
	peeloff.sd = retval;
	if (copy_to_user(optval, &peeloff, len))
		retval = -EFAULT;

out:
	return retval;
}

static int sctp_getsockopt_peer_addr_params(struct sock *sk, int len, 
						char *optval, int *optlen)
{
	struct sctp_paddrparams params;
	sctp_association_t *asoc;
	union sctp_addr *addr;
	struct sctp_transport *trans;

	if (len != sizeof(struct sctp_paddrparams))
		return -EINVAL;
	if (copy_from_user(&params, optval, *optlen))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, params.spp_assoc_id);
	if (!asoc)
		return -EINVAL;

	addr = (union sctp_addr *) &(params.spp_address);

	trans = sctp_assoc_lookup_paddr(asoc, addr);
	if (!trans)
		return -ENOENT;

	/* The value of the heartbeat interval, in milliseconds. A value of 0,
	 * when modifying the parameter, specifies that the heartbeat on this
	 * address should be disabled.
	 */
	if (!trans->hb_allowed)
		params.spp_hbinterval = 0;
	else
		params.spp_hbinterval = trans->hb_interval * 1000 / HZ;

	/* spp_pathmaxrxt contains the maximum number of retransmissions
	 * before this address shall be considered unreachable.
	 */
	params.spp_pathmaxrxt = trans->error_threshold;

	if (copy_to_user(optval, &params, len))
		return -EFAULT;
	*optlen = len;

	return 0;
}

static int sctp_getsockopt_initmsg(struct sock *sk, int len, char *optval, int *optlen)
{
	if (len != sizeof(struct sctp_initmsg))
		return -EINVAL;
	if (copy_to_user(optval, &sctp_sk(sk)->initmsg, len))
		return -EFAULT;
	return 0;
}

static int sctp_getsockopt_peer_addrs_num(struct sock *sk, int len, 
					char *optval, int *optlen)
{
	sctp_assoc_t id;
	sctp_association_t *asoc;
	struct list_head *pos;
	int cnt = 0;

	if (len != sizeof(sctp_assoc_t))
		return -EINVAL;

	if (copy_from_user(&id, optval, sizeof(sctp_assoc_t)))
		return -EFAULT;

	/*
	 *  For UDP-style sockets, id specifies the association to query.
	 */
	asoc = sctp_id2assoc(sk, id);
	if (!asoc)
		return -EINVAL;

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		cnt ++;
	}
	if (copy_to_user(optval, &cnt, sizeof(int)))
		return -EFAULT;

	return 0;
}

static int sctp_getsockopt_peer_addrs(struct sock *sk, int len,
				char *optval, int *optlen)
{
	sctp_association_t *asoc;
	struct list_head *pos;
	int cnt = 0;
	struct sctp_getaddrs getaddrs;
	struct sctp_transport *from;
	struct sockaddr_storage *to;

	if (len != sizeof(struct sctp_getaddrs))
		return -EINVAL;

	if (copy_from_user(&getaddrs, optval, sizeof(struct sctp_getaddrs)))
		return -EFAULT;

	if (getaddrs.addr_num <= 0) return -EINVAL;
	/*
	 *  For UDP-style sockets, id specifies the association to query.
	 */
	asoc = sctp_id2assoc(sk, getaddrs.assoc_id);
	if (!asoc)
		return -EINVAL;

	to = getaddrs.addrs;
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		from = list_entry(pos, struct sctp_transport, transports);
		if (copy_to_user(to, &from->ipaddr, sizeof(from->ipaddr)))
			return -EFAULT;
		to ++;
		cnt ++;
		if (cnt >= getaddrs.addr_num) break;
	}
	getaddrs.addr_num = cnt;
	if (copy_to_user(optval, &getaddrs, sizeof(struct sctp_getaddrs)))
		return -EFAULT;

	return 0;
}

static int sctp_getsockopt_local_addrs_num(struct sock *sk, int len,
						char *optval, int *optlen)
{
	sctp_assoc_t id;
	sctp_bind_addr_t *bp;
	sctp_association_t *asoc;
	struct list_head *pos;
	int cnt = 0;

	if (len != sizeof(sctp_assoc_t))
		return -EINVAL;

	if (copy_from_user(&id, optval, sizeof(sctp_assoc_t)))
		return -EFAULT;

	/*
	 *  For UDP-style sockets, id specifies the association to query.
	 *  If the id field is set to the value '0' then the locally bound
	 *  addresses are returned without regard to any particular
	 *  association.
	 */
	if (0 == id) {
		bp = &sctp_sk(sk)->ep->base.bind_addr;
	} else {
		asoc = sctp_id2assoc(sk, id);
		if (!asoc)
			return -EINVAL;
		bp = &asoc->base.bind_addr;
	}

	list_for_each(pos, &bp->address_list) {
		cnt ++;
	}
	if (copy_to_user(optval, &cnt, sizeof(int)))
		return -EFAULT;

	return 0;
}

static int sctp_getsockopt_local_addrs(struct sock *sk, int len,
					char *optval, int *optlen)
{
	sctp_bind_addr_t *bp;
	sctp_association_t *asoc;
	struct list_head *pos;
	int cnt = 0;
	struct sctp_getaddrs getaddrs;
	struct sockaddr_storage_list *from;
	struct sockaddr_storage *to;

	if (len != sizeof(struct sctp_getaddrs))
		return -EINVAL;

	if (copy_from_user(&getaddrs, optval, sizeof(struct sctp_getaddrs)))
		return -EFAULT;

	if (getaddrs.addr_num <= 0) return -EINVAL;
	/*
	 *  For UDP-style sockets, id specifies the association to query.
	 *  If the id field is set to the value '0' then the locally bound
	 *  addresses are returned without regard to any particular
	 *  association.
	 */
	if (0 == getaddrs.assoc_id) {
		bp = &sctp_sk(sk)->ep->base.bind_addr;
	} else {
		asoc = sctp_id2assoc(sk, getaddrs.assoc_id);
		if (!asoc)
			return -EINVAL;
		bp = &asoc->base.bind_addr;
	}

	to = getaddrs.addrs;
	list_for_each(pos, &bp->address_list) {
		from = list_entry(pos,
				struct sockaddr_storage_list,
				list);
		if (copy_to_user(to, &from->a, sizeof(from->a)))
			return -EFAULT;
		to ++;
		cnt ++;
		if (cnt >= getaddrs.addr_num) break;
	}
	getaddrs.addr_num = cnt;
	if (copy_to_user(optval, &getaddrs, sizeof(struct sctp_getaddrs)))
		return -EFAULT;

	return 0;
}

/* 7.1.10 Set Peer Primary Address (SCTP_SET_PEER_PRIMARY_ADDR)
 *
 * Requests that the local SCTP stack use the enclosed peer address as
 * the association primary.  The enclosed address must be one of the
 * association peer's addresses.
 */
static int sctp_getsockopt_peer_prim(struct sock *sk, int len,
				char *optval, int *optlen)
{
	struct sctp_setpeerprim prim;
	struct sctp_association *asoc;

	if (len != sizeof(struct sctp_setpeerprim))
		return -EINVAL;

	if (copy_from_user(&prim, optval, sizeof(struct sctp_setpeerprim)))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, prim.sspp_assoc_id);
	if (!asoc)
		return -EINVAL;

	if (!asoc->peer.primary_path)
		return -ENOTCONN;

	memcpy(&prim.sspp_addr, &asoc->peer.primary_path->ipaddr,
	       sizeof(union sctp_addr));

	if (copy_to_user(optval, &prim, sizeof(struct sctp_setpeerprim)))
		return -EFAULT;

	return 0;
}

/*
 *
 * 7.1.15 Set default send parameters (SET_DEFAULT_SEND_PARAM)
 *
 *   Applications that wish to use the sendto() system call may wish to
 *   specify a default set of parameters that would normally be supplied
 *   through the inclusion of ancillary data.  This socket option allows
 *   such an application to set the default sctp_sndrcvinfo structure.
 *   The application that wishes to use this socket option simply passes
 *   in to this call the sctp_sndrcvinfo structure defined in Section
 *   5.2.2) The input parameters accepted by this call include
 *   sinfo_stream, sinfo_flags, sinfo_ppid, sinfo_context,
 *   sinfo_timetolive.  The user must provide the sinfo_assoc_id field in
 *   to this call if the caller is using the UDP model.
 *
 *   For getsockopt, it get the default sctp_sndrcvinfo structure.
 */
static int sctp_getsockopt_default_send_param(struct sock *sk,
					int len, char *optval, int *optlen)
{
	struct sctp_sndrcvinfo info;
	sctp_association_t *asoc;

	if (len != sizeof(struct sctp_sndrcvinfo))
		return -EINVAL;
	if (copy_from_user(&info, optval, sizeof(struct sctp_sndrcvinfo)))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, info.sinfo_assoc_id);
	if (!asoc)
		return -EINVAL;

	info.sinfo_stream = asoc->defaults.stream;
	info.sinfo_flags = asoc->defaults.flags;
	info.sinfo_ppid = asoc->defaults.ppid;
	info.sinfo_context = asoc->defaults.context;
	info.sinfo_timetolive = asoc->defaults.timetolive;

	if (copy_to_user(optval, &info, sizeof(struct sctp_sndrcvinfo)))
		return -EFAULT;

	return 0;
}

/*
 *
 * 7.1.5 SCTP_NODELAY
 *
 * Turn on/off any Nagle-like algorithm.  This means that packets are
 * generally sent as soon as possible and no unnecessary delays are
 * introduced, at the cost of more packets in the network.  Expects an
 * integer boolean flag.
 */

static int sctp_getsockopt_nodelay(struct sock *sk, int len, 
					char *optval, int *optlen)
{
	__u8 val;

	if (len < sizeof(__u8))
		return -EINVAL;

	len = sizeof(__u8);
	val = (sctp_sk(sk)->nodelay == 1);
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;
	return 0;
}

SCTP_STATIC int sctp_getsockopt(struct sock *sk, int level, int optname,
				char *optval, int *optlen)
{
	int retval = 0;
	int len;

	SCTP_DEBUG_PRINTK("sctp_getsockopt(sk: %p, ...)\n", sk);

	/* I can hardly begin to describe how wrong this is.  This is
	 * so broken as to be worse than useless.  The API draft
	 * REALLY is NOT helpful here...  I am not convinced that the
	 * semantics of getsockopt() with a level OTHER THAN SOL_SCTP
	 * are at all well-founded.
	 */
	if (level != SOL_SCTP) {
		struct sctp_af *af = sctp_sk(sk)->pf->af;

		retval = af->getsockopt(sk, level, optname, optval, optlen);
		return retval;
	}

	if (get_user(len, optlen))
		return -EFAULT;

	sctp_lock_sock(sk);

	switch (optname) {
	case SCTP_STATUS:
		retval = sctp_getsockopt_sctp_status(sk, len, optval, optlen);
		break;
	case SCTP_DISABLE_FRAGMENTS:
		retval = sctp_getsockopt_disable_fragments(sk, len, optval,
							   optlen);
		break;
	case SCTP_SET_EVENTS:
		retval = sctp_getsockopt_set_events(sk, len, optval, optlen);
		break;
	case SCTP_AUTOCLOSE:
		retval = sctp_getsockopt_autoclose(sk, len, optval, optlen);
		break;
	case SCTP_SOCKOPT_PEELOFF:
		retval = sctp_getsockopt_peeloff(sk, len, optval, optlen);
		break;
	case SCTP_GET_PEER_ADDR_PARAMS:
		retval = sctp_getsockopt_peer_addr_params(sk, len, optval,
							  optlen);
		break;
	case SCTP_INITMSG:
		retval = sctp_getsockopt_initmsg(sk, len, optval, optlen);
		break;
	case SCTP_GET_PEER_ADDRS_NUM:
		retval = sctp_getsockopt_peer_addrs_num(sk, len, optval, 
							optlen);
		break;
	case SCTP_GET_LOCAL_ADDRS_NUM:
		retval = sctp_getsockopt_local_addrs_num(sk, len, optval,
							 optlen);
		break;
	case SCTP_GET_PEER_ADDRS:
		retval = sctp_getsockopt_peer_addrs(sk, len, optval,
						    optlen);
		break;
	case SCTP_GET_LOCAL_ADDRS:
		retval = sctp_getsockopt_local_addrs(sk, len, optval,
						     optlen);
		break;
	case SCTP_SET_DEFAULT_SEND_PARAM:
		retval = sctp_getsockopt_default_send_param(sk, len,
							    optval, optlen);
		break;
	case SCTP_SET_PEER_PRIMARY_ADDR:
		retval = sctp_getsockopt_peer_prim(sk, len, optval, optlen);
		break;
	case SCTP_NODELAY:
		retval = sctp_getsockopt_nodelay(sk, len, optval, optlen);
		break;
	default:
		retval = -ENOPROTOOPT;
		break;
	};

	sctp_release_sock(sk);
	return retval;
}

static void sctp_hash(struct sock *sk)
{
	/* STUB */
}

static void sctp_unhash(struct sock *sk)
{
	/* STUB */
}

/* Check if port is acceptable.  Possibly find first available port.
 *
 * The port hash table (contained in the 'global' SCTP protocol storage
 * returned by struct sctp_protocol *sctp_get_protocol()). The hash
 * table is an array of 4096 lists (sctp_bind_hashbucket_t). Each
 * list (the list number is the port number hashed out, so as you
 * would expect from a hash function, all the ports in a given list have
 * such a number that hashes out to the same list number; you were
 * expecting that, right?); so each list has a set of ports, with a
 * link to the socket (struct sock) that uses it, the port number and
 * a fastreuse flag (FIXME: NPI ipg).
 */
static sctp_bind_bucket_t *sctp_bucket_create(sctp_bind_hashbucket_t *head,
					      unsigned short snum);
static long sctp_get_port_local(struct sock *sk, union sctp_addr *addr)
{
	sctp_bind_hashbucket_t *head; /* hash list */
	sctp_bind_bucket_t *pp; /* hash list port iterator */
	struct sctp_protocol *sctp = sctp_get_protocol();
	unsigned short snum;
	int ret;

	/* NOTE:  Remember to put this back to net order. */
	addr->v4.sin_port = ntohs(addr->v4.sin_port);
	snum = addr->v4.sin_port;

	SCTP_DEBUG_PRINTK("sctp_get_port() begins, snum=%d\n", snum);

	sctp_local_bh_disable();

	if (snum == 0) {
		/* Search for an available port.
		 *
		 * 'sctp->port_rover' was the last port assigned, so
		 * we start to search from 'sctp->port_rover +
		 * 1'. What we do is first check if port 'rover' is
		 * already in the hash table; if not, we use that; if
		 * it is, we try next.
		 */
		int low = sysctl_local_port_range[0];
		int high = sysctl_local_port_range[1];
		int remaining = (high - low) + 1;
		int rover;
		int index;

		sctp_spin_lock(&sctp->port_alloc_lock);
		rover = sctp->port_rover;
		do {
			rover++;
			if ((rover < low) || (rover > high))
				rover = low;
			index = sctp_phashfn(rover);
			head = &sctp->port_hashtable[index];
			sctp_spin_lock(&head->lock);
			for (pp = head->chain; pp; pp = pp->next)
				if (pp->port == rover)
					goto next;
			break;
		next:
			sctp_spin_unlock(&head->lock);
		} while (--remaining > 0);
		sctp->port_rover = rover;
		sctp_spin_unlock(&sctp->port_alloc_lock);

		/* Exhausted local port range during search? */
		ret = 1;
		if (remaining <= 0)
			goto fail;

		/* OK, here is the one we will use.  HEAD (the port
		 * hash table list entry) is non-NULL and we hold it's
		 * mutex.
		 */
		snum = rover;
		pp = NULL;
	} else {
		/* We are given an specific port number; we verify
		 * that it is not being used. If it is used, we will
		 * exahust the search in the hash list corresponding
		 * to the port number (snum) - we detect that with the
		 * port iterator, pp being NULL.
		 */
		head = &sctp->port_hashtable[sctp_phashfn(snum)];
		sctp_spin_lock(&head->lock);
		for (pp = head->chain; pp; pp = pp->next) {
			if (pp->port == snum)
				break;
		}
	}


	if (pp != NULL && pp->sk != NULL) {
		/* We had a port hash table hit - there is an
		 * available port (pp != NULL) and it is being
		 * used by other socket (pp->sk != NULL); that other
		 * socket is going to be sk2.
		 */
		int sk_reuse = sk->reuse;
		struct sock *sk2 = pp->sk;

		SCTP_DEBUG_PRINTK("sctp_get_port() found a "
				  "possible match\n");
		if (pp->fastreuse != 0 && sk->reuse != 0)
			goto success;

		/* Run through the list of sockets bound to the port
		 * (pp->port) [via the pointers bind_next and
		 * bind_pprev in the struct sock *sk2 (pp->sk)]. On each one,
		 * we get the endpoint they describe and run through
		 * the endpoint's list of IP (v4 or v6) addresses,
		 * comparing each of the addresses with the address of
		 * the socket sk. If we find a match, then that means
		 * that this port/socket (sk) combination are already
		 * in an endpoint.
		 */
		for ( ; sk2 != NULL; sk2 = sk2->bind_next) {
			sctp_endpoint_t *ep2;
			ep2 = sctp_sk(sk2)->ep;

			if (sk_reuse && sk2->reuse)
				continue;

			if (sctp_bind_addr_match(&ep2->base.bind_addr, addr,
						 sctp_sk(sk)))
				goto found;
		}

	found:
		/* If we found a conflict, fail.  */
		if (sk2 != NULL) {
			ret = (long) sk2;
			goto fail_unlock;
		}
		SCTP_DEBUG_PRINTK("sctp_get_port(): Found a match\n");
	}

	/* If there was a hash table miss, create a new port.  */
	ret = 1;

	if (pp == NULL && (pp = sctp_bucket_create(head, snum)) == NULL)
		goto fail_unlock;

	/* In either case (hit or miss), make sure fastreuse is 1 only
	 * if sk->reuse is too (that is, if the caller requested
	 * SO_REUSEADDR on this socket -sk-).
	 */
	if (pp->sk == NULL) {
		pp->fastreuse = sk->reuse ? 1 : 0;
	} else if (pp->fastreuse && sk->reuse == 0) {
		pp->fastreuse = 0;
	}

	/* We are set, so fill up all the data in the hash table
	 * entry, tie the socket list information with the rest of the
	 * sockets FIXME: Blurry, NPI (ipg).
	 */
success:
	inet_sk(sk)->num = snum;
	if (sk->prev == NULL) {
		if ((sk->bind_next = pp->sk) != NULL)
			pp->sk->bind_pprev = &sk->bind_next;
		pp->sk = sk;
		sk->bind_pprev = &pp->sk;
		sk->prev = (struct sock *) pp;
	}
	ret = 0;

fail_unlock:
	sctp_spin_unlock(&head->lock);

fail:
	sctp_local_bh_enable();

	SCTP_DEBUG_PRINTK("sctp_get_port() ends, ret=%d\n", ret);
	addr->v4.sin_port = htons(addr->v4.sin_port);
	return ret;
}

/* Assign a 'snum' port to the socket.  If snum == 0, an ephemeral
 * port is requested.
 */
static int sctp_get_port(struct sock *sk, unsigned short snum)
{
	long ret;
	union sctp_addr addr;
	struct sctp_af *af = sctp_sk(sk)->pf->af;

	/* Set up a dummy address struct from the sk. */
	af->from_sk(&addr, sk);
	addr.v4.sin_port = htons(snum);

	/* Note: sk->num gets filled in if ephemeral port request. */
	ret = sctp_get_port_local(sk, &addr);

	return (ret ? 1 : 0);
}

/*
 * 3.1.3 listen() - UDP Style Syntax
 *
 *   By default, new associations are not accepted for UDP style sockets.
 *   An application uses listen() to mark a socket as being able to
 *   accept new associations.
 */
SCTP_STATIC int sctp_seqpacket_listen(struct sock *sk, int backlog)
{
	struct sctp_opt *sp = sctp_sk(sk);
	sctp_endpoint_t *ep = sp->ep;

	/* Only UDP style sockets that are not peeled off are allowed to
	 * listen().
	 */
	if (SCTP_SOCKET_UDP != sp->type)
		return -EINVAL;

	if (sk->state == SCTP_SS_LISTENING)
		return 0;

	/*
	 * If a bind() or sctp_bindx() is not called prior to a listen()
	 * call that allows new associations to be accepted, the system
	 * picks an ephemeral port and will choose an address set equivalent
	 * to binding with a wildcard address.
	 *
	 * This is not currently spelled out in the SCTP sockets
	 * extensions draft, but follows the practice as seen in TCP
	 * sockets.
	 */
	if (!ep->base.bind_addr.port) {
		if (sctp_autobind(sk))
			return -EAGAIN;
	}
	sk->state = SCTP_SS_LISTENING;
	sctp_hash_endpoint(ep);
	return 0;
}

/*
 * 4.1.3 listen() - TCP Style Syntax
 *
 *   Applications uses listen() to ready the SCTP endpoint for accepting 
 *   inbound associations.
 */
SCTP_STATIC int sctp_stream_listen(struct sock *sk, int backlog)
{
	struct sctp_opt *sp = sctp_sk(sk);
	sctp_endpoint_t *ep = sp->ep;

	if (sk->state == SCTP_SS_LISTENING)
		return 0;

	/*
	 * If a bind() or sctp_bindx() is not called prior to a listen()
	 * call that allows new associations to be accepted, the system
	 * picks an ephemeral port and will choose an address set equivalent
	 * to binding with a wildcard address.
	 *
	 * This is not currently spelled out in the SCTP sockets
	 * extensions draft, but follows the practice as seen in TCP
	 * sockets.
	 */
	if (!ep->base.bind_addr.port) {
		if (sctp_autobind(sk))
			return -EAGAIN;
	}
	sk->state = SCTP_SS_LISTENING;
	sk->max_ack_backlog = backlog;
	sctp_hash_endpoint(ep);
	return 0;
}

/*
 *  Move a socket to LISTENING state.
 */
int sctp_inet_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err;

	sctp_lock_sock(sk);

	err = -EINVAL;
	if (sock->state != SS_UNCONNECTED)
		goto out;
	switch (sock->type) {
	case SOCK_SEQPACKET:
		err = sctp_seqpacket_listen(sk, backlog);
		break;
	case SOCK_STREAM:
		err = sctp_stream_listen(sk, backlog);
		break;

	default:
		goto out;
	};

out:
	sctp_release_sock(sk);
	return err;
}

/*
 * This function is done by modeling the current datagram_poll() and the
 * tcp_poll().  Note that, based on these implementations, we don't
 * lock the socket in this function, even though it seems that,
 * ideally, locking or some other mechanisms can be used to ensure
 * the integrity of the counters (sndbuf and wmem_queued) used
 * in this place.  We assume that we don't need locks either until proven
 * otherwise.
 *
 * Another thing to note is that we include the Async I/O support
 * here, again, by modeling the current TCP/UDP code.  We don't have
 * a good way to test with it yet.
 */
unsigned int sctp_poll(struct file *file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask;

	poll_wait(file, sk->sleep, wait);
	mask = 0;

	/* Is there any exceptional events?  */
	if (sk->err || !skb_queue_empty(&sk->error_queue))
		mask |= POLLERR;
	if (sk->shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	/* Is it readable?  Reconsider this code with TCP-style support.  */
	if (!skb_queue_empty(&sk->receive_queue) ||
	    (sk->shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	/*
	 * FIXME: We need to set SCTP_SS_DISCONNECTING for TCP-style and
	 * peeled off sockets.  Additionally, TCP-style needs to consider
	 * other establishment conditions.
	 */
	if (SCTP_SOCKET_UDP != sctp_sk(sk)->type) {
		/* The association is going away.  */
		if (SCTP_SS_DISCONNECTING == sk->state)
			mask |= POLLHUP;
		/* The association is either gone or not ready.  */
		if (SCTP_SS_CLOSED == sk->state)
			return mask;
	}

	/* Is it writable?  */
	if (sctp_writeable(sk)) {
		mask |= POLLOUT | POLLWRNORM;
	} else {
		set_bit(SOCK_ASYNC_NOSPACE, &sk->socket->flags);
		/*
		 * Since the socket is not locked, the buffer
		 * might be made available after the writeable check and
		 * before the bit is set.  This could cause a lost I/O
		 * signal.  tcp_poll() has a race breaker for this race
		 * condition.  Based on their implementation, we put
		 * in the following code to cover it as well.
		 */
		if (sctp_writeable(sk))
			mask |= POLLOUT | POLLWRNORM;
	}
	return mask;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

static sctp_bind_bucket_t *sctp_bucket_create(sctp_bind_hashbucket_t *head, unsigned short snum)
{
	sctp_bind_bucket_t *pp;

	SCTP_DEBUG_PRINTK( "sctp_bucket_create() begins, snum=%d\n", snum);
	pp = kmalloc(sizeof(sctp_bind_bucket_t), GFP_ATOMIC);
	if (pp) {
		pp->port = snum;
		pp->fastreuse = 0;
		pp->sk = NULL;
		if ((pp->next = head->chain) != NULL)
			pp->next->pprev = &pp->next;
		head->chain = pp;
		pp->pprev = &head->chain;
	}
	SCTP_DEBUG_PRINTK("sctp_bucket_create() ends, pp=%p\n", pp);
	return pp;
}

/* FIXME: Commments! */
static __inline__ void __sctp_put_port(struct sock *sk)
{
	struct sctp_protocol *sctp_proto = sctp_get_protocol();
	sctp_bind_hashbucket_t *head =
		&sctp_proto->port_hashtable[sctp_phashfn(inet_sk(sk)->num)];
	sctp_bind_bucket_t *pp;

	sctp_spin_lock(&head->lock);
	pp = (sctp_bind_bucket_t *) sk->prev;
	if (sk->bind_next)
		sk->bind_next->bind_pprev = sk->bind_pprev;
	*(sk->bind_pprev) = sk->bind_next;
	sk->prev = NULL;
	inet_sk(sk)->num = 0;
	if (pp->sk) {
		if (pp->next)
			pp->next->pprev = pp->pprev;
		*(pp->pprev) = pp->next;
		kfree(pp);
	}
	sctp_spin_unlock(&head->lock);
}

void sctp_put_port(struct sock *sk)
{
	sctp_local_bh_disable();
	__sctp_put_port(sk);
	sctp_local_bh_enable();
}

/*
 * The system picks an ephemeral port and choose an address set equivalent
 * to binding with a wildcard address.
 * One of those addresses will be the primary address for the association.
 * This automatically enables the multihoming capability of SCTP.
 */
static int sctp_autobind(struct sock *sk)
{
	union sctp_addr autoaddr;
	struct sctp_af *af;
	unsigned short port;

	/* Initialize a local sockaddr structure to INADDR_ANY. */
	af = sctp_sk(sk)->pf->af;

	port = htons(inet_sk(sk)->num);
	af->inaddr_any(&autoaddr, port);

	return sctp_do_bind(sk, &autoaddr, af->sockaddr_len);
}

/* Parse out IPPROTO_SCTP CMSG headers.  Perform only minimal validation.
 *
 * From RFC 2292
 * 4.2 The cmsghdr Structure *
 *
 * When ancillary data is sent or received, any number of ancillary data
 * objects can be specified by the msg_control and msg_controllen members of
 * the msghdr structure, because each object is preceded by
 * a cmsghdr structure defining the object's length (the cmsg_len member).
 * Historically Berkeley-derived implementations have passed only one object
 * at a time, but this API allows multiple objects to be
 * passed in a single call to sendmsg() or recvmsg(). The following example
 * shows two ancillary data objects in a control buffer.
 *
 *   |<--------------------------- msg_controllen -------------------------->|
 *   |                                                                       |
 *
 *   |<----- ancillary data object ----->|<----- ancillary data object ----->|
 *
 *   |<---------- CMSG_SPACE() --------->|<---------- CMSG_SPACE() --------->|
 *   |                                   |                                   |
 *
 *   |<---------- cmsg_len ---------->|  |<--------- cmsg_len ----------->|  |
 *
 *   |<--------- CMSG_LEN() --------->|  |<-------- CMSG_LEN() ---------->|  |
 *   |                                |  |                                |  |
 *
 *   +-----+-----+-----+--+-----------+--+-----+-----+-----+--+-----------+--+
 *   |cmsg_|cmsg_|cmsg_|XX|           |XX|cmsg_|cmsg_|cmsg_|XX|           |XX|
 *
 *   |len  |level|type |XX|cmsg_data[]|XX|len  |level|type |XX|cmsg_data[]|XX|
 *
 *   +-----+-----+-----+--+-----------+--+-----+-----+-----+--+-----------+--+
 *    ^
 *    |
 *
 * msg_control
 * points here
 */
SCTP_STATIC int sctp_msghdr_parse(const struct msghdr *msg,
				  sctp_cmsgs_t *cmsgs)
{
	struct cmsghdr *cmsg;

	for (cmsg = CMSG_FIRSTHDR(msg);
	     cmsg != NULL;
	     cmsg = CMSG_NXTHDR((struct msghdr*)msg, cmsg)) {
		/* Check for minimum length.  The SCM code has this check.  */
		if (cmsg->cmsg_len < sizeof(struct cmsghdr) ||
		    (unsigned long)(((char*)cmsg - (char*)msg->msg_control)
				    + cmsg->cmsg_len) > msg->msg_controllen) {
			return -EINVAL;
		}

		/* Should we parse this header or ignore?  */
		if (cmsg->cmsg_level != IPPROTO_SCTP)
			continue;

		/* Strictly check lengths following example in SCM code.  */
		switch (cmsg->cmsg_type) {
		case SCTP_INIT:
			/* SCTP Socket API Extension (draft 1)
			 * 5.2.1 SCTP Initiation Structure (SCTP_INIT)
			 *
			 * This cmsghdr structure provides information for
			 * initializing new SCTP associations with sendmsg().
			 * The SCTP_INITMSG socket option uses this same data
			 * structure.  This structure is not used for
			 * recvmsg().
			 *
			 * cmsg_level    cmsg_type      cmsg_data[]
			 * ------------  ------------   ----------------------
			 * IPPROTO_SCTP  SCTP_INIT      struct sctp_initmsg
			 */
			if (cmsg->cmsg_len !=
			    CMSG_LEN(sizeof(struct sctp_initmsg)))
				return -EINVAL;
			cmsgs->init = (struct sctp_initmsg *)CMSG_DATA(cmsg);
			break;

		case SCTP_SNDRCV:
			/* SCTP Socket API Extension (draft 1)
			 * 5.2.2 SCTP Header Information Structure(SCTP_SNDRCV)
			 *
			 * This cmsghdr structure specifies SCTP options for
			 * sendmsg() and describes SCTP header information
			 * about a received message through recvmsg().
			 *
			 * cmsg_level    cmsg_type      cmsg_data[]
			 * ------------  ------------   ----------------------
			 * IPPROTO_SCTP  SCTP_SNDRCV    struct sctp_sndrcvinfo
			 */
			if (cmsg->cmsg_len !=
			    CMSG_LEN(sizeof(struct sctp_sndrcvinfo)))
				return -EINVAL;

			cmsgs->info = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);

			/* Minimally, validate the sinfo_flags. */
			if (cmsgs->info->sinfo_flags &
			    ~(MSG_UNORDERED | MSG_ADDR_OVER |
			      MSG_ABORT | MSG_EOF))
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		};
	}
	return 0;
}

/*
 * Wait for a packet..
 * Note: This function is the same function as in core/datagram.c
 * with a few modifications to make lksctp work.
 */
static int sctp_wait_for_packet(struct sock * sk, int *err, long *timeo_p)
{
	int error;
	DECLARE_WAITQUEUE(wait, current);

	__set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue_exclusive(sk->sleep, &wait);

	/* Socket errors? */
	error = sock_error(sk);
	if (error)
		goto out;

	if (!skb_queue_empty(&sk->receive_queue))
		goto ready;

	/* Socket shut down?  */
	if (sk->shutdown & RCV_SHUTDOWN)
		goto out;

	/* Sequenced packets can come disconnected.  If so we report the
	 * problem.
	 */
	error = -ENOTCONN;

	/* Is there a good reason to think that we may receive some data?  */
	if ((list_empty(&sctp_sk(sk)->ep->asocs)) &&
	    (sk->state != SCTP_SS_LISTENING))
		goto out;

	/* Handle signals.  */
	if (signal_pending(current))
		goto interrupted;

	/* Let another process have a go.  Since we are going to sleep
	 * anyway.  Note: This may cause odd behaviors if the message
	 * does not fit in the user's buffer, but this seems to be the
	 * only way to honor MSG_DONTWAIT realistically.
	 */
	sctp_release_sock(sk);
	*timeo_p = schedule_timeout(*timeo_p);
	sctp_lock_sock(sk);

ready:
	remove_wait_queue(sk->sleep, &wait);
	__set_current_state(TASK_RUNNING);
	return 0;

interrupted:
	error = sock_intr_errno(*timeo_p);

out:
	remove_wait_queue(sk->sleep, &wait);
	__set_current_state(TASK_RUNNING);
	*err = error;
	return error;
}

/* Receive a datagram.
 * Note: This is pretty much the same routine as in core/datagram.c
 * with a few changes to make lksctp work.
 */
static struct sk_buff *sctp_skb_recv_datagram(struct sock *sk, int flags, int noblock, int *err)
{
	int error;
	struct sk_buff *skb;
	long timeo;

	/* Caller is allowed not to check sk->err before skb_recv_datagram()  */
	error = sock_error(sk);
	if (error)
		goto no_packet;

	timeo = sock_rcvtimeo(sk, noblock);

	SCTP_DEBUG_PRINTK("Timeout: timeo: %ld, MAX: %ld.\n",
			  timeo, MAX_SCHEDULE_TIMEOUT);

	do {
		/* Again only user level code calls this function,
		 * so nothing interrupt level
		 * will suddenly eat the receive_queue.
		 *
		 *  Look at current nfs client by the way...
		 *  However, this function was corrent in any case. 8)
		 */
		if (flags & MSG_PEEK) {
			unsigned long cpu_flags;

			sctp_spin_lock_irqsave(&sk->receive_queue.lock,
					       cpu_flags);
			skb = skb_peek(&sk->receive_queue);
			if (skb)
				atomic_inc(&skb->users);
			sctp_spin_unlock_irqrestore(&sk->receive_queue.lock,
						    cpu_flags);
		} else {
			skb = skb_dequeue(&sk->receive_queue);
		}

		if (skb)
			return skb;

		/* User doesn't want to wait.  */
		error = -EAGAIN;
		if (!timeo)
			goto no_packet;
	} while (sctp_wait_for_packet(sk, err, &timeo) == 0);

	return NULL;

no_packet:
	*err = error;
	return NULL;
}

/* Verify that this is a valid address. */
static inline int sctp_verify_addr(struct sock *sk, union sctp_addr *addr, 
				   int len)
{
	struct sctp_af *af;

	/* Verify basic sockaddr. */
	af = sctp_sockaddr_af(sctp_sk(sk), addr, len);
	if (!af)
		return -EINVAL;

	/* Is this a valid SCTP address?  */
	if (!af->addr_valid((union sctp_addr *)addr))
		return -EINVAL;

	return 0;
}

/* Get the sndbuf space available at the time on the association.  */
static inline int sctp_wspace(sctp_association_t *asoc)
{
	struct sock *sk = asoc->base.sk;
	int amt = 0;

	amt = sk->sndbuf - asoc->sndbuf_used;
	if (amt < 0)
		amt = 0;
	return amt;
}

/* Increment the used sndbuf space count of the corresponding association by
 * the size of the outgoing data chunk.
 * Also, set the skb destructor for sndbuf accounting later.
 *
 * Since it is always 1-1 between chunk and skb, and also a new skb is always
 * allocated for chunk bundling in sctp_packet_transmit(), we can use the
 * destructor in the data chunk skb for the purpose of the sndbuf space
 * tracking.
 */
static inline void sctp_set_owner_w(sctp_chunk_t *chunk)
{
	sctp_association_t *asoc = chunk->asoc;
	struct sock *sk = asoc->base.sk;

	/* The sndbuf space is tracked per association.  */
	sctp_association_hold(asoc);

	chunk->skb->destructor = sctp_wfree;
	/* Save the chunk pointer in skb for sctp_wfree to use later.  */
	*((sctp_chunk_t **)(chunk->skb->cb)) = chunk;

	asoc->sndbuf_used += SCTP_DATA_SNDSIZE(chunk);
	sk->wmem_queued += SCTP_DATA_SNDSIZE(chunk);
}

/* If sndbuf has changed, wake up per association sndbuf waiters.  */
static void __sctp_write_space(sctp_association_t *asoc)
{
	struct sock *sk = asoc->base.sk;
	struct socket *sock = sk->socket;

	if ((sctp_wspace(asoc) > 0) && sock) {
		if (waitqueue_active(&asoc->wait))
			wake_up_interruptible(&asoc->wait);

		if (sctp_writeable(sk)) {
			if (sk->sleep && waitqueue_active(sk->sleep))
				wake_up_interruptible(sk->sleep);

			/* Note that we try to include the Async I/O support
			 * here by modeling from the current TCP/UDP code.
			 * We have not tested with it yet.
			 */
			if (sock->fasync_list &&
			    !(sk->shutdown & SEND_SHUTDOWN))
				sock_wake_async(sock, 2, POLL_OUT);
		}
	}
}

/* Do accounting for the sndbuf space.
 * Decrement the used sndbuf space of the corresponding association by the
 * data size which was just transmitted(freed).
 */
static void sctp_wfree(struct sk_buff *skb)
{
	sctp_association_t *asoc;
	sctp_chunk_t *chunk;
	struct sock *sk;

	/* Get the saved chunk pointer.  */
	chunk = *((sctp_chunk_t **)(skb->cb));
	asoc = chunk->asoc;
	sk = asoc->base.sk;
	asoc->sndbuf_used -= SCTP_DATA_SNDSIZE(chunk);
	sk->wmem_queued -= SCTP_DATA_SNDSIZE(chunk);
	__sctp_write_space(asoc);

	sctp_association_put(asoc);
}

/* Helper function to wait for space in the sndbuf.  */
static int sctp_wait_for_sndbuf(sctp_association_t *asoc, long *timeo_p,
				int msg_len)
{
	struct sock *sk = asoc->base.sk;
	int err = 0;
	long current_timeo = *timeo_p;
	DECLARE_WAITQUEUE(wait, current);

	SCTP_DEBUG_PRINTK("wait_for_sndbuf: asoc=%p, timeo=%ld, msg_len=%d\n",
	                  asoc, (long)(*timeo_p), msg_len);

	/* Wait on the association specific sndbuf space. */
	add_wait_queue_exclusive(&asoc->wait, &wait);

	/* Increment the association's refcnt.  */
	sctp_association_hold(asoc);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!*timeo_p)
			goto do_nonblock;
		if (sk->err || asoc->state >= SCTP_STATE_SHUTDOWN_PENDING ||
		    asoc->base.dead)
			goto do_error;
		if (signal_pending(current))
			goto do_interrupted;
		if (msg_len <= sctp_wspace(asoc))
			break;

		/* Let another process have a go.  Since we are going
		 * to sleep anyway.
		 */
		sctp_release_sock(sk);
		current_timeo = schedule_timeout(current_timeo);
		sctp_lock_sock(sk);

		*timeo_p = current_timeo;
	}

out:
	remove_wait_queue(&asoc->wait, &wait);

	/* Release the association's refcnt.  */
	sctp_association_put(asoc);

	__set_current_state(TASK_RUNNING);
	return err;

do_error:
	err = -EPIPE;
	goto out;

do_interrupted:
	err = sock_intr_errno(*timeo_p);
	goto out;

do_nonblock:
	err = -EAGAIN;
	goto out;
}

/* If socket sndbuf has changed, wake up all per association waiters.  */
void sctp_write_space(struct sock *sk)
{
	sctp_association_t *asoc;
	struct list_head *pos;

	/* Wake up the tasks in each wait queue.  */
	list_for_each(pos, &((sctp_sk(sk))->ep->asocs)) {
		asoc = list_entry(pos, sctp_association_t, asocs);
		__sctp_write_space(asoc);
	}
}

/* Is there any sndbuf space available on the socket?
 *
 * Note that wmem_queued is the sum of the send buffers on all of the
 * associations on the same socket.  For a UDP-style socket with
 * multiple associations, it is possible for it to be "unwriteable"
 * prematurely.  I assume that this is acceptable because
 * a premature "unwriteable" is better than an accidental "writeable" which
 * would cause an unwanted block under certain circumstances.  For the 1-1
 * UDP-style sockets or TCP-style sockets, this code should work.
 *  - Daisy
 */
static int sctp_writeable(struct sock *sk)
{
	int amt = 0;

	amt = sk->sndbuf - sk->wmem_queued;
	if (amt < 0)
		amt = 0;
	return amt;
}

/* Wait for an association to go into ESTABLISHED state. If timeout is 0,
 * returns immediately with EINPROGRESS.
 */
static int sctp_wait_for_connect(sctp_association_t *asoc, long *timeo_p)
{
	struct sock *sk = asoc->base.sk;
	int err = 0;
	long current_timeo = *timeo_p;
	DECLARE_WAITQUEUE(wait, current);

	SCTP_DEBUG_PRINTK("%s: asoc=%p, timeo=%ld\n", __FUNCTION__, asoc,
			  (long)(*timeo_p));

	add_wait_queue_exclusive(&asoc->wait, &wait);

	/* Increment the association's refcnt.  */
	sctp_association_hold(asoc);

	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		if (!*timeo_p)
			goto do_nonblock;
		if (sk->err || asoc->state >= SCTP_STATE_SHUTDOWN_PENDING ||
		    asoc->base.dead)
			goto do_error;
		if (signal_pending(current))
			goto do_interrupted;

		if (asoc->state == SCTP_STATE_ESTABLISHED)
			break;

		/* Let another process have a go.  Since we are going
		 * to sleep anyway.
		 */
		sctp_release_sock(sk);
		current_timeo = schedule_timeout(current_timeo);
		sctp_lock_sock(sk);

		*timeo_p = current_timeo;
	}

out:
	remove_wait_queue(&asoc->wait, &wait);

	/* Release the association's refcnt.  */
	sctp_association_put(asoc);

	__set_current_state(TASK_RUNNING);

	return err;

do_error:
	err = -ECONNREFUSED;
	goto out;

do_interrupted:
	err = sock_intr_errno(*timeo_p);
	goto out;

do_nonblock:
	err = -EINPROGRESS;
	goto out;
}

static int sctp_wait_for_accept(struct sock *sk, long timeo)
{
	struct sctp_endpoint *ep;
	int err = 0;
	DECLARE_WAITQUEUE(wait, current);

	ep = sctp_sk(sk)->ep;

	add_wait_queue_exclusive(sk->sleep, &wait);

	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		if (list_empty(&ep->asocs)) {
			sctp_release_sock(sk);
			timeo = schedule_timeout(timeo);
			sctp_lock_sock(sk);
		}

		err = -EINVAL;
		if (sk->state != SCTP_SS_LISTENING)
			break;

		err = 0;
		if (!list_empty(&ep->asocs))
			break;

		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			break;

		err = -EAGAIN;
		if (!timeo)
			break;
	}

	remove_wait_queue(sk->sleep, &wait);
	__set_current_state(TASK_RUNNING);

	return err;
}

/* Populate the fields of the newsk from the oldsk and migrate the assoc 
 * and its messages to the newsk.
 */ 
void sctp_sock_migrate(struct sock *oldsk, struct sock *newsk,
		  struct sctp_association *assoc, sctp_socket_type_t type)
{
	struct sctp_opt *oldsp = sctp_sk(oldsk);
	struct sctp_opt *newsp = sctp_sk(newsk);
	sctp_endpoint_t *newep = newsp->ep;
	struct sk_buff *skb, *tmp;
	struct sctp_ulpevent *event;

	/* Migrate socket buffer sizes and all the socket level options to the
	 * new socket.
	 */
	newsk->sndbuf = oldsk->sndbuf;
	newsk->rcvbuf = oldsk->rcvbuf;
	*newsp = *oldsp;

	/* Restore the ep value that was overwritten with the above structure
	 * copy.
	 */
	newsp->ep = newep;

	/* Move any messages in the old socket's receive queue that are for the
	 * peeled off association to the new socket's receive queue.
	 */
	sctp_skb_for_each(skb, &oldsk->receive_queue, tmp) {
		event = sctp_skb2event(skb);
		if (event->asoc == assoc) {
			__skb_unlink(skb, skb->list);
			__skb_queue_tail(&newsk->receive_queue, skb);
		}
	}

	/* Clean up any messages pending delivery due to partial
	 * delivery.   Three cases:
	 * 1) No partial deliver;  no work.
	 * 2) Peeling off partial delivery; keep pd_lobby in new pd_lobby.
	 * 3) Peeling off non-partial delivery; move pd_lobby to recieve_queue.
	 */
	skb_queue_head_init(&newsp->pd_lobby);
	sctp_sk(newsk)->pd_mode = assoc->ulpq.pd_mode;;

	if (sctp_sk(oldsk)->pd_mode) {
		struct sk_buff_head *queue;

		/* Decide which queue to move pd_lobby skbs to. */
		if (assoc->ulpq.pd_mode) {
			queue = &newsp->pd_lobby;
		} else
			queue = &newsk->receive_queue;

		/* Walk through the pd_lobby, looking for skbs that
		 * need moved to the new socket.
		 */
		sctp_skb_for_each(skb, &oldsp->pd_lobby, tmp) {
			event = sctp_skb2event(skb);
			if (event->asoc == assoc) {
				__skb_unlink(skb, skb->list);
				__skb_queue_tail(queue, skb);
			}
		}

		/* Clear up any skbs waiting for the partial
		 * delivery to finish.
		 */
		if (assoc->ulpq.pd_mode)
			sctp_clear_pd(oldsk);

	}

	/* Set the type of socket to indicate that it is peeled off from the
	 * original UDP-style socket or created with the accept() call on a
	 * TCP-style socket..
	 */
	newsp->type = type;

	/* Migrate the association to the new socket. */
	sctp_assoc_migrate(assoc, newsk);

	newsk->state = SCTP_SS_ESTABLISHED;
}
 
/* This proto struct describes the ULP interface for SCTP.  */
struct proto sctp_prot = {
	.name        =	"SCTP",
	.close       =	sctp_close,
	.connect     =	sctp_connect,
	.disconnect  =	sctp_disconnect,
	.accept      =	sctp_accept,
	.ioctl       =	sctp_ioctl,
	.init        =	sctp_init_sock,
	.destroy     =	sctp_destroy_sock,
	.shutdown    =	sctp_shutdown,
	.setsockopt  =	sctp_setsockopt,
	.getsockopt  =	sctp_getsockopt,
	.sendmsg     =	sctp_sendmsg,
	.recvmsg     =	sctp_recvmsg,
	.bind        =	sctp_bind,
	.backlog_rcv =	sctp_backlog_rcv,
	.hash        =	sctp_hash,
	.unhash      =	sctp_unhash,
	.get_port    =	sctp_get_port,
};
