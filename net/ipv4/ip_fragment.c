/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP fragmentation functionality.
 *		
 * Authors:	Fred N. van Kempen <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox <Alan.Cox@linux.org>
 *
 * Fixes:
 *		Alan Cox	:	Split from ip.c , see ip_input.c for history.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/firewall.h>
#include <linux/ip_fw.h>
#include <net/checksum.h>

/*
 *	Fragment cache limits. We will commit 256K at one time. Should we
 *	cross that limit we will prune down to 192K. This should cope with
 *	even the most extreme cases without allowing an attacker to measurably
 *	harm machine performance.
 */
 
#define IPFRAG_HIGH_THRESH		(256*1024)
#define IPFRAG_LOW_THRESH		(192*1024)

/*
 *	This fragment handler is a bit of a heap. On the other hand it works quite
 *	happily and handles things quite well.
 */

static struct ipq *ipqueue = NULL;		/* IP fragment queue	*/

atomic_t ip_frag_mem = 0;			/* Memory used for fragments */

/*
 *	Memory Tracking Functions
 */
 
extern __inline__ void frag_kfree_skb(struct sk_buff *skb, int type)
{
	atomic_sub(skb->truesize, &ip_frag_mem);
	kfree_skb(skb,type);
}

extern __inline__ void frag_kfree_s(void *ptr, int len)
{
	atomic_sub(len, &ip_frag_mem);
	kfree_s(ptr,len);
}
 
extern __inline__ void *frag_kmalloc(int size, int pri)
{
	void *vp=kmalloc(size,pri);
	if(!vp)
		return NULL;
	atomic_add(size, &ip_frag_mem);
	return vp;
}
 
/*
 *	Create a new fragment entry.
 */

static struct ipfrag *ip_frag_create(int offset, int end, struct sk_buff *skb, unsigned char *ptr)
{
	struct ipfrag *fp;
	unsigned long flags;

	fp = (struct ipfrag *) frag_kmalloc(sizeof(struct ipfrag), GFP_ATOMIC);
	if (fp == NULL)
	{
		NETDEBUG(printk("IP: frag_create: no memory left !\n"));
		return(NULL);
	}
	memset(fp, 0, sizeof(struct ipfrag));

	/* Fill in the structure. */
	fp->offset = offset;
	fp->end = end;
	fp->len = end - offset;
	fp->skb = skb;
	fp->ptr = ptr;
	
	/*
	 *	Charge for the SKB as well.
	 */
	 
	save_flags(flags);
	cli();
	ip_frag_mem+=skb->truesize;
	restore_flags(flags);

	return(fp);
}


/*
 *	Find the correct entry in the "incomplete datagrams" queue for
 *	this IP datagram, and return the queue entry address if found.
 */

static struct ipq *ip_find(struct iphdr *iph)
{
	struct ipq *qp;
	struct ipq *qplast;

	cli();
	qplast = NULL;
	for(qp = ipqueue; qp != NULL; qplast = qp, qp = qp->next)
	{
		if (iph->id== qp->iph->id && iph->saddr == qp->iph->saddr &&
			iph->daddr == qp->iph->daddr && iph->protocol == qp->iph->protocol)
		{
			del_timer(&qp->timer);	/* So it doesn't vanish on us. The timer will be reset anyway */
			sti();
			return(qp);
		}
	}
	sti();
	return(NULL);
}


/*
 *	Remove an entry from the "incomplete datagrams" queue, either
 *	because we completed, reassembled and processed it, or because
 *	it timed out.
 */

static void ip_free(struct ipq *qp)
{
	struct ipfrag *fp;
	struct ipfrag *xp;

	/*
	 * Stop the timer for this entry.
	 */

	del_timer(&qp->timer);

	/* Remove this entry from the "incomplete datagrams" queue. */
	cli();
	if (qp->prev == NULL)
	{
		ipqueue = qp->next;
		if (ipqueue != NULL)
			ipqueue->prev = NULL;
	}
	else
	{
		qp->prev->next = qp->next;
		if (qp->next != NULL)
			qp->next->prev = qp->prev;
	}

	/* Release all fragment data. */

	fp = qp->fragments;
	while (fp != NULL)
	{
		xp = fp->next;
		IS_SKB(fp->skb);
		frag_kfree_skb(fp->skb,FREE_READ);
		frag_kfree_s(fp, sizeof(struct ipfrag));
		fp = xp;
	}

	/* Release the IP header. */
	frag_kfree_s(qp->iph, 64 + 8);

	/* Finally, release the queue descriptor itself. */
	frag_kfree_s(qp, sizeof(struct ipq));
	sti();
}


/*
 *	Oops- a fragment queue timed out.  Kill it and send an ICMP reply.
 */

static void ip_expire(unsigned long arg)
{
	struct ipq *qp;

	qp = (struct ipq *)arg;

	/*
	 *	Send an ICMP "Fragment Reassembly Timeout" message.
	 */

	ip_statistics.IpReasmTimeout++;
	ip_statistics.IpReasmFails++;   
	/* This if is always true... shrug */
	if(qp->fragments!=NULL)
		icmp_send(qp->fragments->skb,ICMP_TIME_EXCEEDED,
				ICMP_EXC_FRAGTIME, 0, qp->dev);

	/*
	 *	Nuke the fragment queue.
	 */
	ip_free(qp);
}

/*
 *	Memory limiting on fragments. Evictor trashes the oldest 
 *	fragment queue until we are back under the low threshold
 */
 
static void ip_evictor(void)
{
	while(ip_frag_mem>IPFRAG_LOW_THRESH)
	{
		if(!ipqueue)
			panic("ip_evictor: memcount");
		ip_free(ipqueue);
	}
}

/*
 * 	Add an entry to the 'ipq' queue for a newly received IP datagram.
 * 	We will (hopefully :-) receive all other fragments of this datagram
 * 	in time, so we just create a queue for this datagram, in which we
 * 	will insert the received fragments at their respective positions.
 */

static struct ipq *ip_create(struct sk_buff *skb, struct iphdr *iph, struct device *dev)
{
	struct ipq *qp;
	int ihlen;

	qp = (struct ipq *) frag_kmalloc(sizeof(struct ipq), GFP_ATOMIC);
	if (qp == NULL)
	{
		NETDEBUG(printk("IP: create: no memory left !\n"));
		return(NULL);
	}
	memset(qp, 0, sizeof(struct ipq));

	/*
	 *	Allocate memory for the IP header (plus 8 octets for ICMP).
	 */

	ihlen = iph->ihl * 4;
	qp->iph = (struct iphdr *) frag_kmalloc(64 + 8, GFP_ATOMIC);
	if (qp->iph == NULL)
	{
		NETDEBUG(printk("IP: create: no memory left !\n"));
		frag_kfree_s(qp, sizeof(struct ipq));
		return(NULL);
	}

	memcpy(qp->iph, iph, ihlen + 8);
	qp->len = 0;
	qp->ihlen = ihlen;
	qp->fragments = NULL;
	qp->dev = dev;

	/* Start a timer for this entry. */
	qp->timer.expires = jiffies + IP_FRAG_TIME;	/* about 30 seconds	*/
	qp->timer.data = (unsigned long) qp;		/* pointer to queue	*/
	qp->timer.function = ip_expire;			/* expire function	*/
	add_timer(&qp->timer);

	/* Add this entry to the queue. */
	qp->prev = NULL;
	cli();
	qp->next = ipqueue;
	if (qp->next != NULL)
		qp->next->prev = qp;
	ipqueue = qp;
	sti();
	return(qp);
}


/*
 *	See if a fragment queue is complete.
 */

static int ip_done(struct ipq *qp)
{
	struct ipfrag *fp;
	int offset;

	/* Only possible if we received the final fragment. */
	if (qp->len == 0)
		return(0);

	/* Check all fragment offsets to see if they connect. */
	fp = qp->fragments;
	offset = 0;
	while (fp != NULL)
	{
		if (fp->offset > offset)
			return(0);	/* fragment(s) missing */
		offset = fp->end;
		fp = fp->next;
	}

	/* All fragments are present. */
	return(1);
}


/*
 *	Build a new IP datagram from all its fragments.
 *
 *	FIXME: We copy here because we lack an effective way of handling lists
 *	of bits on input. Until the new skb data handling is in I'm not going
 *	to touch this with a bargepole. 
 */

static struct sk_buff *ip_glue(struct ipq *qp)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct ipfrag *fp;
	unsigned char *ptr;
	int count, len;

	/*
	 *	Allocate a new buffer for the datagram.
	 */
	len = qp->ihlen + qp->len;

	if ((skb = dev_alloc_skb(len)) == NULL)
	{
		ip_statistics.IpReasmFails++;
		NETDEBUG(printk("IP: queue_glue: no memory for gluing queue %p\n", qp));
		ip_free(qp);
		return(NULL);
	}

	/* Fill in the basic details. */
	skb_put(skb,len);
	skb->h.raw = skb->data;
	skb->free = 1;

	/* Copy the original IP headers into the new buffer. */
	ptr = (unsigned char *) skb->h.raw;
	memcpy(ptr, ((unsigned char *) qp->iph), qp->ihlen);
	ptr += qp->ihlen;

	count = 0;

	/* Copy the data portions of all fragments into the new buffer. */
	fp = qp->fragments;
	while(fp != NULL)
	{
		if(count+fp->len > skb->len)
		{
			NETDEBUG(printk("Invalid fragment list: Fragment over size.\n"));
			ip_free(qp);
			frag_kfree_skb(skb,FREE_WRITE);
			ip_statistics.IpReasmFails++;
			return NULL;
		}
		memcpy((ptr + fp->offset), fp->ptr, fp->len);
		count += fp->len;
		fp = fp->next;
	}

	/* We glued together all fragments, so remove the queue entry. */
	ip_free(qp);

	/* Done with all fragments. Fixup the new IP header. */
	iph = skb->h.iph;
	iph->frag_off = 0;
	iph->tot_len = htons((iph->ihl * 4) + count);
	skb->ip_hdr = iph;

	ip_statistics.IpReasmOKs++;
	return(skb);
}


/*
 *	Process an incoming IP datagram fragment.
 */

struct sk_buff *ip_defrag(struct iphdr *iph, struct sk_buff *skb, struct device *dev)
{
	struct ipfrag *prev, *next, *tmp;
	struct ipfrag *tfp;
	struct ipq *qp;
	struct sk_buff *skb2;
	unsigned char *ptr;
	int flags, offset;
	int i, ihl, end;
	
	ip_statistics.IpReasmReqds++;

	/*
	 *	Start by cleaning up the memory
	 */

	if(ip_frag_mem>IPFRAG_HIGH_THRESH)
		ip_evictor();
	/* 
	 *	Find the entry of this IP datagram in the "incomplete datagrams" queue. 
	 */
	 
	qp = ip_find(iph);

	/* Is this a non-fragmented datagram? */
	offset = ntohs(iph->frag_off);
	flags = offset & ~IP_OFFSET;
	offset &= IP_OFFSET;
	if (((flags & IP_MF) == 0) && (offset == 0))
	{
		if (qp != NULL)
			ip_free(qp);	/* Huh? How could this exist?? */
		return(skb);
	}

	offset <<= 3;		/* offset is in 8-byte chunks */
	ihl = iph->ihl * 4;

	/*
	 * If the queue already existed, keep restarting its timer as long
	 * as we still are receiving fragments.  Otherwise, create a fresh
	 * queue entry.
	 */

	if (qp != NULL)
	{
		/* ANK. If the first fragment is received,
		 * we should remember the correct IP header (with options)
		 */
	        if (offset == 0)
		{
			qp->ihlen = ihl;
			memcpy(qp->iph, iph, ihl+8);
		}
		del_timer(&qp->timer);
		qp->timer.expires = jiffies + IP_FRAG_TIME;	/* about 30 seconds */
		qp->timer.data = (unsigned long) qp;	/* pointer to queue */
		qp->timer.function = ip_expire;		/* expire function */
		add_timer(&qp->timer);
	}
	else
	{
		/*
		 *	If we failed to create it, then discard the frame
		 */
		if ((qp = ip_create(skb, iph, dev)) == NULL)
		{
			skb->sk = NULL;
			frag_kfree_skb(skb, FREE_READ);
			ip_statistics.IpReasmFails++;
			return NULL;
		}
	}

	/*
	 *	Determine the position of this fragment.
	 */

	end = offset + ntohs(iph->tot_len) - ihl;

	/*
	 *	Point into the IP datagram 'data' part.
	 */

	ptr = skb->data + ihl;

	/*
	 *	Is this the final fragment?
	 */

	if ((flags & IP_MF) == 0)
		qp->len = end;

	/*
	 * 	Find out which fragments are in front and at the back of us
	 * 	in the chain of fragments so far.  We must know where to put
	 * 	this fragment, right?
	 */

	prev = NULL;
	for(next = qp->fragments; next != NULL; next = next->next)
	{
		if (next->offset > offset)
			break;	/* bingo! */
		prev = next;
	}

	/*
	 * 	We found where to put this one.
	 * 	Check for overlap with preceding fragment, and, if needed,
	 * 	align things so that any overlaps are eliminated.
	 */
	if (prev != NULL && offset < prev->end)
	{
		i = prev->end - offset;
		offset += i;	/* ptr into datagram */
		ptr += i;	/* ptr into fragment data */
	}

	/*
	 * Look for overlap with succeeding segments.
	 * If we can merge fragments, do it.
	 */

	for(tmp=next; tmp != NULL; tmp = tfp)
	{
		tfp = tmp->next;
		if (tmp->offset >= end)
			break;		/* no overlaps at all */

		i = end - next->offset;			/* overlap is 'i' bytes */
		tmp->len -= i;				/* so reduce size of	*/
		tmp->offset += i;			/* next fragment	*/
		tmp->ptr += i;
		/*
		 *	If we get a frag size of <= 0, remove it and the packet
		 *	that it goes with.
		 */
		if (tmp->len <= 0)
		{
			if (tmp->prev != NULL)
				tmp->prev->next = tmp->next;
			else
				qp->fragments = tmp->next;

			if (tfp->next != NULL)
				tmp->next->prev = tmp->prev;
			
			next=tfp;	/* We have killed the original next frame */

			frag_kfree_skb(tmp->skb,FREE_READ);
			frag_kfree_s(tmp, sizeof(struct ipfrag));
		}
	}

	/*
	 *	Insert this fragment in the chain of fragments.
	 */

	tfp = NULL;
	tfp = ip_frag_create(offset, end, skb, ptr);

	/*
	 *	No memory to save the fragment - so throw the lot
	 */

	if (!tfp)
	{
		skb->sk = NULL;
		frag_kfree_skb(skb, FREE_READ);
		return NULL;
	}
	tfp->prev = prev;
	tfp->next = next;
	if (prev != NULL)
		prev->next = tfp;
	else
		qp->fragments = tfp;

	if (next != NULL)
		next->prev = tfp;

	/*
	 * 	OK, so we inserted this new fragment into the chain.
	 * 	Check if we now have a full IP datagram which we can
	 * 	bump up to the IP layer...
	 */

	if (ip_done(qp))
	{
		skb2 = ip_glue(qp);		/* glue together the fragments */
		return(skb2);
	}
	return(NULL);
}


/*
 *	This IP datagram is too large to be sent in one piece.  Break it up into
 *	smaller pieces (each of size equal to the MAC header plus IP header plus
 *	a block of the data of the original IP data part) that will yet fit in a
 *	single device frame, and queue such a frame for sending by calling the
 *	ip_queue_xmit().  Note that this is recursion, and bad things will happen
 *	if this function causes a loop...
 *
 *	Yes this is inefficient, feel free to submit a quicker one.
 *
 */
 
void ip_fragment(struct sock *sk, struct sk_buff *skb, struct device *dev, int is_frag)
{
	struct iphdr *iph;
	unsigned char *raw;
	unsigned char *ptr;
	struct sk_buff *skb2;
	int left, mtu, hlen, len;
	int offset;

	/*
	 *	Point into the IP datagram header.
	 */

	raw = skb->data;
#if 0
	iph = (struct iphdr *) (raw + dev->hard_header_len);	
	skb->ip_hdr = iph;
#else
	iph = skb->ip_hdr;
#endif

	/*
	 *	Setup starting values.
	 */

	hlen = iph->ihl * 4;
	left = ntohs(iph->tot_len) - hlen;	/* Space per frame */
	hlen += dev->hard_header_len;		/* Total header size */
	mtu = (dev->mtu - hlen);		/* Size of data space */
	ptr = (raw + hlen);			/* Where to start from */

	/*
	 *	Check for any "DF" flag. [DF means do not fragment]
	 */

	if (ntohs(iph->frag_off) & IP_DF)
	{
		ip_statistics.IpFragFails++;
		NETDEBUG(printk("ip_queue_xmit: frag needed\n"));
		return;
	}

	/*
	 *	The protocol doesn't seem to say what to do in the case that the
	 *	frame + options doesn't fit the mtu. As it used to fall down dead
	 *	in this case we were fortunate it didn't happen
	 */

	if(mtu<8)
	{
		/* It's wrong but it's better than nothing */
		icmp_send(skb,ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED,dev->mtu, dev);
		ip_statistics.IpFragFails++;
		return;
	}

	/*
	 *	Fragment the datagram.
	 */

	/*
	 *	The initial offset is 0 for a complete frame. When
	 *	fragmenting fragments it's wherever this one starts.
	 */

	if (is_frag & 2)
		offset = (ntohs(iph->frag_off) & IP_OFFSET) << 3;
	else
		offset = 0;


	/*
	 *	Keep copying data until we run out.
	 */

	while(left > 0)
	{
		len = left;
		/* IF: it doesn't fit, use 'mtu' - the data space left */
		if (len > mtu)
			len = mtu;
		/* IF: we are not sending upto and including the packet end
		   then align the next start on an eight byte boundary */
		if (len < left)
		{
			len/=8;
			len*=8;
		}
		/*
		 *	Allocate buffer.
		 */

		if ((skb2 = alloc_skb(len + hlen+15,GFP_ATOMIC)) == NULL)
		{
			NETDEBUG(printk("IP: frag: no memory for new fragment!\n"));
			ip_statistics.IpFragFails++;
			return;
		}

		/*
		 *	Set up data on packet
		 */

		skb2->arp = skb->arp;
		if(skb->free==0)
			printk(KERN_ERR "IP fragmenter: BUG free!=1 in fragmenter\n");
		skb2->free = 1;
		skb_put(skb2,len + hlen);
		skb2->h.raw=(char *) skb2->data;
		/*
		 *	Charge the memory for the fragment to any owner
		 *	it might possess
		 */

		if (sk)
		{
			atomic_add(skb2->truesize, &sk->wmem_alloc);
			skb2->sk=sk;
		}
		skb2->raddr = skb->raddr;	/* For rebuild_header - must be here */

		/*
		 *	Copy the packet header into the new buffer.
		 */

		memcpy(skb2->h.raw, raw, hlen);

		/*
		 *	Copy a block of the IP datagram.
		 */
		memcpy(skb2->h.raw + hlen, ptr, len);
		left -= len;

		skb2->h.raw+=dev->hard_header_len;

		/*
		 *	Fill in the new header fields.
		 */
		iph = (struct iphdr *)(skb2->h.raw/*+dev->hard_header_len*/);
		iph->frag_off = htons((offset >> 3));
		skb2->ip_hdr = iph;

		/* ANK: dirty, but effective trick. Upgrade options only if
		 * the segment to be fragmented was THE FIRST (otherwise,
		 * options are already fixed) and make it ONCE
		 * on the initial skb, so that all the following fragments
		 * will inherit fixed options.
		 */
		if (offset == 0)
			ip_options_fragment(skb);

		/*
		 *	Added AC : If we are fragmenting a fragment that's not the
		 *		   last fragment then keep MF on each bit
		 */
		if (left > 0 || (is_frag & 1))
			iph->frag_off |= htons(IP_MF);
		ptr += len;
		offset += len;

		/*
		 *	Put this fragment into the sending queue.
		 */

		ip_statistics.IpFragCreates++;

		ip_queue_xmit(sk, dev, skb2, 2);
	}
	ip_statistics.IpFragOKs++;
}


