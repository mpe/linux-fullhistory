/* net/atm/common.c - ATM sockets (common part for PVC and SVC) */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/net.h>		/* struct socket, struct net_proto, struct
				   proto_ops */
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmdev.h>
#include <linux/atmclip.h>	/* CLIP_*ENCAP */
#include <linux/atmarp.h>	/* manifest constants */
#include <linux/sonet.h>	/* for ioctls */
#include <linux/socket.h>	/* SOL_SOCKET */
#include <linux/errno.h>	/* error codes */
#include <linux/capability.h>
#include <linux/mm.h>		/* verify_area */
#include <linux/sched.h>
#include <linux/time.h>		/* struct timeval */
#include <linux/skbuff.h>
#include <net/sock.h>		/* struct sock */

#include <asm/uaccess.h>
#include <asm/poll.h>

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
#include <linux/atmlec.h>
#include "lec.h"
#include "lec_arpc.h"
struct atm_lane_ops atm_lane_ops;
#endif
#ifdef CONFIG_ATM_LANE_MODULE
EXPORT_SYMBOL(atm_lane_ops);
#endif

#if defined(CONFIG_ATM_MPOA) || defined(CONFIG_ATM_MPOA_MODULE)
#include <linux/atmmpc.h>
#include "mpc.h"
struct atm_mpoa_ops atm_mpoa_ops;
#endif
#ifdef CONFIG_ATM_MPOA_MODULE
EXPORT_SYMBOL(atm_mpoa_ops);
#ifndef CONFIG_ATM_LANE_MODULE
EXPORT_SYMBOL(atm_lane_ops);
#endif
#endif

#if defined(CONFIG_ATM_TCP) || defined(CONFIG_ATM_TCP_MODULE)
#include <linux/atm_tcp.h>
#ifdef CONFIG_ATM_TCP_MODULE
struct atm_tcp_ops atm_tcp_ops;
EXPORT_SYMBOL(atm_tcp_ops);
#endif
#endif

#include "resources.h"		/* atm_find_dev */
#include "common.h"		/* prototypes */
#include "protocols.h"		/* atm_init_<transport> */
#include "addr.h"		/* address registry */
#ifdef CONFIG_ATM_CLIP
#include <net/atmclip.h>	/* for clip_create */
#endif
#include "signaling.h"		/* for WAITING and sigd_attach */


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


static struct sk_buff *alloc_tx(struct atm_vcc *vcc,unsigned int size)
{
	struct sk_buff *skb;

	if (atomic_read(&vcc->tx_inuse) && !atm_may_send(vcc,size)) {
		DPRINTK("Sorry: tx_inuse = %d, size = %d, sndbuf = %d\n",
		    atomic_read(&vcc->tx_inuse),size,vcc->sk->sndbuf);
		return NULL;
	}
	while (!(skb = alloc_skb(size,GFP_KERNEL))) schedule();
	DPRINTK("AlTx %d += %d\n",atomic_read(&vcc->tx_inuse),skb->truesize);
	atomic_add(skb->truesize+ATM_PDU_OVHD,&vcc->tx_inuse);
	return skb;
}


int atm_create(struct socket *sock,int protocol,int family)
{
	struct sock *sk;
	struct atm_vcc *vcc;

	sock->sk = NULL;
	if (sock->type == SOCK_STREAM) return -EINVAL;
	if (!(sk = alloc_atm_vcc_sk(family))) return -ENOMEM;
	vcc = sk->protinfo.af_atm;
	vcc->flags = 0;
	vcc->dev = NULL;
	vcc->family = sock->ops->family;
	vcc->alloc_tx = alloc_tx;
	vcc->callback = NULL;
	memset(&vcc->local,0,sizeof(struct sockaddr_atmsvc));
	memset(&vcc->remote,0,sizeof(struct sockaddr_atmsvc));
	atomic_set(&vcc->tx_inuse,0);
	atomic_set(&vcc->rx_inuse,0);
	vcc->push = NULL;
	vcc->pop = NULL;
	vcc->push_oam = NULL;
	vcc->vpi = vcc->vci = 0; /* no VCI/VPI yet */
	vcc->atm_options = vcc->aal_options = 0;
	vcc->timestamp.tv_sec = vcc->timestamp.tv_usec = 0;
	init_waitqueue_head(&vcc->sleep);
	init_waitqueue_head(&vcc->wsleep);
	skb_queue_head_init(&vcc->recvq);
	skb_queue_head_init(&vcc->listenq);
	sock->sk = sk;
	return 0;
}


void atm_release_vcc_sk(struct sock *sk,int free_sk)
{
	struct atm_vcc *vcc;
	struct sk_buff *skb;

	vcc = sk->protinfo.af_atm;
	vcc->flags &= ~ATM_VF_READY;
	if (vcc->dev) {
		if (vcc->dev->ops->close) vcc->dev->ops->close(vcc);
		if (vcc->push) vcc->push(vcc,NULL); /* atmarpd has no push */
		while ((skb = skb_dequeue(&vcc->recvq))) {
			atm_return(vcc,skb->truesize);
			if (vcc->dev->ops->free_rx_skb)
				vcc->dev->ops->free_rx_skb(vcc,skb);
			else kfree_skb(skb);
		}
		if (atomic_read(&vcc->rx_inuse))
			printk(KERN_WARNING "atm_release_vcc: strange ... "
			    "rx_inuse == %d after closing\n",
			    atomic_read(&vcc->rx_inuse));
		bind_vcc(vcc,NULL);
	}
	if (free_sk) free_atm_vcc_sk(sk);
}


int atm_release(struct socket *sock)
{
	if (sock->sk)
		atm_release_vcc_sk(sock->sk,1);
	return 0;
}


void atm_async_release_vcc(struct atm_vcc *vcc,int reply)
{
	vcc->flags |= ATM_VF_CLOSE;
	vcc->reply = reply;
	/*vcc->flags &= ~ATM_VF_READY;*/
	wake_up(&vcc->sleep);
}


EXPORT_SYMBOL(atm_async_release_vcc);


static int adjust_tp(struct atm_trafprm *tp,unsigned char aal)
{
	int max_sdu;

	if (!tp->traffic_class) return 0;
	switch (aal) {
		case ATM_AAL0:
			max_sdu = ATM_CELL_SIZE-1;
			break;
		case ATM_AAL34:
			max_sdu = ATM_MAX_AAL34_PDU;
			break;
		default:
			printk(KERN_WARNING "ATM: AAL problems ... "
			    "(%d)\n",aal);
			/* fall through */
		case ATM_AAL5:
			max_sdu = ATM_MAX_AAL5_PDU;
	}
	if (!tp->max_sdu) tp->max_sdu = max_sdu;
	else if (tp->max_sdu > max_sdu) return -EINVAL;
	if (!tp->max_cdv) tp->max_cdv = ATM_MAX_CDV;
	return 0;
}


static int atm_do_connect_dev(struct atm_vcc *vcc,struct atm_dev *dev,int vpi,
    int vci)
{
	int error;

	if ((vpi != ATM_VPI_UNSPEC && vpi != ATM_VPI_ANY &&
	    vpi >> dev->ci_range.vpi_bits) || (vci != ATM_VCI_UNSPEC &&
	    vci != ATM_VCI_ANY && vci >> dev->ci_range.vci_bits))
		return -EINVAL;
	if (vci > 0 && vci < ATM_NOT_RSV_VCI && !capable(CAP_NET_BIND_SERVICE))
		return -EPERM;
	error = 0;
	switch (vcc->qos.aal) {
		case ATM_AAL0:
			error = atm_init_aal0(vcc);
			vcc->stats = &dev->stats.aal0;
			break;
		case ATM_AAL34:
			error = atm_init_aal34(vcc);
			vcc->stats = &dev->stats.aal34;
			break;
		case ATM_NO_AAL:
			/* ATM_AAL5 is also used in the "0 for default" case */
			vcc->qos.aal = ATM_AAL5;
			/* fall through */
		case ATM_AAL5:
			error = atm_init_aal5(vcc);
			vcc->stats = &dev->stats.aal5;
			break;
		default:
			error = -EPROTOTYPE;
	}
	if (!error) error = adjust_tp(&vcc->qos.txtp,vcc->qos.aal);
	if (!error) error = adjust_tp(&vcc->qos.rxtp,vcc->qos.aal);
	if (error) return error;
	bind_vcc(vcc,dev);
	DPRINTK("VCC %d.%d, AAL %d\n",vpi,vci,vcc->qos.aal);
	DPRINTK("  TX: %d, PCR %d..%d, SDU %d\n",vcc->qos.txtp.traffic_class,
	    vcc->qos.txtp.min_pcr,vcc->qos.txtp.max_pcr,vcc->qos.txtp.max_sdu);
	DPRINTK("  RX: %d, PCR %d..%d, SDU %d\n",vcc->qos.rxtp.traffic_class,
	    vcc->qos.rxtp.min_pcr,vcc->qos.rxtp.max_pcr,vcc->qos.rxtp.max_sdu);
	if (dev->ops->open) {
		error = dev->ops->open(vcc,vpi,vci);
		if (error) {
			bind_vcc(vcc,NULL);
			return error;
		}
	}
	return 0;
}


static int atm_do_connect(struct atm_vcc *vcc,int itf,int vpi,int vci)
{
	struct atm_dev *dev;

	dev = atm_find_dev(itf);
	if (!dev) return -ENODEV;
	return atm_do_connect_dev(vcc,dev,vpi,vci);
}


int atm_connect_vcc(struct atm_vcc *vcc,int itf,short vpi,int vci)
{
	if (vpi != ATM_VPI_UNSPEC && vci != ATM_VCI_UNSPEC)
		vcc->flags &= ~ATM_VF_PARTIAL;
	else if (vcc->flags & ATM_VF_PARTIAL) return -EINVAL;
	printk(KERN_DEBUG "atm_connect (TX: cl %d,bw %d-%d,sdu %d; "
	    "RX: cl %d,bw %d-%d,sdu %d,AAL %s%d)\n",
	    vcc->qos.txtp.traffic_class,vcc->qos.txtp.min_pcr,
	    vcc->qos.txtp.max_pcr,vcc->qos.txtp.max_sdu,
	    vcc->qos.rxtp.traffic_class,vcc->qos.rxtp.min_pcr,
	    vcc->qos.rxtp.max_pcr,vcc->qos.rxtp.max_sdu,
	    vcc->qos.aal == ATM_AAL5 ? "" : vcc->qos.aal == ATM_AAL0 ? "" :
	    " ??? code ",vcc->qos.aal == ATM_AAL0 ? 0 : vcc->qos.aal);
	if (!(vcc->flags & ATM_VF_HASQOS)) return -EBADFD;
	if (vcc->qos.txtp.traffic_class == ATM_ANYCLASS ||
	    vcc->qos.rxtp.traffic_class == ATM_ANYCLASS)
		return -EINVAL;
	if (itf != ATM_ITF_ANY) {
		int error;

		error = atm_do_connect(vcc,itf,vpi,vci);
		if (error) return error;
	}
	else {
		struct atm_dev *dev;

		for (dev = atm_devs; dev; dev = dev->next)
			if (!atm_do_connect_dev(vcc,dev,vpi,vci)) break;
		if (!dev) return -ENODEV;
	}
	if (vpi == ATM_VPI_UNSPEC || vci == ATM_VCI_UNSPEC)
		vcc->flags |= ATM_VF_PARTIAL;
	return 0;
}


int atm_connect(struct socket *sock,int itf,short vpi,int vci)
{
	int error;

	DPRINTK("atm_connect (vpi %d, vci %d)\n",vpi,vci);
	if (sock->state == SS_CONNECTED) return -EISCONN;
	if (sock->state != SS_UNCONNECTED) return -EINVAL;
	if (!(vpi || vci)) return -EINVAL;
	error = atm_connect_vcc(ATM_SD(sock),itf,vpi,vci);
	if (error) return error;
	if (ATM_SD(sock)->flags & ATM_VF_READY) sock->state = SS_CONNECTED;
	return 0;
}


int atm_recvmsg(struct socket *sock,struct msghdr *m,int total_len,
    int flags,struct scm_cookie *scm)
{
	struct atm_vcc *vcc;
	struct sk_buff *skb;
	unsigned long cpu_flags;
	int eff_len,error;

	void *buff;
	int size;

	if (sock->state != SS_CONNECTED) return -ENOTCONN;
	if (flags & ~MSG_DONTWAIT) return -EOPNOTSUPP;
	if (m->msg_iovlen != 1) return -ENOSYS; /* fix this later @@@ */
	buff = m->msg_iov->iov_base;
	size = m->msg_iov->iov_len;
	vcc = ATM_SD(sock);
	save_flags(cpu_flags);
	cli();
	while (!(skb = skb_dequeue(&vcc->recvq))) {
		if (vcc->flags & (ATM_VF_RELEASED | ATM_VF_CLOSE)) {
			restore_flags(cpu_flags);
			return vcc->reply;
		}
		if (!(vcc->flags & ATM_VF_READY)) {
			restore_flags(cpu_flags);
			return 0;
		}
		if (flags & MSG_DONTWAIT) {
			restore_flags(cpu_flags);
			return -EAGAIN;
		}
		interruptible_sleep_on(&vcc->sleep);
		if (signal_pending(current)) {
			restore_flags(cpu_flags);
			return -ERESTARTSYS;
		}
	}
	restore_flags(cpu_flags);
	vcc->timestamp = skb->stamp;
	eff_len = skb->len > size ? size : skb->len;
	if (vcc->dev->ops->feedback)
		vcc->dev->ops->feedback(vcc,skb,(unsigned long) skb->data,
		    (unsigned long) buff,eff_len);
	DPRINTK("RcvM %d -= %d\n",atomic_read(&vcc->rx_inuse),skb->truesize);
	atm_return(vcc,skb->truesize);
	if (ATM_SKB(skb)->iovcnt) { /* @@@ hack */
		/* iovcnt set, use scatter-gather for receive */
		int el, cnt;
		struct iovec *iov = (struct iovec *)skb->data;
		unsigned char *p = (unsigned char *)buff;

		el = eff_len;
		error = 0;
		for (cnt = 0; (cnt < ATM_SKB(skb)->iovcnt) && el; cnt++) {
/*printk("s-g???: %p -> %p (%d)\n",iov->iov_base,p,iov->iov_len);*/
			error = copy_to_user(p,iov->iov_base,
			    (iov->iov_len > el) ? el : iov->iov_len) ?
			    -EFAULT : 0;
			if (error) break;
			p += iov->iov_len;
			el -= (iov->iov_len > el)?el:iov->iov_len;
			iov++;
		}
		if (!vcc->dev->ops->free_rx_skb) kfree_skb(skb);
		else vcc->dev->ops->free_rx_skb(vcc, skb);
		return error ? error : eff_len;
	}
	error = copy_to_user(buff,skb->data,eff_len) ? -EFAULT : 0;
	if (!vcc->dev->ops->free_rx_skb) kfree_skb(skb);
	else vcc->dev->ops->free_rx_skb(vcc, skb);
	return error ? error : eff_len;
}


int atm_sendmsg(struct socket *sock,struct msghdr *m,int total_len,
    struct scm_cookie *scm)
{
	struct atm_vcc *vcc;
	struct sk_buff *skb;
	int eff,error;

	const void *buff;
	int size;

	if (sock->state != SS_CONNECTED) return -ENOTCONN;
	if (m->msg_name) return -EISCONN;
	if (m->msg_iovlen != 1) return -ENOSYS; /* fix this later @@@ */
	buff = m->msg_iov->iov_base;
	size = m->msg_iov->iov_len;
	vcc = ATM_SD(sock);
	if (vcc->flags & (ATM_VF_RELEASED | ATM_VF_CLOSE)) return vcc->reply;
	if (!(vcc->flags & ATM_VF_READY)) return -EPIPE;
	if (!size) return 0;
	/* verify_area is done by net/socket.c */
	eff = (size+3) & ~3; /* align to word boundary */
	while (!(skb = vcc->alloc_tx(vcc,eff))) {
		if (m->msg_flags & MSG_DONTWAIT) return -EAGAIN;
		interruptible_sleep_on(&vcc->wsleep);
		if (signal_pending(current)) return -ERESTARTSYS;
		if (vcc->flags & (ATM_VF_RELEASED | ATM_VF_CLOSE))
			return vcc->reply;
		if (!(vcc->flags & ATM_VF_READY)) return -EPIPE;
	}
	skb->dev = NULL; /* for paths shared with net_device interfaces */
	ATM_SKB(skb)->iovcnt = 0;
	ATM_SKB(skb)->atm_options = vcc->atm_options;
	if (copy_from_user(skb_put(skb,size),buff,size)) {
		kfree_skb(skb);
		return -EFAULT;
	}
	if (eff != size) memset(skb->data+size,0,eff-size);
	error = vcc->dev->ops->send(vcc,skb);
	return error ? error : size;
}


unsigned int atm_poll(struct file *file,struct socket *sock,poll_table *wait)
{
	struct atm_vcc *vcc;
	unsigned int mask;

	vcc = ATM_SD(sock);
	poll_wait(file,&vcc->sleep,wait);
	poll_wait(file,&vcc->wsleep,wait);
	mask = 0;
	if (skb_peek(&vcc->recvq) || skb_peek(&vcc->listenq))
		mask |= POLLIN | POLLRDNORM;
	if (vcc->flags & (ATM_VF_RELEASED | ATM_VF_CLOSE)) mask |= POLLHUP;
	if (sock->state != SS_CONNECTING) {
		if (vcc->qos.txtp.traffic_class != ATM_NONE &&
		    vcc->qos.txtp.max_sdu+atomic_read(&vcc->tx_inuse)+
		    ATM_PDU_OVHD <= vcc->sk->sndbuf)
			mask |= POLLOUT | POLLWRNORM;
	}
	else if (vcc->reply != WAITING) {
			mask |= POLLOUT | POLLWRNORM;
			if (vcc->reply) mask |= POLLERR;
		}
	return mask;
}


static int fetch_stats(struct atm_dev *dev,struct atm_dev_stats *arg,int zero)
{
	unsigned long flags;
	int error;

	error = 0;
	save_flags(flags);
	cli();
	if (arg)
		error = copy_to_user(arg,&dev->stats,
		    sizeof(struct atm_dev_stats));
        if (zero && !error)
                memset(&dev->stats,0,sizeof(struct atm_dev_stats));
        restore_flags(flags);
	return error ? -EFAULT : 0;
}


int atm_ioctl(struct socket *sock,unsigned int cmd,unsigned long arg)
{
	struct atm_dev *dev;
	struct atm_vcc *vcc;
	int *tmp_buf;
	void *buf;
	int error,len,size,number;

	vcc = ATM_SD(sock);
	switch (cmd) {
		case SIOCOUTQ:
			if (sock->state != SS_CONNECTED ||
			    !(vcc->flags & ATM_VF_READY)) return -EINVAL;
			return put_user(vcc->sk->sndbuf-
			    atomic_read(&vcc->tx_inuse)-ATM_PDU_OVHD,
			    (int *) arg) ? -EFAULT : 0;
		case SIOCINQ:
			{
				struct sk_buff *skb;

				if (sock->state != SS_CONNECTED)
					return -EINVAL;
				skb = skb_peek(&vcc->recvq);
				return put_user(skb ? skb->len : 0,(int *) arg)
				    ? -EFAULT : 0;
			}
		case ATM_GETNAMES:
			if (get_user(buf,
			    &((struct atm_iobuf *) arg)->buffer))
				return -EFAULT;
			if (get_user(len,
			    &((struct atm_iobuf *) arg)->length))
				return -EFAULT;
			size = 0;
			for (dev = atm_devs; dev; dev = dev->next)
				size += sizeof(int);
			if (size > len) return -E2BIG;
			tmp_buf = kmalloc(size,GFP_KERNEL);
			if (!tmp_buf) return -ENOMEM;
			for (dev = atm_devs; dev; dev = dev->next)
				*tmp_buf++ = dev->number;
			if (copy_to_user(buf,(char *) tmp_buf-size,size))
				return -EFAULT;
			return put_user(size,
			    &((struct atm_iobuf *) arg)->length) ? -EFAULT : 0;
		case SIOCGSTAMP: /* borrowed from IP */
			if (!vcc->timestamp.tv_sec) return -ENOENT;
			vcc->timestamp.tv_sec += vcc->timestamp.tv_usec/1000000;
			vcc->timestamp.tv_usec %= 1000000;
			return copy_to_user((void *) arg,&vcc->timestamp,
			    sizeof(struct timeval)) ? -EFAULT : 0;
		case ATM_SETSC:
			printk(KERN_WARNING "ATM_SETSC is obsolete\n");
			return 0;
		case ATMSIGD_CTRL:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			error = sigd_attach(vcc);
			if (!error) sock->state = SS_CONNECTED;
			return error;
#ifdef CONFIG_ATM_CLIP
		case SIOCMKCLIP:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			return clip_create(arg);
		case ATMARPD_CTRL:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			error = atm_init_atmarp(vcc);
			if (!error) sock->state = SS_CONNECTED;
			return error;
		case ATMARP_MKIP:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			return clip_mkip(vcc,arg);
		case ATMARP_SETENTRY:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			return clip_setentry(vcc,arg);
		case ATMARP_ENCAP:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			return clip_encap(vcc,arg);
#endif
#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
                case ATMLEC_CTRL:
                        if (!capable(CAP_NET_ADMIN)) return -EPERM;
                        if (atm_lane_ops.lecd_attach == NULL)
                                atm_lane_init();
                        if (atm_lane_ops.lecd_attach == NULL) /* try again */
                                return -ENOSYS;
                        error = atm_lane_ops.lecd_attach(vcc, (int)arg);
                        if (error >= 0) sock->state = SS_CONNECTED;
                        return error;
                case ATMLEC_MCAST:
                        if (!capable(CAP_NET_ADMIN)) return -EPERM;
                        return atm_lane_ops.mcast_attach(vcc, (int)arg);
                case ATMLEC_DATA:
                        if (!capable(CAP_NET_ADMIN)) return -EPERM;
                        return atm_lane_ops.vcc_attach(vcc, (void*)arg);
#endif
#if defined(CONFIG_ATM_MPOA) || defined(CONFIG_ATM_MPOA_MODULE)
		case ATMMPC_CTRL:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
                        if (atm_mpoa_ops.mpoad_attach == NULL)
                                atm_mpoa_init();
                        if (atm_mpoa_ops.mpoad_attach == NULL) /* try again */
                                return -ENOSYS;
                        error = atm_mpoa_ops.mpoad_attach(vcc, (int)arg);
                        if (error >= 0) sock->state = SS_CONNECTED;
                        return error;
		case ATMMPC_DATA:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			return atm_mpoa_ops.vcc_attach(vcc, arg);
#endif
#if defined(CONFIG_ATM_TCP) || defined(CONFIG_ATM_TCP_MODULE)
		case SIOCSIFATMTCP:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			if (!atm_tcp_ops.attach) return -ENOPKG;
			error = atm_tcp_ops.attach(vcc,(int) arg);
			if (error >= 0) sock->state = SS_CONNECTED;
			return error;
		case ATMTCP_CREATE:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			if (!atm_tcp_ops.create_persistent) return -ENOPKG;
			return atm_tcp_ops.create_persistent((int) arg);
		case ATMTCP_REMOVE:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			if (!atm_tcp_ops.remove_persistent) return -ENOPKG;
			return atm_tcp_ops.remove_persistent((int) arg);
#endif
		default:
			break;
	}
	if (get_user(buf,&((struct atmif_sioc *) arg)->arg)) return -EFAULT;
	if (get_user(len,&((struct atmif_sioc *) arg)->length)) return -EFAULT;
	if (get_user(number,&((struct atmif_sioc *) arg)->number))
		return -EFAULT;
	if (!(dev = atm_find_dev(number))) return -ENODEV;
	size = 0;
	switch (cmd) {
		case ATM_GETTYPE:
			size = strlen(dev->type)+1;
			if (copy_to_user(buf,dev->type,size)) return -EFAULT;
			break;
		case ATM_GETESI:
			size = ESI_LEN;
			if (copy_to_user(buf,dev->esi,size)) return -EFAULT;
			break;
		case ATM_SETESI:
			{
				int i;

				for (i = 0; i < ESI_LEN; i++)
					if (dev->esi[i]) return -EEXIST;
			}
			/* fall through */
		case ATM_SETESIF:
			{
				unsigned char esi[ESI_LEN];

				if (!capable(CAP_NET_ADMIN)) return -EPERM;
				if (copy_from_user(esi,buf,ESI_LEN))
					return -EFAULT;
				memcpy(dev->esi,esi,ESI_LEN);
				return ESI_LEN;
			}
		case ATM_GETSTATZ:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			/* fall through */
		case ATM_GETSTAT:
			size = sizeof(struct atm_dev_stats);
			error = fetch_stats(dev,buf,cmd == ATM_GETSTATZ);
			if (error) return error;
			break;
		case ATM_GETCIRANGE:
			size = sizeof(struct atm_cirange);
			if (copy_to_user(buf,&dev->ci_range,size))
				return -EFAULT;
			break;
		case ATM_GETLINKRATE:
			size = sizeof(int);
			if (copy_to_user(buf,&dev->link_rate,size))
				return -EFAULT;
			break;
		case ATM_RSTADDR:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			reset_addr(dev);
			break;
		case ATM_ADDADDR:
		case ATM_DELADDR:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			{
				struct sockaddr_atmsvc addr;

				if (copy_from_user(&addr,buf,sizeof(addr)))
					return -EFAULT;
				if (cmd == ATM_ADDADDR)
					return add_addr(dev,&addr);
				else return del_addr(dev,&addr);
			}
		case ATM_GETADDR:
			size = get_addr(dev,buf,len);
			if (size < 0) return size;
			/* may return 0, but later on size == 0 means "don't
			   write the length" */
			return put_user(size,
			    &((struct atmif_sioc *) arg)->length) ? -EFAULT : 0;
		case ATM_SETCIRANGE:
		case SONET_GETSTATZ:
		case SONET_SETDIAG:
		case SONET_CLRDIAG:
		case SONET_SETFRAMING:
			if (!capable(CAP_NET_ADMIN)) return -EPERM;
			/* fall through */
		default:
			if (!dev->ops->ioctl) return -EINVAL;
			size = dev->ops->ioctl(dev,cmd,buf);
			if (size < 0)
				return size == -ENOIOCTLCMD ? -EINVAL : size;
	}
	if (!size) return 0;
	return put_user(size,&((struct atmif_sioc *) arg)->length) ?
	    -EFAULT : 0;
}


int atm_change_qos(struct atm_vcc *vcc,struct atm_qos *qos)
{
	if (!vcc->dev->ops->change_qos) return -EOPNOTSUPP;
	if (vcc->family == AF_ATMPVC)
		return vcc->dev->ops->change_qos(vcc,qos,ATM_MF_SET);
	return svc_change_qos(vcc,qos);
}


static int check_tp(struct atm_trafprm *tp)
{
	/* @@@ Should be merged with adjust_tp */
	if (!tp->traffic_class || tp->traffic_class == ATM_ANYCLASS) return 0;
	if (tp->traffic_class != ATM_UBR && !tp->min_pcr && !tp->pcr &&
	    !tp->max_pcr) return -EINVAL;
	if (tp->min_pcr == ATM_MAX_PCR) return -EINVAL;
	if (tp->min_pcr && tp->max_pcr && tp->max_pcr != ATM_MAX_PCR &&
	    tp->min_pcr > tp->max_pcr) return -EINVAL;
	/*
	 * We allow pcr to be outside [min_pcr,max_pcr], because later
	 * adjustment may still push it in the valid range.
	 */
	return 0;
}


static int check_qos(struct atm_qos *qos)
{
	int error;

	if (!qos->txtp.traffic_class && !qos->rxtp.traffic_class)
                return -EINVAL;
	if (qos->txtp.traffic_class != qos->rxtp.traffic_class &&
	    qos->txtp.traffic_class && qos->rxtp.traffic_class &&
	    qos->txtp.traffic_class != ATM_ANYCLASS &&
	    qos->rxtp.traffic_class != ATM_ANYCLASS) return -EINVAL;
	error = check_tp(&qos->txtp);
	if (error) return error;
	return check_tp(&qos->rxtp);
}


static int atm_do_setsockopt(struct socket *sock,int level,int optname,
    void *optval,int optlen)
{
	struct atm_vcc *vcc;
	unsigned long value;
	int error;

	vcc = ATM_SD(sock);
	switch (optname) {
		case SO_ATMQOS:
			{
				struct atm_qos qos;

				if (copy_from_user(&qos,optval,sizeof(qos)))
					return -EFAULT;
				error = check_qos(&qos);
				if (error) return error;
				if (sock->state == SS_CONNECTED)
					return atm_change_qos(vcc,&qos);
				if (sock->state != SS_UNCONNECTED)
					return -EBADFD;
				vcc->qos = qos;
				vcc->flags |= ATM_VF_HASQOS;
				return 0;
			}
		case SO_SETCLP:
			if (get_user(value,(unsigned long *) optval))
				return -EFAULT;
			if (value) vcc->atm_options |= ATM_ATMOPT_CLP;
			else vcc->atm_options &= ~ATM_ATMOPT_CLP;
			return 0;
		default:
			if (level == SOL_SOCKET) return -EINVAL;
			break;
	}
	if (!vcc->dev || !vcc->dev->ops->setsockopt) return -EINVAL;
	return vcc->dev->ops->setsockopt(vcc,level,optname,optval,optlen);
}


static int atm_do_getsockopt(struct socket *sock,int level,int optname,
    void *optval,int optlen)
{
	struct atm_vcc *vcc;

	vcc = ATM_SD(sock);
	switch (optname) {
		case SO_ATMQOS:
			if (!(vcc->flags & ATM_VF_HASQOS)) return -EINVAL;
			return copy_to_user(optval,&vcc->qos,sizeof(vcc->qos)) ?
			    -EFAULT : 0;
		case SO_SETCLP:
			return put_user(vcc->atm_options & ATM_ATMOPT_CLP ? 1 :
			  0,(unsigned long *) optval) ? -EFAULT : 0;
		case SO_ATMPVC:
			{
				struct sockaddr_atmpvc pvc;

				if (!vcc->dev || !(vcc->flags & ATM_VF_ADDR))
					return -ENOTCONN;
				pvc.sap_family = AF_ATMPVC;
				pvc.sap_addr.itf = vcc->dev->number;
				pvc.sap_addr.vpi = vcc->vpi;
				pvc.sap_addr.vci = vcc->vci;
				return copy_to_user(optval,&pvc,sizeof(pvc)) ?
				    -EFAULT : 0;
			}
		default:
			if (level == SOL_SOCKET) return -EINVAL;
			break;
	}
	if (!vcc->dev || !vcc->dev->ops->getsockopt) return -EINVAL;
	return vcc->dev->ops->getsockopt(vcc,level,optname,optval,optlen);
}


int atm_setsockopt(struct socket *sock,int level,int optname,char *optval,
    int optlen)
{
	if (__SO_LEVEL_MATCH(optname, level) && optlen != __SO_SIZE(optname))
		return -EINVAL;
	return atm_do_setsockopt(sock,level,optname,optval,optlen);
}


int atm_getsockopt(struct socket *sock,int level,int optname,
    char *optval,int *optlen)
{
	int len;

	if (get_user(len,optlen)) return -EFAULT;
	if (__SO_LEVEL_MATCH(optname, level) && len != __SO_SIZE(optname))
		return -EINVAL;
	return atm_do_getsockopt(sock,level,optname,optval,len);
}
