/*********************************************************************
 *                
 * Filename:      iriap.c
 * Version:       0.1
 * Description:   Information Access Protocol (IAP)
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Aug 21 00:02:07 1997
 * Modified at:   Tue Dec 15 16:00:35 1998
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
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>
#include <net/irda/irlmp.h>
#include <net/irda/irias_object.h>
#include <net/irda/iriap_event.h>
#include <net/irda/iriap.h>

hashbin_t *iriap = NULL;

static struct iriap_cb *iriap_open( __u8 slsap, int mode);
static void __iriap_close( struct iriap_cb *self);
static void iriap_disconnect_indication( void *instance, void *sap, 
					 LM_REASON reason, 
					 struct sk_buff *skb);

/*
 * Function iriap_init (void)
 *
 *    Initializes the IrIAP layer, called by the module initialization code
 *    in irmod.c 
 */
__initfunc(int iriap_init(void))
{
	struct ias_object *obj;

	DEBUG( 4, "--> iriap_init\n");
	
	/* Allocate master array */
	iriap = hashbin_new( HB_LOCAL);
	if ( iriap == NULL) {
		printk( KERN_WARNING "IrIAP: Can't allocate iriap hashbin!\n");
		return -ENOMEM;
	}

	objects = hashbin_new( HB_LOCAL);
	if ( objects == NULL) {
		printk( KERN_WARNING 
			"IrIAP: Can't allocate objects hashbin!\n");
		return -ENOMEM;
	}

	/* 
	 *  Register some default services for IrLMP 
	 */
	irlmp_register_layer( S_COMPUTER, SERVER | CLIENT, FALSE, NULL);
	irlmp_register_layer( S_PNP, SERVER, FALSE, NULL);

	/* 
	 *  Register the Device object with LM-IAS
	 */
	obj = irias_new_object( "Device", IAS_DEVICE_ID);
	irias_add_string_attrib( obj, "DeviceName", "Linux");
	irias_insert_object( obj);

	/*  
	 *  Register server support with IrLMP so we can accept incoming 
	 *  connections 
	 */
	iriap_open( LSAP_IAS, IAS_SERVER);
	
	DEBUG( 4, "iriap_init -->\n");
	
	return 0;
}

/*
 * Function iriap_cleanup (void)
 *
 *    Initializes the IrIAP layer, called by the module cleanup code in 
 *    irmod.c
 */
void iriap_cleanup(void) 
{
	irlmp_unregister_layer( S_COMPUTER, SERVER | CLIENT);
	irlmp_unregister_layer( S_PNP, SERVER);
	
	hashbin_delete( iriap, (FREE_FUNC) __iriap_close);
	hashbin_delete( objects, (FREE_FUNC) __irias_delete_object);	
}

/*
 * Function iriap_open (void)
 *
 *    Opens an instance of the IrIAP layer, and registers with IrLMP
 */
struct iriap_cb *iriap_open( __u8 slsap_sel, int mode)
{
	struct iriap_cb *self;
	struct notify_t notify;
	struct lsap_cb *lsap;

	DEBUG( 4, __FUNCTION__ "()\n");

	self = kmalloc( sizeof(struct iriap_cb), GFP_ATOMIC);
	if ( self == NULL) {
		DEBUG( 0, "iriap_open(), Unable to kmalloc!\n");
		return NULL;
	}

	/*
	 *  Initialize instance
	 */
	memset( self, 0, sizeof(struct iriap_cb));

	irda_notify_init( &notify);
	notify.connect_confirm = iriap_connect_confirm;
	notify.connect_indication = iriap_connect_indication;
	notify.disconnect_indication = iriap_disconnect_indication;
	notify.data_indication = iriap_data_indication;
	notify.instance = self;
	if ( mode == IAS_CLIENT)
		strcpy( notify.name, "IrIAS cli");
	else
		strcpy( notify.name, "IrIAS srv");

	lsap = irlmp_open_lsap( slsap_sel, &notify);
	if ( lsap == NULL) {
		DEBUG( 0, "iriap_open: Unable to allocated LSAP!\n");
		return NULL;
	}
	DEBUG( 4, "iriap_register: source LSAP sel=%02x\n", slsap_sel);
	
	self->magic = IAS_MAGIC;
	self->lsap = lsap;
	self->slsap_sel = slsap_sel;
	self->mode = mode;

	init_timer( &self->watchdog_timer);

	hashbin_insert( iriap, (QUEUE*) self, slsap_sel, NULL);
	
	iriap_next_client_state( self, S_DISCONNECT);
	iriap_next_call_state( self, S_MAKE_CALL);
	iriap_next_server_state( self, R_DISCONNECT);
	iriap_next_r_connect_state( self, R_WAITING);
	
	return self;
}

/*
 * Function __iriap_close (self)
 *
 *    Removes (deallocates) the IrIAP instance
 *
 */
static void __iriap_close( struct iriap_cb *self)
{
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);

	del_timer( &self->watchdog_timer);

	self->magic = 0;

	kfree( self);
}

/*
 * Function iriap_close (void)
 *
 *    Closes IrIAP and deregisters with IrLMP
 */
void iriap_close( struct iriap_cb *self)
{
	DEBUG( 4, "iriap_close()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);

	if ( self->lsap) {
		irlmp_close_lsap( self->lsap);
		self->lsap = NULL;
	}

	hashbin_remove( iriap, self->slsap_sel, NULL);

	__iriap_close( self);
}

/*
 * Function iriap_disconnect_indication (handle, reason)
 *
 *    Got disconnect, so clean up everything assosiated with this connection
 *
 */
void iriap_disconnect_indication( void *instance, void *sap, LM_REASON reason, 
				  struct sk_buff *userdata)
{
	struct iriap_cb *self;

	DEBUG( 4, __FUNCTION__ "()\n");

	self = (struct iriap_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);

	ASSERT( iriap != NULL, return;);

	del_timer( &self->watchdog_timer);

	if ( self->mode == IAS_CLIENT) {
		DEBUG( 4, __FUNCTION__ "(), disconnect as client\n");

		/* Inform service user */
		if ( self->confirm)
			self->confirm( 0, NULL, self->priv);

		iriap_do_client_event( self, IAP_LM_DISCONNECT_INDICATION, 
				       NULL);
		/* Close instance only if client */
		iriap_close( self);

	} else {
		DEBUG( 4, __FUNCTION__ "(), disconnect as server\n");
		iriap_do_server_event( self, IAP_LM_DISCONNECT_INDICATION, 
				       NULL);
	}

	if ( userdata) {
		dev_kfree_skb( userdata);
	}
}

/*
 * Function iriap_disconnect_request (handle)
 *
 *    
 *
 */
void iriap_disconnect_request( struct iriap_cb *self)
{
	struct sk_buff *skb;

	DEBUG( 4, "iriap_disconnect_request()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);

	skb = dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0,"iriap_getvaluebyclass: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	/* 
	 *  Reserve space for MUX and LAP header 
	 */
 	skb_reserve( skb, LMP_CONTROL_HEADER+LAP_HEADER);

	irlmp_disconnect_request( self->lsap, skb);
}

void iriap_getinfobasedetails_request(void) 
{
	DEBUG( 0, __FUNCTION__ "(), Not implemented!\n");
}

void iriap_getinfobasedetails_confirm(void) 
{
	DEBUG( 0, __FUNCTION__ "(), Not implemented!\n");
}

void iriap_getobjects_request(void) 
{
	DEBUG( 0, __FUNCTION__ "(), Not implemented!\n");
}

void iriap_getobjects_confirm(void) 
{
	DEBUG( 0, __FUNCTION__ "(), Not implemented!\n");
}

void iriap_getvalue(void) 
{
	DEBUG( 0, __FUNCTION__ "(), Not implemented!\n");
}

/*
 * Function iriap_getvaluebyclass (addr, name, attr)
 *
 *    Retreive all values from attribute in all objects with given class
 *    name
 */
void iriap_getvaluebyclass_request( __u32 daddr, char *name, char *attr, 
				    CONFIRM_CALLBACK callback,
				    void *priv)
{
	struct sk_buff *skb;
	struct iriap_cb *self;
	__u8 *frame;
	int name_len, attr_len;
	__u8 slsap = LSAP_ANY;  /* Source LSAP to use */

	DEBUG( 4, "iriap_getvaluebyclass_request()\n");
	
	self = iriap_open( slsap, IAS_CLIENT);
	if ( self == NULL)
		return;

	self->mode = IAS_CLIENT;
	self->confirm = callback;
	self->priv = priv;
	self->daddr = daddr;

	/* 
	 *  Save operation, so we know what the later indication is about
	 */
	self->operation = GET_VALUE_BY_CLASS; 

	/* Give ourselves 7 secs to finish this operation */
	iriap_start_watchdog_timer( self, 700);
	
	skb = dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0,"iriap_getvaluebyclass: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	name_len = strlen( name);
	attr_len = strlen( attr);

	/* Reserve space for MUX and LAP header */
 	skb_reserve( skb, LMP_CONTROL_HEADER+LAP_HEADER);
	skb_put( skb, 3+name_len+attr_len);
	frame = skb->data;

	/* Build frame */
	frame[0] = LST | GET_VALUE_BY_CLASS;
	frame[1] = name_len;                       /* Insert length of name */
	memcpy( frame+2, name, name_len);          /* Insert name */
	frame[2+name_len] = attr_len;              /* Insert length of attr */
	memcpy( frame+3+name_len, attr, attr_len); /* Insert attr */

	iriap_do_client_event( self, IAP_CALL_REQUEST_GVBC, skb);
}

/*
 * Function iriap_getvaluebyclass_confirm (self, skb)
 *
 *    Got result from GetValueByClass command. Parse it and return result
 *    to service user.
 *
 */
void iriap_getvaluebyclass_confirm( struct iriap_cb *self, 
				    struct sk_buff *skb) 
{
	struct ias_value *value;
	__u16 obj_id;
	int   len;
	__u8  type;
	int   value_len;
	__u8 *fp;
	int n;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	/* Initialize variables */
	fp = skb->data;
	n = 2;

	/* Get length, MSB first */
	len = ntohs( *(__u16 *)( fp+n)); n += 2;

	DEBUG( 4, "iriap_getvaluebyclass_confirm: len=%d\n", len);

	/* Get object ID, MSB first */
	obj_id = ntohs( *(__u16 *)( fp+n)); n += 2;

	type = fp[n++];
	DEBUG( 4, __FUNCTION__ "(), Value type = %d\n", type);


	switch( type) {
	case IAS_INTEGER:
		value = irias_new_integer_value( ntohl(*(__u32 *)(fp+n)));
		/* 
		 *  Legal values restricted to 0x01-0x6f, page 15 irttp 
		 */
		DEBUG( 4, "iriap_getvaluebyclass_confirm: lsap=%d\n", 
		       value->t.integer); 
		break;
	case IAS_STRING:
		/* FIXME: check len of string, and if string is/should be
		 *  null terminated? */
		ASSERT( fp[n++] == 0, return;); /* ASCII only! */
		value_len = fp[n++];
		DEBUG( 0, __FUNCTION__ "(), strlen=%d\n", value_len);
		ASSERT( value_len < 64, return;);
		
		DEBUG( 0, "Got string %s\n", fp+n);

		value = irias_new_string_value( fp+n);

		break;
	case IAS_OCT_SEQ:
		value_len = ntohs( *(__u16 *)( fp+n)); n += 2;
		
		/* FIXME:should be 1024, but.... */
		DEBUG( 0, __FUNCTION__ "():octet sequence:len=%d\n",
		       value_len);
		ASSERT(value_len <= 55, return;);      
		
		value = irias_new_octseq_value( fp+n, value_len);
		
		break;
	default:
		value = &missing;
		break;
	}
	
	if ( self->confirm)
		self->confirm(obj_id, value, self->priv);
	
	/*
	 * Finished, close connection!
	 */
	iriap_disconnect_request( self);
}

/*
 * Function iriap_getvaluebyclass_response ()
 *
 *    Send answer to getvaluebyclass_indication back to peer LM-IAS
 * 
 */
void iriap_getvaluebyclass_response( struct iriap_cb *self,
				     __u16 obj_id, 
				     __u8 ret_code,
				     struct ias_value *value)
{
	struct sk_buff *skb;
	__u8 *fp;
	int n;

	DEBUG( 4, "iriap_getvaluebyclass_response()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);

	/* Initialize variables */
	n = 0;

	/* 
	 *  FIXME: adjust the size of the response after the length of the 
	 *  value 
	 */
	skb = dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0, __FUNCTION__ "(),"
		       "Could not allocate an skb of length %d\n", 64);
		return;
	}

	/* Reserve space for MUX and LAP header */
 	skb_reserve( skb, LMP_CONTROL_HEADER+LAP_HEADER);
	skb_put( skb, 6);
	
	fp = skb->data;

	/* Build frame */
	fp[n++] = GET_VALUE_BY_CLASS | LST;
	fp[n++] = ret_code;

	/* Insert list length (MSB first) */
	*((__u16 *) (fp+n)) = __constant_htons( 0x0001); n += 2; 

	/* Insert object identifier ( MSB first) */
	*((__u16 *) (fp+n)) = htons( obj_id); n += 2;

	switch( value->type) {
	case IAS_STRING:
		skb_put( skb, 3 + value->len);
		fp[n++] = value->type;
		fp[n++] = 0; /* ASCII */
		fp[n++] = (__u8) value->len;
		memcpy( fp+n, value->t.string, value->len); n+=value->len;
		break;
	case IAS_INTEGER:
		skb_put( skb, 5);
		fp[n++] = value->type;
		
		*((__u32 *)(fp+n)) = htonl(value->t.integer); n+=4;
		break;
	case IAS_OCT_SEQ:
		
		/* FIXME:
		 * we can send only 55 octets at this time. 
		 * we should be able to send 1024 octets.       TH
		 */

		ASSERT(value->len <= 55, return ;);
		skb_put( skb, 3 + value->len);
		fp[n++] = value->type;
		*((__u16 *)(fp+n)) = htons(value->len); n+=2;
		memcpy(fp+n, value->t.oct_seq, value->len); n+=value->len;
		
		break;
	case IAS_MISSING:
		DEBUG( 3, __FUNCTION__ ": sending IAS_MISSING\n");
		skb_put( skb, 1);
		fp[n++] = value->type;
		break;

	default:
		DEBUG( 0, "iriap_getvaluebyclass_response: "
		       "type not implemented!\n");
		break;
	}
	iriap_do_r_connect_event( self, IAP_CALL_RESPONSE, skb);
}

/*
 * Function iriap_getvaluebyclass_indication (self, skb)
 *
 *    getvaluebyclass is requested from peer LM-IAS
 *
 */
void iriap_getvaluebyclass_indication( struct iriap_cb *self,
				       struct sk_buff *skb)
{
	__u8 *fp;
	int n;
	int name_len;
	int attr_len;
	char name[64];
	char attr[64];
 	char both[128];
 	struct ias_object *obj;
	struct ias_attrib *attrib;

	DEBUG( 4, "iriap_getvaluebyclass_indication()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	fp = skb->data;
	n = 1;

	name_len = fp[n++];
	memcpy( name, fp+n, name_len); n+=name_len;
	name[name_len] = '\0';

	attr_len = fp[n++]; 
	memcpy( attr, fp+n, attr_len); n+=attr_len;
	attr[attr_len] = '\0';

	dev_kfree_skb( skb);

	/* 
	 *  Now, do some advanced parsing! :-) 
	 */
	DEBUG( 9, "LM-IAS: Looking up %s: %s\n", name, attr);

	sprintf( both, "%s:%s", name, attr);
	DEBUG( 0, "LM-IAS: looking for %s\n", both);

	obj = irias_find_object( name);

	if ( obj == NULL) {
		DEBUG( 0, "LM-IAS: Object not found\n");
		iriap_getvaluebyclass_response( self, 0x1235, 
						IAS_CLASS_UNKNOWN, &missing);
		return;
	}
	DEBUG( 0, "LM-IAS: found %s, id=%d\n", obj->name, obj->id);
	
	attrib = irias_find_attrib( obj, attr);
	if ( attrib == NULL) {
		DEBUG( 0, "LM-IAS: Attribute %s not found\n", attr);
		iriap_getvaluebyclass_response( self, obj->id, 
						IAS_ATTRIB_UNKNOWN, &missing);
		return;
	}
	
	DEBUG( 0, "LM-IAS: found %s\n", attrib->name);
	
	/*
	 * We have a match; send the value.
	 */
	iriap_getvaluebyclass_response( self, obj->id, IAS_SUCCESS, 
					attrib->value);

	return;
}

/*
 * Function iriap_send_ack (void)
 *
 *    
 *
 */
void iriap_send_ack( struct iriap_cb *self) 
{
	struct sk_buff *skb;
	__u8 *frame;

	DEBUG( 6, "iriap_send_ack()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);

	skb =  dev_alloc_skb( 64);
	if (skb == NULL) {
		DEBUG( 0, "iriap_send_ack: "
		       "Could not allocate an sk_buff of length %d\n", 64);
		return;
	}

	/* Reserve space for MUX and LAP header */
 	skb_reserve( skb, 4);
	skb_put( skb, 3);
	frame = skb->data;

	/* Build frame */
	frame[0] = LST | self->operation;
}

/*
 * Function iriap_connect_confirm (handle, skb)
 *
 *    LSAP connection confirmed!
 *
 */
void iriap_connect_confirm( void *instance, void *sap, struct qos_info *qos, 
			    int max_sdu_size, struct sk_buff *userdata)
{
	struct iriap_cb *self;
	
	self = ( struct iriap_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);
	ASSERT( userdata != NULL, return;);
	
	DEBUG( 4, "iriap_connect_confirm()\n");
	
	del_timer( &self->watchdog_timer);

	iriap_do_client_event( self, IAP_LM_CONNECT_CONFIRM, userdata);
}

/*
 * Function iriap_connect_indication ( handle, skb)
 *
 *    Remote LM-IAS is requesting connection
 *
 */
void iriap_connect_indication( void *instance, void *sap, 
			       struct qos_info *qos, int max_sdu_size,
			       struct sk_buff *userdata)
{
	struct iriap_cb *self;

	DEBUG( 4, "iriap_connect_indication()\n");

	self = ( struct iriap_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);
	ASSERT( self->mode == IAS_SERVER, return;);

	iriap_do_server_event( self, IAP_LM_CONNECT_INDICATION, userdata);
}
 
/*
 * Function iriap_data_indication (handle, skb)
 *
 *    Receives data from connection identified by handle from IrLMP
 *
 */
void iriap_data_indication( void *instance, void *sap, struct sk_buff *skb) 
{
	struct iriap_cb *self;
	__u8  *frame;
	__u8  opcode;
	
	DEBUG( 4, "iriap_data_indication()\n"); 
	
	self = ( struct iriap_cb *) instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);

	ASSERT( skb != NULL, return;);

	frame = skb->data;
	
	if ( self->mode == IAS_SERVER) {
		/* Call server */
		DEBUG( 4, __FUNCTION__ "(), Calling server!\n");
		iriap_do_r_connect_event( self, IAP_RECV_F_LST, skb);

		return;
	}
	opcode = frame[0];
	if ( ~opcode & 0x80) {
		printk( KERN_ERR "IrIAS multiframe commands or results is "
			"not implemented yet!\n");
		return;
	}

	if ( ~opcode &0x40) {
		DEBUG( 4, "Got ack frame!\n");
	/* 	return; */
	}

	opcode &= 0x7f; /* Mask away LST bit */
	
	switch( opcode) {
	case GET_INFO_BASE:
		DEBUG( 0, "IrLMP GetInfoBaseDetails not implemented!\n");
		break;
	case GET_VALUE_BY_CLASS:
		DEBUG( 4,"IrLMP GetValueByClass\n");
		
		if ( frame[1] == 0x01) {
			printk( KERN_WARNING "IrIAP No such class!\n");
		} else if ( frame[1] == 0x02) {
			printk( KERN_WARNING 
				"IrIAP No such attribute!\n");
		} else {
			iriap_getvaluebyclass_confirm( self, skb);
		}
		iriap_do_call_event( self, IAP_RECV_F_LST, skb);

		/*  
		 *  We remove LSAPs used by IrIAS as a client since these
		 *  are more difficult to reuse!  
		 */
		iriap_close( self);
		break;
	default:
		DEBUG( 0, "iriap_data_indication: Unknown op-code: %02x\n", 
		       opcode);
		break;
	}
}

/*
 * Function iriap_call_indication (self, skb)
 *
 *    Received call to server from peer LM-IAS
 *
 */
void iriap_call_indication( struct iriap_cb *self, struct sk_buff *skb)
{
	__u8 *fp;
	__u8  opcode;

	DEBUG( 4, "iriap_call_indication()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	fp = skb->data;

	opcode = fp[0];
	if ( ~opcode & 0x80) {
		printk( KERN_ERR "IrIAS multiframe commands or results is "
			"not implemented yet!\n");
		return;
	}
	opcode &= 0x7f; /* Mask away LST bit */
	
	switch( opcode) {
	case GET_INFO_BASE:
		DEBUG( 0, "IrLMP GetInfoBaseDetails not implemented!\n");
		break;
	case GET_VALUE_BY_CLASS:
		iriap_getvaluebyclass_indication( self, skb);
		break;
	}
}

void iriap_watchdog_timer_expired( unsigned long data)
{
	struct iriap_cb *self = ( struct iriap_cb *) data;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IAS_MAGIC, return;);

	DEBUG( 0, __FUNCTION__ "() Timeout! closing myself!\n");
	iriap_close( self);
}

#ifdef CONFIG_PROC_FS

static char *ias_value_types[] = {
	"IAS_MISSING",
	"IAS_INTEGER",
	"IAS_OCT_SEQ",
	"IAS_STRING"
};

/* FIXME: This one should go in irlmp.c */
static char *ias_charset_types[] = {
	"CS_ASCII",
	"CS_ISO_8859_1",
	"CS_ISO_8859_2",
	"CS_ISO_8859_3",
	"CS_ISO_8859_4",
	"CS_ISO_8859_5",
	"CS_ISO_8859_6",
	"CS_ISO_8859_7",
	"CS_ISO_8859_8",
	"CS_ISO_8859_9",
	"CS_UNICODE"
};

int irias_proc_read( char *buf, char **start, off_t offset, int len, 
		     int unused)
{
	struct ias_object *obj;
	struct ias_attrib *attrib;
	unsigned long flags;

	ASSERT( objects != NULL, return 0;);

	save_flags( flags);
	cli();

	len = 0;

	len += sprintf( buf+len, "LM-IAS Objects:\n");

	/* List all objects */
	obj = (struct ias_object *) hashbin_get_first( objects);
	while ( obj != NULL) {
		ASSERT( obj->magic == IAS_OBJECT_MAGIC, return 0;);

		len += sprintf( buf+len, "name: %s, ", obj->name);
		len += sprintf( buf+len, "id=%d", obj->id);
		len += sprintf( buf+len, "\n");

		/* List all attributes for this object */
		attrib = (struct ias_attrib *) 
			hashbin_get_first( obj->attribs);
		while ( attrib != NULL) {
			ASSERT( attrib->magic == IAS_ATTRIB_MAGIC, return 0;);

			len += sprintf( buf+len, " - Attribute name: \"%s\", ",
 					attrib->name);
			len += sprintf( buf+len, "value[%s]: ", 
					ias_value_types[ attrib->value->type]);
			
			switch( attrib->value->type) {
			case IAS_INTEGER:
				len += sprintf( buf+len, "%d\n", 
						attrib->value->t.integer);
				break;
			case IAS_STRING:
				len += sprintf( buf+len, "\"%s\"\n", 
						attrib->value->t.string);
				break;
			case IAS_OCT_SEQ:
				len += sprintf( buf+len, "octet sequence\n");
				break;
			case IAS_MISSING:
				len += sprintf( buf+len, "missing\n");
				break;
			default:
				DEBUG( 0, __FUNCTION__ 
				       "(), Unknown value type!\n");
				return -1;
			}
			len += sprintf( buf+len, "\n");
			
			attrib = ( struct ias_attrib *) 
				hashbin_get_next( obj->attribs);
		}
	        obj = ( struct ias_object *) hashbin_get_next( objects);
 	} 
	restore_flags( flags);

	return len;
}

#endif /* PROC_FS */
