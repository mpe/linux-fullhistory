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
 *     Copyright (c) 1998-1999, Takahide Higuchi, <thiguchi@pluto.dti.ne.jp>,
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

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irttp.h>
#include <net/irda/irias_object.h>

#include <net/irda/ircomm_common.h>

static char *revision_date = "Sun Apr 18 00:40:19 1999";


static void ircomm_state_idle(struct ircomm_cb *self, IRCOMM_EVENT event, 
			      struct sk_buff *skb );
static void ircomm_state_discoverywait(struct ircomm_cb *self, 
				       IRCOMM_EVENT event, 
				       struct sk_buff *skb );
static void ircomm_state_queryparamwait(struct ircomm_cb *self, 
					IRCOMM_EVENT event, 
					struct sk_buff *skb );
static void ircomm_state_querylsapwait(struct ircomm_cb *self, 
				       IRCOMM_EVENT event, 
				       struct sk_buff *skb );
static void ircomm_state_waiti( struct ircomm_cb *self, IRCOMM_EVENT event, 
				struct sk_buff *skb );
static void ircomm_state_waitr( struct ircomm_cb *self, IRCOMM_EVENT event, 
				struct sk_buff *skb );
static void ircomm_state_conn( struct ircomm_cb *self, IRCOMM_EVENT event, 
			       struct sk_buff *skb );
static void ircomm_do_event( struct ircomm_cb *self, IRCOMM_EVENT event,
			     struct sk_buff *skb);
static void ircomm_next_state( struct ircomm_cb *self, IRCOMM_STATE state);

static void ircomm_discovery_indication(discovery_t *discovery);
static void ircomm_tx_controlchannel(struct ircomm_cb *self );
static int ircomm_proc_read(char *buf, char **start, off_t offset,
			    int len, int unused);

static void start_discovering(struct ircomm_cb *self);
static void query_lsapsel(struct ircomm_cb * self);
static void query_parameters(struct ircomm_cb *self);
static void queryias_done(struct ircomm_cb *self);
static void ircomm_getvalue_confirm(int result, __u16 obj_id, 
				    struct ias_value *value, void *priv);


struct ircomm_cb *discovering_instance;

/*
 * debug parameter ircomm_cs:
 *  0 = client/server, 1 = client only 2 = server only
 * usage for example: 
 *  insmod ircomm ircomm_cs=1
 *  LILO boot : Linux ircomm_cs=2   etc.
 */

static int ircomm_cs = 0;
MODULE_PARM(ircomm_cs, "i");



static char *ircommstate[] = {
	"IDLE",

	"DISCOVERY_WAIT",
	"QUERYPARAM_WAIT",
	"QUERYLSAP_WAIT",

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

	"DISCOVERY_INDICATION",
	"GOT_PARAMETERS",
	"GOT_LSAPSEL",
	"QUERYIAS_ERROR",
};

#ifdef CONFIG_PROC_FS
extern struct proc_dir_entry *proc_irda;
#endif

static void (*state[])( struct ircomm_cb *self, IRCOMM_EVENT event,
			struct sk_buff *skb) = 
{
	ircomm_state_idle,

	ircomm_state_discoverywait,
	ircomm_state_queryparamwait,
	ircomm_state_querylsapwait,

	ircomm_state_waiti,
	ircomm_state_waitr,
	ircomm_state_conn,
};

__initfunc(int ircomm_init(void))
{
	int i;

	printk( "Linux-IrDA: IrCOMM protocol ( revision:%s ) \n",
		revision_date);
	DEBUG( 4, __FUNCTION__"()\n");

	/* allocate master array */

	ircomm = (struct ircomm_cb **) kmalloc( sizeof(struct ircomm_cb *) *
						IRCOMM_MAX_CONNECTION,
						GFP_KERNEL);
	if ( ircomm == NULL) {
		printk( KERN_ERR __FUNCTION__"(): kmalloc failed!\n");
		return -ENOMEM;
	}

	memset( ircomm, 0, sizeof(struct ircomm_cb *) * IRCOMM_MAX_CONNECTION);

	/* initialize structures */
	   
	for(i = 0;i < IRCOMM_MAX_CONNECTION; i++){
		ircomm[i] = kmalloc( sizeof(struct ircomm_cb), GFP_KERNEL );

		if(!ircomm[i]){
			printk( KERN_ERR __FUNCTION__"(): kmalloc failed!\n");
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
		ircomm[i]->max_sdu_size = SAR_DISABLE;  
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
	create_proc_entry("ircomm", 0, proc_irda)->get_info = ircomm_proc_read;
#endif /* CONFIG_PROC_FS */

	discovering_instance = NULL;
	return 0;
}

#ifdef MODULE
void ircomm_cleanup(void)
{
	int i;

	DEBUG( 4, "ircomm:cleanup_module\n");
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
	remove_proc_entry("ircomm", proc_irda);
#endif /* CONFIG_PROC_FS */
}
#endif /* MODULE */

/*
 * ---------------------------------------------------------------------- 
 * callbacks which accept incoming indication/confirm from IrTTP  (or IrLMP)
 * ---------------------------------------------------------------------- 
 */

static int ircomm_accept_data_indication(void *instance, void *sap, 
					 struct sk_buff *skb)
{
	struct ircomm_cb *self = (struct ircomm_cb *) instance;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == IRCOMM_MAGIC, return -1;);
	ASSERT(skb != NULL, return -1;);
       
	DEBUG(4,__FUNCTION__"():\n");
	ircomm_do_event(self, TTP_DATA_INDICATION, skb);
	self->rx_packets++;
	
	return 0;
}

static void ircomm_accept_connect_confirm(void *instance, void *sap,
					  struct qos_info *qos, 
					  __u32 max_sdu_size, 
					  __u8 max_header_size,
					  struct sk_buff *skb)
{
	struct ircomm_cb *self = (struct ircomm_cb *) instance;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRCOMM_MAGIC, return;);
	ASSERT(skb != NULL, return;);
	ASSERT(qos != NULL, return;);

	DEBUG(0,__FUNCTION__"(): got connected!\n");

	if (max_sdu_size == SAR_DISABLE)
		self->max_txbuff_size = qos->data_size.value - max_header_size;
	else {
		ASSERT(max_sdu_size >= COMM_DEFAULT_DATA_SIZE, return;);
		self->max_txbuff_size = max_sdu_size; /* use fragmentation */
	}

	self->qos = qos;
	self->max_header_size = max_header_size;
	self->null_modem_mode = 0;         /* disable null modem emulation */

	ircomm_do_event(self, TTP_CONNECT_CONFIRM, skb);
}

static void ircomm_accept_connect_indication(void *instance, void *sap,
					     struct qos_info *qos,
					     __u32 max_sdu_size,
					     __u8 max_header_size,
					     struct sk_buff *skb)
{
	struct ircomm_cb *self = (struct ircomm_cb *)instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	ASSERT( skb != NULL, return;);
	ASSERT( qos != NULL, return;);

	DEBUG(0,__FUNCTION__"()\n");

	if (max_sdu_size == SAR_DISABLE)
		self->max_txbuff_size = qos->data_size.value - max_header_size;
	else
		self->max_txbuff_size = max_sdu_size;

	self->qos = qos;
	self->max_header_size = max_header_size;

	ircomm_do_event( self, TTP_CONNECT_INDICATION, skb);

	/* stop connecting */
	wake_up_interruptible( &self->discovery_wait);
	wake_up_interruptible( &self->ias_wait);

}

static void ircomm_accept_disconnect_indication(void *instance, void *sap, 
					 LM_REASON reason,
					 struct sk_buff *skb)
{
	struct ircomm_cb *self = (struct ircomm_cb *)instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	DEBUG(0,__FUNCTION__"():\n");
	ircomm_do_event( self, TTP_DISCONNECT_INDICATION, skb);

}

static void ircomm_accept_flow_indication( void *instance, void *sap,
					   LOCAL_FLOW cmd)
{
	IRCOMM_CMD command;
	struct ircomm_cb *self = (struct ircomm_cb *)instance;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	switch(cmd){
	case FLOW_START:
		DEBUG(4,__FUNCTION__"():START\n"); 
		command = TX_READY;
		self->ttp_stop = 0;
		if(self->notify.flow_indication)
			self->notify.flow_indication( self->notify.instance, 
						      self, command);
		break;
		
	case FLOW_STOP:
		DEBUG(4,__FUNCTION__":STOP\n"); 
		command = TX_BUSY;
		self->ttp_stop = 1;
		if(self->notify.flow_indication)
			self->notify.flow_indication( self->notify.instance, 
						      self, command);
		break;

	default:
		DEBUG(0,__FUNCTION__"();unknown status!\n"); 
	}

}


/*
 * ircomm_discovery_indication()
 *    Remote device is discovered, try query the remote IAS to see which
 *    device it is, and which services it has.
 */

static void ircomm_discovery_indication(discovery_t *discovery)
{
	struct ircomm_cb *self;

	self = discovering_instance;
	if(self == NULL)
		return;
	ASSERT(self->magic == IRCOMM_MAGIC, return;);

	self->daddr = discovery->daddr;
	self->saddr = discovery->saddr;

	DEBUG( 0, __FUNCTION__"():daddr=%08x\n", self->daddr);

	ircomm_do_event(self, DISCOVERY_INDICATION, NULL);
	return;
}

/*
 * ircomm_getvalue_confirm()
 * handler for iriap_getvaluebyclass_request() 
 */
static void ircomm_getvalue_confirm(int result, __u16 obj_id, 
				    struct ias_value *value, void *priv)
{
	struct ircomm_cb *self = (struct ircomm_cb *) priv;
	struct sk_buff *skb= NULL;
	__u8 *frame;
	__u8 servicetype = 0 ;
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	/* Check if request succeeded */
	if (result != IAS_SUCCESS) {
		DEBUG( 0, __FUNCTION__ "(), got NULL value!\n");
		ircomm_do_event(self, QUERYIAS_ERROR, NULL);
		return;
	}

	DEBUG(4, __FUNCTION__"():type(%d)\n", value->type);

	self->ias_type = value->type;
	switch(value->type){
 	case IAS_OCT_SEQ:
		
		DEBUG(4, __FUNCTION__"():got octet sequence:\n");
#if 0
		{
			int i;
			for ( i=0;i<value->len;i++)
				printk("%02x",
				       (__u8)(*(value->t.oct_seq + i)));
			printk("\n");
		}
#endif
		skb = dev_alloc_skb((value->len) + 2);
		ASSERT(skb != NULL, ircomm_do_event(self, QUERYIAS_ERROR, NULL);return;);
		frame = skb_put(skb,2);
		/* MSB first */
		frame[0] = ( value->len >> 8 ) & 0xff;
		frame[1] = value->len & 0xff;
		
		frame = skb_put(skb,value->len);
		memcpy(frame, value->t.oct_seq, value->len);
		ircomm_parse_tuples(self, skb, IAS_PARAM);
		kfree_skb(skb);

		/* 
		 * check if servicetype we want is available 
		 */

		DEBUG(0,__FUNCTION__"():peer capability is:\n");
		DEBUG(0,"3wire raw: %s\n",
		      ((self->peer_servicetype & THREE_WIRE_RAW) ? "yes":"no"));
		DEBUG(0,"3wire    : %s\n",
		      ((self->peer_servicetype & THREE_WIRE) ? "yes":"no"));
		DEBUG(0,"9wire    : %s\n",
		      ((self->peer_servicetype & NINE_WIRE) ? "yes":"no"));
		DEBUG(0,"IEEE1284 : %s\n",
		      ((self->peer_servicetype & CENTRONICS) ? "yes":"no"));

		self->servicetype &= self->peer_servicetype;
		if(!(self->servicetype)){
			DEBUG(0,__FUNCTION__"(): servicetype mismatch!\n");
			ircomm_do_event(self, QUERYIAS_ERROR, NULL);
			break;
		}

		/*
		 * then choose better one 
		 */
		if(self->servicetype & THREE_WIRE_RAW)
			servicetype = THREE_WIRE_RAW;
		if(self->servicetype & THREE_WIRE)
			servicetype = THREE_WIRE;
		if(self->servicetype & NINE_WIRE)
			servicetype = NINE_WIRE;
		if(self->servicetype & CENTRONICS)
			servicetype = CENTRONICS;

		self->servicetype = servicetype;

		/* enter next state */
		ircomm_do_event(self, GOT_PARAMETERS, NULL);
		break;

	case IAS_INTEGER:
		/* LsapSel seems to be sent to me */	
		DEBUG(0, __FUNCTION__"():got lsapsel = %d\n", value->t.integer);

		if ( value->t.integer == -1){
			DEBUG( 0, __FUNCTION__"():invalid value!\n");
			ircomm_do_event(self, QUERYIAS_ERROR, NULL);
			return;
		}
		self->dlsap = value->t.integer;
		ircomm_do_event(self, GOT_LSAPSEL, NULL);
		break;

	case IAS_MISSING:
		DEBUG( 0, __FUNCTION__":got IAS_MISSING\n");
		ircomm_do_event(self, QUERYIAS_ERROR, NULL);
		break;
   
	default:
		DEBUG( 0, __FUNCTION__":got unknown (strange?)type!\n");
		ircomm_do_event(self, QUERYIAS_ERROR, NULL);
		break;
	}
}



/* 
 * ----------------------------------------------------------------------
 * Impl. of actions (descrived in section 7.4 of the reference)
 * ----------------------------------------------------------------------
 */

static void issue_connect_request(struct ircomm_cb *self,
				  struct sk_buff *userdata )
{
	/* TODO: we have to send/build userdata field which contains 
	   InitialControlParameters */

	switch(self->servicetype){
	case THREE_WIRE_RAW:
		DEBUG(0, __FUNCTION__"():THREE_WIRE_RAW is not implemented\n");
		break;

	case DEFAULT: 
	case THREE_WIRE:
	case NINE_WIRE:
	case CENTRONICS:

		irttp_connect_request(self->tsap, self->dlsap, 
				      self->saddr, self->daddr, 
				      NULL, self->max_sdu_size, userdata); 
		break;

	default:
		printk(KERN_ERR __FUNCTION__"():Illegal servicetype %d\n"
		       ,self->servicetype);
	}
}	

static void disconnect_indication(struct ircomm_cb *self, struct sk_buff *skb)
{

	/*
	 * Not implemented parameter"Reason".That is optional.
	 * What is reason? maybe discribed in irmod.h?
	 */

	if(self->notify.disconnect_indication)
		self->notify.disconnect_indication( self->notify.instance,
						    self,
						    self->reason, skb);
						    
}

static void connect_indication(struct ircomm_cb *self, struct qos_info *qos, 
			       struct sk_buff *skb)
{

/* If controlparameters don't exist, we use the servicetype"DEFAULT".*/
/* 	if( !ircomm_parse_controlchannel( self, data)) */
/* 		self->servicetype = DEFAULT;     TODOD:fix this! TH */

	if (self->notify.connect_indication)
		self->notify.connect_indication(self->notify.instance, self, 
						qos, 0, 0, skb);
}
    
#if 0
/*   it's for THREE_WIRE_RAW.*/
static void connect_indication_three_wire_raw(void)
{ 
	DEBUG(0,"ircomm:connect_indication_threewire():not implemented!");
}    
#endif 


static void connect_confirm(struct ircomm_cb *self, struct sk_buff *skb)
{
	DEBUG(4 ,__FUNCTION__"()\n");

	/* give a connect_confirm to the client */
	if( self->notify.connect_confirm )
		self->notify.connect_confirm(self->notify.instance,
					     self, NULL, SAR_DISABLE, 0, skb);
}

static void issue_connect_response(struct ircomm_cb *self,
				   struct sk_buff *skb)
{
	DEBUG(0,__FUNCTION__"()\n");
	
	if( self->servicetype == THREE_WIRE_RAW){
		DEBUG(0,__FUNCTION__"():THREE_WIRE_RAW is not implemented yet\n");
		/* irlmp_connect_rsp(); */
	} else
		irttp_connect_response(self->tsap, self->max_sdu_size, skb);
}

static void issue_disconnect_request(struct ircomm_cb *self,
				     struct sk_buff *userdata)
{
	if(self->servicetype == THREE_WIRE_RAW){
		DEBUG(0,__FUNCTION__"():3wireraw is not implemented\n");
	}
	else
		irttp_disconnect_request(self->tsap, userdata, 
					 self->disconnect_priority);
}
    
static void issue_data_request(struct ircomm_cb *self,
			       struct sk_buff *userdata )
{
	int err;

	if (self->servicetype == THREE_WIRE_RAW){
		/* irlmp_data_request(self->lmhandle,userdata); */
		DEBUG(0,__FUNCTION__"():not implemented!");
		return;
	}

	DEBUG(4,__FUNCTION__"():sending frame\n");
	err = irttp_data_request(self->tsap, userdata);
	if (err){
		printk(KERN_ERR __FUNCTION__":ttp_data_request failed\n");
		if (userdata)
			dev_kfree_skb( userdata);
	}
	self->tx_packets++;
}

static void issue_control_request(struct ircomm_cb *self,
				  struct sk_buff *userdata)
{
	int err;

	DEBUG(4,__FUNCTION__"()\n"); 
	if (self->servicetype == THREE_WIRE_RAW) {
		DEBUG(0,__FUNCTION__"():THREE_WIRE_RAW is not implemented\n");
		
	}
	else 
	{
		err = irttp_data_request(self->tsap,userdata);
		if(err)
		{
			printk( __FUNCTION__"():ttp_data_request failed\n");
			if(userdata)
				dev_kfree_skb(userdata);
		}
		else
			self->tx_controls++;

		self->pending_control_tuples = 0;
	}
}

static void process_data(struct ircomm_cb *self, struct sk_buff *skb )
{
	
	DEBUG(4,__FUNCTION__":skb->len=%d, ircomm header = 1, clen=%d\n",
	      (int)skb->len ,(int)skb->data[0]);

	/* we have to parse control channel when receiving.  (see
	 * page17 of IrCOMM standard) but it is not parsed here since
	 * upper layer may have some receive buffer.
	 *
	 * hence upper layer have to parse it when it consumes a packet.
	 *   -- TH
	 */

 	/* ircomm_parse_control(self, skb, CONTROL_CHANNEL); */

	if (self->notify.data_indication && skb->len)
		self->notify.data_indication(self->notify.instance, self,
					     skb);
}

int ircomm_data_indication(struct ircomm_cb *self, struct sk_buff *skb)
{
	/* Not implemented yet:THREE_WIRE_RAW service uses this function.  */
	DEBUG(0,"ircomm_data_indication:not implemented yet!\n");

	return 0;
}


/* 
 * ----------------------------------------------------------------------
 * Implementation of state chart,
 * descrived in section 7.1 of the specification.
 * ----------------------------------------------------------------------
 */

static void ircomm_do_event( struct ircomm_cb *self, IRCOMM_EVENT event,
			     struct sk_buff *skb) 
{
	
	DEBUG( 4, __FUNCTION__": STATE = %s, EVENT = %s\n",
	       ircommstate[self->state], ircommevent[event]);
	(*state[self->state])(self, event, skb);
}

static void ircomm_next_state( struct ircomm_cb *self, IRCOMM_STATE state) 
{
	self->state = state;
	DEBUG( 0, __FUNCTION__": NEXT STATE=%d(%s), servicetype=(%d)\n", 
	       (int)state, ircommstate[self->state],self->servicetype);
}



/*
 * ircomm_state_idle
 */

static void ircomm_state_idle( struct ircomm_cb *self, IRCOMM_EVENT event, 
			       struct sk_buff *skb )
{
	switch (event){
	case IRCOMM_CONNECT_REQUEST:

		/* ircomm_next_state(self, COMM_WAITI); */
		/* issue_connect_request( self, skb ); */

		ircomm_next_state(self, COMM_DISCOVERY_WAIT);
		start_discovering(self);
		break;
		
	case TTP_CONNECT_INDICATION:

		ircomm_next_state(self, COMM_WAITR);
		connect_indication( self, self->qos, skb);
		break;

	case LMP_CONNECT_INDICATION:

		DEBUG(0,__FUNCTION__"():LMP_CONNECT_IND is notimplemented!");
 		/* connect_indication_three_wire_raw(); */
 		/* ircomm_next_state(self, COMM_WAITR); */
		break;

	default:
		DEBUG(0,__FUNCTION__"():unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}

/*
 * ircomm_state_discoverywait
 */
static void ircomm_state_discoverywait(struct ircomm_cb *self, 
				       IRCOMM_EVENT event, 
				       struct sk_buff *skb )
{
	switch(event){

	case TTP_CONNECT_INDICATION:

		ircomm_next_state(self, COMM_WAITR);
		queryias_done(self);
		connect_indication( self, self->qos, skb);
		break;

	case DISCOVERY_INDICATION:
		ircomm_next_state(self, COMM_QUERYPARAM_WAIT);
		query_parameters(self);
		break;

	case IRCOMM_DISCONNECT_REQUEST:
		ircomm_next_state(self, COMM_IDLE);
		queryias_done(self);
		break;

	case QUERYIAS_ERROR:
		ircomm_next_state(self, COMM_IDLE);
		disconnect_indication(self, NULL);
		queryias_done(self);
		break;

	default:
		DEBUG(0,__FUNCTION__"():unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}

/*
 * ircomm_state_queryparamwait
 */

static void ircomm_state_queryparamwait(struct ircomm_cb *self, 
					IRCOMM_EVENT event, 
					struct sk_buff *skb)
{
	switch (event) {
	case TTP_CONNECT_INDICATION:

		ircomm_next_state(self, COMM_WAITR);
		connect_indication( self, self->qos, skb);
		break;

	case GOT_PARAMETERS:
		
		ircomm_next_state(self, COMM_QUERYLSAP_WAIT);
		query_lsapsel( self );
		break;

	case IRCOMM_DISCONNECT_REQUEST:
		ircomm_next_state(self, COMM_IDLE);
		queryias_done(self);
		break;

	case QUERYIAS_ERROR:
		ircomm_next_state(self, COMM_IDLE);
		disconnect_indication(self, NULL);
		queryias_done(self);
		break;

	default:
		DEBUG(0,__FUNCTION__"():unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}

/*
 * ircomm_state_querylsapwait
 */

static void ircomm_state_querylsapwait(struct ircomm_cb *self, 
				       IRCOMM_EVENT event, 
				       struct sk_buff *skb )
{
	switch (event) {

	case TTP_CONNECT_INDICATION:

		ircomm_next_state(self, COMM_WAITR);
		connect_indication( self, self->qos, skb);
		break;

	case GOT_LSAPSEL:
		
		ircomm_next_state(self, COMM_WAITI);
		queryias_done(self);
		issue_connect_request( self, skb );
		break;

	case IRCOMM_DISCONNECT_REQUEST:
		ircomm_next_state(self, COMM_IDLE);
		queryias_done(self);
		break;

	case QUERYIAS_ERROR:
		ircomm_next_state(self, COMM_IDLE);
		disconnect_indication(self, NULL);
		queryias_done(self);
		break;


	default:
		DEBUG(0,__FUNCTION__"():unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}

/*
 * ircomm_state_waiti
 */

static void ircomm_state_waiti(struct ircomm_cb *self, IRCOMM_EVENT event, 
			  struct sk_buff *skb )
{
	switch (event) {
	case TTP_CONNECT_CONFIRM:
		ircomm_next_state(self, COMM_CONN);
		connect_confirm(self, skb );
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
		DEBUG(0,__FUNCTION__"():unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}

/*
 * ircomm_state_waitr
 */
static void ircomm_state_waitr(struct ircomm_cb *self, IRCOMM_EVENT event, 
			       struct sk_buff *skb ) 
{
	switch (event) {
	case IRCOMM_CONNECT_RESPONSE:

	        /* issue_connect_response */
		
		if (self->servicetype==THREE_WIRE_RAW) {
			DEBUG(0,__FUNCTION__"():3WIRE_RAW is not implemented\n");
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
		queryias_done(self);
		break;

	case TTP_DISCONNECT_INDICATION:
		ircomm_next_state(self, COMM_IDLE);
		disconnect_indication(self, skb);
		break;

	case DISCOVERY_INDICATION:
		DEBUG(0, __FUNCTION__"():DISCOVERY_INDICATION\n");
		queryias_done(self);
		break;
	case GOT_PARAMETERS:
		DEBUG(0, __FUNCTION__"():GOT_PARAMETERS\n");
		queryias_done(self);
		break;
	case GOT_LSAPSEL:
		DEBUG(0, __FUNCTION__"():GOT_LSAPSEL\n");
		queryias_done(self);
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
			      struct sk_buff *skb )
{
	switch (event) {
	case TTP_DATA_INDICATION:
		process_data(self, skb);
		break;
	case IRCOMM_DATA_REQUEST:
		issue_data_request(self, skb);
		break;
/* 	case LMP_DATA_INDICATION: */
/* 		ircomm_data_indicated(); */
/* 		break; */
	case IRCOMM_CONTROL_REQUEST:
		issue_control_request(self, skb);
		break;
	case TTP_DISCONNECT_INDICATION:
		ircomm_next_state(self, COMM_IDLE);
		disconnect_indication(self, skb);
		break;
	case IRCOMM_DISCONNECT_REQUEST:
		ircomm_next_state(self, COMM_IDLE);
		issue_disconnect_request(self, skb);
		queryias_done(self);
		break;
/* 	case LM_DISCONNECT_INDICATION: */
/* 		disconnect_indication(); */
/* 		ircomm_next_state(self, COMM_IDLE); */
/* 		break; */

	case DISCOVERY_INDICATION:
		DEBUG(0, __FUNCTION__"():DISCOVERY_INDICATION\n");
		queryias_done(self);
		break;
	case GOT_PARAMETERS:
		DEBUG(0, __FUNCTION__"():GOT_PARAMETERS\n");
		queryias_done(self);
		break;
	case GOT_LSAPSEL:
		DEBUG(0, __FUNCTION__"():GOT_LSAPSEL\n");
		queryias_done(self);
		break;

	default:
		DEBUG(0,"ircomm_state_conn:unknown event =%d(%s)\n",
		      event, ircommevent[event]);
	}
}

/*
 *  ----------------------------------------------------------------------
 *  IrCOMM service interfaces and supporting functions
 *
 *  ----------------------------------------------------------------------
 */

/*
 * Function start_discovering (self)
 *
 *    Start discovering and enter DISCOVERY_WAIT state
 *
 */
static void start_discovering(struct ircomm_cb *self)
{
	__u16  hints; 
	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	DEBUG(4,__FUNCTION__"():servicetype = %d\n",self->servicetype);
	

	hints = irlmp_service_to_hint(S_COMM);

	DEBUG(0,__FUNCTION__"():start discovering..\n");
	switch (ircomm_cs) {
	case 0:
		MOD_INC_USE_COUNT;
		self->queryias_lock = 1;
		discovering_instance = self;
		self->skey = irlmp_register_service(hints);
		self->ckey = irlmp_register_client(hints, ircomm_discovery_indication,
					     NULL);
		break;
		
	case 1:    /* client only */
		MOD_INC_USE_COUNT;
		self->queryias_lock = 1;
		discovering_instance = self;
		DEBUG( 0, __FUNCTION__"():client only mode\n");
		self->ckey = irlmp_register_client(hints, ircomm_discovery_indication,
					     NULL);
		break;

	case 2:     /*  server only  */
	default:
		DEBUG( 0, __FUNCTION__"():server only mode\n");
		self->skey = irlmp_register_service(hints);
		discovering_instance = NULL;
		break;
	}
	
	return;
}

/*
 * queryias_done(self)
 *
 * 
 */

/*
 * Function queryias_done (self)
 *
 *    Called when discovery process got wrong results, completed, or
 *    terminated.
 * 
 */
static void queryias_done(struct ircomm_cb *self)
{
	DEBUG(0, __FUNCTION__"():\n");
	if (self->queryias_lock){
		self->queryias_lock = 0;
		discovering_instance = NULL;
		MOD_DEC_USE_COUNT;
		irlmp_unregister_client(self->ckey);
	}
	if (ircomm_cs != 1)
		irlmp_unregister_service(self->skey);
	return;
}



static void query_parameters(struct ircomm_cb *self)
{

	DEBUG(0, __FUNCTION__"():querying IAS: Parameters..\n");
	iriap_getvaluebyclass_request( "IrDA:IrCOMM", "Parameters",
				       self->saddr, self->daddr,
				       ircomm_getvalue_confirm, self );
}

static void query_lsapsel(struct ircomm_cb * self)
{
	DEBUG(0, __FUNCTION__"():querying IAS: Lsapsel...\n");

	if (!(self->servicetype & THREE_WIRE_RAW)) {
		iriap_getvaluebyclass_request( 
			"IrDA:IrCOMM", "IrDA:TinyTP:LsapSel",
			self->saddr, self->daddr,
			ircomm_getvalue_confirm, self );
	} else {
		DEBUG(0, __FUNCTION__ "THREE_WIRE_RAW is not implemented!\n");
	}
}

/*
 * Function ircomm_connect_request (self, servicetype)
 *
 *    Impl. of this function is differ from one of the reference. This
 *    function does discovery as well as sending connect request
 * 
 */
void ircomm_connect_request(struct ircomm_cb *self, __u8 servicetype)
{
	/*
	 * TODO:build a packet which contains "initial control parameters"
	 * and send it with connect_request
	 */

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);


	DEBUG(0, __FUNCTION__"():sending connect_request...\n");

	self->servicetype= servicetype;
	/* ircomm_control_request(self, SERVICETYPE); */ /*servictype*/

	self->max_sdu_size = SAR_DISABLE;
	ircomm_do_event(self, IRCOMM_CONNECT_REQUEST, NULL);
}

void ircomm_connect_response(struct ircomm_cb *self, struct sk_buff *userdata,
			     __u32 max_sdu_size)
{

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	/* ASSERT( userdata != NULL, return;); */

	DEBUG(4,"ircomm_connect_response:\n");

	/*
	 * TODO:build a packet which contains "initial control parameters"
	 * and send it with connect_response
	 */

	if (!userdata){
		/* FIXME: check for errors and initialize? DB */
		userdata = dev_alloc_skb(COMM_DEFAULT_DATA_SIZE);
		if (userdata == NULL)
			return;

		skb_reserve(userdata,COMM_HEADER_SIZE);
	}

	/* enable null-modem emulation (i.e. server mode )*/
	self->null_modem_mode = 1;

	self->max_sdu_size = max_sdu_size;
	if (max_sdu_size != SAR_DISABLE)
		self->max_txbuff_size = max_sdu_size;

	ircomm_do_event(self, IRCOMM_CONNECT_RESPONSE, userdata);
}	

void ircomm_disconnect_request(struct ircomm_cb *self, 
			       struct sk_buff *userdata,
			       int priority)
{

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);
	DEBUG(0,__FUNCTION__"()\n");

#if 0
	/* unregister layer */
	switch (ircomm_cs) {
	case 1:    /* client only */
		irlmp_unregister_client(ckey);
		break;

	case 2:     /*  server only  */
		irlmp_unregister_service(skey);
		break;
	case 0:
	default:
		irlmp_unregister_client(ckey);
		irlmp_unregister_service(skey);
		break;
	}
#endif

	self->disconnect_priority = priority;
	if(priority != P_HIGH)
		self->disconnect_priority = P_NORMAL;

	ircomm_do_event(self, IRCOMM_DISCONNECT_REQUEST, userdata);
}	


int ircomm_data_request(struct ircomm_cb *self, struct sk_buff *userdata)
{
	__u8 * frame;

	DEBUG(4,__FUNCTION__"()\n");
	ASSERT( self != NULL, return -EFAULT;);
	ASSERT( self->magic == IRCOMM_MAGIC, return -EFAULT;);
	ASSERT( userdata != NULL, return -EFAULT;);
	

	if(self->state != COMM_CONN){
		DEBUG(4,__FUNCTION__"():not connected, data is ignored\n");
		return -EINVAL;
	}

	if(self->ttp_stop)
		return -EBUSY;

	if(self->control_ch_pending){
		/* send control_channel */
		ircomm_tx_controlchannel(self);
	}

	if(self->ttp_stop)
		return -EBUSY;

	/* add "clen" field */
	frame = skb_push(userdata,1);
	frame[0]=0;          /* without control channel */

	ircomm_do_event(self, IRCOMM_DATA_REQUEST, userdata);
	return 0;
}

/*
 *  ----------------------------------------------------------------------
 *  IrCOMM_control.req and friends
 *
 *  ----------------------------------------------------------------------
 */


static void ircomm_tx_controlchannel(struct ircomm_cb *self )
{

	__u8 clen;
	struct sk_buff *skb = self->ctrl_skb;
	
	DEBUG(4,__FUNCTION__"()\n");
	/* 'self' should have been checked */
	ASSERT(!self->ttp_stop, return ;);
	ASSERT(self->state == COMM_CONN, return ;);

	/* add "clen" field */

	clen=skb->len;
	ASSERT(clen != 0,return;);

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
	self->control_ch_pending = 0;

	skb = dev_alloc_skb(COMM_DEFAULT_DATA_SIZE);
	ASSERT(skb != NULL, return ;);

	skb_reserve(skb,COMM_HEADER_SIZE);
	self->ctrl_skb = skb;
}


static void append_tuple(struct ircomm_cb *self, __u8 instruction, __u8 pl , 
			 __u8 *value)
{
	__u8 *frame;
	struct sk_buff *skb;
	int i,c = 0;
	unsigned long flags;

	save_flags(flags);cli();

	skb = self->ctrl_skb;
	ASSERT(skb != NULL, return;);
	
	if(skb_tailroom(skb) < (pl+2)){
		DEBUG(0, __FUNCTION__"there's no room.. ignore it\n");
		self->ignored_control_tuples++;
		restore_flags(flags);
		return;
	}

	frame = skb_put(skb,pl+2);
	frame[c++] = instruction;     /* PI */
	frame[c++] = pl;              /* PL */
	for(i=0; i < pl ; i++)
		frame[c++] = *value++;   /* PV */
	restore_flags(flags);	
	self->pending_control_tuples++;
	self->control_ch_pending = 1;
}

/*
 * Function ircomm_control_request (self, instruction)
 *
 *    This function is exported as a request to send some control-channel
 *    tuples * to peer device
 * 
 */
void ircomm_control_request(struct ircomm_cb *self,  __u8 instruction)
{

	__u8  pv[32];      /* 32 max, for PORT_NAME */
	__u8  *value = &pv[0];
	__u32 temp;
	int notsupp=0;

	ASSERT( self != NULL, return;);
	ASSERT( self->magic == IRCOMM_MAGIC, return;);

	if(self->servicetype == THREE_WIRE_RAW){
		DEBUG(0,__FUNCTION__"():THREE_WIRE_RAW shuold not use me!\n");
		return;
	}

	DEBUG(4,__FUNCTION__"()\n");

	/* find parameter and its length */

	if(self->servicetype == THREE_WIRE) goto threewire;
	if(self->servicetype == NINE_WIRE) goto ninewire;


	/* FIXME: centronics service is not fully implemented yet*/
	switch(instruction){
	case IEEE1284_MODE_SUPPORT:
	case IEEE1284_DEVICEID:
		append_tuple(self,instruction,0,NULL);
		break;
	case STATUS_QUERY:
		append_tuple(self,instruction,0,NULL);
		break;
	case SET_BUSY_TIMEOUT:
		value[0] = self->busy_timeout;
		append_tuple(self,instruction,1,value);
		break;
 	case IEEE1284_ECP_EPP_DATA_TRANSFER: 
		value[0] = self->ecp_epp_mode;
		value[1] = self->channel_or_addr;
		append_tuple(self,instruction,2,value); 
		break; 
	default:
		notsupp=1;
	}

 ninewire:
	switch(instruction){
	case POLL_FOR_LINE_SETTINGS:
		append_tuple(self,instruction,0,NULL);
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

	default:
		notsupp=1;
	}

 threewire:
	switch(instruction){

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
		if(temp < 32){
			value = (__u8) (self->port_name);
			append_tuple(self,instruction,temp,value);
		}else
			DEBUG(0,__FUNCTION__"() PORT_NAME:too long\n");
#endif
		break;

	default:
		if(notsupp)
			DEBUG(0,__FUNCTION__"():instruction(0x%02x)is not"
			      "implemented\n",instruction);
	}


}

void ircomm_parse_tuples(struct ircomm_cb *self, struct sk_buff *skb, int type)
{

	__u8 *data;
	__u8 pi,plen;
	int clen = 0;
	int indicate=0;

	ASSERT(skb != NULL, return;);
	ASSERT(self != NULL, return ;);
	ASSERT(self->magic == IRCOMM_MAGIC, return ;);


#ifdef IRCOMM_DEBUG_TUPLE
	DEBUG(0, __FUNCTION__"():tuple sequence is:\n");
	{
		int i;
		for ( i=0;i< skb->len;i++)
			printk("%02x", (__u8)(skb->data[i]));
		printk("\n");
	}
#endif

	data = skb->data;
	if(type == IAS_PARAM)
	{
		clen = (data[0] << 8) & 0xff00;
		clen |= data[1] & 0x00ff;
		ASSERT( clen <= (skb->len - 2) && clen <= 1024, goto corrupted;);
		DEBUG(4, __FUNCTION__"():IAS_PARAM len = %d\n",clen );
		skb_pull( skb, 2);
	}
	else
	{
		/* CONTROL_CHANNEL */
		clen = data[0];
		ASSERT( clen < skb->len, goto corrupted;);
		DEBUG(4, __FUNCTION__"():CONTROL_CHANNEL:len = %d\n",clen );
		skb_pull( skb, 1);
	}

	while( clen >= 2 ){
		data = skb->data;
		indicate = 0;

		/* 
		 * parse controlparameters and set value into structure 
		 */
		pi = data[0];
		plen = data[1];

		ASSERT( clen >= 2+plen, goto corrupted; );
		DEBUG(4, __FUNCTION__"():instruction=0x%02x,len=%d\n",
		      pi, plen) ;


		switch(pi)
		{
		case POLL_FOR_LINE_SETTINGS:
			ircomm_control_request(self, DTELINE_STATE);
			break;
		
		case SERVICETYPE:
			self->peer_servicetype = data[2];
			break;

		case PORT_TYPE:
			self->peer_port_type = data[2];
			break;

		case DATA_FORMAT:   
			self->peer_data_format = data[2];
			break;

		case FLOW_CONTROL:
			self->peer_flow_ctrl = data[2];
			indicate = 1;
			break;

		case LINESTATUS:
			self->peer_line_status = data[2];
			indicate = 1;
			break;

		case BREAK_SIGNAL:
			self->peer_break_signal = data[2];
			/* indicate = 1; */
			break;

		case DCELINE_STATE:
			self->peer_dce = data[2];
			indicate = 1;
			break;

		case DTELINE_STATE:
			if(self->null_modem_mode){
				/* input DTR as {DSR & CD & RI} */
				self->peer_dce = 0;
				if(data[2] & DELTA_DTR)
					self->peer_dce |= (DELTA_DSR|
							   DELTA_RI|
							   DELTA_DCD);
				if(data[2] & MCR_DTR)
					self->peer_dce |= (MSR_DSR|
							   MSR_RI|
							   MSR_DCD);
				/* rts as cts */
				if(data[2] & DELTA_RTS)
					self->peer_dce |= DELTA_CTS;
				if(data[2] & MCR_RTS)
					self->peer_dce |= MSR_CTS;
			}else{
				self->peer_dte = data[2];
			}
			indicate = 1;			
			break;
			
		case XON_XOFF_CHAR:
			self->peer_xon_char = data[2];
			self->peer_xoff_char = data[3];
			indicate = 1;
			break;

		case ENQ_ACK_CHAR:
			self->peer_enq_char = data[2];
			self->peer_ack_char = data[3];
			indicate = 1;
			break;

		case DATA_RATE:
			self->peer_data_rate = (  data[5]<<24
						  & data[4]<<16
						  & data[3]<<8
						  & data[2]);
			indicate = 1;
			break;

		case PORT_NAME:
			ASSERT(plen <= 32 , goto corrupted;);
			memcpy(self->port_name, data + 2, plen);
			*(__u8 *)(self->port_name+plen) = 0;
			break;

		case FIXED_PORT_NAME:
			ASSERT(plen <= 32 , goto corrupted;);
			memcpy(self->port_name, data + 2, plen);
			*(__u8 *)(self->port_name+plen) = 0;
			/* 
			 * We should not connect if user of IrCOMM can't
			 * recognize the port name
			 */
			self->port_name_critical = TRUE;
			break;

		default:
			DEBUG(0, __FUNCTION__
			      "():not implemented (PI=%d)\n", pi);
		}


		if(indicate && 
		   self->notify.flow_indication && type == CONTROL_CHANNEL)
		{
			DEBUG(4,__FUNCTION__":indicating..:\n");
			self->pi = pi;
			if(self->notify.flow_indication)
				self->notify.flow_indication(self->notify.instance,
							     self,
							     CONTROL_CHANNEL);
		}
		skb_pull(skb, 2+plen);
		clen -= (2+plen);
	}

	return;

 corrupted:
	skb_pull(skb, skb->len);   /* remove suspicious data */
	return;
}

/*
 * ----------------------------------------------------------------------
 * Function ircomm_open_instance() ,ircomm_close_instance() and friends
 *
 * ircomm_open_instance discoveres the peer device and then issues a
 * connect request
 * ----------------------------------------------------------------------
 */



struct ircomm_cb * ircomm_open_instance( struct notify_t client_notify)
{
	int i;
	struct ircomm_cb *self = NULL;
	struct notify_t notify;
	unsigned long flags;

	ASSERT(ircomm != NULL,return NULL;);
	DEBUG(0,__FUNCTION__"():\n");

	/* find free handle */

	save_flags(flags);
	cli();
	for(i = 0; i < IRCOMM_MAX_CONNECTION; i++){
		ASSERT(ircomm[i] != NULL,return(NULL););
		if(!ircomm[i]->in_use){
			self = ircomm[i];
			break;
		}
	}

	if (!self){
		DEBUG(0,__FUNCTION__"():no free handle!\n");
		return (NULL);
	}

	self->in_use = 1;
	restore_flags(flags);

	self->notify = client_notify;
	self->ttp_stop = 0;
	self->control_ch_pending = 0;

	/* register callbacks */

	irda_notify_init(&notify);
	notify.data_indication = ircomm_accept_data_indication;
	notify.connect_confirm = ircomm_accept_connect_confirm;
	notify.connect_indication = ircomm_accept_connect_indication;
	notify.flow_indication = ircomm_accept_flow_indication;
	notify.disconnect_indication = ircomm_accept_disconnect_indication;
	notify.instance = self;
	strncpy( notify.name, "IrCOMM", NOTIFY_MAX_NAME);

	self->tsap = irttp_open_tsap(LSAP_ANY, DEFAULT_INITIAL_CREDIT,
				     &notify);
	if(!self->tsap){
		DEBUG(0,__FUNCTION__"failed to allocate tsap\n");
		return NULL;
	}

	ircomm_next_state(self, COMM_IDLE);	
	return (self);
}

int ircomm_close_instance(struct ircomm_cb *self)
{
	ASSERT( self != NULL, return -EIO;);
	ASSERT( self->magic == IRCOMM_MAGIC, return -EIO;);
	ASSERT( self->ctrl_skb != NULL, return -EIO;);

	DEBUG(0,__FUNCTION__"()\n");

	/* shutdown ircomm layer */
	if(self->state != COMM_IDLE && self->state != COMM_WAITI)
	{
		DEBUG(0,__FUNCTION__"():force disconnecting..\n");
		ircomm_disconnect_request(self, NULL, P_HIGH);
	}

	skb_trim(self->ctrl_skb,0);
	/* remove a tsap */
	if(self->tsap)
		irttp_close_tsap(self->tsap);
	self->tsap = NULL;
	self->in_use = 0;
	return 0;
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
int init_module(void) 
{
	int err;

	err = ircomm_init();

	DEBUG( 4, __FUNCTION__"():done.\n");
	return err;
}
	
void cleanup_module(void)
{
	ircomm_cleanup();
	DEBUG( 4, __FUNCTION__"():done.\n");
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
		     int len, int unused)
{
	int i, index;

	len = 0;
	for (i=0; i<IRCOMM_MAX_CONNECTION; i++) {

		len += sprintf(buf+len, "instance %d:\n",i);
		if(ircomm[i]->in_use == 0){
			len += sprintf(buf+len, "\tunused\n");
			continue;
		}

		if (ircomm[i] == NULL || ircomm[i]->magic != IRCOMM_MAGIC) {
			len += sprintf(buf+len, "\tbroken???\n");
			continue;
		}

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
		len += sprintf(buf+len, "    service: %s  ",
			       ircommservicetype[index]);
		if(!index)
			continue;

		len += sprintf(buf+len, "porttype: %s  ",
			       ircommporttype[ircomm[i]->port_type]);
		len += sprintf(buf+len, "state: %s  ",
			       ircommstate[ircomm[i]->state]);
		len += sprintf(buf+len, "user: %s\n",
			       ircomm[i]->notify.name);
		
		len += sprintf(buf+len, "    tx packets: %d  ",
			       ircomm[i]->tx_packets);
		len += sprintf(buf+len, "rx packets: %d  ",
			       ircomm[i]->rx_packets);
		len += sprintf(buf+len, "tx controls: %d\n",
			       ircomm[i]->tx_controls);
		
		len += sprintf(buf+len, "    pending tuples: %d  ",
			       ircomm[i]->pending_control_tuples);
		len += sprintf(buf+len, "    ignored tuples: %d\n",
			       ircomm[i]->ignored_control_tuples);
		
		len += sprintf(buf+len, "    nullmodem emulation: %s  ",
			       (ircomm[i]->null_modem_mode ? "yes":"no"));
		len += sprintf(buf+len, "IrTTP: %s\n",
			       (ircomm[i]->ttp_stop ? "BUSY":"READY"));

		len += sprintf(buf+len, "    Peer capability: ");
		if(ircomm[i]->peer_cap & THREE_WIRE_RAW)
			len += sprintf(buf+len, "3wire-raw ");
		if(ircomm[i]->peer_cap & THREE_WIRE)
			len += sprintf(buf+len, "3wire ");
		if(ircomm[i]->peer_cap & NINE_WIRE)
			len += sprintf(buf+len, "9wire ");
		if(ircomm[i]->peer_cap & CENTRONICS)
			len += sprintf(buf+len, "centronics");

		len += sprintf(buf+len, "\n    Port name: %s\n",
			       (ircomm[i]->port_name));
	}

	return len;
}

#endif /* CONFIG_PROC_FS */




