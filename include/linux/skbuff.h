/*
 *	Definitions for the 'struct sk_buff' memory handlers.
 *
 *	Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Florian La Roche, <rzsfl@rz.uni-sb.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
 
#ifndef _LINUX_SKBUFF_H
#define _LINUX_SKBUFF_H
#include <linux/malloc.h>
#include <linux/wait.h>
#include <linux/time.h>

#define CONFIG_SKB_CHECK	1

#define HAVE_ALLOC_SKB		/* For the drivers to know */


#define FREE_READ	1
#define FREE_WRITE	0


struct sk_buff_head {
  struct sk_buff		* volatile next;
  struct sk_buff		* volatile prev;
#if CONFIG_SKB_CHECK
  int				magic_debug_cookie;
#endif
};


struct sk_buff {
  struct sk_buff		* volatile next;
  struct sk_buff		* volatile prev;
#if CONFIG_SKB_CHECK
  int				magic_debug_cookie;
#endif
  struct sk_buff		* volatile link3;
  struct sock			*sk;
  volatile unsigned long	when;	/* used to compute rtt's	*/
  struct timeval		stamp;
  struct device			*dev;
  void				*mem_addr;
  union {
	struct tcphdr	*th;
	struct ethhdr	*eth;
	struct iphdr	*iph;
	struct udphdr	*uh;
	unsigned char	*raw;
	unsigned long	seq;
  } h;
  struct iphdr		*ip_hdr;		/* For IPPROTO_RAW */
  unsigned long			mem_len;
  unsigned long 		len;
  unsigned long			fraglen;
  struct sk_buff		*fraglist;	/* Fragment list */
  unsigned long			truesize;
  unsigned long 		saddr;
  unsigned long 		daddr;
  unsigned long			raddr;		/* next hop addr */
  volatile char 		acked,
				used,
				free,
				arp;
  unsigned char			tries,lock,localroute;
  unsigned short		users;		/* User count - see datagram.c (and soon seqpacket.c/stream.c) */
#ifdef CONFIG_SLAVE_BALANCING
  unsigned short		in_dev_queue;
#endif  
  unsigned long			padding[0];
  unsigned char			data[0];
};

#define SK_WMEM_MAX	32767
#define SK_RMEM_MAX	32767

#ifdef CONFIG_SKB_CHECK
#define SK_FREED_SKB	0x0DE2C0DE
#define SK_GOOD_SKB	0xDEC0DED1
#define SK_HEAD_SKB	0x12231298
#endif

#ifdef __KERNEL__
/*
 *	Handling routines are only of interest to the kernel
 */
 
#if 0
extern void			print_skb(struct sk_buff *);
#endif
extern void			kfree_skb(struct sk_buff *skb, int rw);
extern void			skb_queue_head_init(struct sk_buff_head *list);
extern void			skb_queue_head(struct sk_buff_head *list,struct sk_buff *buf);
extern void			skb_queue_tail(struct sk_buff_head *list,struct sk_buff *buf);
extern struct sk_buff *		skb_dequeue(struct sk_buff_head *list);
extern void 			skb_insert(struct sk_buff *old,struct sk_buff *newsk);
extern void			skb_append(struct sk_buff *old,struct sk_buff *newsk);
extern void			skb_unlink(struct sk_buff *buf);
extern struct sk_buff *		skb_peek_copy(struct sk_buff_head *list);
extern struct sk_buff *		alloc_skb(unsigned int size, int priority);
extern void			kfree_skbmem(void *mem, unsigned size);
extern struct sk_buff *		skb_clone(struct sk_buff *skb, int priority);
extern void			skb_kept_by_device(struct sk_buff *skb);
extern void			skb_device_release(struct sk_buff *skb,
					int mode);
extern int			skb_device_locked(struct sk_buff *skb);
/*
 *	Peek an sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. For an interrupt
 *	type system cli() peek the buffer copy the data and sti();
 */
static __inline__ struct sk_buff *skb_peek(struct sk_buff_head *list_)
{
	struct sk_buff *list = (struct sk_buff *)list_;
	return (list->next != list)? list->next : NULL;
}

#if CONFIG_SKB_CHECK
extern int 			skb_check(struct sk_buff *skb,int,int, char *);
#define IS_SKB(skb)		skb_check((skb), 0, __LINE__,__FILE__)
#define IS_SKB_HEAD(skb)	skb_check((skb), 1, __LINE__,__FILE__)
#else
#define IS_SKB(skb)		0
#define IS_SKB_HEAD(skb)	0
#endif

extern struct sk_buff *		skb_recv_datagram(struct sock *sk,unsigned flags,int noblock, int *err);
extern int			datagram_select(struct sock *sk, int sel_type, select_table *wait);
extern void			skb_copy_datagram(struct sk_buff *from, int offset, char *to,int size);
extern void			skb_free_datagram(struct sk_buff *skb);

#endif	/* __KERNEL__ */
#endif	/* _LINUX_SKBUFF_H */
