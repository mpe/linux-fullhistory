/*********************************************************************
 *                
 * Filename:      attach.c
 * Version:       
 * Description:   An implementation of IrCOMM service interface.
 * Status:        Experimental.
 * Author:        Takahide Higuchi <thiguchi@pluto.dti.ne.jp>
 *
 *     Copyright (c) 1998, Takahide Higuchi, <thiguchi@pluto.dti.ne.jp>,
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     I, Takahide Higuchi, provide no warranty for any of this software.
 *     This material is provided "AS-IS" and at no charge.
 *
 ********************************************************************/


/*
 * ----------------------------------------------------------------------
 * IrIAS related things for IrCOMM
 * If you are to use ircomm layer, use ircomm_attach_cable to
 * setup it and register your program.
 * ----------------------------------------------------------------------
 */


#include <linux/sched.h>
#include <linux/tqueue.h>

#include <net/irda/irlap.h>
#include <net/irda/irttp.h>
#include <net/irda/iriap.h>
#include <net/irda/irias_object.h>

#include <net/irda/ircomm_common.h>

extern struct ircomm_cb **ircomm;
struct ircomm_cb *discovering_instance;

static void got_lsapsel(struct ircomm_cb * info);
static void query_lsapsel(struct ircomm_cb * self);
void ircomm_getvalue_confirm( __u16 obj_id, struct ias_value *value, void *priv );

#if 0
static char *rcsid = "$Id: attach.c,v 1.11 1998/10/22 12:02:20 dagb Exp $";
#endif


/*
 * handler for iriap_getvaluebyclass_request() 
 *
 */

void ircomm_getvalue_confirm( __u16 obj_id, struct ias_value *value, void *priv ){

	struct ircomm_cb *self = (struct ircomm_cb *) priv;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	DEBUG(0, __FUNCTION__"type(%d)\n", value->type);

	switch(value->type){

 	case IAS_OCT_SEQ:
 		/* 
		 * FIXME:we should use data which came here
		 * it is used for nothing at this time 
		 */
		
#if 1
		DEBUG(0, "octet sequence is:\n");
		{
			int i;
			for ( i=0;i<value->len;i++)
				printk("%02x",
				       (int)(*value->t.oct_seq + i) );
			printk("\n");
		}
#endif
		query_lsapsel(self);
		break;

	case IAS_INTEGER:
		/* LsapSel seems to be sent to me */	

		if ( value->t.integer == -1){
			DEBUG( 0, "ircomm_getvalue_confirm: invalid value!\n");
			return;
		}
		if(self->state == COMM_IDLE){
			self->dlsap = value->t.integer;
			got_lsapsel(self);
		}
		break;

	case IAS_STRING:
		DEBUG( 0, __FUNCTION__":STRING is not implemented\n");
		DEBUG( 0, __FUNCTION__":received string:%s\n",
		       value->t.string);
		query_lsapsel(self);  /* experiment */
		break;

	case IAS_MISSING:
		DEBUG( 0, __FUNCTION__":MISSING is not implemented\n");
		break;
   
	default:
		DEBUG( 0, __FUNCTION__":unknown type!\n");
		break;
	}
}


static void got_lsapsel(struct ircomm_cb * self){

	struct notify_t notify;

	DEBUG(0, "ircomm:got_lsapsel: got peersap!(%d)\n", self->dlsap );

	/* remove tsap for server */
	irttp_close_tsap(self->tsap);

	/* create TSAP for initiater ... */
	irda_notify_init(&notify);
	notify.data_indication = ircomm_accept_data_indication;
	notify.connect_confirm = ircomm_accept_connect_confirm;
	notify.connect_indication = ircomm_accept_connect_indication;
	notify.flow_indication = ircomm_accept_flow_indication;
	notify.disconnect_indication = ircomm_accept_disconnect_indication;
	strncpy( notify.name, "IrCOMM cli", NOTIFY_MAX_NAME);
	notify.instance = self;
	
	self->tsap = irttp_open_tsap(LSAP_ANY, DEFAULT_INITIAL_CREDIT,
				     &notify );
	ASSERT(self->tsap != NULL, return;);


	/*
	 * invoke state machine  
	 * and notify that I'm ready to accept connect_request
	 */

	ircomm_next_state(self, COMM_IDLE);	
  	if(self->d_handler)
 		self->d_handler(self);
}




		
static void query_lsapsel(struct ircomm_cb * self){

	DEBUG(0, "ircomm:query_lsapsel..\n");

	/*  
	 *  since we've got Parameters field of IAS, we are to get peersap.
	 */
	
	if(!(self->servicetype & THREE_WIRE_RAW)){
		iriap_getvaluebyclass_request
			(self->daddr, "IrDA:IrCOMM", "IrDA:TinyTP:LsapSel",
			 ircomm_getvalue_confirm, self );
	} else {
		DEBUG(0,"ircomm:query_lsap:"
		      "THREE_WIRE_RAW is not implemented!\n");
	}
}



/*
 * ircomm_discovery_indication()
 *    Remote device is discovered, try query the remote IAS to see which
 *    device it is, and which services it has.
 */

void ircomm_discovery_indication( DISCOVERY *discovery)
{

	struct ircomm_cb *self;

 	DEBUG( 0, "ircomm_discovery_indication\n");
	
	self = discovering_instance;
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRCOMM_MAGIC, return;);

	self->daddr = discovery->daddr;

	DEBUG( 0, "ircomm_discovery_indication:daddr=%08x\n", self->daddr);

	/* query "Parameters" attribute of LM-IAS */

	DEBUG(0, "ircomm:querying parameters..\n");
#if 0
	iriap_getvaluebyclass_request(self->daddr, "IrDA:IrCOMM",

				      "Parameters",
				      ircomm_getvalue_confirm,
				      self);
#else
	query_lsapsel(self);
#endif
	return;
}


struct ircomm_cb * ircomm_attach_cable( __u8 servicetype,
					struct notify_t notify, 
					void *handler ){

	int i;
	struct ircomm_cb *self = NULL;
	struct notify_t server_notify;
	struct ias_object* obj;

	/* FIXME: it should not be hard coded */
	__u8 oct_seq[6] = { 0,1,4,1,1,1 }; 

	ASSERT(ircomm != NULL,return NULL;);
	DEBUG(0,"ircomm_attach_cable:\n");


	/* find free handle */

	for(i = 0; i < IRCOMM_MAX_CONNECTION; i++){
		ASSERT(ircomm[i] != NULL,return(NULL););
		if(!ircomm[i]->in_use){
			self = ircomm[i];
			break;
		}
	}

	if (!self){
		DEBUG(0,"ircomm_attach_cable:no free handle!\n");
		return (NULL);
	}

	self->in_use = 1;
	self->servicetype = servicetype;

	DEBUG(0,"attach_cable:servicetype:%d\n",servicetype);
	self->d_handler = handler;
	self->notify = notify;

	/* register server.. */

	/*
	 * TODO: since server TSAP is currentry hard coded,
	 * we can use *only one* IrCOMM connection.
	 * We have to create one more TSAP and register IAS entry dynamically 
	 * each time when we are to allocate new server here.
	 */
	irda_notify_init(&server_notify);
	server_notify.data_indication = ircomm_accept_data_indication;
	server_notify.connect_confirm = ircomm_accept_connect_confirm;
	server_notify.connect_indication = ircomm_accept_connect_indication;
	server_notify.flow_indication = ircomm_accept_flow_indication;
	server_notify.disconnect_indication = ircomm_accept_disconnect_indication;
	server_notify.instance = self;
	strncpy( server_notify.name, "IrCOMM srv", NOTIFY_MAX_NAME);

	self->tsap = irttp_open_tsap(LSAP_ANY, DEFAULT_INITIAL_CREDIT,
				     &server_notify);
	if(!self->tsap){
		DEBUG(0,"ircomm:Sorry, failed to allocate server_tsap\n");
		return NULL;
	}

        /* 
         *  Register with LM-IAS
         */
	obj = irias_new_object( "IrDA:IrCOMM", IAS_IRCOMM_ID);
	irias_add_integer_attrib( obj, "IrDA:TinyTP:LsapSel", 
				  self->tsap->stsap_sel );

	/* FIXME: it should not be hard coded */

	irias_add_octseq_attrib( obj, "Parameters", 
				 &oct_seq[0], 6);
	irias_insert_object( obj);

/* 	obj = irias_new_object( "IrDA:IrCOMM", IAS_IRCOMM_ID); */
/* 	irias_add_octseq_attrib( obj, "Parameters", len, &octseq); */
/* 	irias_insert_object( obj); */



	/* and start discovering .. */
	discovering_instance = self;

	switch(servicetype){
	case NINE_WIRE:
		DEBUG(0,"ircomm_attach_cable:discovering..\n");
		irlmp_register_layer(S_COMM , CLIENT|SERVER, TRUE,
				     ircomm_discovery_indication);
		break;

/* 	case CENTRONICS: */
/* 	case THREE_WIRE: */
/* 	case THREE_WIRE_RAW: */

	default:
		DEBUG(0,"ircomm_attach_cable:requested servicetype is not "
		      "implemented!\n");
		return NULL;
	}

	ircomm_next_state(self, COMM_IDLE);	
	return (self);
}




int ircomm_detach_cable(struct ircomm_cb *self){

	ASSERT( self != NULL, return -EIO;);
	ASSERT( self->magic == IRCOMM_MAGIC, return -EIO;);


	DEBUG(0,"ircomm_detach_cable:\n");

	/* shutdown ircomm layer */
	if(self->state != COMM_IDLE ){
		DEBUG(0,"ircomm:detach_cable:not IDLE\n");
		if(self->state != COMM_WAITI)
			ircomm_disconnect_request(self, NULL);
	}


	switch(self->servicetype){
/* 	case CENTRONICS: */
	case NINE_WIRE:
/* 	case THREE_WIRE: */
		irlmp_unregister_layer( S_COMM, CLIENT|SERVER );
		break;

/* 	case THREE_WIRE_RAW: */
/* 		irlmp_unregister( S_COMM ) */
/*		irlmp_unregister( S_PRINTER) */
/* 		break; */

	default:
		DEBUG(0,"ircomm_detach_cable:requested servicetype is not "
		      "implemented!\n");
		return -ENODEV;
	}

	/* remove tsaps */
	if(self->tsap)
		irttp_close_tsap(self->tsap);

	self->tsap = NULL;
	self->in_use = 0;
	return 0;
}
