/* net/atm/ipcommon.c - Common items for all ways of doing IP over ATM */

/* Written 1996,1997 by Werner Almesberger, EPFL LRC */


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
 * This function should live in skbuff.c or skbuff.h. Note that skb_migrate
 * is not atomic, so turn off interrupts when using it.
 */


void skb_migrate(struct sk_buff_head *from,struct sk_buff_head *to)
{
	struct sk_buff *skb,*prev;

	for (skb = ((struct sk_buff *) from)->next;
	    skb != (struct sk_buff *) from; skb = skb->next) skb->list = to;
	prev = from->prev;
	from->next->prev = (struct sk_buff *) to;
	prev->next = (struct sk_buff *) to;
	*to = *from;
	skb_queue_head_init(from);
}
