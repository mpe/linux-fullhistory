/*
 * This is a module which is used for queueing IPv4 packets and
 * communicating with userspace via netlink.
 *
 * (C) 2000 James Morris
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/rtnetlink.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <net/sock.h>

#include <linux/netfilter_ipv4/ip_queue.h>

EXPORT_NO_SYMBOLS;

#define IPQ_THR_NAME "kipq"
#define IPQ_NAME "ip_queue"
#define IPQ_QMAX_DEFAULT 1024

#define IPQ_PROC_FS_NAME "ip_queue"

#define NET_IPQ_QMAX 2088
#define NET_IPQ_QMAX_NAME "ip_queue_maxlen"

typedef struct ipq_queue_element {
	struct list_head list;		/* Links element into queue */
	unsigned char state;		/* State of this element */
	int verdict;			/* Current verdict */
	struct nf_info *info;		/* Extra info from netfilter */
	struct sk_buff *skb;		/* Packet inside */
} ipq_queue_element_t;

typedef int (*ipq_send_cb_t)(ipq_queue_element_t *e);

typedef struct ipq_peer {
	pid_t pid;			/* PID of userland peer */
	unsigned char died;		/* We think the peer died */
	unsigned char copy_mode;	/* Copy packet as well as metadata? */
	size_t copy_range;		/* Range past metadata to copy */
	ipq_send_cb_t send;		/* Callback for sending data to peer */
} ipq_peer_t;

typedef struct ipq_thread {
	pid_t pid;			/* PID of kernel thread */
 	unsigned char terminate;	/* Termination flag */
 	unsigned char running;		/* Running flag */
 	wait_queue_head_t wq;		/* I/O wait queue */
 	void (*process)(void *data);	/* Queue processing function */
} ipq_thread_t;

typedef struct ipq_queue {
 	int len;			/* Current queue len */
 	int *maxlen;			/* Maximum queue len, via sysctl */
 	unsigned char state;		/* Current queue state */
 	struct list_head list;		/* Head of packet queue */
 	spinlock_t lock;		/* Queue spinlock */
 	ipq_peer_t peer;		/* Userland peer */
 	ipq_thread_t thread;		/* Thread context */
} ipq_queue_t;


/****************************************************************************
*
* Kernel thread
*
****************************************************************************/

static void ipq_thread_init(char *thread_name)
{
 	lock_kernel();
 	exit_files(current);
 	daemonize();
 	strcpy(current->comm, thread_name);
 	unlock_kernel();
 	spin_lock_irq(&current->sigmask_lock);
 	flush_signals(current);
 	sigfillset(&current->blocked);
 	recalc_sigpending(current);
 	spin_unlock_irq(&current->sigmask_lock);
}

static int ipq_thread_start(void *data)
{
 	ipq_queue_t *q = (ipq_queue_t *)data;

 	q->thread.running = 1;
 	ipq_thread_init(IPQ_THR_NAME);
 	q->thread.pid = current->pid;
 	while (!q->thread.terminate) {
 		interruptible_sleep_on(&q->thread.wq);
 		q->thread.process(q);
 	}
 	q->thread.running = 0;
 	return 0;
}

static void ipq_thread_stop(ipq_queue_t *q)
{
 	if (!(q->thread.pid || q->thread.running))
 		return;
 	q->state = IPQ_QS_FLUSH;
 	q->thread.terminate = 1;
 	wake_up_interruptible(&q->thread.wq);
 	current->state = TASK_INTERRUPTIBLE;
 	while (q->thread.running) {
 		schedule_timeout(HZ/10);
 		current->state = TASK_RUNNING;
 	}
}

static int ipq_thread_create(ipq_queue_t *q)
{
	int status = kernel_thread(ipq_thread_start, q, 0);
	return (status < 0) ? status : 0;
}


/****************************************************************************
 *
 * Packet queue
 *
 ****************************************************************************/

/* Must be called under spinlock */
static __inline__ void
ipq_dequeue(ipq_queue_t *q,
            ipq_queue_element_t *e)
{
	list_del(&e->list);
	nf_reinject(e->skb, e->info, e->verdict);
	kfree(e);
	q->len--;
}

/* Must be called under spinlock */
static __inline__ void
ipq_queue_drop(ipq_queue_t *q,
               ipq_queue_element_t *e)
{
	e->verdict = NF_DROP;
	ipq_dequeue(q, e);
}

static int
ipq_notify_peer(ipq_queue_t *q,
                ipq_queue_element_t *e)
{
	int status = q->peer.send(e);

	if (status >= 0) {
		e->state = IPQ_PS_WAITING;
		return status;
	}
	if (status == -ERESTARTSYS || status == -EAGAIN)
		return 0;
	printk(KERN_INFO "%s: error notifying peer %d, resetting "
	       "state and flushing queue\n", IPQ_NAME, q->peer.pid);
	q->state = IPQ_QS_FLUSH;
	q->peer.died = 1;
	q->peer.pid = 0;
	q->peer.copy_mode = IPQ_COPY_META;
	q->peer.copy_range = 0;
	return status;
}

static void
ipq_queue_process(void *data)
{
	struct list_head *i;
	ipq_queue_t *q = (ipq_queue_t *)data;

restart:
	if (q->state == IPQ_QS_HOLD)
		return;
	spin_lock_bh(&q->lock);
	for (i = q->list.prev; i != &q->list; i = i->prev) {
		ipq_queue_element_t *e = (ipq_queue_element_t *)i;

		if (q->state == IPQ_QS_FLUSH) {
			QDEBUG("flushing packet %p\n", e);
			ipq_queue_drop(q, e);
			continue;
		}
		switch (e->state) {
			case IPQ_PS_NEW: {
				int status = ipq_notify_peer(q, e);
				if (status < 0) {
					spin_unlock_bh(&q->lock);
					goto restart;
				}
				break;
			}
			case IPQ_PS_VERDICT:
				ipq_dequeue(q, e);
				break;
			case IPQ_PS_WAITING:
				break;
			default:
				printk(KERN_INFO "%s: dropping stuck packet %p "
				       "with ps=%d qs=%d\n", IPQ_NAME,
				       e, e->state, q->state);
				ipq_queue_drop(q, e);
		}
	}
	spin_unlock_bh(&q->lock);
	if (q->state == IPQ_QS_FLUSH)
		q->state = IPQ_QS_HOLD;
}

static ipq_queue_t *
ipq_queue_create(nf_queue_outfn_t outfn,
                 ipq_send_cb_t send_cb,
                 int *errp,
                 int *sysctl_qmax)
{
	int status;
	ipq_queue_t *q;

	*errp = 0;
	q = kmalloc(sizeof(ipq_queue_t), GFP_KERNEL);
	if (q == NULL) {
		*errp = -ENOMEM;
		return NULL;
	}
	q->thread.terminate = 0;
	q->thread.running = 0;
	q->thread.process = ipq_queue_process;
	init_waitqueue_head(&q->thread.wq);
	q->peer.pid = 0;
	q->peer.died = 0;
	q->peer.copy_mode = IPQ_COPY_META;
	q->peer.copy_range = 0;
	q->peer.send = send_cb;
	q->len = 0;
	q->maxlen = sysctl_qmax;
	q->state = IPQ_QS_HOLD;
	INIT_LIST_HEAD(&q->list);
	spin_lock_init(&q->lock);
	status = nf_register_queue_handler(PF_INET, outfn, q);
	if (status < 0) {
		*errp = -EBUSY;
		kfree(q);
		return NULL;
	}
	status = ipq_thread_create(q);
	if (status < 0) {
		nf_unregister_queue_handler(PF_INET);
		*errp = status;
		kfree(q);
		return  NULL;
	}
	return q;
}

static int
ipq_enqueue(ipq_queue_t *q,
            struct sk_buff *skb,
            struct nf_info *info)
{
	ipq_queue_element_t *e = NULL;

	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	if (e == NULL) {
		printk(KERN_ERR "%s: out of memory in %s\n",
		       IPQ_NAME, __FUNCTION__);
		return  -ENOMEM;
	}
	e->state = IPQ_PS_NEW;
	e->verdict = NF_DROP;
	e->info = info;
	e->skb = skb;
	spin_lock_bh(&q->lock);
	if (q->len >= *q->maxlen) {
		spin_unlock_bh(&q->lock);
		printk(KERN_WARNING "%s: queue full at %d entries, "
		       "dropping packet.\n", IPQ_NAME, q->len);
		kfree(e);
		nf_reinject(skb, info, NF_DROP);
		return 0;
	}
	list_add(&e->list, &q->list);
	q->len++;
	spin_unlock_bh(&q->lock);
	wake_up_interruptible(&q->thread.wq);
	return 0;
}

/* FIXME: need to find a way to notify user during module unload */
static void
ipq_queue_destroy(ipq_queue_t *q)
{
	ipq_thread_stop(q);
	nf_unregister_queue_handler(PF_INET);
	kfree(q);
}

static int
ipq_queue_mangle_ipv4(unsigned char *buf,
                      ipq_verdict_msg_t *v,
                      ipq_queue_element_t *e)
{
	struct iphdr *user_iph = (struct iphdr *)buf;

	if (v->data_len < sizeof(*user_iph))
		return 0;

	if (e->skb->nh.iph->check != user_iph->check) {
		int diff = v->data_len - e->skb->len;

		if (diff < 0)
			skb_trim(e->skb, v->data_len);
		else if (diff > 0) {
			if (v->data_len > 0xFFFF) {
				e->verdict = NF_DROP;
				return -EINVAL;
			}
			if (diff > skb_tailroom(e->skb)) {
				struct sk_buff *newskb;

				/* Ack, we waste a memcpy() of data here */
				newskb = skb_copy_expand(e->skb,
				                         skb_headroom(e->skb),
				                         diff,
				                         GFP_ATOMIC);
				if (newskb == NULL) {
					printk(KERN_WARNING "%s: OOM in %s, "
					       "dropping packet\n",
					       IPQ_THR_NAME, __FUNCTION__);
					e->verdict = NF_DROP;
					return -ENOMEM;
				}
				kfree_skb(e->skb);
				e->skb = newskb;
			}
			skb_put(e->skb, diff);
		}
		memcpy(e->skb->data, buf, v->data_len);
		e->skb->nfcache |= NFC_ALTERED;
	}
	return 0;
}

static int
ipq_queue_set_verdict(ipq_queue_t *q,
                      ipq_verdict_msg_t *v,
                      unsigned char *buf,
                      unsigned int len)
{
	struct list_head *i;

	if (v->value < 0 || v->value > NF_MAX_VERDICT)
		return -EINVAL;
	spin_lock_bh(&q->lock);
	for (i = q->list.next; i != &q->list; i = i->next) {
		ipq_queue_element_t *e = (ipq_queue_element_t *)i;

		if (v->id == (unsigned long )e) {
			int status = 0;
			e->state = IPQ_PS_VERDICT;
			e->verdict = v->value;

			if (buf && v->data_len == len)
				status = ipq_queue_mangle_ipv4(buf, v, e);
			spin_unlock_bh(&q->lock);
			return status;
		}
	}
	spin_unlock_bh(&q->lock);
	return -ENOENT;
}

static int
ipq_receive_peer(ipq_queue_t *q,
                 ipq_peer_msg_t *m,
                 unsigned char type,
                 unsigned int len)
{
	if (q->state == IPQ_QS_FLUSH)
		return -EBUSY;

	if (len < sizeof(ipq_peer_msg_t))
		return -EINVAL;

	switch (type) {
		case IPQM_MODE:
			switch (m->msg.mode.value) {
				case IPQ_COPY_NONE:
					q->peer.copy_mode = IPQ_COPY_NONE;
					q->peer.copy_range = 0;
					q->state = IPQ_QS_FLUSH;
					break;
				case IPQ_COPY_META:
					if (q->state == IPQ_QS_FLUSH)
						return -EAGAIN;
					q->peer.copy_mode = IPQ_COPY_META;
					q->peer.copy_range = 0;
					q->state = IPQ_QS_COPY;
					break;
				case IPQ_COPY_PACKET:
					if (q->state == IPQ_QS_FLUSH)
						return -EAGAIN;
					q->peer.copy_mode = IPQ_COPY_PACKET;
					q->peer.copy_range = m->msg.mode.range;
					q->state = IPQ_QS_COPY;
					break;
				default:
					return -EINVAL;
			}
			break;
		case IPQM_VERDICT: {
			int status;
			unsigned char *data = NULL;

			if (m->msg.verdict.value > NF_MAX_VERDICT)
				return -EINVAL;
			if (m->msg.verdict.data_len)
				data = (unsigned char *)m + sizeof(*m);
			status = ipq_queue_set_verdict(q, &m->msg.verdict,
			                               data, len - sizeof(*m));
			if (status < 0)
				return status;
			break;
		}
		default:
			return -EINVAL;
	}
	wake_up_interruptible(&q->thread.wq);
	return 0;
}


/****************************************************************************
 *
 * Netfilter interface
 *
 ****************************************************************************/

/*
 * Packets arrive here from netfilter for queuing to userspace.
 * All of them must be fed back via nf_reinject() or Alexey will kill Rusty.
 */
static int
receive_netfilter(struct sk_buff *skb,
                  struct nf_info *info,
                  void *data)
{
	ipq_queue_t *q = (ipq_queue_t *)data;

	if (q->state == IPQ_QS_FLUSH)
		return -EBUSY;
	return ipq_enqueue(q, skb, info);
}

/****************************************************************************
 *
 * Netlink interface.
 *
 ****************************************************************************/

static struct sk_buff *
netlink_build_message(ipq_queue_element_t *e,
                      int *errp);

extern __inline__ void
receive_user_skb(struct sk_buff *skb);

static int
netlink_send_peer(ipq_queue_element_t *e);

static struct sock *nfnl = NULL;
ipq_queue_t *nlq = NULL;

static int
netlink_send_peer(ipq_queue_element_t *e)
{
	int status = 0;
	struct sk_buff *skb;

	if (!nlq->peer.pid)
		return -EINVAL;
	skb = netlink_build_message(e, &status);
	if (skb == NULL)
		return status;
	return netlink_unicast(nfnl, skb, nlq->peer.pid, MSG_DONTWAIT);
}

static struct sk_buff *
netlink_build_message(ipq_queue_element_t *e,
                      int *errp)
{
	unsigned char *old_tail;
	size_t size = 0;
	size_t data_len = 0;
	struct sk_buff *skb;
	ipq_packet_msg_t *pm;
	struct nlmsghdr *nlh;

	switch (nlq->peer.copy_mode) {
		size_t copy_range;

		case IPQ_COPY_META:
			size = NLMSG_SPACE(sizeof(*pm));
			data_len = 0;
			break;
		case IPQ_COPY_PACKET:
			copy_range = nlq->peer.copy_range;
			if (copy_range == 0 || copy_range > e->skb->len)
				data_len = e->skb->len;
			else
				data_len = copy_range;
			size = NLMSG_SPACE(sizeof(*pm) + data_len);
			break;
		case IPQ_COPY_NONE:
		default:
			*errp = -EINVAL;
			return NULL;
	}
	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		goto nlmsg_failure;
	old_tail = skb->tail;
	nlh = NLMSG_PUT(skb, 0, 0, IPQM_PACKET, size - sizeof(*nlh));
	pm = NLMSG_DATA(nlh);
	memset(pm, 0, sizeof(*pm));
	pm->packet_id = (unsigned long )e;
	pm->data_len = data_len;
	pm->timestamp_sec = e->skb->stamp.tv_sec;
	pm->timestamp_usec = e->skb->stamp.tv_usec;
	pm->hook = e->info->hook;
	if (e->info->indev) strcpy(pm->indev_name, e->info->indev->name);
	else pm->indev_name[0] = '\0';
	if (e->info->outdev) strcpy(pm->outdev_name, e->info->outdev->name);
	else pm->outdev_name[0] = '\0';
	if (data_len)
		memcpy(++pm, e->skb->data, data_len);
	nlh->nlmsg_len = skb->tail - old_tail;
	NETLINK_CB(skb).dst_groups = 0;
	return skb;
nlmsg_failure:
	if (skb)
		kfree(skb);
	*errp = 0;
	printk(KERN_ERR "%s: error creating netlink message\n", IPQ_NAME);
	return NULL;
}

#define RCV_SKB_FAIL(err) do { netlink_ack(skb, nlh, (err)); return; } while (0);
/*
 * FIXME: ping old peer if we detect a new peer then resend.
 */
extern __inline__ void
receive_user_skb(struct sk_buff *skb)
{
	int status, type;
	struct nlmsghdr *nlh;

	nlh = (struct nlmsghdr *)skb->data;
	if (nlh->nlmsg_len < sizeof(*nlh)
	    || skb->len < nlh->nlmsg_len
	    || nlh->nlmsg_pid <= 0
	    || !(nlh->nlmsg_flags & NLM_F_REQUEST)
	    || nlh->nlmsg_flags & NLM_F_MULTI)
		RCV_SKB_FAIL(-EINVAL);
	if (nlh->nlmsg_flags & MSG_TRUNC)
		RCV_SKB_FAIL(-ECOMM);
	type = nlh->nlmsg_type;
	if (type < NLMSG_NOOP || type >= IPQM_MAX)
		RCV_SKB_FAIL(-EINVAL);
	if (type <= IPQM_BASE)
		return;
	if(!cap_raised(NETLINK_CB(skb).eff_cap, CAP_NET_ADMIN))
		RCV_SKB_FAIL(-EPERM);
	if (nlq->peer.pid && !nlq->peer.died
	    && (nlq->peer.pid != nlh->nlmsg_pid))
	    	printk(KERN_WARNING "%s: peer pid changed from %d to %d\n",
	    	       IPQ_NAME, nlq->peer.pid, nlh->nlmsg_pid);
	nlq->peer.pid = nlh->nlmsg_pid;
	nlq->peer.died = 0;
	status = ipq_receive_peer(nlq, NLMSG_DATA(nlh),
	                          type, skb->len - NLMSG_LENGTH(0));
	if (status < 0)
		RCV_SKB_FAIL(status);
	if (nlh->nlmsg_flags & NLM_F_ACK)
		netlink_ack(skb, nlh, 0);
        return;
}

/* Note: we are only dealing with single part messages at the moment. */
static void
receive_user_sk(struct sock *sk,
                int len)
{
	do {
		struct sk_buff *skb;

		if (rtnl_shlock_nowait())
			return;
		while ((skb = skb_dequeue(&sk->receive_queue)) != NULL) {
			receive_user_skb(skb);
			kfree_skb(skb);
		}
		up(&rtnl_sem);
	} while (nfnl && nfnl->receive_queue.qlen);
}


/****************************************************************************
 *
 * System events
 *
 ****************************************************************************/

static int
receive_event(struct notifier_block *this,
              unsigned long event,
              void *ptr)
{
	if (event == NETDEV_UNREGISTER)
		if (nlq)
			ipq_thread_stop(nlq);
	return NOTIFY_DONE;
}

struct notifier_block ipq_dev_notifier = {
	receive_event,
	NULL,
	0
};


/****************************************************************************
 *
 * Sysctl - queue tuning.
 *
 ****************************************************************************/

static int sysctl_maxlen = IPQ_QMAX_DEFAULT;

static struct ctl_table_header *ipq_sysctl_header;

static ctl_table ipq_table[] = {
	{ NET_IPQ_QMAX, NET_IPQ_QMAX_NAME, &sysctl_maxlen,
	  sizeof(sysctl_maxlen), 0644,  NULL, proc_dointvec },
 	{ 0 }
};

static ctl_table ipq_dir_table[] = {
	{NET_IPV4, "ipv4", NULL, 0, 0555, ipq_table, 0, 0, 0, 0, 0},
	{ 0 }
};

static ctl_table ipq_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, ipq_dir_table, 0, 0, 0, 0, 0},
	{ 0 }
};

/****************************************************************************
 *
 * Procfs - debugging info.
 *
 ****************************************************************************/

static int
ipq_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len;

	spin_lock_bh(&nlq->lock);
	len = sprintf(buffer,
 	              "Thread pid        : %d\n"
 	              "Thread terminate  : %d\n"
 	              "Thread running    : %d\n"
	              "Peer pid          : %d\n"
	              "Peer died         : %d\n"
	              "Peer copy mode    : %d\n"
	              "Peer copy range   : %d\n"
	              "Queue length      : %d\n"
	              "Queue max. length : %d\n"
	              "Queue state       : %d\n",
 	              nlq->thread.pid,
 	              nlq->thread.terminate,
 	              nlq->thread.running,
	              nlq->peer.pid,
	              nlq->peer.died,
	              nlq->peer.copy_mode,
	              nlq->peer.copy_range,
	              nlq->len,
	              *nlq->maxlen,
	              nlq->state);
	spin_unlock_bh(&nlq->lock);
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	else if (len < 0)
		len = 0;
	return len;
}

/****************************************************************************
 *
 * Module stuff.
 *
 ****************************************************************************/

static int __init init(void)
{
	int status = 0;

	nfnl = netlink_kernel_create(NETLINK_FIREWALL, receive_user_sk);
	if (nfnl == NULL) {
		printk(KERN_ERR "%s: initialisation failed: unable to "
		       "create kernel netlink socket\n", IPQ_NAME);
		return -ENOMEM;
	}
	nlq = ipq_queue_create(receive_netfilter,
	                       netlink_send_peer, &status, &sysctl_maxlen);
	if (nlq == NULL) {
		printk(KERN_ERR "%s: initialisation failed: unable to "
		       "initialise queue\n", IPQ_NAME);
		sock_release(nfnl->socket);
		return status;
	}
	register_netdevice_notifier(&ipq_dev_notifier);
	proc_net_create(IPQ_PROC_FS_NAME, 0, ipq_get_info);
	ipq_sysctl_header = register_sysctl_table(ipq_root_table, 0);
	return status;
}

static void __exit fini(void)
{
	unregister_sysctl_table(ipq_sysctl_header);
	proc_net_remove(IPQ_PROC_FS_NAME);
	unregister_netdevice_notifier(&ipq_dev_notifier);
	ipq_queue_destroy(nlq);
	sock_release(nfnl->socket);
}

MODULE_DESCRIPTION("IPv4 packet queue handler");
module_init(init);
module_exit(fini);

