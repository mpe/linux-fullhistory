/* net/atm/signaling.c - ATM signaling */

/* Written 1995-1999 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/errno.h>	/* error codes */
#include <linux/kernel.h>	/* printk */
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/sched.h>	/* jiffies and HZ */
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmsap.h>
#include <linux/atmsvc.h>
#include <linux/atmdev.h>

#include "tunable.h"
#include "resources.h"
#include "signaling.h"


#undef WAIT_FOR_DEMON		/* #define this if system calls on SVC sockets
				   should block until the demon runs.
				   Danger: may cause nasty hangs if the demon
				   crashes. */

#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


struct atm_vcc *sigd = NULL;
static wait_queue_head_t sigd_sleep;


static void sigd_put_skb(struct sk_buff *skb)
{
#ifdef WAIT_FOR_DEMON
	static unsigned long silence = 0;
#endif

	while (!sigd) {
#ifdef WAIT_FOR_DEMON
		if (time_after(jiffies, silence) || silence == 0) {
			printk(KERN_INFO "atmsvc: waiting for signaling demon "
			    "...\n");
			silence = (jiffies+30*HZ)|1;
		}
		sleep_on(&sigd_sleep);
#else
		printk(KERN_WARNING "atmsvc: no signaling demon\n");
		kfree_skb(skb);
		return;
#endif
	}
	atm_force_charge(sigd,skb->truesize);
	skb_queue_tail(&sigd->recvq,skb);
	wake_up(&sigd->sleep);
}


static void modify_qos(struct atm_vcc *vcc,struct atmsvc_msg *msg)
{
	struct sk_buff *skb;

	if ((vcc->flags & ATM_VF_RELEASED) || !(vcc->flags & ATM_VF_READY))
		return;
	msg->type = as_error;
	if (!vcc->dev->ops->change_qos) msg->reply = -EOPNOTSUPP;
	else {
		/* should lock VCC */
		msg->reply = vcc->dev->ops->change_qos(vcc,&msg->qos,
		    msg->reply);
		if (!msg->reply) msg->type = as_okay;
	}
	/*
	 * Should probably just turn around the old skb. But the, the buffer
	 * space accounting needs to follow the change too. Maybe later.
	 */
	while (!(skb = alloc_skb(sizeof(struct atmsvc_msg),GFP_KERNEL)))
		schedule();
	*(struct atmsvc_msg *) skb_put(skb,sizeof(struct atmsvc_msg)) = *msg;
	sigd_put_skb(skb);
}


static int sigd_send(struct atm_vcc *vcc,struct sk_buff *skb)
{
	struct atmsvc_msg *msg;
	struct atm_vcc *session_vcc;

	msg = (struct atmsvc_msg *) skb->data;
	atomic_sub(skb->truesize+ATM_PDU_OVHD,&vcc->tx_inuse);
	DPRINTK("sigd_send %d (0x%lx)\n",(int) msg->type,msg->vcc);
	vcc = (struct atm_vcc *) msg->vcc;
	switch (msg->type) {
		case as_okay:
			vcc->reply = msg->reply;
			if (!*vcc->local.sas_addr.prv &&
			    !*vcc->local.sas_addr.pub) {
				vcc->local.sas_family = AF_ATMSVC;
				memcpy(vcc->local.sas_addr.prv,
				    msg->local.sas_addr.prv,ATM_ESA_LEN);
				memcpy(vcc->local.sas_addr.pub,
				    msg->local.sas_addr.pub,ATM_E164_LEN+1);
			}
			session_vcc = vcc->session ? vcc->session : vcc;
			if (session_vcc->vpi || session_vcc->vci) break;
			session_vcc->itf = msg->pvc.sap_addr.itf;
			session_vcc->vpi = msg->pvc.sap_addr.vpi;
			session_vcc->vci = msg->pvc.sap_addr.vci;
			if (session_vcc->vpi || session_vcc->vci)
				session_vcc->qos = msg->qos;
			break;
		case as_error:
			vcc->flags &= ~(ATM_VF_REGIS | ATM_VF_READY);
			vcc->reply = msg->reply;
			break;
		case as_indicate:
			vcc = (struct atm_vcc *) msg->listen_vcc;
			DPRINTK("as_indicate!!!\n");
			if (!vcc->backlog_quota) {
				sigd_enq(0,as_reject,vcc,NULL,NULL);
				return 0;
			}
			vcc->backlog_quota--;
			skb_queue_tail(&vcc->listenq,skb);
			if (vcc->callback) {
				DPRINTK("waking vcc->sleep 0x%p\n",
				    &vcc->sleep);
				vcc->callback(vcc);
			}
			return 0;
		case as_close:
			vcc->flags |= ATM_VF_RELEASED;
			vcc->flags &= ~ATM_VF_READY;
			vcc->reply = msg->reply;
			break;
		case as_modify:
			modify_qos(vcc,msg);
			break;
		default:
			printk(KERN_ALERT "sigd_send: bad message type %d\n",
			    (int) msg->type);
			return -EINVAL;
	}
	if (vcc->callback) vcc->callback(vcc);
	dev_kfree_skb(skb);
	return 0;
}


void sigd_enq(struct atm_vcc *vcc,enum atmsvc_msg_type type,
    const struct atm_vcc *listen_vcc,const struct sockaddr_atmpvc *pvc,
    const struct sockaddr_atmsvc *svc)
{
	struct sk_buff *skb;
	struct atmsvc_msg *msg;

	DPRINTK("sigd_enq %d (0x%p)\n",(int) type,vcc);
	while (!(skb = alloc_skb(sizeof(struct atmsvc_msg),GFP_KERNEL)))
		schedule();
	msg = (struct atmsvc_msg *) skb_put(skb,sizeof(struct atmsvc_msg));
	msg->type = type;
	msg->vcc = (unsigned long) vcc;
	msg->listen_vcc = (unsigned long) listen_vcc;
	msg->reply = 0; /* other ISP applications may use this field */
	if (vcc) {
		msg->qos = vcc->qos;
		msg->sap = vcc->sap;
	}
	if (!svc) msg->svc.sas_family = 0;
	else msg->svc = *svc;
	if (vcc) msg->local = vcc->local;
	if (!pvc) memset(&msg->pvc,0,sizeof(msg->pvc));
	else msg->pvc = *pvc;
	sigd_put_skb(skb);
	if (vcc) vcc->flags |= ATM_VF_REGIS;
}


static void purge_vccs(struct atm_vcc *vcc)
{
	while (vcc) {
		if (vcc->family == PF_ATMSVC &&
		    !(vcc->flags & ATM_VF_META)) {
			vcc->flags |= ATM_VF_RELEASED;
			vcc->reply = -EUNATCH;
			wake_up(&vcc->sleep);
		}
		vcc = vcc->next;
	}
}


static void sigd_close(struct atm_vcc *vcc)
{
	struct sk_buff *skb;
	struct atm_dev *dev;

	DPRINTK("sigd_close\n");
	sigd = NULL;
	if (skb_peek(&vcc->recvq))
		printk(KERN_ERR "sigd_close: closing with requests pending\n");
	while ((skb = skb_dequeue(&vcc->recvq))) kfree_skb(skb);
	purge_vccs(nodev_vccs);
	for (dev = atm_devs; dev; dev = dev->next) purge_vccs(dev->vccs);
}


static struct atmdev_ops sigd_dev_ops = {
	NULL,		/* no dev_close */
	NULL,		/* no open */
	sigd_close,	/* close */
	NULL,		/* no ioctl */
	NULL,		/* no getsockopt */
	NULL,		/* no setsockopt */
	sigd_send,	/* send */
	NULL,		/* no sg_send */
	NULL,		/* no send_oam */
	NULL,		/* no phy_put */
	NULL,		/* no phy_get */
	NULL,		/* no feedback */
	NULL,		/* no change_qos */
	NULL		/* no free_rx_skb */
};


static struct atm_dev sigd_dev = {
	&sigd_dev_ops,
	NULL,		/* no PHY */
    	"sig",		/* type */
	999,		/* dummy device number */
	NULL,NULL,	/* pretend not to have any VCCs */
	NULL,NULL,	/* no data */
	0,		/* no flags */
	NULL,		/* no local address */
	{ 0 }		/* no ESI, no statistics */
};


int sigd_attach(struct atm_vcc *vcc)
{
	if (sigd) return -EADDRINUSE;
	DPRINTK("sigd_attach\n");
	sigd = vcc;
	bind_vcc(vcc,&sigd_dev);
	vcc->flags |= ATM_VF_READY | ATM_VF_META;
	wake_up(&sigd_sleep);
	return 0;
}


void signaling_init(void)
{
	init_waitqueue_head(&sigd_sleep);
}
