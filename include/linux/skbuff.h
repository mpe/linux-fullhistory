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
#include <linux/config.h>

/* #define CONFIG_SKB_CHECK 1 */

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
  struct sk_buff		* volatile next;	/* Next buffer in list */
  struct sk_buff		* volatile prev;	/* Previous buffer in list */
#if CONFIG_SKB_CHECK
  int				magic_debug_cookie;
#endif
  struct sk_buff		* volatile link3;	/* Link for IP protocol level buffer chains 	*/
  struct sock			*sk;			/* Socket we are owned by 			*/
  volatile unsigned long	when;			/* used to compute rtt's			*/
  struct timeval		stamp;			/* Time we arrived				*/
  struct device			*dev;			/* Device we arrived on/are leaving by		*/
  union {
	struct tcphdr	*th;
	struct ethhdr	*eth;
	struct iphdr	*iph;
	struct udphdr	*uh;
	unsigned char	*raw;
	unsigned long	seq;
  } h;
  struct iphdr			*ip_hdr;		/* For IPPROTO_RAW 				*/
  unsigned long 		len;			/* Length of actual data			*/
  unsigned long 		saddr;			/* IP source address				*/
  unsigned long 		daddr;			/* IP target address				*/
  unsigned long			raddr;			/* IP next hop address				*/
  volatile char 		acked,			/* Are we acked ?				*/
				used,			/* Are we in use ?				*/
				free,			/* How to free this buffer			*/
				arp;			/* Has IP/ARP resolution finished		*/
  unsigned char			tries,			/* Times tried					*/
  				lock,			/* Are we locked ?				*/
  				localroute,		/* Local routing asserted for this frame	*/
  				pkt_type;		/* Packet class					*/
#define PACKET_HOST		0			/* To us					*/
#define PACKET_BROADCAST	1			/* To all					*/
#define PACKET_MULTICAST	2			/* To group					*/
#define PACKET_OTHERHOST	3			/* To someone else 				*/
  unsigned short		users;			/* User count - see datagram.c,tcp.c 		*/
  unsigned short		protocol;		/* Packet protocol from driver. 		*/
  unsigned short		truesize;		/* Buffer size 					*/
  unsigned char			*head;			/* Head of buffer 				*/
  unsigned char			*data;			/* Data head pointer				*/
  unsigned char			*tail;			/* Tail pointer					*/
  unsigned char 		*end;			/* End pointer					*/
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

#include <asm/system.h>

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
extern struct sk_buff *		dev_alloc_skb(unsigned int size);
extern void			kfree_skbmem(struct sk_buff *skb);
extern struct sk_buff *		skb_clone(struct sk_buff *skb, int priority);
extern void			skb_device_lock(struct sk_buff *skb);
extern void			skb_device_unlock(struct sk_buff *skb);
extern void			dev_kfree_skb(struct sk_buff *skb, int mode);
extern int			skb_device_locked(struct sk_buff *skb);
extern unsigned char *		skb_put(struct sk_buff *skb, int len);
extern unsigned char *		skb_push(struct sk_buff *skb, int len);
extern int			skb_pull(struct sk_buff *skb, int len);
extern int			skb_headroom(struct sk_buff *skb);
extern int			skb_tailroom(struct sk_buff *skb);
extern void			skb_reserve(struct sk_buff *skb, int len);
extern void 			skb_trim(struct sk_buff *skb, int len);

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
#define IS_SKB(skb)		
#define IS_SKB_HEAD(skb)	

extern __inline__ void skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = (struct sk_buff *)list;
	list->next = (struct sk_buff *)list;
}

/*
 *	Insert an sk_buff at the start of a list.
 */

extern __inline__ void skb_queue_head(struct sk_buff_head *list_,struct sk_buff *newsk)
{
	unsigned long flags;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();
	newsk->next = list->next;
	newsk->prev = list;
	newsk->next->prev = newsk;
	newsk->prev->next = newsk;
	restore_flags(flags);
}

/*
 *	Insert an sk_buff at the end of a list.
 */

extern __inline__ void skb_queue_tail(struct sk_buff_head *list_, struct sk_buff *newsk)
{
	unsigned long flags;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

	newsk->next = list;
	newsk->prev = list->prev;

	newsk->next->prev = newsk;
	newsk->prev->next = newsk;

	restore_flags(flags);
}

/*
 *	Remove an sk_buff from a list. This routine is also interrupt safe
 *	so you can grab read and free buffers as another process adds them.
 *
 * 	Note we now do the ful list 
 */

extern __inline__ struct sk_buff *skb_dequeue(struct sk_buff_head *list_)
{
	long flags;
	struct sk_buff *result;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

	result = list->next;
	if (result == list) 
	{
		restore_flags(flags);
		return NULL;
	}
	else
	{
		result->next->prev = list;
		list->next = result->next;

		result->next = NULL;
		result->prev = NULL;

		restore_flags(flags);

		return result;
	}
}

/*
 *	Insert a packet before another one in a list.
 */

extern __inline__ void skb_insert(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	newsk->next = old;
	newsk->prev = old->prev;
	old->prev = newsk;
	newsk->prev->next = newsk;

	restore_flags(flags);
}

/*
 *	Place a packet after a given packet in a list.
 */

extern __inline__ void skb_append(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	newsk->prev = old;
	newsk->next = old->next;
	newsk->next->prev = newsk;
	old->next = newsk;

	restore_flags(flags);
}

/*
 *	Remove an sk_buff from its list. Works even without knowing the list it
 *	is sitting on, which can be handy at times. It also means that THE LIST
 *	MUST EXIST when you unlink. Thus a list must have its contents unlinked
 *	_FIRST_.
 */

extern __inline__ void skb_unlink(struct sk_buff *skb)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	if(skb->prev && skb->next)
	{
		skb->next->prev = skb->prev;
		skb->prev->next = skb->next;
		skb->next = NULL;
		skb->prev = NULL;
	}
	restore_flags(flags);
}

/*
 *	Add data to an sk_buff
 */
 
extern __inline__ unsigned char *skb_put(struct sk_buff *skb, int len)
{
	unsigned char *tmp=skb->tail;
	skb->tail+=len;
	skb->len+=len;
	if(skb->tail>skb->end)
		panic("skput:over: %p:%d", __builtin_return_address(0),len);
	return tmp;
}

extern __inline__ unsigned char *skb_push(struct sk_buff *skb, int len)
{
	skb->data-=len;
	skb->len+=len;
	if(skb->data<skb->head)
		panic("skpush:under: %p:%d", __builtin_return_address(0),len);
	return skb->data;
}

extern __inline__ int skb_pull(struct sk_buff *skb, int len)
{
	if(len>skb->len)
		len=skb->len;
	skb->data+=len;
	skb->len-=len;
	return len;
}

extern __inline__ int skb_headroom(struct sk_buff *skb)
{
	return skb->data-skb->head;
}

extern __inline__ int skb_tailroom(struct sk_buff *skb)
{
	return skb->end-skb->tail;
}

extern __inline__ void skb_reserve(struct sk_buff *skb, int len)
{
	skb->data+=len;
	skb->tail+=len;
}

extern __inline__ void skb_trim(struct sk_buff *skb, int len)
{
	if(skb->len>len)
	{
		skb->len=len;
		skb->tail=skb->data+len;
	}
}

#endif

extern struct sk_buff *		skb_recv_datagram(struct sock *sk,unsigned flags,int noblock, int *err);
extern int			datagram_select(struct sock *sk, int sel_type, select_table *wait);
extern void			skb_copy_datagram(struct sk_buff *from, int offset, char *to,int size);
extern void			skb_free_datagram(struct sk_buff *skb);

#endif	/* __KERNEL__ */
#endif	/* _LINUX_SKBUFF_H */
