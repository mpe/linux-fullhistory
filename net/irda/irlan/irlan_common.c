/*********************************************************************
 *                
 * Filename:      irlan_common.c
 * Version:       0.1
 * Description:   IrDA LAN Access Protocol Implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Tue Jan 19 23:11:30 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997 Dag Brattli <dagb@cs.uit.no>, All Rights Reserved.
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
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/timer.h>

#include <net/irda/irlan_common.h>

static void __irlan_close( struct irlan_cb *self);

/*
 *  Master structure
 */
hashbin_t *irlan = NULL;

#ifdef CONFIG_PROC_FS
static int irlan_proc_read( char *buf, char **start, off_t offset, 
			    int len, int unused);

extern struct proc_dir_entry proc_irda;

struct proc_dir_entry proc_irlan = {
	0, 5, "irlan",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, NULL,
	&irlan_proc_read,
};
#endif

/*
 * Function irlan_init (void)
 *
 *    Initialize IrLAN layer
 *
 */
__initfunc(int irlan_init( void))
{
	/* Allocate master array */
	irlan = hashbin_new( HB_LOCAL); 
	if ( irlan == NULL) {
		printk( KERN_WARNING "IrLAN: Can't allocate hashbin!\n");
		return -ENOMEM;
	}
#ifdef CONFIG_PROC_FS
	proc_register( &proc_irda, &proc_irlan);
#endif /* CONFIG_PROC_FS */

	return 0;
}

void irlan_cleanup(void) 
{
	DEBUG( 4, __FUNCTION__ "()\n");

#ifdef CONFIG_PROC_FS
	proc_unregister( &proc_irda, proc_irlan.low_ino);
#endif
	/*
	 *  Delete hashbin and close all irlan client instances in it
	 */
	hashbin_delete( irlan, (FREE_FUNC) __irlan_close);
}


/*
 * Function irlan_open (void)
 *
 *    
 *
 */
struct irlan_cb *irlan_open(void)
{
	struct irlan_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");

	/* 
	 *  Initialize the irlan structure. 
	 */
	self = kmalloc( sizeof(struct irlan_cb), GFP_ATOMIC);
	if ( self == NULL)
		return NULL;
	
	memset( self, 0, sizeof( struct irlan_cb));

	/*
	 *  Initialize local device structure
	 */
	self->magic = IRLAN_MAGIC;

	return self;
}
/*
 * Function irlan_close (self)
 *
 *    This function closes and deallocates the IrLAN client instances. Be 
 *    aware that other functions which calles client_close() must call 
 *    hashbin_remove() first!!!
 *
 */
void __irlan_close( struct irlan_cb *self)
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	/* 
	 *  Disconnect open TSAP connections
	 */
	if ( self->tsap_data) {
		irttp_disconnect_request( self->tsap_data, NULL, P_HIGH);
		
		/* FIXME: this will close the tsap before the disconenct
		 * frame has been sent 
		 */
		/* irttp_close_tsap( self->tsap_data); */
	}
	if ( self->tsap_ctrl) {
		irttp_disconnect_request( self->tsap_ctrl, NULL, P_HIGH);
		
		/* irttp_close_tsap( self->tsap_control); */
	}
	
	unregister_netdev( &self->dev);

	/*
	 *  Make sure that nobody uses this instance anymore!
	 */
	self->magic = 0;
	
	/*
	 *  Dealloacte structure
	 */
	kfree( self);
}

/*
 * Function irlan_close (self)
 *
 *    
 *
 */
void irlan_close( struct irlan_cb *self)
{
	struct irlan_cb *entry;
	
	DEBUG( 4, __FUNCTION__ "()\n");

        ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	entry = hashbin_remove( irlan, self->daddr, NULL);

	ASSERT( entry == self, return;);
	
        __irlan_close( self);
}

/*
 * Function irlan_get_provider_info (self)
 *
 *    Send Get Provider Information command to peer IrLAN layer
 *
 */
void irlan_get_provider_info( struct irlan_cb *self)
{
	struct sk_buff *skb;
	__u8 *frame;

	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	skb = dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0, __FUNCTION__
		       "(), Could not allocate an skb of length %d\n", 64);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_GET_PROVIDER_INFO;
	frame[1] = 0x00;                 /* Zero parameters */
	
	irttp_data_request( self->tsap_ctrl, skb);
}

/*
 * Function irlan_open_data_channel (self)
 *
 *    Send an Open Data Command to provider
 *
 */
void irlan_open_data_channel( struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	skb =  dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0, __FUNCTION__
		       "(), Could not allocate an skb of length %d\n", 64);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
	
	frame = skb->data;
	
	/* Build frame */
 	frame[0] = CMD_OPEN_DATA_CHANNEL;
	frame[1] = 0x02; /* Two parameters */

	insert_string_param( skb, "MEDIA", "802.3");
	insert_string_param( skb, "ACCESS_TYPE", "DIRECT");
	/* insert_string_param( skb, "MODE", "UNRELIABLE"); */

/* 	self->use_udata = TRUE; */

	irttp_data_request( self->tsap_ctrl, skb);
}

/*
 * Function irlan_open_unicast_addr (self)
 *
 *    Make IrLAN provider accept ethernet frames addressed to the unicast 
 *    address.
 *
 */
void irlan_open_unicast_addr( struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);	
	
	skb = dev_alloc_skb( 128);
	if (skb == NULL) {
		DEBUG( 0, __FUNCTION__
		       "(), Could not allocate an skb of length %d\n", 64);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
 	insert_byte_param( skb, "DATA_CHAN" , self->dtsap_sel_data);
 	insert_string_param( skb, "FILTER_TYPE", "DIRECTED");
 	insert_string_param( skb, "FILTER_MODE", "FILTER"); 
	
	irttp_data_request( self->tsap_ctrl, skb);
}

/*
 * Function irlan_set_broadcast_filter (self, status)
 *
 *    Make IrLAN provider accept ethernet frames addressed to the broadcast
 *    address. Be careful with the use of this one, sice there may be a lot
 *    of broadcast traffic out there. We can still function without this
 *    one but then _we_ have to initiate all communication with other
 *    hosts, sice ARP request for this host will not be answered.
 */
void irlan_set_broadcast_filter( struct irlan_cb *self, int status) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	/* Should only be used by client */
	if (!self->client)
		return;

 	skb =  dev_alloc_skb( 128);
	if (skb == NULL) {
		DEBUG( 0, __FUNCTION__
		       "(), Could not allocate an skb of length %d\n", 64);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
 	insert_byte_param( skb, "DATA_CHAN", self->dtsap_sel_data);
 	insert_string_param( skb, "FILTER_TYPE", "BROADCAST");
	if ( status)
		insert_string_param( skb, "FILTER_MODE", "FILTER"); 
	else
		insert_string_param( skb, "FILTER_MODE", "NONE"); 
	
	irttp_data_request( self->tsap_ctrl, skb);
}

/*
 * Function irlan_set_multicast_filter (self, status)
 *
 *    Make IrLAN provider accept ethernet frames addressed to the multicast
 *    address. 
 *
 */
void irlan_set_multicast_filter( struct irlan_cb *self, int status) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);

	/* Should only be used by client */
	if (!self->client)
		return;
	
 	skb =  dev_alloc_skb( 128);
	if (skb == NULL) {
		DEBUG( 0, __FUNCTION__
		       "(), Could not allocate an skb of length %d\n", 64);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
 	insert_byte_param( skb, "DATA_CHAN", self->dtsap_sel_data);
 	insert_string_param( skb, "FILTER_TYPE", "MULTICAST");
	if ( status)
		insert_string_param( skb, "FILTER_MODE", "ALL"); 
	else
		insert_string_param( skb, "FILTER_MODE", "NONE"); 
	
	irttp_data_request( self->tsap_ctrl, skb);
}

/*
 * Function irlan_get_unicast_addr (self)
 *
 *    Retrives the unicast address from the IrLAN provider. This address
 *    will be inserted into the devices structure, so the ethernet layer
 *    can construct its packets.
 *
 */
void irlan_get_unicast_addr( struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
		
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	skb =  dev_alloc_skb( 128);
	if (skb == NULL) {
		DEBUG( 0, "irlan_client_get_unicast_addr: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
	
	frame = skb->data;
	
 	frame[0] = CMD_FILTER_OPERATION;
	frame[1] = 0x03;                 /* Three parameters */
 	insert_byte_param( skb, "DATA_CHAN", self->dtsap_sel_data);
 	insert_string_param( skb, "FILTER_TYPE", "DIRECTED");
 	insert_string_param( skb, "FILTER_OPERATION", "DYNAMIC"); 
	
	irttp_data_request( self->tsap_ctrl, skb);
}

/*
 * Function irlan_get_media_char (self)
 *
 *    
 *
 */
void irlan_get_media_char( struct irlan_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRLAN_MAGIC, return;);
	
	skb = dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0,"irlan_server_get_media_char: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve( skb, TTP_HEADER+LMP_HEADER+LAP_HEADER);
	skb_put( skb, 2);
	
	frame = skb->data;
	
	/* Build frame */
 	frame[0] = CMD_GET_MEDIA_CHAR;
	frame[1] = 0x01; /* One parameter */
	
	insert_string_param( skb, "MEDIA", "802.3");
	
	irttp_data_request( self->tsap_ctrl, skb);
}

/*
 * Function insert_byte_param (skb, param, value)
 *
 *    Insert byte parameter into frame
 *
 */
int insert_byte_param( struct sk_buff *skb, char *param, __u8 value)
{
	__u8 *frame;
	__u8 len_param;
	__u16 len_value;
	int n=0;
	
	if ( skb == NULL) {
		DEBUG( 0, "insert_param: Got NULL skb\n");
		return 0;
	}
	
	len_param = strlen( param);
	len_value = 1;

	/*
	 *  Insert at end of sk-buffer
	 */
	frame = skb->tail;

	/* Make space for data */
	if ( skb_tailroom(skb) < (len_param+len_value+3)) {
		DEBUG( 0, "insert_param: No more space at end of skb\n");
		return 0;
	}	
	skb_put( skb, len_param+len_value+3);

	/* Insert parameter length */
	frame[n++] = len_param;
	
	/* Insert parameter */
	memcpy( frame+n, param, len_param); 
	n += len_param;
	
	/* Insert value length ( 2 byte little endian format, LSB first) */
	frame[n++] = len_value & 0xff;
	frame[n++] = len_value >> 8;

	frame[n++] = value;
	
	return len_param+len_value+3;
}

/*
 * Function insert_string (skb, param, value)
 *
 *    Insert string parameter into frame
 *
 */
int insert_string_param( struct sk_buff *skb, char *param, char *value)
{
	__u8 *frame;
	__u8 len_param;
	__u16 len_value;
	int n=0;
	
	if ( skb == NULL) {
		DEBUG( 0, "insert_param: Got NULL skb\n");
		return 0;
	}	
	len_param = strlen( param);
	len_value = strlen( value);

	/*
	 *  Insert at end of sk-buffer
	 */
	frame = skb->tail;

	/* Make space for data */
	if ( skb_tailroom(skb) < (len_param+len_value+3)) {
		DEBUG( 0, "insert_param: No more space at end of skb\n");
		return 0;
	}	
	skb_put( skb, len_param+len_value+3);

	/* Insert parameter length */
	frame[n++] = len_param;
	
	/* Insert parameter */
	memcpy( frame+n, param, len_param); 
	n += len_param;
	
	/* Insert value length ( 2 byte little endian format, LSB first) */
	frame[n++] = len_value & 0xff;
	frame[n++] = len_value >> 8;

	memcpy( frame+n, value, len_value); 
	n+=len_value;
	
	return len_param+len_value+3;
}

/*
 * Function insert_array_param( skb, param, value, len_value)
 *
 *    Insert array parameter into frame
 *
 */
int insert_array_param( struct sk_buff *skb, char *name, __u8 *value, 
			__u16 value_len)
{
	__u8 *frame;
	__u8 name_len;
	int n=0;
	
	if ( skb == NULL) {
		DEBUG( 0, __FUNCTION__ "(), Got NULL skb\n");
		return 0;
	}	
	name_len = strlen( name);

	/*
	 *  Insert at end of sk-buffer
	 */
	frame = skb->tail;

	/* Make space for data */
	if ( skb_tailroom(skb) < (name_len+value_len+3)) {
		DEBUG( 0, __FUNCTION__ "(), No more space at end of skb\n");
		return 0;
	}	
	skb_put( skb, name_len+value_len+3);

	/* Insert parameter length */
	frame[n++] = name_len;
	
	/* Insert parameter */
	memcpy( frame+n, name, name_len); 
	n += name_len;
	
	/* Insert value length ( 2 byte little endian format, LSB first) */
	/* FIXME: should we use htons() here? */
	frame[n++] = value_len & 0xff;
	frame[n++] = value_len >> 8;

	memcpy( frame+n, value, value_len); 
	n+=value_len;
	
	return name_len+value_len+3;
}

/*
 * Function insert_param (skb, param, value, byte)
 *
 *    Insert parameter at end of buffer, structure of a parameter is:
 *
 *    -----------------------------------------------------------------------
 *    | Name Length[1] | Param Name[1..255] | Val Length[2] | Value[0..1016]|
 *    -----------------------------------------------------------------------
 */
int insert_param( struct sk_buff *skb, char *param, int type, char *value_char,
 		  __u8 value_byte, __u16 value_short) 
{
	__u8 *frame;
	__u8 len_param;
	__u16 len_value;
	int n;
	
	if ( skb == NULL) {
		DEBUG( 0, "insert_param: Got NULL skb\n");
		return 0;
	}
	
	n = 0;

	len_param = strlen( param);
	switch ( type) {
	case 1:
		ASSERT( value_char != NULL, return 0;);
		len_value = strlen( value_char);
		break;
	case 2:
		len_value = 1;
		break;
	case 3:
		len_value = 2;
		break;
	default:
		DEBUG( 0, "Error in insert_param!\n");
		return 0;
		break;
	}
	
	/*
	 *  Insert at end of sk-buffer
	 */
	frame = skb->tail;

	/* Make space for data */
	if ( skb_tailroom(skb) < (len_param+len_value+3)) {
		DEBUG( 0, "insert_param: No more space at end of skb\n");
		return 0;
	}	
	skb_put( skb, len_param+len_value+3);

	/* Insert parameter length */
	frame[n++] = len_param;
	
	/* Insert parameter */
	memcpy( frame+n, param, len_param); n += len_param;
	
	/* Insert value length ( 2 byte little endian format, LSB first) */
	frame[n++] = len_value & 0xff;
	frame[n++] = len_value >> 8;

	/* Insert value */
	switch (type) {
	case 1:
		memcpy( frame+n, value_char, len_value); n+=len_value;
		break;
	case 2:
		frame[n++] = value_byte;
		break;
	case 3:
		frame[n++] = value_short & 0xff;
		frame[n++] = (value_short >> 8) & 0xff;
		break;
	default:
		break;
	}
	ASSERT( n == (len_param+len_value+3), return 0;);

	return len_param+len_value+3;
}

/*
 * Function irlan_get_response_param (buf, param, value)
 *
 *    Extracts a single parameter name/value pair from buffer and updates
 *    the buffer pointer to point to the next name/value pair.
 * 
 */
int irlan_get_response_param( __u8 *buf, char *name, char *value, int *len)
{
	__u8 name_len;
	__u16 val_len;
	int n=0;

	DEBUG( 4, "irlan_get_response_param()\n");
	
	/* get length of parameter name ( 1 byte) */
	name_len = buf[n++];
	
	if (name_len > 254) {
		DEBUG( 0, __FUNCTION__ "(), name_len > 254\n");
		return -1;
	}
	
	/* get parameter name */
	memcpy( name, buf+n, name_len);
	name[ name_len] = '\0';
	n+=name_len;
	
	/*  
	 *  Get length of parameter value ( 2 bytes in little endian 
	 *  format) 
	 */
	val_len = buf[n++] & 0xff;
	val_len |= buf[n++] << 8;
	
	if (val_len > 1016) {
		DEBUG( 0, __FUNCTION__ "(), parameter length to long\n");
		return -1;
	}
	
	*len = val_len;

	/* get parameter value */
	memcpy( value, buf+n, val_len);
	value[ val_len] = '\0';
	n+=val_len;
	
	DEBUG( 4, "Parameter: %s ", name); 
	DEBUG( 4, "Value: %s\n", value); 

	return n;
}

#ifdef CONFIG_PROC_FS
/*
 * Function irlan_client_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 */
static int irlan_proc_read( char *buf, char **start, off_t offset, 
			    int len, int unused)
{
 	struct irlan_cb *self;
	
	ASSERT( irlan != NULL, return 0;);
	
	len = 0;
	
	len += sprintf( buf+len, "IrLAN\n");

	self = ( struct irlan_cb *) hashbin_get_first( irlan);
	while ( self != NULL) {
		ASSERT( self->magic == IRLAN_MAGIC, return len;);

		len += sprintf( buf+len, "ifname: %s, ",
				self->ifname);
		/* len += sprintf( buf+len, "state: %s, ", */
/* 				irlan_client_state[ self->state]); */
		len += sprintf( buf+len, "saddr: %#08x\n",
				self->saddr);
		len += sprintf( buf+len, "daddr: %#08x\n",
				self->daddr);
		len += sprintf( buf+len, "tbusy: %s\n", self->dev.tbusy ? 
				"TRUE" : "FALSE");
		
		len += sprintf( buf+len, "\n");

		self = ( struct irlan_cb *) hashbin_get_next( irlan);
		DEBUG( 4, "self=%p\n", self);
 	} 
	return len;
}
#endif

/*
 * Function print_ret_code (code)
 *
 *    Print return code of request to peer IrLAN layer.
 *
 */
void print_ret_code( __u8 code) 
{
	switch( code) {
	case 0:
		printk( KERN_INFO "Success\n");
		break;
	case 1:
		printk( KERN_WARNING "Insufficient resources\n");
		break;
	case 2:
		printk( KERN_WARNING "Invalid command format\n");
		break;
	case 3:
		printk( KERN_WARNING "Command not supported\n");
		break;
	case 4:
		printk( KERN_WARNING "Parameter not supported\n");
		break;
	case 5:
		printk( KERN_WARNING "Value not supported\n");
		break;
	case 6:
		printk( KERN_WARNING "Not open\n");
		break;
	case 7:
		printk( KERN_WARNING "Authentication required\n");
		break;
	case 8:
		printk( KERN_WARNING "Invalid password\n");
		break;
	case 9:
		printk( KERN_WARNING "Protocol error\n");
		break;
	case 255:
		printk( KERN_WARNING "Asynchronous status\n");
		break;
	}
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("The Linux IrDA LAN protocol"); 

/*
 * Function init_module (void)
 *
 *    Initialize the IrLAN module, this function is called by the
 *    modprobe(1) program.
 */
int init_module(void) 
{
	DEBUG( 4, __FUNCTION__ "(), irlan.c\n");

	irlan_init();

	return 0;
}

/*
 * Function cleanup_module (void)
 *
 *    Remove the IrLAN module, this function is called by the rmmod(1)
 *    program
 */
void cleanup_module(void) 
{
	DEBUG( 4, "--> irlan, cleanup_module\n");
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */

	/* Free some memory */
 	irlan_cleanup();

	DEBUG( 4, "irlan, cleanup_module -->\n");
}

#endif /* MODULE */


