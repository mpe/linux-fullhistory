/* net/atm/ipcommon.c - Common items for all ways of doing IP over ATM */

/* Written 1996-2000 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/atmdev.h>
#include <linux/atmclip.h>

#include "common.h"
#include "ipcommon.h"


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


const unsigned char llc_oui[] = {
	0xaa,	/* DSAP: non-ISO */
	0xaa,	/* SSAP: non-ISO */
	0x03,	/* Ctrl: Unnumbered Information Command PDU */
	0x00,	/* OUI: EtherType */
	0x00,
	0x00 };


/*
 * skb_migrate moves the list at FROM to TO, emptying FROM in the process.
 * This function should live in skbuff.c or skbuff.h.
 */


void skb_migrate(struct sk_buff_head *from,struct sk_buff_head *to)
{
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&from->lock,flags);
	*to = *from;
	from->prev = (struct sk_buff *) from;
	from->next = (struct sk_buff *) from;
	from->qlen = 0;
	spin_unlock_irqrestore(&from->lock,flags);
	spin_lock_init(&to->lock);
	for (skb = ((struct sk_buff *) to)->next;
	    skb != (struct sk_buff *) from; skb = skb->next) skb->list = to;
	if (to->next == (struct sk_buff *) from)
		to->next = (struct sk_buff *) to;
	to->next->prev = (struct sk_buff *) to;
	to->prev->next = (struct sk_buff *) to;
}
