/*********************************************************************
 *                
 * Filename:      ircomm_common.c
 * Version:       
 * Description:   An implementation of IrCOMM service interface, 
 *                state machine, and incidental function(s).
 * Status:        Experimental.
 * Author:        Takahide Higuchi <thiguchi@pluto.dti.ne.jp>
 * Source:        irlpt_event.c
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
 *    Reference: 
 *    "'IrCOMM':Serial and Parallel Port Emulation Over IR(Wire Replacement)"
 *    version 1.0, which is available at http://www.irda.org/.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include <net/irda/irmod.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irttp.h>

#include <net/irda/ircomm_common.h>

#if 0
static char *rcsid = "$Id: ircomm_common.c,v 1.13 1998/10/13 12:59:05 takahide Exp $";
#endif
static char *version = "IrCOMM_common, $Revision: 1.13 $ $Date: 1998/10/13 12:59:05 $ (Takahide Higuchi)";




static void ircomm_state_discovery( struct ircomm_cb *self,
				  IRCOMM_EVENT event, struct sk_buff *skb );
static void ircomm_state_idle( struct ircomm_cb *self, IRCOMM_EVENT event, 
			       struct sk_buff *skb );
static void ircomm_state_waiti( struct ircomm_cb *self, IRCOMM_EVENT event, 
				struct sk_buff *skb );
static void ircomm_state_waitr( struct ircomm_cb *self, IRCOMM_EVENT event, 
				struct sk_buff *skb );
static void ircomm_state_conn( struct ircomm_cb *self, IRCOMM_EVENT event, 
			       struct sk_buff *skb );
static void ircomm_do_event( struct ircomm_cb *self, IRCOMM_EVENT event,
			     struct sk_buff *skb);
void ircomm_next_state( struct ircomm_cb *self, IRCOMM_STATE state);

int ircomm_check_handle(int handle);

static void ircomm_parse_control(struct ircomm_cb *self, struct sk_buff *skb,
				 int type);
static char *ircommstate[] = {
	"DISCOVERY",
	"IDLE",
	"WAITI",
	"WAITR",
	"CONN",
};

static char *ircommservicetype[] = {
	"N/A",
	"THREE_WIRE_RAW",
	"THREE_WIRE",
	"NINE_WIRE",
	"CENTRONICS",
};
static char *ircommporttype[] = {
	"Unknown",
	"SERIAL",
	"PARALLEL",
};


struct ircomm_cb **ircomm = NULL;

static char *ircommevent[] = {
	"IRCOMM_CONNECT_REQUEST",
	"TTP_CONNECT_INDICATION",
	"LMP_CONNECT_INDICATION",

	"TTP_CONNECT_CONFIRM",
	"TTP_DISCONNECT_INDICATION",
	"LMP_CONNECT_CONFIRM",
	"LMP_DISCONNECT_INDICATION",

	"IRCOMM_CONNECT_RESPONSE",
	"IRCOMM_DISCONNECT_REQUEST",

	"TTP_DATA_INDICATION",
	"IRCOMM_DATA_REQUEST",
	"LMP_DATA_INDICATION",
	"IRCOMM_CONTROL_REQUEST",
};

int ircomm_proc_read(char *buf, char **start, off_t offset,
		     int len, int unused);

#ifdef CONFIG_PROC_FS
extern struct proc_dir_entry proc_irda;
struct proc_dir_entry proc_ircomm = {
	0, 6, "ircomm",
        S_IFREG | S_IRUGO, 1, 0, 0,
        0, NULL,
        &ircomm_proc_read,
};
#endif

static void (*state[])( struct ircomm_cb *self, IRCOMM_EVENT event,
			struct sk_buff *skb) = 
{
	ircomm_state_discovery,
	ircomm_state_idle,
	ircomm_state_waiti,
	ircomm_state_waitr,
	ircomm_state_conn,
};


__initfunc(int ircomm_init(void))
{
	int i;

	printk( KERN_INFO "%s\n", version);
	DEBUG( 4, "ircomm_common:init_module\n");

	/* allocate master array */

	ircomm = (struct ircomm_cb **) kmalloc( sizeof(struct ircomm_cb *) *
						IRCOMM_MAX_CONNECTION,
						GFP_KERNEL);
	if ( ircomm == NULL) {
		printk( KERN_WARNING "IrCOMM: Can't allocate ircomm array!\n");
		return -ENOMEM;
	}

	memset( ircomm, 0, sizeof(struct ircomm_cb *) * IRCOMM_MAX_CONNECTION);

	/* initialize structures */
	   
	for(i = 0;i < IRCOMM_MAX_CONNECTION; i++){
		ircomm[i] = kmalloc( sizeof(struct ircomm_cb), GFP_KERNEL );

		if(!ircomm[i]){
			printk(KERN_ERR "ircomm:kmalloc failed!\n");
			return -ENOMEM;
		} 

		
		memset( ircomm[i], 0, sizeof(struct ircomm_cb));

		ircomm[i]->magic = IRCOMM_MAGIC;
		/* default settings */
		ircomm[i]->data_format = CS8;
		ircomm[i]->flow_ctrl = USE_RTS|USE_DTR;  /*TODO: is this OK? */
		ircomm[i]->xon_char = 0x11; 
		ircomm[i]->xoff_char = 0x13;
		ircomm[i]->enq_char = 0x05;
		ircomm[i]->ack_char = 0x06;  

		ircomm[i]->max_txbuff_size = COMM_DEFAULT_DATA_SIZE;   /* 64 */
		ircomm[i]->maxsdusize = SAR_DISABLE;  
		ircomm[i]->ctrl_skb = dev_alloc_skb(COMM_DEFAULT_DATA_SIZE);
		if (ircomm[i]->ctrl_skb == NULL){
			DEBUG(0,"ircomm:init_module:alloc_skb failed!\n");
			return -ENOMEM;
		}

		skb_reserve(ircomm[i]->ctrl_skb,COMM_HEADER_SIZE);

	}

	/*
	 * we register /proc/irda/ircomm
	 */

#ifdef CONFIG_PROC_FS
	proc_register( &proc_irda, &proc_ircomm);
#endif /* CONFIG_PROC_FS */

	return 0;
}

void ircomm_cleanup(void)
{
	int i;

	DEBUG( 4, "ircomm_common:cleanup_module\n");
	/*
	 * free some resources
	 */
	if (ircomm) {
		for (i=0; i<IRCOMM_MAX_CONNECTION; i++) {
			if (ircomm[i]) {

				if(ircomm[i]->ctrl_skb){
					dev_kfree_skb(ircomm[i]->ctrl_skb);

					ircomm[i]->ctrl_skb = NULL;
				}

				DEBUG( 4, "freeing structures(%d)\n",i);
				kfree(ircomm[i]);
				ircomm[i] = NULL;
			}
		}
		DEBUG( 4, "freeing master array\n");
		kfree(ircomm);
		ircomm = NULL;
	}

#ifdef CONFIG_PROC_FS
	proc_unregister( &proc_irda, proc_ircomm.low_ino);
#endif
}

/*
 * ---------------------------------------------------------------------- 
 * callbacks which accept incoming indication/confirm from IrTTP  (or IrLMP)
 * ---------------------------------------------------------------------- 
 */

void ircomm_accept_data_indication(void *instance, void *sap, struct sk_buff *skb){
	
	struct ircomm_cb *self = (struct ircomm_cb *)instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	ASSERT( skb != NULL, return;);
       
	DEBUG(4,"ircomm_accept_data_indication:\n");
	ircomm_do_event( self, TTP_DATA_INDICATION, skb);
}

void ircomm_accept_connect_confirm(void *instance, void *sap,
				   struct qos_info *qos, 
				   int maxsdusize, struct sk_buff *skb){

	struct ircomm_cb *self = (struct ircomm_cb *)instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	ASSERT( qos != NULL, return;);

	DEBUG(0,"ircomm_accept_connect_confirm:\n");

	if(maxsdusize == SAR_DISABLE)
		self->max_txbuff_size = qos->data_size.value; 
	else {
		ASSERT(maxsdusize >= COMM_DEFAULT_DATA_SIZE, return;);
		self->max_txbuff_size = maxsdusize; /* use fragmentation */
	}

	self->qos = qos;
	self->null_modem_mode = 0;            	/* disable null modem emulation */

	ircomm_do_event( self, TTP_CONNECT_CONFIRM, skb);
}

void ircomm_accept_connect_indication(void *instance, void *sap,
				      struct qos_info *qos,
				      int maxsdusize,
				      struct sk_buff *skb ){

	struct ircomm_cb *self = (struct ircomm_cb *)instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	ASSERT( qos != NULL, return;);

	DEBUG(0,"ircomm_accept_connect_indication:\n");

	if(maxsdusize == SAR_DISABLE)
		self->max_txbuff_size = qos->data_size.value;
	else
		self->max_txbuff_size = maxsdusize;

	self->qos = qos;
	ircomm_do_event( self, TTP_CONNECT_INDICATION, skb);
}

void ircomm_accept_disconnect_indication(void *instance, void *sap, LM_REASON reason,
					 struct sk_buff *skb){
	struct ircomm_cb *self = (struct ircomm_cb *)instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	ASSERT( skb != NULL, return;);

	DEBUG(0,"ircomm_accept_disconnect_indication:\n");
	ircomm_do_event( self, TTP_DISCONNECT_INDICATION, skb);
}

void ircomm_accept_flow_indication( void *instance, void *sap, LOCAL_FLOW cmd){
	
	struct ircomm_cb *self = (struct ircomm_cb *)instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	switch(cmd){
	case FLOW_START:
		DEBUG(0,"ircomm_accept_flow_indication:START\n"); 

		self->pi = TX_READY;
		self->ttp_stop = 0;
		if(self->notify.flow_indication)
			self->notify.flow_indication( self->notify.instance, 
						      self, cmd);
		ircomm_control_request(self);
		break;
		
	case FLOW_STOP:
		DEBUG(0,"ircomm_accept_flow_indication:STOP\n"); 
		self->pi = TX_BUSY;
		self->ttp_stop = 1;
		if(self->notify.flow_indication)
			self->notify.flow_indication( self->notify.instance, 
						      self, cmd);
		break;

	default:
		DEBUG(0,"ircomm_accept_flow_indication:unknown status!\n"); 
	}

}

/* 
 * ----------------------------------------------------------------------
 * Implementation of actions,descrived in section 7.4 of the reference.
 * ----------------------------------------------------------------------
 */


static void issue_connect_request(struct ircomm_cb *self,
				  struct sk_buff *userdata ){

	/* TODO: we have to send/build userdata field which contains 
	   InitialControlParameters */
	/* but userdata field is not implemeted in irttp.c.. */

	switch(self->servicetype){
	case THREE_WIRE_RAW:
		/* not implemented yet! Do nothing */
		DEBUG(0, "ircomm:issue_connect_request:"
		      "not implemented servicetype!");
		break;

	case DEFAULT: 
		irttp_connect_request(self->tsap, self->dlsap, self->daddr,
				      NULL, self->maxsdusize, NULL);  
 		break; 
		
	case THREE_WIRE:
	case NINE_WIRE:
	case CENTRONICS:

		irttp_connect_request(self->tsap, self->dlsap, self->daddr, 
				      NULL, self->maxsdusize, NULL); 
		break;

	default:
		DEBUG(0,"ircomm:issue_connect_request:Illegal servicetype %d\n"
		      ,self->servicetype);
	}
}	


static void disconnect_indication(struct ircomm_cb *self, struct sk_buff *skb){

	/*
	 * Not implemented parameter"Reason".That is optional.
	 * What is reason? maybe discribed in irmod.h?
	 */

	if(self->notify.disconnect_indication)
		self->notify.disconnect_indication( self->notify.instance,
						    self,
						    self->reason,skb);
						    
}

static void connect_indication(struct ircomm_cb *self, struct qos_info *qos, 
			       struct sk_buff *skb){

/* If controlparameters don't exist, we use the servicetype"DEFAULT".*/
/* 	if( !ircomm_parse_controlchannel( self, data)) */
/* 		self->servicetype = DEFAULT;     TODOD:fix this! TH */

	if(self->notify.connect_indication)
		self->notify.connect_indication(self->notify.instance, self, 
						qos, 0, skb);
}
    
#if 0
/*   it's for THREE_WIRE_RAW.*/
static void connect_indication_three_wire_raw(void){ 
	DEBUG(0,"ircomm:connect_indication_threewire():not implemented!");
}    
#endif 


static void connect_confirmation(struct ircomm_cb *self, struct sk_buff *skb){

	/* give a connect_confirm to the client */
	if( self->notify.connect_confirm )
		self->notify.connect_confirm(self->notify.instance,
					     self, NULL, SAR_DISABLE, skb);
}

static void issue_connect_response(struct ircomm_cb *self,
				   struct sk_buff *skb ){

	DEBUG(0,"ircomm:issue_connect_response:\n");
	
	if( self->servicetype == THREE_WIRE_RAW){
		DEBUG(0,"ircomm:issue_connect_response():3WIRE-RAW is not " 
		      "implemented yet !\n");
		/* irlmp_connect_rsp(); */
	} else {
		irttp_connect_response(self->tsap, self->maxsdusize, skb);
	}
}

static void issue_disconnect_request(struct ircomm_cb *self,
				struct sk_buff *userdata ){
	if(self->servicetype == THREE_WIRE_RAW){
		DEBUG(0,"ircomm:issue_disconnect_request():3wireraw is not implemented!");
	}
	else
		irttp_disconnect_request(self->tsap, NULL, P_NORMAL);
}
    
static void issue_data_request(struct ircomm_cb *self,
			       struct sk_buff *userdata ){
	int err;

	if(self->servicetype == THREE_WIRE_RAW){
		/* irlmp_data_request(self->lmhandle,userdata); */
		DEBUG(0,"ircomm:issue_data_request():not implemented!");
		return;
	}

	DEBUG(4,"ircomm:issue_data_request():sending frame\n");
	err = irttp_data_request(self->tsap , userdata  );
	if(err)
		DEBUG(0,"ircomm:ttp_data_request failed\n");
	if(userdata && err)
		dev_kfree_skb( userdata);

}

static void issue_control_request(struct ircomm_cb *self,
				  struct sk_buff *userdata ){
	if(self->servicetype == THREE_WIRE_RAW){
		DEBUG(0,"THREE_WIRE_RAW is not implemented\n");
		
	}else {
		irttp_data_request(self->tsap,userdata);
	}
}


static void process_data(struct ircomm_cb *self, struct sk_buff *skb ){
	
	DEBUG(4,"ircomm:process_data:skb_len is(%d),clen_is(%d)\n",
	      (int)skb->len ,(int)skb->data[0]);

	/* 
	 * we always have to parse control channel
	 *  (see page17 of IrCOMM standard) 
	 */

 	ircomm_parse_control(self, skb, CONTROL_CHANNEL);

	if(self->notify.data_indication && skb->len)
		self->notify.data_indication(self->notify.instance, self,
					     skb);
}

void ircomm_data_indication(struct ircomm_cb *self, struct sk_buff *skb){
	/* Not implemented yet:THREE_WIRE_RAW service uses this function.  */
	DEBUG(0,"ircomm_data_indication:not implemented yet!\n");
}


/* 
 * ----------------------------------------------------------------------
 * Implementation of state chart,
 * descrived in section 7.1 of the specification.
 * ----------------------------------------------------------------------
 */

static void ircomm_do_event( struct ircomm_cb *self, IRCOMM_EVENT event,
			     struct sk_buff *skb) {
	
	DEBUG( 4, "ircomm_do_event: STATE = %s, EVENT = %s\n",
	       ircommstate[self->state], ircommevent[event]);
	(*state[ self->state ]) ( self, event, skb);
}

void ircomm_next_state( struct ircomm_cb *self, IRCOMM_STATE state) {
	self->state = state;
	DEBUG( 0, "ircomm_next_state: NEXT STATE = %d(%s), sv(%d)\n", 
	       (int)state, ircommstate[self->state],self->servicetype);
}


/* 
 * we currently need dummy (discovering) state for debugging,
 * which state is not defined in the reference.
 */

static void ircomm_state_discovery( struct ircomm_cb *self,
				    IRCOMM_EVENT event, struct sk_buff *skb ){
	DEBUG(0,"ircomm_state_discovery: "
	      "why call me? \n");
	if(skb)
		dev_kfree_skb( skb);
}


/*
 * ircomm_state_idle
 */

static void ircomm_state_idle( struct ircomm_cb *self, IRCOMM_EVENT event, 
			       struct sk_buff *skb ){
	switch(event){
	case IRCOMM_CONNECT_REQUEST:

		ircomm_next_state(self, COMM_WAITI);
		issue_connect_request( self, skb );
		break;
		
	case TTP_CONNECT_INDICATION:

		ircomm_next_state(self, COMM_WAITR);
		connect_indication( self, self->qos, skb);
		break;

	case LMP_CONNECT_INDICATION:

		/* I think this is already done in irlpt_event.c */

		DEBUG(0,"ircomm_state_idle():LMP_CONNECT_IND is notimplemented!");
 		/* connect_indication_three_wire_raw(); */
 		/* ircomm_next_state(self, COMM_WAITR); */
		break;

	default:
		DEBUG(0,"ircomm_state_idle():unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}

/*
 * ircomm_state_waiti
 */

static void ircomm_state_waiti(struct ircomm_cb *self, IRCOMM_EVENT event, 
			  struct sk_buff *skb ){
	switch(event){
	case TTP_CONNECT_CONFIRM:
		ircomm_next_state(self, COMM_CONN);
		connect_confirmation( self, skb );
		break;
	case TTP_DISCONNECT_INDICATION:
		ircomm_next_state(self, COMM_IDLE);
		disconnect_indication(self, skb);
		break;
/* 	case LMP_CONNECT_CONFIRM: */
/* 		ircomm_connect_cnfirmation; */
/* 		ircomm_next_state(self, COMM_CONN); */
/* 		break; */
/* 	case LMP_DISCONNECT_INDICATION: */
/* 		ircomm_disconnect_ind; */
/* 		ircomm_next_state(self, COMM_IDLE); */
/* 		break; */
	default:
		DEBUG(0,"ircomm_state_waiti:unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}



/*
 * ircomm_state_waitr
 */
static void ircomm_state_waitr(struct ircomm_cb *self, IRCOMM_EVENT event, 
			  struct sk_buff *skb ) {
	
	switch(event){
	case IRCOMM_CONNECT_RESPONSE:

	        /* issue_connect_response */
		
		if(self->servicetype==THREE_WIRE_RAW){
			DEBUG(0,"ircomm:issue_connect_response:"
			      "THREE_WIRE_RAW is not implemented!\n");
			/* irlmp_connect_response(Vpeersap,
			 *                         ACCEPT,null);
			 */
		} else {
			ircomm_next_state(self, COMM_CONN);
			issue_connect_response(self, skb);
		}
		break;

	case IRCOMM_DISCONNECT_REQUEST:
		ircomm_next_state(self, COMM_IDLE);
		issue_disconnect_request(self, skb);
		break;

	case TTP_DISCONNECT_INDICATION:
		ircomm_next_state(self, COMM_IDLE);
		disconnect_indication(self, skb);
		break;

/* 	case LMP_DISCONNECT_INDICATION: */
/* 		disconnect_indication();		 */
/* 		ircomm_next_state(self, COMM_IDLE);		 */
/* 		break; */
	default:
		DEBUG(0,"ircomm_state_waitr:unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}

/*
 * ircomm_state_conn
 */

static void ircomm_state_conn(struct ircomm_cb *self, IRCOMM_EVENT event, 
			  struct sk_buff *skb ){
	switch(event){
	case TTP_DATA_INDICATION:
		process_data(self, skb);
		/* stay CONN state*/
		break;
	case IRCOMM_DATA_REQUEST:
		issue_data_request(self, skb);
		/* stay CONN state*/
		break;
/* 	case LMP_DATA_INDICATION: */
/* 		ircomm_data_indicated(); */
/* 		 stay CONN state */
/* 		break; */
	case IRCOMM_CONTROL_REQUEST:
		issue_control_request(self, skb);
		/* stay CONN state*/
		break;
	case TTP_DISCONNECT_INDICATION:
		ircomm_next_state(self, COMM_IDLE);
		disconnect_indication(self, skb);
		break;
	case IRCOMM_DISCONNECT_REQUEST:
		ircomm_next_state(self, COMM_IDLE);
		issue_disconnect_request(self, skb);
		break;
/* 	case LM_DISCONNECT_INDICATION: */
/* 		disconnect_indication(); */
/* 		ircomm_next_state(self, COMM_IDLE); */
/* 		break; */
	default:
		DEBUG(0,"ircomm_state_conn:unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}


/*
 *  ----------------------------------------------------------------------
 *  ircomm requests
 *
 *  ----------------------------------------------------------------------
 */


void ircomm_connect_request(struct ircomm_cb *self, int maxsdusize){

	/*
	 * TODO:build a packet which contains "initial control parameters"
	 * and send it with connect_request
	 */

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	DEBUG(0,"ircomm_connect_request:\n");

	self->maxsdusize = maxsdusize;
	ircomm_do_event( self, IRCOMM_CONNECT_REQUEST, NULL);
}

void ircomm_connect_response(struct ircomm_cb *self, struct sk_buff *userdata,
			     int maxsdusize){

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	/* ASSERT( userdata != NULL, return;); */

	DEBUG(4,"ircomm_connect_response:\n");

	/*
	 * TODO:build a packet which contains "initial control parameters"
	 * and send it with connect_response
	 */

	if(!userdata){
		/* FIXME: check for errors and initialize? DB */
		userdata = dev_alloc_skb(COMM_DEFAULT_DATA_SIZE);
		if(!userdata){
		  DEBUG(0, __FUNCTION__"alloc_skb failed\n");
		  return;
		}
		IS_SKB(userdata, return;);
		skb_reserve(userdata,COMM_HEADER_SIZE);
	}

	/* enable null-modem emulation (i.e. server mode )*/
	self->null_modem_mode = 1;

	self->maxsdusize = maxsdusize;
	if(maxsdusize != SAR_DISABLE)
		self->max_txbuff_size = maxsdusize;
	ircomm_do_event(self, IRCOMM_CONNECT_RESPONSE, userdata);
}	

void ircomm_disconnect_request(struct ircomm_cb *self, struct sk_buff *userdata){

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	DEBUG(0,"ircomm_disconnect_request\n");
	ircomm_do_event(self, IRCOMM_DISCONNECT_REQUEST, NULL);
}	


void ircomm_data_request(struct ircomm_cb *self, struct sk_buff *userdata){

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	ASSERT( userdata != NULL, return;);
	
	if(self->state != COMM_CONN){
		DEBUG(4,"ignore IRCOMM_DATA_REQUEST:not connected\n");
		if(userdata)
			dev_kfree_skb(userdata);
		return;
	}

	DEBUG(4,"ircomm_data_request\n");
	ircomm_do_event(self, IRCOMM_DATA_REQUEST, userdata);
}

/*
 *  ----------------------------------------------------------------------
 *  IrCOMM_control.req and friends
 *
 *  ----------------------------------------------------------------------
 */

static void ircomm_tx_ctrlbuffer(struct ircomm_cb *self ){

	__u8 clen;
	struct sk_buff *skb = self->ctrl_skb;
	
	DEBUG(4,"ircomm_tx_ctrlbuffer:\n");

	/* add "clen" field */

	clen=skb->len;
	if(clen){
		skb_push(skb,1);
		skb->data[0]=clen;

#if 0
	printk("tx_ctrl:");
	{
		int i;
		for ( i=0;i<skb->len;i++)
			printk("%02x", skb->data[i]);
		printk("\n");
	}
#endif

		ircomm_do_event(self, IRCOMM_CONTROL_REQUEST, skb);

		skb = dev_alloc_skb(COMM_DEFAULT_DATA_SIZE);
		if (skb==NULL){
			DEBUG(0,"ircomm_tx_ctrlbuffer:alloc_skb failed!\n");
			return;
		}
		skb_reserve(skb,COMM_HEADER_SIZE);
		self->ctrl_skb = skb;
	}
}


void ircomm_control_request(struct ircomm_cb *self){
	
	struct sk_buff *skb;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	DEBUG(0, "ircomm_control_request:\n");

	if(self->ttp_stop || self->state != COMM_CONN){
		DEBUG(0,"ircomm_control_request:can't send it.. ignore it\n");
		return;
	}
	
	skb = self->ctrl_skb;
	IS_SKB(skb,return;);
		
	if(skb->len)
		ircomm_tx_ctrlbuffer(self);
}


static void append_tuple(struct ircomm_cb *self,
			 __u8 instruction, __u8 pl , __u8 *value){

	__u8 *frame;
	struct sk_buff *skb;
	int i,c;

	skb = self->ctrl_skb;
	ASSERT(skb != NULL, return;);
	IS_SKB(skb,return;);
	
	/*if there is little room in the packet... */

	if(skb->len > COMM_DEFAULT_DATA_SIZE - COMM_HEADER_SIZE - (pl+2)){
		if(!self->ttp_stop && self->state == COMM_CONN){

			/* send a packet if we can */
			ircomm_tx_ctrlbuffer(self);
			skb = self->ctrl_skb;
		} else {
			DEBUG(0, "ircomm_append_ctrl:there's no room.. ignore it\n");

			/* TODO: we have to detect whether we have to resend some
			   information after ttp_stop is cleared */

			/* self->resend_ctrl = 1; */
			return;
		}
	}

	frame = skb_put(skb,pl+2);
	c = 0;
	frame[c++] = instruction;     /* PI */
	frame[c++] = pl;              /* PL */
	for(i=0; i < pl ; i++)
		frame[c++] = *value++;   /* PV */
	
}



/*
 * ircomm_append_ctrl();
 * this function is exported as a request to send some control-channel tuples
 * to peer device
 */

void ircomm_append_ctrl(struct ircomm_cb *self,  __u8 instruction){

	__u8  pv[70];
	__u8  *value = &pv[0];
	__u32 temp;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	if(self->state != COMM_CONN)
		return;

	if(self->servicetype == THREE_WIRE_RAW){
		DEBUG(0,"THREE_WIRE_RAW shuold not use me!\n");
		return;
	}

	DEBUG(4,"ircomm_append_ctrl:\n");

	/* find parameter and its length */

	switch(instruction){

	case POLL_FOR_LINE_SETTINGS:
	case STATUS_QUERY:
	case IEEE1284_MODE_SUPPORT:
	case IEEE1284_DEVICEID:
		append_tuple(self,instruction,0,NULL);
		break;

	case SERVICETYPE:
		value[0] = self->servicetype;
		append_tuple(self,instruction,1,value);
		break;
	case DATA_FORMAT:
		value[0] = self->data_format;
		append_tuple(self,instruction,1,value);
		break;
	case FLOW_CONTROL:
		if(self->null_modem_mode){

			/* inside out */
			value[0]  = (self->flow_ctrl & 0x55) << 1;
			value[0] |= (self->flow_ctrl & 0xAA) >> 1;
		}else{
			value[0] = self->flow_ctrl;
		}
		append_tuple(self,instruction,1,value);
		break;
	case LINESTATUS:
		value[0] = self->line_status;
		append_tuple(self,instruction,1,value);
		break;
	case BREAK_SIGNAL:
		value[0] = self->break_signal;
		append_tuple(self,instruction,1,value);
		break;
	case DTELINE_STATE:		
		if(self->null_modem_mode){
			/* null modem emulation */

			/* output RTS as CTS */

			if(self->dte & DELTA_RTS)
				value[0] = DELTA_CTS;
			if(self->dte & MCR_RTS)
				value[0] |= MSR_CTS;

			/* output DTR as {DSR & CD & RI} */

			if(self->dte & DELTA_DTR)
				value[0] |= (DELTA_DSR|DELTA_RI|DELTA_DCD);
			if(self->dte & MCR_DTR)
				value[0] |= (MSR_DSR|MSR_RI|MSR_DCD);
			append_tuple(self,DCELINE_STATE,1,value);
		}else{
			value[0] = self->dte;
			append_tuple(self,instruction,1,value);
		}
		self->dte &= ~(DELTA_RTS|DELTA_DTR);
		break;

	case DCELINE_STATE:		
		value[0] = self->dce;
		append_tuple(self,instruction,1,value);
		break;
	case SET_BUSY_TIMEOUT:
		value[0] = self->busy_timeout;
		append_tuple(self,instruction,1,value);
		break;

	case XON_XOFF_CHAR:
		value[0] = self->xon_char;
		value[1] = self->xoff_char;
		append_tuple(self,instruction,2,value); 
		break; 

	case ENQ_ACK_CHAR:
		value[0] = self->enq_char;
		value[1] = self->ack_char;
		append_tuple(self,instruction,2,value); 
		break; 

 	case IEEE1284_ECP_EPP_DATA_TRANSFER: 
		value[0] = self->ecp_epp_mode;
		value[1] = self->channel_or_addr;
		append_tuple(self,instruction,2,value); 
		break; 

	case DATA_RATE:
		temp = self->data_rate;
		value[3] = (__u8)((temp >> 24) & 0x000000ff);
		value[2] = (__u8)((temp >> 16) & 0x000000ff);
		value[1] = (__u8)((temp >> 8) & 0x000000ff);
		value[0] = (__u8)(temp & 0x000000ff);
 		append_tuple(self,instruction,4,value);
 		break;

#if 0
	case PORT_NAME:
	case FIXED_PORT_NAME:
		temp = strlen(&self->port_name);
		if(temp < 70){
			value = (__u8) (self->port_name);
			append_tuple(self,instruction,temp,value);
		}
		break;
#endif

/* TODO: control tuples for centronics emulation is not implemented */
/* 	case IEEE1284_MODE: */

	default:
		DEBUG(0,"ircomm_append_ctrl:instruction(0x%02x)is not"
		      "implemented\n",instruction);
	}


}

static void ircomm_parse_control(struct ircomm_cb *self,
				 struct sk_buff *skb,
				 int type){

	__u8 *data;
	__u8 pi,pl,pv[64];
	int clen = 0;
	int i,indicate,count = 0;

	
	data = skb->data;
	if(type == IAS_PARAM) 
		clen = ((data[count++] << 8) & data[count++]); /* MSB first */
	else /* CONTROL_CHANNEL */
		clen = data[count++];


	if(clen == 0){
		skb_pull( skb, 1);  /* remove clen field */
		return;
	}




	while( count < clen ){
		/* 
		 * parse controlparameters and set value into structure 
		 */
		pi = data[count++];
		pl = data[count++];
			
		DEBUG(0, "parse_control:instruction(0x%02x)\n",pi) ;


		/* copy a tuple into pv[] */

#ifdef IRCOMM_DEBUG_TUPLE
		printk("data:");
		for(i=0; i < pl; i++){
			pv[i] = data[count++];
			printk("%02x",pv[i]);
		}
		printk("\n");
#else
		for(i=0; i < pl; i++)
			pv[i] = data[count++];
#endif			
		

		/* parse pv */
		indicate = 0;

		switch(pi){
			
			/*
			 * for 3-wire/9-wire/centronics
			 */
			
		case SERVICETYPE:
			self->peer_servicetype = pv[0];
			break;
		case PORT_TYPE:
			self->peer_port_type = pv[0];
			break;
#if 0 
		case PORT_NAME:
			self->peer_port_name = *pv;
			break;
		case FIXED_PORT_NAME:
			self->peer_port_name = *pv;
			/* 
			 * We should not connect if user of IrCOMM can't
			 * recognize the port name
			 */
			self->port_name_critical = TRUE;
			break;
#endif
		case DATA_RATE:     
			self->peer_data_rate	= (pv[3]<<24) & (pv[2]<<16)
				& (pv[1]<<8) & pv[0];
			indicate = 1;
			break;
		case DATA_FORMAT:   
			self->peer_data_format = pv[0];
			break;
		case FLOW_CONTROL:
			self->peer_flow_ctrl = pv[0];
			indicate = 1;
			break;
		case XON_XOFF_CHAR:
			self->peer_xon_char = pv[0];
			self->peer_xoff_char = pv[1];
			indicate = 1;
			break;
		case ENQ_ACK_CHAR:
			self->peer_enq_char = pv[0];
			self->peer_ack_char = pv[1];
			indicate = 1;
			break;
		case LINESTATUS:
			self->peer_line_status = pv[0];
			indicate = 1;
			break;
		case BREAK_SIGNAL:
			self->peer_break_signal = pv[0];
			/* indicate = 1; */
			break;
			
			/*
			 * for 9-wire
			 */

		case DTELINE_STATE:
			if(self->null_modem_mode){
				/* input DTR as {DSR & CD & RI} */
				self->peer_dce = 0;
				if(pv[0] & DELTA_DTR)
					self->peer_dce |= DELTA_DSR|DELTA_RI|DELTA_DCD;
				if(pv[0] & MCR_DTR)
					self->peer_dce |= MSR_DSR|MSR_RI|MSR_DCD;

				/* rts as cts */
				if(pv[0] & DELTA_RTS)
					self->peer_dce |= DELTA_CTS;
				if(pv[0] & MCR_RTS)
					self->peer_dce |= MSR_CTS;
			}else{
				self->peer_dte = pv[0];
			}
			indicate = 1;			
			break;

		case DCELINE_STATE:
			self->peer_dce = pv[0];
			indicate = 1;
			break;

		case POLL_FOR_LINE_SETTINGS:
			ircomm_append_ctrl(self, DTELINE_STATE);
			ircomm_control_request(self);
			break;

			/*
			 * for centronics .... not implemented yet
			 */
/* 			case STATUS_QUERY: */
/* 			case SET_BUSY_TIMEOUT: */
/* 			case IEEE1284_MODE_SUPPORT: */
/* 			case IEEE1284_DEVICEID: */
/* 			case IEEE1284_MODE: */
/* 			case IEEE1284_ECP_EPP_DATA_TRANSFER:	 */
			
		default:
			DEBUG(0, "ircomm_parse_control:not implemented "
			      "instruction(%d)\n", pi);
			break;
		}

		if(indicate && self->notify.flow_indication 
		   && type == CONTROL_CHANNEL){
			
			DEBUG(0,"ircomm:parse_control:indicating..:\n");
			self->pi = pi;
			if(self->notify.flow_indication)
				self->notify.flow_indication(self->notify.instance, self, 0);
			indicate = 0;
		}
	}
	skb_pull( skb, 1+clen);
	return;
}

/*
 * ----------------------------------------------------------------------
 * Function init_module(void) ,cleanup_module()
 *
 *   Initializes the ircomm control structure
 *   These Function are called when you insmod / rmmod .
 * ----------------------------------------------------------------------
 */

#ifdef MODULE

int init_module(void) {
	ircomm_init();

	DEBUG( 4, "ircomm:init_module:done\n");
	return 0;
}
	
void cleanup_module(void)
{
	ircomm_cleanup();
	DEBUG( 0, "ircomm_common:cleanup_module:done.\n");
}

#endif /* MODULE */

/************************************************************
 *     proc stuff
 ************************************************************/

#ifdef CONFIG_PROC_FS

/*
 * Function proc_ircomm_read (buf, start, offset, len, unused)
 *
 * this function is called if there is a access on /proc/irda/comm .
 *
 */
int ircomm_proc_read(char *buf, char **start, off_t offset,
		     int len, int unused){
	int i, index;

	len = 0;
	for (i=0; i<IRCOMM_MAX_CONNECTION; i++) {

		if (ircomm[i] == NULL || ircomm[i]->magic != IRCOMM_MAGIC) {
			len += sprintf(buf+len, "???\t");
		}else {
			switch (ircomm[i]->servicetype) {
			case UNKNOWN:
				index = 0;
				break;
			case THREE_WIRE_RAW:
				index = 1;
				break;
			case THREE_WIRE:
				index = 2;
				break;
			case NINE_WIRE:
				index = 3;
				break;
			case CENTRONICS:
				index = 4;
				break;
			default:
				index = 0;
				break;
			}
			len += sprintf(buf+len, "service: %s\t",
				       ircommservicetype[index]);
			if(index){
				len += sprintf(buf+len, "porttype: %s  ",
					       ircommporttype[ircomm[i]->port_type]);
				len += sprintf(buf+len, "state: %s  ",
					       ircommstate[ircomm[i]->state]);
				len += sprintf(buf+len, "user: %s  ",
					       ircomm[i]->notify.name);
				len += sprintf(buf+len, "nullmodem emulation: %s",
					       (ircomm[i]->null_modem_mode ? "yes":"no"));
			}
		}
		len += sprintf(buf+len, "\n");
	}
	return len;
}


#endif /* CONFIG_PROC_FS */

