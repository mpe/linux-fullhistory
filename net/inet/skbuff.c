/*
 *	Routines having to do with the 'struct sk_buff' memory handlers.
 *
 *	Authors:	Alan Cox <iiitac@pyr.swan.ac.uk>
 *			Florian La Roche <rzsfl@rz.uni-sb.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
 
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include "ip.h"
#include "protocol.h"
#include <linux/string.h>
#include "route.h"
#include "tcp.h"
#include "udp.h"
#include <linux/skbuff.h>
#include "sock.h"


/*
 *	Resource tracking variables
 */
 
volatile unsigned long net_memory = 0;
volatile unsigned long net_skbcount = 0;


#if CONFIG_SKB_CHECK

/*
 *	Debugging paranoia. Can go later when this crud stack works
 */ 

int skb_check(struct sk_buff *skb, int head, int line, char *file)
{
	if (head) {
		if (skb->magic_debug_cookie != SK_HEAD_SKB) {
			printk("File: %s Line %d, found a bad skb-head\n",
				file,line);
			return -1;
		}
		if (!skb->next || !skb->prev) {
			printk("skb_check: head without next or prev\n");
			return -1;
		}
		if (skb->next->magic_debug_cookie != SK_HEAD_SKB
			&& skb->next->magic_debug_cookie != SK_GOOD_SKB) {
			printk("File: %s Line %d, bad next head-skb member\n",
				file,line);
			return -1;
		}
		if (skb->prev->magic_debug_cookie != SK_HEAD_SKB
			&& skb->prev->magic_debug_cookie != SK_GOOD_SKB) {
			printk("File: %s Line %d, bad prev head-skb member\n",
				file,line);
			return -1;
		}
#if 0
		{
		struct sk_buff *skb2 = skb->next;
		int i = 0;
		while (skb2 != skb && i < 5) {
			if (skb_check(skb2, 0, line, file) < 0) {
				printk("bad queue element in whole queue\n");
				return -1;
			}
			i++;
			skb2 = skb2->next;
		}
		}
#endif
		return 0;
	}
	if (skb->next != NULL && skb->next->magic_debug_cookie != SK_HEAD_SKB
		&& skb->next->magic_debug_cookie != SK_GOOD_SKB) {
		printk("File: %s Line %d, bad next skb member\n",
			file,line);
		return -1;
	}
	if (skb->prev != NULL && skb->prev->magic_debug_cookie != SK_HEAD_SKB
		&& skb->prev->magic_debug_cookie != SK_GOOD_SKB) {
		printk("File: %s Line %d, bad prev skb member\n",
			file,line);
		return -1;
	}


	if(skb->magic_debug_cookie==SK_FREED_SKB)
	{
		printk("File: %s Line %d, found a freed skb lurking in the undergrowth!\n",
			file,line);
		printk("skb=%p, real size=%ld, claimed size=%ld, free=%d\n",
			skb,skb->truesize,skb->mem_len,skb->free);
		return -1;
	}
	if(skb->magic_debug_cookie!=SK_GOOD_SKB)
	{
		printk("File: %s Line %d, passed a non skb!\n", file,line);
		printk("skb=%p, real size=%ld, claimed size=%ld, free=%d\n",
			skb,skb->truesize,skb->mem_len,skb->free);
		return -1;
	}
	if(skb->mem_len!=skb->truesize)
	{
		printk("File: %s Line %d, Dubious size setting!\n",file,line);
		printk("skb=%p, real size=%ld, claimed size=%ld\n",
			skb,skb->truesize,skb->mem_len);
		return -1;
	}
	/* Guess it might be acceptable then */
	return 0;
}
#endif


void skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = (struct sk_buff *)list;
	list->next = (struct sk_buff *)list;
#if CONFIG_SKB_CHECK
	list->magic_debug_cookie = SK_HEAD_SKB;
#endif
}


/*
 *	Insert an sk_buff at the start of a list.
 */
    
void skb_queue_head(struct sk_buff_head *list_,struct sk_buff *newsk)
{
	unsigned long flags;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

#if CONFIG_SKB_CHECK
	IS_SKB(newsk);
	IS_SKB_HEAD(list);
	if (newsk->next || newsk->prev)
		printk("Suspicious queue head: sk_buff on list!\n");
#endif

	newsk->next = list->next;
	newsk->prev = list;

	newsk->next->prev = newsk;
	newsk->prev->next = newsk;
	
	restore_flags(flags);
}

/*
 *	Insert an sk_buff at the end of a list.
 */
 
void skb_queue_tail(struct sk_buff_head *list_, struct sk_buff *newsk)
{
	unsigned long flags;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

#if CONFIG_SKB_CHECK
	if (newsk->next || newsk->prev)
		printk("Suspicious queue tail: sk_buff on list!\n");
	IS_SKB(newsk);
	IS_SKB_HEAD(list);
#endif

	newsk->next = list;
	newsk->prev = list->prev;

	newsk->next->prev = newsk;
	newsk->prev->next = newsk;

	restore_flags(flags);
}

/*
 *	Remove an sk_buff from a list. This routine is also interrupt safe
 *	so you can grab read and free buffers as another process adds them.
 */

struct sk_buff *skb_dequeue(struct sk_buff_head *list_)
{
	long flags;
	struct sk_buff *result;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

	IS_SKB_HEAD(list);
	
	result = list->next;
	if (result == list) {
		restore_flags(flags);
		return NULL;
	}
	
	result->next->prev = list;
	list->next = result->next;

	result->next = NULL;
	result->prev = NULL;

	restore_flags(flags);
	
	return result;
}

/*
 *	Insert a packet before another one in a list.
 */
 
void skb_insert(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

#if CONFIG_SKB_CHECK
	IS_SKB(old);
	IS_SKB(newsk);

	if(!old->next || !old->prev)
		printk("insert before unlisted item!\n");
	if(newsk->next || newsk->prev)
		printk("inserted item is already on a list.\n");
#endif

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
 
void skb_append(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

#if CONFIG_SKB_CHECK
	IS_SKB(old);
	IS_SKB(newsk);

	if(!old->next || !old->prev)
		printk("append before unlisted item!\n");
	if(newsk->next || newsk->prev)
		printk("append item is already on a list.\n");
#endif

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
 
void skb_unlink(struct sk_buff *skb)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	IS_SKB(skb);

	if(skb->prev && skb->next)
	{
		skb->next->prev = skb->prev;
		skb->prev->next = skb->next;
		skb->next = NULL;
		skb->prev = NULL;
	}
#ifdef PARANOID_BUGHUNT_MODE	/* This is legal but we sometimes want to watch it */
	else
		printk("skb_unlink: not a linked element\n");
#endif		
	restore_flags(flags);
}

/*
 *	Get a clone of an sk_buff. This is the safe way to peek at
 *	a socket queue without accidents. Its a bit long but most
 *	of it acutally ends up as tiny bits of inline assembler
 *	anyway. Only the memcpy of upto 4K with ints off is not
 *	as nice as I'd like.
 */
 
struct sk_buff *skb_peek_copy(struct sk_buff_head *list_)
{
	struct sk_buff *list = (struct sk_buff *)list_;
	struct sk_buff *orig,*newsk;
	unsigned long flags;
	unsigned int len;
	/* Now for some games to avoid races */

	IS_SKB_HEAD(list);
	do
	{
		save_flags(flags);
		cli();
		orig = list->next;
		if (orig == list) {
			restore_flags(flags);
			return NULL;
		}
		IS_SKB(orig);
		len = orig->truesize;
		restore_flags(flags);

		newsk = alloc_skb(len,GFP_KERNEL);	/* May sleep */

		if (newsk == NULL)		/* Oh dear... not to worry */
			return NULL;
	
		save_flags(flags);
		cli();
		if (list->next != orig)	/* List changed go around another time */
		{
			restore_flags(flags);
			newsk->sk = NULL;
			newsk->free = 1;
			newsk->mem_addr = newsk;
			newsk->mem_len = len;
			kfree_skb(newsk, FREE_WRITE);
			continue;
		}
		
		IS_SKB(orig);
		IS_SKB(newsk);
		memcpy(newsk,orig,len);
		newsk->next = NULL;
		newsk->prev = NULL;
		newsk->mem_addr = newsk;
		newsk->h.raw += ((char *)newsk - (char *)orig);
		newsk->link3 = NULL;
		newsk->sk = NULL;
		newsk->free = 1;
	}
	while(0);
	
	restore_flags(flags);
	return newsk;
}	

/*
 *	Free an sk_buff. This still knows about things it should
 *	not need to like protocols and sockets.
 */

void kfree_skb(struct sk_buff *skb, int rw)
{
	if (skb == NULL) 
	{
		printk("kfree_skb: skb = NULL\n");
		return;
  	}
	IS_SKB(skb);
	if (skb->lock) 
	{
		skb->free = 1;    /* Free when unlocked */
		return;
  	}
  	if (skb->free == 2)
		printk("Warning: kfree_skb passed an skb that nobody set the free flag on!\n");
	if (skb->next)
	 	printk("Warning: kfree_skb passed an skb still on a list.\n");
	if (skb->sk) 
	{
	        if(skb->sk->prot!=NULL)
		{
			if (rw)
		     		skb->sk->prot->rfree(skb->sk, skb->mem_addr, skb->mem_len);
		     	else
		     		skb->sk->prot->wfree(skb->sk, skb->mem_addr, skb->mem_len);

		}
		else
		{
			/* Non INET - default wmalloc/rmalloc handler */
			if (rw)
				skb->sk->rmem_alloc-=skb->mem_len;
			else
				skb->sk->wmem_alloc-=skb->mem_len;
			if(!skb->sk->dead)
				wake_up_interruptible(skb->sk->sleep);
			kfree_skbmem(skb->mem_addr,skb->mem_len);
		}
	} 
	else 
		kfree_skbmem(skb->mem_addr, skb->mem_len);
}

/*
 *	Allocate a new skbuff. We do this ourselves so we can fill in a few 'private'
 *	fields and also do memory statistics to find all the [BEEP] leaks.
 */
 
 struct sk_buff *alloc_skb(unsigned int size,int priority)
 {
 	struct sk_buff *skb;
 		
 	if (intr_count && priority!=GFP_ATOMIC) {
		static int count = 0;
		if (++count < 5) {
			printk("alloc_skb called nonatomically from interrupt %08lx\n",
				((unsigned long *)&size)[-1]);
			priority = GFP_ATOMIC;
		}
 	}
 	
 	size+=sizeof(struct sk_buff);
 	skb=(struct sk_buff *)kmalloc(size,priority);
 	if (skb == NULL)
 		return NULL;

 	skb->free = 2;	/* Invalid so we pick up forgetful users */
	skb->lock = 0;
 	skb->truesize = size;
 	skb->mem_len = size;
 	skb->mem_addr = skb;
 	skb->fraglist = NULL;
	skb->prev = skb->next = NULL;
	skb->link3 = NULL;
	skb->sk = NULL;
	skb->stamp.tv_sec=0;	/* No idea about time */
 	net_memory += size;
 	net_skbcount++;
#if CONFIG_SKB_CHECK
	skb->magic_debug_cookie = SK_GOOD_SKB;
#endif
 	skb->users = 0;
 	return skb;
}

/*
 *	Free an skbuff by memory
 */ 	

void kfree_skbmem(void *mem,unsigned size)
{
#if CONFIG_SKB_CHECK
	struct sk_buff *x = mem;
	IS_SKB(x);
	if(x->magic_debug_cookie == SK_GOOD_SKB)
	{
		x->magic_debug_cookie = SK_FREED_SKB;
		kfree_s(mem,size);
		net_skbcount--;
		net_memory -= size;
	}
	else
		printk("kfree_skbmem: bad magic cookie\n");
#else
	kfree_s(mem, size);
#endif
}

/*
 *	Duplicate an sk_buff. The new one is not owned by a socket or locked
 *	and will be freed on deletion.
 */

struct sk_buff *skb_clone(struct sk_buff *skb, int priority)
{
	struct sk_buff *n;
	unsigned long offset;
	
	n=alloc_skb(skb->mem_len-sizeof(struct sk_buff),priority);
	if(n==NULL)
		return NULL;
		
	offset=((char *)n)-((char *)skb);
		
	memcpy(n->data,skb->data,skb->mem_len-sizeof(struct sk_buff));
	n->len=skb->len;
	n->link3=NULL;
	n->sk=NULL;
	n->when=skb->when;
	n->dev=skb->dev;
	n->h.raw=skb->h.raw+offset;
	n->ip_hdr=(struct iphdr *)(((char *)skb->ip_hdr)+offset);
	n->fraglen=skb->fraglen;
	n->fraglist=skb->fraglist;
	n->saddr=skb->saddr;
	n->daddr=skb->daddr;
	n->raddr=skb->raddr;
	n->acked=skb->acked;
	n->used=skb->used;
	n->free=1;
	n->arp=skb->arp;
	n->tries=0;
	n->lock=0;
	n->users=0;
	return n;
}
	
	
/*
 *     Skbuff device locking
 */

void skb_kept_by_device(struct sk_buff *skb)
{
	skb->lock++;
}

void skb_device_release(struct sk_buff *skb, int mode)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	if (!--skb->lock && skb->free == 1)
		kfree_skb(skb,mode);
	restore_flags(flags);
}

int skb_device_locked(struct sk_buff *skb)
{
	return skb->lock? 1 : 0;
}

