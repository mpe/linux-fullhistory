/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: ipoib_multicast.c 1362 2004-12-18 15:56:29Z roland $
 */

#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/igmp.h>
#include <linux/inetdevice.h>
#include <linux/delay.h>
#include <linux/completion.h>

#include "ipoib.h"

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
static int mcast_debug_level;

module_param(mcast_debug_level, int, 0644);
MODULE_PARM_DESC(mcast_debug_level,
		 "Enable multicast debug tracing if > 0");
#endif

static DECLARE_MUTEX(mcast_mutex);

/* Used for all multicast joins (broadcast, IPv4 mcast and IPv6 mcast) */
struct ipoib_mcast {
	struct ib_sa_mcmember_rec mcmember;
	struct ipoib_ah          *ah;

	struct rb_node    rb_node;
	struct list_head  list;
	struct completion done;

	int                 query_id;
	struct ib_sa_query *query;

	unsigned long created;
	unsigned long backoff;

	unsigned long flags;
	unsigned char logcount;

	struct list_head  neigh_list;

	struct sk_buff_head pkt_queue;

	struct net_device *dev;
};

struct ipoib_mcast_iter {
	struct net_device *dev;
	union ib_gid       mgid;
	unsigned long      created;
	unsigned int       queuelen;
	unsigned int       complete;
	unsigned int       send_only;
};

static void ipoib_mcast_free(struct ipoib_mcast *mcast)
{
	struct net_device *dev = mcast->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_neigh *neigh, *tmp;
	unsigned long flags;
	LIST_HEAD(ah_list);
	struct ipoib_ah *ah, *tah;

	ipoib_dbg_mcast(netdev_priv(dev),
			"deleting multicast group " IPOIB_GID_FMT "\n",
			IPOIB_GID_ARG(mcast->mcmember.mgid));

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each_entry_safe(neigh, tmp, &mcast->neigh_list, list) {
		if (neigh->ah)
			list_add_tail(&neigh->ah->list, &ah_list);
		*to_ipoib_neigh(neigh->neighbour) = NULL;
		neigh->neighbour->ops->destructor = NULL;
		kfree(neigh);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	list_for_each_entry_safe(ah, tah, &ah_list, list)
		ipoib_put_ah(ah);

	if (mcast->ah)
		ipoib_put_ah(mcast->ah);

	while (!skb_queue_empty(&mcast->pkt_queue)) {
		struct sk_buff *skb = skb_dequeue(&mcast->pkt_queue);

		skb->dev = dev;
		dev_kfree_skb_any(skb);
	}

	kfree(mcast);
}

static struct ipoib_mcast *ipoib_mcast_alloc(struct net_device *dev,
					     int can_sleep)
{
	struct ipoib_mcast *mcast;

	mcast = kmalloc(sizeof (*mcast), can_sleep ? GFP_KERNEL : GFP_ATOMIC);
	if (!mcast)
		return NULL;

	memset(mcast, 0, sizeof (*mcast));

	init_completion(&mcast->done);

	mcast->dev = dev;
	mcast->created = jiffies;
	mcast->backoff = HZ;
	mcast->logcount = 0;

	INIT_LIST_HEAD(&mcast->list);
	INIT_LIST_HEAD(&mcast->neigh_list);
	skb_queue_head_init(&mcast->pkt_queue);

	mcast->ah    = NULL;
	mcast->query = NULL;

	return mcast;
}

static struct ipoib_mcast *__ipoib_mcast_find(struct net_device *dev, union ib_gid *mgid)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct rb_node *n = priv->multicast_tree.rb_node;

	while (n) {
		struct ipoib_mcast *mcast;
		int ret;

		mcast = rb_entry(n, struct ipoib_mcast, rb_node);

		ret = memcmp(mgid->raw, mcast->mcmember.mgid.raw,
			     sizeof (union ib_gid));
		if (ret < 0)
			n = n->rb_left;
		else if (ret > 0)
			n = n->rb_right;
		else
			return mcast;
	}

	return NULL;
}

static int __ipoib_mcast_add(struct net_device *dev, struct ipoib_mcast *mcast)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct rb_node **n = &priv->multicast_tree.rb_node, *pn = NULL;

	while (*n) {
		struct ipoib_mcast *tmcast;
		int ret;

		pn = *n;
		tmcast = rb_entry(pn, struct ipoib_mcast, rb_node);

		ret = memcmp(mcast->mcmember.mgid.raw, tmcast->mcmember.mgid.raw,
			     sizeof (union ib_gid));
		if (ret < 0)
			n = &pn->rb_left;
		else if (ret > 0)
			n = &pn->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(&mcast->rb_node, pn, n);
	rb_insert_color(&mcast->rb_node, &priv->multicast_tree);

	return 0;
}

static int ipoib_mcast_join_finish(struct ipoib_mcast *mcast,
				   struct ib_sa_mcmember_rec *mcmember)
{
	struct net_device *dev = mcast->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int ret;

	mcast->mcmember = *mcmember;

	/* Set the cached Q_Key before we attach if it's the broadcast group */
	if (!memcmp(mcast->mcmember.mgid.raw, priv->dev->broadcast + 4,
		    sizeof (union ib_gid))) {
		priv->qkey = be32_to_cpu(priv->broadcast->mcmember.qkey);
		priv->tx_wr.wr.ud.remote_qkey = priv->qkey;
	}

	if (!test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
		if (test_and_set_bit(IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags)) {
			ipoib_warn(priv, "multicast group " IPOIB_GID_FMT
				   " already attached\n",
				   IPOIB_GID_ARG(mcast->mcmember.mgid));

			return 0;
		}

		ret = ipoib_mcast_attach(dev, be16_to_cpu(mcast->mcmember.mlid),
					 &mcast->mcmember.mgid);
		if (ret < 0) {
			ipoib_warn(priv, "couldn't attach QP to multicast group "
				   IPOIB_GID_FMT "\n",
				   IPOIB_GID_ARG(mcast->mcmember.mgid));

			clear_bit(IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags);
			return ret;
		}
	}

	{
		struct ib_ah_attr av = {
			.dlid	       = be16_to_cpu(mcast->mcmember.mlid),
			.port_num      = priv->port,
			.sl	       = mcast->mcmember.sl,
			.ah_flags      = IB_AH_GRH,
			.grh	       = {
				.flow_label    = be32_to_cpu(mcast->mcmember.flow_label),
				.hop_limit     = mcast->mcmember.hop_limit,
				.sgid_index    = 0,
				.traffic_class = mcast->mcmember.traffic_class
			}
		};

		av.grh.dgid = mcast->mcmember.mgid;

		if (ib_sa_rate_enum_to_int(mcast->mcmember.rate) > 0)
			av.static_rate = (2 * priv->local_rate -
					  ib_sa_rate_enum_to_int(mcast->mcmember.rate) - 1) /
				(priv->local_rate ? priv->local_rate : 1);

		ipoib_dbg_mcast(priv, "static_rate %d for local port %dX, mcmember %dX\n",
				av.static_rate, priv->local_rate,
				ib_sa_rate_enum_to_int(mcast->mcmember.rate));

		mcast->ah = ipoib_create_ah(dev, priv->pd, &av);
		if (!mcast->ah) {
			ipoib_warn(priv, "ib_address_create failed\n");
		} else {
			ipoib_dbg_mcast(priv, "MGID " IPOIB_GID_FMT
					" AV %p, LID 0x%04x, SL %d\n",
					IPOIB_GID_ARG(mcast->mcmember.mgid),
					mcast->ah->ah,
					be16_to_cpu(mcast->mcmember.mlid),
					mcast->mcmember.sl);
		}
	}

	/* actually send any queued packets */
	while (!skb_queue_empty(&mcast->pkt_queue)) {
		struct sk_buff *skb = skb_dequeue(&mcast->pkt_queue);

		skb->dev = dev;

		if (!skb->dst || !skb->dst->neighbour) {
			/* put pseudoheader back on for next time */
			skb_push(skb, sizeof (struct ipoib_pseudoheader));
		}

		if (dev_queue_xmit(skb))
			ipoib_warn(priv, "dev_queue_xmit failed to requeue packet\n");
	}

	return 0;
}

static void
ipoib_mcast_sendonly_join_complete(int status,
				   struct ib_sa_mcmember_rec *mcmember,
				   void *mcast_ptr)
{
	struct ipoib_mcast *mcast = mcast_ptr;
	struct net_device *dev = mcast->dev;

	if (!status)
		ipoib_mcast_join_finish(mcast, mcmember);
	else {
		if (mcast->logcount++ < 20)
			ipoib_dbg_mcast(netdev_priv(dev), "multicast join failed for "
					IPOIB_GID_FMT ", status %d\n",
					IPOIB_GID_ARG(mcast->mcmember.mgid), status);

		/* Flush out any queued packets */
		while (!skb_queue_empty(&mcast->pkt_queue)) {
			struct sk_buff *skb = skb_dequeue(&mcast->pkt_queue);

			skb->dev = dev;

			dev_kfree_skb_any(skb);
		}

		/* Clear the busy flag so we try again */
		clear_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags);
	}

	complete(&mcast->done);
}

static int ipoib_mcast_sendonly_join(struct ipoib_mcast *mcast)
{
	struct net_device *dev = mcast->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_sa_mcmember_rec rec = {
#if 0				/* Some SMs don't support send-only yet */
		.join_state = 4
#else
		.join_state = 1
#endif
	};
	int ret = 0;

	if (!test_bit(IPOIB_FLAG_OPER_UP, &priv->flags)) {
		ipoib_dbg_mcast(priv, "device shutting down, no multicast joins\n");
		return -ENODEV;
	}

	if (test_and_set_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags)) {
		ipoib_dbg_mcast(priv, "multicast entry busy, skipping\n");
		return -EBUSY;
	}

	rec.mgid     = mcast->mcmember.mgid;
	rec.port_gid = priv->local_gid;
	rec.pkey     = be16_to_cpu(priv->pkey);

	ret = ib_sa_mcmember_rec_set(priv->ca, priv->port, &rec,
				     IB_SA_MCMEMBER_REC_MGID		|
				     IB_SA_MCMEMBER_REC_PORT_GID	|
				     IB_SA_MCMEMBER_REC_PKEY		|
				     IB_SA_MCMEMBER_REC_JOIN_STATE,
				     1000, GFP_ATOMIC,
				     ipoib_mcast_sendonly_join_complete,
				     mcast, &mcast->query);
	if (ret < 0) {
		ipoib_warn(priv, "ib_sa_mcmember_rec_set failed (ret = %d)\n",
			   ret);
	} else {
		ipoib_dbg_mcast(priv, "no multicast record for " IPOIB_GID_FMT
				", starting join\n",
				IPOIB_GID_ARG(mcast->mcmember.mgid));

		mcast->query_id = ret;
	}

	return ret;
}

static void ipoib_mcast_join_complete(int status,
				      struct ib_sa_mcmember_rec *mcmember,
				      void *mcast_ptr)
{
	struct ipoib_mcast *mcast = mcast_ptr;
	struct net_device *dev = mcast->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg_mcast(priv, "join completion for " IPOIB_GID_FMT
			" (status %d)\n",
			IPOIB_GID_ARG(mcast->mcmember.mgid), status);

	if (!status && !ipoib_mcast_join_finish(mcast, mcmember)) {
		mcast->backoff = HZ;
		down(&mcast_mutex);
		if (test_bit(IPOIB_MCAST_RUN, &priv->flags))
			queue_work(ipoib_workqueue, &priv->mcast_task);
		up(&mcast_mutex);
		complete(&mcast->done);
		return;
	}

	if (status == -EINTR) {
		complete(&mcast->done);
		return;
	}

	if (status && mcast->logcount++ < 20) {
		if (status == -ETIMEDOUT || status == -EINTR) {
			ipoib_dbg_mcast(priv, "multicast join failed for " IPOIB_GID_FMT
					", status %d\n",
					IPOIB_GID_ARG(mcast->mcmember.mgid),
					status);
		} else {
			ipoib_warn(priv, "multicast join failed for "
				   IPOIB_GID_FMT ", status %d\n",
				   IPOIB_GID_ARG(mcast->mcmember.mgid),
				   status);
		}
	}

	mcast->backoff *= 2;
	if (mcast->backoff > IPOIB_MAX_BACKOFF_SECONDS)
		mcast->backoff = IPOIB_MAX_BACKOFF_SECONDS;

	mcast->query = NULL;

	down(&mcast_mutex);
	if (test_bit(IPOIB_MCAST_RUN, &priv->flags)) {
		if (status == -ETIMEDOUT)
			queue_work(ipoib_workqueue, &priv->mcast_task);
		else
			queue_delayed_work(ipoib_workqueue, &priv->mcast_task,
					   mcast->backoff * HZ);
	} else
		complete(&mcast->done);
	up(&mcast_mutex);

	return;
}

static void ipoib_mcast_join(struct net_device *dev, struct ipoib_mcast *mcast,
			     int create)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_sa_mcmember_rec rec = {
		.join_state = 1
	};
	ib_sa_comp_mask comp_mask;
	int ret = 0;

	ipoib_dbg_mcast(priv, "joining MGID " IPOIB_GID_FMT "\n",
			IPOIB_GID_ARG(mcast->mcmember.mgid));

	rec.mgid     = mcast->mcmember.mgid;
	rec.port_gid = priv->local_gid;
	rec.pkey     = be16_to_cpu(priv->pkey);

	comp_mask =
		IB_SA_MCMEMBER_REC_MGID		|
		IB_SA_MCMEMBER_REC_PORT_GID	|
		IB_SA_MCMEMBER_REC_PKEY		|
		IB_SA_MCMEMBER_REC_JOIN_STATE;

	if (create) {
		comp_mask |=
			IB_SA_MCMEMBER_REC_QKEY		|
			IB_SA_MCMEMBER_REC_SL		|
			IB_SA_MCMEMBER_REC_FLOW_LABEL	|
			IB_SA_MCMEMBER_REC_TRAFFIC_CLASS;

		rec.qkey	  = priv->broadcast->mcmember.qkey;
		rec.sl		  = priv->broadcast->mcmember.sl;
		rec.flow_label	  = priv->broadcast->mcmember.flow_label;
		rec.traffic_class = priv->broadcast->mcmember.traffic_class;
	}

	ret = ib_sa_mcmember_rec_set(priv->ca, priv->port, &rec, comp_mask,
				     mcast->backoff * 1000, GFP_ATOMIC,
				     ipoib_mcast_join_complete,
				     mcast, &mcast->query);

	if (ret < 0) {
		ipoib_warn(priv, "ib_sa_mcmember_rec_set failed, status %d\n", ret);

		mcast->backoff *= 2;
		if (mcast->backoff > IPOIB_MAX_BACKOFF_SECONDS)
			mcast->backoff = IPOIB_MAX_BACKOFF_SECONDS;

		down(&mcast_mutex);
		if (test_bit(IPOIB_MCAST_RUN, &priv->flags))
			queue_delayed_work(ipoib_workqueue,
					   &priv->mcast_task,
					   mcast->backoff);
		up(&mcast_mutex);
	} else
		mcast->query_id = ret;
}

void ipoib_mcast_join_task(void *dev_ptr)
{
	struct net_device *dev = dev_ptr;
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	if (!test_bit(IPOIB_MCAST_RUN, &priv->flags))
		return;

	if (ib_query_gid(priv->ca, priv->port, 0, &priv->local_gid))
		ipoib_warn(priv, "ib_gid_entry_get() failed\n");
	else
		memcpy(priv->dev->dev_addr + 4, priv->local_gid.raw, sizeof (union ib_gid));

	{
		struct ib_port_attr attr;

		if (!ib_query_port(priv->ca, priv->port, &attr)) {
			priv->local_lid  = attr.lid;
			priv->local_rate = attr.active_speed *
				ib_width_enum_to_int(attr.active_width);
		} else
			ipoib_warn(priv, "ib_query_port failed\n");
	}

	if (!priv->broadcast) {
		priv->broadcast = ipoib_mcast_alloc(dev, 1);
		if (!priv->broadcast) {
			ipoib_warn(priv, "failed to allocate broadcast group\n");
			down(&mcast_mutex);
			if (test_bit(IPOIB_MCAST_RUN, &priv->flags))
				queue_delayed_work(ipoib_workqueue,
						   &priv->mcast_task, HZ);
			up(&mcast_mutex);
			return;
		}

		memcpy(priv->broadcast->mcmember.mgid.raw, priv->dev->broadcast + 4,
		       sizeof (union ib_gid));

		spin_lock_irq(&priv->lock);
		__ipoib_mcast_add(dev, priv->broadcast);
		spin_unlock_irq(&priv->lock);
	}

	if (!test_bit(IPOIB_MCAST_FLAG_ATTACHED, &priv->broadcast->flags)) {
		ipoib_mcast_join(dev, priv->broadcast, 0);
		return;
	}

	while (1) {
		struct ipoib_mcast *mcast = NULL;

		spin_lock_irq(&priv->lock);
		list_for_each_entry(mcast, &priv->multicast_list, list) {
			if (!test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)
			    && !test_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags)
			    && !test_bit(IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags)) {
				/* Found the next unjoined group */
				break;
			}
		}
		spin_unlock_irq(&priv->lock);

		if (&mcast->list == &priv->multicast_list) {
			/* All done */
			break;
		}

		ipoib_mcast_join(dev, mcast, 1);
		return;
	}

	priv->mcast_mtu = ib_mtu_enum_to_int(priv->broadcast->mcmember.mtu) -
		IPOIB_ENCAP_LEN;
	dev->mtu = min(priv->mcast_mtu, priv->admin_mtu);

	ipoib_dbg_mcast(priv, "successfully joined all multicast groups\n");

	clear_bit(IPOIB_MCAST_RUN, &priv->flags);
	netif_carrier_on(dev);
}

int ipoib_mcast_start_thread(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg_mcast(priv, "starting multicast thread\n");

	down(&mcast_mutex);
	if (!test_and_set_bit(IPOIB_MCAST_RUN, &priv->flags))
		queue_work(ipoib_workqueue, &priv->mcast_task);
	up(&mcast_mutex);

	return 0;
}

int ipoib_mcast_stop_thread(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_mcast *mcast;

	ipoib_dbg_mcast(priv, "stopping multicast thread\n");

	down(&mcast_mutex);
	clear_bit(IPOIB_MCAST_RUN, &priv->flags);
	cancel_delayed_work(&priv->mcast_task);
	up(&mcast_mutex);

	flush_workqueue(ipoib_workqueue);

	if (priv->broadcast && priv->broadcast->query) {
		ib_sa_cancel_query(priv->broadcast->query_id, priv->broadcast->query);
		priv->broadcast->query = NULL;
		ipoib_dbg_mcast(priv, "waiting for bcast\n");
		wait_for_completion(&priv->broadcast->done);
	}

	list_for_each_entry(mcast, &priv->multicast_list, list) {
		if (mcast->query) {
			ib_sa_cancel_query(mcast->query_id, mcast->query);
			mcast->query = NULL;
			ipoib_dbg_mcast(priv, "waiting for MGID " IPOIB_GID_FMT "\n",
					IPOIB_GID_ARG(mcast->mcmember.mgid));
			wait_for_completion(&mcast->done);
		}
	}

	return 0;
}

static int ipoib_mcast_leave(struct net_device *dev, struct ipoib_mcast *mcast)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_sa_mcmember_rec rec = {
		.join_state = 1
	};
	int ret = 0;

	if (!test_and_clear_bit(IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags))
		return 0;

	ipoib_dbg_mcast(priv, "leaving MGID " IPOIB_GID_FMT "\n",
			IPOIB_GID_ARG(mcast->mcmember.mgid));

	rec.mgid     = mcast->mcmember.mgid;
	rec.port_gid = priv->local_gid;
	rec.pkey     = be16_to_cpu(priv->pkey);

	/* Remove ourselves from the multicast group */
	ret = ipoib_mcast_detach(dev, be16_to_cpu(mcast->mcmember.mlid),
				 &mcast->mcmember.mgid);
	if (ret)
		ipoib_warn(priv, "ipoib_mcast_detach failed (result = %d)\n", ret);

	/*
	 * Just make one shot at leaving and don't wait for a reply;
	 * if we fail, too bad.
	 */
	ret = ib_sa_mcmember_rec_delete(priv->ca, priv->port, &rec,
					IB_SA_MCMEMBER_REC_MGID		|
					IB_SA_MCMEMBER_REC_PORT_GID	|
					IB_SA_MCMEMBER_REC_PKEY		|
					IB_SA_MCMEMBER_REC_JOIN_STATE,
					0, GFP_ATOMIC, NULL,
					mcast, &mcast->query);
	if (ret < 0)
		ipoib_warn(priv, "ib_sa_mcmember_rec_delete failed "
			   "for leave (result = %d)\n", ret);

	return 0;
}

void ipoib_mcast_send(struct net_device *dev, union ib_gid *mgid,
		      struct sk_buff *skb)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_mcast *mcast;

	/*
	 * We can only be called from ipoib_start_xmit, so we're
	 * inside tx_lock -- no need to save/restore flags.
	 */
	spin_lock(&priv->lock);

	mcast = __ipoib_mcast_find(dev, mgid);
	if (!mcast) {
		/* Let's create a new send only group now */
		ipoib_dbg_mcast(priv, "setting up send only multicast group for "
				IPOIB_GID_FMT "\n", IPOIB_GID_ARG(*mgid));

		mcast = ipoib_mcast_alloc(dev, 0);
		if (!mcast) {
			ipoib_warn(priv, "unable to allocate memory for "
				   "multicast structure\n");
			dev_kfree_skb_any(skb);
			goto out;
		}

		set_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags);
		mcast->mcmember.mgid = *mgid;
		__ipoib_mcast_add(dev, mcast);
		list_add_tail(&mcast->list, &priv->multicast_list);
	}

	if (!mcast->ah) {
		if (skb_queue_len(&mcast->pkt_queue) < IPOIB_MAX_MCAST_QUEUE)
			skb_queue_tail(&mcast->pkt_queue, skb);
		else
			dev_kfree_skb_any(skb);

		if (mcast->query)
			ipoib_dbg_mcast(priv, "no address vector, "
					"but multicast join already started\n");
		else if (test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags))
			ipoib_mcast_sendonly_join(mcast);

		/*
		 * If lookup completes between here and out:, don't
		 * want to send packet twice.
		 */
		mcast = NULL;
	}

out:
	if (mcast && mcast->ah) {
		if (skb->dst            &&
		    skb->dst->neighbour &&
		    !*to_ipoib_neigh(skb->dst->neighbour)) {
			struct ipoib_neigh *neigh = kmalloc(sizeof *neigh, GFP_ATOMIC);

			if (neigh) {
				kref_get(&mcast->ah->ref);
				neigh->ah  	= mcast->ah;
				neigh->neighbour = skb->dst->neighbour;
				*to_ipoib_neigh(skb->dst->neighbour) = neigh;
				list_add_tail(&neigh->list, &mcast->neigh_list);
			}
		}

		ipoib_send(dev, skb, mcast->ah, IB_MULTICAST_QPN);
	}

	spin_unlock(&priv->lock);
}

void ipoib_mcast_dev_flush(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	LIST_HEAD(remove_list);
	struct ipoib_mcast *mcast, *tmcast, *nmcast;
	unsigned long flags;

	ipoib_dbg_mcast(priv, "flushing multicast list\n");

	spin_lock_irqsave(&priv->lock, flags);
	list_for_each_entry_safe(mcast, tmcast, &priv->multicast_list, list) {
		nmcast = ipoib_mcast_alloc(dev, 0);
		if (nmcast) {
			nmcast->flags =
				mcast->flags & (1 << IPOIB_MCAST_FLAG_SENDONLY);

			nmcast->mcmember.mgid = mcast->mcmember.mgid;

			/* Add the new group in before the to-be-destroyed group */
			list_add_tail(&nmcast->list, &mcast->list);
			list_del_init(&mcast->list);

			rb_replace_node(&mcast->rb_node, &nmcast->rb_node,
					&priv->multicast_tree);

			list_add_tail(&mcast->list, &remove_list);
		} else {
			ipoib_warn(priv, "could not reallocate multicast group "
				   IPOIB_GID_FMT "\n",
				   IPOIB_GID_ARG(mcast->mcmember.mgid));
		}
	}

	if (priv->broadcast) {
		nmcast = ipoib_mcast_alloc(dev, 0);
		if (nmcast) {
			nmcast->mcmember.mgid = priv->broadcast->mcmember.mgid;

			rb_replace_node(&priv->broadcast->rb_node,
					&nmcast->rb_node,
					&priv->multicast_tree);

			list_add_tail(&priv->broadcast->list, &remove_list);
		}

		priv->broadcast = nmcast;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	list_for_each_entry_safe(mcast, tmcast, &remove_list, list) {
		ipoib_mcast_leave(dev, mcast);
		ipoib_mcast_free(mcast);
	}
}

void ipoib_mcast_dev_down(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	unsigned long flags;

	/* Delete broadcast since it will be recreated */
	if (priv->broadcast) {
		ipoib_dbg_mcast(priv, "deleting broadcast group\n");

		spin_lock_irqsave(&priv->lock, flags);
		rb_erase(&priv->broadcast->rb_node, &priv->multicast_tree);
		spin_unlock_irqrestore(&priv->lock, flags);
		ipoib_mcast_leave(dev, priv->broadcast);
		ipoib_mcast_free(priv->broadcast);
		priv->broadcast = NULL;
	}
}

void ipoib_mcast_restart_task(void *dev_ptr)
{
	struct net_device *dev = dev_ptr;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct dev_mc_list *mclist;
	struct ipoib_mcast *mcast, *tmcast;
	LIST_HEAD(remove_list);
	unsigned long flags;

	ipoib_dbg_mcast(priv, "restarting multicast task\n");

	ipoib_mcast_stop_thread(dev);

	spin_lock_irqsave(&priv->lock, flags);

	/*
	 * Unfortunately, the networking core only gives us a list of all of
	 * the multicast hardware addresses. We need to figure out which ones
	 * are new and which ones have been removed
	 */

	/* Clear out the found flag */
	list_for_each_entry(mcast, &priv->multicast_list, list)
		clear_bit(IPOIB_MCAST_FLAG_FOUND, &mcast->flags);

	/* Mark all of the entries that are found or don't exist */
	for (mclist = dev->mc_list; mclist; mclist = mclist->next) {
		union ib_gid mgid;

		memcpy(mgid.raw, mclist->dmi_addr + 4, sizeof mgid);

		/* Add in the P_Key */
		mgid.raw[4] = (priv->pkey >> 8) & 0xff;
		mgid.raw[5] = priv->pkey & 0xff;

		mcast = __ipoib_mcast_find(dev, &mgid);
		if (!mcast || test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
			struct ipoib_mcast *nmcast;

			/* Not found or send-only group, let's add a new entry */
			ipoib_dbg_mcast(priv, "adding multicast entry for mgid "
					IPOIB_GID_FMT "\n", IPOIB_GID_ARG(mgid));

			nmcast = ipoib_mcast_alloc(dev, 0);
			if (!nmcast) {
				ipoib_warn(priv, "unable to allocate memory for multicast structure\n");
				continue;
			}

			set_bit(IPOIB_MCAST_FLAG_FOUND, &nmcast->flags);

			nmcast->mcmember.mgid = mgid;

			if (mcast) {
				/* Destroy the send only entry */
				list_del(&mcast->list);
				list_add_tail(&mcast->list, &remove_list);

				rb_replace_node(&mcast->rb_node,
						&nmcast->rb_node,
						&priv->multicast_tree);
			} else
				__ipoib_mcast_add(dev, nmcast);

			list_add_tail(&nmcast->list, &priv->multicast_list);
		}

		if (mcast)
			set_bit(IPOIB_MCAST_FLAG_FOUND, &mcast->flags);
	}

	/* Remove all of the entries don't exist anymore */
	list_for_each_entry_safe(mcast, tmcast, &priv->multicast_list, list) {
		if (!test_bit(IPOIB_MCAST_FLAG_FOUND, &mcast->flags) &&
		    !test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
			ipoib_dbg_mcast(priv, "deleting multicast group " IPOIB_GID_FMT "\n",
					IPOIB_GID_ARG(mcast->mcmember.mgid));

			rb_erase(&mcast->rb_node, &priv->multicast_tree);

			/* Move to the remove list */
			list_del(&mcast->list);
			list_add_tail(&mcast->list, &remove_list);
		}
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	/* We have to cancel outside of the spinlock */
	list_for_each_entry_safe(mcast, tmcast, &remove_list, list) {
		ipoib_mcast_leave(mcast->dev, mcast);
		ipoib_mcast_free(mcast);
	}

	if (test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
		ipoib_mcast_start_thread(dev);
}

struct ipoib_mcast_iter *ipoib_mcast_iter_init(struct net_device *dev)
{
	struct ipoib_mcast_iter *iter;

	iter = kmalloc(sizeof *iter, GFP_KERNEL);
	if (!iter)
		return NULL;

	iter->dev = dev;
	memset(iter->mgid.raw, 0, sizeof iter->mgid);

	if (ipoib_mcast_iter_next(iter)) {
		ipoib_mcast_iter_free(iter);
		return NULL;
	}

	return iter;
}

void ipoib_mcast_iter_free(struct ipoib_mcast_iter *iter)
{
	kfree(iter);
}

int ipoib_mcast_iter_next(struct ipoib_mcast_iter *iter)
{
	struct ipoib_dev_priv *priv = netdev_priv(iter->dev);
	struct rb_node *n;
	struct ipoib_mcast *mcast;
	int ret = 1;

	spin_lock_irq(&priv->lock);

	n = rb_first(&priv->multicast_tree);

	while (n) {
		mcast = rb_entry(n, struct ipoib_mcast, rb_node);

		if (memcmp(iter->mgid.raw, mcast->mcmember.mgid.raw,
			   sizeof (union ib_gid)) < 0) {
			iter->mgid      = mcast->mcmember.mgid;
			iter->created   = mcast->created;
			iter->queuelen  = skb_queue_len(&mcast->pkt_queue);
			iter->complete  = !!mcast->ah;
			iter->send_only = !!(mcast->flags & (1 << IPOIB_MCAST_FLAG_SENDONLY));

			ret = 0;

			break;
		}

		n = rb_next(n);
	}

	spin_unlock_irq(&priv->lock);

	return ret;
}

void ipoib_mcast_iter_read(struct ipoib_mcast_iter *iter,
			   union ib_gid *mgid,
			   unsigned long *created,
			   unsigned int *queuelen,
			   unsigned int *complete,
			   unsigned int *send_only)
{
	*mgid      = iter->mgid;
	*created   = iter->created;
	*queuelen  = iter->queuelen;
	*complete  = iter->complete;
	*send_only = iter->send_only;
}
