/*********************************************************************
 *                
 * Filename:      af_irda.c
 * Version:       0.6
 * Description:   IrDA sockets implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun May 31 10:12:43 1998
 * Modified at:   Wed May 19 16:12:06 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       af_netroom.c, af_ax25.c, af_rose.c, af_x25.c etc.
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/init.h>
#include <linux/if_arp.h>
#include <linux/net.h>
#include <linux/irda.h>
#include <linux/poll.h>

#include <asm/uaccess.h>

#include <net/sock.h>

#include <net/irda/irda.h>
#include <net/irda/iriap.h>
#include <net/irda/irias_object.h>
#include <net/irda/irttp.h>
#include <net/irda/discovery.h>

extern int  irda_init(void);
extern void irda_cleanup(void);
extern int  irlap_driver_rcv(struct sk_buff *, struct net_device *, 
			     struct packet_type *);

static struct proto_ops irda_stream_ops;
static struct proto_ops irda_dgram_ops;
static hashbin_t *cachelog = NULL;
static DECLARE_WAIT_QUEUE_HEAD(discovery_wait); /* Wait for discovery */

#define IRDA_MAX_HEADER (TTP_MAX_HEADER)

/*
 * Function irda_data_indication (instance, sap, skb)
 *
 *    Received some data from TinyTP. Just queue it on the receive queue
 *
 */
static int irda_data_indication(void *instance, void *sap, struct sk_buff *skb)
{
	struct irda_sock *self;
	struct sock *sk;
	int err;

	DEBUG(1, __FUNCTION__ "()\n");

	self = (struct irda_sock *) instance;
	ASSERT(self != NULL, return -1;);

	sk = self->sk;
	ASSERT(sk != NULL, return -1;);

	err = sock_queue_rcv_skb(sk, skb);
	if (err) {
		DEBUG(1, __FUNCTION__ "(), error: no more mem!\n");
		self->rx_flow = FLOW_STOP;

		/* When we return error, TTP will need to requeue the skb */
		return err;
	}

	return 0;
}

/*
 * Function irda_disconnect_indication (instance, sap, reason, skb)
 *
 *    Connection has been closed. Chech reason to find out why
 *
 */
static void irda_disconnect_indication(void *instance, void *sap, 
				       LM_REASON reason, struct sk_buff *skb)
{
	struct irda_sock *self;
	struct sock *sk;

	DEBUG(1, __FUNCTION__ "()\n");

	self = (struct irda_sock *) instance;

	sk = self->sk;
	if (sk == NULL)
		return;

	sk->state     = TCP_CLOSE;
        sk->err       = reason;
        sk->shutdown |= SEND_SHUTDOWN;
	if (!sk->dead) {
		sk->state_change(sk);
                sk->dead = 1;
        }
}

/*
 * Function irda_connect_confirm (instance, sap, qos, max_sdu_size, skb)
 *
 *    Connections has been confirmed by the remote device
 *
 */
static void irda_connect_confirm(void *instance, void *sap, 
				 struct qos_info *qos,
				 __u32 max_sdu_size, __u8 max_header_size, 
				 struct sk_buff *skb)
{
	struct irda_sock *self;
	struct sock *sk;

	DEBUG(1, __FUNCTION__ "()\n");

	self = (struct irda_sock *) instance;

	/* How much header space do we need to reserve */
	self->max_header_size = max_header_size;

	/* IrTTP max SDU size in transmit direction */
	self->max_sdu_size_tx = max_sdu_size;

	/* Find out what the largest chunk of data that we can transmit is */
	if (max_sdu_size == SAR_DISABLE)
		self->max_data_size = qos->data_size.value - max_header_size;
	else
		self->max_data_size = max_sdu_size;

	DEBUG(1, __FUNCTION__ "(), max_data_size=%d\n", self->max_data_size);

	memcpy(&self->qos_tx, qos, sizeof(struct qos_info));

	sk = self->sk;
	if (sk == NULL)
		return;

	skb_queue_tail(&sk->receive_queue, skb);

	/* We are now connected! */
	sk->state = TCP_ESTABLISHED;
	sk->state_change(sk);
}

/*
 * Function irda_connect_indication(instance, sap, qos, max_sdu_size, userdata)
 *
 *    Incomming connection
 *
 */
static void irda_connect_indication(void *instance, void *sap, 
				    struct qos_info *qos, __u32 max_sdu_size,
				    __u8 max_header_size, struct sk_buff *skb)
{
	struct irda_sock *self;
	struct sock *sk;

	DEBUG(1, __FUNCTION__ "()\n");

	self = (struct irda_sock *) instance;

	/* How much header space do we need to reserve */
	self->max_header_size = max_header_size;

	/* IrTTP max SDU size in transmit direction */
	self->max_sdu_size_tx = max_sdu_size;	

	/* Find out what the largest chunk of data that we can transmit is */
	if (max_sdu_size == SAR_DISABLE)
		self->max_data_size = qos->data_size.value - max_header_size;
	else
		self->max_data_size = max_sdu_size;

	DEBUG(1, __FUNCTION__ "(), max_data_size=%d\n", self->max_data_size);

	memcpy(&self->qos_tx, qos, sizeof(struct qos_info));

	sk = self->sk;
	if (sk == NULL)
		return;
	
	skb_queue_tail(&sk->receive_queue, skb);

	sk->state_change(sk);
}

/*
 * Function irda_connect_response (handle)
 *
 *    Accept incomming connection
 *
 */
void irda_connect_response(struct irda_sock *self)
{
	struct sk_buff *skb;

	DEBUG(1, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);

	skb = dev_alloc_skb(64);
	if (skb == NULL) {
		DEBUG(0, __FUNCTION__ "() Unable to allocate sk_buff!\n");
		return;
	}

	/* Reserve space for MUX_CONTROL and LAP header */
	skb_reserve(skb, IRDA_MAX_HEADER);

	irttp_connect_response(self->tsap, self->max_sdu_size_rx, skb);
}


/*
 * Function irda_flow_indication (instance, sap, flow)
 *
 *    Used by TinyTP to tell us if it can accept more data or not
 *
 */
static void irda_flow_indication(void *instance, void *sap, LOCAL_FLOW flow) 
{
	struct irda_sock *self;
	struct sock *sk;

	DEBUG(1, __FUNCTION__ "()\n");
	
	self = (struct irda_sock *) instance;
	ASSERT(self != NULL, return;);

	sk = self->sk;
	ASSERT(sk != NULL, return;);
	
	switch (flow) {
	case FLOW_STOP:
		DEBUG(1, __FUNCTION__ "(), IrTTP wants us to slow down\n");
		self->tx_flow = flow;
		break;
	case FLOW_START:
		self->tx_flow = flow;
		DEBUG(1, __FUNCTION__ "(), IrTTP wants us to start again\n");
		wake_up_interruptible(sk->sleep);
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), Unknown flow command!\n");
	}
}

/*
 * Function irda_get_value_confirm (obj_id, value, priv)
 *
 *    Got answer from remote LM-IAS
 *
 */
static void irda_get_value_confirm(int result, __u16 obj_id, 
				   struct ias_value *value, void *priv)
{
	struct irda_sock *self;
	
	DEBUG(1, __FUNCTION__ "()\n");

	ASSERT(priv != NULL, return;);
	self = (struct irda_sock *) priv;
	
	if (!self)
		return;

	/* Check if request succeeded */
	if (result != IAS_SUCCESS) {
		DEBUG(0, __FUNCTION__ "(), IAS query failed!\n");

		self->errno = result;

		/* Wake up any processes waiting for result */
		wake_up_interruptible(&self->ias_wait);

		return;
	}

	switch (value->type) {
	case IAS_INTEGER:
		DEBUG(4, __FUNCTION__ "() int=%d\n", value->t.integer);
		
		if (value->t.integer != -1) {
			self->dtsap_sel = value->t.integer;
		} else 
			self->dtsap_sel = 0;
		break;
	default:
		DEBUG(0, __FUNCTION__ "(), bad type!\n");
		break;
	}
	/* Wake up any processes waiting for result */
	wake_up_interruptible(&self->ias_wait);
}

/*
 * Function irda_discovery_indication (log)
 *
 *    Got a discovery log from IrLMP, wake ut any process waiting for answer
 *
 */
static void irda_discovery_indication(hashbin_t *log)
{
	DEBUG(1, __FUNCTION__ "()\n");

	cachelog = log;

	/* Wake up process if its waiting for device to be discovered */
	wake_up_interruptible(&discovery_wait);
}

/*
 * Function irda_open_tsap (self)
 *
 *    Open local Transport Service Access Point (TSAP)
 *
 */
static int irda_open_tsap(struct irda_sock *self, __u8 tsap_sel, char *name)
{
	struct notify_t notify;
	
	DEBUG(1, __FUNCTION__ "()\n");

	/* Initialize callbacks to be used by the IrDA stack */
	irda_notify_init(&notify);
	notify.connect_confirm       = irda_connect_confirm;
	notify.connect_indication    = irda_connect_indication;
	notify.disconnect_indication = irda_disconnect_indication;
	notify.data_indication       = irda_data_indication;
	notify.flow_indication       = irda_flow_indication;
	notify.instance = self;
	strncpy(notify.name, name, NOTIFY_MAX_NAME);

	self->tsap = irttp_open_tsap(tsap_sel, DEFAULT_INITIAL_CREDIT,
				     &notify);	
	if (self->tsap == NULL) {
		DEBUG( 0, __FUNCTION__ "(), Unable to allocate TSAP!\n");
		return -1;
	}
	/* Remember which TSAP selector we actually got */
	self->stsap_sel = self->tsap->stsap_sel;

	return 0;
}

/*
 * Function irda_find_lsap_sel (self, name)
 *
 *    Try to lookup LSAP selector in remote LM-IAS
 *
 */
static int irda_find_lsap_sel(struct irda_sock *self, char *name)
{
	DEBUG(1, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return -1;);

	/* Query remote LM-IAS */
	iriap_getvaluebyclass_request(name, "IrDA:TinyTP:LsapSel",
				      self->saddr, self->daddr,
				      irda_get_value_confirm, self);
	/* Wait for answer */
	interruptible_sleep_on(&self->ias_wait);

	if (self->dtsap_sel)
		return 0;

	return -ENETUNREACH; /* May not be true */
}

/*
 * Function irda_getname (sock, uaddr, uaddr_len, peer)
 *
 *    Return the our own, or peers socket address (sockaddr_irda)
 *
 */
static int irda_getname(struct socket *sock, struct sockaddr *uaddr,
			int *uaddr_len, int peer)
{
	struct sockaddr_irda saddr;
	struct sock *sk = sock->sk;

	if (peer) {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		
		saddr.sir_family = AF_IRDA;
		saddr.sir_lsap_sel = sk->protinfo.irda->dtsap_sel;
		saddr.sir_addr = sk->protinfo.irda->daddr;
	} else {
		saddr.sir_family = AF_IRDA;
		saddr.sir_lsap_sel = sk->protinfo.irda->stsap_sel;
		saddr.sir_addr = sk->protinfo.irda->saddr;
	}
	
	DEBUG(1, __FUNCTION__ "(), tsap_sel = %#x\n", saddr.sir_lsap_sel);
	DEBUG(1, __FUNCTION__ "(), addr = %08x\n", saddr.sir_addr);

	if (*uaddr_len > sizeof (struct sockaddr_irda))
		*uaddr_len = sizeof (struct sockaddr_irda);
	memcpy(uaddr, &saddr, *uaddr_len);

	return 0;
}

/*
 * Function irda_listen (sock, backlog)
 *
 *    Just move to the listen state
 *
 */
static int irda_listen( struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;

	DEBUG(1, __FUNCTION__ "()\n");

	if (sk->type == SOCK_STREAM && sk->state != TCP_LISTEN) {
		sk->max_ack_backlog = backlog;
		sk->state           = TCP_LISTEN;

		return 0;
	}
	
	return -EOPNOTSUPP;
}

/*
 * Function irda_bind (sock, uaddr, addr_len)
 *
 *    Used by servers to register their well known TSAP
 *
 */
static int irda_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_irda *addr = (struct sockaddr_irda *) uaddr;
	struct irda_sock *self;
	__u16 hints = 0;
	int err;

	DEBUG(1, __FUNCTION__ "()\n");

	self = sk->protinfo.irda;
	ASSERT(self != NULL, return -1;);

	if ((addr_len < sizeof(struct sockaddr_irda)) || 
	    (addr_len > sizeof(struct sockaddr_irda)))
		return -EINVAL;

	err = irda_open_tsap(self, addr->sir_lsap_sel, addr->sir_name);
	if (err < 0)
		return -ENOMEM;
	
	/*  Register with LM-IAS */
	self->ias_obj = irias_new_object(addr->sir_name, jiffies);
	irias_add_integer_attrib(self->ias_obj, "IrDA:TinyTP:LsapSel", 
				 self->stsap_sel);
	irias_insert_object(self->ias_obj);

	/* Fill in some default hint bits values */
	if (strncmp(addr->sir_name, "OBEX", 4) == 0)
		hints = irlmp_service_to_hint(S_OBEX);
	
	if (hints)
		self->skey = irlmp_register_service(hints);

	return 0;
}

/*
 * Function irda_accept (sock, newsock, flags)
 *
 *    Wait for incomming connection
 *
 */
static int irda_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct irda_sock *self, *new;
	struct sock *sk = sock->sk;
	struct sock *newsk;
	struct sk_buff *skb;
	int err;

	self = sk->protinfo.irda;
	ASSERT(self != NULL, return -1;);

	err = irda_create(newsock, sk->protocol);
	if (err)
		return err;

	if (sock->state != SS_UNCONNECTED)
		return -EINVAL;

	if ((sk = sock->sk) == NULL)
		return -EINVAL;

	if (sk->type != SOCK_STREAM)
		return -EOPNOTSUPP;

	if (sk->state != TCP_LISTEN)
		return -EINVAL;

	/*
	 *	The read queue this time is holding sockets ready to use
	 *	hooked into the SABM we saved
	 */
	do {
		if ((skb = skb_dequeue(&sk->receive_queue)) == NULL) {
			if (flags & O_NONBLOCK)
				return -EWOULDBLOCK;

			interruptible_sleep_on(sk->sleep);
			if (signal_pending(current)) 
				return -ERESTARTSYS;
		}
	} while (skb == NULL);

 	newsk = newsock->sk;
	newsk->state = TCP_ESTABLISHED;

	new = newsk->protinfo.irda;
	ASSERT(new != NULL, return -1;);

	/* Now attach up the new socket */
	new->tsap = irttp_dup(self->tsap, new);
	if (!new->tsap) {
		DEBUG(0, __FUNCTION__ "(), dup failed!\n");
		return -1;
	}
		
	new->stsap_sel = new->tsap->stsap_sel;
	new->dtsap_sel = new->tsap->dtsap_sel;
	new->saddr = irttp_get_saddr(new->tsap);
	new->daddr = irttp_get_daddr(new->tsap);

	new->max_sdu_size_tx = self->max_sdu_size_tx;
	new->max_sdu_size_rx = self->max_sdu_size_rx;
	new->max_data_size   = self->max_data_size;
	new->max_header_size = self->max_header_size;

	memcpy(&new->qos_tx, &self->qos_tx, sizeof(struct qos_info));

	/* Clean up the original one to keep it in listen state */
	self->tsap->dtsap_sel = self->tsap->lsap->dlsap_sel = LSAP_ANY;
	self->tsap->lsap->lsap_state = LSAP_DISCONNECTED;

	skb->sk = NULL;
	skb->destructor = NULL;
	kfree_skb(skb);
	sk->ack_backlog--;

	newsock->state = SS_CONNECTED;

	irda_connect_response(new);

	return 0;
}

/*
 * Function irda_connect (sock, uaddr, addr_len, flags)
 *
 *    Connect to a IrDA device
 *
 */
static int irda_connect(struct socket *sock, struct sockaddr *uaddr,
			int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_irda *addr = (struct sockaddr_irda *) uaddr;
	struct irda_sock *self;
	int err;

	self = sk->protinfo.irda;

	DEBUG(1, __FUNCTION__ "()\n");

	if (sk->state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		sock->state = SS_CONNECTED;
		return 0;   /* Connect completed during a ERESTARTSYS event */
	}
	
	if (sk->state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		return -ECONNREFUSED;
	}
	
	if (sk->state == TCP_ESTABLISHED)
		return -EISCONN;      /* No reconnect on a seqpacket socket */
	
	sk->state   = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	if (addr_len != sizeof(struct sockaddr_irda))
		return -EINVAL;

	/* Check if user supplied the required destination device address */
	if (!addr->sir_addr)
		return -EINVAL;

	self->daddr = addr->sir_addr;
	DEBUG(1, __FUNCTION__ "(), daddr = %08x\n", self->daddr);

	/* Query remote LM-IAS */
	err = irda_find_lsap_sel(self, addr->sir_name);
	if (err) {
		DEBUG(0, __FUNCTION__ "(), connect failed!\n");
		return err;
	}

	/* Check if we have opened a local TSAP */
	if (!self->tsap)
		irda_open_tsap(self, LSAP_ANY, addr->sir_name);
	
	/* Move to connecting socket, start sending Connect Requests */
	sock->state = SS_CONNECTING;
	sk->state   = TCP_SYN_SENT;

	/* Connect to remote device */
	err = irttp_connect_request(self->tsap, self->dtsap_sel, 
				    self->saddr, self->daddr, NULL, 
				    self->max_sdu_size_rx, NULL);
	if (err) {
		DEBUG(0, __FUNCTION__ "(), connect failed!\n");
		return err;
	}

	/* Now the loop */
	if (sk->state != TCP_ESTABLISHED && (flags & O_NONBLOCK))
		return -EINPROGRESS;
		
	cli();	/* To avoid races on the sleep */
	
	/* A Connect Ack with Choke or timeout or failed routing will go to
	 * closed.  */
	while (sk->state == TCP_SYN_SENT) {
		interruptible_sleep_on(sk->sleep);
		if (signal_pending(current)) {
			sti();
			return -ERESTARTSYS;
		}
	}
	
	if (sk->state != TCP_ESTABLISHED) {
		sti();
		sock->state = SS_UNCONNECTED;
		return sock_error(sk);	/* Always set at this point */
	}
	
	sock->state = SS_CONNECTED;
	
	sti();
	
	return 0;
}

/*
 * Function irda_create (sock, protocol)
 *
 *    Create IrDA socket
 *
 */
static int irda_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct irda_sock *self;

	DEBUG(2, __FUNCTION__ "()\n");
	
	/* Check for valid socket type */
	switch (sock->type) {
	case SOCK_STREAM:   /* FALLTHROUGH */
	case SOCK_SEQPACKET:
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	/* Allocate socket */
	if ((sk = sk_alloc(PF_IRDA, GFP_ATOMIC, 1)) == NULL)
		return -ENOMEM;
	
	self = kmalloc(sizeof(struct irda_sock), GFP_ATOMIC);
	if (self == NULL)
		return -ENOMEM;
	memset(self, 0, sizeof(struct irda_sock));

	self->sk = sk;
	sk->protinfo.irda = self;

	sock_init_data(sock, sk);

	if (sock->type == SOCK_STREAM)
		sock->ops = &irda_stream_ops;
	else
		sock->ops = &irda_dgram_ops;

	sk->protocol = protocol;

	/* Register as a client with IrLMP */
	self->ckey = irlmp_register_client(0, NULL, NULL);
	self->mask = 0xffff;
	self->rx_flow = self->tx_flow = FLOW_START;
	self->max_sdu_size_rx = SAR_DISABLE; /* Default value */
	self->nslots = DISCOVERY_DEFAULT_SLOTS;

	/* Notify that we are using the irda module, so nobody removes it */
	irda_mod_inc_use_count();

	return 0;
}

/*
 * Function irda_destroy_socket (self)
 *
 *    Destroy socket
 *
 */
void irda_destroy_socket(struct irda_sock *self)
{
	DEBUG(2, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);

	/* Unregister with IrLMP */
	irlmp_unregister_client(self->ckey);
	irlmp_unregister_service(self->skey);

	/* Unregister with LM-IAS */
	if (self->ias_obj)
		irias_delete_object(self->ias_obj->name);

	if (self->tsap) {
		irttp_disconnect_request(self->tsap, NULL, P_NORMAL);
		irttp_close_tsap(self->tsap);
		self->tsap = NULL;
	}

	kfree(self);

	/* Notify that we are not using the irda module anymore */
	irda_mod_dec_use_count();

	return;
}

/*
 * Function irda_release (sock, peer)
 *
 *    
 *
 */
static int irda_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	
	DEBUG(1, __FUNCTION__ "()\n");

        if (sk == NULL) 
		return 0;
	
	sk->state       = TCP_CLOSE;
	sk->shutdown   |= SEND_SHUTDOWN;
	sk->state_change(sk);
	sk->dead        = 1;

	irda_destroy_socket(sk->protinfo.irda);

        sock->sk   = NULL;      
        sk->socket = NULL;      /* Not used, but we should do this. */

        return 0;
}

/*
 * Function irda_sendmsg (sock, msg, len, scm)
 *
 *    Send message down to TinyTP
 *
 */
static int irda_sendmsg(struct socket *sock, struct msghdr *msg, int len, 
			struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
/* 	struct sockaddr_irda *addr = (struct sockaddr_irda *) msg->msg_name; */
	struct irda_sock *self;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int err;

	DEBUG(4, __FUNCTION__ "(), len=%d\n", len);

	if (msg->msg_flags & ~MSG_DONTWAIT)
		return -EINVAL;

	if (sk->shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		return -EPIPE;
	}

	self = sk->protinfo.irda;
	ASSERT(self != NULL, return -1;);

	/* Check if IrTTP is wants us to slow down */
	while (self->tx_flow == FLOW_STOP) {
		DEBUG(2, __FUNCTION__ "(), IrTTP is busy, going to sleep!\n");
		interruptible_sleep_on(sk->sleep);
		
		/* Check if we are still connected */
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
	}

	/* Check that we don't send out to big frames */
	if (len > self->max_data_size) {
		DEBUG(0, __FUNCTION__ "(), Warning to much data! "
		      "Chopping frame from %d to %d bytes!\n", len, 
		      self->max_data_size);
		len = self->max_data_size;
	}

	skb = sock_alloc_send_skb(sk, len + self->max_header_size, 0, 
				  msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return -ENOBUFS;

	skb_reserve(skb, self->max_header_size);
	
	DEBUG(4, __FUNCTION__ "(), appending user data\n");
	asmptr = skb->h.raw = skb_put(skb, len);
	memcpy_fromiovec(asmptr, msg->msg_iov, len);

	/* 
	 * Just send the message to TinyTP, and let it deal with possible 
	 * errors. No need to duplicate all that here
	 */
	err = irttp_data_request(self->tsap, skb);
	if (err) {
		DEBUG(0, __FUNCTION__ "(), err=%d\n", err);
		return err;
	}
	return len;
}

/*
 * Function irda_recvmsg (sock, msg, size, flags, scm)
 *
 *    Try to receive message and copy it to user
 *
 */
static int irda_recvmsg_dgram(struct socket *sock, struct msghdr *msg, 
			      int size, int flags, struct scm_cookie *scm)
{
	struct irda_sock *self;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;

	DEBUG(4, __FUNCTION__ "()\n");

	self = sk->protinfo.irda;
	ASSERT(self != NULL, return -1;);

	skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT, 
				flags & MSG_DONTWAIT, &err);
	if (!skb)
		return err;

	skb->h.raw = skb->data;
	copied     = skb->len;
	
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}
	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	skb_free_datagram(sk, skb);

	/*
	 *  Check if we have previously stopped IrTTP and we know
	 *  have more free space in our rx_queue. If so tell IrTTP
	 *  to start delivering frames again before our rx_queue gets
	 *  empty
	 */
	if (self->rx_flow == FLOW_STOP) {
		if ((atomic_read(&sk->rmem_alloc) << 2) <= sk->rcvbuf) {
			DEBUG(2, __FUNCTION__ "(), Starting IrTTP\n");
			self->rx_flow = FLOW_START;
			irttp_flow_request(self->tsap, FLOW_START);
		}
	}

	return copied;
}

/*
 * Function irda_data_wait (sk)
 *
 *    Sleep until data has arrive. But check for races..
 *
 */
static void irda_data_wait(struct sock *sk)
{
	if (!skb_peek(&sk->receive_queue)) {
		sk->socket->flags |= SO_WAITDATA;
		interruptible_sleep_on(sk->sleep);
		sk->socket->flags &= ~SO_WAITDATA;
	}
}

/*
 * Function irda_recvmsg_stream (sock, msg, size, flags, scm)
 *
 *    
 *
 */
static int irda_recvmsg_stream(struct socket *sock, struct msghdr *msg, 
			       int size, int flags, struct scm_cookie *scm)
{
	struct irda_sock *self;
	struct sock *sk = sock->sk;
	int noblock = flags & MSG_DONTWAIT;
	int copied = 0;
	int target = 1;

	DEBUG(3, __FUNCTION__ "()\n");

	self = sk->protinfo.irda;
	ASSERT(self != NULL, return -1;);

	if (sock->flags & SO_ACCEPTCON) 
		return(-EINVAL);

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (flags & MSG_WAITALL)
		target = size;
		
		
	msg->msg_namelen = 0;

	/* Lock the socket to prevent queue disordering
	 * while sleeps in memcpy_tomsg
	 */
/* 	down(&self->readsem); */

	do {
		int chunk;
		struct sk_buff *skb;

		skb=skb_dequeue(&sk->receive_queue);
		if (skb==NULL) {
			if (copied >= target)
				break;
			
			/*
			 *	POSIX 1003.1g mandates this order.
			 */
			
			if (sk->err) {
				/* up(&self->readsem); */
				return sock_error(sk);
			}

			if (sk->shutdown & RCV_SHUTDOWN)
				break;

		/* 	up(&self->readsem); */

			if (noblock)
				return -EAGAIN;
			irda_data_wait(sk);
			if (signal_pending(current))
				return -ERESTARTSYS;
		/* 	down(&self->readsem); */
			continue;
		}

		/* Never glue messages from different writers */
/* 		if (check_creds && */
/* 		    memcmp(UNIXCREDS(skb), &scm->creds, sizeof(scm->creds)) != 0) */
/* 		{ */
/* 			skb_queue_head(&sk->receive_queue, skb); */
/* 			break; */
/* 		} */

		chunk = min(skb->len, size);
		if (memcpy_toiovec(msg->msg_iov, skb->data, chunk)) {
			skb_queue_head(&sk->receive_queue, skb);
			if (copied == 0)
				copied = -EFAULT;
			break;
		}
		copied += chunk;
		size -= chunk;

 		/* Copy credentials */
/* 		scm->creds = *UNIXCREDS(skb); */
/* 		check_creds = 1; */

		/* Mark read part of skb as used */
		if (!(flags & MSG_PEEK)) {
			skb_pull(skb, chunk);

/* 			if (UNIXCB(skb).fp) */
/* 				unix_detach_fds(scm, skb); */

			/* put the skb back if we didn't use it up.. */
			if (skb->len) {
				DEBUG(1, __FUNCTION__ "(), back on q!\n");
				skb_queue_head(&sk->receive_queue, skb);
				break;
			}

			kfree_skb(skb);
			
/* 			if (scm->fp) */
/* 				break; */
		} else {
			DEBUG(0, __FUNCTION__ "() questionable!?\n");
			/* It is questionable, see note in unix_dgram_recvmsg. */
/* 			if (UNIXCB(skb).fp) */
/* 				scm->fp = scm_fp_dup(UNIXCB(skb).fp); */

			/* put message back and return */
			skb_queue_head(&sk->receive_queue, skb);
			break;
		}
	} while (size);

	/*
	 *  Check if we have previously stopped IrTTP and we know
	 *  have more free space in our rx_queue. If so tell IrTTP
	 *  to start delivering frames again before our rx_queue gets
	 *  empty
	 */
	if (self->rx_flow == FLOW_STOP) {
		if ((atomic_read(&sk->rmem_alloc) << 2) <= sk->rcvbuf) {
			DEBUG(2, __FUNCTION__ "(), Starting IrTTP\n");
			self->rx_flow = FLOW_START;
			irttp_flow_request(self->tsap, FLOW_START);
		}
	}

	/* up(&self->readsem); */

	return copied;
}

/*
 * Function irda_shutdown (sk, how)
 *
 *    
 *
 */
static int irda_shutdown( struct socket *sk, int how)
{
	DEBUG( 0, __FUNCTION__ "()\n");

        /* FIXME - generate DM and RNR states */
        return -EOPNOTSUPP;
}

/*
 * Function irda_poll (file, sock, wait)
 *
 *    
 *
 */
static unsigned int irda_poll(struct file * file, struct socket *sock, 
			      poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask;

	DEBUG(1, __FUNCTION__ "()\n");

	poll_wait(file, sk->sleep, wait);
	mask = 0;

	/* exceptional events? */
	if (sk->err)
		mask |= POLLERR;
	if (sk->shutdown & RCV_SHUTDOWN)
		mask |= POLLHUP;

	/* readable? */
	if (!skb_queue_empty(&sk->receive_queue))
		mask |= POLLIN | POLLRDNORM;

	/* Connection-based need to check for termination and startup */
	if (sk->type == SOCK_STREAM && sk->state==TCP_CLOSE)
		mask |= POLLHUP;

	/*
	 * we set writable also when the other side has shut down the
	 * connection. This prevents stuck sockets.
	 */
	if (sk->sndbuf - (int)atomic_read(&sk->wmem_alloc) >= MIN_WRITE_SPACE)
			mask |= POLLOUT | POLLWRNORM | POLLWRBAND;

	return mask;
}

/*
 * Function irda_ioctl (sock, cmd, arg)
 *
 *    
 *
 */
static int irda_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;

	DEBUG(4, __FUNCTION__ "(), cmd=%#x\n", cmd);
	
	switch (cmd) {
	case TIOCOUTQ: {
		long amount;
		amount = sk->sndbuf - atomic_read(&sk->wmem_alloc);
		if (amount < 0)
			amount = 0;
		if (put_user(amount, (unsigned int *)arg))
			return -EFAULT;
		return 0;
	}
	
	case TIOCINQ: {
		struct sk_buff *skb;
		long amount = 0L;
		/* These two are safe on a single CPU system as only user tasks fiddle here */
		if ((skb = skb_peek(&sk->receive_queue)) != NULL)
			amount = skb->len;
		if (put_user(amount, (unsigned int *)arg))
			return -EFAULT;
		return 0;
	}
	
	case SIOCGSTAMP:
		if (sk != NULL) {
			if (sk->stamp.tv_sec == 0)
				return -ENOENT;
			if (copy_to_user((void *)arg, &sk->stamp, sizeof(struct timeval)))
				return -EFAULT;
			return 0;
		}
		return -EINVAL;
		
	case SIOCGIFADDR:
	case SIOCSIFADDR:
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	case SIOCGIFMETRIC:
	case SIOCSIFMETRIC:
		return -EINVAL;
		
	default:
		DEBUG(1, __FUNCTION__ "(), doing device ioctl!\n");
		return dev_ioctl(cmd, (void *) arg);
	}

	/*NOTREACHED*/
	return 0;
}

/*
 * Function irda_setsockopt (sock, level, optname, optval, optlen)
 *
 *    Set some options for the socket
 *
 */
static int irda_setsockopt(struct socket *sock, int level, int optname, 
			   char *optval, int optlen)
{
 	struct sock *sk = sock->sk;
	struct irda_sock *self;
	int opt;
	
	DEBUG(0, __FUNCTION__ "()\n");

	self = sk->protinfo.irda;
	ASSERT(self != NULL, return -1;);

	if (level != SOL_IRLMP)
		return -ENOPROTOOPT;
	
	if (optlen < sizeof(int))
		return -EINVAL;
	
	if (get_user(opt, (int *)optval))
		return -EFAULT;
	
	switch (optname) {
	case IRLMP_IAS_SET:
		DEBUG(0, __FUNCTION__ "(), sorry not impl. yet!\n");
		return 0;
	case IRTTP_MAX_SDU_SIZE:
		DEBUG(0, __FUNCTION__ "(), setting max_sdu_size = %d\n", opt);
		self->max_sdu_size_rx = opt;
		break;
	default:
		return -ENOPROTOOPT;
	}
	return 0;
}

/*
 * Function irda_getsockopt (sock, level, optname, optval, optlen)
 *
 *    
 *
 */
static int irda_getsockopt(struct socket *sock, int level, int optname, 
			   char *optval, int *optlen)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self;
	struct irda_device_list *list;
	__u8 optbuf[sizeof(struct irda_device_list) +
		   sizeof(struct irda_device_info)*10];
	discovery_t *discovery;
	int val = 0;
	int len = 0;
	int i = 0;

	DEBUG(1, __FUNCTION__ "()\n");

	self = sk->protinfo.irda;

	if (level != SOL_IRLMP)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	switch (optname) {
	case IRLMP_ENUMDEVICES:
		DEBUG(1, __FUNCTION__ "(), IRLMP_ENUMDEVICES\n");
		
		/* Tell IrLMP we want to be notified */
		irlmp_update_client(self->ckey, self->mask, NULL, 
				    irda_discovery_indication);

		/* Do some discovery */
		irlmp_discovery_request(self->nslots);

		/* Devices my be discovered already */
		if (!cachelog) {
			DEBUG(2, __FUNCTION__ "(), no log!\n");

			/* Sleep until device(s) discovered */
			interruptible_sleep_on(&discovery_wait);
			if (!cachelog)
				return -1;
		}

		list = (struct irda_device_list *) optbuf;
		/* 
		 * Now, check all discovered devices (if any), and notify
		 * client only about the services that the client is
		 * interested in 
		 */
		discovery = (discovery_t *) hashbin_get_first(cachelog);
		while (discovery != NULL) {
			/* Mask out the ones we don't want */
			if (discovery->hints.word & self->mask) {
				/* Copy discovery information */
				list->dev[i].saddr = discovery->saddr;
				list->dev[i].daddr = discovery->daddr;
				list->dev[i].charset = discovery->charset;
				list->dev[i].hints[0] = discovery->hints.byte[0];
				list->dev[i].hints[1] = discovery->hints.byte[1];
				strncpy(list->dev[i].info, discovery->info, 22);
				if (++i >= 10)
					break;
			}
			discovery = (discovery_t *) hashbin_get_next(cachelog);
		}
		cachelog = NULL;

		list->len = i;
		len = sizeof(struct irda_device_list) +
			sizeof(struct irda_device_info) * i;

		DEBUG(1, __FUNCTION__ "(), len=%d, i=%d\n", len, i);

		if (put_user(len, optlen))
			return -EFAULT;
		
		if (copy_to_user(optval, &optbuf, len))
			return -EFAULT;
		break;
	case IRTTP_MAX_SDU_SIZE:
		val = self->max_data_size;
		DEBUG(0, __FUNCTION__ "(), getting max_sdu_size = %d\n", val);
		len = sizeof(int);
		if (put_user(len, optlen))
			return -EFAULT;
		
		if (copy_to_user(optval, &val, len))
			return -EFAULT;
		break;
	default:
		return -ENOPROTOOPT;
	}

	return 0;
}

static struct net_proto_family irda_family_ops =
{
	PF_IRDA,
	irda_create
};

static struct proto_ops SOCKOPS_WRAPPED(irda_stream_ops) = {
	PF_IRDA,
	
	irda_release,
	irda_bind,
	irda_connect,
	sock_no_socketpair,
	irda_accept,
	irda_getname,
	irda_poll,
	irda_ioctl,
	irda_listen,
	irda_shutdown,
	irda_setsockopt,
	irda_getsockopt,
	sock_no_fcntl,
	irda_sendmsg,
	irda_recvmsg_stream,
	sock_no_mmap
};

static struct proto_ops SOCKOPS_WRAPPED(irda_dgram_ops) = {
	PF_IRDA,
	
	irda_release,
	irda_bind,
	irda_connect,
	sock_no_socketpair,
	irda_accept,
	irda_getname,
	datagram_poll,
	irda_ioctl,
	irda_listen,
	irda_shutdown,
	irda_setsockopt,
	irda_getsockopt,
	sock_no_fcntl,
	irda_sendmsg,
	irda_recvmsg_dgram,
	sock_no_mmap
};

#include <linux/smp_lock.h>
SOCKOPS_WRAP(irda_dgram, PF_IRDA);
SOCKOPS_WRAP(irda_stream, PF_IRDA);

/*
 * Function irda_device_event (this, event, ptr)
 *
 *    
 *
 */
static int irda_device_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = (struct net_device *) ptr;
	
	DEBUG(3, __FUNCTION__ "()\n");
	
        /* Reject non IrDA devices */
	if (dev->type != ARPHRD_IRDA) 
		return NOTIFY_DONE;
	
        switch (event) {
	case NETDEV_UP:
		DEBUG(3, __FUNCTION__ "(), NETDEV_UP\n");
		/* irda_dev_device_up(dev); */
		break;
	case NETDEV_DOWN:
		DEBUG(3, __FUNCTION__ "(), NETDEV_DOWN\n");
		/* irda_kill_by_device(dev); */
		/* irda_rt_device_down(dev); */
		/* irda_dev_device_down(dev); */
		break;
	default:
		break;
        }

        return NOTIFY_DONE;
}

static struct packet_type irda_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_IRDA),*/
	NULL,
	irlap_driver_rcv,
	NULL,
	NULL,
};

static struct notifier_block irda_dev_notifier = {
	irda_device_event,
	NULL,
	0
};

/*
 * Function irda_proto_init (pro)
 *
 *    Initialize IrDA protocol layer
 *
 */
__initfunc(void irda_proto_init(struct net_proto *pro))
{
	DEBUG( 4, __FUNCTION__ "\n");

	sock_register(&irda_family_ops);

	irda_packet_type.type = htons(ETH_P_IRDA);
        dev_add_pack(&irda_packet_type);

	register_netdevice_notifier( &irda_dev_notifier);

	irda_init();
}

/*
 * Function irda_proto_cleanup (void)
 *
 *    Remove IrDA protocol layer
 *
 */
#ifdef MODULE
void irda_proto_cleanup(void)
{
	DEBUG( 4, __FUNCTION__ "\n");

	irda_packet_type.type = htons(ETH_P_IRDA);
        dev_remove_pack(&irda_packet_type);

        unregister_netdevice_notifier(&irda_dev_notifier);
	
	sock_unregister(PF_IRDA);
	irda_cleanup();
	
        return;
}
#endif /* MODULE */
