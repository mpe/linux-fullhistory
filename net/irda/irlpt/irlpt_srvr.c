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
 *			   Dag Brattli,  <dagb@cs.uit.no>
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

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>

#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irias_object.h>
#include <net/irda/timer.h>
#include <net/irda/irda.h>
#include <net/irda/irlpt_common.h>
#include <net/irda/irlpt_server.h>
#include <net/irda/irlpt_server_fsm.h>

#ifdef CONFIG_PROC_FS
static int irlpt_server_proc_read(char *buf, char **start, off_t offset, 
				  int len, int unused);
#endif /* CONFIG_PROC_FS */

int irlpt_server_init(void);
static void irlpt_server_cleanup(void);
static void irlpt_server_disconnect_indication( void *instance, void *sap, 
						LM_REASON reason,
						struct sk_buff *skb);
static void irlpt_server_connect_confirm( void *instance, void *sap, 
					  struct qos_info *qos,  
					  int max_seg_size,
					  struct sk_buff *skb);
static void irlpt_server_connect_indication( void *instance, void *sap, 
					     struct qos_info *qos, 
					     int max_seg_size,
					     struct sk_buff *skb);
static void irlpt_server_data_indication( void *instance, void *sap, 
					  struct sk_buff *skb);
static void register_irlpt_server(void);
static void deregister_irlpt_server(void);

static struct wait_queue *irlpt_server_wait;

int irlpt_server_lsap = LSAP_IRLPT;
int irlpt_server_debug = 4;

#if 0
static char *rcsid = "$Id: irlpt_server.c,v 1.9 1998/10/22 12:02:22 dagb Exp $";
#endif
static char *version = "IrLPT server, $Revision: 1.9 $/$Date: 1998/10/22 12:02:22 $ (Thomas Davis)";

struct file_operations irlpt_fops = {
	irlpt_seek,	/* seek */
	irlpt_read,     /* read */   
	irlpt_write,
	NULL,		/* readdir */
	NULL,           /* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	irlpt_open,     /* open */
	NULL,           /* flush */
	irlpt_close,    /* release */
	NULL,           /* fsync */
	NULL,           /* fasync */
	NULL,           /* check_media_change */
	NULL,           /* revalidate */
        NULL,           /* lock */
};

#ifdef CONFIG_PROC_FS

/*
 * Function proc_irlpt_read (buf, start, offset, len, unused)
 *
 *
 *
 */
static int irlpt_server_proc_read(char *buf, char **start, off_t offset, 
				  int len, int unused)
{
	int index;

	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");

	if (irlpt_server != NULL) {
	        len = sprintf(buf, "%s\n\n", version);
		len += sprintf(buf+len, "ifname: %s\n", irlpt_server->ifname);
		len += sprintf(buf+len, "minor: %d\n", irlpt_server->ir_dev.minor);

		switch (irlpt_server->servicetype) {
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

		len += sprintf(buf+len, "servicetype: %s\n", 
			       irlpt_service_type[index]);
		len += sprintf(buf+len, "porttype: %s\n", 
			       irlpt_port_type[irlpt_server->porttype]);
		len += sprintf(buf+len, "daddr: %d\n", 
			       irlpt_server->daddr);
		len += sprintf(buf+len, "state: %s\n", 
			       irlpt_server_fsm_state[irlpt_server->state]);
		len += sprintf(buf+len, "retries: %d\n", 
			       irlpt_server->open_retries);
		len += sprintf(buf+len, "peersap: %d\n", 
			       irlpt_server->dlsap_sel);
		len += sprintf(buf+len, "count: %d\n", 
			       irlpt_server->count);
		len += sprintf(buf+len, "rx_queue: %d\n", 
			       skb_queue_len(&irlpt_server->rx_queue));
		len += sprintf(buf+len, "\n");
	}

	DEBUG( irlpt_server_debug, __FUNCTION__ " -->\n");

	return len;
}

extern struct proc_dir_entry proc_irda;

struct proc_dir_entry proc_irlpt_server = {
	0, 12, "irlpt_server",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL /* ops -- default to array */,
	&irlpt_server_proc_read /* get_info */,
};

#endif /* CONFIG_PROC_FS */

/*
 * Function irlpt_init (dev)
 *
 *   Initializes the irlpt control structure
 *
 */

/*int irlpt_init( struct device *dev) {*/
__initfunc(int irlpt_server_init(void))
{
	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");

	printk( KERN_INFO "%s\n", version);

	irlpt_server = (struct irlpt_cb *) kmalloc (sizeof(struct irlpt_cb), 
						    GFP_KERNEL);

	if ( irlpt_server == NULL) {
		printk( KERN_WARNING "IrLPT server: Can't allocate" 
			" irlpt_server control block!\n");
		return -ENOMEM;
	}

	memset( irlpt_server, 0, sizeof(struct irlpt_cb));

	sprintf(irlpt_server->ifname, "irlpt_server");
	irlpt_server->ir_dev.minor = MISC_DYNAMIC_MINOR;
	irlpt_server->ir_dev.name = irlpt_server->ifname;
	irlpt_server->ir_dev.fops = &irlpt_fops;
	misc_register(&irlpt_server->ir_dev);
	irlpt_server->magic = IRLPT_MAGIC;
	irlpt_server->in_use = TRUE;
	irlpt_server->servicetype = IRLPT_THREE_WIRE_RAW;
	irlpt_server->porttype = IRLPT_SERIAL;

	skb_queue_head_init(&irlpt_server->rx_queue);

	irlmp_register_layer( S_PRINTER, SERVER, FALSE, NULL);
	
	register_irlpt_server();

#ifdef CONFIG_PROC_FS
	proc_register( &proc_irda, &proc_irlpt_server);
#endif /* CONFIG_PROC_FS */

	DEBUG( irlpt_server_debug, __FUNCTION__ " -->\n");

	return 0;
}

/*
 * Function irlpt_cleanup (void)
 *
 *
 *
 */
static void irlpt_server_cleanup(void)
{
	struct sk_buff *skb;

	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");

	deregister_irlpt_server();

	while (( skb = skb_dequeue(&irlpt_server->rx_queue)) != NULL) {
		DEBUG(irlpt_server_debug, __FUNCTION__ ": freeing SKB\n");
                IS_SKB( skb, return;);
                FREE_SKB_MAGIC( skb);
                dev_kfree_skb( skb);
	}

	misc_deregister(&irlpt_server->ir_dev);

	kfree(irlpt_server);

#ifdef CONFIG_PROC_FS
	proc_unregister( &proc_irda, proc_irlpt_server.low_ino);
#endif

	DEBUG( irlpt_server_debug, __FUNCTION__ " -->\n");
}

/*
 * Function irlpt_disconnect_indication (handle)
 *
 */
static void irlpt_server_disconnect_indication( void *instance, void *sap, 
						LM_REASON reason,
						struct sk_buff *userdata)
{
	struct irlpt_info info;
	struct irlpt_cb *self;

	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");

	self = ( struct irlpt_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	info.daddr = self->daddr;

	DEBUG( irlpt_server_debug, __FUNCTION__ ": reason=%d (%s), dlsap_sel=%d\n",
	       reason, irlpt_reasons[reason], self->dlsap_sel);

	self->connected = IRLPT_DISCONNECTED;
	self->eof = reason;

        wake_up_interruptible(&irlpt_server_wait);

	DEBUG( irlpt_server_debug, __FUNCTION__ ": skb_queue_len=%d\n",
	       skb_queue_len(&irlpt_server->rx_queue));

	irlpt_server_do_event( self, LMP_DISCONNECT, NULL, NULL);

	deregister_irlpt_server();
	register_irlpt_server();

	DEBUG( irlpt_server_debug, __FUNCTION__ " -->\n");
}

/*
 * Function irlpt_connect_confirm (handle, qos, skb)
 *
 *    LSAP connection confirmed!
 */
static void irlpt_server_connect_confirm( void *instance, void *sap, 
					  struct qos_info *qos,
					  int max_seg_size,
					  struct sk_buff *skb)
{
	struct irlpt_info info;
	struct irlpt_cb *self;

	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");
	self = ( struct irlpt_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	info.daddr = self->daddr;

	/*
	 *  Check if we have got some QoS parameters back! This should be the
	 *  negotiated QoS for the link.
	 */
	if ( qos) {
		DEBUG( irlpt_server_debug, __FUNCTION__ 
		       ": IrLPT Negotiated BAUD_RATE: %02x\n", 
		       qos->baud_rate.bits);			
		DEBUG( irlpt_server_debug, __FUNCTION__ 
		       ": IrLPT Negotiated BAUD_RATE: %d bps.\n", 
		       qos->baud_rate.value);
	}

	self->connected = TRUE;

	irlpt_server_do_event( self, LMP_CONNECT, NULL, NULL);

	DEBUG( irlpt_server_debug, __FUNCTION__ " -->\n");
}

/*
 * Function irlpt_connect_indication (handle)
 *
 */
static void irlpt_server_connect_indication( void *instance, void *sap, 
					     struct qos_info *qos, 
					     int max_seg_size,
					     struct sk_buff *skb)
{
	struct irlpt_cb *self;
	struct irlpt_info info;
	struct lsap_cb *lsap;

	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");

	self = ( struct irlpt_cb *) instance;
	lsap = (struct lsap_cb *) sap;
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	self->connected = IRLPT_CONNECTED;
	self->eof = FALSE;

	info.lsap = lsap;

	irlpt_server_do_event( self, LMP_CONNECT, NULL, &info);

	if (skb) {
		dev_kfree_skb( skb);
	}

	DEBUG( irlpt_server_debug, __FUNCTION__ " -->\n");
}

/*
 * Function irlpt_data_indication (handle, skb)
 *
 *    This function gets the data that is received on the data channel
 *
 */
static void irlpt_server_data_indication( void *instance, void *sap, 
					  struct sk_buff *skb) 
{

	struct irlpt_cb *self;

	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");

	self = ( struct irlpt_cb *) instance;
	     
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLPT_MAGIC, return;);

	ASSERT( skb != NULL, return;);

	DEBUG( irlpt_server_debug, __FUNCTION__ ": len=%d\n", (int) skb->len);

#if 0
	dump_buffer(skb);
#endif
	
	skb_queue_tail(&self->rx_queue, skb);
        wake_up_interruptible(&irlpt_server_wait);

	DEBUG( irlpt_server_debug, __FUNCTION__ " -->\n");
}

/*
 * Function register_irlpt_server(void)
 *
 *    Register server support so we can accept incoming connections. We
 *    must register both a TSAP for control and data
 * 
 */
static void register_irlpt_server(void)
{
	struct notify_t notify;
	struct ias_object *obj;
	
	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");

	/*
	 *  First register control TSAP
	 */

	if ( !irlpt_server || irlpt_server->magic != IRLPT_MAGIC) {
		DEBUG( 0, "irlpt_register_server:, unable to obtain handle!\n");
		return;
	}

	irda_notify_init(&notify);

	notify.connect_confirm = irlpt_server_connect_confirm;
	notify.connect_indication = irlpt_server_connect_indication;
	notify.disconnect_indication = irlpt_server_disconnect_indication;
	notify.data_indication = irlpt_server_data_indication;
	notify.instance = irlpt_server;
	strcpy(notify.name, "IrLPT");

	irlpt_server->lsap = irlmp_open_lsap( irlpt_server_lsap, &notify);

	irlpt_server->connected = IRLPT_WAITING;
	irlpt_server->service_LSAP = irlpt_server_lsap;

	/* 
	 *  Register with LM-IAS
	 */

	obj = irias_new_object("IrLPT", IAS_IRLPT_ID);
	irias_add_integer_attrib(obj, "IrDA:IrLMP:LsapSel", irlpt_server_lsap);
	irias_insert_object(obj);

	DEBUG( irlpt_server_debug, __FUNCTION__ 
	       ": Source LSAP sel=%d\n", irlpt_server->slsap_sel);
	DEBUG( irlpt_server_debug, __FUNCTION__ " -->\n");
}

/*
 * Function register_irlpt_server(void)
 *
 *    Register server support so we can accept incoming connections. We
 *    must register both a TSAP for control and data
 * 
 */
static void deregister_irlpt_server(void)
{
#if 0
	struct notify_t notify;
#endif
	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");

#if 0
	/*
	 *  First register control TSAP
	 */

	if ( !irlpt_server || irlpt_server->magic != IRLPT_MAGIC) {
		DEBUG( 0, "irlpt_register_server:, unable to obtain handle!\n");
		return;
	}

	irda_notify_init(&notify);

	notify.connect_confirm = irlpt_server_connect_confirm;
	notify.connect_indication = irlpt_server_connect_indication;
	notify.disconnect_indication = irlpt_server_disconnect_indication;
	notify.data_indication = irlpt_server_data_indication;
	notify.instance = irlpt_server;
	strcpy(notify.name, "IrLPT");

	irlpt_server->lsap = irlmp_open_lsap( irlpt_server_lsap, &notify);

	irlpt_server->connected = IRLPT_WAITING;
	irlpt_server->service_LSAP = irlpt_server_lsap;
#endif

	/* 
	 *  de-Register with LM-IAS
	 */
	irias_delete_object( "IrLPT");

	DEBUG( irlpt_server_debug, 
	       __FUNCTION__ ": Source LSAP sel=%d\n", irlpt_server->slsap_sel);
	DEBUG( irlpt_server_debug, __FUNCTION__ " -->\n");
}

#ifdef MODULE

MODULE_AUTHOR("Thomas Davis <ratbert@radiks.net>");
MODULE_DESCRIPTION("The Linux IrDA/IrLPT protocol");

/*
 * Function init_module (void)
 *
 *    Initialize the IrLPT server module, this function is called by the
 *    modprobe(1) program.
 */
int init_module(void)
{

        DEBUG( irlpt_server_debug, "--> irlpt server: init_module\n");

        irlpt_server_init();

        DEBUG( irlpt_server_debug, "irlpt server: init_module -->\n");

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
	DEBUG( irlpt_server_debug, "--> " __FUNCTION__ "\n");
        DEBUG( 3, "--> irlpt server: cleanup_module\n");
        /* No need to check MOD_IN_USE, as sys_delete_module() checks. */

        /* Free some memory */
        irlpt_server_cleanup();

        DEBUG( irlpt_server_debug, "irlpt server: cleanup_module -->\n");
}

#endif /* MODULE */
