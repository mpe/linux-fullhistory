/*********************************************************************
 *                
 * Filename:      irlmp.c
 * Version:       0.8
 * Description:   IrDA Link Management Protocol (LMP) layer                 
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 17 20:54:32 1997
 * Modified at:   Sat Jan 16 22:13:20 1999
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

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/timer.h>
#include <net/irda/qos.h>
#include <net/irda/irlap.h>
#include <net/irda/iriap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irlmp_frame.h>
#include <linux/kmod.h>

/* Master structure */
struct irlmp_cb *irlmp = NULL;

int sysctl_discovery = 0;
char sysctl_devname[65];

__u8 *irlmp_hint_to_service( __u8 *hint);
#ifdef CONFIG_PROC_FS
int irlmp_proc_read( char *buf, char **start, off_t offset, int len, 
		     int unused);
#endif

/*
 * Function irlmp_init (void)
 *
 *    Create (allocate) the main IrLMP structure and the pointer array
 *    which will contain pointers to each instance of a LSAP.
 */
__initfunc(int irlmp_init(void))
{
	DEBUG( 4, "--> irlmp_init\n");

	/* Initialize the irlmp structure. */
	if ( irlmp == NULL) {
		irlmp = kmalloc( sizeof(struct irlmp_cb), GFP_KERNEL);
		if ( irlmp == NULL)
			return -ENOMEM;
	}
	memset( irlmp, 0, sizeof(struct irlmp_cb));
	
	irlmp->magic = LMP_MAGIC;

	irlmp->registry = hashbin_new( HB_LOCAL);
	irlmp->links = hashbin_new( HB_LOCAL);
	irlmp->unconnected_lsaps = hashbin_new( HB_GLOBAL);
	
	irlmp->free_lsap_sel = 0x10; /* Servers use 0x00-0x0f */
#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
	irlmp->cache.valid = FALSE;
#endif
	strcpy( sysctl_devname, "Linux");
	
	/* Do discovery every 3 seconds */
	init_timer( &irlmp->discovery_timer);
   	irlmp_start_discovery_timer( irlmp, 600); 

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
	ASSERT( irlmp != NULL, return;);
	ASSERT( irlmp->magic == LMP_MAGIC, return;);

	del_timer( &irlmp->discovery_timer);
	
	/* FIXME, we need a special function to deallocate LAPs */
	hashbin_delete( irlmp->links, (FREE_FUNC) kfree);
	hashbin_delete( irlmp->unconnected_lsaps, (FREE_FUNC) kfree);
	hashbin_delete( irlmp->registry, (FREE_FUNC) kfree);
	
	/* De-allocate main structure */
	kfree( irlmp);
	irlmp = NULL;
}

/*
 * Function irlmp_open_lsap (slsap, notify)
 *
 *   Register with IrLMP and create a local LSAP,
 *   returns handle to LSAP.
 */
struct lsap_cb *irlmp_open_lsap( __u8 slsap_sel, struct notify_t *notify)
{
	struct lsap_cb *self;

	ASSERT( notify != NULL, return NULL;);
	ASSERT( irlmp != NULL, return NULL;);
	ASSERT( irlmp->magic == LMP_MAGIC, return NULL;);

	DEBUG( 4, "irlmp_open_lsap(), slsap_sel=%02x\n", slsap_sel);

	/* 
	 *  Does the client care which Source LSAP selector it gets? 
	 */
	if ( slsap_sel == LSAP_ANY) {
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
		if ( irlmp_slsap_inuse( slsap_sel)) {
			return NULL;
		}
		if ( slsap_sel > irlmp->free_lsap_sel)
			irlmp->free_lsap_sel = slsap_sel+1;
	}

	/*
	 *  Allocate new instance of a LSAP connection
	 */
	self = kmalloc( sizeof(struct lsap_cb), GFP_ATOMIC);
	if ( self == NULL) {
		printk( KERN_ERR "IrLMP: Can't allocate memory for "
			"LSAP control block!\n");
		return NULL;
	}
	memset( self, 0, sizeof(struct lsap_cb));
	
	self->magic = LMP_LSAP_MAGIC;
	self->slsap_sel = slsap_sel;
	self->dlsap_sel = LSAP_ANY;

	init_timer( &self->watchdog_timer);

	ASSERT( notify->instance != NULL, return NULL;);
	self->notify = *notify;

	irlmp_next_lsap_state( self, LSAP_DISCONNECTED);
	
	/*
	 *  Insert into queue of unconnected LSAPs
	 */
	hashbin_insert( irlmp->unconnected_lsaps, (QUEUE *) self, 
			self->slsap_sel, NULL);
	
	return self;
}

/*
 * Function irlmp_close_lsap (self)
 *
 *    Remove an instance of a LSAP
 */
static void __irlmp_close_lsap( struct lsap_cb *self)
{
	DEBUG( 4, "irlmp_close()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	/*
	 *  Set some of the variables to preset values
	 */
	self->magic = ~LMP_LSAP_MAGIC;
	del_timer( &self->watchdog_timer); /* Important! */

#ifdef CONFIG_IRDA_CACHE_LAST_LSAP
	ASSERT( irlmp != NULL, return;);
	irlmp->cache.valid = FALSE;
#endif
	/*
	 *  Deallocate structure
	 */
	kfree( self);

	DEBUG( 4, "irlmp_close() -->\n");
}

/*
 * Function irlmp_close_lsap (self)
 *
 *    
 *
 */
void irlmp_close_lsap( struct lsap_cb *self)
{
	struct lap_cb *lap;
	struct lsap_cb *lsap;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	lap = self->lap;

	/*
	 *  Find out if we should remove this LSAP from a link or from the
	 *  list of unconnected lsaps (not associated with a link)
	 */
	if ( lap == NULL) {
		lsap = hashbin_remove( irlmp->unconnected_lsaps, 
				       self->slsap_sel, NULL);
	} else {
		ASSERT( lap != NULL, return;);
		ASSERT( lap->magic == LMP_LAP_MAGIC, return;);
		
		lsap = hashbin_remove( lap->lsaps, self->slsap_sel, NULL);
	}
	if ( lsap == NULL) {
		DEBUG( 0, __FUNCTION__ 
		       "(), Looks like somebody has removed me already!\n");
		return;
	}
	ASSERT( lsap == self, return;);

	__irlmp_close_lsap( self);
}

/*
 * Function irlmp_register_irlap (saddr, notify)
 *
 *    Register IrLAP layer with IrLMP. There is possible to have multiple
 *    instances of the IrLAP layer, each connected to different IrDA ports
 *
 */
void irlmp_register_irlap( struct irlap_cb *irlap, __u32 saddr, 
			   struct notify_t *notify)
{
	struct lap_cb *lap;

	DEBUG( 4, __FUNCTION__ "(), Registered IrLAP, saddr = %08x\n",
	       saddr);
	
	ASSERT( irlmp != NULL, return;);
	ASSERT( irlmp->magic == LMP_MAGIC, return;);
	ASSERT( notify != NULL, return;);

	/*
	 *  Allocate new instance of a LSAP connection
	 */
	lap = kmalloc( sizeof(struct lap_cb), GFP_KERNEL);
	if ( lap == NULL) {
		printk( KERN_ERR "IrLMP: Can't allocate memory for "
			"LAP control block!\n");
		return;
	}
	memset( lap, 0, sizeof(struct lap_cb));
	
	lap->irlap = irlap;
	lap->magic = LMP_LAP_MAGIC;
	lap->saddr = saddr;
	lap->lsaps = hashbin_new( HB_GLOBAL);
 	lap->cachelog = hashbin_new( HB_LOCAL);

	irlmp_next_lap_state( lap, LAP_STANDBY);
	
	/*
	 *  Insert into queue of unconnected LSAPs
	 */
	hashbin_insert( irlmp->links, (QUEUE *) lap, lap->saddr, NULL);

	/* 
	 *  We set only this variable so IrLAP can tell us on which link the
	 *  different events happened on 
	 */
	irda_notify_init( notify);
	notify->instance = lap;
}

/*
 * Function irlmp_unregister_irlap (saddr)
 *
 *    IrLAP layer has been removed!
 *
 */
void irlmp_unregister_irlap( __u32 saddr)
{
	struct lap_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");

	self = hashbin_remove( irlmp->links, saddr, NULL);
	if ( self != NULL) {
		ASSERT( self->magic == LMP_LAP_MAGIC, return;);

		self->magic = ~LMP_LAP_MAGIC;
		kfree( self);
	} else {
		DEBUG( 0, "irlmp_unregister_irlap(), Didn't find LAP!\n");
	}
}

void dump_discoveries( hashbin_t *log)
{
	DISCOVERY *d;

	ASSERT( log != NULL, return;);

	d = (DISCOVERY *) hashbin_get_first( log);
	while( d != NULL) {
		DEBUG( 0, "Discovery:\n");
		DEBUG( 0, "  daddr=%08x\n", d->daddr);
		DEBUG( 0, "  name=%s\n", d->info);

		d = (DISCOVERY *) hashbin_get_next( log);
	}
}

/*
 * Function irlmp_connect_request (handle, dlsap, userdata)
 *
 *    Connect with a peer LSAP  
 *
 */
void irlmp_connect_request( struct lsap_cb *self, __u8 dlsap_sel, __u32 daddr, 
			    struct qos_info *qos, struct sk_buff *userdata) 
{
	struct sk_buff *skb = NULL;
	struct lap_cb *lap;
	struct lsap_cb *lsap;
	unsigned long flags;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
	
	DEBUG( 4, "irlmp_connect_request(), "
	       "slsap_sel=%02x, dlsap_sel=%02x, daddr=%08x\n", 
	       self->slsap_sel, dlsap_sel, daddr);

	if ( self->connected) {
		DEBUG( 0, __FUNCTION__ "(), Error: already connected!!\n");
		
		return;
	}

	/* Any userdata? */
	if ( userdata == NULL) {
		skb = dev_alloc_skb( 64);
		if (skb == NULL) {
			DEBUG( 0, __FUNCTION__ 
			       "(), Could not allocate sk_buff of length %d\n",
			       64);
			return;
		}
		skb_reserve( skb, LMP_CONTROL_HEADER+LAP_HEADER);
	} else
		skb = userdata;
	
	/* Make room for MUX control header ( 3 bytes) */
	ASSERT( skb_headroom( skb) >= LMP_CONTROL_HEADER, return;);
	skb_push( skb, LMP_CONTROL_HEADER);

	self->dlsap_sel = dlsap_sel;
	self->tmp_skb = skb;
	
	/* 
	 *  Find out which link to connect on, and make sure nothing strange
	 *  happens while we traverse the list
	 */
	save_flags( flags);
	cli();

	lap = (struct lap_cb *) hashbin_get_first( irlmp->links);
	while ( lap != NULL) {
		ASSERT( lap->magic == LMP_LAP_MAGIC, return;);
		/* dump_discoveries( lap->cachelog); */

		if ( hashbin_find( lap->cachelog, daddr, NULL)) {
			DEBUG( 4, "irlmp_connect_request() found link to connect on!\n");
			self->lap = lap;
			break;
		}
		lap = (struct lap_cb *) hashbin_get_next( irlmp->links);
	}
	restore_flags(flags);
	
	/* 
	 *  Remove LSAP from list of unconnected LSAPs and insert it into the 
	 *  list of connected LSAPs for the particular link */
	lsap = hashbin_remove( irlmp->unconnected_lsaps, self->slsap_sel, 
			       NULL);

	/* Check if we found a link to connect on */
	if ( self->lap == NULL) {
		DEBUG( 0, __FUNCTION__ "(), Unable to find a usable link!\n");
		return;
	}

	ASSERT( lsap != NULL, return;);
	ASSERT( lsap->magic == LMP_LSAP_MAGIC, return;);
	ASSERT( lsap->lap != NULL, return;);
	ASSERT( lsap->lap->magic == LMP_LAP_MAGIC, return;);

	hashbin_insert( self->lap->lsaps, (QUEUE *) self, self->slsap_sel, 
			NULL);

	self->connected = TRUE;

	/*
	 *  User supplied qos specifications?
	 */
	if ( qos)
		self->qos = *qos;
	
	DEBUG( 4, "*** Connecting SLSAP=%02x, DLSAP= %02x\n",
	       self->slsap_sel, self->dlsap_sel);
	
	irlmp_do_lsap_event( self, LM_CONNECT_REQUEST, skb);
}

/*
 * Function irlmp_connect_indication (self)
 *
 *    Incomming connection
 *
 */
void irlmp_connect_indication( struct lsap_cb *self, struct sk_buff *skb) 
{
	int max_seg_size;

	DEBUG( 4, "irlmp_connect_indication()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	ASSERT( self->lap != NULL, return;);

	self->qos = *self->lap->qos;

	max_seg_size = self->lap->qos->data_size.value;
	DEBUG( 4, __FUNCTION__ "(), max_seg_size=%d\n", max_seg_size);
	
	/* Hide LMP_CONTROL_HEADER header from layer above */
	skb_pull( skb, LMP_CONTROL_HEADER);

	if ( self->notify.connect_indication)
		self->notify.connect_indication( self->notify.instance, self, 
						 &self->qos, max_seg_size, 
						 skb);
}

/*
 * Function irlmp_connect_response (handle, userdata)
 *
 *    Service user is accepting connection
 *
 */
void irlmp_connect_response( struct lsap_cb *self, struct sk_buff *userdata) 
{
	DEBUG( 4, "irlmp_connect_response()\n");
	
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
void irlmp_connect_confirm( struct lsap_cb *self, struct sk_buff *skb) 
{
	int max_seg_size;

	DEBUG( 4, __FUNCTION__ "()\n");
	
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
 * Function irlmp_disconnect_request (handle, userdata)
 *
 *    The service user is requesting disconnection, this will not remove the 
 *    LSAP, but only mark it as disconnected
 */
void irlmp_disconnect_request( struct lsap_cb *self, struct sk_buff *userdata) 
{
	struct lsap_cb *lsap;

	DEBUG( 4, "irlmp_disconnect_request()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);

	/* Already disconnected? */
	if ( !self->connected) {
		DEBUG( 0, __FUNCTION__ "(), already disconnected!\n");
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

	lsap = hashbin_remove( self->lap->lsaps, self->slsap_sel, NULL);

	ASSERT( lsap != NULL, return;);
	ASSERT( lsap->magic == LMP_LSAP_MAGIC, return;);
	ASSERT( lsap == self, return;);

	hashbin_insert( irlmp->unconnected_lsaps, (QUEUE *) self, 
			self->slsap_sel, NULL);
	
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

	DEBUG( 4, __FUNCTION__ "()\n");	

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
 	ASSERT( self->connected == TRUE, return;); 

	DEBUG( 4, __FUNCTION__ "(), slsap_sel=%02x, dlsap_sel=%02x\n", 
	       self->slsap_sel, self->dlsap_sel);

	self->connected = FALSE;
	self->dlsap_sel = LSAP_ANY;

	/* 
	 *  Remove assosiation betwen this LSAP and the kink it used 
	 */
	ASSERT( self->lap != NULL, return;);
	ASSERT( self->lap->lsaps != NULL, return;);

	lsap = hashbin_remove( self->lap->lsaps, self->slsap_sel, NULL);

	ASSERT( lsap != NULL, return;);
	ASSERT( lsap == self, return;);
	hashbin_insert( irlmp->unconnected_lsaps, (QUEUE *) lsap, 
			lsap->slsap_sel, NULL);

	self->lap = NULL;

	/* FIXME: the reasons should be extracted somewhere else? */
	if ( userdata) {
		DEBUG( 4, __FUNCTION__ "(), reason=%02x\n", userdata->data[3]);
	}
	
	/*
	 *  Inform service user
	 */
	if ( self->notify.disconnect_indication) {
		self->notify.disconnect_indication( self->notify.instance, 
						    self, reason, userdata);
	}
}

/*
 * Function irlmp_discovery_request (nslots)
 *
 *    Do a discovery of devices in front of the computer
 *
 */
void irlmp_discovery_request( int nslots)
{
	struct lap_cb *lap;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( irlmp != NULL, return;);

	if ( !sysctl_discovery)
		return;

	/*
	 *  Construct new discovery info to be used by IrLAP,
	 *  TODO: no need to do this every time!
	 */
	irlmp->discovery_cmd.hint[0] = irlmp->hint[0];
	irlmp->discovery_cmd.hint[1] = irlmp->hint[1];
	
	/* 
	 *  Set character set for device name (we use ASCII), and 
	 *  copy device name. Remember to make room for a \0 at the 
	 *  end
	 */
	irlmp->discovery_cmd.charset = CS_ASCII;
	
	strncpy( irlmp->discovery_cmd.info, sysctl_devname, 31);
	irlmp->discovery_cmd.info_len = strlen( irlmp->discovery_cmd.info);
	
	/*
	 * Try to send discovery packets on all links
	 */
	lap = ( struct lap_cb *) hashbin_get_first( irlmp->links);
	while ( lap != NULL) {
		ASSERT( lap->magic == LMP_LAP_MAGIC, return;);

		DEBUG( 4, "irlmp_discovery_request() sending request!\n");
		irlmp_do_lap_event( lap, LM_LAP_DISCOVERY_REQUEST, NULL);
		
		lap = ( struct lap_cb *) hashbin_get_next( irlmp->links);
	}
}

/*
 * Function irlmp_check_services (discovery)
 *
 *    
 *
 */
void irlmp_check_services( DISCOVERY *discovery) 
{
	struct irlmp_registration *entry;
	struct irmanager_event event;
	__u8 *service;
	int i = 0;

	printk( KERN_INFO "IrDA Discovered: %s\n", discovery->info);
	printk( KERN_INFO "    Services: ");

	service = irlmp_hint_to_service( discovery->hint);
	if (service != NULL) {
		/*
		 *  Check all services on the device
		 */
		while ( service[i] != S_END) {
			DEBUG( 4, "service=%02x\n", service[i]);
			entry = hashbin_find( irlmp->registry, 
					      service[i], NULL);
			if ( entry && entry->discovery_callback) {
				DEBUG( 4, "discovery_callback!\n");
					entry->discovery_callback( discovery);
			} else {
				/* 
				 * Found no clients for dealing with this
				 * service, so ask the user space irmanager
				 * to try to load the right module for us
				 */

				event.event = EVENT_DEVICE_DISCOVERED;
				event.service = service[i];
				event.daddr = discovery->daddr;
				sprintf( event.info, "%s", 
					 discovery->info);
				irmanager_notify( &event);
			}
			i++; /* Next service */
		}
		kfree( service);
	}
}

/*
 * Function irlmp_discovery_confirm ( self, log)
 *
 *    Some device(s) answered to our discovery request! Check to see which
 *    device it is, and give indication to the client(s)
 * 
 */
void irlmp_discovery_confirm( struct lap_cb *self, hashbin_t *log) 
{
	DISCOVERY *discovery;
	
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LAP_MAGIC, return;);

	/*
	 *  Now, check all discovered devices (if any)
	 */
	discovery = ( DISCOVERY *) hashbin_get_first( log);
	while ( discovery != NULL) {
		self->daddr = discovery->daddr;

		DEBUG( 4, "discovery->daddr = 0x%08x\n", discovery->daddr); 
		
		irlmp_check_services( discovery);

		discovery = ( DISCOVERY *) hashbin_get_next( log);
	}
}

/*
 * Function irlmp_discovery_indication (discovery)
 *
 *    A remote device is discovering us!
 *
 */
void irlmp_discovery_indication( struct lap_cb *self, DISCOVERY *discovery)
{
	/* struct irda_event event; */

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LAP_MAGIC, return;);
	ASSERT( discovery != NULL, return;);

	DEBUG( 4, __FUNCTION__ "()\n");

	DEBUG( 4, "discovery->daddr = 0x%08x\n", discovery->daddr); 
	self->daddr = discovery->daddr;

	/*
	 *  Create a new discovery log if neccessary
	 */
	/* if ( self->cachelog == NULL) */
/* 		self->cachelog = hashbin_new( HB_LOCAL); */
	ASSERT( self->cachelog != NULL, return;);

	/*
	 *  Insert this discovery device into the discovery_log if its
	 *  not there already
	 */
	if ( !hashbin_find( self->cachelog, discovery->daddr, NULL))
		hashbin_insert( self->cachelog, (QUEUE *) discovery,
				discovery->daddr, NULL);

	irlmp_check_services( discovery);
}

/*
 * Function irlmp_get_discovery_response ()
 *
 *    Used by IrLAP to get the disocvery info it needs when answering
 *    discovery requests by other devices.
 */
DISCOVERY *irlmp_get_discovery_response()
{
	DEBUG( 4, "irlmp_get_discovery_response()\n");

	ASSERT( irlmp != NULL, return NULL;);

	irlmp->discovery_rsp.hint[0] = irlmp->hint[0];
	irlmp->discovery_rsp.hint[1] = irlmp->hint[1];

	/* 
	 *  Set character set for device name (we use ASCII), and 
	 *  copy device name. Remember to make room for a \0 at the 
	 *  end
	 */
	irlmp->discovery_rsp.charset = CS_ASCII;

	strncpy( irlmp->discovery_rsp.info, sysctl_devname, 31);
	irlmp->discovery_rsp.info_len = strlen( irlmp->discovery_rsp.info) + 2;

	return &irlmp->discovery_rsp;
}

/*
 * Function irlmp_data_request (self, skb)
 *
 *    Send some data to peer device
 *
 */
void irlmp_data_request( struct lsap_cb *self, struct sk_buff *skb) 
{
 	DEBUG( 4, __FUNCTION__ "()\n"); 

	ASSERT( skb != NULL, return;);
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
	
	/* Make room for MUX header */
	ASSERT( skb_headroom( skb) >= LMP_HEADER, return;);
	skb_push( skb, LMP_HEADER);

	irlmp_do_lsap_event( self, LM_DATA_REQUEST, skb);
}

/*
 * Function irlmp_data_indication (handle, skb)
 *
 *    Got data from LAP layer so pass it up to upper layer
 *
 */
void irlmp_data_indication( struct lsap_cb *self, struct sk_buff *skb) 
{
 	DEBUG( 4, "irlmp_data_indication()\n"); 

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Hide LMP header from layer above */
	skb_pull( skb, LMP_HEADER);

	if ( self->notify.data_indication)
		self->notify.data_indication(self->notify.instance, self, skb);
}

/*
 * Function irlmp_udata_request (self, skb)
 *
 *    
 *
 */
void irlmp_udata_request( struct lsap_cb *self, struct sk_buff *skb) 
{
 	DEBUG( 4, __FUNCTION__ "()\n"); 

	ASSERT( skb != NULL, return;);
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == LMP_LSAP_MAGIC, return;);
	
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

	if ( self->notify.udata_indication)
		self->notify.udata_indication( self->notify.instance, self, 
					      skb);
}

/*
 * Function irlmp_connection_less_data_request (skb)
 *
 *    Send out of connection UI frames
 *
 */
void irlmp_connectionless_data_request( struct sk_buff *skb)
{
	DEBUG( 0, __FUNCTION__ "(), Sorry not implemented\n"); 
}

/*
 * Function irlmp_connection_less_data_indication (skb)
 *
 *    
 *
 */
void irlmp_connectionless_data_indication( struct sk_buff *skb)
{
	DEBUG( 0, __FUNCTION__ "()\n"); 
}

void irlmp_status_request(void) 
{
	DEBUG( 0, "irlmp_status_request(), Not implemented\n");
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
__u8 *irlmp_hint_to_service( __u8 *hint)
{
	__u8 *service;
	int i = 0;

	/* Allocate array to store services in */
	service = kmalloc( 16, GFP_ATOMIC);
	if ( !service) {
		DEBUG( 0, "irlmp_hint_to_service: Unable to kmalloc!\n");
		return NULL;
	}

	if ( !hint[0]) {
		printk( "<None>\n");
		return NULL;
	}
	if ( hint[0] & HINT_PNP)
		printk( "PnP Compatible ");
	if ( hint[0] & HINT_PDA)
		printk( "PDA/Palmtop ");
	if ( hint[0] & HINT_COMPUTER)
		printk( "Computer ");
	if ( hint[0] & HINT_PRINTER) {
		printk( "Printer\n");
		service[i++] = S_PRINTER;
	}
	if ( hint[0] & HINT_MODEM)
		printk( "Modem ");
	if ( hint[0] & HINT_FAX)
		printk( "Fax ");
	if ( hint[0] & HINT_LAN) {
		printk( "LAN Access\n");			
		service[i++] = S_LAN;
	}
	/* 
	 *  Test if extension byte exists. This byte will usually be
	 *  there, but this is not really required by the standard.
	 *  (IrLMP p. 29)
	 */
	if ( hint[0] & HINT_EXTENSION) {
		if ( hint[1] & HINT_TELEPHONY)
			printk( "Telephony ");
		
		if ( hint[1] & HINT_FILE_SERVER)
			printk( "File Server ");
		
		if ( hint[1] & HINT_COMM) {
			printk( "IrCOMM ");
			service[i++] = S_COMM;
		}
		if ( hint[1] & HINT_OBEX) {
			printk( "IrOBEX ");
			service[i++] = S_OBEX;
		}
	}
	printk( "\n");

	service[i] = S_END;
	
	return service;
}

/*
 * Function irlmp_service_to_hint (service, hint)
 *
 *    
 *
 */
void irlmp_service_to_hint( int service, __u8 *hint)
{
	switch (service) {
	case S_PNP:
		hint[0] |= HINT_PNP;
		break;
	case S_PDA:
		hint[0] |= HINT_PDA;
		break;
	case S_COMPUTER:
		hint[0] |= HINT_COMPUTER;
		break;
	case S_PRINTER:
		hint[0] |= HINT_PRINTER;
		break;
	case S_MODEM:
		hint[0] |= HINT_PRINTER;
		break;
	case S_LAN:
		hint[0] |= HINT_LAN;
		break;
	case S_COMM:
		hint[0] |= HINT_EXTENSION;
		hint[1] |= HINT_COMM;
		break;
	case S_OBEX:
		hint[0] |= HINT_EXTENSION;
		hint[1] |= HINT_OBEX;
		break;
	default:
		DEBUG( 0, "irlmp_service_to_hint(), Unknown service!\n");
		break;
	}
}

/*
 * Function irlmp_register (service, type, callback)
 *
 *    Register a local client or server with IrLMP
 *
 */
void irlmp_register_layer( int service, int type, int do_discovery, 
			   DISCOVERY_CALLBACK callback)
{
	struct irlmp_registration *entry;

	sysctl_discovery |= do_discovery;

	if ( type & SERVER)
		irlmp_service_to_hint( service, irlmp->hint);

	/* Check if this service has been registred before */
	entry = hashbin_find( irlmp->registry, service, NULL);
	if ( entry != NULL) {
		/* Update type in entry */
		entry->type |= type;

		/*  Update callback only if client, since servers don't 
		 *  use callbacks, and we don't want to overwrite a 
		 *  previous registred client callback
		 */
		if ( type & CLIENT)
			entry->discovery_callback = callback;
		return;
	}

	/* Make a new registration */
 	entry = kmalloc( sizeof( struct irlmp_registration), GFP_ATOMIC);
	if ( !entry) {
		DEBUG( 0, "irlmp_register(), Unable to kmalloc!\n");
		return;
	}

	entry->service = service;
	entry->type = type;
	entry->discovery_callback = callback;

 	hashbin_insert( irlmp->registry, (QUEUE*) entry, entry->service, NULL);
}

/*
 * Function irlmp_unregister (serivice)
 *
 *    
 *
 */
void irlmp_unregister_layer( int service, int type)
{
 	struct irlmp_registration *entry;
 
 	DEBUG( 4, __FUNCTION__ "()\n");
 
	entry = hashbin_find( irlmp->registry, service, NULL);
	if ( entry != NULL) {
		DEBUG( 4, "Found entry to change or remove!\n");
		/* Remove this type from the service registration */
		entry->type &= ~type;
	}

	if ( !entry) {
		DEBUG( 0, "Unable to find entry to unregister!\n");
		return;
	}

	/* 
	 *  Remove entry if there is no more client and server support 
	 *  left in entry
	 */
	if ( !entry->type) {
		DEBUG( 4, __FUNCTION__ "(), removing entry!\n");
		entry = hashbin_remove( irlmp->registry, service, NULL);
		if ( entry != NULL)
			kfree( entry);
	}

	/* Remove old hint bits */
	irlmp->hint[0] = 0;
	irlmp->hint[1] = 0;

	/* Refresh current hint bits */
        entry = (struct irlmp_registration *) hashbin_get_first( irlmp->registry);
        while( entry != NULL) {
		if ( entry->type & SERVER)
			irlmp_service_to_hint( entry->service, 
					       irlmp->hint);
                entry = (struct irlmp_registration *) 
			hashbin_get_next( irlmp->registry);
        }
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

	DEBUG( 4, "irlmp_slsap_inuse()\n");

	/*
	 *  Check if slsap is already in use. To do this we have to loop over
	 *  every IrLAP connection and check every LSAP assosiated with each
	 *  the connection.
	 */
	lap = ( struct lap_cb *) hashbin_get_first( irlmp->links);
	while ( lap != NULL) {
		ASSERT( lap->magic == LMP_LAP_MAGIC, return TRUE;);

		self = (struct lsap_cb *) hashbin_get_first( lap->lsaps);
		while ( self != NULL) {
			ASSERT( self->magic == LMP_LSAP_MAGIC, return TRUE;);

			if (( self->slsap_sel == slsap_sel))/*  &&  */
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

	ASSERT( irlmp != NULL, return -1;);
	ASSERT( irlmp->magic == LMP_MAGIC, return -1;);
      
	lsap_sel = irlmp->free_lsap_sel++;

	DEBUG( 4, "irlmp_find_free_slsap(), picked next free lsap_sel=%02x\n",
	       lsap_sel);

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
		reason = LM_USER_REQUEST;
		break;
	case LAP_NO_RESPONSE:    /* To many retransmits without response */
		reason = LM_LAP_DISCONNECT;
		break;
	case LAP_RESET_INDICATION:
		reason = LM_LAP_RESET;
		break;
	case LAP_FOUND_NONE:
	case LAP_MEDIA_BUSY:
	case LAP_PRIMARY_CONFLICT:
		reason = LM_CONNECT_FAILURE;
		break;
	default:
		DEBUG( 0, __FUNCTION__ 
		       "(), Unknow IrLAP disconnect reason %d!\n", lap_reason);
		reason = LM_LAP_DISCONNECT;
		break;
	}

	return reason;
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

		len += sprintf( buf+len, 
				"saddr: %#08x, daddr: %#08x, ",
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



