/*
 *	Routines having to do with the 'struct sk_buff' memory handlers.
 *
 *	Authors:	Alan Cox <iiitac@pyr.swan.ac.uk>
 *			Florian La Roche <rzsfl@rz.uni-sb.de>
 *
 *	Fixes:	
 *		Alan Cox	:	Fixed the worst of the load balancer bugs.
 *		Dave Platt	:	Interrupt stacking fix.
 *	Richard Kooijman	:	Timestamp fixes.
 *		Alan Cox	:	Changed buffer format.
 *		Alan Cox	:	destructor hook for AF_UNIX etc.
 *		Linus Torvalds	:	Better skb_clone.
 *		Alan Cox	:	Added skb_copy.
 *		Alan Cox	:	Added all the changed routines Linus
 *					only put in the headers
 *		Ray VanTassle	:	Fixed --skb->lock in free
 *		Alan Cox	:	skb_copy copy arp field
 *
 *	NOTE:
 *		The __skb_ routines should be called with interrupts 
 *	disabled, or you better be *real* sure that the operation is atomic 
 *	with respect to whatever list is being frobbed (e.g. via lock_sock()
 *	or via disabling bottom half handlers, etc).
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

/*
 *	The functions in this file will not compile correctly with gcc 2.4.x
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/malloc.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/skbuff.h>

#include <net/ip.h>
#include <net/protocol.h>
#include <net/dst.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/sock.h>

#include <asm/uaccess.h>
#include <asm/system.h>

/*
 *	Resource tracking variables
 */

static atomic_t net_skbcount = ATOMIC_INIT;
static atomic_t net_allocs = ATOMIC_INIT;
static atomic_t net_fails  = ATOMIC_INIT;

extern atomic_t ip_frag_mem;

/*
 *	Strings we don't want inline's duplicating
 */
 
char *skb_push_errstr="skpush:under: %p:%d";
char *skb_put_errstr ="skput:over: %p:%d";

void show_net_buffers(void)
{
	printk(KERN_INFO "Networking buffers in use          : %u\n",
	       atomic_read(&net_skbcount));
	printk(KERN_INFO "Total network buffer allocations   : %u\n",
	       atomic_read(&net_allocs));
	printk(KERN_INFO "Total failed network buffer allocs : %u\n",
	       atomic_read(&net_fails));
#ifdef CONFIG_INET
	printk(KERN_INFO "IP fragment buffer size            : %u\n",
	       atomic_read(&ip_frag_mem));
#endif	
}

#if CONFIG_SKB_CHECK

/*
 *	Debugging paranoia. Used for debugging network stacks.
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
		printk("skb=%p, real size=%d, free=%d\n",
			skb,skb->truesize,skb->free);
		return -1;
	}
	if(skb->magic_debug_cookie!=SK_GOOD_SKB)
	{
		printk("File: %s Line %d, passed a non skb!\n", file,line);
		printk("skb=%p, real size=%d, free=%d\n",
			skb,skb->truesize,skb->free);
		return -1;
	}
	if(skb->head>skb->data)
	{
		printk("File: %s Line %d, head > data !\n", file,line);
		printk("skb=%p, head=%p, data=%p\n",
			skb,skb->head,skb->data);
		return -1;
	}
	if(skb->tail>skb->end)
	{
		printk("File: %s Line %d, tail > end!\n", file,line);
		printk("skb=%p, tail=%p, end=%p\n",
			skb,skb->tail,skb->end);
		return -1;
	}
	if(skb->data>skb->tail)
	{
		printk("File: %s Line %d, data > tail!\n", file,line);
		printk("skb=%p, data=%p, tail=%p\n",
			skb,skb->data,skb->tail);
		return -1;
	}
	if(skb->tail-skb->data!=skb->len)
	{
		printk("File: %s Line %d, wrong length\n", file,line);
		printk("skb=%p, data=%p, end=%p len=%ld\n",
			skb,skb->data,skb->end,skb->len);
		return -1;
	}
	if((unsigned long) skb->end > (unsigned long) skb)
	{
		printk("File: %s Line %d, control overrun\n", file,line);
		printk("skb=%p, end=%p\n",
			skb,skb->end);
		return -1;
	}

	/* Guess it might be acceptable then */
	return 0;
}
#endif


#if CONFIG_SKB_CHECK
void skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = (struct sk_buff *)list;
	list->next = (struct sk_buff *)list;
	list->qlen = 0;
	list->magic_debug_cookie = SK_HEAD_SKB;
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

	IS_SKB(newsk);
	IS_SKB_HEAD(list);
	if (newsk->next || newsk->prev)
		printk("Suspicious queue head: sk_buff on list!\n");

	newsk->next = list->next;
	newsk->prev = list;

	newsk->next->prev = newsk;
	newsk->prev->next = newsk;
	newsk->list = list_;
	list_->qlen++;

	restore_flags(flags);
}

void __skb_queue_head(struct sk_buff_head *list_,struct sk_buff *newsk)
{
	struct sk_buff *list = (struct sk_buff *)list_;


	IS_SKB(newsk);
	IS_SKB_HEAD(list);
	if (newsk->next || newsk->prev)
		printk("Suspicious queue head: sk_buff on list!\n");

	newsk->next = list->next;
	newsk->prev = list;

	newsk->next->prev = newsk;
	newsk->prev->next = newsk;
	newsk->list = list_;
	list_->qlen++;

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

	if (newsk->next || newsk->prev)
		printk("Suspicious queue tail: sk_buff on list!\n");
	IS_SKB(newsk);
	IS_SKB_HEAD(list);

	newsk->next = list;
	newsk->prev = list->prev;

	newsk->next->prev = newsk;
	newsk->prev->next = newsk;
	
	newsk->list = list_;
	list_->qlen++;

	restore_flags(flags);
}

void __skb_queue_tail(struct sk_buff_head *list_, struct sk_buff *newsk)
{
	struct sk_buff *list = (struct sk_buff *)list_;

	if (newsk->next || newsk->prev)
		printk("Suspicious queue tail: sk_buff on list!\n");
	IS_SKB(newsk);
	IS_SKB_HEAD(list);

	newsk->next = list;
	newsk->prev = list->prev;

	newsk->next->prev = newsk;
	newsk->prev->next = newsk;
	
	newsk->list = list_;
	list_->qlen++;
}

/*
 *	Remove an sk_buff from a list. This routine is also interrupt safe
 *	so you can grab read and free buffers as another process adds them.
 */

struct sk_buff *skb_dequeue(struct sk_buff_head *list_)
{
	unsigned long flags;
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
	list_->qlen--;
	result->list = NULL;
	
	restore_flags(flags);

	IS_SKB(result);
	return result;
}

struct sk_buff *__skb_dequeue(struct sk_buff_head *list_)
{
	struct sk_buff *result;
	struct sk_buff *list = (struct sk_buff *)list_;

	IS_SKB_HEAD(list);

	result = list->next;
	if (result == list) {
		return NULL;
	}

	result->next->prev = list;
	list->next = result->next;

	result->next = NULL;
	result->prev = NULL;
	list_->qlen--;
	result->list = NULL;
	
	IS_SKB(result);
	return result;
}

/*
 *	Insert a packet before another one in a list.
 */
void skb_insert(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	IS_SKB(old);
	IS_SKB(newsk);

	if(!old->next || !old->prev)
		printk("insert before unlisted item!\n");
	if(newsk->next || newsk->prev)
		printk("inserted item is already on a list.\n");

	save_flags(flags);
	cli();
	newsk->next = old;
	newsk->prev = old->prev;
	old->prev = newsk;
	newsk->prev->next = newsk;
	newsk->list = old->list;
	newsk->list->qlen++;

	restore_flags(flags);
}

/*
 *	Insert a packet before another one in a list.
 */

void __skb_insert(struct sk_buff *newsk,
	struct sk_buff * prev, struct sk_buff *next,
	struct sk_buff_head * list)
{
	IS_SKB(prev);
	IS_SKB(newsk);
	IS_SKB(next);

	if(!prev->next || !prev->prev)
		printk("insert after unlisted item!\n");
	if(!next->next || !next->prev)
		printk("insert before unlisted item!\n");
	if(newsk->next || newsk->prev)
		printk("inserted item is already on a list.\n");

	newsk->next = next;
	newsk->prev = prev;
	next->prev = newsk;
	prev->next = newsk;
	newsk->list = list;
	list->qlen++;

}

/*
 *	Place a packet after a given packet in a list.
 */
void skb_append(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	IS_SKB(old);
	IS_SKB(newsk);

	if(!old->next || !old->prev)
		printk("append before unlisted item!\n");
	if(newsk->next || newsk->prev)
		printk("append item is already on a list.\n");

	save_flags(flags);
	cli();

	newsk->prev = old;
	newsk->next = old->next;
	newsk->next->prev = newsk;
	old->next = newsk;
	newsk->list = old->list;
	newsk->list->qlen++;

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

	if(skb->list)
	{
		skb->list->qlen--;
		skb->next->prev = skb->prev;
		skb->prev->next = skb->next;
		skb->next = NULL;
		skb->prev = NULL;
		skb->list = NULL;
	}
#ifdef PARANOID_BUGHUNT_MODE	/* This is legal but we sometimes want to watch it */
	else
		printk("skb_unlink: not a linked element\n");
#endif
	restore_flags(flags);
}

void __skb_unlink(struct sk_buff *skb)
{
	IS_SKB(skb);

	if(skb->list)
	{
		skb->list->qlen--;
		skb->next->prev = skb->prev;
		skb->prev->next = skb->next;
		skb->next = NULL;
		skb->prev = NULL;
		skb->list = NULL;
	}
#ifdef PARANOID_BUGHUNT_MODE	/* This is legal but we sometimes want to watch it */
	else
		printk("skb_unlink: not a linked element\n");
#endif
}

/*
 *	Add data to an sk_buff
 */
 
unsigned char *skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp=skb->tail;
	IS_SKB(skb);
	skb->tail+=len;
	skb->len+=len;
	IS_SKB(skb);
	if(skb->tail>skb->end)
		panic("skput:over: %p:%d", __builtin_return_address(0),len);
	return tmp;
}

unsigned char *skb_push(struct sk_buff *skb, unsigned int len)
{
	IS_SKB(skb);
	skb->data-=len;
	skb->len+=len;
	IS_SKB(skb);
	if(skb->data<skb->head)
		panic("skpush:under: %p:%d", __builtin_return_address(0),len);
	return skb->data;
}

unsigned char * skb_pull(struct sk_buff *skb, unsigned int len)
{
	IS_SKB(skb);
	if(len>skb->len)
		return 0;
	skb->data+=len;
	skb->len-=len;
	return skb->data;
}

int skb_headroom(struct sk_buff *skb)
{
	IS_SKB(skb);
	return skb->data-skb->head;
}

int skb_tailroom(struct sk_buff *skb)
{
	IS_SKB(skb);
	return skb->end-skb->tail;
}

void skb_reserve(struct sk_buff *skb, unsigned int len)
{
	IS_SKB(skb);
	skb->data+=len;
	skb->tail+=len;
	if(skb->tail>skb->end)
		panic("sk_res: over");
	if(skb->data<skb->head)
		panic("sk_res: under");
	IS_SKB(skb);
}

void skb_trim(struct sk_buff *skb, unsigned int len)
{
	IS_SKB(skb);
	if(skb->len>len)
	{
		skb->len=len;
		skb->tail=skb->data+len;
	}
}



#endif

/**************************************************************************

      Stuff below this point isn't debugging duplicates of the inlines
      used for buffer handling
      
***************************************************************************/

/*
 *	Free an sk_buff. Release anything attached to the buffer.
 */

void __kfree_skb(struct sk_buff *skb)
{
#if CONFIG_SKB_CHECK
	if (skb == NULL)
	{
		printk(KERN_CRIT "kfree_skb: skb = NULL (from %p)\n",
 			__builtin_return_address(0));
		return;
  	}
	IS_SKB(skb);
#endif
	if (skb->list)
	 	printk(KERN_WARNING "Warning: kfree_skb passed an skb still on a list (from %p).\n",
			__builtin_return_address(0));

	dst_release(skb->dst);

	if(skb->destructor)
		skb->destructor(skb);
	kfree_skbmem(skb);
}

/*
 *	Allocate a new skbuff. We do this ourselves so we can fill in a few 'private'
 *	fields and also do memory statistics to find all the [BEEP] leaks.
 *
 *	Note: For now we put the header after the data to get better cache
 *	usage. Once we have a good cache aware kmalloc this will cease
 *	to be a good idea.
 */

struct sk_buff *alloc_skb(unsigned int size,int priority)
{
	struct sk_buff *skb;
	int len;
	unsigned char *bptr;

	if (in_interrupt() && priority!=GFP_ATOMIC) 
	{
		static int count = 0;
		if (++count < 5) {
			printk(KERN_ERR "alloc_skb called nonatomically from interrupt %p\n",
				__builtin_return_address(0));
			priority = GFP_ATOMIC;
		}
	}

	/*
	 *	FIXME: We could do with an architecture dependant
	 *	'alignment mask'.
	 */
	 
	size=(size+15)&~15;		/* Allow for alignments. Make a multiple of 16 bytes */
	len = size;
	
	size+=sizeof(struct sk_buff);	/* And stick the control itself on the end */
	
	/*
	 *	Allocate some space
	 */
	 
	bptr=(unsigned char *)kmalloc(size,priority);
	if (bptr == NULL)
	{
		atomic_inc(&net_fails);
		return NULL;
	}
#ifdef PARANOID_BUGHUNT_MODE
	if(skb->magic_debug_cookie == SK_GOOD_SKB)
		printk("Kernel kmalloc handed us an existing skb (%p)\n",skb);
#endif
	/*
	 *	Now we play a little game with the caches. Linux kmalloc is
	 *	a bit cache dumb, in fact its just about maximally non 
	 *	optimal for typical kernel buffers. We actually run faster
	 *	by doing the following. Which is to deliberately put the
	 *	skb at the _end_ not the start of the memory block.
	 */
	atomic_inc(&net_allocs);
	
	skb=(struct sk_buff *)(bptr+size)-1;

	atomic_set(&skb->count, 1);		/* only one reference to this */
	skb->data_skb = skb;			/* and we're our own data skb */

	skb->pkt_type = PACKET_HOST;	/* Default type */
	skb->pkt_bridged = 0;		/* Not bridged */
	skb->prev = skb->next = NULL;
	skb->list = NULL;
	skb->sk = NULL;
	skb->truesize=size;
	skb->stamp.tv_sec=0;	/* No idea about time */
	skb->ip_summed = 0;
	skb->security = 0;	/* By default packets are insecure */
	skb->dst = NULL;
	skb->destructor = NULL;
	memset(skb->cb, 0, sizeof(skb->cb));
	skb->priority = SOPRI_NORMAL;
	atomic_inc(&net_skbcount);
#if CONFIG_SKB_CHECK
	skb->magic_debug_cookie = SK_GOOD_SKB;
#endif
	atomic_set(&skb->users, 1);
	/* Load the data pointers */
	skb->head=bptr;
	skb->data=bptr;
	skb->tail=bptr;
	skb->end=bptr+len;
	skb->len=0;
	skb->inclone = 0;
	return skb;
}

/*
 *	Free an skbuff by memory
 */

extern inline void __kfree_skbmem(struct sk_buff *skb)
{
	/* don't do anything if somebody still uses us */
	if (atomic_dec_and_test(&skb->count)) {
		kfree(skb->head);
		atomic_dec(&net_skbcount);
	}
}

void kfree_skbmem(struct sk_buff *skb)
{
	void * addr = skb->head;

	/* don't do anything if somebody still uses us */
	if (atomic_dec_and_test(&skb->count)) {

		int free_head;

		free_head = (skb->inclone != SKB_CLONE_INLINE);

		/* free the skb that contains the actual data if we've clone()'d */
		if (skb->data_skb != skb) {
			addr = skb;
			__kfree_skbmem(skb->data_skb);
		}
		if (free_head)
			kfree(addr);
		atomic_dec(&net_skbcount);
	}
}

/*
 *	Duplicate an sk_buff. The new one is not owned by a socket.
 */

struct sk_buff *skb_clone(struct sk_buff *skb, int priority)
{
	struct sk_buff *n;
	int inbuff = 0;
	
	IS_SKB(skb);
	if (!skb->inclone && skb_tailroom(skb) >= sizeof(struct sk_buff))
	{
		n = ((struct sk_buff *) skb->end) - 1;
		skb->end -= sizeof(struct sk_buff);
		skb->inclone = SKB_CLONE_ORIG;
		inbuff = SKB_CLONE_INLINE;
	}
	else
	{
		n = kmalloc(sizeof(*n), priority);
		if (!n)
			return NULL;
	}
	memcpy(n, skb, sizeof(*n));
	atomic_set(&n->count, 1);
	skb = skb->data_skb;
	atomic_inc(&skb->count);
	atomic_inc(&net_allocs);
	atomic_inc(&net_skbcount);
	dst_clone(n->dst);
	n->data_skb = skb;
	n->next = n->prev = NULL;
	n->list = NULL;
	n->sk = NULL;
	n->tries = 0;
	atomic_set(&n->users, 1);
	n->inclone = inbuff;
	n->destructor = NULL;
	return n;
}

/*
 *	This is slower, and copies the whole data area 
 */
 
struct sk_buff *skb_copy(struct sk_buff *skb, int priority)
{
	struct sk_buff *n;
	unsigned long offset;

	/*
	 *	Allocate the copy buffer
	 */
	 
	IS_SKB(skb);
	
	n=alloc_skb(skb->end - skb->head, priority);
	if(n==NULL)
		return NULL;

	/*
	 *	Shift between the two data areas in bytes
	 */
	 
	offset=n->head-skb->head;

	/* Set the data pointer */
	skb_reserve(n,skb->data-skb->head);
	/* Set the tail pointer and length */
	skb_put(n,skb->len);
	/* Copy the bytes */
	memcpy(n->head,skb->head,skb->end-skb->head);
	n->list=NULL;
	n->sk=NULL;
	n->when=skb->when;
	n->dev=skb->dev;
	n->priority=skb->priority;
	n->protocol=skb->protocol;
	n->dst=dst_clone(skb->dst);
	n->h.raw=skb->h.raw+offset;
	n->nh.raw=skb->nh.raw+offset;
	n->mac.raw=skb->mac.raw+offset;
	n->seq=skb->seq;
	n->end_seq=skb->end_seq;
	n->ack_seq=skb->ack_seq;
	n->acked=skb->acked;
	memcpy(n->cb, skb->cb, sizeof(skb->cb));
	n->used=skb->used;
	n->arp=skb->arp;
	n->tries=0;
	atomic_set(&n->users, 1);
	n->pkt_type=skb->pkt_type;
	n->stamp=skb->stamp;
	n->destructor = NULL;
	n->security=skb->security;
	IS_SKB(n);
	return n;
}

struct sk_buff *skb_realloc_headroom(struct sk_buff *skb, int newheadroom)
{
	struct sk_buff *n;
	unsigned long offset;
	int headroom = skb_headroom(skb);

	/*
	 *	Allocate the copy buffer
	 */
 	 
	IS_SKB(skb);
	
	n=alloc_skb(skb->truesize+newheadroom-headroom-sizeof(struct sk_buff), GFP_ATOMIC);
	if(n==NULL)
		return NULL;

	skb_reserve(n,newheadroom);

	/*
	 *	Shift between the two data areas in bytes
	 */
	 
	offset=n->data-skb->data;

	/* Set the tail pointer and length */
	skb_put(n,skb->len);
	/* Copy the bytes */
	memcpy(n->data,skb->data,skb->len);
	n->list=NULL;
	n->sk=NULL;
	n->when=skb->when;
	n->priority=skb->priority;
	n->protocol=skb->protocol;
	n->dev=skb->dev;
	n->dst=dst_clone(skb->dst);
	n->h.raw=skb->h.raw+offset;
	n->nh.raw=skb->nh.raw+offset;
	n->mac.raw=skb->mac.raw+offset;
	memcpy(n->cb, skb->cb, sizeof(skb->cb));
	n->seq=skb->seq;
 	n->end_seq=skb->end_seq;
	n->ack_seq=skb->ack_seq;
	n->acked=skb->acked;
	n->used=skb->used;
	n->arp=skb->arp;
	n->tries=0;
	atomic_set(&n->users, 1);
	n->pkt_type=skb->pkt_type;
	n->stamp=skb->stamp;
	n->destructor = NULL;
	n->security=skb->security;

	IS_SKB(n);
	return n;
}
  
struct sk_buff *dev_alloc_skb(unsigned int length)
{
	struct sk_buff *skb;

	skb = alloc_skb(length+16, GFP_ATOMIC);
	if (skb)
		skb_reserve(skb,16);
	return skb;
}
