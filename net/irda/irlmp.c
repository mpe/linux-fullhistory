/*********************************************************************
 *                
 * Filename:      irlmp.c
 * Version:       0.9
 * Description:   IrDA Link Management Protocol (LMP) layer                 
 * Status:        Stable.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 17 20:54:32 1997
 * Modified at:   Fri Apr 23 09:13:24 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
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

#include <linux/config.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/random.h>
#include <linux/irda.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/timer.h>
#include <net/irda/qos.h>
#include <net/irda/irlap.h>
#include <net/irda/iriap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irlmp_frame.h>

/* Master structure */
struct irlmp_cb *irlmp = NULL;

/* These can be altered by the sysctl interface */
int  sysctl_discovery = 0;
int  sysctl_discovery_slots = 6;
char sysctl_devname[65];

char *lmp_reasons[] = {
	"ERROR, NOT USED",
	"LM_USER_REQUEST",
	"LM_LAP_DISCONNECT",
	"LM_CONNECT_FAILURE",
	"LM_LAP_RESET",
	"LM_INIT_DISCONNECT",
	"ERROR, NOT USED",
};

__u8 *irlmp_hint_to_service( __u8 *hint);
#ifdef CONFIG_PROC_FS
int irlmp_proc_read(char *buf, char **start, off_t offst, int len, int unused);
#endif

/*
 * Function irlmp_init (void)
 *
 *    Create (allocate) the main IrLMP structure and the pointer array
 *    which will contain pointers to each instance of a LSAP.
 */
__initfunc(int irlmp_init(void))
{
	/* Initialize the irlmp structure. */
	if ( irlmp == NULL) {
		irlmp = kmalloc( sizeof(struct irlmp_cb), GFP_KERNEL);
		if ( irlmp == NULL)
			return -ENOMEM;
	}
	memset( irlmp, 0, sizeof(struct irlmp_cb));
	
	irlmp->magic = LMP_MAGIC;

	irlmp->clients = hashbin_new(HB_GLOBAL);
	irlmp->services = hashbin_new(HB_GLOBAL);
	irlmp->links = hashbin_new(HB_GLOBAL);
	irlmp->unconnected_lsaps = hashbin_new(HB_GLOBAL);
	irlmp->cachelog = hashbin_new(HB_GLOBAL);
	
	irlmp->free_lsap_sel = 0x10; /* Servers use 0x00-0x0f */
#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
	irlmp->cache.valid = FALSE;
#endif
	strcpy(sysctl_devname, "Linux");
	
	/* Do discovery every 3 seconds */
	init_timer(&irlmp->discovery_timer);
   	irlmp_start_discovery_timer(irlmp, 600); 

	return 0;
}

/*
 * Function irlmp_cleanup (void)
 *
 *    Remove IrLMP layer
 *
 */
void irlmp_cleanup(void) 
{
	/* Check for main structure */
	ASSERT(irlmp != NULL, return;);
	ASSERT(irlmp->magic == LMP_MAGIC, return;);

	del_timer(&irlmp->discovery_timer);
	
	hashbin_delete(irlmp->links, (FREE_FUNC) kfree);
	hashbin_delete(irlmp->unconnected_lsaps, (FREE_FUNC) kfree);
	hashbin_delete(irlmp->clients, (FREE_FUNC) kfree);
	hashbin_delete(irlmp->services, (FREE_FUNC) kfree);
	hashbin_delete(irlmp->cachelog, (FREE_FUNC) kfree);
	
	/* De-allocate main structure */
	kfree(irlmp);
	irlmp = NULL;
}

/*
 * Function irlmp_open_lsap (slsap, notify)
 *
 *   Register with IrLMP and create a local LSAP,
 *   returns handle to LSAP.
 */
struct lsap_cb *irlmp_open_lsap(__u8 slsap_sel, struct notify_t *notify)
{
	struct lsap_cb *self;

	ASSERT(notify != NULL, return NULL;);
	ASSERT(irlmp != NULL, return NULL;);
	ASSERT(irlmp->magic == LMP_MAGIC, return NULL;);

	DEBUG(4, __FUNCTION__ "(), slsap_sel=%02x\n", slsap_sel);

	/* 
	 *  Does the client care which Source LSAP selector it gets? 
	 */
	if (slsap_sel == LSAP_ANY) {
		/*
		 *  Find unused LSAP
		 */
		slsap_sel = irlmp_find_free_slsap();
		if ( slsap_sel == 0)
			return NULL;
	} else {
		/*
		 *  Client wants specific LSAP, so check if it's already
		 *  in use
		 */
		if (irlmp_slsap_inuse(slsap_sel)) {
			return NULL;
		}
	}

	/*
	 *  Allocate new instance of a LSAP connection
	 */
	self = kmalloc(sizeof(struct lsap_cb), GFP_ATOMIC);
	if (self == NULL) {
		printk( KERN_ERR "IrLMP: Can't allocate memory for "
			"LSAP control block!\n");
		return NULL;
	}
	memset(self, 0, sizeof(struct lsap_cb));
	
	self->magic = LMP_LSAP_MAGIC;
	self->slsap_sel = slsap_sel;
	self->dlsap_sel = LSAP_ANY;
	self->connected = FALSE;

	init_timer(&self->watchdog_timer);

	ASSERT(notify->instance != NULL, return NULL;);
	self->notify = *notify;

	irlmp_next_lsap_state(self, LSAP_DISCONNECTED);
	
	/*
	 *  Insert into queue of unconnected LSAPs
	 */
	hashbin_insert(irlmp->unconnected_lsaps, (QUEUE *) self, (int) self, 
		       NULL);
	
	return self;
}

/*
 * Function irlmp_close_lsap (self)
 *
 *    Remove an instance of LSAP
 */
static void __irlmp_close_lsap(struct lsap_cb *self)
{
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == LMP_LSAP_MAGIC, return;);

	/*
	 *  Set some of the variables to preset values
	 */
	self->magic = ~LMP_LSAP_MAGIC;
	del_timer(&self->watchdog_timer); /* Important! */

#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
	ASSERT(irlmp != NULL, return;);
	irlmp->cache.valid = FALSE;
#endif
	kfree(self);
}

/*
 * Function irlmp_close_lsap (self)
 *
 *    Close and remove LSAP
 *
 */
void irlmp_close_lsap(struct lsap_cb *self)
{
	struct lap_cb *lap;
	struct lsap_cb *lsap = NULL;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == LMP_LSAP_MAGIC, return;);

	/*
	 *  Find out if we should remove this LSAP from a link or from the
	 *  list of unconnected lsaps (not associated with a link)
	 */
	lap = self->lap;
	if (lap) {
		ASSERT(lap->magic == LMP_LAP_MAGIC, return;);
		lsap = hashbin_remove(lap->lsaps, (int) self, NULL);
	}
	/* Check if we found the LSAP! If not then try the unconnected lsaps */
	if (!lsap) {
		lsap = hashbin_remove(irlmp->unconnected_lsaps, (int) self, 
				      NULL);
	}
	if (!lsap) {
		DEBUG(0, __FUNCTION__ 
		       "(), Looks like somebody has removed me already!\n");
		return;
	}
	__irlmp_close_lsap(self);
}

/*
 * Function irlmp_register_irlap (saddr, notify)
 *
 *    Register IrLAP layer with IrLMP. There is possible to have multiple
 *    instances of the IrLAP layer, each connected to different IrDA ports
 *
 */
void irlmp_register_link(struct irlap_cb *irlap, __u32 saddr, 
			 struct notify_t *notify)
{
	struct lap_cb *lap;

	DEBUG(4, __FUNCTION__ "(), Registered IrLAP, saddr = %08x\n", saddr);
	
	ASSERT(irlmp != NULL, return;);
	ASSERT(irlmp->magic == LMP_MAGIC, return;);
	ASSERT(notify != NULL, return;);

	/*
	 *  Allocate new instance of a LSAP connection
	 */
	lap = kmalloc(sizeof(struct lap_cb), GFP_KERNEL);
	if (lap == NULL) {
		DEBUG(3, __FUNCTION__ "(), unable to kmalloc\n");
		return;
	}
	memset(lap, 0, sizeof(struct lap_cb));
	
	lap->irlap = irlap;
	lap->magic = LMP_LAP_MAGIC;
	lap->saddr = saddr;
	lap->daddr = DEV_ADDR_ANY;
	lap->lsaps = hashbin_new(HB_GLOBAL);

	irlmp_next_lap_state(lap, LAP_STANDBY);
	
	init_timer(&lap->idle_timer);

	/*
	 *  Insert into queue of unconnected LSAPs
	 */
	hashbin_insert(irlmp->links, (QUEUE *) lap, lap->saddr, NULL);

	/* 
	 *  We set only this variable so IrLAP can tell us on which link the
	 *  different events happened on 
	 */
	irda_notify_init(notify);
	notify->instance = lap;
}

/*
 * Function irlmp_unregister_irlap (saddr)
 *
 *    IrLAP layer has been removed!
 *
 */
void irlmp_unregister_link(__u32 saddr)
{
	struct lap_cb *link;

	DEBUG(4, __FUNCTION__ "()\n");

	link = hashbin_remove(irlmp->links, saddr, NULL);
	if (link) {
		ASSERT(link->magic == LMP_LAP_MAGIC, return;);

		/* Remove all discoveries discovered at this link */
		irlmp_expire_discoveries(irlmp->cachelog, link->saddr, TRUE);

		del_timer(&link->idle_timer);	

		link->magic = 0;
		kfree(link);
	}
}

/*
 * Function irlmp_connect_request (handle, dlsap, userdata)
 *
 *    Connect with a peer LSAP  
 *
 */
int irlmp_connect_request(struct lsap_cb *self, __u8 dlsap_sel, 
			  __u32 saddr, __u32 daddr, 
			  struct qos_info *qos, struct sk_buff *userdata) 
{
	struct sk_buff *skb = NULL;
	struct lap_cb *lap;
	struct lsap_cb *lsap;
	discovery_t *discovery;

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return -1;);
	
	DEBUG(2, __FUNCTION__ 
	      "(), slsap_sel=%02x, dlsap_sel=%02x, saddr=%08x, daddr=%08x\n", 
	      self->slsap_sel, dlsap_sel, saddr, daddr);
	
	if ( self->connected) 
		return -EISCONN;

	/* Client must supply destination device address */
	if (!daddr)
		return -EINVAL;

	/* Any userdata? */
	if (userdata == NULL) {
		skb = dev_alloc_skb(64);
		if (!skb)
			return -ENOMEM;

		skb_reserve(skb, LMP_CONTROL_HEADER+LAP_HEADER);
	} else
		skb = userdata;
	
	/* Make room for MUX control header ( 3 bytes) */
	ASSERT(skb_headroom(skb) >= LMP_CONTROL_HEADER, return -1;);
	skb_push(skb, LMP_CONTROL_HEADER);

	self->dlsap_sel = dlsap_sel;
	self->tmp_skb = skb;
	
	/*  
	 * Find the link to where we should try to connect since there may
	 * be more than one IrDA port on this machine. If the client has
	 * passed us the saddr (and already knows which link to use), then
	 * we use that to find the link, if not then we have to look in the
	 * discovery log and check if any of the links has discovered a
	 * device with the given daddr 
	 */
	if (!saddr) {
		discovery = hashbin_find(irlmp->cachelog, daddr, NULL);
		if (discovery)
			saddr = discovery->saddr;
	}
	lap = hashbin_find(irlmp->links, saddr, NULL);	
	if (lap == NULL) {
		DEBUG(1, __FUNCTION__ "(), Unable to find a usable link!\n");
		return -EHOSTUNREACH;
	}

	if (lap->daddr == DEV_ADDR_ANY)
		lap->daddr = daddr;
	else if (lap->daddr != daddr) {
		DEBUG(0, __FUNCTION__ "(), sorry, but link is busy!\n");
		return -EBUSY;
	}

	self->lap = lap;

	/* 
	 *  Remove LSAP from list of unconnected LSAPs and insert it into the 
	 *  list of connected LSAPs for the particular link 
	 */
	lsap = hashbin_remove(irlmp->unconnected_lsaps, (int) self, NULL);

	ASSERT(lsap != NULL, return -1;);
	ASSERT(lsap->magic == LMP_LSAP_MAGIC, return -1;);
	ASSERT(lsap->lap != NULL, return -1;);
	ASSERT(lsap->lap->magic == LMP_LAP_MAGIC, return -1;);

	hashbin_insert(self->lap->lsaps, (QUEUE *) self, (int) self, NULL);

	self->connected = TRUE;
	
	/*
	 *  User supplied qos specifications?
	 */
	if (qos)
		self->qos = *qos;
	
	irlmp_do_lsap_event(self, LM_CONNECT_REQUEST, skb);

	return 0;
}

/*
 * Function irlmp_connect_indication (self)
 *
 *    Incomming connection
 *
 */
void irlmp_connect_indication(struct lsap_cb *self, struct sk_buff *skb) 
{
	int max_seg_size;

	DEBUG(3, __FUNCTION__ "()\n");
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == LMP_LSAP_MAGIC, return;);
	ASSERT(skb != NULL, return;);
	ASSERT(self->lap != NULL, return;);

	self->qos = *self->lap->qos;

	max_seg_size = self->lap->qos->data_size.value;
	DEBUG(4, __FUNCTION__ "(), max_seg_size=%d\n", max_seg_size);
	
	/* Hide LMP_CONTROL_HEADER header from layer above */
	skb_pull(skb, LMP_CONTROL_HEADER);

	if (self->notify.connect_indication)
		self->notify.connect_indication(self->notify.instance, self, 
						&self->qos, max_seg_size, skb);
}

/*
 * Function irlmp_connect_response (handle, userdata)
 *
 *    Service user is accepting connection
 *
 */
void irlmp_connect_response( struct lsap_cb *self, struct sk_buff *userdata) 
{
	DEBUG(3, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
	ASSERT( userdata != NULL, return;);

	self->connected = TRUE;

	DEBUG( 4, "irlmp_connect_response: slsap_sel=%02x, dlsap_sel=%02x\n", 
	       self->slsap_sel, self->dlsap_sel);

	/* Make room for MUX control header ( 3 bytes) */
	ASSERT( skb_headroom( userdata) >= LMP_CONTROL_HEADER, return;);
	skb_push( userdata, LMP_CONTROL_HEADER);
	
	irlmp_do_lsap_event( self, LM_CONNECT_RESPONSE, userdata);
}

/*
 * Function irlmp_connect_confirm (handle, skb)
 *
 *    LSAP connection confirmed peer device!
 */
void irlmp_connect_confirm(struct lsap_cb *self, struct sk_buff *skb) 
{
	int max_seg_size;

	DEBUG(3, __FUNCTION__ "()\n");
	
	ASSERT( skb != NULL, return;);
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
	
	ASSERT( self->lap != NULL, return;);
	self->qos = *self->lap->qos;

	max_seg_size = self->qos.data_size.value;
	DEBUG( 4, __FUNCTION__ "(), max_seg_size=%d\n", max_seg_size);
	
	/* Hide LMP_CONTROL_HEADER header from layer above */
	skb_pull( skb, LMP_CONTROL_HEADER);

	if ( self->notify.connect_confirm) {
		self->notify.connect_confirm( self->notify.instance, self,
					      &self->qos, max_seg_size, skb);
	}
}

/*
 * Function irlmp_dup (orig, instance)
 *
 *    Duplicate LSAP, can be used by servers to confirm a connection on a
 *    new LSAP so it can keep listening on the old one.
 *
 */
struct lsap_cb *irlmp_dup(struct lsap_cb *orig, void *instance) 
{
	struct lsap_cb *new;

	DEBUG(1, __FUNCTION__ "()\n");

	/* Only allowed to duplicate unconnected LSAP's */
	if (!hashbin_find(irlmp->unconnected_lsaps, (int) orig, NULL)) {
		DEBUG(0, __FUNCTION__ "(), unable to find LSAP\n");
		return NULL;
	}
	new = kmalloc(sizeof(struct lsap_cb), GFP_ATOMIC);
	if (!new)  {
		DEBUG(0, __FUNCTION__ "(), unable to kmalloc\n");
		return NULL;
	}
	/* Dup */
	memcpy(new, orig, sizeof(struct lsap_cb));
	new->notify.instance = instance;
	
	init_timer(&new->watchdog_timer);
	
	hashbin_insert(irlmp->unconnected_lsaps, (QUEUE *) new, (int) new, 
		       NULL);

	/* Make sure that we invalidate the cache */
#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
	irlmp->cache.valid = FALSE;
#endif /* CONFIG_IRDA_CACHE_LAST_LSAP */

	return new;
}

/*
 * Function irlmp_disconnect_request (handle, userdata)
 *
 *    The service user is requesting disconnection, this will not remove the 
 *    LSAP, but only mark it as disconnected
 */
void irlmp_disconnect_request(struct lsap_cb *self, struct sk_buff *userdata) 
{
	struct lsap_cb *lsap;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	/* Already disconnected? */
	if ( !self->connected) {
		DEBUG( 1, __FUNCTION__ "(), already disconnected!\n");
		return;
	}

	ASSERT( userdata != NULL, return;);
	ASSERT( self->connected == TRUE, return;);
	
	skb_push( userdata, LMP_CONTROL_HEADER);

	/* 
	 *  Do the event before the other stuff since we must know
	 *  which lap layer that the frame should be transmitted on
	 */
	irlmp_do_lsap_event( self, LM_DISCONNECT_REQUEST, userdata);

	/* 
	 *  Remove LSAP from list of connected LSAPs for the particular link
	 *  and insert it into the list of unconnected LSAPs
	 */
	ASSERT( self->lap != NULL, return;);
	ASSERT( self->lap->magic == LMP_LAP_MAGIC, return;);
	ASSERT( self->lap->lsaps != NULL, return;);

	lsap = hashbin_remove(self->lap->lsaps, (int) self, NULL);

	ASSERT( lsap != NULL, return;);
	ASSERT( lsap->magic == LMP_LSAP_MAGIC, return;);
	ASSERT( lsap == self, return;);

	hashbin_insert(irlmp->unconnected_lsaps, (QUEUE *) self, (int) self, 
		       NULL);
	
	/* Reset some values */
	self->connected = FALSE;
	self->dlsap_sel = LSAP_ANY;
	self->lap = NULL;
}

/*
 * Function irlmp_disconnect_indication (reason, userdata)
 *
 *    LSAP is being closed!
 */
void irlmp_disconnect_indication( struct lsap_cb *self, LM_REASON reason, 
				  struct sk_buff *userdata) 
{
	struct lsap_cb *lsap;

	DEBUG( 1, __FUNCTION__ "(), reason=%s\n", lmp_reasons[reason]);	

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
 	ASSERT( self->connected == TRUE, return;); 

	DEBUG( 3, __FUNCTION__ "(), slsap_sel=%02x, dlsap_sel=%02x\n", 
	       self->slsap_sel, self->dlsap_sel);

	self->connected = FALSE;
	self->dlsap_sel = LSAP_ANY;

	/* 
	 *  Remove association between this LSAP and the link it used 
	 */
	ASSERT( self->lap != NULL, return;);
	ASSERT( self->lap->lsaps != NULL, return;);

	lsap = hashbin_remove( self->lap->lsaps, (int) self, NULL);

	ASSERT( lsap != NULL, return;);
	ASSERT( lsap == self, return;);
	hashbin_insert(irlmp->unconnected_lsaps, (QUEUE *) lsap, (int) lsap, 
		       NULL);

	self->lap = NULL;
	
	/*
	 *  Inform service user
	 */
	if ( self->notify.disconnect_indication) {
		self->notify.disconnect_indication( self->notify.instance, 
						    self, reason, userdata);
	}
}

/*
 * Function irlmp_do_discovery (nslots)
 *
 *    Do some discovery on all links
 *
 */
void irlmp_do_discovery(int nslots)
{
	struct lap_cb *lap;

	/* Make sure value is sane */
	if ((nslots != 1) && (nslots != 6) && (nslots != 8)&&(nslots != 16)) {
		printk(KERN_WARNING __FUNCTION__ 
		       "(), invalid value for number of slots!\n");
		nslots = sysctl_discovery_slots = 8;
	}

	/* Construct new discovery info to be used by IrLAP, */
	irlmp->discovery_cmd.hints.word = irlmp->hints.word;
	
	/* 
	 *  Set character set for device name (we use ASCII), and 
	 *  copy device name. Remember to make room for a \0 at the 
	 *  end
	 */
	irlmp->discovery_cmd.charset = CS_ASCII;
	strncpy(irlmp->discovery_cmd.info, sysctl_devname, 31);
	irlmp->discovery_cmd.info_len = strlen(irlmp->discovery_cmd.info);
	irlmp->discovery_cmd.nslots = nslots;
	
	/*
	 * Try to send discovery packets on all links
	 */
	lap = (struct lap_cb *) hashbin_get_first(irlmp->links);
	while (lap != NULL) {
		ASSERT(lap->magic == LMP_LAP_MAGIC, return;);
		
		if (lap->lap_state == LAP_STANDBY) {
			/* Expire discoveries discovered on this link */
			irlmp_expire_discoveries(irlmp->cachelog, lap->saddr,
						 FALSE);

			/* Try to discover */
			irlmp_do_lap_event(lap, LM_LAP_DISCOVERY_REQUEST, 
					   NULL);
		}
		lap = (struct lap_cb *) hashbin_get_next(irlmp->links);
	}
}

/*
 * Function irlmp_discovery_request (nslots)
 *
 *    Do a discovery of devices in front of the computer
 *
 */
void irlmp_discovery_request(int nslots)
{
	DEBUG(4, __FUNCTION__ "(), nslots=%d\n", nslots);

	/* Check if user wants to override the default */
	if (nslots == DISCOVERY_DEFAULT_SLOTS)
		nslots = sysctl_discovery_slots;

	/* 
	 * If discovery is already running, then just return the current 
	 * discovery log
	 */
	if (sysctl_discovery) {
		DEBUG(2, __FUNCTION__ "() discovery already running, so we"
		      " just return the old discovery log!\n");
		irlmp_discovery_confirm(irlmp->cachelog);
	} else
		irlmp_do_discovery(nslots);
}

#if 0
/*
 * Function irlmp_check_services (discovery)
 *
 *    
 *
 */
void irlmp_check_services(discovery_t *discovery)
{
	struct irlmp_client *client;
	struct irmanager_event event;
	__u8 *service_log;
	__u8 service;
	int i = 0;

	DEBUG(1, "IrDA Discovered: %s\n", discovery->info);
	DEBUG(1, "    Services: ");

	service_log = irlmp_hint_to_service(discovery->hints.byte);
	if (!service_log)
		return;

	/*
	 *  Check all services on the device
	 */
	while ((service = service_log[i++]) != S_END) {
		DEBUG( 4, "service=%02x\n", service);
		client = hashbin_find(irlmp->registry, service, NULL);
		if (entry && entry->discovery_callback) {
			DEBUG( 4, "discovery_callback!\n");

			entry->discovery_callback(discovery);
		} else {
			/* Don't notify about the ANY service */
			if (service == S_ANY)
				continue;
			/*  
			 * Found no clients for dealing with this service,
			 * so ask the user space irmanager to try to load
			 * the right module for us 
			 */
			event.event = EVENT_DEVICE_DISCOVERED;
			event.service = service;
			event.daddr = discovery->daddr;
			sprintf(event.info, "%s", discovery->info);
			irmanager_notify(&event);
		}
	}
	kfree(service_log);
}
#endif
/*
 * Function irlmp_notify_client (log)
 *
 *    Notify all about discovered devices
 *
 */
void irlmp_notify_client(irlmp_client_t *client, hashbin_t *log)
{
	discovery_t *discovery;

	DEBUG(3, __FUNCTION__ "()\n");
	
	/* Check if client wants the whole log */
	if (client->callback2)
		client->callback2(log);
	
	/* 
	 * Now, check all discovered devices (if any), and notify client 
	 * only about the services that the client is interested in 
	 */
	discovery = (discovery_t *) hashbin_get_first(log);
	while (discovery != NULL) {
		DEBUG(3, "discovery->daddr = 0x%08x\n", discovery->daddr); 
		
		if (client->hint_mask & discovery->hints.word) {
			if (client->callback1)
				client->callback1(discovery);
		}
		discovery = (discovery_t *) hashbin_get_next(log);
	}
}

/*
 * Function irlmp_discovery_confirm ( self, log)
 *
 *    Some device(s) answered to our discovery request! Check to see which
 *    device it is, and give indication to the client(s)
 * 
 */
void irlmp_discovery_confirm(hashbin_t *log) 
{
	irlmp_client_t *client;
	
	DEBUG(3, __FUNCTION__ "()\n");
	
	ASSERT(log != NULL, return;);
	
	if (!hashbin_get_size(log))
		return;
	
	client = (irlmp_client_t *) hashbin_get_first(irlmp->clients);
	while (client != NULL) {
		/* Check if we should notify client */
		irlmp_notify_client(client, log);
			
		client = (irlmp_client_t *) hashbin_get_next(irlmp->clients);
	}
}

/*
 * Function irlmp_get_discovery_response ()
 *
 *    Used by IrLAP to get the disocvery info it needs when answering
 *    discovery requests by other devices.
 */
discovery_t *irlmp_get_discovery_response()
{
	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(irlmp != NULL, return NULL;);

	irlmp->discovery_rsp.hints.word = irlmp->hints.word;

	/* 
	 *  Set character set for device name (we use ASCII), and 
	 *  copy device name. Remember to make room for a \0 at the 
	 *  end
	 */
	irlmp->discovery_rsp.charset = CS_ASCII;

	strncpy(irlmp->discovery_rsp.info, sysctl_devname, 31);
	irlmp->discovery_rsp.info_len = strlen(irlmp->discovery_rsp.info) + 2;

	return &irlmp->discovery_rsp;
}

/*
 * Function irlmp_data_request (self, skb)
 *
 *    Send some data to peer device
 *
 */
void irlmp_data_request(struct lsap_cb *self, struct sk_buff *skb) 
{
	ASSERT(skb != NULL, return;);
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == LMP_LSAP_MAGIC, return;);
	
	/* Make room for MUX header */
	ASSERT(skb_headroom( skb) >= LMP_HEADER, return;);
	skb_push(skb, LMP_HEADER);

	irlmp_do_lsap_event(self, LM_DATA_REQUEST, skb);
}

/*
 * Function irlmp_data_indication (handle, skb)
 *
 *    Got data from LAP layer so pass it up to upper layer
 *
 */
inline void irlmp_data_indication(struct lsap_cb *self, struct sk_buff *skb) 
{
	/* Hide LMP header from layer above */
	skb_pull(skb, LMP_HEADER);

	if (self->notify.data_indication)
		self->notify.data_indication(self->notify.instance, self, skb);
}

/*
 * Function irlmp_udata_request (self, skb)
 *
 *    
 *
 */
inline void irlmp_udata_request( struct lsap_cb *self, struct sk_buff *skb) 
{
 	DEBUG( 4, __FUNCTION__ "()\n"); 

	ASSERT( skb != NULL, return;);
	
	/* Make room for MUX header */
	ASSERT( skb_headroom( skb) >= LMP_HEADER, return;);
	skb_push( skb, LMP_HEADER);

	irlmp_do_lsap_event( self, LM_UDATA_REQUEST, skb);
}

/*
 * Function irlmp_udata_indication (self, skb)
 *
 *    Send unreliable data (but still within the connection)
 *
 */
void irlmp_udata_indication( struct lsap_cb *self, struct sk_buff *skb) 
{
 	DEBUG( 4, __FUNCTION__ "()\n"); 

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Hide LMP header from layer above */
	skb_pull( skb, LMP_HEADER);

	if (self->notify.udata_indication)
		self->notify.udata_indication(self->notify.instance, self, skb);
}

/*
 * Function irlmp_connection_less_data_request (skb)
 *
 *    Send out of connection UI frames
 *
 */
void irlmp_connectionless_data_request( struct sk_buff *skb)
{
	DEBUG( 1, __FUNCTION__ "(), Sorry not implemented\n"); 
}

/*
 * Function irlmp_connection_less_data_indication (skb)
 *
 *    
 *
 */
void irlmp_connectionless_data_indication( struct sk_buff *skb)
{
	DEBUG( 1, __FUNCTION__ "()\n"); 
}

void irlmp_status_request(void) 
{
	DEBUG( 1, "irlmp_status_request(), Not implemented\n");
}

void irlmp_status_indication( LINK_STATUS link, LOCK_STATUS lock) 
{
	DEBUG( 4, "irlmp_status_indication(), Not implemented\n");
}

/*
 * Function irlmp_hint_to_service (hint)
 *
 *    Returns a list of all servics contained in the given hint bits. This
 *    funtion assumes that the hint bits have the size of two bytes only
 */
__u8 *irlmp_hint_to_service(__u8 *hint)
{
	__u8 *service;
	int i = 0;

	/* 
	 * Allocate array to store services in. 16 entries should be safe 
	 * since we currently only support 2 hint bytes
	 */
	service = kmalloc( 16, GFP_ATOMIC);
	if ( !service) {
		DEBUG(1, __FUNCTION__ "(), Unable to kmalloc!\n");
		return NULL;
	}

	if ( !hint[0]) {
		DEBUG(1, "<None>\n");
		return NULL;
	}
	if ( hint[0] & HINT_PNP)
		DEBUG(1, "PnP Compatible ");
	if ( hint[0] & HINT_PDA)
		DEBUG(1, "PDA/Palmtop ");
	if ( hint[0] & HINT_COMPUTER)
		DEBUG(1, "Computer ");
	if ( hint[0] & HINT_PRINTER) {
		DEBUG(1, "Printer ");
		service[i++] = S_PRINTER;
	}
	if ( hint[0] & HINT_MODEM)
		DEBUG(1, "Modem ");
	if ( hint[0] & HINT_FAX)
		DEBUG(1, "Fax ");
	if ( hint[0] & HINT_LAN) {
		DEBUG(1, "LAN Access ");		
		service[i++] = S_LAN;
	}
	/* 
	 *  Test if extension byte exists. This byte will usually be
	 *  there, but this is not really required by the standard.
	 *  (IrLMP p. 29)
	 */
	if ( hint[0] & HINT_EXTENSION) {
		if ( hint[1] & HINT_TELEPHONY) {
			DEBUG(1, "Telephony ");
			service[i++] = S_TELEPHONY;
		} if ( hint[1] & HINT_FILE_SERVER)
			DEBUG(1, "File Server ");
		
		if ( hint[1] & HINT_COMM) {
			DEBUG(1, "IrCOMM ");
			service[i++] = S_COMM;
		}
		if ( hint[1] & HINT_OBEX) {
			DEBUG(1, "IrOBEX ");
			service[i++] = S_OBEX;
		}
	}
	DEBUG(1, "\n");

	/* So that client can be notified about any discovery */
	service[i++] = S_ANY;

	service[i] = S_END;
	
	return service;
}

/*
 * Function irlmp_service_to_hint (service)
 *
 *    Converts a service type, to a hint bit
 *
 *    Returns: a 16 bit hint value, with the service bit set
 */
__u16 irlmp_service_to_hint(int service)
{
	__u16_host_order hint;

	hint.word = 0;

	switch (service) {
	case S_PNP:
		hint.byte[0] |= HINT_PNP;
		break;
	case S_PDA:
		hint.byte[0] |= HINT_PDA;
		break;
	case S_COMPUTER:
		hint.byte[0] |= HINT_COMPUTER;
		break;
	case S_PRINTER:
		hint.byte[0] |= HINT_PRINTER;
		break;
	case S_MODEM:
		hint.byte[0] |= HINT_PRINTER;
		break;
	case S_LAN:
		hint.byte[0] |= HINT_LAN;
		break;
	case S_COMM:
		hint.byte[0] |= HINT_EXTENSION;
		hint.byte[1] |= HINT_COMM;
		break;
	case S_OBEX:
		hint.byte[0] |= HINT_EXTENSION;
		hint.byte[1] |= HINT_OBEX;
		break;
	case S_ANY:
		hint.word = 0xffff;
		break;
	default:
		DEBUG( 1, __FUNCTION__ "(), Unknown service!\n");
		break;
	}
	return hint.word;
}

/*
 * Function irlmp_register_service (service)
 *
 *    Register local service with IrLMP
 *
 */
__u32 irlmp_register_service(__u16 hints)
{
	irlmp_service_t *service;
	__u32 handle;

	DEBUG(4, __FUNCTION__ "(), hints = %04x\n", hints);

	/* Get a unique handle for this service */
	get_random_bytes(&handle, sizeof(handle));
	while (hashbin_find(irlmp->services, handle, NULL) || !handle)
		get_random_bytes(&handle, sizeof(handle));

	irlmp->hints.word |= hints;

	/* Make a new registration */
 	service = kmalloc(sizeof(irlmp_service_t), GFP_ATOMIC);
	if (!service) {
		DEBUG(1, __FUNCTION__ "(), Unable to kmalloc!\n");
		return 0;
	}
	service->hints = hints;
	hashbin_insert(irlmp->services, (QUEUE*) service, handle, NULL);

	return handle;
}

/*
 * Function irlmp_unregister_service (handle)
 *
 *    Unregister service with IrLMP. 
 *
 *    Returns: 0 on success, -1 on error
 */
int irlmp_unregister_service(__u32 handle)
{
	irlmp_service_t *service;
		
 	DEBUG(4, __FUNCTION__ "()\n");

	if (!handle)
		return -1;
 
	service = hashbin_find(irlmp->services, handle, NULL);
	if (!service) {
		DEBUG(1, __FUNCTION__ "(), Unknown service!\n");
		return -1;
	}

	service = hashbin_remove(irlmp->services, handle, NULL);
	if (service)
		kfree(service);

	/* Remove old hint bits */
	irlmp->hints.word = 0;

	/* Refresh current hint bits */
        service = (irlmp_service_t *) hashbin_get_first(irlmp->services);
        while (service) {
		irlmp->hints.word |= service->hints;

                service = (irlmp_service_t *)hashbin_get_next(irlmp->services);
        }
	return 0;
}

/*
 * Function irlmp_register_client (hint_mask, callback1, callback2)
 *
 *    Register a local client with IrLMP
 *
 *    Returns: handle > 0 on success, 0 on error
 */
__u32 irlmp_register_client(__u16 hint_mask, DISCOVERY_CALLBACK1 callback1,
			    DISCOVERY_CALLBACK2 callback2)
{
	irlmp_client_t *client;
	__u32 handle;

	/* Get a unique handle for this client */
	get_random_bytes(&handle, sizeof(handle));
	while (hashbin_find(irlmp->clients, handle, NULL) || !handle)
		get_random_bytes(&handle, sizeof(handle));

	/* Make a new registration */
 	client = kmalloc(sizeof(irlmp_client_t), GFP_ATOMIC);
	if (!client) {
		DEBUG( 1, __FUNCTION__ "(), Unable to kmalloc!\n");

		return 0;
	}

	/* Register the details */
	client->hint_mask = hint_mask;
	client->callback1 = callback1;
	client->callback2 = callback2;

 	hashbin_insert(irlmp->clients, (QUEUE *) client, handle, NULL);

	return handle;
}

/*
 * Function irlmp_update_client (handle, hint_mask, callback1, callback2)
 *
 *    Updates specified client (handle) with possibly new hint_mask and
 *    callback
 *
 *    Returns: 0 on success, -1 on error
 */
int irlmp_update_client(__u32 handle, __u16 hint_mask, 
			DISCOVERY_CALLBACK1 callback1, 
			DISCOVERY_CALLBACK2 callback2)
{
	irlmp_client_t *client;

	if (!handle)
		return -1;

	client = hashbin_find(irlmp->clients, handle, NULL);
	if (!client) {
		DEBUG(1, __FUNCTION__ "(), Unknown client!\n");
		return -1;
	}

	client->hint_mask = hint_mask;
	client->callback1 = callback1;
	client->callback2 = callback2;
	
	return 0;
}

/*
 * Function irlmp_unregister_client (handle)
 *
 *    Returns: 0 on success, -1 on error
 *
 */
int irlmp_unregister_client(__u32 handle)
{
 	struct irlmp_client *client;
 
 	DEBUG(4, __FUNCTION__ "()\n");

	if (!handle)
		return -1;
 
	client = hashbin_find(irlmp->clients, handle, NULL);
	if (!client) {
		DEBUG(1, __FUNCTION__ "(), Unknown client!\n");
		return -1;
	}

	DEBUG( 4, __FUNCTION__ "(), removing client!\n");
	client = hashbin_remove( irlmp->clients, handle, NULL);
	if (client)
		kfree(client);
	
	return 0;
}

/*
 * Function irlmp_slsap_inuse (slsap)
 *
 *    Check if the given source LSAP selector is in use
 */
int irlmp_slsap_inuse( __u8 slsap_sel)
{
	struct lsap_cb *self;
	struct lap_cb *lap;

	ASSERT( irlmp != NULL, return TRUE;);
	ASSERT( irlmp->magic == LMP_MAGIC, return TRUE;);
	ASSERT( slsap_sel != LSAP_ANY, return TRUE;);

	DEBUG( 4, __FUNCTION__ "()\n");

	/* Valid values are between 0 and 127 */
	if (slsap_sel > 127)
		return TRUE;

	/*
	 *  Check if slsap is already in use. To do this we have to loop over
	 *  every IrLAP connection and check every LSAP assosiated with each
	 *  the connection.
	 */
	lap = (struct lap_cb *) hashbin_get_first(irlmp->links);
	while (lap != NULL) {
		ASSERT(lap->magic == LMP_LAP_MAGIC, return TRUE;);

		self = (struct lsap_cb *) hashbin_get_first(lap->lsaps);
		while (self != NULL) {
			ASSERT(self->magic == LMP_LSAP_MAGIC, return TRUE;);

			if ((self->slsap_sel == slsap_sel))/*  &&  */
/* 			    ( self->dlsap_sel == LSAP_ANY)) */
			{
				DEBUG( 4, "Source LSAP selector=%02x in use\n",
				       self->slsap_sel); 
				return TRUE;
			}
			self = (struct lsap_cb*) hashbin_get_next( lap->lsaps);
		}
		lap = (struct lap_cb *) hashbin_get_next( irlmp->links);
	}     
	return FALSE;
}

/*
 * Function irlmp_find_free_slsap ()
 *
 *    Find a free source LSAP to use. This function is called if the service
 *    user has requested a source LSAP equal to LM_ANY
 */
__u8 irlmp_find_free_slsap(void) 
{
	__u8 lsap_sel;
	int wrapped = 0;

	ASSERT(irlmp != NULL, return -1;);
	ASSERT(irlmp->magic == LMP_MAGIC, return -1;);
      
	lsap_sel = irlmp->free_lsap_sel++;
	
	/* Check if the new free lsap is really free */
	while (irlmp_slsap_inuse(irlmp->free_lsap_sel)) {
		irlmp->free_lsap_sel++;

		/* Check if we need to wraparound */
		if (irlmp->free_lsap_sel > 127) {
			irlmp->free_lsap_sel = 10;

			/* Make sure we terminate the loop */
			if (wrapped++)
				return 0;
		}
	}
	DEBUG(4, __FUNCTION__ "(), next free lsap_sel=%02x\n", lsap_sel);
	
	return lsap_sel;
}

/*
 * Function irlmp_convert_lap_reason (lap_reason)
 *
 *    Converts IrLAP disconnect reason codes to IrLMP disconnect reason
 *    codes
 *
 */
LM_REASON irlmp_convert_lap_reason( LAP_REASON lap_reason)
{
	int reason = LM_LAP_DISCONNECT;

	switch (lap_reason) {		
	case LAP_DISC_INDICATION: /* Received a disconnect request from peer */
		DEBUG( 1, __FUNCTION__ "(), LAP_DISC_INDICATION\n");
		reason = LM_USER_REQUEST;
		break;
	case LAP_NO_RESPONSE:    /* To many retransmits without response */
		DEBUG( 1, __FUNCTION__ "(), LAP_NO_RESPONSE\n");
		reason = LM_LAP_DISCONNECT;
		break;
	case LAP_RESET_INDICATION:
		DEBUG( 1, __FUNCTION__ "(), LAP_RESET_INDICATION\n");
		reason = LM_LAP_RESET;
		break;
	case LAP_FOUND_NONE:
	case LAP_MEDIA_BUSY:
	case LAP_PRIMARY_CONFLICT:
		DEBUG( 1, __FUNCTION__ "(), LAP_FOUND_NONE, LAP_MEDIA_BUSY or LAP_PRIMARY_CONFLICT\n");
		reason = LM_CONNECT_FAILURE;
		break;
	default:
		DEBUG( 1, __FUNCTION__ 
		       "(), Unknow IrLAP disconnect reason %d!\n", lap_reason);
		reason = LM_LAP_DISCONNECT;
		break;
	}

	return reason;
}	

__u32 irlmp_get_saddr(struct lsap_cb *self)
{
	DEBUG(3, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return 0;);
	ASSERT(self->lap != NULL, return 0;);

	return self->lap->saddr;
}

__u32 irlmp_get_daddr(struct lsap_cb *self)
{
	DEBUG(3, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return 0;);
	ASSERT(self->lap != NULL, return 0;);
	
	return self->lap->daddr;
}

#ifdef CONFIG_PROC_FS
/*
 * Function irlmp_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 *
 */
int irlmp_proc_read( char *buf, char **start, off_t offset, int len, 
		     int unused)
{
	struct lsap_cb *self;
	struct lap_cb *lap;
	unsigned long flags;

	ASSERT( irlmp != NULL, return 0;);
	
	save_flags( flags);
	cli();

	len = 0;
	
	len += sprintf( buf+len, "Unconnected LSAPs:\n");
	self = (struct lsap_cb *) hashbin_get_first( irlmp->unconnected_lsaps);
	while ( self != NULL) {
		ASSERT( self->magic == LMP_LSAP_MAGIC, return 0;);
		len += sprintf( buf+len, "lsap state: %s, ", 
				irlsap_state[ self->lsap_state]);
		len += sprintf( buf+len, 
				"slsap_sel: %#02x, dlsap_sel: %#02x, ",
				self->slsap_sel, self->dlsap_sel); 
		len += sprintf( buf+len, "(%s)", self->notify.name);
		len += sprintf( buf+len, "\n");

		self = ( struct lsap_cb *) hashbin_get_next( 
			irlmp->unconnected_lsaps);
 	} 

	len += sprintf( buf+len, "\nRegistred Link Layers:\n");
	lap = (struct lap_cb *) hashbin_get_first( irlmp->links);
	while ( lap != NULL) {
		ASSERT( lap->magic == LMP_LAP_MAGIC, return 0;);

		len += sprintf( buf+len, "lap state: %s, ", 
				irlmp_state[ lap->lap_state]);

		len += sprintf( buf+len, "saddr: %#08x, daddr: %#08x, ",
				lap->saddr, lap->daddr); 
		len += sprintf( buf+len, "\n");

		len += sprintf( buf+len, "\nConnected LSAPs:\n");
		self = (struct lsap_cb *) hashbin_get_first( lap->lsaps);
		while ( self != NULL) {
			ASSERT( self->magic == LMP_LSAP_MAGIC, return 0;);
			len += sprintf( buf+len, "lsap state: %s, ", 
					irlsap_state[ self->lsap_state]);
			len += sprintf( buf+len, 
					"slsap_sel: %#02x, dlsap_sel: %#02x, ",
					self->slsap_sel, self->dlsap_sel);
			len += sprintf( buf+len, "(%s)", self->notify.name);
			len += sprintf( buf+len, "\n");
			
			self = ( struct lsap_cb *) hashbin_get_next( 
				lap->lsaps);
		} 

		lap = ( struct lap_cb *) hashbin_get_next( 
			irlmp->links);
 	} 

	restore_flags( flags);

	return len;
}

#endif /* PROC_FS */



