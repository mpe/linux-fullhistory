/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Raw Sockets Interface
 *
 * Author:      Steve Whitehouse <SteveW@ACM.org>
 *
 *
 * Changes:
 *           Steve Whitehouse - connect() function.
 *           Steve Whitehouse - SMP changes, removed MOP stubs. MOP will
 *                              be userland only.
 */

#include <linux/config.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <net/sock.h>
#include <net/dst.h>
#include <net/dn.h>
#include <net/dn_raw.h>
#include <net/dn_route.h>

static rwlock_t dn_raw_hash_lock = RW_LOCK_UNLOCKED;
static struct sock *dn_raw_nsp_sklist = NULL;
static struct sock *dn_raw_routing_sklist = NULL;

static void dn_raw_hash(struct sock *sk)
{
	struct sock **skp;

	switch(sk->protocol) {
		case DNPROTO_NSP:
			skp = &dn_raw_nsp_sklist;
			break;
		case DNPROTO_ROU:
			skp = &dn_raw_routing_sklist;
			break;
		default:
			printk(KERN_DEBUG "dn_raw_hash: Unknown protocol\n");
			return;
	}

	write_lock_bh(&dn_raw_hash_lock);
	sk->next = *skp;
	sk->pprev = skp;
	*skp = sk;
	write_unlock_bh(&dn_raw_hash_lock);
}

static void dn_raw_unhash(struct sock *sk)
{
	struct sock **skp = sk->pprev;

	if (skp == NULL)
		return;

	write_lock_bh(&dn_raw_hash_lock);
	while(*skp != sk)
		skp = &((*skp)->next);
	*skp = sk->next;
	write_unlock_bh(&dn_raw_hash_lock);

	sk->next = NULL;
	sk->pprev = NULL;
}

static void dn_raw_autobind(struct sock *sk)
{
	dn_raw_hash(sk);
	sk->zapped = 0;
}

static int dn_raw_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk == NULL)
		return 0;

	if (!sk->dead) sk->state_change(sk);

	sk->dead = 1;
	sk->socket = NULL;
	sock->sk = NULL;

	dn_raw_unhash(sk);
	sock_put(sk);

	return 0;
}

/*
 * Bind does odd things with raw sockets. Its basically used to filter
 * the incoming packets, but this differs with the different layers
 * at which you extract packets.
 *
 * For Routing layer sockets, the object name is a host ordered unsigned
 * short which is a mask for the 16 different types of possible routing
 * packet. I'd like to also select by destination address of the packets
 * but alas, this is rather too difficult to do at the moment.
 */
static int dn_raw_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_dn *addr = (struct sockaddr_dn *)uaddr;

	if (addr_len != sizeof(struct sockaddr_dn))
		return -EINVAL;

	if (sk->zapped == 0)
		return -EINVAL;

	switch(sk->protocol) {
		case DNPROTO_ROU:
			if (dn_ntohs(addr->sdn_objnamel) && (dn_ntohs(addr->sdn_objnamel) != 2))
				return -EINVAL;
			/* Fall through here */
		case DNPROTO_NSP:
			if (dn_ntohs(addr->sdn_add.a_len) && (dn_ntohs(addr->sdn_add.a_len) != 2))
				return -EINVAL;
			break;
		default:
			return -EPROTONOSUPPORT;
	}

	if (dn_ntohs(addr->sdn_objnamel) > (DN_MAXOBJL-1))
		return -EINVAL;

	if (dn_ntohs(addr->sdn_add.a_len) > DN_MAXADDL)
		return -EINVAL;

	memcpy(&sk->protinfo.dn.addr, addr, sizeof(struct sockaddr_dn));

	dn_raw_autobind(sk);

	return 0;
}

/*
 * This is to allow send() and write() to work. You set the destination address
 * with this function.
 */
static int dn_raw_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct dn_scp *scp = &sk->protinfo.dn;
	struct sockaddr_dn *saddr = (struct sockaddr_dn *)uaddr;
	int err;

	lock_sock(sk);

	err = -EINVAL;
	if (addr_len != sizeof(struct sockaddr_dn))
		goto out;

	if (saddr->sdn_family != AF_DECnet)
		goto out;

	if (dn_ntohs(saddr->sdn_objnamel) > (DN_MAXOBJL-1))
		goto out;

	if (dn_ntohs(saddr->sdn_add.a_len) > DN_MAXADDL)
		goto out;

	if (sk->zapped)
		dn_raw_autobind(sk);

	if ((err = dn_route_output(&sk->dst_cache, dn_saddr2dn(saddr), dn_saddr2dn(&scp->addr), 0)) < 0)
		goto out;

	memcpy(&scp->peer, saddr, sizeof(struct sockaddr_dn));
out:
	release_sock(sk);

	return err;
}

/*
 * TBD.
 */
static int dn_raw_sendmsg(struct socket *sock, struct msghdr *hdr, int size,
			struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;

	if (sk->zapped)
		dn_raw_autobind(sk);

	if (sk->protocol != DNPROTO_NSP)
		return -EOPNOTSUPP;

	return 0;
}

/*
 * This works fine, execpt that it doesn't report the originating address
 * or anything at the moment.
 */
static int dn_raw_recvmsg(struct socket *sock, struct msghdr *msg, int size,
			int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int err = 0;
	int copied = 0;

	lock_sock(sk);

	if (sk->zapped)
		dn_raw_autobind(sk);

	if ((skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT, flags & MSG_DONTWAIT, &err)) == NULL)
		goto out;

	copied = skb->len;

	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	if ((err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied)) != 0) {
		if (flags & MSG_PEEK)
			atomic_dec(&skb->users);
		else
			skb_queue_head(&sk->receive_queue, skb);

		goto out;
	}

	skb_free_datagram(sk, skb);

out:
	release_sock(sk);

	return copied ? copied : err;
}

struct proto_ops dn_raw_proto_ops = {
	AF_DECnet,

	dn_raw_release,
	dn_raw_bind,
	dn_raw_connect,
	sock_no_socketpair,
	sock_no_accept,
	sock_no_getname,
	datagram_poll,
	sock_no_ioctl,
	sock_no_listen,
	sock_no_shutdown,
	sock_no_setsockopt,
	sock_no_getsockopt,
	sock_no_fcntl,
	dn_raw_sendmsg,
	dn_raw_recvmsg,
	sock_no_mmap
};

#ifdef CONFIG_PROC_FS
int dn_raw_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;
	struct sock *sk;

	read_lock_bh(&dn_raw_hash_lock);
	for(sk = dn_raw_nsp_sklist; sk; sk = sk->next) {	
		len += sprintf(buffer+len, "NSP\n");

		pos = begin + len;

		if (pos < offset) {
			len = 0;
			begin = pos;
		}

		if (pos > offset + length)
			goto all_done;

	}

	for(sk = dn_raw_routing_sklist; sk; sk = sk->next) {
		len += sprintf(buffer+len, "ROU\n");

		pos = begin + len;

		if (pos < offset) {
			len = 0;
			begin = pos;
		}

		if (pos > offset + length)
			goto all_done;
	}

all_done:
	read_unlock_bh(&dn_raw_hash_lock);

	*start = buffer + (offset - begin);
	len -= (offset - begin);

	if (len > length) len = length;

	return(len);
}
#endif /* CONFIG_PROC_FS */

void dn_raw_rx_nsp(struct sk_buff *skb)
{
	struct sock *sk;
	struct sk_buff *skb2;

	read_lock(&dn_raw_hash_lock);
	for(sk = dn_raw_nsp_sklist; sk != NULL; sk = sk->next) {
		if (skb->len > sock_rspace(sk))
			continue;
		if (sk->dead)
			continue;
		if ((skb2 = skb_clone(skb, GFP_ATOMIC)) != NULL) {
			skb_set_owner_r(skb2, sk);
			skb_queue_tail(&sk->receive_queue, skb2);
			sk->data_ready(sk, skb->len);	
		}
	}
	read_unlock(&dn_raw_hash_lock);
}

void dn_raw_rx_routing(struct sk_buff *skb)
{
	struct sock *sk;
	struct sk_buff *skb2;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	unsigned short rt_flagmask;
	unsigned short objnamel;
	struct dn_scp *scp;

	read_lock(&dn_raw_hash_lock);
	for(sk = dn_raw_routing_sklist; sk != NULL; sk = sk->next) {
		if (skb->len > sock_rspace(sk))
			continue;
		if (sk->dead)
			continue;
		scp = &sk->protinfo.dn;

		rt_flagmask = dn_ntohs(*(unsigned short *)scp->addr.sdn_objname);
		objnamel = dn_ntohs(scp->addr.sdn_objnamel);

		if ((objnamel == 2) && (!((1 << (cb->rt_flags & 0x0f)) & rt_flagmask)))
			continue;

		if ((skb2 = skb_clone(skb, GFP_ATOMIC)) != NULL) {
			skb_set_owner_r(skb2, sk);
			skb_queue_tail(&sk->receive_queue, skb2);
			sk->data_ready(sk, skb->len);
		}
	}
	read_unlock(&dn_raw_hash_lock);
}

