/*********************************************************************
 *
 * Filename:      irlpt.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Thomas Davis, <ratbert@radiks.net>
 * Created at:    Sat Feb 21 18:54:38 1998
 * Modified at:   Sun Mar  8 23:44:19 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:	  irlan.c
 *
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>,
 *     Copyright (c) 1998, Dag Brattli,  <dagb@cs.uit.no>
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     I, Thomas Davis, provide no warranty for any of this software.
 *     This material is provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <net/irda/irlap.h>
#include <net/irda/irttp.h>
#include <net/irda/irlmp.h>
#include <net/irda/irias_object.h>
#include <net/irda/iriap.h>
#include <net/irda/irlpt_common.h>
#include <net/irda/irlpt_cli.h>
#include <net/irda/irlpt_cli_fsm.h>
#include <net/irda/timer.h>
#include <net/irda/irda.h>

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>

int  irlpt_client_init(void);
static void irlpt_client_cleanup(void);
static void irlpt_client_close(struct irlpt_cb *self);

static void irlpt_client_discovery_indication( DISCOVERY *);

static void irlpt_client_connect_confirm( void *instance, void *sap, 
					  struct qos_info *qos, 
					  int max_seg_size, 
					  struct sk_buff *skb);
static void irlpt_client_disconnect_indication( void *instance, void *sap, 
						LM_REASON reason,
						struct sk_buff *userdata);

#if 0
static char *rcsid = "$Id: irlpt_client.c,v 1.10 1998/11/10 22:50:57 dagb Exp $";
#endif
static char *version = "IrLPT, $Revision: 1.10 $/$Date: 1998/11/10 22:50:57 $ (Thomas Davis)";

struct file_operations client_fops = {
	irlpt_seek,    /* seek */
	NULL,          /* read_irlpt (server) */
	irlpt_write,   /* write */
	NULL,	       /* readdir */
	NULL,          /* poll */
	NULL,	       /* ioctl */
	NULL,	       /* mmap */
	irlpt_open,    /* open */
	NULL,          /* flush */
	irlpt_close,   /* release */
	NULL,          /* fsync */
	NULL,          /* fasync */
	NULL,          /* check_media_change */
	NULL,          /* revalidate */
	NULL,          /* lock */
};

int irlpt_client_debug = 4;

extern char *irlptstate[];

#ifdef CONFIG_PROC_FS

/*
 * Function client_proc_read (buf, start, offset, len, unused)
 *
 */
static int irlpt_client_proc_read( char *buf, char **start, off_t offset, 
				   int len, int unused)
{
	struct irlpt_cb *self;
	int index;

	len = sprintf(buf, "%s\n\n", version);

	self = (struct irlpt_cb *) hashbin_get_first( irlpt_clients);
	while( self) {
	        ASSERT( self != NULL, return len;);
          	ASSERT( self->magic == IRLPT_MAGIC, return len;);

		len += sprintf(buf+len, "ifname: %s\n", self->ifname);
		len += sprintf(buf+len, "minor: %d\n", self->ir_dev.minor);

		switch ( self->servicetype) {
		case IRLPT_UNKNOWN:
			index = 0;
			break;
		case IRLPT_THREE_WIRE_RAW:
			index = 1;
			break;
		case IRLPT_THREE_WIRE:
			index = 2;
			break;
		case IRLPT_NINE_WIRE:
			index = 3;
			break;
		case IRLPT_CENTRONICS:
			index = 4;
			break;
		case IRLPT_SERVER_MODE:
			index = 5;
			break;
		default:
			index = 0;
			break;
		}
		
		len += sprintf(buf+len, "service_type: %s\n", 
			       irlpt_service_type[index]);
		len += sprintf(buf+len, "port_type: %s\n", 
			       irlpt_port_type[ self->porttype]);
		len += sprintf(buf+len, "daddr: 0x%08x\n", self->daddr);
		len += sprintf(buf+len, "fsm_state: %s\n", 
			       irlpt_client_fsm_state[self->state]);
		len += sprintf(buf+len, "retries: %d\n", self->open_retries);
		len += sprintf(buf+len, "dlsap: %d\n", self->dlsap_sel);

		len += sprintf(buf+len, "count: %d\n", self->count);
		len += sprintf(buf+len, "rx_queue: %d\n", 
			       skb_queue_len(&self->rx_queue));
		len += sprintf(buf+len, "\n\n");
		
		self = (struct irlpt_cb *) hashbin_get_next( irlpt_clients);
	}
	
	return len;
}

struct proc_dir_entry proc_irlpt_client = {
	0, 12, "irlpt_client",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL /* ops -- default to array */,
	&irlpt_client_proc_read /* get_info */,
};

extern struct proc_dir_entry proc_irda;

#endif /* CONFIG_PROC_FS */

/*
 * Function irlpt_init (dev)
 *
 *   Initializes the irlpt control structure
 *
 */
__initfunc(int irlpt_client_init(void))
{
	DEBUG( irlpt_client_debug, "--> "__FUNCTION__ "\n");

	printk( KERN_INFO "%s\n", version);

	irlpt_clients = hashbin_new( HB_LOCAL); 
	if ( irlpt_clients == NULL) {
		printk( KERN_WARNING "IrLPT: Can't allocate hashbin!\n");
		return -ENOMEM;
	}

	irlmp_register_layer( S_PRINTER, CLIENT, TRUE, 
			      irlpt_client_discovery_indication);

#ifdef CONFIG_PROC_FS
	proc_register( &proc_irda, &proc_irlpt_client);
#endif /* CONFIG_PROC_FS */

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");

	return 0;
}


#ifdef MODULE
/*
 * Function irlpt_cleanup (void)
 *
 *
 *
 */
static void irlpt_client_cleanup(void)
{
	DEBUG( irlpt_client_debug, "--> "__FUNCTION__ "\n");

	irlmp_unregister_layer( S_PRINTER, CLIENT);

	/*
	 *  Delete hashbin and close all irlan client instances in it
	 */
	hashbin_delete( irlpt_clients, (FREE_FUNC) irlpt_client_close);

#ifdef CONFIG_PROC_FS
	proc_unregister( &proc_irda, proc_irlpt_client.low_ino);
#endif

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");
}
#endif /* MODULE */


/*
 * Function irlpt_open (void)
 *
 *    This is the entry-point which starts all the fun! Currently this
 *
 */
static struct irlpt_cb *irlpt_client_open( __u32 daddr)
{
	struct irlpt_cb *self;

	DEBUG( irlpt_client_debug, "--> "__FUNCTION__ "\n");

	self = kmalloc(sizeof(struct irlpt_cb), GFP_ATOMIC);
	if (self == NULL)
		return NULL;

	memset(self, 0, sizeof(struct irlpt_cb));

	ASSERT( self != NULL, return NULL;);

 	sprintf(self->ifname, "irlpt%d", hashbin_get_size(irlpt_clients));
	self->ir_dev.minor = MISC_DYNAMIC_MINOR;
	self->ir_dev.name = self->ifname;
	self->ir_dev.fops = &client_fops;

	misc_register(&self->ir_dev);

	self->magic = IRLPT_MAGIC;
	self->in_use = TRUE;
	self->servicetype = IRLPT_THREE_WIRE_RAW;
	self->porttype = IRLPT_SERIAL;

	skb_queue_head_init(&self->rx_queue);

	irlpt_client_next_state( self, IRLPT_CLIENT_IDLE);

	hashbin_insert( irlpt_clients, (QUEUE *) self, daddr, NULL);

	/*	MOD_INC_USE_COUNT; */

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");

	return self;
}

/*
 * Function irlpt_client_close (self)
 *
 *    This function closes and marks the IrLPT instance as not in use.
 */
static void irlpt_client_close( struct irlpt_cb *self)
{
	struct sk_buff *skb;

	DEBUG( irlpt_client_debug, "--> " __FUNCTION__ "\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	while (( skb = skb_dequeue(&self->rx_queue)) != NULL) {
		DEBUG(3, "irlpt_client_close: freeing SKB\n");
                dev_kfree_skb( skb);
	}

	misc_deregister(&self->ir_dev);

	self->magic = ~IRLPT_MAGIC;

	kfree( self);

	/* MOD_DEC_USE_COUNT; */

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");
}

/*
 * Function irlpt_discovery_indication (daddr)
 *
 *    Remote device discovered, try query the remote IAS to see which
 *    device it is, and which services it has.
 *
 */
static void irlpt_client_discovery_indication( DISCOVERY *discovery)
{
	struct irlpt_info info;
	struct irlpt_cb *self;
	__u32 daddr;

	DEBUG( irlpt_client_debug, "--> " __FUNCTION__ "\n");

	ASSERT( irlpt_clients != NULL, return;);
	ASSERT( discovery != NULL, return;);

	daddr = discovery->daddr;

	/*
	 *  Check if an instance is already dealing with this device
	 *  (daddr)
	 */
	self = (struct irlpt_cb *) hashbin_find( irlpt_clients, daddr, NULL);
      	if ( self != NULL) {
		ASSERT( self->magic == IRLPT_MAGIC, return;);
		if ( self->state == IRLPT_CLIENT_IDLE) {
			irlpt_client_do_event( self, 
					       IRLPT_DISCOVERY_INDICATION, 
					       NULL, &info);
		}
		return;
	}
    

	/*
	 * We have no instance for daddr, so time to start a new instance.
	 * First we must find a free entry in master array
	 */
	if (( self = irlpt_client_open( daddr)) == NULL) {
		DEBUG(irlpt_client_debug, __FUNCTION__ 
		      ":irlpt_client_open failed!\n");
	}

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLPT_MAGIC, return;);

	self->daddr = info.daddr = daddr;
	
	if (self->state == IRLPT_CLIENT_IDLE) {
		irlpt_client_do_event( self, IRLPT_DISCOVERY_INDICATION, 
				       NULL, &info);
	}

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");
}

/*
 * Function irlpt_disconnect_indication (handle)
 *
 */
static void irlpt_client_disconnect_indication( void *instance, void *sap, 
						LM_REASON reason,
						struct sk_buff *skb)
{
	struct irlpt_info info;
	struct irlpt_cb *self;

	DEBUG( irlpt_client_debug, "--> " __FUNCTION__ "\n");

	self = ( struct irlpt_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	info.daddr = self->daddr;

        DEBUG( irlpt_client_debug, __FUNCTION__ 
	       ": reason=%d (%s), peersap=%d\n",
	       reason, irlpt_reasons[reason], self->dlsap_sel);

	self->connected = IRLPT_DISCONNECTED;
	self->eof = reason;

	wake_up_interruptible( &self->write_wait);

	irlpt_client_do_event( self, LMP_DISCONNECT, NULL, NULL);

	if (skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");
}

/*
 * Function irlpt_connect_confirm (handle, qos, skb)
 *
 *    LSAP connection confirmed!
 */
static void irlpt_client_connect_confirm( void *instance, void *sap, 
					  struct qos_info *qos, 
					  int max_sdu_size,
					  struct sk_buff *skb)
{
	struct irlpt_info info;
	struct irlpt_cb *self;

	DEBUG( irlpt_client_debug, "--> " __FUNCTION__ "\n");

	self = ( struct irlpt_cb *) instance;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	info.daddr = self->daddr;

	/*
	 *  Check if we have got some QoS parameters back! This should be the
	 *  negotiated QoS for the link.
	 */
	if ( qos) {
		DEBUG( irlpt_client_debug, __FUNCTION__ ": Frame Size: %d\n",
		       qos->data_size.value);
	}

	self->irlap_data_size = (qos->data_size.value - IRLPT_MAX_HEADER);
	self->connected = TRUE;
	
	irlpt_client_do_event( self, LMP_CONNECT, NULL, NULL);

	if (skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");
}

/*
 * Function client_data_indication (handle, skb)
 *
 *    This function gets the data that is received on the data channel
 *
 */
static void irlpt_client_data_indication( void *instance, void *sap, 
					  struct sk_buff *skb) 
{
	struct irlpt_cb *self;

	DEBUG( irlpt_client_debug, "--> " __FUNCTION__ "\n");

	ASSERT( skb != NULL, return;);
	DEBUG( irlpt_client_debug, __FUNCTION__ ": len=%d\n", (int) skb->len);

	self = ( struct irlpt_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);
#if 1
	{
		int i;

		for(i=0;i<skb->len;i++)
			if (skb->data[i] > 31 && skb->data[i] < 128) {
				printk("%c", skb->data[i]);
			} else {
				if (skb->data[i] == 0x0d) {
					printk("\n");
				} else {
					printk(".");
				}
			}

		printk("\n");
	}
#endif
	
	skb_queue_tail(&self->rx_queue, skb);
        wake_up_interruptible(&self->read_wait);

/* 	if (skb) { */
/* 		dev_kfree_skb( skb); */
/* 	} */

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");
}

/*
 * Function irlpt_get_value_confirm (obj_id, type, value_int, value_char, priv)
 *
 *    Fixed to match changes in iriap.h, DB.
 *
 */

void irlpt_client_get_value_confirm(__u16 obj_id, struct ias_value *value, 
				    void *priv)
{
	struct irlpt_info info;
	struct irlpt_cb *self;

	DEBUG( irlpt_client_debug, "--> " __FUNCTION__ "\n");

	ASSERT( priv != NULL, return;);

	self = (struct irlpt_cb *) priv;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	/* can't stop here..  if we get a bad obj, must tell the state
	   machine that!
	ASSERT( type == IAS_INTEGER, return;);
	*/

	if ( value->type == IAS_INTEGER && value->t.integer != -1) {
	        info.dlsap_sel = value->t.integer;
		self->dlsap_sel = value->t.integer;

		DEBUG( irlpt_client_debug, __FUNCTION__ 
		       ": obj_id = %d, value = %d\n", 
		       obj_id, value->t.integer);

		irlpt_client_do_event( self, IAS_PROVIDER_AVAIL, 
				       NULL, &info);
	} else
		irlpt_client_do_event( self, IAS_PROVIDER_NOT_AVAIL, 
				       NULL, &info);

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");
}

void irlpt_client_connect_request( struct irlpt_cb *self) 
{
	struct notify_t lpt_notify;

	DEBUG( irlpt_client_debug, "--> " __FUNCTION__ "\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	lpt_notify.connect_confirm = irlpt_client_connect_confirm;
	lpt_notify.disconnect_indication = irlpt_client_disconnect_indication;
	lpt_notify.data_indication = irlpt_client_data_indication;
	lpt_notify.instance = self;

	self->lsap = irlmp_open_lsap( LSAP_ANY, &lpt_notify);
	DEBUG( irlpt_client_debug, __FUNCTION__ ": Dest LSAP sel= %d\n", 
	       self->dlsap_sel);
	
	if (self->servicetype == IRLPT_THREE_WIRE_RAW) {
	        DEBUG( irlpt_client_debug, __FUNCTION__ 
		       ": issue THREE_WIRE_RAW connect\n");
		irlmp_connect_request( self->lsap, self->dlsap_sel, 
				       self->daddr, NULL, NULL);
	}

	DEBUG( irlpt_client_debug, __FUNCTION__ " -->\n");
}


#ifdef MODULE

MODULE_AUTHOR("Thomas Davis <ratbert@radiks.net>");
MODULE_DESCRIPTION("The Linux IrDA/IrLPT protocol");

/*
 * Function init_module (void)
 *
 *    Initialize the IrLPT client module, this function is called by the
 *    modprobe(1) program.
 */
int init_module(void)
{

        DEBUG( irlpt_client_debug, "--> irlpt client: init_module\n");

        irlpt_client_init();

        DEBUG( irlpt_client_debug, "irlpt client: init_module -->\n");

        return 0;
}

/*
 * Function cleanup_module (void)
 *
 *    Remove the IrLPT server module, this function is called by the rmmod(1)
 *    program
 */
void cleanup_module(void)
{
        DEBUG( irlpt_client_debug, "--> irlpt client: cleanup_module\n");
        /* No need to check MOD_IN_USE, as sys_delete_module() checks. */

        /* Free some memory */
        irlpt_client_cleanup();

        DEBUG( irlpt_client_debug, "irlpt client: cleanup_module -->\n");
}

#endif /* MODULE */
