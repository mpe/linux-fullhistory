/* net/atm/raw.c - Raw AAL0 and AAL5 transports */

/* Written 1995-1999 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/atmdev.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/mm.h>

#ifdef CONFIG_MMU_HACKS
#include <linux/mmuio.h>
#include <linux/uio.h>
#endif

#include "common.h"
#include "protocols.h"
#include "tunable.h"		/* tunable parameters */


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


/*
 * SKB == NULL indicates that the link is being closed
 */

void atm_push_raw(struct atm_vcc *vcc,struct sk_buff *skb)
{
	if (skb) {
		skb_queue_tail(&vcc->recvq,skb);
		wake_up(&vcc->sleep);
	}
}


static void atm_pop_raw(struct atm_vcc *vcc,struct sk_buff *skb)
{
#ifdef CONFIG_MMU_HACKS
	if (ATM_SKB(skb)->iovcnt)
		unlock_user(ATM_SKB(skb)->iovcnt,(struct iovec *) skb->data);
#endif
	DPRINTK("APopR (%d) %d -= %d\n",vcc->vci,vcc->tx_inuse,skb->truesize);
	atomic_sub(skb->truesize+ATM_PDU_OVHD,&vcc->tx_inuse);
	dev_kfree_skb(skb);
	wake_up(&vcc->wsleep);
}


int atm_init_aal0(struct atm_vcc *vcc)
{
	vcc->push = atm_push_raw;
	vcc->pop = atm_pop_raw;
	vcc->push_oam = NULL;
	return 0;
}


int atm_init_aal34(struct atm_vcc *vcc)
{
	vcc->push = atm_push_raw;
	vcc->pop = atm_pop_raw;
	vcc->push_oam = NULL;
	return 0;
}


int atm_init_aal5(struct atm_vcc *vcc)
{
	vcc->push = atm_push_raw;
	vcc->pop = atm_pop_raw;
	vcc->push_oam = NULL;
	return 0;
}


EXPORT_SYMBOL(atm_init_aal5);
