/*
 *	IPv6 fragment reassembly
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on: net/ipv4/ip_fragment.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>


static struct frag_queue ipv6_frag_queue = {
	&ipv6_frag_queue, &ipv6_frag_queue,
	0, {0}, NULL, NULL,
	0
};

static void			create_frag_entry(struct sk_buff *skb, 
						  struct device *dev,
						  __u8 *nhptr,
						  struct frag_hdr *fhdr);
static int			reasm_frag_1(struct frag_queue *fq, 
					     struct sk_buff **skb_in);

static void			reasm_queue(struct frag_queue *fq, 
					    struct sk_buff *skb, 
					    struct frag_hdr *fhdr);

static int reasm_frag(struct frag_queue *fq, struct sk_buff **skb, 
		      __u8 *nhptr,
		      struct frag_hdr *fhdr)
{
	__u32	expires;
	int nh;

	expires = del_timer(&fq->timer);

	/*
	 *	We queue the packet even if it's the last.
	 *	It's a trade off. This allows the reassembly 
	 *	code to be simpler (=faster) and of the
	 *	steps we do for queueing the only unnecessary 
	 *	one it's the kmalloc for a struct ipv6_frag.
	 *	Feel free to try other alternatives...
	 */
	reasm_queue(fq, *skb, fhdr);

	if ((fhdr->frag_off & __constant_htons(0x0001)) == 0)
	{
		fq->last_in = 1;
		fq->nhptr = nhptr;
	}

	if (fq->last_in)
	{
		if ((nh = reasm_frag_1(fq, skb)))
			return nh;
	}
		
	fq->timer.expires = expires;
	add_timer(&fq->timer);
	
	return 0;
}

int ipv6_reassembly(struct sk_buff **skb, struct device *dev, __u8 *nhptr,
		    struct ipv6_options *opt)
{
	struct frag_hdr *fhdr = (struct frag_hdr *) ((*skb)->h.raw);
	struct frag_queue *fq;
	
	for (fq = ipv6_frag_queue.next; fq != &ipv6_frag_queue; fq = fq->next)
	{
		if (fq->id == fhdr->identification)
		{			
			return reasm_frag(fq, skb, nhptr,fhdr);
		}
	}
	
	create_frag_entry(*skb, dev, nhptr, fhdr);


	return 0;
}


static void fq_free(struct frag_queue *fq)
{
	struct ipv6_frag *fp, *back;

	for(fp = fq->fragments; fp; )
	{
		kfree_skb(fp->skb, FREE_READ);		
		back = fp;
		fp=fp->next;
		kfree(back);
	}

	fq->prev->next = fq->next;
	fq->next->prev = fq->prev;

	fq->prev = fq->next = NULL;
	
	kfree(fq);

}

static void frag_expire(unsigned long data)
{
	struct frag_queue *fq;
	struct ipv6_frag *frag;

	fq = (struct frag_queue *) data;

	del_timer(&fq->timer);

	frag = fq->fragments;

	if (frag == NULL)
	{
		printk(KERN_DEBUG "invalid fragment queue\n");
		return;
	}

	icmpv6_send(frag->skb, ICMPV6_TIME_EXCEEDED, ICMPV6_EXC_FRAGTIME, 0,
		    frag->skb->dev);
	
	fq_free(fq);
}


static void create_frag_entry(struct sk_buff *skb, struct device *dev, 
			      __u8 *nhptr,
			      struct frag_hdr *fhdr)
{
	struct frag_queue *fq;

	fq = (struct frag_queue *) kmalloc(sizeof(struct frag_queue), 
					   GFP_ATOMIC);

	if (fq == NULL)
	{
		kfree_skb(skb, FREE_READ);
		return;
	}

	memset(fq, 0, sizeof(struct frag_queue));

	fq->id = fhdr->identification;

	fq->dev = dev;

	/* init_timer has been done by the memset */
	fq->timer.function = frag_expire;
	fq->timer.data = (long) fq;
	fq->timer.expires = jiffies + IPV6_FRAG_TIMEOUT;

	fq->nexthdr = fhdr->nexthdr;


	if ((fhdr->frag_off & __constant_htons(0x0001)) == 0)
	{
		fq->last_in = 1;
		fq->nhptr = nhptr;
	}
	reasm_queue(fq, skb, fhdr);

	fq->prev = ipv6_frag_queue.prev;
	fq->next = &ipv6_frag_queue;
	fq->prev->next = fq;
	ipv6_frag_queue.prev = fq;
	
	add_timer(&fq->timer);
}


static void reasm_queue(struct frag_queue *fq, struct sk_buff *skb, 
			struct frag_hdr *fhdr)
{
	struct ipv6_frag *nfp, *fp, **bptr;

	nfp = (struct ipv6_frag *) kmalloc(sizeof(struct ipv6_frag), 
					   GFP_ATOMIC);

	if (nfp == NULL)
	{
		kfree_skb(skb, FREE_READ);
		return;
	}

	
	nfp->offset = ntohs(fhdr->frag_off) & ~0x7;
	nfp->len = (ntohs(skb->nh.ipv6h->payload_len) -
		    ((u8 *) (fhdr + 1) - (u8 *) (skb->nh.ipv6h + 1)));


	nfp->skb  = skb;
	nfp->fhdr = fhdr;

	nfp->next = NULL;

	bptr = &fq->fragments;
	

	for (fp = fq->fragments; fp; fp=fp->next)
	{
		if (nfp->offset <= fp->offset)
			break;
		bptr = &fp->next;
	}
	
	if (fp && fp->offset == nfp->offset)
	{
		if (fp->len != nfp->len)
		{
			/* this cannot happen */
			printk(KERN_DEBUG "reasm_queue: dup with wrong len\n");
		}

		/* duplicate. discard it. */
		kfree_skb(skb, FREE_READ);
		kfree(nfp);
		return;
	}
	

	*bptr = nfp;
	nfp->next = fp;
}

/*
 *	check if this fragment completes the packet
 *	returns true on success
 */
static int reasm_frag_1(struct frag_queue *fq, struct sk_buff **skb_in)
{
	struct ipv6_frag *fp;
	struct ipv6_frag *tail = NULL;
	struct sk_buff *skb;
	__u32  offset = 0;
	__u32  payload_len;
	__u16  unfrag_len;
	__u16  copy;
	int    nh;


	for(fp = fq->fragments; fp; fp=fp->next)
	{
		if (offset != fp->offset)
			return 0;

		offset += fp->len;
		tail = fp;
	}

	/* 
	 * we know the m_flag arrived and we have a queue,
	 * starting from 0, without gaps.
	 * this means we have all fragments.
	 */

	unfrag_len = (u8 *) (tail->fhdr) - (u8 *) (tail->skb->nh.ipv6h + 1);

	payload_len = (unfrag_len + tail->offset + 
		       (tail->skb->tail - (__u8 *) (tail->fhdr + 1)));

	printk(KERN_DEBUG "reasm: payload len = %d\n", payload_len);

	if ((skb = dev_alloc_skb(sizeof(struct ipv6hdr) + payload_len))==NULL)
	{
		printk(KERN_DEBUG "reasm_frag: no memory for reassembly\n");
		fq_free(fq);
		return 1;
	}

	copy = unfrag_len + sizeof(struct ipv6hdr);

	skb->nh.ipv6h = (struct ipv6hdr *) skb->data;

	skb->dev = fq->dev;

	
	nh = fq->nexthdr;

	*(fq->nhptr) = nh;
	memcpy(skb_put(skb, copy), tail->skb->nh.ipv6h, copy);

	skb->h.raw = skb->tail;

	skb->nh.ipv6h->payload_len = ntohs(payload_len);

	*skb_in = skb;

	/*
	 *	FIXME: If we don't have a checksum we ought to be able
	 *	to defragment and checksum in this pass. [AC]
	 */
	for(fp = fq->fragments; fp; )
	{
		struct ipv6_frag *back;

		memcpy(skb_put(skb, fp->len), (__u8*)(fp->fhdr + 1), fp->len);
		kfree_skb(fp->skb, FREE_READ);
		back = fp;
		fp=fp->next;
		kfree(back);
	}
	
	fq->prev->next = fq->next;
	fq->next->prev = fq->prev;

	fq->prev = fq->next = NULL;
	
	kfree(fq);

	return nh;
}



/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fno-strength-reduce -pipe -m486 -DCPU=486 -DMODULE -DMODVERSIONS -include /usr/src/linux/include/linux/modversions.h  -c -o reassembly.o reassembly.c"
 * c-file-style: "Linux"
 * End:
 */
