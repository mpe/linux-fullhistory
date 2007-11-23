/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The User Datagram Protocol (UDP).
 *
 * Version:	@(#)udp.c	1.0.13	06/02/93
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
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/termios.h>
#include <linux/mm.h>
#include "inet.h"
#include "timer.h"
#include "dev.h"
#include "ip.h"
#include "protocol.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "udp.h"
#include "icmp.h"


#define min(a,b)	((a)<(b)?(a):(b))


static void
print_udp(struct udphdr *uh)
{
  if (inet_debug != DBG_UDP) return;

  if (uh == NULL) {
	printk("(NULL)\n");
	return;
  }
  printk("UDP: source = %d, dest = %d\n", ntohs(uh->source), ntohs(uh->dest));
  printk("     len = %d, check = %d\n", ntohs(uh->len), ntohs(uh->check));
}


int
udp_select(struct sock *sk, int sel_type, select_table *wait)
{
  select_wait(sk->sleep, wait);
  switch(sel_type) {
	case SEL_IN:
		if (sk->rqueue != NULL) {
			return(1);
		}
		return(0);

	case SEL_OUT:
		if (sk->prot->wspace(sk) >= MIN_WRITE_SPACE) {
			return(1);
		}
		return(0);
	
	case SEL_EX:
		if (sk->err) return(1); /* can this ever happen? */
		return(0);
  }
  return(0);
}


/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  
 * header points to the first 8 bytes of the tcp header.  We need
 * to find the appropriate port.
 */
void
udp_err(int err, unsigned char *header, unsigned long daddr,
	unsigned long saddr, struct inet_protocol *protocol)
{
  struct udphdr *th;
  struct sock *sk;
   
  DPRINTF((DBG_UDP,
	"UDP: err(err=%d, header=%X, daddr=%X, saddr=%X, protocl=%X)\n",
					err, header, daddr, saddr, protocol));

  th = (struct udphdr *)header;
  sk = get_sock(&udp_prot, th->dest, saddr, th->source, daddr);

  if (sk == NULL) return;
  if (err & 0xff00 ==(ICMP_SOURCE_QUENCH << 8)) {
	if (sk->cong_window > 1) sk->cong_window = sk->cong_window/2;
	return;
  }

  sk->err = icmp_err_convert[err & 0xff].errno;

  /* It's only fatal if we have connected to them. */
  if (icmp_err_convert[err & 0xff].fatal && sk->state == TCP_ESTABLISHED) {
	sk->prot->close(sk, 0);
  }
}


static unsigned short
udp_check(struct udphdr *uh, int len,
	  unsigned long saddr, unsigned long daddr)
{
  unsigned long sum;

  DPRINTF((DBG_UDP, "UDP: check(uh=%X, len = %d, saddr = %X, daddr = %X)\n",
	   						uh, len, saddr, daddr));

  print_udp(uh);

  __asm__("\t addl %%ecx,%%ebx\n"
	  "\t adcl %%edx,%%ebx\n"
	  "\t adcl $0, %%ebx\n"
	  : "=b"(sum)
	  : "0"(daddr), "c"(saddr), "d"((ntohs(len) << 16) + IPPROTO_UDP*256)
	  : "cx","bx","dx" );

  if (len > 3) {
	__asm__("\tclc\n"
		"1:\n"
		"\t lodsl\n"
		"\t adcl %%eax, %%ebx\n"
		"\t loop 1b\n"
		"\t adcl $0, %%ebx\n"
		: "=b"(sum) , "=S"(uh)
		: "0"(sum), "c"(len/4) ,"1"(uh)
		: "ax", "cx", "bx", "si" );
  }

  /* Convert from 32 bits to 16 bits. */
  __asm__("\t movl %%ebx, %%ecx\n"
	  "\t shrl $16,%%ecx\n"
	  "\t addw %%cx, %%bx\n"
	  "\t adcw $0, %%bx\n"
	  : "=b"(sum)
	  : "0"(sum)
	  : "bx", "cx");

  /* Check for an extra word. */
  if ((len & 2) != 0) {
	__asm__("\t lodsw\n"
		"\t addw %%ax,%%bx\n"
		"\t adcw $0, %%bx\n"
		: "=b"(sum), "=S"(uh)
		: "0"(sum) ,"1"(uh)
		: "si", "ax", "bx");
  }

  /* Now check for the extra byte. */
  if ((len & 1) != 0) {
	__asm__("\t lodsb\n"
		"\t movb $0,%%ah\n"
		"\t addw %%ax,%%bx\n"
		"\t adcw $0, %%bx\n"
		: "=b"(sum)
		: "0"(sum) ,"S"(uh)
		: "si", "ax", "bx");
  }

  /* We only want the bottom 16 bits, but we never cleared the top 16. */
  return((~sum) & 0xffff);
}


static void
udp_send_check(struct udphdr *uh, unsigned long saddr, 
	       unsigned long daddr, int len, struct sock *sk)
{
  uh->check = 0;
  if (sk && sk->no_check) return;
  uh->check = udp_check(uh, len, saddr, daddr);
}


static int
udp_send(struct sock *sk, struct sockaddr_in *sin,
	 unsigned char *from, int len)
{
  struct sk_buff *skb;
  struct device *dev;
  struct udphdr *uh;
  unsigned char *buff;
  unsigned long saddr;
  int size, tmp;

  DPRINTF((DBG_UDP, "UDP: send(dst=%s:%d buff=%X len=%d)\n",
		in_ntoa(sin->sin_addr.s_addr), ntohs(sin->sin_port),
		from, len));

  /* Allocate a copy of the packet. */
  size = sizeof(struct sk_buff) + sk->prot->max_header + len;
  skb = (struct sk_buff *) sk->prot->wmalloc(sk, size, 0, GFP_KERNEL);
  if (skb == NULL) return(-ENOMEM);

  skb->lock     = 0;
  skb->mem_addr = skb;
  skb->mem_len  = size;
  skb->sk       = NULL;
  skb->free     = 1;
  skb->arp      = 0;

  /* Now build the IP and MAC header. */
  buff = (unsigned char *) (skb+1);
  saddr = 0;
  dev = NULL;
  DPRINTF((DBG_UDP, "UDP: >> IP_Header: %X -> %X dev=%X prot=%X len=%d\n",
			saddr, sin->sin_addr.s_addr, dev, IPPROTO_UDP, skb->mem_len));
  tmp = sk->prot->build_header(skb, saddr, sin->sin_addr.s_addr,
			       &dev, IPPROTO_UDP, sk->opt, skb->mem_len);
  if (tmp < 0 ) {
	sk->prot->wfree(sk, skb->mem_addr, skb->mem_len);
	return(tmp);
  }
  buff += tmp;
  saddr = dev->pa_addr;
  DPRINTF((DBG_UDP, "UDP: >> MAC+IP len=%d\n", tmp));

  skb->len = tmp + sizeof(struct udphdr) + len;	/* len + UDP + IP + MAC */
  skb->dev = dev;

  /*
   * This code used to hack in some form of fragmentation.
   * I removed that, since it didn't work anyway, and it made the
   * code a bad thing to read and understand. -FvK
   */
  if (len > dev->mtu) {
	printk("UDP: send: length %d > mtu %d (ignored)\n", len, dev->mtu);
	sk->prot->wfree(sk, skb->mem_addr, skb->mem_len);
	return(-EINVAL);
  }

  /* Fill in the UDP header. */
  uh = (struct udphdr *) buff;
  uh->len = htons(len + sizeof(struct udphdr));
  uh->source = sk->dummy_th.source;
  uh->dest = sin->sin_port;
  buff = (unsigned char *) (uh + 1);

  /* Copy the user data. */
  verify_area(VERIFY_WRITE, from, len);
  memcpy_fromfs(buff, from, len);

  /* Set up the UDP checksum. */
  udp_send_check(uh, saddr, sin->sin_addr.s_addr, skb->len - tmp, sk);

  /* Send the datagram to the interface. */
  sk->prot->queue_xmit(sk, dev, skb, 1);

  return(len);
}


static int
udp_sendto(struct sock *sk, unsigned char *from, int len, int noblock,
	   unsigned flags, struct sockaddr_in *usin, int addr_len)
{
  struct sockaddr_in sin;
  int tmp;

  DPRINTF((DBG_UDP, "UDP: sendto(len=%d, flags=%X)\n", len, flags));

  /* Check the flags. */
  if (flags) return(-EINVAL);
  if (len < 0) return(-EINVAL);
  if (len == 0) return(0);

  /* Get and verify the address. */
  if (usin) {
	if (addr_len < sizeof(sin)) return(-EINVAL);
	/* verify_area(VERIFY_WRITE, usin, sizeof(sin));*/
	memcpy_fromfs(&sin, usin, sizeof(sin));
	if (sin.sin_family && sin.sin_family != AF_INET) return(-EINVAL);
	if (sin.sin_port == 0) return(-EINVAL);
  } else {
	if (sk->state != TCP_ESTABLISHED) return(-EINVAL);
	sin.sin_family = AF_INET;
	sin.sin_port = sk->dummy_th.dest;
	sin.sin_addr.s_addr = sk->daddr;
  }
  sk->inuse = 1;

  /* Send the packet. */
  tmp = udp_send(sk, &sin, from, len);

  /* The datagram has been sent off.  Release the socket. */
  release_sock(sk);
  return(tmp);
}


static int
udp_write(struct sock *sk, unsigned char *buff, int len, int noblock,
	  unsigned flags)
{
  return(udp_sendto(sk, buff, len, noblock, flags, NULL, 0));
}


int
udp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
  switch(cmd) {
	case DDIOCSDBG:
		{
			int val;

			if (!suser()) return(-EPERM);
			verify_area(VERIFY_WRITE, (void *)arg, sizeof(int));
			val = get_fs_long((int *)arg);
			switch(val) {
				case 0:
					inet_debug = 0;
					break;
				case 1:
					inet_debug = DBG_UDP;
					break;
				default:
					return(-EINVAL);
			}
		}
		break;
	case TIOCOUTQ:
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = sk->prot->wspace(sk)/2;
			verify_area(VERIFY_WRITE,(void *)arg,
					sizeof(unsigned long));
			put_fs_long(amount,(unsigned long *)arg);
			return(0);
		}

	case TIOCINQ:
#if 0	/* FIXME: */
	case FIONREAD:
#endif
		{
			struct sk_buff *skb;
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = 0;
			skb = sk->rqueue;
			if (skb != NULL) {
				/*
				 * We will only return the amount
				 * of this packet since that is all
				 * that will be read.
				 */
				amount = skb->len;
			}
			verify_area(VERIFY_WRITE,(void *)arg,
						sizeof(unsigned long));
			put_fs_long(amount,(unsigned long *)arg);
			return(0);
		}

	default:
		return(-EINVAL);
  }
  return(0);
}


/*
 * This should be easy, if there is something there we\
 * return it, otherwise we block.
 */
int
udp_recvfrom(struct sock *sk, unsigned char *to, int len,
	     int noblock, unsigned flags, struct sockaddr_in *sin,
	     int *addr_len)
{
  int copied = 0;
  struct sk_buff *skb;

  if (len == 0) return(0);
  if (len < 0) return(-EINVAL);

  /*
   * This will pick up errors that occured while the program
   * was doing something else.
   */
  if (sk->err) {
	int err;

	err = -sk->err;
	sk->err = 0;
	return(err);
  }
  if (addr_len) {
	verify_area(VERIFY_WRITE, addr_len, sizeof(*addr_len));
	put_fs_long(sizeof(*sin), addr_len);
  }
  sk->inuse = 1;
  while(sk->rqueue == NULL) {
	if (sk->shutdown & RCV_SHUTDOWN) {
		return(0);
	}

	if (noblock) {
		release_sock(sk);
		return(-EAGAIN);
	}
	release_sock(sk);
	cli();
	if (sk->rqueue == NULL) {
		interruptible_sleep_on(sk->sleep);
		if (current->signal & ~current->blocked) {
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
		sk->rqueue =(struct sk_buff *)sk->rqueue ->next;
		skb->prev->next = skb->next;
		skb->next->prev = skb->prev;
	}
  }
  copied = min(len, skb->len);
  verify_area(VERIFY_WRITE, to, copied);
  memcpy_tofs(to, skb->h.raw + sizeof(struct udphdr), copied);

  /* Copy the address. */
  if (sin) {
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = skb->h.uh->source;
	addr.sin_addr.s_addr = skb->daddr;
	verify_area(VERIFY_WRITE, sin, sizeof(*sin));
	memcpy_tofs(sin, &addr, sizeof(*sin));
  }

  if (!(flags & MSG_PEEK)) {
	kfree_skb(skb, FREE_READ);
  }
  release_sock(sk);
  return(copied);
}


int
udp_read(struct sock *sk, unsigned char *buff, int len, int noblock,
	 unsigned flags)
{
  return(udp_recvfrom(sk, buff, len, noblock, flags, NULL, NULL));
}


int
udp_connect(struct sock *sk, struct sockaddr_in *usin, int addr_len)
{
  struct sockaddr_in sin;

  if (addr_len < sizeof(sin)) return(-EINVAL);

  /* verify_area(VERIFY_WRITE, usin, sizeof(sin)); */

  memcpy_fromfs(&sin, usin, sizeof(sin));
  if (sin.sin_family && sin.sin_family != AF_INET) return(-EAFNOSUPPORT);
  sk->daddr = sin.sin_addr.s_addr;
  sk->dummy_th.dest = sin.sin_port;
  sk->state = TCP_ESTABLISHED;
  return(0);
}


static void
udp_close(struct sock *sk, int timeout)
{
  sk->inuse = 1;
  sk->state = TCP_CLOSE;
  if (sk->dead) destroy_sock(sk);
    else release_sock(sk);
}


/* All we need to do is get the socket, and then do a checksum. */
int
udp_rcv(struct sk_buff *skb, struct device *dev, struct options *opt,
	unsigned long daddr, unsigned short len,
	unsigned long saddr, int redo, struct inet_protocol *protocol)
{
  struct sock *sk;
  struct udphdr *uh;

  uh = (struct udphdr *) skb->h.uh;
  sk = get_sock(&udp_prot, uh->dest, saddr, uh->source, daddr);
  if (sk == NULL) {
	if (chk_addr(daddr) == IS_MYADDR) 
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, dev);

	/*
	 * Hmm.  We got an UDP broadcast to a port to which we
	 * don't wanna listen.  The only thing we can do now is
	 * to ignore the packet... -FvK
	 */
	skb->sk = NULL;
	kfree_skb(skb, FREE_WRITE);
	return(0);
  }

  if (!redo) {
	if (uh->check && udp_check(uh, len, saddr, daddr)) {
		DPRINTF((DBG_UDP, "UDP: bad checksum\n"));
		skb->sk = NULL;
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}

	skb->sk = sk;
	skb->dev = dev;
	skb->len = len;

	/* These are supposed to be switched. */
	skb->daddr = saddr;
	skb->saddr = daddr;

	/* Now deal with the in use. */
	cli();
	if (sk->inuse) {
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

  /* Charge it to the socket. */
  if (sk->rmem_alloc + skb->mem_len >= SK_RMEM_MAX) {
	skb->sk = NULL;
	kfree_skb(skb, FREE_WRITE);
	release_sock(sk);
	return(0);
  }
  sk->rmem_alloc += skb->mem_len;

  /* At this point we should print the thing out. */
  DPRINTF((DBG_UDP, "<< \n"));
  print_udp(uh);

  /* Now add it to the data chain and wake things up. */
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
  skb->len = len - sizeof(*uh);

  if (!sk->dead) wake_up(sk->sleep);

  release_sock(sk);
  return(0);
}


struct proto udp_prot = {
  sock_wmalloc,
  sock_rmalloc,
  sock_wfree,
  sock_rfree,
  sock_rspace,
  sock_wspace,
  udp_close,
  udp_read,
  udp_write,
  udp_sendto,
  udp_recvfrom,
  ip_build_header,
  udp_connect,
  NULL,
  ip_queue_xmit,
  ip_retransmit,
  NULL,
  NULL,
  udp_rcv,
  udp_select,
  udp_ioctl,
  NULL,
  NULL,
  128,
  0,
  {NULL,},
  "UDP"
};
