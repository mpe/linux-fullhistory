/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	@(#)tcp.c	1.0.16	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or(at your option) any later version.
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/termios.h>
#include <linux/in.h>
#include <linux/fcntl.h>
#include "inet.h"
#include "timer.h"
#include "dev.h"
#include "ip.h"
#include "protocol.h"
#include "icmp.h"
#include "tcp.h"
#include "skbuff.h"
#include "sock.h"
#include "arp.h"
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <linux/mm.h>


#define tmax(a,b)(before((a),(b)) ?(b) :(a))
#define swap(a,b) {unsigned long c; c=a; a=b; b=c;}


static int 
min(unsigned int a, unsigned int b)
{
  if (a < b) return(a);
  return(b);
}


void
print_th(struct tcphdr *th)
{
  unsigned char *ptr;

  if (inet_debug != DBG_TCP) return;

  printk("TCP header:\n");
  ptr =(unsigned char *)(th + 1);
  printk("    source=%d, dest=%d, seq =%d, ack_seq = %d\n",
	ntohs(th->source), ntohs(th->dest),
	ntohl(th->seq), ntohl(th->ack_seq));
  printk("    fin=%d, syn=%d, rst=%d, psh=%d, ack=%d, urg=%d res1=%d res2=%d\n",
	th->fin, th->syn, th->rst, th->psh, th->ack,
	th->urg, th->res1, th->res2);
  printk("    window = %d, check = %d urg_ptr = %d\n",
	ntohs(th->window), ntohs(th->check), ntohs(th->urg_ptr));
  printk("    doff = %d\n", th->doff);
  printk("    options = %d %d %d %d\n", ptr[0], ptr[1], ptr[2], ptr[3]);
 }


/* This routine grabs the first thing off of a rcv queue. */
static struct sk_buff *
get_firstr(struct sock *sk)
{
  struct sk_buff *skb;

  skb = sk->rqueue;
  if (skb == NULL) return(NULL);
  sk->rqueue =(struct sk_buff *)skb->next;
  if (sk->rqueue == skb) {
	sk->rqueue = NULL;
  } else {
	sk->rqueue->prev = skb->prev;
	sk->rqueue->prev->next = sk->rqueue;
  }
  return(skb);
}


static long
diff(unsigned long seq1, unsigned long seq2)
{
  long d;

  d = seq1 - seq2;
  if (d > 0) return(d);

  /* I hope this returns what I want. */
  return(~d+1);
}


/* Enter the time wait state. */
static void
tcp_time_wait(struct sock *sk)
{
  sk->state = TCP_TIME_WAIT;
  sk->shutdown = SHUTDOWN_MASK;
  if (!sk->dead) wake_up(sk->sleep);
  sk->time_wait.len = TCP_TIMEWAIT_LEN;
  sk->timeout = TIME_CLOSE;
  reset_timer((struct timer *)&sk->time_wait);
}


static void
tcp_retransmit(struct sock *sk, int all)
{
  if (all) {
	ip_retransmit(sk, all);
	return;
  }

  if (sk->cong_window > 4)
       sk->cong_window = sk->cong_window / 2;
  sk->exp_growth = 0;

  /* Do the actuall retransmit. */
  ip_retransmit(sk, all);
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
tcp_err(int err, unsigned char *header, unsigned long daddr,
	unsigned long saddr, struct inet_protocol *protocol)
{
  struct tcphdr *th;
  struct sock *sk;
   
  DPRINTF((DBG_TCP, "TCP: tcp_err(%d, hdr=%X, daddr=%X saddr=%X, protocol=%X)\n",
					err, header, daddr, saddr, protocol));

  th =(struct tcphdr *)header;
  sk = get_sock(&tcp_prot, th->dest, saddr, th->source, daddr);
  print_th(th);

  if (sk == NULL) return;

  if ((err & 0xff00) == (ICMP_SOURCE_QUENCH << 8)) {
	/*
	 * FIXME:
	 * For now we will just trigger a linear backoff.
	 * The slow start code should cause a real backoff here.
	 */
	if (sk->cong_window > 4) sk->cong_window--;
	return;
  }

  DPRINTF((DBG_TCP, "TCP: icmp_err got error\n"));
  sk->err = icmp_err_convert[err & 0xff].errno;

  /*
   * If we've already connected we will keep trying
   * until we time out, or the user gives up.
   */
  if (icmp_err_convert[err & 0xff].fatal) {
	if (sk->state == TCP_SYN_SENT) {
		sk->state = TCP_CLOSE;
		sk->prot->close(sk, 0);
	}
  }
  return;
}


static int
tcp_readable(struct sock *sk)
{
  unsigned long counted;
  unsigned long amount;
  struct sk_buff *skb;
  int count=0;
  int sum;

  DPRINTF((DBG_TCP, "tcp_readable(sk=%X)\n", sk));

  if (sk == NULL || sk->rqueue == NULL) return(0);

  counted = sk->copied_seq+1;
  amount = 0;
  skb =(struct sk_buff *)sk->rqueue->next;

  /* Do until a push or until we are out of data. */
  do {
	count++;
	if (count > 20) {
		DPRINTF((DBG_TCP, "tcp_readable, more than 20 packets without a psh\n"));
		DPRINTF((DBG_TCP, "possible read_queue corruption.\n"));
		return(amount);
	}
	if (before(counted, skb->h.th->seq)) break;
	sum = skb->len -(counted - skb->h.th->seq);
	if (skb->h.th->syn) sum++;
	if (skb->h.th->urg) {
		sum -= ntohs(skb->h.th->urg_ptr);
	}
	if (sum >= 0) {
		amount += sum;
		if (skb->h.th->syn) amount--;
		counted += sum;
	}
	if (amount && skb->h.th->psh) break;
	skb =(struct sk_buff *)skb->next;
  } while(skb != sk->rqueue->next);
  DPRINTF((DBG_TCP, "tcp readable returning %d bytes\n", amount));
  return(amount);
}


static int
tcp_select(struct sock *sk, int sel_type, select_table *wait)
{
  DPRINTF((DBG_TCP, "tcp_select(sk=%X, sel_type = %d, wait = %X)\n",
	  					sk, sel_type, wait));

  sk->inuse = 1;
  switch(sel_type) {
	case SEL_IN:
		select_wait(sk->sleep, wait);
		if (sk->rqueue != NULL) {
			if (sk->state == TCP_LISTEN || tcp_readable(sk)) {
				release_sock(sk);
				return(1);
			}
		}
		if (sk->shutdown & RCV_SHUTDOWN) {
			release_sock(sk);
			return(1);
		} else {
			release_sock(sk);
			return(0);
		}
	case SEL_OUT:
		select_wait(sk->sleep, wait);
		if (sk->shutdown & SEND_SHUTDOWN) {
			DPRINTF((DBG_TCP,
				"write select on shutdown socket.\n"));

			/* FIXME: should this return an error? */
			release_sock(sk);
			return(0);
		}

		/*
		 * FIXME:
		 * Hack so it will probably be able to write
		 * something if it says it's ok to write.
		 */
		if (sk->prot->wspace(sk) >= sk->mtu) {
			release_sock(sk);
			/* This should cause connect to work ok. */
			if (sk->state == TCP_SYN_RECV ||
			    sk->state == TCP_SYN_SENT) return(0);
			return(1);
		}
		DPRINTF((DBG_TCP,
			"tcp_select: sleeping on write sk->wmem_alloc = %d, "
			"sk->packets_out = %d\n"
			"sk->wback = %X, sk->wfront = %X\n"
			"sk->send_seq = %u, sk->window_seq=%u\n", 
				sk->wmem_alloc, sk->packets_out,
				sk->wback, sk->wfront,
				sk->send_seq, sk->window_seq));

		release_sock(sk);
		return(0);
	case SEL_EX:
		select_wait(sk->sleep,wait);
		if (sk->err) {
			release_sock(sk);
			return(1);
		}
		release_sock(sk);
		return(0);
  }

  release_sock(sk);
  return(0);
}


int
tcp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
  DPRINTF((DBG_TCP, "tcp_ioctl(sk=%X, cmd = %d, arg=%X)\n", sk, cmd, arg));
  switch(cmd) {
	case DDIOCSDBG:
		return(dbg_ioctl((void *) arg, DBG_TCP));

	case TIOCINQ:
#ifdef FIXME	/* FIXME: */
	case FIONREAD:
#endif
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);

			amount = 0;
			sk->inuse = 1;
			if (sk->rqueue != NULL) {
				amount = tcp_readable(sk);
			}
			release_sock(sk);
			DPRINTF((DBG_TCP, "returning %d\n", amount));
			verify_area(VERIFY_WRITE,(void *)arg,
						   sizeof(unsigned long));
			put_fs_long(amount,(unsigned long *)arg);
			return(0);
		}
	case SIOCATMARK:
		{
			struct sk_buff *skb;
			int answ = 0;

			/*
			 * Try to figure out if we need to read
			 * some urgent data.
			 */
			sk->inuse = 1;
			if (sk->rqueue != NULL) {
				skb =(struct sk_buff *)sk->rqueue->next;
				if (sk->copied_seq+1 == skb->h.th->seq &&
					skb->h.th->urg) answ = 1;
			}
			release_sock(sk);
			verify_area(VERIFY_WRITE,(void *) arg,
						  sizeof(unsigned long));
			put_fs_long(answ,(int *) arg);
			return(0);
		}
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
	default:
		return(-EINVAL);
  }
}


/* This routine computes a TCP checksum. */
unsigned short
tcp_check(struct tcphdr *th, int len,
	  unsigned long saddr, unsigned long daddr)
{     
  unsigned long sum;
   
  if (saddr == 0) saddr = my_addr();
  print_th(th);
  __asm__("\t addl %%ecx,%%ebx\n"
	  "\t adcl %%edx,%%ebx\n"
	  "\t adcl $0, %%ebx\n"
	  : "=b"(sum)
	  : "0"(daddr), "c"(saddr), "d"((ntohs(len) << 16) + IPPROTO_TCP*256)
	  : "cx","bx","dx" );
   
  if (len > 3) {
	__asm__("\tclc\n"
		"1:\n"
		"\t lodsl\n"
		"\t adcl %%eax, %%ebx\n"
		"\t loop 1b\n"
		"\t adcl $0, %%ebx\n"
		: "=b"(sum) , "=S"(th)
		: "0"(sum), "c"(len/4) ,"1"(th)
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
		: "=b"(sum), "=S"(th)
		: "0"(sum) ,"1"(th)
		: "si", "ax", "bx");
  }
   
  /* Now check for the extra byte. */
  if ((len & 1) != 0) {
	__asm__("\t lodsb\n"
		"\t movb $0,%%ah\n"
		"\t addw %%ax,%%bx\n"
		"\t adcw $0, %%bx\n"
		: "=b"(sum)
		: "0"(sum) ,"S"(th)
		: "si", "ax", "bx");
  }
   
  /* We only want the bottom 16 bits, but we never cleared the top 16. */
  return((~sum) & 0xffff);
}


void
tcp_send_check(struct tcphdr *th, unsigned long saddr, 
	       unsigned long daddr, int len, struct sock *sk)
{
  th->check = 0;
  if (sk && sk->no_check) return;
  th->check = tcp_check(th, len, saddr, daddr);
  return;
}


static void
tcp_send_partial(struct sock *sk)
{
  struct sk_buff *skb;
  
  if (sk == NULL || sk->send_tmp == NULL) return;
  
  skb = sk->send_tmp;

  /* We need to complete and send the packet. */
  tcp_send_check(skb->h.th, sk->saddr, sk->daddr,
		 skb->len-(unsigned long)skb->h.th +
		(unsigned long)(skb+1), sk);
  
  skb->h.seq = sk->send_seq;
  if (after(sk->send_seq , sk->window_seq) ||
      sk->packets_out >= sk->cong_window) {
	DPRINTF((DBG_TCP, "sk->cong_window = %d, sk->packets_out = %d\n",
					sk->cong_window, sk->packets_out));
	DPRINTF((DBG_TCP, "sk->send_seq = %d, sk->window_seq = %d\n",
					sk->send_seq, sk->window_seq));
	skb->next = NULL;
	skb->magic = TCP_WRITE_QUEUE_MAGIC;
	if (sk->wback == NULL) {
		sk->wfront=skb;
	} else {
		sk->wback->next = skb;
	}
	sk->wback = skb;
  } else {
	sk->prot->queue_xmit(sk, skb->dev, skb,0);
  }
  sk->send_tmp = NULL;
}


/* This routine sends an ack and also updates the window. */
static void
tcp_send_ack(unsigned long sequence, unsigned long ack,
	     struct sock *sk,
	     struct tcphdr *th, unsigned long daddr)
{
  struct sk_buff *buff;
  struct tcphdr *t1;
  struct device *dev = NULL;
  int tmp;

  /*
   * We need to grab some memory, and put together an ack,
   * and then put it into the queue to be sent.
   */
  buff = (struct sk_buff *) sk->prot->wmalloc(sk, MAX_ACK_SIZE, 1, GFP_ATOMIC);
  if (buff == NULL) {
	/* Force it to send an ack. */
	sk->ack_backlog++;
	if (sk->timeout != TIME_WRITE && tcp_connected(sk->state)) {
		sk->timeout = TIME_WRITE;
		sk->time_wait.len = 10;		/* got to do it quickly */
		reset_timer((struct timer *)&sk->time_wait);
	}
if (inet_debug == DBG_SLIP) printk("\rtcp_ack: malloc failed\n");
	return;
  }

  buff->mem_addr = buff;
  buff->mem_len = MAX_ACK_SIZE;
  buff->lock = 0;
  buff->len = sizeof(struct tcphdr);
  buff->sk = sk;
  t1 =(struct tcphdr *)(buff + 1);

  /* Put in the IP header and routing stuff. */
  tmp = sk->prot->build_header(buff, sk->saddr, daddr, &dev,
				IPPROTO_TCP, sk->opt, MAX_ACK_SIZE);
  if (tmp < 0) {
	sk->prot->wfree(sk, buff->mem_addr, buff->mem_len);
if (inet_debug == DBG_SLIP) printk("\rtcp_ack: build_header failed\n");
	return;
  }
  buff->len += tmp;
  t1 =(struct tcphdr *)((char *)t1 +tmp);

  /* FIXME: */
  memcpy(t1, th, sizeof(*t1)); /* this should probably be removed */

  /* swap the send and the receive. */
  t1->dest = th->source;
  t1->source = th->dest;
  t1->seq = ntohl(sequence);
  t1->ack = 1;
  sk->window = sk->prot->rspace(sk);
  t1->window = ntohs(sk->window);
  t1->res1 = 0;
  t1->res2 = 0;
  t1->rst = 0;
  t1->urg = 0;
  t1->syn = 0;
  t1->psh = 0;
  t1->fin = 0;
  if (ack == sk->acked_seq) {
	sk->ack_backlog = 0;
	sk->bytes_rcv = 0;
	sk->ack_timed = 0;
	if (sk->send_head == NULL && sk->wfront == NULL) {
		delete_timer((struct timer *)&sk->time_wait);
		sk->timeout = 0;
	}
  }
  t1->ack_seq = ntohl(ack);
  t1->doff = sizeof(*t1)/4;
  tcp_send_check(t1, sk->saddr, daddr, sizeof(*t1), sk);
if (inet_debug == DBG_SLIP) printk("\rtcp_ack: seq %x ack %x\n",
				   sequence, ack);
  sk->prot->queue_xmit(sk, dev, buff, 1);
}


/* This routine builds a generic TCP header. */
static int
tcp_build_header(struct tcphdr *th, struct sock *sk, int push)
{

  /* FIXME: want to get rid of this. */
  memcpy(th,(void *) &(sk->dummy_th), sizeof(*th));
  th->seq = ntohl(sk->send_seq);
  th->psh =(push == 0) ? 1 : 0;
  th->doff = sizeof(*th)/4;
  th->ack = 1;
  th->fin = 0;
  sk->ack_backlog = 0;
  sk->bytes_rcv = 0;
  sk->ack_timed = 0;
  th->ack_seq = ntohl(sk->acked_seq);
  sk->window = sk->prot->rspace(sk);
  th->window = ntohs(sk->window);

  return(sizeof(*th));
}


/*
 * This routine copies from a user buffer into a socket,
 * and starts the transmit system.
 */
static int
tcp_write(struct sock *sk, unsigned char *from,
	  int len, int nonblock, unsigned flags)
{
  int copied = 0;
  int copy;
  int tmp;
  struct sk_buff *skb;
  unsigned char *buff;
  struct proto *prot;
  struct device *dev = NULL;

  DPRINTF((DBG_TCP, "tcp_write(sk=%X, from=%X, len=%d, nonblock=%d, flags=%X)\n",
					sk, from, len, nonblock, flags));

  prot = sk->prot;
  while(len > 0) {
	if (sk->err) {
		if (copied) return(copied);
		tmp = -sk->err;
		sk->err = 0;
		return(tmp);
	}

	/* First thing we do is make sure that we are established. */	 
	sk->inuse = 1; /* no one else will use this socket.*/
	if (sk->shutdown & SEND_SHUTDOWN) {
		release_sock(sk);
		sk->err = EPIPE;
		if (copied) return(copied);
		sk->err = 0;
		return(-EPIPE);
	}

	while(sk->state != TCP_ESTABLISHED && sk->state != TCP_CLOSE_WAIT) {
		if (sk->err) {
			if (copied) return(copied);
			tmp = -sk->err;
			sk->err = 0;
			return(tmp);
		}

		if (sk->state != TCP_SYN_SENT && sk->state != TCP_SYN_RECV) {
			release_sock(sk);
			DPRINTF((DBG_TCP, "tcp_write: return 1\n"));
			if (copied) return(copied);

			if (sk->err) {
				tmp = -sk->err;
				sk->err = 0;
				return(tmp);
			}

			if (sk->keepopen) {
				send_sig(SIGPIPE, current, 0);
			}
			return(-EPIPE);
		}

		if (nonblock || copied) {
			release_sock(sk);
			DPRINTF((DBG_TCP, "tcp_write: return 2\n"));
			if (copied) return(copied);
			return(-EAGAIN);
		}

		/*
		 * FIXME:
		 * Now here is a race condition.
		 * release_sock could cause the connection to enter the
		 * `established' mode, if that is the case, then we will
		 * block here for ever, because we will have gotten our
		 * wakeup call before we go to sleep.
		 */
		release_sock(sk);
		cli();
		if (sk->state != TCP_ESTABLISHED &&
		    sk->state != TCP_CLOSE_WAIT && sk->err == 0) {
			interruptible_sleep_on(sk->sleep);
			if (current->signal & ~current->blocked) {
				sti();
				DPRINTF((DBG_TCP, "tcp_write: return 3\n"));
				if (copied) return(copied);
				return(-ERESTARTSYS);
			}
		}
		sk->inuse = 1;
		sti();
	}

	/* Now we need to check if we have a half built packet. */
	if (sk->send_tmp != NULL) {
		/* If sk->mss has been changed this could cause problems. */

		/* Add more stuff to the end of skb->len */
		skb = sk->send_tmp;
		if (!(flags & MSG_OOB)) {
			copy = min(sk->mss - skb->len + 128 +
				   prot->max_header, len);
	      
		/* FIXME: this is really a bug. */
		if (copy <= 0) {
			printk("TCP: **bug**: \"copy\" <= 0!!\n");
			copy = 0;
		}
	  
		memcpy_fromfs((unsigned char *)(skb+1) + skb->len, from, copy);
		skb->len += copy;
		from += copy;
		copied += copy;
		len -= copy;
		sk->send_seq += copy;
	}

	if (skb->len -(unsigned long)skb->h.th +
	   (unsigned long)(skb+1) >= sk->mss ||(flags & MSG_OOB)) {
		tcp_send_partial(sk);
	}
	continue;
  }

  /*
   * We also need to worry about the window.
   * The smallest we will send is about 200 bytes.
   */
  copy = min(sk->mtu, diff(sk->window_seq, sk->send_seq));

  /* FIXME: redundent check here. */
  if (copy < 200 || copy > sk->mtu) copy = sk->mtu;
  copy = min(copy, len);

  /* We should really check the window here also. */
  if (sk->packets_out && copy < sk->mss && !(flags & MSG_OOB)) {
	/* We will release the socket incase we sleep here. */
	release_sock(sk);
	skb = (struct sk_buff *) prot->wmalloc(sk,
			sk->mss + 128 + prot->max_header +
			sizeof(*skb), 0, GFP_KERNEL);
	sk->inuse = 1;
	sk->send_tmp = skb;
	if (skb != NULL)
		skb->mem_len = sk->mss + 128 + prot->max_header + sizeof(*skb);
	} else {
		/* We will release the socket incase we sleep here. */
		release_sock(sk);
		skb = (struct sk_buff *) prot->wmalloc(sk,
				copy + prot->max_header +
				sizeof(*skb), 0, GFP_KERNEL);
		sk->inuse = 1;
		if (skb != NULL)
			skb->mem_len = copy+prot->max_header + sizeof(*skb);
	}

	/* If we didn't get any memory, we need to sleep. */
	if (skb == NULL) {
		if (nonblock || copied) {
			release_sock(sk);
			DPRINTF((DBG_TCP, "tcp_write: return 4\n"));
			if (copied) return(copied);
			return(-EAGAIN);
		}

		/* FIXME: here is another race condition. */
		tmp = sk->wmem_alloc;
		release_sock(sk);

		/* Again we will try to avoid it. */
		cli();
		if (tmp <= sk->wmem_alloc &&
		  (sk->state == TCP_ESTABLISHED||sk->state == TCP_CLOSE_WAIT)
				&& sk->err == 0) {
			interruptible_sleep_on(sk->sleep);
			if (current->signal & ~current->blocked) {
				sti();
				DPRINTF((DBG_TCP, "tcp_write: return 5\n"));
				if (copied) return(copied);
				return(-ERESTARTSYS);
			}
		}
		sk->inuse = 1;
		sti();
		continue;
	}

	skb->mem_addr = skb;
	skb->len = 0;
	skb->sk = sk;
	skb->lock = 0;
	skb->free = 0;

	buff =(unsigned char *)(skb+1);

	/*
	 * FIXME: we need to optimize this.
	 * Perhaps some hints here would be good.
	 */
	tmp = prot->build_header(skb, sk->saddr, sk->daddr, &dev,
				 IPPROTO_TCP, sk->opt, skb->mem_len);
	if (tmp < 0 ) {
		prot->wfree(sk, skb->mem_addr, skb->mem_len);
		release_sock(sk);
		DPRINTF((DBG_TCP, "tcp_write: return 6\n"));
		if (copied) return(copied);
		return(tmp);
	}
	skb->len += tmp;
	skb->dev = dev;
	buff += tmp;
	skb->h.th =(struct tcphdr *) buff;
	tmp = tcp_build_header((struct tcphdr *)buff, sk, len-copy);
	if (tmp < 0) {
		prot->wfree(sk, skb->mem_addr, skb->mem_len);
		release_sock(sk);
		DPRINTF((DBG_TCP, "tcp_write: return 7\n"));
		if (copied) return(copied);
		return(tmp);
	}

	if (flags & MSG_OOB) {
		((struct tcphdr *)buff)->urg = 1;
		((struct tcphdr *)buff)->urg_ptr = ntohs(copy);
	}
	skb->len += tmp;
	memcpy_fromfs(buff+tmp, from, copy);

	from += copy;
	copied += copy;
	len -= copy;
	skb->len += copy;
	skb->free = 0;
	sk->send_seq += copy;

	if (sk->send_tmp != NULL) continue;

	tcp_send_check((struct tcphdr *)buff, sk->saddr, sk->daddr,
		        copy + sizeof(struct tcphdr), sk);

	skb->h.seq = sk->send_seq;
	if (after(sk->send_seq , sk->window_seq) ||
		  sk->packets_out >= sk->cong_window) {
		DPRINTF((DBG_TCP, "sk->cong_window = %d, sk->packets_out = %d\n",
					sk->cong_window, sk->packets_out));
		DPRINTF((DBG_TCP, "sk->send_seq = %d, sk->window_seq = %d\n",
					sk->send_seq, sk->window_seq));
		skb->next = NULL;
		skb->magic = TCP_WRITE_QUEUE_MAGIC;
		if (sk->wback == NULL) {
			sk->wfront = skb;
		} else {
			sk->wback->next = skb;
		}
		sk->wback = skb;
	} else {
		prot->queue_xmit(sk, dev, skb,0);
	}
  }
  sk->err = 0;
  release_sock(sk);
  DPRINTF((DBG_TCP, "tcp_write: return 8\n"));
  return(copied);
}


static int
tcp_sendto(struct sock *sk, unsigned char *from,
	   int len, int nonblock, unsigned flags,
	   struct sockaddr_in *addr, int addr_len)
{
  struct sockaddr_in sin;

  if (addr_len < sizeof(sin)) return(-EINVAL);
  memcpy_fromfs(&sin, addr, sizeof(sin));
  if (sin.sin_family && sin.sin_family != AF_INET) return(-EINVAL);
  if (sin.sin_port != sk->dummy_th.dest) return(-EINVAL);
  if (sin.sin_addr.s_addr != sk->daddr) return(-EINVAL);
  return(tcp_write(sk, from, len, nonblock, flags));
}


static void
tcp_read_wakeup(struct sock *sk)
{
  int tmp;
  struct device *dev = NULL;
  struct tcphdr *t1;
  struct sk_buff *buff;

  DPRINTF((DBG_TCP, "in tcp read wakeup\n"));
  if (!sk->ack_backlog) return;

  /*
   * FIXME: we need to put code here to prevent this routine from
   * being called.  Being called once in a while is ok, so only check
   * if this is the second time in a row.
   */

  /*
   * We need to grab some memory, and put together an ack,
   * and then put it into the queue to be sent.
   */
  buff = (struct sk_buff *) sk->prot->wmalloc(sk,MAX_ACK_SIZE,1, GFP_ATOMIC);
  if (buff == NULL) {
	/* Try again real soon. */
	sk->timeout = TIME_WRITE;
	sk->time_wait.len = 10;
	reset_timer((struct timer *) &sk->time_wait);
	return;
  }

  buff->mem_addr = buff;
  buff->mem_len = MAX_ACK_SIZE;
  buff->lock = 0;
  buff->len = sizeof(struct tcphdr);
  buff->sk = sk;

  /* Put in the IP header and routing stuff. */
  tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
			       IPPROTO_TCP, sk->opt, MAX_ACK_SIZE);
  if (tmp < 0) {
	sk->prot->wfree(sk, buff->mem_addr, buff->mem_len);
	return;
  }

  buff->len += tmp;
  t1 =(struct tcphdr *)((char *)(buff+1) +tmp);

  memcpy(t1,(void *) &sk->dummy_th, sizeof(*t1));
  t1->seq = ntohl(sk->send_seq);
  t1->ack = 1;
  t1->res1 = 0;
  t1->res2 = 0;
  t1->rst = 0;
  t1->urg = 0;
  t1->syn = 0;
  t1->psh = 0;
  sk->ack_backlog = 0;
  sk->bytes_rcv = 0;
  sk->window = sk->prot->rspace(sk);
  t1->window = ntohs(sk->window);
  t1->ack_seq = ntohl(sk->acked_seq);
  t1->doff = sizeof(*t1)/4;
  tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), sk);
  sk->prot->queue_xmit(sk, dev, buff, 1);
}


/*
 * FIXME:
 * This routine frees used buffers.
 * It should consider sending an ACK to let the
 * other end know we now have a bigger window.
 */
static void
cleanup_rbuf(struct sock *sk)
{
  int left;

  DPRINTF((DBG_TCP, "cleaning rbuf for sk=%X\n", sk));
  left = sk->prot->rspace(sk);

  /*
   * We have to loop through all the buffer headers,
   * and try to free up all the space we can.
   */
  while(sk->rqueue != NULL ) {
	struct sk_buff *skb;

	skb =(struct sk_buff *)sk->rqueue->next;
	if (!skb->used) break;
	if (sk->rqueue == skb) {
		sk->rqueue = NULL;
	} else {
		skb->next->prev = skb->prev;
		skb->prev->next = skb->next;
	}
	skb->sk = sk;
	kfree_skb(skb, FREE_READ);
  }

  /*
   * FIXME:
   * At this point we should send an ack if the difference
   * in the window, and the amount of space is bigger than
   * TCP_WINDOW_DIFF.
   */
  DPRINTF((DBG_TCP, "sk->window left = %d, sk->prot->rspace(sk)=%d\n",
			sk->window - sk->bytes_rcv, sk->prot->rspace(sk)));

  if (sk->prot->rspace(sk) != left) {
	/*
	 * This area has caused the most trouble.  The current strategy
	 * is to simply do nothing if the other end has room to send at
	 * least 3 full packets, because the ack from those will auto-
	 * matically update the window.  If the other end doesn't think
	 * we have much space left, but we have room for atleast 1 more
	 * complete packet than it thinks we do, we will send an ack
	 * immediatedly.  Otherwise we will wait up to .5 seconds in case
	 * the user reads some more.
	 */
	sk->ack_backlog++;
	if ((sk->prot->rspace(sk) > (sk->window - sk->bytes_rcv + sk->mtu))) {
		/* Send an ack right now. */
		tcp_read_wakeup(sk);
	} else {
		/* Force it to send an ack soon. */
		if (jiffies + TCP_ACK_TIME < sk->time_wait.when) {
			sk->time_wait.len = TCP_ACK_TIME;
			sk->timeout = TIME_WRITE;
			reset_timer((struct timer *) &sk->time_wait);
		}
	}
  }
} 


/* Handle reading urgent data. */
static int
tcp_read_urg(struct sock * sk, int nonblock,
	     unsigned char *to, int len, unsigned flags)
{
  int copied = 0;
  struct sk_buff *skb;

  DPRINTF((DBG_TCP, "tcp_read_urg(sk=%X, to=%X, len=%d, flags=%X)\n",
					sk, to, len, flags));

  while(len > 0) {
	sk->inuse = 1;
	while(sk->urg==0 || sk->rqueue == NULL) {
		if (sk->err) {
			int tmp;

			release_sock(sk);
			if (copied) return(copied);
			tmp = -sk->err;
			sk->err = 0;
			return(tmp);
		}

		if (sk->state == TCP_CLOSE || sk->done) {
			release_sock(sk);
			if (copied) return(copied);
			if (!sk->done) {
				sk->done = 1;
				return(0);
			}
			return(-ENOTCONN);
		}
		 
		if (sk->shutdown & RCV_SHUTDOWN) {
			release_sock(sk);
			if (copied == 0) sk->done = 1;
			return(copied);
		}

		if (nonblock || copied) {
			release_sock(sk);
			if (copied) return(copied);
			return(-EAGAIN);
		}

		/* Now at this point, we may have gotten some data. */
		release_sock(sk);
		cli();
		if ((sk->urg == 0 || sk->rqueue == NULL) &&
		    sk->err == 0 && !(sk->shutdown & RCV_SHUTDOWN)) {
			interruptible_sleep_on(sk->sleep);
			if (current->signal & ~current->blocked) {
				sti();
				if (copied) return(copied);
				return(-ERESTARTSYS);
			}
		}
		sk->inuse = 1;
		sti();
	}

	skb =(struct sk_buff *)sk->rqueue->next;
	do {
		int amt;

		if (skb->h.th->urg && !skb->urg_used) {
			if (skb->h.th->urg_ptr == 0) {
				skb->h.th->urg_ptr = ntohs(skb->len);
			}
			amt = min(ntohs(skb->h.th->urg_ptr),len);
			verify_area(VERIFY_WRITE, to, amt);
			memcpy_tofs(to,(unsigned char *)(skb->h.th) +
							skb->h.th->doff*4, amt);

			if (!(flags & MSG_PEEK)) {
				skb->urg_used = 1;
				sk->urg--;
			}
			release_sock(sk);
			copied += amt;
			return(copied);
		}
		skb =(struct sk_buff *)skb->next;
	} while(skb != sk->rqueue->next);
  }
  sk->urg = 0;
  release_sock(sk);
  return(0);
}


/* This routine copies from a sock struct into the user buffer. */
static int
tcp_read(struct sock *sk, unsigned char *to,
	 int len, int nonblock, unsigned flags)
{
  int copied=0; /* will be used to say how much has been copied. */
  struct sk_buff *skb;
  unsigned long offset;
  unsigned long used;

  if (len == 0) return(0);
  if (len < 0) {
	return(-EINVAL);
  }
    
  /* This error should be checked. */
  if (sk->state == TCP_LISTEN) return(-ENOTCONN);

  /* Urgent data needs to be handled specially. */
  if ((flags & MSG_OOB)) return(tcp_read_urg(sk, nonblock, to, len, flags));

  /* So no-one else will use this socket. */
  sk->inuse = 1;
  if (sk->rqueue != NULL) skb =(struct sk_buff *)sk->rqueue->next;
    else skb = NULL;

  DPRINTF((DBG_TCP, "tcp_read(sk=%X, to=%X, len=%d, nonblock=%d, flags=%X)\n",
						sk, to, len, nonblock, flags));

  while(len > 0) {
	/* skb->used just checks to see if we've gone all the way around. */
	while(skb == NULL ||
	      before(sk->copied_seq+1, skb->h.th->seq) || skb->used) {
		DPRINTF((DBG_TCP, "skb = %X:\n", skb));
		cleanup_rbuf(sk);
		if (sk->err) {
			int tmp;

			release_sock(sk);
			if (copied) {
				DPRINTF((DBG_TCP, "tcp_read: returing %d\n",
									copied));
				return(copied);
			}
			tmp = -sk->err;
			sk->err = 0;
			return(tmp);
		}

		if (sk->state == TCP_CLOSE) {
			release_sock(sk);
			if (copied) {
				DPRINTF((DBG_TCP, "tcp_read: returing %d\n",
								copied));
				return(copied);
			}
			if (!sk->done) {
				sk->done = 1;
				return(0);
			}
			return(-ENOTCONN);
		}

		if (sk->shutdown & RCV_SHUTDOWN) {
			release_sock(sk);
			if (copied == 0) sk->done = 1;
			DPRINTF((DBG_TCP, "tcp_read: returing %d\n", copied));
			return(copied);
		}
			
		if (nonblock || copied) {
			release_sock(sk);
			if (copied) {
				DPRINTF((DBG_TCP, "tcp_read: returing %d\n",
								copied));
				return(copied);
			}
			return(-EAGAIN);
		}

		if ((flags & MSG_PEEK) && copied != 0) {
			release_sock(sk);
			DPRINTF((DBG_TCP, "tcp_read: returing %d\n", copied));
			return(copied);
		}
		 
		DPRINTF((DBG_TCP, "tcp_read about to sleep. state = %d\n",
								sk->state));
		release_sock(sk);

		/*
		 * Now we may have some data waiting or we could
		 * have changed state.
		 */
		cli();
		if (sk->shutdown & RCV_SHUTDOWN || sk->err != 0) {
			sk->inuse = 1;
			sti();
			continue;
		}

		if (sk->rqueue == NULL ||
		    before(sk->copied_seq+1, sk->rqueue->next->h.th->seq)) {
			interruptible_sleep_on(sk->sleep);
			if (current->signal & ~current->blocked) {
				sti();
				if (copied) {
					DPRINTF((DBG_TCP, "tcp_read: returing %d\n",
								copied));
					return(copied);
				}
				return(-ERESTARTSYS);
			}
		}
		sk->inuse = 1;
		sti();
		DPRINTF((DBG_TCP, "tcp_read woke up. \n"));


		if (sk->rqueue == NULL) skb = NULL;
		  else skb =(struct sk_buff *)sk->rqueue->next;

	}

	/*
	 * Copy anything from the current block that needs
	 * to go into the user buffer.
	 */
	 offset = sk->copied_seq+1 - skb->h.th->seq;

	 if (skb->h.th->syn) offset--;
	 if (offset < skb->len) {
		/*
		 * If there is urgent data we must either
		 * return or skip over it.
		 */
		if (skb->h.th->urg) {
			if (skb->urg_used) {
				sk->copied_seq += ntohs(skb->h.th->urg_ptr);
				offset += ntohs(skb->h.th->urg_ptr);
				if (offset >= skb->len) {
					skb->used = 1;
					skb =(struct sk_buff *)skb->next;
					continue;
				}
			} else {
				release_sock(sk);
				if (copied) return(copied);
				send_sig(SIGURG, current, 0);
				return(-EINTR);
			}
		}
		used = min(skb->len - offset, len);
		verify_area(VERIFY_WRITE, to, used);
		memcpy_tofs(to,((unsigned char *)skb->h.th) +
			    skb->h.th->doff*4 + offset, used);
		copied += used;
		len -= used;
		to += used;
		if (!(flags & MSG_PEEK)) sk->copied_seq += used;
	      
		/*
		 * Mark this data used if we are really reading it,
		 * and if it doesn't contain any urgent data. And we
		 * have used all the data.
		 */
		if (!(flags & MSG_PEEK) &&
		   (!skb->h.th->urg || skb->urg_used) &&
		   (used + offset >= skb->len)) skb->used = 1;
	      
		/*
		 * See if this is the end of a message or if the
		 * remaining data is urgent.
		 */
		if (skb->h.th->psh || skb->h.th->urg) {
			break;
		}
	} else {	/* already used this data, must be a retransmit */
		skb->used = 1;
	}
	skb =(struct sk_buff *)skb->next;
  }
  cleanup_rbuf(sk);
  release_sock(sk);
  DPRINTF((DBG_TCP, "tcp_read: returing %d\n", copied));
  if (copied == 0 && nonblock) return(-EAGAIN);
  return(copied);
}

  
/*
 * Send a FIN without closing the connection.
 * Not called at interrupt time.
 */
void
tcp_shutdown(struct sock *sk, int how)
{
  struct sk_buff *buff;
  struct tcphdr *t1, *th;
  struct proto *prot;
  int tmp;
  struct device *dev = NULL;

  /*
   * We need to grab some memory, and put together a FIN,
   * and then put it into the queue to be sent.
   * FIXME:
   *	Tim MacKenzie(tym@dibbler.cs.monash.edu.au) 4 Dec '92.
   *	Most of this is guesswork, so maybe it will work...
   */
  /* If we've already sent a FIN, return. */
  if (sk->state == TCP_FIN_WAIT1 || sk->state == TCP_FIN_WAIT2) return;
  if (!(how & SEND_SHUTDOWN)) return;
  sk->inuse = 1;

  /* Clear out any half completed packets. */
  if (sk->send_tmp) tcp_send_partial(sk);

  prot =(struct proto *)sk->prot;
  th =(struct tcphdr *)&sk->dummy_th;
  release_sock(sk); /* incase the malloc sleeps. */
  buff = (struct sk_buff *) prot->wmalloc(sk, MAX_RESET_SIZE,1 , GFP_KERNEL);
  if (buff == NULL) return;
  sk->inuse = 1;

  DPRINTF((DBG_TCP, "tcp_shutdown_send buff = %X\n", buff));
  buff->mem_addr = buff;
  buff->mem_len = MAX_RESET_SIZE;
  buff->lock = 0;
  buff->sk = sk;
  buff->len = sizeof(*t1);
  t1 =(struct tcphdr *)(buff + 1);

  /* Put in the IP header and routing stuff. */
  tmp = prot->build_header(buff,sk->saddr, sk->daddr, &dev,
			   IPPROTO_TCP, sk->opt,
			   sizeof(struct tcphdr));
  if (tmp < 0) {
	prot->wfree(sk,buff->mem_addr, buff->mem_len);
	release_sock(sk);
	DPRINTF((DBG_TCP, "Unable to build header for fin.\n"));
	return;
  }

  t1 =(struct tcphdr *)((char *)t1 +tmp);
  buff ->len += tmp;
  buff->dev = dev;
  memcpy(t1, th, sizeof(*t1));
  t1->seq = ntohl(sk->send_seq);
  sk->send_seq++;
  buff->h.seq = sk->send_seq;
  t1->ack = 1;
  t1->ack_seq = ntohl(sk->acked_seq);
  t1->window = ntohs(sk->prot->rspace(sk));
  t1->fin = 1;
  t1->rst = 0;
  t1->doff = sizeof(*t1)/4;
  tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), sk);

  /*
   * Can't just queue this up.
   * It should go at the end of the write queue.
   */
  if (sk->wback != NULL) {
	buff->next = NULL;
	sk->wback->next = buff;
	sk->wback = buff;
	buff->magic = TCP_WRITE_QUEUE_MAGIC;
  } else {
	sk->prot->queue_xmit(sk, dev, buff, 0);
  }

  if (sk->state == TCP_ESTABLISHED) sk->state = TCP_FIN_WAIT1;
    else sk->state = TCP_FIN_WAIT2;

  release_sock(sk);
}


static int
tcp_recvfrom(struct sock *sk, unsigned char *to,
	     int to_len, int nonblock, unsigned flags,
	     struct sockaddr_in *addr, int *addr_len)
{
  struct sockaddr_in sin;
  int len;
  int result = tcp_read(sk, to, to_len, nonblock, flags);

  if (result < 0) return(result);
  len = get_fs_long(addr_len);
  if (len > sizeof(sin)) len = sizeof(sin);
  sin.sin_family = AF_INET;
  sin.sin_port = sk->dummy_th.dest;
  sin.sin_addr.s_addr = sk->daddr;
  verify_area(VERIFY_WRITE, addr, len);
  memcpy_tofs(addr, &sin, len);
  verify_area(VERIFY_WRITE, addr_len, sizeof(len));
  put_fs_long(len, addr_len);
  return(result);
}


/* This routine will send an RST to the other tcp. */
static void
tcp_reset(unsigned long saddr, unsigned long daddr, struct tcphdr *th,
	  struct proto *prot, struct options *opt, struct device *dev)
{
  struct sk_buff *buff;
  struct tcphdr *t1;
  int tmp;

  /*
   * We need to grab some memory, and put together an RST,
   * and then put it into the queue to be sent.
   */
  buff = (struct sk_buff *) prot->wmalloc(NULL, MAX_RESET_SIZE, 1, GFP_ATOMIC);
  if (buff == NULL) return;

  DPRINTF((DBG_TCP, "tcp_reset buff = %X\n", buff));
  buff->mem_addr = buff;
  buff->mem_len = MAX_RESET_SIZE;
  buff->lock = 0;
  buff->len = sizeof(*t1);
  buff->sk = NULL;
  buff->dev = dev;

  t1 =(struct tcphdr *)(buff + 1);

  /* Put in the IP header and routing stuff. */
  tmp = prot->build_header(buff, saddr, daddr, &dev, IPPROTO_TCP, opt,
			   sizeof(struct tcphdr));
  if (tmp < 0) {
	prot->wfree(NULL, buff->mem_addr, buff->mem_len);
	return;
  }
  t1 =(struct tcphdr *)((char *)t1 +tmp);
  buff->len += tmp;
  memcpy(t1, th, sizeof(*t1));

  /* Wwap the send and the receive. */
  t1->dest = th->source;
  t1->source = th->dest;
  t1->seq = th->ack_seq; /* add one so it will be in the right range */
  t1->rst = 1;
  t1->window = 0;		/* should be set to 0 -FB */
  t1->ack = 0;
  t1->syn = 0;
  t1->urg = 0;
  t1->fin = 0;
  t1->psh = 0;
  t1->doff = sizeof(*t1)/4;
  tcp_send_check(t1, saddr, daddr, sizeof(*t1), NULL);
  prot->queue_xmit(NULL, dev, buff, 1);
}


/*
 * This routine handles a connection request.
 * It should make sure we haven't already responded.
 * Because of the way BSD works, we have to send a syn/ack now.
 * This also means it will be harder to close a socket which is
 * listening.
 */
static void
tcp_conn_request(struct sock *sk, struct sk_buff *skb,
		 unsigned long daddr, unsigned long saddr,
		 struct options *opt, struct device *dev)
{
  struct sk_buff *buff;
  struct tcphdr *t1;
  unsigned char *ptr;
  struct sock *newsk;
  struct tcphdr *th;
  int tmp;

  DPRINTF((DBG_TCP, "tcp_conn_request(sk = %X, skb = %X, daddr = %X, sadd4= %X, \n"
	  "                  opt = %X, dev = %X)\n",
	  sk, skb, daddr, saddr, opt, dev));
  
  th = skb->h.th;

  /* If the socket is dead, don't accept the connection. */
  if (!sk->dead) {
	wake_up(sk->sleep);
  } else {
	DPRINTF((DBG_TCP, "tcp_conn_request on dead socket\n"));
	tcp_reset(daddr, saddr, th, sk->prot, opt, dev);
	kfree_skb(skb, FREE_READ);
	return;
  }

  /*
   * Make sure we can accept more.  This will prevent a
   * flurry of syns from eating up all our memory.
   */
  if (sk->ack_backlog >= sk->max_ack_backlog) {
	kfree_skb(skb, FREE_READ);
	return;
  }

  /*
   * We need to build a new sock struct.
   * It is sort of bad to have a socket without an inode attached
   * to it, but the wake_up's will just wake up the listening socket,
   * and if the listening socket is destroyed before this is taken
   * off of the queue, this will take care of it.
   */
  newsk = (struct sock *) kmalloc(sizeof(struct sock), GFP_ATOMIC);
  if (newsk == NULL) {
	/* just ignore the syn.  It will get retransmitted. */
	kfree_skb(skb, FREE_READ);
	return;
  }

  DPRINTF((DBG_TCP, "newsk = %X\n", newsk));
  memcpy((void *)newsk,(void *)sk, sizeof(*newsk));
  newsk->wback = NULL;
  newsk->wfront = NULL;
  newsk->rqueue = NULL;
  newsk->send_head = NULL;
  newsk->send_tail = NULL;
  newsk->back_log = NULL;
  newsk->rtt = TCP_CONNECT_TIME;
  newsk->mdev = 0;
  newsk->backoff = 0;
  newsk->blog = 0;
  newsk->intr = 0;
  newsk->proc = 0;
  newsk->done = 0;
  newsk->send_tmp = NULL;
  newsk->pair = NULL;
  newsk->wmem_alloc = 0;
  newsk->rmem_alloc = 0;

  newsk->max_unacked = MAX_WINDOW - TCP_WINDOW_DIFF;

  newsk->err = 0;
  newsk->shutdown = 0;
  newsk->ack_backlog = 0;
  newsk->acked_seq = skb->h.th->seq+1;
  newsk->fin_seq = skb->h.th->seq;
  newsk->copied_seq = skb->h.th->seq;
  newsk->state = TCP_SYN_RECV;
  newsk->timeout = 0;
  newsk->send_seq = timer_seq * SEQ_TICK - seq_offset;
  newsk->rcv_ack_seq = newsk->send_seq;
  newsk->urg =0;
  newsk->retransmits = 0;
  newsk->destroy = 0;
  newsk->time_wait.sk = newsk;
  newsk->time_wait.next = NULL;
  newsk->dummy_th.source = skb->h.th->dest;
  newsk->dummy_th.dest = skb->h.th->source;

  /* Swap these two, they are from our point of view. */
  newsk->daddr = saddr;
  newsk->saddr = daddr;

  put_sock(newsk->num,newsk);
  newsk->dummy_th.res1 = 0;
  newsk->dummy_th.doff = 6;
  newsk->dummy_th.fin = 0;
  newsk->dummy_th.syn = 0;
  newsk->dummy_th.rst = 0;
  newsk->dummy_th.psh = 0;
  newsk->dummy_th.ack = 0;
  newsk->dummy_th.urg = 0;
  newsk->dummy_th.res2 = 0;
  newsk->acked_seq = skb->h.th->seq + 1;
  newsk->copied_seq = skb->h.th->seq;

  if (skb->h.th->doff == 5) {
	newsk->mtu = dev->mtu - HEADER_SIZE;
  } else {
	ptr =(unsigned char *)(skb->h.th + 1);
	if (ptr[0] != 2 || ptr[1] != 4) {
		newsk->mtu = dev->mtu - HEADER_SIZE;
	} else {
		newsk->mtu = min(ptr[2] * 256 + ptr[3] - HEADER_SIZE,
			         dev->mtu - HEADER_SIZE);
	}
  }

  buff = (struct sk_buff *) newsk->prot->wmalloc(newsk, MAX_SYN_SIZE, 1, GFP_ATOMIC);
  if (buff == NULL) {
	sk->err = -ENOMEM;
	newsk->dead = 1;
	release_sock(newsk);
	kfree_skb(skb, FREE_READ);
	return;
  }
  
  buff->lock = 0;
  buff->mem_addr = buff;
  buff->mem_len = MAX_SYN_SIZE;
  buff->len = sizeof(struct tcphdr)+4;
  buff->sk = newsk;
  
  t1 =(struct tcphdr *)(buff + 1);

  /* Put in the IP header and routing stuff. */
  tmp = sk->prot->build_header(buff, newsk->saddr, newsk->daddr, &dev,
			       IPPROTO_TCP, NULL, MAX_SYN_SIZE);

  /* Something went wrong. */
  if (tmp < 0) {
	sk->err = tmp;
	sk->prot->wfree(newsk, buff->mem_addr, buff->mem_len);
	newsk->dead = 1;
	release_sock(newsk);
	skb->sk = sk;
	kfree_skb(skb, FREE_READ);
	return;
  }

  buff->len += tmp;
  t1 =(struct tcphdr *)((char *)t1 +tmp);
  
  memcpy(t1, skb->h.th, sizeof(*t1));
  buff->h.seq = newsk->send_seq;

  /* Swap the send and the receive. */
  t1->dest = skb->h.th->source;
  t1->source = newsk->dummy_th.source;
  t1->seq = ntohl(newsk->send_seq++);
  t1->ack = 1;
  newsk->window = newsk->prot->rspace(newsk);
  t1->window = ntohs(newsk->window);
  t1->res1 = 0;
  t1->res2 = 0;
  t1->rst = 0;
  t1->urg = 0;
  t1->psh = 0;
  t1->syn = 1;
  t1->ack_seq = ntohl(skb->h.th->seq+1);
  t1->doff = sizeof(*t1)/4+1;

  ptr =(unsigned char *)(t1+1);
  ptr[0] = 2;
  ptr[1] = 4;
  ptr[2] =((dev->mtu - HEADER_SIZE) >> 8) & 0xff;
  ptr[3] =(dev->mtu - HEADER_SIZE) & 0xff;

  tcp_send_check(t1, daddr, saddr, sizeof(*t1)+4, newsk);
  newsk->prot->queue_xmit(newsk, dev, buff, 0);

  newsk->time_wait.len = TCP_CONNECT_TIME;
  DPRINTF((DBG_TCP, "newsk->time_wait.sk = %X\n", newsk->time_wait.sk));
  reset_timer((struct timer *)&newsk->time_wait);
  skb->sk = newsk;

  /* Charge the sock_buff to newsk. */
  sk->rmem_alloc -= skb->mem_len;
  newsk->rmem_alloc += skb->mem_len;

  if (sk->rqueue == NULL) {
	skb->next = skb;
	skb->prev = skb;
	sk->rqueue = skb;
  } else {
	skb->next = sk->rqueue;
	skb->prev = sk->rqueue->prev;
	sk->rqueue->prev = skb;
	skb->prev->next = skb;
  }
  sk->ack_backlog++;
  release_sock(newsk);
}


static void
tcp_close(struct sock *sk, int timeout)
{
  struct sk_buff *buff;
  int need_reset = 0;
  struct tcphdr *t1, *th;
  struct proto *prot;
  struct device *dev=NULL;
  int tmp;

  /*
   * We need to grab some memory, and put together a FIN,
   * and then put it into the queue to be sent.
   */
  DPRINTF((DBG_TCP, "tcp_close((struct sock *)%X, %d)\n",sk, timeout));
  sk->inuse = 1;
  sk->keepopen = 1;
  sk->shutdown = SHUTDOWN_MASK;

  if (!sk->dead) wake_up(sk->sleep);

  /* We need to flush the recv. buffs. */
  if (sk->rqueue != NULL) {
	struct sk_buff *skb;
	struct sk_buff *skb2;

	skb = sk->rqueue;
	do {
		skb2 =(struct sk_buff *)skb->next;
		/* if there is some real unread data, send a reset. */
		if (skb->len > 0 &&
		    after(skb->h.th->seq + skb->len + 1, sk->copied_seq))
								need_reset = 1;
		kfree_skb(skb, FREE_READ);
		skb = skb2;
	} while(skb != sk->rqueue);
  }
  sk->rqueue = NULL;

  /* Get rid off any half-completed packets. */
  if (sk->send_tmp) {
	tcp_send_partial(sk);
  }

  switch(sk->state) {
	case TCP_FIN_WAIT1:
	case TCP_FIN_WAIT2:
	case TCP_LAST_ACK:
		/* start a timer. */
		sk->time_wait.len = 4*sk->rtt;;
		sk->timeout = TIME_CLOSE;
		reset_timer((struct timer *)&sk->time_wait);
		if (timeout) tcp_time_wait(sk);
		release_sock(sk);
		break;
	case TCP_TIME_WAIT:
		if (timeout) {
		  sk->state = TCP_CLOSE;
		}
		release_sock(sk);
		return;
	case TCP_LISTEN:
		sk->state = TCP_CLOSE;
		release_sock(sk);
		return;
	case TCP_CLOSE:
		release_sock(sk);
		return;
	case TCP_CLOSE_WAIT:
	case TCP_ESTABLISHED:
	case TCP_SYN_SENT:
	case TCP_SYN_RECV:
		prot =(struct proto *)sk->prot;
		th =(struct tcphdr *)&sk->dummy_th;
		buff = (struct sk_buff *) prot->wmalloc(sk, MAX_FIN_SIZE, 1, GFP_ATOMIC);
		if (buff == NULL) {
			/* This will force it to try again later. */
			if (sk->state != TCP_CLOSE_WAIT)
					sk->state = TCP_ESTABLISHED;
			sk->timeout = TIME_CLOSE;
			sk->time_wait.len = 100; /* wait a second. */
			reset_timer((struct timer *)&sk->time_wait);
			return;
		}
		buff->lock = 0;
		buff->mem_addr = buff;
		buff->mem_len = MAX_FIN_SIZE;
		buff->sk = sk;
		buff->len = sizeof(*t1);
		t1 =(struct tcphdr *)(buff + 1);

		/* Put in the IP header and routing stuff. */
		tmp = prot->build_header(buff,sk->saddr, sk->daddr, &dev,
					 IPPROTO_TCP, sk->opt,
				         sizeof(struct tcphdr));
		if (tmp < 0) {
			prot->wfree(sk,buff->mem_addr, buff->mem_len);
			DPRINTF((DBG_TCP, "Unable to build header for fin.\n"));
			release_sock(sk);
			return;
		}

		t1 =(struct tcphdr *)((char *)t1 +tmp);
		buff ->len += tmp;
		buff->dev = dev;
		memcpy(t1, th, sizeof(*t1));
		t1->seq = ntohl(sk->send_seq);
		sk->send_seq++;
		buff->h.seq = sk->send_seq;
		t1->ack = 1;

		/* Ack everything immediately from now on. */
		sk->delay_acks = 0;
		t1->ack_seq = ntohl(sk->acked_seq);
		t1->window = ntohs(sk->prot->rspace(sk));
		t1->fin = 1;
		t1->rst = need_reset;
		t1->doff = sizeof(*t1)/4;
		tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), sk);

		if (sk->wfront == NULL) {
			prot->queue_xmit(sk, dev, buff, 0);
		} else {
			sk->time_wait.len = backoff(sk->backoff) *
			  (2 * sk->mdev + sk->rtt);
			sk->timeout = TIME_WRITE;
			reset_timer((struct timer *)&sk->time_wait);
			buff->next = NULL;
			if (sk->wback == NULL) {
				sk->wfront=buff;
			} else {
				sk->wback->next = buff;
			}
			sk->wback = buff;
			buff->magic = TCP_WRITE_QUEUE_MAGIC;
		}

		if (sk->state == TCP_CLOSE_WAIT) {
			sk->state = TCP_FIN_WAIT2;
		} else {
			sk->state = TCP_FIN_WAIT1;
	}
  }
  release_sock(sk);
}


/*
 * This routine takes stuff off of the write queue,
 * and puts it in the xmit queue.
 */
static void
tcp_write_xmit(struct sock *sk)
{
  struct sk_buff *skb;

  DPRINTF((DBG_TCP, "tcp_write_xmit(sk=%X)\n", sk));
  while(sk->wfront != NULL &&
        before(sk->wfront->h.seq, sk->window_seq) &&
        sk->packets_out < sk->cong_window) {
		skb = sk->wfront;
		sk->wfront =(struct sk_buff *)skb->next;
		if (sk->wfront == NULL) sk->wback = NULL;
		skb->next = NULL;
		if (skb->magic != TCP_WRITE_QUEUE_MAGIC) {
			DPRINTF((DBG_TCP, "tcp.c skb with bad magic(%X) on write queue. Squashing "
				"queue\n", skb->magic));
			sk->wfront = NULL;
			sk->wback = NULL;
			return;
		}
		skb->magic = 0;
		DPRINTF((DBG_TCP, "Sending a packet.\n"));

		/* See if we really need to send the packet. */
		if (before(skb->h.seq, sk->rcv_ack_seq +1)) {
			sk->retransmits = 0;
			kfree_skb(skb, FREE_WRITE);
			if (!sk->dead) wake_up(sk->sleep);
		} else {
			sk->prot->queue_xmit(sk, skb->dev, skb, skb->free);
		}
	}
}


/*
 * This routine sorts the send list, and resets the
 * sk->send_head and sk->send_tail pointers.
 */
void
sort_send(struct sock *sk)
{
  struct sk_buff *list = NULL;
  struct sk_buff *skb,*skb2,*skb3;

  for (skb = sk->send_head; skb != NULL; skb = skb2) {
	skb2 = (struct sk_buff *)skb->link3;
	if (list == NULL || before (skb2->h.seq, list->h.seq)) {
		skb->link3 = list;
		sk->send_tail = skb;
		list = skb;
	} else {
		for (skb3 = list; ; skb3 = (struct sk_buff *)skb3->link3) {
			if (skb3->link3 == NULL ||
			    before(skb->h.seq, skb3->link3->h.seq)) {
				skb->link3 = skb3->link3;
				skb3->link3 = skb;
				if (skb->link3 == NULL) sk->send_tail = skb;
				break;
			}
		}
	}
  }
  sk->send_head = list;
}
  

/* This routine deals with incoming acks, but not outgoing ones. */
static int
tcp_ack(struct sock *sk, struct tcphdr *th, unsigned long saddr, int len)
{
  unsigned long ack;
  int flag = 0;

  ack = ntohl(th->ack_seq);
  DPRINTF((DBG_TCP, "tcp_ack ack=%d, window=%d, "
	  "sk->rcv_ack_seq=%d, sk->window_seq = %d\n",
	  ack, ntohs(th->window), sk->rcv_ack_seq, sk->window_seq));

  if (after(ack, sk->send_seq+1) || before(ack, sk->rcv_ack_seq-1)) {
	if (after(ack, sk->send_seq) ||
	   (sk->state != TCP_ESTABLISHED && sk->state != TCP_CLOSE_WAIT)) {
		return(0);
	}
	if (sk->keepopen) {
		sk->time_wait.len = TCP_TIMEOUT_LEN;
		sk->timeout = TIME_KEEPOPEN;
		reset_timer((struct timer *)&sk->time_wait);
	}
	return(1);
  }

  if (len != th->doff*4) flag |= 1;

  /* See if our window has been shrunk. */
  if (after(sk->window_seq, ack+ntohs(th->window))) {
	/*
	 * We may need to move packets from the send queue
	 * to the write queue, if the window has been shrunk on us.
	 * The RFC says you are not allowed to shrink your window
	 * like this, but if the other end does, you must be able
	 * to deal with it.
	 */
	struct sk_buff *skb;
	struct sk_buff *skb2;
	struct sk_buff *wskb = NULL;
  
	skb2 = sk->send_head;
	sk->send_head = NULL;
	sk->send_tail = NULL;

	flag |= 4;

	sk->window_seq = ack + ntohs(th->window);
	cli();
	while (skb2 != NULL) {
		skb = skb2;
		skb2 = (struct sk_buff *)skb->link3;
		skb->link3 = NULL;
		if (after(skb->h.seq, sk->window_seq)) {
			if (sk->packets_out > 0) sk->packets_out--;

			/* We may need to remove this from the dev send list. */
			if (skb->next != NULL) {
				int i;

				if (skb->next != skb) {
					skb->next->prev = skb->prev;
					skb->prev->next = skb->next;
				}

				for(i = 0; i < DEV_NUMBUFFS; i++) {
					if (skb->dev->buffs[i] == skb) {
						if (skb->next == skb)
							skb->dev->buffs[i] = NULL;
						  else
							skb->dev->buffs[i] = skb->next;
						break;
					}
				}
				if (arp_q == skb) {
					if (skb->next == skb) arp_q = NULL;
					  else arp_q = skb->next;
				}
			}

			/* Now add it to the write_queue. */
			skb->magic = TCP_WRITE_QUEUE_MAGIC;
			if (wskb == NULL) {
				skb->next = sk->wfront;
				sk->wfront = skb;
			} else {
				skb->next = wskb->next;
				wskb->next = skb;
			}
			if (sk->wback == wskb) sk->wback = skb;
			wskb = skb;
		} else {
			if (sk->send_head == NULL) {
				sk->send_head = skb;
				sk->send_tail = skb;
			} else {
				sk->send_tail->link3 = skb;
				sk->send_tail = skb;
			}
			skb->link3 = NULL;
		}
	}
	sti();
  }

  if (sk->send_tail == NULL || sk->send_head == NULL) {
	sk->send_head = NULL;
	sk->send_tail = NULL;
	sk->packets_out= 0;
  }

  sk->window_seq = ack + ntohs(th->window);

  /* We don't want too many packets out there. */
  if (sk->cong_window < 2048 && ack != sk->rcv_ack_seq) {
	if (sk->exp_growth) sk->cong_window *= 2;
	  else sk->cong_window++;
  }

  DPRINTF((DBG_TCP, "tcp_ack: Updating rcv ack sequence.\n"));
  sk->rcv_ack_seq = ack;

  /* See if we can take anything off of the retransmit queue. */
  while(sk->send_head != NULL) {
	/* Check for a bug. */
	if (sk->send_head->link3 &&
	    after(sk->send_head->h.seq, sk->send_head->link3->h.seq)) {
		printk("INET: tcp.c: *** bug send_list out of order.\n");
		sort_send(sk);
	}

	if (before(sk->send_head->h.seq, ack+1)) {
		struct sk_buff *oskb;

		sk->retransmits = 0;

		/* We have one less packet out there. */
		if (sk->packets_out > 0) sk->packets_out --;
		DPRINTF((DBG_TCP, "skb=%X skb->h.seq = %d acked ack=%d\n",
				sk->send_head, sk->send_head->h.seq, ack));

		/* Wake up the process, it can probably write more. */
		if (!sk->dead) wake_up(sk->sleep);

		oskb = sk->send_head;

		/* Estimate the RTT. Ignore the ones right after a retransmit. */
		if (sk->retransmits == 0 && !(flag&2)) {
		  long abserr, rtt = jiffies - oskb->when;

		  if (sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV)
		    /* first ack, so nothing else to average with */
		    sk->rtt = rtt;
		  else {
		    abserr = (rtt > sk->rtt) ? rtt - sk->rtt : sk->rtt - rtt;
		    sk->rtt = (7 * sk->rtt + rtt) >> 3;
		    sk->mdev = (3 * sk->mdev + abserr) >> 2;
		  }
		  sk->backoff = 0;
		}
		flag |= (2|4);
		/* no point retransmitting faster than .1 sec */
		/* 2 minutes is max legal rtt for Internet */
		if (sk->rtt < 10) sk->rtt = 10;
		if (sk->rtt > 12000) sk->rtt = 12000;

		cli();

		oskb = sk->send_head;
		sk->send_head =(struct sk_buff *)oskb->link3;
		if (sk->send_head == NULL) {
			sk->send_tail = NULL;
		}

		/* We may need to remove this from the dev send list. */
		if (oskb->next != NULL) {
			int i;

			if (oskb->next != oskb) {
				oskb->next->prev = oskb->prev;
				oskb->prev->next = oskb->next;
			}
			for(i = 0; i < DEV_NUMBUFFS; i++) {
				if (oskb->dev->buffs[i] == oskb) {
					if (oskb== oskb->next)
						oskb->dev->buffs[i]= NULL;
					  else
						oskb->dev->buffs[i] = oskb->next;
					break;
				}
			}
			if (arp_q == oskb) {
				if (oskb == oskb->next) arp_q = NULL;
				  else arp_q =(struct sk_buff *)oskb->next;
			}
		}
		sti();
		oskb->magic = 0;
		kfree_skb(oskb, FREE_WRITE); /* write. */
		if (!sk->dead) wake_up(sk->sleep);
	} else {
		break;
	}
  }

  /*
   * Maybe we can take some stuff off of the write queue,
   * and put it onto the xmit queue.
   */
  if (sk->wfront != NULL) {
	if (after (sk->window_seq, sk->wfront->h.seq) &&
		sk->packets_out < sk->cong_window) {
		flag |= 1;
		tcp_write_xmit(sk);
	}
  } else {
	if (sk->send_head == NULL && sk->ack_backlog == 0 &&
	    sk->state != TCP_TIME_WAIT && !sk->keepopen) {
		DPRINTF((DBG_TCP, "Nothing to do, going to sleep.\n")); 
		if (!sk->dead) wake_up(sk->sleep);

		delete_timer((struct timer *)&sk->time_wait);
		sk->timeout = 0;
	} else {
		if (sk->state != (unsigned char) sk->keepopen) {
			sk->timeout = TIME_WRITE;
			sk->time_wait.len = backoff(sk->backoff) *
			  (2 * sk->mdev + sk->rtt);
			reset_timer((struct timer *)&sk->time_wait);
		}
		if (sk->state == TCP_TIME_WAIT) {
			sk->time_wait.len = TCP_TIMEWAIT_LEN;
			reset_timer((struct timer *)&sk->time_wait);
			sk->timeout = TIME_CLOSE;
		}
	}
  }

  if (sk->packets_out == 0 && sk->send_tmp != NULL &&
      sk->wfront == NULL && sk->send_head == NULL) {
	flag |= 1;
	tcp_send_partial(sk);
  }

  /* See if we are done. */
  if (sk->state == TCP_TIME_WAIT) {
	if (!sk->dead) wake_up(sk->sleep);
	if (sk->rcv_ack_seq == sk->send_seq && sk->acked_seq == sk->fin_seq) {
		flag |= 1;
		sk->state = TCP_CLOSE;
		sk->shutdown = SHUTDOWN_MASK;
	}
  }

  if (sk->state == TCP_LAST_ACK || sk->state == TCP_FIN_WAIT2) {
	if (!sk->dead) wake_up(sk->sleep);
	if (sk->rcv_ack_seq == sk->send_seq) {
		flag |= 1;
		if (sk->acked_seq != sk->fin_seq) {
			tcp_time_wait(sk);
		} else {
			DPRINTF((DBG_TCP, "tcp_ack closing socket - %X\n", sk));
			tcp_send_ack(sk->send_seq, sk->acked_seq, sk,
				     th, sk->daddr);
			sk->shutdown = SHUTDOWN_MASK;
			sk->state = TCP_CLOSE;
		}
	}
  }

  if (((!flag) || (flag&4)) && sk->send_head != NULL &&
      (sk->send_head->when + backoff(sk->backoff) * (2 * sk->mdev + sk->rtt)
       < jiffies)) {
	sk->exp_growth = 0;
	ip_retransmit(sk, 0);
  }

  DPRINTF((DBG_TCP, "leaving tcp_ack\n"));
  return(1);
}


/*
 * This routine handles the data.  If there is room in the buffer,
 * it will be have already been moved into it.  If there is no
 * room, then we will just have to discard the packet.
 */
static int
tcp_data(struct sk_buff *skb, struct sock *sk, 
	 unsigned long saddr, unsigned short len)
{
  struct sk_buff *skb1, *skb2;
  struct tcphdr *th;

  th = skb->h.th;
  print_th(th);
  skb->len = len -(th->doff*4);

  DPRINTF((DBG_TCP, "tcp_data len = %d sk = %X:\n", skb->len, sk));

  sk->bytes_rcv += skb->len;
  if (skb->len == 0 && !th->fin && !th->urg && !th->psh) {
	/* Don't want to keep passing ack's back and fourth. */
	if (!th->ack) tcp_send_ack(sk->send_seq, sk->acked_seq,sk, th, saddr);
	kfree_skb(skb, FREE_READ);
	return(0);
  }

  if (sk->shutdown & RCV_SHUTDOWN) {
	sk->acked_seq = th->seq + skb->len + th->syn + th->fin;
	tcp_reset(sk->saddr, sk->daddr, skb->h.th,
	sk->prot, NULL, skb->dev);
	sk->state = TCP_CLOSE;
	sk->err = EPIPE;
	sk->shutdown = SHUTDOWN_MASK;
	DPRINTF((DBG_TCP, "tcp_data: closing socket - %X\n", sk));
	kfree_skb(skb, FREE_READ);
	if (!sk->dead) wake_up(sk->sleep);
	return(0);
  }

  /*
   * Now we have to walk the chain, and figure out where this one
   * goes into it.  This is set up so that the last packet we received
   * will be the first one we look at, that way if everything comes
   * in order, there will be no performance loss, and if they come
   * out of order we will be able to fit things in nicely.
   */

  /* This should start at the last one, and then go around forwards. */
  if (sk->rqueue == NULL) {
	DPRINTF((DBG_TCP, "tcp_data: skb = %X:\n", skb));

	sk->rqueue = skb;
	skb->next = skb;
	skb->prev = skb;
	skb1= NULL;
  } else {
	DPRINTF((DBG_TCP, "tcp_data adding to chain sk = %X:\n", sk));

	for(skb1=sk->rqueue; ; skb1 =(struct sk_buff *)skb1->prev) {
		DPRINTF((DBG_TCP, "skb1=%X\n", skb1));
		DPRINTF((DBG_TCP, "skb1->h.th->seq = %d\n", skb1->h.th->seq));
		if (after(th->seq+1, skb1->h.th->seq)) {
			skb->prev = skb1;
			skb->next = skb1->next;
			skb->next->prev = skb;
			skb1->next = skb;
			if (skb1 == sk->rqueue) sk->rqueue = skb;
			break;
		}
		if (skb1->prev == sk->rqueue) {
			skb->next= skb1;
			skb->prev = skb1->prev;
			skb->prev->next = skb;
			skb1->prev = skb;
			skb1 = NULL; /* so we know we might be able
					to ack stuff. */
			break;
		}
	}
	DPRINTF((DBG_TCP, "skb = %X:\n", skb));
  }

  th->ack_seq = th->seq + skb->len;
  if (th->syn) th->ack_seq++;
  if (th->fin) th->ack_seq++;

  if (before(sk->acked_seq, sk->copied_seq)) {
	printk("*** tcp.c:tcp_data bug acked < copied\n");
	sk->acked_seq = sk->copied_seq;
  }

  /* Now figure out if we can ack anything. */
  if (skb1 == NULL || skb1->acked || before(th->seq, sk->acked_seq+1)) {
      if (before(th->seq, sk->acked_seq+1)) {
		if (after(th->ack_seq, sk->acked_seq))
					sk->acked_seq = th->ack_seq;
		skb->acked = 1;

		/* When we ack the fin, we turn on the RCV_SHUTDOWN flag. */
		if (skb->h.th->fin) {
			if (!sk->dead) wake_up(sk->sleep);
			sk->shutdown |= RCV_SHUTDOWN;
		}
	  
		for(skb2 = (struct sk_buff *)skb->next;
		    skb2 !=(struct sk_buff *) sk->rqueue->next;
		    skb2 = (struct sk_buff *)skb2->next) {
			if (before(skb2->h.th->seq, sk->acked_seq+1)) {
				if (after(skb2->h.th->ack_seq, sk->acked_seq))
					sk->acked_seq = skb2->h.th->ack_seq;
				skb2->acked = 1;

				/*
				 * When we ack the fin, we turn on
				 * the RCV_SHUTDOWN flag.
				 */
				if (skb2->h.th->fin) {
					sk->shutdown |= RCV_SHUTDOWN;
					if (!sk->dead) wake_up(sk->sleep);
				}

				/* Force an immediate ack. */
				sk->ack_backlog = sk->max_ack_backlog;
			} else {
				break;
			}
		}

		/*
		 * This also takes care of updating the window.
		 * This if statement needs to be simplified.
		 */
		if (!sk->delay_acks ||
		    sk->ack_backlog >= sk->max_ack_backlog || 
		    sk->bytes_rcv > sk->max_unacked || th->fin) {
/*			tcp_send_ack(sk->send_seq, sk->acked_seq,sk,th, saddr); */
		} else {
			sk->ack_backlog++;
			sk->time_wait.len = TCP_ACK_TIME;
			sk->timeout = TIME_WRITE;
			reset_timer((struct timer *)&sk->time_wait);
		}
	}
  }

  /*
   * If we've missed a packet, send an ack.
   * Also start a timer to send another.
   */
  if (!skb->acked) {
	/*
	 * This is important.  If we don't have much room left,
	 * we need to throw out a few packets so we have a good
	 * window.
	 */
	while (sk->prot->rspace(sk) < sk->mtu) {
		skb1 = (struct sk_buff *)sk->rqueue;
		if (skb1 == NULL) {
			printk("INET: tcp.c:tcp_data memory leak detected.\n");
			break;
		}

		/* Don't throw out something that has been acked. */
		if (skb1->acked) {
			break;
		}
		if (skb1->prev == skb1) {
			sk->rqueue = NULL;
		} else {
			sk->rqueue = (struct sk_buff *)skb1->prev;
			skb1->next->prev = skb1->prev;
			skb1->prev->next = skb1->next;
		}
		kfree_skb(skb1, FREE_READ);
	}
	tcp_send_ack(sk->send_seq, sk->acked_seq, sk, th, saddr);
	sk->ack_backlog++;
	sk->time_wait.len = TCP_ACK_TIME;
	sk->timeout = TIME_WRITE;
	reset_timer((struct timer *)&sk->time_wait);
  } else {
	/* We missed a packet.  Send an ack to try to resync things. */
	tcp_send_ack(sk->send_seq, sk->acked_seq, sk, th, saddr);
  }

  /* Now tell the user we may have some data. */
  if (!sk->dead) {
	wake_up(sk->sleep);
  } else {
	DPRINTF((DBG_TCP, "data received on dead socket.\n"));
  }

  if (sk->state == TCP_FIN_WAIT2 &&
      sk->acked_seq == sk->fin_seq && sk->rcv_ack_seq == sk->send_seq) {
	DPRINTF((DBG_TCP, "tcp_data: entering last_ack state sk = %X\n", sk));

/*	tcp_send_ack(sk->send_seq, sk->acked_seq, sk, th, saddr); */
	sk->shutdown = SHUTDOWN_MASK;
	sk->state = TCP_LAST_ACK;
	if (!sk->dead) wake_up(sk->sleep);
  }

  return(0);
}


static int
tcp_urg(struct sock *sk, struct tcphdr *th, unsigned long saddr)
{
  extern int kill_pg(int pg, int sig, int priv);
  extern int kill_proc(int pid, int sig, int priv);
    
  if (!sk->dead) wake_up(sk->sleep);
    
  if (sk->urginline) {
	th->urg = 0;
	th->psh = 1;
	return(0);
  }

  if (!sk->urg) {
	/* So if we get more urgent data, we don't signal the user again. */
	if (sk->proc != 0) {
		if (sk->proc > 0) {
			kill_proc(sk->proc, SIGURG, 1);
		} else {
			kill_pg(-sk->proc, SIGURG, 1);
		}
	}
  }
  sk->urg++;
  return(0);
}


/* This deals with incoming fins. */
static int
tcp_fin(struct sock *sk, struct tcphdr *th, 
	 unsigned long saddr, struct device *dev)
{
  DPRINTF((DBG_TCP, "tcp_fin(sk=%X, th=%X, saddr=%X, dev=%X)\n",
						sk, th, saddr, dev));
  
  if (!sk->dead) {
	wake_up(sk->sleep);
  }

  switch(sk->state) {
	case TCP_SYN_RECV:
	case TCP_SYN_SENT:
	case TCP_ESTABLISHED:
		/* Contains the one that needs to be acked */
		sk->fin_seq = th->seq+1;
		sk->state = TCP_CLOSE_WAIT;
		if (th->rst) sk->shutdown = SHUTDOWN_MASK;
		break;

	case TCP_CLOSE_WAIT:
	case TCP_FIN_WAIT2:
		break; /* we got a retransmit of the fin. */

	case TCP_FIN_WAIT1:
		/* Contains the one that needs to be acked */
		sk->fin_seq = th->seq+1;
		sk->state = TCP_FIN_WAIT2;
		break;

	default:
	case TCP_TIME_WAIT:
		sk->state = TCP_LAST_ACK;

		/* Start the timers. */
		sk->time_wait.len = TCP_TIMEWAIT_LEN;
		sk->timeout = TIME_CLOSE;
		reset_timer((struct timer *)&sk->time_wait);
		return(0);
  }
  sk->ack_backlog++;

  return(0);
}


/* This will accept the next outstanding connection. */
static struct sock *
tcp_accept(struct sock *sk, int flags)
{
  struct sock *newsk;
  struct sk_buff *skb;
  
  DPRINTF((DBG_TCP, "tcp_accept(sk=%X, flags=%X, addr=%s)\n",
				sk, flags, in_ntoa(sk->saddr)));

  /*
   * We need to make sure that this socket is listening,
   * and that it has something pending.
   */
  if (sk->state != TCP_LISTEN) {
	sk->err = EINVAL;
	return(NULL); 
  }

  /* avoid the race. */
  cli();
  sk->inuse = 1;
  while((skb = get_firstr(sk)) == NULL) {
	if (flags & O_NONBLOCK) {
		sti();
		release_sock(sk);
		sk->err = EAGAIN;
		return(NULL);
	}

	release_sock(sk);
	interruptible_sleep_on(sk->sleep);
	if (current->signal & ~current->blocked) {
		sti();
		sk->err = ERESTARTSYS;
		return(NULL);
	}
	sk->inuse = 1;
  }
  sti();

  /* Now all we need to do is return skb->sk. */
  newsk = skb->sk;

  kfree_skb(skb, FREE_READ);
  sk->ack_backlog--;
  release_sock(sk);
  return(newsk);
}


/* This will initiate an outgoing connection. */
static int
tcp_connect(struct sock *sk, struct sockaddr_in *usin, int addr_len)
{
  struct sk_buff *buff;
  struct sockaddr_in sin;
  struct device *dev=NULL;
  unsigned char *ptr;
  int tmp;
  struct tcphdr *t1;

  if (sk->state != TCP_CLOSE) return(-EISCONN);
  if (addr_len < 8) return(-EINVAL);

  /* verify_area(VERIFY_WRITE, usin, addr_len);*/
  memcpy_fromfs(&sin,usin, min(sizeof(sin), addr_len));

  if (sin.sin_family && sin.sin_family != AF_INET) return(-EAFNOSUPPORT);

  DPRINTF((DBG_TCP, "TCP connect daddr=%s\n", in_ntoa(sin.sin_addr.s_addr)));
  
  /* Don't want a TCP connection going to a broadcast address */
  if (chk_addr(sin.sin_addr.s_addr) == IS_BROADCAST) { 
	DPRINTF((DBG_TCP, "TCP connection to broadcast address not allowed\n"));
	return(-ENETUNREACH);
  }
  sk->inuse = 1;
  sk->daddr = sin.sin_addr.s_addr;
  sk->send_seq = timer_seq*SEQ_TICK-seq_offset;
  sk->rcv_ack_seq = sk->send_seq -1;
  sk->err = 0;
  sk->dummy_th.dest = sin.sin_port;
  release_sock(sk);

  buff = (struct sk_buff *) sk->prot->wmalloc(sk,MAX_SYN_SIZE,0, GFP_KERNEL);
  if (buff == NULL) {
	return(-ENOMEM);
  }
  sk->inuse = 1;
  buff->lock = 0;
  buff->mem_addr = buff;
  buff->mem_len = MAX_SYN_SIZE;
  buff->len = 24;
  buff->sk = sk;
  t1 = (struct tcphdr *)(buff + 1);

  /* Put in the IP header and routing stuff. */
  /* We need to build the routing stuff fromt the things saved in skb. */
  tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
					IPPROTO_TCP, NULL, MAX_SYN_SIZE);
  if (tmp < 0) {
	sk->prot->wfree(sk, buff->mem_addr, buff->mem_len);
	release_sock(sk);
	return(-ENETUNREACH);
  }
  buff->len += tmp;
  t1 = (struct tcphdr *)((char *)t1 +tmp);

  memcpy(t1,(void *)&(sk->dummy_th), sizeof(*t1));
  t1->seq = ntohl(sk->send_seq++);
  buff->h.seq = sk->send_seq;
  t1->ack = 0;
  t1->window = 2;
  t1->res1=0;
  t1->res2=0;
  t1->rst = 0;
  t1->urg = 0;
  t1->psh = 0;
  t1->syn = 1;
  t1->urg_ptr = 0;
  t1->doff = 6;

  /* Put in the TCP options to say MTU. */
  ptr = (unsigned char *)(t1+1);
  ptr[0] = 2;
  ptr[1] = 4;
  ptr[2] = (dev->mtu- HEADER_SIZE) >> 8;
  ptr[3] = (dev->mtu- HEADER_SIZE) & 0xff;
  sk->mtu = dev->mtu - HEADER_SIZE;
  tcp_send_check(t1, sk->saddr, sk->daddr,
		  sizeof(struct tcphdr) + 4, sk);

  /* This must go first otherwise a really quick response will get reset. */
  sk->state = TCP_SYN_SENT;

  sk->prot->queue_xmit(sk, dev, buff, 0);
  
  sk->time_wait.len = TCP_CONNECT_TIME;
  sk->rtt = TCP_CONNECT_TIME;
  reset_timer((struct timer *)&sk->time_wait);
  sk->retransmits = TCP_RETR2 - TCP_SYN_RETRIES;
  release_sock(sk);
  return(0);
}


/* This functions checks to see if the tcp header is actually acceptible. */
static int
tcp_sequence(struct sock *sk, struct tcphdr *th, short len,
	     struct options *opt, unsigned long saddr)
{
  /*
   * This isn't quite right.  sk->acked_seq could be more recent
   * than sk->window.  This is however close enough.  We will accept
   * slightly more packets than we should, but it should not cause
   * problems unless someone is trying to forge packets.
   */
  DPRINTF((DBG_TCP, "tcp_sequence(sk=%X, th=%X, len = %d, opt=%d, saddr=%X)\n",
	  sk, th, len, opt, saddr));

  if (between(th->seq, sk->acked_seq, sk->acked_seq + sk->window)||
      between(th->seq + len-(th->doff*4), sk->acked_seq + 1,
	      sk->acked_seq + sk->window) ||
     (before(th->seq, sk->acked_seq) &&
       after(th->seq + len -(th->doff*4), sk->acked_seq + sk->window))) {
       return(1);
   }
  DPRINTF((DBG_TCP, "tcp_sequence: rejecting packet.\n"));

  /*
   * If it's too far ahead, send an ack to let the
   * other end know what we expect.
   */
  if (after(th->seq, sk->acked_seq + sk->window)) {
	tcp_send_ack(sk->send_seq, sk->acked_seq, sk, th, saddr);
	return(0);
  }

  /* In case it's just a late ack, let it through. */
  if (th->ack && len == (th->doff * 4) &&
      after(th->seq, sk->acked_seq - 32767) &&
      !th->fin && !th->syn) return(1);

  if (!th->rst) {
	/* Try to resync things. */
	tcp_send_ack(sk->send_seq, sk->acked_seq, sk, th, saddr);
  }
  return(0);
}


/* This deals with the tcp option.  It isn't very general yet. */
static void
tcp_options(struct sock *sk, struct tcphdr *th)
{
  unsigned char *ptr;

  ptr = (unsigned char *)(th + 1);
  if (ptr[0] != 2 || ptr[1] != 4) {
	sk->mtu = min(sk->mtu, 576 - HEADER_SIZE);
	return;
  }
  sk->mtu = min(sk->mtu, ptr[2]*256 + ptr[3] - HEADER_SIZE);
}


int
tcp_rcv(struct sk_buff *skb, struct device *dev, struct options *opt,
	unsigned long daddr, unsigned short len,
	unsigned long saddr, int redo, struct inet_protocol * protocol)
{
  struct tcphdr *th;
  struct sock *sk;

  if (!skb) {
	DPRINTF((DBG_TCP, "tcp.c: tcp_rcv skb = NULL\n"));
	return(0);
  }
#if 0	/* FIXME: it's ok for protocol to be NULL */
  if (!protocol) {
	DPRINTF((DBG_TCP, "tcp.c: tcp_rcv protocol = NULL\n"));
	return(0);
  }

  if (!opt) {	/* FIXME: it's ok for opt to be NULL */
	DPRINTF((DBG_TCP, "tcp.c: tcp_rcv opt = NULL\n"));
  }
#endif
  if (!dev) {
	DPRINTF((DBG_TCP, "tcp.c: tcp_rcv dev = NULL\n"));
	return(0);
  }
  th = skb->h.th;

  /* Find the socket. */
  sk = get_sock(&tcp_prot, th->dest, saddr, th->source, daddr);
  DPRINTF((DBG_TCP, "<<\n"));
  DPRINTF((DBG_TCP, "len = %d, redo = %d, skb=%X\n", len, redo, skb));

  if (sk) {
	 DPRINTF((DBG_TCP, "sk = %X:\n", sk));
  }

  if (!redo) {
	if (th->check && tcp_check(th, len, saddr, daddr )) {
		skb->sk = NULL;
		DPRINTF((DBG_TCP, "packet dropped with bad checksum.\n"));
if (inet_debug == DBG_SLIP) printk("\rtcp_rcv: back checksum\n");
		kfree_skb(skb, 0);
		/*
		 * We don't release the socket because it was
		 * never marked in use.
		 */
		return(0);
	}

	/* See if we know about the socket. */
	if (sk == NULL) {
		if (!th->rst) tcp_reset(daddr, saddr, th, &tcp_prot, opt,dev);
		skb->sk = NULL;
		kfree_skb(skb, 0);
		return(0);
	}

	skb->len = len;
	skb->sk = sk;
	skb->acked = 0;
	skb->used = 0;
	skb->free = 0;
	skb->urg_used = 0;
	skb->saddr = daddr;
	skb->daddr = saddr;

	th->seq = ntohl(th->seq);

       /* We may need to add it to the backlog here. */
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
  } else {
	if (!sk) {
		DPRINTF((DBG_TCP, "tcp.c: tcp_rcv bug sk=NULL redo = 1\n"));
		return(0);
	}
  }

  if (!sk->prot) {
	DPRINTF((DBG_TCP, "tcp.c: tcp_rcv sk->prot = NULL \n"));
	return(0);
  }

  /* Charge the memory to the socket. */
  if (sk->rmem_alloc + skb->mem_len >= SK_RMEM_MAX) {
	skb->sk = NULL;
	DPRINTF((DBG_TCP, "dropping packet due to lack of buffer space.\n"));
	kfree_skb(skb, 0);
	release_sock(sk);
	return(0);
  }
  sk->rmem_alloc += skb->mem_len;

  DPRINTF((DBG_TCP, "About to do switch.\n"));

  /* Now deal with it. */
  switch(sk->state) {
	/*
	 * This should close the system down if it's waiting
	 * for an ack that is never going to be sent.
	 */
	case TCP_LAST_ACK:
		if (th->rst) {
			sk->err = ECONNRESET;
			sk->state = TCP_CLOSE;
			sk->shutdown = SHUTDOWN_MASK;
			if (!sk->dead) {
				wake_up(sk->sleep);
			}
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}

	case TCP_ESTABLISHED:
	case TCP_CLOSE_WAIT:
	case TCP_FIN_WAIT1:
	case TCP_FIN_WAIT2:
	case TCP_TIME_WAIT:
		if (!tcp_sequence(sk, th, len, opt, saddr)) {
if (inet_debug == DBG_SLIP) printk("\rtcp_rcv: not in seq\n");
			tcp_send_ack(sk->send_seq, sk->acked_seq, 
				     sk, th, saddr);
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}

		if (th->rst) {
			/* This means the thing should really be closed. */
			sk->err = ECONNRESET;

			if (sk->state == TCP_CLOSE_WAIT) {
				sk->err = EPIPE;
			}

			/*
			 * A reset with a fin just means that
			 * the data was not all read.
			 */
/* The comment above appears completely bogus --clh */
/*			if (!th->fin) { */
				sk->state = TCP_CLOSE;
				sk->shutdown = SHUTDOWN_MASK;
				if (!sk->dead) {
					wake_up(sk->sleep);
				}
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
/*			} */
		}
#if 0
		if (opt && (opt->security != 0 ||
			    opt->compartment != 0 || th->syn)) {
			sk->err = ECONNRESET;
			sk->state = TCP_CLOSE;
			sk->shutdown = SHUTDOWN_MASK;
			tcp_reset(daddr, saddr,  th, sk->prot, opt,dev);
			if (!sk->dead) {
				wake_up(sk->sleep);
			}
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}
#endif
		if (th->ack) {
			if (!tcp_ack(sk, th, saddr, len)) {
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
		}
		if (th->urg) {
			if (tcp_urg(sk, th, saddr)) {
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
		}

		if (th->fin && tcp_fin(sk, th, saddr, dev)) {
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}

		if (tcp_data(skb, sk, saddr, len)) {
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}

		release_sock(sk);
		return(0);

	case TCP_CLOSE:
		if (sk->dead || sk->daddr) {
			DPRINTF((DBG_TCP, "packet received for closed,dead socket\n"));
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}

		if (!th->rst) {
			if (!th->ack)
			th->ack_seq = 0;
			tcp_reset(daddr, saddr, th, sk->prot, opt,dev);
		}
		kfree_skb(skb, FREE_READ);
		release_sock(sk);
		return(0);

	case TCP_LISTEN:
		if (th->rst) {
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}
		if (th->ack) {
			tcp_reset(daddr, saddr, th, sk->prot, opt,dev);
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}

		if (th->syn) {
#if 0
			if (opt->security != 0 || opt->compartment != 0) {
				tcp_reset(daddr, saddr, th, prot, opt,dev);
				release_sock(sk);
				return(0);
			}
#endif

			/*
			 * Now we just put the whole thing including
			 * the header and saddr, and protocol pointer
			 * into the buffer.  We can't respond until the
			 * user tells us to accept the connection.
			 */
			tcp_conn_request(sk, skb, daddr, saddr, opt, dev);
			release_sock(sk);
			return(0);
		}

		kfree_skb(skb, FREE_READ);
		release_sock(sk);
		return(0);

	default:
		if (!tcp_sequence(sk, th, len, opt, saddr)) {
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}

	case TCP_SYN_SENT:
		if (th->rst) {
			sk->err = ECONNREFUSED;
			sk->state = TCP_CLOSE;
			sk->shutdown = SHUTDOWN_MASK;
			if (!sk->dead) {
				wake_up(sk->sleep);
			}
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}
#if 0
		if (opt->security != 0 || opt->compartment != 0) {
			sk->err = ECONNRESET;
			sk->state = TCP_CLOSE;
			sk->shutdown = SHUTDOWN_MASK;
			tcp_reset(daddr, saddr,  th, sk->prot, opt, dev);
			if (!sk->dead) {
				wake_up(sk->sleep);
			}
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}
#endif
		if (!th->ack) {
			if (th->syn) {
				sk->state = TCP_SYN_RECV;
			}

			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}

		switch(sk->state) {
			case TCP_SYN_SENT:
				if (!tcp_ack(sk, th, saddr, len)) {
					tcp_reset(daddr, saddr, th,
							sk->prot, opt,dev);
					kfree_skb(skb, FREE_READ);
					release_sock(sk);
					return(0);
				}

				/*
				 * If the syn bit is also set, switch to
				 * tcp_syn_recv, and then to established.
				 */
				if (!th->syn) {
					kfree_skb(skb, FREE_READ);
					release_sock(sk);
					return(0);
				}

				/* Ack the syn and fall through. */
				sk->acked_seq = th->seq+1;
				sk->fin_seq = th->seq;
				tcp_send_ack(sk->send_seq, th->seq+1,
							sk, th, sk->daddr);
	
			case TCP_SYN_RECV:
				if (!tcp_ack(sk, th, saddr, len)) {
					tcp_reset(daddr, saddr, th,
							sk->prot, opt, dev);
					kfree_skb(skb, FREE_READ);
					release_sock(sk);
					return(0);
				}
				sk->state = TCP_ESTABLISHED;

				/*
				 * Now we need to finish filling out
				 * some of the tcp header.
				 */
				/* We need to check for mtu info. */
				tcp_options(sk, th);
				sk->dummy_th.dest = th->source;
				sk->copied_seq = sk->acked_seq-1;
				if (!sk->dead) {
					wake_up(sk->sleep);
				}

				/*
				 * Now process the rest like we were
				 * already in the established state.
				 */
				if (th->urg) {
					if (tcp_urg(sk, th, saddr)) { 
						kfree_skb(skb, FREE_READ);
						release_sock(sk);
						return(0);
					}
			}
			if (tcp_data(skb, sk, saddr, len))
						kfree_skb(skb, FREE_READ);

			if (th->fin) tcp_fin(sk, th, saddr, dev);
			release_sock(sk);
			return(0);
		}

		if (th->urg) {
			if (tcp_urg(sk, th, saddr)) {
				kfree_skb(skb, FREE_READ);
				release_sock(sk);
				return(0);
			}
		}

		if (tcp_data(skb, sk, saddr, len)) {
			kfree_skb(skb, FREE_READ);
			release_sock(sk);
			return(0);
		}

		if (!th->fin) {
			release_sock(sk);
			return(0);
		}
		tcp_fin(sk, th, saddr, dev);
		release_sock(sk);
		return(0);
	}
}


/*
  * This routine sends a packet with an out of date sequence
  * number. It assumes the other end will try to ack it.
  */
static void
tcp_write_wakeup(struct sock *sk)
{
  struct sk_buff *buff;
  struct tcphdr *t1;
  struct device *dev=NULL;
  int tmp;

  if (sk -> state != TCP_ESTABLISHED && sk->state != TCP_CLOSE_WAIT) return;

  buff = (struct sk_buff *) sk->prot->wmalloc(sk,MAX_ACK_SIZE,1, GFP_ATOMIC);
  if (buff == NULL) return;

  buff->lock = 0;
  buff->mem_addr = buff;
  buff->mem_len = MAX_ACK_SIZE;
  buff->len = sizeof(struct tcphdr);
  buff->free = 1;
  buff->sk = sk;
  DPRINTF((DBG_TCP, "in tcp_write_wakeup\n"));
  t1 = (struct tcphdr *)(buff + 1);

  /* Put in the IP header and routing stuff. */
  tmp = sk->prot->build_header(buff, sk->saddr, sk->daddr, &dev,
				IPPROTO_TCP, sk->opt, MAX_ACK_SIZE);
  if (tmp < 0) {
	sk->prot->wfree(sk, buff->mem_addr, buff->mem_len);
	return;
  }

  buff->len += tmp;
  t1 = (struct tcphdr *)((char *)t1 +tmp);

  memcpy(t1,(void *) &sk->dummy_th, sizeof(*t1));

  /*
   * Use a previous sequence.
   * This should cause the other end to send an ack.
   */
  t1->seq = ntohl(sk->send_seq-1);
  t1->ack = 1; 
  t1->res1= 0;
  t1->res2= 0;
  t1->rst = 0;
  t1->urg = 0;
  t1->psh = 0;
  t1->fin = 0;
  t1->syn = 0;
  t1->ack_seq = ntohl(sk->acked_seq);
  t1->window = ntohs(sk->prot->rspace(sk));
  t1->doff = sizeof(*t1)/4;
  tcp_send_check(t1, sk->saddr, sk->daddr, sizeof(*t1), sk);

  /* Send it and free it.
   * This will prevent the timer from automatically being restarted.
  */
  sk->prot->queue_xmit(sk, dev, buff, 1);
}


struct proto tcp_prot = {
  sock_wmalloc,
  sock_rmalloc,
  sock_wfree,
  sock_rfree,
  sock_rspace,
  sock_wspace,
  tcp_close,
  tcp_read,
  tcp_write,
  tcp_sendto,
  tcp_recvfrom,
  ip_build_header,
  tcp_connect,
  tcp_accept,
  ip_queue_xmit,
  tcp_retransmit,
  tcp_write_wakeup,
  tcp_read_wakeup,
  tcp_rcv,
  tcp_select,
  tcp_ioctl,
  NULL,
  tcp_shutdown,
  128,
  0,
  {NULL,},
  "TCP"
};
