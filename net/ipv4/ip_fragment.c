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
#include <linux/inet.h>
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

atomic_t ip_frag_mem = ATOMIC_INIT(0);		/* Memory used for fragments */

char *in_ntoa(unsigned long in);

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
		NETDEBUG(printk(KERN_ERR "IP: frag_create: no memory left !\n"));
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
	atomic_add(skb->truesize, &ip_frag_mem);
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
				ICMP_EXC_FRAGTIME, 0);

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
	while(atomic_read(&ip_frag_mem)>IPFRAG_LOW_THRESH)
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

static struct ipq *ip_create(struct sk_buff *skb, struct iphdr *iph)
{
	struct ipq *qp;
	int ihlen;

	qp = (struct ipq *) frag_kmalloc(sizeof(struct ipq), GFP_ATOMIC);
	if (qp == NULL)
	{
		NETDEBUG(printk(KERN_ERR "IP: create: no memory left !\n"));
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
		NETDEBUG(printk(KERN_ERR "IP: create: no memory left !\n"));
		frag_kfree_s(qp, sizeof(struct ipq));
		return(NULL);
	}

	memcpy(qp->iph, iph, ihlen + 8);
	qp->len = 0;
	qp->ihlen = ihlen;
	qp->fragments = NULL;
	qp->dev = skb->dev;

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
	
	if(len>65535)
	{
		printk(KERN_INFO "Oversized IP packet from %s.\n", in_ntoa(qp->iph->saddr));
		ip_statistics.IpReasmFails++;
		ip_free(qp);
		return NULL;
	}
	
	if ((skb = dev_alloc_skb(len)) == NULL)
	{
		ip_statistics.IpReasmFails++;
		NETDEBUG(printk(KERN_ERR "IP: queue_glue: no memory for gluing queue %p\n", qp));
		ip_free(qp);
		return(NULL);
	}

	/* Fill in the basic details. */
	skb->mac.raw = ptr = skb->data;
	skb->nh.iph = iph = (struct iphdr*)skb_put(skb,len);

	/* Copy the original IP headers into the new buffer. */
	memcpy(ptr, qp->iph, qp->ihlen);
	ptr += qp->ihlen;

	count = 0;

	/* Copy the data portions of all fragments into the new buffer. */
	fp = qp->fragments;
	while(fp != NULL)
	{
		if(count+fp->len > skb->len)
		{
			NETDEBUG(printk(KERN_ERR "Invalid fragment list: Fragment over size.\n"));
			ip_free(qp);
			kfree_skb(skb,FREE_WRITE);
			ip_statistics.IpReasmFails++;
			return NULL;
		}
		memcpy((ptr + fp->offset), fp->ptr, fp->len);
		if (!count) {
			skb->dst = dst_clone(fp->skb->dst);
			skb->dev = fp->skb->dev;
		}
		count += fp->len;
		fp = fp->next;
	}

	/* We glued together all fragments, so remove the queue entry. */
	ip_free(qp);

	/* Done with all fragments. Fixup the new IP header. */
	iph = skb->nh.iph;
	iph->frag_off = 0;
	iph->tot_len = htons((iph->ihl * 4) + count);

	ip_statistics.IpReasmOKs++;
	return(skb);
}


/*
 *	Process an incoming IP datagram fragment.
 */

struct sk_buff *ip_defrag(struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
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

	if(atomic_read(&ip_frag_mem)>IPFRAG_HIGH_THRESH)
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
			ip_free(qp);	/* Fragmented frame replaced by full unfragmented copy */
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
		if ((qp = ip_create(skb, iph)) == NULL)
		{
			kfree_skb(skb, FREE_READ);
			ip_statistics.IpReasmFails++;
			return NULL;
		}
	}
	
	/*
	 *	Attempt to construct an oversize packet.
	 */
	 
	if(ntohs(iph->tot_len)+(int)offset>65535)
	{
		printk(KERN_INFO "Oversized packet received from %s\n",in_ntoa(iph->saddr));
		frag_kfree_skb(skb, FREE_READ);
		ip_statistics.IpReasmFails++;
		return NULL;
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

			if (tmp->next != NULL)
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
