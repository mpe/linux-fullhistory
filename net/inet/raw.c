/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		RAW - implementation of IP "raw" sockets.
 *
 * Version:	@(#)raw.c	1.0.4	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/system.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include "inet.h"
#include "dev.h"
#include "ip.h"
#include "protocol.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "icmp.h"
#include "udp.h"


static unsigned long
min(unsigned long a, unsigned long b)
{
  if (a < b) return(a);
  return(b);
}


/* raw_err gets called by the icmp module. */
void
raw_err (int err, unsigned char *header, unsigned long daddr,
	 unsigned long saddr, struct inet_protocol *protocol)
{
  struct sock *sk;
   
  DPRINTF((DBG_RAW, "raw_err(err=%d, hdr=%X, daddr=%X, saddr=%X, protocl=%X)\n",
		err, header, daddr, saddr, protocol));

  if (protocol == NULL) return;
  sk = (struct sock *) protocol->data;
  if (sk == NULL) return;

  /* This is meaningless in raw sockets. */
  if (err & 0xff00 == (ICMP_SOURCE_QUENCH << 8)) {
	if (sk->cong_window > 1) sk->cong_window = sk->cong_window/2;
	return;
  }

  sk->err = icmp_err_convert[err & 0xff].errno;

  /* None of them are fatal for raw sockets. */
#if 0
  if (icmp_err_convert[err & 0xff].fatal) {
	sk->prot->close(sk, 0);
  }
#endif
  return;
}


/*
 * This should be the easiest of all, all we do is\
 * copy it into a buffer.
 */
int
raw_rcv(struct sk_buff *skb, struct device *dev, struct options *opt,
	unsigned long daddr, unsigned short len, unsigned long saddr,
	int redo, struct inet_protocol *protocol)
{
  struct sock *sk;

  DPRINTF((DBG_RAW, "raw_rcv(skb=%X, dev=%X, opt=%X, daddr=%X,\n"
	   "         len=%d, saddr=%X, redo=%d, protocol=%X)\n",
	   skb, dev, opt, daddr, len, saddr, redo, protocol));

  if (skb == NULL) return(0);
  if (protocol == NULL) {
	kfree_skb(skb, FREE_READ);
	return(0);
  }
  sk = (struct sock *) protocol->data;
  if (sk == NULL) {
	kfree_skb(skb, FREE_READ);
	return(0);
  }

  /* Now we need to copy this into memory. */
  skb->sk = sk;
  skb->len = len;
  skb->dev = dev;
  skb->saddr = daddr;
  skb->daddr = saddr;

  if (!redo) {
	/* Now see if we are in use. */
	cli();
	if (sk->inuse) {
		DPRINTF((DBG_RAW, "raw_rcv adding to backlog.\n"));
		if (sk->back_log == NULL) {
			sk->back_log = skb;
			skb->next = skb;
			skb->prev = skb;
		} else {
			skb->next = sk->back_log;
			skb->prev = sk->back_log->prev;
			skb->prev->next = skb;
			skb->next->prev = skb;
		}
		sti();
		return(0);
	}
	sk->inuse = 1;
	sti();
  }

  /* Charge it too the socket. */
  if (sk->rmem_alloc + skb->mem_len >= SK_RMEM_MAX) {
	skb->sk = NULL;
	kfree_skb(skb, FREE_READ);
	return(0);
  }
  sk->rmem_alloc += skb->mem_len;

  /* Now just put it onto the queue. */
  if (sk->rqueue == NULL) {
	sk->rqueue = skb;
	skb->next = skb;
	skb->prev = skb;
  } else {
	skb->next = sk->rqueue;
	skb->prev = sk->rqueue->prev;
	skb->prev->next = skb;
	skb->next->prev = skb;
  }
  wake_up(sk->sleep);
  release_sock(sk);
  return(0);
}


/* This will do terrible things if len + ipheader + devheader > dev->mtu */
static int
raw_sendto(struct sock *sk, unsigned char *from, int len,
	   int noblock,
	   unsigned flags, struct sockaddr_in *usin, int addr_len)
{
  struct sk_buff *skb;
  struct device *dev=NULL;
  struct sockaddr_in sin;
  int tmp;

  DPRINTF((DBG_RAW, "raw_sendto(sk=%X, from=%X, len=%d, noblock=%d, flags=%X,\n"
	   "            usin=%X, addr_len = %d)\n", sk, from, len, noblock,
	   flags, usin, addr_len));

  /* Check the flags. */
  if (flags) return(-EINVAL);
  if (len < 0) return(-EINVAL);

  /* Get and verify the address. */
  if (usin) {
	if (addr_len < sizeof(sin)) return(-EINVAL);
	/* verify_area (VERIFY_WRITE, usin, sizeof (sin));*/
	memcpy_fromfs(&sin, usin, sizeof(sin));
	if (sin.sin_family && sin.sin_family != AF_INET) return(-EINVAL);
  } else {
	if (sk->state != TCP_ESTABLISHED) return(-EINVAL);
	sin.sin_family = AF_INET;
	sin.sin_port = sk->protocol;
	sin.sin_addr.s_addr = sk->daddr;
  }
  if (sin.sin_port == 0) sin.sin_port = sk->protocol;

  sk->inuse = 1;
  skb = NULL;
  while (skb == NULL) {
	skb = (struct sk_buff *) sk->prot->wmalloc(sk,
			len+sizeof(*skb) + sk->prot->max_header,
			0, GFP_KERNEL);
	/* This shouldn't happen, but it could. */
	/* FIXME: need to change this to sleep. */
	if (skb == NULL) {
		int tmp;

		DPRINTF((DBG_RAW, "raw_sendto: write buffer full?\n"));
		if (noblock) return(-EAGAIN);
		tmp = sk->wmem_alloc;
		release_sock(sk);
		cli();
		if (tmp <= sk->wmem_alloc) {
			interruptible_sleep_on(sk->sleep);
			if (current->signal & ~current->blocked) {
				sti();
				return(-ERESTARTSYS);
			}
		}
		sk->inuse = 1;
		sti();
	}
  }
  skb->lock = 0;
  skb->mem_addr = skb;
  skb->mem_len = len + sizeof(*skb) +sk->prot->max_header;
  skb->sk = sk;

  skb->free = 1; /* these two should be unecessary. */
  skb->arp = 0;

  tmp = sk->prot->build_header(skb, sk->saddr, 
			       sin.sin_addr.s_addr, &dev,
			       sk->protocol, sk->opt, skb->mem_len);
  if (tmp < 0) {
	DPRINTF((DBG_RAW, "raw_sendto: error building ip header.\n"));
	sk->prot->wfree(sk, skb->mem_addr, skb->mem_len);
	release_sock(sk);
	return(tmp);
  }

  /* verify_area(VERIFY_WRITE, from, len);*/
  memcpy_fromfs ((unsigned char *)(skb+1)+tmp, from, len);

  /* If we are using IPPROTO_RAW, we need to fill in the source address in
     the IP header */

  if(sk->protocol==IPPROTO_RAW) {
    unsigned char *buff;
    struct iphdr *iph;

    buff = (unsigned char *)(skb + 1);
    buff += tmp;
    iph = (struct iphdr *)buff;
    iph->saddr = sk->saddr;
  }

  skb->len = tmp + len;
  sk->prot->queue_xmit(sk, dev, skb, 1);
  release_sock(sk);
  return(len);
}


static int
raw_write(struct sock *sk, unsigned char *buff, int len, int noblock,
	   unsigned flags)
{
  return(raw_sendto(sk, buff, len, noblock, flags, NULL, 0));
}


static void
raw_close(struct sock *sk, int timeout)
{
  sk->inuse = 1;
  sk->state = TCP_CLOSE;

  DPRINTF((DBG_RAW, "raw_close: deleting protocol %d\n",
	   ((struct inet_protocol *)sk->pair)->protocol));

  if (inet_del_protocol((struct inet_protocol *)sk->pair) < 0)
		DPRINTF((DBG_RAW, "raw_close: del_protocol failed.\n"));
  kfree_s((void *)sk->pair, sizeof (struct inet_protocol));
  sk->pair = NULL;
  release_sock(sk);
}


static int
raw_init(struct sock *sk)
{
  struct inet_protocol *p;

  p = (struct inet_protocol *) kmalloc(sizeof (*p), GFP_KERNEL);
  if (p == NULL) return(-ENOMEM);

  p->handler = raw_rcv;
  p->protocol = sk->protocol;
  p->data = (void *)sk;
  p->err_handler = raw_err;
  inet_add_protocol(p);
   
  /* We need to remember this somewhere. */
  sk->pair = (struct sock *)p;

  DPRINTF((DBG_RAW, "raw init added protocol %d\n", sk->protocol));

  return(0);
}


/*
 * This should be easy, if there is something there
 * we return it, otherwise we block.
 */
int
raw_recvfrom(struct sock *sk, unsigned char *to, int len,
	     int noblock, unsigned flags, struct sockaddr_in *sin,
	     int *addr_len)
{
  int copied=0;
  struct sk_buff *skb;

  DPRINTF((DBG_RAW, "raw_recvfrom (sk=%X, to=%X, len=%d, noblock=%d, flags=%X,\n"
	   "              sin=%X, addr_len=%X)\n",
		sk, to, len, noblock, flags, sin, addr_len));

  if (len == 0) return(0);
  if (len < 0) return(-EINVAL);

  if (sk->shutdown & RCV_SHUTDOWN) return(0);
  if (addr_len) {
	verify_area(VERIFY_WRITE, addr_len, sizeof(*addr_len));
	put_fs_long(sizeof(*sin), addr_len);
  }
  sk->inuse = 1;
  while (sk->rqueue == NULL) {
	if (noblock) {
		release_sock(sk);
		if (copied) return(copied);
		return(-EAGAIN);
	}
	release_sock(sk);
	cli();
	if (sk->rqueue == NULL) {
		interruptible_sleep_on(sk->sleep);
		if (current->signal & ~current->blocked) {
			sti();
			return(-ERESTARTSYS);
		}
	}
	sk->inuse = 1;
	sti();
  }
  skb = sk->rqueue;

  if (!(flags & MSG_PEEK)) {
	if (skb->next == skb) {
		sk->rqueue = NULL;
	} else {
		sk->rqueue = (struct sk_buff *)sk->rqueue ->next;
		skb->prev->next = skb->next;
		skb->next->prev = skb->prev;
	}
  }
  copied = min(len, skb->len);
  verify_area(VERIFY_WRITE, to, copied);
  memcpy_tofs(to, skb->h.raw,  copied);

  /* Copy the address. */
  if (sin) {
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = skb->daddr;
	verify_area(VERIFY_WRITE, sin, sizeof(*sin));
	memcpy_tofs(sin, &addr, sizeof(*sin));
  }

  if (!(flags & MSG_PEEK)) {
	kfree_skb(skb, FREE_READ);
  }
  release_sock(sk);
  return (copied);
}


int
raw_read (struct sock *sk, unsigned char *buff, int len, int noblock,
	  unsigned flags)
{
  return(raw_recvfrom(sk, buff, len, noblock, flags, NULL, NULL));
}


struct proto raw_prot = {
  sock_wmalloc,
  sock_rmalloc,
  sock_wfree,
  sock_rfree,
  sock_rspace,
  sock_wspace,
  raw_close,
  raw_read,
  raw_write,
  raw_sendto,
  raw_recvfrom,
  ip_build_header,
  udp_connect,
  NULL,
  ip_queue_xmit,
  ip_retransmit,
  NULL,
  NULL,
  raw_rcv,
  udp_select,
  NULL,
  raw_init,
  NULL,
  128,
  0,
  {NULL,},
  "RAW"
};
