/*********************************************************************
 *                
 * Filename:      irvtd_driver.c
 * Version:       
 * Description:   Virtual tty driver (the "port emulation entity" of IrCOMM)
 * Status:        Experimental.
 * Author:        Takahide Higuchi <thiguchi@pluto.dti.ne.jp>
 * Source:        serial.c by Linus Torvalds
 *                isdn_tty.c by Fritz Elfert
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

#include <linux/module.h>
#include <linux/init.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>
#include <net/irda/irias_object.h>

#include <net/irda/irvtd.h>

#ifndef MIN
#define MIN(a,b)        ((a) < (b) ? (a) : (b))
#endif

#define DO_RESTART
#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

struct tty_driver irvtd_drv;
struct tty_struct *irvtd_table[COMM_MAX_TTY];
struct termios *irvtd_termios[COMM_MAX_TTY];
struct termios *irvtd_termios_locked[COMM_MAX_TTY];
static int irvtd_refcount;
struct irvtd_cb **irvtd = NULL;

static char *revision_date = "Wed Mar 10 15:33:03 1999";


/*
 * prototypes
 */

int irvtd_open(struct tty_struct *tty, struct file *filp);
void irvtd_close(struct tty_struct * tty, struct file * filp);
int irvtd_write(struct tty_struct * tty, int from_user,
			const unsigned char *buf, int count);
void irvtd_put_char(struct tty_struct *tty, unsigned char ch);
int irvtd_write_room(struct tty_struct *tty);
int irvtd_chars_in_buffer(struct tty_struct *tty);
int irvtd_ioctl(struct tty_struct *tty, struct file * file,
	     unsigned int cmd, unsigned long arg);
void irvtd_set_termios(struct tty_struct *tty, struct termios * old);
void irvtd_throttle(struct tty_struct *tty);
void irvtd_unthrottle(struct tty_struct *tty);
void irvtd_stop(struct tty_struct *tty);
void irvtd_start(struct tty_struct *tty);
void irvtd_hangup(struct tty_struct *tty);
void irvtd_flush_buffer(struct tty_struct *tty);

static void change_speed(struct irvtd_cb *driver);
static void irvtd_write_to_tty( struct irvtd_cb *);
static void irvtd_send_data_request( struct irvtd_cb *);
static void irvtd_break(struct tty_struct *tty, int break_state);
static void irvtd_send_xchar(struct tty_struct *tty, char ch);
static void irvtd_wait_until_sent(struct tty_struct *tty, int timeout);

static void irvtd_start_timer( struct irvtd_cb *driver);
static void irvtd_timer_expired(unsigned long data);

static int line_info(char *buf, struct irvtd_cb *driver);
static int irvtd_read_proc(char *buf, char **start, off_t offset, int len,
			   int *eof, void *unused);


/*
 * ----------------------------------------------------------------------
 * 
 *

 * ----------------------------------------------------------------------
 */

/*
 **********************************************************************
 *
 * ircomm_receive_data() and friends
 *
 * like interrupt handler in the serial.c,we receive data when 
 * ircomm_data_indication comes
 *
 **********************************************************************
 */



/* 
 * irvtd_write_to_tty
 * send incoming/queued data to tty
 */

static void irvtd_write_to_tty( struct irvtd_cb *driver)
{	
	int status, c, flag;
	struct sk_buff *skb;
	struct tty_struct *tty = driver->tty;
	
	skb = skb_dequeue(&driver->rxbuff);
	if(skb == NULL)
		return; /* there's nothing */


	/* 
	 * we should parse controlchannel field here. 
	 * (see process_data() in ircomm.c)
	 */
	ircomm_parse_tuples(driver->comm, skb, CONTROL_CHANNEL);
	
#ifdef IRVTD_DEBUG_RX
	printk("received data:");
	{
		int i;
		for ( i=0;i<skb->len;i++)
			printk("%02x ", skb->data[i]);
		printk("\n");
	}
#endif
	
	status = driver->comm->peer_line_status & driver->read_status_mask;
	
	/* 
	 * if there are too many errors which make a character ignored,
	 * drop characters
	 */

	if(status & driver->ignore_status_mask){
		DEBUG(0,__FUNCTION__":some error:ignore characters.\n");
		dev_kfree_skb(skb);
		return;
	}
	
	c = MIN(skb->len, (TTY_FLIPBUF_SIZE - tty->flip.count));
	DEBUG(4, __FUNCTION__"skb_len=%d, tty->flip.count=%d \n"
	      ,(int)skb->len, tty->flip.count);
	
	if (driver->comm->peer_break_signal ) {
		driver->comm->peer_break_signal = 0;
		DEBUG(0,"handling break....\n");
		
		flag = TTY_BREAK;
		if (driver->flags & ASYNC_SAK)
			/*
			 * do_SAK() seems to be an implementation of the 
			 * idea called "Secure Attention Key",
			 * which seems to be discribed in "Orange book".
			 * (which is published by U.S.military!!?? ,
			 * see source of do_SAK())
			 *
			 * but what kind of security do we need 
			 * when we use infrared communication??? :p)
			 */
			do_SAK(tty);
	}else if (status & LSR_PE)
		flag = TTY_PARITY;
	else if (status & LSR_FE)
		flag = TTY_FRAME;
	else if (status & LSR_OE)
		flag = TTY_OVERRUN;
	else 
		flag = TTY_NORMAL;
	
	if(c){
		DEBUG(4,"writing %d chars to tty\n",c);
		driver->icount.rx += c;
		memset(tty->flip.flag_buf_ptr, flag, c);
		memcpy(tty->flip.char_buf_ptr, skb->data, c);
		tty->flip.flag_buf_ptr += c;
		tty->flip.char_buf_ptr += c;
		tty->flip.count += c;
		skb_pull(skb,c);
	}

	if(skb->len == 0)
		dev_kfree_skb(skb);
	else
	{
		/* queue rest of data again */
		DEBUG(4,__FUNCTION__":retrying frame!\n");

		/* build a dummy control channel */
		skb_push(skb,1);
		*skb->data = 0;  /* clen is 0 */
		skb_queue_head( &driver->rxbuff, skb );
	}
	
	if(c)
	/* let the process read its buffer! */
		tty_flip_buffer_push(tty);
	
	if(skb_queue_len(&driver->rxbuff)< IRVTD_RX_QUEUE_LOW &&
	   driver->ttp_stoprx){
		irttp_flow_request(driver->comm->tsap, FLOW_START);
		driver->ttp_stoprx = 0;
	}

	if(skb_queue_empty(&driver->rxbuff) && driver->disconnect_pend){
		/* disconnect */
		driver->disconnect_pend = 0;
		driver->rx_disable = 1;
		tty_hangup(driver->tty);
	}
}

static int irvtd_receive_data(void *instance, void *sap, struct sk_buff *skb)
{	
	struct irvtd_cb *driver = (struct irvtd_cb *)instance;

	ASSERT(driver != NULL, return -1;);
	ASSERT(driver->magic == IRVTD_MAGIC, return -1;);
	DEBUG(4, __FUNCTION__"(): queue frame\n");

	/* queue incoming data and make bottom half handler ready */

	skb_queue_tail( &driver->rxbuff, skb );

	if(skb_queue_len(&driver->rxbuff) > IRVTD_RX_QUEUE_HIGH){
		irttp_flow_request(driver->comm->tsap, FLOW_STOP);
		driver->ttp_stoprx = 1;
	}
	return 0;
}

/*
 ***********************************************************************
 *
 * irvtd_send_data() and friends
 *
 * like interrupt handler in the serial.c,we send data when 
 * a timer is expired
 *
 ***********************************************************************
 */


static void irvtd_start_timer( struct irvtd_cb *driver)
{
	ASSERT( driver != NULL, return;);
	ASSERT( driver->magic == IRVTD_MAGIC, return;);

	del_timer( &driver->timer);
	
	driver->timer.data     = (unsigned long) driver;
	driver->timer.function = &irvtd_timer_expired;
	driver->timer.expires  = jiffies + (HZ / 20);  /* 50msec */
	
	add_timer( &driver->timer);
}


static void irvtd_timer_expired(unsigned long data)
{
	struct irvtd_cb *driver = (struct irvtd_cb *)data;

	ASSERT(driver != NULL,return;);
	ASSERT(driver->magic == IRVTD_MAGIC,return;);
	DEBUG(4, __FUNCTION__"()\n");

	if(!(driver->tty->hw_stopped) && !(driver->tx_disable))
		irvtd_send_data_request(driver);

	if(!(driver->rx_disable)){
		irvtd_write_to_tty(driver);
	}
	
	/* start our timer again and again */
	irvtd_start_timer(driver);
}


static void irvtd_send_data_request(struct irvtd_cb *driver)
{
	int err;
	struct sk_buff *skb = driver->txbuff;

	ASSERT(skb != NULL,return;);
	DEBUG(4, __FUNCTION__"()\n");

	if(!skb->len)
		return;   /* no data to send */

#ifdef IRVTD_DEBUG_TX
	DEBUG(4, "flush_txbuff:count(%d)\n",(int)skb->len);
	{
		int i;
		for ( i=0;i<skb->len;i++)
			printk("%02x", skb->data[i]);
		printk("\n");
	}
#endif

	DEBUG(4, __FUNCTION__"():sending %d bytes\n",(int)skb->len );
	driver->icount.tx += skb->len;
	err = ircomm_data_request(driver->comm, driver->txbuff);
	if (err){
		ASSERT(err == 0,;);
		DEBUG(0,"%d chars are lost\n",(int)skb->len);
		skb_trim(skb, 0);
	}

	/* allocate a new frame */
	skb = driver->txbuff = dev_alloc_skb(driver->comm->max_txbuff_size);
	if (skb == NULL){
		printk(__FUNCTION__"():flush_txbuff():alloc_skb failed!\n");
	} else {
		skb_reserve(skb, COMM_HEADER_SIZE);
	}

	wake_up_interruptible(&driver->tty->write_wait);
}


/*
 ***********************************************************************
 *
 * indication/confirmation handlers:
 *
 * these routines are handlers for IrCOMM protocol stack
 *
 ***********************************************************************
 */


/*
 * Function irvtd_connect_confirm (instance, sap, qos, max_sdu_size, skb)
 *
 *    ircomm_connect_request which we have send have succeed!
 *
 */
void irvtd_connect_confirm(void *instance, void *sap, struct qos_info *qos,
			   __u32 max_sdu_size, struct sk_buff *skb)
{
	struct irvtd_cb *driver = (struct irvtd_cb *)instance;
	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	/*
	 * set default value
	 */
	
	driver->msr |= (MSR_DCD|MSR_RI|MSR_DSR|MSR_CTS);

	/*
	 * sending initial control parameters here
	 */
#if 1
	if(driver->comm->servicetype ==	THREE_WIRE_RAW)
		return;                /* do nothing */

	ircomm_control_request(driver->comm, SERVICETYPE);
	ircomm_control_request(driver->comm, DATA_RATE);
	ircomm_control_request(driver->comm, DATA_FORMAT);
	ircomm_control_request(driver->comm, FLOW_CONTROL);
	ircomm_control_request(driver->comm, XON_XOFF_CHAR);
	/* ircomm_control_request(driver->comm, ENQ_ACK_CHAR); */

	switch(driver->comm->servicetype){
	case CENTRONICS:
		break;

	case NINE_WIRE:
		ircomm_control_request(driver->comm, DTELINE_STATE);
		break;
	default:
	}
#endif
	
	driver->tx_disable = 0;
	wake_up_interruptible(&driver->open_wait);
}

/*
 * Function irvtd_connect_indication (instance, sap, qos, max_sdu_size, skb)
 *
 *    we are discovered and being requested to connect by remote device !
 *
 */
void irvtd_connect_indication(void *instance, void *sap, struct qos_info *qos,
			      __u32 max_sdu_size, struct sk_buff *skb)
{

	struct irvtd_cb *driver = (struct irvtd_cb *)instance;
	struct ircomm_cb *comm = (struct ircomm_cb *)sap;
	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);
	ASSERT(comm != NULL, return;);
	ASSERT(comm->magic == IRCOMM_MAGIC, return;);

	DEBUG(4,"irvtd_connect_indication:sending connect_response...\n");

	/*TODO: connect_response should send initialcontrolparameters! TH*/

	ircomm_connect_response(comm, NULL, SAR_DISABLE );

	/*
	 * set default value
	 */
	driver->msr |= (MSR_DCD|MSR_RI|MSR_DSR|MSR_CTS);
	driver->tx_disable = 0;
	wake_up_interruptible(&driver->open_wait);
}

/*
 * Function irvtd_disconnect_indication (instance, sap, reason, skb)
 *
 *    This is a handler for ircomm_disconnect_indication. since this
 *    function is called in the context of interrupt handler,
 *    interruptible_sleep_on() MUST not be used.
 */
void irvtd_disconnect_indication(void *instance, void *sap , LM_REASON reason,
				 struct sk_buff *skb)
{
	struct irvtd_cb *driver = (struct irvtd_cb *)instance;

	ASSERT(driver != NULL, return;);
	ASSERT(driver->tty != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	DEBUG(4,"irvtd_disconnect_indication:\n");

	driver->tx_disable = 1;
	driver->disconnect_pend = 1;
}

/*
 * Function irvtd_control_indication (instance, sap, cmd)
 *
 *    
 *
 */
void irvtd_control_indication(void *instance, void *sap, IRCOMM_CMD cmd)
{
	struct irvtd_cb *driver = (struct irvtd_cb *)instance;

	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	DEBUG(4,__FUNCTION__"()\n");

	if(cmd == TX_READY){
		driver->ttp_stoptx = 0;
		driver->tty->hw_stopped = driver->cts_stoptx;
		irvtd_start_timer( driver);

		if(driver->cts_stoptx)
			return;

		/* 
		 * driver->tty->write_wait will keep asleep if
		 * our txbuff is full.
		 * now that we can send packets to IrTTP layer,
		 * we kick it here.
		 */
		if ((driver->tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && 
		    driver->tty->ldisc.write_wakeup)
			(driver->tty->ldisc.write_wakeup)(driver->tty);
		return;
	}

	if(cmd == TX_BUSY){
		driver->ttp_stoptx = driver->tty->hw_stopped = 1;
		del_timer( &driver->timer);
		return;
	}


	ASSERT(cmd == CONTROL_CHANNEL,return;);

	switch(driver->comm->pi){

	case DCELINE_STATE:
	driver->msr = driver->comm->peer_dce;

	if(driver->msr & (DELTA_CTS|DELTA_DSR|DELTA_RI|DELTA_DCD)){
		if(driver->msr & DELTA_CTS)
			driver->icount.cts++;
		if(driver->msr & DELTA_DSR)
			driver->icount.dsr++;
		if(driver->msr & DELTA_RI)
			driver->icount.rng++;
		if(driver->msr & DELTA_DCD)
			driver->icount.dcd++;
		wake_up_interruptible(&driver->delta_msr_wait);
	}

	if ((driver->flags & ASYNC_CHECK_CD) && (driver->msr & DELTA_DCD)) {

		DEBUG(0,"CD now %s...\n",
		      (driver->msr & MSR_DCD) ? "on" : "off");

		if (driver->msr & MSR_DCD)
		{
			/* DCD raised! */
			wake_up_interruptible(&driver->open_wait);
		}
		else
		{
			/* DCD falled */
                        DEBUG(0,__FUNCTION__"():hangup..\n");
			tty_hangup(driver->tty);
		}

	}

	if (driver->comm->flow_ctrl & USE_CTS) {
		if (driver->tty->hw_stopped) {
			if (driver->msr & MSR_CTS) {
				DEBUG(0,"CTS tx start...\n");

				driver->cts_stoptx = 0;
				driver->tty->hw_stopped = driver->ttp_stoptx;
				/*  
				 * replacement of 
				 * rs_sched_event(info, RS_EVENT_WRITE_WAKEUP)
				 * in serial.c
				 */

				if ((driver->tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
				    driver->tty->ldisc.write_wakeup)
					(driver->tty->ldisc.write_wakeup)(driver->tty);

				wake_up_interruptible(&driver->tty->write_wait);
				return;
			}
		} else {
			if (!(driver->msr & MSR_CTS)) {
				DEBUG(0,"CTS tx stop...");

				driver->cts_stoptx = 1;
				driver->tty->hw_stopped = 1;
                        }
                }
        }
	break;

	default:
		DEBUG(0,__FUNCTION__"():PI = 0x%02x is not implemented\n",
		      (int)driver->comm->pi);
	}
}

/*
 ***********************************************************************
 *
 * driver kernel interfaces
 * these functions work as an interface between the kernel and this driver
 *
 ***********************************************************************
 */



/*
 * ----------------------------------------------------------------------
 * irvtd_open() and friends
 *
 * ----------------------------------------------------------------------
 */


static int irvtd_block_til_ready(struct tty_struct *tty, struct file * filp,
				 struct irvtd_cb *driver)
{

 	struct wait_queue wait = { current, NULL };
	int		retval = 0;
	int		do_clocal = 0;

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */

	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {

		return 0;
	}

	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;


	/*
	 * We wait until ircomm_connect_request() succeed or
	 *   ircomm_connect_indication comes
	 */


	add_wait_queue(&driver->open_wait, &wait);

	DEBUG(0,__FUNCTION__"():before block( line = %d, count = %d )\n",
	      driver->line, driver->count);

	driver->blocked_open++;

	/* wait for a connection established */
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		
		if (driver->comm->state == COMM_CONN){
			/* 
			 * signal DTR and RTS
			 */
			driver->comm->dte = driver->mcr |= (MCR_DTR  |
							    MCR_RTS  |
							    DELTA_DTR|
							    DELTA_RTS );

			ircomm_control_request(driver->comm, DTELINE_STATE);
		}


		if (tty_hung_up_p(filp) ||
		    !(driver->flags & ASYNC_INITIALIZED)) {
#ifdef DO_RESTART
			if (driver->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}

		if (!(driver->flags & ASYNC_CLOSING) &&
		    (driver->comm->state == COMM_CONN) && 
		    ( do_clocal || (driver->msr & MSR_DCD)))
			break;

		if(signal_pending(current)){
			retval = -ERESTARTSYS;
			break;
		}


		DEBUG(4,__FUNCTION__"():blocking( %s%d, count = %d )\n",
		      tty->driver.name, driver->line, driver->count);

		schedule();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&driver->open_wait, &wait);

	driver->blocked_open--;

	DEBUG(0, __FUNCTION__"():after blocking\n");

	if (retval)
		return retval;
	return 0;
}	

static void change_speed(struct irvtd_cb *driver)
{
	unsigned cflag,cval;

	if (!driver->tty || !driver->tty->termios || !driver->comm)
		return;
	cflag = driver->tty->termios->c_cflag;

	/* 
	 * byte size and parity
	 */
	switch (cflag & CSIZE) 
	{
	case CS5: cval = IRCOMM_WLEN5; break;
	case CS6: cval = IRCOMM_WLEN6; break;
	case CS7: cval = IRCOMM_WLEN7; break;
	case CS8: cval = IRCOMM_WLEN8; break;
	default:  cval = IRCOMM_WLEN5; break;	/* too keep GCC shut... */
	}
	if (cflag & CSTOPB) {      /* use 2 stop bit mode */
		cval |= IRCOMM_STOP2;
	}
	if (cflag & PARENB)
		cval |= IRCOMM_PARENB;    /* enable parity check */
	if (!(cflag & PARODD))
		cval |= IRCOMM_PAREVEN;         /* even parity */
	
	/* CTS flow control flag and modem status interrupts */

	if (cflag & CRTSCTS)
		driver->comm->flow_ctrl |= USE_CTS;
	else
		driver->comm->flow_ctrl |= ~USE_CTS;
	
	if (cflag & CLOCAL)
		driver->flags &= ~ASYNC_CHECK_CD;
	else
		driver->flags |= ASYNC_CHECK_CD;
	
	/*
	 * Set up parity check flag
	 */

	driver->read_status_mask = LSR_OE;
	if (I_INPCK(driver->tty))
		driver->read_status_mask |= LSR_FE | LSR_PE;
	if (I_BRKINT(driver->tty) || I_PARMRK(driver->tty))
		driver->read_status_mask |= LSR_BI;
	
	/*
	 * Characters to ignore
	 */
	driver->ignore_status_mask = 0;
	if (I_IGNPAR(driver->tty))
		driver->ignore_status_mask |= LSR_PE | LSR_FE;

	if (I_IGNBRK(driver->tty)) {
		driver->ignore_status_mask |= LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore 
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(driver->tty)) 
			driver->ignore_status_mask |= LSR_OE;
	}

	driver->comm->data_format = cval;
	ircomm_control_request(driver->comm, DATA_FORMAT);
 	ircomm_control_request(driver->comm, FLOW_CONTROL);
}




static int irvtd_startup(struct irvtd_cb *driver)
{
	int retval = 0;
	struct ias_object* obj;
	struct notify_t irvtd_notify;

	/* FIXME: it should not be hard coded */
	__u8 oct_seq[6] = { 0,1,4,1,1,1 }; 

	DEBUG(4,__FUNCTION__"()\n" );
	if(driver->flags & ASYNC_INITIALIZED)
		return 0;

	/*
	 * initialize our tx/rx buffer
	 */

	skb_queue_head_init(&driver->rxbuff);
	driver->txbuff = dev_alloc_skb(COMM_DEFAULT_DATA_SIZE); 
	if (!driver->txbuff){
		DEBUG(0,__FUNCTION__"():alloc_skb failed!\n");
		return -ENOMEM;
	}
	skb_reserve(driver->txbuff, COMM_HEADER_SIZE);

	irda_notify_init(&irvtd_notify);
	irvtd_notify.data_indication = irvtd_receive_data;
	irvtd_notify.connect_confirm = irvtd_connect_confirm;
	irvtd_notify.connect_indication = irvtd_connect_indication;
	irvtd_notify.disconnect_indication = irvtd_disconnect_indication;
	irvtd_notify.flow_indication = irvtd_control_indication;
	strncpy( irvtd_notify.name, "ircomm_tty", NOTIFY_MAX_NAME);
	irvtd_notify.instance = driver;

	driver->comm = ircomm_open_instance(irvtd_notify);
	if(!driver->comm){
		return -ENODEV;
	}


	/* 
         *  Register with LM-IAS as a server
         */

	obj = irias_new_object( "IrDA:IrCOMM", IAS_IRCOMM_ID);
	irias_add_integer_attrib( obj, "IrDA:TinyTP:LsapSel", 
				  driver->comm->tsap->stsap_sel );

	irias_add_octseq_attrib( obj, "Parameters", &oct_seq[0], 6);
	irias_insert_object( obj);

	driver->flags |= ASYNC_INITIALIZED;
	/*
	 * discover a peer device
	 *	   TODO: other servicetype(i.e. 3wire,3wireraw) support
	 */
	retval = ircomm_query_ias_and_connect(driver->comm, NINE_WIRE);
	if(retval){
		DEBUG(0, __FUNCTION__"(): ircomm_query_ias returns %d\n",
		      retval);
		return retval;
	}

	/*
	 * TODO:we have to initialize control-channel here!
	 *   i.e.set something into RTS,CTS and so on....
	 */

	if (driver->tty)
		clear_bit(TTY_IO_ERROR, &driver->tty->flags);

	change_speed(driver);
	irvtd_start_timer( driver);

	driver->rx_disable = 0;
	driver->tx_disable = 1;
	driver->disconnect_pend = 0;
	return 0;
}


int irvtd_open(struct tty_struct * tty, struct file * filp)
{
	struct irvtd_cb *driver;
	int retval;
	int line;

	DEBUG(4, __FUNCTION__"():\n");
	MOD_INC_USE_COUNT;

	line = MINOR(tty->device) - tty->driver.minor_start;
	if ((line <0) || (line >= COMM_MAX_TTY)){
		MOD_DEC_USE_COUNT;
		return -ENODEV;
	}

	driver = irvtd[line];
	ASSERT(driver != NULL, MOD_DEC_USE_COUNT;return -ENOMEM;);
	ASSERT(driver->magic == IRVTD_MAGIC, MOD_DEC_USE_COUNT;return -EINVAL;);

	driver->count++;

	DEBUG(0, __FUNCTION__"():%s%d count %d\n", tty->driver.name, line, 
	      driver->count);
	
	tty->driver_data = driver;
	driver->tty = tty;

	driver->tty->low_latency = (driver->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * (sleep) until it's done, then exit.
	 */

	if (tty_hung_up_p(filp) ||
	    (driver->flags & ASYNC_CLOSING)) {
		if (driver->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&driver->close_wait);
#ifdef DO_RESTART
		if (driver->flags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}

	/* 
	 * start up discovering process and ircomm_layer 
	 */
	
	retval = irvtd_startup(driver);
	if (retval){
		DEBUG(0, __FUNCTION__"():irvtd_startup returns %d\n",retval);
		return retval;
	}

	retval = irvtd_block_til_ready(tty, filp, driver);
	if (retval){
		DEBUG(0,__FUNCTION__
		      "():returning after block_til_ready (errno = %d)\n", retval);
                return retval;
	}

	driver->session = current->session;
	driver->pgrp = current->pgrp;
	return 0;
}





/*
 * ----------------------------------------------------------------------
 * irvtd_close() and friends
 *
 * most of this function is stolen from serial.c
 * ----------------------------------------------------------------------
 */

/*
 * Function irvtd_wait_until_sent (tty, timeout)
 *
 *    wait until Tx queue of IrTTP is empty 
 *
 */
static void irvtd_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	unsigned long orig_jiffies;

	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);
	ASSERT(driver->comm != NULL, return;);

	DEBUG(0, __FUNCTION__"():\n");	
	if(!tty->closing)
		return;   /* nothing to do */
	
	/* 
	 * at disconnection, we should wait until Tx queue of IrTTP is
	 * flushed
	 */

 	ircomm_disconnect_request(driver->comm, NULL, P_NORMAL);
	orig_jiffies = jiffies;

	while (driver->comm->tsap->disconnect_pend) {
		DEBUG(0, __FUNCTION__"():wait..\n");
		current->state = TASK_INTERRUPTIBLE;
		current->counter = 0;	/* make us low-priority */
		schedule_timeout(HZ);    /* 1sec */
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
	}
	current->state = TASK_RUNNING;
}


static void irvtd_shutdown(struct irvtd_cb * driver)
{
	unsigned long	flags;

	if (!(driver->flags & ASYNC_INITIALIZED))
		return;

	DEBUG(0,__FUNCTION__"()\n");

	/*
	 * This comment is written in serial.c:
	 *
	 * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
	 * here so the queue might never be waken up
	 */
 	wake_up_interruptible(&driver->delta_msr_wait);
	
	/* clear DTR and RTS */
	if (!driver->tty || (driver->tty->termios->c_cflag & HUPCL))
 		driver->mcr &= ~(MCR_DTR|MCR_RTS);

	driver->comm->dte = driver->mcr;
	ircomm_control_request(driver->comm, DTELINE_STATE );



	save_flags(flags); cli(); /* Disable interrupts */

	if (driver->tty)
		set_bit(TTY_IO_ERROR, &driver->tty->flags);
	
	del_timer( &driver->timer);

	irias_delete_object("IrDA:IrCOMM");

	/*
	 * Free the transmit buffer here 
	 */

	while(skb_queue_len(&driver->rxbuff)){
		struct sk_buff *skb;
		skb = skb_dequeue( &driver->rxbuff);
		dev_kfree_skb(skb);
	}
	if(driver->txbuff){
		dev_kfree_skb(driver->txbuff);
		driver->txbuff = NULL;
	}
	ircomm_close_instance(driver->comm);
	driver->comm = NULL;
	driver->flags &= ~ASYNC_INITIALIZED;
	restore_flags(flags);
}

void irvtd_close(struct tty_struct * tty, struct file * filp)
{
	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	int line;
	unsigned long flags;

	DEBUG(0, __FUNCTION__"():refcount= %d\n",irvtd_refcount);

	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	save_flags(flags);cli();


	if(tty_hung_up_p(filp)){
		/*
		 * upper tty layer caught a HUP signal and called irvtd_hangup()
		 * before. so we do nothing here.
		 */
		DEBUG(0, __FUNCTION__"():tty_hung_up_p.\n");
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return;
	}
	

	line = MINOR(tty->device) - tty->driver.minor_start;
	DEBUG(0, __FUNCTION__"():%s%d count %d\n", tty->driver.name, line, 
	      driver->count);

	if ((tty->count == 1) && (driver->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Driver->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * ircomm service layer won't be shutdown.
		 */
		printk(KERN_ERR"irvtd_close: bad serial port count;" 
		       "tty->count is 1, but driver->count is %d\n", driver->count);
		driver->count = 1;
	}
	if (--driver->count < 0) {
		printk(KERN_ERR"irvtd_close: bad count for line%d: %d\n",
		       line, driver->count);
		driver->count = 0;
	}

	if (driver->count) { 	/* do nothing */
		DEBUG(0, __FUNCTION__"():driver->count is not 0\n");
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return; 
	}

	driver->flags |= ASYNC_CLOSING;
	
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (driver->closing_wait != ASYNC_CLOSING_WAIT_NONE){
		DEBUG(4, __FUNCTION__"():calling tty_wait_until_sent()\n");
		tty_wait_until_sent(tty, driver->closing_wait);
	}
	/* 
	 * we can send disconnect_request with P_HIGH since
	 * tty_wait_until_sent() and irvtd_wait_until_sent() should
	 * have disconnected the link
	 */
	ircomm_disconnect_request(driver->comm, NULL, P_HIGH);
	
	/* 
	 * Now we stop accepting input.
	 */

	driver->rx_disable = TRUE;
	/* drop ldisc's buffer */
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);

 	if (tty->driver.flush_buffer) 
 		tty->driver.flush_buffer(driver->tty);  

	tty->closing = 0;
	driver->tty = NULL;

	if (driver->blocked_open)
	{
		if (driver->close_delay) {
			/* kill time */
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(driver->close_delay);
		}
		wake_up_interruptible(&driver->open_wait);
	}
	irvtd_shutdown(driver);
	driver->flags &= ~ASYNC_CLOSING;
        wake_up_interruptible(&driver->close_wait); 

	MOD_DEC_USE_COUNT;
        restore_flags(flags);
	DEBUG(4, __FUNCTION__"():done\n");
}

/*
 * ----------------------------------------------------------------------
 * irvtd_write() and friends
 * This routine will be called when something data are passed from
 * kernel or user.
 * ----------------------------------------------------------------------
 */

int irvtd_write(struct tty_struct * tty, int from_user,
		const unsigned char *buf, int count)
{
	struct irvtd_cb *driver;
	int c = 0;
	int wrote = 0;
 	unsigned long flags;
	struct sk_buff *skb;
	__u8 *frame;

	ASSERT(tty != NULL, return -EFAULT;);
	driver = (struct irvtd_cb *)tty->driver_data;
	ASSERT(driver != NULL, return -EFAULT;);
	ASSERT(driver->magic == IRVTD_MAGIC, return -EFAULT;);

	DEBUG(4, __FUNCTION__"()\n");


	save_flags(flags);
	while(1){
		cli();
		skb = driver->txbuff;
		ASSERT(skb != NULL, break;);
		c = MIN(count, (skb_tailroom(skb)));
		if (c <= 0)
			break;

		/* write to the frame */

		frame = skb_put(skb,c);
		if(from_user){
			copy_from_user(frame,buf,c);
		} else
			memcpy(frame, buf, c);

		restore_flags(flags);
		wrote += c;
		count -= c;
		buf += c;
	}
	restore_flags(flags);
	return (wrote);
}

/*
 * Function irvtd_put_char (tty, ch)
 *
 *    This routine is called by the kernel to pass a single character.
 *    If we exausted our buffer,we can ignore the character!
 *
 */
void irvtd_put_char(struct tty_struct *tty, unsigned char ch)
{
	__u8 *frame ;
	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	struct sk_buff *skb;
	unsigned long flags;

	ASSERT(driver != NULL, return;);
	DEBUG(4, __FUNCTION__"()\n");


	save_flags(flags);cli();
	skb = driver->txbuff;
	ASSERT(skb != NULL,return;);
	ASSERT(skb_tailroom(skb) > 0, return;);
	DEBUG(4, "irvtd_put_char(0x%02x) skb_len(%d) MAX(%d):\n",
	      (int)ch ,(int)skb->len,
	      driver->comm->max_txbuff_size - COMM_HEADER_SIZE);

	/* append a character  */
	frame = skb_put(skb,1);
	frame[0] = ch;

	restore_flags(flags);
	return;
}

/*
 * Function irvtd_write_room (tty)
 *
 *    This routine returns the room that our buffer has now.
 *
 */
int irvtd_write_room(struct tty_struct *tty){

	int ret;
	struct sk_buff *skb = ((struct irvtd_cb *) tty->driver_data)->txbuff;

	ASSERT(skb !=NULL, return 0;);

	ret = skb_tailroom(skb);

	DEBUG(4, __FUNCTION__"(): room is %d bytes\n",ret);

	return(ret);
}

/*
 * Function irvtd_chars_in_buffer (tty)
 *
 *    This function returns how many characters which have not been sent yet 
 *    are still in buffer.
 *
 */
int irvtd_chars_in_buffer(struct tty_struct *tty){

	struct sk_buff *skb;
	unsigned long flags;

	DEBUG(4, __FUNCTION__"()\n");

	save_flags(flags);cli();
	skb = ((struct irvtd_cb *) tty->driver_data)->txbuff;
	if(skb == NULL) goto err;

	restore_flags(flags);
	return (skb->len );
err:	
	ASSERT(skb != NULL, ;);
	restore_flags(flags);
	return 0;   /* why not -EFAULT or such? see driver/char/serial.c */
}

/*
 * Function irvtd_break (tty, break_state)
 *
 *    Routine which turns the break handling on or off
 *
 */
static void irvtd_break(struct tty_struct *tty, int break_state){
	
 	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	unsigned long flags;

	DEBUG(0, __FUNCTION__"()\n");
	ASSERT(tty->driver_data != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);
	
	save_flags(flags);cli();
	if (break_state == -1)
	{
		driver->comm->break_signal = 0x01;
		ircomm_control_request(driver->comm, BREAK_SIGNAL);

	}
	else
	{
		driver->comm->break_signal = 0x00;
		ircomm_control_request(driver->comm, BREAK_SIGNAL);

	}

	restore_flags(flags);
}

/*
 * ----------------------------------------------------------------------
 * irvtd_ioctl() and friends
 * This routine allows us to implement device-specific ioctl's.
 * If passed ioctl number (i.e.cmd) is unknown one, we should return 
 * ENOIOCTLCMD.
 *
 * TODO: we can't use setserial on IrCOMM because some ioctls are not implemented.
 * we should add some ioctls and make some tool which is resemble to setserial.
 * ----------------------------------------------------------------------
 */

static int get_modem_info(struct irvtd_cb * driver, unsigned int *value)
{
	unsigned int result;
	result =  ((driver->mcr & MCR_RTS) ? TIOCM_RTS : 0)
		| ((driver->mcr & MCR_DTR) ? TIOCM_DTR : 0)
		| ((driver->msr & DELTA_DCD) ? TIOCM_CAR : 0)
		| ((driver->msr & DELTA_RI) ? TIOCM_RNG : 0)
		| ((driver->msr & DELTA_DSR) ? TIOCM_DSR : 0)
		| ((driver->msr & DELTA_CTS) ? TIOCM_CTS : 0);
	return put_user(result,value);
}

static int set_modem_info(struct irvtd_cb * driver, unsigned int cmd,
			  unsigned int *value)
{ 
	int error;
	unsigned int arg;

	error = get_user(arg, value);
	if(error)
		return error;

	switch (cmd) {
	case TIOCMBIS: 
		if (arg & TIOCM_RTS) 
			driver->mcr |= MCR_RTS;
		if (arg & TIOCM_DTR)
			driver->mcr |= MCR_DTR;
		break;
		
	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			driver->mcr &= ~MCR_RTS;
		if (arg & TIOCM_DTR)
 			driver->mcr &= ~MCR_DTR;
 		break;
		
	case TIOCMSET:
 		driver->mcr = ((driver->mcr & ~(MCR_RTS | MCR_DTR))
			       | ((arg & TIOCM_RTS) ? MCR_RTS : 0)
			       | ((arg & TIOCM_DTR) ? MCR_DTR : 0));
		break;
		
	default:
		return -EINVAL;
	}
	
	driver->comm->dte = driver->mcr;
	ircomm_control_request(driver->comm, DTELINE_STATE );

	return 0;
}

static int get_serial_info(struct irvtd_cb * driver,
			   struct serial_struct * retinfo)
{
	struct serial_struct tmp;
   
	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.line = driver->line;
	tmp.flags = driver->flags;
	tmp.baud_base = driver->comm->data_rate;
	tmp.close_delay = driver->close_delay;
	tmp.closing_wait = driver->closing_wait;

	/* for compatibility  */

 	tmp.type = PORT_16550A;
 	tmp.port = 0;
 	tmp.irq = 0;
	tmp.xmit_fifo_size = 0;
	tmp.hub6 = 0;   
	tmp.custom_divisor = driver->custom_divisor;

	if (copy_to_user(retinfo,&tmp,sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int set_serial_info(struct irvtd_cb * driver,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct irvtd_cb old_driver;

	if (copy_from_user(&new_serial,new_info,sizeof(new_serial)))
		return -EFAULT;

	old_driver = *driver;
  
	if (!capable(CAP_SYS_ADMIN)) {
		if ((new_serial.baud_base != driver->comm->data_rate) ||
		    (new_serial.close_delay != driver->close_delay) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (driver->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		driver->flags = ((driver->flags & ~ASYNC_USR_MASK) |
				 (new_serial.flags & ASYNC_USR_MASK));
		driver->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	if(driver->comm->data_rate != new_serial.baud_base){
		driver->comm->data_rate = new_serial.baud_base;
		if(driver->comm->state == COMM_CONN)
			ircomm_control_request(driver->comm,DATA_RATE);
	}
	driver->close_delay = new_serial.close_delay * HZ/100;
	driver->closing_wait = new_serial.closing_wait * HZ/100;
	driver->custom_divisor = new_serial.custom_divisor;

	driver->flags = ((driver->flags & ~ASYNC_FLAGS) |
			 (new_serial.flags & ASYNC_FLAGS));
	driver->tty->low_latency = (driver->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

 check_and_exit:
	if (driver->flags & ASYNC_INITIALIZED) {
		if (((old_driver.flags & ASYNC_SPD_MASK) !=
		     (driver->flags & ASYNC_SPD_MASK)) ||
		    (old_driver.custom_divisor != driver->custom_divisor)) {
			if ((driver->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
				driver->tty->alt_speed = 57600;
			if ((driver->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
				driver->tty->alt_speed = 115200;
			if ((driver->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
				driver->tty->alt_speed = 230400;
			if ((driver->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
				driver->tty->alt_speed = 460800;
			change_speed(driver);
		}
	}
	return 0;
}




int irvtd_ioctl(struct tty_struct *tty, struct file * file,
		unsigned int cmd, unsigned long arg)
{
	int error;
	unsigned long flags;
 	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;

	struct serial_icounter_struct cnow,cprev;
	struct serial_icounter_struct *p_cuser;	/* user space */


	DEBUG(4,"irvtd_ioctl:requested ioctl(0x%08x)\n",cmd);

#ifdef IRVTD_DEBUG_IOCTL
	{
		/* kill time so that debug messages will come slowly  */
		save_flags(flags);cli();
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + HZ/4; /*0.25sec*/
		schedule();
		restore_flags(flags);
	}
#endif



	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR)){
			return -EIO;
		}
	}
	
	switch (cmd) {

	case TIOCMGET:
		return get_modem_info(driver, (unsigned int *) arg);

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		return set_modem_info(driver, cmd, (unsigned int *) arg);
	case TIOCGSERIAL:
		return get_serial_info(driver, (struct serial_struct *) arg);
	case TIOCSSERIAL:
		return set_serial_info(driver, (struct serial_struct *) arg);


	case TIOCMIWAIT:
		save_flags(flags); cli();
		/* note the counters on entry */
		cprev = driver->icount;
		restore_flags(flags);
		while (1) {
			interruptible_sleep_on(&driver->delta_msr_wait);
				/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			save_flags(flags); cli();
			cnow = driver->icount; /* atomic copy */
			restore_flags(flags);
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr && 
			    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				return -EIO; /* no change => error */
			if ( ((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			     ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			     ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
			     ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts)) ) {
				return 0;
			}
			cprev = cnow;
		}
		/* NOTREACHED */

	case TIOCGICOUNT:
		save_flags(flags); cli();
		cnow = driver->icount;
		restore_flags(flags);
		p_cuser = (struct serial_icounter_struct *) arg;
		error = put_user(cnow.cts, &p_cuser->cts);
		if (error) return error;
		error = put_user(cnow.dsr, &p_cuser->dsr);
		if (error) return error;
		error = put_user(cnow.rng, &p_cuser->rng);
		if (error) return error;
		error = put_user(cnow.dcd, &p_cuser->dcd);
		if (error) return error;
		error = put_user(cnow.rx, &p_cuser->rx);
		if (error) return error;
		error = put_user(cnow.tx, &p_cuser->tx);
		if (error) return error;
		error = put_user(cnow.frame, &p_cuser->frame);
		if (error) return error;
		error = put_user(cnow.overrun, &p_cuser->overrun);
		if (error) return error;
		error = put_user(cnow.parity, &p_cuser->parity);
		if (error) return error;
		error = put_user(cnow.brk, &p_cuser->brk);
		if (error) return error;
		error = put_user(cnow.buf_overrun, &p_cuser->buf_overrun);
		if (error) return error;			
		return 0;
		
		
		/* ioctls which are imcompatible with serial.c */

	case TIOCSERGSTRUCT:
		DEBUG(0,__FUNCTION__"():sorry, TIOCSERGSTRUCT is not supported\n");
		return -ENOIOCTLCMD;  
	case TIOCSERGETLSR:
		DEBUG(0,__FUNCTION__"():sorry, TIOCSERGETLSR is not supported\n");
		return -ENOIOCTLCMD;  
 	case TIOCSERCONFIG:
		DEBUG(0,__FUNCTION__"():sorry, TIOCSERCONFIG is not supported\n");
		return -ENOIOCTLCMD;  


	default:
		return -ENOIOCTLCMD;  /* ioctls which we must ignore */
	}
	return 0;
}



/*
 * ----------------------------------------------------------------------
 * irvtd_set_termios()
 * This is called when termios is changed.
 * If things that changed is significant for us,(i.e. changing baud rate etc.)
 * send something to peer device.
 * ----------------------------------------------------------------------
 */

void irvtd_set_termios(struct tty_struct *tty, struct termios * old_termios){

	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;

	ASSERT(driver != NULL,return;);
	ASSERT(driver->magic == IRVTD_MAGIC ,return;);

	DEBUG(0, "irvtd_set_termios:\n");
	return;

	if((tty->termios->c_cflag == old_termios->c_cflag) &&
	   (RELEVANT_IFLAG(tty->termios->c_iflag) 
	    == RELEVANT_IFLAG(old_termios->c_iflag)))
		return;

	change_speed(driver);

	if((old_termios->c_cflag & CRTSCTS) &&
	   !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = driver->ttp_stoptx;
		/* irvtd_start(tty); */       /* we don't need this */
	}
}

/*
 * ----------------------------------------------------------------------
 * irvtd_throttle,irvtd_unthrottle
 *   These routines will be called when we have to pause sending up data to tty.
 *   We use RTS virtual signal when servicetype is NINE_WIRE
 * ----------------------------------------------------------------------
 */

static void irvtd_send_xchar(struct tty_struct *tty, char ch){

	DEBUG(0, __FUNCTION__"():\n");
	irvtd_put_char(tty, ch);
}

void irvtd_throttle(struct tty_struct *tty){
	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;

	DEBUG(0, "irvtd_throttle:\n");

	if (I_IXOFF(tty))
		irvtd_put_char(tty, STOP_CHAR(tty));

	driver->mcr &= ~MCR_RTS; 
	driver->mcr |= DELTA_RTS; 
	driver->comm->dte = driver->mcr;
	ircomm_control_request(driver->comm, DTELINE_STATE );

        irttp_flow_request(driver->comm->tsap, FLOW_STOP);
}

void irvtd_unthrottle(struct tty_struct *tty){
	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	DEBUG(0, "irvtd_unthrottle:\n");

	if (I_IXOFF(tty))
		irvtd_put_char(tty, START_CHAR(tty));

	driver->mcr |= (MCR_RTS|DELTA_RTS);
	driver->comm->dte = driver->mcr;
	ircomm_control_request(driver->comm, DTELINE_STATE );

        irttp_flow_request(driver->comm->tsap, FLOW_START);
}


/*
 * ------------------------------------------------------------
 * irvtd_stop() and irvtd_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable an interrupt which means "transmitter-is-ready"
 * in serial.c, but  I think these routine are not necessary for us. 
 * ------------------------------------------------------------
 */

#if 0
irvtd_stop(struct tty_struct *tty){
 	DEBUG(0, "irvtd_stop()\n");

	struct irvtd_cb *info = (struct irvtd_cb *)tty->driver_data; 
	DEBUG(0, "irvtd_start():not implemented!\n");
}
irvtd_start(struct tty_struct *tty){

	struct irvtd_cb *info = (struct irvtd_cb *)tty->driver_data;
	DEBUG(0, "irvtd_start():not_implemented!\n");
}
#endif

/*
 * ------------------------------------------------------------
 * irvtd_hangup()
 * This routine notifies that tty layer have got HUP signal
 * ------------------------------------------------------------
 */

void irvtd_hangup(struct tty_struct *tty){

	struct irvtd_cb *info = (struct irvtd_cb *)tty->driver_data;
	DEBUG(0, __FUNCTION__"()\n");

	irvtd_flush_buffer(tty);
	irvtd_shutdown(info);
	info->count = 0;
	info->tty = NULL;
	wake_up_interruptible(&info->open_wait);
}

void irvtd_flush_buffer(struct tty_struct *tty){

	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	struct sk_buff *skb;

	skb = driver->txbuff;
	ASSERT(skb != NULL, return;); 

	if(skb->len){
		DEBUG(0, __FUNCTION__"():%d chars in txbuff are lost..\n",(int)skb->len);
		skb_trim(skb,0); 
	}
	
	/* write_wait is a wait queue of tty_wait_until_sent().
	 * see tty_io.c of kernel 
	 */
	wake_up_interruptible(&tty->write_wait); 

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}



/*
 * Function ircomm_register_device(void), init_module() and friends
 *
 *   we register "port emulation entity"(see IrCOMM specification) here
 *   as a tty device.
 */

int irvtd_register_ttydriver(void){

        DEBUG( 4, "-->irvtd_register_ttydriver\n");

	/* setup virtual serial port device */

        /* Initialize the tty_driver structure ,which is defined in 
	   tty_driver.h */
        
        memset(&irvtd_drv, 0, sizeof(struct tty_driver));
	irvtd_drv.magic = IRVTD_MAGIC;
	irvtd_drv.driver_name = "IrCOMM_tty";
	irvtd_drv.name = "irnine";
	irvtd_drv.major = IRCOMM_MAJOR;
	irvtd_drv.minor_start = IRVTD_MINOR;
	irvtd_drv.num = COMM_MAX_TTY;
	irvtd_drv.type = TTY_DRIVER_TYPE_SERIAL;  /* see tty_driver.h */


	/*
	 * see drivers/char/tty_io.c and termios(3)
	 */

        irvtd_drv.init_termios = tty_std_termios;
        irvtd_drv.init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
        irvtd_drv.flags = TTY_DRIVER_REAL_RAW;   /* see tty_driver.h */
        irvtd_drv.refcount = &irvtd_refcount;

	/* pointer to the tty data structures */

        irvtd_drv.table = irvtd_table;  
        irvtd_drv.termios = irvtd_termios;
        irvtd_drv.termios_locked = irvtd_termios_locked;

        /*
         * Interface table from the kernel(tty driver) to the ircomm
         * layer
         */

        irvtd_drv.open = irvtd_open;
        irvtd_drv.close = irvtd_close;
        irvtd_drv.write = irvtd_write;
        irvtd_drv.put_char = irvtd_put_char;
	irvtd_drv.flush_chars = NULL;
        irvtd_drv.write_room = irvtd_write_room;
	irvtd_drv.chars_in_buffer = irvtd_chars_in_buffer; 
        irvtd_drv.flush_buffer = irvtd_flush_buffer;
 	irvtd_drv.ioctl = irvtd_ioctl; 
	irvtd_drv.throttle = irvtd_throttle;
	irvtd_drv.unthrottle = irvtd_unthrottle;
 	irvtd_drv.set_termios = irvtd_set_termios;
	irvtd_drv.stop = NULL;          /*  irvtd_stop; */
	irvtd_drv.start = NULL;         /* irvtd_start; */
        irvtd_drv.hangup = irvtd_hangup;

        irvtd_drv.send_xchar = irvtd_send_xchar;
	irvtd_drv.break_ctl = irvtd_break;
	irvtd_drv.read_proc = irvtd_read_proc;
 	irvtd_drv.wait_until_sent = irvtd_wait_until_sent;



	if (tty_register_driver(&irvtd_drv)){
		DEBUG(0,"IrCOMM:Couldn't register tty driver\n");
		return(1);
	}

	DEBUG( 4, "irvtd_register_ttydriver: done.\n");
	return(0);
}


/*
 * Function irvtd_unregister_device(void) 
 *   it will be called when you rmmod
 */

void irvtd_unregister_ttydriver(void){

	int err;	
        DEBUG( 4, "--> irvtd_unregister_device\n");

	/* unregister tty device   */

	err = tty_unregister_driver(&irvtd_drv);
        if (err)
                printk("IrCOMM: failed to unregister vtd driver(%d)\n",err);
        DEBUG( 4, "irvtd_unregister_device -->\n");
	return;
}

/*
 **********************************************************************
 *    proc stuff
 *
 **********************************************************************
 */

static int line_info(char *buf, struct irvtd_cb *driver)
{
	int	ret=0;

	ASSERT(driver != NULL,goto exit;);
	ASSERT(driver->magic == IRVTD_MAGIC,goto exit;);

	ret += sprintf(buf, "tx: %d rx: %d"
		      ,driver->icount.tx, driver->icount.rx);

	if (driver->icount.frame)
                ret += sprintf(buf+ret, " fe:%d", driver->icount.frame);
        if (driver->icount.parity)
                ret += sprintf(buf+ret, " pe:%d", driver->icount.parity);
        if (driver->icount.brk)
                ret += sprintf(buf+ret, " brk:%d", driver->icount.brk);  
        if (driver->icount.overrun)
                ret += sprintf(buf+ret, " oe:%d", driver->icount.overrun);

	if (driver->mcr & MCR_RTS)
                ret += sprintf(buf+ret, "|RTS");
        if (driver->msr & MSR_CTS)
                ret += sprintf(buf+ret, "|CTS");
        if (driver->mcr & MCR_DTR)
                ret += sprintf(buf+ret, "|DTR");
        if (driver->msr & MSR_DSR)
                ret += sprintf(buf+ret, "|DSR");
        if (driver->msr & MSR_DCD)
                ret += sprintf(buf+ret, "|CD");
	if (driver->msr & MSR_RI) 
		ret += sprintf(buf+ret, "|RI");

 exit:
	ret += sprintf(buf+ret, "\n");
	return ret;
}



static int irvtd_read_proc(char *buf, char **start, off_t offset, int len,
		 int *eof, void *unused)
{
	int i, count = 0, l;
	off_t	begin = 0;

	count += sprintf(buf, "driver revision:%s\n", revision_date);
	for (i = 0; i < COMM_MAX_TTY && count < 4000; i++) {
		l = line_info(buf + count, irvtd[i]);
		count += l;
		if (count+begin > offset+len)
			goto done;
		if (count+begin < offset) {
			begin += count;
			count = 0;
		}
	}
	*eof = 1;
done:
	if (offset >= count+begin)
		return 0;
	*start = buf + (begin-offset);
	return ((len < begin+count-offset) ? len : begin+count-offset);
}




/************************************************************
 *    init & cleanup this module 
 ************************************************************/

__initfunc(int irvtd_init(void))
{
	int i;

	DEBUG( 4, __FUNCTION__"()\n");
	printk( KERN_INFO 
		"ircomm_tty: virtual tty driver for IrCOMM ( revision:%s )\n",
		revision_date);


	/* allocate a master array */

	irvtd = (struct irvtd_cb **) kmalloc( sizeof(void *) *
						COMM_MAX_TTY,GFP_KERNEL);
	if ( irvtd == NULL) {
		printk( KERN_WARNING __FUNCTION__"(): kmalloc failed!\n");
		return -ENOMEM;
	}

	memset( irvtd, 0, sizeof(void *) * COMM_MAX_TTY);

	for (i=0; i < COMM_MAX_TTY; i++){
		irvtd[i] = kmalloc( sizeof(struct irvtd_cb), GFP_KERNEL);	
		if(irvtd[i] == NULL){
			printk(KERN_ERR __FUNCTION__"(): kmalloc failed!\n");
			return -ENOMEM;
		}
		memset( irvtd[i], 0, sizeof(struct irvtd_cb));
		irvtd[i]->magic = IRVTD_MAGIC;
		irvtd[i]->line = i;
		irvtd[i]->closing_wait = 30*HZ ;
		irvtd[i]->close_delay = 5*HZ/10 ; 
	}

	/* 
	 * initialize a "port emulation entity"
	 */

	if(irvtd_register_ttydriver()){
		printk( KERN_WARNING "IrCOMM: Error in ircomm_register_device\n");
		return -ENODEV;
	}

	return 0;
}

void irvtd_cleanup(void)
{
	int i;
	DEBUG( 4, __FUNCTION__"()\n");

	/*
	 * free some resources
	 */
	if (irvtd) {
		for (i=0; i<COMM_MAX_TTY; i++) {
			if (irvtd[i]) {
				if(irvtd[i]->comm)
					ircomm_close_instance(irvtd[i]->comm);
				if(irvtd[i]->txbuff)
					dev_kfree_skb(irvtd[i]->txbuff);
				DEBUG( 4, "freeing structures\n");
				kfree(irvtd[i]);
				irvtd[i] = NULL;
			}
		}
		DEBUG( 4, "freeing master array\n");
		kfree(irvtd);
		irvtd = NULL;
	}

 	irvtd_unregister_ttydriver();

}

#ifdef MODULE

int init_module(void) 
{
	irvtd_init();
	return 0;
}

/*
 * Function ircomm_cleanup (void)
 *   This is called when you rmmod.
 */

void cleanup_module(void)
{
	irvtd_cleanup();
}

#endif /* MODULE */
