/*********************************************************************
 *
 * Filename:      irlpt_common.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Thomas Davis, <ratbert@radiks.net>
 * Created at:    Sat Feb 21 18:54:38 1998
 * Modified at:   Sun Mar  8 23:44:19 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:	  irlpt.c
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

#include <linux/module.h> 

#include <asm/segment.h>

#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irttp.h>
#include <net/irda/timer.h>

#include <net/irda/irlpt_common.h>
/* #include "irlpt_client.h" */
/* #include "irlpt_server.h" */

#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>

char *irlpt_service_type[] = {
	"IRLPT_UNKNOWN",
	"IRLPT_THREE_WIRE_RAW",
	"IRLPT_THREE_WIRE",
	"IRLPT_NINE_WIRE",
	"IRLPT_CENTRONICS",
	"IRLPT_SERVER_MODE",
};

char *irlpt_port_type[] = {
	"IRLPT_UNKNOWN",
	"IRLPT_SERIAL",
	"IRLPT_PARALLEL",
};

char *irlpt_connected[] = {
	"IRLPT_DISCONNECTED",
	"IRLPT_WAITING",
	"IRLPT_CONNECTED",
	"IRLPT_FLUSHED",
};

char *irlpt_reasons[] = {
	"SERVICE_CLOSE",     /* Service has closed the connection */
	"DISC_INDICATION",   /* Received a disconnect request from peer entity*/
	"NO_RESPONSE",       /* To many retransmits without response */
	"DEADLOCK_DETECTED", /* To many retransmits _with_ response */
	"FOUND_NONE",        /* No devices were discovered */
	"MEDIA_BUSY",

};

char *irlpt_client_fsm_state[] = {
	"IRLPT_CLIENT_IDLE",
	"IRLPT_CLIENT_QUERY",
	"IRLPT_CLIENT_READY",
	"IRLPT_CLIENT_WAITI",
	"IRLPT_CLIENT_WAITR",
	"IRLPT_CLIENT_CONN"
};

char *irlpt_server_fsm_state[] = {
	"IRLPT_SERVER_IDLE",
	"IRLPT_SERVER_CONN"
};

char *irlpt_fsm_event[] = {
	"QUERY_REMOTE_IAS",
	"IAS_PROVIDER_AVAIL",
	"IAS_PROVIDER_NOT_AVAIL",
	"LAP_DISCONNECT",
	"LMP_CONNECT",
	"LMP_DISCONNECT",
	"LMP_CONNECT_INDICATION",
	"LMP_DISCONNECT_INDICATION",
        "IRLPT_DISCOVERY_INDICATION",
	"IRLPT_CONNECT_REQUEST",
	"IRLPT_DISCONNECT_REQUEST",
	"CLIENT_DATA_INDICATION",
};

hashbin_t *irlpt_clients = NULL;
struct irlpt_cb *irlpt_server = NULL;
int irlpt_common_debug = 4;  /* want to change this? please don't! 
				use irlpt_common_debug=3 on the command line! */

static struct wait_queue *irlpt_wait;

#if 0
static char *rcsid = "$Id: irlpt_common.c,v 1.6 1998/11/10 22:50:58 dagb Exp $";
#endif

struct irlpt_cb *irlpt_find_handle(unsigned int minor)
{
	struct irlpt_cb *self;

	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");
	
	/* Check for server */
	if (irlpt_server != NULL && irlpt_server->ir_dev.minor == minor) {
		DEBUG(irlpt_common_debug, __FUNCTION__ 
		      ": irlpt_server file handle!\n");
		return irlpt_server;
	}

	/* Check the clients */
	self = (struct irlpt_cb *) hashbin_get_first( irlpt_clients);
	while ( self) {
		ASSERT( self->magic == IRLPT_MAGIC, return NULL;);
		
		if ( minor == self->ir_dev.minor)
			return self;

		self = (struct irlpt_cb *) hashbin_get_next( irlpt_clients);
	}

	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");

	return NULL;
}

ssize_t irlpt_read( struct file *file, char *buffer, size_t count, loff_t
		    *noidea)
{
	int len=0;
	char *ptr = buffer;
	struct irlpt_cb *self;
	struct sk_buff *skb = NULL;
	struct wait_queue wait = { current, NULL };

	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");

	self = irlpt_find_handle(MINOR( file->f_dentry->d_inode->i_rdev));

	ASSERT( self != NULL, return 0;);
	ASSERT( self->magic == IRLPT_MAGIC, return 0;);

	DEBUG( irlpt_common_debug, __FUNCTION__ 
	       ": count=%d, skb_len=%d, connected=%d (%s) eof=%d\n", 
	       count, skb_queue_len(&self->rx_queue), self->connected, 
	       irlpt_connected[self->connected], self->eof);

	if (self->eof && !skb_queue_len(&self->rx_queue)) {
		switch (self->eof) {
		case LM_USER_REQUEST:
			self->eof = FALSE;
			DEBUG(3, "irlpt_read: returning 0\n");
			return 0;
		case LM_LAP_DISCONNECT:
			self->eof = FALSE;
			return -EIO;
		case LM_LAP_RESET:
			self->eof = FALSE;
			return -ECONNRESET;
		default:
			self->eof = FALSE;
			return -EIO;
		}
	}

	while (len <= count) {
		skb = skb_dequeue(&self->rx_queue);

		if (skb != NULL) {
			DEBUG(irlpt_common_debug, __FUNCTION__ 
			      ": len=%d, skb->len=%d, count=%d\n", 
			       len, (int) skb->len, count);

			if ((skb->len + len) < count) {
				copy_to_user(ptr, skb->data, skb->len);
				len += skb->len;
				ptr += skb->len;
		
				dev_kfree_skb( skb);
			} else {
				skb_queue_head(&self->rx_queue, skb);
				break;
			}
		} else {
			DEBUG( irlpt_common_debug, __FUNCTION__ 
			       ": skb=NULL, len=%d, count=%d, eof=%d\n", 
			       len, count, self->eof);

			if (!signal_pending(current) && !self->eof) {
			        add_wait_queue(&irlpt_wait, &wait);
				current->state = TASK_INTERRUPTIBLE;
				schedule();
				current->state = TASK_RUNNING;
				remove_wait_queue(&irlpt_wait, &wait);
			} else
				break;
		}
	}

	DEBUG(irlpt_common_debug, __FUNCTION__ ": len=%d\n", len);
	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");
	return len;
}

ssize_t irlpt_write(struct file *file, const char *buffer,
		    size_t count, loff_t *noidea)
{
	struct irlpt_cb *self;
	struct sk_buff *skb;
	struct wait_queue wait = { current, NULL };

	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");

	self = irlpt_find_handle(MINOR( file->f_dentry->d_inode->i_rdev));

	ASSERT( self != NULL, return 0;);
	ASSERT( self->magic == IRLPT_MAGIC, return 0;);
	
	DEBUG( irlpt_common_debug, __FUNCTION__ 
	       ": count = %d\n", count);

	if (self->state != IRLPT_CLIENT_CONN) {
		DEBUG( irlpt_common_debug, __FUNCTION__ 
		       ": state != IRLPT_CONN (possible link problems?)\n");
		return -ENOLINK;
	}

	DEBUG( irlpt_common_debug, __FUNCTION__ 
	       ": pkt_count = %d\n", self->pkt_count);
	if (self->pkt_count > 8) {
		DEBUG( irlpt_common_debug, __FUNCTION__ 
		       ": too many outstanding buffers, going to sleep\n");
		add_wait_queue(&self->write_wait, &wait);
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		current->state = TASK_RUNNING;
		remove_wait_queue(&self->write_wait, &wait);
	}

	DEBUG( irlpt_common_debug, __FUNCTION__ 
	       ":count = %d, irlap_data_size = %d, IRLPT_MAX_HEADER = %d\n",
		count, self->irlap_data_size, IRLPT_MAX_HEADER);

 	if (count > (self->irlap_data_size - IRLPT_MAX_HEADER)) {
 		count = (self->irlap_data_size - IRLPT_MAX_HEADER);
 		DEBUG(irlpt_common_debug, __FUNCTION__ 
		      ": setting count to %d\n", count);
 	}

	DEBUG( irlpt_common_debug, __FUNCTION__ ": count = %d\n", count);

	skb = dev_alloc_skb(count + IRLPT_MAX_HEADER);
	if ( skb == NULL) {
		printk( KERN_INFO __FUNCTION__ ": couldn't allocate skbuff!\n");
		return 0;
	}

	/*
	 * we use the unused stamp field to hold the device minor
	 * number, so we can look it up when the skb is destroyed.
	 */
	*((__u32 *) &skb->stamp) = MINOR( file->f_dentry->d_inode->i_rdev);

	skb_reserve( skb, IRLPT_MAX_HEADER);
	skb_put( skb, count);

	skb->destructor = irlpt_flow_control;
	self->pkt_count++;

	copy_from_user( skb->data, buffer, count);

	irlmp_data_request(self->lsap, skb);

	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");

	return(count);
}

loff_t irlpt_seek( struct file *file, loff_t offset, int count)
{
	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");

	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");
	return -ESPIPE;
}

#if 0

/*
 * Function irlpt_select (inode, filp, mode, table)
 *
 *    Implementation for the select() call
 *
 */
int irlpt_select( struct inode *inode, struct file *filp, int mode, 
			 select_table *table)
{
	struct irlpt_cb *self;

	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");
	
	self = irlpt_find_handle(MINOR( inode->i_rdev));

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	switch (mode) {
	case SEL_IN:
		if ( skb_queue_len( &self->rx_queue))
			return 1; /* Readable */
		select_wait( &self->read_wait, table);
		break;
	case SEL_OUT:
		if ( self->connected)
			return 1;
		select_wait( &self->write_wait, table);
		break;
	case SEL_EX:
		if ( self->connected)
			return 1;
		select_wait( &self->ex_wait, table);
		break;
	default:
		break;
	}

	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");

	return 0;
}

#else

/*
 * Function irobex_poll (file, wait)
 *
 *    
 *
 */
static u_int irlpt_poll(struct file *file, poll_table *wait)
{
	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");

	/* check out /usr/src/pcmcia/modules/ds.c for an example */
	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");
	return 0;
}

#endif

/*
 * Function open_irlpt (inode, file)
 *
 *
 *
 */
int irlpt_open(struct inode *inode, struct file *file)
{
	struct irlpt_cb *self;

	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");

	self = irlpt_find_handle(MINOR( file->f_dentry->d_inode->i_rdev));

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

#if 0
	if (self->state == IRLPT_IDLE) {
		DEBUG( irlpt_common_debug, __FUNCTION__ 
		       ": state == IRLPT_IDLE! (no device found yet)\n");
		return -ENODEV;
	}

	if (self->state == IRLPT_QUERY ||
	    self->state == IRLPT_READY ||
	    self->state == IRLPT_WAITI) {
		DEBUG( irlpt_common_debug, __FUNCTION__ ": state == IRLPT_QUERY, " 
		       "IRLPT_READY or IRLPT_WAITI (link problems)!\n");
		return -EBUSY;
	}
#endif

	if (self->count++) {
		DEBUG( irlpt_common_debug, __FUNCTION__ 
		       ": count not zero; actual = %d\n", self->count);
		self->count--;
		return -EBUSY;
	}

	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");

	return 0;
}

/*
 * Function close_irlpt (inode, file)
 *
 *
 *
 */
int irlpt_close(struct inode *inode, struct file *file)
{
	struct irlpt_cb *self;

	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");

	self = irlpt_find_handle(MINOR( file->f_dentry->d_inode->i_rdev));

	DEBUG(irlpt_common_debug, __FUNCTION__ ": have handle!\n");

	ASSERT( self != NULL, return -1;);
	ASSERT( self->magic == IRLPT_MAGIC, return -1;);

	DEBUG(irlpt_common_debug, __FUNCTION__ ": self->count=%d\n", self->count);
	if (self->count > 0)
		self->count--;

	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");

	return 0;
}

void irlpt_dump_buffer( struct sk_buff *skb) 
{
	int i;

	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");

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
	
	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");
}

void irlpt_flow_control(struct sk_buff *skb)
{
	struct irlpt_cb *self;

	DEBUG(irlpt_common_debug, "--> " __FUNCTION__ "\n");

	self = irlpt_find_handle( *((__u32 *) &skb->stamp));

	self->pkt_count--;

	ASSERT(self->pkt_count >= 0, return;);

	DEBUG(irlpt_common_debug, __FUNCTION__ 
	      ": packet destroyed, count = %d\n", self->pkt_count);

	wake_up_interruptible( &self->write_wait);

	DEBUG(irlpt_common_debug, __FUNCTION__ " -->\n");
}

#ifdef MODULE

/*
 * Function init_module (void)
 *
 *    Initialize the module, this function is called by the
 *    modprobe(1) program.
 */
int init_module(void) 
{
	return 0;
}

/*
 * Function cleanup_module (void)
 *
 *    Remove the module, this function is called by the rmmod(1)
 *    program
 */
void cleanup_module(void) 
{

}

#endif /* MODULE */
