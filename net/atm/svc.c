/* net/atm/svc.c - ATM SVC sockets */

/* Written 1995-1999 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/string.h>
#include <linux/net.h>		/* struct socket, struct net_proto,
				   struct proto_ops */
#include <linux/errno.h>	/* error codes */
#include <linux/kernel.h>	/* printk */
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/sched.h>	/* jiffies and HZ */
#include <linux/fcntl.h>	/* O_NONBLOCK */
#include <linux/init.h>
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmsap.h>
#include <linux/atmsvc.h>
#include <linux/atmdev.h>
#include <net/sock.h>		/* for sock_no_* */
#include <asm/uaccess.h>

#include "resources.h"
#include "common.h"		/* common for PVCs and SVCs */
#include "signaling.h"
#include "addr.h"


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


static int svc_create(struct socket *sock,int protocol);


/*
 * Note: since all this is still nicely synchronized with the signaling demon,
 *       there's no need to protect sleep loops with clis. If signaling is
 *       moved into the kernel, that would change.
 */


void svc_callback(struct atm_vcc *vcc)
{
	wake_up(&vcc->sleep);
}




static int svc_shutdown(struct socket *sock,int how)
{
	return 0;
}


static void svc_disconnect(struct atm_vcc *vcc)
{
	struct sk_buff *skb;

	DPRINTK("svc_disconnect %p\n",vcc);
	if (vcc->flags & ATM_VF_REGIS) {
		sigd_enq(vcc,as_close,NULL,NULL,NULL);
		while (!(vcc->flags & ATM_VF_RELEASED) && sigd)
		    sleep_on(&vcc->sleep);
	}
	/* beware - socket is still in use by atmsigd until the last
	   as_indicate has been answered */
	while ((skb = skb_dequeue(&vcc->listenq))) {
		DPRINTK("LISTEN REL\n");
		sigd_enq(NULL,as_reject,vcc,NULL,NULL); /* @@@ should include
							       the reason */
		dev_kfree_skb(skb);
	}
	vcc->flags &= ~(ATM_VF_REGIS | ATM_VF_RELEASED | ATM_VF_CLOSE);
	    /* may retry later */
}


static int svc_release(struct socket *sock)
{
	struct atm_vcc *vcc;

	if (!sock->sk) return 0;
	vcc = ATM_SD(sock);
	DPRINTK("svc_release %p\n",vcc);
	vcc->flags &= ~ATM_VF_READY;
	atm_release_vcc_sk(sock->sk,0);
	svc_disconnect(vcc);
	    /* VCC pointer is used as a reference, so we must not free it
	       (thereby subjecting it to re-use) before all pending connections
	        are closed */
	free_atm_vcc_sk(sock->sk);
	return 0;
}


static int svc_bind(struct socket *sock,struct sockaddr *sockaddr,
    int sockaddr_len)
{
	struct sockaddr_atmsvc *addr;
	struct atm_vcc *vcc;

	if (sockaddr_len != sizeof(struct sockaddr_atmsvc)) return -EINVAL;
	if (sock->state == SS_CONNECTED) return -EISCONN;
	if (sock->state != SS_UNCONNECTED) return -EINVAL;
	vcc = ATM_SD(sock);
	if (vcc->flags & ATM_VF_SESSION) return -EINVAL;
	addr = (struct sockaddr_atmsvc *) sockaddr;
	if (addr->sas_family != AF_ATMSVC) return -EAFNOSUPPORT;
	vcc->flags &= ~ATM_VF_BOUND; /* failing rebind will kill old binding */
	/* @@@ check memory (de)allocation on rebind */
	if (!(vcc->flags & ATM_VF_HASQOS)) return -EBADFD;
	vcc->local = *addr;
	vcc->reply = WAITING;
	sigd_enq(vcc,as_bind,NULL,NULL,&vcc->local);
	while (vcc->reply == WAITING && sigd) sleep_on(&vcc->sleep);
	vcc->flags &= ~ATM_VF_REGIS; /* doesn't count */
	if (!sigd) return -EUNATCH;
        if (!vcc->reply) vcc->flags |= ATM_VF_BOUND;
	return vcc->reply;
}


static int svc_connect(struct socket *sock,struct sockaddr *sockaddr,
    int sockaddr_len,int flags)
{
	struct sockaddr_atmsvc *addr;
	struct atm_vcc *vcc = ATM_SD(sock);
	int error;

	DPRINTK("svc_connect %p\n",vcc);
	if (sockaddr_len != sizeof(struct sockaddr_atmsvc)) return -EINVAL;
	if (sock->state == SS_CONNECTED) return -EISCONN;
	if (sock->state == SS_CONNECTING) {
		if (vcc->reply == WAITING) return -EALREADY;
		sock->state = SS_UNCONNECTED;
		if (vcc->reply) return vcc->reply;
	}
	else {
		if (sock->state != SS_UNCONNECTED) return -EINVAL;
		if (vcc->flags & ATM_VF_SESSION) return -EINVAL;
		addr = (struct sockaddr_atmsvc *) sockaddr;
		if (addr->sas_family != AF_ATMSVC) return -EAFNOSUPPORT;
		if (!(vcc->flags & ATM_VF_HASQOS)) return -EBADFD;
		if (vcc->qos.txtp.traffic_class == ATM_ANYCLASS ||
		    vcc->qos.rxtp.traffic_class == ATM_ANYCLASS)
			return -EINVAL;
		if (!vcc->qos.txtp.traffic_class &&
		    !vcc->qos.rxtp.traffic_class) return -EINVAL;
		vcc->remote = *addr;
		vcc->reply = WAITING;
		sigd_enq(vcc,as_connect,NULL,NULL,&vcc->remote);
		if (flags & O_NONBLOCK) {
			sock->state = SS_CONNECTING;
			return -EINPROGRESS;
		}
		while (vcc->reply == WAITING && sigd) {
			interruptible_sleep_on(&vcc->sleep);
			if (signal_pending(current)) {
				DPRINTK("*ABORT*\n");
				/*
				 * This is tricky:
				 *   Kernel ---close--> Demon
				 *   Kernel <--close--- Demon
			         * or
				 *   Kernel ---close--> Demon
				 *   Kernel <--error--- Demon
				 * or
				 *   Kernel ---close--> Demon
				 *   Kernel <--okay---- Demon
				 *   Kernel <--close--- Demon
				 */
				sigd_enq(vcc,as_close,NULL,NULL,NULL);
				while (vcc->reply == WAITING && sigd)
					sleep_on(&vcc->sleep);
				if (!vcc->reply)
					while (!(vcc->flags & ATM_VF_RELEASED)
					    && sigd) sleep_on(&vcc->sleep);
				vcc->flags &= ~(ATM_VF_REGIS | ATM_VF_RELEASED
				    | ATM_VF_CLOSE);
				    /* we're gone now but may connect later */
				return -EINTR;
			}
		}
		if (!sigd) return -EUNATCH;
		if (vcc->reply) return vcc->reply;
	}
/*
 * Not supported yet
 *
 * #ifndef CONFIG_SINGLE_SIGITF
 */
	vcc->qos.txtp.max_pcr = SELECT_TOP_PCR(vcc->qos.txtp);
	vcc->qos.txtp.pcr = 0;
	vcc->qos.txtp.min_pcr = 0;
/*
 * #endif
 */
	if (!(error = atm_connect(sock,vcc->itf,vcc->vpi,vcc->vci)))
		sock->state = SS_CONNECTED;
	else (void) svc_disconnect(vcc);
	return error;
}


static int svc_listen(struct socket *sock,int backlog)
{
	struct atm_vcc *vcc = ATM_SD(sock);

	DPRINTK("svc_listen %p\n",vcc);
	/* let server handle listen on unbound sockets */
	if (vcc->flags & ATM_VF_SESSION) return -EINVAL;
	vcc->reply = WAITING;
	sigd_enq(vcc,as_listen,NULL,NULL,&vcc->local);
	while (vcc->reply == WAITING && sigd) sleep_on(&vcc->sleep);
	if (!sigd) return -EUNATCH;
	vcc->flags |= ATM_VF_LISTEN;
	vcc->backlog_quota = backlog > 0 ? backlog : ATM_BACKLOG_DEFAULT;
	return vcc->reply;
}


static int svc_accept(struct socket *sock,struct socket *newsock,int flags)
{
	struct sk_buff *skb;
	struct atmsvc_msg *msg;
	struct atm_vcc *old_vcc = ATM_SD(sock);
	struct atm_vcc *new_vcc;
	int error;

	error = svc_create(newsock,0);
	if (error)
		return error;

	new_vcc = ATM_SD(newsock);

	DPRINTK("svc_accept %p -> %p\n",old_vcc,new_vcc);
	while (1) {
		while (!(skb = skb_dequeue(&old_vcc->listenq)) && sigd) {
			if (old_vcc->flags & ATM_VF_RELEASED) break;
			if (old_vcc->flags & ATM_VF_CLOSE)
				return old_vcc->reply;
			if (flags & O_NONBLOCK) return -EAGAIN;
			interruptible_sleep_on(&old_vcc->sleep);
			if (signal_pending(current)) return -ERESTARTSYS;
		}
		if (!skb) return -EUNATCH;
		msg = (struct atmsvc_msg *) skb->data;
		new_vcc->qos = msg->qos;
		new_vcc->flags |= ATM_VF_HASQOS;
		new_vcc->remote = msg->svc;
		new_vcc->sap = msg->sap;
		error = atm_connect(newsock,msg->pvc.sap_addr.itf,
		    msg->pvc.sap_addr.vpi,msg->pvc.sap_addr.vci);
		dev_kfree_skb(skb);
		old_vcc->backlog_quota++;
		if (error) {
			sigd_enq(NULL,as_reject,old_vcc,NULL,NULL);
				/* @@@ should include the reason */
			return error == -EAGAIN ? -EBUSY : error;
		}
		/* wait should be short, so we ignore the non-blocking flag */
		new_vcc->reply = WAITING;
		sigd_enq(new_vcc,as_accept,old_vcc,NULL,NULL);
		while (new_vcc->reply == WAITING && sigd)
			sleep_on(&new_vcc->sleep);
		if (!sigd) return -EUNATCH;
		if (!new_vcc->reply) break;
		if (new_vcc->reply != -ERESTARTSYS) return new_vcc->reply;
	}
	newsock->state = SS_CONNECTED;
	return 0;
}


static int svc_getname(struct socket *sock,struct sockaddr *sockaddr,
    int *sockaddr_len,int peer)
{
	struct sockaddr_atmsvc *addr;

	*sockaddr_len = sizeof(struct sockaddr_atmsvc);
	addr = (struct sockaddr_atmsvc *) sockaddr;
	memcpy(addr,peer ? &ATM_SD(sock)->remote : &ATM_SD(sock)->local,
	    sizeof(struct sockaddr_atmsvc));
	return 0;
}


int svc_change_qos(struct atm_vcc *vcc,struct atm_qos *qos)
{
	struct atm_qos save_qos;

	vcc->reply = WAITING;
	save_qos = vcc->qos; /* @@@ really gross hack ... */
	vcc->qos = *qos;
	sigd_enq(vcc,as_modify,NULL,NULL,&vcc->local);
	vcc->qos = save_qos;
	while (vcc->reply == WAITING && !(vcc->flags & ATM_VF_RELEASED) &&
	    sigd) sleep_on(&vcc->sleep);
	if (!sigd) return -EUNATCH;
	return vcc->reply;
}


static int svc_setsockopt(struct socket *sock,int level,int optname,
    char *optval,int optlen)
{
	struct atm_vcc *vcc;

	if (level != __SO_LEVEL(optname) || optname != SO_ATMSAP ||
	    optlen != sizeof(struct atm_sap))
		return atm_setsockopt(sock,level,optname,optval,optlen);
	vcc = ATM_SD(sock);
	if (copy_from_user(&vcc->sap,optval,optlen)) return -EFAULT;
	vcc->flags |= ATM_VF_HASSAP;
	return 0;
}


static int svc_getsockopt(struct socket *sock,int level,int optname,
    char *optval,int *optlen)
{
	int len;

	if (level != __SO_LEVEL(optname) || optname != SO_ATMSAP)
		return atm_getsockopt(sock,level,optname,optval,optlen);
	if (get_user(len,optlen)) return -EFAULT;
	if (len != sizeof(struct atm_sap)) return -EINVAL;
	return copy_to_user(optval,&ATM_SD(sock)->sap,sizeof(struct atm_sap)) ?
	    -EFAULT : 0;
}


static struct proto_ops SOCKOPS_WRAPPED(svc_proto_ops) = {
	PF_ATMSVC,
	svc_release,
	svc_bind,
	svc_connect,
	sock_no_socketpair,
	svc_accept,
	svc_getname,
	atm_poll,
	atm_ioctl,
	svc_listen,
	svc_shutdown,
	svc_setsockopt,
	svc_getsockopt,
	sock_no_fcntl,
	atm_sendmsg,
	atm_recvmsg,
	sock_no_mmap
};


#include <linux/smp_lock.h>
SOCKOPS_WRAP(svc_proto, PF_ATMSVC);

static int svc_create(struct socket *sock,int protocol)
{
	int error;

	sock->ops = &svc_proto_ops;
	error = atm_create(sock,protocol,AF_ATMSVC);
	if (error) return error;
	ATM_SD(sock)->callback = svc_callback;
	ATM_SD(sock)->local.sas_family = AF_ATMSVC;
	ATM_SD(sock)->remote.sas_family = AF_ATMSVC;
	return 0;
}


static struct net_proto_family svc_family_ops = {
	PF_ATMSVC,
	svc_create,
	0,			/* no authentication */
	0,			/* no encryption */
	0			/* no encrypt_net */
};


/*
 *	Initialize the ATM SVC protocol family
 */

void __init atmsvc_proto_init(struct net_proto *pro)
{
	if (sock_register(&svc_family_ops) < 0) {
		printk(KERN_ERR "ATMSVC: can't register");
		return;
	}
	signaling_init();
	init_addr();
}
