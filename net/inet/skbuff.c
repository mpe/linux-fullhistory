/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		A saner implementation of the skbuff stuff scattered everywhere
 *		in the old NET2D code.
 *
 *	Authors:	Alan Cox <iiitac@pyr.swan.ac.uk>
 *
 *	Fixes:
 *		Alan Cox	:	Tracks memory and number of buffers for kernel memory report
 *					and memory leak hunting.
 */
 
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include "inet.h"
#include "dev.h"
#include "ip.h"
#include "protocol.h"
#include "arp.h"
#include "route.h"
#include "tcp.h"
#include "udp.h"
#include "skbuff.h"
#include "sock.h"


/* Socket buffer operations. Ideally much of this list swap stuff ought to be using
   exch instructions on the 386, and CAS/CAS2 on a 68K. This is the boring generic
   slow C version. No doubt when Linus sees this comment he'll do horrible things
   to this code 8-)
*/

/*
 *	Resource tracking variables
 */
 
volatile unsigned long net_memory=0;
volatile unsigned long net_skbcount=0;

/*
 *	Debugging paranoia. Can go later when this crud stack works
 */ 
 


void skb_check(struct sk_buff *skb, int line, char *file)
{
	if(skb->magic_debug_cookie==SK_FREED_SKB)
	{
		printk("File: %s Line %d, found a freed skb lurking in the undergrowth!\n",
			file,line);
		printk("skb=%p, real size=%ld, claimed size=%ld, magic=%ld, list=%p, free=%d\n",
			skb,skb->truesize,skb->mem_len,skb->magic,skb->list,skb->free);
	}
	if(skb->magic_debug_cookie!=SK_GOOD_SKB)
	{
		printk("File: %s Line %d, passed a non skb!\n", file,line);
		printk("skb=%p, real size=%ld, claimed size=%ld, magic=%ld, list=%p, free=%d\n",
			skb,skb->truesize,skb->mem_len,skb->magic,skb->list,skb->free);
	}
	if(skb->mem_len!=skb->truesize)
	{
		printk("File: %s Line %d, Dubious size setting!\n",file,line);
		printk("skb=%p, real size=%ld, claimed size=%ld, magic=%ld, list=%p\n",
			skb,skb->truesize,skb->mem_len,skb->magic,skb->list);
	}
	/* Guess it might be acceptable then */
}

/*
 *	Insert an sk_buff at the start of a list.
 */
    
void skb_queue_head(struct sk_buff *volatile* list,struct sk_buff *new)
{
	unsigned long flags;
	
	IS_SKB(new);	
	if(new->list)
		printk("Suspicious queue head: sk_buff on list!\n");
	save_flags(flags);
	cli();
	new->list=list;
	
	new->next=*list;
	
	if(*list)
		new->prev=(*list)->prev;
	else
		new->prev=new;
	new->prev->next=new;
	new->next->prev=new;
	IS_SKB(new->prev);
	IS_SKB(new->next);
	*list=new;
	restore_flags(flags);
}

/*
 *	Insert an sk_buff at the end of a list.
 */
 
void skb_queue_tail(struct sk_buff *volatile* list, struct sk_buff *new)
{
	unsigned long flags;
	
	if(new->list)
		printk("Suspicious queue tail: sk_buff on list!\n");
	
	IS_SKB(new);
	save_flags(flags);
	cli();

	new->list=list;
	if(*list)
	{
		(*list)->prev->next=new;
		(*list)->prev=new;
		new->next=*list;
		new->prev=(*list)->prev;
	}
	else
	{
		new->next=new;
		new->prev=new;
		*list=new;
	}
	IS_SKB(new->prev);
	IS_SKB(new->next);		
	restore_flags(flags);

}

/*
 *	Remove an sk_buff from a list. This routine is also interrupt safe
 *	so you can grab read and free buffers as another process adds them.
 */
 
struct sk_buff *skb_dequeue(struct sk_buff *volatile* list)
{
	long flags;
	struct sk_buff *result;
	
	save_flags(flags);
	cli();
	
	if(*list==NULL)
	{
		restore_flags(flags);
		return(NULL);
	}
	
	result=*list;
	if(result->next==result)
		*list=NULL;
	else
	{
		result->next->prev=result->prev;
		result->prev->next=result->next;
		*list=result->next;
	}

	IS_SKB(result);
	restore_flags(flags);
	
	if(result->list!=list)
		printk("Dequeued packet has invalid list pointer\n");

	result->list=0;
	result->next=0;
	result->prev=0;
	return(result);
}

/*
 *	Insert a packet before another one in a list.
 */
 
void skb_insert(struct sk_buff *old, struct sk_buff *new)
{
	unsigned long flags;

	IS_SKB(old);
	IS_SKB(new);
		
	if(!old->list)
		printk("insert before unlisted item!\n");
	if(new->list)
		printk("inserted item is already on a list.\n");
		
	save_flags(flags);
	cli();
	new->list=old->list;
	new->next=old;
	new->prev=old->prev;
	new->next->prev=new;
	new->prev->next=new;
	
	restore_flags(flags);
}

/*
 *	Place a packet after a given packet in a list.
 */
 
void skb_append(struct sk_buff *old, struct sk_buff *new)
{
	unsigned long flags;
	
	IS_SKB(old);
	IS_SKB(new);

	if(!old->list)
		printk("append before unlisted item!\n");
	if(new->list)
		printk("append item is already on a list.\n");
		
	save_flags(flags);
	cli();
	new->list=old->list;
	new->prev=old;
	new->next=old->next;
	new->next->prev=new;
	new->prev->next=new;
	
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
		skb->next->prev=skb->prev;
		skb->prev->next=skb->next;
		if(*skb->list==skb)
		{
			if(skb->next==skb)
				*skb->list=NULL;
			else
				*skb->list=skb->next;
		}
		skb->next=0;
		skb->prev=0;
		skb->list=0;
	}
	restore_flags(flags);
}

/*
 *	An skbuff list has had its head reassigned. Move all the list
 *	pointers. Must be called with ints off during the whole head
 *	shifting
 */

void skb_new_list_head(struct sk_buff *volatile* list)
{
	struct sk_buff *skb=skb_peek(list);
	if(skb!=NULL)
	{
		do
		{
			IS_SKB(skb);
			skb->list=list;
			skb=skb->next;
		}
		while(skb!=*list);
	}
}
			
/*
 *	Peek an sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. For an interrupt
 *	type system cli() peek the buffer copy the data and sti();
 */

struct sk_buff *skb_peek(struct sk_buff *volatile* list)
{
	return *list;
}

/*
 *	Get a clone of an sk_buff. This is the safe way to peek at
 *	a socket queue without accidents. Its a bit long but most
 *	of it acutally ends up as tiny bits of inline assembler
 *	anyway. Only the memcpy of upto 4K with ints off is not
 *	as nice as I'd like.
 */
 
struct sk_buff *skb_peek_copy(struct sk_buff *volatile* list)
{
	struct sk_buff *orig,*new;
	unsigned long flags;
	unsigned int len;
	/* Now for some games to avoid races */
	
	do
	{
		save_flags(flags);
		cli();
		orig=skb_peek(list);
		if(orig==NULL)
		{
			restore_flags(flags);
			return NULL;
		}
		IS_SKB(orig);
		len=orig->truesize;
		restore_flags(flags);

		new=alloc_skb(len,GFP_KERNEL);	/* May sleep */

		if(new==NULL)		/* Oh dear... not to worry */
			return NULL;
	
		save_flags(flags);
		cli();
		if(skb_peek(list)!=orig)	/* List changed go around another time */
		{
			restore_flags(list);
			new->sk=NULL;
			new->free=1;
			new->mem_addr=new;
			new->mem_len=len;
			kfree_skb(new, FREE_WRITE);
			continue;
		}
		
		IS_SKB(orig);
		IS_SKB(new);
		memcpy(new,orig,len);
		new->list=NULL;
		new->magic=0;
		new->next=NULL;
		new->prev=NULL;
		new->mem_addr=new;
		new->h.raw+=((char *)new-(char *)orig);
		new->link3=NULL;
		new->sk=NULL;
		new->free=1;
	}
	while(0);
	
	restore_flags(flags);
	return(new);
}	
	
/*
 *	Free an sk_buff. This still knows about things it should
 *	not need to like protocols and sockets.
 */

void kfree_skb(struct sk_buff *skb, int rw)
{
  if (skb == NULL) {
	printk("kfree_skb: skb = NULL\n");
	return;
  }
  IS_SKB(skb);
  if(skb->free == 2)
  	printk("Warning: kfree_skb passed an skb that nobody set the free flag on!\n");
  if(skb->list)
  	printk("Warning: kfree_skb passed an skb still on a list.\n");
  skb->magic = 0;
  if (skb->sk) {
	if (rw) {
	     skb->sk->prot->rfree(skb->sk, skb->mem_addr, skb->mem_len);
	} else {
	     skb->sk->prot->wfree(skb->sk, skb->mem_addr, skb->mem_len);
	}
  } else {
	kfree_skbmem(skb->mem_addr, skb->mem_len);
  }
}

/*
 *	Allocate a new skbuff. We do this ourselves so we can fill in a few 'private'
 *	fields and also do memory statistics to find all the [BEEP] leaks.
 */
 
 struct sk_buff *alloc_skb(unsigned int size,int priority)
 {
 	struct sk_buff *skb=(struct sk_buff *)kmalloc(size,priority);
 	if(skb==NULL)
 		return NULL;
 	skb->free= 2;	/* Invalid so we pick up forgetful users */
 	skb->list= 0;	/* Not on a list */
 	skb->truesize=size;
 	skb->mem_len=size;
 	skb->mem_addr=skb;
 	net_memory+=size;
 	net_skbcount++;
 	skb->magic_debug_cookie=SK_GOOD_SKB;
 	return skb;
 }

/*
 *	Free an skbuff by memory
 */ 	

void kfree_skbmem(void *mem,unsigned size)
{
	struct sk_buff *x=mem;
	IS_SKB(x);
	if(x->magic_debug_cookie==SK_GOOD_SKB)
	{
		x->magic_debug_cookie=SK_FREED_SKB;
		kfree_s(mem,size);
		net_skbcount--;
		net_memory-=size;
	}
}
 
