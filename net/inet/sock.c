/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		SOCK - AF_INET protocol family socket handler.
 *
 * Version:	@(#)sock.c	1.0.17	06/02/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include "inet.h"
#include "timer.h"
#include "dev.h"
#include "ip.h"
#include "protocol.h"
#include "arp.h"
#include "route.h"
#include "tcp.h"
#include "udp.h"
#include "skbuff.h"
#include "sock.h"
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include "raw.h"
#include "icmp.h"


int inet_debug = DBG_OFF;		/* INET module debug flag	*/


#define min(a,b)	((a)<(b)?(a):(b))
#define swap(a,b)	{unsigned long c; c=a; a=b; b=c;}


extern struct proto packet_prot;


void
print_sk(struct sock *sk)
{
  if (!sk) {
	printk("  print_sk(NULL)\n");
	return;
  }
  printk("  wmem_alloc = %d\n", sk->wmem_alloc);
  printk("  rmem_alloc = %d\n", sk->rmem_alloc);
  printk("  send_head = %X\n", sk->send_head);
  printk("  state = %d\n",sk->state);
  printk("  wback = %X, rqueue = %X\n", sk->wback, sk->rqueue);
  printk("  wfront = %X\n", sk->wfront);
  printk("  daddr = %X, saddr = %X\n", sk->daddr,sk->saddr);
  printk("  num = %d", sk->num);
  printk(" next = %X\n", sk->next);
  printk("  send_seq = %d, acked_seq = %d, copied_seq = %d\n",
	  sk->send_seq, sk->acked_seq, sk->copied_seq);
  printk("  rcv_ack_seq = %d, window_seq = %d, fin_seq = %d\n",
	  sk->rcv_ack_seq, sk->window_seq, sk->fin_seq);
  printk("  prot = %X\n", sk->prot);
  printk("  pair = %X, back_log = %X\n", sk->pair,sk->back_log);
  printk("  inuse = %d , blog = %d\n", sk->inuse, sk->blog);
  printk("  dead = %d delay_acks=%d\n", sk->dead, sk->delay_acks);
  printk("  retransmits = %d, timeout = %d\n", sk->retransmits, sk->timeout);
  printk("  cong_window = %d, packets_out = %d\n", sk->cong_window,
	  sk->packets_out);
  printk("  urg = %d shutdown=%d\n", sk->urg, sk->shutdown);
}


void
print_skb(struct sk_buff *skb)
{
  if (!skb) {
	printk("  print_skb(NULL)\n");
	return;
  }
  printk("  prev = %X, next = %X\n", skb->prev, skb->next);
  printk("  sk = %X link3 = %X\n", skb->sk, skb->link3);
  printk("  mem_addr = %X, mem_len = %d\n", skb->mem_addr, skb->mem_len);
  printk("  used = %d free = %d\n", skb->used,skb->free);
}


void
lock_skb(struct sk_buff *skb)
{
  if (skb->lock) {
	printk("*** bug more than one lock on sk_buff. \n");
  }
  skb->lock = 1;
}


void
kfree_skb(struct sk_buff *skb, int rw)
{
  if (skb == NULL) {
	printk("kfree_skb: skb = NULL\n");
	return;
  }

  if (skb->lock) {
	skb->free = 1;
	return;
  }
  skb->magic = 0;
  if (skb->sk) {
	if (rw) {
	     skb->sk->prot->rfree(skb->sk, skb->mem_addr, skb->mem_len);
	} else {
	     skb->sk->prot->wfree(skb->sk, skb->mem_addr, skb->mem_len);
	}
  } else {
	kfree_s(skb->mem_addr, skb->mem_len);
  }
}


void
unlock_skb(struct sk_buff *skb, int rw)
{
  if (skb->lock != 1) {
	printk("INET: *** bug unlocking non-locked sk_buff. \n");
  }
  skb->lock = 0;
  if (skb->free) kfree_skb(skb, rw);
}


static int
sk_inuse(struct proto *prot, int num)
{
  struct sock *sk;

  for(sk = prot->sock_array[num & (SOCK_ARRAY_SIZE -1 )];
      sk != NULL;
      sk=sk->next) {
	if (sk->num == num) return(1);
  }
  return(0);
}


unsigned short
get_new_socknum(struct proto *prot, unsigned short base)
{
  static int start=0;

  /*
   * Used to cycle through the port numbers so the
   * chances of a confused connection drop.
   */
  int i, j;
  int best = 0;
  int size = 32767; /* a big num. */
  struct sock *sk;

  if (base == 0) base = PROT_SOCK+1+(start % 1024);
  if (base <= PROT_SOCK) {
	base += PROT_SOCK+(start % 1024);
  }

  /* Now look through the entire array and try to find an empty ptr. */
  for(i=0; i < SOCK_ARRAY_SIZE; i++) {
	j = 0;
	sk = prot->sock_array[(i+base+1) &(SOCK_ARRAY_SIZE -1)];
	while(sk != NULL) {
		sk = sk->next;
		j++;
	}
	if (j == 0) {
		start =(i+1+start )%1024;
		DPRINTF((DBG_INET, "get_new_socknum returning %d, start = %d\n",
							i + base + 1, start));
		return(i+base+1);
	}
	if (j < size) {
		best = i;
		size = j;
	}
  }

  /* Now make sure the one we want is not in use. */
  while(sk_inuse(prot, base +best+1)) {
	best += SOCK_ARRAY_SIZE;
  }
  DPRINTF((DBG_INET, "get_new_socknum returning %d, start = %d\n",
						best + base + 1, start));
  return(best+base+1);
}


void
put_sock(unsigned short num, struct sock *sk)
{
  struct sock *sk1;
  struct sock *sk2;
  int mask;

  DPRINTF((DBG_INET, "put_sock(num = %d, sk = %X\n", num, sk));
  sk->num = num;
  sk->next = NULL;
  num = num &(SOCK_ARRAY_SIZE -1);

  /* We can't have an interupt re-enter here. */
  cli();
  if (sk->prot->sock_array[num] == NULL) {
	sk->prot->sock_array[num] = sk;
	sti();
	return;
  }
  sti();
  for(mask = 0xff000000; mask != 0xffffffff; mask = (mask >> 8) | mask) {
	if ((mask & sk->saddr) &&
	    (mask & sk->saddr) != (mask & 0xffffffff)) {
		mask = mask << 8;
		break;
	}
  }
  DPRINTF((DBG_INET, "mask = %X\n", mask));

  cli();
  sk1 = sk->prot->sock_array[num];
  for(sk2 = sk1; sk2 != NULL; sk2=sk2->next) {
	if (!(sk2->saddr & mask)) {
		if (sk2 == sk1) {
			sk->next = sk->prot->sock_array[num];
			sk->prot->sock_array[num] = sk;
			sti();
			return;
		}
		sk->next = sk2;
		sk1->next= sk;
		sti();
		return;
	}
	sk1 = sk2;
  }

  /* Goes at the end. */
  sk->next = NULL;
  sk1->next = sk;
  sti();
}


static void
remove_sock(struct sock *sk1)
{
  struct sock *sk2;

  DPRINTF((DBG_INET, "remove_sock(sk1=%X)\n", sk1));
  if (!sk1) {
	printk("sock.c: remove_sock: sk1 == NULL\n");
	return;
  }

  if (!sk1->prot) {
	printk("sock.c: remove_sock: sk1->prot == NULL\n");
	return;
  }

  /* We can't have this changing out from under us. */
  cli();
  sk2 = sk1->prot->sock_array[sk1->num &(SOCK_ARRAY_SIZE -1)];
  if (sk2 == sk1) {
	sk1->prot->sock_array[sk1->num &(SOCK_ARRAY_SIZE -1)] = sk1->next;
	sti();
	return;
  }

  while(sk2 && sk2->next != sk1) {
	sk2 = sk2->next;
  }

  if (sk2) {
	sk2->next = sk1->next;
	sti();
	return;
  }
  sti();

  if (sk1->num != 0) DPRINTF((DBG_INET, "remove_sock: sock not found.\n"));
}


void
destroy_sock(struct sock *sk)
{
  struct sk_buff *skb;

  DPRINTF((DBG_INET, "destroying socket %X\n", sk));
  sk->inuse = 1;			/* just to be safe. */

  /* Incase it's sleeping somewhere. */
  if (!sk->dead) wake_up(sk->sleep);

  remove_sock(sk);

  /* Now we can no longer get new packets. */
  delete_timer((struct timer *)&sk->time_wait);

  if (sk->send_tmp != NULL) kfree_skb(sk->send_tmp, FREE_WRITE);

  /* Cleanup up the write buffer. */
  for(skb = sk->wfront; skb != NULL; ) {
	struct sk_buff *skb2;

	skb2=(struct sk_buff *)skb->next;
	if (skb->magic != TCP_WRITE_QUEUE_MAGIC) {
		printk("sock.c:destroy_sock write queue with bad magic(%X)\n",
								skb->magic);
		break;
	}
	kfree_skb(skb, FREE_WRITE);
	skb = skb2;
  }

  sk->wfront = NULL;
  sk->wback = NULL;

  if (sk->rqueue != NULL) {
	skb = sk->rqueue;
	do {
		struct sk_buff *skb2;

		skb2 = (struct sk_buff *)skb->next;

		/*
		 * This will take care of closing sockets that were
		 * listening and didn't accept everything.
		 */
		if (skb->sk != NULL && skb->sk != sk) {
			skb->sk->dead = 1;
			skb->sk->prot->close(skb->sk, 0);
		}
		kfree_skb(skb, FREE_READ);
		skb = skb2;
	} while(skb != sk->rqueue);
  }
  sk->rqueue = NULL;

  /* Now we need to clean up the send head. */
  for(skb = sk->send_head; skb != NULL; ) {
	struct sk_buff *skb2;

	/*
	 * We need to remove skb from the transmit queue,
	 * or maybe the arp queue.
	 */
	cli();
	/* see if it's in a transmit queue. */
	/* this can be simplified quite a bit.  Look */
	/* at tcp.c:tcp_ack to see how. */
	if (skb->next != NULL) {
		int i;

		if (skb->next != skb) {
			skb->next->prev = skb->prev;
			skb->prev->next = skb->next;

			if (skb == arp_q) {
				if (skb->magic != ARP_QUEUE_MAGIC) {
					sti();
					printk("sock.c: destroy_sock skb on arp queue with"
						"bas magic(%X)\n", skb->magic);
					cli();
					arp_q = NULL;
					continue;
				}
				arp_q = skb->next;
			} else {
				for(i = 0; i < DEV_NUMBUFFS; i++) {
					if (skb->dev &&
					    skb->dev->buffs[i] == skb) {
						if (skb->magic != DEV_QUEUE_MAGIC) {
							sti();
							printk("sock.c: destroy sock skb on dev queue"
								"with bad magic(%X)\n", skb->magic);
							cli();
							break;
						}
						skb->dev->buffs[i]= skb->next;
						break;
					}
				}
			}
		} else {
			if (skb == arp_q) {
				if (skb->magic != ARP_QUEUE_MAGIC) {
					sti();
					printk("sock.c: destroy_sock skb on arp queue with"
						"bas magic(%X)\n", skb->magic);
					cli();
				}
				arp_q = NULL;
			} else {
				for(i = 0; i < DEV_NUMBUFFS; i++) {
					if (skb->dev &&
					    skb->dev->buffs[i] == skb) {
			    			if (skb->magic != DEV_QUEUE_MAGIC) {
							sti();
							printk("sock.c: destroy sock skb on dev queue"
								"with bad magic(%X)\n", skb->magic);
							cli();
							break;
			      			}
						skb->dev->buffs[i]= NULL;
						break;
					}
				}
			}
		}
	}
	skb->dev = NULL;
	sti();
	skb2 = (struct sk_buff *)skb->link3;
	kfree_skb(skb, FREE_WRITE);
	skb = skb2;
  }
  sk->send_head = NULL;

  /* And now the backlog. */
  if (sk->back_log != NULL) {
	/* this should never happen. */
	printk("cleaning back_log. \n");
	cli();
	skb = (struct sk_buff *)sk->back_log;
	do {
		struct sk_buff *skb2;

		skb2 = (struct sk_buff *)skb->next;
		kfree_skb(skb, FREE_READ);
		skb = skb2;
	} while(skb != sk->back_log);
	sti();
  }
  sk->back_log = NULL;

  /* Now if it has a half accepted/ closed socket. */
  if (sk->pair) {
	sk->pair->dead = 1;
	sk->pair->prot->close(sk->pair, 0);
	sk->pair = NULL;
  }

  /*
   * Now if everything is gone we can free the socket
   * structure, otherwise we need to keep it around until
   * everything is gone.
   */
  if (sk->rmem_alloc == 0 && sk->wmem_alloc == 0) {
	kfree_s((void *)sk,sizeof(*sk));
  } else {
	/* this should never happen. */
	/* actually it can if an ack has just been sent. */
	DPRINTF((DBG_INET, "possible memory leak in socket = %X\n", sk));
	sk->destroy = 1;
	sk->ack_backlog = 0;
	sk->inuse = 0;
	sk->time_wait.len = SOCK_DESTROY_TIME;
	sk->timeout = TIME_DESTROY;
	reset_timer((struct timer *)&sk->time_wait);
  }
  DPRINTF((DBG_INET, "leaving destroy_sock\n"));
}


static int
inet_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  switch(cmd) {
	case F_SETOWN:
		/*
		 * This is a little restrictive, but it's the only
		 * way to make sure that you can't send a sigurg to
		 * another process.
		 */
		if (!suser() && current->pgrp != -arg &&
				current->pid != arg) return(-EPERM);
		sk->proc = arg;
		return(0);
	case F_GETOWN:
		return(sk->proc);
	default:
		return(-EINVAL);
  }
}


static int
inet_setsockopt(struct socket *sock, int level, int optname,
		    char *optval, int optlen)
{
  struct sock *sk;
  int val;

  /* This should really pass things on to the other levels. */
  if (level != SOL_SOCKET) return(-EOPNOTSUPP);

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }
  if (optval == NULL) return(-EINVAL);

  /* verify_area(VERIFY_WRITE, optval, sizeof(int));*/
  val = get_fs_long((unsigned long *)optval);
  switch(optname) {
	case SO_TYPE:
	case SO_ERROR:
	  	return(-ENOPROTOOPT);

	case SO_DEBUG:	/* not implemented. */
	case SO_DONTROUTE:
	case SO_BROADCAST:
	case SO_SNDBUF:
	case SO_RCVBUF:
		return(0);

	case SO_REUSEADDR:
		if (val) sk->reuse = 1;
		  else sk->reuse = 0;
		return(0);

	case SO_KEEPALIVE:
		if (val) sk->keepopen = 1;
		  else sk->keepopen = 0;
		return(0);

	 case SO_OOBINLINE:
		if (val) sk->urginline = 1;
		  else sk->urginline = 0;
		return(0);

	 case SO_NO_CHECK:
		if (val) sk->no_check = 1;
		  else sk->no_check = 0;
		return(0);

	 case SO_PRIORITY:
		if (val >= 0 && val < DEV_NUMBUFFS) {
			sk->priority = val;
		} else {
			return(-EINVAL);
		}
		return(0);

	default:
	  	return(-ENOPROTOOPT);
  }
}


static int
inet_getsockopt(struct socket *sock, int level, int optname,
		    char *optval, int *optlen)
{
  struct sock *sk;
  int val;

  /* This should really pass things on to the other levels. */
  if (level != SOL_SOCKET) return(-EOPNOTSUPP);

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  switch(optname) {
	case SO_DEBUG:		/* not implemented. */
	case SO_DONTROUTE:
	case SO_BROADCAST:
	case SO_SNDBUF:
	case SO_RCVBUF:
		val = 0;
		break;

	case SO_REUSEADDR:
		val = sk->reuse;
		break;

	case SO_KEEPALIVE:
		val = sk->keepopen;
		break;

	case SO_TYPE:
		if (sk->prot == &tcp_prot) val = SOCK_STREAM;
		  else val = SOCK_DGRAM;
		break;

	case SO_ERROR:
		val = sk->err;
		sk->err = 0;
		break;

	case SO_OOBINLINE:
		val = sk->urginline;
		break;

	case SO_NO_CHECK:
		val = sk->no_check;
		break;

	case SO_PRIORITY:
		val = sk->priority;
		break;

	default:
		return(-ENOPROTOOPT);
  }
  verify_area(VERIFY_WRITE, optlen, sizeof(int));
  put_fs_long(sizeof(int),(unsigned long *) optlen);

  verify_area(VERIFY_WRITE, optval, sizeof(int));
  put_fs_long(val,(unsigned long *)optval);

  return(0);
}


static int
inet_listen(struct socket *sock, int backlog)
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  /* We may need to bind the socket. */
  if (sk->num == 0) {
	sk->num = get_new_socknum(sk->prot, 0);
	if (sk->num == 0) return(-EAGAIN);
	put_sock(sk->num, sk);
	sk->dummy_th.source = ntohs(sk->num);
  }

  /* We might as well re use these. */ 
  sk->max_ack_backlog = backlog;
  if (sk->state != TCP_LISTEN) {
	sk->ack_backlog = 0;
	sk->state = TCP_LISTEN;
  }
  return(0);
}


static int
inet_create(struct socket *sock, int protocol)
{
  struct sock *sk;
  struct proto *prot;
  int err;

  sk = (struct sock *) kmalloc(sizeof(*sk), GFP_KERNEL);
  if (sk == NULL) return(-ENOMEM);
  sk->num = 0;

  switch(sock->type) {
	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		if (protocol && protocol != IPPROTO_TCP) {
			kfree_s((void *)sk, sizeof(*sk));
			return(-EPROTONOSUPPORT);
		}
		sk->no_check = TCP_NO_CHECK;
		prot = &tcp_prot;
		break;

	case SOCK_DGRAM:
		if (protocol && protocol != IPPROTO_UDP) {
			kfree_s((void *)sk, sizeof(*sk));
			return(-EPROTONOSUPPORT);
		}
		sk->no_check = UDP_NO_CHECK;
		prot=&udp_prot;
		break;
      
	case SOCK_RAW:
		if (!suser()) {
			kfree_s((void *)sk, sizeof(*sk));
			return(-EPERM);
		}
		if (!protocol) {
			kfree_s((void *)sk, sizeof(*sk));
			return(-EPROTONOSUPPORT);
		}
		prot = &raw_prot;
		sk->reuse = 1;
		sk->no_check = 0;	/*
					 * Doesn't matter no checksum is
					 * preformed anyway.
					 */
		sk->num = protocol;
		break;

	case SOCK_PACKET:
		if (!suser()) {
			kfree_s((void *)sk, sizeof(*sk));
			return(-EPERM);
		}
		if (!protocol) {
			kfree_s((void *)sk, sizeof(*sk));
			return(-EPROTONOSUPPORT);
		}
		prot = &packet_prot;
		sk->reuse = 1;
		sk->no_check = 0;	/* Doesn't matter no checksum is
					 * preformed anyway.
					 */
		sk->num = protocol;
		break;

	default:
		kfree_s((void *)sk, sizeof(*sk));
		return(-ESOCKTNOSUPPORT);
  }
  sk->protocol = protocol;
  sk->wmem_alloc = 0;
  sk->rmem_alloc = 0;
  sk->pair = NULL;
  sk->opt = NULL;
  sk->send_seq = 0;
  sk->acked_seq = 0;
  sk->copied_seq = 0;
  sk->fin_seq = 0;
  sk->proc = 0;
  sk->rtt = TCP_WRITE_TIME;
  sk->mdev = 0;
  sk->backoff = 0;
  sk->packets_out = 0;
  sk->cong_window = 1; /* start with only sending one packet at a time. */
  sk->exp_growth = 1;  /* if set cong_window grow exponentially every time
			  we get an ack. */
  sk->urginline = 0;
  sk->intr = 0;
  sk->linger = 0;
  sk->destroy = 0;
  sk->reuse = 0;
  sk->priority = 1;
  sk->shutdown = 0;
  sk->urg = 0;
  sk->keepopen = 0;
  sk->done = 0;
  sk->ack_backlog = 0;
  sk->window = 0;
  sk->bytes_rcv = 0;
  sk->state = TCP_CLOSE;
  sk->dead = 0;
  sk->ack_timed = 0;
  sk->send_tmp = NULL;
  sk->mss = 0; /* we will try not to send any packets smaller than this. */

  /* this is how many unacked bytes we will accept for this socket.  */
  sk->max_unacked = 2048; /* needs to be at most 2 full packets. */

  /* how many packets we should send before forcing an ack. 
     if this is set to zero it is the same as sk->delay_acks = 0 */
  sk->max_ack_backlog = 0;
  sk->inuse = 0;
  sk->delay_acks = 0;
  sk->wback = NULL;
  sk->wfront = NULL;
  sk->rqueue = NULL;
  sk->mtu = 576;
  sk->prot = prot;
  sk->sleep = sock->wait;
  sk->daddr = 0;
  sk->saddr = my_addr();
  sk->err = 0;
  sk->next = NULL;
  sk->pair = NULL;
  sk->send_tail = NULL;
  sk->send_head = NULL;
  sk->time_wait.len = TCP_CONNECT_TIME;
  sk->time_wait.when = 0;
  sk->time_wait.sk = sk;
  sk->time_wait.next = NULL;
  sk->timeout = 0;
  sk->back_log = NULL;
  sk->blog = 0;
  sock->data =(void *) sk;
  sk->dummy_th.doff = sizeof(sk->dummy_th)/4;
  sk->dummy_th.res1=0;
  sk->dummy_th.res2=0;
  sk->dummy_th.urg_ptr = 0;
  sk->dummy_th.fin = 0;
  sk->dummy_th.syn = 0;
  sk->dummy_th.rst = 0;
  sk->dummy_th.psh = 0;
  sk->dummy_th.ack = 0;
  sk->dummy_th.urg = 0;
  sk->dummy_th.dest = 0;

  if (sk->num) {
	/*
	 * It assumes that any protocol which allows
	 * the user to assign a number at socket
	 * creation time automatically
	 * shares.
	 */
	put_sock(sk->num, sk);
	sk->dummy_th.source = ntohs(sk->num);
  }

  if (sk->prot->init) {
	err = sk->prot->init(sk);
	if (err != 0) {
		destroy_sock(sk);
		return(err);
	}
  }
  return(0);
}


static int
inet_dup(struct socket *newsock, struct socket *oldsock)
{
  return(inet_create(newsock,
		   ((struct sock *)(oldsock->data))->protocol));
}


/* The peer socket should always be NULL. */
static int
inet_release(struct socket *sock, struct socket *peer)
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) return(0);

  DPRINTF((DBG_INET, "inet_release(sock = %X, peer = %X)\n", sock, peer));
  wake_up(sk->sleep);

  /* Start closing the connection.  This may take a while. */
  /*
   * If linger is set, we don't return until the close
   * is complete.  Other wise we return immediately. The
   * actually closing is done the same either way.
   */
  if (sk->linger == 0) {
	sk->prot->close(sk,0);
	sk->dead = 1;
  } else {
	DPRINTF((DBG_INET, "sk->linger set.\n"));
	sk->prot->close(sk, 0);
	cli();
	while(sk->state != TCP_CLOSE) {
		interruptible_sleep_on(sk->sleep);
		if (current->signal & ~current->blocked) {
			sti();
			return(-ERESTARTSYS);
		}
	}
	sti();
	sk->dead = 1;
  }
  sk->inuse = 1;

  /* This will destroy it. */
  release_sock(sk);
  sock->data = NULL;
  DPRINTF((DBG_INET, "inet_release returning\n"));
  return(0);
}


/* this needs to be changed to dissallow
   the rebinding of sockets.   What error
   should it return? */

static int
inet_bind(struct socket *sock, struct sockaddr *uaddr,
	       int addr_len)
{
  struct sockaddr_in addr;
  struct sock *sk, *sk2;
  unsigned short snum;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  /* check this error. */
  if (sk->state != TCP_CLOSE) return(-EIO);
  if (sk->num != 0) return(-EINVAL);

  /* verify_area(VERIFY_WRITE, uaddr, addr_len);*/
  memcpy_fromfs(&addr, uaddr, min(sizeof(addr), addr_len));

#if 0	/* FIXME: */
  if (addr.sin_family && addr.sin_family != AF_INET) {
	/*
	 * This is really a bug in BSD which we need
	 * to emulate because ftp expects it.
	 */
	return(-EINVAL);
  }
#endif

  snum = ntohs(addr.sin_port);
  DPRINTF((DBG_INET, "bind sk =%X to port = %d\n", sk, snum));
  sk = (struct sock *) sock->data;

  /*
   * We can't just leave the socket bound wherever it is, it might
   * be bound to a privileged port. However, since there seems to
   * be a bug here, we will leave it if the port is not privileged.
   */
  if (snum == 0) {
/*	if (sk->num > PROT_SOCK) return(0); */
	snum = get_new_socknum(sk->prot, 0);
  }
  if (snum <= PROT_SOCK && !suser()) return(-EACCES);

  if (chk_addr(addr.sin_addr.s_addr) || addr.sin_addr.s_addr == 0)
					sk->saddr = addr.sin_addr.s_addr;

  DPRINTF((DBG_INET, "sock_array[%d] = %X:\n", snum &(SOCK_ARRAY_SIZE -1),
	  		sk->prot->sock_array[snum &(SOCK_ARRAY_SIZE -1)]));

  /* Make sure we are allowed to bind here. */
  cli();
outside_loop:
  for(sk2 = sk->prot->sock_array[snum & (SOCK_ARRAY_SIZE -1)];
					sk2 != NULL; sk2 = sk2->next) {
#if 	1	/* should be below! */
	if (sk2->num != snum) continue;
/*	if (sk2->saddr != sk->saddr) continue; */
#endif
	if (sk2->dead) {
		destroy_sock(sk2);
		goto outside_loop;
	}
	if (!sk->reuse) {
		sti();
		return(-EADDRINUSE);
	}
	if (!sk2->reuse) {
		sti();
		return(-EADDRINUSE);
	}
  }
  sti();

  remove_sock(sk);
  put_sock(snum, sk);
  sk->dummy_th.source = ntohs(sk->num);
  sk->daddr = 0;
  sk->dummy_th.dest = 0;
  return(0);
}


static int
inet_connect(struct socket *sock, struct sockaddr * uaddr,
		  int addr_len, int flags)
{
  struct sock *sk;
  int err;

  sock->conn = NULL;
  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  if (sock->state != SS_CONNECTING) {
	/* We may need to bind the socket. */
	if (sk->num == 0) {
		sk->num = get_new_socknum(sk->prot, 0);
		if (sk->num == 0) return(-EAGAIN);
		put_sock(sk->num, sk);
		sk->dummy_th.source = htons(sk->num);
	}

	if (sk->prot->connect == NULL) return(-EOPNOTSUPP);
  
	err = sk->prot->connect(sk, (struct sockaddr_in *)uaddr, addr_len);
	if (err < 0) return(err);
  
	sock->state = SS_CONNECTING;
  }

  if (sk->state != TCP_ESTABLISHED &&(flags & O_NONBLOCK)) return(-EINPROGRESS);

  cli(); /* avoid the race condition */
  while(sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV) {
	interruptible_sleep_on(sk->sleep);
	if (current->signal & ~current->blocked) {
		sti();
		return(-ERESTARTSYS);
	}
  }
  sti();
  sock->state = SS_CONNECTED;

  if (sk->state != TCP_ESTABLISHED && sk->err) {
	sock->state = SS_UNCONNECTED;
	return(-sk->err);
  }
  return(0);
}


static int
inet_socketpair(struct socket *sock1, struct socket *sock2)
{
  return(-EOPNOTSUPP);
}


static int
inet_accept(struct socket *sock, struct socket *newsock, int flags)
{
  struct sock *sk1, *sk2;

  sk1 = (struct sock *) sock->data;
  if (sk1 == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  /*
   * We've been passed an extra socket.
   * We need to free it up because the tcp module creates
   * it's own when it accepts one.
   */
  if (newsock->data) kfree_s(newsock->data, sizeof(struct sock));
  newsock->data = NULL;

  if (sk1->prot->accept == NULL) return(-EOPNOTSUPP);

  /* Restore the state if we have been interrupted, and then returned. */
  if (sk1->pair != NULL ) {
	sk2 = sk1->pair;
	sk1->pair = NULL;
  } else {
	sk2 = sk1->prot->accept(sk1,flags);
	if (sk2 == NULL) {
		if (sk1->err <= 0)
			printk("Warning sock.c:sk1->err <= 0.  Returning non-error.\n");
		return(-sk1->err);
	}
  }
  newsock->data = (void *)sk2;
  sk2->sleep = newsock->wait;
  newsock->conn = NULL;
  if (flags & O_NONBLOCK) return(0);

  cli(); /* avoid the race. */
  while(sk2->state == TCP_SYN_RECV) {
	interruptible_sleep_on(sk2->sleep);
	if (current->signal & ~current->blocked) {
		sti();
		sk1->pair = sk2;
		sk2->sleep = NULL;
		newsock->data = NULL;
		return(-ERESTARTSYS);
	}
  }
  sti();

  if (sk2->state != TCP_ESTABLISHED && sk2->err > 0) {
	int err;

	err = -sk2->err;
	destroy_sock(sk2);
	newsock->data = NULL;
	return(err);
  }
  newsock->state = SS_CONNECTED;
  return(0);
}


static int
inet_getname(struct socket *sock, struct sockaddr *uaddr,
		 int *uaddr_len, int peer)
{
  struct sockaddr_in sin;
  struct sock *sk;
  int len;

  len = get_fs_long(uaddr_len);

  /* Check this error. */
  if (len < sizeof(sin)) return(-EINVAL);

  sin.sin_family = AF_INET;
  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }
  if (peer) {
	if (!tcp_connected(sk->state)) return(-ENOTCONN);
	sin.sin_port = sk->dummy_th.dest;
	sin.sin_addr.s_addr = sk->daddr;
  } else {
	sin.sin_port = sk->dummy_th.source;
	if (sk->saddr == 0) sin.sin_addr.s_addr = my_addr();
	  else sin.sin_addr.s_addr = sk->saddr;
  }
  len = sizeof(sin);
  verify_area(VERIFY_WRITE, uaddr, len);
  memcpy_tofs(uaddr, &sin, sizeof(sin));
  verify_area(VERIFY_WRITE, uaddr_len, sizeof(len));
  put_fs_long(len, uaddr_len);
  return(0);
}


static int
inet_read(struct socket *sock, char *ubuf, int size, int noblock)
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  /* We may need to bind the socket. */
  if (sk->num == 0) {
	sk->num = get_new_socknum(sk->prot, 0);
	if (sk->num == 0) return(-EAGAIN);
	put_sock(sk->num, sk);
	sk->dummy_th.source = ntohs(sk->num);
  }
  return(sk->prot->read(sk, (unsigned char *) ubuf, size, noblock,0));
}


static int
inet_recv(struct socket *sock, void *ubuf, int size, int noblock,
	  unsigned flags)
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  /* We may need to bind the socket. */
  if (sk->num == 0) {
	sk->num = get_new_socknum(sk->prot, 0);
	if (sk->num == 0) return(-EAGAIN);
	put_sock(sk->num, sk);
	sk->dummy_th.source = ntohs(sk->num);
  }
  return(sk->prot->read(sk, (unsigned char *) ubuf, size, noblock, flags));
}


static int
inet_write(struct socket *sock, char *ubuf, int size, int noblock)
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }
  if (sk->shutdown & SEND_SHUTDOWN) {
	send_sig(SIGPIPE, current, 1);
	return(-EPIPE);
  }

  /* We may need to bind the socket. */
  if (sk->num == 0) {
	sk->num = get_new_socknum(sk->prot, 0);
	if (sk->num == 0) return(-EAGAIN);
	put_sock(sk->num, sk);
	sk->dummy_th.source = ntohs(sk->num);
  }

  return(sk->prot->write(sk, (unsigned char *) ubuf, size, noblock, 0));
}


static int
inet_send(struct socket *sock, void *ubuf, int size, int noblock, 
	       unsigned flags)
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }
  if (sk->shutdown & SEND_SHUTDOWN) {
	send_sig(SIGPIPE, current, 1);
	return(-EPIPE);
  }

  /* We may need to bind the socket. */
  if (sk->num == 0) {
	sk->num = get_new_socknum(sk->prot, 0);
	if (sk->num == 0) return(-EAGAIN);
	put_sock(sk->num, sk);
	sk->dummy_th.source = ntohs(sk->num);
  }

  return(sk->prot->write(sk, (unsigned char *) ubuf, size, noblock, flags));
}


static int
inet_sendto(struct socket *sock, void *ubuf, int size, int noblock, 
	    unsigned flags, struct sockaddr *sin, int addr_len)
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }
  if (sk->shutdown & SEND_SHUTDOWN) {
	send_sig(SIGPIPE, current, 1);
	return(-EPIPE);
  }

  if (sk->prot->sendto == NULL) return(-EOPNOTSUPP);

  /* We may need to bind the socket. */
  if (sk->num == 0) {
	sk->num = get_new_socknum(sk->prot, 0);
	if (sk->num == 0) return(-EAGAIN);
	put_sock(sk->num, sk);
	sk->dummy_th.source = ntohs(sk->num);
  }

  return(sk->prot->sendto(sk, (unsigned char *) ubuf, size, noblock, flags, 
			   (struct sockaddr_in *)sin, addr_len));
}


static int
inet_recvfrom(struct socket *sock, void *ubuf, int size, int noblock, 
		   unsigned flags, struct sockaddr *sin, int *addr_len )
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  if (sk->prot->recvfrom == NULL) return(-EOPNOTSUPP);

  /* We may need to bind the socket. */
  if (sk->num == 0) {
	sk->num = get_new_socknum(sk->prot, 0);
	if (sk->num == 0) return(-EAGAIN);
	put_sock(sk->num, sk);
	sk->dummy_th.source = ntohs(sk->num);
  }

  return(sk->prot->recvfrom(sk, (unsigned char *) ubuf, size, noblock, flags,
			     (struct sockaddr_in*)sin, addr_len));
}


static int
inet_shutdown(struct socket *sock, int how)
{
  struct sock *sk;

  /*
   * This should really check to make sure
   * the socket is a TCP socket.
   */
  how++; /* maps 0->1 has the advantage of making bit 1 rcvs and
		       1->2 bit 2 snds.
		       2->3 */
  if (how & ~SHUTDOWN_MASK) return(-EINVAL);
  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }
  if (sock->state == SS_CONNECTING && sk->state == TCP_ESTABLISHED)
						sock->state = SS_CONNECTED;

  if (!tcp_connected(sk->state)) return(-ENOTCONN);
  sk->shutdown |= how;
  if (sk->prot->shutdown) sk->prot->shutdown(sk, how);
  return(0);
}


static int
inet_select(struct socket *sock, int sel_type, select_table *wait )
{
  struct sock *sk;

  sk = (struct sock *) sock->data;
  if (sk == NULL) {
	printk("Warning: sock->data = NULL: %d\n" ,__LINE__);
	return(0);
  }

  if (sk->prot->select == NULL) {
	DPRINTF((DBG_INET, "select on non-selectable socket.\n"));
	return(0);
  }
  return(sk->prot->select(sk, sel_type, wait));
}


static int
inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
  struct sock *sk;

  DPRINTF((DBG_INET, "INET: in inet_ioctl\n"));
  sk = NULL;
  if (sock && (sk = (struct sock *) sock->data) == NULL) {
	printk("AF_INET: Warning: sock->data = NULL: %d\n" , __LINE__);
	return(0);
  }

  switch(cmd) {
	case FIOSETOWN:
	case SIOCSPGRP:
		if (sk)
			sk->proc = get_fs_long((int *) arg);
		return(0);
	case FIOGETOWN:
	case SIOCGPGRP:
		if (sk) {
			verify_area(VERIFY_WRITE,(void *) arg, sizeof(long));
			put_fs_long(sk->proc,(int *)arg);
		}
		return(0);
#if 0	/* FIXME: */
	case SIOCATMARK:
		printk("AF_INET: ioctl(SIOCATMARK, 0x%08X)\n",(void *) arg);
		return(-EINVAL);
#endif

	case DDIOCSDBG:
		return(dbg_ioctl((void *) arg, DBG_INET));

	case SIOCADDRT:
	case SIOCDELRT:
		return(rt_ioctl(cmd,(void *) arg));

	case SIOCDARP:
	case SIOCGARP:
	case SIOCSARP:
		return(arp_ioctl(cmd,(void *) arg));

	case IP_SET_DEV:
	case SIOCGIFCONF:
	case SIOCGIFFLAGS:
	case SIOCSIFFLAGS:
	case SIOCGIFADDR:
	case SIOCSIFADDR:
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	case SIOCGIFMETRIC:
	case SIOCSIFMETRIC:
	case SIOCGIFMEM:
	case SIOCSIFMEM:
	case SIOCGIFMTU:
	case SIOCSIFMTU:
	case SIOCSIFLINK:
		return(dev_ioctl(cmd,(void *) arg));

	default:
		if (!sk || !sk->prot->ioctl) return(-EINVAL);
		return(sk->prot->ioctl(sk, cmd, arg));
  }
  /*NOTREACHED*/
  return(0);
}


void *
sock_wmalloc(struct sock *sk, unsigned long size, int force,
	     int priority)
{
  if (sk) {
	if (sk->wmem_alloc + size < SK_WMEM_MAX || force) {
		cli();
		sk->wmem_alloc+= size;
		sti();
		return(kmalloc(size, priority));
	}
	DPRINTF((DBG_INET, "sock_wmalloc(%X,%d,%d,%d) returning NULL\n",
						sk, size, force, priority));
	return(NULL);
  }
  return(kmalloc(size, priority));
}


void *
sock_rmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
  if (sk) {
	if (sk->rmem_alloc + size < SK_RMEM_MAX || force) {
		void *c = kmalloc(size, priority);
		cli();
		if (c) sk->rmem_alloc += size;
		sti();
		return(c);
	}
	DPRINTF((DBG_INET, "sock_rmalloc(%X,%d,%d,%d) returning NULL\n",
						sk,size,force, priority));
	return(NULL);
  }
  return(kmalloc(size, priority));
}


unsigned long
sock_rspace(struct sock *sk)
{
  int amt;

  if (sk != NULL) {
	if (sk->rmem_alloc >= SK_RMEM_MAX-2*MIN_WINDOW) return(0);
	amt = min((SK_RMEM_MAX-sk->rmem_alloc)/2-MIN_WINDOW, MAX_WINDOW);
	if (amt < 0) return(0);
	return(amt);
  }
  return(0);
}


unsigned long
sock_wspace(struct sock *sk)
{
  if (sk != NULL) {
	if (sk->shutdown & SEND_SHUTDOWN) return(0);
	if (sk->wmem_alloc >= SK_WMEM_MAX) return(0);
	return(SK_WMEM_MAX-sk->wmem_alloc );
  }
  return(0);
}


void
sock_wfree(struct sock *sk, void *mem, unsigned long size)
{
  DPRINTF((DBG_INET, "sock_wfree(sk=%X, mem=%X, size=%d)\n", sk, mem, size));

  kfree_s(mem, size);
  if (sk) {
	sk->wmem_alloc -= size;

	/* In case it might be waiting for more memory. */
	if (!sk->dead) wake_up(sk->sleep);
	if (sk->destroy && sk->wmem_alloc == 0 && sk->rmem_alloc == 0) {
		DPRINTF((DBG_INET,
			"recovered lost memory, destroying sock = %X\n", sk));
		delete_timer((struct timer *)&sk->time_wait);
		kfree_s((void *)sk, sizeof(*sk));
	}
	return;
  }
}


void
sock_rfree(struct sock *sk, void *mem, unsigned long size)
{
  DPRINTF((DBG_INET, "sock_rfree(sk=%X, mem=%X, size=%d)\n", sk, mem, size));

  kfree_s(mem, size);
  if (sk) {
	sk->rmem_alloc -= size;
	if (sk->destroy && sk->wmem_alloc == 0 && sk->rmem_alloc == 0) {
		delete_timer((struct timer *)&sk->time_wait);
		kfree_s((void *)sk, sizeof(*sk));
	}
  }
}


/*
 * This routine must find a socket given a TCP or UDP header.
 * Everyhting is assumed to be in net order.
 */
struct sock *get_sock(struct proto *prot, unsigned short num,
				unsigned long raddr,
				unsigned short rnum, unsigned long laddr)
{
  struct sock *s;
  unsigned short hnum;

  hnum = ntohs(num);
  DPRINTF((DBG_INET, "get_sock(prot=%X, num=%d, raddr=%X, rnum=%d, laddr=%X)\n",
	  prot, num, raddr, rnum, laddr));

  /*
   * SOCK_ARRAY_SIZE must be a power of two.  This will work better
   * than a prime unless 3 or more sockets end up using the same
   * array entry.  This should not be a problem because most
   * well known sockets don't overlap that much, and for
   * the other ones, we can just be careful about picking our
   * socket number when we choose an arbitrary one.
   */
  for(s = prot->sock_array[hnum & (SOCK_ARRAY_SIZE - 1)];
      s != NULL; s = s->next) {
	if (s->num == hnum) {
		/* We need to see if this is the socket that we want. */
		if (ip_addr_match(s->daddr, raddr) == 0) continue;
		if (s->dummy_th.dest != rnum && s->dummy_th.dest != 0) continue;
#if 1	/* C.E. Hawkins  ceh@eng.cam.ac.uk */
		if (s->prot != &udp_prot || prot != &udp_prot)
#endif
			if (ip_addr_match(s->saddr, laddr) == 0) continue;
		if (s->dead && (s->state == TCP_CLOSE)) continue;
		return(s);
	}
  }
  return(NULL);
}


void release_sock(struct sock *sk)
{
  if (!sk) {
	printk("sock.c: release_sock sk == NULL\n");
	return;
  }
  if (!sk->prot) {
	printk("sock.c: release_sock sk->prot == NULL\n");
	return;
  }

  if (sk->blog) return;

  /* See if we have any packets built up. */
  cli();
  sk->inuse = 1;
  while(sk->back_log != NULL) {
	struct sk_buff *skb;

	sk->blog = 1;
	skb =(struct sk_buff *)sk->back_log;
	DPRINTF((DBG_INET, "release_sock: skb = %X:\n", skb));
	if (skb->next != skb) {
		sk->back_log = skb->next;
		skb->prev->next = skb->next;
		skb->next->prev = skb->prev;
	} else {
		sk->back_log = NULL;
	}
	sti();
	DPRINTF((DBG_INET, "sk->back_log = %X\n", sk->back_log));
	if (sk->prot->rcv) sk->prot->rcv(skb, skb->dev, sk->opt,
					 skb->saddr, skb->len, skb->daddr, 1,

	/* Only used for/by raw sockets. */
	(struct inet_protocol *)sk->pair); 
	cli();
  }
  sk->blog = 0;
  sk->inuse = 0;
  sti();
  if (sk->dead && sk->state == TCP_CLOSE) {
	/* Should be about 2 rtt's */
	sk->time_wait.len = min(sk->rtt * 2, TCP_DONE_TIME);
	sk->timeout = TIME_DONE;
	reset_timer((struct timer *)&sk->time_wait);
  }
}


static int
inet_fioctl(struct inode *inode, struct file *file,
	 unsigned int cmd, unsigned long arg)
{
  int minor, ret;

  /* Extract the minor number on which we work. */
  minor = MINOR(inode->i_rdev);
  if (minor != 0) return(-ENODEV);

  /* Now dispatch on the minor device. */
  switch(minor) {
	case 0:		/* INET */
		ret = inet_ioctl(NULL, cmd, arg);
		break;
	case 1:		/* IP */
		ret = ip_ioctl(NULL, cmd, arg);
		break;
	case 2:		/* ICMP */
		ret = icmp_ioctl(NULL, cmd, arg);
		break;
	case 3:		/* TCP */
		ret = tcp_ioctl(NULL, cmd, arg);
		break;
	case 4:		/* UDP */
		ret = udp_ioctl(NULL, cmd, arg);
		break;
	default:
		ret = -ENODEV;
  }

  return(ret);
}


static struct file_operations inet_fops = {
  NULL,		/* LSEEK	*/
  NULL,		/* READ		*/
  NULL,		/* WRITE	*/
  NULL,		/* READDIR	*/
  NULL,		/* SELECT	*/
  inet_fioctl,	/* IOCTL	*/
  NULL,		/* MMAP		*/
  NULL,		/* OPEN		*/
  NULL		/* CLOSE	*/
};


static struct proto_ops inet_proto_ops = {
  AF_INET,

  inet_create,
  inet_dup,
  inet_release,
  inet_bind,
  inet_connect,
  inet_socketpair,
  inet_accept,
  inet_getname, 
  inet_read,
  inet_write,
  inet_select,
  inet_ioctl,
  inet_listen,
  inet_send,
  inet_recv,
  inet_sendto,
  inet_recvfrom,
  inet_shutdown,
  inet_setsockopt,
  inet_getsockopt,
  inet_fcntl,
};


void inet_proto_init(struct ddi_proto *pro)
{
  struct inet_protocol *p;
  int i;

  /* Set up our UNIX VFS major device. */
  if (register_chrdev(AF_INET_MAJOR, "af_inet", &inet_fops) < 0) {
	printk("5s: cannot register major device %d!\n",
					pro->name, AF_INET_MAJOR);
	return;
  }

  /* Tell SOCKET that we are alive... */
  (void) sock_register(inet_proto_ops.family, &inet_proto_ops);

  seq_offset = CURRENT_TIME*250;

  /* Add all the protocols. */
  for(i = 0; i < SOCK_ARRAY_SIZE; i++) {
	tcp_prot.sock_array[i] = NULL;
	udp_prot.sock_array[i] = NULL;
	raw_prot.sock_array[i] = NULL;
  }
  for(p = inet_protocol_base; p != NULL;) {
	struct inet_protocol *tmp;

	tmp = (struct inet_protocol *) p->next;
	inet_add_protocol(p);
	p = tmp;
  }

  /* Initialize the DEV module. */
  dev_init();

  /* Initialize the "Buffer Head" pointers. */
  bh_base[INET_BH].routine = inet_bh;
  timer_table[NET_TIMER].fn = net_timer;
}
